#pragma once

#include "engine/AudioEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace bridgerack
{

/** Pick the input device, the output device, and their sample rates/buffer sizes independently. */
class DeviceSetupPanel : public juce::Component
{
public:
    DeviceSetupPanel (AudioEngine&, std::function<juce::String (const DeviceSelection&)> onApply);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Side
    {
        juce::Label title, typeLabel, deviceLabel, rateLabel, blockLabel;
        juce::ComboBox type, device, rate, block;
        bool isInput = false;
    };

    void setUpSide (Side&, bool isInput, const juce::String& title);
    void refreshDevices (Side&, const juce::String& preferred);
    void refreshFormats (Side&, double preferredRate, int preferredBlock);
    void updateSharedState();
    void layoutSide (Side&, juce::Rectangle<int>);
    DeviceSelection getSelection() const;

    AudioEngine& engine;
    std::function<juce::String (const DeviceSelection&)> onApply;

    Side input, output;
    juce::Label safetyLabel, info, status;
    juce::ComboBox safety;
    juce::ToggleButton adaptive { "Adaptive buffer (learns driver jitter)" };
    juce::TextButton applyButton { "Apply" };
};

} // namespace bridgerack
