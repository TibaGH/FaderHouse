#include "MainComponent.h"
#include "SettingsComponent.h"
#include "VirtualMicPanel.h"
#include "AppLogo.h"

using namespace juce;

namespace
{
constexpr int topBarHeight = 48;
constexpr int statusBarHeight = 26;
constexpr int stripGap = 6;

struct ScannerWindow final : public DocumentWindow
{
    ScannerWindow (Component* content, std::function<void()> onClose)
        : DocumentWindow ("Plugins", Theme::panel, DocumentWindow::closeButton), closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
        centreWithSize (720, 520);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        auto cb = closeCallback;
        MessageManager::callAsync (cb);
    }

    std::function<void()> closeCallback;
};
} // namespace

void LogoComponent::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    AppLogo::draw (g, b.removeFromLeft (b.getHeight()).expanded (3.0f));
    b.removeFromLeft (6.0f);
    g.setColour (Theme::text);
    g.setFont (FontOptions (17.0f, Font::bold));
    g.drawText ("FADERHOUSE", b, Justification::centredLeft);
}

void MicMuteButton::paintButton (Graphics& g, bool over, bool down)
{
    const bool muted = getToggleState();
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const auto base = muted ? Theme::mute : Colour (0xff1f3a30);

    g.setColour (base.brighter (down ? 0.2f : (over ? 0.1f : 0.0f)));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (muted ? Theme::mute.brighter (0.4f) : Theme::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);

    auto dot = b.removeFromLeft (b.getHeight()).reduced (b.getHeight() * 0.3f);
    g.setColour (muted ? Colours::white : Theme::accent);
    g.fillEllipse (dot);

    g.setColour (Colours::white);
    g.setFont (FontOptions (12.5f, Font::bold));
    g.drawText (muted ? "MIC MUTED" : "MIC LIVE", b.withTrimmedRight (10.0f), Justification::centred);
}

MainComponent::MainComponent (MixerEngine& e) : engine (e)
{
    addAndMakeVisible (logo);

    micMuteButton.setTooltip ("Mute your mic everywhere: Discord/OBS (Mic FX) and your monitor.  Shortcut: Cmd+Shift+M");
    micMuteButton.setToggleState (engine.isMicMuted(), dontSendNotification);
    micMuteButton.onClick = [this] { engine.setMicMuted (micMuteButton.getToggleState()); };
    addAndMakeVisible (micMuteButton);
    setWantsKeyboardFocus (true);

    addChannelButton.onClick = [this] { showAddChannelMenu(); };
    pluginsButton.onClick = [this] { openPluginScanner(); };
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible (addChannelButton);
    addAndMakeVisible (pluginsButton);
    addAndMakeVisible (settingsButton);

    statusLabel.setFont (FontOptions (12.0f));
    statusLabel.setColour (Label::textColourId, Theme::textDim);
    addAndMakeVisible (statusLabel);

    channelViewport.setViewedComponent (&channelHolder, false);
    channelViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (channelViewport);

    outputViewport.setViewedComponent (&outputHolder, false);
    outputViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (outputViewport);

    engine.onStructureChanged = [this] { triggerAsyncUpdate(); };
    engine.onPluginAboutToBeDeleted = [this] (AudioProcessor* p) { pluginWindows.close (p); };

    rebuild();
    setSize (1280, 840);
    startTimerHz (30);

    // First run: offer to install our own virtual microphone.
    MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->offerVirtualMicInstall();
    });

    if (! engine.lastLoadErrors.isEmpty())
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Some plugins could not be loaded",
                                          engine.lastLoadErrors.joinIntoString ("\n"));
}

MainComponent::~MainComponent()
{
    engine.onStructureChanged = nullptr;
    engine.onPluginAboutToBeDeleted = nullptr;
    pluginWindows.closeAll();

    if (settingsWindow != nullptr)
        delete settingsWindow.getComponent();
}

void MainComponent::offerVirtualMicInstall()
{
    const auto state = VirtualMic::getState();
    const bool update = state == VirtualMic::State::UpdateAvailable;

    if (state != VirtualMic::State::NotInstalled && ! update)
        return;

    if (! update && engine.settings->getBoolValue ("virtualMicPromptDismissed", false))
        return;

    auto* engineRef = &engine;
    AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon,
        update ? "Update Mic FX?" : "Install Mic FX?",
        update ? String ("This Faderhouse contains a newer version of its virtual microphone. Update it now? (admin password needed)")
               : String ("Faderhouse can install its own virtual microphone, \"Mic FX\", so Discord and OBS get your "
                         "processed mic. No other software needed.\n\nThis asks for your admin password once and "
                         "restarts Faderhouse. You can also do this later in Settings > Mix routing."),
        update ? "Update" : "Install", "Later", nullptr,
        ModalCallbackFunction::create ([engineRef, update] (int ok)
        {
            if (ok)
            {
                VirtualMicPanel::runInstall (*engineRef);
            }
            else if (! update)
            {
                engineRef->settings->setValue ("virtualMicPromptDismissed", true);
                engineRef->settings->saveIfNeeded();
            }
        }));
}

//==============================================================================
void MainComponent::rebuild()
{
    channelStrips.clear();
    outputStrips.clear();

    for (auto& s : engine.getSources())
    {
        auto* strip = channelStrips.add (new ChannelStrip (*s, engine, pluginWindows));
        strip->onScanRequested = [this] { openPluginScanner(); };
        channelHolder.addAndMakeVisible (strip);
    }

    for (auto& o : engine.getOutputs())
    {
        auto* strip = outputStrips.add (new OutputStrip (*o, engine, pluginWindows));
        const int slot = o->slot;
        strip->onSelect = [this, slot] { selectMix (slot); };
        strip->onOpenRouting = [this] { openSettings (1); };
        strip->onScanRequested = [this] { openPluginScanner(); };
        outputHolder.addAndMakeVisible (strip);
    }

    micFxStrip = std::make_unique<MicFxStrip> (engine);
    micFxStrip->onOpenRouting = [this] { openSettings (1); };
    addAndMakeVisible (*micFxStrip);

    if (engine.findOutputBySlot (selectedSlot) == nullptr && ! engine.getOutputs().empty())
        selectedSlot = engine.getOutputs().front()->slot;

    updateMixTabs();
    selectMix (selectedSlot);
    resized();
}

void MainComponent::updateMixTabs()
{
    mixTabs.clear();

    for (auto& o : engine.getOutputs())
    {
        auto* b = mixTabs.add (new TextButton (o->name));
        const int slot = o->slot;
        b->setClickingTogglesState (false);
        b->setToggleState (slot == selectedSlot, dontSendNotification);
        b->setTooltip ("Show the faders of the \"" + o->name + "\" mix");
        b->onClick = [this, slot] { selectMix (slot); };
        addAndMakeVisible (b);
    }

    auto* add = mixTabs.add (new TextButton ("+"));
    add->setTooltip ("Add a mix (another output)");
    add->onClick = [this]
    {
        if (auto* o = engine.addOutput ("Mix " + String (engine.getOutputs().size() + 1)))
        {
            engine.setOutputToNone (*o);
            selectedSlot = o->slot;
            openSettings (1);
        }
    };
    addAndMakeVisible (add);
    resized();
}

void MainComponent::selectMix (int slot)
{
    selectedSlot = slot;
    auto* output = engine.findOutputBySlot (slot);
    const auto name = output != nullptr ? output->name : String ("-");

    for (auto* strip : channelStrips)
        strip->setMix (slot, name);

    for (auto* strip : outputStrips)
        strip->setSelected (strip->output.slot == slot);

    for (int i = 0; i < mixTabs.size() - 1 && i < (int) engine.getOutputs().size(); ++i)
    {
        mixTabs[i]->setButtonText (engine.getOutputs()[(size_t) i]->name);
        mixTabs[i]->setToggleState (engine.getOutputs()[(size_t) i]->slot == slot, dontSendNotification);
    }
}

void MainComponent::showAddChannelMenu()
{
    PopupMenu menu;
    menu.addItem (1, "App channel  (Discord, game, browser...)");
    menu.addItem (2, "Input channel  (mic / instrument on your interface)");

    const bool hasSystem = std::any_of (engine.getSources().begin(), engine.getSources().end(),
                                        [] (auto& s) { return s->kind == SourceKind::System; });
    menu.addItem (3, "System channel  (all unassigned apps)", ! hasSystem);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (addChannelButton), [this] (int result)
    {
        switch (result)
        {
            case 1: engine.addSource (SourceKind::App, "New app"); break;
            case 2: engine.addSource (SourceKind::Input, "Input"); break;
            case 3: engine.addSource (SourceKind::System, "System"); break;
            default: return;
        }

        // Scroll to the new strip once the rebuild has happened.
        MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->channelViewport.setViewPositionProportionately (1.0, 0.0);
        });
    });
}

void MainComponent::openSettings (int tab)
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront (true);
        if (auto* s = dynamic_cast<SettingsComponent*> (settingsWindow->getContentComponent()))
            s->showTab (tab);
        return;
    }

    auto* content = new SettingsComponent (engine);
    content->onRoutingChanged = [this] { triggerAsyncUpdate(); };

    DialogWindow::LaunchOptions options;
    options.content.setOwned (content);
    options.dialogTitle = "Faderhouse Settings";
    options.dialogBackgroundColour = Theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    settingsWindow = options.launchAsync();

    content->showTab (tab);
}

void MainComponent::openPluginScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    auto deadMansPedal = engine.settings->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
    auto* list = new PluginListComponent (engine.formatManager, engine.knownPlugins, deadMansPedal,
                                          engine.settings.get(), true);

    scannerWindow = std::make_unique<ScannerWindow> (list, [safe = SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->scannerWindow.reset();
    });
}

//==============================================================================
void MainComponent::timerCallback()
{
    for (auto* s : channelStrips) s->updateMeters();
    for (auto* s : outputStrips)  s->updateMeters();
    if (micFxStrip != nullptr)    micFxStrip->updateMeters();

    if (micMuteButton.getToggleState() != engine.isMicMuted())
        micMuteButton.setToggleState (engine.isMicMuted(), dontSendNotification);

    if (++tick % 15 != 0)
        return;

    // Twice a second: status line and output routing labels.
    String status;
    if (auto* d = engine.deviceManager.getCurrentAudioDevice())
        status << d->getName() << "   " << String (d->getCurrentSampleRate() / 1000.0, 1) << " kHz / "
               << d->getCurrentBufferSizeSamples() << " smp   CPU " << roundToInt (engine.deviceManager.getCpuUsage() * 100.0) << "%";
    else
        status << "No audio interface selected - open Settings";

    status << "      |      " << engine.getCaptureStatus();
    statusLabel.setText (status, dontSendNotification);

    for (auto* s : outputStrips)
        s->refreshRouting();
    if (micFxStrip != nullptr)
        micFxStrip->refreshRouting();
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    if (key == KeyPress ('m', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0))
    {
        engine.setMicMuted (! engine.isMicMuted());
        micMuteButton.setToggleState (engine.isMicMuted(), dontSendNotification);
        return true;
    }
    return false;
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);

    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (topBarHeight));
    g.fillRect (getLocalBounds().removeFromBottom (statusBarHeight));

    // divider between channels and mixes
    g.setColour (Theme::outline);
    g.fillRect (outputViewport.getX() - 6, topBarHeight + 8, 1, getHeight() - topBarHeight - statusBarHeight - 16);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (topBarHeight).reduced (10, 9);
    logo.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    settingsButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (6);
    pluginsButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    addChannelButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (16);
    micMuteButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (16);

    for (auto* b : mixTabs)
    {
        const int w = b->getButtonText() == "+" ? 30 : jmax (80, GlyphArrangement::getStringWidthInt (FontOptions (14.0f), b->getButtonText()) + 24);
        b->setBounds (top.removeFromLeft (w));
        top.removeFromLeft (4);
    }

    statusLabel.setBounds (area.removeFromBottom (statusBarHeight).reduced (10, 0));

    area.reduce (8, 8);
    if (micFxStrip != nullptr)
    {
        micFxStrip->setBounds (area.removeFromRight (MicFxStrip::stripWidth).withTrimmedBottom (10));
        area.removeFromRight (stripGap);
    }
    const int outputsWidth = jmin ((int) engine.getOutputs().size(), 3) * (OutputStrip::stripWidth + stripGap);
    outputViewport.setBounds (area.removeFromRight (outputsWidth));
    area.removeFromRight (10);
    channelViewport.setBounds (area);

    layoutStrips();
}

void MainComponent::layoutStrips()
{
    const int hScroll = 10;

    const int chHeight = jmax (ChannelStrip::minimumHeight, channelViewport.getHeight() - hScroll);
    channelHolder.setSize (jmax (channelViewport.getWidth(), channelStrips.size() * (ChannelStrip::stripWidth + stripGap)), chHeight);
    int x = 0;
    for (auto* s : channelStrips)
    {
        s->setBounds (x, 0, ChannelStrip::stripWidth, chHeight);
        x += ChannelStrip::stripWidth + stripGap;
    }

    const int outHeight = jmax (ChannelStrip::minimumHeight, outputViewport.getHeight() - hScroll);
    outputHolder.setSize (jmax (outputViewport.getWidth(), outputStrips.size() * (OutputStrip::stripWidth + stripGap)), outHeight);
    x = 0;
    for (auto* s : outputStrips)
    {
        s->setBounds (x, 0, OutputStrip::stripWidth, outHeight);
        x += OutputStrip::stripWidth + stripGap;
    }
}
