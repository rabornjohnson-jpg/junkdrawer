#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Modulation.h"

// Drawers (presets).  A drawer holds every setting except what belongs to the loaded sounds: the sample files, the number
// of regions and where each region starts and ends.  Anything a drawer does not mention goes back to its default.
namespace jdp
{
struct Drawer
{
    juce::String name, group;
    std::map<juce::String, float> values;     // real (not 0..1) values by parameter id
    std::vector<jd::Assign> mods;
};

inline bool isSoundSpecific (const juce::String& id)
{
    if (id == "seq_run" || id == "seq_bpm" || id == "seq_hostbpm") return true;
    if (id.endsWith ("_regions") || id.endsWith ("_tune") || id == "key_root" || id == "key_mode") return true;
    return id.startsWithChar ('t') && id.contains ("_r") && (id.endsWith ("_start") || id.endsWith ("_end"));
}

inline const juce::StringArray kGroups { "Tidy", "Skitter", "Wander", "Hush", "Tumble", "Rattle", "Dusty", "Echoes", "Odd Meters", "Fidget" };

inline const char* const kNames[10][10] = {
    { "Loose Change", "Spare Key", "Tape Measure", "Birthday Candles", "Twist Ties", "Safety Pins", "Paper Clips", "Thumbtacks", "Pencil Stubs", "Sticky Notes" },
    { "Dead Batteries", "Dice Roll", "Bottle Caps", "Buttons Spilled", "Loose Screws", "Spare Bulbs", "Gum Wrappers", "Matchbooks", "Marbles", "Cuff Links" },
    { "Takeout Menus", "Warranty Cards", "Old Receipts", "Instruction Manual", "Phone Book", "Coupons", "Return Labels", "Ticket Stubs", "Postcards", "Street Maps" },
    { "Unlabeled Cassette", "Dried Highlighter", "Single Earring", "Spare Fuse", "Flashlight Dim", "Lone Glove", "Wax Seal", "Dust Bunny", "Faded Photo", "Pocket Lint" },
    { "Wind-Up Key", "Yo-Yo", "Jacks", "Harmonica", "Whistle", "Kazoo", "Slinky", "Spinning Top", "Tin Soldier", "Music Box" },
    { "Hammer Drawer", "Nuts and Bolts", "Washers", "Door Hinges", "Chain Links", "Coat Hooks", "Cabinet Knobs", "Curtain Rings", "Drawer Pulls", "Skeleton Key Ring" },
    { "Cassette Pile", "Old Radio", "Rotary Dial", "Transistor", "Worn Vinyl", "Floppy Disks", "Dial Tone", "Fuzzy Dice", "Static Cling", "Rabbit Ears" },
    { "Empty Jar", "Seashell", "Glass Marbles", "Snow Globe", "Wind Chimes", "Brass Bell", "Tin Can Phone", "Echo Canyon Postcard", "Fog Horn", "Rain Gauge" },
    { "Five and Seven", "Odd Socks", "Third Wheel", "Seven Dwarves", "Prime Time", "Nine Lives", "Three Legged", "Thirteen Steps", "Left Over", "Uneven Table" },
    { "Fidget Spinner", "Pen Clicker", "Loose Tooth", "Jumping Beans", "Static Shock", "Hiccups", "Tangled Cords", "Windup Chatter", "Restless Leg", "Junk Mail" }
};

// Euclidean pattern: `hits` evenly spread over `len` steps, turned by `rot`.
inline std::vector<bool> euclid (int hits, int len, int rot)
{
    std::vector<bool> v ((size_t) len, false);
    for (int i = 0; i < hits; ++i) v[(size_t) (((i * len) / juce::jmax (1, hits) + rot) % len)] = true;
    return v;
}

inline Drawer makeFactory (int index)
{
    const int group = index / 10, k = index % 10;
    Drawer d;
    d.name = kNames[group][k];
    d.group = kGroups[group];
    juce::Random rnd (9000 + index * 77);
    auto set = [&] (const juce::String& id, float v) { d.values[id] = v; };
    auto pick = [&] (std::initializer_list<float> l) { return *(l.begin() + rnd.nextInt ((int) l.size())); };
    auto range = [&] (float a, float b) { return a + (b - a) * rnd.nextFloat(); };

    // per-group character -----------------------------------------------------------------------------------------
    int div = 2, feel = 0, tracks = 3 + k % 3;
    float swing = 0.5f, density = 0.5f, ratchetChance = 0.1f, filt = 3200.f, delayMix = 0.2f, reverbMix = 0.3f;
    bool lengthsDiffer = false;
    switch (group)
    {
        case 0: swing = range (0.5f, 0.56f); density = range (0.4f, 0.6f); break;
        case 1: div = 3; density = range (0.5f, 0.75f); ratchetChance = range (0.35f, 0.6f); break;
        case 2: div = pick ({ 1, 2 }); density = range (0.3f, 0.45f); reverbMix = range (0.45f, 0.65f); filt = range (900.f, 2500.f); break;
        case 3: div = 1; density = range (0.12f, 0.25f); reverbMix = range (0.5f, 0.7f); delayMix = range (0.3f, 0.45f); filt = range (700.f, 1800.f); tracks = 2 + k % 2; break;
        case 4: feel = k % 2 ? 2 : 1; swing = range (0.55f, 0.66f); density = range (0.4f, 0.6f); break;
        case 5: div = pick ({ 2, 3 }); density = range (0.45f, 0.65f); ratchetChance = 0.2f; filt = range (5000.f, 12000.f); reverbMix = 0.12f; delayMix = 0.1f; break;
        case 6: density = range (0.35f, 0.55f); filt = range (500.f, 1400.f); reverbMix = range (0.2f, 0.35f); break;
        case 7: div = pick ({ 1, 2 }); density = range (0.25f, 0.4f); delayMix = range (0.4f, 0.6f); reverbMix = range (0.5f, 0.75f); break;
        case 8: density = range (0.4f, 0.6f); lengthsDiffer = true; break;
        default: div = pick ({ 2, 3 }); density = range (0.45f, 0.7f); ratchetChance = 0.3f; break;
    }
    set ("seq_div", (float) div); set ("seq_feel", (float) feel); set ("m_swing", swing);
    set ("fx_filter_cut", filt); set ("fx_filter_res", range (0.1f, group == 6 ? 0.45f : 0.3f));
    set ("fx_delay_mix", delayMix); set ("fx_delay_fb", range (0.25f, group == 7 ? 0.65f : 0.45f));
    set ("fx_reverb_mix", reverbMix); set ("fx_reverb_mode", (float) rnd.nextInt (4));
    set ("fx_reverb_decay", group == 7 || group == 3 ? range (3000.f, 7000.f) : range (900.f, 2600.f));
    set ("fx_delay_mode", (float) (1 + rnd.nextInt (3)));
    set ("fx_lim_on", 1.f);
    if (group == 6) set ("fx_filter_drive", range (5.f, 11.f));

    // patterns -------------------------------------------------------------------------------------------------
    static const float scale[] = { 0, 0, 3, 5, 7, 10, 12, -5, -7, -12 };
    for (int t = 0; t < jd::kTracks; ++t)
    {
        const bool active = t < tracks;
        set (jd::trackId (t, "level"), active ? range (-6.f, 0.f) : 0.f);
        set (jd::trackId (t, "pan"), active ? range (-0.5f, 0.5f) : 0.f);
        const int len = lengthsDiffer ? pick ({ 5.f, 7.f, 9.f, 11.f, 13.f, 15.f }) : pick ({ 16.f, 16.f, 12.f, 8.f });
        for (int ln = 0; ln < jd::kLanes; ++ln)
            set (jd::laneLenId (t, ln), lengthsDiffer && ln != jd::LaneRegion ? pick ({ 3.f, 4.f, 6.f, 9.f, 16.f }) : (float) len);
        if (! active) continue;
        const int hits = juce::jlimit (1, len, juce::roundToInt (density * (float) len * range (0.6f, 1.2f)));
        const auto pat = euclid (hits, len, rnd.nextInt (len));
        for (int s = 0; s < jd::kSteps; ++s)
        {
            const int st = s % len;
            const bool on = pat[(size_t) st];
            set (jd::stepId (t, s, jd::LaneRegion), on ? (float) (1 + rnd.nextInt (4)) : 0.f);
            set (jd::stepId (t, s, jd::LaneVolume), st % 4 == 0 ? 1.f : range (0.55f, 0.9f));
            set (jd::stepId (t, s, jd::LanePitch), rnd.nextFloat() < (group == 2 || group == 3 ? 0.5f : 0.25f) ? scale[rnd.nextInt (10)] : 0.f);
            set (jd::stepId (t, s, jd::LaneRatchet), rnd.nextFloat() < ratchetChance ? (float) (2 + rnd.nextInt (3)) : 1.f);
            set (jd::stepId (t, s, jd::LaneDirection), rnd.nextFloat() < (group == 9 ? 0.3f : 0.1f) ? (float) (1 + rnd.nextInt (2)) : 0.f);
            set (jd::stepId (t, s, jd::LaneLength), group == 5 ? range (0.2f, 0.5f) : (group == 3 || group == 7 ? range (0.7f, 1.f) : range (0.45f, 0.95f)));
        }
    }

    // LFOs and what they move ----------------------------------------------------------------------------------
    for (int i = 0; i < jd::kLfos; ++i)
    {
        set (jd::lfoId (i, "shape"), (float) rnd.nextInt (7));
        set (jd::lfoId (i, "sync"), 1.f);
        set (jd::lfoId (i, "div"), (float) (group == 2 || group == 3 ? rnd.nextInt (2) : 1 + rnd.nextInt (4)));
        set (jd::lfoId (i, "depth"), range (0.4f, 0.9f));
        set (jd::lfoId (i, "polar"), 1.f);
    }
    const char* targets[] = { "fx_filter_cut", "fx_delay_mix", "fx_reverb_mix", "m_width", "fx_filter_res", "fx_delay_fb", "m_pan" };
    const int links = group == 9 ? 6 : group == 2 ? 4 : group == 3 ? 2 : 3;
    for (int i = 0; i < links; ++i)
        d.mods.push_back ({ targets[(i + k) % 7], i % jd::kLfos, range (0.15f, group == 9 ? 0.6f : 0.35f) * (rnd.nextBool() ? 1.f : -1.f) });
    return d;
}

inline const std::vector<Drawer>& factory()
{
    static const std::vector<Drawer> all = [] { std::vector<Drawer> v; for (int i = 0; i < 100; ++i) v.push_back (makeFactory (i)); return v; }();
    return all;
}

// ---- user drawers on disk ----------------------------------------------------------------------------------
inline juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Fading Fractal Audio").getChildFile ("Rotary Dial").getChildFile ("Stations");
}
inline juce::String safeName (juce::String n) { return juce::File::createLegalFileName (n.trim()).substring (0, 60); }

inline bool writeFile (const Drawer& d, const juce::File& f)
{
    juce::XmlElement x ("drawer");
    x.setAttribute ("name", d.name);
    for (auto& [id, v] : d.values)
    {
        auto* e = x.createNewChildElement ("v");
        e->setAttribute ("id", id); e->setAttribute ("x", (double) v);
    }
    for (auto& m : d.mods)
    {
        auto* e = x.createNewChildElement ("m");
        e->setAttribute ("id", m.target); e->setAttribute ("lfo", m.lfo); e->setAttribute ("amount", (double) m.amount);
    }
    return f.getParentDirectory().createDirectory().wasOk() && x.writeTo (f);
}

// Reads one .drawer file.  Anything that is not a drawer, or is absurdly large, is refused.
inline bool readFile (const juce::File& f, Drawer& d)
{
    if (! f.existsAsFile() || f.getSize() > 2 * 1024 * 1024) return false;
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("drawer")) return false;
    d = {};
    d.name = x->getStringAttribute ("name", f.getFileNameWithoutExtension()).trim();
    if (d.name.isEmpty()) d.name = f.getFileNameWithoutExtension();
    d.group = "Mine";
    int count = 0;
    for (auto* e : x->getChildWithTagNameIterator ("v")) { if (++count > 6000) break; d.values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("x"); }
    for (auto* e : x->getChildWithTagNameIterator ("m")) { if (++count > 6000) break; d.mods.push_back ({ e->getStringAttribute ("id"), e->getIntAttribute ("lfo"), (float) e->getDoubleAttribute ("amount") }); }
    return true;
}

inline bool saveUser (const Drawer& d)
{
    return writeFile (d, userFolder().getChildFile (safeName (d.name) + ".drawer"));
}

inline std::vector<Drawer> loadUser()
{
    std::vector<Drawer> out;
    for (auto& f : userFolder().findChildFiles (juce::File::findFiles, false, "*.drawer"))
        if (auto x = juce::XmlDocument::parse (f))
            if (x->hasTagName ("drawer"))
            {
                Drawer d; d.name = x->getStringAttribute ("name", f.getFileNameWithoutExtension()); d.group = "Mine";
                for (auto* e : x->getChildWithTagNameIterator ("v")) d.values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("x");
                for (auto* e : x->getChildWithTagNameIterator ("m")) d.mods.push_back ({ e->getStringAttribute ("id"), e->getIntAttribute ("lfo"), (float) e->getDoubleAttribute ("amount") });
                out.push_back (std::move (d));
            }
    std::sort (out.begin(), out.end(), [] (const Drawer& a, const Drawer& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}
} // namespace jdp
