#include "PluginHost.h"

namespace bridgerack
{

PluginHost::PluginHost (juce::PropertiesFile& s)
    : settings (s)
{
    juce::addDefaultFormatsToManager (formatManager);

    if (auto xml = settings.getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);
}

PluginHost::~PluginHost()
{
    knownPlugins.removeChangeListener (this);
}

juce::File PluginHost::getDeadMansPedalFile() const
{
    return settings.getFile().getSiblingFile ("RecentlyCrashedPluginsList");
}

juce::Array<juce::PluginDescription> PluginHost::getSortedTypes() const
{
    auto types = knownPlugins.getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
    {
        if (a.manufacturerName != b.manufacturerName)
            return a.manufacturerName.compareIgnoreCase (b.manufacturerName) < 0;

        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return types;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& description,
                                                                       double sampleRate, int blockSize,
                                                                       juce::String& error)
{
    return formatManager.createPluginInstance (description, sampleRate, blockSize, error);
}

juce::String PluginHost::getFormatNames() const
{
    juce::StringArray names;

    for (auto* format : formatManager.getFormats())
        names.add (format->getName());

    return names.joinIntoString (", ");
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
    {
        settings.setValue ("knownPlugins", xml.get());
        settings.saveIfNeeded();
    }
}

} // namespace bridgerack
