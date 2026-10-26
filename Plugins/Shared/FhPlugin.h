#pragma once

// Shared plumbing for the Faderhouse plugins: parameters, state, mono/stereo buses, meters.

#include <juce_audio_processors/juce_audio_processors.h>
#include "FhDsp.h"

namespace fh
{
using APVTS = juce::AudioProcessorValueTreeState;

inline juce::NormalisableRange<float> freqRange (float lo, float hi)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

inline std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                             juce::NormalisableRange<float> range, float def,
                                                             const juce::String& unit = {}, int decimals = 1)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (unit)
            .withStringFromValueFunction ([decimals, unit] (float v, int)
            {
                if (unit == "Hz" && v >= 1000.0f)
                    return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
                return juce::String (v, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
            }));
}

inline std::unique_ptr<juce::AudioParameterBool> boolParam (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

inline std::unique_ptr<juce::AudioParameterChoice> choiceParam (const juce::String& id, const juce::String& name,
                                                               const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
}

/** Base for every Faderhouse plugin. */
class PluginBase : public juce::AudioProcessor
{
public:
    PluginBase (juce::AudioProcessorValueTreeState::ParameterLayout layout)
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "FaderhouseState", std::move (layout))
    {}

    using juce::AudioProcessor::processBlock;

    // Mono or stereo, same in and out.
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
            && layouts.getMainInputChannelSet() == out;
    }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override                          { return true; }
    void releaseResources() override                         {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (state.state.getType()))
                state.replaceState (juce::ValueTree::fromXml (*xml));
    }

    float param (const char* id) const   { return state.getRawParameterValue (id)->load(); }
    bool flag (const char* id) const     { return param (id) > 0.5f; }
    int choice (const char* id) const    { return juce::roundToInt (param (id)); }

    /** Sets a parameter from the UI or a preset (host gets notified). */
    void setParam (const juce::String& id, float value)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    juce::AudioProcessorValueTreeState state;
    LevelMeter inputMeter, outputMeter;
};
}
