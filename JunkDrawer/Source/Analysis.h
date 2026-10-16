#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>

// Working out what note or key a sound is in.  Runs once when a sound loads (key) or when a region is picked (note); never on the audio thread.
namespace jd
{
struct PitchResult { bool found = false; float midi = 0.f; float clarity = 0.f; };          // midi 69 = A4, fractional
struct KeyResult   { bool found = false; int tonic = 0; bool minor = false; float confidence = 0.f; };

inline const char* noteName (int pc) { static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[((pc % 12) + 12) % 12]; }
inline juce::String noteText (float midi, bool withCents = true)
{
    const int n = juce::roundToInt (midi);
    const int cents = juce::roundToInt ((midi - (float) n) * 100.f);
    auto s = juce::String (noteName (n)) + juce::String (n / 12 - 1);
    if (withCents) s += " " + juce::String (cents > 0 ? "+" : "") + juce::String (cents) + "c";
    return s;
}
inline juce::String keyText (const KeyResult& k) { return k.found ? juce::String (noteName (k.tonic)) + (k.minor ? " minor" : " major") : juce::String ("No clear key"); }

// ---- Note ---------------------------------------------------------------------------------------------------
// YIN pitch tracking on the loudest few slices of [from, to).  Only answers when most of those slices agree.
inline PitchResult detectPitch (const juce::AudioBuffer<float>& a, double sr, int from, int to)
{
    PitchResult out;
    const int n = a.getNumSamples();
    from = juce::jlimit (0, n, from); to = juce::jlimit (from, n, to);
    const int step = juce::jmax (1, (int) std::lround (sr / 24000.0));          // work at about 24 kHz
    const double sr2 = sr / step;
    const int W = 1024, tauMin = juce::jmax (2, (int) (sr2 / 2200.0)), tauMax = juce::jmin (W, (int) (sr2 / 45.0));
    const int need = W + tauMax + 4;
    const int len = (to - from) / step;
    if (len < need) return out;

    std::vector<float> x ((size_t) len);
    const float* l = a.getReadPointer (0);
    const float* r = a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1));
    for (int i = 0; i < len; ++i)
    {
        float s = 0.f;
        for (int k = 0; k < step; ++k) { const int j = from + i * step + k; s += 0.5f * (l[j] + r[j]); }
        x[(size_t) i] = s / (float) step;
    }

    // loudest slices, spaced apart
    struct Slice { int pos; float rms; };
    std::vector<Slice> slices;
    for (int p = 0; p + need <= len; p += W / 2)
    {
        double e = 0.0;
        for (int i = 0; i < W; ++i) e += (double) x[(size_t) (p + i)] * x[(size_t) (p + i)];
        slices.push_back ({ p, (float) std::sqrt (e / W) });
    }
    if (slices.empty()) return out;
    std::sort (slices.begin(), slices.end(), [] (const Slice& u, const Slice& v) { return u.rms > v.rms; });
    const float top = slices.front().rms;
    if (top < 0.003f) return out;

    std::vector<float> midis, clar;
    std::vector<int> used;
    std::vector<float> d ((size_t) tauMax + 1), dn ((size_t) tauMax + 1);
    for (auto& s : slices)
    {
        if (midis.size() + 0 >= 6 || s.rms < 0.25f * top) break;
        bool close = false; for (int u : used) close = close || std::abs (u - s.pos) < W; if (close) continue;
        used.push_back (s.pos);
        const float* f = x.data() + s.pos;
        for (int t = 1; t <= tauMax; ++t)
        {
            double acc = 0.0;
            for (int i = 0; i < W; ++i) { const double df = (double) f[i] - f[i + t]; acc += df * df; }
            d[(size_t) t] = (float) acc;
        }
        double run = 0.0; dn[0] = 1.f;
        for (int t = 1; t <= tauMax; ++t) { run += d[(size_t) t]; dn[(size_t) t] = run > 0.0 ? (float) (d[(size_t) t] * t / run) : 1.f; }
        int tau = -1;
        for (int t = tauMin; t < tauMax; ++t)
            if (dn[(size_t) t] < 0.15f) { while (t + 1 < tauMax && dn[(size_t) (t + 1)] < dn[(size_t) t]) ++t; tau = t; break; }
        if (tau < 0) continue;
        float tf = (float) tau;
        if (tau > 1 && tau < tauMax)
        {
            const float a0 = dn[(size_t) (tau - 1)], b0 = dn[(size_t) tau], c0 = dn[(size_t) (tau + 1)];
            const float den = a0 - 2.f * b0 + c0;
            if (std::abs (den) > 1.0e-9f) tf += 0.5f * (a0 - c0) / den;
        }
        const float hz = (float) (sr2 / tf);
        midis.push_back (69.f + 12.f * std::log2 (hz / 440.f));
        clar.push_back (1.f - dn[(size_t) tau]);
    }
    if (midis.empty()) return out;

    auto sorted = midis; std::sort (sorted.begin(), sorted.end());
    const float med = sorted[sorted.size() / 2];
    float sum = 0.f, csum = 0.f; int agree = 0;
    for (size_t i = 0; i < midis.size(); ++i)
        if (std::abs (midis[i] - med) < 0.7f) { sum += midis[i]; csum += clar[i]; ++agree; }
    if (agree < 1 || agree * 2 < (int) midis.size()) return out;
    out.found = true; out.midi = sum / (float) agree; out.clarity = csum / (float) agree;
    return out;
}

// ---- Key ----------------------------------------------------------------------------------------------------
// Pitch-class energy from a few dozen spectra, compared with the Krumhansl-Kessler major and minor profiles.
inline KeyResult detectKey (const juce::AudioBuffer<float>& a, double sr)
{
    KeyResult out;
    const int n = a.getNumSamples();
    constexpr int order = 13, N = 1 << order;
    if (n < N * 2 || sr <= 0.0) return out;

    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1));
    const float* l = a.getReadPointer (0);
    const float* r = a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1));

    const int frames = juce::jmin (32, juce::jmax (1, (n - N) / (N / 2)));
    std::vector<double> chroma (12, 0.0);
    std::vector<float> rms ((size_t) frames);
    std::vector<int> pos ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        pos[(size_t) f] = frames == 1 ? 0 : (int) ((juce::int64) (n - N) * f / (frames - 1));
        double e = 0.0;
        for (int i = 0; i < N; ++i) { const float s = 0.5f * (l[pos[(size_t) f] + i] + r[pos[(size_t) f] + i]); e += (double) s * s; }
        rms[(size_t) f] = (float) std::sqrt (e / N);
    }
    auto sorted = rms; std::sort (sorted.begin(), sorted.end());
    const float floorRms = juce::jmax (0.002f, 0.2f * sorted.back());
    int used = 0;
    for (int f = 0; f < frames; ++f)
    {
        if (rms[(size_t) f] < floorRms) continue;
        std::fill (buf.begin(), buf.end(), 0.f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (l[pos[(size_t) f] + i] + r[pos[(size_t) f] + i]) * win[(size_t) i];
        fft.performRealOnlyForwardTransform (buf.data(), true);
        std::vector<float> mag ((size_t) N / 2);
        float mx = 0.f;
        for (int k = 0; k < N / 2; ++k) { mag[(size_t) k] = std::hypot (buf[(size_t) 2 * k], buf[(size_t) 2 * k + 1]); mx = juce::jmax (mx, mag[(size_t) k]); }
        if (mx <= 0.f) continue;
        for (int k = 2; k < N / 2 - 1; ++k)
        {
            const double hz = k * sr / N;
            if (hz < 80.0 || hz > 5000.0) continue;
            if (mag[(size_t) k] < 0.08f * mx || mag[(size_t) k] < mag[(size_t) k - 1] || mag[(size_t) k] < mag[(size_t) k + 1]) continue;
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            chroma[(size_t) pc] += std::log1p (20.0 * mag[(size_t) k] / mx) / (1.0 + hz / 1500.0);
        }
        ++used;
    }
    if (used == 0) return out;

    static const double maj[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double cm = 0.0; for (double c : chroma) cm += c; cm /= 12.0;
    if (cm <= 0.0) return out;
    auto corr = [&] (const double* prof, int tonic)
    {
        double pm = 0.0; for (int i = 0; i < 12; ++i) pm += prof[i]; pm /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = chroma[(size_t) ((i + tonic) % 12)] - cm, y = prof[i] - pm;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    struct Cand { double r; int tonic; bool minor; };
    std::vector<Cand> c;
    for (int t = 0; t < 12; ++t) { c.push_back ({ corr (maj, t), t, false }); c.push_back ({ corr (mnr, t), t, true }); }
    std::sort (c.begin(), c.end(), [] (const Cand& x, const Cand& y) { return x.r > y.r; });
    // the runner-up that is not the relative key (those two share every note, so they are not a real disagreement)
    double second = -1.0;
    for (size_t i = 1; i < c.size(); ++i)
    {
        const bool relative = c[i].minor != c[0].minor && ((c[0].minor ? c[0].tonic + 3 : c[0].tonic) % 12) == ((c[i].minor ? c[i].tonic + 3 : c[i].tonic) % 12);
        if (! relative) { second = c[i].r; break; }
    }
    const double conf = juce::jlimit (0.0, 1.0, (c[0].r - 0.45) / 0.35) * juce::jlimit (0.0, 1.0, (c[0].r - second) / 0.12);
    if (c[0].r < 0.6) return out;
    out.found = true; out.tonic = c[0].tonic; out.minor = c[0].minor; out.confidence = (float) conf;
    return out;
}

// ---- Matching -------------------------------------------------------------------------------------------------
// Project key: mode 0 = no key (any note), 1 = major, 2 = minor.
inline bool inKey (int pc, int root, int mode)
{
    if (mode == 0) return true;
    static const bool maj[] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
    static const bool mnr[] = { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
    const int i = (((pc - root) % 12) + 12) % 12;
    return mode == 1 ? maj[i] : mnr[i];
}

// The note of the project key nearest to a (fractional) midi note.
inline float nearestInKey (float midi, int root, int mode)
{
    float best = std::round (midi), bd = 1.0e9f;
    for (int nn = (int) std::floor (midi) - 2; nn <= (int) std::ceil (midi) + 2; ++nn)
        if (inKey (nn, root, mode) && std::abs ((float) nn - midi) < bd) { bd = std::abs ((float) nn - midi); best = (float) nn; }
    return best;
}

// Semitones (-6..+6) that bring a sound in key `det` into the project key.  Relative major and minor count as the same key.
// With no project mode, only the tonic is matched.
inline int matchShift (const KeyResult& det, int root, int mode)
{
    if (! det.found) return 0;
    int from = det.tonic, to = root;
    if (mode != 0) { if (det.minor) from += 3; if (mode == 2) to += 3; }
    int s = (((to - from) % 12) + 12) % 12;
    if (s > 6) s -= 12;
    return s;
}
} // namespace jd
