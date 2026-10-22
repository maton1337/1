#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace bridgerack
{

/** Which shelf a plugin belongs on in the insert menu, and its colour on the insert slot. */
struct PluginCategory
{
    juce::String name;
    juce::Colour colour;

    static PluginCategory of (const juce::PluginDescription&);
    static const juce::Array<PluginCategory>& all();
};

/**
    The insert menu: recently used plugins first, then one submenu per category
    (EQ, Dynamics, Reverb, ...), then everything by manufacturer.
*/
juce::PopupMenu buildPluginMenu (const juce::Array<juce::PluginDescription>& types,
                                 const juce::Array<juce::PluginDescription>& recent,
                                 std::function<void (const juce::PluginDescription&)> onChosen,
                                 std::function<void()> onOpenManager);

} // namespace bridgerack
