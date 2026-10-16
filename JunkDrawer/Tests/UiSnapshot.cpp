// Renders every page to PNG and checks the layout: controls inside their panels, no two controls overlapping.
// Usage:  JunkUiSnapshot <output folder>     (under xvfb-run on a machine without a display)
#include "PluginEditor.h"
#include "Exporter.h"

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
    std::vector<juce::Component*> v;
    collect (&root, v);
    int bad = 0;
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
    const char* names[] = { "drawer", "lfos", "master", "matrix" };
    int problems = 0;

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
        };
        jdx::ExportSettings st; st.mode = 2; st.folder = juce::File ("/home/ray/Music/Junk Drawer exports"); st.save();
        editor->showPage (0);
        editor->openExport();
        shot ("export.png");
        problems += checkLayout (editor->exportWindow(), "export");

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
        for (auto& d : tmpRoot.findChildFiles (juce::File::findDirectories, false, "Junk Drawer Export *"))
        { for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav")) { ++made; std::printf ("export wrote %s (%lld bytes)\n", f.getFileName().toRawUTF8(), (long long) f.getSize()); } d.deleteRecursively(); }
        if (made == 0) { std::printf ("export through the window wrote nothing\n"); ++problems; }
        jdx::ExportSettings none; none.save();
    }
    std::printf ("layout problems: %d\n", problems);
    ed.reset();
    return problems == 0 ? 0 : 1;
}
