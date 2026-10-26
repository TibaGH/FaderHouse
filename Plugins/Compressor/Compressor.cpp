// Faderhouse Compressor: easy voice compression. Two modes to compare:
//   Smart   - one Amount knob, fixed attack, automatic release and make-up gain
//   Leveler - an automatic fader that brings your voice to a target level

#include "FhUi.h"

using namespace juce;
using fh::APVTS;

namespace
{
APVTS::ParameterLayout layout()
{
    APVTS::ParameterLayout l;
    l.add (fh::choiceParam ("mode", "Mode", { "Smart", "Leveler" }, 0));
    l.add (fh::floatParam ("amount", "Amount", { 0.0f, 100.0f, 0.1f }, 40.0f, "%", 0));
    l.add (fh::floatParam ("target", "Target", { -30.0f, -12.0f, 0.1f }, -20.0f, "dB", 1));
    l.add (fh::floatParam ("strength", "Strength", { 0.0f, 100.0f, 0.1f }, 70.0f, "%", 0));
    l.add (fh::floatParam ("output", "Output", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB", 1));
    return l;
}

class Processor final : public fh::PluginBase
{
public:
    Processor() : PluginBase (layout()) {}

    void prepareToPlay (double sr, int) override { update(); comp.prepare (sr); }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        inputMeter.measure (buffer);
        update();
        comp.process (buffer);
        outputMeter.measure (buffer);
    }

    AudioProcessorEditor* createEditor() override;

    fh::VoiceCompressor comp;

private:
    void update()
    {
        fh::VoiceCompressor::Settings s;
        s.mode = choice ("mode") == 1 ? fh::VoiceCompressor::Mode::Leveler : fh::VoiceCompressor::Mode::Smart;
        s.amount = param ("amount") / 100.0f;
        s.targetDb = param ("target");
        s.strength = param ("strength") / 100.0f;
        s.outputDb = param ("output");
        comp.setSettings (s);
    }
};

class Editor final : public fh::ui::EditorBase
{
public:
    explicit Editor (Processor& p)
        : EditorBase (p, "Faderhouse Compressor", "Even out your voice - Smart: one knob  |  Leveler: automatic fader"),
          proc (p),
          mode (p, "mode", { "Smart", "Leveler" }),
          amount (p, "amount", "Amount"),
          target (p, "target", "Target level", fh::ui::Colours::blue),
          strength (p, "strength", "Strength", fh::ui::Colours::blue),
          output (p, "output", "Output", fh::ui::Colours::textDim),
          inMeter ([&p] { return p.inputMeter.peakDb.load(); }, "In"),
          gain ([&p] { return p.comp.meterGainDb.load(); }, "Gain", 18.0f, true),
          outMeter ([&p] { return p.outputMeter.peakDb.load(); }, "Out")
    {
        for (Component* c : std::initializer_list<Component*> { &mode, &amount, &target, &strength, &output, &inMeter, &gain, &outMeter, &hint })
            addAndMakeVisible (c);

        hint.setColour (Label::textColourId, fh::ui::Colours::textDim);
        hint.setFont (FontOptions (12.0f));
        hint.setJustificationType (Justification::topLeft);
        mode.onChange = [this] { updateMode(); };
        updateMode();
        setSize (520, 330);
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
        metersArea = r.removeFromRight (150);
        r.removeFromRight (10);
        controlsArea = r;

        auto c = controlsArea.reduced (14, 12);
        mode.setBounds (c.removeFromTop (28));
        c.removeFromTop (10);
        hint.setBounds (c.removeFromBottom (40));
        c.removeFromBottom (8);
        auto knobs = c;
        const bool leveler = proc.choice ("mode") == 1;
        if (leveler)
        {
            const int w = knobs.getWidth() / 3;
            target.setBounds (knobs.removeFromLeft (w).reduced (4));
            strength.setBounds (knobs.removeFromLeft (w).reduced (4));
            output.setBounds (knobs.reduced (4));
        }
        else
        {
            output.setBounds (knobs.removeFromRight (knobs.getWidth() / 3).reduced (4, 24));
            amount.setBounds (knobs.reduced (4));
        }

        auto m = metersArea.reduced (10, 12);
        const int w = m.getWidth() / 3;
        inMeter.setBounds (m.removeFromLeft (w));
        gain.setBounds (m.removeFromLeft (w));
        outMeter.setBounds (m);
    }

private:
    void updateMode()
    {
        const bool leveler = proc.choice ("mode") == 1;
        amount.setVisible (! leveler);
        target.setVisible (leveler);
        strength.setVisible (leveler);
        hint.setText (leveler ? "Turns you up when you're quiet and down when you're loud, towards the target level. "
                                "Strength: how far it goes. Silences are left alone."
                              : "Turn up Amount until your voice sounds even. Around 30-50% is a good start. "
                                "The volume you lose is given back automatically.",
                      dontSendNotification);
        resized();
        repaint();
    }

    Processor& proc;
    fh::ui::ChoiceButtons mode;
    fh::ui::Knob amount, target, strength, output;
    fh::ui::LevelBar inMeter;
    fh::ui::GainBar gain;
    fh::ui::LevelBar outMeter;
    Label hint;
    Rectangle<int> controlsArea, metersArea;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
