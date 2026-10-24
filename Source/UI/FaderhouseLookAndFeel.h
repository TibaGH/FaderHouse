#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    const juce::Colour background   { 0xff121418 };
    const juce::Colour panel        { 0xff1b1e24 };
    const juce::Colour panelRaised  { 0xff242830 };
    const juce::Colour outline      { 0xff2e333c };
    const juce::Colour text         { 0xffe7e9ec };
    const juce::Colour textDim      { 0xff8b919b };
    const juce::Colour accent       { 0xff52c7a6 };   // selected mix, fader caps
    const juce::Colour input        { 0xffe0a458 };   // mic / input channels
    const juce::Colour app          { 0xff6aa7f0 };   // app channels
    const juce::Colour system       { 0xffa58cf0 };   // system channel
    const juce::Colour output       { 0xff52c7a6 };   // mixes
    const juce::Colour mute         { 0xffe5484d };   // lit MUTE (red)
    const juce::Colour solo         { 0xff3ee07a };   // lit SOLO (green)
    const juce::Colour stripTop     { 0xff2a2c31 };
    const juce::Colour stripBottom  { 0xff1c1d21 };
    const juce::Colour meterLow     { 0xff3ccf7a };
    const juce::Colour meterMid     { 0xffe8c547 };
    const juce::Colour meterHigh    { 0xffe5484d };
}

class FaderhouseLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FaderhouseLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
};
