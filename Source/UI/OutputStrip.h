#pragma once

#include "../Engine/MixerEngine.h"
#include "ChannelStrip.h"

/** Master strip of one mix: where it goes, its inserts, master fader. Click the strip to select the mix. */
class OutputStrip final : public juce::Component
{
public:
    OutputStrip (Output& output, MixerEngine& engine, PluginWindowManager& windows);

    void setSelected (bool shouldBeSelected);
    void updateMeters()   { meter.update(); }
    void refreshFromModel();
    void refreshRouting();   // cheap, called by the UI timer

    std::function<void()> onSelect, onOpenRouting, onScanRequested;

    Output& output;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int stripWidth = 140;

private:
    void showMenu();

    MixerEngine& engine;
    bool selected = false;

    juce::Label nameLabel;
    juce::TextButton menuButton { "..." };
    juce::TextButton routeButton;
    PluginChainEditor chainEditor;
    LevelMeterComponent meter;
    ConsoleFader fader;
    ConsoleButton muteButton { "Mute", Theme::mute };
    juce::Label readout, caption;
};

/** The fixed mic direct out to the "Mic FX" device: meter, device and the global MIC MUTE. */
class MicFxStrip final : public juce::Component
{
public:
    MicFxStrip (MixerEngine& engine);

    void updateMeters();
    void refreshRouting();

    std::function<void()> onOpenRouting;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int stripWidth = 110;

private:
    MixerEngine& engine;
    ChannelIconButton icon;
    juce::Label title, info;
    juce::TextButton routeButton;
    LevelMeterComponent meter;
    ConsoleButton muteButton { "Mic mute", Theme::mute };
};
