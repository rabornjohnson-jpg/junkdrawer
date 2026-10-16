#pragma once
#include <juce_core/juce_core.h>

// Step timing, shared by the audio engine and the tests.
namespace jd
{
// u is the position in steps (1.0 per step).  Swing delays every second step: 0.5 is straight, 0.75 is a heavy shuffle.
// Returns the running step number and how far (0..1) we are through that step.
inline void locateStep (double u, double swing, long long& step, double& frac)
{
    const double pair = std::floor (u * 0.5);
    const double f2 = u * 0.5 - pair;                           // 0..1 across a pair of steps
    const double s = juce::jlimit (0.5, 0.75, swing);           // where the second step of the pair starts
    const bool second = f2 >= s;
    step = (long long) pair * 2 + (second ? 1 : 0);
    frac = second ? (f2 - s) / (1.0 - s) : f2 / s;
}

inline double stepBeats (int divIndex, int feel = 0)
{
    static const double b[] = { 1.0, 0.5, 0.25, 0.125 };
    static const double f[] = { 1.0, 1.5, 2.0 / 3.0 };      // straight, dotted, triplet
    return b[juce::jlimit (0, 3, divIndex)] * f[juce::jlimit (0, 2, feel)];
}

// Sync mode snaps the rate to 1/4x, 1/2x, 1x, 2x or 4x.
inline double snapRate (double rate)
{
    static const double r[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
    double best = 1.0, bd = 1.0e9;
    for (double v : r) { const double d = std::abs (std::log2 (rate / v)); if (d < bd) { bd = d; best = v; } }
    return best;
}
} // namespace jd
