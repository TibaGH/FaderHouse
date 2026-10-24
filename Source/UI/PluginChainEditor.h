#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/MixerEngine.h"
#include "PluginWindows.h"

/** The insert slots of one channel or mix: click to open, right-click for bypass/order/remove. */
class PluginChainEditor final : public juce::Component
{
public:
    PluginChainEditor (PluginChain& chain, MixerEngine& engine, PluginWindowManager& windows,
                       std::function<juce::String()> contextName);

    void refresh();
    std::function<void()> onScanRequested;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int rowHeight = 22;

private:
    void showAddMenu();
    void showSlotMenu (int index);
    void openEditor (int index);

    struct SlotButton final : public juce::TextButton
    {
        std::function<void (const juce::MouseEvent&)> onMouseUp;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onMouseUp) onMouseUp (e); return; }
            TextButton::mouseUp (e);
        }
    };

    PluginChain& chain;
    MixerEngine& engine;
    PluginWindowManager& windows;
    std::function<juce::String()> contextName;

    juce::OwnedArray<SlotButton> slots;
    juce::OwnedArray<juce::TextButton> removeButtons;
    juce::TextButton addButton { "+ FX" };
};
