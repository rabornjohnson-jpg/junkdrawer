#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Shared look and small widgets.  Rules: no text below 11 px, nothing drawn outside its box, every control is a real parameter.
namespace ui
{
namespace pal
{
const juce::Colour counterTop   { 0xff313836 }, counterBottom { 0xff232827 };
const juce::Colour wood         { 0xff6a4a2d }, woodDark { 0xff4b3320 }, woodLight { 0xff8a6a45 };
const juce::Colour panel        { 0xcc120a05 };                       // dark panel, about 80 % opaque
const juce::Colour text         { 0xfff3e7c9 }, textSoft { 0xffe0cd9f }, textDim { 0xffcdb98a };
const juce::Colour amber        { 0xffe9b872 }, amberInk { 0xff1c1108 };
const juce::Colour line         { 0x4df0e2c4 };                       // 30 % cream hairline
const juce::Colour brass        { 0xffb8893a };
}

inline juce::Font sans (float h, bool bold = false)  { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
inline juce::Font mono (float h, bool bold = false)  { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }
inline juce::Font serif (float h, bool bold = true)  { return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

inline void drawText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Font f, juce::Colour c,
                      juce::Justification j = juce::Justification::centredLeft)
{
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

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const float d = (float) juce::jmin (w, h) - 4.f;
        const auto b = juce::Rectangle<float> (d, d).withCentre ({ x + w * 0.5f, y + h * 0.5f });
        const auto c = b.getCentre();

        g.setColour (juce::Colours::black.withAlpha (0.45f));                                   // drop shadow
        g.fillEllipse (b.translated (0.f, 2.5f).expanded (0.5f));

        juce::ColourGradient grad (juce::Colour (0xffcfa56d), c.x - d * 0.14f, c.y - d * 0.2f, juce::Colour (0xff6a4526), c.x + d * 0.5f, c.y + d * 0.55f, true);
        grad.addColour (0.5, juce::Colour (0xffa07445));
        g.setGradientFill (grad);
        g.fillEllipse (b);

        g.setColour (juce::Colour (0x333c200a));                                                  // growth rings
        for (float k = 0.25f; k < 0.95f; k += 0.17f) g.drawEllipse (b.withSizeKeepingCentre (d * k, d * k), 1.f);

        g.setColour (juce::Colour (0xb31c0e04));
        g.drawEllipse (b, 1.5f);
        g.setColour (juce::Colour (0x66ffebbe));
        g.drawEllipse (b.reduced (1.5f), 1.f);

        const float ang = a0 + pos * (a1 - a0);
        juce::Path tick;
        tick.addRoundedRectangle (-1.5f, -d * 0.5f + d * 0.1f, 3.f, d * 0.3f, 1.5f);
        g.setColour (s.isEnabled() ? juce::Colour (0xff1d1008) : juce::Colour (0x661d1008));
        g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c));

        if (s.hasKeyboardFocus (false))
        {
            g.setColour (pal::amber);
            g.drawEllipse (b.expanded (2.f), 1.5f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return sans (12.f, true); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (on ? pal::amber : juce::Colours::white.withAlpha (over || down ? 0.12f : 0.0f));
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (on ? pal::amber : pal::line.withAlpha (b.isEnabled() ? 0.8f : 0.35f));
        g.drawRoundedRectangle (r, 4.f, 1.f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (on ? pal::amberInk : (b.isEnabled() ? pal::text : pal::textDim.withAlpha (0.6f)));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
    }
};

//==============================================================================
// A parameter knob: wood dial, name, and the value as the parameter prints it.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label, int dialSize = 38,
          juce::Colour nameColour = pal::text, juce::Colour valueColour = pal::amber, juce::String note = {})
        : param (*s.getParameter (id)), size (dialSize), name (label), noteText (std::move (note)), nameCol (nameColour), valCol (valueColour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (-135.f), juce::degreesToRadians (135.f), true);
        slider.setTitle (param.getName (64));
        slider.setTooltip (param.getName (64));
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
        attach = std::make_unique<juce::SliderParameterAttachment> (param, slider, nullptr);
    }

    void resized() override { slider.setBounds (getLocalBounds().withSizeKeepingCentre (size + 4, size + 4).withY (0)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedTop (size + 6);
        drawText (g, name, r.removeFromTop (14), sans (12.f, true), nameCol, juce::Justification::centred);
        drawText (g, param.getCurrentValueAsText(), r.removeFromTop (14), mono (11.f), valCol, juce::Justification::centred);
        if (noteText.isNotEmpty()) drawText (g, noteText, r.removeFromTop (14), sans (11.f), pal::textDim, juce::Justification::centred);
    }

    juce::Slider slider;

private:
    juce::RangedAudioParameter& param;
    int size;
    juce::String name, noteText;
    juce::Colour nameCol, valCol;
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
        attach.sendInitialUpdate();
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
// Dark inset panel with an optional title.
struct Panel : juce::Component
{
    explicit Panel (juce::String t = {}, bool lit = false) : title (std::move (t)), highlighted (lit) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (pal::panel);
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (highlighted ? pal::amber.withAlpha (0.55f) : juce::Colour (0x29ffebbe));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
        if (title.isNotEmpty()) drawText (g, title, { 14, 12, getWidth() - 200, 24 }, mono (15.f, true), pal::text);
    }
    juce::String title;
    bool highlighted;
};

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

// Wood and brass backdrop shared by the pages.
inline void paintDrawerFrame (juce::Graphics& g, bool tallFrontBoard)
{
    const juce::Rectangle<float> body (14.f, 4.f, 1252.f, 836.f);
    g.setGradientFill (juce::ColourGradient (pal::wood.brighter (0.1f), 0, body.getY(), pal::wood.darker (0.35f), 0, body.getBottom(), false));
    g.fillRoundedRectangle (body, 6.f);
    g.setColour (juce::Colour (0xff2e1a0c));
    g.drawRoundedRectangle (body, 6.f, 1.f);

    const juce::Rectangle<float> cav (30.f, 108.f, 1220.f, 716.f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a3e26), 0, cav.getY(), juce::Colour (0xff3a2615), 0, cav.getBottom(), false));
    g.fillRect (cav);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xa6000000), 0, cav.getY(), juce::Colour (0x00000000), 0, cav.getY() + 18.f, false));
    g.fillRect (cav.withHeight (18.f));
    g.setColour (juce::Colour (0xff2e1a0c));
    g.drawRect (cav, 1.f);

    juce::Rectangle<float> board (14.f, 4.f, 1252.f, 104.f);
    g.setGradientFill (juce::ColourGradient (pal::woodLight.brighter (0.15f), 0, board.getY(), pal::woodLight.darker (0.25f), 0, board.getBottom(), false));
    g.fillRoundedRectangle (board, 6.f);
    g.setColour (juce::Colour (0xff6c4d2d));
    g.fillRect (board.removeFromBottom (14.f));
    const float screws[][2] = { { 22.f, 12.f }, { 1244.f, 12.f }, { 22.f, 92.f }, { 1244.f, 92.f } };
    for (auto& sp : screws)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6df9a), sp[0] - 2.f, sp[1] - 2.f, juce::Colour (0xff8a6528), sp[0] + 4.f, sp[1] + 4.f, true));
        g.fillEllipse (sp[0] - 4.5f, sp[1] - 4.5f, 9.f, 9.f);
    }
    juce::ignoreUnused (tallFrontBoard);
}
} // namespace ui
