#pragma once
#include "Params.h"
#include "Voice.h"

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

    // Preview from the Drawer page.  The sequencer (Stage 3) will trigger the same voices on steps.
    void auditionRegion (int track, int region)  { audition[(size_t) track].store (1 + region); }
    void auditionWhole  (int track)              { audition[(size_t) track].store (100); }
    void stopAudition   (int track)              { audition[(size_t) track].store (200); }
    bool isAuditioning  (int track) const        { return playing[(size_t) track].load(); }

    juce::AudioProcessorValueTreeState state;

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

    juce::SmoothedValue<float> volume, pan;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pPan = nullptr;

    struct TrackPtrs { std::atomic<float>* mute; std::atomic<float>* solo; std::atomic<float>* level; std::atomic<float>* pan; };
    std::array<TrackPtrs, jd::kTracks> tp {};
    struct RegionPtrs { std::atomic<float> *start, *end, *pitch, *gain, *pan, *cutoff, *reso, *drive, *attack, *decay, *sustain, *release, *loop, *reverse; };
    std::array<std::array<RegionPtrs, jd::kRegions>, jd::kTracks> rp {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkDrawerProcessor)
};
