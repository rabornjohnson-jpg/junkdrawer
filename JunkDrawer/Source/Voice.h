#pragma once
#include "Sample.h"
#include <juce_dsp/juce_dsp.h>

// One playing piece of a sample.  The sequencer (Stage 3) will trigger these on steps; for now the Drawer page auditions with them.
// Everything is set when the voice starts, so nothing here allocates or locks while it runs.
namespace jd
{
enum LoopMode { OneShot, Loop, PingPong };

struct RegionSnapshot
{
    float start = 0.f, end = 1.f;             // 0..1 of the sample
    float pitch = 0.f;                         // semitones (fractions are cents)
    float gainDb = 0.f, pan = 0.f;
    float cutoff = 20000.f, reso = 0.2f, driveDb = 0.f;
    float attackMs = 2.f, decayMs = 100.f, sustain = 1.f, releaseMs = 50.f;
    int loop = OneShot;
    bool reverse = false;
};

class RegionVoice
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, 512u, 1u };
        for (auto& f : filt) { f.prepare (spec); f.setType (juce::dsp::StateVariableTPTFilterType::lowpass); }
    }

    bool isActive() const noexcept { return active; }

    void trigger (std::shared_ptr<const TrackData> d, const RegionSnapshot& s, float trackGain, float trackPan)
    {
        if (d == nullptr || d->length() < 4) return;
        data = std::move (d);
        const int n = data->length();
        startS = juce::jlimit (0, n - 2, (int) std::lround ((double) juce::jmin (s.start, s.end) * n));
        endS   = juce::jlimit (startS + 2, n - 1, (int) std::lround ((double) juce::jmax (s.start, s.end) * n));
        reverse = s.reverse;
        loop = s.loop;
        dir = reverse ? -1 : 1;
        pos = reverse ? (double) endS - 1.0 : (double) startS;
        ratio = std::pow (2.0, (double) s.pitch / 12.0);

        gain = juce::Decibels::decibelsToGain (s.gainDb, -60.f) * trackGain;
        const float pn = juce::jlimit (-1.f, 1.f, s.pan + trackPan);
        const float a = (pn + 1.f) * 0.25f * juce::MathConstants<float>::pi;
        panL = std::cos (a) * 1.41421356f;
        panR = std::sin (a) * 1.41421356f;

        useFilter = s.cutoff < 19000.f;
        if (useFilter)
        {
            const float fc = juce::jlimit (20.f, (float) (sampleRate * 0.45), s.cutoff);
            for (auto& f : filt) { f.reset(); f.setCutoffFrequency (fc); f.setResonance (0.707f + s.reso * 9.f); }
        }
        driveGain = juce::Decibels::decibelsToGain (s.driveDb);
        useDrive = s.driveDb > 0.05f;
        driveNorm = useDrive ? 1.f / std::tanh (driveGain) : 1.f;

        const float sr = (float) sampleRate;
        atkInc = 1.f / juce::jmax (1.f, s.attackMs * 0.001f * sr);
        decCoef = std::exp (-1.f / juce::jmax (1.f, s.decayMs * 0.001f * sr / 4.f));          // reaches the sustain level within about 4 time constants
        relCoef = std::exp (-1.f / juce::jmax (1.f, juce::jmax (3.f, s.releaseMs) * 0.001f * sr / 4.f));
        sus = juce::jlimit (0.f, 1.f, s.sustain);
        env = 0.f;
        stage = Attack;
        active = true;
    }

    // Lets a looping region fade out (a one-shot ends by itself when it reaches the end of its piece).
    void release() noexcept { if (active && stage != Release) stage = Release; }
    void kill() noexcept { active = false; data.reset(); }

    // Adds this voice to left/right.
    void render (float* left, float* right, int n)
    {
        if (! active) return;
        const float* sl = data->audio.getReadPointer (0);
        const float* sr = data->audio.getReadPointer (1);
        const int fade = juce::jmax (8, (int) (sampleRate * 0.002));                        // 2 ms at both edges of the piece, no clicks

        for (int i = 0; i < n; ++i)
        {
            // envelope
            switch (stage)
            {
                case Attack:  env += atkInc; if (env >= 1.f) { env = 1.f; stage = Decay; } break;
                case Decay:   env = sus + (env - sus) * decCoef; if (std::abs (env - sus) < 1.0e-4f) { env = sus; stage = Sustain; } break;
                case Sustain: break;
                case Release: env *= relCoef; if (env < 1.0e-4f) { kill(); return; } break;
            }

            const int i0 = juce::jlimit (startS, endS - 1, (int) pos);
            const int i1 = juce::jmin (endS - 1, i0 + 1);
            const float fr = (float) (pos - std::floor (pos));
            float a = sl[i0] + (sl[i1] - sl[i0]) * fr;
            float b = sr[i0] + (sr[i1] - sr[i0]) * fr;

            const int toEdgeA = i0 - startS, toEdgeB = endS - 1 - i0;
            // loops wrap on their own seam; every other mode fades at the edges of its piece
            const float edge = loop == Loop ? 1.f : juce::jmin ((float) juce::jmin (toEdgeA, toEdgeB) / (float) fade, 1.f);

            if (useFilter) { a = filt[0].processSample (0, a); b = filt[1].processSample (0, b); }
            if (useDrive)  { a = std::tanh (a * driveGain) * driveNorm; b = std::tanh (b * driveGain) * driveNorm; }

            const float g = gain * env * edge;
            left[i]  += a * g * panL;
            right[i] += b * g * panR;

            // move on
            pos += dir * ratio;
            if (dir > 0 && pos >= (double) (endS - 1))
            {
                if (loop == Loop) pos = (double) startS + (pos - (double) (endS - 1));
                else if (loop == PingPong) { pos = 2.0 * (double) (endS - 1) - pos; dir = -1; }
                else { kill(); return; }
            }
            else if (dir < 0 && pos <= (double) startS)
            {
                if (loop == Loop) pos = (double) (endS - 1) - ((double) startS - pos);
                else if (loop == PingPong) { pos = 2.0 * (double) startS - pos; dir = 1; }
                else { kill(); return; }
            }
        }
    }

private:
    enum Stage { Attack, Decay, Sustain, Release };
    std::shared_ptr<const TrackData> data;
    double sampleRate = 44100.0, pos = 0.0, ratio = 1.0;
    int startS = 0, endS = 1, dir = 1, loop = OneShot;
    bool active = false, reverse = false, useFilter = false, useDrive = false;
    float gain = 1.f, panL = 1.f, panR = 1.f, driveGain = 1.f, driveNorm = 1.f;
    float env = 0.f, atkInc = 1.f, decCoef = 0.f, relCoef = 0.f, sus = 1.f;
    Stage stage = Attack;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filt;
};
} // namespace jd
