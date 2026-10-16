#pragma once
#include <juce_dsp/juce_dsp.h>

// The Master page effects: Filter, Delay, Reverb and Limiter, in that order.  Each one is skipped completely while it is switched off.
// Control values are read once per block, so everything here costs the same however many things are modulating it.
namespace jd
{
//==============================================================================
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f, g = 0.f, k = 1.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
    void set (float cutoff, float q, double sr)
    {
        g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.f, (float) sr * 0.45f, cutoff) / (float) sr);
        k = 1.f / q;
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.f; }
    // type: 0 low pass, 1 band, 2 high pass, 3 notch
    float process (float v0, int type)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2;
        switch (type)
        {
            case 0:  return v2;
            case 1:  return k * v1;
            case 2:  return v0 - k * v1 - v2;
            default: return v0 - k * v1;
        }
    }
};

struct FxFilter
{
    void prepare (double sr) { rate = sr; reset(); }
    void reset() { for (auto& c : s) for (auto& f : c) f.reset(); }
    void set (int t, float cutoff, float reso, float driveDb, float m, int slope)
    {
        type = t; mix = m; stages = slope == 1 ? 2 : 1;
        for (auto& c : s) for (auto& f : c) f.set (cutoff, 0.707f + reso * 9.f, rate);
        drive = juce::Decibels::decibelsToGain (driveDb);
        useDrive = driveDb > 0.05f;
        norm = useDrive ? 1.f / std::tanh (drive) : 1.f;
    }
    void process (float* l, float* r, int n)
    {
        float* ch[2] = { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float dry = ch[c][i];
                float x = useDrive ? std::tanh (dry * drive) * norm : dry;
                for (int st = 0; st < stages; ++st) x = s[(size_t) c][(size_t) st].process (x, type);
                ch[c][i] = dry + (x - dry) * mix;
            }
    }
    double rate = 44100.0;
    int type = 0, stages = 1;
    float mix = 1.f, drive = 1.f, norm = 1.f;
    bool useDrive = false;
    std::array<std::array<Svf, 2>, 2> s;
};

//==============================================================================
struct FxDelay
{
    void prepare (double sr)
    {
        rate = sr;
        size = (int) (sr * 4.3) + 8;
        for (auto& b : buf) b.assign ((size_t) size, 0.f);
        reset();
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f); w = 0; lp[0] = lp[1] = 0.f; cur = target; }

    // mode 0 free, 1 sync, 2 dotted, 3 triplet.  In the synced modes the Time knob picks the nearest note length at the current tempo.
    void set (int mode, float timeMs, double bpm, float fb, float toneHz, float m, bool pingPong)
    {
        double ms = timeMs;
        if (mode > 0)
        {
            const double beatMs = 60000.0 / juce::jmax (20.0, bpm);
            static const double divs[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
            double best = divs[0] * beatMs, bd = 1.0e9;
            for (double d : divs) { const double e = std::abs (std::log (timeMs / (d * beatMs))); if (e < bd) { bd = e; best = d * beatMs; } }
            ms = best * (mode == 2 ? 1.5 : (mode == 3 ? 2.0 / 3.0 : 1.0));
        }
        target = (float) juce::jlimit (1.0, (double) (size - 4) / rate * 1000.0, ms) * 0.001f * (float) rate;
        feedback = fb; mix = m; ping = pingPong;
        lpCoef = 1.f - std::exp (-2.f * juce::MathConstants<float>::pi * juce::jlimit (100.f, 18000.f, toneHz) / (float) rate);
    }

    void process (float* l, float* r, int n)
    {
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            cur += (target - cur) * 0.0008f;                              // glide to a new time instead of jumping
            float rp = (float) w - cur;
            if (rp < 0.f) rp += (float) size;
            const int i0 = (int) rp, i1 = (i0 + 1) % size;
            const float fr = rp - (float) i0;
            float out[2];
            for (int c = 0; c < 2; ++c) out[c] = buf[(size_t) c][(size_t) i0] + (buf[(size_t) c][(size_t) i1] - buf[(size_t) c][(size_t) i0]) * fr;
            for (int c = 0; c < 2; ++c) lp[c] += (out[c] - lp[c]) * lpCoef;
            const float inL = ch[0][i], inR = ch[1][i];
            if (ping)
            {
                const float mono = 0.5f * (inL + inR);
                buf[0][(size_t) w] = mono + lp[1] * feedback;
                buf[1][(size_t) w] = lp[0] * feedback;
            }
            else
            {
                buf[0][(size_t) w] = inL + lp[0] * feedback;
                buf[1][(size_t) w] = inR + lp[1] * feedback;
            }
            w = (w + 1) % size;
            ch[0][i] = inL + out[0] * mix;
            ch[1][i] = inR + out[1] * mix;
        }
    }
    double rate = 44100.0;
    int size = 1, w = 0;
    float cur = 0.f, target = 1000.f, feedback = 0.f, mix = 0.f, lpCoef = 0.2f;
    bool ping = false;
    float lp[2] = { 0.f, 0.f };
    std::array<std::vector<float>, 2> buf;
};

//==============================================================================
struct FxReverb
{
    void prepare (double sr)
    {
        rate = sr;
        verb.setSampleRate (sr);
        verb.reset();
        pre = (int) (sr * 0.045) + 4;
        for (auto& b : preBuf) b.assign ((size_t) pre, 0.f);
        pw = 0;
    }
    void reset() { verb.reset(); for (auto& b : preBuf) std::fill (b.begin(), b.end(), 0.f); }

    // mode 0 room, 1 hall, 2 plate, 3 spring
    void set (int mode, float size, float decayMs, float damping, float m, int preIndex)
    {
        static const float scale[] = { 0.7f, 1.0f, 0.85f, 0.6f }, damp[] = { 0.15f, 0.f, -0.2f, 0.3f };
        const float decayNorm = juce::jlimit (0.f, 1.f, std::log (decayMs / 200.f) / std::log (50.f));
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit (0.f, 1.f, (0.5f * size + 0.5f * decayNorm) * scale[juce::jlimit (0, 3, mode)]);
        p.damping = juce::jlimit (0.f, 1.f, damping + damp[juce::jlimit (0, 3, mode)]);
        p.wetLevel = 0.33f; p.dryLevel = 0.f; p.width = 1.f; p.freezeMode = 0.f;
        verb.setParameters (p);
        mix = m;
        preSamples = (int) (rate * (preIndex == 1 ? 0.02 : (preIndex == 2 ? 0.04 : 0.0)));
    }

    void process (float* l, float* r, int n)
    {
        std::array<float, 256> tl, tr;
        for (int pos = 0; pos < n; pos += 256)
        {
            const int m = juce::jmin (256, n - pos);
            float* a = l + pos; float* b = r + pos;
            for (int i = 0; i < m; ++i)
            {
                float x = a[i], y = b[i];
                if (preSamples > 0)
                {
                    const int rp = (pw - preSamples + pre) % pre;
                    tl[(size_t) i] = preBuf[0][(size_t) rp]; tr[(size_t) i] = preBuf[1][(size_t) rp];
                    preBuf[0][(size_t) pw] = x; preBuf[1][(size_t) pw] = y;
                    pw = (pw + 1) % pre;
                }
                else { tl[(size_t) i] = x; tr[(size_t) i] = y; }
            }
            verb.processStereo (tl.data(), tr.data(), m);
            for (int i = 0; i < m; ++i) { a[i] = a[i] * (1.f - mix * 0.5f) + tl[(size_t) i] * mix; b[i] = b[i] * (1.f - mix * 0.5f) + tr[(size_t) i] * mix; }
        }
    }
    double rate = 44100.0;
    juce::Reverb verb;
    int pre = 1, pw = 0, preSamples = 0;
    float mix = 0.f;
    std::array<std::vector<float>, 2> preBuf;
};

//==============================================================================
struct FxLimiter
{
    void prepare (double sr)
    {
        rate = sr;
        size = (int) (sr * 0.006) + 4;
        for (auto& b : buf) b.assign ((size_t) size, 0.f);
        reset();
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f); w = 0; gain = 1.f; }

    void set (bool hardMode, float ceilingDb, float releaseMs, float inputDb, float m, int lookIndex)
    {
        hard = hardMode; ceiling = juce::Decibels::decibelsToGain (ceilingDb); inGain = juce::Decibels::decibelsToGain (inputDb); mix = m;
        look = lookIndex == 1 ? (int) (rate * 0.001) : (lookIndex == 2 ? (int) (rate * 0.005) : 0);
        relCoef = 1.f - std::exp (-1.f / juce::jmax (1.f, releaseMs * 0.001f * (float) rate));
        atkCoef = look > 0 ? 1.f - std::exp (-3.f / (float) look) : 1.f;                   // reaches the target before the delayed audio arrives
    }

    void process (float* l, float* r, int n)
    {
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float x0 = ch[0][i] * inGain, x1 = ch[1][i] * inGain;
            const float peak = juce::jmax (std::abs (x0), std::abs (x1));
            const float target = peak > ceiling ? ceiling / peak : 1.f;
            if (target < gain) gain += (target - gain) * atkCoef; else gain += (1.f - gain) * relCoef;
            buf[0][(size_t) w] = x0; buf[1][(size_t) w] = x1;
            const int rp = (w - look + size) % size;
            w = (w + 1) % size;
            for (int c = 0; c < 2; ++c)
            {
                float y = buf[(size_t) c][(size_t) rp] * gain;
                if (hard) y = juce::jlimit (-ceiling, ceiling, y);
                else
                {
                    const float knee = 0.7f * ceiling, a = std::abs (y);
                    if (a > knee) y = (y < 0.f ? -1.f : 1.f) * (knee + (ceiling - knee) * std::tanh ((a - knee) / (ceiling - knee)));
                }
                const float dry = buf[(size_t) c][(size_t) rp] / juce::jmax (1.0e-6f, inGain);
                ch[c][i] = dry + (y - dry) * mix;
            }
        }
    }
    double rate = 44100.0;
    int size = 1, w = 0, look = 0;
    float ceiling = 1.f, inGain = 1.f, mix = 1.f, gain = 1.f, atkCoef = 1.f, relCoef = 0.001f;
    bool hard = false;
    std::array<std::vector<float>, 2> buf;
};
} // namespace jd
