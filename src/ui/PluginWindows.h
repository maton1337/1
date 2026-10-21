#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace bridgerack
{

/** Keeps one editor window per plugin instance and closes it before the plugin is destroyed. */
class PluginWindowManager
{
public:
    PluginWindowManager();
    ~PluginWindowManager();

    void show (juce::AudioProcessor&, const juce::String& title);
    void closeFor (const juce::AudioProcessor*);
    void closeAll();

private:
    class Window;
    juce::OwnedArray<Window> windows;
};

} // namespace bridgerack
