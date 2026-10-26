// Faderhouse Limiter: a safety net. Your peaks never go over -1 dBFS; "Push" makes you a bit
// louder and more consistent without squashing.

#include "FhUi.h"

using namespace juce;
using fh::APVTS;

namespace
{
APVTS::ParameterLayout layout()
{
    APVTS::ParameterLayout l;
    l.add (fh::floatParam ("push", "Push", { 0.0f, 12.0f, 0.1f }, 3.0f, "dB", 1));
    l.add (fh::floatParam ("ceiling", "Ceiling", { -6.0f, 0.0f, 0.1f }, -1.0f, "dB", 1));
    return l;
}

class Processor final : public fh::PluginBase
{
public:
    Processor() : PluginBase (layout()) {}

    void prepareToPlay (double sr, int block) override
    {
        update();
        limiter.prepare (sr, block);
        setLatencySamples (limiter.getLatencySamples());
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        inputMeter.measure (buffer);
        update();
        limiter.process (buffer);
        outputMeter.measure (buffer);
    }

    AudioProcessorEditor* createEditor() override;
    fh::Limiter limiter;

private:
    void update() { limiter.setSettings ({ param ("push"), param ("ceiling") }); }
};

class Editor final : public fh::ui::EditorBase
{
public:
    explicit Editor (Processor& p)
        : EditorBase (p, "Faderhouse Limiter", "A safety net for your peaks - a little more consistent, never clipping"),
          push (p, "push", "Push"),
          ceiling (p, "ceiling", "Ceiling", fh::ui::Colours::textDim),
          inMeter ([&p] { return p.inputMeter.peakDb.load(); }, "In"),
          reduction ([&p] { return p.limiter.meterReductionDb.load(); }, "Reduce", 12.0f),
          outMeter ([&p] { return p.outputMeter.peakDb.load(); }, "Out")
    {
        for (Component* c : std::initializer_list<Component*> { &push, &ceiling, &inMeter, &reduction, &outMeter, &hint })
            addAndMakeVisible (c);
        outMeter.thresholdDb = [&p] { return p.param ("ceiling"); };
        hint.setText ("Push 2-4 dB is plenty for voice. If the Reduce meter is busy all the time, push less.", dontSendNotification);
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
        metersArea = r.removeFromRight (150);
        r.removeFromRight (10);
        controlsArea = r;
        auto c = controlsArea.reduced (14, 12);
        hint.setBounds (c.removeFromBottom (40));
        c.removeFromBottom (8);
        ceiling.setBounds (c.removeFromRight (c.getWidth() / 3).reduced (2, 30));
        push.setBounds (c.reduced (4));

        auto m = metersArea.reduced (10, 12);
        const int w = m.getWidth() / 3;
        inMeter.setBounds (m.removeFromLeft (w));
        reduction.setBounds (m.removeFromLeft (w));
        outMeter.setBounds (m);
    }

private:
    fh::ui::Knob push, ceiling;
    fh::ui::LevelBar inMeter;
    fh::ui::GainBar reduction;
    fh::ui::LevelBar outMeter;
    Label hint;
    Rectangle<int> controlsArea, metersArea;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
