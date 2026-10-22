#include "MainComponent.h"
#include "DeviceSetupPanel.h"
#include "PluginMenu.h"
#include "RoutingMatrix.h"

namespace bridgerack
{

namespace
{
    /** A window that hides instead of closing, for tool panels. */
    class ToolWindow : public juce::DocumentWindow
    {
    public:
        ToolWindow (const juce::String& title, juce::Component* content)
            : DocumentWindow (title, Colours::background, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { setVisible (false); }
    };

    juce::String formatPpm (double ppm)
    {
        return (ppm >= 0.0 ? "+" : "") + juce::String (ppm, 1) + " ppm";
    }
}

//==============================================================================
MainComponent::MainComponent (juce::ApplicationProperties& p)
    : properties (p), host (*p.getUserSettings())
{
    rowCallbacks.onSlotClicked = [this] (Rack& rack, int slot)
    {
        if (auto* processor = rack.getProcessor (slot))
            editors.show (*processor, rack.getName() + " / " + processor->getName());
        else
            showPluginChooser (rack, slot);
    };

    rowCallbacks.onSlotMenu = [this] (Rack& rack, int slot) { showSlotMenu (rack, slot); };
    rowCallbacks.onMoveSlot = [this] (Rack& from, int fromSlot, Rack& to, int toSlot, bool copy)
    {
        moveSlot (from, fromSlot, to, toSlot, copy);
    };
    rowCallbacks.onRouting = [this] (Rack& rack, bool input) { openRouting (rack, input); };
    rowCallbacks.onChannelsChanged = [this] (Rack& rack, int n) { rack.setNumChannels (n); refreshRows(); };
    rowCallbacks.onRackMenu = [this] (Rack& rack) { showRackMenu (rack); };

    sessionButton.onClick = [this] { showSessionMenu(); };
    devicesButton.onClick = [this] { showDeviceSetup(); };
    pluginsButton.onClick = [this] { showPluginManager(); };
    addRackButton.onClick = [this] { showAddRackMenu(); };

    sessionButton.setTooltip ("New, open and save sessions");
    devicesButton.setTooltip ("Choose input and output devices (they can be different, e.g. Dante in, USB out)");
    pluginsButton.setTooltip ("Scan for and manage VST3 / AU / VST2 plugins");
    addRackButton.setTooltip ("Add a mono or stereo channel");


    snapshotLabel.setText ("Snapshot", juce::dontSendNotification);
    snapshotLabel.setColour (juce::Label::textColourId, Colours::textDim);
    snapshotLabel.setJustificationType (juce::Justification::centredRight);
    snapshotCombo.setTextWhenNothingSelected ("no snapshot");
    snapshotCombo.setTooltip ("Recall a snapshot of every rack, plugin and setting");
    snapshotCombo.onChange = [this]
    {
        const auto index = snapshotCombo.getSelectedId() - 1;

        if (index >= 0 && index != snapshots.getCurrent())
            recallSnapshot (index);
    };

    previousSnapshot.onClick = [this] { recallSnapshot (snapshots.getCurrent() - 1); };
    nextSnapshot.onClick = [this] { recallSnapshot (snapshots.getCurrent() + 1); };
    storeButton.setTooltip ("Store the current state into the selected snapshot (or a new one)");
    storeButton.onClick = [this] { storeSnapshot(); };
    snapshotMenuButton.onClick = [this] { showSnapshotMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &sessionButton, &devicesButton, &pluginsButton, &addRackButton,
                                                             &snapshotLabel, &snapshotCombo, &previousSnapshot,
                                                             &nextSnapshot, &storeButton, &snapshotMenuButton, &ioLabel, &statusLabel })
        addAndMakeVisible (c);

    ioLabel.setFont (juce::FontOptions (12.0f));
    ioLabel.setColour (juce::Label::textColourId, Colours::textDim);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);

    viewport.setViewedComponent (&rackContainer, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    engine.onDevicesChanged = [this] { updateStatus(); };

    // Devices are a property of this machine, so they come from the user settings.
    DeviceSelection selection = defaultSelection();

    if (auto xml = properties.getUserSettings()->getXmlValue ("devices"))
        selection = DeviceSelection::fromValueTree (juce::ValueTree::fromXml (*xml));

    openDevices (selection);

    if (! loadSession (getAutosaveFile(), false))
        addDefaultRacks();


    setWantsKeyboardFocus (true);
    setSize (1280, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    editors.closeAll();
    rows.clear();
    engine.close();
    engine.getRacks().removeAll();
}

//==============================================================================
void MainComponent::addRack (int numChannels)
{
    auto& racks = engine.getRacks();
    auto& rack = racks.addRack (std::make_unique<Rack> ("Ch " + juce::String (racks.size() + 1), numChannels));

    // Patch new racks after the channels the previous rack uses.
    if (auto* previous = racks.getRack (racks.size() - 2))
    {
        const auto in = previous->getInputMap();
        const auto out = previous->getOutputMap();
        const auto nextIn = in.empty() ? 0 : juce::jmax (0, in.back() + 1);
        const auto nextOut = out.empty() ? 0 : juce::jmax (0, out.back() + 1);
        std::vector<int> newIn, newOut;

        for (int i = 0; i < numChannels; ++i)
        {
            newIn.push_back (nextIn + i);
            newOut.push_back (nextOut + i);
        }

        rack.setInputMap (newIn);
        rack.setOutputMap (newOut);
    }

    rebuildRows();
}

void MainComponent::showAddRackMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("New channel");
    menu.addItem (1, "Mono");
    menu.addItem (2, "Stereo");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addRackButton), [this] (int result)
    {
        if (result > 0)
            addRack (result);
    });
}

void MainComponent::addDefaultRacks()
{
    // A fresh session looks like a console: eight stereo channels on inputs/outputs 1-16.
    for (int i = 0; i < 8; ++i)
        engine.getRacks().addRack (std::make_unique<Rack> ("Ch " + juce::String (i + 1), 2))
            .setInputMap ({ i * 2, i * 2 + 1 });

    for (int i = 0; i < 8; ++i)
        engine.getRacks().getRack (i)->setOutputMap ({ i * 2, i * 2 + 1 });

    rebuildRows();
}

void MainComponent::removeRack (Rack& rack)
{
    // Deferred: this can be called from inside the strip being removed.
    juce::MessageManager::callAsync ([this, r = &rack]
    {
        const auto index = engine.getRacks().indexOf (r);

        if (index >= 0)
        {
            disposeRack (engine.getRacks().removeRack (index));
            rebuildRows();
        }
    });
}

void MainComponent::rebuildRows()
{
    rows.clear();
    auto& racks = engine.getRacks();

    for (int i = 0; i < racks.size(); ++i)
        rackContainer.addAndMakeVisible (rows.add (new ChannelStrip (*racks.getRack (i), i, rowCallbacks)));

    resized();
}

void MainComponent::refreshRows()
{
    if (rows.size() != engine.getRacks().size())
    {
        rebuildRows();
        return;
    }

    for (auto* row : rows)
        row->refresh();
}


//==============================================================================
void MainComponent::showPluginChooser (Rack& rack, int slot)
{
    auto menu = buildPluginMenu (host.getSortedTypes(), host.getRecent(),
                                 [this, r = &rack, slot] (const juce::PluginDescription& d)
                                 {
                                     if (engine.getRacks().indexOf (r) >= 0)
                                         loadPlugin (*r, slot, d);
                                 },
                                 [this] { showPluginManager(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MainComponent::moveSlot (Rack& from, int fromSlot, Rack& to, int toSlot, bool copy)
{
    if (copy)
    {
        const auto saved = clipboard;
        copySlot (from, fromSlot);
        pasteSlot (to, toSlot);
        clipboard = saved;
    }
    else if (&from == &to)
    {
        from.moveSlot (fromSlot, toSlot);
    }
    else
    {
        // Swap, so dropping onto an occupied slot never loses a plugin. The instances
        // move as they are (settings, editor windows) and are re-prepared for their new channel.
        auto moving = from.takeSlot (fromSlot);
        auto displaced = to.putSlot (toSlot, std::move (moving));
        from.putSlot (fromSlot, std::move (displaced));
    }

    engine.getRacks().updateLatencyCompensation();
    refreshRows();
}

void MainComponent::loadPlugin (Rack& rack, int slot, const juce::PluginDescription& description)
{
    const auto& racks = engine.getRacks();
    juce::String error;
    auto instance = host.createInstance (description, racks.getSampleRate(), racks.getBlockSize(), error);

    if (instance == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load plugin",
                                                description.name + "\n\n" + error);
        return;
    }

    auto* processor = instance.get();
    host.addRecent (description);
    dispose (rack.setProcessor (slot, std::move (instance), description));
    refreshRows();
    editors.show (*processor, rack.getName() + " / " + description.name);
}

void MainComponent::showSlotMenu (Rack& rack, int slot)
{
    auto* processor = rack.getProcessor (slot);
    juce::PopupMenu menu;

    // "Copy to" targets: the same slot number on every other channel.
    constexpr int copyToBase = 1000;
    juce::PopupMenu copyTo;
    auto& racks = engine.getRacks();

    for (int i = 0; i < racks.size(); ++i)
        if (auto* target = racks.getRack (i); target != &rack && slot < target->getNumSlots())
            copyTo.addItem (copyToBase + i, juce::String (i + 1) + ". " + target->getName()
                                                + (target->getProcessor (slot) != nullptr ? "  (replace "
                                                       + target->getProcessor (slot)->getName() + ")" : juce::String()));

    const auto pasteText = clipboard.valid ? "Paste " + clipboard.description.name : juce::String ("Paste");

    if (processor == nullptr)
    {
        menu.addItem (1, "Insert plugin...");
        menu.addItem (9, pasteText, clipboard.valid);
    }
    else
    {
        menu.addSectionHeader (processor->getName() + "  (latency " + juce::String (processor->getLatencySamples()) + " smp)");
        menu.addItem (2, "Open editor");
        menu.addItem (3, "Bypass", true, rack.isSlotBypassed (slot));
        menu.addItem (4, "Replace...");
        menu.addItem (5, "Remove");
        menu.addSeparator();
        menu.addItem (8, "Copy");
        menu.addItem (9, pasteText + " (replace)", clipboard.valid);
        menu.addSubMenu ("Copy to channel", copyTo, copyTo.getNumItems() > 0);
        menu.addSeparator();
        menu.addItem (6, "Move up", slot > 0);
        menu.addItem (7, "Move down", slot < rack.getNumSlots() - 1);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, r = &rack, slot] (int result)
    {
        if (engine.getRacks().indexOf (r) < 0)
            return;

        if (result >= copyToBase)
        {
            if (auto* target = engine.getRacks().getRack (result - copyToBase))
            {
                const auto saved = clipboard;
                copySlot (*r, slot);
                pasteSlot (*target, slot);
                clipboard = saved;
            }

            refreshRows();
            return;
        }

        switch (result)
        {
            case 1: case 4: showPluginChooser (*r, slot); break;
            case 2: if (auto* p = r->getProcessor (slot)) editors.show (*p, r->getName() + " / " + p->getName()); break;
            case 3: r->setSlotBypassed (slot, ! r->isSlotBypassed (slot)); break;
            case 5: dispose (r->removeProcessor (slot)); break;
            case 6: r->moveSlot (slot, slot - 1); break;
            case 7: r->moveSlot (slot, slot + 1); break;
            case 8: copySlot (*r, slot); break;
            case 9: pasteSlot (*r, slot); break;
            default: return;
        }

        refreshRows();
    });
}

void MainComponent::copySlot (Rack& rack, int slot)
{
    auto* processor = rack.getProcessor (slot);

    if (processor == nullptr)
        return;

    clipboard.description = rack.getDescription (slot);
    clipboard.state.reset();
    processor->getStateInformation (clipboard.state);
    clipboard.bypassed = rack.isSlotBypassed (slot);
    clipboard.valid = true;
}

void MainComponent::pasteSlot (Rack& rack, int slot)
{
    if (! clipboard.valid)
        return;

    const auto& racks = engine.getRacks();
    juce::String error;
    auto instance = host.createInstance (clipboard.description, racks.getSampleRate(), racks.getBlockSize(), error);

    if (instance == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not paste plugin",
                                                clipboard.description.name + "\n\n" + error);
        return;
    }

    dispose (rack.setProcessor (slot, std::move (instance), clipboard.description));
    rack.setProcessorState (slot, clipboard.state);
    rack.setSlotBypassed (slot, clipboard.bypassed);
}

void MainComponent::showRackMenu (Rack& rack)
{
    auto& racks = engine.getRacks();
    const auto index = racks.indexOf (&rack);

    juce::PopupMenu menu;
    juce::PopupMenu colours;
    constexpr int colourBase = 100;

    for (int i = 0; i < 8; ++i)
        colours.addColouredItem (colourBase + i, "Colour " + juce::String (i + 1), Colours::forChannel (i));

    colours.addItem (colourBase + 99, "Automatic");

    menu.addSectionHeader (rack.getName());
    menu.addItem (1, "Rename...");
    menu.addSubMenu ("Colour", colours);
    menu.addItem (2, "Duplicate");
    menu.addItem (3, "Move left", index > 0);
    menu.addItem (4, "Move right", index < racks.size() - 1);
    menu.addSeparator();
    menu.addItem (7, "Add insert slot", rack.getNumSlots() < kMaxSlots);
    menu.addItem (8, "Remove last empty slot", rack.getNumSlots() > kMinSlots && rack.getProcessor (rack.getNumSlots() - 1) == nullptr);
    menu.addItem (5, "Remove all plugins");
    menu.addSeparator();
    menu.addItem (6, "Remove channel");

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, r = &rack] (int result)
    {
        auto& rackSet = engine.getRacks();
        const auto position = rackSet.indexOf (r);

        if (position < 0)
            return;

        switch (result)
        {
            case 1:
                promptForText ("Rename channel", r->getName(), [this, r] (juce::String name)
                {
                    if (engine.getRacks().indexOf (r) >= 0 && name.isNotEmpty())
                    {
                        r->setName (name);
                        refreshRows();
                    }
                });
                break;

            case 2: duplicateRack (*r); break;
            case 3: rackSet.moveRack (position, position - 1); rebuildRows(); break;
            case 4: rackSet.moveRack (position, position + 1); rebuildRows(); break;

            case 5:
                for (int i = 0; i < r->getNumSlots(); ++i)
                    dispose (r->removeProcessor (i));

                refreshRows();
                break;

            case 6: removeRack (*r); break;
            case 7: r->setNumSlots (r->getNumSlots() + 1); refreshRows(); break;
            case 8: r->setNumSlots (r->getNumSlots() - 1); refreshRows(); break;

            default:
                if (result >= colourBase)
                {
                    r->setColour (result == colourBase + 99 ? 0u : Colours::forChannel (result - colourBase).getARGB());
                    refreshRows();
                }
                break;
        }
    });
}

void MainComponent::openRouting (Rack& rack, bool input)
{
    const auto names = input ? engine.getInputChannelNames() : engine.getOutputChannelNames();
    const auto title = rack.getName() + (input ? " - inputs" : " - outputs");

    RoutingMatrix::show (title, rack.getNumChannels(), names,
                         input ? rack.getInputMap() : rack.getOutputMap(), input,
                         [this, r = &rack, input] (std::vector<int> map)
                         {
                             if (engine.getRacks().indexOf (r) < 0)
                                 return;

                             if (input)
                                 r->setInputMap (std::move (map));
                             else
                                 r->setOutputMap (std::move (map));

                             refreshRows();
                         });
}

void MainComponent::duplicateRack (Rack& rack)
{
    auto tree = rack.toValueTree();
    tree.setProperty ("name", rack.getName() + " copy", nullptr);

    auto& copy = engine.getRacks().addRack (std::make_unique<Rack> (rack.getName() + " copy", rack.getNumChannels(),
                                                                    getSlotCount (tree)));
    reportErrors (applyRack (tree, copy, makeSessionCallbacks()));
    rebuildRows();
}

void MainComponent::dispose (std::unique_ptr<juce::AudioProcessor> processor)
{
    if (processor != nullptr)
        editors.closeFor (processor.get());
}

void MainComponent::disposeRack (std::unique_ptr<Rack> rack)
{
    if (rack == nullptr)
        return;

    for (int i = 0; i < rack->getNumSlots(); ++i)
        editors.closeFor (rack->getProcessor (i));
}

SessionCallbacks MainComponent::makeSessionCallbacks()
{
    SessionCallbacks cb;
    cb.createProcessor = [this] (const juce::PluginDescription& d, juce::String& error) -> std::unique_ptr<juce::AudioProcessor>
    {
        const auto& racks = engine.getRacks();
        return host.createInstance (d, racks.getSampleRate(), racks.getBlockSize(), error);
    };
    cb.disposeProcessor = [this] (std::unique_ptr<juce::AudioProcessor> p) { dispose (std::move (p)); };
    cb.disposeRack = [this] (std::unique_ptr<Rack> r) { disposeRack (std::move (r)); };
    return cb;
}

void MainComponent::reportErrors (const juce::StringArray& errors)
{
    if (! errors.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Some plugins could not be loaded",
                                                errors.joinIntoString ("\n"));
}

//==============================================================================
void MainComponent::refreshSnapshots()
{
    snapshotCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < snapshots.size(); ++i)
        snapshotCombo.addItem (juce::String (i + 1) + ". " + snapshots.get (i)->name, i + 1);

    snapshotCombo.setSelectedId (snapshots.getCurrent() + 1, juce::dontSendNotification);
    previousSnapshot.setEnabled (snapshots.getCurrent() > 0);
    nextSnapshot.setEnabled (snapshots.getCurrent() < snapshots.size() - 1);
}

void MainComponent::recallSnapshot (int index)
{
    const auto* snapshot = snapshots.get (index);

    if (snapshot == nullptr)
        return;

    const auto errors = applyRacks (snapshot->racks, engine.getRacks(), makeSessionCallbacks());
    snapshots.setCurrent (index);
    refreshRows();
    refreshSnapshots();
    reportErrors (errors);
}

void MainComponent::storeSnapshot()
{
    if (snapshots.getCurrent() < 0)
        snapshots.add ("Snapshot " + juce::String (snapshots.size() + 1), captureRacks (engine.getRacks()));
    else
        snapshots.update (snapshots.getCurrent(), captureRacks (engine.getRacks()));

    refreshSnapshots();
}

void MainComponent::showSnapshotMenu()
{
    const auto current = snapshots.getCurrent();

    juce::PopupMenu menu;
    menu.addItem (5, "Recall selected (discard changes)", current >= 0);
    menu.addItem (1, "New snapshot from current state...");
    menu.addItem (2, "Update selected snapshot", current >= 0);
    menu.addItem (3, "Rename selected...", current >= 0);
    menu.addItem (4, "Delete selected", current >= 0);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapshotMenuButton), [this] (int result)
    {
        const auto selected = snapshots.getCurrent();

        switch (result)
        {
            case 1:
                promptForText ("New snapshot", "Snapshot " + juce::String (snapshots.size() + 1), [this] (juce::String name)
                {
                    snapshots.add (name.isNotEmpty() ? name : "Snapshot", captureRacks (engine.getRacks()));
                    refreshSnapshots();
                });
                break;

            case 2:
                storeSnapshot();
                break;

            case 3:
                if (auto* s = snapshots.get (selected))
                    promptForText ("Rename snapshot", s->name, [this, selected] (juce::String name)
                    {
                        snapshots.rename (selected, name);
                        refreshSnapshots();
                    });
                break;

            case 4:
                snapshots.remove (selected);
                refreshSnapshots();
                break;

            case 5:
                recallSnapshot (selected);
                break;

            default:
                break;
        }
    });
}

//==============================================================================
void MainComponent::showSessionMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "New session");
    menu.addItem (2, "Open...");
    menu.addItem (3, "Save");
    menu.addItem (4, "Save as...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sessionButton), [this] (int result)
    {
        switch (result)
        {
            case 1: newSession(); break;
            case 2: openSession(); break;
            case 3: saveSession (false); break;
            case 4: saveSession (true); break;
            default: break;
        }
    });
}

void MainComponent::newSession()
{
    editors.closeAll();
    engine.getRacks().removeAll();
    snapshots.clear();
    sessionFile = juce::File();
    addDefaultRacks();
    refreshSnapshots();
}

void MainComponent::openSession()
{
    chooser = std::make_unique<juce::FileChooser> ("Open session", sessionFile.getParentDirectory(),
                                                   juce::String ("*") + SessionFile::extension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file.existsAsFile() && ! loadSession (file, true))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "Open session", "Not a BridgeRack session: " + file.getFullPathName());
                          });
}

void MainComponent::saveSession (bool saveAs)
{
    const auto write = [this] (const juce::File& file)
    {
        const auto session = SessionFile::create (engine.getSelection().toValueTree(), engine.getRacks(), snapshots);

        if (SessionFile::write (file, session))
            sessionFile = file;
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save session",
                                                    "Could not write " + file.getFullPathName());
    };

    if (! saveAs && sessionFile != juce::File())
    {
        write (sessionFile);
        return;
    }

    const auto start = sessionFile != juce::File() ? sessionFile
                                                   : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                         .getChildFile (juce::String ("Untitled") + SessionFile::extension);

    chooser = std::make_unique<juce::FileChooser> ("Save session", start, juce::String ("*") + SessionFile::extension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [write] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file != juce::File())
                                  write (file.withFileExtension (SessionFile::extension));
                          });
}

bool MainComponent::loadSession (const juce::File& file, bool applyDevices)
{
    const auto session = SessionFile::read (file);

    if (! session.isValid())
        return false;

    if (applyDevices)
    {
        const auto devices = session.getChildWithName ("DEVICES");

        if (devices.isValid() && devices.getProperty ("outputDevice").toString().isNotEmpty())
            openDevices (DeviceSelection::fromValueTree (devices));
    }

    editors.closeAll();
    const auto errors = applyRacks (session.getChildWithName ("RACKS"), engine.getRacks(), makeSessionCallbacks());
    snapshots.loadFrom (session.getChildWithName ("SNAPSHOTS"));

    sessionFile = file == getAutosaveFile() ? juce::File (session.getProperty ("sourceFile").toString()) : file;

    rebuildRows();
    refreshSnapshots();
    reportErrors (errors);
    return true;
}

void MainComponent::saveAutosave()
{
    auto session = SessionFile::create (engine.getSelection().toValueTree(), engine.getRacks(), snapshots);
    session.setProperty ("sourceFile", sessionFile.getFullPathName(), nullptr);
    SessionFile::write (getAutosaveFile(), session);
}

juce::File MainComponent::getAutosaveFile() const
{
    return properties.getUserSettings()->getFile().getSiblingFile (juce::String ("Autosave") + SessionFile::extension);
}

//==============================================================================
juce::String MainComponent::openDevices (const DeviceSelection& selection)
{
    const auto error = engine.open (selection);

    if (auto xml = selection.toValueTree().createXml())
    {
        properties.getUserSettings()->setValue ("devices", xml.get());
        properties.getUserSettings()->saveIfNeeded();
    }

    refreshRows();
    updateStatus();
    return error;
}

DeviceSelection MainComponent::defaultSelection() const
{
    DeviceSelection s;

    if (auto* type = engine.getDeviceTypes().getFirst())
    {
        type->scanForDevices();
        const auto outs = type->getDeviceNames (false);
        const auto ins = type->getDeviceNames (true);

        s.inputType = s.outputType = type->getTypeName();
        s.outputDevice = outs[juce::jmax (0, type->getDefaultDeviceIndex (false))];
        s.inputDevice = ins[juce::jmax (0, type->getDefaultDeviceIndex (true))];
    }

    return s;
}

void MainComponent::showDeviceSetup()
{
    // Rebuilt every time so it reflects the devices present right now.
    deviceWindow = std::make_unique<ToolWindow> ("Audio I/O",
        new DeviceSetupPanel (engine, [this] (const DeviceSelection& s) { return openDevices (s); }));
}

void MainComponent::showPluginManager()
{
    if (pluginListWindow == nullptr)
    {
        auto* list = new juce::PluginListComponent (host.getFormatManager(), host.getKnownPlugins(),
                                                    host.getDeadMansPedalFile(), properties.getUserSettings(), true);
        list->setSize (760, 520);
        pluginListWindow = std::make_unique<ToolWindow> ("Plugins (" + host.getFormatNames() + ")", list);
    }

    pluginListWindow->setVisible (true);
    pluginListWindow->toFront (true);
}

//==============================================================================
void MainComponent::timerCallback()
{
    for (auto* row : rows)
        row->updateMeters();

    if (++ticks % 8 == 0)
        updateStatus();

    // Autosave every minute so a crash or power cut loses little.
    if (ticks % (30 * 60) == 0)
        saveAutosave();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('s', command, 0))                                    { saveSession (false); return true; }
    if (key == juce::KeyPress ('s', command | juce::ModifierKeys::shiftModifier, 0)) { saveSession (true);  return true; }
    if (key == juce::KeyPress ('o', command, 0))                                    { openSession();       return true; }
    if (key == juce::KeyPress (juce::KeyPress::rightKey))                           { recallSnapshot (snapshots.getCurrent() + 1); return true; }
    if (key == juce::KeyPress (juce::KeyPress::leftKey))                            { recallSnapshot (snapshots.getCurrent() - 1); return true; }

    return false;
}

void MainComponent::updateStatus()
{
    const auto status = engine.getStatus();
    const auto& sel = engine.getSelection();

    if (! status.running)
    {
        ioLabel.setText ("Audio stopped - open Audio I/O to choose devices", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::bad);
        statusLabel.setText (status.lastError, juce::dontSendNotification);
    }
    else
    {
        const auto input = sel.inputDevice.isEmpty() ? juce::String ("none") : sel.inputDevice + " [" + sel.inputType + "]";
        ioLabel.setText ("IN  " + input + "    >>    OUT  " + sel.outputDevice + " [" + sel.outputType + "]    "
                             + juce::String (status.sampleRate / 1000.0, 1) + " kHz / " + juce::String (status.blockSize),
                         juce::dontSendNotification);

        juce::String text;
        auto colour = Colours::text;

        if (status.bridged)
        {
            const auto& b = status.bridge;
            text << "Bridge " << (b.locked ? "LOCKED " : "SYNCING ") << formatPpm (b.driftPpm)
                 << ", drops " << (b.underruns + b.overruns) << "   |   ";
            colour = b.locked ? Colours::ok : Colours::warn;
        }

        // Where the latency comes from, so it is obvious what to change.
        const auto ms = [] (double v) { return juce::String (v, 1); };
        // Inserts only delay their own channel, so they're reported separately from the base path.
        const auto base = status.inputDeviceMs + status.bridgeMs + status.outputDeviceMs;
        text << "CPU " << juce::roundToInt (status.cpuLoad * 100.0) << "%   |   "
             << "Latency " << ms (base) << " ms  (in " << ms (status.inputDeviceMs);

        if (status.bridged)
            text << " + bridge " << ms (status.bridgeMs);

        text << " + out " << ms (status.outputDeviceMs) << ")";

        if (status.pluginMs > 0.05)
            text << "  + inserts up to " << ms (status.pluginMs) << " ms on the slowest channel";

        if (status.pluginMs > 5.0 && colour == Colours::text)
            colour = Colours::warn;

        if (status.cpuLoad > 0.8)
            colour = Colours::bad;

        statusLabel.setColour (juce::Label::textColourId, colour);
        statusLabel.setText (text, juce::dontSendNotification);
    }

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ("BridgeRack" + (sessionFile != juce::File() ? " - " + sessionFile.getFileNameWithoutExtension() : juce::String()));
}

//==============================================================================
void MainComponent::promptForText (const juce::String& title, const juce::String& initial,
                                   std::function<void (juce::String)> onOk)
{
    auto* window = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", initial);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onOk] (int result)
    {
        if (result == 1)
            onOk (window->getTextEditorContents ("text").trim());
    }), true);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    g.setColour (Colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (72));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (44).reduced (8, 7);

    for (auto* b : { &sessionButton, &devicesButton, &pluginsButton, &addRackButton })
    {
        b->setBounds (top.removeFromLeft (92));
        top.removeFromLeft (6);
    }

    snapshotMenuButton.setBounds (top.removeFromRight (32));
    top.removeFromRight (4);
    storeButton.setBounds (top.removeFromRight (60));
    top.removeFromRight (4);
    nextSnapshot.setBounds (top.removeFromRight (28));
    top.removeFromRight (2);
    previousSnapshot.setBounds (top.removeFromRight (28));
    top.removeFromRight (4);
    snapshotCombo.setBounds (top.removeFromRight (220));
    snapshotLabel.setBounds (top.removeFromRight (70));

    auto statusBar = area.removeFromTop (28).reduced (10, 2);
    statusLabel.setBounds (statusBar.removeFromRight (statusBar.getWidth() * 3 / 5));
    ioLabel.setBounds (statusBar);

    viewport.setBounds (area.reduced (0, 4));

    // Channel strips side by side, like a console surface.
    int height = viewport.getMaximumVisibleHeight();

    for (auto* strip : rows)
        height = juce::jmax (height, strip->getMinimumHeight());

    rackContainer.setSize (juce::jmax (viewport.getMaximumVisibleWidth(), rows.size() * ChannelStrip::width + 8), height);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (4 + i * ChannelStrip::width, 0, ChannelStrip::width, height);
}

} // namespace bridgerack
