#include "Session.h"

namespace bridgerack
{

namespace
{
    std::vector<int> parseMap (const juce::String& text)
    {
        std::vector<int> map;

        for (auto& item : juce::StringArray::fromTokens (text, ",", {}))
            if (item.isNotEmpty())
                map.push_back (item.getIntValue());

        return map;
    }

    void dispose (const SessionCallbacks& cb, std::unique_ptr<juce::AudioProcessor> p)
    {
        if (p == nullptr)
            return;

        if (cb.disposeProcessor)
            cb.disposeProcessor (std::move (p));
    }

    void applySlot (Rack& rack, const juce::ValueTree& slotTree, int index,
                    const SessionCallbacks& cb, juce::StringArray& errors)
    {
        const auto pluginTree = slotTree.getChildWithName ("PLUGIN");

        if (! pluginTree.isValid())
        {
            dispose (cb, rack.removeProcessor (index));
            return;
        }

        juce::PluginDescription description;

        if (auto xml = pluginTree.createXml(); xml == nullptr || ! description.loadFromXml (*xml))
        {
            errors.add ("Rack \"" + rack.getName() + "\" slot " + juce::String (index + 1) + ": unreadable plugin description");
            return;
        }

        const auto sameAsLoaded = rack.getProcessor (index) != nullptr
                               && rack.getDescription (index).createIdentifierString() == description.createIdentifierString();

        if (! sameAsLoaded)
        {
            juce::String error;
            auto processor = cb.createProcessor ? cb.createProcessor (description, error) : nullptr;

            if (processor == nullptr)
            {
                errors.add (description.name + ": " + (error.isNotEmpty() ? error : juce::String ("could not be loaded")));
                dispose (cb, rack.removeProcessor (index));
                return;
            }

            dispose (cb, rack.setProcessor (index, std::move (processor), description));
        }

        juce::MemoryBlock state;

        if (state.fromBase64Encoding (slotTree.getProperty ("state").toString()) && state.getSize() > 0)
            rack.setProcessorState (index, state);

        rack.setSlotBypassed (index, slotTree.getProperty ("bypassed", false));
    }
}

juce::ValueTree captureRacks (const RackSet& racks)
{
    juce::ValueTree tree ("RACKS");
    tree.setProperty ("latencyCompensation", racks.isLatencyCompensationOn(), nullptr);

    for (int i = 0; i < racks.size(); ++i)
        tree.appendChild (racks.getRack (i)->toValueTree(), nullptr);

    return tree;
}

int getSlotCount (const juce::ValueTree& rackTree)
{
    int numSlots = kMinSlots;

    for (const auto& slotTree : rackTree)
        if (slotTree.hasType ("SLOT"))
            numSlots = juce::jmax (numSlots, (int) slotTree.getProperty ("index", 0) + 1);

    return juce::jmin (numSlots, kMaxSlots);
}

juce::StringArray applyRack (const juce::ValueTree& rackTree, Rack& rack, const SessionCallbacks& cb)
{
    juce::StringArray errors;
    const auto numSlots = getSlotCount (rackTree);

    rack.setName (rackTree.getProperty ("name", rack.getName()).toString());
    rack.setNumChannels (rackTree.getProperty ("channels", 2));
    rack.setNumSlots (numSlots);
    rack.setInputMap (parseMap (rackTree.getProperty ("inputMap")));
    rack.setOutputMap (parseMap (rackTree.getProperty ("outputMap")));
    rack.setInputGainDb (rackTree.getProperty ("inputGainDb", 0.0f));
    rack.setOutputGainDb (rackTree.getProperty ("outputGainDb", 0.0f));
    rack.setMuted (rackTree.getProperty ("muted", false));
    rack.setBypassed (rackTree.getProperty ("bypassed", false));

    std::vector<bool> covered ((size_t) rack.getNumSlots(), false);

    for (const auto& slotTree : rackTree)
    {
        if (! slotTree.hasType ("SLOT"))
            continue;

        const int index = slotTree.getProperty ("index", -1);

        if (! juce::isPositiveAndBelow (index, rack.getNumSlots()))
            continue;

        covered[(size_t) index] = true;
        applySlot (rack, slotTree, index, cb, errors);
    }

    for (int i = 0; i < rack.getNumSlots(); ++i)
        if (! covered[(size_t) i])
            dispose (cb, rack.removeProcessor (i));

    // Now that surplus plugins are gone, trailing empty slots can shrink away.
    rack.setNumSlots (numSlots);
    return errors;
}

juce::StringArray applyRacks (const juce::ValueTree& racksTree, RackSet& racks, const SessionCallbacks& cb)
{
    juce::StringArray errors;
    int numRacks = 0;

    racks.setLatencyCompensation (racksTree.getProperty ("latencyCompensation", true));

    for (const auto& rackTree : racksTree)
    {
        if (! rackTree.hasType ("RACK"))
            continue;

        auto* rack = racks.getRack (numRacks);

        if (rack == nullptr)
            rack = &racks.addRack (std::make_unique<Rack> (rackTree.getProperty ("name", "Rack").toString(),
                                                           (int) rackTree.getProperty ("channels", 2),
                                                           getSlotCount (rackTree)));

        errors.addArray (applyRack (rackTree, *rack, cb));
        ++numRacks;
    }

    while (racks.size() > numRacks)
    {
        auto removed = racks.removeRack (racks.size() - 1);

        if (cb.disposeRack)
            cb.disposeRack (std::move (removed));
    }

    racks.updateLatencyCompensation();
    return errors;
}

//==============================================================================
const SnapshotBank::Snapshot* SnapshotBank::get (int index) const
{
    return juce::isPositiveAndBelow (index, size()) ? &snapshots[(size_t) index] : nullptr;
}

int SnapshotBank::add (const juce::String& name, juce::ValueTree racks)
{
    snapshots.push_back ({ name, std::move (racks) });
    current = size() - 1;
    return current;
}

void SnapshotBank::update (int index, juce::ValueTree racks)
{
    if (juce::isPositiveAndBelow (index, size()))
        snapshots[(size_t) index].racks = std::move (racks);
}

void SnapshotBank::rename (int index, const juce::String& name)
{
    if (juce::isPositiveAndBelow (index, size()))
        snapshots[(size_t) index].name = name;
}

void SnapshotBank::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;

    snapshots.erase (snapshots.begin() + index);

    if (current >= size())
        current = size() - 1;
}

juce::ValueTree SnapshotBank::toValueTree() const
{
    juce::ValueTree tree ("SNAPSHOTS");
    tree.setProperty ("current", current, nullptr);

    for (auto& s : snapshots)
    {
        juce::ValueTree snapshot ("SNAPSHOT");
        snapshot.setProperty ("name", s.name, nullptr);
        snapshot.appendChild (s.racks.createCopy(), nullptr);
        tree.appendChild (snapshot, nullptr);
    }

    return tree;
}

void SnapshotBank::loadFrom (const juce::ValueTree& tree)
{
    snapshots.clear();

    for (const auto& snapshot : tree)
        if (snapshot.hasType ("SNAPSHOT"))
            snapshots.push_back ({ snapshot.getProperty ("name").toString(),
                                   snapshot.getChildWithName ("RACKS").createCopy() });

    setCurrent (tree.getProperty ("current", -1));
}

//==============================================================================
juce::ValueTree SessionFile::create (const juce::ValueTree& devices, const RackSet& racks, const SnapshotBank& snapshots)
{
    juce::ValueTree session ("BRIDGERACK_SESSION");
    session.setProperty ("version", 1, nullptr);
    session.appendChild (devices.createCopy(), nullptr);
    session.appendChild (captureRacks (racks), nullptr);
    session.appendChild (snapshots.toValueTree(), nullptr);
    return session;
}

bool SessionFile::write (const juce::File& file, const juce::ValueTree& session)
{
    if (auto xml = session.createXml())
        return xml->writeTo (file);

    return false;
}

juce::ValueTree SessionFile::read (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.hasType ("BRIDGERACK_SESSION"))
            return tree;
    }

    return {};
}

} // namespace bridgerack
