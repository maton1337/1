// Headless tests for the BridgeRack engine: clock bridging, rack routing,
// latency compensation and session/snapshot state.

#include "engine/AudioEngine.h"
#include "engine/ClockBridge.h"
#include "FakeDevices.h"
#include "engine/RackSet.h"
#include "engine/Session.h"

#include <cstdio>
#include <functional>

using namespace bridgerack;

namespace
{
int failures = 0;

void check (bool condition, const juce::String& what)
{
    std::printf ("  [%s] %s\n", condition ? "ok" : "FAIL", what.toRawUTF8());

    if (! condition)
        ++failures;
}

//==============================================================================
/** Minimal processor base for test stand-ins of plugins. */
struct TestProcessor : juce::AudioProcessor
{
    TestProcessor()
        : AudioProcessor (BusesProperties().withInput  ("In",  juce::AudioChannelSet::stereo())
                                           .withOutput ("Out", juce::AudioChannelSet::stereo())) {}

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

struct GainProcessor : TestProcessor
{
    const juce::String getName() const override { return "Gain"; }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gain);
    }

    void getStateInformation (juce::MemoryBlock& data) override { data.append (&gain, sizeof (gain)); }

    void setStateInformation (const void* data, int size) override
    {
        if (size == (int) sizeof (gain))
            std::memcpy (&gain, data, sizeof (gain));
    }

    float gain = 1.0f;
};

struct DelayProcessor : TestProcessor
{
    explicit DelayProcessor (int samples) : delay (samples) { setLatencySamples (samples); }

    const juce::String getName() const override { return "Delay"; }

    void prepareToPlay (double, int) override
    {
        line.setSize (getTotalNumOutputChannels(), delay + 1);
        line.clear();
        pos = 0;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto size = line.getNumSamples();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                line.setSample (ch, pos, buffer.getSample (ch, i));
                buffer.setSample (ch, i, line.getSample (ch, (pos + 1) % size));
            }

            pos = (pos + 1) % size;
        }
    }

    int delay, pos = 0;
    juce::AudioBuffer<float> line;
};

juce::PluginDescription describe (const juce::String& name)
{
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = "Test";
    d.fileOrIdentifier = name;
    d.uniqueId = name.hashCode();
    return d;
}

//==============================================================================
struct BridgeRun
{
    int underrunsAfterSettle = 0, totalUnderruns = 0, overruns = 0;
    double finalPpm = 0.0, finalFill = 0.0, target = 0.0;
    double maxStepAfterSettle = 0.0;
    double measuredFrequency = 0.0;
    double startTarget = 0.0;
    bool locked = false;
};

/** Runs two simulated devices against a ClockBridge in event order.
    drift is how much faster the input clock runs than nominal (e.g. 100e-6). */
BridgeRun simulateBridge (double inRate, double outRate, int inBlock, int outBlock,
                          double drift, double seconds, double sineHz = 0.0, double stallAt = -1.0,
                          double burstMs = 0.0)
{
    ClockBridge bridge;
    ClockBridge::Settings s;
    s.numChannels = 2;
    s.inputSampleRate = inRate;
    s.outputSampleRate = outRate;
    s.inputBlockSize = inBlock;
    s.outputBlockSize = outBlock;
    bridge.prepare (s);

    juce::AudioBuffer<float> in (2, inBlock), out (2, outBlock);
    BridgeRun r;
    r.startTarget = bridge.getStats().targetFrames;
    double tIn = 0.0, tOut = 0.0, phase = 0.0;
    const auto actualInRate = inRate * (1.0 + drift);
    const auto settleTime = 5.0;

    float previous = 0.0f;
    bool havePrevious = false;
    int risingCrossings = 0;
    double firstCrossing = -1.0, lastCrossing = -1.0;
    long long outFrames = 0;
    int underrunsSoFar = 0;

    // Callbacks never fire exactly on time; each one gets up to 0.5 ms of jitter.
    // Events are processed in the order they actually fire, like real threads.
    juce::Random random (1234);
    const auto jitter = [&] { return random.nextDouble() * 0.0005; };

    tIn = inBlock / actualInRate;   // an input callback fires once its block has been captured
    auto inStamp = tIn + jitter();
    auto outStamp = tOut + jitter();

    while (tOut < seconds)
    {
        if (inStamp <= outStamp)
        {
            for (int i = 0; i < inBlock; ++i)
            {
                const auto v = (float) std::sin (phase);
                in.setSample (0, i, v);
                in.setSample (1, i, -v);
                phase += juce::MathConstants<double>::twoPi * sineHz / inRate;
            }

            bridge.push (in.getArrayOfReadPointers(), 2, inBlock, inStamp);
            const auto previousStamp = inStamp;
            tIn += inBlock / actualInRate;
            inStamp = tIn + jitter();

            // Software drivers sometimes freeze for burstMs and then deliver the backlog back to back.
            if (burstMs > 0.0 && random.nextFloat() < 0.01f)
                inStamp += burstMs / 1000.0;

            inStamp = juce::jmax (inStamp, previousStamp);

            // Optionally one input callback arrives 3 ms late, like a busy driver.
            if (stallAt > 0.0 && tIn >= stallAt)
            {
                inStamp += 0.003;
                stallAt = -1.0;
            }
        }
        else
        {
            bridge.pull (out.getArrayOfWritePointers(), 2, outBlock, outStamp);
            const auto after = bridge.getStats().underruns;

            if (tOut > settleTime)
            {
                r.underrunsAfterSettle += after - underrunsSoFar;

                for (int i = 0; i < outBlock; ++i)
                {
                    const auto v = out.getSample (0, i);

                    if (havePrevious)
                    {
                        r.maxStepAfterSettle = juce::jmax (r.maxStepAfterSettle, (double) std::abs (v - previous));

                        if (previous < 0.0f && v >= 0.0f)
                        {
                            const auto t = (double) (outFrames + i) / outRate;

                            if (firstCrossing < 0.0)
                                firstCrossing = t;
                            else
                                ++risingCrossings;

                            lastCrossing = t;
                        }
                    }

                    previous = v;
                    havePrevious = true;
                }
            }

            underrunsSoFar = after;
            outFrames += outBlock;
            tOut += outBlock / outRate;
            outStamp = tOut + jitter();
        }
    }

    const auto stats = bridge.getStats();
    r.totalUnderruns = stats.underruns;
    r.overruns = stats.overruns;
    r.finalPpm = stats.driftPpm;
    r.finalFill = stats.fillFrames;
    r.target = stats.targetFrames;
    r.locked = stats.locked;

    if (risingCrossings > 0)
        r.measuredFrequency = risingCrossings / (lastCrossing - firstCrossing);

    return r;
}

void testBridgeDrift (double driftPpm, int inBlock, int outBlock)
{
    std::printf ("ClockBridge: %+.0f ppm drift, in %d / out %d frames\n", driftPpm, inBlock, outBlock);
    const auto r = simulateBridge (48000.0, 48000.0, inBlock, outBlock, driftPpm * 1.0e-6, 120.0);

    // The output consumes (1 + drift) input frames per output frame once locked.
    check (r.underrunsAfterSettle == 0, "no underruns after settling (total " + juce::String (r.totalUnderruns) + ")");
    check (r.overruns == 0, "no overruns");
    check (std::abs (r.finalPpm - driftPpm) < 15.0, "tracks drift: " + juce::String (r.finalPpm, 2) + " ppm");
    check (std::abs (r.finalFill - r.target) < juce::jmax (16.0, r.target * 0.1),
           "fill " + juce::String (r.finalFill, 1) + " ~ target " + juce::String (r.target, 1));
    check (r.locked, "reports locked");
}

void testBridgeAdaptation()
{
    std::printf ("ClockBridge: adaptive buffer\n");
    const auto r = simulateBridge (48000.0, 48000.0, 256, 256, 20.0e-6, 120.0);
    check (r.target < r.startTarget * 0.8 && r.underrunsAfterSettle == 0,
           "shrinks from " + juce::String (r.startTarget, 0) + " to " + juce::String (r.target, 0) + " frames without dropouts");

    const auto stalled = simulateBridge (48000.0, 48000.0, 256, 256, 20.0e-6, 120.0, 0.0, 60.0);
    check (stalled.totalUnderruns <= 1, "a late input callback costs at most one dropout (" + juce::String (stalled.totalUnderruns) + ")");
    check (stalled.target > r.target, "and the buffer grows afterwards (" + juce::String (stalled.target, 0) + " frames)");

    // Repeated 40 ms freezes, like a busy virtual soundcard: a few dropouts while
    // learning, then none.
    const auto bursty = simulateBridge (48000.0, 48000.0, 256, 256, 20.0e-6, 120.0, 0.0, -1.0, 40.0);
    check (bursty.totalUnderruns <= 4 && bursty.underrunsAfterSettle <= 2,
           "40 ms input freezes: " + juce::String (bursty.totalUnderruns) + " dropouts in 120 s, buffer "
               + juce::String (bursty.target * 1000.0 / 48000.0, 1) + " ms");
}

void testBridgeRateConversion()
{
    std::printf ("ClockBridge: 44.1 kHz -> 48 kHz, 1 kHz sine, +50 ppm\n");
    const auto r = simulateBridge (44100.0, 48000.0, 128, 256, 50.0e-6, 60.0, 1000.0);

    // 1 kHz at a 44.1 kHz clock that runs 50 ppm fast is 1000.05 Hz in real time.
    check (r.underrunsAfterSettle == 0, "no underruns after settling");
    check (std::abs (r.measuredFrequency - 1000.05) < 0.05, "output frequency " + juce::String (r.measuredFrequency, 3) + " Hz");

    // A 1 kHz full-scale sine at 48 kHz moves at most 2*pi*1000/48000 = 0.131 per sample.
    check (r.maxStepAfterSettle < 0.14, "no discontinuities (max step " + juce::String (r.maxStepAfterSettle, 4) + ")");
}

//==============================================================================
void testRouting()
{
    std::printf ("RackSet: routing and summing\n");

    RackSet racks;
    auto& a = racks.addRack (std::make_unique<Rack> ("A", 2));
    auto& b = racks.addRack (std::make_unique<Rack> ("B", 1));
    racks.prepare (48000.0, 64);

    auto gain = std::make_unique<GainProcessor>();
    gain->gain = 0.5f;
    a.setProcessor (0, std::move (gain), describe ("Gain"));

    // Rack B takes device input 2 and adds it into device output 1 on top of rack A.
    b.setInputMap ({ 2 });
    b.setOutputMap ({ 1 });

    juce::AudioBuffer<float> in (3, 64), out (2, 64);
    in.clear();
    juce::FloatVectorOperations::fill (in.getWritePointer (0), 1.0f, 64);
    juce::FloatVectorOperations::fill (in.getWritePointer (1), 2.0f, 64);
    juce::FloatVectorOperations::fill (in.getWritePointer (2), 4.0f, 64);

    racks.process (in.getArrayOfReadPointers(), 3, out.getArrayOfWritePointers(), 2, 64);

    check (juce::approximatelyEqual (out.getSample (0, 10), 0.5f), "out 1 = in 1 * 0.5");
    check (juce::approximatelyEqual (out.getSample (1, 10), 1.0f + 4.0f), "out 2 = in 2 * 0.5 + in 3");

    a.setMuted (true);
    racks.process (in.getArrayOfReadPointers(), 3, out.getArrayOfWritePointers(), 2, 64);
    check (juce::approximatelyEqual (out.getSample (1, 10), 4.0f), "muted rack drops out of the sum");

    a.setMuted (false);
    a.setSlotBypassed (0, true);
    racks.process (in.getArrayOfReadPointers(), 3, out.getArrayOfWritePointers(), 2, 64);
    check (juce::approximatelyEqual (out.getSample (0, 10), 1.0f), "bypassed slot passes audio through");

    // Larger device blocks than the prepared size get split into chunks.
    juce::AudioBuffer<float> bigIn (3, 200), bigOut (2, 200);
    bigIn.clear();
    juce::FloatVectorOperations::fill (bigIn.getWritePointer (2), 4.0f, 200);
    racks.process (bigIn.getArrayOfReadPointers(), 3, bigOut.getArrayOfWritePointers(), 2, 200);
    check (juce::approximatelyEqual (bigOut.getSample (1, 199), 4.0f), "oversized blocks are processed in chunks");

    b.setNumChannels (2);
    check (b.getInputMap() == std::vector<int> { 2, 3 }, "mono -> stereo extends routing sequentially");

    a.setNumChannels (8);
    check (a.getNumChannels() == 2, "racks are limited to stereo");
}

void testLatencyCompensation()
{
    std::printf ("RackSet: plugin delay compensation\n");

    RackSet racks;
    auto& slow = racks.addRack (std::make_unique<Rack> ("Slow", 1));
    auto& fast = racks.addRack (std::make_unique<Rack> ("Fast", 1));
    racks.prepare (48000.0, 32);

    slow.setProcessor (0, std::make_unique<DelayProcessor> (100), describe ("Delay"));
    slow.setOutputMap ({ 0 });
    fast.setOutputMap ({ 1 });
    racks.updateLatencyCompensation();
    check (fast.getCompensationDelay() == 0, "racks are not delayed unless they opt in");

    slow.setAligned (true);
    fast.setAligned (true);
    auto& other = racks.addRack (std::make_unique<Rack> ("Other", 1));
    other.setOutputMap ({ -1 });
    racks.updateLatencyCompensation();

    check (fast.getCompensationDelay() == 100 && slow.getCompensationDelay() == 0, "aligned faster rack is delayed by 100 samples");
    check (other.getCompensationDelay() == 0, "a rack that isn't aligned keeps its own latency");

    juce::AudioBuffer<float> in (1, 32), out (2, 32);
    int slowPeak = -1, fastPeak = -1;

    for (int block = 0; block < 8; ++block)
    {
        in.clear();

        if (block == 0)
            in.setSample (0, 0, 1.0f);

        racks.process (in.getArrayOfReadPointers(), 1, out.getArrayOfWritePointers(), 2, 32);

        for (int i = 0; i < 32; ++i)
        {
            if (out.getSample (0, i) > 0.5f) slowPeak = block * 32 + i;
            if (out.getSample (1, i) > 0.5f) fastPeak = block * 32 + i;
        }
    }

    check (slowPeak == 100 && fastPeak == 100, "impulse arrives at sample " + juce::String (slowPeak) + " / " + juce::String (fastPeak));
}

void testSessionAndSnapshots()
{
    std::printf ("Session: capture, restore and snapshot recall\n");

    int created = 0;
    SessionCallbacks cb;
    cb.createProcessor = [&] (const juce::PluginDescription& d, juce::String& error) -> std::unique_ptr<juce::AudioProcessor>
    {
        ++created;

        if (d.name == "Gain")
            return std::make_unique<GainProcessor>();

        error = "not found";
        return nullptr;
    };

    RackSet original;
    auto& rack = original.addRack (std::make_unique<Rack> ("Vocals", 2));
    original.prepare (48000.0, 64);
    auto gain = std::make_unique<GainProcessor>();
    gain->gain = 0.25f;
    rack.setProcessor (2, std::move (gain), describe ("Gain"));
    rack.setSlotBypassed (2, true);
    rack.setInputMap ({ 5, 6 });
    rack.setOutputGainDb (-6.0f);

    const auto captured = captureRacks (original);

    RackSet restored;
    restored.prepare (48000.0, 64);
    const auto errors = applyRacks (captured, restored, cb);

    check (errors.isEmpty(), "restores without errors");
    check (restored.size() == 1 && restored.getRack (0)->getName() == "Vocals", "rack restored");

    auto* r = restored.getRack (0);
    auto* g = dynamic_cast<GainProcessor*> (r->getProcessor (2));
    check (g != nullptr && juce::approximatelyEqual (g->gain, 0.25f), "plugin state restored into the same slot");
    check (r->isSlotBypassed (2), "slot bypass restored");
    check (r->getInputMap() == std::vector<int> { 5, 6 }, "routing restored");
    check (juce::approximatelyEqual (r->getOutputGainDb(), -6.0f), "gain restored");

    // Snapshot recall into the same plugin only restores state.
    SnapshotBank bank;
    bank.add ("Verse", captureRacks (restored));
    g->gain = 0.9f;
    const auto before = created;
    applyRacks (bank.get (0)->racks, restored, cb);
    check (created == before && restored.getRack (0)->getProcessor (2) == g, "recall reuses the loaded plugin instance");
    check (juce::approximatelyEqual (g->gain, 0.25f), "recall restores plugin settings");

    juce::ValueTree missing ("RACKS");
    auto rackTree = captured.getChild (0).createCopy();
    rackTree.getChild (2).getChildWithName ("PLUGIN").setProperty ("name", "Missing", nullptr);
    missing.appendChild (rackTree, nullptr);
    check (applyRacks (missing, restored, cb).size() == 1, "missing plugin is reported, not fatal");

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bridgerack-test.bridgerack");
    SessionFile::write (file, SessionFile::create (juce::ValueTree ("DEVICES"), original, bank));
    const auto loaded = SessionFile::read (file);
    SnapshotBank loadedBank;
    loadedBank.loadFrom (loaded.getChildWithName ("SNAPSHOTS"));
    check (loaded.getChildWithName ("RACKS").getNumChildren() == 1 && loadedBank.size() == 1, "session file round-trips");
    file.deleteFile();
}
}

void testEndToEnd (const juce::String& inputDevice, const juce::String& outputDevice, double seconds, bool adaptive = true)
{
    std::printf ("AudioEngine end to end: %s -> %s, real-time threads for %.0f s%s\n",
                 inputDevice.toRawUTF8(), outputDevice.toRawUTF8(), seconds, adaptive ? "" : ", fixed bridge buffer");

    AudioEngine engine;
    auto fakeType = std::make_unique<test::FakeDeviceType>();
    auto* fake = fakeType.get();
    engine.addDeviceType (std::move (fakeType));

    auto& rack = engine.getRacks().addRack (std::make_unique<Rack> ("Ch 1", 2));
    rack.setInputMap ({ 0, 1 });
    rack.setOutputMap ({ 0, 1 });

    DeviceSelection sel;
    sel.inputType = sel.outputType = "Fake";
    sel.inputDevice = inputDevice;
    sel.outputDevice = outputDevice;
    sel.inputBlockSize = sel.outputBlockSize = 256;
    sel.adaptiveBridge = adaptive;

    const auto error = engine.open (sel);
    check (error.isEmpty(), "devices open" + (error.isEmpty() ? juce::String() : ": " + error));

    // Let the bridge lock, then look at what actually reaches the output device.
    juce::Thread::sleep (1500);

    for (int ch = 0; ch < 2; ++ch)
        fake->last->takeOutputPeak (ch);

    juce::Thread::sleep ((int) (seconds * 1000.0) - 1500);
    const auto left = fake->last->takeOutputPeak (0), right = fake->last->takeOutputPeak (1);
    const auto status = engine.getStatus();

    check (left > 0.4f && right > 0.4f, "input signal reaches the outputs (peak " + juce::String (left, 2) + " / " + juce::String (right, 2) + ")");
    check (rack.takeInputPeak (0) > 0.4f, "channel input meter sees the signal");

    if (status.bridged)
    {
        const auto& b = status.bridge;
        std::printf ("  [info] %s: target %.0f, fill %.0f, ratio %.6f, callbacks in %d / out %d, max gap in %.1f ms / out %.1f ms, drops %d\n",
                     adaptive ? "adaptive" : "fixed", b.targetFrames, b.fillFrames, b.ratio,
                     fake->lastInput->getCallbacks(), fake->last->getCallbacks(),
                     fake->lastInput->getMaxGapMs(), fake->last->getMaxGapMs(), b.underruns);
        // A fixed buffer is only measured; the adaptive one must learn this system's jitter quickly.
        if (adaptive)
            check (b.underruns <= 6, "bridge learns the device jitter (dropouts: " + juce::String (b.underruns) + ")");
    }

    engine.close();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testBridgeDrift (100.0, 64, 128);
    testBridgeDrift (-80.0, 256, 32);
    testBridgeDrift (30.0, 480, 512);
    testBridgeAdaptation();
    testBridgeRateConversion();
    testRouting();
    testLatencyCompensation();
    testSessionAndSnapshots();
    testEndToEnd ("Duplex", "Duplex", 3.0);
    testEndToEnd ("Dante", "USB", 8.0);
    testEndToEnd ("Dante", "USB", 8.0, false);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
