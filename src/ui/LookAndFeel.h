#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bridgerack
{

/** Large-format console palette: charcoal surface, dark blue "screen" areas, white legends. */
namespace Colours
{
    const juce::Colour background   { 0xff0e0f11 };
    const juce::Colour panel        { 0xff1b1d21 };
    const juce::Colour surface      { 0xff2a2d33 };   // channel strip metal
    const juce::Colour surfaceLight { 0xff353941 };
    const juce::Colour screen       { 0xff0b1624 };   // insert / scribble "glass"
    const juce::Colour screenLine   { 0xff1d3550 };
    const juce::Colour row          { 0xff22262d };
    const juce::Colour rowAlt       { 0xff252a32 };
    const juce::Colour outline      { 0xff3d424b };
    const juce::Colour slotEmpty    { 0xff111d2c };
    const juce::Colour slotLoaded   { 0xff1c4f7a };
    const juce::Colour slotBypassed { 0xff5a4a1c };
    const juce::Colour accent       { 0xff3fa9f5 };
    const juce::Colour text         { 0xffe8ebef };
    const juce::Colour textDim      { 0xff8a919c };
    const juce::Colour ok           { 0xff3ddc84 };
    const juce::Colour warn         { 0xfff0b429 };
    const juce::Colour bad          { 0xffe5534b };
    const juce::Colour mute         { 0xffe03c31 };
    const juce::Colour faderCap     { 0xffc9ccd1 };

    /** Default scribble-strip colours, cycled per channel. */
    juce::Colour forChannel (int index);
}

/** Console fader law: position 0..1 <-> gain in dB (-inf .. +10 dB, 0 dB at 75 % travel). */
namespace FaderLaw
{
    constexpr double minusInfinityDb = -100.0;
    constexpr double maxDb = 10.0;

    double positionToDb (double position);
    double dbToPosition (double db);
    juce::String format (double db);
}

class BridgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BridgeLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    int getSliderThumbRadius (juce::Slider&) override;
};

/** Segmented LED meter, one column per channel, with peak decay. */
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
