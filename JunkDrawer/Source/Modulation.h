#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// LFO shapes and the table that says which LFO moves which control.
namespace jd
{
inline float hash01 (juce::uint32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (float) (x & 0xffffffU) / (float) 0x1000000;
}

// phase is in cycles (1.0 = one full turn).  Returns -1..+1.  Random shapes depend only on the cycle number and the seed,
// so every copy of an LFO that shares a seed agrees without keeping any state.
inline float lfoShape (int shape, double phase, juce::uint32 seed)
{
    const double cyc = std::floor (phase);
    const float f = (float) (phase - cyc);
    const juce::uint32 c = (juce::uint32) (juce::int64) cyc;
    auto r = [&] (juce::uint32 k) { return hash01 (k * 2654435761U + seed * 40503U + 12345U) * 2.f - 1.f; };
    auto smooth = [&] (double p, juce::uint32 salt)
    {
        const double pc = std::floor (p);
        const float pf = (float) (p - pc);
        const juce::uint32 k = (juce::uint32) (juce::int64) pc;
        const float a = r (k + salt), b = r (k + 1 + salt), t = 0.5f - 0.5f * std::cos (pf * juce::MathConstants<float>::pi);
        return a + (b - a) * t;
    };
    switch (shape)
    {
        case 0:  return std::sin (juce::MathConstants<float>::twoPi * f);
        case 1:  return f < 0.25f ? 4.f * f : (f < 0.75f ? 2.f - 4.f * f : 4.f * f - 4.f);
        case 2:  return 1.f - 2.f * f;
        case 3:  return f < 0.5f ? 1.f : -1.f;
        case 4:  return r (c);
        case 5:  return smooth (phase, 0);
        default: return (smooth (phase * 0.37, 101) + 0.5f * smooth (phase * 0.91 + 3.0, 977)) / 1.5f;
    }
}

// beats in one cycle for a division index (1/1 ... 1/32) and a sync mode (1 sync, 2 dotted, 3 triplet)
inline double lfoPeriodBeats (int div, int mode)
{
    static const double b[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    return b[juce::jlimit (0, 5, div)] * (mode == 2 ? 1.5 : (mode == 3 ? 2.0 / 3.0 : 1.0));
}

constexpr int kMaxMods = 128;

// One link: an LFO moves a control by `amount` (-1..+1) of that control's range.
struct Assign { juce::String target; int lfo = 0; float amount = 0.f; };

// What the audio thread works from.  Built on the message thread and handed over whole.
struct ModTable
{
    struct Target { std::atomic<float>* raw = nullptr; juce::RangedAudioParameter* param = nullptr; int track = -1; };
    struct Entry  { int target = 0; int lfo = 0; float amount = 0.f; };
    std::vector<Target> targets;
    std::vector<Entry> entries;
};
} // namespace jd
