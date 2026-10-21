#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace bridgerack
{

/**
    Grid patch editor: rows are rack channels, columns are device channels.
    Each rack channel connects to at most one device channel.
*/
class RoutingMatrix : public juce::Component
{
public:
    RoutingMatrix (int numRackChannels, juce::StringArray deviceChannelNames,
                   std::vector<int> map, bool isInput,
                   std::function<void (std::vector<int>)> onChange);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens the matrix in a modal-less dialog. */
    static void show (const juce::String& title, int numRackChannels, juce::StringArray deviceChannelNames,
                      std::vector<int> map, bool isInput, std::function<void (std::vector<int>)> onChange);

private:
    class Grid;

    void setMap (std::vector<int>);
    void mapSequentially (int firstDeviceChannel);

    int numRackChannels;
    juce::StringArray deviceNames;
    std::vector<int> map;
    bool isInput;
    std::function<void (std::vector<int>)> onChange;

    juce::Label startLabel;
    juce::ComboBox startCombo;
    juce::TextButton clearButton { "Clear" };
    juce::Viewport viewport;
    std::unique_ptr<Grid> grid;
};

} // namespace bridgerack
