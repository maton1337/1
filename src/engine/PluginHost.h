#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace bridgerack
{

/**
    Plugin formats, the scanned plugin list and instantiation.

    Supported formats depend on the build: VST3 everywhere, Audio Units on
    macOS, LV2 on Linux, and VST2 only if the project was configured with the
    legacy VST2 SDK. AAX cannot be hosted: Avid only allows AAX plugins to run
    inside Avid-approved hosts (Pro Tools, VENUE) and does not license a hosting SDK.
*/
class PluginHost : private juce::ChangeListener
{
public:
    explicit PluginHost (juce::PropertiesFile& settings);
    ~PluginHost() override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept  { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept           { return knownPlugins; }
    juce::File getDeadMansPedalFile() const;

    /** Plugins sorted for menus. */
    juce::Array<juce::PluginDescription> getSortedTypes() const;

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&,
                                                               double sampleRate, int blockSize,
                                                               juce::String& error);

    /** e.g. "VST3, AudioUnit" */
    juce::String getFormatNames() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::PropertiesFile& settings;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
};

} // namespace bridgerack
