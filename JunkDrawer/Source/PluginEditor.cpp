#include "PluginEditor.h"
#include "Exporter.h"
#include <BinaryData.h>
#include <map>

using namespace ui;
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
juce::Image& logoImage()
{
    static juce::Image img = juce::ImageCache::getFromMemory (BinaryData::ffa_mark_png, BinaryData::ffa_mark_pngSize);
    return img;
}

void drawLogo (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.drawImage (logoImage(), r.toFloat(), juce::RectanglePlacement::centred);
}

// Waveform preview for an LFO shape; w x h box, 0..1 sample positions.
juce::Path lfoPath (int shape, float w, float h)
{
    juce::Path p;
    const int N = 120;
    const float mid = h * 0.5f, amp = h * 0.36f;
    auto yAt = [&] (float t)
    {
        const float pi = juce::MathConstants<float>::pi;
        switch (shape)
        {
            case 0:  return mid - amp * std::sin (t * pi * 4.f);
            case 1:  return mid - amp * (1.f - 4.f * std::abs (std::fmod (t * 2.f, 1.f) - 0.5f));
            case 2:  return mid + amp - 2.f * amp * std::fmod (t * 3.f, 1.f);
            case 3:  return ((int) std::floor (t * 6.f)) % 2 == 0 ? mid - amp : mid + amp;
            case 4:  return mid - amp + 2.f * amp * std::abs (std::fmod (std::sin ((std::floor (t * 12.f) + 3.f) * 12.9898f), 1.f));
            case 6:  return mid - amp * 0.9f * std::sin (t * 5.2f + 1.f) * std::cos (t * 2.3f);
            default: return mid - amp * 0.95f * std::sin (t * 17.f) * std::sin (t * 5.f);
        }
    };
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i / N;
        const float x = t * w;
        if (i == 0) p.startNewSubPath (x, yAt (t)); else p.lineTo (x, yAt (t));
        if (shape == 3 || shape == 4)                                   // hold shapes step instead of slope
        {
            const float tn = (float) (i + 1) / N;
            if (i < N) p.lineTo (tn * w, yAt (t));
        }
    }
    return p;
}

// Small helper: a ParameterAttachment that just repaints a component when the value moves.
struct Watch
{
    Watch (APVTS& s, const juce::String& id, juce::Component& c)
        : att (*s.getParameter (id), [&c] (float) { c.repaint(); }, nullptr) { att.sendInitialUpdate(); }
    juce::ParameterAttachment att;
};

juce::String hookedNone() { return "Nothing attached yet."; }

// "Track 3 · Cutoff" and "Region B"-style names for a control, from its parameter ID.
std::pair<juce::String, juce::String> describeTarget (APVTS& s, const juce::String& id)
{
    static const std::map<juce::String, juce::String> region { { "start", "Start" }, { "end", "End" }, { "pitch", "Pitch" }, { "gain", "Gain" }, { "pan", "Pan" }, { "cutoff", "Cutoff" },
                                                               { "reso", "Reso" }, { "drive", "Drive" }, { "attack", "Attack" }, { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" } };
    const auto dot = juce::String::charToString (0x00b7);
    auto paramName = [&] { return s.getParameter (id) != nullptr ? s.getParameter (id)->getName (64) : id; };
    if (id.startsWithChar ('t') && juce::CharacterFunctions::isDigit (id[1]))
    {
        const int track = id.substring (1).getIntValue();
        const auto rest = id.fromFirstOccurrenceOf ("_", false, false);
        if (rest.startsWithChar ('r') && juce::CharacterFunctions::isDigit (rest[1]))
        {
            const int r = rest.substring (1).getIntValue();
            const auto what = rest.fromFirstOccurrenceOf ("_", false, false);
            const auto it = region.find (what);
            return { "Track " + juce::String (track) + " " + dot + " " + (it != region.end() ? it->second : what), "Region " + juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 5, r - 1))) };
        }
        return { "Track " + juce::String (track) + " " + dot + " " + (rest == "level" ? "Level" : (rest == "pan" ? "Pan" : rest)), "Track" };
    }
    if (id.startsWith ("m_"))  return { "Master " + dot + " " + paramName(), "Master page" };
    if (id.startsWith ("fx_"))
    {
        const auto fx = id.fromFirstOccurrenceOf ("_", false, false).upToFirstOccurrenceOf ("_", false, false);
        const juce::String names[] = { "Filter", "Delay", "Reverb", "Limiter" };
        const char* keys[] = { "filter", "delay", "reverb", "lim" };
        for (int i = 0; i < 4; ++i) if (fx == keys[i]) return { names[i] + " " + dot + " " + paramName(), "Master page, effect" };
    }
    if (id.startsWithChar ('l') && juce::CharacterFunctions::isDigit (id[1]))
    {
        const auto what = id.fromFirstOccurrenceOf ("_", false, false);
        const juce::String nice = what == "rate" ? "Rate" : (what == "depth" ? "Depth" : (what == "phase" ? "Phase" : (what == "fade" ? "Fade in" : what)));
        return { "LFO " + juce::String (id.substring (1).getIntValue()) + " " + dot + " " + nice, "LFOs page" };
    }
    return { paramName(), "" };
}

// "HOOKED UP" and a chip for each link that matches; "Nothing attached yet." when none do.
void drawHooked (juce::Graphics& g, int x, int y, int width, const std::function<bool (const LinkInfo&)>& match, const std::function<juce::String (const LinkInfo&)>& label)
{
    drawText (g, "HOOKED UP", { x, y, 80, 24 }, sans (11.f, true).withExtraKerningFactor (0.12f), pal::textDim);
    std::vector<LinkInfo> mine;
    if (auto* c = currentMod()) for (auto& l : c->links()) if (match (l)) mine.push_back (l);
    if (mine.empty()) { drawText (g, hookedNone(), { x + 88, y, 220, 24 }, mono (11.f), pal::textDim); return; }
    int cx = x + 88;
    for (size_t i = 0; i < mine.size(); ++i)
    {
        const auto t = label (mine[i]);
        const int w = chipWidth (t);
        const bool last = i + 1 == mine.size();
        const int more = (int) (mine.size() - i);
        if (cx + w + (last ? 0 : 40) > x + width)
        {
            drawChip (g, { cx, y + 1, chipWidth ("+" + juce::String (more)), 22 }, "+" + juce::String (more));
            return;
        }
        drawChip (g, { cx, y + 1, w, 22 }, t);
        cx += w + 6;
    }
}

void drawStripLabel (juce::Graphics& g, const juce::String& t, int x, int y)
{
    drawText (g, t, { x, y, 220, 14 }, sans (11.f, true).withExtraKerningFactor (0.16f), pal::amber);
}
} // namespace

//==============================================================================
// Header: logo, name, page tabs, preset bar, host tempo, Reduce motion
struct JunkDrawerEditor::Header : juce::Component, juce::Timer, juce::ChangeListener
{
    Header (JunkDrawerEditor& e) : ed (e)
    {
        ed.proc.addChangeListener (this);
        const char* names[] = { "Drawer", "LFOs", "Master", "Matrix", "Song" };
        const char* tips[]  = { "The main drawer", "The six LFOs", "Master and effects", "Everything that is hooked up", "Patterns and the chain" };
        for (int i = 0; i < NumPages; ++i)
        {
            auto& b = tabs[(size_t) i];
            b.setButtonText (names[i]);
            b.setTooltip (tips[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (101);
            b.onClick = [this, i] { ed.showPage (i); };
            addAndMakeVisible (b);
        }
        tabs[0].setToggleState (true, juce::dontSendNotification);
        reduce.setButtonText ("Reduce motion");
        reduce.setClickingTogglesState (true);
        reduce.setToggleState ((bool) ed.proc.state.state.getProperty ("reduceMotion", false), juce::dontSendNotification);
        reduce.setTooltip ("Turns off the fading glow on the playing step");
        reduce.onClick = [this] { ed.proc.state.state.setProperty ("reduceMotion", reduce.getToggleState(), nullptr); };
        addAndMakeVisible (reduce);
        exportBtn.setButtonText ("Export");
        exportBtn.setTooltip ("Play the pattern offline into audio files");
        exportBtn.onClick = [this] { ed.openExport(); };
        addAndMakeVisible (exportBtn);
        startTimerHz (4);
    }

    ~Header() override { ed.proc.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void select (int i) { tabs[(size_t) i].setToggleState (true, juce::dontSendNotification); }

    void resized() override
    {
        int x = 312;
        for (auto& b : tabs) { b.setBounds (x, 12, 62, 28); x += 66; }
        reduce.setBounds (getWidth() - 24 - 110, 12, 110, 28);
        exportBtn.setBounds (928, 12, 70, 28);
    }

    void timerCallback() override
    {
        double tempo = 0.0;
        if (auto* ph = ed.proc.getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto t = pos->getBpm()) tempo = *t;
        if (std::abs (tempo - bpm) > 0.001) { bpm = tempo; repaint (bpmArea()); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! barArea().contains (e.getPosition())) return;
        if (e.x < barArea().getX() + 30) ed.proc.stepDrawer (-1);
        else if (e.x > barArea().getRight() - 30) ed.proc.stepDrawer (1);
        else openMenu();
    }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (barArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }

    juce::Rectangle<int> barArea() const { return { 648, 11, 268, 30 }; }

    void openMenu()
    {
        juce::PopupMenu m;
        for (int gI = 0; gI < jdp::kGroups.size(); ++gI)
        {
            juce::PopupMenu sub;
            for (int k = 0; k < 10; ++k)
                sub.addItem (gI * 10 + k + 1, ed.proc.drawer (gI * 10 + k).name, true, ed.proc.currentDrawer() == gI * 10 + k);
            m.addSubMenu (jdp::kGroups[gI], sub);
        }
        if (ed.proc.drawerCount() > 100)
        {
            juce::PopupMenu mine;
            for (int i = 100; i < ed.proc.drawerCount(); ++i) mine.addItem (i + 1, ed.proc.drawer (i).name, true, ed.proc.currentDrawer() == i);
            m.addSubMenu ("Mine", mine);
        }
        m.addSeparator();
        m.addItem (100000, "Save this drawer as...");
        if (ed.proc.currentDrawer() >= 100)
        {
            m.addItem (100001, "Rename this drawer...");
            m.addItem (100002, "Delete this drawer...");
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (barArea())).withMinimumWidth (barArea().getWidth()),
                         [this, safe = juce::Component::SafePointer<Header> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 100000) { askName (false); return; }
            if (r == 100001) { askName (true); return; }
            if (r == 100002) { confirmDelete(); return; }
            ed.proc.loadDrawer (r - 1);
        });
    }

    void confirmDelete()
    {
        const int idx = ed.proc.currentDrawer();
        if (idx < 100) return;
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::NoIcon).withTitle ("Delete this drawer?")
                                          .withMessage ("\"" + ed.proc.drawer (idx).name + "\" will be removed from your saved drawers. Your current settings stay as they are.")
                                          .withButton ("Delete").withButton ("Keep it"),
                                      [this, safe = juce::Component::SafePointer<Header> (this), idx] (int r)
        {
            if (safe != nullptr && r == 1) ed.proc.deleteDrawer (idx);
        });
    }

    void askName (bool rename)
    {
        auto* w = new juce::AlertWindow (rename ? "Rename this drawer" : "Save this drawer", rename ? "Type the new name." : "Give it a name. It will show up under Mine.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", rename ? ed.proc.drawer (ed.proc.currentDrawer()).name : juce::String(), "");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        const int idx = ed.proc.currentDrawer();
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, safe = juce::Component::SafePointer<Header> (this), w, rename, idx] (int r)
        {
            if (r == 1 && safe != nullptr)
            {
                if (rename) ed.proc.renameDrawer (idx, w->getTextEditorContents ("name"));
                else ed.proc.saveDrawer (w->getTextEditorContents ("name"));
            }
            delete w;
        }), false);
    }

    juce::Rectangle<int> bpmArea() const { return { getWidth() - 24 - 110 - 14 - 110, 12, 110, 28 }; }

    void paint (juce::Graphics& g) override
    {
        drawLogo (g, { 24, 11, 58, 29 });
        drawText (g, "FADING FRACTAL AUDIO", { 94, 6, 220, 14 }, sans (11.f, true).withExtraKerningFactor (0.12f), juce::Colour (0xffd7c196));
        drawText (g, "Junk Drawer", { 94, 18, 220, 30 }, serif (25.f), juce::Colour (0xfff6e8c9));

        const juce::Rectangle<float> bar (648.f, 11.f, 268.f, 30.f);
        g.setColour (juce::Colour (0x52000000));
        g.fillRoundedRectangle (bar, 15.f);
        g.setColour (juce::Colour (0x80d6ac5c));
        g.drawRoundedRectangle (bar.reduced (0.5f), 15.f, 1.f);
        drawText (g, juce::String::charToString (0x2039), bar.toNearestInt().removeFromLeft (30), sans (16.f), pal::amber.withAlpha (0.4f), juce::Justification::centred);
        drawText (g, juce::String::charToString (0x203a), bar.toNearestInt().removeFromRight (30), sans (16.f), pal::amber.withAlpha (0.4f), juce::Justification::centred);
        const int cur = ed.proc.currentDrawer();
        const auto label = cur < 0 ? juce::String ("Pick a drawer") : juce::String (cur + 1).paddedLeft ('0', 3) + " " + juce::String::charToString (0x00b7) + " " + ed.proc.drawer (cur).name;
        drawText (g, label, bar.toNearestInt().reduced (32, 0), serif (15.f, false), cur < 0 ? pal::textDim : pal::text, juce::Justification::centred);

        if (ed.proc.armedLfo() >= 0)
        {
            const auto pill = bpmArea().withTrimmedLeft (-8).withTrimmedRight (-4);
            g.setColour (pal::amber); g.fillRoundedRectangle (pill.toFloat(), 14.f);
            drawText (g, "Assigning L" + juce::String (ed.proc.armedLfo() + 1), pill, sans (12.f, true), pal::amberInk, juce::Justification::centred);
        }
        else
            drawText (g, bpm > 0.0 ? juce::String (bpm, 1) + " BPM" : juce::String ("No host tempo"), bpmArea(), mono (12.f), pal::amber, juce::Justification::centredRight);
    }

    JunkDrawerEditor& ed;
    std::array<juce::TextButton, NumPages> tabs;
    juce::TextButton reduce, exportBtn;
    double bpm = 0.0;
};

//==============================================================================
// The open track: waveform with draggable regions, slice tools, and the tag for the selected region.
namespace
{
using Proc = JunkDrawerProcessor;

void setNorm (Proc& p, const juce::String& id, float plain)
{
    auto* prm = p.state.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

float plainValue (Proc& p, const juce::String& id) { return p.state.getRawParameterValue (id)->load(); }

juce::String seconds (double s) { return juce::String (s, s < 10.0 ? 2 : 1) + " s"; }

juce::Colour regionTint (int i)
{
    static const juce::uint32 t[] = { 0xffe9b872, 0xff9bc4a0, 0xffc98f7a, 0xff8fb0d4, 0xffd4c06a, 0xffb59ad1 };
    return juce::Colour (t[juce::jlimit (0, 5, i)]);
}

struct WaveView : juce::Component, juce::SettableTooltipClient
{
    explicit WaveView (Proc& p) : proc (p)
    {
        setTooltip ("Drag a region edge to move it. Click inside a region to pick it and hear it.");
    }

    void setTrack (int t, int sel) { track = t; selected = sel; peaksFor = nullptr; repaint(); }
    void setSelected (int r) { selected = r; repaint(); }
    int regionCount() const { return juce::roundToInt (plainValue (proc, jd::trackId (track, "regions"))); }
    float edge (int r, bool end) const { return plainValue (proc, jd::regionId (track, r, end ? "end" : "start")); }

    std::function<void (int)> onPick;

    void resized() override { peaksFor = nullptr; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff1b120b));
        g.fillRoundedRectangle (r, 4.f);

        auto td = proc.getTrack (track);
        if (td == nullptr) return;
        buildPeaks (td);

        const int w = getWidth(), h = getHeight(), mid = h / 2;
        const int n = regionCount();

        for (int i = 0; i < n; ++i)
        {
            const float a = edge (i, false) * (float) w, b = edge (i, true) * (float) w;
            const auto box = juce::Rectangle<float> (juce::jmin (a, b), 2.f, std::abs (b - a), (float) h - 4.f);
            g.setColour (regionTint (i).withAlpha (i == selected ? 0.34f : 0.16f));
            g.fillRect (box);
        }

        g.setColour (pal::text.withAlpha (0.9f));
        for (int x = 0; x < w && x < (int) mins.size(); ++x)
        {
            const float top = (float) mid - maxs[(size_t) x] * (float) (mid - 6), bot = (float) mid - mins[(size_t) x] * (float) (mid - 6);
            g.fillRect ((float) x, top, 1.f, juce::jmax (1.f, bot - top));
        }

        for (int i = 0; i < n; ++i)
        {
            const float a = edge (i, false) * (float) w, b = edge (i, true) * (float) w;
            const bool sel = i == selected;
            g.setColour (regionTint (i).withAlpha (sel ? 1.f : 0.65f));
            g.fillRect (a - 1.f, 2.f, 2.f, (float) h - 4.f);
            g.fillRect (b - 1.f, 2.f, 2.f, (float) h - 4.f);
            if (std::abs (b - a) > 26.f)
            {
                const auto tab = juce::Rectangle<float> (juce::jmin (a, b) + 4.f, 6.f, 18.f, 16.f);
                g.setColour (regionTint (i));
                g.fillRoundedRectangle (tab, 3.f);
                drawText (g, juce::String (i + 1), tab.toNearestInt(), mono (11.f, true), pal::amberInk, juce::Justification::centred);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (findEdge (e.x).first >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (proc.getTrack (track) == nullptr) return;
        dragRegion = -1;
        auto [r, isEnd] = findEdge (e.x);
        if (r >= 0)
        {
            dragRegion = r; dragEnd = isEnd;
            if (r != selected) { selected = r; if (onPick) onPick (r); }
            proc.state.getParameter (jd::regionId (track, r, isEnd ? "end" : "start"))->beginChangeGesture();
            return;
        }
        // click inside a region: pick the narrowest one that contains the point
        const float nx = (float) e.x / (float) juce::jmax (1, getWidth());
        int best = -1;
        float bestLen = 2.f;
        for (int i = 0; i < regionCount(); ++i)
        {
            const float a = juce::jmin (edge (i, false), edge (i, true)), b = juce::jmax (edge (i, false), edge (i, true));
            if (nx >= a && nx <= b && b - a < bestLen) { best = i; bestLen = b - a; }
        }
        if (best >= 0) { selected = best; if (onPick) onPick (best); proc.auditionRegion (track, best); repaint(); }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragRegion < 0) return;
        const float minGap = 0.003f;
        float v = snapped (juce::jlimit (0.f, 1.f, (float) e.x / (float) juce::jmax (1, getWidth())));
        const float other = edge (dragRegion, ! dragEnd);
        v = dragEnd ? juce::jlimit (other + minGap, 1.f, v) : juce::jlimit (0.f, other - minGap, v);
        setNorm (proc, jd::regionId (track, dragRegion, dragEnd ? "end" : "start"), v);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragRegion >= 0)
        {
            proc.state.getParameter (jd::regionId (track, dragRegion, dragEnd ? "end" : "start"))->endChangeGesture();
            dragRegion = -1;
        }
    }

private:
    std::pair<int, bool> findEdge (int x) const
    {
        if (proc.getTrack (track) == nullptr) return { -1, false };
        const float w = (float) getWidth();
        int bestR = -1;
        bool bestEnd = false;
        float bestD = 7.f;
        for (int pass = 0; pass < 2; ++pass)                    // the selected region's edges win ties
            for (int i = 0; i < regionCount(); ++i)
            {
                if ((pass == 0) != (i == selected)) continue;
                for (bool end : { false, true })
                {
                    const float d = std::abs (edge (i, end) * w - (float) x);
                    if (d < bestD) { bestD = d; bestR = i; bestEnd = end; }
                }
            }
        return { bestR, bestEnd };
    }

    float snapped (float n) const
    {
        auto td = proc.getTrack (track);
        if (td == nullptr) return n;
        const int mode = juce::roundToInt (plainValue (proc, "snap"));
        if (mode == 1) return jd::snapToOnset (proc.hitsFor (track), td->seconds(), n);
        if (mode == 2) return jd::snapToZero (td->audio, td->sampleRate, n);
        return n;
    }

    void buildPeaks (const std::shared_ptr<const jd::TrackData>& td)
    {
        if (peaksFor == td.get() && (int) mins.size() == getWidth()) return;
        peaksFor = td.get();
        const int w = juce::jmax (1, getWidth()), len = td->length();
        mins.assign ((size_t) w, 0.f);
        maxs.assign ((size_t) w, 0.f);
        const float* l = td->audio.getReadPointer (0);
        const float* r = td->audio.getReadPointer (1);
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((juce::int64) x * len / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * len / w));
            float lo = 0.f, hi = 0.f;
            for (int i = a; i < b && i < len; ++i) { const float m = 0.5f * (l[i] + r[i]); lo = juce::jmin (lo, m); hi = juce::jmax (hi, m); }
            mins[(size_t) x] = lo; maxs[(size_t) x] = hi;
        }
    }

    Proc& proc;
    int track = 0, selected = 0, dragRegion = -1;
    bool dragEnd = false;
    const jd::TrackData* peaksFor = nullptr;
    std::vector<float> mins, maxs;
};

// Everything about the selected region: Pitch, Gain, Pan / Cutoff, Reso, Drive / loop mode, Reverse / A D S R.
struct RegionTag : Panel
{
    RegionTag (Proc& p, int track, int region) : Panel(), proc (p), tr (track), reg (region)
    {
        auto& s = p.state;
        const char* ids[]   = { "pitch", "gain", "pan", "cutoff", "reso", "drive" };
        const char* names[] = { "Pitch", "Gain", "Pan", "Cutoff", "Reso", "Drive" };
        for (int k = 0; k < 6; ++k) { main[(size_t) k] = std::make_unique<Knob> (s, jd::regionId (track, region, ids[k]), names[k], 38); addAndMakeVisible (*main[(size_t) k]); }
        const char* eIds[]   = { "attack", "decay", "sustain", "release" };
        const char* eNames[] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) { env[(size_t) k] = std::make_unique<Knob> (s, jd::regionId (track, region, eIds[k]), eNames[k], 30); addAndMakeVisible (*env[(size_t) k]); }
        loop = std::make_unique<Seg> (s, jd::regionId (track, region, "loop"), jd::kLoopModes);
        loop->setTooltip ("What happens when playback reaches the end of the region");
        addAndMakeVisible (*loop);
        rev.setButtonText ("Reverse");
        rev.setClickingTogglesState (true);
        rev.setTooltip ("Play this region backwards");
        addAndMakeVisible (rev);
        revAtt = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter (jd::regionId (track, region, "reverse")), rev, nullptr);
        tuneBtn.setButtonText ("Tune");
        tuneBtn.setTooltip ("Pull this region onto the nearest note of the project key, so it sounds in tune with the rest. It sets the Pitch knob.");
        tuneBtn.onClick = [this] { proc.tuneRegionToKey (tr, reg); repaint(); };
        addAndMakeVisible (tuneBtn);
        wStart = std::make_unique<Watch> (s, jd::regionId (track, region, "start"), *this);
        wEnd   = std::make_unique<Watch> (s, jd::regionId (track, region, "end"), *this);
    }

    void resized() override
    {
        for (int k = 0; k < 6; ++k) main[(size_t) k]->setBounds (6 + (k % 3) * 68, 34 + (k / 3) * 80, 68, 76);
        loop->setBounds (12, 198, getWidth() - 24, 24);
        rev.setBounds (12, 228, 90, 24);
        tuneBtn.setBounds (getWidth() - 12 - 70, 273, 70, 22);
        for (int k = 0; k < 4; ++k) env[(size_t) k]->setBounds (10 + k * 49, 318, 49, 64);
    }

    void paint (juce::Graphics& g) override
    {
        Panel::paint (g);
        drawStripLabel (g, "REGION " + juce::String (reg + 1), 12, 12);
        auto td = proc.getTrack (tr);
        const double dur = td != nullptr ? td->seconds() : 0.0;
        const float a = plainValue (proc, jd::regionId (tr, reg, "start")), b = plainValue (proc, jd::regionId (tr, reg, "end"));
        drawText (g, "Start " + seconds (a * dur), { 12, 258, 100, 16 }, mono (11.f), pal::amber);
        drawText (g, "End " + seconds (b * dur), { 12, 274, 100, 16 }, mono (11.f), pal::amber);
        if (td != nullptr && (a != seenA || b != seenB || td.get() != seenData)) { seenA = a; seenB = b; seenData = td.get(); pitch = proc.regionPitch (tr, reg); }
        drawText (g, pitch.found ? jd::noteText (pitch.midi) : juce::String ("no note"), { getWidth() - 12 - 90, 256, 90, 16 }, mono (11.f, true), pitch.found ? pal::text : pal::textDim, juce::Justification::centredRight);
        tuneBtn.setEnabled (pitch.found);
        drawStripLabel (g, "ENVELOPE", 12, 298);
    }

    Proc& proc;
    int tr, reg;
    std::array<std::unique_ptr<Knob>, 6> main;
    std::array<std::unique_ptr<Knob>, 4> env;
    std::unique_ptr<Seg> loop;
    juce::TextButton rev;
    std::unique_ptr<juce::ButtonParameterAttachment> revAtt;
    std::unique_ptr<Watch> wStart, wEnd;
    juce::TextButton tuneBtn;
    jd::PitchResult pitch;
    float seenA = -1.f, seenB = -1.f;
    const jd::TrackData* seenData = nullptr;
};

// Dark pad set into the paper behind a control, because the controls are cream on dark.
void padBehind (juce::Graphics& g, std::initializer_list<juce::Component*> cs)
{
    g.setColour (juce::Colour (0xf0241710));
    for (auto* c : cs) if (c != nullptr && c->isVisible()) g.fillRoundedRectangle (c->getBounds().toFloat(), 4.f);
}

// The step lane editor: 16 cells for the chosen lane, plus a small overview of the volume lane underneath.
struct StepGrid : juce::Component, juce::Timer, juce::SettableTooltipClient
{
    explicit StepGrid (Proc& p) : proc (p)
    {
        setTooltip ("Click a step to change it, drag across steps to paint, double-click to reset, right-click to clear a region step.");
    }

    static constexpr int labelW = 64, stripW = 592, stripH = 70, overviewY = 80, overviewH = 22;

    void setTrack (int t) { track = t; sig = -1; repaint(); }
    void setLane (int l) { lane = l; sig = -1; repaint(); }
    int laneIndex() const { return lane; }
    std::function<void()> onChanged;

    void visibilityChanged() override { if (isShowing()) startTimerHz (30); else stopTimer(); }

    void timerCallback() override
    {
        juce::int64 h = 1469598103934665603LL;
        auto mix = [&] (float v) { h = (h ^ (juce::int64) std::lround (v * 1000.f)) * 1099511628211LL; };
        mix ((float) track); mix ((float) lane);
        mix ((float) proc.laneLength (track, lane)); mix ((float) proc.laneLength (track, jd::LaneVolume));
        mix (plainValue (proc, jd::trackId (track, "regions")));
        for (int s = 0; s < jd::kSteps; ++s) { mix (plainValue (proc, jd::stepId (track, s, lane))); mix (plainValue (proc, jd::stepId (track, s, jd::LaneVolume))); }
        mix ((float) (currentIndex (lane) * 7 + currentIndex (jd::LaneVolume)));
        // The step that just played fades out over about a sixth of a second.  With Reduce motion on it just switches off.
        const bool still = (bool) proc.state.state.getProperty ("reduceMotion", false);
        const int cur = currentIndex (lane);
        bool fading = false;
        for (int i = 0; i < jd::kSteps; ++i)
        {
            const float before = glow[(size_t) i];
            glow[(size_t) i] = i == cur ? 1.f : (still ? 0.f : juce::jmax (0.f, before - 0.2f));
            fading = fading || glow[(size_t) i] != before;
        }
        if (h != sig) { sig = h; repaint(); if (onChanged) onChanged(); }
        else if (fading) repaint();
    }

    std::array<float, jd::kSteps> glow {};

    int currentIndex (int ln) const
    {
        const auto s = proc.uiStep.load (std::memory_order_relaxed);
        if (s < 0) return -1;
        const int len = proc.laneLength (track, ln);
        return (int) (((s % len) + len) % len);
    }

    float cellW() const { return ((float) stripW - 6.f - 45.f) / 16.f; }
    float cellX (int i) const { return (float) labelW + 3.f + (float) i * (cellW() + 3.f); }

    static float defaultFor (int ln) { return ln == jd::LaneVolume || ln == jd::LaneLength || ln == jd::LaneProb || ln == jd::LaneRatchet ? 1.f : 0.f; }

    void paint (juce::Graphics& g) override
    {
        static const char* names[] = { "Region", "Volume", "Pitch", "Ratchet", "Direction", "Length", "Chance", "Timing", "Pan" };
        drawText (g, names[lane], { 0, 0, labelW - 6, stripH }, sans (11.f, true), juce::Colour (0xff4a3320));
        drawText (g, "Volume", { 0, overviewY, labelW - 6, overviewH }, sans (11.f, true), juce::Colour (0xff4a3320));

        const juce::Rectangle<float> strip ((float) labelW, 0.f, (float) stripW, (float) stripH);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a7a52), 0.f, 0.f, juce::Colour (0xff7f6038), 0.f, (float) stripH, false));
        g.fillRoundedRectangle (strip, 5.f);

        const int len = proc.laneLength (track, lane), cur = currentIndex (lane), count = juce::roundToInt (plainValue (proc, jd::trackId (track, "regions")));
        for (int i = 0; i < jd::kSteps; ++i)
        {
            const juce::Rectangle<float> c (cellX (i), 3.f, cellW(), (float) stripH - 6.f);
            const bool on = i < len;
            g.setColour (juce::Colour (0xff2a1b10).withAlpha (on ? 0.6f : 0.22f));
            g.fillRoundedRectangle (c, 3.f);
            const float v = plainValue (proc, jd::stepId (track, i, lane));
            if (on) paintValue (g, c, v, count);
            drawText (g, juce::String (i + 1), { (int) c.getX(), (int) c.getBottom() - 15, (int) c.getWidth(), 14 }, mono (11.f), juce::Colour (0xffcdb589).withAlpha (on ? 1.f : 0.45f), juce::Justification::centred);
            if (glow[(size_t) i] > 0.f) { g.setColour (pal::amber.withAlpha (0.22f * glow[(size_t) i])); g.fillRoundedRectangle (c, 3.f); }
            if (i == cur) { g.setColour (pal::amber); g.drawRoundedRectangle (c.reduced (0.5f), 3.f, 2.f); }
        }

        // overview of the volume lane
        const int vlen = proc.laneLength (track, jd::LaneVolume), vcur = currentIndex (jd::LaneVolume);
        g.setColour (juce::Colour (0x66402a14));
        g.fillRect ((float) labelW, (float) overviewY + overviewH - 1.f, (float) stripW, 1.f);
        for (int i = 0; i < jd::kSteps; ++i)
        {
            const float v = plainValue (proc, jd::stepId (track, i, jd::LaneVolume));
            const float h = juce::jmax (1.f, v * (float) (overviewH - 3));
            g.setColour ((i == vcur ? pal::amber : juce::Colour (0xff5a3a1c)).withAlpha (i < vlen ? 1.f : 0.3f));
            g.fillRect (cellX (i), (float) overviewY + overviewH - 1.f - h, cellW(), h);
        }
    }

    void paintValue (juce::Graphics& g, juce::Rectangle<float> c, float v, int count)
    {
        const auto bar = juce::Rectangle<float> (c.getX() + 4.f, c.getY() + 18.f, c.getWidth() - 8.f, c.getHeight() - 38.f);
        auto fill = [&] (float frac) { g.setColour (pal::amber); g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.f - juce::jlimit (0.f, 1.f, frac)))); };
        auto top = [&] (const juce::String& t) { drawText (g, t, { (int) c.getX(), (int) c.getY() + 2, (int) c.getWidth(), 14 }, mono (11.f), pal::text, juce::Justification::centred); };
        if (lane != jd::LaneRegion && lane != jd::LaneDirection) { g.setColour (juce::Colour (0x40f0e2c4)); g.drawRect (bar, 1.f); }
        switch (lane)
        {
            case jd::LaneRegion:
                if (juce::roundToInt (v) > 0)
                {
                    const int r = juce::roundToInt (v) - 1;
                    const auto chip = c.withSizeKeepingCentre (c.getWidth() - 8.f, 30.f).withY (c.getY() + 14.f);
                    g.setColour (regionTint (r).withAlpha (r < count ? 1.f : 0.35f));
                    g.fillRoundedRectangle (chip, 4.f);
                    drawText (g, juce::String (r + 1), chip.toNearestInt(), mono (13.f, true), pal::amberInk, juce::Justification::centred);
                }
                break;
            case jd::LaneVolume: fill (v); top (juce::String (juce::roundToInt (v * 100.f))); break;
            case jd::LaneLength: fill ((v - 0.1f) / 0.9f); top (juce::String (juce::roundToInt (v * 100.f))); break;
            case jd::LanePitch:
            {
                const float mid = bar.getCentreY(), y = mid - (v / 12.f) * (bar.getHeight() * 0.5f);
                g.setColour (pal::amber);
                g.fillRect (bar.getX(), juce::jmin (mid, y), bar.getWidth(), juce::jmax (1.5f, std::abs (y - mid)));
                g.setColour (juce::Colour (0x80f0e2c4)); g.fillRect (bar.getX(), mid, bar.getWidth(), 1.f);
                top ((v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)));
                break;
            }
            case jd::LaneRatchet: fill (juce::roundToInt (v) / 4.f); top ("x" + juce::String (juce::roundToInt (v))); break;
            case jd::LaneProb: fill (v); top (juce::String (juce::roundToInt (v * 100.f))); break;
            case jd::LanePan:
            {
                const float mid = bar.getCentreY(), y = mid - v * (bar.getHeight() * 0.5f);
                g.setColour (pal::amber);
                g.fillRect (bar.getX(), juce::jmin (mid, y), bar.getWidth(), juce::jmax (1.5f, std::abs (y - mid)));
                g.setColour (juce::Colour (0x80f0e2c4)); g.fillRect (bar.getX(), mid, bar.getWidth(), 1.f);
                const int pc = juce::roundToInt (v * 100.f);
                top (pc == 0 ? juce::String ("C") : (pc < 0 ? "L" : "R") + juce::String (std::abs (pc)));
                break;
            }
            case jd::LaneTime:
            {
                const float mid = bar.getCentreY(), y = mid - (v / 0.5f) * (bar.getHeight() * 0.5f);
                g.setColour (pal::amber);
                g.fillRect (bar.getX(), juce::jmin (mid, y), bar.getWidth(), juce::jmax (1.5f, std::abs (y - mid)));
                g.setColour (juce::Colour (0x80f0e2c4)); g.fillRect (bar.getX(), mid, bar.getWidth(), 1.f);
                const int pc = juce::roundToInt (v * 100.f);
                top ((pc > 0 ? "+" : "") + juce::String (pc));
                break;
            }
            case jd::LaneDirection:
            {
                const int d = juce::roundToInt (v);
                const auto chip = c.withSizeKeepingCentre (c.getWidth() - 8.f, 30.f).withY (c.getY() + 14.f);
                g.setColour (pal::amber.withAlpha (d == 0 ? 0.25f : 1.f));
                g.fillRoundedRectangle (chip, 4.f);
                drawText (g, d == 0 ? "Reg" : (d == 1 ? "Fwd" : "Rev"), chip.toNearestInt(), sans (11.f, true), d == 0 ? pal::text : pal::amberInk, juce::Justification::centred);
                break;
            }
        }
    }

    int cellAt (int x) const
    {
        for (int i = 0; i < jd::kSteps; ++i) if ((float) x >= cellX (i) - 1.5f && (float) x < cellX (i) + cellW() + 1.5f) return i;
        return -1;
    }

    void setStep (int i, float plain)
    {
        auto* p = proc.state.getParameter (jd::stepId (track, i, lane));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }

    void editAt (const juce::MouseEvent& e, bool first)
    {
        const int i = cellAt (e.x);
        if (i < 0 || i >= proc.laneLength (track, lane) || e.y > stripH) return;
        const float cur = plainValue (proc, jd::stepId (track, i, lane));
        if (lane == jd::LaneRegion || lane == jd::LaneDirection)
        {
            if (! first) return;
            const int top = lane == jd::LaneRegion ? juce::roundToInt (plainValue (proc, jd::trackId (track, "regions"))) : 2;
            const int now = juce::roundToInt (cur);
            setStep (i, e.mods.isPopupMenu() ? 0.f : (float) (now >= top ? 0 : now + 1));
            return;
        }
        const float yn = 1.f - juce::jlimit (0.f, 1.f, ((float) e.y - 21.f) / ((float) stripH - 41.f));
        switch (lane)
        {
            case jd::LaneVolume: setStep (i, yn); break;
            case jd::LaneLength: setStep (i, 0.1f + 0.9f * yn); break;
            case jd::LanePitch:  setStep (i, (float) juce::roundToInt (-12.f + 24.f * yn)); break;
            case jd::LaneRatchet: setStep (i, (float) juce::jlimit (1, 4, 1 + (int) (yn * 4.f))); break;
            case jd::LaneProb:   setStep (i, std::round (yn * 20.f) / 20.f); break;
            case jd::LanePan:    setStep (i, std::round ((yn - 0.5f) * 40.f) / 20.f); break;
            case jd::LaneTime:   setStep (i, std::round ((yn - 0.5f) * 20.f) / 20.f); break;
            default: break;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { editAt (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { editAt (e, false); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = cellAt (e.x);
        if (i >= 0 && e.y <= stripH) setStep (i, defaultFor (lane));
    }

    Proc& proc;
    int track = 0, lane = 0;
    juce::int64 sig = -1;
};

// Catch something: the panel that takes the place of the waveform sheet while a take is set up or running.
struct RecPanel : juce::Component, juce::Timer
{
    explicit RecPanel (Proc& p) : proc (p), input ({ "1 + 2", "Left", "Right" }), start ({ "Now", "Next bar", "Level" }), length ({ "Free", "1 bar", "2 bars", "4 bars" })
    {
        input.setTooltip ("Which input channels to record. Left or Right alone is copied to both sides.");
        start.setTooltip ("Now starts at once. Next bar waits for the next bar line while the host plays (with the host stopped it starts at once). Level waits for sound, and keeps the moment just before it.");
        length.setTooltip ("Free runs until you press Stop. A bar count stops by itself, up to the 60 second limit.");
        go.setTooltip ("Start catching"); cancel.setTooltip ("Close without recording");
        go.onClick = [this]
        {
            if (proc.catchState() == Proc::CatchIdle) { static const int bars[] = { 0, 1, 2, 4 }; proc.startCatch (track, input.selected(), start.selected(), bars[length.selected()]); }
            else proc.stopCatch (true);
            update();
        };
        cancel.onClick = [this]
        {
            if (proc.catchState() == Proc::CatchIdle) { if (onClose) onClose(); }
            else proc.stopCatch (false);
            update();
        };
        listen.setClickingTogglesState (true);
        listen.setButtonText ("Keep listening");
        listen.setTooltip ("Keeps the last 30 seconds of input in memory, so you can save something that already went by. Uses about 12 MB while it is on.");
        listen.setToggleState (proc.listening(), juce::dontSendNotification);
        listen.onClick = [this] { proc.setListening (listen.getToggleState()); update(); };
        grab.setButtonText ("Save last 30 s");
        grab.setTooltip ("Grab whatever just went by and put it in this track");
        grab.onClick = [this] { if (proc.catchLast (track) && onClose) onClose(); };
        for (auto* c : std::initializer_list<juce::Component*> { &input, &start, &length, &go, &cancel, &listen, &grab }) addAndMakeVisible (*c);
        update();
    }
    ~RecPanel() override { proc.setInputMeter (false); }

    void setTrack (int t) { track = t; update(); }
    void visibilityChanged() override
    {
        proc.setInputMeter (isShowing());
        if (isShowing()) { wasActive = proc.catchState() != Proc::CatchIdle; startTimerHz (30); update(); } else stopTimer();
    }

    void timerCallback() override
    {
        shown = juce::jmax (proc.takeInputPeak(), shown * 0.86f);
        const bool active = proc.catchState() != Proc::CatchIdle;
        if (wasActive && ! active && onClose) { wasActive = false; onClose(); return; }
        wasActive = active;
        update();
        repaint();
    }

    void update()
    {
        const bool active = proc.catchState() != Proc::CatchIdle;
        for (auto* c : std::initializer_list<juce::Component*> { &input, &start, &length }) c->setEnabled (! active);
        listen.setVisible (! active);
        grab.setVisible (! active);
        grab.setEnabled (proc.listening() && proc.catchLastSeconds() >= 0.25f);
        listen.setToggleState (proc.listening(), juce::dontSendNotification);
        go.setButtonText (active ? "Stop" : "Record");
        cancel.setButtonText (active ? "Discard" : "Cancel");
        go.setTooltip (active ? "Stop and keep the take" : "Start catching");
        cancel.setTooltip (active ? "Throw the take away" : "Close without recording");
    }

    static juce::String clock (double s)
    {
        const int m = (int) (s / 60.0);
        return juce::String (m) + ":" + juce::String (s - m * 60.0, 1).paddedLeft ('0', 4);
    }

    void paint (juce::Graphics& g) override
    {
        update();
        const auto ink = juce::Colour (0xff2a1b10), red = juce::Colour (0xffb5532f), sub = juce::Colour (0xff5a4329);
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xfff1e7c9)); g.fillRoundedRectangle (r, 3.f);
        g.setColour (juce::Colour (0x4d5a3c1e)); g.drawRoundedRectangle (r.reduced (0.5f), 3.f, 1.f);

        const int st = proc.catchState();
        const bool running = st == Proc::CatchRunning, waiting = st == Proc::CatchWaiting;
        g.setColour (running ? red : (waiting ? juce::Colour (0xffb8893a) : juce::Colour (0xff9a8a68)));
        g.fillEllipse (14.f, 15.f, 11.f, 11.f);
        const juce::String stateLabel = running ? "CATCHING" : waiting ? (start.selected() == 1 ? "WAITING FOR THE BAR" : "WAITING FOR SOUND") : "READY";
        drawText (g, stateLabel, { 32, 10, 190, 20 }, sans (11.f, true).withExtraKerningFactor (0.14f), running ? red : sub);
        if (running) drawText (g, clock (proc.catchSeconds()), { 222, 10, 70, 20 }, mono (13.f, true), ink);
        drawText (g, "Up to 1:00 per track", { 272, 10, 134, 20 }, sans (11.f), sub, juce::Justification::centredRight);

        const juce::Rectangle<int> well (14, 34, 392, 164);
        g.setColour (juce::Colour (0xffe6dab6)); g.fillRoundedRectangle (well.toFloat(), 2.f);
        g.setColour (juce::Colour (0x59281608)); g.drawRoundedRectangle (well.toFloat().reduced (0.5f), 2.f, 1.f);
        const int mid = well.getCentreY() - 8;
        const double elapsed = proc.catchSeconds();
        if (st != Proc::CatchIdle || elapsed > 0.0) { g.setColour (juce::Colour (0x47281608)); g.fillRect (well.getX(), mid, well.getWidth(), 1); }

        if (st == Proc::CatchIdle && elapsed <= 0.0)
            drawText (g, "Press Record to start catching.", well.withTrimmedBottom (44), serif (15.f, false), ink.withAlpha (0.75f), juce::Justification::centred);
        else
        {
            const double span = juce::jmax (10.0, std::ceil (elapsed / 5.0) * 5.0 + 5.0);
            const int cols = juce::jlimit (0, well.getWidth(), (int) std::round (elapsed / span * well.getWidth()));
            std::vector<float> pk ((size_t) juce::jmax (1, cols));
            if (cols > 0) proc.catchPeaks (pk.data(), cols);
            g.setColour (juce::Colour (0xe03a2415));
            for (int x = 0; x < cols; ++x)
            {
                const float h = juce::jmax (1.f, pk[(size_t) x] * (float) (well.getHeight() - 40) * 0.5f);
                g.fillRect ((float) (well.getX() + x), (float) mid - h, 1.f, 2.f * h);
            }
            g.setColour (red); g.fillRect (well.getX() + cols, well.getY(), 2, well.getHeight());
            const double step = span <= 20.0 ? 5.0 : span <= 40.0 ? 10.0 : 15.0;
            for (double t = 0.0; t < span - 0.01; t += step)
            {
                const int x = well.getX() + (int) std::round (t / span * well.getWidth());
                g.setColour (juce::Colour (0x80281608)); g.fillRect (x, well.getBottom() - 8, 1, 8);
                drawText (g, clock (t).dropLastCharacters (2), { x + 3, well.getBottom() - 20, 40, 16 }, mono (11.f), sub);
            }
        }

        if (st == Proc::CatchIdle)
        {
            const float have = proc.catchLastSeconds();
            drawText (g, proc.listening() ? (have < 0.25f ? juce::String ("Listening. Nothing yet.") : juce::String (have, 1) + " s ready") : juce::String ("Not listening"),
                      { well.getX() + 118, well.getBottom() - 34, 130, 24 }, sans (11.f), sub, juce::Justification::centred);
            g.setColour (juce::Colour (0xf0241710));
            for (auto* c : { &listen, &grab }) g.fillRoundedRectangle (c->getBounds().toFloat(), 4.f);
        }

        const int cx = 424;
        auto label = [&] (const juce::String& t, int y) { drawText (g, t, { cx, y, 216, 12 }, sans (11.f, true).withExtraKerningFactor (0.14f), sub); };
        label ("INPUT", 8); label ("START", 71); label ("LENGTH", 117);

        const float db = juce::Decibels::gainToDecibels (shown, -60.f);
        const juce::Rectangle<float> bar ((float) cx, 54.f, 164.f, 8.f);
        g.setColour (juce::Colour (0xff3a2415)); g.fillRoundedRectangle (bar, 4.f);
        const float f = juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f);
        g.saveState(); g.reduceClipRegion (bar.withWidth (bar.getWidth() * f).toNearestInt());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9bb36a), bar.getX(), 0.f, juce::Colour (0xffb5654f), bar.getRight(), 0.f, false));
        g.fillRoundedRectangle (bar, 4.f); g.restoreState();
        drawText (g, db <= -59.f ? "-inf" : juce::String (juce::roundToInt (db)) + " dB", { cx + 172, 49, 44, 18 }, mono (11.f), sub, juce::Justification::centredRight);

        g.setColour (juce::Colour (0xf0241710));
        for (auto* c : std::initializer_list<juce::Component*> { &input, &start, &length, &cancel }) g.fillRoundedRectangle (c->getBounds().toFloat(), 4.f);
        g.setColour (st == Proc::CatchIdle ? red : juce::Colour (0xf0241710));
        g.fillRoundedRectangle (go.getBounds().toFloat(), 4.f);
        if (st == Proc::CatchIdle) { g.setColour (juce::Colour (0xfffff4e0)); g.fillEllipse ((float) go.getX() + 22.f, (float) go.getBounds().getCentreY() - 4.5f, 9.f, 9.f); }
        else { g.setColour (red); g.fillRoundedRectangle ((float) go.getX() + 22.f, (float) go.getBounds().getCentreY() - 4.5f, 9.f, 9.f, 1.f); }
    }

    void resized() override
    {
        const int cx = 424;
        input.setBounds (cx, 23, 216, 24);
        start.setBounds (cx, 86, 216, 24);
        length.setBounds (cx, 132, 216, 24);
        go.setBounds (cx, 164, 124, 28);
        cancel.setBounds (cx + 132, 164, 84, 28);
        listen.setBounds (14 + 6, 34 + 164 - 34, 108, 28);
        grab.setBounds (14 + 392 - 6 - 130, 34 + 164 - 34, 130, 28);
    }

    Proc& proc;
    int track = 0;
    bool wasActive = false;
    float shown = 0.f;
    ui::PickSeg input, start, length;
    juce::TextButton go, cancel, listen, grab;
    std::function<void()> onClose;
};

struct Liner : juce::Component, juce::FileDragAndDropTarget
{
    explicit Liner (Proc& p) : proc (p), wave (p), grid (p), rec (p)
    {
        browse.setButtonText ("Browse");  browse.setTooltip ("Pick a sound file. You can also drop one anywhere on this paper.");
        clear.setButtonText ("Clear");    clear.setTooltip ("Take the sound out of this track");
        record.setButtonText ("Record"); record.setTooltip ("Catch something: record from the plugin's audio input into this track");
        for (auto* b : { &browse, &clear, &record }) addAndMakeVisible (*b);
        laneBtn.setButtonText ("Edit");
        laneBtn.setTooltip ("The letter is the pattern you are editing. Copy this lane to paste on another track, nudge its values a little, or put it back to plain");
        laneBtn.onClick = [this] { openLaneMenu(); };
        addAndMakeVisible (laneBtn);
        keyBtn.setTooltip ("The project key. Match and Tune work towards it. Pick No key to tune to the nearest note only.");
        matchBtn.setButtonText ("Match");
        matchBtn.setTooltip ("Shift this whole track into the project key. It sets the track's Tune from the key found in the sound.");
        matchAllBtn.setButtonText ("Match all");
        matchAllBtn.setTooltip ("Set every track's Tune so its key lands in the project key. It is a one-time shift: you can still change any track afterwards.");
        matchAllBtn.onClick = [this]
        {
            const int n = proc.matchAllToKey();
            message = n == 0 ? "No track has a key to match yet." : juce::String (n) + (n == 1 ? " track" : " tracks") + " tuned to the project key.";
            repaint();
        };
        addAndMakeVisible (matchAllBtn);
        keyBtn.onClick = [this] { openKeyMenu(); };
        matchBtn.onClick = [this] { proc.matchTrackToKey (track); repaint(); };
        addAndMakeVisible (keyBtn); addAndMakeVisible (matchBtn);
        wRoot = std::make_unique<Watch> (p.state, "key_root", *this);
        wMode = std::make_unique<Watch> (p.state, "key_mode", *this);
        addChildComponent (rec);
        record.onClick = [this] { if (proc.catchState() == Proc::CatchIdle) { recOpen = ! recOpen; rec.setTrack (track); refresh(); } };
        rec.onClose = [this] { recOpen = false; refresh(); };
        browse.onClick = [this] { openChooser(); };
        clear.onClick = [this] { proc.clearTrack (track); };
        addAndMakeVisible (wave);
        addAndMakeVisible (grid);
        laneBtn.toFront (false);

        static const char* laneNames[] = { "Region", "Volume", "Pitch", "Ratchet", "Direction", "Length", "Chance", "Timing", "Pan" };
        static const char* laneTips[] = { "Which region plays on each step", "How loud each step is", "Shifts each step up or down in semitones", "Repeats a step 1 to 4 times",
                                          "Region keeps the region's own setting, or force forward or backward", "How long each step is held before it lets go",
                                          "How likely each step is to play. Below 100 it sometimes skips, the same way every run.", "Pushes each step early or late, by up to half a step", "Places each step left or right of the track's own pan" };
        for (int i = 0; i < jd::kLanes; ++i)
        {
            auto& b = tabs[(size_t) i];
            b.setButtonText (laneNames[i]); b.setTooltip (laneTips[i]);
            b.setClickingTogglesState (true); b.setRadioGroupId (202);
            b.onClick = [this, i] { grid.setLane (i); repaint(); };
            addAndMakeVisible (b);
        }
        tabs[0].setToggleState (true, juce::dontSendNotification);
        less.setButtonText ("-"); more.setButtonText ("+");
        less.setTooltip ("Fewer steps in this lane"); more.setTooltip ("More steps in this lane");
        less.onClick = [this] { nudgeLength (-1); };
        more.onClick = [this] { nudgeLength (1); };
        addAndMakeVisible (less); addAndMakeVisible (more);
        grid.onChanged = [this] { repaint (506, 262, 170, 34); };

        wave.onPick = [this] (int r) { pick (r); };
        setTrack (0);
    }

    int regionCount() const { return juce::roundToInt (plainValue (proc, jd::trackId (track, "regions"))); }

    void nudgeLength (int d)
    {
        const int lane = grid.laneIndex();
        setNorm (proc, jd::laneLenId (track, lane), (float) juce::jlimit (1, jd::kSteps, proc.laneLength (track, lane) + d));
        repaint (506, 262, 170, 34);
    }

    void openKeyMenu()
    {
        juce::PopupMenu m;
        const int root = proc.projectRoot(), mode = proc.projectMode();
        m.addItem (1, "No key (nearest note)", true, mode == 0);
        m.addSeparator();
        for (int i = 0; i < 12; ++i) m.addItem (2 + i, juce::String (jd::noteName (i)) + " major", true, mode == 1 && root == i);
        m.addSeparator();
        for (int i = 0; i < 12; ++i) m.addItem (14 + i, juce::String (jd::noteName (i)) + " minor", true, mode == 2 && root == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyBtn), [this, safe = juce::Component::SafePointer<Liner> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) setNorm (proc, "key_mode", 0.f);
            else { setNorm (proc, "key_mode", r < 14 ? 1.f : 2.f); setNorm (proc, "key_root", (float) ((r - 2) % 12)); }
            refresh();
        });
    }

    void openLaneMenu()
    {
        const int lane = grid.laneIndex();
        static const char* names[] = { "region", "volume", "pitch", "ratchet", "direction", "length", "chance", "timing" };
        juce::PopupMenu m;
        m.addItem (1, juce::String ("Copy the ") + names[lane] + " lane");
        m.addItem (2, "Paste", clip.lane == lane);
        m.addSeparator();
        m.addItem (3, "Humanize", Proc::canHumanize (lane));
        m.addItem (4, "Reset to plain");
        m.addSeparator();
        juce::PopupMenu edit, copyTo;
        for (int i = 0; i < jd::kPatterns; ++i)
        {
            const juce::String letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
            edit.addItem (100 + i, "Pattern " + letter, true, i == proc.editPattern());
            copyTo.addItem (200 + i, "Pattern " + letter, i != proc.editPattern());
        }
        juce::PopupMenu order;
        static const char* orderNames[] = { "As written", "Walk through the regions", "Pendulum", "Random" };
        const int curOrder = juce::roundToInt (proc.state.getRawParameterValue (jd::trackId (track, "order"))->load());
        for (int i = 0; i < 4; ++i) order.addItem (300 + i, orderNames[i], true, i == curOrder);
        m.addSubMenu ("Region order", order);
        juce::PopupMenu shape;
        static const char* shapeNames[] = { "Even", "Fade out", "Swell", "Climb" };
        const int curShape = juce::roundToInt (proc.state.getRawParameterValue (jd::trackId (track, "rshape"))->load());
        for (int i = 0; i < 4; ++i) shape.addItem (500 + i, shapeNames[i], true, i == curShape);
        m.addSubMenu ("Ratchet shape", shape);
        juce::PopupMenu cond;
        static const char* condNames[] = { "Every loop", "Every 2nd loop", "Every 3rd loop", "Every 4th loop", "First loop only" };
        const int curCond = juce::roundToInt (proc.state.getRawParameterValue (jd::trackId (track, "cond"))->load());
        for (int i = 0; i < 5; ++i) cond.addItem (400 + i, condNames[i], true, i == curCond);
        m.addSubMenu ("This track plays", cond);
        m.addSubMenu ("Edit pattern", edit);
        m.addSubMenu ("Copy this pattern to", copyTo);
        m.addItem (5, "Clear this pattern");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&laneBtn), [this, safe = juce::Component::SafePointer<Liner> (this), lane] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) clip = proc.copyLane (track, lane);
            else if (r == 2) proc.pasteLane (track, clip);
            else if (r == 3) proc.humanizeLane (track, lane);
            else if (r == 4) proc.resetLane (track, lane);
            else if (r == 5) proc.clearPattern();
            else if (r >= 500 && r < 504) setNorm (proc, jd::trackId (track, "rshape"), (float) (r - 500));
            else if (r >= 400 && r < 405) setNorm (proc, jd::trackId (track, "cond"), (float) (r - 400));
            else if (r >= 300 && r < 304) setNorm (proc, jd::trackId (track, "order"), (float) (r - 300));
            else if (r >= 100 && r < 100 + jd::kPatterns) proc.setEditPattern (r - 100);
            else if (r >= 200 && r < 200 + jd::kPatterns) proc.copyPatternTo (r - 200);
            syncPatternLabel();
            grid.repaint();
            repaint();
        });
    }

    juce::String keyButtonText() const
    {
        const int mode = proc.projectMode();
        return mode == 0 ? juce::String ("Key: none") : "Key: " + juce::String (jd::noteName (proc.projectRoot())) + (mode == 2 ? " minor" : " major");
    }

    void setTrack (int t)
    {
        track = t;
        wTune = std::make_unique<Watch> (proc.state, jd::trackId (t, "tune"), *this);
        tuneKnob = std::make_unique<Knob> (proc.state, jd::trackId (t, "tune"), "Tune", 22);
        tuneKnob->showText = false;
        tuneKnob->slider.setTooltip ("Tune this whole track up or down in semitones. Match sets it for you.");
        addAndMakeVisible (*tuneKnob);
        recOpen = proc.catchState() != Proc::CatchIdle && proc.catchTrack() == t;
        rec.setTrack (t);
        selected = juce::jlimit (0, juce::jmax (0, regionCount() - 1), regions[(size_t) t]);
        wave.setTrack (track, selected);
        grid.setTrack (track);
        buildTag();
        refresh();
    }

    void pick (int r)
    {
        selected = juce::jlimit (0, juce::jmax (0, regionCount() - 1), r);
        regions[(size_t) track] = selected;
        wave.setSelected (selected);
        buildTag();
    }

    void buildTag()
    {
        tag.reset();
        if (proc.getTrack (track) == nullptr) return;
        tag = std::make_unique<RegionTag> (proc, track, selected);
        addAndMakeVisible (*tag);
        resized();
    }

    // Called when a sample finished loading, was cleared, or the region count changed.
    void syncPatternLabel()
    {
        const auto t = "Edit " + juce::String::charToString ((juce::juce_wchar) ('A' + proc.editPattern()));
        if (laneBtn.getButtonText() != t) laneBtn.setButtonText (t);
    }

    void refresh()
    {
        syncPatternLabel();
        const bool loaded = proc.getTrack (track) != nullptr;
        if (proc.catchState() != Proc::CatchIdle && proc.catchTrack() == track) recOpen = true;     // a take started from elsewhere shows its panel
        if (selected >= regionCount()) pick (regionCount() - 1);
        if (loaded && tag == nullptr) buildTag();
        if (! loaded) tag.reset();
        for (auto* c : std::initializer_list<juce::Component*> { &wave, &grid, &clear, &less, &more, &tabs[0], &tabs[1], &tabs[2], &tabs[3], &tabs[4], &tabs[5], &tabs[6], &tabs[7], &tabs[8], &keyBtn, &matchBtn, &matchAllBtn, &laneBtn, tuneKnob.get() })
            if (c != nullptr) c->setVisible (loaded);
        keyBtn.setButtonText (keyButtonText());
        matchBtn.setEnabled (loaded && proc.trackKey (track).found);
        wave.setVisible (loaded && ! recOpen);
        rec.setVisible (recOpen);
        record.setToggleState (recOpen, juce::dontSendNotification);
        record.setEnabled (proc.catchState() == Proc::CatchIdle);
        const auto m = proc.takeLoadMessage();
        if (m.isNotEmpty()) message = m;
        else if (loaded) message = {};
        resized();
        repaint();
    }

    void resized() override
    {
        const bool loaded = proc.getTrack (track) != nullptr;
        clear.setBounds (16 + 656 - 56, 12, 56, 28);
        browse.setBounds (loaded ? 16 + 656 - 56 - 8 - 64 : getWidth() / 2 - 128, loaded ? 12 : 268, loaded ? 64 : 120, 28);
        record.setBounds (loaded ? 16 + 656 - 56 - 8 - 64 - 8 - 64 : getWidth() / 2 + 8, loaded ? 12 : 268, loaded ? 64 : 120, 28);
        keyBtn.setBounds (292, 6, 104, 24);
        matchAllBtn.setBounds (402, 6, 62, 24);
        matchBtn.setBounds (292, 32, 58, 22);
        if (tuneKnob != nullptr) tuneKnob->setBounds (356, 30, 30, 26);
        wave.setBounds (16, 56, 656, 204);
        rec.setBounds (16, 56, 656, 204);
        int x = 16;
        const int w[] = { 52, 52, 44, 52, 58, 50, 50, 50, 36 };
        for (int i = 0; i < jd::kLanes; ++i) { tabs[(size_t) i].setBounds (x, 268, w[i], 24); x += w[i] + 5; }
        less.setBounds (552, 268, 24, 24);
        more.setBounds (610, 268, 24, 24);
        grid.setBounds (16, 300, 656, 102);
        laneBtn.setBounds (16, 300 + 44, 52, 22);
        if (tag != nullptr) tag->setBounds (getWidth() - 216 - 12, 10, 216, 406);
    }

    void paint (juce::Graphics& g) override
    {
        const auto ink = juce::Colour (0xff2a1b10);
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd3c8a7), 0.f, 0.f, juce::Colour (0xffc2b58f), 0.f, r.getHeight(), false));
        g.fillRect (r);
        g.setColour (juce::Colour (0x99281608));
        g.drawRect (r, 1.f);

        padBehind (g, { &laneBtn, &keyBtn, &matchAllBtn, &matchBtn, &browse, &record, &clear, &tabs[0], &tabs[1], &tabs[2], &tabs[3], &tabs[4], &tabs[5], &tabs[6], &tabs[7], &tabs[8], &less, &more });

        auto td = proc.getTrack (track);
        if (td == nullptr && ! recOpen)
        {
            drawText (g, "Nothing in here yet.", getLocalBounds().withHeight (200).withY (80), serif (26.f, false), ink, juce::Justification::centred);
            drawText (g, "Drop something in.", getLocalBounds().withHeight (30).withY (200), sans (14.f), ink.withAlpha (0.8f), juce::Justification::centred);
            drawText (g, "WAV, AIFF, FLAC, MP3 and Ogg all fit.", getLocalBounds().withHeight (20).withY (312), sans (12.f), ink.withAlpha (0.65f), juce::Justification::centred);
        }
        else if (td != nullptr)
        {
            const auto dot = juce::String::charToString (0x00b7);
            drawText (g, td->name, { 16, 10, 268, 22 }, serif (18.f, false), ink);
            const auto k = td->key;
            const auto keyPart = k.found ? jd::keyText (k) + (k.confidence >= 0.5f ? ", likely" : ", maybe") : juce::String ("no clear key");
            drawText (g, seconds (td->seconds()) + "  " + dot + "  " + juce::String (regionCount()) + (regionCount() == 1 ? " region" : " regions") + "  " + dot + "  " + keyPart,
                      { 16, 32, 272, 16 }, sans (11.f), ink.withAlpha (0.75f));
            const float tune = plainValue (proc, jd::trackId (track, "tune"));
            drawText (g, "Tune", { 392, 31, 72, 12 }, sans (11.f, true), ink.withAlpha (0.85f));
            drawText (g, (tune > 0.05f ? "+" : "") + juce::String (tune, 1) + " st", { 392, 42, 72, 12 }, mono (11.f), ink);
            drawText (g, "Steps", { 512, 268, 38, 24 }, sans (11.f, true), ink.withAlpha (0.85f), juce::Justification::centredRight);
            drawText (g, juce::String (proc.laneLength (track, grid.laneIndex())), { 578, 268, 30, 24 }, mono (13.f, true), ink, juce::Justification::centred);
            const int feel = juce::jlimit (0, 2, juce::roundToInt (plainValue (proc, "seq_feel")));
            drawText (g, jd::kStepDivs[juce::jlimit (0, 3, juce::roundToInt (plainValue (proc, "seq_div")))] + (feel == 1 ? "d" : feel == 2 ? "t" : ""), { 636, 268, 36, 24 }, mono (11.f), ink.withAlpha (0.8f), juce::Justification::centredRight);
        }
        if (message.isNotEmpty())
            drawText (g, message, { 16, td == nullptr ? 350 : 404, 656, 18 }, sans (12.f), juce::Colour (0xff7a2e16), td == nullptr ? juce::Justification::centred : juce::Justification::centredLeft);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() > 0 && isSupported (juce::File (files[0])); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() > 0 && isSupported (juce::File (files[0]))) proc.loadFile (track, juce::File (files[0]));
    }

    static bool isSupported (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"); }

    void openChooser()
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick a sound", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) proc.loadFile (track, f);
        });
    }

    Proc& proc;
    int track = 0, selected = 0;
    std::array<int, jd::kTracks> regions {};
    WaveView wave;
    StepGrid grid;
    std::unique_ptr<RegionTag> tag;
    std::array<juce::TextButton, jd::kLanes> tabs;
    juce::TextButton browse, clear, record, less, more, keyBtn, matchBtn, matchAllBtn, laneBtn;
    Proc::LaneCopy clip;
    std::unique_ptr<Knob> tuneKnob;
    std::unique_ptr<Watch> wTune, wRoot, wMode;
    RecPanel rec;
    bool recOpen = false;
    juce::String message;
    std::unique_ptr<juce::FileChooser> chooser;
};

// The junk compartment: the pattern card, the regions card and the brass nameplate.
struct Junk : juce::Component
{
    explicit Junk (Proc& p) : proc (p)
    {
        auto& s = p.state;
        div = std::make_unique<Seg> (s, "seq_div", jd::kStepDivs);
        div->setTooltip ("How long one step is");
        feel = std::make_unique<Seg> (s, "seq_feel", jd::kStepFeels);
        feel->setTooltip ("Straight, dotted (1.5 times longer) or triplet (three in the space of two)");
        swing = std::make_unique<Knob> (s, "m_swing", "Swing", 38, juce::Colour (0xff2a1b10), juce::Colour (0xff5a4329));
        tempo = std::make_unique<Knob> (s, "seq_bpm", "Tempo", 38, juce::Colour (0xff2a1b10), juce::Colour (0xff5a4329));
        tempo->slider.setTooltip ("Only used when there is no host, such as the standalone app");
        run.setButtonText ("Run"); run.setClickingTogglesState (true);
        run.setTooltip ("Runs the pattern without a host. In a DAW the host's play button drives it.");
        runAtt = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter ("seq_run"), run, nullptr);
        snap = std::make_unique<Seg> (s, "snap", juce::StringArray { "Off", "Hit", "Zero" });
        rummage.setButtonText ("Rummage"); rummage.setTooltip ("Rummage through the drawer: re-roll every step on this track, except the lanes you keep");
        shakeBtn.setButtonText ("Shake");  shakeBtn.setTooltip ("Shake the drawer: re-roll only some of the steps. The Amount knob sets how many.");
        rummage.onClick = [this] { proc.rummage (track); };
        shakeBtn.onClick = [this] { proc.shake (track); };
        shakeAmt = std::make_unique<Knob> (s, "shake_amt", "Amount", 38, juce::Colour (0xff2a1b10), juce::Colour (0xff5a4329));
        static const char* lockNames[] = { "Region", "Volume", "Pitch", "Ratchet", "Dir", "Length" };
        for (int i = 0; i < jd::kRolled; ++i)
        {
            keep[(size_t) i].setButtonText (lockNames[i]); keep[(size_t) i].setClickingTogglesState (true);
            keep[(size_t) i].setTooltip ("Lit lanes are kept as they are when you Rummage or Shake");
            keepAtt[(size_t) i] = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter (juce::String ("lock_") + jd::laneKey (i)), keep[(size_t) i], nullptr);
            addAndMakeVisible (keep[(size_t) i]);
        }
        addAndMakeVisible (rummage); addAndMakeVisible (shakeBtn); addAndMakeVisible (*shakeAmt);
        snap->setTooltip ("Off: edges go where you drop them. Hit: they stick to the nearest loud hit. Zero: they stick to the nearest quiet crossing, so there are no clicks.");

        sliceEq.setButtonText ("Slice equal");     sliceEq.setTooltip ("Cut the sound into equal regions");
        sliceHit.setButtonText ("Slice by hits");  sliceHit.setTooltip ("Cut the sound where the loudest hits start");
        add.setButtonText ("+ Region");  add.setTooltip ("Split the longest region in two");
        sub.setButtonText ("- Region");  sub.setTooltip ("Remove the last region");
        play.setButtonText ("Play all"); play.setTooltip ("Hear the whole sound");
        stop.setButtonText ("Stop");
        sliceEq.onClick = [this] { proc.sliceEqual (track, regionCount()); };
        sliceHit.onClick = [this] { proc.sliceByHits (track, regionCount()); };
        add.onClick = [this] { proc.addRegion (track); };
        sub.onClick = [this] { proc.removeRegion (track); };
        play.onClick = [this] { proc.auditionWhole (track); };
        stop.onClick = [this] { proc.stopAudition (track); };
        for (auto* c : std::initializer_list<juce::Component*> { div.get(), feel.get(), swing.get(), tempo.get(), &run, snap.get(), &sliceEq, &sliceHit, &add, &sub, &play, &stop }) addAndMakeVisible (*c);
        setTrack (0);
    }

    int regionCount() const { return juce::roundToInt (plainValue (proc, jd::trackId (track, "regions"))); }
    void setTrack (int t)
    {
        track = t;
        wMorph = std::make_unique<Watch> (proc.state, jd::trackId (t, "morph"), *this);
        morphKnob = std::make_unique<Knob> (proc.state, jd::trackId (t, "morph"), "Morph", 22, juce::Colour (0xff2a1b10), juce::Colour (0xff5a4329));
        morphKnob->showText = false;
        morphKnob->slider.setTooltip ("Blends each step's region with the next region. 0 is the region alone, 100 is its neighbor.");
        addAndMakeVisible (*morphKnob);
        sendDelKnob = std::make_unique<Knob> (proc.state, jd::trackId (t, "senddel"), "Delay", 38, juce::Colour (0xff2a1b10), juce::Colour (0xff5a4329));
        sendRevKnob = std::make_unique<Knob> (proc.state, jd::trackId (t, "sendrev"), "Reverb", 38, juce::Colour (0xff2a1b10), juce::Colour (0xff5a4329));
        sendDelKnob->slider.setTooltip ("How much of this track goes to the Delay. It uses the Delay settings on the Master page.");
        sendRevKnob->slider.setTooltip ("How much of this track goes to the Reverb. It uses the Reverb settings on the Master page.");
        addAndMakeVisible (*sendDelKnob); addAndMakeVisible (*sendRevKnob);
        resized();
        repaint();
    }

    void resized() override
    {
        // pattern card at (14,14), regions card at (236,14); both 214 x 222
        div->setBounds (14 + 16, 14 + 36, 182, 24);
        feel->setBounds (14 + 16, 14 + 64, 182, 24);
        swing->setBounds (14 + 20, 14 + 96, 76, 76);
        tempo->setBounds (14 + 118, 14 + 96, 76, 76);
        run.setBounds (14 + 16, 14 + 184, 86, 28);

        const int kx = 458 + 16;
        rummage.setBounds (kx, 14 + 34, 88, 26);
        shakeBtn.setBounds (kx + 94, 14 + 34, 88, 26);
        shakeAmt->setBounds (458 + 69, 14 + 66, 76, 76);
        for (int i = 0; i < jd::kRolled; ++i)
            keep[(size_t) i].setBounds (kx + (i % 3) * 62, 14 + 168 + (i / 3) * 26, 58, 24);

        const int rx = 236 + 16;
        sliceEq.setBounds (rx, 14 + 34, 88, 26);
        sliceHit.setBounds (rx + 94, 14 + 34, 88, 26);
        add.setBounds (rx, 14 + 66, 88, 26);
        sub.setBounds (rx + 94, 14 + 66, 88, 26);
        snap->setBounds (rx + 16, 14 + 116, 150, 26);
        if (morphKnob != nullptr) morphKnob->setBounds (rx + 54, 14 + 148, 30, 26);
        if (sendDelKnob != nullptr) sendDelKnob->setBounds (680 + 20, 14 + 44, 76, 76);
        if (sendRevKnob != nullptr) sendRevKnob->setBounds (680 + 118, 14 + 44, 76, 76);
        play.setBounds (rx, 14 + 184, 88, 26);
        stop.setBounds (rx + 94, 14 + 184, 88, 26);
    }

    static void card (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
    {
        g.setColour (juce::Colour (0x66000000)); g.fillRoundedRectangle (r.translated (0.f, 4.f), 3.f);
        g.setColour (juce::Colour (0xfff3ead0)); g.fillRoundedRectangle (r, 3.f);
        drawText (g, title, { (int) r.getX() + 16, (int) r.getY() + 12, (int) r.getWidth() - 32, 16 }, sans (11.f, true).withExtraKerningFactor (0.16f), juce::Colour (0xff5a4329));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xb8120903));
        g.fillRect (getLocalBounds());
        card (g, { 14.f, 14.f, 212.f, 222.f }, "PATTERN");
        card (g, { 236.f, 14.f, 212.f, 222.f }, "REGIONS");
        card (g, { 458.f, 14.f, 212.f, 222.f }, "RUMMAGE");
        drawText (g, "Keep these lanes", { 458 + 16, 14 + 148, 182, 16 }, sans (11.f, true), juce::Colour (0xff5a4329));
        drawText (g, "Morph", { 236 + 16, 14 + 150, 50, 22 }, sans (11.f, true), juce::Colour (0xff5a4329));
        drawText (g, juce::String (juce::roundToInt (plainValue (proc, jd::trackId (track, "morph")) * 100.f)) + "%", { 236 + 16 + 98, 14 + 150, 60, 22 }, mono (11.f), juce::Colour (0xff5a4329));
        padBehind (g, { &rummage, &shakeBtn, &keep[0], &keep[1], &keep[2], &keep[3], &keep[4], &keep[5], div.get(), feel.get(), &run, snap.get(), &sliceEq, &sliceHit, &add, &sub, &play, &stop });
        drawText (g, "Snap edges to", { 236 + 16, 14 + 96, 182, 18 }, sans (11.f, true), juce::Colour (0xff5a4329));

        card (g, { 680.f, 14.f, 212.f, 222.f }, "SENDS");
        g.setColour (juce::Colour (0xff5a4329));
        g.setFont (sans (11.f));
        g.drawFittedText ("How much of this track goes to the Delay and the Reverb. They use the settings on the Master page, and go quiet when switched off there.", { 680 + 16, 14 + 152, 182, 62 }, juce::Justification::topLeft, 4, 1.f);
    }

    Proc& proc;
    int track = 0;
    std::unique_ptr<Seg> div, feel, snap;
    std::unique_ptr<Knob> swing, tempo, shakeAmt, morphKnob, sendDelKnob, sendRevKnob;
    std::unique_ptr<Watch> wMorph;
    juce::TextButton rummage, shakeBtn;
    std::array<juce::TextButton, jd::kRolled> keep;
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, jd::kRolled> keepAtt;
    juce::TextButton run, sliceEq, sliceHit, add, sub, play, stop;
    std::unique_ptr<juce::ButtonParameterAttachment> runAtt;
};
} // namespace

//==============================================================================
// Drawer page
struct JunkDrawerEditor::DrawerPage : juce::Component, juce::ChangeListener
{
    struct LfoTile : juce::Component, juce::SettableTooltipClient
    {
        LfoTile (JunkDrawerProcessor& p, int i) : index (i), proc (p), state (p.state), wShape (p.state, jd::lfoId (i, "shape"), *this), wRate (p.state, jd::lfoId (i, "rate"), *this),
                                    wDiv (p.state, jd::lfoId (i, "div"), *this), wSync (p.state, jd::lfoId (i, "sync"), *this)
        { setTooltip ("LFO " + juce::String (i + 1) + ". Click to start assigning it: then drag any knob up or down to set how much this LFO moves it. Click again to stop."); }
        void mouseDown (const juce::MouseEvent&) override { proc.setArmedLfo (proc.armedLfo() == index ? -1 : index); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, 4.f);
            const bool armed = proc.armedLfo() == index;
            g.setColour (armed ? pal::amber : pal::line.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.f, armed ? 2.f : 1.f);

            const int shape = juce::roundToInt (state.getRawParameterValue (jd::lfoId (index, "shape"))->load());
            const int sync  = juce::roundToInt (state.getRawParameterValue (jd::lfoId (index, "sync"))->load());
            const float hz  = state.getRawParameterValue (jd::lfoId (index, "rate"))->load();
            const int div   = juce::roundToInt (state.getRawParameterValue (jd::lfoId (index, "div"))->load());
            drawText (g, "L" + juce::String (index + 1), { 8, 3, 30, 16 }, mono (12.f, true), pal::text);

            auto wave = lfoPath (shape, (float) getWidth() - 20.f, 14.f);
            g.setColour (pal::textDim);
            g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (10.f, 20.f));

            const juce::String rate = sync == 0 ? juce::String (hz, hz < 10.f ? 2 : 1) + "Hz" : jd::kLfoSyncsShort (div, sync);
            drawText (g, jd::kLfoShapes[shape].toLowerCase() + " " + rate, { 8, 34, getWidth() - 12, 14 }, sans (11.f, true), pal::textSoft);
        }

        int index;
        JunkDrawerProcessor& proc;
        APVTS& state;
        Watch wShape, wRate, wDiv, wSync;
    };

    struct TrackRow : juce::Component
    {
        TrackRow (JunkDrawerProcessor& p, int t) : proc (p), track (t)
        {
            auto& s = p.state;
            m.setButtonText ("M"); s_.setButtonText ("S");
            m.setClickingTogglesState (true); s_.setClickingTogglesState (true);
            m.setTooltip ("Mute track " + juce::String (t + 1));
            s_.setTooltip ("Solo track " + juce::String (t + 1));
            addAndMakeVisible (m); addAndMakeVisible (s_);
            am = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter (jd::trackId (t, "mute")), m, nullptr);
            as = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter (jd::trackId (t, "solo")), s_, nullptr);
        }
        void resized() override { m.setBounds (204, 10, 22, 26); s_.setBounds (230, 10, 22, 26); }
        void mouseDown (const juce::MouseEvent&) override { if (onPick) onPick (track); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xc7120905));
            g.fillRoundedRectangle (r, 3.f);
            g.setColour (picked ? pal::amber : juce::Colour (0x29ffebbe));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.f, picked ? 2.f : 1.f);

            const juce::Rectangle<float> badge (10.f, 10.f, 28.f, 28.f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3dc98), 14.f, 14.f, juce::Colour (0xff6d4e1d), 34.f, 36.f, true));
            g.fillEllipse (badge);
            drawText (g, juce::String (track + 1).paddedLeft ('0', 2), badge.toNearestInt(), mono (12.f, true), juce::Colour (0xff2a1b10), juce::Justification::centred);

            auto td = proc.getTrack (track);
            const juce::Rectangle<float> card (46.f, 10.f, 150.f, 26.f);
            g.setColour (juce::Colour (0xfff6eed6));
            g.fillRect (card);
            g.setColour (pal::brass);
            g.drawRect (card.expanded (1.5f), 1.5f);
            drawText (g, td != nullptr ? td->name : "Track " + juce::String (track + 1), card.toNearestInt().reduced (8, 0), mono (13.f), juce::Colour (0xff7a6140));

            if (td != nullptr)
            {
                const int n = juce::roundToInt (proc.state.getRawParameterValue (jd::trackId (track, "regions"))->load());
                drawText (g, seconds (td->seconds()) + " " + juce::String::charToString (0x00b7) + " " + juce::String (n) + (n == 1 ? " region" : " regions"),
                          { 46, 46, 200, 20 }, sans (12.f), juce::Colour (0xffd9c39a));
            }
            else
                drawText (g, "Nothing in here yet.", { 46, 46, 200, 20 }, sans (12.f), juce::Colour (0xffd9c39a));
            g.setColour (juce::Colour (0x80000000));
            g.fillRoundedRectangle (252.f, 12.f, 8.f, 56.f, 3.f);
        }
        JunkDrawerProcessor& proc;
        int track;
        bool picked = false;
        std::function<void (int)> onPick;
        juce::TextButton m, s_;
        std::unique_ptr<juce::ButtonParameterAttachment> am, as;
    };

    DrawerPage (JunkDrawerEditor& e) : ed (e)
    {
        auto& st = ed.proc.state;
        out = std::make_unique<Knob> (st, "m_volume", "Volume", 34);
        addAndMakeVisible (*out);
        const char* fxNames[] = { "Filter", "Delay", "Reverb", "Limiter" };
        const char* fxIds[]   = { "fx_filter_on", "fx_delay_on", "fx_reverb_on", "fx_lim_on" };
        for (int i = 0; i < 4; ++i)
        {
            pills[(size_t) i].setButtonText (fxNames[i]);
            pills[(size_t) i].setClickingTogglesState (true);
            pills[(size_t) i].setTooltip (juce::String (fxNames[i]) + " on or off");
            addAndMakeVisible (pills[(size_t) i]);
            pillAtt[(size_t) i] = std::make_unique<juce::ButtonParameterAttachment> (*st.getParameter (fxIds[i]), pills[(size_t) i], nullptr);
        }
        for (int i = 0; i < jd::kLfos; ++i) { tiles[(size_t) i] = std::make_unique<LfoTile> (ed.proc, i); addAndMakeVisible (*tiles[(size_t) i]); }
        for (int t = 0; t < jd::kTracks; ++t) { rows[(size_t) t] = std::make_unique<TrackRow> (ed.proc, t); rows[(size_t) t]->onPick = [this] (int i) { pickTrack (i); }; addAndMakeVisible (*rows[(size_t) t]); }
        liner = std::make_unique<Liner> (ed.proc);
        addAndMakeVisible (*liner);
        junk = std::make_unique<Junk> (ed.proc);
        addAndMakeVisible (*junk);
        rows[0]->picked = true;
        ed.proc.addChangeListener (this);
    }

    ~DrawerPage() override { ed.proc.removeChangeListener (this); }

    void pickTrack (int t)
    {
        for (int i = 0; i < jd::kTracks; ++i) { rows[(size_t) i]->picked = i == t; rows[(size_t) i]->repaint(); }
        liner->setTrack (t);
        junk->setTrack (t);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        for (auto& r : rows) r->repaint();
        for (auto& t : tiles) t->repaint();
        liner->refresh();
    }

    void resized() override
    {
        liner->setBounds (328, 122, 908, 426);
        junk->setBounds (328, 560, 908, 250);
        out->setBounds (62, 32, 58, 62);
        for (int i = 0; i < 4; ++i) pills[(size_t) i].setBounds (180 + i * 68, 34, 62, 24);
        for (int i = 0; i < jd::kLfos; ++i) tiles[(size_t) i]->setBounds (520 + i * 118, 34, 112, 52);
        for (int t = 0; t < jd::kTracks; ++t)
            rows[(size_t) t]->setBounds (42, 122 + juce::roundToInt (t * (80.f + (688.f - 640.f) / 7.f)), 274, 80);
    }

    void paint (juce::Graphics& g) override
    {
        paintDrawerFrame (g, true);
        auto strip = juce::Rectangle<float> (44.f, 10.f, 1192.f, 90.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (strip, 4.f);
        g.setColour (juce::Colour (0x2effebbe));
        g.drawRoundedRectangle (strip.reduced (0.5f), 4.f, 1.f);

        drawStripLabel (g, "OUTPUT", 62, 16);
        g.setColour (pal::line); g.fillRect (150, 22, 1, 62);
        drawStripLabel (g, "EFFECTS", 180, 16);
        drawText (g, "Details on the Master page", { 180, 66, 260, 16 }, sans (11.f), pal::textDim);
        g.setColour (pal::line); g.fillRect (470, 22, 1, 62);
        drawStripLabel (g, "LFOS", 520, 16);

    }

    JunkDrawerEditor& ed;
    std::unique_ptr<Knob> out;
    std::array<juce::TextButton, 4> pills;
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, 4> pillAtt;
    std::array<std::unique_ptr<LfoTile>, jd::kLfos> tiles;
    std::array<std::unique_ptr<TrackRow>, jd::kTracks> rows;
    std::unique_ptr<Liner> liner;
    std::unique_ptr<Junk> junk;
};

//==============================================================================
// LFO page
struct JunkDrawerEditor::LfoPage : juce::Component, juce::ChangeListener
{
    struct LfoPanel : Panel
    {
        LfoPanel (APVTS& s, int i) : Panel ("LFO " + juce::String (i + 1)), index (i), state (s),
                                     wShape (s, jd::lfoId (i, "shape"), *this)
        {
            scope = std::make_unique<Seg> (s, jd::lfoId (i, "global"), juce::StringArray { "Global", "Per track" }, true);
            scope->setTooltip ("Global: one shared LFO for the whole plugin. Per track: every track runs its own copy.");
            shapes = std::make_unique<Seg> (s, jd::lfoId (i, "shape"), jd::kLfoShapes);
            sync = std::make_unique<Seg> (s, jd::lfoId (i, "sync"), jd::kLfoSyncs);
            polar = std::make_unique<Seg> (s, jd::lfoId (i, "polar"), jd::kLfoPolarity);
            const char* ids[]   = { "rate", "depth", "phase", "fade" };
            const char* names[] = { "Rate", "Depth", "Phase", "Fade in" };
            for (int k = 0; k < 4; ++k) knobs[(size_t) k] = std::make_unique<Knob> (s, jd::lfoId (i, ids[k]), names[k], 38);
            addAndMakeVisible (*scope); addAndMakeVisible (*shapes); addAndMakeVisible (*sync); addAndMakeVisible (*polar);
            for (auto& k : knobs) addAndMakeVisible (*k);
        }
        void resized() override
        {
            scope->setBounds (getWidth() - 14 - 156, 13, 156, 22);
            shapes->setBounds (14, 44, getWidth() - 28, 26);
            for (int k = 0; k < 4; ++k) knobs[(size_t) k]->setBounds (14 + k * 90, 176, 90, 76);
            sync->setBounds (14, 262, getWidth() - 28 - 120, 24);
            polar->setBounds (getWidth() - 14 - 104, 262, 104, 24);
        }
        void paint (juce::Graphics& g) override
        {
            Panel::paint (g);
            const juce::Rectangle<float> sc (14.f, 80.f, (float) getWidth() - 28.f, 84.f);
            g.setColour (juce::Colour (0x66000000));
            g.fillRoundedRectangle (sc, 4.f);
            g.setColour (juce::Colour (0x1af0e2c4));
            g.drawHorizontalLine ((int) sc.getCentreY(), sc.getX(), sc.getRight());
            for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (sc.getX() + sc.getWidth() * k / 4.f), sc.getY(), sc.getBottom());
            const int shape = juce::roundToInt (state.getRawParameterValue (jd::lfoId (index, "shape"))->load());
            g.setColour (pal::amber);
            g.strokePath (lfoPath (shape, sc.getWidth(), sc.getHeight()), juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (sc.getX(), sc.getY()));
            drawHooked (g, 14, 296, getWidth() - 28, [this] (const LinkInfo& l) { return l.lfo == index; }, [] (const LinkInfo& l) { return l.name; });
        }
        int index;
        APVTS& state;
        Watch wShape;
        std::unique_ptr<Seg> scope, shapes, sync, polar;
        std::array<std::unique_ptr<Knob>, 4> knobs;
    };

    LfoPage (JunkDrawerEditor& e) : ed (e)
    {
        for (int i = 0; i < jd::kLfos; ++i)
        {
            auto& b = picks[(size_t) i];
            b.setButtonText ("L" + juce::String (i + 1));
            b.setClickingTogglesState (true);
            b.setTooltip ("Click to start assigning this LFO, then drag any knob up or down to set how much it moves it. Click again to stop.");
            b.onClick = [this, i] { ed.proc.setArmedLfo (ed.proc.armedLfo() == i ? -1 : i); };
            addAndMakeVisible (b);
            panels[(size_t) i] = std::make_unique<LfoPanel> (ed.proc.state, i);
            addAndMakeVisible (*panels[(size_t) i]);
        }
        matrix.setButtonText ("Mod matrix");
        matrix.onClick = [this] { ed.showPage (Matrix); };
        clear.setButtonText ("Clear all");
        clear.setTooltip ("Take every LFO off");
        clear.onClick = [this] { ed.proc.clearMods(); };
        addAndMakeVisible (matrix); addAndMakeVisible (clear);
        ed.proc.addChangeListener (this);
        changeListenerCallback (nullptr);
    }
    ~LfoPage() override { ed.proc.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        for (int i = 0; i < jd::kLfos; ++i) picks[(size_t) i].setToggleState (ed.proc.armedLfo() == i, juce::dontSendNotification);
        clear.setEnabled (! ed.proc.getMods().empty());
    }

    void resized() override
    {
        for (int i = 0; i < jd::kLfos; ++i)
        {
            picks[(size_t) i].setBounds (62 + i * 52, 44, 46, 30);
            panels[(size_t) i]->setBounds (44 + (i % 3) * 401, 122 + (i / 3) * 350, 389, 338);
        }
        clear.setBounds (1236 - 22 - 98, 37, 98, 30);
        matrix.setBounds (1236 - 22 - 98 - 10 - 104, 37, 104, 30);
    }

    void paint (juce::Graphics& g) override
    {
        paintDrawerFrame (g, false);
        const juce::Rectangle<float> strip (44.f, 12.f, 1192.f, 80.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (strip, 4.f);
        g.setColour (juce::Colour (0x2effebbe));
        g.drawRoundedRectangle (strip.reduced (0.5f), 4.f, 1.f);
        drawStripLabel (g, "ASSIGN", 66, 26);
        g.setColour (pal::line); g.fillRect (390, 24, 1, 56);
        g.setColour (pal::textSoft);
        g.setFont (sans (12.f));
        g.drawFittedText ("Pick an LFO, then drag any knob on any page up or down to set the amount. Pick it again to stop.", { 410, 28, 440, 52 }, juce::Justification::centredLeft, 3, 1.f);
    }

    JunkDrawerEditor& ed;
    std::array<juce::TextButton, jd::kLfos> picks;
    std::array<std::unique_ptr<LfoPanel>, jd::kLfos> panels;
    juce::TextButton matrix, clear;
};

//==============================================================================
// Master page
struct JunkDrawerEditor::MasterPage : juce::Component
{
    struct FxCard : Panel
    {
        enum Kind { Filter, Delay, Reverb, Limiter };
        struct Cfg
        {
            Kind kind; const char* title; const char* on; const char* mode; juce::StringArray modes;
            std::array<const char*, 4> knobs; const char* optLabel; const char* opt; juce::StringArray opts;
        };
        FxCard (APVTS& s, const Cfg& c) : Panel (c.title, false), cfg (c), state (s)
        {
            power = std::make_unique<Seg> (s, c.on, juce::StringArray { "On", "Bypass" }, true);
            power->setTooltip ("Bypass this effect");
            modes = std::make_unique<Seg> (s, c.mode, c.modes);
            opts = std::make_unique<Seg> (s, c.opt, c.opts);
            addAndMakeVisible (*power); addAndMakeVisible (*modes); addAndMakeVisible (*opts);
            for (size_t k = 0; k < 4; ++k)
            {
                auto* p = s.getParameter (c.knobs[k]);
                knobs[k] = std::make_unique<Knob> (s, c.knobs[k], p->getName (64), 38);
                addAndMakeVisible (*knobs[k]);
                watch.push_back (std::make_unique<Watch> (s, c.knobs[k], *this));
            }
            watch.push_back (std::make_unique<Watch> (s, c.mode, *this));
            watch.push_back (std::make_unique<Watch> (s, c.on, *this));
        }
        float v (const char* id) const { return state.getRawParameterValue (id)->load(); }
        float n (const char* id) const { return state.getParameter (id)->convertTo0to1 (v (id)); }

        void resized() override
        {
            power->setBounds (getWidth() - 14 - 112, 13, 112, 22);
            modes->setBounds (14, 44, getWidth() - 28, 26);
            for (size_t k = 0; k < 4; ++k) knobs[k]->setBounds (14 + (int) k * 90, 176, 90, 76);
            const int ow = opts->idealWidth();
            opts->setBounds (getWidth() - 14 - ow, 262, ow, 24);
        }

        void paint (juce::Graphics& g) override
        {
            highlighted = false;
            Panel::paint (g);
            const bool on = v (cfg.on) > 0.5f;
            const juce::Rectangle<float> vz (14.f, 80.f, (float) getWidth() - 28.f, 84.f);
            g.setColour (juce::Colour (0x66000000));
            g.fillRoundedRectangle (vz, 4.f);
            g.setColour (juce::Colour (0x1af0e2c4));
            g.drawHorizontalLine ((int) vz.getCentreY(), vz.getX(), vz.getRight());
            for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (vz.getX() + vz.getWidth() * k / 4.f), vz.getY(), vz.getBottom());

            const float W = vz.getWidth(), H = vz.getHeight();
            juce::Path p;
            bool fill = false;
            switch (cfg.kind)
            {
                case Filter:
                {
                    const int type = juce::roundToInt (v ("fx_filter_type"));
                    const float fc = n ("fx_filter_cut"), res = v ("fx_filter_res");
                    for (int i = 0; i <= 120; ++i)
                    {
                        const float t = (float) i / 120.f;
                        const float x = std::max (0.001f, t / std::max (0.02f, fc));
                        float gain;
                        const float lp = 1.f / (1.f + std::pow (x, 6.f)), hp = 1.f - lp;
                        gain = type == 0 ? lp : type == 2 ? hp : type == 1 ? lp * hp * 4.f : 1.f - lp * hp * 4.f;
                        gain += res * 0.35f * std::exp (-std::pow ((t - fc) * 14.f, 2.f));
                        const float y = H - 14.f - juce::jlimit (0.f, 1.f, gain) * (H - 28.f);
                        if (i == 0) p.startNewSubPath (t * W, y); else p.lineTo (t * W, y);
                    }
                    break;
                }
                case Delay:
                {
                    const float fb = v ("fx_delay_fb");
                    for (int i = 0; i < 6; ++i)
                    {
                        const float x = 30.f + (float) i * (W - 60.f) / 5.f, h = (H - 14.f) * std::pow (std::max (0.05f, fb) * 1.6f, (float) i);
                        p.startNewSubPath (x, H - 10.f);
                        p.lineTo (x, H - 10.f - juce::jlimit (2.f, H - 14.f, h));
                    }
                    break;
                }
                case Reverb:
                {
                    const float decay = n ("fx_reverb_decay"), size = v ("fx_reverb_size");
                    p.startNewSubPath (0.f, H - 10.f);
                    for (int i = 0; i <= 120; ++i)
                    {
                        const float t = (float) i / 120.f;
                        const float env = std::exp (-t * (7.f - 5.5f * decay)) * (0.82f + 0.18f * std::sin (t * (40.f + 140.f * size)));
                        p.lineTo (t * W, H - 10.f - (H - 24.f) * env);
                    }
                    p.lineTo (W, H - 10.f);
                    p.closeSubPath();
                    fill = true;
                    break;
                }
                case Limiter:
                {
                    const float ceil = n ("fx_lim_ceiling"), in = n ("fx_lim_input");
                    const float yc = H - 10.f - (H - 24.f) * (0.35f + 0.6f * ceil);
                    const float knee = 0.25f + 0.5f * (1.f - in * 0.5f);
                    p.startNewSubPath (0.f, H - 10.f);
                    p.lineTo (W * knee, yc);
                    p.lineTo (W, yc);
                    break;
                }
            }
            g.setColour (on ? pal::amber : juce::Colour (0xffb5a37c));
            if (fill) { g.setColour ((on ? pal::amber : juce::Colour (0xffb5a37c)).withAlpha (0.14f)); g.fillPath (p, juce::AffineTransform::translation (vz.getX(), vz.getY())); g.setColour (on ? pal::amber : juce::Colour (0xffb5a37c)); }
            g.strokePath (p, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (vz.getX(), vz.getY()));

            drawText (g, cfg.optLabel, { 14, 262, 120, 24 }, sans (11.f, true).withExtraKerningFactor (0.12f), pal::textDim);
            const juce::String prefix = juce::String (cfg.on).upToLastOccurrenceOf ("_on", false, false) + "_";
            drawHooked (g, 14, 296, getWidth() - 28, [prefix] (const LinkInfo& l) { return l.target.startsWith (prefix); },
                        [] (const LinkInfo& l) { return "L" + juce::String (l.lfo + 1) + " " + l.name.fromLastOccurrenceOf (" " + juce::String::charToString (0x00b7) + " ", false, false); });
        }

        Cfg cfg;
        APVTS& state;
        std::unique_ptr<Seg> power, modes, opts;
        std::array<std::unique_ptr<Knob>, 4> knobs;
        std::vector<std::unique_ptr<Watch>> watch;
    };

    struct MasterPanel : Panel, juce::Timer
    {
        MasterPanel (APVTS& s, JunkDrawerProcessor& p) : Panel ("MASTER"), proc (p)
        {
            const char* ids[]   = { "m_volume", "m_pan", "m_pitch", "m_rate", "m_swing", "m_width" };
            const char* names[] = { "Volume", "Pan", "Pitch", "Rate", "Swing", "Width" };
            const char* notes[] = { "Output level", "Left to right", "All tracks", "Step speed", "Off-beat delay", "Stereo spread" };
            for (int k = 0; k < 6; ++k) { knobs[(size_t) k] = std::make_unique<Knob> (s, ids[k], names[k], 56, pal::text, pal::amber, notes[k]); addAndMakeVisible (*knobs[(size_t) k]); }
            range = std::make_unique<Seg> (s, "m_pitchrange", juce::StringArray { juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "12")), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "24")), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "48")) });
            rate = std::make_unique<Seg> (s, "m_ratemode", juce::StringArray { "Free", "Sync" });
            addAndMakeVisible (*range); addAndMakeVisible (*rate);
            startTimerHz (30);
        }
        ~MasterPanel() override { stopTimer(); }
        void resized() override
        {
            for (int k = 0; k < 6; ++k) knobs[(size_t) k]->setBounds (14 + (k % 2) * 120, 50 + (k / 2) * 150, 120, 140);
            range->setBounds (getWidth() - 14 - 190, 536, 190, 24);
            rate->setBounds (getWidth() - 14 - 190, 576, 190, 24);
        }
        void timerCallback() override
        {
            // peaks fall back smoothly; the processor only ever raises them
            const float l = proc.peakL.exchange (0.f), r = proc.peakR.exchange (0.f);
            pl = std::max (l, pl * 0.88f); pr = std::max (r, pr * 0.88f);
            if (std::abs (pl - drawnL) > 0.002f || std::abs (pr - drawnR) > 0.002f) { drawnL = pl; drawnR = pr; repaint (meterArea()); }
        }
        juce::Rectangle<int> meterArea() const { return { 256, 40, 126, 480 }; }
        static float dbToY (float db) { return juce::jmap (juce::jlimit (-48.f, 0.f, db), -48.f, 0.f, 440.f, 0.f); }
        void paint (juce::Graphics& g) override
        {
            Panel::paint (g);
            const float peak = std::max (drawnL, drawnR);
            const float pdb = juce::Decibels::gainToDecibels (peak, -60.f);
            drawText (g, pdb <= -59.f ? juce::String ("Peak -inf") : "Peak " + juce::String (pdb, 1) + " dB", { getWidth() - 14 - 140, 12, 140, 24 }, mono (11.f), pal::amber, juce::Justification::centredRight);

            const juce::Rectangle<float> a (256.f + 22.f, 50.f, 22.f, 440.f), b (256.f + 50.f, 50.f, 22.f, 440.f);
            for (auto& bar : { a, b })
            {
                g.setColour (juce::Colour (0x80000000));
                g.fillRoundedRectangle (bar, 4.f);
            }
            const float vals[] = { drawnL, drawnR };
            int k = 0;
            for (auto& bar : { a, b })
            {
                const float y = dbToY (juce::Decibels::gainToDecibels (vals[k++], -60.f));
                auto fillR = juce::Rectangle<float> (bar.getX(), bar.getY() + y, bar.getWidth(), bar.getHeight() - y);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb5654f), 0, bar.getY(), juce::Colour (0xff9bb36a), 0, bar.getBottom(), false));
                g.fillRoundedRectangle (fillR, 3.f);
            }
            const int marks[] = { 0, -6, -12, -24, -36, -48 };
            for (int m : marks)
            {
                const float y = 50.f + dbToY ((float) m);
                g.setColour (juce::Colour (0x66f0e2c4));
                g.drawHorizontalLine ((int) y, 256.f + 78.f, 256.f + 86.f);
                drawText (g, juce::String (m), { 256 + 90, (int) y - 7, 34, 14 }, mono (11.f), pal::textDim);
            }

            g.setColour (pal::line.withAlpha (0.6f));
            g.fillRect (14, 520, getWidth() - 28, 1);
            drawText (g, "PITCH RANGE", { 14, 536, 140, 24 }, sans (11.f, true).withExtraKerningFactor (0.12f), pal::textDim);
            drawText (g, "RATE", { 14, 576, 140, 24 }, sans (11.f, true).withExtraKerningFactor (0.12f), pal::textDim);
            drawHooked (g, 14, 640, getWidth() - 28, [] (const LinkInfo& l) { return l.target.startsWith ("m_"); },
                        [] (const LinkInfo& l) { return "L" + juce::String (l.lfo + 1) + " " + l.name.fromLastOccurrenceOf (" " + juce::String::charToString (0x00b7) + " ", false, false); });
        }
        JunkDrawerProcessor& proc;
        std::unique_ptr<Seg> range, rate;
        std::array<std::unique_ptr<Knob>, 6> knobs;
        float pl = 0.f, pr = 0.f, drawnL = 0.f, drawnR = 0.f;
    };

    MasterPage (JunkDrawerEditor& e) : ed (e)
    {
        auto& s = ed.proc.state;
        master = std::make_unique<MasterPanel> (s, ed.proc);
        addAndMakeVisible (*master);
        const FxCard::Cfg cfgs[] = {
            { FxCard::Filter,  "Filter",  "fx_filter_on", "fx_filter_type", { "Low pass", "Band", "High pass", "Notch" }, { "fx_filter_cut", "fx_filter_res", "fx_filter_drive", "fx_filter_mix" }, "SLOPE", "fx_filter_slope", { "12 dB", "24 dB" } },
            { FxCard::Delay,   "Delay",   "fx_delay_on",  "fx_delay_mode", jd::kLfoSyncs, { "fx_delay_time", "fx_delay_fb", "fx_delay_tone", "fx_delay_mix" }, "STYLE", "fx_delay_style", { "Mono", "Ping-pong" } },
            { FxCard::Reverb,  "Reverb",  "fx_reverb_on", "fx_reverb_mode", { "Room", "Hall", "Plate", "Spring" }, { "fx_reverb_size", "fx_reverb_decay", "fx_reverb_damp", "fx_reverb_mix" }, "PRE-DELAY", "fx_reverb_pre", { "0 ms", "20 ms", "40 ms" } },
            { FxCard::Limiter, "Limiter", "fx_lim_on",    "fx_lim_mode", { "Soft", "Hard" }, { "fx_lim_ceiling", "fx_lim_release", "fx_lim_input", "fx_lim_mix" }, "LOOKAHEAD", "fx_lim_look", { "Off", "1 ms", "5 ms" } }
        };
        for (size_t i = 0; i < 4; ++i) { cards[i] = std::make_unique<FxCard> (s, cfgs[i]); addAndMakeVisible (*cards[i]); }

        bypass.setButtonText ("Bypass all");
        bypass.setTooltip ("Switch every effect off");
        bypass.onClick = [this] { for (auto* id : { "fx_filter_on", "fx_delay_on", "fx_reverb_on", "fx_lim_on" }) setParam (id, 0.f); };
        reset.setButtonText ("Reset section");
        reset.setTooltip ("Put everything back");
        reset.onClick = [this]
        {
            for (auto* p : ed.proc.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (rp->paramID.startsWith ("m_") || rp->paramID.startsWith ("fx_")) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
        };
        addAndMakeVisible (bypass); addAndMakeVisible (reset);
    }
    void setParam (const char* id, float v)
    {
        if (auto* p = ed.proc.state.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    }
    void resized() override
    {
        master->setBounds (44, 122, 389, 688);
        for (size_t i = 0; i < 4; ++i) cards[i]->setBounds (44 + (1 + (int) (i % 2)) * 401, 122 + (int) (i / 2) * 350, 389, 338);
        reset.setBounds (1236 - 22 - 120, 37, 120, 30);
        bypass.setBounds (1236 - 22 - 120 - 10 - 100, 37, 100, 30);
    }
    void paint (juce::Graphics& g) override
    {
        paintDrawerFrame (g, false);
        const juce::Rectangle<float> strip (44.f, 12.f, 1192.f, 80.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (strip, 4.f);
        g.setColour (juce::Colour (0x2effebbe));
        g.drawRoundedRectangle (strip.reduced (0.5f), 4.f, 1.f);
        drawStripLabel (g, "THE BACK OF THE DRAWER", 66, 24);
        drawText (g, "Master and effects", { 66, 40, 320, 32 }, serif (22.f), juce::Colour (0xfff6e8c9));
        g.setColour (pal::line); g.fillRect (390, 24, 1, 56);
        g.setColour (pal::textSoft);
        g.setFont (sans (12.f));
        g.drawFittedText ("Every knob here takes an LFO too. Pick one on the LFOs page, click a knob, then drag up or down to set the amount.", { 410, 28, 440, 52 }, juce::Justification::centredLeft, 3, 1.f);
    }
    JunkDrawerEditor& ed;
    std::unique_ptr<MasterPanel> master;
    std::array<std::unique_ptr<FxCard>, 4> cards;
    juce::TextButton bypass, reset;
};

//==============================================================================
// Song page: eight patterns (A to H) and a chain that plays them one after another
struct JunkDrawerEditor::SongPage : juce::Component, juce::ChangeListener, juce::Timer
{
    explicit SongPage (JunkDrawerEditor& e) : ed (e)
    {
        chain.setButtonText ("Chain");
        chain.setClickingTogglesState (true);
        chain.setTooltip ("On: the chain below plays pattern after pattern. Off: the pattern you are editing just loops.");
        chainAtt = std::make_unique<juce::ButtonParameterAttachment> (*ed.proc.state.getParameter ("chain_on"), chain, nullptr);
        addAndMakeVisible (chain);
        ed.proc.addChangeListener (this);
    }
    ~SongPage() override { ed.proc.removeChangeListener (this); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void visibilityChanged() override { if (isVisible()) startTimerHz (8); else stopTimer(); }
    void timerCallback() override
    {
        const int pl = ed.proc.playingPattern(), ps = ed.proc.chainPosition();
        if (pl != shownPlaying || ps != shownSlot) { shownPlaying = pl; shownSlot = ps; repaint(); }
    }

    void resized() override { chain.setBounds (1236 - 22 - 96, 37, 96, 30); }

    static juce::Rectangle<int> patRect (int i) { return { 58 + i * 146, 172, 138, 120 }; }
    static juce::Rectangle<int> slotRect (int i) { return { 58 + (i % 8) * 146, 396 + (i / 8) * 132, 138, 112 }; }
    static juce::String letter (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }
    juce::String barsText (int b) const { return juce::String (b) + (b == 1 ? " bar" : " bars"); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        for (int i = 0; i < jd::kPatterns; ++i)
            if (patRect (i).contains (e.getPosition())) { ed.proc.setEditPattern (i); return; }
        const int n = ed.proc.chainLength();
        for (int i = 0; i <= n && i < jd::kChainMax; ++i)
            if (slotRect (i).contains (e.getPosition())) { openSlotMenu (i); return; }
    }

    void openSlotMenu (int slot)
    {
        const bool adding = slot == ed.proc.chainLength();
        const int curP = adding ? ed.proc.editPattern() : ed.proc.chainPattern (slot);
        const int curB = adding ? 1 : ed.proc.chainBars (slot);
        juce::PopupMenu m, pats, bars;
        for (int i = 0; i < jd::kPatterns; ++i) pats.addItem (100 + i, "Pattern " + letter (i), true, ! adding && i == curP);
        for (int b : { 1, 2, 4, 8 }) bars.addItem (200 + b, barsText (b), true, ! adding && b == curB);
        m.addSubMenu (adding ? "Add pattern" : "Pattern", pats);
        m.addSubMenu ("Length", bars, ! adding);
        if (! adding) { m.addSeparator(); m.addItem (1, "Remove this slot"); }
        m.showMenuAsync (juce::PopupMenu::Options(), [this, safe = juce::Component::SafePointer<SongPage> (this), slot, curP, curB] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) ed.proc.removeChainSlot (slot);
            else if (r >= 100 && r < 100 + jd::kPatterns) ed.proc.setChainSlot (slot, r - 100, curB);
            else if (r > 200) ed.proc.setChainSlot (slot, curP, r - 200);
        });
    }

    void paint (juce::Graphics& g) override
    {
        paintDrawerFrame (g, false);
        auto panel = [&] (juce::Rectangle<float> r)
        {
            g.setColour (pal::panel); g.fillRoundedRectangle (r, 4.f);
            g.setColour (juce::Colour (0x29ffebbe)); g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
        };
        const juce::Rectangle<float> strip (44.f, 12.f, 1192.f, 80.f);
        panel (strip);
        drawStripLabel (g, "PATTERNS IN ORDER", 66, 24);
        drawText (g, "Song", { 66, 40, 320, 32 }, serif (22.f), juce::Colour (0xfff6e8c9));
        g.setColour (pal::line); g.fillRect (390, 24, 1, 56);
        g.setColour (pal::textSoft);
        g.setFont (sans (12.f));
        g.drawFittedText ("Click a pattern to edit it. Build a chain below and turn Chain on; the pattern changes on the bar line.", { 410, 28, 560, 52 }, juce::Justification::centredLeft, 3, 1.f);

        const int editing = ed.proc.editPattern(), playing = ed.proc.playingPattern(), slotNow = ed.proc.chainPosition();
        panel ({ 44.f, 122.f, 1192.f, 190.f });
        drawText (g, "PATTERNS", { 58, 134, 200, 24 }, mono (15.f, true), pal::text);
        for (int i = 0; i < jd::kPatterns; ++i)
        {
            const auto r = patRect (i);
            const bool ed_ = i == editing, pl = i == playing && ed.proc.state.getRawParameterValue ("seq_run")->load() > 0.5f;
            g.setColour (ed_ ? pal::amber.withAlpha (0.18f) : juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (r.toFloat(), 4.f);
            g.setColour (ed_ ? pal::amber : pal::line);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, ed_ ? 1.5f : 1.f);
            drawText (g, letter (i), r.withTrimmedBottom (40).withTrimmedTop (10), serif (44.f), ed_ ? pal::amber : pal::text, juce::Justification::centred);
            drawText (g, ed_ ? "Editing" : "Click to edit", r.withTop (r.getBottom() - 32).reduced (8, 0), sans (11.f, ed_), ed_ ? pal::amber : pal::textDim, juce::Justification::centred);
            if (pl) { g.setColour (pal::amber); g.fillEllipse ((float) r.getRight() - 16.f, (float) r.getY() + 8.f, 8.f, 8.f); }
        }

        panel ({ 44.f, 330.f, 1192.f, 340.f });
        drawText (g, "CHAIN", { 58, 342, 200, 24 }, mono (15.f, true), pal::text);
        const int n = ed.proc.chainLength();
        int total = 0;
        for (int i = 0; i < n; ++i) total += ed.proc.chainBars (i);
        drawText (g, n == 0 ? juce::String ("Empty") : juce::String (n) + (n == 1 ? " slot, " : " slots, ") + barsText (total), { 700, 342, 522, 24 }, mono (11.f), pal::amber, juce::Justification::centredRight);
        for (int i = 0; i <= n && i < jd::kChainMax; ++i)
        {
            const auto r = slotRect (i);
            if (i == n)
            {
                g.setColour (pal::line);
                const float dash[] = { 4.f, 4.f };
                juce::Path p; p.addRoundedRectangle (r.toFloat().reduced (0.5f), 4.f);
                juce::PathStrokeType (1.f).createDashedStroke (p, p, dash, 2);
                g.fillPath (p);
                drawText (g, "+ Add", r, sans (13.f, true), pal::textDim, juce::Justification::centred);
                continue;
            }
            const bool now = i == slotNow;
            g.setColour (now ? pal::amber.withAlpha (0.22f) : juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (r.toFloat(), 4.f);
            g.setColour (now ? pal::amber : pal::line);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, now ? 1.5f : 1.f);
            drawText (g, juce::String (i + 1), { r.getX() + 10, r.getY() + 8, 40, 16 }, mono (11.f), pal::textDim);
            drawText (g, letter (ed.proc.chainPattern (i)), r.withTrimmedTop (18).withTrimmedBottom (34), serif (38.f), now ? pal::amber : pal::text, juce::Justification::centred);
            drawText (g, barsText (ed.proc.chainBars (i)), r.withTop (r.getBottom() - 32).reduced (8, 0), sans (11.f), pal::textSoft, juce::Justification::centred);
        }
        drawText (g, "Click a slot to change its pattern or length, or remove it. Export follows the chain when it is on.", { 58, 630, 1164, 24 }, sans (11.f), pal::textDim);
    }

    JunkDrawerEditor& ed;
    juce::TextButton chain;
    std::unique_ptr<juce::ButtonParameterAttachment> chainAtt;
    int shownPlaying = -2, shownSlot = -2;
};

//==============================================================================
// Matrix page (empty until the LFO engine and assignments exist)
struct JunkDrawerEditor::MatrixPage : juce::Component, juce::ChangeListener
{
    // The table of every link: target, LFO, amount bar (drag it), polarity and a cross to clear.
    struct Table : juce::Component
    {
        explicit Table (MatrixPage& m) : owner (m) {}
        static constexpr int rowH = 44, barX = 478, barW = 168;

        std::vector<LinkInfo> rows() const
        {
            std::vector<LinkInfo> out;
            if (auto* c = currentMod()) for (auto& l : c->links()) if (owner.filter < 0 || l.lfo == owner.filter) out.push_back (l);
            return out;
        }

        void paint (juce::Graphics& g) override
        {
            const auto list = rows();
            if (list.empty())
            {
                drawText (g, hookedNone(), getLocalBounds().withHeight (60).withY (150), serif (22.f, false), pal::textSoft, juce::Justification::centred);
                drawText (g, "Pick an LFO, then drag any knob up or down.", getLocalBounds().withHeight (24).withY (214), sans (13.f), pal::textDim, juce::Justification::centred);
                return;
            }
            auto& state = owner.ed.proc.state;
            for (size_t i = 0; i < list.size() && (int) i * rowH < getHeight(); ++i)
            {
                const auto& l = list[i];
                const int y = (int) i * rowH;
                g.setColour (juce::Colour (0x1af0e2c4)); g.fillRect (0, y + rowH - 1, getWidth(), 1);
                drawText (g, l.name, { 0, y + 4, 306, 20 }, sans (13.f, true), pal::text);
                drawText (g, l.where, { 0, y + 23, 306, 16 }, sans (11.f), pal::textDim);
                drawChip (g, { 316, y + 11, 40, 22 }, "L" + juce::String (l.lfo + 1));
                const bool perTrack = state.getRawParameterValue (jd::lfoId (l.lfo, "global"))->load() < 0.5f && l.target.startsWithChar ('t') && juce::CharacterFunctions::isDigit (l.target[1]);
                drawText (g, perTrack ? "Per track" : "Global", { 364, y + 11, 100, 22 }, sans (11.f), pal::textSoft);

                const juce::Rectangle<float> bar ((float) barX, (float) y + 17.f, (float) barW, 10.f);
                g.setColour (juce::Colour (0x80000000)); g.fillRoundedRectangle (bar, 5.f);
                g.setColour (pal::line); g.drawRoundedRectangle (bar.reduced (0.5f), 5.f, 1.f);
                const float half = bar.getWidth() * 0.5f, w = std::abs (l.amount) * half;
                g.setColour (pal::amber);
                g.fillRoundedRectangle (l.amount >= 0.f ? bar.getCentreX() : bar.getCentreX() - w, bar.getY(), juce::jmax (2.f, w), bar.getHeight(), 5.f);
                g.setColour (pal::textDim); g.fillRect (bar.getCentreX() - 1.f, bar.getY() - 3.f, 2.f, bar.getHeight() + 6.f);
                const int pct = juce::roundToInt (l.amount * 100.f);
                drawText (g, (pct > 0 ? "+" : "") + juce::String (pct) + "%", { barX + barW + 8, y + 11, 56, 22 }, mono (12.f), pal::amber, juce::Justification::centredRight);
                drawText (g, juce::roundToInt (state.getRawParameterValue (jd::lfoId (l.lfo, "polar"))->load()) == 0 ? "Uni" : "Bi", { 740, y + 11, 50, 22 }, mono (11.f), pal::textSoft);

                const juce::Rectangle<float> x ((float) getWidth() - 28.f, (float) y + 9.f, 26.f, 26.f);
                g.setColour (pal::line); g.drawEllipse (x.reduced (0.5f), 1.f);
                g.setColour (pal::text);
                g.drawLine (x.getX() + 9.f, x.getY() + 9.f, x.getRight() - 9.f, x.getBottom() - 9.f, 1.6f);
                g.drawLine (x.getRight() - 9.f, x.getY() + 9.f, x.getX() + 9.f, x.getBottom() - 9.f, 1.6f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto list = rows();
            const int i = e.y / rowH;
            if (i < 0 || i >= (int) list.size()) return;
            dragRow = -1;
            if (e.x >= getWidth() - 32) { owner.ed.proc.removeMod (list[(size_t) i].target, list[(size_t) i].lfo); return; }
            if (e.x >= barX - 8 && e.x <= barX + barW + 8) { dragRow = i; setFromX (list[(size_t) i], e.x); }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto list = rows();
            if (dragRow >= 0 && dragRow < (int) list.size()) setFromX (list[(size_t) dragRow], e.x);
        }
        void setFromX (const LinkInfo& l, int x)
        {
            const float a = juce::jlimit (-1.f, 1.f, (float) (x - (barX + barW / 2)) / (float) (barW / 2));
            owner.ed.proc.setMod (l.target, l.lfo, std::round (a * 100.f) / 100.f);
        }

        MatrixPage& owner;
        int dragRow = -1;
    };

    explicit MatrixPage (JunkDrawerEditor& e) : ed (e), table (*this)
    {
        const char* names[] = { "All", "L1", "L2", "L3", "L4", "L5", "L6" };
        for (int i = 0; i < 7; ++i)
        {
            auto& b = chips[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (303);
            b.onClick = [this, i] { filter = i - 1; table.repaint(); };
            addAndMakeVisible (b);
        }
        chips[0].setToggleState (true, juce::dontSendNotification);
        clear.setButtonText ("Clear all");
        clear.setTooltip ("Take every LFO off");
        clear.onClick = [this] { ed.proc.clearMods(); };
        addAndMakeVisible (clear);
        addAndMakeVisible (table);
        ed.proc.addChangeListener (this);
        changeListenerCallback (nullptr);
    }
    ~MatrixPage() override { ed.proc.removeChangeListener (this); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        clear.setEnabled (! ed.proc.getMods().empty());
        table.repaint();
        repaint();
    }

    void resized() override
    {
        int x = 112;
        for (int i = 0; i < 7; ++i) { const int w = i == 0 ? 48 : 42; chips[(size_t) i].setBounds (x, 168, w, 26); x += w + 6; }
        clear.setBounds (1236 - 22 - 98, 37, 98, 30);
        table.setBounds (58, 234, 864, 528);
    }

    void paint (juce::Graphics& g) override
    {
        paintDrawerFrame (g, false);
        const juce::Rectangle<float> strip (44.f, 12.f, 1192.f, 80.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (strip, 4.f);
        g.setColour (juce::Colour (0x2effebbe));
        g.drawRoundedRectangle (strip.reduced (0.5f), 4.f, 1.f);
        drawStripLabel (g, "EVERYTHING HOOKED UP", 66, 24);
        drawText (g, "Matrix", { 66, 40, 320, 32 }, serif (22.f), juce::Colour (0xfff6e8c9));
        g.setColour (pal::line); g.fillRect (390, 24, 1, 56);
        g.setColour (pal::textSoft);
        g.setFont (sans (12.f));
        g.drawFittedText ("Every control with an LFO on it, in one list. Drag a bar to change how much, or clear it with the cross.", { 410, 28, 440, 52 }, juce::Justification::centredLeft, 3, 1.f);

        const auto links = currentMod() != nullptr ? currentMod()->links() : std::vector<LinkInfo>();
        auto panel = [&] (juce::Rectangle<float> r)
        {
            g.setColour (pal::panel); g.fillRoundedRectangle (r, 4.f);
            g.setColour (juce::Colour (0x29ffebbe)); g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
        };
        panel ({ 44.f, 122.f, 892.f, 688.f });
        drawText (g, "MATRIX", { 58, 134, 200, 24 }, mono (15.f, true), pal::text);
        drawText (g, juce::String ((int) links.size()) + " hooked up", { 700, 134, 222, 24 }, mono (11.f), pal::amber, juce::Justification::centredRight);
        drawText (g, "SHOW", { 58, 168, 50, 26 }, sans (11.f, true).withExtraKerningFactor (0.14f), pal::textDim);
        const char* heads[] = { "TARGET", "LFO", "AMOUNT", "POLARITY" };
        const int hx[] = { 58, 374, 536, 798 };
        for (int i = 0; i < 4; ++i) drawText (g, heads[i], { hx[i], 206, 160, 24 }, sans (11.f, true).withExtraKerningFactor (0.14f), pal::textDim);
        drawText (g, "CLEAR", { 822, 206, 100, 24 }, sans (11.f, true).withExtraKerningFactor (0.14f), pal::textDim, juce::Justification::centredRight);
        g.setColour (pal::line); g.fillRect (58, 230, 864, 1);
        drawText (g, "Amounts run from -100% to +100% of the control's range.", { 58, 774, 864, 24 }, sans (11.f), pal::textDim);

        // the six LFOs
        panel ({ 948.f, 122.f, 288.f, 338.f });
        drawText (g, "LFOS", { 962, 134, 200, 24 }, mono (15.f, true), pal::text);
        static const char* full[] = { "Sine", "Triangle", "Saw", "Square", "Sample and hold", "Smooth random", "Drift" };
        auto& st = ed.proc.state;
        for (int i = 0; i < jd::kLfos; ++i)
        {
            const int y = 122 + 46 + i * 46;
            g.setColour (juce::Colour (0x1af0e2c4)); g.fillRect (962, y + 45, 260, 1);
            drawChip (g, { 962, y + 12, 36, 22 }, "L" + juce::String (i + 1));
            const int shape = juce::jlimit (0, 6, juce::roundToInt (st.getRawParameterValue (jd::lfoId (i, "shape"))->load()));
            const int sync = juce::roundToInt (st.getRawParameterValue (jd::lfoId (i, "sync"))->load());
            const float hz = st.getRawParameterValue (jd::lfoId (i, "rate"))->load();
            const int div = juce::roundToInt (st.getRawParameterValue (jd::lfoId (i, "div"))->load());
            const bool global = st.getRawParameterValue (jd::lfoId (i, "global"))->load() > 0.5f;
            const juce::String rate = sync == 0 ? juce::String (hz, hz < 10.f ? 2 : 1) + " Hz" : jd::kLfoSyncsShort (div, sync);
            int n = 0;
            for (auto& l : links) if (l.lfo == i) ++n;
            drawText (g, full[shape], { 1008, y + 6, 130, 18 }, sans (12.f, true), pal::text);
            drawText (g, juce::String (global ? "Global" : "Per track") + " " + juce::String::charToString (0x00b7) + " " + rate, { 1008, y + 24, 140, 16 }, sans (11.f), pal::textDim);
            drawText (g, juce::String (n) + " hooked", { 1140, y + 12, 82, 22 }, mono (11.f), pal::amber, juce::Justification::centredRight);
        }

        // how to hook one up
        const juce::Rectangle<float> card (948.f, 474.f, 288.f, 336.f);
        g.setColour (juce::Colour (0x66000000)); g.fillRoundedRectangle (card.translated (0.f, 4.f), 3.f);
        g.setColour (juce::Colour (0xfff3ead0)); g.fillRoundedRectangle (card, 3.f);
        const auto ink = juce::Colour (0xff2a1b10);
        drawText (g, "HOOK ONE UP", { 968, 492, 248, 16 }, sans (11.f, true).withExtraKerningFactor (0.16f), juce::Colour (0xff5a4329));
        const char* steps[] = { "Pick an LFO on the LFOs page, or click one in the top strip.", "Drag any knob up or down to set how much it moves.", "It lands in this list. Change the amount here or clear it." };
        for (int i = 0; i < 3; ++i)
        {
            const int y = 524 + i * 52;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3dc98), 978.f, (float) y, juce::Colour (0xff8a6528), 996.f, (float) y + 22.f, true));
            g.fillEllipse (968.f, (float) y, 22.f, 22.f);
            drawText (g, juce::String (i + 1), { 968, y, 22, 22 }, mono (11.f, true), ink, juce::Justification::centred);
            g.setFont (sans (12.5f)); g.setColour (ink);
            g.drawFittedText (steps[i], { 1000, y - 4, 216, 44 }, juce::Justification::centredLeft, 3, 1.f);
        }
        if (links.empty())
        {
            g.setColour (juce::Colour (0x4d3c2814)); g.fillRect (968, 690, 248, 1);
            drawText (g, "Nothing attached yet.", { 968, 700, 248, 20 }, mono (12.f), juce::Colour (0xff5a4329));
            drawText (g, "Pick an LFO and hook it up.", { 968, 722, 248, 20 }, mono (12.f), juce::Colour (0xff5a4329));
        }
    }

    JunkDrawerEditor& ed;
    Table table;
    std::array<juce::TextButton, 7> chips;
    juce::TextButton clear;
    int filter = -1;
};

//==============================================================================
//==============================================================================
// Export window: a dim sheet over the whole editor with the options card, and the first-time folder prompt.
namespace
{
struct ExportPanel : juce::Component, juce::Timer
{
    explicit ExportPanel (JunkDrawerProcessor& p)
        : proc (p), what ({ "Master mix", "One file per track" }), lenKind ({ "Passes", "Time", "Bars" }), fileKind ({ "WAV", "AIFF", "FLAC" }), bits ({ "24-bit", "16-bit, dithered" }),
          rate ({ "96 kHz", "48 kHz", "44.1 kHz" }), ending ({ "Cut", "Fade 2 s", "Fade 5 s", "Fade 10 s", "Ring 3 s", "Ring 8 s" }), norm ({ "Off", "Relative", "Maximum" })
    {
        setInterceptsMouseClicks (true, true);
        what.setTooltip ("The whole mix in one file, or one file for each track that plays");
        lenKind.setTooltip ("Ask for the length in passes of the longest lane, in minutes and seconds, or in bars");
        fileKind.setTooltip ("WAV, AIFF or FLAC");
        bits.setTooltip ("16-bit files are dithered");
        rate.setTooltip ("Sample rate of the files");
        ending.setTooltip ("Cut stops at the end. Fade lowers the level inside the length. Ring out stops the pattern and lets Delay and Reverb tails finish after the length.");
        norm.setTooltip ("Off leaves the level as it plays. Relative raises everything together until the loudest file peaks at -0.3 dB. Maximum does that to each file.");
        for (auto* c : std::initializer_list<juce::Component*> { &what, &lenKind, &fileKind, &bits, &rate, &ending, &norm }) addAndMakeVisible (*c);
        for (auto* c : std::initializer_list<PickSeg*> { &what, &lenKind, &fileKind, &bits, &rate, &ending, &norm }) c->onChange = [this] { readControls(); updateInfo(); };

        field.setJustification (juce::Justification::centred);
        field.setFont (mono (13.f, true));
        field.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffffaf0));
        field.setColour (juce::TextEditor::textColourId, juce::Colour (0xff2a1b10));
        field.setColour (juce::TextEditor::outlineColourId, juce::Colour (0x00000000));
        field.setColour (juce::TextEditor::focusedOutlineColourId, pal::amber);
        field.setColour (juce::TextEditor::highlightColourId, pal::amber.withAlpha (0.5f));
        field.setColour (juce::CaretComponent::caretColourId, juce::Colour (0xff2a1b10));
        field.setInputRestrictions (8, "0123456789:.");
        field.onTextChange = [this] { readControls(); updateInfo(); };
        field.onReturnKey = [this] { field.giveAwayKeyboardFocus(); };
        addAndMakeVisible (field);

        folderBtn.setButtonText ("Change..."); folderBtn.onClick = [this] { showFolderMenu(); };
        cancel.setButtonText ("Cancel");       cancel.onClick = [this] { if (running()) job->cancel.store (true); else close(); };
        go.setButtonText ("Export");           go.setToggleState (true, juce::dontSendNotification); go.onClick = [this] { if (done) reveal(); else begin(); };
        for (auto* b : { &folderBtn, &cancel, &go }) addAndMakeVisible (*b);

        prompt.pick.setButtonText ("Choose a folder...");
        prompt.pick.setToggleState (true, juce::dontSendNotification);
        prompt.automatic.setButtonText ("Auto-assign (temporary folder)");
        prompt.pick.onClick = [this] { chooseFolder (true); };
        prompt.automatic.onClick = [this] { settings.mode = 1; settings.save(); prompt.setVisible (false); updateInfo(); begin(); };
        addChildComponent (prompt);
        setVisible (false);
    }
    ~ExportPanel() override { stopTimer(); }

    bool running() const { return job != nullptr && ! job->finished.load(); }

    void open()
    {
        if (running()) { setVisible (true); toFront (false); return; }
        settings = jdx::ExportSettings::load();
        jdx::ExportSettings::loadOptions (opt);
        snap = jdx::takeSnapshot (proc);
        job.reset(); done = false; status = {}; statusOk = true; prompt.setVisible (false);
        syncControls();
        setVisible (true); toFront (true);
        updateInfo();
        resized();
    }
    void close() { stopTimer(); prompt.setVisible (false); setVisible (false); }

    //---- options <-> controls
    void syncControls()
    {
        what.setSelected (opt.mode == jdx::ExportMode::Stems ? 1 : 0);
        lenKind.setSelected ((int) opt.lengthKind);
        fileKind.setSelected ((int) opt.file);
        bits.setSelected (opt.bits == 16 ? 1 : 0);
        rate.setSelected (opt.rate >= 96000.0 ? 0 : opt.rate >= 48000.0 ? 1 : 2);
        int e = 0;
        if (opt.ending == jdx::EndingKind::Fade) e = opt.endingSeconds >= 10.0 ? 3 : opt.endingSeconds >= 5.0 ? 2 : 1;
        else if (opt.ending == jdx::EndingKind::RingOut) e = opt.endingSeconds >= 8.0 ? 5 : 4;
        ending.setSelected (e);
        norm.setSelected ((int) opt.norm);
        showLengthText();
    }
    void showLengthText()
    {
        switch (opt.lengthKind)
        {
            case jdx::LengthKind::Passes: field.setText (juce::String (opt.passes), false); break;
            case jdx::LengthKind::Bars:   field.setText (juce::String (opt.bars), false); break;
            default:                      field.setText (clock (opt.timeSeconds), false); break;
        }
    }
    static juce::String clock (double s) { const int m = (int) (s / 60.0), r = (int) std::round (s - m * 60.0); return juce::String (m) + ":" + juce::String (r).paddedLeft ('0', 2); }
    static double parseClock (const juce::String& t)
    {
        const int c = t.indexOfChar (':');
        if (c < 0) return t.getDoubleValue();
        return t.substring (0, c).getIntValue() * 60.0 + t.substring (c + 1).getDoubleValue();
    }
    void readControls()
    {
        opt.mode = what.selected() == 1 ? jdx::ExportMode::Stems : jdx::ExportMode::Master;
        const auto before = opt.lengthKind;
        opt.lengthKind = (jdx::LengthKind) lenKind.selected();
        if (opt.lengthKind != before) showLengthText();
        else
        {
            const auto t = field.getText();
            if (opt.lengthKind == jdx::LengthKind::Passes) opt.passes = juce::jlimit (1, 16, t.getIntValue());
            else if (opt.lengthKind == jdx::LengthKind::Bars) opt.bars = juce::jlimit (1, 999, t.getIntValue());
            else opt.timeSeconds = juce::jlimit (1.0, jdx::kMaxExportSeconds, parseClock (t));
        }
        opt.file = (jdx::FileKind) fileKind.selected();
        opt.bits = bits.selected() == 1 ? 16 : 24;
        opt.rate = rate.selected() == 0 ? 96000.0 : rate.selected() == 1 ? 48000.0 : 44100.0;
        static const double sec[] = { 0.0, 2.0, 5.0, 10.0, 3.0, 8.0 };
        const int e = ending.selected();
        opt.ending = e == 0 ? jdx::EndingKind::Cut : e <= 3 ? jdx::EndingKind::Fade : jdx::EndingKind::RingOut;
        opt.endingSeconds = sec[e];
        opt.norm = (jdx::NormMode) norm.selected();
    }

    void updateInfo()
    {
        info = {};
        lengthNote = {};
        canExport = false;
        if (snap == nullptr) return;
        const auto audible = jdx::Exporter::audibleTracks (*snap);
        if (audible.empty()) { info = "Nothing to export yet. Load a sound into a track first."; repaint(); return; }
        const double pass = jdx::Exporter::passSeconds (*snap, audible);
        const double main = jdx::exportMainSeconds (opt, pass, snap->bpm, snap->beatsPerBar);
        const double total = main + jdx::exportTailSeconds (opt);
        const int files = opt.mode == jdx::ExportMode::Stems ? (int) audible.size() : 1;
        const double bytes = jdx::exportBytesPerFile (opt, total);
        const juce::String each = juce::File::descriptionOfSizeInBytes ((juce::int64) bytes);
        info = juce::String (files) + (files == 1 ? " file" : " files") + ", about " + each + (files == 1 ? "" : " each")
             + (opt.file == jdx::FileKind::Flac ? " (FLAC size is an estimate)" : "") + ".";
        const juce::String len = main < 10.0 ? juce::String (main, 1) + " s" : clock (main);
        lengthNote = opt.lengthKind == jdx::LengthKind::Passes ? (opt.passes == 1 ? "pass (1-16)" : "passes (1-16)") + juce::String (" ") + juce::String::charToString (0x00b7) + " " + len + " long"
                   : opt.lengthKind == jdx::LengthKind::Bars   ? (opt.bars == 1 ? "bar" : "bars") + juce::String (" at ") + juce::String (snap->bpm, 0) + " BPM " + juce::String::charToString (0x00b7) + " " + len + " long"
                                                               : "minutes:seconds (up to 30:00)";
        canExport = true;
        repaint();
    }

    //---- folder
    juce::String folderLabel() const
    {
        return settings.mode == 2 ? settings.folder.getFullPathName()
             : settings.mode == 1 ? juce::String ("Automatic - temporary folder (your system clears it)")
                                  : juce::String ("Not chosen yet");
    }
    void showFolderMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Choose a folder...");
        m.addItem (2, "Auto-assign (temporary folder)", true, settings.mode == 1);
        m.addSeparator();
        m.addItem (3, "Show the current folder", settings.root().isDirectory());
        juce::Component::SafePointer<ExportPanel> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderBtn), [self] (int r)
        {
            if (self == nullptr) return;
            if (r == 1) self->chooseFolder (false);
            else if (r == 2) { self->settings.mode = 1; self->settings.save(); self->repaint(); }
            else if (r == 3) self->settings.root().revealToUser();
        });
    }
    void chooseFolder (bool thenExport)
    {
        auto start = settings.mode == 2 && settings.folder.isDirectory() ? settings.folder : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Choose the folder Junk Drawer exports go into", start);
        juce::Component::SafePointer<ExportPanel> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [self, thenExport] (const juce::FileChooser& fc)
        {
            if (self == nullptr) return;
            const auto dir = fc.getResult();
            if (dir == juce::File() || ! dir.isDirectory()) return;
            self->settings.mode = 2; self->settings.folder = dir; self->settings.save();
            self->prompt.setVisible (false);
            self->repaint();
            if (thenExport) self->begin();
        });
    }
    void reveal() { if (lastFolder.isDirectory()) lastFolder.revealToUser(); }

    //---- running
    void begin()
    {
        if (running() || ! canExport) return;
        settings = jdx::ExportSettings::load();
        if (settings.mode == 0) { prompt.setVisible (true); prompt.toFront (false); resized(); repaint(); return; }
        auto root = settings.root();
        if (settings.mode == 2 && ! root.isDirectory() && ! root.createDirectory().wasOk())
        {
            status = "The export folder is not available (" + root.getFullPathName() + ") - please choose another.";
            statusOk = false;
            repaint();
            chooseFolder (false);
            return;
        }
        jdx::ExportSettings::saveOptions (opt);
        auto o = opt;
        o.folder = root.getChildFile ("Junk Drawer Export " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
        lastFolder = o.folder;
        status = {}; statusOk = true; done = false;
        snap = jdx::takeSnapshot (proc);
        juce::Component::SafePointer<ExportPanel> self (this);
        job = std::make_unique<jdx::ExportThread> (jdx::takeSnapshot (proc), o, [self] (jdx::ExportResult r) { if (self != nullptr) self->finished (r); });
        job->startThread();
        startTimerHz (15);
        setBusy (true);
    }
    void finished (const jdx::ExportResult& r)
    {
        stopTimer();
        done = r.ok;
        statusOk = r.ok;
        status = r.ok ? "Done. " + juce::String (r.files.size()) + (r.files.size() == 1 ? " file, " : " files, ") + juce::File::descriptionOfSizeInBytes (r.bytes) + "." : r.error;
        setBusy (false);
        resized(); repaint();
    }
    void setBusy (bool b)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &what, &lenKind, &fileKind, &bits, &rate, &ending, &norm, &field, &folderBtn }) c->setEnabled (! b);
        go.setEnabled (! b);
        cancel.setButtonText (b ? "Stop" : (done ? "Close" : "Cancel"));
        go.setButtonText (done ? "Show in folder" : "Export");
        resized(); repaint();
    }
    void timerCallback() override { repaint (cardArea()); }

    //---- layout and painting
    juce::Rectangle<int> cardArea() const { return { (getWidth() - 640) / 2, 130, 640, 456 }; }

    void resized() override
    {
        const auto c = cardArea();
        const int cx = c.getX() + 28 + 112, w = 640 - 56 - 112;
        int y = c.getY() + 84;
        auto place = [&] (juce::Component& k, int yy, int ww = -1) { k.setBounds (cx, yy + 5, ww < 0 ? w : ww, 26); };
        place (what, y);
        place (lenKind, y + 36, 190);
        field.setBounds (cx + 200, y + 36 + 5, 70, 26);
        place (fileKind, y + 72); place (bits, y + 108); place (rate, y + 144); place (ending, y + 180); place (norm, y + 216);
        folderBtn.setBounds (cx + w - 86, y + 252 + 7, 86, 26);
        const int by = c.getBottom() - 22 - 32;
        go.setBounds (c.getRight() - 28 - 140, by, 140, 32);
        cancel.setBounds (c.getRight() - 28 - 140 - 8 - 92, by, 92, 32);
        prompt.setBounds ((getWidth() - 520) / 2, 300, 520, 176);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0x9e0a0603));
        const auto c = cardArea();
        const auto ink = juce::Colour (0xff2a1b10), sub = juce::Colour (0xff5a4329);
        g.setColour (juce::Colour (0x99000000)); g.fillRoundedRectangle (c.toFloat().translated (0.f, 8.f).expanded (2.f), 8.f);
        g.setColour (juce::Colour (0xfff3ead0)); g.fillRoundedRectangle (c.toFloat(), 6.f);
        g.setColour (juce::Colour (0x665a3c1e)); g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 6.f, 1.f);
        drawText (g, "Export", { c.getX() + 28, c.getY() + 20, 400, 32 }, serif (26.f, false), ink);
        drawText (g, "Plays the pattern offline, with the LFOs and effects moving the whole way.", { c.getX() + 28, c.getY() + 54, 584, 18 }, sans (12.f), sub);

        static const char* labels[] = { "WHAT", "LENGTH", "FILE TYPE", "BIT DEPTH", "SAMPLE RATE", "ENDING", "NORMALIZE", "FOLDER" };
        for (int i = 0; i < 8; ++i)
            drawText (g, labels[i], { c.getX() + 28, c.getY() + 84 + i * 36 + 5, 108, 26 }, sans (11.f, true).withExtraKerningFactor (0.14f), sub);

        g.setColour (juce::Colour (0xf0241710));
        for (auto* k : std::initializer_list<juce::Component*> { &what, &lenKind, &fileKind, &bits, &rate, &ending, &norm, &folderBtn, &cancel })
            g.fillRoundedRectangle (k->getBounds().toFloat(), 4.f);
        g.setColour (juce::Colour (0xfffffaf0)); g.fillRoundedRectangle (field.getBounds().toFloat(), 4.f);

        drawText (g, lengthNote, { field.getRight() + 10, field.getY(), 472 - 280, 26 }, sans (12.f), sub);

        const int cx = c.getX() + 28 + 112, fy = c.getY() + 84 + 7 * 36 + 7;
        g.setColour (juce::Colour (0xffe6dab6)); g.fillRoundedRectangle ((float) cx, (float) fy, 472.f - 96.f, 26.f, 4.f);
        drawText (g, folderLabel(), { cx + 10, fy, 472 - 96 - 20, 26 }, mono (11.f), ink);

        g.setColour (juce::Colour (0x40281608)); g.fillRect (c.getX() + 28, c.getBottom() - 22 - 32 - 14, 584, 1);
        const int by = c.getBottom() - 22 - 32;
        if (running())
        {
            const juce::Rectangle<float> bar ((float) c.getX() + 28.f, (float) by + 11.f, 340.f, 10.f);
            g.setColour (juce::Colour (0xff3a2415)); g.fillRoundedRectangle (bar, 5.f);
            g.setColour (pal::amber); g.fillRoundedRectangle (bar.withWidth (juce::jmax (10.f, bar.getWidth() * job->progress.load())), 5.f);
            drawText (g, juce::String (juce::roundToInt (job->progress.load() * 100.f)) + "%", { c.getX() + 28 + 350, by, 60, 32 }, mono (12.f, true), ink);
        }
        else if (status.isNotEmpty())
            drawText (g, status, { c.getX() + 28, by - 4, 340, 40 }, sans (12.f, true), statusOk ? juce::Colour (0xff3d5a22) : juce::Colour (0xff7a2e16), juce::Justification::centredLeft);
        else
        {
            drawText (g, info, { c.getX() + 28, by - 2, 340, 18 }, sans (12.f), sub);
            drawText (g, "Each export gets its own dated folder.", { c.getX() + 28, by + 16, 340, 18 }, sans (12.f), sub);
        }
        go.setEnabled (canExport && ! running());
        if (! go.isEnabled() && ! done) { g.setColour (juce::Colour (0x66f3ead0)); }
    }

    struct Prompt : juce::Component
    {
        Prompt() { addAndMakeVisible (pick); addAndMakeVisible (automatic); }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xcc000000)); g.fillRoundedRectangle (r.translated (0.f, 8.f), 6.f);
            g.setColour (juce::Colour (0xfff3ead0)); g.fillRoundedRectangle (r, 6.f);
            g.setColour (pal::brass); g.drawRoundedRectangle (r.reduced (1.f), 6.f, 2.f);
            drawText (g, "Where should exports go?", { 26, 20, getWidth() - 52, 30 }, serif (22.f, false), juce::Colour (0xff2a1b10));
            g.setFont (sans (13.f)); g.setColour (juce::Colour (0xff4a3320));
            g.drawFittedText ("Pick a folder of your own, or let Junk Drawer save to a temporary folder. Each export gets its own dated sub-folder. You can change this any time with Change... in the Export window.",
                              juce::Rectangle<int> (26, 54, getWidth() - 52, 60), juce::Justification::topLeft, 3);
            g.setColour (juce::Colour (0xf0241710));
            g.fillRoundedRectangle (pick.getBounds().toFloat(), 4.f); g.fillRoundedRectangle (automatic.getBounds().toFloat(), 4.f);
        }
        void resized() override { pick.setBounds (26, getHeight() - 26 - 36, 200, 36); automatic.setBounds (236, getHeight() - 26 - 36, getWidth() - 236 - 26, 36); }
        juce::TextButton pick, automatic;
    };

    void mouseDown (const juce::MouseEvent&) override {}                  // the sheet swallows clicks meant for the page below
    JunkDrawerProcessor& proc;
    jdx::ExportOptions opt;
    jdx::ExportSettings settings;
    std::unique_ptr<jdx::ExportSnapshot> snap;
    std::unique_ptr<jdx::ExportThread> job;
    ui::PickSeg what, lenKind, fileKind, bits, rate, ending, norm;
    juce::TextEditor field;
    juce::TextButton folderBtn, cancel, go;
    Prompt prompt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;
    juce::String info, lengthNote, status;
    bool canExport = false, done = false, statusOk = true;
};
} // namespace

JunkDrawerEditor::JunkDrawerEditor (JunkDrawerProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (kW, kH);
    setResizable (false, false);
    buildModContext();
    currentMod() = &modCtx;
    proc.addChangeListener (this);

    header = std::make_unique<Header> (*this);
    drawerPage = std::make_unique<DrawerPage> (*this);
    lfoPage = std::make_unique<LfoPage> (*this);
    masterPage = std::make_unique<MasterPage> (*this);
    matrixPage = std::make_unique<MatrixPage> (*this);
    songPage = std::make_unique<SongPage> (*this);
    exportOverlay = std::make_unique<ExportPanel> (proc);
    addAndMakeVisible (*header);
    for (juce::Component* c : { (juce::Component*) drawerPage.get(), (juce::Component*) lfoPage.get(), (juce::Component*) masterPage.get(), (juce::Component*) matrixPage.get(), (juce::Component*) songPage.get() })
        addChildComponent (c);
    addChildComponent (*exportOverlay);
    showPage (Drawer);
    resized();
}

void JunkDrawerEditor::openExport() { static_cast<ExportPanel&> (*exportOverlay).open(); }

JunkDrawerEditor::~JunkDrawerEditor()
{
    proc.removeChangeListener (this);
    if (currentMod() == &modCtx) currentMod() = nullptr;
    setLookAndFeel (nullptr);
}

void JunkDrawerEditor::buildModContext()
{
    modCtx.armed = [this] { return proc.armedLfo(); };
    modCtx.amount = [this] (const juce::String& id, int lfo) { return proc.modAmount (id, lfo); };
    modCtx.setAmount = [this] (const juce::String& id, int lfo, float a) { proc.setMod (id, lfo, a); };
    modCtx.remove = [this] (const juce::String& id, int lfo) { proc.removeMod (id, lfo); };
    modCtx.lfosOn = [this] (const juce::String& id)
    {
        std::vector<int> out;
        for (auto& a : proc.getMods()) if (a.target == id && std::find (out.begin(), out.end(), a.lfo) == out.end()) out.push_back (a.lfo);
        std::sort (out.begin(), out.end());
        return out;
    };
    modCtx.links = [this]
    {
        std::vector<LinkInfo> out;
        for (auto& a : proc.getMods())
        {
            auto d = describeTarget (proc.state, a.target);
            out.push_back ({ a.target, d.first, d.second, a.lfo, a.amount });
        }
        return out;
    };
}

void JunkDrawerEditor::showPage (int p)
{
    page = juce::jlimit (0, (int) NumPages - 1, p);
    juce::Component* pages[] = { drawerPage.get(), lfoPage.get(), masterPage.get(), matrixPage.get(), songPage.get() };
    for (int i = 0; i < NumPages; ++i) pages[i]->setVisible (i == page);
    header->select (page);
}

void JunkDrawerEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (pal::counterTop, 0, 0, pal::counterBottom, 0, (float) kH, false));
    g.fillAll();
}

void JunkDrawerEditor::resized()
{
    if (header == nullptr || songPage == nullptr) return;                 // setSize() runs before the pages exist
    header->setBounds (0, 0, kW, 52);
    for (juce::Component* c : { (juce::Component*) drawerPage.get(), (juce::Component*) lfoPage.get(), (juce::Component*) masterPage.get(), (juce::Component*) matrixPage.get(), (juce::Component*) songPage.get() })
        c->setBounds (0, 52, kW, kH - 52);
    if (exportOverlay != nullptr) exportOverlay->setBounds (getLocalBounds());
}
