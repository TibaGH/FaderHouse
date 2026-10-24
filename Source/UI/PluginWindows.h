#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FaderhouseLookAndFeel.h"

/** Keeps one editor window per hosted plugin. */
class PluginWindowManager
{
public:
    ~PluginWindowManager() { closeAll(); }

    void show (juce::AudioProcessor& processor, const juce::String& context)
    {
        for (auto* w : windows)
        {
            if (&w->processor == &processor)
            {
                w->setVisible (true);
                w->toFront (true);
                return;
            }
        }

        windows.add (new Window (processor, context, *this));
    }

    void close (juce::AudioProcessor* processor)
    {
        for (int i = windows.size(); --i >= 0;)
            if (&windows[i]->processor == processor)
                windows.remove (i);
    }

    void closeAll() { windows.clear(); }

private:
    struct Window final : public juce::DocumentWindow
    {
        Window (juce::AudioProcessor& p, const juce::String& context, PluginWindowManager& m)
            : DocumentWindow (p.getName() + "  -  " + context, Theme::panel, DocumentWindow::closeButton),
              processor (p), manager (m)
        {
            setUsingNativeTitleBar (true);

            juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;
            if (editor == nullptr)
                editor = new juce::GenericAudioProcessorEditor (p);

            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        ~Window() override { clearContentComponent(); }

        void closeButtonPressed() override
        {
            // Delete asynchronously: we are inside this window's own callback.
            auto* p = &processor;
            auto* m = &manager;
            juce::MessageManager::callAsync ([m, p] { m->close (p); });
            setVisible (false);
        }

        juce::AudioProcessor& processor;
        PluginWindowManager& manager;
    };

    juce::OwnedArray<Window> windows;
};
