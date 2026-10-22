#include "ChannelStrip.h"
#include "PluginMenu.h"

namespace bridgerack
{

namespace
{
    const juce::String slotDragPrefix = "bridgerack-slot:";
    constexpr int optionsWidth = 16;
}

//==============================================================================
class ChannelStrip::SlotButton : public juce::Component,
                                 public juce::DragAndDropTarget,
                                 public juce::SettableTooltipClient
{
public:
    SlotButton (ChannelStrip& o, int i) : owner (o), slot (i) {}

    void paint (juce::Graphics& g) override
    {
        auto& rack = owner.rack;
        auto* processor = rack.getProcessor (slot);
        const auto bypassed = rack.isSlotBypassed (slot);
        auto bounds = getLocalBounds().toFloat().reduced (1.0f, 1.0f);

        auto fill = processor == nullptr ? Colours::slotEmpty : bypassed ? Colours::slotBypassed : Colours::slotLoaded;

        if (isMouseOver() || dragOver)
            fill = fill.brighter (0.2f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (dragOver ? Colours::accent : Colours::screenLine);
        g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

        // Options button at the right edge.
        auto options = bounds.removeFromRight ((float) optionsWidth);
        g.setColour (Colours::screenLine);
        g.drawVerticalLine ((int) options.getX(), options.getY() + 2.0f, options.getBottom() - 2.0f);
        g.setColour (Colours::textDim);
        juce::Path arrow;
        arrow.addTriangle (options.getCentreX() - 3.5f, options.getCentreY() - 2.0f,
                           options.getCentreX() + 3.5f, options.getCentreY() - 2.0f,
                           options.getCentreX(), options.getCentreY() + 2.5f);
        g.fillPath (arrow);

        bounds.removeFromLeft (4.0f);
        g.setFont (juce::FontOptions (11.0f, processor != nullptr ? juce::Font::bold : juce::Font::plain));

        if (processor == nullptr)
        {
            g.setColour (Colours::textDim.withAlpha (0.6f));
            g.drawText (juce::String (slot + 1), bounds, juce::Justification::centredLeft);
            return;
        }

        // Category colour on the left edge.
        g.setColour (PluginCategory::of (rack.getDescription (slot)).colour.withAlpha (bypassed ? 0.4f : 1.0f));
        g.fillRect (bounds.getX() - 3.0f, bounds.getY() + 2.0f, 2.5f, bounds.getHeight() - 4.0f);

        // Plugins that add latency show it right on the slot.
        if (const auto latency = processor->getLatencySamples(); latency > 0 && rack.getSampleRate() > 0.0)
        {
            const auto ms = latency * 1000.0 / rack.getSampleRate();
            g.setColour (ms >= 5.0 ? Colours::bad : Colours::warn);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            const auto text = ms >= 10.0 ? juce::String (juce::roundToInt (ms)) : juce::String (ms, 1);
            g.drawText (text, bounds.removeFromRight (22.0f), juce::Justification::centredRight);
            bounds.removeFromRight (2.0f);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        }

        g.setColour (bypassed ? Colours::textDim : Colours::text);
        g.drawText (processor->getName(), bounds, juce::Justification::centredLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging || ! getLocalBounds().contains (e.getPosition()))
        {
            dragging = false;
            return;
        }

        if (e.mods.isPopupMenu() || e.x >= getWidth() - optionsWidth - 1)
            owner.callbacks.onSlotMenu (owner.rack, slot);
        else
            owner.callbacks.onSlotClicked (owner.rack, slot);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || owner.rack.getProcessor (slot) == nullptr || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            container->startDragging (slotDragPrefix + juce::String (slot), this);
        }
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return dynamic_cast<SlotButton*> (details.sourceComponent.get()) != nullptr
            && details.description.toString().startsWith (slotDragPrefix);
    }

    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver = false; repaint(); }

    void itemDropped (const SourceDetails& details) override
    {
        dragOver = false;
        repaint();

        auto* source = dynamic_cast<SlotButton*> (details.sourceComponent.get());

        if (source == nullptr || source == this)
            return;

        const auto copy = juce::ModifierKeys::getCurrentModifiers().isAltDown();
        owner.callbacks.onMoveSlot (source->owner.rack, source->slot, owner.rack, slot, copy);
    }

private:
    ChannelStrip& owner;
    int slot;
    bool dragOver = false, dragging = false;
};

//==============================================================================
ChannelStrip::ChannelStrip (Rack& r, int i, StripCallbacks& cb)
    : rack (r), index (i), callbacks (cb)
{
    nameLabel.setEditable (false, true);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this] { rack.setName (nameLabel.getText()); };
    addAndMakeVisible (nameLabel);

    widthButton.setTooltip ("Mono / stereo channel");
    widthButton.onClick = [this] { callbacks.onChannelsChanged (rack, rack.getNumChannels() == 1 ? 2 : 1); };

    inputButton.setTooltip ("Input patch: which device inputs feed this channel");
    inputButton.onClick = [this] { callbacks.onRouting (rack, true); };
    outputButton.setTooltip ("Output patch: which device outputs this channel goes to");
    outputButton.onClick = [this] { callbacks.onRouting (rack, false); };

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, Colours::mute);
    muteButton.setColour (juce::TextButton::textColourOnId, Colours::text);
    muteButton.onClick = [this] { rack.setMuted (muteButton.getToggleState()); repaint(); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, Colours::warn);
    bypassButton.setTooltip ("Bypass every insert on this channel");
    bypassButton.onClick = [this] { rack.setBypassed (bypassButton.getToggleState()); };

    alignButton.setClickingTogglesState (true);
    alignButton.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    alignButton.setTooltip ("Latency alignment: channels with PDC on are delayed to line up with the slowest PDC channel. "
                            "Channels with PDC off keep only their own latency.");
    alignButton.onClick = [this] { rack.setAligned (alignButton.getToggleState()); };

    menuButton.setTooltip ("Channel options");
    menuButton.onClick = [this] { callbacks.onRackMenu (rack); };

    for (auto* b : { &widthButton, &inputButton, &outputButton, &muteButton, &bypassButton, &menuButton, &alignButton })
        addAndMakeVisible (b);

    gain.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gain.setRange (-24.0, 24.0, 0.5);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTooltip ("Input gain (double-click for 0 dB)");
    gain.onValueChange = [this]
    {
        rack.setInputGainDb ((float) gain.getValue());
        gainLabel.setText ("GAIN " + FaderLaw::format (gain.getValue()), juce::dontSendNotification);
    };
    addAndMakeVisible (gain);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setNormalisableRange ({ FaderLaw::minusInfinityDb, FaderLaw::maxDb,
                                  [] (double, double, double p) { return FaderLaw::positionToDb (p); },
                                  [] (double, double, double db) { return FaderLaw::dbToPosition (db); },
                                  [] (double, double, double db) { return db <= FaderLaw::minusInfinityDb + 0.5 ? FaderLaw::minusInfinityDb
                                                                                                                : std::round (db * 10.0) / 10.0; } });
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setTooltip ("Channel level (double-click for 0 dB)");
    fader.onValueChange = [this]
    {
        rack.setOutputGainDb ((float) fader.getValue());
        faderLabel.setText (FaderLaw::format (fader.getValue()), juce::dontSendNotification);
    };
    addAndMakeVisible (fader);

    for (auto* l : { &gainLabel, &faderLabel, &latencyLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (11.0f));
        l->setColour (juce::Label::textColourId, Colours::text);
        addAndMakeVisible (l);
    }

    latencyLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (meter);

    refresh();
}

ChannelStrip::~ChannelStrip() = default;

juce::String ChannelStrip::describeMap (const std::vector<int>& map)
{
    juce::StringArray parts;

    for (auto ch : map)
        parts.add (ch >= 0 ? juce::String (ch + 1) : juce::String ("-"));

    return parts.joinIntoString ("/");
}

juce::Colour ChannelStrip::getStripColour() const
{
    return rack.getColour() != 0 ? juce::Colour (rack.getColour()) : Colours::forChannel (index);
}

void ChannelStrip::refresh()
{
    nameLabel.setText (rack.getName(), juce::dontSendNotification);
    widthButton.setButtonText (rack.getNumChannels() == 1 ? "MONO" : "ST");
    inputButton.setButtonText ("IN " + describeMap (rack.getInputMap()));
    outputButton.setButtonText ("OUT " + describeMap (rack.getOutputMap()));
    muteButton.setToggleState (rack.isMuted(), juce::dontSendNotification);
    bypassButton.setToggleState (rack.isBypassed(), juce::dontSendNotification);
    alignButton.setToggleState (rack.isAligned(), juce::dontSendNotification);
    gain.setValue (rack.getInputGainDb(), juce::dontSendNotification);
    fader.setValue (rack.getOutputGainDb(), juce::dontSendNotification);
    gainLabel.setText ("GAIN " + FaderLaw::format (rack.getInputGainDb()), juce::dontSendNotification);
    faderLabel.setText (FaderLaw::format (rack.getOutputGainDb()), juce::dontSendNotification);
    meter.setNumChannels (rack.getNumChannels());

    if (slotButtons.size() != rack.getNumSlots())
    {
        slotButtons.clear();

        for (int i = 0; i < rack.getNumSlots(); ++i)
            addAndMakeVisible (slotButtons.add (new SlotButton (*this, i)));

        resized();
    }

    for (int i = 0; i < slotButtons.size(); ++i)
    {
        juce::String tip = "Click: insert a plugin";

        if (auto* p = rack.getProcessor (i))
        {
            const auto latency = p->getLatencySamples();
            const auto description = rack.getDescription (i);
            tip = description.name + " (" + description.manufacturerName + ", " + PluginCategory::of (description).name + ")"
                + "\nLatency: " + juce::String (latency) + " samples"
                + "\nClick: editor   Arrow / right-click: options"
                + "\nDrag to any channel to move it, hold Option/Alt to copy";
        }

        slotButtons[i]->setTooltip (tip);
        slotButtons[i]->repaint();
    }

    repaint();
}

void ChannelStrip::updateMeters()
{
    float inputPeak = 0.0f;

    for (int ch = 0; ch < rack.getNumChannels(); ++ch)
    {
        inputPeak = juce::jmax (inputPeak, rack.takeInputPeak (ch));
        meter.pushLevel (ch, rack.takeOutputPeak (ch));
    }

    // Input clip LED next to the gain knob, held for a moment.
    const auto newHold = inputPeak >= 0.89f ? 1.0f : inputPeakHold * 0.9f;

    if ((newHold > 0.1f) != (inputPeakHold > 0.1f))
        repaint (clipLed.expanded (2));

    inputPeakHold = newHold;

    const auto rate = rack.getSampleRate();
    const auto latency = rack.getLatencySamples() + rack.getCompensationDelay();
    const auto ms = rate > 0.0 ? latency * 1000.0 / rate : 0.0;
    latencyLabel.setText (juce::String (ms, 1) + " ms", juce::dontSendNotification);
    latencyLabel.setColour (juce::Label::textColourId, ms > 5.0 ? Colours::warn : Colours::textDim);
    latencyLabel.setTooltip ("Inserts add " + juce::String (rack.getLatencySamples()) + " samples"
                             + (rack.getCompensationDelay() > 0 ? ", plus " + juce::String (rack.getCompensationDelay())
                                                                    + " to align with other channels"
                                                                : juce::String()));
}

int ChannelStrip::getMinimumHeight() const
{
    return 36 + 26 + 76 + 16 + rack.getNumSlots() * slotHeight + 6 + 26 + 26 + 200 + 20 + 12;
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f, 0.0f);

    // Brushed-metal strip with dark gutters between channels.
    g.setGradientFill (juce::ColourGradient (Colours::surfaceLight, bounds.getX(), 0.0f,
                                             Colours::surface, bounds.getRight(), 0.0f, false));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // Scribble strip: channel colour bar + LCD.
    auto scribble = scribbleArea;
    g.setColour (Colours::screen);
    g.fillRoundedRectangle (scribble.toFloat(), 2.0f);
    g.setColour (rack.isMuted() ? Colours::mute : getStripColour());
    g.fillRect (scribble.removeFromTop (4).toFloat());
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (index + 1), scribble.reduced (4, 1), juce::Justification::topLeft);

    // Inserts "screen".
    g.setColour (Colours::screen);
    g.fillRoundedRectangle (insertsArea.toFloat(), 2.0f);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("INSERTS", insertsArea.withHeight (14).reduced (4, 0), juce::Justification::centredLeft);

    // Input clip LED.
    g.setColour (inputPeakHold > 0.1f ? Colours::bad : Colours::bad.withAlpha (0.15f));
    g.fillEllipse (clipLed.toFloat());
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (7, 6);

    scribbleArea = area.removeFromTop (34);
    nameLabel.setBounds (scribbleArea.withTrimmedTop (8).reduced (2, 0));
    area.removeFromTop (4);

    auto io = area.removeFromTop (22);
    widthButton.setBounds (io.removeFromLeft (38));
    io.removeFromLeft (3);
    inputButton.setBounds (io);
    area.removeFromTop (4);

    auto gainArea = area.removeFromTop (76);
    gainLabel.setBounds (gainArea.removeFromBottom (14));
    gain.setBounds (gainArea.withSizeKeepingCentre (62, 62));
    clipLed = juce::Rectangle<int> (7, 7).withPosition (gainArea.getRight() - 9, gainArea.getY() + 2);

    insertsArea = area.removeFromTop (16 + slotButtons.size() * slotHeight + 4);
    auto slots = insertsArea.withTrimmedTop (16).reduced (2, 0);

    for (auto* b : slotButtons)
        b->setBounds (slots.removeFromTop (slotHeight));

    area.removeFromTop (6);
    auto keys = area.removeFromTop (22);
    menuButton.setBounds (keys.removeFromRight (20));
    keys.removeFromRight (2);
    bypassButton.setBounds (keys.removeFromRight (32));
    keys.removeFromRight (3);
    muteButton.setBounds (keys);
    area.removeFromTop (4);

    outputButton.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    auto footer = area.removeFromBottom (18);
    alignButton.setBounds (footer.removeFromLeft (34));
    latencyLabel.setBounds (footer);
    faderLabel.setBounds (area.removeFromTop (16));

    auto faderArea = area;
    meter.setBounds (faderArea.removeFromRight (16).reduced (0, 12));
    faderArea.removeFromRight (2);
    fader.setBounds (faderArea);
}

} // namespace bridgerack
