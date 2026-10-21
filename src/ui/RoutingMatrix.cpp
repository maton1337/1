#include "RoutingMatrix.h"
#include "LookAndFeel.h"

namespace bridgerack
{

namespace
{
    constexpr int cell = 20;
    constexpr int rowHeader = 64;
    constexpr int columnHeader = 28;
}

class RoutingMatrix::Grid : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit Grid (RoutingMatrix& o) : owner (o)
    {
        setSize (rowHeader + owner.deviceNames.size() * cell + 1, columnHeader + owner.numRackChannels * cell + 1);
    }

    void paint (juce::Graphics& g) override
    {
        const auto cols = owner.deviceNames.size();
        const auto rows = owner.numRackChannels;

        g.setFont (juce::FontOptions (11.0f));

        for (int c = 0; c < cols; ++c)
        {
            g.setColour (c == hoverColumn ? Colours::accent : Colours::textDim);
            g.drawText (juce::String (c + 1), rowHeader + c * cell, 0, cell, columnHeader, juce::Justification::centredBottom);
        }

        for (int r = 0; r < rows; ++r)
        {
            g.setColour (r == hoverRow ? Colours::accent : Colours::textDim);
            g.drawText ("Ch " + juce::String (r + 1), 0, columnHeader + r * cell, rowHeader - 6, cell, juce::Justification::centredRight);

            for (int c = 0; c < cols; ++c)
            {
                const juce::Rectangle<int> area (rowHeader + c * cell, columnHeader + r * cell, cell, cell);
                g.setColour ((r == hoverRow || c == hoverColumn) ? Colours::rowAlt : ((r + c) % 2 == 0 ? Colours::row : Colours::panel));
                g.fillRect (area);
                g.setColour (Colours::outline);
                g.drawRect (area);

                if (owner.map[(size_t) r] == c)
                {
                    g.setColour (Colours::accent);
                    g.fillEllipse (area.reduced (5).toFloat());
                }
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto [r, c] = cellAt (e.getPosition());

        if (r != hoverRow || c != hoverColumn)
        {
            hoverRow = r;
            hoverColumn = c;
            setTooltip (juce::isPositiveAndBelow (c, owner.deviceNames.size()) ? owner.deviceNames[c] : juce::String());
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverRow = hoverColumn = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto [r, c] = cellAt (e.getPosition());

        if (! juce::isPositiveAndBelow (r, owner.numRackChannels) || ! juce::isPositiveAndBelow (c, owner.deviceNames.size()))
            return;

        auto map = owner.map;
        map[(size_t) r] = map[(size_t) r] == c ? -1 : c;
        owner.setMap (std::move (map));
    }

private:
    std::pair<int, int> cellAt (juce::Point<int> p) const
    {
        if (p.x < rowHeader || p.y < columnHeader)
            return { -1, -1 };

        return { (p.y - columnHeader) / cell, (p.x - rowHeader) / cell };
    }

    RoutingMatrix& owner;
    int hoverRow = -1, hoverColumn = -1;
};

//==============================================================================
RoutingMatrix::RoutingMatrix (int rackChannels, juce::StringArray names, std::vector<int> m, bool input,
                              std::function<void (std::vector<int>)> callback)
    : numRackChannels (rackChannels), deviceNames (std::move (names)), map (std::move (m)),
      isInput (input), onChange (std::move (callback))
{
    // With no device open, still allow patching to the first 64 channels.
    if (deviceNames.isEmpty())
        for (int i = 0; i < 64; ++i)
            deviceNames.add ((isInput ? "Input " : "Output ") + juce::String (i + 1));

    map.resize ((size_t) numRackChannels, -1);

    startLabel.setText ("Patch sequentially from:", juce::dontSendNotification);
    addAndMakeVisible (startLabel);

    for (int i = 0; i < deviceNames.size(); ++i)
        startCombo.addItem (juce::String (i + 1) + "  " + deviceNames[i], i + 1);

    startCombo.setTextWhenNothingSelected ("choose...");
    startCombo.onChange = [this] { mapSequentially (startCombo.getSelectedId() - 1); };
    addAndMakeVisible (startCombo);

    clearButton.onClick = [this] { setMap (std::vector<int> ((size_t) numRackChannels, -1)); };
    addAndMakeVisible (clearButton);

    grid = std::make_unique<Grid> (*this);
    viewport.setViewedComponent (grid.get(), false);
    addAndMakeVisible (viewport);

    setSize (juce::jlimit (420, 1200, grid->getWidth() + 30), juce::jlimit (240, 800, grid->getHeight() + 70));
}

void RoutingMatrix::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void RoutingMatrix::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto top = area.removeFromTop (28);
    startLabel.setBounds (top.removeFromLeft (170));
    clearButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (8);
    startCombo.setBounds (top);
    area.removeFromTop (8);
    viewport.setBounds (area);
}

void RoutingMatrix::setMap (std::vector<int> newMap)
{
    map = std::move (newMap);
    grid->repaint();

    if (onChange)
        onChange (map);
}

void RoutingMatrix::mapSequentially (int first)
{
    if (first < 0)
        return;

    std::vector<int> m ((size_t) numRackChannels);

    for (int i = 0; i < numRackChannels; ++i)
        m[(size_t) i] = first + i < deviceNames.size() ? first + i : -1;

    setMap (std::move (m));
}

void RoutingMatrix::show (const juce::String& title, int rackChannels, juce::StringArray names,
                          std::vector<int> m, bool input, std::function<void (std::vector<int>)> callback)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.content.setOwned (new RoutingMatrix (rackChannels, std::move (names), std::move (m), input, std::move (callback)));
    options.dialogBackgroundColour = Colours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

} // namespace bridgerack
