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

    proc.clearTrack (0);
    check (proc.getTrack (0) == nullptr, "clearing a track empties it");

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
