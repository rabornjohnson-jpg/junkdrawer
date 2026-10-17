#include <set>
// Headless checks of the sample engine: loading, resampling, hit detection, regions, voices and mute/solo.
// Usage: JunkEngineTest   (exit code 0 when every check passes)
#include "PluginEditor.h"
#include "Exporter.h"

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

void fxOff (JunkDrawerProcessor& p)
{
    for (auto id : { "fx_filter_on", "fx_delay_on", "fx_reverb_on", "fx_lim_on" }) setP (p, id, 0.f);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double rate = 48000.0;
    JunkDrawerProcessor proc;
    proc.prepareToPlay (rate, 256);
    setP (proc, "m_volume", 0.f);
    fxOff (proc);

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
        check (std::abs (jd::stepBeats (2, 1) - 0.375) < 1e-9 && std::abs (jd::stepBeats (1, 2) - 1.0 / 3.0) < 1e-9 && jd::stepBeats (2) == 0.25, "dotted steps run 1.5 times longer and triplets 2/3 as long");
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
    fxOff (sq);
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

    // chance and timing
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        restart();
        onlySteps ({ 2 });
        setP (sq, jd::stepId (0, 2, jd::LaneTime), 0.5f);
        o = run (sq, 0.4, rate);
        check (win (o, 12100, 14800) < 1.0e-5f && win (o, 15100, 15900) > 0.05f, "a step pushed late by half a step starts 3000 samples later");
        setP (sq, jd::stepId (0, 2, jd::LaneTime), -0.5f);
        restart();
        o = run (sq, 0.4, rate);
        check (win (o, 9100, 9900) > 0.05f && win (o, 12100, 12900) < 1.0e-5f, "a step pushed early starts 3000 samples sooner");
        setP (sq, jd::stepId (0, 2, jd::LaneTime), 0.f);
        restart();
        o = run (sq, 0.4, rate);
        check (win (o, 12100, 12900) > 0.05f && win (o, 9100, 9900) < 1.0e-5f, "with timing back at zero it is on the grid again");

        onlySteps ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
        for (int k = 0; k < jd::kSteps; ++k) setP (sq, jd::stepId (0, k, jd::LaneProb), 0.f);
        restart();
        o = run (sq, 1.0, rate);
        check (rms (o) < 1.0e-6f, "chance at zero plays nothing");
        for (int k = 0; k < jd::kSteps; ++k) setP (sq, jd::stepId (0, k, jd::LaneProb), 0.5f);
        restart();
        o = run (sq, 4.0, rate);                                              // 32 steps
        int played = 0;
        for (int k = 0; k < 32; ++k) played += win (o, k * 6000 + 100, k * 6000 + 900) > 0.05f ? 1 : 0;
        check (played >= 6 && played <= 26, "chance at one half plays some steps and skips others");
        restart();
        auto o2 = run (sq, 4.0, rate);
        bool same = true;
        for (int k = 0; k < 32; ++k) same = same && (win (o, k * 6000 + 100, k * 6000 + 900) > 0.05f) == (win (o2, k * 6000 + 100, k * 6000 + 900) > 0.05f);
        check (same, "the dice are the same on every run");
        for (int k = 0; k < jd::kSteps; ++k) setP (sq, jd::stepId (0, k, jd::LaneProb), 1.f);
        restart();
        o = run (sq, 1.0, rate);
        check (win (o, 100, 900) > 0.05f && win (o, 6100, 6900) > 0.05f, "chance back at full plays every step");
        onlySteps ({ 0, 2 });
        setP (sq, "seq_run", 0.f);
    }

    // region order
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        onlySteps ({ 0, 2 });
        for (int mode = 0; mode < 4; ++mode)
        {
            setP (sq, jd::trackId (0, "order"), (float) mode);
            restart();
            o = run (sq, 0.4, rate);
            check (win (o, 100, 900) > 0.05f && win (o, 6100, 6900) < 1.0e-5f && win (o, 12100, 12900) > 0.05f, "region order " + juce::String (mode) + " keeps the written steps on the grid");
        }
        setP (sq, jd::trackId (0, "order"), 0.f);
        setP (sq, "seq_run", 0.f);
    }

    // pan lane
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        onlySteps ({ 0 });
        setP (sq, jd::stepId (0, 0, jd::LanePan), -1.f);
        restart();
        o = run (sq, 0.2, rate);
        auto side = [&] (int ch) { juce::AudioBuffer<float> v (const_cast<float**> (o.getArrayOfReadPointers()) + ch, 1, 100, 800); return rms (v); };
        check (side (0) > 0.02f && side (1) < side (0) * 0.2f, "a step panned hard left is heard on the left only");
        setP (sq, jd::stepId (0, 0, jd::LanePan), 1.f);
        restart();
        o = run (sq, 0.2, rate);
        check (side (1) > 0.02f && side (0) < side (1) * 0.2f, "a step panned hard right is heard on the right only");
        setP (sq, jd::stepId (0, 0, jd::LanePan), 0.f);
        onlySteps ({ 0, 2 });
        setP (sq, "seq_run", 0.f);
    }

    // polymeter: each lane loops at its own length
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        onlySteps ({ 0, 1, 2, 3, 4, 5, 6, 7 });
        setP (sq, jd::laneLenId (0, jd::LaneVolume), 3.f);
        setP (sq, jd::stepId (0, 0, jd::LaneVolume), 1.f);
        setP (sq, jd::stepId (0, 1, jd::LaneVolume), 0.2f);
        setP (sq, jd::stepId (0, 2, jd::LaneVolume), 0.2f);
        restart();
        o = run (sq, 0.8, rate);                                              // steps are 6000 samples at this tempo
        check (win (o, 100, 900) > win (o, 6100, 6900) * 2.f && win (o, 18100, 18900) > win (o, 24100, 24900) * 2.f && win (o, 18100, 18900) > win (o, 12100, 12900) * 2.f, "a three-step volume lane loops under an eight-step region lane");
        setP (sq, jd::laneLenId (0, jd::LaneVolume), 16.f);
        setP (sq, jd::stepId (0, 1, jd::LaneVolume), 1.f);
        setP (sq, jd::stepId (0, 2, jd::LaneVolume), 1.f);
        setP (sq, "seq_run", 0.f);
        onlySteps ({ 0, 2 });
    }

    // region morph
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        sq.sliceEqual (0, 2);
        onlySteps ({ 0 });
        setP (sq, jd::trackId (0, "morph"), 0.f);
        restart();
        o = run (sq, 0.2, rate);
        juce::AudioBuffer<float> plain; plain.makeCopyOf (o);
        setP (sq, jd::trackId (0, "morph"), 0.5f);
        restart();
        o = run (sq, 0.2, rate);
        check (win (o, 100, 900) > 0.01f, "a half morph still plays the step");
        setP (sq, jd::trackId (0, "morph"), 1.f);
        restart();
        o = run (sq, 0.2, rate);
        float diff = 0.f;
        for (int i = 100; i < 900; ++i) diff = juce::jmax (diff, std::abs (o.getSample (0, i) - plain.getSample (0, i)));
        check (win (o, 100, 900) > 0.01f && diff > 0.01f, "a full morph plays the neighboring region instead");
        setP (sq, jd::trackId (0, "morph"), 0.f);
        setP (sq, "seq_run", 0.f);
        sq.sliceEqual (0, 1);
        onlySteps ({ 0, 2 });
    }

    // ratchet shape
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        onlySteps ({ 0 });
        setP (sq, jd::stepId (0, 0, jd::LaneRatchet), 4.f);
        setP (sq, jd::trackId (0, "rshape"), 1.f);                            // fade out
        restart();
        o = run (sq, 0.2, rate);
        check (win (o, 100, 900) > win (o, 4600, 5300) * 1.8f && win (o, 4600, 5300) > 0.01f, "a fading ratchet gets quieter with each repeat");
        setP (sq, jd::trackId (0, "rshape"), 2.f);                            // swell
        restart();
        o = run (sq, 0.2, rate);
        check (win (o, 4600, 5300) > win (o, 100, 900) * 1.8f, "a swelling ratchet gets louder with each repeat");
        setP (sq, jd::trackId (0, "rshape"), 0.f);
        setP (sq, jd::stepId (0, 0, jd::LaneRatchet), 1.f);
        onlySteps ({ 0, 2 });
        setP (sq, "seq_run", 0.f);
    }

    // loop conditions (the Region lane is 16 steps, so a pass is 96000 samples at 120 BPM)
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        onlySteps ({ 0 });
        setP (sq, jd::trackId (0, "cond"), 1.f);                              // every 2nd loop
        restart();
        o = run (sq, 4.0, rate);
        check (win (o, 100, 900) > 0.05f && win (o, 96100, 96900) < 1.0e-5f, "every 2nd loop plays the first pass and skips the second");
        setP (sq, jd::trackId (0, "cond"), 4.f);                              // first loop only
        restart();
        o = run (sq, 4.0, rate);
        check (win (o, 100, 900) > 0.05f && win (o, 96100, 96900) < 1.0e-5f, "first loop only skips the second pass");
        setP (sq, jd::trackId (0, "cond"), 0.f);
        restart();
        o = run (sq, 4.0, rate);
        check (win (o, 100, 900) > 0.05f && win (o, 96100, 96900) > 0.05f, "every loop plays every pass");
        onlySteps ({ 0, 2 });
        setP (sq, "seq_run", 0.f);
    }

    // patterns and the chain
    {
        auto restart = [&] { setP (sq, "seq_run", 0.f); run (sq, 0.1, rate); setP (sq, "seq_run", 1.f); };
        setP (sq, "seq_run", 0.f);
        sq.setEditPattern (0); onlySteps ({ 0 });
        sq.setEditPattern (1);
        check (sq.editPattern() == 1 && juce::roundToInt (sq.state.getRawParameterValue (jd::stepId (0, 0, jd::LaneRegion))->load()) == 0, "a new pattern starts empty");
        onlySteps ({ 2 });
        sq.setEditPattern (0);
        check (juce::roundToInt (sq.state.getRawParameterValue (jd::stepId (0, 0, jd::LaneRegion))->load()) == 1 && juce::roundToInt (sq.state.getRawParameterValue (jd::stepId (0, 2, jd::LaneRegion))->load()) == 0, "going back brings pattern A back as it was");
        sq.setEditPattern (1);
        check (juce::roundToInt (sq.state.getRawParameterValue (jd::stepId (0, 2, jd::LaneRegion))->load()) == 1, "and pattern B too");
        sq.setEditPattern (0);

        sq.setChainSlot (0, 0, 1); sq.setChainSlot (1, 1, 1);
        check (sq.chainLength() == 2 && sq.chainPattern (1) == 1 && sq.chainBars (0) == 1, "the chain holds slots");
        setP (sq, "chain_on", 1.f);
        restart();
        o = run (sq, 4.0, rate);                                              // two bars at 120 BPM
        check (win (o, 100, 900) > 0.05f && win (o, 12100, 12900) < 1.0e-5f, "bar 1 plays pattern A (step 1 only)");
        check (win (o, 96100, 96900) < 1.0e-5f && win (o, 108100, 108900) > 0.05f, "bar 2 plays pattern B from its own first step (step 3 only)");

        setP (sq, "chain_on", 0.f);
        restart();
        o = run (sq, 0.4, rate);
        check (win (o, 100, 900) > 0.05f && win (o, 12100, 12900) < 1.0e-5f, "with the chain off the pattern being edited plays");

        sq.removeChainSlot (0);
        check (sq.chainLength() == 1 && sq.chainPattern (0) == 1, "a slot can be removed");
        sq.setChainSlot (0, 0, 2);
        check (sq.chainBars (0) == 2, "and changed");

        sq.copyPatternTo (3);
        sq.setEditPattern (3);
        check (juce::roundToInt (sq.state.getRawParameterValue (jd::stepId (0, 0, jd::LaneRegion))->load()) == 1, "a pattern copies to another slot");
        sq.clearPattern();
        check (juce::roundToInt (sq.state.getRawParameterValue (jd::stepId (0, 0, jd::LaneRegion))->load()) == 0, "and a pattern can be cleared");
        sq.setEditPattern (1);

        juce::MemoryBlock mb;
        sq.getStateInformation (mb);
        JunkDrawerProcessor back;
        back.prepareToPlay (rate, 256);
        back.setStateInformation (mb.getData(), (int) mb.getSize());
        check (back.editPattern() == 1 && back.chainLength() == 1 && back.chainBars (0) == 2, "the edited pattern and the chain are saved with the project");
        back.setEditPattern (0);
        check (juce::roundToInt (back.state.getRawParameterValue (jd::stepId (0, 0, jd::LaneRegion))->load()) == 1, "and the other patterns come back too");
        sq.setEditPattern (0);
        sq.removeChainSlot (0);
        setP (sq, "seq_run", 0.f);
    }

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

    // ------------------------------------------------------------------------------------------------------------
    std::printf ("Effects\n");
    {
        JunkDrawerProcessor fp;
        fp.prepareToPlay (rate, 256);
        setP (fp, "m_volume", 0.f);
        fxOff (fp);
        fp.loadFile (0, tone);
        check (waitForTrack (fp, 0), "the tone loads for the effects tests");
        fp.sliceEqual (0, 1);
        for (int s = 0; s < jd::kSteps; ++s) setP (fp, jd::stepId (0, s, jd::LaneRegion), s == 0 ? 1.f : 0.f);
        setP (fp, "seq_bpm", 120.f);
        auto hit = [&] (double secs)
        {
            setP (fp, "seq_run", 0.f); run (fp, 0.2, rate); setP (fp, "seq_run", 1.f);
            return run (fp, secs, rate);
        };
        auto dry = hit (0.5);
        const float dryRms = win (dry, 0, 2000);

        setP (fp, "fx_delay_on", 1.f); setP (fp, "fx_delay_mode", 0.f); setP (fp, "fx_delay_time", 100.f);
        setP (fp, "fx_delay_fb", 0.f); setP (fp, "fx_delay_mix", 1.f); setP (fp, "fx_delay_style", 0.f); setP (fp, "fx_delay_tone", 16000.f);
        auto d = hit (0.5);
        check (win (d, 4900, 6000) > 0.05f && win (d, 2000, 4500) < 0.01f, "a 100 ms delay repeats the hit 4800 samples later");
        setP (fp, "fx_delay_on", 0.f);

        setP (fp, "fx_reverb_on", 1.f); setP (fp, "fx_reverb_mix", 1.f); setP (fp, "fx_reverb_size", 0.8f); setP (fp, "fx_reverb_pre", 0.f);
        auto v = hit (0.6);
        check (win (v, 3000, 12000) > 0.003f && win (v, 3000, 12000) < dryRms, "the reverb adds a tail after the hit");
        setP (fp, "fx_reverb_on", 0.f);

        setP (fp, "fx_filter_on", 1.f); setP (fp, "fx_filter_type", 0.f); setP (fp, "fx_filter_cut", 200.f); setP (fp, "fx_filter_mix", 1.f); setP (fp, "fx_filter_drive", 0.f);
        auto f = hit (0.3);
        check (win (f, 0, 900) < dryRms * 0.5f, "a low pass at 200 Hz takes a 760 Hz tone down");
        setP (fp, "fx_filter_type", 2.f); setP (fp, "fx_filter_cut", 4000.f);
        f = hit (0.3);
        check (win (f, 0, 900) < dryRms * 0.5f, "and a high pass at 4 kHz does too");
        setP (fp, "fx_filter_on", 0.f);

        setP (fp, "m_width", 0.f);
        {
            fp.loadFile (1, tone);
            check (waitForTrack (fp, 1), "a second tone loads");
            fp.sliceEqual (1, 1);
            setP (fp, "t2_pan", -1.f);
            for (int s = 0; s < jd::kSteps; ++s) setP (fp, jd::stepId (1, s, jd::LaneRegion), s == 0 ? 1.f : 0.f);
            setP (fp, "t1_mute", 1.f);
            auto w0 = hit (0.2);
            float diff = 0.f;
            for (int i = 0; i < 900; ++i) diff = juce::jmax (diff, std::abs (w0.getSample (0, i) - w0.getSample (1, i)));
            check (diff < 1.0e-4f && win (w0, 0, 900) > 0.05f, "width 0 makes the output mono");
            setP (fp, "t1_mute", 0.f); setP (fp, "t2_pan", 0.f); setP (fp, "m_width", 1.f);
            setP (fp, "t2_mute", 1.f);
        }

        setP (fp, "m_volume", 6.f);
        setP (fp, "fx_lim_on", 1.f); setP (fp, "fx_lim_mode", 1.f); setP (fp, "fx_lim_ceiling", -12.f); setP (fp, "fx_lim_input", 0.f); setP (fp, "fx_lim_mix", 1.f); setP (fp, "fx_lim_look", 2.f);
        auto lim = hit (0.3);
        float peak = 0.f;
        for (int i = 0; i < lim.getNumSamples(); ++i) peak = juce::jmax (peak, std::abs (lim.getSample (0, i)));
        check (peak <= juce::Decibels::decibelsToGain (-12.f) + 0.002f && peak > 0.2f, "the limiter holds the output at its -12 dB ceiling");
        setP (fp, "m_volume", 0.f);
        setP (fp, "fx_lim_on", 0.f);
        auto bypass = hit (0.3);
        check (std::abs (win (bypass, 0, 2000) - dryRms) < 0.002f, "with every effect off the hit comes through untouched");

        const double t0c = (double) juce::Time::getMillisecondCounter();
        setP (fp, "fx_filter_on", 1.f); setP (fp, "fx_delay_on", 1.f); setP (fp, "fx_reverb_on", 1.f); setP (fp, "fx_lim_on", 1.f);
        setP (fp, "fx_filter_cut", 3000.f); setP (fp, "fx_filter_type", 0.f);
        run (fp, 10.0, rate);
        const double ms = (double) juce::Time::getMillisecondCounter() - t0c;
        std::printf ("  note  10 s of audio with every effect on took %.0f ms (%.1f percent of one core)\n", ms, ms / 100.0);
        check (ms < 3000.0, "all four effects together run far faster than real time");
    }

    // ------------------------------------------------------------------------------------------------------------
    std::printf ("LFOs\n");
    {
        check (std::abs (jd::lfoShape (0, 0.25, 0) - 1.f) < 1.0e-5f && std::abs (jd::lfoShape (0, 0.75, 0) + 1.f) < 1.0e-5f, "a sine peaks a quarter of the way round");
        check (std::abs (jd::lfoShape (1, 0.25, 0) - 1.f) < 1.0e-5f && std::abs (jd::lfoShape (1, 0.75, 0) + 1.f) < 1.0e-5f, "a triangle does the same");
        check (jd::lfoShape (3, 0.2, 0) == 1.f && jd::lfoShape (3, 0.7, 0) == -1.f, "a square flips at the half");
        check (jd::lfoShape (4, 3.1, 7) == jd::lfoShape (4, 3.9, 7) && jd::lfoShape (4, 3.1, 7) != jd::lfoShape (4, 4.1, 7), "sample and hold holds for a whole cycle and then picks a new value");
        bool inRange = true;
        for (int s = 0; s < 7; ++s) for (double ph = 0.0; ph < 40.0; ph += 0.037) { const float x = jd::lfoShape (s, ph, 3); inRange = inRange && x >= -1.001f && x <= 1.001f; }
        check (inRange, "every shape stays between -1 and +1");

        JunkDrawerProcessor lp;
        lp.prepareToPlay (rate, 256);
        setP (lp, "m_volume", 0.f);
        fxOff (lp);
        auto blocks = [&] (JunkDrawerProcessor& p, double secs, std::function<void (int)> each)
        {
            juce::AudioBuffer<float> b (2, 256);
            juce::MidiBuffer mm;
            const int count = (int) (secs * rate / 256.0);
            for (int i = 0; i < count; ++i) { b.clear(); p.processBlock (b, mm); each (i); }
        };
        auto crossings = [&] (JunkDrawerProcessor& p, int lfo, double secs)
        {
            int n = 0; float prev = 0.f;
            blocks (p, secs, [&] (int) { const float x = p.lfoValue (lfo); if ((x > 0.f) != (prev > 0.f) && std::abs (prev) > 1.0e-4f) ++n; prev = x; });
            return n;
        };

        setP (lp, "l1_shape", 0.f); setP (lp, "l1_rate", 4.f); setP (lp, "l1_sync", 0.f); setP (lp, "l1_depth", 1.f); setP (lp, "l1_polar", 1.f); setP (lp, "l1_global", 1.f);
        const int free4 = crossings (lp, 0, 2.0);
        check (free4 >= 14 && free4 <= 17, "a free LFO at 4 Hz crosses zero about 16 times in 2 seconds");

        setP (lp, "l1_sync", 1.f); setP (lp, "l1_div", 2.f);              // 1/4 note
        setP (lp, "seq_bpm", 120.f);
        const int synced = crossings (lp, 0, 2.0);
        check (synced >= 6 && synced <= 9, "synced to 1/4 at 120 BPM it runs at 2 Hz (about 8 crossings in 2 seconds)");

        setP (lp, "l1_sync", 0.f); setP (lp, "l1_polar", 0.f); setP (lp, "l1_depth", 0.5f);
        float lo = 9.f, hi = -9.f;
        blocks (lp, 1.0, [&] (int) { lo = juce::jmin (lo, lp.lfoValue (0)); hi = juce::jmax (hi, lp.lfoValue (0)); });
        check (lo >= -0.001f && hi <= 0.501f && hi > 0.45f, "unipolar with depth 50 percent swings between 0 and 0.5");

        setP (lp, "l1_polar", 1.f); setP (lp, "l1_depth", 1.f); setP (lp, "l1_fade", 1000.f);
        lp.prepareToPlay (rate, 256);
        float early = 0.f, late = 0.f;
        blocks (lp, 1.5, [&] (int i) { const float x = std::abs (lp.lfoValue (0)); if (i < 20) early = juce::jmax (early, x); if (i > 240) late = juce::jmax (late, x); });
        check (early < 0.2f && late > 0.8f, "fade in brings an LFO up over its fade time");
        setP (lp, "l1_fade", 0.f);

        // an LFO moving the master pan swings the sound between the ears
        lp.loadFile (0, tone);
        check (waitForTrack (lp, 0), "the tone loads for the LFO tests");
        lp.sliceEqual (0, 1);
        setP (lp, "t1_r1_loop", 1.f);                                    // loop, so the tone keeps sounding
        for (int s = 0; s < jd::kSteps; ++s) setP (lp, jd::stepId (0, s, jd::LaneRegion), s == 0 ? 1.f : 0.f);
        setP (lp, "l1_rate", 2.f); setP (lp, "l1_sync", 0.f); setP (lp, "m_pan", 0.f);
        lp.setMod ("m_pan", 0, 1.f);
        check (lp.getMods().size() == 1 && lp.modAmount ("m_pan", 0) == 1.f, "an assignment is stored");
        setP (lp, "seq_run", 1.f);
        auto sw = run (lp, 0.6, rate);
        auto side = [&] (int a, int z, int ch) { double s = 0.0; for (int i = a; i < z; ++i) s += std::abs (sw.getSample (ch, i)); return s; };
        const int q1 = (int) (0.25 * rate / 2.0 * 1.0), q3 = (int) (0.75 * rate / 2.0);       // quarter and three quarters of a 2 Hz cycle
        check (side (q1 - 400, q1 + 400, 1) > 4.0 * side (q1 - 400, q1 + 400, 0), "at the top of the LFO the sound is in the right ear");
        check (side (q3 - 400, q3 + 400, 0) > 4.0 * side (q3 - 400, q3 + 400, 1), "at the bottom of it the sound is in the left ear");

        lp.removeMod ("m_pan", 0);
        setP (lp, "seq_run", 0.f); run (lp, 0.1, rate); setP (lp, "seq_run", 1.f);
        sw = run (lp, 0.3, rate);
        const double l0 = side (2000, 8000, 0), r0 = side (2000, 8000, 1);
        check (std::abs (l0 - r0) < 0.05 * (l0 + r0) && lp.getMods().empty(), "taking the assignment off puts the pan back in the middle");

        lp.setMod ("t1_mute", 0, 1.f);
        check (lp.getMods().empty(), "a switch cannot be assigned, only continuous controls");

        // LFO moving another LFO
        JunkDrawerProcessor lq;
        lq.prepareToPlay (rate, 256);
        setP (lq, "l1_shape", 0.f); setP (lq, "l1_rate", 1.f); setP (lq, "l1_polar", 0.f); setP (lq, "l1_depth", 1.f);
        setP (lq, "l2_shape", 3.f); setP (lq, "l2_rate", 20.f); setP (lq, "l2_polar", 0.f); setP (lq, "l2_depth", 0.5f);
        lq.setMod ("l2_depth", 0, 1.f);
        float mn = 9.f, mx = -9.f;
        blocks (lq, 2.0, [&] (int) { mn = juce::jmin (mn, lq.lfoValue (1)); mx = juce::jmax (mx, lq.lfoValue (1)); });
        check (mx > 0.9f, "one LFO can move another one's depth (it reaches nearly full)");

        // per-track copies keep their own phase and start over with their loop
        JunkDrawerProcessor lt;
        lt.prepareToPlay (rate, 256);
        setP (lt, "l3_global", 0.f);
        check (lt.lfoValue (2) == 0.f, "an LFO that has not run yet reads zero");

        // assignments survive saving
        juce::MemoryBlock saved2;
        lp.setMod ("fx_filter_cut", 3, -0.4f);
        lp.getStateInformation (saved2);
        JunkDrawerProcessor lr;
        lr.prepareToPlay (rate, 256);
        lr.setStateInformation (saved2.getData(), (int) saved2.getSize());
        check (lr.getMods().size() == 1 && lr.modAmount ("fx_filter_cut", 3) == -0.4f, "assignments come back when a project is reloaded");
    }

    std::printf ("Catch something\n");
    {
        const int T = 6;
        auto runBlocks = [&] (int blocks, float level, float freq, float* lastOutPeak = nullptr)
        {
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
            static double ph = 0.0;
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < 256; ++i) { const float x = level * (float) std::sin (ph); ph += 6.2831853 * freq / rate; b.setSample (0, i, x); b.setSample (1, i, x * 0.5f); }
                proc.processBlock (b, m);
                if (lastOutPeak != nullptr) *lastOutPeak = b.getMagnitude (0, 256);
            }
        };
        // start now, free length
        check (proc.startCatch (T, 0, 0, 0), "a take can be started on an empty track");
        check (! proc.startCatch (T, 0, 0, 0), "a second take cannot start while one is open");
        float outPk = 1.f;
        runBlocks (40, 0.5f, 440.f, &outPk);
        check (proc.catchState() == JunkDrawerProcessor::CatchRunning, "a take that starts now is running at once");
        check (outPk == 0.f, "the input does not pass through to the output");
        proc.stopCatch (true);
        runBlocks (1, 0.5f, 440.f);
        check (waitForTrack (proc, T), "a finished take is saved and loaded into the track");
        auto d = proc.getTrack (T);
        check (d != nullptr && std::abs (d->length() - 40 * 256) <= 256, "it holds what was played (to the nearest block)");
        check (d != nullptr && d->audio.getMagnitude (0, 0, 2000) > 0.4f && d->audio.getMagnitude (1, 0, 2000) < 0.3f, "both input channels were kept apart");
        check (d != nullptr && juce::File (d->path).existsAsFile() && juce::File (d->path).getParentDirectory() == JunkDrawerProcessor::recordingsFolder(), "the take lives in the Recordings folder");
        if (d != nullptr) juce::File (d->path).deleteFile();
        proc.clearTrack (T);

        // wait for sound, left channel only
        check (proc.startCatch (T, 1, 2, 0), "a take can wait for sound");
        runBlocks (30, 0.f, 440.f);
        check (proc.catchState() == JunkDrawerProcessor::CatchWaiting, "it keeps waiting while the input is quiet");
        runBlocks (10, 0.6f, 440.f);
        check (proc.catchState() == JunkDrawerProcessor::CatchRunning, "it starts when sound arrives");
        proc.stopCatch (true); runBlocks (1, 0.6f, 440.f);
        check (waitForTrack (proc, T), "the take is loaded");
        d = proc.getTrack (T);
        check (d != nullptr && std::abs (d->length() - (10 * 256 + (int) (rate * 0.1))) <= 256, "the quiet lead-in is left out (only a 0.1 s pre-roll stays)");
        check (d != nullptr && std::abs (d->audio.getSample (0, 6000) - d->audio.getSample (1, 6000)) < 1e-5f && d->audio.getMagnitude (0, 6000, 1000) > 0.3f, "left-only input fills both sides");
        if (d != nullptr) juce::File (d->path).deleteFile();
        proc.clearTrack (T);

        // one bar at 120 BPM (no host) is two seconds
        check (proc.startCatch (T, 0, 1, 1), "a take can run for one bar");
        runBlocks ((int) (2.2 * rate / 256), 0.4f, 330.f);
        check (proc.catchState() == JunkDrawerProcessor::CatchIdle || waitForTrack (proc, T), "it stops by itself at the end of the bar");
        check (waitForTrack (proc, T), "the bar take is loaded");
        d = proc.getTrack (T);
        check (d != nullptr && std::abs (d->seconds() - 2.0) < 0.03, "and is two seconds long");
        if (d != nullptr) juce::File (d->path).deleteFile();
        proc.clearTrack (T);

        // discard
        check (proc.startCatch (T, 0, 0, 0), "a take can be started to be thrown away");
        runBlocks (10, 0.5f, 440.f);
        proc.stopCatch (false); runBlocks (1, 0.5f, 440.f); pump (300);
        check (proc.getTrack (T) == nullptr && proc.catchState() == JunkDrawerProcessor::CatchIdle, "a discarded take leaves the track empty");
    }

    std::printf ("Export\n");
    {
        JunkDrawerProcessor ex;
        ex.prepareToPlay (rate, 256);
        setP (ex, "m_volume", 0.f);
        fxOff (ex);
        ex.loadFile (0, makeWav (44100.0, 2, 2.0, { 0.0, 0.5, 1.0, 1.5 }, "jd_test_export.wav"));
        check (waitForTrack (ex, 0), "the export test has a sound to play");
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("jd_export_test");
        tmp.deleteRecursively();
        std::atomic<float> prog { 0.f };
        std::atomic<bool> cancel { false };
        auto readBack = [&] (const juce::File& f, double& sr, int& ch, int& bits, juce::int64& len, float& peak)
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return false;
            sr = r->sampleRate; ch = (int) r->numChannels; bits = (int) r->bitsPerSample; len = r->lengthInSamples;
            juce::AudioBuffer<float> b (2, (int) std::min<juce::int64> (len, 4000000));
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
            peak = juce::jmax (b.getMagnitude (0, 0, b.getNumSamples()), b.getMagnitude (1, 0, b.getNumSamples()));
            return true;
        };
        // one pass of the pattern: 16 steps of 1/16 at 120 BPM is 2 seconds
        auto opt = [&] (const juce::String& sub)
        {
            jdx::ExportOptions o; o.folder = tmp.getChildFile (sub); o.passes = 1; o.rate = 48000.0; return o;
        };
        {
            auto snap = jdx::takeSnapshot (ex);
            check (std::abs (jdx::Exporter::passSeconds (*snap, jdx::Exporter::audibleTracks (*snap)) - 2.0) < 1e-6, "one pass is one run of the longest lane");
            auto o = opt ("master");
            auto res = jdx::Exporter::run (*snap, o, prog, cancel);
            double sr; int ch, bits; juce::int64 len; float pk;
            check (res.ok && res.files.size() == 1 && readBack (res.files[0], sr, ch, bits, len, pk), "a master export writes one readable file");
            if (res.ok && readBack (res.files[0], sr, ch, bits, len, pk))
            {
                check (sr == 48000.0 && ch == 2 && bits == 24, "at the rate and depth that were asked for");
                check (std::abs ((double) len - 2.0 * 48000.0) < 600.0, "and one pass long");
                check (std::abs (juce::Decibels::gainToDecibels (pk) + 0.3f) < 0.3f, "Relative normalizing puts the peak at -0.3 dB");
            }
            o = opt ("off"); o.norm = jdx::NormMode::Off;
            auto r2 = jdx::Exporter::run (*snap, o, prog, cancel);
            const bool rb2 = r2.ok && readBack (r2.files[0], sr, ch, bits, len, pk);
            std::printf ("   (off: ok=%d err=%s peak=%.4f)\n", (int) r2.ok, r2.error.toRawUTF8(), rb2 ? (double) pk : -1.0);
            check (rb2 && pk > 0.01f && std::abs (juce::Decibels::gainToDecibels (pk) + 0.3f) > 0.1f, "with normalizing off the level is left as it plays");
        }
        {   // every file type, bit depth and rate reads back as asked
            auto snap = jdx::takeSnapshot (ex);
            int good = 0, total = 0;
            for (int ft = 0; ft < 3; ++ft) for (int bd = 0; bd < 2; ++bd) for (int rt = 0; rt < 3; ++rt)
            {
                auto o = opt ("combo" + juce::String (ft) + juce::String (bd) + juce::String (rt));
                o.file = (jdx::FileKind) ft; o.bits = bd == 0 ? 24 : 16; o.rate = rt == 0 ? 96000.0 : rt == 1 ? 48000.0 : 44100.0;
                auto r = jdx::Exporter::run (*snap, o, prog, cancel);
                double sr; int ch, bits; juce::int64 len; float pk;
                ++total;
                if (r.ok && readBack (r.files[0], sr, ch, bits, len, pk) && sr == o.rate && ch == 2 && bits == o.bits && std::abs ((double) len - 2.0 * o.rate) < 0.02 * o.rate && pk > 0.05f) ++good;
            }
            check (good == total && total == 18, "all 18 file type, bit depth and rate combinations read back correctly");
        }
        {   // lengths
            auto snap = jdx::takeSnapshot (ex);
            double sr; int ch, bits; juce::int64 len; float pk;
            auto o = opt ("time"); o.lengthKind = jdx::LengthKind::Time; o.timeSeconds = 3.0;
            auto r = jdx::Exporter::run (*snap, o, prog, cancel);
            check (r.ok && readBack (r.files[0], sr, ch, bits, len, pk) && std::abs ((double) len - 3.0 * 48000.0) < 600.0, "a typed time sets the length");
            o = opt ("bars"); o.lengthKind = jdx::LengthKind::Bars; o.bars = 2;
            r = jdx::Exporter::run (*snap, o, prog, cancel);
            check (r.ok && readBack (r.files[0], sr, ch, bits, len, pk) && std::abs ((double) len - 4.0 * 48000.0) < 600.0, "two bars at 120 BPM are four seconds");
            jdx::ExportOptions big; big.lengthKind = jdx::LengthKind::Time; big.timeSeconds = 99999.0;
            check (jdx::exportMainSeconds (big, 2.0, 120.0, 4.0) == 1800.0, "the length is capped at 30 minutes");
        }
        {   // endings
            auto snap = jdx::takeSnapshot (ex);
            double sr; int ch, bits; juce::int64 len; float pk;
            auto o = opt ("fade"); o.lengthKind = jdx::LengthKind::Time; o.timeSeconds = 4.0; o.ending = jdx::EndingKind::Fade; o.endingSeconds = 2.0;
            auto r = jdx::Exporter::run (*snap, o, prog, cancel);
            bool quiet = false;
            if (r.ok)
            {
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (r.files[0]));
                juce::AudioBuffer<float> b (2, 200);
                rd->read (&b, 0, 200, rd->lengthInSamples - 200, true, true);
                quiet = b.getMagnitude (0, 200) < 0.002f;
            }
            check (r.ok && quiet, "a fade out reaches silence");
            o = opt ("ring"); o.ending = jdx::EndingKind::RingOut; o.endingSeconds = 3.0;
            r = jdx::Exporter::run (*snap, o, prog, cancel);
            check (r.ok && readBack (r.files[0], sr, ch, bits, len, pk) && std::abs ((double) len - 5.0 * 48000.0) < 600.0, "a ring-out adds its time after the music");
        }
        {   // folders, space, stems, mute
            auto snap = jdx::takeSnapshot (ex);
            auto o = opt ("full"); o.freeBytesOverride = 1000;
            auto r = jdx::Exporter::run (*snap, o, prog, cancel);
            check (! r.ok && r.error.contains ("Not enough free disk space"), "an export refuses to start when the drive is too full");
            ex.loadFile (1, makeWav (22050.0, 1, 1.0, { 0.1, 0.4 }, "jd_test_export2.wav"));
            check (waitForTrack (ex, 1), "a second sound is loaded");
            snap = jdx::takeSnapshot (ex);
            o = opt ("stems"); o.mode = jdx::ExportMode::Stems;
            r = jdx::Exporter::run (*snap, o, prog, cancel);
            check (r.ok && r.files.size() == 2 && r.files[0].getFileName().contains ("Track1") && r.files[1].getFileName().contains ("Track2"), "stems write one file per track");
            setP (ex, "t2_mute", 1.f);
            snap = jdx::takeSnapshot (ex);
            check (jdx::Exporter::audibleTracks (*snap).size() == 1, "a muted track is left out");
            setP (ex, "t2_mute", 0.f);
            // stems add up to the master mix (effects are off here, so the mix is a plain sum)
            snap = jdx::takeSnapshot (ex);
            o = opt ("sum_m"); o.norm = jdx::NormMode::Off; auto rm = jdx::Exporter::run (*snap, o, prog, cancel);
            o = opt ("sum_s"); o.norm = jdx::NormMode::Off; o.mode = jdx::ExportMode::Stems; auto rs = jdx::Exporter::run (*snap, o, prog, cancel);
            bool sums = rm.ok && rs.ok && rs.files.size() == 2;
            if (sums)
            {
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> a (fm.createReaderFor (rm.files[0])), b (fm.createReaderFor (rs.files[0])), c (fm.createReaderFor (rs.files[1]));
                const int n = (int) std::min ({ (juce::int64) a->lengthInSamples, (juce::int64) b->lengthInSamples, (juce::int64) c->lengthInSamples });
                juce::AudioBuffer<float> A (2, n), B (2, n), C (2, n);
                a->read (&A, 0, n, 0, true, true); b->read (&B, 0, n, 0, true, true); c->read (&C, 0, n, 0, true, true);
                float worst = 0.f;
                for (int i = 0; i < n; ++i) worst = juce::jmax (worst, std::abs (A.getSample (0, i) - B.getSample (0, i) - C.getSample (0, i)));
                sums = worst < 0.01f;
            }
            check (sums, "the stems add up to the master mix");
        }
        {   // remembered per computer
            jdx::ExportOptions a; a.mode = jdx::ExportMode::Stems; a.file = jdx::FileKind::Flac; a.bits = 16; a.rate = 44100.0; a.passes = 8; a.norm = jdx::NormMode::Maximum;
            a.ending = jdx::EndingKind::Fade; a.endingSeconds = 5.0;
            jdx::ExportSettings::saveOptions (a);
            jdx::ExportOptions b; jdx::ExportSettings::loadOptions (b);
            check (b.mode == a.mode && b.file == a.file && b.bits == 16 && b.rate == 44100.0 && b.passes == 8 && b.norm == a.norm && b.ending == a.ending && b.endingSeconds == 5.0, "the export options are saved and restored");
            jdx::ExportSettings st; st.mode = 2; st.folder = tmp; st.save();
            check (jdx::ExportSettings::load().mode == 2 && jdx::ExportSettings::load().folder == tmp, "the chosen folder is saved and restored");
            st.mode = 0; st.save();
            jdx::ExportOptions def; jdx::ExportSettings::saveOptions (def);
        }
        tmp.deleteRecursively();
    }

    std::printf ("Rummage\n");
    {
        auto steps = [&] (int lane) { std::vector<float> v; for (int k = 0; k < jd::kSteps; ++k) v.push_back (proc.state.getRawParameterValue (jd::stepId (0, k, lane))->load()); return v; };
        setP (proc, "lock_vol", 1.f);
        for (int k = 0; k < jd::kSteps; ++k) setP (proc, jd::stepId (0, k, jd::LaneVolume), 0.3f);
        proc.rummage (0);
        bool volSame = true; for (auto x : steps (jd::LaneVolume)) volSame = volSame && std::abs (x - 0.3f) < 1e-4f;
        check (volSame, "a kept lane is left alone");
        const auto reg = steps (jd::LaneRegion);
        const int nreg = juce::roundToInt (proc.state.getRawParameterValue ("t1_regions")->load());
        bool inRange = true; int moved = 0;
        for (auto x : reg) { inRange = inRange && x >= 0 && x <= (float) nreg; moved += x != 0; }
        check (inRange, "rummaged regions stay within the track's regions");
        setP (proc, "lock_vol", 0.f);
        setP (proc, "shake_amt", 0.f);
        const auto before = steps (jd::LanePitch);
        proc.shake (0);
        check (steps (jd::LanePitch) == before, "shaking with the amount at zero changes nothing");
        proc.rummage (0);
        check (steps (jd::LaneVolume) != std::vector<float> (jd::kSteps, 0.3f), "an unkept lane gets re-rolled");
    }

    std::printf ("Key and note\n");
    {
        const double sr = 44100.0;
        auto tone = [&] (juce::AudioBuffer<float>& b, int from, int len, double hz, float amp)
        {
            for (int i = 0; i < len && from + i < b.getNumSamples(); ++i)
            {
                float v = 0.f;
                for (int h = 1; h <= 4; ++h) v += (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * i / sr) * amp / (float) h;
                const float env = std::min (1.f, (float) i / 400.f) * std::min (1.f, (float) (len - i) / 400.f);
                for (int ch = 0; ch < 2; ++ch) b.addSample (ch, from + i, v * env);
            }
        };
        auto hzOf = [] (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); };
        auto chordSeq = [&] (std::vector<std::vector<int>> chords)
        {
            juce::AudioBuffer<float> b (2, (int) (sr * 8.0)); b.clear();
            const int seg = b.getNumSamples() / (int) chords.size();
            for (size_t c = 0; c < chords.size(); ++c)
                for (int m : chords[c]) tone (b, (int) c * seg, seg, hzOf (m), 0.15f);
            return b;
        };

        juce::AudioBuffer<float> one (2, (int) (sr * 1.0)); one.clear();
        tone (one, 0, one.getNumSamples(), hzOf (57.14), 0.4f);
        auto p = jd::detectPitch (one, sr, 0, one.getNumSamples());
        check (p.found && std::abs (p.midi - 57.14f) < 0.1f, "a steady note is found to within a few cents");
        check (jd::noteText (p.midi) == "A3 +14c", "and named with its cents");

        juce::AudioBuffer<float> noise (2, (int) (sr * 1.0)); juce::Random rnd (5);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (ch, i, rnd.nextFloat() * 0.6f - 0.3f);
        check (! jd::detectPitch (noise, sr, 0, noise.getNumSamples()).found, "noise has no note");
        check (! jd::detectKey (noise, sr).found, "noise has no key");
        juce::AudioBuffer<float> tiny (2, 500); tiny.clear();
        check (! jd::detectPitch (tiny, sr, 0, 500).found && ! jd::detectKey (tiny, sr).found, "a tiny or silent sound has neither");

        auto gMajor = chordSeq ({ { 55, 59, 62 }, { 60, 64, 67 }, { 62, 66, 69 }, { 55, 59, 62 } });
        auto k = jd::detectKey (gMajor, sr);
        check (k.found && jd::matchShift (k, 7, 1) == 0, "G major chords are heard as G major or E minor");
        auto aMinor = chordSeq ({ { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 }, { 57, 60, 64 } });
        k = jd::detectKey (aMinor, sr);
        check (k.found && jd::matchShift (k, 9, 2) == 0, "A minor chords are heard as A minor or C major");
        check (k.found && jd::keyText (k).isNotEmpty() && k.confidence >= 0.f && k.confidence <= 1.f, "a key reads as text with a confidence");

        jd::KeyResult am; am.found = true; am.tonic = 9; am.minor = true;
        check (jd::matchShift (am, 0, 2) == 3, "A minor to C minor is up 3");
        check (jd::matchShift (am, 0, 1) == 0, "A minor and C major are the same key");
        check (jd::matchShift (am, 5, 0) == -4, "with no mode only the tonic moves (A to F is down 4)");
        check (jd::matchShift (jd::KeyResult(), 5, 1) == 0, "no key means no shift");
        check (jd::nearestInKey (57.14f, 0, 1) == 57.f && jd::nearestInKey (61.2f, 0, 1) == 62.f, "the nearest note of C major");
        check (jd::nearestInKey (61.2f, 0, 0) == 61.f, "with no key it is the nearest semitone");

        // through the processor
        auto td = std::make_shared<jd::TrackData>();
        td->audio.makeCopyOf (gMajor); td->sampleRate = sr; td->name = "chords";
        td->key = jd::detectKey (td->audio, sr);
        proc.setTrackAudio (5, td);
        setP (proc, "key_root", 0.f); setP (proc, "key_mode", 1.f);
        check (proc.matchTrackToKey (5), "a track with a key can be matched");
        const float tune = proc.state.getRawParameterValue ("t6_tune")->load();
        check (std::abs (tune - (float) jd::matchShift (td->key, 0, 1)) < 0.01f && std::abs (tune) <= 6.f, "its Tune becomes the shift into C major");
        {
            auto d2 = std::make_shared<jd::TrackData>(); d2->audio.makeCopyOf (aMinor); d2->sampleRate = sr; d2->name = "am";
            d2->key = jd::detectKey (d2->audio, sr);
            proc.setTrackAudio (6, d2);
            setP (proc, "t6_tune", 0.f); setP (proc, "t7_tune", 0.f);
            check (proc.matchAllToKey() == 2, "Match all sets every track that has a key");
            const float a = proc.state.getRawParameterValue ("t6_tune")->load(), b = proc.state.getRawParameterValue ("t7_tune")->load();
            check (std::abs (a - (float) jd::matchShift (td->key, 0, 1)) < 0.01f && std::abs (b - (float) jd::matchShift (d2->key, 0, 1)) < 0.01f, "each by its own amount");
            setP (proc, "t6_tune", 5.f);
            check (std::abs (proc.state.getRawParameterValue ("t6_tune")->load() - 5.f) < 0.01f, "and a track can be changed by hand afterwards");
            setP (proc, "t7_tune", 0.f);
            proc.setTrackAudio (6, nullptr);
        }
        proc.setTrackAudio (5, nullptr);

        auto note = std::make_shared<jd::TrackData>();
        note->audio.makeCopyOf (one); note->sampleRate = sr; note->name = "note";
        proc.setTrackAudio (5, note);
        setP (proc, "t6_tune", 0.f); setP (proc, "t6_regions", 1.f); setP (proc, "t6_r1_start", 0.f); setP (proc, "t6_r1_end", 1.f);
        const auto rp = proc.regionPitch (5, 0);
        check (rp.found && std::abs (rp.midi - 57.14f) < 0.1f, "a region reports its note");
        check (proc.tuneRegionToKey (5, 0), "a region can be tuned");
        check (std::abs (proc.state.getRawParameterValue ("t6_r1_pitch")->load() + 0.14f) < 0.1f, "A3 plus 14 cents is pulled down onto A3");
        setP (proc, "t6_tune", 4.f);
        proc.tuneRegionToKey (5, 0);
        check (std::abs (proc.state.getRawParameterValue ("t6_r1_pitch")->load() - 0.86f) < 0.1f, "tuning takes the track's Tune into account (A3 + 4 lands on the nearest note of C major)");
        setP (proc, "t6_tune", 0.f); setP (proc, "t6_r1_pitch", 0.f);
        proc.setTrackAudio (5, nullptr);
        check (! proc.tuneRegionToKey (5, 0) && ! proc.matchTrackToKey (5), "an empty track has nothing to tune");
    }

    std::printf ("Lane tools\n");
    {
        JunkDrawerProcessor lp;
        lp.prepareToPlay (48000.0, 256);
        auto setS = [&] (int tr, int s, int lane, float v) { setP (lp, jd::stepId (tr, s, lane), v); };
        auto getS = [&] (int tr, int s, int lane) { return lp.state.getRawParameterValue (jd::stepId (tr, s, lane))->load(); };
        for (int s = 0; s < jd::kSteps; ++s) setS (0, s, jd::LanePitch, (float) (s % 5) - 2.f);
        setP (lp, jd::laneLenId (0, jd::LanePitch), 12.f);
        auto clip = lp.copyLane (0, jd::LanePitch);
        check (clip.lane == jd::LanePitch && clip.length == 12 && clip.v[3] == 1.f, "a lane copies its values and its length");
        check (lp.pasteLane (1, clip), "and pastes onto another track");
        bool same = true; for (int s = 0; s < jd::kSteps; ++s) same = same && getS (1, s, jd::LanePitch) == getS (0, s, jd::LanePitch);
        check (same && lp.laneLength (1, jd::LanePitch) == 12, "values and length arrive as they were");
        check (getS (1, 0, jd::LaneVolume) == 1.f, "other lanes are untouched");
        check (! lp.pasteLane (1, JunkDrawerProcessor::LaneCopy()), "an empty clipboard pastes nothing");

        for (int s = 0; s < jd::kSteps; ++s) setS (0, s, jd::LaneVolume, 0.8f);
        check (lp.humanizeLane (0, jd::LaneVolume), "volume can be humanized");
        bool moved = false, inRange = true;
        for (int s = 0; s < jd::kSteps; ++s) { moved = moved || std::abs (getS (0, s, jd::LaneVolume) - 0.8f) > 0.001f; inRange = inRange && std::abs (getS (0, s, jd::LaneVolume) - 0.8f) <= 0.121f; }
        check (moved && inRange, "each step moves a little, never far");
        for (int s = 0; s < jd::kSteps; ++s) setS (0, s, jd::LaneVolume, 1.f);
        lp.humanizeLane (0, jd::LaneVolume);
        bool capped = true; for (int s = 0; s < jd::kSteps; ++s) capped = capped && getS (0, s, jd::LaneVolume) <= 1.f && getS (0, s, jd::LaneVolume) >= 0.87f;
        check (capped, "volume never goes past full");
        check (lp.humanizeLane (0, jd::LaneTime), "timing can be humanized");
        bool small = true; for (int s = 0; s < jd::kSteps; ++s) small = small && std::abs (getS (0, s, jd::LaneTime)) <= 0.061f;
        check (small, "by only a few percent");
        check (! lp.humanizeLane (0, jd::LanePitch) && ! lp.humanizeLane (0, jd::LaneRegion), "pitch and region are not humanized");

        lp.resetLane (0, jd::LaneVolume); lp.resetLane (0, jd::LaneTime); lp.resetLane (0, jd::LanePitch);
        bool plain = true; for (int s = 0; s < jd::kSteps; ++s) plain = plain && getS (0, s, jd::LaneVolume) == 1.f && getS (0, s, jd::LaneTime) == 0.f && getS (0, s, jd::LanePitch) == 0.f;
        check (plain, "reset puts a lane back to plain values");

        setP (lp, "t2_regions", 2.f);
        for (int s = 0; s < jd::kSteps; ++s) setS (0, s, jd::LaneRegion, (float) (s % 6));
        clip = lp.copyLane (0, jd::LaneRegion);
        lp.pasteLane (1, clip);
        bool fits = true; for (int s = 0; s < jd::kSteps; ++s) fits = fits && getS (1, s, jd::LaneRegion) <= 2.f;
        check (fits, "pasted regions are folded onto the regions the track has");
    }

    std::printf ("Catch Last\n");
    {
        const int T = 6;
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
        auto feed = [&] (int blocks, float level)
        {
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < 256; ++i) { b.setSample (0, i, level); b.setSample (1, i, -level); }
                proc.processBlock (b, m);
            }
        };
        check (! proc.catchLast (T), "nothing to catch while it is switched off");
        proc.setListening (true);
        check (proc.catchLastSeconds() == 0.f, "nothing is held at first");
        feed (10, 0.8f);
        check (proc.catchLastSeconds() > 0.05f, "it counts what it has heard");
        feed ((int) (rate * 2 / 256), 0.25f);
        check (proc.catchLast (T), "the last stretch can be saved");
        check (waitForTrack (proc, T), "the saved stretch is loaded");
        auto d = proc.getTrack (T);
        check (d != nullptr && std::abs (d->length() - (10 * 256 + (int) (rate * 2 / 256) * 256)) <= 512, "it holds everything heard so far");
        check (d != nullptr && std::abs (d->audio.getSample (0, d->length() - 100) - 0.25f) < 0.01f && std::abs (d->audio.getSample (1, d->length() - 100) + 0.25f) < 0.01f, "the newest sound is at the end, channels kept apart");
        check (d != nullptr && std::abs (d->audio.getSample (0, 100) - 0.8f) < 0.01f, "the oldest sound is at the start");
        if (d != nullptr) juce::File (d->path).deleteFile();
        proc.clearTrack (T);
        check (proc.catchLastSeconds() == 0.f, "a catch empties the buffer");
        feed ((int) (rate * 35 / 256), 0.5f);
        check (proc.catchLastSeconds() <= (float) JunkDrawerProcessor::kCatchLastSeconds + 0.01f, "it never holds more than 30 seconds");
        check (proc.catchLast (T) && waitForTrack (proc, T), "a full buffer can be saved");
        d = proc.getTrack (T);
        check (d != nullptr && std::abs (d->length() - (int) (30.0 * rate)) <= 1, "that is exactly 30 seconds");
        if (d != nullptr) juce::File (d->path).deleteFile();
        proc.clearTrack (T);
        proc.setListening (false);
        check (! proc.catchLast (T), "switching it off frees the buffer");
    }

    std::printf ("Drawers\n");
    {
        check (proc.drawerCount() >= 100, "100 factory drawers");
        std::set<juce::String> names; bool allNamed = true;
        for (int i = 0; i < 100; ++i) { names.insert (proc.drawer (i).name); allNamed = allNamed && proc.drawer (i).name.isNotEmpty(); }
        check (allNamed && names.size() == 100, "every factory drawer has its own name");
        bool banned = false;
        for (auto& n : names) banned = banned || n.containsIgnoreCase ("rubber");
        check (! banned, "no banned words in names");
        for (int i = 0; i < 100; ++i) if (proc.drawer (i).mods.empty()) { check (false, "each drawer hooks up at least one LFO"); break; }
        setP (proc, "t1_regions", 5.f);
        proc.loadDrawer (7);
        check (proc.currentDrawer() == 7, "loading a drawer remembers which one");
        check (juce::roundToInt (proc.state.getRawParameterValue ("t1_regions")->load()) == 5, "a drawer leaves the number of regions alone");
        check (! proc.modAmount ("fx_filter_cut", 0) == false || proc.drawer (7).mods.size() > 0, "drawer mods applied");
        bool differs = false;
        for (int i = 8; i < 14; ++i) { proc.loadDrawer (i); differs = differs || proc.state.getRawParameterValue ("fx_filter_cut")->load() != proc.drawer (7).values.at ("fx_filter_cut"); }
        check (differs, "different drawers load different settings");
        setP (proc, "t2_level", -33.f);
        proc.loadDrawer (7);
        check (std::abs (proc.state.getRawParameterValue ("t2_level")->load() - proc.drawer (7).values.at ("t2_level")) < 0.05f, "loading overwrites earlier edits");
        proc.stepDrawer (1);
        check (proc.currentDrawer() == 8, "next steps forward");
        proc.stepDrawer (-1); proc.stepDrawer (-8);
        check (proc.currentDrawer() == proc.drawerCount() - 1, "previous wraps around");
        {
            const int before = proc.drawerCount();
            check (proc.saveDrawer ("Test Drawer ZZ"), "a drawer can be saved");
            check (proc.drawerCount() == before + 1 && proc.currentDrawer() >= 100, "it appears in the list and becomes the current one");
            check (proc.drawer (proc.currentDrawer()).name == "Test Drawer ZZ", "it keeps its name");
            check (! proc.renameDrawer (7, "Nope") && ! proc.deleteDrawer (7), "factory drawers cannot be renamed or deleted");
            check (proc.renameDrawer (proc.currentDrawer(), "Test Drawer YY") && proc.drawer (proc.currentDrawer()).name == "Test Drawer YY" && proc.drawerCount() == before + 1, "a saved drawer can be renamed");
            check (proc.deleteDrawer (proc.currentDrawer()) && proc.drawerCount() == before && proc.currentDrawer() == -1, "a saved drawer can be deleted");
            proc.loadDrawer (7);
        }
        check (proc.getNumPrograms() == proc.drawerCount() && proc.getProgramName (0).isNotEmpty(), "hosts see the drawers as programs");
    }

    proc.clearTrack (0);
    check (proc.getTrack (0) == nullptr, "clearing a track empties it");

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
