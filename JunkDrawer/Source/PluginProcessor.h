#pragma once
#include "Params.h"

class JunkDrawerProcessor : public juce::AudioProcessor
{
public:
    JunkDrawerProcessor();
    ~JunkDrawerProcessor() override = default;

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

    juce::AudioProcessorValueTreeState state;

    // Output peak since the editor last looked (the editor reads and resets these about 30 times a second).
    std::atomic<float> peakL { 0.f }, peakR { 0.f };

private:
    juce::SmoothedValue<float> volume, pan;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pPan = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkDrawerProcessor)
};
