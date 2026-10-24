#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Analog-style long-throw fader: black cap with a white index line, a dark slot,
    and a printed dB scale (+10 ... -inf) like a console channel strip.

    Value is in dB; -60 means -inf (off). Double-click returns to 0 dB.
    Drag is relative (the cap doesn't jump to the mouse), hold Shift for fine moves.
*/
class ConsoleFader final : public juce::Component
{
public:
    ConsoleFader();
    ~ConsoleFader() override;

    void setValueDb (double db, juce::NotificationType n = juce::dontSendNotification);
    double getValueDb() const                      { return slider.getValue(); }

    std::function<void (double)> onValueChange;    // dB

    /** Colour of the thin "lit" line along the slot below the cap. */
    void setAccent (juce::Colour c)                { accent = c; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::String formatDb (double db);

    static constexpr int scaleWidth = 22;
    static constexpr int capHeight = 44;

private:
    struct FaderLookAndFeel;
    struct FineSlider;

    std::unique_ptr<FaderLookAndFeel> lnf;
    std::unique_ptr<FineSlider> sliderImpl;
    juce::Slider& slider;
    juce::Colour accent;
};
