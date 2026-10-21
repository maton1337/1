#pragma once

#include "engine/Rack.h"
#include "LookAndFeel.h"

namespace bridgerack
{

/** What a rack row asks its owner to do; the owner has the plugin host, devices and windows. */
struct RackRowCallbacks
{
    std::function<void (Rack&, int slot)> onSlotClicked;
    std::function<void (Rack&, int slot)> onSlotMenu;
    std::function<void (Rack&, int from, int to)> onMoveSlot;
    std::function<void (Rack&, bool input)> onRouting;
    std::function<void (Rack&, int numChannels)> onChannelsChanged;
    std::function<void (Rack&)> onSlotCountChanged;
    std::function<void (Rack&)> onRemove;
    std::function<void (Rack&)> onRackMenu;
};

/** One horizontal rack strip: name, channels, input patch, plugin slots, output patch, gains and meters. */
class RackRow : public juce::Component
{
public:
    static constexpr int height = 78;

    RackRow (Rack&, int index, RackRowCallbacks&);
    ~RackRow() override;

    Rack& getRack() noexcept     { return rack; }

    /** Re-reads names, plugins and routing from the rack. */
    void refresh();

    /** Called at the meter refresh rate. */
    void updateMeters();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SlotButton;

    static juce::String describeMap (const std::vector<int>&, const juce::String& prefix);

    Rack& rack;
    int index;
    RackRowCallbacks& callbacks;

    juce::Label nameLabel;
    juce::ComboBox channels;
    juce::TextButton muteButton { "M" }, bypassButton { "BYP" }, menuButton { "..." };
    juce::TextButton inputButton, outputButton;
    juce::TextButton addSlotButton { "+" }, removeSlotButton { "-" };
    juce::Slider inputGain, outputGain;
    juce::Label latencyLabel;
    MultiMeter inputMeter, outputMeter;
    juce::OwnedArray<SlotButton> slotButtons;
};

} // namespace bridgerack
