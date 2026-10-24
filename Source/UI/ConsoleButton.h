#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FaderhouseLookAndFeel.h"

/** Square, backlit console key (MUTE / SOLO) with its label printed underneath. */
class ConsoleButton final : public juce::Button
{
public:
    ConsoleButton (const juce::String& label, juce::Colour litColour)
        : Button (label), lit (litColour)
    {
        setClickingTogglesState (true);
    }

    static constexpr int labelHeight = 13;
    static constexpr int glowMargin = 5;     // room around the key so the glow isn't clipped
    static constexpr int keySize = 34;
    static constexpr int preferredWidth  = keySize + 2 * glowMargin;
    static constexpr int preferredHeight = keySize + 2 * glowMargin + labelHeight;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        auto label = b.removeFromBottom ((float) labelHeight);
        const float size = juce::jmin (b.getWidth(), b.getHeight()) - 2.0f * (float) glowMargin;
        auto key = juce::Rectangle<float> (size, size).withCentre (b.getCentre());
        const bool on = getToggleState();

        // well
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (key.expanded (1.5f), 4.0f);

        // glow when lit
        if (on)
        {
            // soft halo on all four sides
            g.setColour (lit.withAlpha (0.12f));
            g.fillRoundedRectangle (key.expanded ((float) glowMargin), 9.0f);
            g.setColour (lit.withAlpha (0.22f));
            g.fillRoundedRectangle (key.expanded ((float) glowMargin * 0.55f), 7.0f);
        }

        // key face
        const auto base = on ? lit : juce::Colour (0xff4a4c50);
        juce::ColourGradient face (base.brighter (on ? 0.45f : 0.25f), key.getX(), key.getY(),
                                   base.darker (on ? 0.15f : 0.35f), key.getX(), key.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (key.translated (0.0f, down ? 1.0f : 0.0f), 3.0f);

        // diffuser highlight
        g.setColour (juce::Colours::white.withAlpha (on ? 0.35f : (over ? 0.14f : 0.08f)));
        g.drawRoundedRectangle (key.reduced (1.0f), 3.0f, 1.0f);

        g.setColour (on ? lit.brighter (0.3f) : Theme::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (getButtonText().toUpperCase(), label, juce::Justification::centred);
    }

private:
    juce::Colour lit;
};
