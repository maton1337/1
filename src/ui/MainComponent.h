#pragma once

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/Session.h"
#include "LookAndFeel.h"
#include "PluginWindows.h"
#include "RackRow.h"

namespace bridgerack
{

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    explicit MainComponent (juce::ApplicationProperties&);
    ~MainComponent() override;

    /** Saves the current session to the autosave file, restored on the next launch. */
    void saveAutosave();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    //==============================================================================
    // Racks and plugins
    void addRack (int numChannels);
    void showAddRackMenu();
    void rebuildRows();
    void refreshRows();
    RackRow* findRow (const Rack&) const;

    void showPluginChooser (Rack&, int slot);
    void loadPlugin (Rack&, int slot, const juce::PluginDescription&);
    void showSlotMenu (Rack&, int slot);
    void showRackMenu (Rack&);
    void openRouting (Rack&, bool input);
    void duplicateRack (Rack&);

    void dispose (std::unique_ptr<juce::AudioProcessor>);
    void disposeRack (std::unique_ptr<Rack>);
    SessionCallbacks makeSessionCallbacks();
    void reportErrors (const juce::StringArray&);

    //==============================================================================
    // Snapshots
    void refreshSnapshots();
    void recallSnapshot (int index);
    void storeSnapshot();
    void showSnapshotMenu();

    //==============================================================================
    // Sessions and devices
    void showSessionMenu();
    void newSession();
    void openSession();
    void saveSession (bool saveAs);
    bool loadSession (const juce::File&, bool applyDevices);
    void showDeviceSetup();
    void showPluginManager();
    juce::String openDevices (const DeviceSelection&);
    DeviceSelection defaultSelection() const;
    juce::File getAutosaveFile() const;

    void timerCallback() override;
    void updateStatus();

    static void promptForText (const juce::String& title, const juce::String& initial,
                               std::function<void (juce::String)> onOk);

    //==============================================================================
    juce::ApplicationProperties& properties;

    AudioEngine engine;
    PluginHost host;
    PluginWindowManager editors;
    SnapshotBank snapshots;
    RackRowCallbacks rowCallbacks;
    juce::File sessionFile;

    juce::TextButton sessionButton { "Session" }, devicesButton { "Audio I/O" }, pluginsButton { "Plugins" },
                     addRackButton { "+ Rack" };
    juce::ToggleButton alignButton { "Align latency" };
    juce::Label snapshotLabel;
    juce::ComboBox snapshotCombo;
    juce::TextButton previousSnapshot { "<" }, nextSnapshot { ">" }, storeButton { "Store" }, snapshotMenuButton { "..." };
    juce::Label ioLabel, statusLabel;

    juce::Viewport viewport;
    juce::Component rackContainer;
    juce::OwnedArray<RackRow> rows;

    std::unique_ptr<juce::DocumentWindow> deviceWindow, pluginListWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };
    int ticks = 0;
};

} // namespace bridgerack
