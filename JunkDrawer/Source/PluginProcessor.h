#pragma once
#include "Params.h"
#include "Presets.h"
#include "Voice.h"
#include "Sequencer.h"
#include "Effects.h"
#include "Modulation.h"

class JunkDrawerProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    JunkDrawerProcessor();
    ~JunkDrawerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Junk Drawer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return drawerCount(); }
    int getCurrentProgram() override { return juce::jmax (0, drawerIndex); }
    void setCurrentProgram (int i) override { loadDrawer (i); }
    const juce::String getProgramName (int i) override { return i >= 0 && i < drawerCount() ? drawer (i).name : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Samples.  Loading runs on a background thread; when it finishes the processor sends a change message
    // (editor refreshes) and, for a fresh load, cuts the sample into equal regions.
    void loadFile (int track, const juce::File& file, bool sliceAfter = true);
    void clearTrack (int track);
    std::shared_ptr<const jd::TrackData> getTrack (int track) const;
    juce::String takeLoadMessage();                               // last problem or note, cleared when read

    // Regions are parameters; these helpers move several of them at once.
    void sliceEqual (int track, int count);
    void sliceByHits (int track, int count);
    void addRegion (int track);                                   // splits the longest region in two (up to 6)
    void removeRegion (int track);                                // drops the last region (down to 1)
    std::vector<float> hitsFor (int track) const;                 // cached hit positions of the loaded sample
    jd::RegionSnapshot readRegion (int track, int region) const;

    // Catch something: records the plugin's audio input into a track.  The take is kept in memory while it runs (up to 60 seconds,
    // allocated only while a take is open), then saved as a 24-bit WAV in the Recordings folder and loaded like any other file.
    // All of these run on the message thread.  Start: 0 now, 1 next bar (waits for the host transport), 2 when sound arrives.
    // Length: 0 free, 1/2/4 bars.  Input: 0 both channels, 1 left only, 2 right only.
    enum CatchState { CatchIdle = 0, CatchWaiting = 1, CatchRunning = 2, CatchDone = 3 };
    bool startCatch (int track, int input, int start, int lengthBars);
    void stopCatch (bool keep);                                   // keep = false throws the take away
    int  catchState() const noexcept { return recState.load(); }
    int  catchTrack() const noexcept { return recTrack.load(); }
    double catchSeconds() const noexcept { return hostRate > 0.0 ? recLen.load() / hostRate : 0.0; }
    /** The take so far, as `bins` peak values (0..1), for the live waveform. */
    void catchPeaks (float* out, int bins) const;
    void setInputMeter (bool on) noexcept { meterOn = on; }
    float takeInputPeak() noexcept { return inPeak.exchange (0.f); }
    static juce::File recordingsFolder();

    // Patterns: eight sets of step data (A to H).  The one being edited lives in the parameters; the others sit in a bank the audio thread can read.
    // The chain plays patterns one after another, changing on bar lines.
    int  editPattern() const noexcept { return editPat.load(); }
    void setEditPattern (int p);
    void copyPatternTo (int dst);
    void clearPattern();
    int  playingPattern() const noexcept { return uiPattern.load(std::memory_order_relaxed); }
    int  chainPosition() const noexcept { return uiChain.load(std::memory_order_relaxed); }       // which chain slot is playing, -1 when the chain is not
    int  chainLength() const noexcept { return chainLen.load(); }
    int  chainPattern (int i) const noexcept { return chainPat[(size_t) i].load(); }
    int  chainBars (int i) const noexcept { return chainBarsA[(size_t) i].load(); }
    void setChainSlot (int i, int pattern, int bars);                // i == chainLength() adds a slot
    void removeChainSlot (int i);
    void flushPatterns();                                            // writes the bank and the chain into the saved state

    // Lane tools (message thread): copy and paste a lane's 16 steps and its length, nudge values by chance, or put a lane back to plain.
    struct LaneCopy { int lane = -1, length = 16; std::array<float, jd::kSteps> v {}; };
    LaneCopy copyLane (int track, int lane) const;
    bool pasteLane (int track, const LaneCopy& c);                   // only into the same kind of lane
    static bool canHumanize (int lane) noexcept { return lane == jd::LaneVolume || lane == jd::LaneLength || lane == jd::LaneTime; }
    bool humanizeLane (int track, int lane);                         // volume, length and timing only
    void resetLane (int track, int lane);

    // Key and note: what a sound is in, and the buttons that bring it to the project key.
    jd::KeyResult trackKey (int track) const { auto d = getTrack (track); return d != nullptr ? d->key : jd::KeyResult(); }
    int projectRoot() const { return juce::roundToInt (state.getRawParameterValue ("key_root")->load()); }
    int projectMode() const { return juce::roundToInt (state.getRawParameterValue ("key_mode")->load()); }
    jd::PitchResult regionPitch (int track, int region) const;       // the note a region holds (message thread)
    int  matchAllToKey();                                            // Match on every track that has a key; returns how many were set
    bool matchTrackToKey (int track);                                // sets the track's Tune so its key lands in the project key
    bool tuneRegionToKey (int track, int region);                    // sets the region's Pitch so its note lands on the nearest note of the project key

    // Catch Last: while switched on, the last 30 seconds of input are kept so they can be saved into a track.
    static constexpr double kCatchLastSeconds = 30.0;
    void setListening (bool on);
    bool listening() const noexcept { return listenWanted; }
    bool catchLast (int track);
    float catchLastSeconds() const;

    // Drawers (presets): 100 factory drawers first, then the ones the person saved.
    int drawerCount() const { return 100 + (int) userDrawers.size(); }
    const jdp::Drawer& drawer (int i) const { return i < 100 ? jdp::factory()[(size_t) i] : userDrawers[(size_t) (i - 100)]; }
    int  currentDrawer() const noexcept { return drawerIndex; }          // -1 when nothing has been picked
    void loadDrawer (int index);
    void stepDrawer (int by) { const int n = drawerCount(); loadDrawer (((drawerIndex < 0 ? (by > 0 ? -1 : 0) : drawerIndex) + by + n) % n); }
    bool saveDrawer (const juce::String& name);
    bool renameDrawer (int index, const juce::String& name);      // saved drawers only
    bool deleteDrawer (int index);                                // saved drawers only
    void reloadUserDrawers();

    // Offline export support: an export runs a second processor of its own (see Exporter.h).
    void importSettings (const juce::ValueTree& tree) { state.replaceState (tree); loadMods(); loadPatternsFromState(); }
    void setTrackAudio (int track, std::shared_ptr<const jd::TrackData> d)
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        slots[(size_t) track].data = std::move (d);
    }
    /** The tempo and bar length the sequencer last ran at (the host's when there is one), so an export can match what you hear. */
    double lastBpm() const noexcept { return bpmSeen.load(); }
    double lastBeatsPerBar() const noexcept { return barSeen.load(); }

    //==========================================================================
    // LFO assignments.  A link says "this LFO moves this control by this much"; many controls per LFO and many LFOs per control are fine.
    // Only continuous controls can be linked.  These run on the message thread.
    static bool canModulate (juce::RangedAudioParameter* p) { return dynamic_cast<juce::AudioParameterFloat*> (p) != nullptr; }
    void setMod (const juce::String& target, int lfo, float amount);   // adds the link, or changes its amount
    void removeMod (const juce::String& target, int lfo);
    void clearMods();
    float modAmount (const juce::String& target, int lfo) const;       // 0 when there is no link
    std::vector<jd::Assign> getMods() const { return mods; }
    int armedLfo() const noexcept { return armed; }                    // the LFO a drag on a knob will assign, or -1
    void setArmedLfo (int lfo) { armed = lfo; sendChangeMessage(); }
    /** The LFO's current output (what the editor draws); -1..+1 before depth and polarity are applied is not kept, so this is the final 0..depth or -depth..+depth value. */
    float lfoValue (int lfo) const noexcept { return lfoOut[(size_t) lfo][8].load (std::memory_order_relaxed); }

    // Preview from the Drawer page.  The sequencer (Stage 3) will trigger the same voices on steps.
    void auditionRegion (int track, int region)  { audition[(size_t) track].store (1 + region); }
    void auditionWhole  (int track)              { audition[(size_t) track].store (100); }
    void stopAudition   (int track)              { audition[(size_t) track].store (200); }
    bool isAuditioning  (int track) const        { return playing[(size_t) track].load(); }

    int laneLength (int track, int lane) const { return juce::jlimit (1, jd::kSteps, juce::roundToInt (state.getRawParameterValue (jd::laneLenId (track, lane))->load())); }

    juce::AudioProcessorValueTreeState state;

    // The running step number while the sequencer plays (-1 when stopped).  The editor shows each lane's step as this modulo the lane's length.
    std::atomic<long long> uiStep { -1 };
    std::atomic<int> uiPattern { 0 }, uiChain { -1 };

    // Output peak since the editor last looked (the editor reads and resets these about 30 times a second).
    std::atomic<float> peakL { 0.f }, peakR { 0.f };

private:
    struct Slot
    {
        std::shared_ptr<const jd::TrackData> data;
        std::vector<float> hits;
    };
    void publish (int track, std::shared_ptr<const jd::TrackData> d, bool slice);
    void saveSamplePaths();

    mutable juce::SpinLock slotLock;
    std::array<Slot, jd::kTracks> slots;
    std::array<std::atomic<int>, jd::kTracks> audition, playing, loadToken;
    std::array<jd::RegionVoice, jd::kTracks> voices, morphVoices;     // the second set only sounds while a track's Morph is up

    juce::AudioFormatManager formats;
    juce::ThreadPool loader { 1 };
    double hostRate = 44100.0;
    juce::CriticalSection messageLock;
    juce::String loadMessage;
    std::atomic<bool> alive { true };

    // ---- recording ----
    void timerCallback() override;
    void finishCatch (bool keep);
    void captureInput (juce::AudioBuffer<float>& buf, int n, bool running, double ppq0, double dppq, double bpm);
    std::atomic<int> recState { 0 }, recTrack { -1 }, recStop { 0 }, recLen { 0 };   // recStop: 1 = stop and keep, 2 = stop and discard
    std::atomic<float> inPeak { 0.f };
    std::atomic<bool> meterOn { false };
    juce::AudioBuffer<float> recBuf;                                   // allocated before a take starts, freed when it ends
    int recCap = 0, recPos = 0, recTarget = 0, recInputSel = 0, recStartMode = 0, recLenBars = 0;
    long long recLastBar = -1;
    int preLen = 0, prePos = 0, preFill = 0;
    juce::AudioBuffer<float> recPre;                                   // short ring kept while waiting for sound, so the first hit is not clipped
    double beatsPerBar = 4.0;
    std::atomic<double> bpmSeen { 120.0 }, barSeen { 4.0 };

    // ---- modulation (audio thread unless noted) ----
    std::vector<jd::Assign> mods;                                      // message thread
    int armed = -1;
    void publishMods();
    struct Ring { juce::AudioBuffer<float> buf; int pos = 0, filled = 0; };
    mutable juce::SpinLock ringLock;
    std::unique_ptr<Ring> ring;
    bool listenWanted = false;
    void syncRing();
    void writeTake (std::shared_ptr<juce::AudioBuffer<float>> take, int len, int track);
    std::vector<jdp::Drawer> userDrawers;
    int drawerIndex = -1;
    void saveMods();
    void loadMods();
    mutable juce::SpinLock modLock;
    std::shared_ptr<const jd::ModTable> modIncoming;                   // handed over under modLock
    std::atomic<bool> modDirty { false };
    std::shared_ptr<const jd::ModTable> table;                         // the audio thread's copy
    std::array<float, jd::kMaxMods> effPlain {};
    float v (const std::atomic<float>* raw) const noexcept
    {
        if (table != nullptr)
            for (size_t k = 0; k < table->targets.size(); ++k)
                if (table->targets[k].raw == raw) return effPlain[k];
        return raw->load();
    }
    float vId (const char* id) const { return v (state.getRawParameterValue (id)); }
    void updateModulation (int n, double clockBeats, bool running, bool justStarted, double bpm);
    void evalTargets();
    std::atomic<float> lfoOut[jd::kLfos][9] {};                        // [lfo][track 0-7, or 8 for the shared copy]
    double lfoAcc[jd::kLfos][9] {}, lfoSince[jd::kLfos][9] {}, lfoRestartBeat[jd::kLfos][9] {};
    bool lfoPerTrack[jd::kLfos] {};
    std::array<bool, jd::kTracks> restartFlag {};
    struct LfoPtrs { std::atomic<float> *shape, *rate, *div, *depth, *phase, *fade, *sync, *polar, *global; };
    std::array<LfoPtrs, jd::kLfos> lp {};
    double idleBeats = 0.0;

    juce::SmoothedValue<float> volume, pan;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pPan = nullptr;

    struct TrackPtrs { std::atomic<float>* mute; std::atomic<float>* solo; std::atomic<float>* level; std::atomic<float>* pan; std::atomic<float>* tune; std::atomic<float>* order; std::atomic<float>* cond; std::atomic<float>* rshape; std::atomic<float>* morph; std::atomic<float>* senddel; std::atomic<float>* sendrev; };
    std::array<TrackPtrs, jd::kTracks> tp {};
    struct RegionPtrs { std::atomic<float> *start, *end, *pitch, *gain, *pan, *cutoff, *reso, *drive, *attack, *decay, *sustain, *release, *loop, *reverse; };
    std::array<std::array<RegionPtrs, jd::kRegions>, jd::kTracks> rp {};

    struct StepPtrs { std::array<std::atomic<float>*, jd::kLanes> v {}; };
    std::array<std::array<StepPtrs, jd::kSteps>, jd::kTracks> sp {};
    std::array<std::array<std::atomic<float>*, jd::kLanes>, jd::kTracks> laneLen {};
    std::array<std::atomic<float>*, jd::kTracks> regCount {};
    std::atomic<float> *pDiv = nullptr, *pFeel = nullptr, *pBpm = nullptr, *pRun = nullptr, *pRate = nullptr, *pRateMode = nullptr, *pSwing = nullptr;

    struct SeqTrack
    {
        long long step = -1;
        int region = 0, ratchet = 1, sub = -1, dir = 0;
        float vol = 1.f, pitch = 0.f, len = 1.f, pan = 0.f;
        bool released = true, noGate = false, fromSeq = false;
    };
    std::array<SeqTrack, jd::kTracks> seq;
    jd::FxFilter fxFilter; jd::FxDelay fxDelay; jd::FxReverb fxReverb; jd::FxLimiter fxLimiter;
    // Per-track sends: the tracks that use them feed these two buses, which run the Master page's Delay and Reverb settings wet-only.
    jd::FxDelay sendDelay; jd::FxReverb sendVerb;
    std::vector<float> sbDL, sbDR, sbVL, sbVR;
    int sbCap = 0, tailDelay = 0, tailVerb = 0;
    bool sendDelayWas = false, sendVerbWas = false;
    bool wasOn[4] = { false, false, false, false };
    double internalPpq = 0.0;
    bool seqWasRunning = false;
    void applyStep (int track, SeqTrack& q, long long step, int pat) const;
    // patterns
    static constexpr int kPatN = jd::kTracks * jd::kLanes * jd::kSteps + jd::kTracks * jd::kLanes;
    static int patIndex (int t, int lane, int s) noexcept { return (t * jd::kLanes + lane) * jd::kSteps + s; }
    static int patLenIndex (int t, int lane) noexcept { return jd::kTracks * jd::kLanes * jd::kSteps + t * jd::kLanes + lane; }
    std::array<std::array<std::atomic<float>, kPatN>, jd::kPatterns> bank;
    std::atomic<int> editPat { 0 };
    int lastLive = 0;
    std::array<std::atomic<int>, jd::kChainMax> chainPat, chainBarsA;
    std::atomic<int> chainLen { 0 };
    std::atomic<float>* pChain = nullptr;
    long long chainBar = -1, chainStart = 0;
    int chainSlot = -1, chainTotalBarsCache = 0;
    float patStep (int pat, int t, int s, int lane) const { return pat == editPat.load (std::memory_order_relaxed) ? sp[(size_t) t][(size_t) s].v[(size_t) lane]->load() : bank[(size_t) pat][(size_t) patIndex (t, lane, s)].load(); }
    int patLen (int pat, int t, int lane) const
    {
        const float x = pat == editPat.load (std::memory_order_relaxed) ? laneLen[(size_t) t][(size_t) lane]->load() : bank[(size_t) pat][(size_t) patLenIndex (t, lane)].load();
        return juce::jlimit (1, jd::kSteps, juce::roundToInt (x));
    }
    void saveLiveToBank (int pat);
    void loadBankToLive (int pat);
    void loadPatternsFromState();
    void saveChainToState();
    void fillPatternIfEmpty (int track);
public:
    // Rummage re-rolls every step of every lane that is not kept. Shake re-rolls only some of them (the Shake amount).
    void rummage (int track)  { rollSteps (track, 1.f); }
    void shake (int track);
private:
    void rollSteps (int track, float chance);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkDrawerProcessor)
};
