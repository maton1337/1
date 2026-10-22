#include "ClockBridge.h"

#include <limits>

namespace bridgerack
{

namespace
{
    // PI controller on the FIFO error expressed in seconds. The closed loop is
    // x'' + kP x' + kI x = 0, i.e. natural frequency sqrt(kI) = 0.5 rad/s and
    // damping kP / (2 sqrt(kI)) = 0.7: settles in a few seconds without overshoot.
    constexpr double kProportional  = 0.7;
    constexpr double kIntegral      = 0.25;
    constexpr double kMaxCorrection = 0.002;   // +-2000 ppm, about 3.5 cents worst case
    constexpr double kFillTimeConst = 0.2;     // seconds
    constexpr double kStatsTimeConst = 5.0;    // seconds, smoothing of the reported drift
    constexpr int    kLookahead     = 4;       // spare frames for the interpolator
    constexpr double kAdaptWindow   = 4.0;     // seconds of observation before shrinking the buffer
    constexpr double kMaxBufferSecs = 0.25;    // the most an adaptive bridge will ever buffer
    constexpr int    kNoMargin      = std::numeric_limits<int>::max();
}

double ClockBridge::now() noexcept
{
    return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks());
}

void ClockBridge::prepare (const Settings& s)
{
    settings = s;
    settings.numChannels     = juce::jmax (1, s.numChannels);
    settings.inputBlockSize  = juce::jmax (1, s.inputBlockSize);
    settings.outputBlockSize = juce::jmax (1, s.outputBlockSize);

    nominalRatio = settings.inputSampleRate / settings.outputSampleRate;

    // The continuous fill estimate never exceeds what is really in the FIFO, so
    // the target only has to cover one output block plus scheduling jitter.
    target = std::ceil (settings.outputBlockSize * nominalRatio * (1.0 + kMaxCorrection))
           + kLookahead
           + settings.inputBlockSize / 2
           + settings.extraSafetyFrames;

    startTarget = target;
    minTarget = std::ceil (settings.outputBlockSize * nominalRatio * (1.0 + kMaxCorrection)) + kLookahead + 8;
    maxTarget = juce::jmax (startTarget * 2.0, kMaxBufferSecs * settings.inputSampleRate);
    statTarget = target;

    capacity = juce::nextPowerOfTwo (juce::jmax (16384, (int) (maxTarget + settings.inputBlockSize) * 4));
    fifo.setTotalSize (capacity);
    ring.setSize (settings.numChannels, capacity);
    ring.clear();

    const auto maxNeeded = (int) std::ceil (settings.outputBlockSize * nominalRatio * (1.0 + kMaxCorrection)) + kLookahead + 2;
    staging.setSize (settings.numChannels, maxNeeded);
    interpolators = std::make_unique<juce::LagrangeInterpolator[]> ((size_t) settings.numChannels);

    statUnderruns = 0;
    statOverruns = 0;
    reset();
}

void ClockBridge::reset()
{
    fifo.reset();
    framesWritten = 0;
    framesRead = 0;
    lastPushFrames = 0;
    lastPushTime = 0.0;
    smoothedCorrection = 0.0;
    reprime();
}

void ClockBridge::reprime() noexcept
{
    primed = false;
    smoothedFill = target;
    integral = 0.0;
    currentRatio = nominalRatio;
    blocksSincePrime = 0;
    statLocked = false;
    windowMinMargin = kNoMargin;
    windowSeconds = 0.0;

    for (int ch = 0; ch < settings.numChannels && interpolators != nullptr; ++ch)
        interpolators[(size_t) ch].reset();
}

//==============================================================================
void ClockBridge::push (const float* const* channels, int numChannels, int numFrames) noexcept
{
    push (channels, numChannels, numFrames, now());
}

void ClockBridge::push (const float* const* channels, int numChannels, int numFrames, double nowSeconds) noexcept
{
    const auto free = fifo.getFreeSpace();

    if (numFrames > free)
    {
        ++statOverruns;
        numFrames = free;
    }

    if (numFrames <= 0)
        return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

    for (int ch = 0; ch < settings.numChannels; ++ch)
    {
        auto* dst = ring.getWritePointer (ch);

        if (ch < numChannels && channels[ch] != nullptr)
        {
            juce::FloatVectorOperations::copy (dst + start1, channels[ch], size1);

            if (size2 > 0)
                juce::FloatVectorOperations::copy (dst + start2, channels[ch] + size1, size2);
        }
        else
        {
            juce::FloatVectorOperations::clear (dst + start1, size1);

            if (size2 > 0)
                juce::FloatVectorOperations::clear (dst + start2, size2);
        }
    }

    pushSequence.fetch_add (1, std::memory_order_acq_rel);
    framesWritten.fetch_add (size1 + size2, std::memory_order_relaxed);
    lastPushFrames.store (size1 + size2, std::memory_order_relaxed);
    lastPushTime.store (nowSeconds, std::memory_order_relaxed);
    pushSequence.fetch_add (1, std::memory_order_acq_rel);

    fifo.finishedWrite (size1 + size2);
}

double ClockBridge::estimateFill (double nowSeconds) const noexcept
{
    // Input arrives in bursts of one device block, so the raw FIFO level is a
    // sawtooth. Treat the last block as if it had been arriving smoothly over
    // the time since it was pushed; this gives a fill level that changes
    // continuously and reflects only the real rate difference.
    juce::int64 written;
    int blockFrames;
    double pushTime;

    for (;;)
    {
        const auto before = pushSequence.load (std::memory_order_acquire);
        written     = framesWritten.load (std::memory_order_relaxed);
        blockFrames = lastPushFrames.load (std::memory_order_relaxed);
        pushTime    = lastPushTime.load (std::memory_order_relaxed);
        const auto after = pushSequence.load (std::memory_order_acquire);

        if (before == after && (before & 1) == 0)
            break;
    }

    const auto arrived = juce::jlimit (0.0, (double) blockFrames, (nowSeconds - pushTime) * settings.inputSampleRate);
    return (double) (written - blockFrames - framesRead) + arrived;
}

void ClockBridge::pull (float* const* dest, int numDestChannels, int numFrames) noexcept
{
    pull (dest, numDestChannels, numFrames, now());
}

void ClockBridge::pull (float* const* dest, int numDestChannels, int numFrames, double nowSeconds) noexcept
{
    for (int offset = 0; offset < numFrames;)
    {
        const auto n = juce::jmin (numFrames - offset, settings.outputBlockSize);
        const auto t = nowSeconds + offset / settings.outputSampleRate;
        const auto silence = [&]
        {
            for (int ch = 0; ch < numDestChannels; ++ch)
                juce::FloatVectorOperations::clear (dest[ch] + offset, n);
        };

        const auto available = fifo.getNumReady();
        auto fill = estimateFill (t);

        if (! primed || available > capacity * 3 / 4)
        {
            if (primed)
                ++statOverruns;   // the reader fell far behind (e.g. the output device stalled)

            if (fill < target)
            {
                silence();
                offset += n;
                continue;
            }

            const auto excess = juce::jlimit (0, available, (int) (fill - target));
            fifo.finishedRead (excess);
            framesRead += excess;
            fill -= excess;

            const auto wasPrimed = primed;
            reprime();
            primed = true;

            if (wasPrimed)
                blocksSincePrime = 1;
        }

        const auto dt = n / settings.outputSampleRate;
        smoothedFill += juce::jlimit (0.0, 1.0, dt / kFillTimeConst) * (fill - smoothedFill);

        const auto errorSeconds = (smoothedFill - target) / settings.inputSampleRate;
        integral = juce::jlimit (-kMaxCorrection / kIntegral, kMaxCorrection / kIntegral, integral + errorSeconds * dt);
        const auto correction = juce::jlimit (-kMaxCorrection, kMaxCorrection, kProportional * errorSeconds + kIntegral * integral);

        currentRatio = nominalRatio * (1.0 + correction);
        smoothedCorrection += juce::jlimit (0.0, 1.0, dt / kStatsTimeConst) * (correction - smoothedCorrection);

        const auto needed = juce::jmin (staging.getNumSamples(), (int) std::ceil (n * currentRatio) + kLookahead);
        const auto ready = fifo.getNumReady();

        if (ready < needed)
        {
            ++statUnderruns;

            // The buffer was too tight for this system's jitter: give it more room, and
            // never shrink below that again so one dropout doesn't become a pattern.
            if (settings.adaptive)
            {
                // Size the buffer to the input gap actually happening right now (software
                // drivers such as Dante Virtual Soundcard can stall for tens of ms), so it
                // takes one or two dropouts to learn the system rather than dozens.
                const auto stallFrames = juce::jmax (0.0, t - lastPushTime.load (std::memory_order_relaxed)) * settings.inputSampleRate;
                const auto keep = juce::jmax (16, settings.extraSafetyFrames / 2);
                const auto wanted = (double) needed + stallFrames + settings.inputBlockSize + keep;
                target = juce::jlimit (minTarget, maxTarget, juce::jmax (target + juce::jmax (32, settings.inputBlockSize / 2), wanted));
                minTarget = target;
            }

            statTarget = target;
            silence();
            reprime();
            offset += n;
            continue;
        }

        if (settings.adaptive && blocksSincePrime * dt > 2.0)
        {
            // Track the smallest spare margin seen; if even the worst case left plenty of
            // room over a whole window, shrink the target latency by part of the excess.
            windowMinMargin = juce::jmin (windowMinMargin, ready - needed);
            windowSeconds += dt;

            if (windowSeconds >= kAdaptWindow)
            {
                const auto keep = juce::jmax (16, settings.extraSafetyFrames / 2);

                if (windowMinMargin > keep * 2)
                    target = juce::jmax (minTarget, target - (windowMinMargin - keep) * 0.5);

                statTarget = target;
                windowMinMargin = kNoMargin;
                windowSeconds = 0.0;
            }
        }

        int start1, size1, start2, size2;
        fifo.prepareToRead (needed, start1, size1, start2, size2);

        for (int ch = 0; ch < settings.numChannels; ++ch)
        {
            auto* stage = staging.getWritePointer (ch);
            juce::FloatVectorOperations::copy (stage, ring.getReadPointer (ch, start1), size1);

            if (size2 > 0)
                juce::FloatVectorOperations::copy (stage + size1, ring.getReadPointer (ch, start2), size2);
        }

        int used = 0;

        for (int ch = 0; ch < settings.numChannels; ++ch)
        {
            // Channels the destination doesn't have still run so every interpolator stays in step.
            float* out = ch < numDestChannels ? dest[ch] + offset : staging.getWritePointer (ch);
            used = interpolators[(size_t) ch].process (currentRatio, staging.getReadPointer (ch), out, n, needed, 0);
        }

        for (int ch = settings.numChannels; ch < numDestChannels; ++ch)
            juce::FloatVectorOperations::clear (dest[ch] + offset, n);

        fifo.finishedRead (used);
        framesRead += used;

        ++blocksSincePrime;
        statRatio = nominalRatio * (1.0 + smoothedCorrection);
        statFill = smoothedFill;
        statLocked = blocksSincePrime * dt > kStatsTimeConst
                  && std::abs (smoothedFill - target) < juce::jmax (32.0, target * 0.25);

        offset += n;
    }
}

ClockBridge::Stats ClockBridge::getStats() const noexcept
{
    Stats s;
    s.ratio        = statRatio;
    s.driftPpm     = (s.ratio / nominalRatio - 1.0) * 1.0e6;
    s.fillFrames   = statFill;
    s.targetFrames = statTarget;
    s.latencyMs    = (s.targetFrames + settings.inputBlockSize * 0.5) / settings.inputSampleRate * 1000.0;
    s.underruns    = statUnderruns;
    s.overruns     = statOverruns;
    s.locked       = statLocked;
    return s;
}

} // namespace bridgerack
