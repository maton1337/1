#include "LookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace bridgerack
{

juce::Colour Colours::forChannel (int index)
{
    static const juce::Colour palette[] = { juce::Colour (0xff3fa9f5), juce::Colour (0xff5ad16b), juce::Colour (0xfff0b429),
                                            juce::Colour (0xffe8743b), juce::Colour (0xffd65ac4), juce::Colour (0xff47d6c8),
                                            juce::Colour (0xffb9c2cf), juce::Colour (0xff8f7cf0) };
    return palette[(size_t) juce::jmax (0, index) % std::size (palette)];
}

//==============================================================================
namespace
{
    // (fader position, dB) breakpoints, modelled on large-format console scales.
    constexpr std::pair<double, double> law[] = {
        { 0.00, FaderLaw::minusInfinityDb }, { 0.04, -60.0 }, { 0.14, -40.0 }, { 0.25, -30.0 },
        { 0.38, -20.0 }, { 0.55, -10.0 },  { 0.65, -5.0 },  { 0.75, 0.0 },   { 0.875, 5.0 }, { 1.00, 10.0 } };
}

double FaderLaw::positionToDb (double position)
{
    position = juce::jlimit (0.0, 1.0, position);

    for (size_t i = 1; i < std::size (law); ++i)
        if (position <= law[i].first)
            return juce::jmap (position, law[i - 1].first, law[i].first, law[i - 1].second, law[i].second);

    return maxDb;
}

double FaderLaw::dbToPosition (double db)
{
    db = juce::jlimit (minusInfinityDb, maxDb, db);

    for (size_t i = 1; i < std::size (law); ++i)
        if (db <= law[i].second)
            return juce::jmap (db, law[i - 1].second, law[i].second, law[i - 1].first, law[i].first);

    return 1.0;
}

juce::String FaderLaw::format (double db)
{
    if (db <= minusInfinityDb + 0.5)
        return "-inf";

    return (db > 0.05 ? "+" : "") + juce::String (db, 1);
}

//==============================================================================
BridgeLookAndFeel::BridgeLookAndFeel()
{
    setColourScheme ({ Colours::background, Colours::panel, Colours::surface,
                       Colours::outline, Colours::text, Colours::accent,
                       Colours::background, Colours::accent, Colours::text });

    setColour (juce::TextButton::buttonColourId, Colours::surfaceLight);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::ComboBox::backgroundColourId, Colours::surfaceLight);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::DocumentWindow::textColourId, Colours::text);
    setColour (juce::ListBox::backgroundColourId, Colours::panel);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
    setColour (juce::TooltipWindow::outlineColourId, Colours::outline);
}

void BridgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                              bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = background.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);

    if (down || highlighted)
        colour = colour.brighter (down ? 0.2f : 0.1f);

    // Slight top-to-bottom gradient, like a backlit console key.
    g.setGradientFill (juce::ColourGradient (colour.brighter (0.08f), 0.0f, bounds.getY(),
                                             colour.darker (0.15f), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 2.5f, 1.0f);
}

juce::Font BridgeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (juce::jmin (12.0f, buttonHeight * 0.52f), juce::Font::bold);
}

void BridgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // LED ring around the encoder.
    constexpr int leds = 15;

    for (int i = 0; i < leds; ++i)
    {
        const auto ledAngle = startAngle + (float) i / (leds - 1) * (endAngle - startAngle);
        const auto lit = (float) i / (leds - 1) <= sliderPos + 0.001f;
        const auto p = centre.getPointOnCircumference (radius - 2.0f, ledAngle);
        g.setColour (lit ? Colours::accent : Colours::screenLine);
        g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (p));
    }

    // Knob body.
    const auto knobRadius = radius - 8.0f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f58), centre.x, knob.getY(),
                                             juce::Colour (0xff1c1e22), centre.x, knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colours::black);
    g.drawEllipse (knob, 1.2f);

    // Pointer.
    g.setColour (slider.isEnabled() ? Colours::text : Colours::textDim);
    g.drawLine (juce::Line<float> (centre.getPointOnCircumference (knobRadius * 0.25f, angle),
                                   centre.getPointOnCircumference (knobRadius * 0.9f, angle)), 2.2f);
}

void BridgeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    juce::ignoreUnused (minSliderPos, maxSliderPos);
    const auto top = (float) y, bottom = (float) (y + height);   // the slider maps its range onto exactly this span
    const auto slotX = area.getCentreX() + 6.0f;

    // dB scale to the left of the fader slot.
    g.setFont (juce::FontOptions (9.5f));

    for (auto db : { 10.0, 5.0, 0.0, -5.0, -10.0, -20.0, -30.0, -40.0, -60.0, FaderLaw::minusInfinityDb })
    {
        const auto ty = bottom - (float) FaderLaw::dbToPosition (db) * (bottom - top);
        const auto isUnity = std::abs (db) < 0.01;
        g.setColour (isUnity ? Colours::text : Colours::textDim);
        g.drawHorizontalLine ((int) ty, slotX - 12.0f, slotX - 5.0f);
        g.drawText (db <= FaderLaw::minusInfinityDb ? juce::String ("inf") : juce::String ((int) std::abs (db)),
                    juce::Rectangle<float> (area.getX(), ty - 6.0f, slotX - 14.0f - area.getX(), 12.0f),
                    juce::Justification::centredRight);
    }

    // Slot.
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (juce::Rectangle<float> (slotX - 2.5f, top, 5.0f, bottom - top), 2.5f);

    // Cap.
    const auto cap = juce::Rectangle<float> (26.0f, 34.0f).withCentre ({ slotX, sliderPos });
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (1.5f, 2.0f), 3.0f);
    g.setGradientFill (juce::ColourGradient (Colours::faderCap, cap.getX(), cap.getY(),
                                             Colours::faderCap.darker (0.55f), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);

    for (auto offset : { -9.0f, -5.0f, 5.0f, 9.0f })
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawHorizontalLine ((int) (cap.getCentreY() + offset), cap.getX() + 3.0f, cap.getRight() - 3.0f);
    }

    g.setColour (juce::Colours::black);
    g.fillRect (cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);
}

int BridgeLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    // Leaves room for the whole fader cap at both ends of travel.
    return slider.isVertical() ? 17 : LookAndFeel_V4::getSliderThumbRadius (slider);
}

//==============================================================================
void MultiMeter::setNumChannels (int n)
{
    if ((int) levels.size() != n)
    {
        levels.assign ((size_t) n, 0.0f);
        repaint();
    }
}

void MultiMeter::pushLevel (int channel, float peak)
{
    if (! juce::isPositiveAndBelow (channel, (int) levels.size()))
        return;

    auto& level = levels[(size_t) channel];
    const auto newLevel = juce::jmax (peak, level * 0.85f);

    if (std::abs (newLevel - level) > 1.0e-4f)
    {
        level = newLevel;
        repaint();
    }
}

void MultiMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (bounds, 2.0f);

    if (levels.empty())
        return;

    bounds.reduce (2.0f, 2.0f);
    const auto columnWidth = bounds.getWidth() / (float) levels.size();
    constexpr int segments = 30;
    const auto segmentHeight = bounds.getHeight() / segments;

    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto db = juce::Decibels::gainToDecibels (levels[i], -60.0f);
        const auto lit = juce::roundToInt (juce::jmap (db, -60.0f, 0.0f, 0.0f, (float) segments));

        for (int s = 0; s < segments; ++s)
        {
            const auto segmentDb = juce::jmap ((float) s, 0.0f, (float) segments, -60.0f, 0.0f);
            auto colour = segmentDb >= -3.0f ? Colours::bad : segmentDb >= -18.0f ? Colours::warn : Colours::ok;
            g.setColour (s < lit ? colour : colour.withAlpha (0.12f));
            g.fillRect (bounds.getX() + (float) i * columnWidth + 1.0f,
                        bounds.getBottom() - (float) (s + 1) * segmentHeight + 1.0f,
                        columnWidth - 2.0f, segmentHeight - 1.0f);
        }
    }
}

} // namespace bridgerack
