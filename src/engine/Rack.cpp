#include "Rack.h"

namespace bridgerack
{

namespace
{
    std::vector<int> sequentialMap (int size, int first = 0)
    {
        std::vector<int> map ((size_t) size);

        for (int i = 0; i < size; ++i)
            map[(size_t) i] = first + i;

        return map;
    }

    juce::AudioChannelSet channelSetFor (int numChannels, bool discrete)
    {
        if (! discrete)
        {
            auto set = juce::AudioChannelSet::canonicalChannelSet (numChannels);

            if (! set.isDisabled())
                return set;
        }

        return juce::AudioChannelSet::discreteChannels (numChannels);
    }

    void updatePeak (std::atomic<float>& peak, float value) noexcept
    {
        auto current = peak.load (std::memory_order_relaxed);

        while (value > current && ! peak.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {}
    }

    juce::String mapToString (const std::vector<int>& map)
    {
        juce::StringArray items;

        for (auto v : map)
            items.add (juce::String (v));

        return items.joinIntoString (",");
    }
}

Rack::Rack (juce::String n, int channels, int numSlots)
    : name (std::move (n)),
      numChannels (juce::jlimit (1, kMaxChannelsPerRack, channels))
{
    slots.resize ((size_t) juce::jlimit (kMinSlots, kMaxSlots, numSlots));
    inputMap  = sequentialMap (numChannels);
    outputMap = sequentialMap (numChannels);
}

Rack::~Rack()
{
    release();
}

void Rack::prepare (double newSampleRate, int maximumBlockSize)
{
    const juce::ScopedLock sl (lock);

    sampleRate = newSampleRate;
    blockSize = juce::jmax (1, maximumBlockSize);

    work.setSize (kMaxRackChannels, blockSize);
    pluginBuffer.setSize (kMaxRackChannels, blockSize);
    delayBuffer.setSize (numChannels, kMaxCompensation + blockSize);
    delayBuffer.clear();
    delayWritePos = 0;
    activeDelay = 0;
    midi.ensureSize (2048);

    for (auto& slot : slots)
        if (slot.processor != nullptr)
            configureProcessor (*slot.processor);

    prepared = true;
}

void Rack::release()
{
    const juce::ScopedLock sl (lock);

    for (auto& slot : slots)
        if (slot.processor != nullptr)
            slot.processor->releaseResources();

    prepared = false;
}

void Rack::configureProcessor (juce::AudioProcessor& p) const
{
    p.releaseResources();

    // Ask the plugin to run with exactly the rack's channel count on its main
    // buses, and disable any side-chain/aux buses. If it refuses, it keeps its
    // default layout and the rack passes the extra channels through unprocessed.
    for (const auto discrete : { false, true })
    {
        auto layout = p.getBusesLayout();
        const auto set = channelSetFor (numChannels, discrete);

        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = i == 0 ? set : juce::AudioChannelSet::disabled();

        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = i == 0 ? set : juce::AudioChannelSet::disabled();

        if (p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout))
            break;
    }

    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.setNonRealtime (false);
    p.prepareToPlay (sampleRate, blockSize);
}

//==============================================================================
void Rack::setNumChannels (int newNumChannels)
{
    newNumChannels = juce::jlimit (1, kMaxChannelsPerRack, newNumChannels);

    const juce::ScopedLock sl (lock);

    if (newNumChannels == numChannels)
        return;

    // Extend the routing sequentially from the last mapped channel.
    const auto extend = [&] (std::vector<int>& map)
    {
        const auto last = map.empty() ? -1 : map.back();
        const auto oldSize = (int) map.size();
        map.resize ((size_t) newNumChannels, -1);

        for (int i = oldSize; i < newNumChannels; ++i)
            map[(size_t) i] = last < 0 ? -1 : last + (i - oldSize) + 1;
    };

    extend (inputMap);
    extend (outputMap);
    numChannels = newNumChannels;

    if (prepared)
    {
        delayBuffer.setSize (numChannels, kMaxCompensation + blockSize);
        delayBuffer.clear();

        for (auto& slot : slots)
            if (slot.processor != nullptr)
                configureProcessor (*slot.processor);
    }
}

void Rack::setNumSlots (int newNumSlots)
{
    newNumSlots = juce::jlimit (kMinSlots, kMaxSlots, newNumSlots);

    const juce::ScopedLock sl (lock);

    // Never drop a loaded plugin when shrinking: only trailing empty slots go.
    while ((int) slots.size() > newNumSlots && slots.back().processor == nullptr)
        slots.pop_back();

    slots.resize ((size_t) juce::jmax (newNumSlots, (int) slots.size()));
}

std::unique_ptr<juce::AudioProcessor> Rack::setProcessor (int slot,
                                                          std::unique_ptr<juce::AudioProcessor> processor,
                                                          const juce::PluginDescription& description)
{
    if (! juce::isPositiveAndBelow (slot, getNumSlots()))
        return processor;

    // Preparing a plugin can be slow, so do it before taking the lock.
    if (processor != nullptr && prepared)
        configureProcessor (*processor);

    const juce::ScopedLock sl (lock);
    auto& s = slots[(size_t) slot];
    std::swap (s.processor, processor);
    s.description = description;
    s.bypassed = false;
    return processor;
}

std::unique_ptr<juce::AudioProcessor> Rack::removeProcessor (int slot)
{
    return setProcessor (slot, nullptr);
}

void Rack::moveSlot (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, getNumSlots()) || ! juce::isPositiveAndBelow (to, getNumSlots()) || from == to)
        return;

    const juce::ScopedLock sl (lock);
    auto moving = std::move (slots[(size_t) from]);
    slots.erase (slots.begin() + from);
    slots.insert (slots.begin() + to, std::move (moving));
}

juce::AudioProcessor* Rack::getProcessor (int slot) const
{
    return juce::isPositiveAndBelow (slot, getNumSlots()) ? slots[(size_t) slot].processor.get() : nullptr;
}

juce::PluginDescription Rack::getDescription (int slot) const
{
    return juce::isPositiveAndBelow (slot, getNumSlots()) ? slots[(size_t) slot].description : juce::PluginDescription();
}

void Rack::setProcessorState (int slot, const juce::MemoryBlock& state)
{
    if (auto* p = getProcessor (slot))
    {
        const juce::ScopedLock sl (lock);
        p->setStateInformation (state.getData(), (int) state.getSize());
    }
}

bool Rack::isSlotBypassed (int slot) const
{
    return juce::isPositiveAndBelow (slot, getNumSlots()) && slots[(size_t) slot].bypassed;
}

void Rack::setSlotBypassed (int slot, bool b)
{
    if (! juce::isPositiveAndBelow (slot, getNumSlots()))
        return;

    const juce::ScopedLock sl (lock);
    slots[(size_t) slot].bypassed = b;
}

//==============================================================================
std::vector<int> Rack::fitMap (std::vector<int> map, int size)
{
    map.resize ((size_t) size, -1);
    return map;
}

std::vector<int> Rack::getInputMap() const   { const juce::ScopedLock sl (lock); return inputMap; }
std::vector<int> Rack::getOutputMap() const  { const juce::ScopedLock sl (lock); return outputMap; }

void Rack::setInputMap (std::vector<int> map)
{
    map = fitMap (std::move (map), numChannels);
    const juce::ScopedLock sl (lock);
    inputMap = std::move (map);
}

void Rack::setOutputMap (std::vector<int> map)
{
    map = fitMap (std::move (map), numChannels);
    const juce::ScopedLock sl (lock);
    outputMap = std::move (map);
}

void Rack::setInputGainDb (float db)
{
    inputGainDb = db;
    inputGain = juce::Decibels::decibelsToGain (db, -96.0f);
}

void Rack::setOutputGainDb (float db)
{
    outputGainDb = db;
    outputGain = juce::Decibels::decibelsToGain (db, -96.0f);
}

int Rack::getLatencySamples() const
{
    const juce::ScopedLock sl (lock);
    int total = 0;

    // Bypassed slots still go through processBlockBypassed, which keeps the
    // plugin's latency, so they count too.
    for (auto& slot : slots)
        if (slot.processor != nullptr)
            total += slot.processor->getLatencySamples();

    return total;
}

void Rack::setCompensationDelay (int samples) noexcept
{
    compensationDelay = juce::jlimit (0, kMaxCompensation, samples);
}

float Rack::takeInputPeak (int channel) noexcept
{
    return juce::isPositiveAndBelow (channel, kMaxRackChannels) ? inputPeaks[(size_t) channel].exchange (0.0f) : 0.0f;
}

float Rack::takeOutputPeak (int channel) noexcept
{
    return juce::isPositiveAndBelow (channel, kMaxRackChannels) ? outputPeaks[(size_t) channel].exchange (0.0f) : 0.0f;
}

//==============================================================================
void Rack::process (const float* const* inputs, int numInputs,
                    float* const* outputs, int numOutputs,
                    int numSamples) noexcept
{
    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked() || ! prepared || numSamples > blockSize)
        return;

    work.setSize (numChannels, numSamples, false, false, true);

    const auto inGain = inputGain.load();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto src = inputMap[(size_t) ch];

        if (juce::isPositiveAndBelow (src, numInputs) && inputs[src] != nullptr)
            juce::FloatVectorOperations::multiply (work.getWritePointer (ch), inputs[src], inGain, numSamples);
        else
            work.clear (ch, 0, numSamples);

        updatePeak (inputPeaks[(size_t) ch], work.getMagnitude (ch, 0, numSamples));
    }

    if (! bypassed)
        for (auto& slot : slots)
            if (slot.processor != nullptr && ! slot.processor->isSuspended())
                runSlot (slot, numSamples);

    applyCompensation (numSamples);

    const auto outGain = muted ? 0.0f : outputGain.load();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        work.applyGain (ch, 0, numSamples, outGain);
        updatePeak (outputPeaks[(size_t) ch], work.getMagnitude (ch, 0, numSamples));

        const auto dst = outputMap[(size_t) ch];

        if (juce::isPositiveAndBelow (dst, numOutputs) && outputs[dst] != nullptr)
            juce::FloatVectorOperations::add (outputs[dst], work.getReadPointer (ch), numSamples);
    }
}

void Rack::runSlot (Slot& slot, int numSamples) noexcept
{
    auto& p = *slot.processor;
    const auto pluginIns  = p.getTotalNumInputChannels();
    const auto pluginOuts = p.getTotalNumOutputChannels();
    const auto pluginChannels = juce::jmin (kMaxRackChannels, juce::jmax (pluginIns, pluginOuts));

    if (pluginChannels == 0)
        return;

    pluginBuffer.setSize (pluginChannels, numSamples, false, false, true);

    for (int ch = 0; ch < pluginChannels; ++ch)
    {
        if (ch < numChannels && ch < pluginIns)
            pluginBuffer.copyFrom (ch, 0, work, ch, 0, numSamples);
        else
            pluginBuffer.clear (ch, 0, numSamples);
    }

    midi.clear();

    if (slot.bypassed)
        p.processBlockBypassed (pluginBuffer, midi);
    else
        p.processBlock (pluginBuffer, midi);

    // Rack channels beyond what the plugin outputs pass through unchanged.
    for (int ch = 0; ch < juce::jmin (numChannels, pluginOuts); ++ch)
        work.copyFrom (ch, 0, pluginBuffer, ch, 0, numSamples);
}

void Rack::applyCompensation (int numSamples) noexcept
{
    const auto wanted = compensationDelay.load();

    if (wanted != activeDelay)
    {
        delayBuffer.clear();
        activeDelay = wanted;
    }

    if (activeDelay == 0)
        return;

    const auto size = delayBuffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* line = delayBuffer.getWritePointer (ch);
        auto* data = work.getWritePointer (ch);
        auto writePos = delayWritePos;

        for (int i = 0; i < numSamples; ++i)
        {
            auto readPos = writePos - activeDelay;

            if (readPos < 0)
                readPos += size;

            line[writePos] = data[i];
            data[i] = line[readPos];

            if (++writePos == size)
                writePos = 0;
        }
    }

    delayWritePos = (delayWritePos + numSamples) % size;
}

//==============================================================================
juce::ValueTree Rack::toValueTree() const
{
    const juce::ScopedLock sl (lock);

    juce::ValueTree tree ("RACK");
    tree.setProperty ("name", name, nullptr)
        .setProperty ("channels", numChannels, nullptr)
        .setProperty ("inputGainDb", inputGainDb, nullptr)
        .setProperty ("outputGainDb", outputGainDb, nullptr)
        .setProperty ("colour", (juce::int64) colour, nullptr)
        .setProperty ("muted", muted.load(), nullptr)
        .setProperty ("bypassed", bypassed.load(), nullptr)
        .setProperty ("inputMap", mapToString (inputMap), nullptr)
        .setProperty ("outputMap", mapToString (outputMap), nullptr);

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& slot = slots[i];
        juce::ValueTree slotTree ("SLOT");
        slotTree.setProperty ("index", (int) i, nullptr)
                .setProperty ("bypassed", slot.bypassed, nullptr);

        if (slot.processor != nullptr)
        {
            if (auto xml = slot.description.createXml())
                slotTree.appendChild (juce::ValueTree::fromXml (*xml), nullptr);

            juce::MemoryBlock state;
            slot.processor->getStateInformation (state);
            slotTree.setProperty ("state", state.toBase64Encoding(), nullptr);
        }

        tree.appendChild (slotTree, nullptr);
    }

    return tree;
}

} // namespace bridgerack
