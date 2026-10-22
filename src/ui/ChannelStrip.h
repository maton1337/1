#pragma once

#include "engine/Rack.h"
#include "LookAndFeel.h"

namespace bridgerack
{

/** What a channel strip asks its owner to do; the owner has the plugin host, devices and windows. */
struct StripCallbacks
{
    std::function<void (Rack&, int slot)> onSlotClicked;
    std::function<void (Rack&, int slot)> onSlotMenu;
    /** A plugin was dragged onto a slot, possibly on another channel. copy = Option/Alt held. */
    std::function<void (Rack& from, int fromSlot, Rack& to, int toSlot, bool copy)> onMoveSlot;
    std::function<void (Rack&, bool input)> onRouting;
    std::function<void (Rack&, int numChannels)> onChannelsChanged;
    std::function<void (Rack&)> onRackMenu;
};

/**
    One rack drawn as a console channel strip, top to bottom:
    name, input patch, gain, inserts, mute/bypass, output patch, fader with meter.
*/
class ChannelStrip : public juce::Component
{
public:
    static constexpr int width = 112;
    static constexpr int slotHeight = 20;

    ChannelStrip (Rack&, int index, StripCallbacks&);
    ~ChannelStrip() override;

    Rack& getRack() noexcept     { return rack; }

    /** Re-reads names, plugins and routing from the rack. */
    void refresh();

    /** Called at the meter refresh rate. */
    void updateMeters();

    /** Height the strip needs to show every slot with a usable fader. */
    int getMinimumHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SlotButton;

    static juce::String describeMap (const std::vector<int>&);
    juce::Colour getStripColour() const;

    Rack& rack;
    int index;
    StripCallbacks& callbacks;

    juce::Label nameLabel, gainLabel, faderLabel, latencyLabel;
    juce::TextButton widthButton, inputButton, outputButton;
    juce::TextButton muteButton { "MUTE" }, bypassButton { "BYP" }, menuButton { "..." }, alignButton { "PDC" };
    juce::Slider gain, fader;
    MultiMeter meter;
    juce::OwnedArray<SlotButton> slotButtons;

    juce::Rectangle<int> scribbleArea, insertsArea, clipLed;
    float inputPeakHold = 0.0f;
};

} // namespace bridgerack
