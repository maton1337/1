#include "LookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace bridgerack
{

BridgeLookAndFeel::BridgeLookAndFeel()
{
    setColourScheme ({ Colours::background, Colours::panel, Colours::row,
                       Colours::outline, Colours::text, Colours::accent,
                       Colours::background, Colours::accent, Colours::text });

    setColour (juce::TextButton::buttonColourId, Colours::slotEmpty);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent.withAlpha (0.8f));
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::ComboBox::backgroundColourId, Colours::slotEmpty);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.6f));
    setColour (juce::Slider::backgroundColourId, Colours::slotEmpty);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
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

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

juce::Font BridgeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (juce::jmin (14.0f, buttonHeight * 0.55f));
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
    g.setColour (Colours::background);
    g.fillRoundedRectangle (bounds, 2.0f);

    if (levels.empty())
        return;

    bounds.reduce (1.0f, 1.0f);
    const auto barWidth = bounds.getWidth() / (float) levels.size();
    const auto gap = barWidth > 3.0f ? 1.0f : 0.0f;

    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto db = juce::Decibels::gainToDecibels (levels[i], -60.0f);
        const auto proportion = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
        const auto height = bounds.getHeight() * juce::jlimit (0.0f, 1.0f, proportion);

        g.setColour (db > -0.5f ? Colours::bad : db > -12.0f ? Colours::warn : Colours::ok);
        g.fillRect (bounds.getX() + (float) i * barWidth, bounds.getBottom() - height, juce::jmax (0.5f, barWidth - gap), height);
    }
}

} // namespace bridgerack
