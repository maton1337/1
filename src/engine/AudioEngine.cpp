#include "AudioEngine.h"

namespace bridgerack
{

juce::ValueTree DeviceSelection::toValueTree() const
{
    juce::ValueTree tree ("DEVICES");
    tree.setProperty ("inputType", inputType, nullptr)
        .setProperty ("inputDevice", inputDevice, nullptr)
        .setProperty ("outputType", outputType, nullptr)
        .setProperty ("outputDevice", outputDevice, nullptr)
        .setProperty ("inputSampleRate", inputSampleRate, nullptr)
        .setProperty ("outputSampleRate", outputSampleRate, nullptr)
        .setProperty ("inputBlockSize", inputBlockSize, nullptr)
        .setProperty ("outputBlockSize", outputBlockSize, nullptr)
        .setProperty ("bridgeSafetyFrames", bridgeSafetyFrames, nullptr);
    return tree;
}

DeviceSelection DeviceSelection::fromValueTree (const juce::ValueTree& tree)
{
    DeviceSelection s;
    s.inputType          = tree.getProperty ("inputType").toString();
    s.inputDevice        = tree.getProperty ("inputDevice").toString();
    s.outputType         = tree.getProperty ("outputType").toString();
    s.outputDevice       = tree.getProperty ("outputDevice").toString();
    s.inputSampleRate    = tree.getProperty ("inputSampleRate", s.inputSampleRate);
    s.outputSampleRate   = tree.getProperty ("outputSampleRate", s.outputSampleRate);
    s.inputBlockSize     = tree.getProperty ("inputBlockSize", s.inputBlockSize);
    s.outputBlockSize    = tree.getProperty ("outputBlockSize", s.outputBlockSize);
    s.bridgeSafetyFrames = tree.getProperty ("bridgeSafetyFrames", s.bridgeSafetyFrames);
    return s;
}

//==============================================================================
AudioEngine::AudioEngine()
{
    juce::AudioDeviceManager().createAudioDeviceTypes (deviceTypes);

    for (auto* type : deviceTypes)
        type->scanForDevices();

    startTimer (500);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    close();
}

juce::AudioIODeviceType* AudioEngine::findType (const juce::String& typeName) const
{
    for (auto* type : deviceTypes)
        if (type->getTypeName() == typeName)
            return type;

    return nullptr;
}

std::unique_ptr<juce::AudioIODevice> AudioEngine::createAndOpen (const juce::String& typeName,
                                                                 const juce::String& outputName,
                                                                 const juce::String& inputName,
                                                                 double sampleRate, int blockSize,
                                                                 juce::String& error)
{
    auto* type = findType (typeName);

    if (type == nullptr)
    {
        error = "Unknown driver type: " + typeName;
        return {};
    }

    type->scanForDevices();
    std::unique_ptr<juce::AudioIODevice> device (type->createDevice (outputName, inputName));

    if (device == nullptr)
    {
        error = "Could not create device \"" + (outputName.isNotEmpty() ? outputName : inputName) + "\"";
        return {};
    }

    juce::BigInteger ins, outs;

    if (inputName.isNotEmpty())
        ins.setRange (0, device->getInputChannelNames().size(), true);

    if (outputName.isNotEmpty())
        outs.setRange (0, device->getOutputChannelNames().size(), true);

    // Fall back to the closest rate/size the device actually offers.
    const auto rates = device->getAvailableSampleRates();

    if (! rates.isEmpty() && ! rates.contains (sampleRate))
        sampleRate = rates.contains (48000.0) ? 48000.0 : rates[0];

    error = device->open (ins, outs, sampleRate, blockSize);

    if (error.isNotEmpty())
        return {};

    return device;
}

juce::String AudioEngine::open (const DeviceSelection& newSelection)
{
    close();
    selection = newSelection;
    wantRunning = true;

    if (selection.outputDevice.isEmpty())
    {
        reportError ("No output device selected");
        return lastError;
    }

    juce::String error;

    if (selection.usesSingleDevice())
    {
        outputDevice = createAndOpen (selection.outputType, selection.outputDevice, selection.inputDevice,
                                      selection.outputSampleRate, selection.outputBlockSize, error);
    }
    else
    {
        outputDevice = createAndOpen (selection.outputType, selection.outputDevice, {},
                                      selection.outputSampleRate, selection.outputBlockSize, error);

        if (outputDevice != nullptr && selection.inputDevice.isNotEmpty())
        {
            inputDevice = createAndOpen (selection.inputType, {}, selection.inputDevice,
                                         selection.inputSampleRate, selection.inputBlockSize, error);

            if (inputDevice == nullptr)
                outputDevice.reset();
        }
    }

    if (outputDevice == nullptr)
    {
        reportError (error);

        if (onDevicesChanged)
            onDevicesChanged();

        return error;
    }

    const auto outRate  = outputDevice->getCurrentSampleRate();
    const auto outBlock = outputDevice->getCurrentBufferSizeSamples();

    racks.prepare (outRate, outBlock);

    if (inputDevice != nullptr)
    {
        ClockBridge::Settings s;
        s.numChannels       = juce::jmax (1, inputDevice->getActiveInputChannels().countNumberOfSetBits());
        s.inputSampleRate   = inputDevice->getCurrentSampleRate();
        s.outputSampleRate  = outRate;
        s.inputBlockSize    = inputDevice->getCurrentBufferSizeSamples();
        s.outputBlockSize   = outBlock;
        s.extraSafetyFrames = selection.bridgeSafetyFrames;
        bridge.prepare (s);

        bridgedInput.setSize (s.numChannels, outBlock);
        bridgedInputPointers.resize ((size_t) s.numChannels);
    }

    bridgeActive = inputDevice != nullptr;
    currentSampleRate = outRate;

    {
        const juce::ScopedLock sl (errorLock);
        lastError.clear();
    }

    outputDevice->start (this);

    if (inputDevice != nullptr)
        inputDevice->start (&inputCallback);

    if (onDevicesChanged)
        onDevicesChanged();

    return {};
}

void AudioEngine::close()
{
    // Stop the master (output) callback first so it never sees a half-closed input.
    if (outputDevice != nullptr)
        outputDevice->stop();

    if (inputDevice != nullptr)
        inputDevice->stop();

    bridgeActive = false;

    if (inputDevice != nullptr)
    {
        inputDevice->close();
        inputDevice.reset();
    }

    if (outputDevice != nullptr)
    {
        outputDevice->close();
        outputDevice.reset();
    }

    racks.release();
    wantRunning = false;
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (inputDevice != nullptr)
        return inputDevice->getInputChannelNames();

    if (outputDevice != nullptr && selection.usesSingleDevice())
        return outputDevice->getInputChannelNames();

    return {};
}

juce::StringArray AudioEngine::getOutputChannelNames() const
{
    return outputDevice != nullptr ? outputDevice->getOutputChannelNames() : juce::StringArray();
}

AudioEngine::Status AudioEngine::getStatus() const
{
    Status s;
    s.running = outputDevice != nullptr;
    s.bridged = inputDevice != nullptr;
    s.cpuLoad = cpuLoad;
    s.inputPeak = inputPeak.exchange (0.0f);
    s.pluginLatency = alignedLatency;

    {
        const juce::ScopedLock sl (errorLock);
        s.lastError = lastError;
    }

    if (outputDevice == nullptr)
        return s;

    s.sampleRate = outputDevice->getCurrentSampleRate();
    s.blockSize  = outputDevice->getCurrentBufferSizeSamples();

    const auto toMs = [] (double samples, double rate) { return rate > 0.0 ? samples * 1000.0 / rate : 0.0; };

    // Each device adds its own buffer plus whatever latency its driver reports.
    s.outputDeviceMs = toMs (outputDevice->getOutputLatencyInSamples() + s.blockSize, s.sampleRate);
    s.pluginMs = toMs (s.pluginLatency, s.sampleRate);

    if (inputDevice != nullptr)
    {
        const auto inRate = inputDevice->getCurrentSampleRate();
        s.bridge = bridge.getStats();
        s.inputDeviceMs = toMs (inputDevice->getInputLatencyInSamples() + inputDevice->getCurrentBufferSizeSamples(), inRate);
        s.bridgeMs = s.bridge.latencyMs;
    }
    else
    {
        s.inputDeviceMs = toMs (outputDevice->getInputLatencyInSamples(), s.sampleRate);
    }

    s.totalLatencyMs = s.inputDeviceMs + s.bridgeMs + s.pluginMs + s.outputDeviceMs;
    return s;
}

void AudioEngine::meterInput (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    float peak = 0.0f;

    for (int ch = 0; ch < numInputs; ++ch)
    {
        if (inputs[ch] != nullptr)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (inputs[ch], numSamples);
            peak = juce::jmax (peak, -range.getStart(), range.getEnd());
        }
    }

    auto current = inputPeak.load (std::memory_order_relaxed);

    while (peak > current && ! inputPeak.compare_exchange_weak (current, peak, std::memory_order_relaxed))
    {}
}

void AudioEngine::reportError (const juce::String& message)
{
    const juce::ScopedLock sl (errorLock);
    lastError = message;
}

void AudioEngine::timerCallback()
{
    alignedLatency = racks.updateLatencyCompensation();

    // Every few seconds, reopen devices that disappeared (cable pulled, Dante
    // Virtual Soundcard restarted, USB interface power-cycled...).
    if (++timerTicks % 6 != 0 || ! wantRunning)
        return;

    const auto healthy = outputDevice != nullptr && outputDevice->isPlaying()
                      && (selection.usesSingleDevice() || selection.inputDevice.isEmpty()
                          || (inputDevice != nullptr && inputDevice->isPlaying()));

    if (! healthy)
        open (selection);
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();

    if (! bridgeActive)
    {
        meterInput (inputs, numInputs, numSamples);
        racks.process (inputs, numInputs, outputs, numOutputs, numSamples);
    }
    else
    {
        // Pull drift-corrected input in pieces no bigger than the staging buffer.
        for (int offset = 0; offset < numSamples;)
        {
            const auto n = juce::jmin (numSamples - offset, bridgedInput.getNumSamples());
            bridge.pull (bridgedInput.getArrayOfWritePointers(), bridgedInput.getNumChannels(), n);

            for (size_t ch = 0; ch < bridgedInputPointers.size(); ++ch)
                bridgedInputPointers[ch] = bridgedInput.getReadPointer ((int) ch);

            float* outs[256];
            const auto numOuts = juce::jmin (numOutputs, 256);

            for (int ch = 0; ch < numOuts; ++ch)
                outs[ch] = outputs[ch] + offset;

            racks.process (bridgedInputPointers.data(), (int) bridgedInputPointers.size(), outs, numOuts, n);
            offset += n;
        }
    }

    const auto budgetMs = numSamples * 1000.0 / currentSampleRate;
    const auto load = (juce::Time::getMillisecondCounterHiRes() - start) / budgetMs;
    cpuLoad = cpuLoad * 0.9 + load * 0.1;
}

} // namespace bridgerack
