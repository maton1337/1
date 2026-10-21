#include "DeviceSetupPanel.h"
#include "LookAndFeel.h"

namespace bridgerack
{

namespace
{
    constexpr int noneId = 1;   // "<none>" entry in the input device list

    std::unique_ptr<juce::AudioIODevice> probe (juce::AudioIODeviceType* type, const juce::String& name, bool isInput)
    {
        if (type == nullptr || name.isEmpty())
            return {};

        return std::unique_ptr<juce::AudioIODevice> (isInput ? type->createDevice ({}, name) : type->createDevice (name, {}));
    }
}

DeviceSetupPanel::DeviceSetupPanel (AudioEngine& e, std::function<juce::String (const DeviceSelection&)> apply)
    : engine (e), onApply (std::move (apply))
{
    setUpSide (input, true, "INPUT  (receive)");
    setUpSide (output, false, "OUTPUT  (send, master clock)");

    const auto& sel = engine.getSelection();

    for (auto* side : { &input, &output })
    {
        auto& s = *side;
        const auto& typeName = s.isInput ? sel.inputType : sel.outputType;
        int index = 0;

        for (auto* t : engine.getDeviceTypes())
        {
            s.type.addItem (t->getTypeName(), ++index);

            if (t->getTypeName() == typeName)
                s.type.setSelectedId (index, juce::dontSendNotification);
        }

        if (s.type.getSelectedId() == 0)
            s.type.setSelectedId (1, juce::dontSendNotification);

        refreshDevices (s, s.isInput ? sel.inputDevice : sel.outputDevice);
        refreshFormats (s, s.isInput ? sel.inputSampleRate : sel.outputSampleRate,
                           s.isInput ? sel.inputBlockSize : sel.outputBlockSize);
    }

    safetyLabel.setText ("Bridge safety buffer", juce::dontSendNotification);
    addAndMakeVisible (safetyLabel);

    for (auto frames : { 16, 32, 64, 128, 256, 512, 1024, 2048 })
        safety.addItem (juce::String (frames) + " samples", frames);

    safety.setSelectedId (sel.bridgeSafetyFrames, juce::dontSendNotification);

    if (safety.getSelectedId() == 0)
        safety.setSelectedId (64, juce::dontSendNotification);

    addAndMakeVisible (safety);

    info.setText ("Input and output can be different devices and even different drivers, "
                  "e.g. receive from Dante Virtual Soundcard and send to a USB interface. "
                  "The output device is the master clock; input is resampled adaptively to follow it. "
                  "Raise the safety buffer if you hear dropouts.",
                  juce::dontSendNotification);
    info.setColour (juce::Label::textColourId, Colours::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);

    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    applyButton.setColour (juce::TextButton::buttonColourId, Colours::accent.withAlpha (0.7f));
    applyButton.onClick = [this]
    {
        const auto error = onApply ? onApply (getSelection()) : juce::String();
        status.setColour (juce::Label::textColourId, error.isEmpty() ? Colours::ok : Colours::bad);
        status.setText (error.isEmpty() ? "Running" : error, juce::dontSendNotification);
    };
    addAndMakeVisible (applyButton);

    updateSharedState();
    setSize (640, 380);
}

void DeviceSetupPanel::setUpSide (Side& s, bool isInput, const juce::String& title)
{
    s.isInput = isInput;
    s.title.setText (title, juce::dontSendNotification);
    s.title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    s.title.setColour (juce::Label::textColourId, Colours::accent);
    s.typeLabel.setText ("Driver", juce::dontSendNotification);
    s.deviceLabel.setText ("Device", juce::dontSendNotification);
    s.rateLabel.setText ("Sample rate", juce::dontSendNotification);
    s.blockLabel.setText ("Buffer size", juce::dontSendNotification);

    for (auto* c : { &s.title, &s.typeLabel, &s.deviceLabel, &s.rateLabel, &s.blockLabel })
        addAndMakeVisible (c);

    for (auto* c : { &s.type, &s.device, &s.rate, &s.block })
        addAndMakeVisible (c);

    s.type.onChange = [this, &s]
    {
        refreshDevices (s, {});
        refreshFormats (s, 48000.0, 256);
        updateSharedState();
    };

    s.device.onChange = [this, &s]
    {
        refreshFormats (s, s.rate.getSelectedId() > 0 ? (double) s.rate.getSelectedId() : 48000.0,
                           s.block.getSelectedId() > 0 ? s.block.getSelectedId() : 256);
        updateSharedState();
    };
}

void DeviceSetupPanel::refreshDevices (Side& s, const juce::String& preferred)
{
    s.device.clear (juce::dontSendNotification);
    auto* type = engine.findType (s.type.getText());

    if (type == nullptr)
        return;

    type->scanForDevices();
    const auto names = type->getDeviceNames (s.isInput);

    if (s.isInput)
        s.device.addItem ("<none>", noneId);

    for (int i = 0; i < names.size(); ++i)
        s.device.addItem (names[i], i + 2);

    const auto preferredIndex = names.indexOf (preferred);

    if (preferredIndex >= 0)
        s.device.setSelectedId (preferredIndex + 2, juce::dontSendNotification);
    else if (! names.isEmpty())
        s.device.setSelectedId (juce::jmax (0, type->getDefaultDeviceIndex (s.isInput)) + 2, juce::dontSendNotification);
    else if (s.isInput)
        s.device.setSelectedId (noneId, juce::dontSendNotification);
}

void DeviceSetupPanel::refreshFormats (Side& s, double preferredRate, int preferredBlock)
{
    s.rate.clear (juce::dontSendNotification);
    s.block.clear (juce::dontSendNotification);

    const auto name = s.device.getSelectedId() == noneId && s.isInput ? juce::String() : s.device.getText();
    auto device = probe (engine.findType (s.type.getText()), name, s.isInput);

    if (device == nullptr)
        return;

    for (auto rate : device->getAvailableSampleRates())
        s.rate.addItem (juce::String (rate / 1000.0, std::fmod (rate, 1000.0) < 1.0 ? 0 : 1) + " kHz", (int) rate);

    for (auto size : device->getAvailableBufferSizes())
        s.block.addItem (juce::String (size) + " samples", size);

    s.rate.setSelectedId ((int) preferredRate, juce::dontSendNotification);

    if (s.rate.getSelectedId() == 0)
        s.rate.setSelectedId ((int) device->getCurrentSampleRate() > 0 ? (int) device->getCurrentSampleRate() : 48000,
                              juce::dontSendNotification);

    if (s.rate.getSelectedId() == 0 && s.rate.getNumItems() > 0)
        s.rate.setSelectedItemIndex (0, juce::dontSendNotification);

    s.block.setSelectedId (preferredBlock, juce::dontSendNotification);

    if (s.block.getSelectedId() == 0)
        s.block.setSelectedId (device->getDefaultBufferSize(), juce::dontSendNotification);

    if (s.block.getSelectedId() == 0 && s.block.getNumItems() > 0)
        s.block.setSelectedItemIndex (0, juce::dontSendNotification);
}

void DeviceSetupPanel::updateSharedState()
{
    // The same device for both sides runs as one duplex device on the output's settings.
    const auto shared = getSelection().usesSingleDevice();
    input.rate.setEnabled (! shared);
    input.block.setEnabled (! shared);
    safety.setEnabled (! shared && input.device.getSelectedId() != noneId);
}

DeviceSelection DeviceSetupPanel::getSelection() const
{
    DeviceSelection s;
    s.inputType   = input.type.getText();
    s.inputDevice = input.device.getSelectedId() == noneId ? juce::String() : input.device.getText();
    s.outputType   = output.type.getText();
    s.outputDevice = output.device.getText();
    s.inputSampleRate  = input.rate.getSelectedId() > 0 ? input.rate.getSelectedId() : 48000;
    s.outputSampleRate = output.rate.getSelectedId() > 0 ? output.rate.getSelectedId() : 48000;
    s.inputBlockSize   = input.block.getSelectedId() > 0 ? input.block.getSelectedId() : 256;
    s.outputBlockSize  = output.block.getSelectedId() > 0 ? output.block.getSelectedId() : 256;
    s.bridgeSafetyFrames = safety.getSelectedId() > 0 ? safety.getSelectedId() : 64;
    return s;
}

void DeviceSetupPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (170);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (top.removeFromLeft (top.getWidth() / 2 - 5).toFloat(), 4.0f);
    top.removeFromLeft (10);
    g.fillRoundedRectangle (top.toFloat(), 4.0f);
}

void DeviceSetupPanel::layoutSide (Side& s, juce::Rectangle<int> area)
{
    area.reduce (10, 8);
    s.title.setBounds (area.removeFromTop (24));

    for (auto [label, combo] : { std::pair { &s.typeLabel, &s.type }, { &s.deviceLabel, &s.device },
                                 { &s.rateLabel, &s.rate }, { &s.blockLabel, &s.block } })
    {
        auto line = area.removeFromTop (30);
        label->setBounds (line.removeFromLeft (84));
        combo->setBounds (line.reduced (0, 3));
    }
}

void DeviceSetupPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (170);
    layoutSide (input, top.removeFromLeft (top.getWidth() / 2 - 5));
    top.removeFromLeft (10);
    layoutSide (output, top);

    area.removeFromTop (10);
    auto line = area.removeFromTop (28);
    safetyLabel.setBounds (line.removeFromLeft (150));
    safety.setBounds (line.removeFromLeft (150).reduced (0, 2));

    area.removeFromTop (6);
    auto bottom = area.removeFromBottom (32);
    applyButton.setBounds (bottom.removeFromRight (110));
    status.setBounds (bottom);
    info.setBounds (area);
}

} // namespace bridgerack
