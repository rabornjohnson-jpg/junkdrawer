#include "PluginProcessor.h"
#include "PluginEditor.h"

JunkDrawerProcessor::JunkDrawerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "JunkDrawer", jd::createLayout())
{
    jd::registerFormats (formats);
    pVolume = state.getRawParameterValue ("m_volume");
    pPan    = state.getRawParameterValue ("m_pan");
    pDiv = state.getRawParameterValue ("seq_div");   pFeel = state.getRawParameterValue ("seq_feel");   pBpm = state.getRawParameterValue ("seq_bpm");  pRun = state.getRawParameterValue ("seq_run");
    for (int i = 0; i < jd::kLfos; ++i)
    {
        auto g = [&] (const char* s) { return state.getRawParameterValue (jd::lfoId (i, s)); };
        lp[(size_t) i] = { g ("shape"), g ("rate"), g ("div"), g ("depth"), g ("phase"), g ("fade"), g ("sync"), g ("polar"), g ("global") };
    }
    pRate = state.getRawParameterValue ("m_rate");   pRateMode = state.getRawParameterValue ("m_ratemode");  pSwing = state.getRawParameterValue ("m_swing");
    for (int t = 0; t < jd::kTracks; ++t)
    {
        regCount[(size_t) t] = state.getRawParameterValue (jd::trackId (t, "regions"));
        for (int ln = 0; ln < jd::kLanes; ++ln) laneLen[(size_t) t][(size_t) ln] = state.getRawParameterValue (jd::laneLenId (t, ln));
        for (int s = 0; s < jd::kSteps; ++s)
            for (int ln = 0; ln < jd::kLanes; ++ln) sp[(size_t) t][(size_t) s].v[(size_t) ln] = state.getRawParameterValue (jd::stepId (t, s, ln));
        audition[(size_t) t] = 0; playing[(size_t) t] = 0; loadToken[(size_t) t] = 0;
        tp[(size_t) t] = { state.getRawParameterValue (jd::trackId (t, "mute")), state.getRawParameterValue (jd::trackId (t, "solo")),
                           state.getRawParameterValue (jd::trackId (t, "level")), state.getRawParameterValue (jd::trackId (t, "pan")) };
        for (int r = 0; r < jd::kRegions; ++r)
        {
            auto g = [&] (const char* s) { return state.getRawParameterValue (jd::regionId (t, r, s)); };
            rp[(size_t) t][(size_t) r] = { g ("start"), g ("end"), g ("pitch"), g ("gain"), g ("pan"), g ("cutoff"), g ("reso"), g ("drive"),
                                           g ("attack"), g ("decay"), g ("sustain"), g ("release"), g ("loop"), g ("reverse") };
        }
    }
}

JunkDrawerProcessor::~JunkDrawerProcessor()
{
    alive = false;
    loader.removeAllJobs (true, 5000);
}

void JunkDrawerProcessor::prepareToPlay (double sampleRate, int)
{
    const bool rateChanged = std::abs (sampleRate - hostRate) > 0.5;
    hostRate = sampleRate;
    volume.reset (sampleRate, 0.02);
    pan.reset (sampleRate, 0.02);
    volume.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
    pan.setCurrentAndTargetValue (pPan->load());
    for (auto& v : voices) { v.prepare (sampleRate); v.kill(); }
    fxFilter.prepare (sampleRate); fxDelay.prepare (sampleRate); fxReverb.prepare (sampleRate); fxLimiter.prepare (sampleRate);
    for (auto& b : wasOn) b = false;
    for (int i = 0; i < jd::kLfos; ++i) for (int k = 0; k < 9; ++k) { lfoAcc[i][k] = 0.0; lfoSince[i][k] = 0.0; lfoRestartBeat[i][k] = 0.0; lfoOut[i][k].store (0.f); }
    idleBeats = 0.0;

    if (rateChanged)                                          // samples are stored at the session rate, so a new rate means reading them again
        for (int t = 0; t < jd::kTracks; ++t)
            if (auto d = getTrack (t))
                if (std::abs (d->sampleRate - sampleRate) > 0.5) loadFile (t, juce::File (d->path), false);
}

bool JunkDrawerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

//==============================================================================
std::shared_ptr<const jd::TrackData> JunkDrawerProcessor::getTrack (int track) const
{
    const juce::SpinLock::ScopedLockType sl (slotLock);
    return slots[(size_t) track].data;
}

std::vector<float> JunkDrawerProcessor::hitsFor (int track) const
{
    const juce::SpinLock::ScopedLockType sl (slotLock);
    return slots[(size_t) track].hits;
}

juce::String JunkDrawerProcessor::takeLoadMessage()
{
    const juce::ScopedLock sl (messageLock);
    auto m = loadMessage;
    loadMessage.clear();
    return m;
}

jd::RegionSnapshot JunkDrawerProcessor::readRegion (int t, int r) const
{
    const auto& p = rp[(size_t) t][(size_t) r];
    jd::RegionSnapshot s;
    s.start = v (p.start); s.end = v (p.end);
    s.pitch = v (p.pitch); s.gainDb = v (p.gain); s.pan = v (p.pan);
    s.cutoff = v (p.cutoff); s.reso = v (p.reso); s.driveDb = v (p.drive);
    s.attackMs = v (p.attack); s.decayMs = v (p.decay); s.sustain = v (p.sustain); s.releaseMs = v (p.release);
    s.loop = juce::roundToInt (p.loop->load()); s.reverse = p.reverse->load() > 0.5f;
    return s;
}

void JunkDrawerProcessor::loadFile (int track, const juce::File& file, bool sliceAfter)
{
    const int token = ++loadToken[(size_t) track];                // a newer load for the same track makes this one stale
    const double rate = hostRate;
    loader.addJob ([this, track, file, rate, token, sliceAfter]
    {
        juce::String err;
        auto d = jd::loadTrackData (file, rate, formats, err);
        if (! alive.load()) return;
        juce::MessageManager::callAsync ([this, track, d, err, token, sliceAfter]
        {
            if (! alive.load() || token != loadToken[(size_t) track].load()) return;
            if (d == nullptr)
            {
                const juce::ScopedLock sl (messageLock);
                loadMessage = err;
            }
            else
            {
                if (d->truncated) { const juce::ScopedLock sl (messageLock); loadMessage = d->name + " is longer than 60 seconds, so only the first minute was kept."; }
                publish (track, d, sliceAfter);
            }
            sendChangeMessage();
        });
    });
}

void JunkDrawerProcessor::publish (int track, std::shared_ptr<const jd::TrackData> d, bool slice)
{
    auto hits = jd::detectOnsets (d->audio, d->sampleRate, 32);
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        slots[(size_t) track].data = d;
        slots[(size_t) track].hits = std::move (hits);
    }
    saveSamplePaths();
    if (slice) { sliceEqual (track, 4); fillPatternIfEmpty (track); }
}

void JunkDrawerProcessor::clearTrack (int track)
{
    ++loadToken[(size_t) track];
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        slots[(size_t) track] = {};
    }
    audition[(size_t) track].store (200);
    saveSamplePaths();
    sendChangeMessage();
}

void JunkDrawerProcessor::saveSamplePaths()
{
    auto node = state.state.getOrCreateChildWithName ("samples", nullptr);
    for (int t = 0; t < jd::kTracks; ++t)
    {
        auto d = getTrack (t);
        node.setProperty ("t" + juce::String (t + 1), d != nullptr ? d->path : juce::String(), nullptr);
    }
}

static void setParam (juce::AudioProcessorValueTreeState& s, const juce::String& id, float realValue)
{
    if (auto* p = s.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (realValue)); p->endChangeGesture(); }
}

void JunkDrawerProcessor::sliceEqual (int track, int count)
{
    count = juce::jlimit (1, jd::kRegions, count);
    setParam (state, jd::trackId (track, "regions"), (float) count);
    for (int r = 0; r < count; ++r)
    {
        setParam (state, jd::regionId (track, r, "start"), (float) r / (float) count);
        setParam (state, jd::regionId (track, r, "end"),   (float) (r + 1) / (float) count);
    }
    sendChangeMessage();
}

void JunkDrawerProcessor::addRegion (int track)
{
    const int count = juce::roundToInt (state.getRawParameterValue (jd::trackId (track, "regions"))->load());
    if (count >= jd::kRegions) return;
    int longest = 0;
    float best = -1.f;
    for (int r = 0; r < count; ++r)
    {
        const float len = state.getRawParameterValue (jd::regionId (track, r, "end"))->load() - state.getRawParameterValue (jd::regionId (track, r, "start"))->load();
        if (len > best) { best = len; longest = r; }
    }
    const float a = state.getRawParameterValue (jd::regionId (track, longest, "start"))->load();
    const float b = state.getRawParameterValue (jd::regionId (track, longest, "end"))->load();
    const float mid = 0.5f * (a + b);
    setParam (state, jd::regionId (track, longest, "end"), mid);
    setParam (state, jd::regionId (track, count, "start"), mid);
    setParam (state, jd::regionId (track, count, "end"), b);
    setParam (state, jd::trackId (track, "regions"), (float) (count + 1));
    sendChangeMessage();
}

void JunkDrawerProcessor::removeRegion (int track)
{
    const int count = juce::roundToInt (state.getRawParameterValue (jd::trackId (track, "regions"))->load());
    if (count <= 1) return;
    setParam (state, jd::trackId (track, "regions"), (float) (count - 1));
    sendChangeMessage();
}

void JunkDrawerProcessor::sliceByHits (int track, int count)
{
    auto d = getTrack (track);
    if (d == nullptr) return;
    count = juce::jlimit (1, jd::kRegions, count);
    auto hits = jd::detectOnsets (d->audio, d->sampleRate, count);         // the strongest `count` hits, in order
    if (hits.empty()) { sliceEqual (track, count); return; }
    if (hits.front() > 0.002f) hits.insert (hits.begin(), 0.f);            // sound before the first hit stays in region A
    if ((int) hits.size() > count) hits.resize ((size_t) count);
    const int n = (int) hits.size();
    setParam (state, jd::trackId (track, "regions"), (float) n);
    for (int r = 0; r < n; ++r)
    {
        setParam (state, jd::regionId (track, r, "start"), hits[(size_t) r]);
        setParam (state, jd::regionId (track, r, "end"),   r + 1 < n ? hits[(size_t) r + 1] : 1.f);
    }
    sendChangeMessage();
}

//==============================================================================
void JunkDrawerProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    if (buf.getNumChannels() < 2) return;

    if (modDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (modLock);
        if (sl.isLocked()) { std::swap (table, modIncoming); modDirty = false; }
    }
    volume.setTargetValue (juce::Decibels::decibelsToGain (v (pVolume), -59.f));
    pan.setTargetValue (v (pPan));

    auto* l = buf.getWritePointer (0);
    auto* r = buf.getWritePointer (1);

    // 1. preview requests from the editor
    bool anySolo = false;
    for (int t = 0; t < jd::kTracks; ++t) anySolo = anySolo || tp[(size_t) t].solo->load() > 0.5f;
    for (int t = 0; t < jd::kTracks; ++t)
    {
        const int cmd = audition[(size_t) t].exchange (0);
        if (cmd == 0) continue;
        if (cmd == 200) { voices[(size_t) t].release(); continue; }
        std::shared_ptr<const jd::TrackData> d;
        {
            const juce::SpinLock::ScopedTryLockType sl (slotLock);          // never wait on the audio thread; try again next block
            if (! sl.isLocked()) { audition[(size_t) t].store (cmd); continue; }
            d = slots[(size_t) t].data;
        }
        if (d == nullptr) continue;
        jd::RegionSnapshot s;
        if (cmd == 100) { s.loop = jd::OneShot; }                            // the whole sample, plain
        else s = readRegion (t, juce::jlimit (0, jd::kRegions - 1, cmd - 1));
        const float g = juce::Decibels::decibelsToGain (v (tp[(size_t) t].level), -59.f);
        voices[(size_t) t].trigger (std::move (d), s, g, v (tp[(size_t) t].pan));
    }

    // 2. transport: the host's position when it is playing, otherwise our own clock (standalone, or the Run switch)
    bool running = false;
    double ppq0 = 0.0, bpm = pBpm->load();
    bool hostKnown = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostKnown = true;
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
            {
                running = true;
                if (auto p = pos->getPpqPosition()) ppq0 = *p; else ppq0 = internalPpq;
            }
        }
    if (! hostKnown) { running = pRun->load() > 0.5f; ppq0 = internalPpq; }
    if (ppq0 < 0.0) running = false;
    const double dppq = bpm / (60.0 * hostRate);
    internalPpq = running ? internalPpq + dppq * n : 0.0;
    updateModulation (n, running ? ppq0 : idleBeats, running, running && ! seqWasRunning, bpm);
    if (! running) idleBeats += dppq * n;

    double rate = v (pRate);
    if (pRateMode->load() > 0.5f) rate = jd::snapRate (rate);
    const double beats = jd::stepBeats (juce::roundToInt (pDiv->load()), juce::roundToInt (pFeel->load()));
    const double u0 = ppq0 * rate / beats, dU = dppq * rate / beats;
    const double swing = v (pSwing);
    static const float ranges[] = { 12.f, 24.f, 48.f };
    const float masterPitch = juce::jlimit (-ranges[juce::jlimit (0, 2, juce::roundToInt (state.getRawParameterValue ("m_pitchrange")->load()))], ranges[juce::jlimit (0, 2, juce::roundToInt (state.getRawParameterValue ("m_pitchrange")->load()))], vId ("m_pitch"));

    auto renderVoices = [&] (int from, int to)
    {
        if (to <= from) return;
        for (int t = 0; t < jd::kTracks; ++t)
        {
            const bool audible = tp[(size_t) t].mute->load() < 0.5f && (! anySolo || tp[(size_t) t].solo->load() > 0.5f);
            if (audible) voices[(size_t) t].render (l + from, r + from, to - from);
            else         voices[(size_t) t].kill();
        }
    };

    int segStart = 0;
    if (running)
    {
        for (int i = 0; i < n; ++i)
        {
            long long S;
            double frac;
            jd::locateStep (u0 + dU * i, swing, S, frac);
            if (i == 0 || S != seq[0].step) uiStep.store (S, std::memory_order_relaxed);

            std::array<uint8_t, jd::kTracks> action {};                    // 1 = trigger, 2 = release
            bool any = false;
            for (int t = 0; t < jd::kTracks; ++t)
            {
                auto& q = seq[(size_t) t];
                if (S != q.step)
                {
                    applyStep (t, q, S);
                    if (S % laneLength (t, jd::LaneRegion) == 0) restartFlag[(size_t) t] = true;      // a per-track LFO starts over with its track's loop
                }
                if (q.region == 0) continue;
                const int sub = juce::jmin (q.ratchet - 1, (int) (frac * q.ratchet));
                const double subFrac = frac * q.ratchet - sub;
                if (sub != q.sub)                                          { q.sub = sub; q.released = false; action[(size_t) t] = 1; any = true; }
                else if (! q.released && ! q.noGate && subFrac >= q.len)  { q.released = true; action[(size_t) t] = 2; any = true; }
            }
            if (! any) continue;

            renderVoices (segStart, i);
            segStart = i;
            for (int t = 0; t < jd::kTracks; ++t)
            {
                auto& q = seq[(size_t) t];
                if (action[(size_t) t] == 2) { voices[(size_t) t].release(); continue; }
                if (action[(size_t) t] != 1) continue;
                std::shared_ptr<const jd::TrackData> d;
                {
                    const juce::SpinLock::ScopedTryLockType sl (slotLock);
                    if (! sl.isLocked()) continue;                         // try again on the next step
                    d = slots[(size_t) t].data;
                }
                if (d == nullptr) continue;
                auto s = readRegion (t, q.region - 1);
                s.pitch += q.pitch + masterPitch;
                if (q.dir == 1) s.reverse = false; else if (q.dir == 2) s.reverse = true;
                const float g = juce::Decibels::decibelsToGain (v (tp[(size_t) t].level), -59.f) * q.vol;
                voices[(size_t) t].trigger (std::move (d), s, g, v (tp[(size_t) t].pan));
                q.fromSeq = true;
            }
        }
    }
    else
    {
        if (seqWasRunning)
            for (int t = 0; t < jd::kTracks; ++t) { if (seq[(size_t) t].fromSeq) voices[(size_t) t].release(); seq[(size_t) t].fromSeq = false; seq[(size_t) t].step = -1; }
        uiStep.store (-1, std::memory_order_relaxed);
    }
    seqWasRunning = running;

    // 3. voices (muted tracks are silent; solo silences the rest)
    renderVoices (segStart, n);
    for (int t = 0; t < jd::kTracks; ++t) playing[(size_t) t].store (voices[(size_t) t].isActive() ? 1 : 0);

    // 4. effects, each skipped while it is off (and cleared when it comes back on, so no old tail jumps out)
    {
        auto P = [&] (const char* id) { return vId (id); };
        if (P ("fx_filter_on") > 0.5f)
        {
            if (! wasOn[0]) fxFilter.reset();
            fxFilter.set (juce::roundToInt (P ("fx_filter_type")), P ("fx_filter_cut"), P ("fx_filter_res"), P ("fx_filter_drive"), P ("fx_filter_mix"), juce::roundToInt (P ("fx_filter_slope")));
            fxFilter.process (l, r, n);
            wasOn[0] = true;
        } else wasOn[0] = false;
        if (P ("fx_delay_on") > 0.5f)
        {
            if (! wasOn[1]) fxDelay.reset();
            fxDelay.set (juce::roundToInt (P ("fx_delay_mode")), P ("fx_delay_time"), bpm, P ("fx_delay_fb"), P ("fx_delay_tone"), P ("fx_delay_mix"), juce::roundToInt (P ("fx_delay_style")) == 1);
            fxDelay.process (l, r, n);
            wasOn[1] = true;
        } else wasOn[1] = false;
        if (P ("fx_reverb_on") > 0.5f)
        {
            if (! wasOn[2]) fxReverb.reset();
            fxReverb.set (juce::roundToInt (P ("fx_reverb_mode")), P ("fx_reverb_size"), P ("fx_reverb_decay"), P ("fx_reverb_damp"), P ("fx_reverb_mix"), juce::roundToInt (P ("fx_reverb_pre")));
            fxReverb.process (l, r, n);
            wasOn[2] = true;
        } else wasOn[2] = false;

        const float width = P ("m_width");
        if (std::abs (width - 1.f) > 0.002f)
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * width;
                l[i] = m + s; r[i] = m - s;
            }
    }

    // 5. master volume and pan
    float pl = 0.f, pr = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float g = volume.getNextValue();
        const float p = pan.getNextValue();                       // -1 (left) ... +1 (right), equal power
        const float a = (p + 1.f) * 0.25f * juce::MathConstants<float>::pi;
        l[i] *= g * std::cos (a) * 1.41421356f;
        r[i] *= g * std::sin (a) * 1.41421356f;
    }
    if (state.getRawParameterValue ("fx_lim_on")->load() > 0.5f)
    {
        auto P = [&] (const char* id) { return vId (id); };
        if (! wasOn[3]) fxLimiter.reset();
        fxLimiter.set (juce::roundToInt (P ("fx_lim_mode")) == 1, P ("fx_lim_ceiling"), P ("fx_lim_release"), P ("fx_lim_input"), P ("fx_lim_mix"), juce::roundToInt (P ("fx_lim_look")));
        fxLimiter.process (l, r, n);
        wasOn[3] = true;
    } else wasOn[3] = false;
    for (int i = 0; i < n; ++i) { pl = juce::jmax (pl, std::abs (l[i])); pr = juce::jmax (pr, std::abs (r[i])); }
    if (pl > peakL.load (std::memory_order_relaxed)) peakL.store (pl, std::memory_order_relaxed);
    if (pr > peakR.load (std::memory_order_relaxed)) peakR.store (pr, std::memory_order_relaxed);
}

// Reads one track's values for a running step number.  Each lane wraps on its own length, so lanes of different lengths drift against each other.
void JunkDrawerProcessor::applyStep (int t, SeqTrack& q, long long step) const
{
    auto at = [&] (int lane)
    {
        const int len = juce::jlimit (1, jd::kSteps, juce::roundToInt (laneLen[(size_t) t][(size_t) lane]->load()));
        const int idx = (int) (((step % len) + len) % len);
        return sp[(size_t) t][(size_t) idx].v[(size_t) lane]->load();
    };
    q.step = step;
    const int count = juce::roundToInt (regCount[(size_t) t]->load());
    q.region = juce::roundToInt (at (jd::LaneRegion));
    if (q.region > count) q.region = 0;
    q.vol = at (jd::LaneVolume);
    q.pitch = at (jd::LanePitch);
    q.ratchet = juce::jlimit (1, 4, juce::roundToInt (at (jd::LaneRatchet)));
    q.dir = juce::roundToInt (at (jd::LaneDirection));
    q.len = at (jd::LaneLength);
    q.sub = -1;
    q.released = false;
    q.noGate = q.region > 0 && q.len >= 0.999f && juce::roundToInt (rp[(size_t) t][(size_t) (q.region - 1)].loop->load()) == jd::OneShot;
}

// A freshly loaded sample gets a simple pattern (the regions in order) so there is something to hear right away.
void JunkDrawerProcessor::fillPatternIfEmpty (int track)
{
    for (int s = 0; s < jd::kSteps; ++s)
        if (juce::roundToInt (sp[(size_t) track][(size_t) s].v[jd::LaneRegion]->load()) != 0) return;
    const int count = juce::jmax (1, juce::roundToInt (regCount[(size_t) track]->load()));
    for (int s = 0; s < jd::kSteps; ++s) setParam (state, jd::stepId (track, s, jd::LaneRegion), (float) (s % count + 1));
}

//==============================================================================
// LFOs.  Everything here runs once per block, so ten assignments or a hundred cost about the same.
void JunkDrawerProcessor::evalTargets()
{
    if (table == nullptr) return;
    for (size_t k = 0; k < table->targets.size(); ++k)
    {
        const auto& tg = table->targets[k];
        float sum = 0.f;
        for (const auto& e : table->entries)
            if (e.target == (int) k) sum += e.amount * lfoOut[e.lfo][(tg.track >= 0 && lfoPerTrack[e.lfo]) ? tg.track : 8].load (std::memory_order_relaxed);
        const float base = tg.param->convertTo0to1 (tg.raw->load());
        effPlain[k] = tg.param->convertFrom0to1 (juce::jlimit (0.f, 1.f, base + sum));
    }
}

void JunkDrawerProcessor::updateModulation (int n, double clockBeats, bool running, bool justStarted, double bpm)
{
    juce::ignoreUnused (running, bpm);
    const double dt = (double) n / hostRate;
    evalTargets();                                                // values for LFO settings that other LFOs move, from the last block
    for (int i = 0; i < jd::kLfos; ++i)
    {
        const auto& p = lp[(size_t) i];
        const int shape = juce::roundToInt (p.shape->load());
        const float hz = v (p.rate), depth = juce::jlimit (0.f, 1.f, v (p.depth)), phaseDeg = v (p.phase), fadeMs = v (p.fade);
        const int div = juce::roundToInt (p.div->load()), sync = juce::roundToInt (p.sync->load());
        const bool uni = juce::roundToInt (p.polar->load()) == 0, global = p.global->load() > 0.5f;
        lfoPerTrack[i] = ! global;
        const double period = sync > 0 ? jd::lfoPeriodBeats (div, sync) : 1.0;

        for (int inst = 0; inst < 9; ++inst)
        {
            if (inst < jd::kTracks && global) { lfoOut[i][inst].store (lfoOut[i][8].load (std::memory_order_relaxed), std::memory_order_relaxed); continue; }
            if (inst == 8 && justStarted) { lfoAcc[i][8] = 0.0; lfoSince[i][8] = 0.0; }
            if (inst < jd::kTracks && restartFlag[(size_t) inst]) { lfoAcc[i][inst] = 0.0; lfoSince[i][inst] = 0.0; lfoRestartBeat[i][inst] = clockBeats; }

            double ph;
            if (sync == 0) { ph = lfoAcc[i][inst]; lfoAcc[i][inst] += (double) hz * dt; }
            else ph = (clockBeats - (inst < jd::kTracks ? lfoRestartBeat[i][inst] : 0.0)) / period;
            ph += (double) phaseDeg / 360.0;
            if (ph < 0.0) ph += std::ceil (-ph);

            float out = jd::lfoShape (shape, ph, (juce::uint32) (i * 16 + inst));
            if (uni) out = 0.5f * (out + 1.f);
            const float fade = fadeMs > 0.5f ? (float) juce::jmin (1.0, lfoSince[i][inst] / ((double) fadeMs * 0.001)) : 1.f;
            lfoSince[i][inst] += dt;
            lfoOut[i][inst].store (out * depth * fade, std::memory_order_relaxed);
        }
        evalTargets();                                            // LFOs further down the list now see this one
    }
    restartFlag.fill (false);
}

//==============================================================================
void JunkDrawerProcessor::publishMods()
{
    auto t = std::make_shared<jd::ModTable>();
    std::vector<juce::String> ids;
    for (const auto& a : mods)
    {
        auto* prm = state.getParameter (a.target);
        if (prm == nullptr || ! canModulate (prm) || (int) t->entries.size() >= jd::kMaxMods) continue;
        int idx = -1;
        for (size_t k = 0; k < ids.size(); ++k) if (ids[k] == a.target) { idx = (int) k; break; }
        if (idx < 0)
        {
            idx = (int) ids.size();
            ids.push_back (a.target);
            int track = -1;
            if (a.target.startsWithChar ('t') && juce::CharacterFunctions::isDigit (a.target[1])) track = juce::jlimit (0, jd::kTracks - 1, a.target.substring (1).getIntValue() - 1);
            t->targets.push_back ({ state.getRawParameterValue (a.target), prm, track });
        }
        t->entries.push_back ({ idx, juce::jlimit (0, jd::kLfos - 1, a.lfo), juce::jlimit (-1.f, 1.f, a.amount) });
    }
    {
        const juce::SpinLock::ScopedLockType sl (modLock);
        modIncoming = t;
        modDirty = true;
    }
    saveMods();
    sendChangeMessage();
}

void JunkDrawerProcessor::setMod (const juce::String& target, int lfo, float amount)
{
    auto* prm = state.getParameter (target);
    if (prm == nullptr || ! canModulate (prm)) return;
    for (auto& a : mods) if (a.target == target && a.lfo == lfo) { a.amount = juce::jlimit (-1.f, 1.f, amount); publishMods(); return; }
    if ((int) mods.size() >= jd::kMaxMods) return;
    mods.push_back ({ target, lfo, juce::jlimit (-1.f, 1.f, amount) });
    publishMods();
}

void JunkDrawerProcessor::removeMod (const juce::String& target, int lfo)
{
    mods.erase (std::remove_if (mods.begin(), mods.end(), [&] (const jd::Assign& a) { return a.target == target && a.lfo == lfo; }), mods.end());
    publishMods();
}

void JunkDrawerProcessor::clearMods() { mods.clear(); publishMods(); }

float JunkDrawerProcessor::modAmount (const juce::String& target, int lfo) const
{
    for (const auto& a : mods) if (a.target == target && a.lfo == lfo) return a.amount;
    return 0.f;
}

void JunkDrawerProcessor::saveMods()
{
    auto node = state.state.getOrCreateChildWithName ("mods", nullptr);
    node.removeAllChildren (nullptr);
    for (const auto& a : mods)
    {
        juce::ValueTree m ("m");
        m.setProperty ("id", a.target, nullptr); m.setProperty ("lfo", a.lfo, nullptr); m.setProperty ("amount", a.amount, nullptr);
        node.appendChild (m, nullptr);
    }
}

void JunkDrawerProcessor::loadMods()
{
    mods.clear();
    auto node = state.state.getChildWithName ("mods");
    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        auto m = node.getChild (i);
        mods.push_back ({ m.getProperty ("id").toString(), (int) m.getProperty ("lfo"), (float) (double) m.getProperty ("amount") });
    }
    publishMods();
}

juce::AudioProcessorEditor* JunkDrawerProcessor::createEditor() { return new JunkDrawerEditor (*this); }

void JunkDrawerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    saveSamplePaths();
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void JunkDrawerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            loadMods();
            auto node = state.state.getChildWithName ("samples");
            for (int t = 0; t < jd::kTracks; ++t)
            {
                const juce::String path = node.getProperty ("t" + juce::String (t + 1)).toString();
                if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadFile (t, juce::File (path), false);   // the saved regions stay as they were
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JunkDrawerProcessor(); }
