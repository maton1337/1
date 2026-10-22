#include "PluginMenu.h"

namespace bridgerack
{

namespace
{
    struct Rule
    {
        PluginCategory category;
        juce::StringArray hostCategories;   // tokens in PluginDescription::category, e.g. "Fx|Dynamics"
        juce::StringArray nameWords;        // whole words in the plugin name
        juce::StringArray nameParts;        // substrings in the plugin name
    };

    const std::vector<Rule>& rules()
    {
        // Order matters: the first rule that matches wins.
        static const std::vector<Rule> r {
            { { "Mastering",        juce::Colour (0xff8f7cf0) }, { "mastering" },
              {}, { "ozone", "stabilizer", "maximizer", "loudness", "imager", "master" } },
            { { "EQ & Filter",      juce::Colour (0xff3fa9f5) }, { "eq", "filter" },
              { "eq", "f6", "q10", "q6", "q4", "q3", "q2", "q1", "hpf", "lpf" }, { "equaliz", "filter", "pro-q", "pultec", "puigtec" } },
            { { "Dynamics",         juce::Colour (0xff5ad16b) }, { "dynamics" },
              { "c1", "c4", "c6", "l1", "l2", "l3", "rcomp", "rvox", "mv2" },
              { "comp", "limit", "gate", "expander", "de-ess", "deess", "dynamic", "emo d", "emo-d", "leveler", "vocalrider" } },
            { { "Reverb",           juce::Colour (0xff47d6c8) }, { "reverb", "room" },
              {}, { "verb", "room", "plate", "hall", "spring", "ir-" } },
            { { "Delay",            juce::Colour (0xff59c2e8) }, { "delay" },
              {}, { "delay", "echo", "doubler", "supertap" } },
            { { "Modulation",       juce::Colour (0xffd65ac4) }, { "modulation" },
              {}, { "chorus", "flang", "phaser", "tremolo", "vibrato", "rotary", "ensemble" } },
            { { "Saturation",       juce::Colour (0xffe8743b) }, { "distortion" },
              {}, { "satur", "distort", "drive", "tape", "clip", "tube", "exciter", "kramer", "j37" } },
            { { "Pitch",            juce::Colour (0xfff0b429) }, { "pitch shift", "pitch" },
              {}, { "tune", "pitch", "harmon", "shifter" } },
            { { "Tools & Analysis", juce::Colour (0xffb9c2cf) }, { "analyzer", "tools", "spatial", "restoration", "surround", "network" },
              { "ns1", "nx", "rx" }, { "meter", "analy", "scope", "tuner", "utility", "trim", "phase", "noise", "gain" } },
        };
        return r;
    }

    const PluginCategory instruments { "Instruments", juce::Colour (0xffe5534b) };
    const PluginCategory other       { "Other",       juce::Colour (0xff8a919c) };

    juce::String itemText (const juce::PluginDescription& d)
    {
        return d.name + "   (" + d.manufacturerName + ", " + d.pluginFormatName + ")";
    }

    void sortByName (juce::Array<juce::PluginDescription>& types)
    {
        std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
        {
            return a.name.compareIgnoreCase (b.name) < 0;
        });
    }
}

PluginCategory PluginCategory::of (const juce::PluginDescription& d)
{
    if (d.isInstrument)
        return instruments;

    // The host-supplied category (VST3 subcategories, AU types) is the most reliable signal.
    const auto tokens = juce::StringArray::fromTokens (d.category.toLowerCase(), "|", {});

    for (auto& rule : rules())
        for (auto& token : tokens)
            if (rule.hostCategories.contains (token.trim()))
                return rule.category;

    if (tokens.contains ("instrument") || tokens.contains ("synth") || tokens.contains ("generator"))
        return instruments;

    // Otherwise guess from the name: "F6", "eMo D5", "Ozone Stabilizer"...
    const auto name = d.name.toLowerCase();
    const auto words = juce::StringArray::fromTokens (name, " -_()[]", {});

    for (auto& rule : rules())
    {
        for (auto& w : rule.nameWords)
            if (words.contains (w))
                return rule.category;

        for (auto& part : rule.nameParts)
            if (name.contains (part))
                return rule.category;
    }

    return other;
}

const juce::Array<PluginCategory>& PluginCategory::all()
{
    static const juce::Array<PluginCategory> list = []
    {
        juce::Array<PluginCategory> l;

        for (auto& rule : rules())
            l.add (rule.category);

        l.add (instruments);
        l.add (other);
        return l;
    }();

    return list;
}

juce::PopupMenu buildPluginMenu (const juce::Array<juce::PluginDescription>& types,
                                 const juce::Array<juce::PluginDescription>& recent,
                                 std::function<void (const juce::PluginDescription&)> onChosen,
                                 std::function<void()> onOpenManager)
{
    juce::PopupMenu menu;

    if (types.isEmpty())
    {
        menu.addItem ("No plugins scanned yet - open Plugin Manager...", onOpenManager);
        return menu;
    }

    const auto add = [&onChosen] (juce::PopupMenu& m, const juce::PluginDescription& d, const juce::String& text)
    {
        m.addItem (text, [onChosen, d] { onChosen (d); });
    };

    if (! recent.isEmpty())
    {
        menu.addSectionHeader ("Recent");

        for (auto& d : recent)
            add (menu, d, itemText (d));

        menu.addSeparator();
    }

    menu.addSectionHeader ("Categories");

    for (auto& category : PluginCategory::all())
    {
        juce::Array<juce::PluginDescription> inCategory;

        for (auto& d : types)
            if (PluginCategory::of (d).name == category.name)
                inCategory.add (d);

        if (inCategory.isEmpty())
            continue;

        sortByName (inCategory);
        juce::PopupMenu sub;

        juce::StringArray makers;

        for (auto& d : inCategory)
            makers.addIfNotAlreadyThere (d.manufacturerName);

        // Long shelves from several companies get one more level, by manufacturer.
        if (inCategory.size() > 24 && makers.size() > 1)
        {
            std::map<juce::String, juce::PopupMenu> byMaker;

            for (auto& d : inCategory)
                add (byMaker[d.manufacturerName], d, d.name + "   (" + d.pluginFormatName + ")");

            for (auto& [maker, makerMenu] : byMaker)
                sub.addSubMenu (maker, makerMenu);
        }
        else
        {
            for (auto& d : inCategory)
                add (sub, d, itemText (d));
        }

        juce::PopupMenu::Item item (category.name + "  (" + juce::String (inCategory.size()) + ")");
        item.subMenu = std::make_unique<juce::PopupMenu> (std::move (sub));
        item.colour = category.colour;
        menu.addItem (std::move (item));
    }

    menu.addSeparator();

    juce::PopupMenu byManufacturer;
    std::map<juce::String, juce::PopupMenu> makers;

    for (auto& d : types)
        add (makers[d.manufacturerName], d, d.name + "   (" + d.pluginFormatName + ")");

    for (auto& [maker, makerMenu] : makers)
        byManufacturer.addSubMenu (maker, makerMenu);

    menu.addSubMenu ("All by manufacturer", byManufacturer);
    menu.addSeparator();
    menu.addItem ("Plugin Manager...", onOpenManager);
    return menu;
}

} // namespace bridgerack
