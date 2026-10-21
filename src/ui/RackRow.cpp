#include "RackRow.h"

namespace bridgerack
{

namespace
{
    const juce::String slotDragPrefix = "bridgerack-slot:";
    constexpr int channelChoices[] = { 1, 2, 4, 6, 8, 10, 12, 16, 24, 32, 48, 64 };
}

//==============================================================================
class RackRow::SlotButton : public juce::Component,
                            public juce::DragAndDropTarget,
                            public juce::SettableTooltipClient
{
public:
    SlotButton (RackRow& o, int i) : owner (o), slot (i) {}

    void paint (juce::Graphics& g) override
    {
        auto& rack = owner.rack;
        auto* processor = rack.getProcessor (slot);
        const auto bypassed = rack.isSlotBypassed (slot);
        auto bounds = getLocalBounds().toFloat().reduced (2.0f, 4.0f);

        auto fill = processor == nullptr ? Colours::slotEmpty : bypassed ? Colours::slotBypassed : Colours::slotLoaded;

        if (isMouseOver() || dragOver)
            fill = fill.brighter (0.15f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (dragOver ? Colours::accent : Colours::outline);
        g.drawRoundedRectangle (bounds, 4.0f, dragOver ? 2.0f : 1.0f);

        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (slot + 1), bounds.reduced (5.0f, 3.0f), juce::Justification::topLeft);

        if (processor == nullptr)
        {
            g.drawText ("empty", bounds, juce::Justification::centred);
            return;
        }

        const auto description = rack.getDescription (slot);
        g.setColour (bypassed ? Colours::textDim : Colours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (processor->getName(), bounds.reduced (6.0f, 0.0f).toNearestInt().withTrimmedTop (8), juce::Justification::centred, 2);

        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (bypassed ? "BYPASS" : description.pluginFormatName, bounds.reduced (5.0f, 3.0f), juce::Justification::topRight);
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

        if (e.mods.isPopupMenu())
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
        auto* source = dynamic_cast<SlotButton*> (details.sourceComponent.get());
        return source != nullptr && &source->owner == &owner && details.description.toString().startsWith (slotDragPrefix);
    }

    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver = false; repaint(); }

    void itemDropped (const SourceDetails& details) override
    {
        dragOver = false;
        const auto from = details.description.toString().fromFirstOccurrenceOf (slotDragPrefix, false, false).getIntValue();

        if (from != slot)
            owner.callbacks.onMoveSlot (owner.rack, from, slot);

        repaint();
    }

private:
    RackRow& owner;
    int slot;
    bool dragOver = false, dragging = false;
};

//==============================================================================
RackRow::RackRow (Rack& r, int i, RackRowCallbacks& cb)
    : rack (r), index (i), callbacks (cb)
{
    nameLabel.setEditable (false, true);
    nameLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this] { rack.setName (nameLabel.getText()); };
    addAndMakeVisible (nameLabel);

    for (auto n : channelChoices)
        channels.addItem (n == 1 ? "Mono" : n == 2 ? "Stereo" : juce::String (n) + " ch", n);

    channels.setTooltip ("Number of channels in this rack");
    channels.onChange = [this]
    {
        if (channels.getSelectedId() > 0 && channels.getSelectedId() != rack.getNumChannels())
            callbacks.onChannelsChanged (rack, channels.getSelectedId());
    };
    addAndMakeVisible (channels);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, Colours::mute);
    muteButton.setTooltip ("Mute rack output");
    muteButton.onClick = [this] { rack.setMuted (muteButton.getToggleState()); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, Colours::warn);
    bypassButton.setTooltip ("Bypass every plugin in this rack");
    bypassButton.onClick = [this] { rack.setBypassed (bypassButton.getToggleState()); };

    menuButton.setTooltip ("Rack options");
    menuButton.onClick = [this] { callbacks.onRackMenu (rack); };

    inputButton.setTooltip ("Choose which device inputs feed this rack");
    inputButton.onClick = [this] { callbacks.onRouting (rack, true); };
    outputButton.setTooltip ("Choose which device outputs this rack sends to");
    outputButton.onClick = [this] { callbacks.onRouting (rack, false); };

    addSlotButton.setTooltip ("Add a plugin slot");
    addSlotButton.onClick = [this] { rack.setNumSlots (rack.getNumSlots() + 1); callbacks.onSlotCountChanged (rack); };
    removeSlotButton.setTooltip ("Remove the last empty slot");
    removeSlotButton.onClick = [this] { rack.setNumSlots (rack.getNumSlots() - 1); callbacks.onSlotCountChanged (rack); };

    for (auto* b : { &muteButton, &bypassButton, &menuButton, &inputButton, &outputButton, &addSlotButton, &removeSlotButton })
        addAndMakeVisible (b);

    for (auto* s : { &inputGain, &outputGain })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setRange (-60.0, 24.0, 0.1);
        s->setDoubleClickReturnValue (true, 0.0);
        s->setTextValueSuffix (" dB");
        s->setColour (juce::Slider::textBoxTextColourId, Colours::text);
        addAndMakeVisible (s);
    }

    inputGain.setTooltip ("Rack input gain (double-click for 0 dB)");
    inputGain.onValueChange = [this] { rack.setInputGainDb ((float) inputGain.getValue()); };
    outputGain.setTooltip ("Rack output gain (double-click for 0 dB)");
    outputGain.onValueChange = [this] { rack.setOutputGainDb ((float) outputGain.getValue()); };

    latencyLabel.setFont (juce::FontOptions (11.0f));
    latencyLabel.setColour (juce::Label::textColourId, Colours::textDim);
    latencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (latencyLabel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    refresh();
}

RackRow::~RackRow() = default;

juce::String RackRow::describeMap (const std::vector<int>& map, const juce::String& prefix)
{
    bool sequential = true, anyConnected = false;

    for (size_t i = 0; i < map.size(); ++i)
    {
        anyConnected = anyConnected || map[i] >= 0;
        sequential = sequential && map[i] >= 0 && map[i] == map[0] + (int) i;
    }

    if (! anyConnected)
        return prefix + " --";

    if (sequential)
        return map.size() == 1 ? prefix + " " + juce::String (map[0] + 1)
                               : prefix + " " + juce::String (map[0] + 1) + "-" + juce::String (map.back() + 1);

    return prefix + " custom";
}

void RackRow::refresh()
{
    nameLabel.setText (rack.getName(), juce::dontSendNotification);
    channels.setSelectedId (rack.getNumChannels(), juce::dontSendNotification);

    if (channels.getSelectedId() == 0)
        channels.setText (juce::String (rack.getNumChannels()) + " ch", juce::dontSendNotification);

    muteButton.setToggleState (rack.isMuted(), juce::dontSendNotification);
    bypassButton.setToggleState (rack.isBypassed(), juce::dontSendNotification);
    inputGain.setValue (rack.getInputGainDb(), juce::dontSendNotification);
    outputGain.setValue (rack.getOutputGainDb(), juce::dontSendNotification);
    inputButton.setButtonText (describeMap (rack.getInputMap(), "IN"));
    outputButton.setButtonText (describeMap (rack.getOutputMap(), "OUT"));
    inputMeter.setNumChannels (rack.getNumChannels());
    outputMeter.setNumChannels (rack.getNumChannels());

    addSlotButton.setEnabled (rack.getNumSlots() < kMaxSlots);
    removeSlotButton.setEnabled (rack.getNumSlots() > kMinSlots && rack.getProcessor (rack.getNumSlots() - 1) == nullptr);

    if (slotButtons.size() != rack.getNumSlots())
    {
        slotButtons.clear();

        for (int i = 0; i < rack.getNumSlots(); ++i)
            addAndMakeVisible (slotButtons.add (new SlotButton (*this, i)));

        resized();
    }

    for (int i = 0; i < slotButtons.size(); ++i)
    {
        auto* p = rack.getProcessor (i);
        slotButtons[i]->setTooltip (p == nullptr ? "Click to insert a plugin"
                                                 : rack.getDescription (i).descriptiveName
                                                       + "\nClick: open editor   Right-click: options   Drag: reorder");
        slotButtons[i]->repaint();
    }
}

void RackRow::updateMeters()
{
    for (int ch = 0; ch < rack.getNumChannels(); ++ch)
    {
        inputMeter.pushLevel (ch, rack.takeInputPeak (ch));
        outputMeter.pushLevel (ch, rack.takeOutputPeak (ch));
    }

    const auto latency = rack.getLatencySamples();
    const auto comp = rack.getCompensationDelay();
    latencyLabel.setText (comp > 0 ? juce::String (latency) + "+" + juce::String (comp) : juce::String (latency) + " smp",
                          juce::dontSendNotification);
    latencyLabel.setTooltip ("Plugin latency " + juce::String (latency) + " samples"
                             + (comp > 0 ? ", delayed " + juce::String (comp) + " more to align with other racks" : juce::String()));
}

void RackRow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
    g.setColour (index % 2 == 0 ? Colours::row : Colours::rowAlt);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (rack.isMuted() ? Colours::mute : Colours::accent);
    g.fillRoundedRectangle (bounds.removeFromLeft (5.0f), 2.0f);
}

void RackRow::resized()
{
    auto area = getLocalBounds().reduced (12, 6);

    auto left = area.removeFromLeft (170);
    nameLabel.setBounds (left.removeFromTop (26));
    left.removeFromTop (4);
    auto controls = left.removeFromTop (28);
    channels.setBounds (controls.removeFromLeft (78));
    controls.removeFromLeft (4);
    muteButton.setBounds (controls.removeFromLeft (26));
    controls.removeFromLeft (2);
    bypassButton.setBounds (controls.removeFromLeft (36));
    controls.removeFromLeft (2);
    menuButton.setBounds (controls);

    area.removeFromLeft (8);
    auto in = area.removeFromLeft (72);
    inputButton.setBounds (in.removeFromTop (30).reduced (0, 2));
    in.removeFromTop (4);
    inputGain.setBounds (in.removeFromTop (24));
    area.removeFromLeft (4);
    inputMeter.setBounds (area.removeFromLeft (26).reduced (0, 4));

    auto right = area.removeFromRight (64);
    latencyLabel.setBounds (right.reduced (0, 18));

    auto out = area.removeFromRight (72);
    outputButton.setBounds (out.removeFromTop (30).reduced (0, 2));
    out.removeFromTop (4);
    outputGain.setBounds (out.removeFromTop (24));
    area.removeFromRight (4);
    outputMeter.setBounds (area.removeFromRight (26).reduced (0, 4));
    area.removeFromRight (4);

    auto slotEdit = area.removeFromRight (22);
    addSlotButton.setBounds (slotEdit.removeFromTop (slotEdit.getHeight() / 2).reduced (0, 4));
    removeSlotButton.setBounds (slotEdit.reduced (0, 4));
    area.removeFromRight (4);

    if (slotButtons.isEmpty())
        return;

    const auto slotWidth = area.getWidth() / slotButtons.size();

    for (auto* b : slotButtons)
        b->setBounds (area.removeFromLeft (slotWidth));
}

} // namespace bridgerack
