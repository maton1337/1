// BridgeRackProbe: loads installed plugins the same way BridgeRack does and reports,
// for each one, the latency it declares, the delay actually measured with an impulse,
// and how much of the audio deadline it uses.
//
//   BridgeRackProbe "F6" "eMo D5" "Stabilizer"         (name fragments, case-insensitive)
//   BridgeRackProbe --rate 48000 --block 128 "Ozone"

#include "engine/Rack.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>

namespace
{
struct Result
{
    juce::String name, format;
    int reported = 0, measured = -1;
    double cpuPercent = 0.0;
    juce::String error;
};

Result probe (juce::AudioPluginFormatManager& formats, const juce::PluginDescription& d, double rate, int block)
{
    Result r;
    r.name = d.name + " (" + d.manufacturerName + ")";
    r.format = d.pluginFormatName;
    auto plugin = formats.createPluginInstance (d, rate, block, r.error);

    if (plugin == nullptr)
        return r;

    // Same bus setup BridgeRack uses for a stereo channel.
    auto layout = plugin->getBusesLayout();

    for (int i = 0; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();

    for (int i = 0; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();

    if (plugin->checkBusesLayoutSupported (layout))
        plugin->setBusesLayout (layout);

    plugin->setRateAndBufferSizeDetails (rate, block);
    plugin->prepareToPlay (rate, block);

    const auto channels = juce::jmax (2, juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;

    // Let the plugin settle (some only report latency after the first blocks).
    for (int i = 0; i < 20; ++i)
    {
        buffer.clear();
        plugin->processBlock (buffer, midi);
    }

    // VST3 plugins announce latency changes asynchronously on the message thread.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    r.reported = plugin->getLatencySamples();

    // Impulse, then find where the strongest response comes out.
    const auto totalBlocks = (int) std::ceil (rate * 1.0 / block);
    float peak = 0.0f;
    double processingSeconds = 0.0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        buffer.clear();

        if (b == 0)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, 0, 0.5f);

        const auto start = juce::Time::getMillisecondCounterHiRes();
        plugin->processBlock (buffer, midi);
        processingSeconds += (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

        for (int i = 0; i < block; ++i)
        {
            const auto v = std::abs (buffer.getSample (0, i));

            if (v > peak * 1.05f && v > 1.0e-4f)
            {
                peak = v;
                r.measured = b * block + i;
            }
        }
    }

    r.cpuPercent = processingSeconds / (totalBlocks * block / rate) * 100.0;
    plugin->releaseResources();
    return r;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    double rate = 48000.0;
    int block = 256;
    juce::StringArray queries;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--rate" && i + 1 < argc)        rate = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--block" && i + 1 < argc)  block = juce::String (argv[++i]).getIntValue();
        else                                        queries.add (arg);
    }

    if (queries.isEmpty())
    {
        std::printf ("usage: BridgeRackProbe [--rate 48000] [--block 256] \"plugin name\" ...\n");
        return 1;
    }

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    std::printf ("Searching %s plugins for: %s  (%.0f Hz, %d-sample blocks)\n\n",
                 formats.getFormats().size() > 0 ? "installed" : "no", queries.joinIntoString (", ").toRawUTF8(), rate, block);
    std::printf ("%-44s %-10s %12s %12s %8s\n", "Plugin", "Format", "Declared", "Measured", "CPU");

    // Prefer the list BridgeRack already scanned; otherwise scan the default folders.
    juce::PropertiesFile::Options options;
    options.applicationName     = "BridgeRack";
    options.filenameSuffix      = "settings";
    options.folderName          = "BridgeRack";
    options.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile settings (options);
    juce::KnownPluginList known;

    if (auto xml = settings.getXmlValue ("knownPlugins"))
        known.recreateFromXml (*xml);

    juce::Array<juce::PluginDescription> candidates = known.getTypes();

    if (candidates.isEmpty())
    {
        std::printf ("(BridgeRack has not scanned plugins yet - scanning default folders, this can take a while)\n");

        for (auto* format : formats.getFormats())
        {
            for (auto& file : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
            {
                juce::OwnedArray<juce::PluginDescription> types;
                format->findAllTypesForFile (types, file);

                for (auto* d : types)
                    candidates.add (*d);
            }
        }
    }

    int found = 0;
    juce::Array<juce::PluginDescription> chain;

    for (auto& d : candidates)
    {
        bool matches = false;

        for (auto& q : queries)
            matches = matches || d.name.containsIgnoreCase (q);

        if (! matches || d.isInstrument)
            continue;

        ++found;
        const auto r = probe (formats, d, rate, block);

        if (r.error.isEmpty() && r.measured >= 0 && chain.size() < bridgerack::kMaxSlots)
            chain.add (d);

        if (r.error.isNotEmpty())
        {
            std::printf ("%-44s %-10s  could not load: %s\n", r.name.substring (0, 44).toRawUTF8(),
                         r.format.toRawUTF8(), r.error.toRawUTF8());
            continue;
        }

        const auto toMs = [rate] (int samples) { return samples * 1000.0 / rate; };
        std::printf ("%-44s %-10s %5d %5.1fms ", r.name.substring (0, 44).toRawUTF8(), r.format.toRawUTF8(),
                     r.reported, toMs (r.reported));

        if (r.measured >= 0)
            std::printf ("%5d %5.1fms", r.measured, toMs (r.measured));
        else
            std::printf ("%12s", "no output");

        std::printf (" %7.1f%%\n", r.cpuPercent);
    }

    // The same plugins in series on one BridgeRack channel, end to end through the engine.
    if (chain.size() > 1)
    {
        bridgerack::Rack rack ("Probe", 2, juce::jmax (bridgerack::kMinSlots, chain.size()));
        rack.prepare (rate, block);
        int declaredSum = 0;

        for (int i = 0; i < chain.size(); ++i)
        {
            juce::String error;

            if (auto instance = formats.createPluginInstance (chain[i], rate, block, error))
                rack.setProcessor (i, std::move (instance), chain[i]);
        }

        juce::AudioBuffer<float> in (2, block), out (2, block);
        int measured = -1;
        float peak = 0.0f;

        for (int b = 0; b < (int) std::ceil (rate * 1.5 / block); ++b)
        {
            // Run a few silent blocks first so latency reports settle, then send the impulse.
            if (b == 20)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

                for (int i = 0; i < chain.size(); ++i)
                    if (auto* p = rack.getProcessor (i))
                        declaredSum += p->getLatencySamples();
            }

            in.clear();

            if (b == 20)
                for (int ch = 0; ch < 2; ++ch)
                    in.setSample (ch, 0, 0.5f);

            out.clear();
            rack.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, block);

            for (int i = 0; b >= 20 && i < block; ++i)
            {
                const auto v = std::abs (out.getSample (0, i));

                if (v > peak * 1.05f && v > 1.0e-4f)
                {
                    peak = v;
                    measured = (b - 20) * block + i;
                }
            }
        }

        std::printf ("\nAll %d in series on one BridgeRack channel: declared %d samples (%.1f ms), measured %d samples (%.1f ms)\n",
                     chain.size(), declaredSum, declaredSum * 1000.0 / rate, measured, measured * 1000.0 / rate);
    }

    if (found == 0)
        std::printf ("No matching plugins found.\n");

    std::printf ("\nDeclared = latency the plugin reports (what BridgeRack compensates).\n"
                 "Measured = where an impulse comes out; for EQs and compressors this matches the real delay.\n"
                 "CPU      = share of the real-time budget one instance uses at this block size.\n");
    return 0;
}
