#include "RackSet.h"

namespace bridgerack
{

void RackSet::prepare (double newSampleRate, int maximumBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = juce::jmax (1, maximumBlockSize);

    for (auto& rack : racks)
        rack->prepare (sampleRate, blockSize);

    inputChunk.reserve (1024);
    outputChunk.reserve (1024);
    prepared = true;
    updateLatencyCompensation();
}

void RackSet::release()
{
    for (auto& rack : racks)
        rack->release();

    prepared = false;
}

Rack& RackSet::addRack (std::unique_ptr<Rack> rack)
{
    if (prepared)
        rack->prepare (sampleRate, blockSize);

    auto& ref = *rack;

    // Grow outside the lock so the audio thread never waits on an allocation.
    std::vector<std::unique_ptr<Rack>> grown;
    grown.reserve (racks.size() + 1);

    {
        const juce::SpinLock::ScopedLockType sl (listLock);

        for (auto& r : racks)
            grown.push_back (std::move (r));

        grown.push_back (std::move (rack));
        std::swap (racks, grown);
    }

    updateLatencyCompensation();
    return ref;
}

std::unique_ptr<Rack> RackSet::removeRack (int index)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return {};

    std::unique_ptr<Rack> removed;

    {
        const juce::SpinLock::ScopedLockType sl (listLock);
        removed = std::move (racks[(size_t) index]);
        racks.erase (racks.begin() + index);
    }

    updateLatencyCompensation();
    return removed;
}

void RackSet::moveRack (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, size()) || ! juce::isPositiveAndBelow (to, size()) || from == to)
        return;

    const juce::SpinLock::ScopedLockType sl (listLock);
    auto moving = std::move (racks[(size_t) from]);
    racks.erase (racks.begin() + from);
    racks.insert (racks.begin() + to, std::move (moving));
}

std::vector<std::unique_ptr<Rack>> RackSet::removeAll()
{
    std::vector<std::unique_ptr<Rack>> removed;

    {
        const juce::SpinLock::ScopedLockType sl (listLock);
        std::swap (removed, racks);
    }

    return removed;
}

Rack* RackSet::getRack (int index) const
{
    return juce::isPositiveAndBelow (index, size()) ? racks[(size_t) index].get() : nullptr;
}

int RackSet::indexOf (const Rack* rack) const
{
    for (size_t i = 0; i < racks.size(); ++i)
        if (racks[i].get() == rack)
            return (int) i;

    return -1;
}

int RackSet::updateLatencyCompensation()
{
    int maxLatency = 0, maxAligned = 0;
    std::vector<int> latencies;

    for (auto& rack : racks)
    {
        latencies.push_back (rack->isBypassed() ? 0 : rack->getLatencySamples());
        maxLatency = juce::jmax (maxLatency, latencies.back());

        if (rack->isAligned())
            maxAligned = juce::jmax (maxAligned, latencies.back());
    }

    for (size_t i = 0; i < racks.size(); ++i)
        racks[i]->setCompensationDelay (racks[i]->isAligned() ? maxAligned - latencies[i] : 0);

    return maxLatency;
}

void RackSet::process (const float* const* inputs, int numInputs,
                       float* const* outputs, int numOutputs,
                       int numSamples) noexcept
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const juce::SpinLock::ScopedLockType sl (listLock);

    if (! prepared)
        return;

    inputChunk.resize ((size_t) numInputs);
    outputChunk.resize ((size_t) numOutputs);

    // Devices may deliver blocks larger than the size plugins were prepared for.
    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        const auto n = juce::jmin (blockSize, numSamples - offset);

        for (int ch = 0; ch < numInputs; ++ch)
            inputChunk[(size_t) ch] = inputs[ch] != nullptr ? inputs[ch] + offset : nullptr;

        for (int ch = 0; ch < numOutputs; ++ch)
            outputChunk[(size_t) ch] = outputs[ch] != nullptr ? outputs[ch] + offset : nullptr;

        for (auto& rack : racks)
            rack->process (inputChunk.data(), numInputs, outputChunk.data(), numOutputs, n);
    }
}

} // namespace bridgerack
