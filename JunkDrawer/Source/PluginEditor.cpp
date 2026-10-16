#include "PluginEditor.h"
#include <BinaryData.h>

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

void drawHookedRow (juce::Graphics& g, int x, int y)
{
    drawText (g, "HOOKED UP", { x, y, 80, 24 }, sans (11.f, true).withExtraKerningFactor (0.12f), pal::textDim);
    drawText (g, hookedNone(), { x + 88, y, 220, 24 }, mono (11.f), pal::textDim);
}

void drawStripLabel (juce::Graphics& g, const juce::String& t, int x, int y)
{
    drawText (g, t, { x, y, 220, 14 }, sans (11.f, true).withExtraKerningFactor (0.16f), pal::amber);
}
} // namespace

//==============================================================================
// Header: logo, name, page tabs, preset bar, host tempo, Reduce motion
struct JunkDrawerEditor::Header : juce::Component, juce::Timer
{
    Header (JunkDrawerEditor& e) : ed (e)
    {
        const char* names[] = { "Drawer", "LFOs", "Master", "Matrix" };
        const char* tips[]  = { "The main drawer", "Rubber bands", "Master and effects", "Everything that is hooked up" };
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
        reduce.setTooltip ("Turns off the ambient animation");
        reduce.onClick = [this] { ed.proc.state.state.setProperty ("reduceMotion", reduce.getToggleState(), nullptr); };
        addAndMakeVisible (reduce);
        startTimerHz (4);
    }

    void select (int i) { tabs[(size_t) i].setToggleState (true, juce::dontSendNotification); }

    void resized() override
    {
        int x = 312;
        for (auto& b : tabs) { b.setBounds (x, 12, 70, 28); x += 76; }
        reduce.setBounds (getWidth() - 24 - 110, 12, 110, 28);
    }

    void timerCallback() override
    {
        double tempo = 0.0;
        if (auto* ph = ed.proc.getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto t = pos->getBpm()) tempo = *t;
        if (std::abs (tempo - bpm) > 0.001) { bpm = tempo; repaint (bpmArea()); }
    }

    juce::Rectangle<int> bpmArea() const { return { getWidth() - 24 - 110 - 14 - 110, 12, 110, 28 }; }

    void paint (juce::Graphics& g) override
    {
        drawLogo (g, { 24, 11, 58, 29 });
        drawText (g, "FADING FRACTAL AUDIO", { 94, 6, 220, 14 }, sans (11.f, true).withExtraKerningFactor (0.12f), juce::Colour (0xffd7c196));
        drawText (g, "Junk Drawer", { 94, 18, 220, 30 }, serif (25.f), juce::Colour (0xfff6e8c9));

        const juce::Rectangle<float> bar (660.f, 11.f, 320.f, 30.f);
        g.setColour (juce::Colour (0x52000000));
        g.fillRoundedRectangle (bar, 15.f);
        g.setColour (juce::Colour (0x80d6ac5c));
        g.drawRoundedRectangle (bar.reduced (0.5f), 15.f, 1.f);
        drawText (g, juce::String::charToString (0x2039), bar.toNearestInt().removeFromLeft (30), sans (16.f), pal::amber.withAlpha (0.4f), juce::Justification::centred);
        drawText (g, juce::String::charToString (0x203a), bar.toNearestInt().removeFromRight (30), sans (16.f), pal::amber.withAlpha (0.4f), juce::Justification::centred);
        drawText (g, juce::String ("Drawer 001 ") + juce::String::charToString (0x00b7) + " Empty drawer", bar.toNearestInt().reduced (32, 0), serif (15.f, false), pal::text, juce::Justification::centred);

        drawText (g, bpm > 0.0 ? juce::String (bpm, 1) + " BPM" : juce::String ("No host tempo"), bpmArea(), mono (12.f), pal::amber, juce::Justification::centredRight);
    }

    JunkDrawerEditor& ed;
    std::array<juce::TextButton, NumPages> tabs;
    juce::TextButton reduce;
    double bpm = 0.0;
};

//==============================================================================
// Drawer page
struct JunkDrawerEditor::DrawerPage : juce::Component
{
    struct LfoTile : juce::Component, juce::SettableTooltipClient
    {
        LfoTile (APVTS& s, int i) : index (i), state (s), wShape (s, jd::lfoId (i, "shape"), *this), wRate (s, jd::lfoId (i, "rate"), *this),
                                    wDiv (s, jd::lfoId (i, "div"), *this), wSync (s, jd::lfoId (i, "sync"), *this)
        { setTooltip ("Rubber band " + juce::String (i + 1)); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, 4.f);
            g.setColour (pal::line.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);

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
        APVTS& state;
        Watch wShape, wRate, wDiv, wSync;
    };

    struct TrackRow : juce::Component
    {
        TrackRow (APVTS& s, int t) : track (t)
        {
            m.setButtonText ("M"); s_.setButtonText ("S");
            m.setClickingTogglesState (true); s_.setClickingTogglesState (true);
            m.setTooltip ("Mute track " + juce::String (t + 1));
            s_.setTooltip ("Solo track " + juce::String (t + 1));
            addAndMakeVisible (m); addAndMakeVisible (s_);
            am = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter (jd::trackId (t, "mute")), m, nullptr);
            as = std::make_unique<juce::ButtonParameterAttachment> (*s.getParameter (jd::trackId (t, "solo")), s_, nullptr);
        }
        void resized() override { m.setBounds (204, 10, 22, 26); s_.setBounds (230, 10, 22, 26); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xc7120905));
            g.fillRoundedRectangle (r, 3.f);
            g.setColour (juce::Colour (0x29ffebbe));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.f, 1.f);

            const juce::Rectangle<float> badge (10.f, 10.f, 28.f, 28.f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3dc98), 14.f, 14.f, juce::Colour (0xff6d4e1d), 34.f, 36.f, true));
            g.fillEllipse (badge);
            drawText (g, juce::String (track + 1).paddedLeft ('0', 2), badge.toNearestInt(), mono (12.f, true), juce::Colour (0xff2a1b10), juce::Justification::centred);

            const juce::Rectangle<float> card (46.f, 10.f, 150.f, 26.f);
            g.setColour (juce::Colour (0xfff6eed6));
            g.fillRect (card);
            g.setColour (pal::brass);
            g.drawRect (card.expanded (1.5f), 1.5f);
            drawText (g, "Track " + juce::String (track + 1), card.toNearestInt().reduced (8, 0), mono (13.f), juce::Colour (0xff7a6140));

            drawText (g, "Nothing in here yet.", { 46, 46, 200, 20 }, sans (12.f), juce::Colour (0xffd9c39a));
            g.setColour (juce::Colour (0x80000000));
            g.fillRoundedRectangle (252.f, 12.f, 8.f, 56.f, 3.f);
        }
        int track;
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
        for (int i = 0; i < jd::kLfos; ++i) { tiles[(size_t) i] = std::make_unique<LfoTile> (st, i); addAndMakeVisible (*tiles[(size_t) i]); }
        for (int t = 0; t < jd::kTracks; ++t) { rows[(size_t) t] = std::make_unique<TrackRow> (st, t); addAndMakeVisible (*rows[(size_t) t]); }
    }

    void resized() override
    {
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

        // the open track's paper liner (loading and regions arrive in Stages 1 and 2)
        const juce::Rectangle<float> liner (328.f, 122.f, 908.f, 426.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd3c8a7), liner.getX(), liner.getY(), juce::Colour (0xffc2b58f), liner.getX(), liner.getBottom(), false));
        g.fillRect (liner);
        g.setColour (juce::Colour (0x99281608));
        g.drawRect (liner, 1.f);
        const auto ink = juce::Colour (0xff2a1b10);
        drawText (g, "Nothing in here yet.", liner.toNearestInt().withTrimmedBottom (40), serif (26.f, false), ink, juce::Justification::centred);
        drawText (g, "Drop something in.", liner.toNearestInt().withTrimmedTop (200).withHeight (30), sans (14.f), ink.withAlpha (0.8f), juce::Justification::centred);
        drawText (g, "Loading samples and cutting regions are the next stages of the build.", liner.toNearestInt().withTrimmedTop (390), sans (12.f), ink.withAlpha (0.65f), juce::Justification::centred);

        // junk compartment with the brass nameplate
        g.setColour (juce::Colour (0xb8120903));
        g.fillRect (328, 560, 908, 250);
        const juce::Rectangle<float> plate (654.f, 610.f, 474.f, 150.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6cd84), plate.getX(), plate.getY(), juce::Colour (0xff8a6728), plate.getRight(), plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 3.f);
        g.setColour (juce::Colour (0xff4a3413));
        g.drawRoundedRectangle (plate.reduced (0.5f), 3.f, 1.f);
        g.setColour (juce::Colour (0x59ffe9a8));
        g.drawRoundedRectangle (plate.reduced (3.f), 2.f, 1.f);
        const auto ink2 = juce::Colour (0xff3a2606);
        g.setOpacity (0.85f);
        drawLogo (g, { 690, 640, 130, 65 });
        drawText (g, "FADING FRACTAL AUDIO", { 846, 650, 270, 16 }, sans (12.f, true).withExtraKerningFactor (0.14f), ink2);
        drawText (g, "Junk Drawer", { 846, 668, 270, 40 }, serif (30.f), ink2);
        drawText (g, "EIGHT-TRACK SAMPLE SEQUENCER", { 846, 712, 270, 16 }, sans (11.f, true).withExtraKerningFactor (0.1f), ink2);
    }

    JunkDrawerEditor& ed;
    std::unique_ptr<Knob> out;
    std::array<juce::TextButton, 4> pills;
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, 4> pillAtt;
    std::array<std::unique_ptr<LfoTile>, jd::kLfos> tiles;
    std::array<std::unique_ptr<TrackRow>, jd::kTracks> rows;
};

//==============================================================================
// LFO page
struct JunkDrawerEditor::LfoPage : juce::Component
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
            drawHookedRow (g, 14, 296);
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
            b.setRadioGroupId (202);
            addAndMakeVisible (b);
            panels[(size_t) i] = std::make_unique<LfoPanel> (ed.proc.state, i);
            addAndMakeVisible (*panels[(size_t) i]);
        }
        picks[2].setToggleState (true, juce::dontSendNotification);
        matrix.setButtonText ("Mod matrix");
        matrix.onClick = [this] { ed.showPage (Matrix); };
        clear.setButtonText ("Clear all");
        clear.setTooltip ("Take every rubber band off");
        clear.setEnabled (false);
        addAndMakeVisible (matrix); addAndMakeVisible (clear);
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
        g.drawFittedText ("Pick an LFO, click any control on the Drawer page, then drag up or down to set the amount.", { 410, 28, 440, 52 }, juce::Justification::centredLeft, 3, 1.f);
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
            drawHookedRow (g, 14, 296);
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
            drawHookedRow (g, 14, 640);
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
// Matrix page (empty until the LFO engine and assignments exist)
struct JunkDrawerEditor::MatrixPage : juce::Component
{
    explicit MatrixPage (JunkDrawerEditor&) {}
    void paint (juce::Graphics& g) override
    {
        paintDrawerFrame (g, false);
        const juce::Rectangle<float> strip (44.f, 12.f, 1192.f, 80.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (strip, 4.f);
        g.setColour (juce::Colour (0x2effebbe));
        g.drawRoundedRectangle (strip.reduced (0.5f), 4.f, 1.f);
        drawStripLabel (g, "EVERYTHING THAT IS HOOKED UP", 66, 24);
        drawText (g, "Mod matrix", { 66, 40, 320, 32 }, serif (22.f), juce::Colour (0xfff6e8c9));

        const juce::Rectangle<float> pn (44.f, 122.f, 1192.f, 688.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (pn, 4.f);
        g.setColour (juce::Colour (0x29ffebbe));
        g.drawRoundedRectangle (pn.reduced (0.5f), 4.f, 1.f);
        const int cols[] = { 70, 520, 700, 900 };
        const char* heads[] = { "TARGET", "LFO", "AMOUNT", "POLARITY" };
        for (int i = 0; i < 4; ++i) drawText (g, heads[i], { cols[i], 140, 200, 20 }, sans (11.f, true).withExtraKerningFactor (0.12f), pal::amber);
        g.setColour (pal::line); g.fillRect (62, 168, 1156, 1);
        drawText (g, hookedNone(), { 44, 330, 1192, 40 }, serif (22.f, false), pal::textSoft, juce::Justification::centred);
        drawText (g, "Pick an LFO on the LFOs page, click a control, then drag up or down.", { 44, 372, 1192, 24 }, sans (13.f), pal::textDim, juce::Justification::centred);
    }
};

//==============================================================================
JunkDrawerEditor::JunkDrawerEditor (JunkDrawerProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (kW, kH);
    setResizable (false, false);

    header = std::make_unique<Header> (*this);
    drawerPage = std::make_unique<DrawerPage> (*this);
    lfoPage = std::make_unique<LfoPage> (*this);
    masterPage = std::make_unique<MasterPage> (*this);
    matrixPage = std::make_unique<MatrixPage> (*this);
    addAndMakeVisible (*header);
    for (juce::Component* c : { (juce::Component*) drawerPage.get(), (juce::Component*) lfoPage.get(), (juce::Component*) masterPage.get(), (juce::Component*) matrixPage.get() })
        addChildComponent (c);
    showPage (Drawer);
    resized();
}

JunkDrawerEditor::~JunkDrawerEditor() { setLookAndFeel (nullptr); }

void JunkDrawerEditor::showPage (int p)
{
    page = juce::jlimit (0, (int) NumPages - 1, p);
    juce::Component* pages[] = { drawerPage.get(), lfoPage.get(), masterPage.get(), matrixPage.get() };
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
    if (header == nullptr || matrixPage == nullptr) return;                 // setSize() runs before the pages exist
    header->setBounds (0, 0, kW, 52);
    for (juce::Component* c : { (juce::Component*) drawerPage.get(), (juce::Component*) lfoPage.get(), (juce::Component*) masterPage.get(), (juce::Component*) matrixPage.get() })
        c->setBounds (0, 52, kW, kH - 52);
}
