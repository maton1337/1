#pragma once

// Simulated audio devices for end-to-end engine tests: each device runs its own
// real-time thread and calls its callback once per block, like a real driver.

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

namespace bridgerack::test
{

class FakeDevice : public juce::AudioIODevice,
                   private juce::Thread
{
public:
    FakeDevice (const juce::String& deviceName, int numIns, int numOuts, double clockScale, float sineHz)
        : AudioIODevice (deviceName, "Fake"), Thread ("Fake " + deviceName),
          ins (numIns), outs (numOuts), scale (clockScale), hz (sineHz) {}

    ~FakeDevice() override { close(); }

    juce::StringArray getOutputChannelNames() override { return names ("Out", outs); }
    juce::StringArray getInputChannelNames() override  { return names ("In", ins); }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override    { return { 64, 128, 256, 512 }; }
    int getDefaultBufferSize() override                    { return 256; }

    juce::String open (const juce::BigInteger& inputs, const juce::BigInteger& outputs, double rate, int block) override
    {
        activeIns = inputs;
        activeOuts = outputs;
        sampleRate = rate;
        blockSize = block;
        isOpenFlag = true;
        return {};
    }

    void close() override                                  { stop(); isOpenFlag = false; }
    bool isOpen() override                                 { return isOpenFlag; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        callback = cb;
        startThread (juce::Thread::Priority::highest);
    }

    void stop() override
    {
        stopThread (2000);
        callback = nullptr;
    }

    bool isPlaying() override                              { return isThreadRunning(); }
    juce::String getLastError() override                   { return {}; }
    int getCurrentBufferSizeSamples() override             { return blockSize; }
    double getCurrentSampleRate() override                 { return sampleRate; }
    int getCurrentBitDepth() override                      { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
    juce::BigInteger getActiveInputChannels() const override  { return activeIns; }
    int getOutputLatencyInSamples() override               { return 0; }
    int getInputLatencyInSamples() override                { return 0; }

    /** Largest output sample seen per channel since the last call. */
    float takeOutputPeak (int channel) { return juce::isPositiveAndBelow (channel, 8) ? peaks[(size_t) channel].exchange (0.0f) : 0.0f; }
    int getCallbacks() const { return callbacks; }

private:
    static juce::StringArray names (const juce::String& prefix, int n)
    {
        juce::StringArray s;

        for (int i = 0; i < n; ++i)
            s.add (prefix + " " + juce::String (i + 1));

        return s;
    }

    void run() override
    {
        const auto numIns = activeIns.countNumberOfSetBits();
        const auto numOuts = activeOuts.countNumberOfSetBits();
        juce::AudioBuffer<float> in (juce::jmax (1, numIns), blockSize), out (juce::jmax (1, numOuts), blockSize);
        const auto period = blockSize / sampleRate * scale * 1000.0;
        auto next = juce::Time::getMillisecondCounterHiRes();
        double phase = 0.0;

        while (! threadShouldExit())
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = (float) (0.5 * std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * hz / sampleRate;

                for (int ch = 0; ch < numIns; ++ch)
                    in.setSample (ch, i, v);
            }

            out.clear();

            if (auto* cb = callback.load())
                cb->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), numIns,
                                                      out.getArrayOfWritePointers(), numOuts, blockSize, {});

            for (int ch = 0; ch < juce::jmin (numOuts, 8); ++ch)
            {
                const auto p = out.getMagnitude (ch, 0, blockSize);
                auto current = peaks[(size_t) ch].load();

                while (p > current && ! peaks[(size_t) ch].compare_exchange_weak (current, p)) {}
            }

            ++callbacks;
            next += period;

            // Sleep most of the way, then yield until the deadline, so the device keeps
            // time like hardware does instead of drifting with the OS timer granularity.
            for (;;)
            {
                const auto wait = next - juce::Time::getMillisecondCounterHiRes();

                if (wait <= 0.0)
                    break;

                if (wait > 2.0)
                    juce::Thread::sleep ((int) wait - 1);
                else
                    juce::Thread::yield();
            }
        }
    }

    int ins, outs;
    double scale;
    float hz;
    juce::BigInteger activeIns, activeOuts;
    double sampleRate = 48000.0;
    int blockSize = 256;
    bool isOpenFlag = false;
    std::atomic<juce::AudioIODeviceCallback*> callback { nullptr };
    std::array<std::atomic<float>, 8> peaks {};
    std::atomic<int> callbacks { 0 };
};

/** Driver type exposing an input-only "Dante" device and an output-only "USB" device,
    whose clocks run at slightly different speeds. */
class FakeDeviceType : public juce::AudioIODeviceType
{
public:
    FakeDeviceType() : AudioIODeviceType ("Fake") {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool wantInputNames) const override
    {
        return wantInputNames ? juce::StringArray { "Dante", "Duplex" } : juce::StringArray { "USB", "Duplex" };
    }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override { return getDeviceNames (asInput).indexOf (d->getName()); }
    bool hasSeparateInputsAndOutputs() const override { return true; }

    juce::AudioIODevice* createDevice (const juce::String& outputName, const juce::String& inputName) override
    {
        if (outputName == "Duplex" || inputName == "Duplex")
            return last = new FakeDevice ("Duplex", 2, 2, 1.0, 440.0f);

        if (inputName.isNotEmpty())
            return lastInput = new FakeDevice (inputName, 4, 0, 1.0 / (1.0 + 80e-6), 440.0f);   // input clock 80 ppm fast

        return last = new FakeDevice (outputName, 0, 4, 1.0, 0.0f);
    }

    /** The most recently created device with outputs (where the result can be observed). */
    FakeDevice* last = nullptr;
    FakeDevice* lastInput = nullptr;
};

} // namespace bridgerack::test
