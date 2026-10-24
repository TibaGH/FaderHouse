#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    The Faderhouse logo: three console faders whose caps form the gable of a house.
    Used for the app icon (rendered to PNG at build time by the icon tool) and in the top bar.
*/
namespace AppLogo
{
    /** Draws the full app icon (rounded-square background included) into a square area. */
    inline void draw (juce::Graphics& g, juce::Rectangle<float> area, bool withBackground = true)
    {
        using namespace juce;

        const float s = jmin (area.getWidth(), area.getHeight()) / 1024.0f;
        const auto origin = area.getCentre() - Point<float> (512.0f * s, 512.0f * s);
        auto P = [&] (float x, float y) { return origin + Point<float> (x * s, y * s); };
        auto R = [&] (float x, float y, float w, float h) { return Rectangle<float> (P (x, y), P (x + w, y + h)); };

        const Colour teal (0xff52c7a6);

        if (withBackground)
        {
            auto bg = R (100, 100, 824, 824);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (bg.translated (0, 10 * s).expanded (4 * s), 190 * s);

            ColourGradient grad (Colour (0xff34373e), bg.getX(), bg.getY(), Colour (0xff111215), bg.getX(), bg.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bg, 185 * s);

            // brushed texture
            g.setColour (Colours::white.withAlpha (0.018f));
            for (float x = 120; x < 904; x += 7)
                g.fillRect (R (x, 140, 2, 744));

            g.setColour (Colours::white.withAlpha (0.10f));
            g.drawRoundedRectangle (bg.reduced (2 * s), 183 * s, 3 * s);
        }

        const float xs[] = { 330.0f, 512.0f, 694.0f };
        const float caps[] = { 590.0f, 360.0f, 590.0f };   // cap centres: the roof shape

        // scale ticks
        g.setColour (Colours::white.withAlpha (0.16f));
        for (float y = 260; y <= 790; y += 53)
            for (float x : xs)
            {
                g.fillRect (R (x - 62, y - 2, 34, 4));
                g.fillRect (R (x + 28, y - 2, 34, 4));
            }

        // slots
        for (float x : xs)
        {
            g.setColour (Colour (0xff050506));
            g.fillRoundedRectangle (R (x - 14, 230, 28, 590), 14 * s);
            g.setColour (Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (R (x - 14, 230, 28, 590), 14 * s, 2 * s);
        }

        // roof line through the caps (glow + line)
        Path roof;
        roof.startNewSubPath (P (190, 700));
        roof.lineTo (P (512, 360));
        roof.lineTo (P (834, 700));
        g.setColour (teal.withAlpha (0.22f));
        g.strokePath (roof, PathStrokeType (46 * s, PathStrokeType::mitered, PathStrokeType::rounded));
        g.setColour (teal);
        g.strokePath (roof, PathStrokeType (20 * s, PathStrokeType::mitered, PathStrokeType::rounded));

        // fader caps
        for (int i = 0; i < 3; ++i)
        {
            auto cap = R (xs[i] - 66, caps[i] - 44, 132, 88);

            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (cap.translated (0, 10 * s), 16 * s);

            ColourGradient body (Colour (0xff45474c), cap.getX(), cap.getY(), Colour (0xff101113), cap.getX(), cap.getBottom(), false);
            body.addColour (0.5, Colour (0xff1f2023));
            g.setGradientFill (body);
            g.fillRoundedRectangle (cap, 16 * s);

            g.setColour (Colours::black.withAlpha (0.55f));
            for (float dy : { -28.0f, -16.0f, 16.0f, 28.0f })
                g.fillRect (R (xs[i] - 54, caps[i] + dy - 2, 108, 4));

            g.setColour (Colour (0xfff4f3ee));
            g.fillRoundedRectangle (R (xs[i] - 60, caps[i] - 4, 120, 8), 3 * s);

            g.setColour (Colours::white.withAlpha (0.16f));
            g.drawRoundedRectangle (cap.reduced (1.5f * s), 16 * s, 3 * s);
        }
    }
}
