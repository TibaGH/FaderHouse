#include "ConsoleFader.h"
#include "FaderhouseLookAndFeel.h"

using namespace juce;

namespace
{
// Console-style scale: dB -> position (0 = bottom, 1 = top). -60 means -inf.
constexpr double kPoints[][2] = {
    { -60.0, 0.000 }, { -50.0, 0.060 }, { -40.0, 0.130 }, { -30.0, 0.220 },
    { -20.0, 0.340 }, { -15.0, 0.420 }, { -10.0, 0.520 }, {  -5.0, 0.630 },
    {   0.0, 0.750 }, {   5.0, 0.870 }, {  10.0, 1.000 }
};
constexpr int kNumPoints = (int) (sizeof (kPoints) / sizeof (kPoints[0]));

double dbToProportion (double db)
{
    if (db <= kPoints[0][0]) return 0.0;
    for (int i = 1; i < kNumPoints; ++i)
        if (db <= kPoints[i][0])
        {
            const double t = (db - kPoints[i - 1][0]) / (kPoints[i][0] - kPoints[i - 1][0]);
            return kPoints[i - 1][1] + t * (kPoints[i][1] - kPoints[i - 1][1]);
        }
    return 1.0;
}

double proportionToDb (double p)
{
    if (p <= 0.0) return kPoints[0][0];
    for (int i = 1; i < kNumPoints; ++i)
        if (p <= kPoints[i][1])
        {
            const double t = (p - kPoints[i - 1][1]) / (kPoints[i][1] - kPoints[i - 1][1]);
            return kPoints[i - 1][0] + t * (kPoints[i][0] - kPoints[i - 1][0]);
        }
    return kPoints[kNumPoints - 1][0];
}

const struct { double db; const char* label; } kMarks[] = {
    { 10, "+10" }, { 5, "+5" }, { 0, "0" }, { -5, "-5" }, { -10, "-10" }, { -15, "-15" },
    { -20, "-20" }, { -30, "-30" }, { -40, "-40" }, { -50, "-50" }, { -60, "\xe2\x88\x9e" } // infinity sign
};
} // namespace

//==============================================================================
struct ConsoleFader::FaderLookAndFeel final : public LookAndFeel_V4
{
    int getSliderThumbRadius (Slider&) override { return capHeight / 2; }

    void drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float, float, Slider::SliderStyle, Slider& s) override
    {
        const float cx = (float) x + (float) width * 0.5f;
        ignoreUnused (y, height);
        const float top = (float) s.getPositionOfValue (10.0) - 8.0f;
        const float bottom = (float) s.getPositionOfValue (-60.0) + 8.0f;

        // slot
        g.setColour (Colour (0xff050506));
        g.fillRoundedRectangle (cx - 3.5f, top, 7.0f, bottom - top, 3.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (cx - 3.5f, top, 7.0f, bottom - top, 3.0f, 1.0f);

        // thin lit line below the cap
        if (auto* owner = dynamic_cast<ConsoleFader*> (s.getParentComponent()))
        {
            const float litHeight = bottom - sliderPos - 4.0f;
            if (s.getValue() > -59.9 && litHeight > 0.0f)
            {
                g.setColour (owner->accent.withAlpha (0.8f));
                g.fillRect (cx - 1.0f, sliderPos, 2.0f, litHeight);
            }
        }

        // cap
        const float capW = jmin (30.0f, (float) width - 6.0f);
        auto cap = Rectangle<float> (capW, (float) capHeight).withCentre ({ cx, sliderPos });

        g.setColour (Colours::black.withAlpha (0.45f));                      // drop shadow
        g.fillRoundedRectangle (cap.translated (0.0f, 3.0f).expanded (1.0f), 4.0f);

        // body + sloped faces, like a classic long-throw console cap seen from the front
        g.setColour (Colour (0xff0b0b0c));
        g.fillRoundedRectangle (cap, 4.0f);

        auto upper = cap.reduced (2.0f).withHeight (cap.getHeight() * 0.40f);
        g.setGradientFill (ColourGradient (Colour (0xff6a6c72), upper.getX(), upper.getY(),
                                           Colour (0xff26272a), upper.getX(), upper.getBottom(), false));
        g.fillRoundedRectangle (upper, 3.0f);

        auto lower = cap.reduced (2.0f);
        lower = lower.withTop (lower.getBottom() - cap.getHeight() * 0.30f);
        g.setGradientFill (ColourGradient (Colour (0xff1a1b1d), lower.getX(), lower.getY(),
                                           Colour (0xff050506), lower.getX(), lower.getBottom(), false));
        g.fillRoundedRectangle (lower, 3.0f);

        // grip grooves
        g.setColour (Colours::black.withAlpha (0.5f));
        for (float dy : { -16.0f, -12.5f, -9.0f })
            g.fillRect (cap.getX() + 4.0f, cap.getCentreY() + dy, cap.getWidth() - 8.0f, 1.2f);

        // white index line
        g.setColour (Colour (0xfff6f5f0));
        g.fillRect (cap.getX() + 1.5f, cap.getCentreY() - 1.0f, cap.getWidth() - 3.0f, 2.5f);

        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (cap.reduced (0.5f), 4.0f, 1.0f);
    }
};

struct ConsoleFader::FineSlider final : public Slider
{
    void mouseDrag (const MouseEvent& e) override
    {
        // Shift = fine adjustment
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1600 : 300);
        Slider::mouseDrag (e);
    }
};

//==============================================================================
ConsoleFader::ConsoleFader()
    : lnf (std::make_unique<FaderLookAndFeel>()),
      sliderImpl (std::make_unique<FineSlider>()),
      slider (*sliderImpl),
      accent (Theme::accent)
{
    slider.setLookAndFeel (lnf.get());
    slider.setSliderStyle (Slider::LinearVertical);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setNormalisableRange (NormalisableRange<double> (
        -60.0, 10.0,
        [] (double, double, double p) { return proportionToDb (p); },
        [] (double, double, double db) { return dbToProportion (db); },
        [] (double, double, double db) { return db <= -59.9 ? -60.0 : std::round (db * 10.0) / 10.0; }));
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setSliderSnapsToMousePosition (false);
    slider.setVelocityBasedMode (false);
    slider.setScrollWheelEnabled (true);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.textFromValueFunction = [] (double v) { return formatDb (v); };
    slider.onValueChange = [this] { if (onValueChange) onValueChange (slider.getValue()); };
    slider.setValue (0.0, dontSendNotification);
    addAndMakeVisible (slider);
}

ConsoleFader::~ConsoleFader()
{
    slider.setLookAndFeel (nullptr);
}

String ConsoleFader::formatDb (double db)
{
    if (db <= -59.9)
        return "-inf";
    return (db > 0.05 ? "+" : "") + String (db, 1) + " dB";
}

void ConsoleFader::setValueDb (double db, NotificationType n)
{
    slider.setValue (db, n);
}

void ConsoleFader::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedLeft (scaleWidth));
}

void ConsoleFader::paint (Graphics& g)
{
    const auto sb = slider.getBounds();
    const float cx = (float) sb.getCentreX();

    g.setFont (FontOptions (9.5f));

    for (auto& m : kMarks)
    {
        const float y = (float) sb.getY() + (float) slider.getPositionOfValue (m.db);
        const bool unity = approximatelyEqual (m.db, 0.0);

        g.setColour (Theme::textDim.withAlpha (unity ? 1.0f : 0.75f));
        g.drawText (String::fromUTF8 (m.label), Rectangle<float> (0.0f, y - 6.0f, (float) scaleWidth - 1.0f, 12.0f), Justification::centredRight);

        g.setColour (Theme::textDim.withAlpha (unity ? 0.9f : 0.45f));
        const float len = unity ? 13.0f : 9.0f;
        g.fillRect (cx - 7.0f - len, y - 0.5f, len, unity ? 1.5f : 1.0f);
        g.fillRect (cx + 7.0f, y - 0.5f, len, unity ? 1.5f : 1.0f);
    }
}
