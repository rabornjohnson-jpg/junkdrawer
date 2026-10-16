#pragma once
#include "Params.h"
#include "Voice.h"
#include "Sequencer.h"
#include "Effects.h"
#include "Modulation.h"

class JunkDrawerProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
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
    std::array<jd::RegionVoice, jd::kTracks> voices;

    juce::AudioFormatManager formats;
    juce::ThreadPool loader { 1 };
    double hostRate = 44100.0;
    juce::CriticalSection messageLock;
    juce::String loadMessage;
    std::atomic<bool> alive { true };

    // ---- modulation (audio thread unless noted) ----
    std::vector<jd::Assign> mods;                                      // message thread
    int armed = -1;
    void publishMods();
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

    struct TrackPtrs { std::atomic<float>* mute; std::atomic<float>* solo; std::atomic<float>* level; std::atomic<float>* pan; };
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
        float vol = 1.f, pitch = 0.f, len = 1.f;
        bool released = true, noGate = false, fromSeq = false;
    };
    std::array<SeqTrack, jd::kTracks> seq;
    jd::FxFilter fxFilter; jd::FxDelay fxDelay; jd::FxReverb fxReverb; jd::FxLimiter fxLimiter;
    bool wasOn[4] = { false, false, false, false };
    double internalPpq = 0.0;
    bool seqWasRunning = false;
    void applyStep (int track, SeqTrack& q, long long step) const;
    void fillPatternIfEmpty (int track);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkDrawerProcessor)
};
