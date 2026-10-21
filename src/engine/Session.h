#pragma once

#include "RackSet.h"

namespace bridgerack
{

/** How a session or snapshot gets its plugins: the app uses PluginHost, tests use stand-ins. */
struct SessionCallbacks
{
    std::function<std::unique_ptr<juce::AudioProcessor> (const juce::PluginDescription&, juce::String& error)> createProcessor;

    /** Called with every processor/rack that gets replaced, so editor windows can close first. */
    std::function<void (std::unique_ptr<juce::AudioProcessor>)> disposeProcessor;
    std::function<void (std::unique_ptr<Rack>)> disposeRack;
};

/** Full state of every rack: routing, gains, plugins and plugin settings. */
juce::ValueTree captureRacks (const RackSet&);

/**
    Makes the rack set match a captured state. Plugins that are already loaded
    in the same slot only get their state restored (fast, no reload), which is
    what makes snapshot recall quick. Returns any plugin load errors.
*/
juce::StringArray applyRacks (const juce::ValueTree& racksTree, RackSet&, const SessionCallbacks&);

/** Applies one captured RACK tree to an existing rack (used by applyRacks and for duplicating racks). */
juce::StringArray applyRack (const juce::ValueTree& rackTree, Rack&, const SessionCallbacks&);

/** Slot count needed to hold everything in a captured RACK tree. */
int getSlotCount (const juce::ValueTree& rackTree);

//==============================================================================
/** A list of named snapshots of the rack state, recalled like scenes on a console. */
class SnapshotBank
{
public:
    struct Snapshot
    {
        juce::String name;
        juce::ValueTree racks;
    };

    int size() const noexcept                       { return (int) snapshots.size(); }
    const Snapshot* get (int index) const;

    int add (const juce::String& name, juce::ValueTree racks);
    void update (int index, juce::ValueTree racks);
    void rename (int index, const juce::String& name);
    void remove (int index);
    void clear()                                    { snapshots.clear(); current = -1; }

    int getCurrent() const noexcept                 { return current; }
    void setCurrent (int index) noexcept            { current = juce::isPositiveAndBelow (index, size()) ? index : -1; }

    juce::ValueTree toValueTree() const;
    void loadFrom (const juce::ValueTree&);

private:
    std::vector<Snapshot> snapshots;
    int current = -1;
};

//==============================================================================
namespace SessionFile
{
    constexpr auto extension = ".bridgerack";

    juce::ValueTree create (const juce::ValueTree& devices, const RackSet&, const SnapshotBank&);
    bool write (const juce::File&, const juce::ValueTree& session);
    juce::ValueTree read (const juce::File&);
}

} // namespace bridgerack
