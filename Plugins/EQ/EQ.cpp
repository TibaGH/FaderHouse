// Faderhouse EQ: 5-band parametric EQ with low cut / high cut and a live spectrum of what
// comes in. Drag the points on the curve; scroll on a point for its width (Q).

#include "FhUi.h"

using namespace juce;
using fh::APVTS;

namespace
{
const StringArray bandTypes { "Bell", "Low shelf", "High shelf" };
const StringArray slopes { "12 dB/oct", "24 dB/oct" };

struct BandDefault { int type; float freq; };
constexpr BandDefault bandDefaults[fh::Equaliser::numBands] { { 1, 120.0f }, { 0, 350.0f }, { 0, 1200.0f }, { 0, 3500.0f }, { 2, 10000.0f } };

String bandId (int band, const char* what) { return "b" + String (band + 1) + "_" + what; }

APVTS::ParameterLayout layout()
{
    APVTS::ParameterLayout l;
    l.add (fh::boolParam ("hp_on", "Low cut on", true));
    l.add (fh::floatParam ("hp_freq", "Low cut", fh::freqRange (20.0f, 400.0f), 80.0f, "Hz", 0));
    l.add (fh::choiceParam ("hp_slope", "Low cut slope", slopes, 0));
    l.add (fh::boolParam ("lp_on", "High cut on", false));
    l.add (fh::floatParam ("lp_freq", "High cut", fh::freqRange (2000.0f, 20000.0f), 16000.0f, "Hz", 0));
    l.add (fh::choiceParam ("lp_slope", "High cut slope", slopes, 0));

    for (int b = 0; b < fh::Equaliser::numBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        l.add (fh::boolParam (bandId (b, "on"), n + "on", true));
        l.add (fh::choiceParam (bandId (b, "type"), n + "type", bandTypes, bandDefaults[b].type));
        l.add (fh::floatParam (bandId (b, "freq"), n + "frequency", fh::freqRange (20.0f, 20000.0f), bandDefaults[b].freq, "Hz", 0));
        l.add (fh::floatParam (bandId (b, "gain"), n + "gain", { -18.0f, 18.0f, 0.01f }, 0.0f, "dB", 1));
        l.add (fh::floatParam (bandId (b, "q"), n + "Q", { 0.2f, 10.0f, 0.001f, 0.4f }, 0.9f, "", 2));
    }
    return l;
}

/** Lock-free mono sample tap for the analyser (audio thread pushes, UI pulls). */
struct SpectrumTap
{
    static constexpr int size = 1 << 14;
    AbstractFifo fifo { size };
    std::vector<float> data = std::vector<float> (size, 0.0f);

    void push (const AudioBuffer<float>& b)
    {
        const int n = b.getNumSamples(), chs = b.getNumChannels();
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto put = [&] (int start, int count, int offset)
        {
            for (int i = 0; i < count; ++i)
            {
                float v = 0.0f;
                for (int ch = 0; ch < chs; ++ch) v += b.getSample (ch, offset + i);
                data[(size_t) (start + i)] = v / (float) jmax (1, chs);
            }
        };
        put (s1, n1, 0);
        put (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);
    }

    int pull (float* out, int maxCount)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (jmin (maxCount, fifo.getNumReady()), s1, n1, s2, n2);
        std::copy (data.begin() + s1, data.begin() + s1 + n1, out);
        std::copy (data.begin() + s2, data.begin() + s2 + n2, out + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
};

class Processor final : public fh::PluginBase
{
public:
    Processor() : PluginBase (layout(), "Faderhouse EQ", "EQ")
    {
        auto get = [this] (const String& id) { return state.getRawParameterValue (id); };
        hpOn = get ("hp_on"); hpFreq = get ("hp_freq"); hpSlope = get ("hp_slope");
        lpOn = get ("lp_on"); lpFreq = get ("lp_freq"); lpSlope = get ("lp_slope");
        for (int b = 0; b < fh::Equaliser::numBands; ++b)
            bands[(size_t) b] = { get (bandId (b, "on")), get (bandId (b, "type")), get (bandId (b, "freq")),
                                  get (bandId (b, "gain")), get (bandId (b, "q")) };
    }

    void prepareToPlay (double sr, int) override
    {
        sampleRateNow.store (sr);
        eq.setSettings (readSettings());
        eq.prepare (sr);
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        inputMeter.measure (buffer);
        inputTap.push (buffer);
        eq.setSettings (readSettings());
        eq.process (buffer);
        outputMeter.measure (buffer);
    }

    fh::Equaliser::Settings readSettings() const
    {
        // Cached parameter pointers: no string work on the audio thread.
        auto v = [] (const std::atomic<float>* a) { return a->load(); };
        fh::Equaliser::Settings s;
        s.highPass = { v (hpOn) > 0.5f, v (hpFreq), v (hpSlope) > 0.5f ? 24 : 12 };
        s.lowPass  = { v (lpOn) > 0.5f, v (lpFreq), v (lpSlope) > 0.5f ? 24 : 12 };
        for (size_t b = 0; b < (size_t) fh::Equaliser::numBands; ++b)
        {
            auto& band = s.bands[b];
            band.enabled = v (bands[b].on) > 0.5f;
            band.type = (fh::Equaliser::BandType) roundToInt (v (bands[b].type));
            band.freq = v (bands[b].freq);
            band.gainDb = v (bands[b].gain);
            band.q = v (bands[b].q);
        }
        return s;
    }

    AudioProcessorEditor* createEditor() override;

    SpectrumTap inputTap;
    std::atomic<double> sampleRateNow { 48000.0 };

private:
    fh::Equaliser eq;
    std::atomic<float> *hpOn {}, *hpFreq {}, *hpSlope {}, *lpOn {}, *lpFreq {}, *lpSlope {};
    struct BandParams { std::atomic<float> *on {}, *type {}, *freq {}, *gain {}, *q {}; };
    std::array<BandParams, fh::Equaliser::numBands> bands {};
};

//==============================================================================
const Colour bandColours[fh::Equaliser::numBands] { Colour (0xffe0a458), Colour (0xffe8c547), Colour (0xff52c7a6),
                                                    Colour (0xff6aa7f0), Colour (0xffa58cf0) };
const Colour cutColour { 0xffcfd3d9 };

// Node ids: 0..4 bands, 5 low cut, 6 high cut.
constexpr int lowCutNode = fh::Equaliser::numBands, highCutNode = fh::Equaliser::numBands + 1;

class EqDisplay final : public Component, private Timer
{
public:
    explicit EqDisplay (Processor& p) : proc (p), fft (fftOrder)
    {
        window.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
        ring.assign (fftSize, 0.0f);
        spectrumDb.assign (fftSize / 2, -120.0f);
        fftData.assign (fftSize * 2, 0.0f);
        setRepaintsOnMouseActivity (true);
        startTimerHz (30);
    }

    std::function<void (int)> onSelect;
    int selected = 2;

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (fh::ui::Colours::background);
        g.fillRoundedRectangle (r, 8.0f);
        plot = r.reduced (2.0f, 8.0f).withTrimmedLeft (6.0f).withTrimmedRight (30.0f);

        drawGrid (g);
        drawSpectrum (g);

        const auto settings = proc.readSettings();
        const double fs = proc.sampleRateNow.load();

        // Selected band's own shape.
        if (selected < fh::Equaliser::numBands && settings.bands[(size_t) selected].enabled)
        {
            Path area;
            const auto& band = settings.bands[(size_t) selected];
            area.startNewSubPath (plot.getX(), gainToY (0.0));
            for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
                area.lineTo (x, gainToY (fh::Equaliser::bandResponseDb (band, xToFreq (x), fs)));
            area.lineTo (plot.getRight(), gainToY (0.0));
            area.closeSubPath();
            g.setColour (bandColours[selected].withAlpha (0.18f));
            g.fillPath (area);
        }

        // Total curve (clipped to the plot).
        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());
        Path curve;
        for (float x = plot.getX(); x <= plot.getRight(); x += 1.5f)
        {
            const float y = gainToY (fh::Equaliser::responseDb (settings, xToFreq (x), fs));
            if (x == plot.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (fh::ui::Colours::text);
        g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved));

        // Points.
        for (int node = 0; node < fh::Equaliser::numBands + 2; ++node)
        {
            const auto pos = nodePosition (node, settings);
            const bool on = nodeEnabled (node, settings);
            const auto colour = node < fh::Equaliser::numBands ? bandColours[node] : cutColour;
            const float radius = node == selected ? 9.0f : 7.0f;
            auto circle = Rectangle<float> (radius * 2, radius * 2).withCentre (pos);

            g.setColour (on ? colour : colour.withAlpha (0.25f));
            g.fillEllipse (circle);
            if (node == selected)
            {
                g.setColour (fh::ui::Colours::text);
                g.drawEllipse (circle.expanded (2.5f), 1.5f);
            }
            g.setColour (on ? fh::ui::Colours::background : fh::ui::Colours::textDim);
            g.setFont (FontOptions (10.5f, Font::bold));
            g.drawText (node < fh::Equaliser::numBands ? String (node + 1) : (node == lowCutNode ? "L" : "H"),
                        circle, Justification::centred);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragging = nearestNode (e.position);
        if (dragging < 0)
            return;

        selected = dragging;
        if (onSelect) onSelect (selected);

        for (auto* p : dragParams (dragging))
            p->beginChangeGesture();
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging < 0)
            return;

        const auto pos = e.position;
        const float freq = (float) xToFreq (jlimit (plot.getX(), plot.getRight(), pos.x));
        if (dragging < fh::Equaliser::numBands)
        {
            set (bandId (dragging, "freq"), freq);
            set (bandId (dragging, "gain"), (float) jlimit (-18.0, 18.0, yToGain (pos.y)));
        }
        else
        {
            set (dragging == lowCutNode ? "hp_freq" : "lp_freq", freq);
        }
    }

    void mouseUp (const MouseEvent&) override
    {
        if (dragging >= 0)
            for (auto* p : dragParams (dragging))
                p->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int node = nearestNode (e.position);
        if (node < 0)
            return;
        const String id = node < fh::Equaliser::numBands ? bandId (node, "on") : (node == lowCutNode ? "hp_on" : "lp_on");
        proc.setParam (id, proc.flag (id.toRawUTF8()) ? 0.0f : 1.0f);
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const int node = nearestNode (e.position);
        if (node < 0 || node >= fh::Equaliser::numBands)
            return;
        const auto id = bandId (node, "q");
        const float q = proc.param (id.toRawUTF8()) * std::pow (1.15f, (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f));
        proc.setParam (id, jlimit (0.2f, 10.0f, q));
    }

private:
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;

    void timerCallback() override
    {
        float incoming[4096];
        int got;
        while ((got = proc.inputTap.pull (incoming, 4096)) > 0)
        {
            for (int i = 0; i < got; ++i)
            {
                ring[(size_t) ringPos] = incoming[i];
                ringPos = (ringPos + 1) % fftSize;
            }
            fresh = true;
        }

        if (fresh)
        {
            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)] * window[(size_t) i];
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            const float norm = 4.0f / (float) fftSize;   // Hann window sum = N/2, single-sided x2
            for (int b = 1; b < fftSize / 2; ++b)
            {
                const float db = fh::gainToDb (fftData[(size_t) b] * norm);
                auto& shown = spectrumDb[(size_t) b];
                shown = db > shown ? shown + (db - shown) * 0.6f : jmax (db, shown - 1.2f);
            }
            fresh = false;
        }
        else
        {
            for (auto& v : spectrumDb) v = jmax (-120.0f, v - 1.2f);
        }
        repaint();
    }

    // Frequency / gain mapping.
    double xToFreq (float x) const  { return 20.0 * std::pow (1000.0, (x - plot.getX()) / plot.getWidth()); }
    float freqToX (double f) const  { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); }
    float gainToY (double db) const { return jmap ((float) db, 20.0f, -20.0f, plot.getY(), plot.getBottom()); }
    double yToGain (float y) const  { return jmap (y, plot.getY(), plot.getBottom(), 20.0f, -20.0f); }
    float specToY (float db) const  { return jmap (jlimit (-96.0f, 0.0f, db), -96.0f, 0.0f, plot.getBottom(), plot.getY() + plot.getHeight() * 0.08f); }

    void drawGrid (Graphics& g)
    {
        g.setFont (FontOptions (10.0f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = freqToX (f);
            g.setColour (fh::ui::Colours::outline.withAlpha (0.7f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (fh::ui::Colours::textDim.withAlpha (0.8f));
            g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f), (int) x + 3, (int) plot.getBottom() - 14, 30, 12, Justification::left);
        }
        for (int db : { -18, -12, -6, 0, 6, 12, 18 })
        {
            const float y = gainToY (db);
            g.setColour (db == 0 ? fh::ui::Colours::textDim.withAlpha (0.5f) : fh::ui::Colours::outline.withAlpha (0.5f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (fh::ui::Colours::textDim.withAlpha (0.8f));
            g.drawText ((db > 0 ? "+" : "") + String (db), (int) plot.getRight() + 4, (int) y - 6, 26, 12, Justification::left);
        }
    }

    void drawSpectrum (Graphics& g)
    {
        const double fs = proc.sampleRateNow.load();
        const double binHz = fs / fftSize;
        Path p;
        p.startNewSubPath (plot.getX(), plot.getBottom());

        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double f0 = xToFreq (x), f1 = xToFreq (x + 2.0f);
            const int b0 = jlimit (1, fftSize / 2 - 1, (int) (f0 / binHz));
            const int b1 = jlimit (b0, fftSize / 2 - 1, (int) (f1 / binHz));
            float db = -120.0f;
            for (int b = b0; b <= b1; ++b) db = jmax (db, spectrumDb[(size_t) b]);
            // +3 dB/octave tilt around 1 kHz, so voice looks about level.
            db += 3.0f * (float) std::log2 (jmax (20.0, f0) / 1000.0);
            p.lineTo (x, specToY (db));
        }
        p.lineTo (plot.getRight(), plot.getBottom());
        p.closeSubPath();

        ColourGradient grad (fh::ui::Colours::blue.withAlpha (0.35f), 0, plot.getY(), fh::ui::Colours::blue.withAlpha (0.05f), 0, plot.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (p);
        g.setColour (fh::ui::Colours::blue.withAlpha (0.45f));
        g.strokePath (p, PathStrokeType (1.0f));
    }

    Point<float> nodePosition (int node, const fh::Equaliser::Settings& s) const
    {
        if (node < fh::Equaliser::numBands)
        {
            const auto& b = s.bands[(size_t) node];
            return { freqToX (b.freq), gainToY (b.gainDb) };
        }
        const auto& c = node == lowCutNode ? s.highPass : s.lowPass;
        return { freqToX (c.freq), gainToY (0.0) };
    }

    static bool nodeEnabled (int node, const fh::Equaliser::Settings& s)
    {
        if (node < fh::Equaliser::numBands) return s.bands[(size_t) node].enabled;
        return node == lowCutNode ? s.highPass.enabled : s.lowPass.enabled;
    }

    int nearestNode (Point<float> pos) const
    {
        const auto s = proc.readSettings();
        int best = -1;
        float bestDist = 14.0f;
        for (int node = 0; node < fh::Equaliser::numBands + 2; ++node)
        {
            const float d = nodePosition (node, s).getDistanceFrom (pos);
            if (d < bestDist) { bestDist = d; best = node; }
        }
        return best;
    }

    std::vector<RangedAudioParameter*> dragParams (int node)
    {
        std::vector<RangedAudioParameter*> list;
        auto add = [&] (const String& id) { if (auto* p = proc.state.getParameter (id)) list.push_back (p); };
        if (node < fh::Equaliser::numBands) { add (bandId (node, "freq")); add (bandId (node, "gain")); }
        else add (node == lowCutNode ? "hp_freq" : "lp_freq");
        return list;
    }

    void set (const String& id, float value)
    {
        if (auto* p = proc.state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    Processor& proc;
    dsp::FFT fft;
    std::vector<float> window, ring, fftData, spectrumDb;
    int ringPos = 0;
    bool fresh = false;
    Rectangle<float> plot;
    int dragging = -1;
};

//==============================================================================
/** Controls of the selected point (rebuilt when you pick another one). */
class BandPanel final : public Component
{
public:
    explicit BandPanel (Processor& p) : proc (p) {}

    void show (int node)
    {
        current = node;
        removeAllChildren();
        freq.reset(); gain.reset(); q.reset(); onKey.reset(); typeBox.reset(); typeAttachment.reset();

        const bool band = node < fh::Equaliser::numBands;
        const auto colour = band ? bandColours[node] : cutColour;
        const String prefix = band ? "b" + String (node + 1) + "_" : (node == lowCutNode ? "hp_" : "lp_");

        title = band ? "Band " + String (node + 1) : (node == lowCutNode ? "Low cut" : "High cut");
        titleColour = colour;

        onKey = std::make_unique<fh::ui::ToggleKey> (proc, prefix + "on", "On");
        addAndMakeVisible (*onKey);

        typeBox = std::make_unique<ComboBox>();
        typeBox->addItemList (band ? bandTypes : slopes, 1);
        addAndMakeVisible (*typeBox);
        typeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, prefix + (band ? "type" : "slope"), *typeBox);

        freq = std::make_unique<fh::ui::Knob> (proc, prefix + "freq", "Frequency", colour);
        addAndMakeVisible (*freq);
        if (band)
        {
            gain = std::make_unique<fh::ui::Knob> (proc, prefix + "gain", "Gain", colour);
            q = std::make_unique<fh::ui::Knob> (proc, prefix + "q", "Width (Q)", colour);
            addAndMakeVisible (*gain);
            addAndMakeVisible (*q);
        }
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.setColour (titleColour);
        g.fillEllipse (12.0f, 14.0f, 10.0f, 10.0f);
        g.setColour (fh::ui::Colours::text);
        g.setFont (FontOptions (14.0f, Font::bold));
        g.drawText (title, 30, 8, 140, 22, Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (24);
        top.removeFromLeft (150);
        if (onKey) onKey->setBounds (top.removeFromLeft (52));
        top.removeFromLeft (8);
        if (typeBox) typeBox->setBounds (top.removeFromLeft (130));
        r.removeFromTop (6);

        const int w = r.getWidth() / 3;
        if (freq) freq->setBounds (r.removeFromLeft (w).reduced (6, 0));
        if (gain) gain->setBounds (r.removeFromLeft (w).reduced (6, 0));
        if (q)    q->setBounds (r.removeFromLeft (w).reduced (6, 0));
    }

private:
    Processor& proc;
    int current = -1;
    String title;
    Colour titleColour;
    std::unique_ptr<fh::ui::Knob> freq, gain, q;
    std::unique_ptr<fh::ui::ToggleKey> onKey;
    std::unique_ptr<ComboBox> typeBox;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
};

class Editor final : public fh::ui::EditorBase
{
public:
    explicit Editor (Processor& p)
        : EditorBase (p, "Faderhouse EQ", "Drag the points - scroll for width, double-click to switch a point on/off"),
          display (p), panel (p),
          inMeter ([&p] { return p.inputMeter.peakDb.load(); }, "In"),
          outMeter ([&p] { return p.outputMeter.peakDb.load(); }, "Out")
    {
        addAndMakeVisible (display);
        addAndMakeVisible (panel);
        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);
        display.onSelect = [this] (int node) { panel.show (node); };
        panel.show (display.selected);
        setResizable (true, true);
        setResizeLimits (640, 460, 1600, 1000);
        setSize (760, 520);
    }

    void paint (Graphics& g) override
    {
        EditorBase::paint (g);
        paintGroup (g, panelArea, {});
    }

    void resized() override
    {
        auto r = getContentArea();
        auto meters = r.removeFromRight (64);
        r.removeFromRight (8);
        panelArea = r.removeFromBottom (150);
        r.removeFromBottom (10);
        display.setBounds (r);
        panel.setBounds (panelArea);
        inMeter.setBounds (meters.removeFromLeft (32));
        outMeter.setBounds (meters);
    }

private:
    EqDisplay display;
    BandPanel panel;
    fh::ui::LevelBar inMeter, outMeter;
    Rectangle<int> panelArea;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

namespace fh::builtin { juce::AudioPluginInstance* createEQ() { return new Processor(); } }

#if FH_BUILD_AS_PLUGIN
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return fh::builtin::createEQ(); }
#endif
