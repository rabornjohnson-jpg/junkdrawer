#pragma once
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Shared look and small widgets.  Rules: no text below 11 px, nothing drawn outside its box, every control is a real parameter.
namespace ui
{
// High contrast: one switch, read at paint time.  The editor rebuilds its controls when it changes.
inline bool& highContrast() { static bool on = false; return on; }

namespace pal
{
// A palette colour that has a standard value and a high contrast value.
struct TC
{
    juce::uint32 standard, contrast;
    constexpr TC (juce::uint32 s, juce::uint32 c) : standard (s), contrast (c) {}
    juce::Colour get() const { return juce::Colour (highContrast() ? contrast : standard); }
    operator juce::Colour() const { return get(); }
    juce::Colour withAlpha (float a) const { return get().withAlpha (a); }
    juce::Colour withMultipliedAlpha (float a) const { return get().withMultipliedAlpha (a); }
    juce::Colour brighter (float a = 0.4f) const { return get().brighter (a); }
    juce::Colour darker (float a = 0.4f) const { return get().darker (a); }
};
const juce::Colour counterTop   { 0xff313836 }, counterBottom { 0xff232827 };
const juce::Colour wood         { 0xff6a4a2d }, woodDark { 0xff4b3320 }, woodLight { 0xff8a6a45 };
const TC panel        { 0xcc120a05, 0xff000000 };                       // dark panel
const TC text         { 0xfff3e7c9, 0xffffffff }, textSoft { 0xffe0cd9f, 0xfff4f4f4 }, textDim { 0xffcdb98a, 0xffdcdcdc };
const TC amber        { 0xffe9b872, 0xffffd60a }, amberInk { 0xff1c1108, 0xff000000 };
const TC line         { 0x4df0e2c4, 0xb3ffffff };                       // hairline
const TC brass        { 0xffb8893a, 0xffffffff };
const TC mint         { 0xffa7cbbc, 0xff0a0a0a }, mintDark { 0xff6e9686, 0xff2a2a2a }, mintInk { 0xff17261f, 0xffffffff };   // the painted case
const TC brassHi      { 0xffc9a24e, 0xffffffff }, brassDeep { 0xff5a431c, 0xffc8c8c8 };
const TC bakelite     { 0xff5b2a22, 0xff3a3a3a }, bakeliteDark { 0xff321612, 0xff000000 };
}

// Keyboard focus ring: a dark ring under a bright one, so it shows on any background.  Thicker in high contrast.
inline void paintFocus (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool round = false)
{
    const float w = highContrast() ? 3.f : 2.f;
    auto draw = [&] (juce::Rectangle<float> rr, float th, juce::Colour c)
    {
        g.setColour (c);
        if (round) g.drawEllipse (rr, th); else g.drawRoundedRectangle (rr, radius, th);
    };
    draw (r.reduced (w * 0.5f), w + 2.f, juce::Colours::black);
    draw (r.reduced (w * 0.5f), w, highContrast() ? juce::Colour (0xffffd60a) : juce::Colour (0xfff3e1b0));
}

// A text value for screen readers (current station, selected segment, and so on).
struct TextValue : juce::AccessibilityTextValueInterface
{
    std::function<juce::String()> get;
    std::function<void (const juce::String&)> set;
    bool isReadOnly() const override { return ! set; }
    juce::String getCurrentValueAsString() const override { return get != nullptr ? get() : juce::String(); }
    void setValueAsString (const juce::String& s) override { if (set != nullptr) set (s); }
};

// A transparent stand-in for a custom-drawn control: it takes keyboard focus, shows the focus ring, handles keys,
// and tells screen readers what the control is, what it holds and what pressing it does.  Mouse clicks pass through.
struct FocusProxy : juce::Component
{
    FocusProxy (juce::String name, juce::String help)
    {
        setTitle (name); setDescription (help);
        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (false, false);
    }
    juce::AccessibilityRole role = juce::AccessibilityRole::button;
    std::function<juce::String()> valueText;                         // what a screen reader reads as the value
    std::function<bool (const juce::KeyPress&)> onKey;               // arrow keys and so on; return true when used
    std::function<void()> onPress;                                   // Enter or Space
    float radius = 5.f;

    void paint (juce::Graphics& g) override { if (hasKeyboardFocus (false)) paintFocus (g, getLocalBounds().toFloat().reduced (1.f), radius); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (onKey != nullptr && onKey (k)) return true;
        if (onPress != nullptr && (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)) { onPress(); return true; }
        return false;
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        juce::AccessibilityActions actions;
        if (onPress != nullptr) actions.addAction (juce::AccessibilityActionType::press, [this] { onPress(); });
        juce::AccessibilityHandler::Interfaces ifs;
        if (valueText != nullptr)
        {
            auto v = std::make_unique<TextValue>();
            v->get = valueText;
            ifs.value = std::move (v);
        }
        return std::make_unique<juce::AccessibilityHandler> (*this, role, std::move (actions), std::move (ifs));
    }
};

// Embedded typefaces: IBM Plex Sans, IBM Plex Mono, Fraunces and Special Elite (all SIL Open Font License).
inline juce::Typeface::Ptr face (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}
inline juce::Font sans (float h, bool bold = false)
{
    static auto reg = face (BinaryData::PlexSansRegular_ttf, BinaryData::PlexSansRegular_ttfSize);
    static auto semi = face (BinaryData::PlexSansSemiBold_ttf, BinaryData::PlexSansSemiBold_ttfSize);
    return juce::Font (juce::FontOptions (bold ? semi : reg).withHeight (h));
}
inline juce::Font mono (float h, bool bold = false)
{
    static auto reg = face (BinaryData::PlexMonoRegular_ttf, BinaryData::PlexMonoRegular_ttfSize);
    static auto med = face (BinaryData::PlexMonoMedium_ttf, BinaryData::PlexMonoMedium_ttfSize);
    return juce::Font (juce::FontOptions (bold ? med : reg).withHeight (h));
}
inline juce::Font serif (float h, bool = true)
{
    static auto f = face (BinaryData::FrauncesMedium_ttf, BinaryData::FrauncesMedium_ttfSize);
    return juce::Font (juce::FontOptions (f).withHeight (h));
}
inline juce::Font typewriter (float h)
{
    static auto f = face (BinaryData::SpecialEliteRegular_ttf, BinaryData::SpecialEliteRegular_ttfSize);
    return juce::Font (juce::FontOptions (f).withHeight (h));
}

// Contrast audit hook, used only by the screenshot tool: records every label drawn, where and in what colour.
struct TextAudit { juce::Rectangle<float> r; juce::Colour c; juce::String t; float h; };
inline std::vector<TextAudit>*& auditSink() { static std::vector<TextAudit>* p = nullptr; return p; }
inline bool& auditHideText() { static bool b = false; return b; }
inline juce::Point<float>& auditOrigin() { static juce::Point<float> p; return p; }
inline void auditNote (juce::Graphics&, const juce::String& t, juce::Rectangle<int> r, juce::Colour c, float h)
{
    if (auto* s = auditSink()) s->push_back ({ r.toFloat().translated (auditOrigin().x, auditOrigin().y), c, t, h });
}

inline void drawText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Font f, juce::Colour c,
                      juce::Justification j = juce::Justification::centredLeft)
{
    auditNote (g, t, r, c, f.getHeight());
    if (auditHideText()) return;
    g.setFont (f);
    g.setColour (c);
    g.drawText (t, r, j, true);
}

//==============================================================================
// Turned-wood knob with a dark inlaid tick.
struct LookAndFeel : juce::LookAndFeel_V4
{
    LookAndFeel()
    {
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1c1108));
        setColour (juce::TooltipWindow::textColourId, pal::text);
        setColour (juce::TooltipWindow::outlineColourId, pal::brass);
    }

    // Fluted bakelite skirt with a ringed brass cap and a cream pointer.  The disc is drawn once per size and reused.
    static juce::Image knobDisc (int d)
    {
        static std::map<int, juce::Image> cache;
        const int key = d * 2 + (highContrast() ? 1 : 0);
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
        const int px = d * 2;
        const bool hc0 = highContrast();
        juce::Image img (juce::Image::ARGB, px, px, true);
        juce::Graphics g (img);
        const auto b = juce::Rectangle<float> ((float) px, (float) px).reduced (2.f);
        const auto c = b.getCentre();
        g.setColour (pal::bakeliteDark);
        g.fillEllipse (b);
        g.setColour (pal::bakelite);
        for (int k = 0; k < 24; ++k)
        {
            juce::Path w;
            w.addPieSegment (b, juce::degreesToRadians ((float) k * 15.f), juce::degreesToRadians ((float) k * 15.f + 7.5f), 0.f);
            g.fillPath (w);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x00000000), c.x, c.y, juce::Colour (0x80000000), c.x + b.getWidth() * 0.5f, c.y, true));
        g.fillEllipse (b);
        g.setColour (hc0 ? juce::Colours::white : juce::Colour (0xd90a0402)); g.drawEllipse (b, hc0 ? 4.f : 3.f);
        g.setColour (juce::Colour (0x40ffc8aa)); g.drawEllipse (b.reduced (3.f), 2.f);
        const float cr = b.getWidth() * 0.285f;
        const bool hc = highContrast();
        juce::ColourGradient cap (hc ? juce::Colours::white : juce::Colour (0xffecd584), c.x - cr * 0.4f, c.y - cr * 0.5f, hc ? juce::Colour (0xffc8c8c8) : juce::Colour (0xff8a6a2a), c.x + cr * 0.8f, c.y + cr, true);
        g.setGradientFill (cap);
        g.fillEllipse (juce::Rectangle<float> (cr * 2.f, cr * 2.f).withCentre (c));
        g.setColour (juce::Colour (0x998a6a2a));
        for (float k : { 0.30f, 0.52f, 0.76f }) g.drawEllipse (juce::Rectangle<float> (cr * 2.f * k, cr * 2.f * k).withCentre (c), 1.6f);
        g.setColour (juce::Colour (0xff5a431c));
        g.drawEllipse (juce::Rectangle<float> (cr * 2.f, cr * 2.f).withCentre (c), 2.f);
        cache[key] = img;
        return img;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const int di = juce::jmax (8, juce::jmin (w, h) - 4);
        const float d = (float) di;
        const auto b = juce::Rectangle<float> (d, d).withCentre ({ x + w * 0.5f, y + h * 0.5f });
        const auto c = b.getCentre();

        g.setColour (juce::Colours::black.withAlpha (0.45f));                                   // drop shadow
        g.fillEllipse (b.translated (0.f, 2.5f).expanded (0.5f));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (s.isEnabled() ? 1.f : 0.5f);
        g.drawImage (knobDisc (di), b, juce::RectanglePlacement::stretchToFit);

        const float ang = a0 + pos * (a1 - a0);
        juce::Path tick;
        const float tw = highContrast() ? 3.6f : 2.2f;
        tick.addRoundedRectangle (-tw * 0.5f, -d * 0.5f + 2.f, tw, juce::jmax (4.f, d * 0.17f), tw * 0.5f);
        g.setColour (highContrast() ? (s.isEnabled() ? pal::amber.get() : pal::amber.withAlpha (0.4f)) : (s.isEnabled() ? juce::Colour (0xfff3e1b0) : juce::Colour (0x66f3e1b0)));
        g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c));

        if (s.hasKeyboardFocus (false)) paintFocus (g, b.expanded (4.f), 0.f, true);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return sans (12.f, true); }

    static bool onMint (const juce::Component& c) { return (bool) c.getProperties().getWithDefault ("mint", false); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down) override
    {
        paintButtonBody (g, b, c, over, down);
        if (b.hasKeyboardFocus (false)) paintFocus (g, b.getLocalBounds().toFloat().reduced (1.f), 5.f);
    }

    void paintButtonBody (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("switch"))
        {
            const auto t = juce::Rectangle<float> ((float) b.getWidth() - 36.f, (float) b.getHeight() * 0.5f - 9.f, 34.f, 18.f);
            g.setColour (on ? juce::Colour (0xffd9a64a) : pal::mintInk.withAlpha (0.35f)); g.fillRoundedRectangle (t, 9.f);
            g.setColour (pal::mintInk.withAlpha (0.6f)); g.drawRoundedRectangle (t.reduced (0.5f), 9.f, 1.f);
            g.setColour (juce::Colour (0xfffdf3d6)); g.fillEllipse (t.getX() + (on ? 18.f : 2.f), t.getY() + 2.f, 14.f, 14.f);
            return;
        }
        if (onMint (b))
        {
            g.setColour (on ? (highContrast() ? pal::amber.get() : juce::Colour (0xffead79e)) : juce::Colours::white.withAlpha (over || down ? 0.3f : 0.0f));
            g.fillRoundedRectangle (r, 14.f);
            g.setColour (on ? (highContrast() ? juce::Colours::white : juce::Colour (0xff8a6a2a)) : (highContrast() ? pal::mintInk.get() : pal::mintInk.withAlpha (b.isEnabled() ? 0.5f : 0.25f)));
            g.drawRoundedRectangle (r, 14.f, highContrast() ? 2.f : (on ? 1.5f : 1.f));
            return;
        }
        g.setColour (on ? pal::amber : juce::Colours::white.withAlpha (over || down ? 0.12f : 0.0f));
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (on ? pal::amber : pal::line.withAlpha (b.isEnabled() ? 0.8f : 0.35f));
        g.drawRoundedRectangle (r, 4.f, highContrast() ? 2.f : 1.f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        g.setFont (getTextButtonFont (b, b.getHeight()));
        if (b.getProperties().contains ("switch"))
        {
            g.setColour (pal::mintInk);
            auditNote (g, b.getButtonText(), b.getLocalBounds().withTrimmedRight (40), pal::mintInk, 12.f);
            if (! auditHideText()) g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedRight (40), juce::Justification::centredRight, true);
            return;
        }
        juce::Colour tc;
        if (onMint (b)) tc = on ? (highContrast() ? juce::Colours::black : juce::Colour (0xff2f1b06)) : pal::mintInk.get();
        else tc = on ? pal::amberInk.get() : (b.isEnabled() ? pal::text.get() : pal::textDim.withAlpha (0.6f));
        g.setColour (tc);
        auditNote (g, b.getButtonText(), b.getLocalBounds().reduced (4, 0), tc, 12.f);
        if (! auditHideText()) g.drawText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
    }
};

//==============================================================================
// LFO assignment, as the controls see it.  The editor fills this in; a knob asks it what is armed and what is linked.
struct ModContext;
inline ModContext*& currentMod() { static ModContext* p = nullptr; return p; }
struct LinkInfo { juce::String target, name, where; int lfo = 0; float amount = 0.f; };
struct ModContext
{
    juce::ChangeBroadcaster changed;
    std::function<std::vector<LinkInfo>()> links;                            // every link, with names ready to show
    std::function<int()> armed;                                               // the LFO a drag assigns, or -1
    std::function<float (const juce::String&, int)> amount;                  // 0 when not linked
    std::function<void (const juce::String&, int, float)> setAmount;
    std::function<void (const juce::String&, int)> remove;
    std::function<std::vector<int> (const juce::String&)> lfosOn;            // LFOs linked to a control
};

//==============================================================================
// A parameter knob: wood dial, name, and the value as the parameter prints it.
// With an LFO armed, a drag on the knob sets how much that LFO moves it instead of changing the knob.
class Knob : public juce::Component, private juce::ChangeListener
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label, int dialSize = 38,
          juce::Colour nameColour = pal::text, juce::Colour valueColour = pal::amber, juce::String note = {}, ModContext* modulation = nullptr)
        : param (*s.getParameter (id)), paramId (id), size (dialSize), name (label), noteText (std::move (note)), nameCol (nameColour), valCol (valueColour), ctx (modulation != nullptr ? modulation : currentMod())
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (-135.f), juce::degreesToRadians (135.f), true);
        slider.setTitle (param.getName (64));
        slider.setTooltip (param.getName (64));
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
        attach = std::make_unique<juce::SliderParameterAttachment> (param, slider, nullptr);
        if (ctx != nullptr) { ctx->changed.addChangeListener (this); refreshMode(); }
    }

    ~Knob() override { if (ctx != nullptr) ctx->changed.removeChangeListener (this); }

    void resized() override { slider.setBounds (getLocalBounds().withSizeKeepingCentre (size + 4, size + 4).withY (0)); }

    void paint (juce::Graphics& g) override
    {
        const int armedLfo = ctx != nullptr ? ctx->armed() : -1;
        const auto linked = ctx != nullptr ? ctx->lfosOn (paramId) : std::vector<int>();
        const auto dial = slider.getBounds().toFloat();

        if (! linked.empty())                                                   // the range the LFO sweeps, drawn around the dial
        {
            const int shown = armedLfo >= 0 && std::find (linked.begin(), linked.end(), armedLfo) != linked.end() ? armedLfo : linked.front();
            const float amt = ctx->amount (paramId, shown), v = param.getValue();
            const float a0 = juce::degreesToRadians (-135.f + 270.f * v), a1 = juce::degreesToRadians (-135.f + 270.f * juce::jlimit (0.f, 1.f, v + amt));
            juce::Path arc;
            arc.addCentredArc (dial.getCentreX(), dial.getCentreY(), (float) size * 0.5f + 1.f, (float) size * 0.5f + 1.f, 0.f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
            g.setColour (pal::amber);
            g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const int n = (int) linked.size();
            for (int k = 0; k < juce::jmin (n, 2); ++k)
            {
                const auto b = juce::Rectangle<float> (dial.getRight() - 8.f - (float) k * 14.f, dial.getY() - 4.f, 18.f, 18.f);
                g.setColour (juce::Colour (0xff2a1b10)); g.fillEllipse (b);
                g.setColour (pal::amber); g.drawEllipse (b.reduced (0.5f), 1.f);
                drawText (g, "L" + juce::String (linked[(size_t) k] + 1), b.toNearestInt(), mono (11.f, true), juce::Colour (0xfff6e8c9), juce::Justification::centred);
            }
        }

        if (! showText) return;
        auto r = getLocalBounds().withTrimmedTop (size + 6);
        drawText (g, name, r.removeFromTop (14), sans (12.f, true), nameCol, juce::Justification::centred);
        juce::String valueText = param.getCurrentValueAsText();
        if (armedLfo >= 0 && ! linked.empty() && std::find (linked.begin(), linked.end(), armedLfo) != linked.end())
        {
            const int pct = juce::roundToInt (ctx->amount (paramId, armedLfo) * 100.f);
            valueText = "L" + juce::String (armedLfo + 1) + " " + (pct > 0 ? "+" : "") + juce::String (pct) + "%";
        }
        drawText (g, valueText, r.removeFromTop (14), mono (11.f), valCol, juce::Justification::centred);
        if (noteText.isNotEmpty()) drawText (g, noteText, r.removeFromTop (14), sans (11.f), pal::textDim, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! assigning()) return;
        startAmount = ctx->amount (paramId, ctx->armed());
        startY = e.y;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! assigning()) return;
        const float a = juce::jlimit (-1.f, 1.f, startAmount + (float) (startY - e.y) / 160.f);
        if (std::abs (a) < 0.01f) ctx->remove (paramId, ctx->armed()); else ctx->setAmount (paramId, ctx->armed(), a);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (assigning()) ctx->remove (paramId, ctx->armed()); }

    juce::Slider slider;
    bool showText = true;                                   // false: only the dial (the caller writes its own label)

private:
    bool assigning() const { return ctx != nullptr && ctx->armed() >= 0; }
    void refreshMode()
    {
        const bool on = assigning();
        slider.setInterceptsMouseClicks (! on, ! on);
        setMouseCursor (on ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshMode(); }

    juce::RangedAudioParameter& param;
    juce::String paramId;
    int size;
    juce::String name, noteText;
    juce::Colour nameCol, valCol;
    ModContext* ctx = nullptr;
    float startAmount = 0.f;
    int startY = 0;
    std::unique_ptr<juce::SliderParameterAttachment> attach;
};

//==============================================================================
// A row of choices bound to a choice parameter, or to a bool when the two labels read like { "On", "Bypass" }.
class Seg : public juce::Component, public juce::SettableTooltipClient
{
public:
    Seg (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::StringArray items, bool boolOnFirst = false)
        : labels (std::move (items)), invert (boolOnFirst), param (*s.getParameter (id)),
          attach (param, [this] (float v) { idx = invert ? (v > 0.5f ? 0 : 1) : juce::roundToInt (v); repaint(); }, nullptr)
    {
        setTitle (param.getName (64));
        setWantsKeyboardFocus (true);
        attach.sendInitialUpdate();
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void choose (int i)
    {
        i = juce::jlimit (0, labels.size() - 1, i);
        attach.setValueAsCompleteGesture (invert ? (i == 0 ? 1.f : 0.f) : (float) i);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::upKey)    { choose (idx - 1); return true; }
        if (k == juce::KeyPress::rightKey || k == juce::KeyPress::downKey) { choose (idx + 1); return true; }
        return false;
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        juce::AccessibilityHandler::Interfaces ifs;
        auto v = std::make_unique<TextValue>();
        v->get = [this] { return labels[juce::jlimit (0, labels.size() - 1, idx)]; };
        v->set = [this] (const juce::String& s) { const int i = labels.indexOf (s); if (i >= 0) choose (i); };
        ifs.value = std::move (v);
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::comboBox, juce::AccessibilityActions(), std::move (ifs));
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (pal::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
        const int n = labels.size();
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<int> (juce::roundToInt (r.getWidth() * i / n), 0, juce::roundToInt (r.getWidth() * (i + 1) / n) - juce::roundToInt (r.getWidth() * i / n), getHeight());
            const bool on = i == idx;
            if (on) { g.setColour (pal::amber); g.fillRoundedRectangle (cell.toFloat().reduced (1.f), 3.f); }
            drawText (g, labels[i], cell.reduced (2, 0), sans (11.f, true), on ? pal::amberInk : pal::textSoft, juce::Justification::centred);
        }
        if (hasKeyboardFocus (false)) paintFocus (g, r.reduced (1.f), 4.f);
    }

    // width that fits every label with room to breathe
    int idealWidth() const
    {
        int w = 0;
        for (auto& l : labels) w += (int) std::ceil (sans (11.f, true).getStringWidthFloat (l)) + 24;
        return w;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, labels.size() - 1, e.x * labels.size() / juce::jmax (1, getWidth()));
        attach.setValueAsCompleteGesture (invert ? (i == 0 ? 1.f : 0.f) : (float) i);
    }

private:
    juce::StringArray labels;
    bool invert;
    int idx = 0;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attach;
};

//==============================================================================
// A row of choices that is not a parameter (settings of a dialog or a take).  Same look as Seg.
class PickSeg : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PickSeg (juce::StringArray items, int initial = 0) : labels (std::move (items)), idx (initial) { setWantsKeyboardFocus (true); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! isEnabled()) return false;
        int d = 0;
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::upKey) d = -1;
        else if (k == juce::KeyPress::rightKey || k == juce::KeyPress::downKey) d = 1;
        if (d == 0) return false;
        setSelected (idx + d);
        if (onChange) onChange();
        return true;
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        juce::AccessibilityHandler::Interfaces ifs;
        auto v = std::make_unique<TextValue>();
        v->get = [this] { return labels[juce::jlimit (0, labels.size() - 1, idx)]; };
        v->set = [this] (const juce::String& s) { const int i = labels.indexOf (s); if (i >= 0) { setSelected (i); if (onChange) onChange(); } };
        ifs.value = std::move (v);
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::comboBox, juce::AccessibilityActions(), std::move (ifs));
    }
    int selected() const noexcept { return idx; }
    void setSelected (int i) { idx = juce::jlimit (0, labels.size() - 1, i); repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float a = isEnabled() ? 1.f : 0.45f;
        g.setColour (pal::line.withMultipliedAlpha (a));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
        const int n = labels.size();
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<int> (juce::roundToInt (r.getWidth() * i / n), 0, juce::roundToInt (r.getWidth() * (i + 1) / n) - juce::roundToInt (r.getWidth() * i / n), getHeight());
            const bool on = i == idx;
            if (on) { g.setColour (pal::amber.withMultipliedAlpha (a)); g.fillRoundedRectangle (cell.toFloat().reduced (1.f), 3.f); }
            drawText (g, labels[i], cell.reduced (2, 0), sans (11.f, true), (on ? pal::amberInk : pal::textSoft).withMultipliedAlpha (a), juce::Justification::centred);
        }
        if (hasKeyboardFocus (false)) paintFocus (g, r.reduced (1.f), 4.f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        setSelected (e.x * labels.size() / juce::jmax (1, getWidth()));
        if (onChange) onChange();
    }
    std::function<void()> onChange;
private:
    juce::StringArray labels;
    int idx = 0;
};

//==============================================================================

inline juce::Colour cream() { return highContrast() ? juce::Colours::white : juce::Colour (0xfff3ead0); }

// A window lit from behind, in a brass frame: the same glass the waveform sits in.
inline void paintGlass (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (highContrast())
    {
        g.setColour (juce::Colours::white); g.fillRoundedRectangle (r.expanded (2.f), 7.f);
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r, 5.f);
        return;
    }
    g.setGradientFill (juce::ColourGradient (pal::brassHi, 0.f, r.getY(), pal::brassDeep, 0.f, r.getBottom(), false));
    g.fillRoundedRectangle (r.expanded (2.f), 7.f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a4d1c), r.getCentreX(), r.getCentreY(), juce::Colour (0xff22100a), r.getX(), r.getY(), true));
    g.fillRoundedRectangle (r, 5.f);
}

// Dark inset panel with an optional title.
struct Panel : juce::Component, private juce::ChangeListener
{
    explicit Panel (juce::String t = {}, bool lit = false) : title (std::move (t)), highlighted (lit), ctx (currentMod())
    {
        setInterceptsMouseClicks (false, true);
        if (ctx != nullptr) ctx->changed.addChangeListener (this);
    }
    ~Panel() override { if (ctx != nullptr) ctx->changed.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (highContrast())
        {
            g.setColour (juce::Colours::black); g.fillRoundedRectangle (r, 6.f);
            g.setColour (highlighted ? pal::amber : juce::Colours::white); g.drawRoundedRectangle (r.reduced (1.f), 6.f, 2.f);
            if (title.isNotEmpty()) drawText (g, title, { 14, 12, getWidth() - 200, 24 }, serif (17.f, true), juce::Colours::white);
            return;
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xf02e1a10), 0.f, 0.f, juce::Colour (0xf01a0d08), 0.f, r.getHeight(), false));
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (highlighted ? pal::amber.withAlpha (0.8f) : pal::brassDeep.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.f, highlighted ? 1.5f : 1.f);
        g.setColour (juce::Colour (0x1fffd9a0)); g.drawHorizontalLine (1, 8.f, r.getWidth() - 8.f);
        if (title.isNotEmpty()) drawText (g, title, { 14, 12, getWidth() - 200, 24 }, serif (17.f, true), juce::Colour (0xfff6e8c9));
    }
    juce::String title;
    bool highlighted;
    ModContext* ctx = nullptr;
};


//==============================================================================
// Region parts: each of the six regions is a little radio part, drawn as vectors (cheap, and sharp at any size).
inline const char* regionPartName (int i)
{
    static const char* n[] = { "Tube", "Cap", "Coil", "Dial", "Fuse", "Resistor" };
    return n[juce::jlimit (0, 5, i)];
}

inline void drawRegionPart (juce::Graphics& g, int part, juce::Rectangle<float> box, float alpha = 1.f)
{
    const float sz = juce::jmin (box.getWidth(), box.getHeight());
    if (sz < 6.f) return;
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (sz / 24.f).translated (box.getCentreX() - sz * 0.5f, box.getCentreY() - sz * 0.5f));
    g.setOpacity (alpha);
    auto col = [&] (juce::uint32 c) { return juce::Colour (c).withMultipliedAlpha (alpha); };
    switch (juce::jlimit (0, 5, part))
    {
        case 0: // vacuum tube
        {
            juce::Path glass; glass.addRoundedRectangle (7.f, 2.f, 10.f, 14.f, 5.f);
            g.setGradientFill (juce::ColourGradient (col (0xfffbe9b8), 7.f, 4.f, col (0xffd9964a), 17.f, 16.f, false)); g.fillPath (glass);
            g.setColour (col (0xff6b4520)); g.drawLine (10.f, 7.f, 10.f, 14.f, 1.f); g.drawLine (14.f, 7.f, 14.f, 14.f, 1.f); g.drawLine (10.f, 7.f, 14.f, 7.f, 1.f);
            g.setColour (col (0xff3a2616)); g.fillRoundedRectangle (6.f, 15.f, 12.f, 4.f, 1.f);
            g.setColour (col (0xffc9a24e)); for (int p = 0; p < 4; ++p) g.fillRect (7.f + (float) p * 3.3f, 19.f, 1.2f, 3.f);
            break;
        }
        case 1: // electrolytic capacitor
        {
            g.setColour (col (0xff3f6fb5)); g.fillRoundedRectangle (6.f, 3.f, 12.f, 16.f, 2.f);
            g.setColour (col (0xff8fb4ea).withMultipliedAlpha (0.8f)); g.fillRect (8.f, 5.f, 2.f, 12.f);
            g.setColour (col (0xffb8bcc4)); g.fillRoundedRectangle (6.f, 3.f, 12.f, 3.f, 1.5f);
            g.setColour (col (0xff2a2016)); g.drawLine (10.f, 19.f, 10.f, 23.f, 1.2f); g.drawLine (14.f, 19.f, 14.f, 23.f, 1.2f);
            break;
        }
        case 2: // coil
        {
            g.setColour (col (0xff8a8f98)); g.drawLine (1.f, 12.f, 5.f, 12.f, 1.4f); g.drawLine (19.f, 12.f, 23.f, 12.f, 1.4f);
            for (int k = 0; k < 6; ++k)
            {
                const float x = 4.6f + (float) k * 2.6f;
                g.setColour (col (0xffb8632a)); g.fillRoundedRectangle (x, 5.f, 2.2f, 14.f, 1.f);
                g.setColour (col (0xfff0b06a)); g.fillRect (x + 0.5f, 6.f, 0.8f, 12.f);
            }
            break;
        }
        case 3: // tuning dial
        {
            g.setColour (col (0xffc9a24e)); g.fillEllipse (2.f, 2.f, 20.f, 20.f);
            g.setColour (col (0xff2a1a10)); g.fillEllipse (4.f, 4.f, 16.f, 16.f);
            g.setColour (col (0xffe9c88a));
            for (int k = 0; k < 7; ++k)
            {
                const float a = (-0.75f + 1.5f * (float) k / 6.f) * juce::MathConstants<float>::pi;
                g.drawLine (12.f + std::sin (a) * 6.f, 12.f - std::cos (a) * 6.f, 12.f + std::sin (a) * 7.8f, 12.f - std::cos (a) * 7.8f, 0.9f);
            }
            g.setColour (col (0xffe9532a)); g.drawLine (12.f, 12.f, 15.5f, 7.5f, 1.3f);
            break;
        }
        case 4: // glass fuse
        {
            g.setColour (col (0xff8a8f98)); g.drawLine (1.f, 12.f, 4.f, 12.f, 1.3f); g.drawLine (20.f, 12.f, 23.f, 12.f, 1.3f);
            g.setColour (col (0xffeee6cf)); g.fillRoundedRectangle (4.f, 8.f, 16.f, 8.f, 2.f);
            g.setColour (col (0xff9a8a68)); g.drawLine (6.f, 12.f, 18.f, 12.f, 0.8f);
            g.setColour (col (0xffc9a24e)); g.fillRoundedRectangle (3.f, 7.f, 4.f, 10.f, 1.5f); g.fillRoundedRectangle (17.f, 7.f, 4.f, 10.f, 1.5f);
            break;
        }
        default: // resistor
        {
            g.setColour (col (0xff8a8f98)); g.drawLine (1.f, 12.f, 5.f, 12.f, 1.3f); g.drawLine (19.f, 12.f, 23.f, 12.f, 1.3f);
            g.setColour (col (0xffd9b98a)); g.fillRoundedRectangle (4.f, 7.f, 16.f, 10.f, 4.f);
            g.setColour (col (0xff6b3a1c)); g.fillRect (8.f, 7.f, 2.f, 10.f);
            g.setColour (col (0xff1c1410)); g.fillRect (12.f, 7.f, 2.f, 10.f);
            g.setColour (col (0xffb5301e)); g.fillRect (16.f, 7.f, 2.f, 10.f);
            break;
        }
    }
    g.restoreState();
}

// Chip: rounded outlined label, used for "Hooked up" entries.
inline int chipWidth (const juce::String& t) { return (int) std::ceil (sans (11.f, true).getStringWidthFloat (t)) + 18; }
inline void drawChip (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
{
    g.setColour (pal::amber.withAlpha (0.16f));
    g.fillRoundedRectangle (r.toFloat(), 11.f);
    g.setColour (pal::amber.withAlpha (0.55f));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 11.f, 1.f);
    drawText (g, t, r.reduced (8, 0), sans (11.f, true), pal::text, juce::Justification::centred);
}

// The painted case: rounded top, seafoam enamel, a brass hairline inside the edge and a few chips.  Drawn once by the editor behind everything.
inline void addTopRoundedRect (juce::Path& p, float x, float y, float w, float h, float rt, float rb)
{
    const float pi = juce::MathConstants<float>::pi, hp = pi * 0.5f;
    p.startNewSubPath (x + rt, y);
    p.lineTo (x + w - rt, y);
    p.addCentredArc (x + w - rt, y + rt, rt, rt, 0.f, 0.f, hp);
    p.lineTo (x + w, y + h - rb);
    p.addCentredArc (x + w - rb, y + h - rb, rb, rb, 0.f, hp, pi);
    p.lineTo (x + rb, y + h);
    p.addCentredArc (x + rb, y + h - rb, rb, rb, 0.f, pi, pi + hp);
    p.lineTo (x, y + rt);
    p.addCentredArc (x + rt, y + rt, rt, rt, 0.f, pi + hp, 2.f * pi);
    p.closeSubPath();
}

inline void paintCabinet (juce::Graphics& g)
{
    if (highContrast())
    {
        g.fillAll (juce::Colours::black);
        const juce::Rectangle<float> hb (14.f, 8.f, 1252.f, 884.f);
        juce::Path hp;
        addTopRoundedRect (hp, hb.getX(), hb.getY(), hb.getWidth(), hb.getHeight(), 52.f, 10.f);
        g.setColour (juce::Colour (0xff0a0a0a)); g.fillPath (hp);
        g.setColour (juce::Colours::white); g.strokePath (hp, juce::PathStrokeType (3.f));
        return;
    }
    // the table the radio stands on
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a3720), 0, 0, juce::Colour (0xff40200f), 0, 900.f, false));
    g.fillAll();
    g.setColour (juce::Colour (0x1a000000));
    for (int x = 0; x < 1280; x += 11) g.fillRect (x, 0, 2, 900);

    const juce::Rectangle<float> body (14.f, 8.f, 1252.f, 884.f);
    g.setColour (juce::Colour (0x73140803));
    g.fillRoundedRectangle (body.translated (0.f, 10.f).expanded (4.f), 56.f);
    juce::Path shape;
    addTopRoundedRect (shape, body.getX(), body.getY(), body.getWidth(), body.getHeight(), 52.f, 10.f);
    juce::ColourGradient paint (pal::mint.brighter (0.12f), 0.f, body.getY(), pal::mint.darker (0.12f), 0.f, body.getBottom(), false);
    g.setGradientFill (paint);
    g.fillPath (shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x1f000000), body.getX(), 0.f, juce::Colour (0x00000000), body.getX() + 100.f, 0.f, false));
    g.fillPath (shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00000000), body.getRight() - 100.f, 0.f, juce::Colour (0x24000000), body.getRight(), 0.f, false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xff3d5a4e)); g.strokePath (shape, juce::PathStrokeType (1.f));
    g.setColour (juce::Colour (0x80ffffff)); g.drawLine (body.getX() + 40.f, body.getY() + 1.5f, body.getRight() - 40.f, body.getY() + 1.5f, 1.5f);
    // paint chips
    g.setColour (juce::Colour (0x806e8279));
    const int chips[][4] = { { 1190, 300, 4, 8 }, { 70, 520, 4, 9 }, { 1190, 780, 5, 7 }, { 30, 860, 9, 5 }, { 640, 872, 10, 4 } };
    for (auto& c : chips) g.fillRoundedRectangle ((float) c[0], (float) c[1], (float) c[2], (float) c[3], 1.5f);

}

// Grille cloth, a brass rim, and the brass-framed control strip, shared by the pages.  Page coordinates start 52 px down.
inline juce::Image& clothTile()
{
    static juce::Image tiles[2];
    auto& t = tiles[highContrast() ? 1 : 0];
    if (! t.isValid())
    {
        if (highContrast()) { t = juce::Image (juce::Image::ARGB, 1, 1, true); juce::Graphics (t).fillAll (juce::Colours::black); return t; }
        juce::Image i (juce::Image::ARGB, 3, 3, true);
        juce::Graphics g (i);
        g.fillAll (juce::Colour (0xff47301f));
        g.setColour (juce::Colour (0x24ffcd96)); g.fillRect (0, 0, 3, 1);
        g.setColour (juce::Colour (0x47000000)); g.fillRect (0, 0, 1, 3);
        t = i;
    }
    return t;
}
inline juce::Image& latticeTile()
{
    static juce::Image tiles[2];
    auto& t = tiles[highContrast() ? 1 : 0];
    if (! t.isValid())
    {
        if (highContrast()) { t = juce::Image (juce::Image::ARGB, 1, 1, true); juce::Graphics (t).fillAll (juce::Colours::black); return t; }
        juce::Image i (juce::Image::ARGB, 9, 9, true);
        juce::Graphics g (i);
        g.fillAll (juce::Colour (0xff1a0f0a));
        g.setColour (juce::Colour (0x28c9a24e));
        g.drawLine (0.f, 9.f, 9.f, 0.f, 1.f); g.drawLine (0.f, 0.f, 9.f, 9.f, 1.f);
        t = i;
    }
    return t;
}
// The control strip: brass frame, faint lattice, then the dark panel.
inline void paintStrip (juce::Graphics& g, juce::Rectangle<float> strip)
{
    if (highContrast())
    {
        g.setColour (juce::Colours::white); g.fillRoundedRectangle (strip.expanded (3.f), 6.f);
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (strip, 4.f);
        return;
    }
    g.setColour (pal::brassDeep);  g.fillRoundedRectangle (strip.expanded (4.f), 7.f);
    g.setColour (pal::brassHi);    g.fillRoundedRectangle (strip.expanded (3.f), 6.f);
    g.setTiledImageFill (latticeTile(), 0, 0, 1.f);
    g.fillRoundedRectangle (strip, 4.f);
    g.setColour (pal::panel);
    g.fillRoundedRectangle (strip, 4.f);
    g.setColour (juce::Colour (0x2effebbe));
    g.drawRoundedRectangle (strip.reduced (0.5f), 4.f, 1.f);
}

inline void paintDrawerFrame (juce::Graphics& g, bool tallFrontBoard)
{
    juce::ignoreUnused (tallFrontBoard);
    const juce::Rectangle<float> cav (30.f, 108.f, 1220.f, 716.f);
    if (highContrast())
    {
        g.setColour (juce::Colours::white); g.fillRoundedRectangle (cav.expanded (3.f), 18.f);
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (cav, 16.f);
        return;
    }
    g.setColour (pal::brassDeep);  g.fillRoundedRectangle (cav.expanded (6.f), 20.f);
    g.setGradientFill (juce::ColourGradient (pal::brassHi.brighter (0.2f), cav.getX(), cav.getY(), pal::brassHi.darker (0.3f), cav.getRight(), cav.getBottom(), false));
    g.fillRoundedRectangle (cav.expanded (5.f), 19.f);
    g.setTiledImageFill (clothTile(), 0, 0, 1.f);
    g.fillRoundedRectangle (cav, 16.f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xa6000000), cav.getX(), cav.getY(), juce::Colour (0x00000000), cav.getX(), cav.getY() + 18.f, false));
    g.fillRoundedRectangle (cav.withHeight (18.f), 16.f);
}
} // namespace ui
