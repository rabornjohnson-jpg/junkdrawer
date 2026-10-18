#include <set>
// Renders every page to PNG and checks the layout: controls inside their panels, no two controls overlapping.
// Usage:  JunkUiSnapshot <output folder>     (under xvfb-run on a machine without a display)
#include "PluginEditor.h"
#include "Exporter.h"

static void paintAll (JunkDrawerEditor& root, juce::Component& c);
namespace
{
bool isLeaf (juce::Component* c)
{
    return dynamic_cast<ui::Knob*> (c) || dynamic_cast<ui::Seg*> (c) || dynamic_cast<ui::PickSeg*> (c) || dynamic_cast<juce::Button*> (c);
}

void collect (juce::Component* c, std::vector<juce::Component*>& out)
{
    if (c->getParentComponent() != nullptr && ! c->isVisible()) return;
    if (isLeaf (c)) { out.push_back (c); return; }
    for (auto* k : c->getChildren()) collect (k, out);
}

bool visibleWithin (juce::Component* c, juce::Component* root)
{
    for (; c != nullptr && c != root; c = c->getParentComponent()) if (! c->isVisible()) return false;
    return true;
}

juce::String describe (juce::Component* c)
{
    if (auto* b = dynamic_cast<juce::Button*> (c)) return "button '" + b->getButtonText() + "'";
    return juce::String (typeid (*c).name()) + " '" + c->getTitle() + "'";
}

int checkLayout (juce::Component& root, const juce::String& page)
{
    int cut = 0;
    auto* ed = dynamic_cast<JunkDrawerEditor*> (&root);
    if (ed == nullptr) ed = root.findParentComponentOfClass<JunkDrawerEditor>();
    if (ed != nullptr)
    {
        std::vector<juce::String> over;
        ui::overflowSink() = &over; paintAll (*ed, root); ui::overflowSink() = nullptr;
        std::set<juce::String> seen;
        for (auto& s : over) if (seen.insert (s).second) { std::printf ("  [%s] TEXT DOES NOT FIT: %s\n", page.toRawUTF8(), s.toRawUTF8()); ++cut; }
    }
    std::vector<juce::Component*> v;
    collect (&root, v);
    int bad = cut;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (! visibleWithin (v[i], &root)) continue;
        const auto a = root.getLocalArea (v[i], v[i]->getLocalBounds());
        if (! root.getLocalBounds().contains (a)) { std::printf ("  [%s] OUTSIDE WINDOW: %s\n", page.toRawUTF8(), describe (v[i]).toRawUTF8()); ++bad; }
        if (auto* b = dynamic_cast<juce::TextButton*> (v[i]))
        {
            const float tw = b->getLookAndFeel().getTextButtonFont (*b, b->getHeight()).getStringWidthFloat (b->getButtonText());
            if (tw > (float) b->getWidth() - 8.f) { std::printf ("  [%s] TEXT TOO WIDE: %s (%.0f > %d)\n", page.toRawUTF8(), describe (b).toRawUTF8(), tw, b->getWidth() - 8); ++bad; }
        }
        if (auto* s = dynamic_cast<ui::Seg*> (v[i]))
            if (s->getWidth() < s->idealWidth() - 8 * 2) { std::printf ("  [%s] SEGMENTS TIGHT: %s (%d < %d)\n", page.toRawUTF8(), describe (s).toRawUTF8(), s->getWidth(), s->idealWidth()); ++bad; }
        for (size_t j = i + 1; j < v.size(); ++j)
        {
            if (! visibleWithin (v[j], &root) || v[i]->isParentOf (v[j]) || v[j]->isParentOf (v[i])) continue;
            // the dial of a knob is the control; its label strip below may sit inside the knob's own box
            const auto b = root.getLocalArea (v[j], v[j]->getLocalBounds());
            if (a.intersects (b)) { std::printf ("  [%s] OVERLAP: %s and %s\n", page.toRawUTF8(), describe (v[i]).toRawUTF8(), describe (v[j]).toRawUTF8()); ++bad; }
        }
    }
    return bad;
}
} // namespace


// Contrast audit (JD_AUDIT=1): paint once to record every label, paint again with labels hidden to see what is behind each one.
static juce::Image snap (JunkDrawerEditor& e) { return e.createComponentSnapshot (e.getLocalBounds(), true, 1.0f); }
static double lum (juce::Colour c)
{
    auto f = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126 * f (c.getFloatRed()) + 0.7152 * f (c.getFloatGreen()) + 0.0722 * f (c.getFloatBlue());
}
// Paint every visible component on its own, remembering where it sits in the editor.
static void paintAll (JunkDrawerEditor& root, juce::Component& c)
{
    if ((&c != &root && ! c.isVisible()) || c.getWidth() <= 0 || c.getHeight() <= 0) return;
    const auto o = root.getLocalPoint (&c, juce::Point<int> (0, 0));
    ui::auditOrigin() = o.toFloat();
    juce::Image img (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    juce::Graphics g (img);
    if (c.getNumChildComponents() == 0) c.paintEntireComponent (g, false); else c.paint (g);
    for (auto* ch : c.getChildren()) paintAll (root, *ch);
}

static int auditPage (JunkDrawerEditor& e, const juce::String& name, juce::Component* only = nullptr)
{
    std::vector<ui::TextAudit> v;
    ui::auditSink() = &v; paintAll (e, only != nullptr ? *only : static_cast<juce::Component&> (e)); ui::auditSink() = nullptr;
    ui::auditHideText() = true; auto bgImg = snap (e); ui::auditHideText() = false;
    { juce::PNGImageFormat png; juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile ("audit_bg_" + name + ".png")); f.deleteFile(); juce::FileOutputStream fs (f); png.writeImageToStream (bgImg, fs); }
    int bad = 0;
    std::printf ("  [%s] audited %d labels\n", name.toRawUTF8(), (int) v.size());
    std::set<juce::String> seen;
    for (auto& a : v)
    {
        const auto r = a.r.toNearestInt().getIntersection (bgImg.getBounds());
        if (r.isEmpty() || a.t.isEmpty()) continue;
        double rr = 0, gg = 0, bb = 0; int n = 0;
        for (int y = r.getY(); y < r.getBottom(); y += 2) for (int x = r.getX(); x < r.getRight(); x += 2) { auto p = bgImg.getPixelAt (x, y); rr += p.getFloatRed(); gg += p.getFloatGreen(); bb += p.getFloatBlue(); ++n; }
        if (n == 0) continue;
        const auto bg = juce::Colour::fromFloatRGBA ((float) (rr / n), (float) (gg / n), (float) (bb / n), 1.f);
        const auto fg = bg.overlaidWith (a.c);
        const double l1 = lum (fg), l2 = lum (bg), ratio = (std::max (l1, l2) + 0.05) / (std::min (l1, l2) + 0.05);
        if (ratio < 4.5)
        {
            const auto key = a.t + "|" + juce::String (ratio, 1);
            if (seen.insert (key).second) { std::printf ("  [%s] %.1f:1  '%s' at (%d,%d)  text %08x\n", name.toRawUTF8(), ratio, a.t.substring (0, 28).toRawUTF8(), r.getX(), r.getY(), (unsigned) a.c.getARGB()); ++bad; }
        }
    }
    return bad;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    out.createDirectory();

    JunkDrawerProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // a short burst of audio so the meter has something to show
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b)
    {
        for (int i = 0; i < 512; ++i) { const float v = 0.5f * std::sin ((float) (b * 512 + i) * 0.05f); buf.setSample (0, i, v); buf.setSample (1, i, v * 0.8f); }
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<JunkDrawerEditor*> (ed.get());
    if (juce::SystemStats::getEnvironmentVariable ("JD_HC", "0") == "1") editor->setHighContrast (true);
    const char* names[] = { "drawer", "lfos", "master", "matrix", "song" };
    int problems = 0;
    const bool auditOn = juce::SystemStats::getEnvironmentVariable ("JD_AUDIT", "0") == "1";
    auto audit = [&] (const juce::String& n, juce::Component* only = nullptr) { if (auditOn) auditPage (*editor, n, only); };

    // empty drawer first, then one with a generated sample in track 1 (a few decaying hits)
    {
        editor->showPage (0);
        for (int i = 0; i < 4; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = out.getChildFile ("drawer_empty.png");
        f.deleteFile();
        juce::FileOutputStream fs (f);
        png.writeImageToStream (img, fs);
        problems += checkLayout (*editor, "drawer-empty");
        audit ("drawer-empty");
    }
    {
        auto wav = out.getChildFile ("test_hits.wav");
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), 44100.0, 2, 16, {}, 0));
        const int n = 44100 * 3;
        juce::AudioBuffer<float> b (2, n);
        const double hitAt[] = { 0.0, 0.55, 1.1, 1.45, 2.05, 2.5 };
        for (int i = 0; i < n; ++i)
        {
            float v = 0.f;
            for (double h : hitAt)
            {
                const double t = i / 44100.0 - h;
                if (t >= 0.0) v += (float) (std::exp (-t * 9.0) * std::sin (t * 2.0 * juce::MathConstants<double>::pi * (140.0 + 90.0 * h)));
            }
            b.setSample (0, i, 0.6f * v); b.setSample (1, i, 0.6f * v);
        }
        w->writeFromAudioSampleBuffer (b, 0, n);
        w.reset();
        proc.loadFile (0, wav);
        for (int i = 0; i < 100 && proc.getTrack (0) == nullptr; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        proc.sliceByHits (0, 6);
        auto setP = [&] (const juce::String& id, float v) { auto* q = proc.state.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        const int pattern[] = { 1, 0, 3, 2, 0, 4, 1, 5, 0, 6, 2, 0, 3, 1, 4, 0 };
        for (int s = 0; s < 16; ++s) setP (jd::stepId (0, s, jd::LaneRegion), (float) pattern[s]);
        for (int s = 0; s < 16; ++s) setP (jd::stepId (0, s, jd::LaneVolume), 0.35f + 0.65f * (float) ((s * 5) % 7) / 6.f);
        setP (jd::laneLenId (0, jd::LaneVolume), 12.f);
        proc.setMod ("t1_r1_cutoff", 0, 0.45f);  proc.setMod ("t1_r1_pan", 3, -0.30f);  proc.setMod ("t1_r1_gain", 1, 0.20f);
        proc.setMod ("t1_r1_drive", 0, 0.60f);   proc.setMod ("t1_r1_pitch", 2, 0.08f);
        proc.setMod ("m_swing", 4, 0.15f);       proc.setMod ("fx_filter_cut", 0, 0.55f);  proc.setMod ("fx_delay_mix", 5, 0.25f);
        proc.setMod ("fx_reverb_size", 1, -0.40f); proc.setMod ("l3_rate", 5, 0.10f);       proc.setMod ("fx_filter_res", 4, 0.30f);
        proc.setArmedLfo (1);
        editor->showPage (0);
        for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }
    {
        const int ch[][2] = { { 0, 2 }, { 1, 1 }, { 0, 1 }, { 2, 4 }, { 1, 2 } };
        for (int i = 0; i < 5; ++i) proc.setChainSlot (i, ch[i][0], ch[i][1]);
        auto* q = proc.state.getParameter ("chain_on");
        q->setValueNotifyingHost (1.f);
    }
    for (int p = 0; p < JunkDrawerEditor::NumPages; ++p)
    {
        editor->showPage (p);
        for (int i = 0; i < 4; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = out.getChildFile (juce::String (names[p]) + ".png");
        f.deleteFile();
        juce::FileOutputStream fs (f);
        png.writeImageToStream (img, fs);
        problems += checkLayout (*editor, names[p]);
        if (juce::SystemStats::getEnvironmentVariable ("JD_AUDIT", "0") == "1") auditPage (*editor, names[p]);
    }
    // the Chance and Timing lanes
    {
        std::function<juce::TextButton*(juce::Component&, const juce::String&)> findTab = [&] (juce::Component& c, const juce::String& name) -> juce::TextButton*
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == name && b->isVisible()) return b;
                if (auto* f = findTab (*ch, name)) return f;
            }
            return nullptr;
        };
        editor->showPage (0);
        for (int k = 0; k < jd::kSteps; ++k)
        {
            auto set = [&] (int lane, float v) { auto* p = proc.state.getParameter (jd::stepId (0, k, lane)); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            set (jd::LaneProb, k % 4 == 0 ? 1.f : (k % 4 == 1 ? 0.75f : (k % 4 == 2 ? 0.5f : 0.25f)));
            set (jd::LaneTime, k % 3 == 0 ? 0.f : (k % 3 == 1 ? 0.25f : -0.2f));
            set (jd::LanePan, k % 4 == 0 ? 0.f : (k % 4 == 1 ? -0.6f : (k % 4 == 2 ? 0.4f : 1.f)));
        }
        for (const char* nm : { "Chance", "Timing", "Pan" })
        {
            if (auto* b = findTab (*editor, nm)) b->triggerClick();
            for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto f = out.getChildFile (juce::String ("lane_") + juce::String (nm).toLowerCase() + ".png");
            f.deleteFile();
            juce::FileOutputStream fs (f);
            png.writeImageToStream (img, fs);
            problems += checkLayout (*editor, nm);
            audit (juce::String ("lane-") + nm);
        }
    }
    // the recording panel, set up and ready (opened with the Record button, as a person would)
    {
        std::function<juce::TextButton*(juce::Component&)> find = [&] (juce::Component& c) -> juce::TextButton*
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "Record" && b->isVisible()) return b;
                if (auto* f = find (*ch)) return f;
            }
            return nullptr;
        };
        editor->showPage (0);
        for (int i = 0; i < 4; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        if (auto* b = find (*editor)) b->triggerClick();
        proc.setInputMeter (true);
        proc.setListening (true);
        juce::AudioBuffer<float> mb (2, 512); juce::MidiBuffer mm;
        for (int k = 0; k < 6; ++k) { for (int i = 0; i < 512; ++i) { mb.setSample (0, i, 0.35f * std::sin ((float) i * 0.1f)); mb.setSample (1, i, 0.35f * std::sin ((float) i * 0.1f)); } proc.processBlock (mb, mm); }
        for (int k = 0; k < 400; ++k) proc.processBlock (mb, mm);
        for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = out.getChildFile ("record_ready.png");
        f.deleteFile();
        juce::FileOutputStream fs (f);
        png.writeImageToStream (img, fs);
        problems += checkLayout (*editor, "record-ready");
        audit ("record-ready");
    }
    // a take in progress: the recording panel replaces the waveform sheet
    {
        proc.startCatch (0, 0, 0, 0);
        juce::AudioBuffer<float> rb (2, 512);
        for (int b = 0; b < 140; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (double) (b * 512 + i) / 48000.0;
                const float env = 0.15f + 0.7f * std::abs ((float) std::sin (t * 2.3)) * (0.5f + 0.5f * (float) std::sin (t * 17.0));
                const float v = env * std::sin ((float) t * 900.f);
                rb.setSample (0, i, v); rb.setSample (1, i, v * 0.8f);
            }
            proc.processBlock (rb, midi);
        }
        editor->showPage (0);
        for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = out.getChildFile ("record.png");
        f.deleteFile();
        juce::FileOutputStream fs (f);
        png.writeImageToStream (img, fs);
        problems += checkLayout (*editor, "record");
        audit ("record");
        proc.stopCatch (false);
        proc.processBlock (rb, midi);
        for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    }
    // the Export window, with a folder chosen, and then the first-time folder prompt
    {
        auto shot = [&] (const char* name)
        {
            for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto f = out.getChildFile (name);
            f.deleteFile();
            juce::FileOutputStream fs (f);
            png.writeImageToStream (img, fs);
            juce::Component* only = juce::String (name) == "about.png" ? &editor->aboutWindow() : &editor->exportWindow();
            if (juce::String (name) == "export_first.png")        // the folder prompt sits on top of the options card: audit the prompt alone
                for (int k = only->getNumChildComponents() - 1; k >= 0; --k) if (only->getChildComponent (k)->isVisible()) { only = only->getChildComponent (k); break; }
            audit (name, only);
        };
        jdx::ExportSettings st; st.mode = 2; st.folder = juce::File ("/home/ray/Music/Rotary Dial exports"); st.save();
        editor->showPage (0);
        editor->openExport();
        shot ("export.png");
        problems += checkLayout (editor->exportWindow(), "export");

        editor->exportWindow().setVisible (false);
        editor->openAbout();
        shot ("about.png");
        problems += checkLayout (editor->aboutWindow(), "about");
        editor->aboutWindow().setVisible (false);

        st.mode = 0; st.save();
        editor->openExport();
        std::function<juce::TextButton*(juce::Component&)> findGo = [&] (juce::Component& c) -> juce::TextButton*
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "Export" && b->isVisible()) return b;
                if (auto* f = findGo (*ch)) return f;
            }
            return nullptr;
        };
        if (auto* b = findGo (editor->exportWindow())) b->triggerClick();
        shot ("export_first.png");
        // (the prompt sits on top of the options card on purpose, so the overlap check is not run for it)
        // a real export through the window: automatic folder, one click, wait for the files
        st.mode = 1; st.save();
        editor->openExport();
        if (auto* b = findGo (editor->exportWindow())) b->triggerClick();
        for (int i = 0; i < 400; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (50); if (findGo (editor->exportWindow()) == nullptr) break; }
        shot ("export_done.png");
        auto tmpRoot = juce::File::getSpecialLocation (juce::File::tempDirectory);
        int made = 0;
        for (auto& d : tmpRoot.findChildFiles (juce::File::findDirectories, false, "Rotary Dial Export *"))
        { for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav")) { ++made; std::printf ("export wrote %s (%lld bytes)\n", f.getFileName().toRawUTF8(), (long long) f.getSize()); } d.deleteRecursively(); }
        if (made == 0) { std::printf ("export through the window wrote nothing\n"); ++problems; }
        jdx::ExportSettings none; none.save();
    }

    // Every page that refreshes itself on a timer must be ticking whenever it is on screen, also after the look was switched there and back.
    {
        auto idle = [&] (const char* when)
        {
            int dead = 0, total = 0;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* t = dynamic_cast<juce::Timer*> (&c))
                {
                    const juce::String n (typeid (c).name());
                    if (ui::shown (c) && (n.contains ("StepGrid") || n.contains ("LfoTile") || n.contains ("TrackRow") || n.contains ("SongPage") || n.contains ("Junk")))
                    { ++total; if (! t->isTimerRunning()) { ++dead; std::printf ("  not ticking: %s\n", typeid (c).name()); } }
                }
                for (auto* ch : c.getChildren()) walk (*ch);
            };
            walk (*editor);
            std::printf ("timers %s: %d on screen, %d not ticking\n", when, total, dead);
            problems += dead;
        };
        editor->showPage (0);
        idle ("at the start");
        editor->setHighContrast (true);  editor->showPage (0);
        idle ("in high contrast");
        editor->setHighContrast (false); editor->showPage (0);
        idle ("back in the standard look");
    }
    std::printf ("layout problems: %d\n", problems);
    ed.reset();
    return problems == 0 ? 0 : 1;
}
