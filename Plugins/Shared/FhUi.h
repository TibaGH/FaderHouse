#pragma once

// Shared look and controls of the Faderhouse plugins (same colours as the Faderhouse app).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "FhPlugin.h"

namespace fh::ui
{
namespace Colours
{
    const juce::Colour background   { 0xff121418 };
    const juce::Colour panel        { 0xff1b1e24 };
    const juce::Colour panelRaised  { 0xff242830 };
    const juce::Colour outline      { 0xff2e333c };
    const juce::Colour text         { 0xffe7e9ec };
    const juce::Colour textDim      { 0xff8b919b };
    const juce::Colour accent       { 0xff52c7a6 };
    const juce::Colour warm         { 0xffe0a458 };
    const juce::Colour blue         { 0xff6aa7f0 };
    const juce::Colour violet       { 0xffa58cf0 };
    const juce::Colour red          { 0xffe5484d };
    const juce::Colour green        { 0xff3ee07a };
    const juce::Colour meterMid     { 0xffe8c547 };
}

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override              { return juce::FontOptions (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override     { return juce::FontOptions (12.5f, juce::Font::bold); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/** Rotary knob with a name above and its value below. */
class Knob final : public juce::Component
{
public:
    Knob (PluginBase& processor, const juce::String& paramId, const juce::String& name, juce::Colour colour = Colours::accent);

    void resized() override;
    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A row of buttons for a choice parameter (e.g. Smart | Leveler). */
class ChoiceButtons final : public juce::Component, private juce::Timer
{
public:
    ChoiceButtons (PluginBase& processor, const juce::String& paramId, const juce::StringArray& labels);
    void resized() override;
    std::function<void()> onChange;

private:
    void timerCallback() override;
    PluginBase& processor;
    juce::String paramId;
    juce::OwnedArray<juce::TextButton> buttons;
    int shown = -1;
};

/** On/off button for a bool parameter. */
class ToggleKey final : public juce::TextButton
{
public:
    ToggleKey (PluginBase& processor, const juce::String& paramId, const juce::String& caption);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Vertical level meter (-60..0 dBFS), optionally with a threshold line. */
class LevelBar final : public juce::Component, private juce::Timer
{
public:
    LevelBar (std::function<float()> levelDb, const juce::String& caption);
    std::function<float()> thresholdDb;   // optional
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::function<float()> source;
    juce::String caption;
    float shownDb = -100.0f, holdDb = -100.0f;
    int holdCount = 0;
};

/** Gain change meter: reduction grows down from the top (red), boost up from the middle (green). */
class GainBar final : public juce::Component, private juce::Timer
{
public:
    GainBar (std::function<float()> gainDb, const juce::String& caption, float rangeDb = 24.0f, bool bipolar = false);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::function<float()> source;
    juce::String caption;
    float range;
    bool bipolar;
    float shown = 0.0f;
};

/** Small round light (e.g. gate open / voice detected). */
class Lamp final : public juce::Component, private juce::Timer
{
public:
    Lamp (std::function<float()> amount01, const juce::String& caption, juce::Colour colour);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::function<float()> source;
    juce::String caption;
    juce::Colour colour;
    float shown = 0.0f;
};

/** Editor base: background, header with the plugin name, our look. */
class EditorBase : public juce::AudioProcessorEditor
{
public:
    EditorBase (PluginBase& p, const juce::String& title, const juce::String& subtitle);
    ~EditorBase() override;
    void paint (juce::Graphics&) override;

protected:
    juce::Rectangle<int> getContentArea() const { return getLocalBounds().withTrimmedTop (headerHeight).reduced (14, 10); }
    static void paintGroup (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
    static constexpr int headerHeight = 44;

    PluginBase& base;

private:
    Look look;
    juce::String title, subtitle;
};
}
