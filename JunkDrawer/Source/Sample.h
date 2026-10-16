#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Analysis.h"

// Sample data and the helpers that work on it (loading, hit detection, snapping).  Nothing here touches the audio thread.
namespace jd
{
constexpr int kRegions = 6;
constexpr double kMaxSeconds = 60.0;          // per-track cap, keeps memory predictable (60 s of stereo float at 48 kHz is 23 MB)

struct TrackData
{
    juce::AudioBuffer<float> audio;           // always stereo, already at the session sample rate
    double sampleRate = 44100.0;
    juce::String path, name;
    bool truncated = false;                   // the file was longer than kMaxSeconds
    KeyResult key;                            // worked out once, when the sound loads

    int length() const noexcept { return audio.getNumSamples(); }
    double seconds() const noexcept { return sampleRate > 0.0 ? length() / sampleRate : 0.0; }
};

inline void registerFormats (juce::AudioFormatManager& fm)
{
    fm.registerBasicFormats();                // WAV, AIFF, FLAC, Ogg and (when JUCE_USE_MP3AUDIOFORMAT is on) MP3
}

// Reads a file, folds it to stereo, resamples it to targetRate and caps it at kMaxSeconds.  Returns null and sets `error` on failure.
inline std::shared_ptr<TrackData> loadTrackData (const juce::File& file, double targetRate, juce::AudioFormatManager& fm, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) { error = "Could not read " + file.getFileName(); return nullptr; }

    auto td = std::make_shared<TrackData>();
    td->path = file.getFullPathName();
    td->name = file.getFileNameWithoutExtension();
    td->sampleRate = targetRate;

    const double ratio = reader->sampleRate / targetRate;                          // source samples per output sample
    const juce::int64 maxSrc = (juce::int64) std::floor (kMaxSeconds * reader->sampleRate);
    juce::int64 srcLen = reader->lengthInSamples;
    if (srcLen > maxSrc) { srcLen = maxSrc; td->truncated = true; }

    juce::AudioBuffer<float> src (2, (int) srcLen);
    src.clear();
    reader->read (&src, 0, (int) srcLen, 0, true, true);
    if (reader->numChannels == 1) src.copyFrom (1, 0, src, 0, 0, (int) srcLen);    // mono files play in both ears

    if (std::abs (ratio - 1.0) < 1.0e-9) { td->audio = std::move (src); td->key = detectKey (td->audio, td->sampleRate); return td; }

    const int outLen = (int) std::floor ((double) srcLen / ratio);
    td->audio.setSize (2, juce::jmax (1, outLen));
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator li;
        li.process (ratio, src.getReadPointer (ch), td->audio.getWritePointer (ch), td->audio.getNumSamples());
    }
    td->key = detectKey (td->audio, td->sampleRate);
    return td;
}

// The same sound at another sample rate (used by the exporter, which renders at the rate you pick).
inline std::shared_ptr<TrackData> resampledCopy (const TrackData& src, double targetRate)
{
    auto td = std::make_shared<TrackData>();
    td->path = src.path; td->name = src.name; td->truncated = src.truncated; td->key = src.key;
    td->sampleRate = targetRate;
    if (std::abs (src.sampleRate - targetRate) < 0.5) { td->audio = src.audio; td->sampleRate = src.sampleRate; return td; }
    const double ratio = src.sampleRate / targetRate;
    const int outLen = juce::jmax (1, (int) std::floor ((double) src.length() / ratio));
    td->audio.setSize (2, outLen);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator li;
        li.process (ratio, src.audio.getReadPointer (ch), td->audio.getWritePointer (ch), outLen);
    }
    return td;
}

// Positions (0..1) of the strongest hits, in order.  A hit is a jump in short-term energy well above what came just before it.
inline std::vector<float> detectOnsets (const juce::AudioBuffer<float>& a, double sr, int maxCount)
{
    std::vector<float> out;
    const int n = a.getNumSamples();
    const int hop = juce::jmax (1, (int) (sr * 0.005));
    const int frames = n / hop;
    if (frames < 8 || maxCount <= 0) return out;

    std::vector<float> e ((size_t) frames, 0.f);
    const float* l = a.getReadPointer (0);
    const float* r = a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1));
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i) { const double m = 0.5 * ((double) l[i] + (double) r[i]); s += m * m; }
        e[(size_t) f] = (float) std::sqrt (s / hop);
    }

    struct Cand { int frame; float score; };
    std::vector<Cand> cands;
    const int back = 6;                                                            // 30 ms of history
    for (int f = back; f < frames - 1; ++f)
    {
        float prev = 0.f;
        for (int k = 1; k <= back; ++k) prev += e[(size_t) (f - k)];
        prev /= (float) back;
        const float jump = e[(size_t) f] - prev;
        if (jump > 0.02f && e[(size_t) f] > prev * 2.f && e[(size_t) f] >= e[(size_t) (f - 1)] && e[(size_t) f] >= e[(size_t) (f + 1)])
            cands.push_back ({ f, jump });
    }
    std::sort (cands.begin(), cands.end(), [] (const Cand& x, const Cand& y) { return x.score > y.score; });

    const int minGap = (int) (0.08 * sr / hop);                                    // hits closer than 80 ms count once
    std::vector<int> picked;
    for (auto& c : cands)
    {
        bool ok = true;
        for (int p : picked) if (std::abs (p - c.frame) < minGap) { ok = false; break; }
        if (ok) picked.push_back (c.frame);
        if ((int) picked.size() >= maxCount) break;
    }
    std::sort (picked.begin(), picked.end());
    for (int p : picked) out.push_back ((float) (p * hop) / (float) n);
    return out;
}

// Nearest zero crossing (of the left+right mix) within windowSec of norm, or norm itself if there is none.
inline float snapToZero (const juce::AudioBuffer<float>& a, double sr, float norm, double windowSec = 0.005)
{
    const int n = a.getNumSamples();
    if (n < 4) return norm;
    const int c = juce::jlimit (1, n - 2, (int) std::lround ((double) norm * n));
    const int w = (int) (windowSec * sr);
    const float* l = a.getReadPointer (0);
    const float* r = a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1));
    auto mix = [&] (int i) { return l[i] + r[i]; };
    for (int d = 0; d <= w; ++d)
        for (int sgn : { -1, 1 })
        {
            const int i = c + sgn * d;
            if (i < 1 || i >= n - 1) continue;
            if ((mix (i) <= 0.f && mix (i + 1) > 0.f) || (mix (i) >= 0.f && mix (i + 1) < 0.f)) return (float) i / (float) n;
        }
    return norm;
}

// Nearest detected hit within windowSec of norm, or norm itself.
inline float snapToOnset (const std::vector<float>& onsets, double seconds, float norm, double windowSec = 0.03)
{
    float best = norm, bestD = (float) (windowSec / juce::jmax (0.001, seconds));
    for (float o : onsets)
        if (std::abs (o - norm) < bestD) { bestD = std::abs (o - norm); best = o; }
    return best;
}
} // namespace jd
