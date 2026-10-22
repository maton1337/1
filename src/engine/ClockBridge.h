#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace bridgerack
{

/**
    Carries multichannel audio from one device clock domain to another.

    The input device's callback pushes frames in; the output device's callback
    (the master clock) pulls frames out. Two independent devices never run at
    exactly the same rate (e.g. Dante Virtual Soundcard locked to the network
    PTP clock versus a USB interface on its own crystal), so the reader
    resamples adaptively: a PI controller watches the FIFO fill level and nudges
    the resampling ratio so the buffer stays centred on its target latency.

    The same mechanism handles different nominal sample rates
    (e.g. 44.1 kHz in, 48 kHz out).

    push() and pull() are lock-free and safe to call from two different
    real-time threads. prepare() and reset() must not run concurrently with them.
*/
class ClockBridge
{
public:
    struct Settings
    {
        int    numChannels       = 2;
        double inputSampleRate   = 48000.0;
        double outputSampleRate  = 48000.0;
        int    inputBlockSize    = 256;
        int    outputBlockSize   = 256;
        int    extraSafetyFrames = 64;   // added on top of the computed target latency
        bool   adaptive          = true; // shrink the buffer to the jitter actually observed
    };

    struct Stats
    {
        double ratio        = 1.0;   // input frames consumed per output frame
        double driftPpm     = 0.0;   // correction applied relative to the nominal ratio
        double fillFrames   = 0.0;   // smoothed FIFO level
        double targetFrames = 0.0;
        double latencyMs    = 0.0;   // latency added by the bridge
        int    underruns    = 0;
        int    overruns     = 0;
        bool   locked       = false; // true once running and within tolerance
    };

    ClockBridge() = default;

    void prepare (const Settings&);
    void reset();

    /** Called from the input device thread. */
    void push (const float* const* channels, int numChannels, int numFrames) noexcept;

    /** Called from the output device thread. Always writes numFrames to every destination channel. */
    void pull (float* const* dest, int numDestChannels, int numFrames) noexcept;

    /** Same as above, with an explicit timestamp in seconds (used by the simulation tests). */
    void push (const float* const* channels, int numChannels, int numFrames, double nowSeconds) noexcept;
    void pull (float* const* dest, int numDestChannels, int numFrames, double nowSeconds) noexcept;

    Stats getStats() const noexcept;
    int getNumChannels() const noexcept { return settings.numChannels; }

private:
    void reprime() noexcept;
    double estimateFill (double nowSeconds) const noexcept;
    static double now() noexcept;

    Settings settings;
    int capacity = 0;

    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> ring;      // planar, capacity frames per channel
    juce::AudioBuffer<float> staging;   // contiguous copy of the frames peeked for resampling
    std::unique_ptr<juce::LagrangeInterpolator[]> interpolators;   // one per channel

    // Written by the producer under a sequence counter so the consumer can read
    // a consistent (total frames, time of last push) pair without locking.
    std::atomic<juce::uint32> pushSequence { 0 };
    std::atomic<juce::int64> framesWritten { 0 };
    std::atomic<int> lastPushFrames { 0 };
    std::atomic<double> lastPushTime { 0.0 };
    juce::int64 framesRead = 0;

    double nominalRatio = 1.0;
    double target = 0.0;
    double startTarget = 0.0, minTarget = 0.0, maxTarget = 0.0;   // adaptive range
    int windowMinMargin = 0;
    double windowSeconds = 0.0;
    double smoothedFill = 0.0;
    double integral = 0.0;
    double currentRatio = 1.0;
    double smoothedCorrection = 0.0;
    bool primed = false;
    int blocksSincePrime = 0;

    std::atomic<double> statRatio { 1.0 }, statFill { 0.0 }, statTarget { 0.0 };
    std::atomic<int> statUnderruns { 0 }, statOverruns { 0 };
    std::atomic<bool> statLocked { false };
};

} // namespace bridgerack
