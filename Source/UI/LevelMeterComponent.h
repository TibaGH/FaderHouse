#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LevelMeter.h"
#include "FaderhouseLookAndFeel.h"

/** Stereo peak meter, -60..+6 dBFS, with a short peak hold. update() is called by the main UI timer. */
class LevelMeterComponent final : public juce::Component
{
public:
    explicit LevelMeterComponent (LevelMeter& m) : meter (m) {}

    void update()
    {
        const auto [l, r] = meter.takePeaks();
        bool changed = false;

        auto step = [&] (float peak, float& shown, float& hold, int& holdTicks)
        {
            const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
            const float next = juce::jmax (db, shown - 1.2f); // ~36 dB/s fall-off at 30 fps
            if (! juce::approximatelyEqual (next, shown)) { shown = next; changed = true; }

            if (db > hold) { hold = db; holdTicks = 45; changed = true; }
            else if (--holdTicks <= 0 && hold > -60.0f) { hold = juce::jmax (-60.0f, hold - 1.5f); changed = true; }
        };

        step (l, shownL, holdL, holdTicksL);
        step (r, shownR, holdR, holdTicksR);

        if (changed)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float gap = 2.0f;
        const float w = (b.getWidth() - gap) / 2.0f;

        drawBar (g, b.withWidth (w), shownL, holdL);
        drawBar (g, b.withTrimmedLeft (w + gap), shownR, holdR);
    }

private:
    static float proportion (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }

    void drawBar (juce::Graphics& g, juce::Rectangle<float> area, float db, float hold)
    {
        g.setColour (Theme::background);
        g.fillRoundedRectangle (area, 1.5f);

        const float h = area.getHeight();
        const float level = proportion (db) * h;

        juce::ColourGradient grad (Theme::meterLow, 0.0f, area.getBottom(),
                                   Theme::meterHigh, 0.0f, area.getY(), false);
        grad.addColour (proportion (-18.0f), Theme::meterLow);
        grad.addColour (proportion (-6.0f), Theme::meterMid);
        grad.addColour (proportion (0.0f), Theme::meterHigh);
        g.setGradientFill (grad);
        g.fillRect (area.withTop (area.getBottom() - level));

        if (hold > -59.0f)
        {
            const float y = area.getBottom() - proportion (hold) * h;
            g.setColour (hold >= 0.0f ? Theme::meterHigh : Theme::text.withAlpha (0.8f));
            g.fillRect (area.getX(), y, area.getWidth(), 1.5f);
        }
    }

    LevelMeter& meter;
    float shownL = -60.0f, shownR = -60.0f, holdL = -60.0f, holdR = -60.0f;
    int holdTicksL = 0, holdTicksR = 0;
};
