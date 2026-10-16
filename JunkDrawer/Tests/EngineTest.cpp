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

    // ------------------------------------------------------------------------------------------------------------
    std::printf ("Sequencer\n");
    {
        long long s; double f;
        jd::locateStep (3.5, 0.5, s, f);
        check (s == 3 && std::abs (f - 0.5) < 1.0e-9, "straight timing: halfway through step 4");
        jd::locateStep (1.0, 0.75, s, f);
        check (s == 0 && std::abs (f - 2.0 / 3.0) < 1.0e-9, "with swing the first step runs long");
        jd::locateStep (1.5, 0.75, s, f);
        check (s == 1 && std::abs (f) < 1.0e-9, "and the second step starts three quarters of the way through the pair");
        check (jd::snapRate (0.8) == 1.0 && jd::snapRate (3.0) == 4.0 && jd::snapRate (0.3) == 0.25, "sync snaps the rate to the five allowed values");
    }

    // a 20 ms tone, so every trigger is a short burst we can find
    juce::File tone = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("jd_test_tone.wav");
    {
        tone.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (tone), rate, 2, 16, {}, 0));
        const int n = (int) (rate * 0.02);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, 0.6f * std::sin (i * 0.1f));
        w->writeFromAudioSampleBuffer (b, 0, n);
    }
    JunkDrawerProcessor sq;
    sq.prepareToPlay (rate, 256);
    setP (sq, "m_volume", 0.f);
    setP (sq, "seq_bpm", 120.f);                                  // 1/16 steps are then 6000 samples long at 48 kHz
    sq.loadFile (0, tone);
    check (waitForTrack (sq, 0), "the tone loads");
    sq.sliceEqual (0, 1);
    auto onlySteps = [&] (std::initializer_list<int> on)
    {
        for (int s = 0; s < jd::kSteps; ++s) setP (sq, jd::stepId (0, s, jd::LaneRegion), 0.f);
        for (int s : on) setP (sq, jd::stepId (0, s, jd::LaneRegion), 1.f);
    };
    auto win = [&] (const juce::AudioBuffer<float>& b, int a, int z)
    {
        juce::AudioBuffer<float> v (const_cast<float**> (b.getArrayOfReadPointers()), 2, a, z - a);
        return rms (v);
    };

    onlySteps ({ 0, 2 });
    check (rms (run (sq, 0.2, rate)) < 1.0e-6f, "nothing plays until the sequencer runs");
    setP (sq, "seq_run", 1.f);
    o = run (sq, 0.4, rate);
    check (win (o, 0, 600) > 0.05f && win (o, 1500, 11800) < 1.0e-5f, "step 1 plays at the start and then goes quiet");
    check (win (o, 11000, 11900) < 1.0e-5f && win (o, 12100, 12900) > 0.05f, "step 3 starts exactly 12000 samples in");

    setP (sq, "seq_run", 0.f);
    run (sq, 0.1, rate);
    setP (sq, "seq_run", 1.f);
    onlySteps ({ 0 });
    setP (sq, jd::stepId (0, 0, jd::LaneRatchet), 4.f);
    o = run (sq, 0.2, rate);
    check (win (o, 100, 900) > 0.05f && win (o, 1100, 1450) < 1.0e-5f && win (o, 1600, 2300) > 0.05f && win (o, 3100, 3800) > 0.05f && win (o, 4600, 5300) > 0.05f, "a ratchet of 4 plays four hits inside the step");
    setP (sq, jd::stepId (0, 0, jd::LaneRatchet), 1.f);

    setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f);
    onlySteps ({ 0, 1 });
    setP (sq, "m_swing", 0.75f);
    o = run (sq, 0.4, rate);
    check (win (o, 8000, 8950) < 1.0e-5f && win (o, 9100, 9800) > 0.05f, "swing pushes the second step of a pair later (9000 instead of 6000)");
    setP (sq, "m_swing", 0.5f);

    setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f);
    setP (sq, "m_rate", 2.f);
    o = run (sq, 0.4, rate);
    check (win (o, 2800, 2990) < 1.0e-5f && win (o, 3100, 3800) > 0.05f, "a rate of 2x makes steps half as long");
    setP (sq, "m_rate", 1.f);

    // lanes run on their own lengths: the volume lane has 2 steps while the region lane has 16
    setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f);
    for (int s = 0; s < jd::kSteps; ++s) setP (sq, jd::stepId (0, s, jd::LaneRegion), 1.f);
    setP (sq, jd::laneLenId (0, jd::LaneVolume), 2.f);
    setP (sq, jd::stepId (0, 0, jd::LaneVolume), 1.f);
    setP (sq, jd::stepId (0, 1, jd::LaneVolume), 0.f);
    o = run (sq, 0.6, rate);
    check (win (o, 100, 900) > 0.05f && win (o, 6100, 6900) < 1.0e-5f && win (o, 12100, 12900) > 0.05f && win (o, 18100, 18900) < 1.0e-5f, "a 2-step volume lane alternates loud and silent against a 16-step region lane");
    setP (sq, jd::laneLenId (0, jd::LaneVolume), 16.f);

    setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f);
    setP (sq, jd::laneLenId (0, jd::LaneRegion), 3.f);
    for (int s = 0; s < jd::kSteps; ++s) setP (sq, jd::stepId (0, s, jd::LaneRegion), s == 2 ? 1.f : 0.f);
    o = run (sq, 0.8, rate);
    check (win (o, 12100, 12900) > 0.05f && win (o, 18100, 18900) < 1.0e-5f && win (o, 30100, 30900) > 0.05f, "a 3-step region lane repeats its third step every 18000 samples");
    setP (sq, jd::laneLenId (0, jd::LaneRegion), 16.f);
    setP (sq, "seq_run", 0.f);
    check (rms (run (sq, 0.2, rate)) < 1.0e-5f, "stopping the sequencer silences it");
    check (sq.uiStep.load() == -1, "and the editor sees it stopped");

    proc.clearTrack (0);
    check (proc.getTrack (0) == nullptr, "clearing a track empties it");

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
