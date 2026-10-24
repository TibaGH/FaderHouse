#include <juce_gui_extra/juce_gui_extra.h>
#include "Engine/MixerEngine.h"
#include "Engine/PluginScanner.h"
#include "UI/MainComponent.h"
#include "UI/ChannelStrip.h"
#include "UI/FaderhouseLookAndFeel.h"

class FaderhouseApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    // Must be true so the plugin-scanner helper (a second copy of this binary) can start.
    // A second *GUI* instance is prevented with an inter-process lock below.
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (PluginScanner::runAsScannerIfRequested (commandLine))
            return;   // this process only scans plugins for the main app

        instanceLock = std::make_unique<juce::InterProcessLock> ("FaderhouseMainInstance");
        if (! instanceLock->enter (0))
        {
            quit();   // Faderhouse is already running
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<MixerEngine>();
        engine->initialise();

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        mainWindow.reset();   // closes plugin editors first
        engine.reset();       // saves the session, stops capture, unloads plugins
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        instanceLock.reset();
    }

    void systemRequestedQuit() override                    { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, MixerEngine& engine)
            : DocumentWindow (name, Theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            setResizable (true, true);
            // Big enough that the faders always keep their full size (top bar + status bar + strip).
            // (the frame difference covers a title bar, if the window size includes it)
            const int frame = juce::jmax (0, getHeight() - getContentComponent()->getHeight());
            setResizeLimits (960, MainComponent::getMinimumHeight() + frame, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    FaderhouseLookAndFeel lookAndFeel;
    std::unique_ptr<MixerEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::InterProcessLock> instanceLock;
};

START_JUCE_APPLICATION (FaderhouseApplication)
