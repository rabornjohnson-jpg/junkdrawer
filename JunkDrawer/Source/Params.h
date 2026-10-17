#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Sample.h"

// Every parameter lives here, so the processor, the editor and (later) the LFO engine agree on the IDs.
namespace jd
{
constexpr int kTracks = 8;
constexpr int kLfos   = 6;

inline juce::String trackId (int track, const char* suffix) { return "t" + juce::String (track + 1) + "_" + suffix; }
inline juce::String lfoId   (int lfo,   const char* suffix) { return "l" + juce::String (lfo + 1)   + "_" + suffix; }

inline juce::String regionId (int track, int region, const char* suffix) { return "t" + juce::String (track + 1) + "_r" + juce::String (region + 1) + "_" + suffix; }
// Sequencer lanes.  Every lane has its own length (1-16 steps) and its own 16 step values, so lanes can run against each other.
enum Lane { LaneRegion, LaneVolume, LanePitch, LaneRatchet, LaneDirection, LaneLength, LaneProb, LaneTime, LanePan, kLanes };
constexpr int kPatterns = 8, kChainMax = 16;
constexpr int kRolled = 6;                // Rummage and Shake re-roll the first six lanes; probability and timing are left as set
constexpr int kSteps = 16;
inline const char* laneKey (int lane)
{
    static const char* k[] = { "region", "vol", "pitch", "ratchet", "dir", "len", "prob", "time", "pan" };
    return k[lane];
}
inline juce::String stepId (int track, int step, int lane) { return "t" + juce::String (track + 1) + "_s" + juce::String (step + 1) + "_" + laneKey (lane); }
inline juce::String laneLenId (int track, int lane)        { return "t" + juce::String (track + 1) + "_" + laneKey (lane) + "_n"; }
inline const juce::StringArray kDirections { "Region", "Fwd", "Rev" };
inline const juce::StringArray kStepFeels { "Straight", "Dotted", "Triplet" };
inline const juce::StringArray kStepDivs   { "1/4", "1/8", "1/16", "1/32" };

inline const juce::StringArray kLoopModes { "One-shot", "Loop", "Ping-pong" };

inline const juce::StringArray kLfoShapes   { "Sine", "Tri", "Saw", "Sq", "S+H", "Rand", "Drift" };
inline const juce::StringArray kLfoSyncs    { "Free", "Sync", "Dotted", "Triplet" };
inline const juce::StringArray kLfoPolarity { "Uni", "Bi" };

// "1/4", "1/4." (dotted) or "1/4T" (triplet) for a tempo-synced LFO
inline juce::String kLfoSyncsShort (int divIndex, int sync)
{
    static const char* divs[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    return juce::String (divs[juce::jlimit (0, 5, divIndex)]) + (sync == 2 ? "." : sync == 3 ? "T" : "");
}

namespace detail
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

inline void addFloat (Layout& l, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def,
                      std::function<juce::String (float, int)> text = {})
{
    juce::AudioParameterFloatAttributes a;
    if (text) a = a.withStringFromValueFunction (std::move (text));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, a));
}
inline void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
}
inline void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}

inline std::function<juce::String (float, int)> fmt (const char* unit, int decimals)
{
    return [unit, decimals] (float v, int) { return juce::String (v, decimals) + unit; };
}
inline std::function<juce::String (float, int)> pct()
{
    return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; };
}
inline std::function<juce::String (float, int)> hz()
{
    return [] (float v, int) { return v >= 1000.f ? juce::String (v / 1000.f, 1) + " kHz" : juce::String (v, v < 10.f ? 2 : 0) + " Hz"; };
}
inline std::function<juce::String (float, int)> ms()
{
    return [] (float v, int) { return v >= 1000.f ? juce::String (v / 1000.f, 1) + " s" : juce::String (juce::roundToInt (v)) + " ms"; };
}
inline std::function<juce::String (float, int)> db()
{
    return [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
}
inline std::function<juce::String (float, int)> semis()
{
    return [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st"; };
}
inline std::function<juce::String (float, int)> times()
{
    return [] (float v, int) { return juce::String (v, 2) + "x"; };
}
inline std::function<juce::String (float, int)> degrees()
{
    return [] (float v, int) { return juce::String (juce::roundToInt (v)) + juce::String::charToString (0x00b0); };
}
inline juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}
} // namespace detail

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace detail;
    Layout l;

    // ---- Master page ----------------------------------------------------------------------------------------------
    addFloat  (l, "m_volume", "Volume", { -60.f, 6.f, 0.01f }, -4.f, db());
    addFloat  (l, "m_pan",    "Pan",    { -1.f, 1.f, 0.001f }, 0.f, [] (float v, int) { return v > -0.005f && v < 0.005f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (v) * 100.f)) + (v < 0.f ? " L" : " R"); });
    addFloat  (l, "m_pitch",  "Pitch",  { -48.f, 48.f, 0.01f }, 0.f, semis());
    addChoice (l, "m_pitchrange", "Pitch range", { "12", "24", "48" }, 0);
    addFloat  (l, "m_rate",   "Rate",   skewed (0.25f, 4.f, 1.f), 1.f, times());          // step speed only, never pitch
    addChoice (l, "m_ratemode", "Rate mode", { "Free", "Sync" }, 0);
    addFloat  (l, "m_swing",  "Swing",  { 0.5f, 0.75f, 0.001f }, 0.5f, pct());
    addFloat  (l, "m_width",  "Width",  { 0.f, 2.f, 0.001f }, 1.f, pct());

    // ---- Effects (each card: On, mode, four knobs, one extra option) ---------------------------------------------
    addBool   (l, "fx_filter_on",   "Filter on", true);
    addChoice (l, "fx_filter_type", "Filter type", { "Low pass", "Band", "High pass", "Notch" }, 0);
    addFloat  (l, "fx_filter_cut",  "Cutoff", skewed (20.f, 20000.f, 1000.f), 3200.f, hz());
    addFloat  (l, "fx_filter_res",  "Reso",   { 0.f, 1.f, 0.001f }, 0.2f, pct());
    addFloat  (l, "fx_filter_drive","Drive",  { 0.f, 18.f, 0.01f }, 2.f, db());
    addFloat  (l, "fx_filter_mix",  "Mix",    { 0.f, 1.f, 0.001f }, 1.f, pct());
    addChoice (l, "fx_filter_slope","Slope",  { "12 dB", "24 dB" }, 0);

    addBool   (l, "fx_delay_on",    "Delay on", true);
    addChoice (l, "fx_delay_mode",  "Delay mode", kLfoSyncs, 1);
    addFloat  (l, "fx_delay_time",  "Time",     skewed (1.f, 2000.f, 250.f), 250.f, ms());
    addFloat  (l, "fx_delay_fb",    "Feedback", { 0.f, 0.95f, 0.001f }, 0.38f, pct());
    addFloat  (l, "fx_delay_tone",  "Tone",     skewed (200.f, 16000.f, 3000.f), 4100.f, hz());
    addFloat  (l, "fx_delay_mix",   "Mix",      { 0.f, 1.f, 0.001f }, 0.25f, pct());
    addChoice (l, "fx_delay_style", "Style",    { "Mono", "Ping-pong" }, 1);

    addBool   (l, "fx_reverb_on",   "Reverb on", true);
    addChoice (l, "fx_reverb_mode", "Reverb mode", { "Room", "Hall", "Plate", "Spring" }, 1);
    addFloat  (l, "fx_reverb_size", "Size",    { 0.f, 1.f, 0.001f }, 0.6f, pct());
    addFloat  (l, "fx_reverb_decay","Decay",   skewed (200.f, 10000.f, 1500.f), 2400.f, ms());
    addFloat  (l, "fx_reverb_damp", "Damping", { 0.f, 1.f, 0.001f }, 0.45f, pct());
    addFloat  (l, "fx_reverb_mix",  "Mix",     { 0.f, 1.f, 0.001f }, 0.4f, pct());
    addChoice (l, "fx_reverb_pre",  "Pre-delay", { "0 ms", "20 ms", "40 ms" }, 1);

    addBool   (l, "fx_lim_on",      "Limiter on", false);
    addChoice (l, "fx_lim_mode",    "Limiter mode", { "Soft", "Hard" }, 0);
    addFloat  (l, "fx_lim_ceiling", "Ceiling", { -12.f, 0.f, 0.01f }, -0.3f, db());
    addFloat  (l, "fx_lim_release", "Release", skewed (10.f, 500.f, 80.f), 80.f, ms());
    addFloat  (l, "fx_lim_input",   "Input",   { -12.f, 12.f, 0.01f }, 0.f, db());
    addFloat  (l, "fx_lim_mix",     "Mix",     { 0.f, 1.f, 0.001f }, 1.f, pct());
    addChoice (l, "fx_lim_look",    "Lookahead", { "Off", "1 ms", "5 ms" }, 1);

    addChoice (l, "key_root", "Project key", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    addChoice (l, "key_mode", "Project key mode", { "No key", "Major", "Minor" }, 0);
    addChoice (l, "snap", "Snap region edges", { "Off", "Hit", "Zero" }, 1);

    // ---- Sequencer (global) ---------------------------------------------------------------------------------------
    addChoice (l, "seq_div", "Step length", kStepDivs, 2);
    addChoice (l, "seq_feel", "Step feel", kStepFeels, 0);
    for (int ln = 0; ln < kRolled; ++ln) addBool (l, juce::String ("lock_") + laneKey (ln), juce::String ("Keep ") + laneKey (ln) + " lane", false);
    addBool   (l, "chain_on", "Play the chain", false);
    addFloat  (l, "shake_amt", "Shake amount", { 0.f, 1.f, 0.01f }, 0.25f, pct());
    addFloat  (l, "seq_bpm", "Tempo without a host", { 40.f, 240.f, 0.1f }, 120.f, fmt (" BPM", 1));
    addBool   (l, "seq_run", "Run without a host", false);

    // ---- Tracks (mute, solo, level, pan; regions and steps arrive in Stage 3) ------------------------------
    for (int t = 0; t < kTracks; ++t)
    {
        const juce::String n = "Track " + juce::String (t + 1) + " ";
        addBool  (l, trackId (t, "mute"),  n + "mute", false);
        addBool  (l, trackId (t, "solo"),  n + "solo", false);
        addFloat (l, trackId (t, "level"), n + "level", { -60.f, 6.f, 0.01f }, 0.f, db());
        addFloat (l, trackId (t, "pan"),   n + "pan",   { -1.f, 1.f, 0.001f }, 0.f);
        addFloat (l, trackId (t, "tune"),  n + "tune",  { -24.f, 24.f, 0.01f }, 0.f, semis());
        addChoice (l, trackId (t, "order"), n + "region order", { "As written", "Walk", "Pendulum", "Random", "Mostly as written" }, 0);
        addFloat (l, trackId (t, "senddel"), n + "delay send",  { 0.f, 1.f, 0.001f }, 0.f, pct());
        addFloat (l, trackId (t, "sendrev"), n + "reverb send", { 0.f, 1.f, 0.001f }, 0.f, pct());
        addFloat (l, trackId (t, "morph"), n + "morph", { 0.f, 1.f, 0.001f }, 0.f, pct());
        addFloat (l, trackId (t, "rprob"), n + "ratchet chance", { 0.f, 1.f, 0.001f }, 1.f, pct());
        addChoice (l, trackId (t, "rshape"), n + "ratchet shape", { "Even", "Fade out", "Swell", "Climb", "Bounce" }, 0);
        addChoice (l, trackId (t, "cond"), n + "plays", { "Every loop", "Every 2nd loop", "Every 3rd loop", "Every 4th loop", "First loop only" }, 0);
        l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { trackId (t, "regions"), 1 }, n + "regions", 1, kRegions, 4));

        for (int r = 0; r < kRegions; ++r)
        {
            const juce::String rn = n + "region " + juce::String (r + 1) + " ";
            addFloat  (l, regionId (t, r, "start"),  rn + "start",   { 0.f, 1.f, 0.00001f }, (float) r / (float) kRegions);
            addFloat  (l, regionId (t, r, "end"),    rn + "end",     { 0.f, 1.f, 0.00001f }, (float) (r + 1) / (float) kRegions);
            addFloat  (l, regionId (t, r, "pitch"),  rn + "pitch",   { -24.f, 24.f, 0.01f }, 0.f, semis());
            addFloat  (l, regionId (t, r, "gain"),   rn + "gain",    { -24.f, 12.f, 0.01f }, 0.f, db());
            addFloat  (l, regionId (t, r, "pan"),    rn + "pan",     { -1.f, 1.f, 0.001f }, 0.f);
            addFloat  (l, regionId (t, r, "cutoff"), rn + "cutoff",  skewed (20.f, 20000.f, 1000.f), 20000.f, hz());
            addFloat  (l, regionId (t, r, "reso"),   rn + "reso",    { 0.f, 1.f, 0.001f }, 0.2f, pct());
            addFloat  (l, regionId (t, r, "drive"),  rn + "drive",   { 0.f, 18.f, 0.01f }, 0.f, db());
            addFloat  (l, regionId (t, r, "attack"), rn + "attack",  skewed (0.f, 2000.f, 80.f), 2.f, ms());
            addFloat  (l, regionId (t, r, "decay"),  rn + "decay",   skewed (1.f, 5000.f, 300.f), 90.f, ms());
            addFloat  (l, regionId (t, r, "sustain"),rn + "sustain", { 0.f, 1.f, 0.001f }, 1.f, pct());
            addFloat  (l, regionId (t, r, "release"),rn + "release", skewed (3.f, 5000.f, 300.f), 50.f, ms());
            addChoice (l, regionId (t, r, "loop"),   rn + "loop mode", kLoopModes, 0);
            addBool   (l, regionId (t, r, "reverse"),rn + "reverse", false);
        }

        for (int ln = 0; ln < kLanes; ++ln)
            l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { laneLenId (t, ln), 1 }, n + laneKey (ln) + " lane steps", 1, kSteps, kSteps));
        for (int s = 0; s < kSteps; ++s)
        {
            const juce::String sn = n + "step " + juce::String (s + 1) + " ";
            l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { stepId (t, s, LaneRegion), 1 }, sn + "region", 0, kRegions, 0));
            addFloat  (l, stepId (t, s, LaneVolume),  sn + "volume",  { 0.f, 1.f, 0.001f }, 1.f, pct());
            addFloat  (l, stepId (t, s, LanePitch),   sn + "pitch",   { -12.f, 12.f, 1.f }, 0.f, semis());
            l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { stepId (t, s, LaneRatchet), 1 }, sn + "ratchet", 1, 4, 1));
            addChoice (l, stepId (t, s, LaneDirection), sn + "direction", kDirections, 0);
            addFloat  (l, stepId (t, s, LaneLength),  sn + "length",  { 0.1f, 1.f, 0.001f }, 1.f, pct());
            addFloat  (l, stepId (t, s, LaneProb),    sn + "chance",  { 0.f, 1.f, 0.001f }, 1.f, pct());
            addFloat  (l, stepId (t, s, LanePan),     sn + "pan",     { -1.f, 1.f, 0.01f }, 0.f, [] (float v, int) { const int p = juce::roundToInt (v * 100.f); return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p)); });
            addFloat  (l, stepId (t, s, LaneTime),    sn + "timing",  { -0.5f, 0.5f, 0.001f }, 0.f, [] (float v, int) { const int p = juce::roundToInt (v * 100.f); return (p > 0 ? "+" : "") + juce::String (p) + "%"; });
        }
    }

    // ---- LFOs (settings only; the engine and the assignments arrive in Stage 5) ----------------------------------
    for (int i = 0; i < kLfos; ++i)
    {
        const juce::String n = "LFO " + juce::String (i + 1) + " ";
        addChoice (l, lfoId (i, "shape"),  n + "shape", kLfoShapes, i == 1 ? 1 : (i == 2 ? 6 : (i == 3 ? 2 : (i == 4 ? 4 : (i == 5 ? 5 : 0)))));
        addFloat  (l, lfoId (i, "rate"),   n + "rate",  skewed (0.01f, 30.f, 1.f), 1.f, [] (float v, int) { return juce::String (v, v < 10.f ? 2 : 1) + " Hz"; });
        addChoice (l, lfoId (i, "div"),    n + "division", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 2);
        addFloat  (l, lfoId (i, "depth"),  n + "depth", { 0.f, 1.f, 0.001f }, 0.5f, pct());
        addFloat  (l, lfoId (i, "phase"),  n + "phase", { 0.f, 360.f, 0.1f }, 0.f, degrees());
        addFloat  (l, lfoId (i, "fade"),   n + "fade in", { 0.f, 2000.f, 1.f }, 0.f, ms());
        addChoice (l, lfoId (i, "sync"),   n + "sync", kLfoSyncs, 1);
        addChoice (l, lfoId (i, "polar"),  n + "polarity", kLfoPolarity, 1);
        addBool   (l, lfoId (i, "global"), n + "global", i != 2 && i != 5);   // true: one shared LFO; false: one copy per track
    }
    return l;
}
} // namespace jd
