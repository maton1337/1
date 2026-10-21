#pragma once

#include "ClockBridge.h"
#include "RackSet.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace bridgerack
{

/** Which devices to use. Input and output can be different devices, even of different driver types. */
struct DeviceSelection
{
    juce::String inputType, inputDevice;
    juce::String outputType, outputDevice;
    double inputSampleRate  = 48000.0;
    double outputSampleRate = 48000.0;
    int inputBlockSize  = 256;
    int outputBlockSize = 256;
    int bridgeSafetyFrames = 64;

    bool usesSingleDevice() const
    {
        return inputType == outputType && inputDevice == outputDevice && inputDevice.isNotEmpty();
    }

    juce::ValueTree toValueTree() const;
    static DeviceSelection fromValueTree (const juce::ValueTree&);
};

/**
    Owns the audio devices and drives the rack set.

    With a single device (same device for input and output), racks run directly
    in that device's callback. With two devices, the output device is the master
    clock: the input device feeds a ClockBridge, and the output callback pulls
    drift-corrected input from it, runs the racks and writes the result.
    This is what makes e.g. "receive on Dante, send to a USB interface" work.
*/
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    const juce::OwnedArray<juce::AudioIODeviceType>& getDeviceTypes() const noexcept { return deviceTypes; }
    juce::AudioIODeviceType* findType (const juce::String& typeName) const;

    /** Opens the devices. Returns an empty string on success, or an error description. */
    juce::String open (const DeviceSelection&);
    void close();

    const DeviceSelection& getSelection() const noexcept   { return selection; }
    bool isRunning() const noexcept                         { return outputDevice != nullptr; }
    bool isBridged() const noexcept                         { return inputDevice != nullptr; }

    juce::StringArray getInputChannelNames() const;
    juce::StringArray getOutputChannelNames() const;

    RackSet& getRacks() noexcept                            { return racks; }

    struct Status
    {
        bool running = false, bridged = false;
        double sampleRate = 0.0;
        int blockSize = 0;
        double cpuLoad = 0.0;               // 0..1, share of the block period spent processing
        int pluginLatency = 0;              // aligned rack latency, samples
        double totalLatencyMs = 0.0;        // device in + bridge + plugins + device out
        ClockBridge::Stats bridge;
        juce::String lastError;
    };

    Status getStatus() const;

    /** Called on the message thread after devices open or close. */
    std::function<void()> onDevicesChanged;

private:
    struct InputCallback : juce::AudioIODeviceCallback
    {
        explicit InputCallback (AudioEngine& e) : engine (e) {}

        void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                               float* const*, int, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            engine.bridge.push (inputs, numInputs, numSamples);
        }

        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
        void audioDeviceError (const juce::String& message) override { engine.reportError (message); }

        AudioEngine& engine;
    };

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String& message) override { reportError (message); }

    void timerCallback() override;
    void reportError (const juce::String&);
    std::unique_ptr<juce::AudioIODevice> createAndOpen (const juce::String& typeName,
                                                        const juce::String& outputName,
                                                        const juce::String& inputName,
                                                        double sampleRate, int blockSize,
                                                        juce::String& error);

    juce::OwnedArray<juce::AudioIODeviceType> deviceTypes;
    DeviceSelection selection;
    bool wantRunning = false;
    int timerTicks = 0;

    std::unique_ptr<juce::AudioIODevice> outputDevice, inputDevice;
    InputCallback inputCallback { *this };
    ClockBridge bridge;
    RackSet racks;

    juce::AudioBuffer<float> bridgedInput;
    std::vector<const float*> bridgedInputPointers;

    std::atomic<bool> bridgeActive { false };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<double> cpuLoad { 0.0 };
    std::atomic<int> alignedLatency { 0 };
    mutable juce::CriticalSection errorLock;
    juce::String lastError;
};

} // namespace bridgerack
