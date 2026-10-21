#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bridgerack
{

namespace Colours
{
    const juce::Colour background   { 0xff15171b };
    const juce::Colour panel        { 0xff1d2026 };
    const juce::Colour row          { 0xff22262d };
    const juce::Colour rowAlt       { 0xff252a32 };
    const juce::Colour outline      { 0xff343a44 };
    const juce::Colour slotEmpty    { 0xff2a2f37 };
    const juce::Colour slotLoaded   { 0xff1f4b57 };
    const juce::Colour slotBypassed { 0xff57471f };
    const juce::Colour accent       { 0xff35c4d6 };
    const juce::Colour text         { 0xffe2e6ec };
    const juce::Colour textDim      { 0xff8a93a0 };
    const juce::Colour ok           { 0xff3ddc84 };
    const juce::Colour warn         { 0xffe8c547 };
    const juce::Colour bad          { 0xffe5534b };
    const juce::Colour mute         { 0xffd9534f };
}

class BridgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BridgeLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

/** Thin vertical bars, one per channel, with peak decay and colour zones. */
class MultiMeter : public juce::Component
{
public:
    void setNumChannels (int);
    void pushLevel (int channel, float peak);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> levels;
};

} // namespace bridgerack
