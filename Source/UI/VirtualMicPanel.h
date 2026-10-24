#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../VirtualMic/VirtualMicInstaller.h"

class MixerEngine;

/** Status + Install / Remove buttons for the built-in "Mic FX" device. */
class VirtualMicPanel final : public juce::Component
{
public:
    explicit VirtualMicPanel (MixerEngine& engine);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Runs install (password prompt), then restarts Faderhouse. Shared with the first-run prompt. */
    static void runInstall (MixerEngine& engine);
    static void runUninstall (MixerEngine& engine);

private:
    MixerEngine& engine;
    juce::Label title, status;
    juce::TextButton installButton, removeButton { "Remove" };
};
