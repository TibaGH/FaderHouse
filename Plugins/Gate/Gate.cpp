// Faderhouse Gate: turns your mic down when you're not talking (keyboard, breathing, room).
// It lowers by "Reduction" instead of cutting to silence, so it sounds natural.

#include "FhUi.h"

using namespace juce;
using fh::APVTS;

namespace
{
APVTS::ParameterLayout layout()
{
    APVTS::ParameterLayout l;
    l.add (fh::floatParam ("threshold", "Threshold", { -80.0f, -20.0f, 0.1f }, -50.0f, "dB", 1));
    l.add (fh::floatParam ("range", "Reduction", { 0.0f, 60.0f, 0.1f }, 18.0f, "dB", 1));
    return l;
}

class Processor final : public fh::PluginBase
{
public:
    Processor() : PluginBase (layout(), "Faderhouse Gate", "Dynamics") {}

    void prepareToPlay (double sr, int) override { update(); gate.prepare (sr); }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        inputMeter.measure (buffer);
        update();
        gate.process (buffer);
        outputMeter.measure (buffer);
    }

    AudioProcessorEditor* createEditor() override;
    fh::Gate gate;

private:
    void update() { gate.setSettings ({ param ("threshold"), param ("range") }); }
};

class Editor final : public fh::ui::EditorBase
{
public:
    explicit Editor (Processor& p)
        : EditorBase (p, "Faderhouse Gate", "Quieter between your sentences - keyboard and room noise go down"),
          threshold (p, "threshold", "Threshold"),
          range (p, "range", "Reduction", fh::ui::Colours::violet),
          level ([&p] { return p.gate.meterLevelDb.load(); }, "Level"),
          outMeter ([&p] { return p.outputMeter.peakDb.load(); }, "Out"),
          open ([&p] { return p.gate.meterOpen.load() ? 1.0f : 0.0f; }, "Open", fh::ui::Colours::green)
    {
        for (Component* c : std::initializer_list<Component*> { &threshold, &range, &level, &outMeter, &open, &hint })
            addAndMakeVisible (c);
        level.thresholdDb = [&p] { return p.param ("threshold"); };
        hint.setText ("Stay quiet and set Threshold just above the Level meter. Then talk: the light should be on.",
                      dontSendNotification);
        hint.setColour (Label::textColourId, fh::ui::Colours::textDim);
        hint.setFont (FontOptions (12.0f));
        hint.setJustificationType (Justification::topLeft);
        setSize (460, 310);
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
        metersArea = r.removeFromRight (130);
        r.removeFromRight (10);
        controlsArea = r;
        auto c = controlsArea.reduced (14, 12);
        hint.setBounds (c.removeFromBottom (40));
        c.removeFromBottom (8);
        const int w = c.getWidth() / 2;
        threshold.setBounds (c.removeFromLeft (w).reduced (4));
        range.setBounds (c.reduced (4));

        auto m = metersArea.reduced (10, 12);
        open.setBounds (m.removeFromTop (22));
        m.removeFromTop (6);
        level.setBounds (m.removeFromLeft (m.getWidth() / 2));
        outMeter.setBounds (m);
    }

private:
    fh::ui::Knob threshold, range;
    fh::ui::LevelBar level, outMeter;
    fh::ui::Lamp open;
    Label hint;
    Rectangle<int> controlsArea, metersArea;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

namespace fh::builtin { juce::AudioPluginInstance* createGate() { return new Processor(); } }

#if FH_BUILD_AS_PLUGIN
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return fh::builtin::createGate(); }
#endif
