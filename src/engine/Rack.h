#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace bridgerack
{

constexpr int kMinSlots        = 4;
constexpr int kDefaultSlots    = 8;
constexpr int kMaxSlots        = 16;
constexpr int kMaxRackChannels = 64;       // buffer capacity
constexpr int kMaxChannelsPerRack = 2;     // racks are mono or stereo, like console channels
constexpr int kMaxCompensation = 1 << 16;   // samples of delay a rack can add for PDC

/**
    One processing rack: N channels taken from arbitrary device inputs, run
    through a serial chain of plugin slots, and summed into arbitrary device
    outputs. This is the equivalent of a SuperRack "rack".

    Threading: every non-const method except process() is for the message
    thread. process() runs on the audio thread; it try-locks the rack, so a
    rack that is being reconfigured simply goes silent for that block instead
    of blocking the audio callback or affecting other racks.
*/
class Rack
{
public:
    Rack (juce::String name, int numChannels, int numSlots = kDefaultSlots);
    ~Rack();

    void prepare (double sampleRate, int maximumBlockSize);
    void release();

    //==============================================================================
    juce::String getName() const                 { return name; }
    void setName (const juce::String& n)         { name = n; }

    int getNumChannels() const noexcept          { return numChannels; }
    double getSampleRate() const noexcept        { return sampleRate; }
    void setNumChannels (int newNumChannels);

    int getNumSlots() const noexcept             { return (int) slots.size(); }
    void setNumSlots (int newNumSlots);

    //==============================================================================
    /** Installs a processor (already instantiated, not yet prepared) into a slot.
        Returns whatever was there before so the caller can dispose of it (and its
        editor window) on the message thread.
    */
    std::unique_ptr<juce::AudioProcessor> setProcessor (int slot,
                                                        std::unique_ptr<juce::AudioProcessor>,
                                                        const juce::PluginDescription& = {});
    std::unique_ptr<juce::AudioProcessor> removeProcessor (int slot);
    void moveSlot (int from, int to);

    juce::AudioProcessor* getProcessor (int slot) const;
    juce::PluginDescription getDescription (int slot) const;

    /** Restores a slot plugin's state; the rack is silent while this runs. */
    void setProcessorState (int slot, const juce::MemoryBlock&);

    bool isSlotBypassed (int slot) const;
    void setSlotBypassed (int slot, bool);

    //==============================================================================
    /** inputMap[rackChannel] = device input channel, or -1 for silence. */
    std::vector<int> getInputMap() const;
    void setInputMap (std::vector<int>);

    /** outputMap[rackChannel] = device output channel, or -1 for not sent anywhere. */
    std::vector<int> getOutputMap() const;
    void setOutputMap (std::vector<int>);

    void setInputGainDb (float);
    float getInputGainDb() const noexcept        { return inputGainDb; }
    void setOutputGainDb (float);
    float getOutputGainDb() const noexcept       { return outputGainDb; }

    /** Strip colour as ARGB; 0 means "pick automatically". */
    void setColour (juce::uint32 argb) noexcept  { colour = argb; }
    juce::uint32 getColour() const noexcept      { return colour; }

    void setMuted (bool m) noexcept              { muted = m; }
    bool isMuted() const noexcept                { return muted; }
    void setBypassed (bool b) noexcept           { bypassed = b; }
    bool isBypassed() const noexcept             { return bypassed; }

    //==============================================================================
    /** Total latency reported by the plugins in this rack. */
    int getLatencySamples() const;

    /** Extra delay applied at the rack output to line it up with slower racks. */
    void setCompensationDelay (int samples) noexcept;
    int getCompensationDelay() const noexcept    { return compensationDelay; }

    /** Peak since the last call, for metering. */
    float takeInputPeak (int channel) noexcept;
    float takeOutputPeak (int channel) noexcept;

    //==============================================================================
    /** Audio thread. Adds this rack's output into the device output buffers. */
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs,
                  int numSamples) noexcept;

    /** Serialises everything except plugin binaries: routing, gains, slot plugins and their state. */
    juce::ValueTree toValueTree() const;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioProcessor> processor;
        juce::PluginDescription description;
        bool bypassed = false;
    };

    void configureProcessor (juce::AudioProcessor&) const;
    void runSlot (Slot&, int numSamples) noexcept;
    void applyCompensation (int numSamples) noexcept;
    static std::vector<int> fitMap (std::vector<int>, int size);

    juce::String name;
    int numChannels;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool prepared = false;

    juce::CriticalSection lock;
    std::vector<Slot> slots;
    std::vector<int> inputMap, outputMap;

    float inputGainDb = 0.0f, outputGainDb = 0.0f;
    juce::uint32 colour = 0;
    std::atomic<float> inputGain { 1.0f }, outputGain { 1.0f };
    std::atomic<bool> muted { false }, bypassed { false };

    std::atomic<int> compensationDelay { 0 };
    int activeDelay = 0, delayWritePos = 0;
    juce::AudioBuffer<float> delayBuffer;

    juce::AudioBuffer<float> work, pluginBuffer;
    juce::MidiBuffer midi;

    std::array<std::atomic<float>, kMaxRackChannels> inputPeaks {}, outputPeaks {};
};

} // namespace bridgerack
