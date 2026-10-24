#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Engine/MixerEngine.h"
#include "ChannelStrip.h"
#include "OutputStrip.h"
#include "PluginWindows.h"

/** Top-bar logo: the app icon plus the name. */
struct LogoComponent final : public juce::Component
{
    void paint (juce::Graphics&) override;
};

/** Big global mic mute in the top bar ("MIC LIVE" / "MIC MUTED"). */
struct MicMuteButton final : public juce::Button
{
    MicMuteButton() : Button ("Mic") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::AsyncUpdater
{
public:
    explicit MainComponent (MixerEngine& engine);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void handleAsyncUpdate() override { rebuild(); }

    void rebuild();
    void selectMix (int slot);
    void updateMixTabs();
    void showAddChannelMenu();
    void openSettings (int tab = 0);
    void openPluginScanner();
    void layoutStrips();
    void offerVirtualMicInstall();

    MixerEngine& engine;
    PluginWindowManager pluginWindows;

    LogoComponent logo;
    MicMuteButton micMuteButton;
    juce::OwnedArray<juce::TextButton> mixTabs;
    juce::TextButton addChannelButton { "+ Channel" };
    juce::TextButton pluginsButton { "Plugins" };
    juce::TextButton settingsButton { "Settings" };
    juce::Label statusLabel;

    juce::Viewport channelViewport;
    juce::Component channelHolder;
    juce::Viewport outputViewport;
    juce::Component outputHolder;

    juce::OwnedArray<ChannelStrip> channelStrips;
    juce::OwnedArray<OutputStrip> outputStrips;
    std::unique_ptr<MicFxStrip> micFxStrip;

    int selectedSlot = 0;
    int tick = 0;

    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;
    juce::TooltipWindow tooltips { this, 600 };
};
