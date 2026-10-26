// Faderhouse Voice: the whole voice chain in one simple strip, with presets.
//   Low cut -> Noise reduction -> Gate -> Tone (EQ) -> Compressor / Leveler -> Limiter

#include "FhUi.h"
#include "FhDenoise.h"

using namespace juce;
using fh::APVTS;

namespace
{
APVTS::ParameterLayout layout()
{
    APVTS::ParameterLayout l;
    l.add (fh::floatParam ("lowcut", "Low cut", fh::freqRange (20.0f, 250.0f), 90.0f, "Hz", 0));
    l.add (fh::floatParam ("denoise", "Noise reduction", { 0.0f, 100.0f, 0.1f }, 70.0f, "%", 0));
    l.add (fh::floatParam ("gate", "Gate", { -80.0f, -25.0f, 0.1f }, -55.0f, "dB", 0));
    l.add (fh::floatParam ("warmth", "Warmth", { -6.0f, 6.0f, 0.01f }, 0.0f, "dB", 1));
    l.add (fh::floatParam ("mud", "Mud cut", { -6.0f, 0.0f, 0.01f }, -1.5f, "dB", 1));
    l.add (fh::floatParam ("clarity", "Clarity", { -6.0f, 6.0f, 0.01f }, 1.5f, "dB", 1));
    l.add (fh::floatParam ("air", "Air", { -6.0f, 6.0f, 0.01f }, 1.0f, "dB", 1));
    l.add (fh::choiceParam ("compmode", "Compressor mode", { "Smart", "Leveler" }, 0));
    l.add (fh::floatParam ("compression", "Compression", { 0.0f, 100.0f, 0.1f }, 40.0f, "%", 0));
    l.add (fh::floatParam ("loudness", "Loudness", { 0.0f, 9.0f, 0.1f }, 3.0f, "dB", 1));
    return l;
}

// Starting points; tune to taste. Values: lowcut, denoise, gate, warmth, mud, clarity, air, mode, compression, loudness.
struct Preset { const char* name; float lowcut, denoise, gate, warmth, mud, clarity, air, mode, compression, loudness; };
const Preset presets[] {
    { "Natural",                  80.0f,  50.0f, -60.0f,  0.0f, -1.0f, 1.0f,  1.0f, 0, 30.0f, 2.0f },
    { "Headset mic",             120.0f,  70.0f, -52.0f,  2.0f, -1.0f, 1.0f,  0.0f, 0, 45.0f, 3.0f },
    { "USB mic, close up",        90.0f,  60.0f, -55.0f, -1.0f, -2.5f, 2.0f,  2.0f, 0, 45.0f, 3.0f },
    { "Thin mic, make it warmer", 70.0f,  60.0f, -55.0f,  4.0f, -0.5f, 0.5f, -0.5f, 0, 40.0f, 3.0f },
    { "Broadcast / podcast",      80.0f,  60.0f, -55.0f,  2.0f, -2.0f, 3.0f,  2.5f, 0, 65.0f, 5.0f },
    { "Noisy room",              120.0f, 100.0f, -45.0f,  0.0f, -2.0f, 2.0f,  0.0f, 0, 40.0f, 3.0f },
    { "Even volume (leveler)",    90.0f,  70.0f, -55.0f,  0.0f, -1.5f, 1.5f,  1.0f, 1, 75.0f, 3.0f },
};
constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));

class Processor final : public fh::PluginBase
{
public:
    Processor() : PluginBase (layout()) {}

    int getNumPrograms() override                       { return numPresets; }
    int getCurrentProgram() override                    { return currentPreset; }
    const String getProgramName (int i) override        { return isPositiveAndBelow (i, numPresets) ? presets[i].name : ""; }
    void setCurrentProgram (int i) override
    {
        if (! isPositiveAndBelow (i, numPresets))
            return;
        currentPreset = i;
        const auto& p = presets[i];
        setParam ("lowcut", p.lowcut);   setParam ("denoise", p.denoise); setParam ("gate", p.gate);
        setParam ("warmth", p.warmth);   setParam ("mud", p.mud);         setParam ("clarity", p.clarity);
        setParam ("air", p.air);         setParam ("compmode", p.mode);   setParam ("compression", p.compression);
        setParam ("loudness", p.loudness);
    }

    void prepareToPlay (double sr, int block) override
    {
        update();
        lowCut.prepare (sr);
        denoiser.prepare (sr, block, getTotalNumOutputChannels());
        gate.prepare (sr);
        tone.prepare (sr);
        comp.prepare (sr);
        limiter.prepare (sr, block);
        setLatencySamples (denoiser.getLatencySamples() + limiter.getLatencySamples());
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        inputMeter.measure (buffer);
        update();
        lowCut.process (buffer);
        denoiser.process (buffer);
        gate.process (buffer);
        tone.process (buffer);
        comp.process (buffer);
        limiter.process (buffer);
        outputMeter.measure (buffer);
    }

    AudioProcessorEditor* createEditor() override;

    fh::Equaliser lowCut, tone;
    fh::Denoiser denoiser;
    fh::Gate gate;
    fh::VoiceCompressor comp;
    fh::Limiter limiter;
    int currentPreset = 0;

private:
    void update()
    {
        fh::Equaliser::Settings hp;
        hp.highPass = { param ("lowcut") > 21.0f, param ("lowcut"), 12 };
        for (auto& b : hp.bands) b.enabled = false;
        lowCut.setSettings (hp);

        denoiser.setAmount (param ("denoise") / 100.0f);
        gate.setSettings ({ param ("gate"), param ("gate") <= -79.5f ? 0.0f : 15.0f });

        using T = fh::Equaliser::BandType;
        fh::Equaliser::Settings eq;
        eq.bands[0] = { true, T::LowShelf, 180.0f, param ("warmth"), 0.7f };
        eq.bands[1] = { true, T::Bell, 350.0f, param ("mud"), 1.2f };
        eq.bands[2] = { true, T::Bell, 3200.0f, param ("clarity"), 0.8f };
        eq.bands[3] = { true, T::HighShelf, 10000.0f, param ("air"), 0.7f };
        eq.bands[4] = { false, T::Bell, 1000.0f, 0.0f, 1.0f };
        tone.setSettings (eq);

        fh::VoiceCompressor::Settings c;
        const bool leveler = choice ("compmode") == 1;
        c.mode = leveler ? fh::VoiceCompressor::Mode::Leveler : fh::VoiceCompressor::Mode::Smart;
        c.amount = param ("compression") / 100.0f;
        c.strength = param ("compression") / 100.0f;
        c.targetDb = -20.0f;
        comp.setSettings (c);

        limiter.setSettings ({ param ("loudness"), -1.0f });
    }
};

//==============================================================================
class Editor final : public fh::ui::EditorBase
{
public:
    explicit Editor (Processor& p)
        : EditorBase (p, "Faderhouse Voice", "Your whole voice chain in one place - pick a preset, then adjust to taste"),
          proc (p),
          denoise (p, "denoise", "Noise reduction"),
          gate (p, "gate", "Gate", fh::ui::Colours::violet),
          lowcut (p, "lowcut", "Low cut", fh::ui::Colours::textDim),
          warmth (p, "warmth", "Warmth", fh::ui::Colours::warm),
          clarity (p, "clarity", "Clarity", fh::ui::Colours::warm),
          air (p, "air", "Air", fh::ui::Colours::warm),
          mode (p, "compmode", { "Smart", "Leveler" }),
          compression (p, "compression", "Compression", fh::ui::Colours::blue),
          loudness (p, "loudness", "Loudness", fh::ui::Colours::blue),
          inMeter ([&p] { return p.inputMeter.peakDb.load(); }, "In"),
          compGain ([&p] { return p.comp.meterGainDb.load(); }, "Comp", 18.0f, true),
          outMeter ([&p] { return p.outputMeter.peakDb.load(); }, "Out"),
          voice ([&p] { return p.denoiser.voiceProbability.load(); }, "Voice", fh::ui::Colours::green),
          gateOpen ([&p] { return p.gate.meterOpen.load() ? 1.0f : 0.0f; }, "Gate open", fh::ui::Colours::violet)
    {
        for (int i = 0; i < numPresets; ++i)
            presetBox.addItem (presets[i].name, i + 1);
        presetBox.setTextWhenNothingSelected ("Choose a preset...");
        presetBox.setSelectedId (0, dontSendNotification);
        presetBox.onChange = [this]
        {
            const int i = presetBox.getSelectedId() - 1;
            if (i >= 0) proc.setCurrentProgram (i);
        };
        presetLabel.setText ("Preset", dontSendNotification);
        presetLabel.setColour (Label::textColourId, fh::ui::Colours::textDim);
        presetLabel.setFont (FontOptions (12.0f, Font::bold));

        for (Component* c : std::initializer_list<Component*> { &presetLabel, &presetBox, &denoise, &gate, &lowcut, &warmth, &clarity,
                                                                &air, &mode, &compression, &loudness, &inMeter, &compGain,
                                                                &outMeter, &voice, &gateOpen })
            addAndMakeVisible (c);

        compression.slider.setTooltip ("Smart: how much the compressor evens you out.  Leveler: how strongly it rides your level.");
        loudness.slider.setTooltip ("Pushes into the limiter: louder and more even, peaks never above -1 dB.");
        gate.slider.setTooltip ("Below this level your mic is turned down. All the way left = off.");
        setSize (900, 400);
    }

    void paint (Graphics& g) override
    {
        EditorBase::paint (g);
        paintGroup (g, cleanArea, "Clean up");
        paintGroup (g, toneArea, "Tone");
        paintGroup (g, levelArea, "Level");
        paintGroup (g, meterArea, {});
    }

    void resized() override
    {
        auto r = getContentArea();
        auto top = r.removeFromTop (30);
        presetLabel.setBounds (top.removeFromLeft (56));
        presetBox.setBounds (top.removeFromLeft (260));
        r.removeFromTop (10);

        meterArea = r.removeFromRight (150);
        r.removeFromRight (10);
        const int w = (r.getWidth() - 20) / 3;
        cleanArea = r.removeFromLeft (w);  r.removeFromLeft (10);
        toneArea  = r.removeFromLeft (w);  r.removeFromLeft (10);
        levelArea = r;

        auto threeKnobs = [] (Rectangle<int> area, fh::ui::Knob& a, fh::ui::Knob& b, fh::ui::Knob& c)
        {
            auto inner = area.reduced (8, 0).withTrimmedTop (28).withTrimmedBottom (10);
            auto big = inner.removeFromTop (inner.getHeight() * 55 / 100);
            a.setBounds (big.reduced (20, 0));
            const int half = inner.getWidth() / 2;
            b.setBounds (inner.removeFromLeft (half).reduced (4, 2));
            c.setBounds (inner.reduced (4, 2));
        };
        threeKnobs (cleanArea, denoise, gate, lowcut);
        threeKnobs (toneArea, clarity, warmth, air);

        auto lv = levelArea.reduced (10, 0).withTrimmedTop (28).withTrimmedBottom (10);
        mode.setBounds (lv.removeFromTop (26));
        lv.removeFromTop (6);
        const int half = lv.getWidth() / 2;
        compression.setBounds (lv.removeFromLeft (half).reduced (2));
        loudness.setBounds (lv.reduced (2));

        auto m = meterArea.reduced (8, 10);
        voice.setBounds (m.removeFromTop (20));
        gateOpen.setBounds (m.removeFromTop (20));
        m.removeFromTop (8);
        const int mw = m.getWidth() / 3;
        inMeter.setBounds (m.removeFromLeft (mw));
        compGain.setBounds (m.removeFromLeft (mw));
        outMeter.setBounds (m);
    }

private:
    Processor& proc;
    Label presetLabel;
    ComboBox presetBox;
    fh::ui::Knob denoise, gate, lowcut, warmth, clarity, air;
    fh::ui::ChoiceButtons mode;
    fh::ui::Knob compression, loudness;
    fh::ui::LevelBar inMeter;
    fh::ui::GainBar compGain;
    fh::ui::LevelBar outMeter;
    fh::ui::Lamp voice, gateOpen;
    TooltipWindow tooltips { this };
    Rectangle<int> cleanArea, toneArea, levelArea, meterArea;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
