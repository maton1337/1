#include "MainComponent.h"
#include "DeviceSetupPanel.h"
#include "RoutingMatrix.h"

namespace bridgerack
{

namespace
{
    constexpr int pluginManagerItem = 1;

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
    rowCallbacks.onMoveSlot = [this] (Rack& rack, int from, int to) { rack.moveSlot (from, to); refreshRows(); };
    rowCallbacks.onRouting = [this] (Rack& rack, bool input) { openRouting (rack, input); };
    rowCallbacks.onChannelsChanged = [this] (Rack& rack, int n) { rack.setNumChannels (n); refreshRows(); };
    rowCallbacks.onSlotCountChanged = [this] (Rack&) { refreshRows(); };
    rowCallbacks.onRackMenu = [this] (Rack& rack) { showRackMenu (rack); };
    rowCallbacks.onRemove = [this] (Rack& rack)
    {
        // Deferred: this can be called from inside the row being removed.
        juce::MessageManager::callAsync ([this, r = &rack]
        {
            const auto index = engine.getRacks().indexOf (r);

            if (index >= 0)
            {
                disposeRack (engine.getRacks().removeRack (index));
                rebuildRows();
            }
        });
    };

    sessionButton.onClick = [this] { showSessionMenu(); };
    devicesButton.onClick = [this] { showDeviceSetup(); };
    pluginsButton.onClick = [this] { showPluginManager(); };
    addRackButton.onClick = [this] { showAddRackMenu(); };

    sessionButton.setTooltip ("New, open and save sessions");
    devicesButton.setTooltip ("Choose input and output devices (they can be different, e.g. Dante in, USB out)");
    pluginsButton.setTooltip ("Scan for and manage VST3 / AU / VST2 plugins");
    addRackButton.setTooltip ("Add a processing rack");

    alignButton.setTooltip ("Delay racks with less plugin latency so all rack outputs stay time-aligned");
    alignButton.onClick = [this] { engine.getRacks().setLatencyCompensation (alignButton.getToggleState()); };

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
                                                             &alignButton, &snapshotLabel, &snapshotCombo, &previousSnapshot,
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
        addRack (2);

    alignButton.setToggleState (engine.getRacks().isLatencyCompensationOn(), juce::dontSendNotification);

    setWantsKeyboardFocus (true);
    setSize (1440, 760);
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
    auto& rack = racks.addRack (std::make_unique<Rack> ("Rack " + juce::String (racks.size() + 1), numChannels));

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
    menu.addSectionHeader ("New rack");

    for (auto n : { 1, 2, 4, 6, 8, 16, 32, 64 })
        menu.addItem (n, n == 1 ? "Mono" : n == 2 ? "Stereo" : n == 6 ? "6 channels (5.1)" : juce::String (n) + " channels");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addRackButton), [this] (int result)
    {
        if (result > 0)
            addRack (result);
    });
}

void MainComponent::rebuildRows()
{
    rows.clear();
    auto& racks = engine.getRacks();

    for (int i = 0; i < racks.size(); ++i)
        rackContainer.addAndMakeVisible (rows.add (new RackRow (*racks.getRack (i), i, rowCallbacks)));

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

RackRow* MainComponent::findRow (const Rack& rack) const
{
    for (auto* row : rows)
        if (&row->getRack() == &rack)
            return row;

    return nullptr;
}

//==============================================================================
void MainComponent::showPluginChooser (Rack& rack, int slot)
{
    const auto types = host.getSortedTypes();
    juce::PopupMenu menu;

    if (types.isEmpty())
    {
        menu.addItem (pluginManagerItem, "No plugins scanned yet - open Plugin Manager...");
    }
    else
    {
        juce::KnownPluginList::addToMenu (menu, types, juce::KnownPluginList::sortByManufacturer);
        menu.addSeparator();
        menu.addItem (pluginManagerItem, "Plugin Manager...");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, r = &rack, slot, types] (int result)
    {
        if (result == pluginManagerItem)
        {
            showPluginManager();
            return;
        }

        const auto index = juce::KnownPluginList::getIndexChosenByMenu (types, result);

        if (engine.getRacks().indexOf (r) >= 0 && juce::isPositiveAndBelow (index, types.size()))
            loadPlugin (*r, slot, types[index]);
    });
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
    dispose (rack.setProcessor (slot, std::move (instance), description));
    refreshRows();
    editors.show (*processor, rack.getName() + " / " + description.name);
}

void MainComponent::showSlotMenu (Rack& rack, int slot)
{
    auto* processor = rack.getProcessor (slot);
    juce::PopupMenu menu;

    if (processor == nullptr)
    {
        menu.addItem (1, "Insert plugin...");
    }
    else
    {
        menu.addSectionHeader (processor->getName());
        menu.addItem (2, "Open editor");
        menu.addItem (3, "Bypass", true, rack.isSlotBypassed (slot));
        menu.addItem (4, "Replace...");
        menu.addItem (5, "Remove");
        menu.addSeparator();
        menu.addItem (6, "Move left", slot > 0);
        menu.addItem (7, "Move right", slot < rack.getNumSlots() - 1);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, r = &rack, slot] (int result)
    {
        if (engine.getRacks().indexOf (r) < 0)
            return;

        switch (result)
        {
            case 1: case 4: showPluginChooser (*r, slot); break;
            case 2: if (auto* p = r->getProcessor (slot)) editors.show (*p, r->getName() + " / " + p->getName()); break;
            case 3: r->setSlotBypassed (slot, ! r->isSlotBypassed (slot)); break;
            case 5: dispose (r->removeProcessor (slot)); break;
            case 6: r->moveSlot (slot, slot - 1); break;
            case 7: r->moveSlot (slot, slot + 1); break;
            default: return;
        }

        refreshRows();
    });
}

void MainComponent::showRackMenu (Rack& rack)
{
    auto& racks = engine.getRacks();
    const auto index = racks.indexOf (&rack);

    juce::PopupMenu menu;
    menu.addSectionHeader (rack.getName());
    menu.addItem (1, "Rename...");
    menu.addItem (2, "Duplicate");
    menu.addItem (3, "Move up", index > 0);
    menu.addItem (4, "Move down", index < racks.size() - 1);
    menu.addItem (5, "Remove all plugins");
    menu.addSeparator();
    menu.addItem (6, "Remove rack");

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, r = &rack] (int result)
    {
        auto& rackSet = engine.getRacks();
        const auto position = rackSet.indexOf (r);

        if (position < 0)
            return;

        switch (result)
        {
            case 1:
                promptForText ("Rename rack", r->getName(), [this, r] (juce::String name)
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

            case 6: rowCallbacks.onRemove (*r); break;
            default: break;
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
    alignButton.setToggleState (engine.getRacks().isLatencyCompensationOn(), juce::dontSendNotification);
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
    addRack (2);
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
    alignButton.setToggleState (engine.getRacks().isLatencyCompensationOn(), juce::dontSendNotification);

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
            text << "Clock bridge: " << (b.locked ? "LOCKED " : "SYNCING ") << formatPpm (b.driftPpm)
                 << "   buffer " << juce::String (b.latencyMs, 1) << " ms"
                 << "   dropouts " << (b.underruns + b.overruns) << "      ";
            colour = b.locked ? Colours::ok : Colours::warn;
        }

        text << "CPU " << juce::roundToInt (status.cpuLoad * 100.0) << "%"
             << "   total latency " << juce::String (status.totalLatencyMs, 1) << " ms";

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

    alignButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (12);
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
    statusLabel.setBounds (statusBar.removeFromRight (statusBar.getWidth() / 2));
    ioLabel.setBounds (statusBar);

    viewport.setBounds (area.reduced (0, 4));

    const auto width = juce::jmax (viewport.getMaximumVisibleWidth(), 1280);
    rackContainer.setSize (width, juce::jmax (1, rows.size() * RackRow::height + 8));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, 4 + i * RackRow::height, width, RackRow::height);
}

} // namespace bridgerack
