#include "PluginProcessor.h"
#include "PluginEditor.h"

JunkDrawerProcessor::JunkDrawerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "JunkDrawer", jd::createLayout())
{
    pVolume = state.getRawParameterValue ("m_volume");
    pPan    = state.getRawParameterValue ("m_pan");
}

void JunkDrawerProcessor::prepareToPlay (double sampleRate, int)
{
    volume.reset (sampleRate, 0.02);
    pan.reset (sampleRate, 0.02);
    volume.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
    pan.setCurrentAndTargetValue (pPan->load());
}

bool JunkDrawerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

// Stage 0: the plug-in is a stereo pass-through with the master Volume and Pan applied.
// Samples, regions, the sequencer, LFOs and the effects arrive in later stages (see ROADMAP.md).
void JunkDrawerProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    if (buf.getNumChannels() < 2) return;

    volume.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load(), -59.f));
    pan.setTargetValue (pPan->load());

    auto* l = buf.getWritePointer (0);
    auto* r = buf.getWritePointer (1);
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
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void JunkDrawerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType())) state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JunkDrawerProcessor(); }
