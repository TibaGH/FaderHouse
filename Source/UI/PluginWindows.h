#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FaderhouseLookAndFeel.h"
#include <memory>
#include <set>

/** Keeps one editor window per hosted plugin. */
class PluginWindowManager
{
public:
    ~PluginWindowManager() { *alive = false; closeAll(); }

    void show (juce::AudioProcessor& processor, const juce::String& context, int attempt = 0)
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

        // The plugin's previous editor can take a moment to really go away after its window
        // closed (seen with VST3s on macOS). A plugin then refuses a new editor and you'd get
        // the plain slider view, so wait for it (up to 2 s) before opening.
        if (processor.hasEditor() && processor.getActiveEditor() != nullptr && attempt < 20)
        {
            pending.insert (&processor);
            auto token = alive;
            juce::Timer::callAfterDelay (100, [this, token, p = &processor, context, attempt]
            {
                if (*token && pending.erase (p) > 0)
                    show (*p, context, attempt + 1);
            });
            return;
        }

        pending.erase (&processor);
        windows.add (new Window (processor, context, *this));
    }

    void close (juce::AudioProcessor* processor)
    {
        pending.erase (processor);   // the plugin is going away: don't open it later
        for (int i = windows.size(); --i >= 0;)
            if (&windows[i]->processor == processor)
                windows.remove (i);
    }

    void closeAll() { pending.clear(); windows.clear(); }

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

        ~Window() override
        {
            // Tell the plugin its editor is going away, otherwise it thinks it still has one and
            // the next time it can't make a new editor (you'd get the plain slider view instead).
            if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
                processor.editorBeingDeleted (editor);
            clearContentComponent();
        }

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
    std::set<juce::AudioProcessor*> pending;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
