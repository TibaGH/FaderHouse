#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelIcons.h"
#include "FaderhouseLookAndFeel.h"

/**
    The Faderhouse logo: three channel strips drawn the way the app draws them
    (strip panel with colour band, channel icon, MUTE / SOLO keys, meter, fader with
    scale). The three fader caps form the gable of a house.
    Used for the app icon (rendered to PNG by Tools/IconRender) and in the top bar.
*/
namespace AppLogo
{
    inline void draw (juce::Graphics& g, juce::Rectangle<float> area, bool withBackground = true, bool roofLine = false)
    {
        using namespace juce;

        const float s = jmin (area.getWidth(), area.getHeight()) / 1024.0f;
        const auto origin = area.getCentre() - Point<float> (512.0f * s, 512.0f * s);
        auto P = [&] (float x, float y) { return origin + Point<float> (x * s, y * s); };
        auto R = [&] (float x, float y, float w, float h) { return Rectangle<float> (P (x, y), P (x + w, y + h)); };

        // --- app background ---------------------------------------------------------------
        auto bg = R (100, 100, 824, 824);
        if (withBackground)
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (bg.translated (0, 10 * s).expanded (4 * s), 190 * s);
            g.setColour (Theme::background);
            g.fillRoundedRectangle (bg, 185 * s);
        }

        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (Path ([&] { Path p; p.addRoundedRectangle (bg, 185 * s); return p; }()));

        struct StripDef { Colour colour; const char* icon; float capY; float meterL, meterR; bool mute, solo; };
        const StripDef strips[] = {
            { Theme::input,  "mic",     640.0f, 0.55f, 0.50f, true,  false },
            { Theme::app,    "music",   470.0f, 0.82f, 0.78f, false, true  },
            { Theme::system, "desktop", 640.0f, 0.38f, 0.42f, false, false },
        };

        const float stripW = 224.0f, gap = 14.0f;
        const float x0 = 512.0f - (3.0f * stripW + 2.0f * gap) * 0.5f;
        const float top = 168.0f, bottom = 872.0f;
        const float slotTop = 340.0f, slotBottom = 810.0f;

        float capCentres[3] {};

        for (int i = 0; i < 3; ++i)
        {
            const auto& d = strips[i];
            const float sx = x0 + (float) i * (stripW + gap);
            auto strip = R (sx, top, stripW, bottom - top);

            // strip panel (as paintStripPanel in the app)
            g.setGradientFill (ColourGradient (Theme::stripTop, strip.getX(), strip.getY(),
                                               Theme::stripBottom, strip.getX(), strip.getBottom(), false));
            g.fillRoundedRectangle (strip, 18 * s);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (strip, 18 * s, 3 * s);

            g.setColour (d.colour);
            g.fillRoundedRectangle (R (sx + 22, top + 4, stripW - 44, 12), 6 * s);

            // channel icon tile
            auto tile = R (sx + stripW * 0.5f - 42, top + 40, 84, 84);
            g.setColour (d.colour.withAlpha (0.20f));
            g.fillRoundedRectangle (tile, 16 * s);
            ChannelIcons::drawGeneric (g, d.icon, tile.reduced (tile.getWidth() * 0.2f), d.colour.brighter (0.3f));

            // left column: MUTE / SOLO keys and the meter
            auto key = [&] (float ky, Colour lit, bool on)
            {
                auto k = R (sx + 18, ky, 64, 64);
                g.setColour (Colours::black.withAlpha (0.6f));
                g.fillRoundedRectangle (k.expanded (4 * s), 10 * s);
                if (on)
                {
                    g.setColour (lit.withAlpha (0.30f));
                    g.fillRoundedRectangle (k.expanded (12 * s), 18 * s);
                }
                const auto base = on ? lit : Colour (0xff4a4c50);
                g.setGradientFill (ColourGradient (base.brighter (on ? 0.45f : 0.25f), k.getX(), k.getY(),
                                                   base.darker (on ? 0.15f : 0.35f), k.getX(), k.getBottom(), false));
                g.fillRoundedRectangle (k, 8 * s);
                g.setColour (Colours::white.withAlpha (on ? 0.35f : 0.10f));
                g.drawRoundedRectangle (k.reduced (2 * s), 7 * s, 2.5f * s);
            };
            key (slotTop - 10, Theme::mute, d.mute);
            key (slotTop + 74, Theme::solo, d.solo);

            auto meterArea = R (sx + 30, slotTop + 170, 40, slotBottom - slotTop - 170);
            auto bar = [&] (Rectangle<float> r, float level)
            {
                g.setColour (Theme::background);
                g.fillRoundedRectangle (r, 3 * s);
                ColourGradient grad (Theme::meterLow, 0, r.getBottom(), Theme::meterHigh, 0, r.getY(), false);
                grad.addColour (0.65, Theme::meterLow);
                grad.addColour (0.85, Theme::meterMid);
                g.setGradientFill (grad);
                g.fillRect (r.withTop (r.getBottom() - r.getHeight() * level));
            };
            bar (meterArea.withWidth (meterArea.getWidth() * 0.45f), d.meterL);
            bar (meterArea.withTrimmedLeft (meterArea.getWidth() * 0.55f), d.meterR);

            // fader: scale ticks, slot, lit line, cap (as ConsoleFader in the app)
            const float cx = sx + 156.0f;
            g.setColour (Theme::textDim.withAlpha (0.55f));
            for (float y = slotTop + 12; y <= slotBottom - 12; y += 48)
            {
                g.fillRect (R (cx - 46, y - 2, 24, 4));
                g.fillRect (R (cx + 22, y - 2, 24, 4));
            }

            auto slot = R (cx - 9, slotTop, 18, slotBottom - slotTop);
            g.setColour (Colour (0xff050506));
            g.fillRoundedRectangle (slot, 8 * s);

            const float capY = d.capY;
            capCentres[i] = capY;
            g.setColour (d.colour.withAlpha (0.85f));
            g.fillRect (R (cx - 3, capY, 6, slotBottom - capY - 14));

            const float w = 64.0f, h = 96.0f;
            auto cap = R (cx - w * 0.5f, capY - h * 0.5f, w, h);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (cap.translated (0, 10 * s).expanded (3 * s), 10 * s);
            g.setColour (Colour (0xff0b0b0c));
            g.fillRoundedRectangle (cap, 9 * s);

            auto upper = cap.reduced (4 * s).withHeight (cap.getHeight() * 0.40f);
            g.setGradientFill (ColourGradient (Colour (0xff6a6c72), upper.getX(), upper.getY(),
                                               Colour (0xff26272a), upper.getX(), upper.getBottom(), false));
            g.fillRoundedRectangle (upper, 6 * s);

            auto lower = cap.reduced (4 * s);
            lower = lower.withTop (lower.getBottom() - cap.getHeight() * 0.30f);
            g.setGradientFill (ColourGradient (Colour (0xff1a1b1d), lower.getX(), lower.getY(),
                                               Colour (0xff050506), lower.getX(), lower.getBottom(), false));
            g.fillRoundedRectangle (lower, 6 * s);

            g.setColour (Colours::black.withAlpha (0.5f));
            for (float dy : { -38.0f, -30.0f, -22.0f })
                g.fillRect (R (cx - w * 0.5f + 9, capY + dy, w - 18, 3));

            g.setColour (Colour (0xfff6f5f0));
            g.fillRect (R (cx - w * 0.5f + 3, capY - 3, w - 6, 6));

            g.setColour (Colours::white.withAlpha (0.14f));
            g.drawRoundedRectangle (cap.reduced (1.5f * s), 9 * s, 2.5f * s);
        }

        if (roofLine)
        {
            Path roof;
            const float c0 = x0 + 156.0f, c1 = c0 + stripW + gap, c2 = c1 + stripW + gap;
            roof.startNewSubPath (P (c0, capCentres[0]));
            roof.lineTo (P (c1, capCentres[1]));
            roof.lineTo (P (c2, capCentres[2]));
            g.setColour (Theme::accent.withAlpha (0.9f));
            g.strokePath (roof, PathStrokeType (8 * s, PathStrokeType::mitered, PathStrokeType::rounded));
        }

        if (withBackground)
        {
            g.setColour (Colours::white.withAlpha (0.10f));
            g.drawRoundedRectangle (bg.reduced (2 * s), 183 * s, 4 * s);
        }
    }
}
