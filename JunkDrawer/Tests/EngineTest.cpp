// Headless checks of the sample engine: loading, resampling, hit detection, regions, voices and mute/solo.
// Usage: JunkEngineTest   (exit code 0 when every check passes)
#include "PluginEditor.h"

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void pump (int ms = 60)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

bool waitForTrack (JunkDrawerProcessor& p, int t, int ms = 5000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        pump (20);
        if (p.getTrack (t) != nullptr) return true;
    }
    return false;
}

// Four decaying noise bursts ("hits") at known positions, at the given rate and channel count.
juce::File makeWav (double rate, int channels, double seconds, const std::vector<double>& hitTimes, const juce::String& name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned) channels, 16, {}, 0));
    os.release();
    const int n = (int) (seconds * rate);
    juce::AudioBuffer<float> b (channels, n);
    juce::Random rnd (42);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
        {
            float v = 0.f;
            for (double h : hitTimes)
            {
                const double dt = (double) i / rate - h;
                if (dt >= 0.0) v += (rnd.nextFloat() * 2.f - 1.f) * 0.8f * (float) std::exp (-dt * 18.0);
            }
            b.setSample (ch, i, v);
        }
    w->writeFromAudioSampleBuffer (b, 0, n);
    return f;
}

float rms (const juce::AudioBuffer<float>& b)
{
    double s = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return (float) std::sqrt (s / (b.getNumChannels() * b.getNumSamples()));
}

// Runs the processor for `seconds` and returns everything it produced.
juce::AudioBuffer<float> run (JunkDrawerProcessor& p, double seconds, double rate)
{
    const int block = 256, total = (int) (seconds * rate);
    juce::AudioBuffer<float> out (2, total), blk (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        blk.clear();
        juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n);
        p.processBlock (view, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, view, c, 0, n);
    }
    return out;
}

void setP (JunkDrawerProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.state.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double rate = 48000.0;
    JunkDrawerProcessor proc;
    proc.prepareToPlay (rate, 256);
    setP (proc, "m_volume", 0.f);

    std::printf ("Loading\n");
    const std::vector<double> hits { 0.10, 0.60, 1.10, 1.60 };
    auto stereo44 = makeWav (44100.0, 2, 2.0, hits, "jd_test_stereo44.wav");
    proc.loadFile (0, stereo44);
    check (waitForTrack (proc, 0), "a 44.1 kHz stereo file loads");
    auto t0 = proc.getTrack (0);
    check (t0 != nullptr && std::abs (t0->seconds() - 2.0) < 0.01, "it is resampled to the session rate and keeps its length");
    check (t0 != nullptr && std::abs (t0->sampleRate - rate) < 0.5, "the stored rate is the session rate");

    auto mono = makeWav (22050.0, 1, 1.0, { 0.1 }, "jd_test_mono.wav");
    proc.loadFile (1, mono);
    check (waitForTrack (proc, 1), "a mono file loads");
    auto t1 = proc.getTrack (1);
    bool same = t1 != nullptr;
    if (same) for (int i = 0; i < t1->length(); i += 97) same = same && t1->audio.getSample (0, i) == t1->audio.getSample (1, i);
    check (same, "mono plays in both channels");

    juce::File bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("jd_not_audio.wav");
    bad.replaceWithText ("this is not audio");
    proc.loadFile (2, bad);
    pump (400);
    check (proc.getTrack (2) == nullptr && proc.takeLoadMessage().isNotEmpty(), "a file that is not audio is refused with a message");

    auto longFile = makeWav (8000.0, 1, 70.0, { 1.0 }, "jd_test_long.wav");
    proc.loadFile (3, longFile);
    check (waitForTrack (proc, 3, 8000), "a 70 second file loads");
    auto t3 = proc.getTrack (3);
    check (t3 != nullptr && t3->truncated && std::abs (t3->seconds() - 60.0) < 0.05, "it is cut to 60 seconds and flagged");
    check (proc.takeLoadMessage().contains ("60 seconds"), "and the editor is told");

    std::printf ("Regions\n");
    check (proc.state.getRawParameterValue ("t1_regions")->load() == 4.f, "a fresh load is cut into 4 equal regions");
    check (std::abs (proc.state.getRawParameterValue ("t1_r2_start")->load() - 0.25f) < 1.0e-4f, "region B starts at 25 percent");
    auto onsets = jd::detectOnsets (t0->audio, t0->sampleRate, 8);
    bool found = onsets.size() == 4;
    for (size_t i = 0; i < onsets.size() && found; ++i) found = std::abs (onsets[i] * (float) t0->seconds() - (float) hits[i]) < 0.02f;
    check (found, "hit detection finds all four hits within 20 ms");
    proc.sliceByHits (0, 4);
    check (std::abs (proc.state.getRawParameterValue ("t1_r1_start")->load()) < 1.0e-4f, "slicing by hits keeps sound before the first hit in region A");
    check (std::abs (proc.state.getRawParameterValue ("t1_r2_start")->load() * 2.0 - 0.10 - 0.0) < 0.2, "and region B begins at the first hit");
    const float zc = jd::snapToZero (t0->audio, rate, 0.3f);
    const int zi = juce::jlimit (1, t0->length() - 2, (int) std::lround (zc * t0->length()));
    const float m0 = t0->audio.getSample (0, zi) + t0->audio.getSample (1, zi), m1 = t0->audio.getSample (0, zi + 1) + t0->audio.getSample (1, zi + 1);
    check (std::abs (zc - 0.3f) * (float) t0->seconds() <= 0.0051f && (m0 * m1 <= 0.f || m0 == 0.f), "snap to zero moves less than 5 ms and lands on a zero crossing");

    std::printf ("Playing\n");
    proc.sliceEqual (0, 4);
    proc.auditionRegion (0, 0);
    auto o = run (proc, 0.6, rate);
    check (rms (o) > 0.01f, "auditioning region A makes sound");
    check (! proc.isAuditioning (0), "a one-shot stops by itself at the end of its piece");

    proc.auditionRegion (0, 0);
    const float full = rms (run (proc, 0.3, rate));
    setP (proc, "t1_r1_gain", -24.f);
    proc.auditionRegion (0, 0);
    const float quiet = rms (run (proc, 0.3, rate));
    check (quiet > 0.f && std::abs (quiet / full - 0.0631f) < 0.01f, "region gain of -24 dB is 24 dB quieter");
    setP (proc, "t1_r1_gain", 0.f);

    setP (proc, "t1_mute", 1.f);
    proc.auditionRegion (0, 0);
    check (rms (run (proc, 0.3, rate)) < 1.0e-5f, "a muted track is silent");
    setP (proc, "t1_mute", 0.f);

    setP (proc, "t2_solo", 1.f);
    proc.auditionRegion (0, 0);
    check (rms (run (proc, 0.3, rate)) < 1.0e-5f, "solo on another track silences this one");
    setP (proc, "t2_solo", 0.f);

    setP (proc, "t1_r1_loop", 1.f);                                    // Loop
    proc.auditionRegion (0, 0);
    run (proc, 1.5, rate);
    check (proc.isAuditioning (0), "a looping region keeps playing");
    proc.stopAudition (0);
    run (proc, 0.5, rate);
    check (! proc.isAuditioning (0), "and fades out when stopped");
    setP (proc, "t1_r1_loop", 0.f);

    setP (proc, "t1_r1_pitch", 12.f);
    proc.auditionRegion (0, 0);
    auto up = run (proc, 0.6, rate);
    setP (proc, "t1_r1_pitch", 0.f);
    proc.auditionRegion (0, 0);
    auto base = run (proc, 0.6, rate);
    int lastUp = 0, lastBase = 0;
    for (int i = up.getNumSamples() - 1; i >= 0 && ! lastUp; --i) if (std::abs (up.getSample (0, i)) > 1.0e-4f) lastUp = i;
    for (int i = base.getNumSamples() - 1; i >= 0 && ! lastBase; --i) if (std::abs (base.getSample (0, i)) > 1.0e-4f) lastBase = i;
    check (lastUp > 0 && std::abs ((double) lastUp * 2.0 - (double) lastBase) < rate * 0.02, "an octave up plays the piece in half the time");

    setP (proc, "t1_r1_cutoff", 300.f);
    proc.auditionRegion (0, 0);
    const float dark = rms (run (proc, 0.3, rate));
    setP (proc, "t1_r1_cutoff", 20000.f);
    proc.auditionRegion (0, 0);
    const float bright = rms (run (proc, 0.3, rate));
    check (dark < bright * 0.7f, "a low cutoff takes the top off the noise");

    setP (proc, "t1_r1_reverse", 1.f);
    proc.auditionRegion (0, 0);
    auto rev = run (proc, 0.6, rate);
    setP (proc, "t1_r1_reverse", 0.f);
    check (rms (rev) > 0.005f, "a reversed region plays");

    std::printf ("State\n");
    juce::MemoryBlock saved;
    proc.getStateInformation (saved);
    JunkDrawerProcessor proc2;
    proc2.prepareToPlay (rate, 256);
    proc2.setStateInformation (saved.getData(), (int) saved.getSize());
    check (waitForTrack (proc2, 0), "a saved project reloads its samples");
    check (std::abs (proc2.state.getRawParameterValue ("t1_r2_start")->load() - 0.25f) < 1.0e-4f, "and its regions");

    proc.clearTrack (0);
    check (proc.getTrack (0) == nullptr, "clearing a track empties it");

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
