#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    The Faderhouse logo: three analog console channel strips (MUTE / SOLO keys, dB scale,
    long-throw faders) whose fader caps form the gable of a house.
    Used for the app icon (rendered to PNG by Tools/IconRender) and in the top bar.
*/
namespace AppLogo
{
    inline void draw (juce::Graphics& g, juce::Rectangle<float> area, bool withBackground = true)
    {
        using namespace juce;

        const float s = jmin (area.getWidth(), area.getHeight()) / 1024.0f;
        const auto origin = area.getCentre() - Point<float> (512.0f * s, 512.0f * s);
        auto P = [&] (float x, float y) { return origin + Point<float> (x * s, y * s); };
        auto R = [&] (float x, float y, float w, float h) { return Rectangle<float> (P (x, y), P (x + w, y + h)); };

        const Colour teal (0xff52c7a6), red (0xffff4a3d), green (0xff3ee07a);

        // --- rounded-square body: dark anodised console panel ---------------------------
        auto bg = R (100, 100, 824, 824);
        if (withBackground)
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (bg.translated (0, 10 * s).expanded (4 * s), 190 * s);

            ColourGradient grad (Colour (0xff3a3d44), bg.getX(), bg.getY(), Colour (0xff15161a), bg.getX(), bg.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bg, 185 * s);
        }

        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (Path ([&] { Path p; p.addRoundedRectangle (bg, 185 * s); return p; }()));

        const float xs[] = { 300.0f, 512.0f, 724.0f };
        const float caps[] = { 640.0f, 470.0f, 640.0f };   // fader cap centres: the roof
        const float stripW = 196.0f;

        // --- channel strips -------------------------------------------------------------
        for (int i = 0; i < 3; ++i)
        {
            auto strip = R (xs[i] - stripW * 0.5f + 4, 60, stripW - 8, 900);
            ColourGradient metal (Colour (0xff4a4d55), strip.getX(), 0, Colour (0xff2c2e33), strip.getRight(), 0, false);
            metal.addColour (0.5, Colour (0xff3e4148));
            g.setGradientFill (metal);
            g.fillRect (strip);

            g.setColour (Colours::white.withAlpha (0.025f));                     // brushing
            for (float y = 60; y < 960; y += 6)
                g.fillRect (R (xs[i] - stripW * 0.5f + 4, y, stripW - 8, 2));

            g.setColour (Colours::black.withAlpha (0.55f));                       // seams
            g.fillRect (R (xs[i] + stripW * 0.5f - 4, 60, 8, 900));
        }

        // --- MUTE / SOLO keys -------------------------------------------------------------
        auto key = [&] (float cx, float cy, Colour lit, bool on)
        {
            auto k = R (cx - 34, cy - 34, 68, 68);
            g.setColour (Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (k.expanded (5 * s), 12 * s);
            if (on)
            {
                g.setColour (lit.withAlpha (0.35f));
                g.fillRoundedRectangle (k.expanded (16 * s), 22 * s);
            }
            const auto base = on ? lit : Colour (0xff6d7078);
            ColourGradient face (base.brighter (on ? 0.5f : 0.35f), k.getX(), k.getY(), base.darker (on ? 0.1f : 0.4f), k.getX(), k.getBottom(), false);
            g.setGradientFill (face);
            g.fillRoundedRectangle (k, 9 * s);
            g.setColour (Colours::white.withAlpha (on ? 0.45f : 0.18f));
            g.drawRoundedRectangle (k.reduced (3 * s), 7 * s, 3 * s);
        };

        key (xs[0] - 40, 210, red, true);      // left strip muted
        key (xs[0] + 40, 210, green, false);
        key (xs[1] - 40, 210, red, false);
        key (xs[1] + 40, 210, green, true);    // middle strip soloed
        key (xs[2] - 40, 210, red, false);
        key (xs[2] + 40, 210, green, false);

        // --- dB scale + fader slots -------------------------------------------------------
        const float slotTop = 300.0f, slotBottom = 850.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float x = xs[i];

            g.setColour (Colours::white.withAlpha (0.45f));
            for (float y = slotTop + 20; y <= slotBottom - 20; y += 42)
            {
                const bool major = std::fmod (y - slotTop - 20, 126.0f) < 1.0f;
                const float len = major ? 40.0f : 26.0f;
                g.fillRect (R (x - 22 - len, y - 2, len, major ? 5.0f : 3.5f));
                g.fillRect (R (x + 22, y - 2, len, major ? 5.0f : 3.5f));
            }

            // slot: dark channel with bevelled metal edges
            auto slot = R (x - 13, slotTop, 26, slotBottom - slotTop);
            g.setColour (Colours::white.withAlpha (0.18f));
            g.fillRoundedRectangle (slot.expanded (3 * s).translated (0, 2 * s), 14 * s);
            g.setColour (Colour (0xff040405));
            g.fillRoundedRectangle (slot, 12 * s);
        }

        // --- roof line through the caps ---------------------------------------------------
        Path roof;
        roof.startNewSubPath (P (150, 790));
        roof.lineTo (P (512, 470));
        roof.lineTo (P (874, 790));
        g.setColour (teal.withAlpha (0.25f));
        g.strokePath (roof, PathStrokeType (40 * s, PathStrokeType::mitered, PathStrokeType::rounded));
        g.setColour (teal);
        g.strokePath (roof, PathStrokeType (16 * s, PathStrokeType::mitered, PathStrokeType::rounded));

        // --- fader caps (front view of a classic long-throw cap) --------------------------
        for (int i = 0; i < 3; ++i)
        {
            const float cx = xs[i], cy = caps[i];
            const float w = 150.0f, h = 118.0f;
            auto cap = R (cx - w * 0.5f, cy - h * 0.5f, w, h);

            // shadow on the panel
            g.setColour (Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (cap.translated (0, 16 * s).expanded (4 * s), 16 * s);

            // body
            g.setColour (Colour (0xff0b0b0c));
            g.fillRoundedRectangle (cap, 14 * s);

            // upper sloped face (catches the light)
            auto upper = R (cx - w * 0.5f + 6, cy - h * 0.5f + 6, w - 12, h * 0.40f);
            ColourGradient up (Colour (0xff6a6c72), upper.getX(), upper.getY(), Colour (0xff26272a), upper.getX(), upper.getBottom(), false);
            g.setGradientFill (up);
            g.fillRoundedRectangle (upper, 9 * s);

            // lower sloped face (in shadow)
            auto lower = R (cx - w * 0.5f + 6, cy + h * 0.5f - 6 - h * 0.30f, w - 12, h * 0.30f);
            ColourGradient lo (Colour (0xff1a1b1d), lower.getX(), lower.getY(), Colour (0xff050506), lower.getX(), lower.getBottom(), false);
            g.setGradientFill (lo);
            g.fillRoundedRectangle (lower, 9 * s);

            // grip grooves on the upper face
            g.setColour (Colours::black.withAlpha (0.5f));
            for (float dy : { -44.0f, -34.0f, -24.0f })
                g.fillRect (R (cx - w * 0.5f + 14, cy + dy, w - 28, 3.5f));

            // white index line across the flat centre band
            g.setColour (Colour (0xfff6f5f0));
            g.fillRect (R (cx - w * 0.5f + 4, cy - 2, w - 8, 9));

            // rim light
            g.setColour (Colours::white.withAlpha (0.22f));
            g.drawRoundedRectangle (cap.reduced (2 * s), 13 * s, 3 * s);
        }

        if (withBackground)
        {
            g.setColour (Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (bg.reduced (2 * s), 183 * s, 4 * s);
        }
    }
}
