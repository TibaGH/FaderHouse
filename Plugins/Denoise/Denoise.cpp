// Faderhouse Denoise: removes background noise from your voice (fans, keyboard, traffic)
// with RNNoise, a small neural network made for speech (Xiph.Org, BSD licence).

#include "FhUi.h"
#include "FhDenoise.h"

using namespace juce;
using fh::APVTS;

namespace
{
APVTS::ParameterLayout layout()
{
    APVTS::ParameterLayout l;
    l.add (fh::floatParam ("amount", "Amount", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    return l;
}

class Processor final : public fh::PluginBase
{
public:
    Processor() : PluginBase (layout()) {}

    void prepareToPlay (double sr, int block) override
    {
        denoiser.setAmount (param ("amount") / 100.0f);
        denoiser.prepare (sr, block, getTotalNumOutputChannels());
        setLatencySamples (denoiser.getLatencySamples());
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        inputMeter.measure (buffer);
        denoiser.setAmount (param ("amount") / 100.0f);
        denoiser.process (buffer);
        outputMeter.measure (buffer);
    }

    AudioProcessorEditor* createEditor() override;
    fh::Denoiser denoiser;
};

class Editor final : public fh::ui::EditorBase
{
public:
    explicit Editor (Processor& p)
        : EditorBase (p, "Faderhouse Denoise", "Background noise out, your voice in - powered by RNNoise"),
          amount (p, "amount", "Noise reduction"),
          inMeter ([&p] { return p.inputMeter.peakDb.load(); }, "In"),
          outMeter ([&p] { return p.outputMeter.peakDb.load(); }, "Out"),
          voice ([&p] { return p.denoiser.voiceProbability.load(); }, "Voice detected", fh::ui::Colours::green)
    {
        for (Component* c : std::initializer_list<Component*> { &amount, &inMeter, &outMeter, &voice, &hint })
            addAndMakeVisible (c);
        hint.setText ("100% removes the most noise. If your voice sounds watery, try 60-80%. Adds about 20 ms delay.",
                      dontSendNotification);
        hint.setColour (Label::textColourId, fh::ui::Colours::textDim);
        hint.setFont (FontOptions (12.0f));
        hint.setJustificationType (Justification::topLeft);
        setSize (440, 310);
    }

    void paint (Graphics& g) override
    {
        EditorBase::paint (g);
        paintGroup (g, controlsArea, {});
        paintGroup (g, metersArea, {});
    }

    void resized() override
    {
        auto r = getContentArea();
        metersArea = r.removeFromRight (110);
        r.removeFromRight (10);
        controlsArea = r;
        auto c = controlsArea.reduced (14, 12);
        voice.setBounds (c.removeFromTop (22));
        hint.setBounds (c.removeFromBottom (40));
        amount.setBounds (c.reduced (4));

        auto m = metersArea.reduced (10, 12);
        inMeter.setBounds (m.removeFromLeft (m.getWidth() / 2));
        outMeter.setBounds (m);
    }

private:
    fh::ui::Knob amount;
    fh::ui::LevelBar inMeter, outMeter;
    fh::ui::Lamp voice;
    Label hint;
    Rectangle<int> controlsArea, metersArea;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
