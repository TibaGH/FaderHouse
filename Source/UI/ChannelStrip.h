#pragma once

#include "../Engine/MixerEngine.h"
#include "ChannelIcons.h"
#include "ConsoleButton.h"
#include "ConsoleFader.h"
#include "LevelMeterComponent.h"
#include "PluginChainEditor.h"

/** "com.hnc.Discord" -> "Discord", "exe:Game.exe" -> "Game.exe". */
juce::String prettyAppName (const juce::String& appId);

/** Brushed console panel with a colour band and two screws, shared by all strips. */
void paintStripPanel (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour band, bool highlighted);

class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip (Source& source, MixerEngine& engine, PluginWindowManager& windows);

    void setMix (int slot, const juce::String& mixName);
    void updateMeters();
    void refreshFromModel();

    std::function<void()> onScanRequested;

    Source& source;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int stripWidth = 140;

private:
    void showChannelMenu();
    void showAppPicker();
    void showIconPicker();
    void rebuildInputChoices();
    void updateIcon();
    juce::Colour kindColour() const;

    MixerEngine& engine;
    int slot = 0;
    juce::String mixName;
    bool showingMicMuted = false;
    juce::Rectangle<int> badgeArea;

    ChannelIconButton iconButton;
    juce::Label nameLabel;
    juce::TextButton menuButton { "..." };
    juce::ComboBox inputChoice;
    juce::TextButton appsButton;
    juce::Label infoLabel;
    PluginChainEditor chainEditor;
    LevelMeterComponent meter;
    ConsoleFader fader;
    ConsoleButton muteButton { "Mute", Theme::mute };
    ConsoleButton soloButton { "Solo", Theme::solo };
    juce::Label readout, mixLabel;
};
