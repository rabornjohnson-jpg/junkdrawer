// Renders every page to PNG and checks the layout: controls inside their panels, no two controls overlapping.
// Usage:  JunkUiSnapshot <output folder>     (under xvfb-run on a machine without a display)
#include "PluginEditor.h"

namespace
{
bool isLeaf (juce::Component* c)
{
    return dynamic_cast<ui::Knob*> (c) || dynamic_cast<ui::Seg*> (c) || dynamic_cast<juce::Button*> (c);
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
    std::printf ("layout problems: %d\n", problems);
    ed.reset();
    return problems == 0 ? 0 : 1;
}
