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
    for (int t = 0; t < jd::kTracks; ++t)
    {
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
    s.start = p.start->load(); s.end = p.end->load();
    s.pitch = p.pitch->load(); s.gainDb = p.gain->load(); s.pan = p.pan->load();
    s.cutoff = p.cutoff->load(); s.reso = p.reso->load(); s.driveDb = p.drive->load();
    s.attackMs = p.attack->load(); s.decayMs = p.decay->load(); s.sustain = p.sustain->load(); s.releaseMs = p.release->load();
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
    if (slice) sliceEqual (track, 4);
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

    volume.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load(), -59.f));
    pan.setTargetValue (pPan->load());

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
        const float g = juce::Decibels::decibelsToGain (tp[(size_t) t].level->load(), -59.f);
        voices[(size_t) t].trigger (std::move (d), s, g, tp[(size_t) t].pan->load());
    }

    // 2. voices (muted tracks are silent; solo silences the rest)
    for (int t = 0; t < jd::kTracks; ++t)
    {
        const bool audible = tp[(size_t) t].mute->load() < 0.5f && (! anySolo || tp[(size_t) t].solo->load() > 0.5f);
        if (audible) voices[(size_t) t].render (l, r, n);
        else         voices[(size_t) t].kill();
        playing[(size_t) t].store (voices[(size_t) t].isActive() ? 1 : 0);
    }

    // 3. master volume and pan, then the meter
    float pl = 0.f, pr = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float g = volume.getNextValue();
        const float p = pan.getNextValue();                       // -1 (left) ... +1 (right), equal power
        const float a = (p + 1.f) * 0.25f * juce::MathConstants<float>::pi;
        l[i] *= g * std::cos (a) * 1.41421356f;
        r[i] *= g * std::sin (a) * 1.41421356f;
        pl = juce::jmax (pl, std::abs (l[i]));
        pr = juce::jmax (pr, std::abs (r[i]));
    }
    if (pl > peakL.load (std::memory_order_relaxed)) peakL.store (pl, std::memory_order_relaxed);
    if (pr > peakR.load (std::memory_order_relaxed)) peakR.store (pr, std::memory_order_relaxed);
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
            auto node = state.state.getChildWithName ("samples");
            for (int t = 0; t < jd::kTracks; ++t)
            {
                const juce::String path = node.getProperty ("t" + juce::String (t + 1)).toString();
                if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadFile (t, juce::File (path), false);   // the saved regions stay as they were
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JunkDrawerProcessor(); }
