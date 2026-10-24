#include "OutputStrip.h"

using namespace juce;

OutputStrip::OutputStrip (Output& o, MixerEngine& e, PluginWindowManager& windows)
    : output (o), engine (e),
      chainEditor (o.chain, e, windows, [this] { return output.name + " (mix)"; }),
      meter (o.meter)
{
    nameLabel.setFont (FontOptions (14.0f, Font::bold));
    nameLabel.setEditable (false, true, false);
    nameLabel.setTooltip ("Click the strip to select this mix, double-click to rename");
    nameLabel.onTextChange = [this]
    {
        output.name = nameLabel.getText().trim().isEmpty() ? String ("Mix") : nameLabel.getText().trim();
        if (onSelect) onSelect(); // refreshes the mix tabs
    };
    nameLabel.addMouseListener (this, false);
    addAndMakeVisible (nameLabel);

    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);

    routeButton.setTooltip ("Where this mix goes");
    routeButton.onClick = [this] { if (onOpenRouting) onOpenRouting(); };
    addAndMakeVisible (routeButton);

    chainEditor.onScanRequested = [this] { if (onScanRequested) onScanRequested(); };
    addAndMakeVisible (chainEditor);
    addAndMakeVisible (meter);

    fader.setAccent (Theme::output);
    fader.onValueChange = [this] (double db)
    {
        output.level.store (sliderDbToGain ((float) db));
        readout.setText (ConsoleFader::formatDb (db), dontSendNotification);
    };
    addAndMakeVisible (fader);

    muteButton.setTooltip ("Mute this whole mix");
    muteButton.onClick = [this] { output.mute.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    readout.setFont (FontOptions (12.0f, Font::bold));
    readout.setJustificationType (Justification::centred);
    readout.setColour (Label::backgroundColourId, Colours::black.withAlpha (0.45f));
    addAndMakeVisible (readout);

    caption.setText ("MIX MASTER", dontSendNotification);
    caption.setFont (FontOptions (10.5f));
    caption.setJustificationType (Justification::centred);
    caption.setColour (Label::textColourId, Theme::textDim);
    addAndMakeVisible (caption);

    refreshFromModel();
}

void OutputStrip::refreshFromModel()
{
    nameLabel.setText (output.name, dontSendNotification);
    const double db = gainToSliderDb (output.level.load());
    fader.setValueDb (db, dontSendNotification);
    readout.setText (ConsoleFader::formatDb (db), dontSendNotification);
    muteButton.setToggleState (output.mute.load(), dontSendNotification);
    refreshRouting();
    chainEditor.refresh();
}

void OutputStrip::refreshRouting()
{
    const auto target = (Output::Target) output.target.load();
    String route;

    if (target == Output::Target::Main)
        route = output.status.fromFirstOccurrenceOf ("Main: ", false, false);
    else if (target == Output::Target::Device)
        route = output.device != nullptr ? output.deviceName : "! " + output.deviceName;
    else
        route = "Not routed";

    routeButton.setButtonText (route.isEmpty() ? String ("Route...") : route);
    routeButton.setTooltip (output.status);
    routeButton.setColour (TextButton::textColourOffId,
                           (target == Output::Target::None || (target == Output::Target::Device && output.device == nullptr))
                               ? Theme::mute : Theme::text);
}

void OutputStrip::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void OutputStrip::mouseUp (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onSelect != nullptr && e.getNumberOfClicks() == 1)
        onSelect();
}

void OutputStrip::showMenu()
{
    PopupMenu menu;
    menu.addItem (1, "Rename");
    menu.addItem (2, "Routing...");
    menu.addSeparator();
    menu.addItem (3, "Remove mix", engine.getOutputs().size() > 1);

    SafePointer<OutputStrip> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (menuButton), [safe] (int result)
    {
        if (safe == nullptr)
            return;

        switch (result)
        {
            case 1: safe->nameLabel.showEditor(); break;
            case 2: if (safe->onOpenRouting) safe->onOpenRouting(); break;
            case 3:
            {
                auto* eng = &safe->engine;
                auto* out = &safe->output;
                AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Remove mix",
                                              "Remove the mix \"" + out->name + "\"?", "Remove", "Cancel", nullptr,
                                              ModalCallbackFunction::create ([eng, out] (int ok) { if (ok) eng->removeOutput (out); }));
                break;
            }
            default: break;
        }
    });
}

void OutputStrip::paint (Graphics& g)
{
    paintStripPanel (g, getLocalBounds().toFloat(), Theme::output, selected);
}

void OutputStrip::resized()
{
    auto area = getLocalBounds().reduced (8, 0);
    area.removeFromTop (12);
    area.removeFromBottom (20);

    auto header = area.removeFromTop (28);
    menuButton.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 22));
    nameLabel.setBounds (header);
    area.removeFromTop (4);

    routeButton.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    chainEditor.setBounds (area.removeFromTop (6 + 5 * (PluginChainEditor::rowHeight + 2) + PluginChainEditor::rowHeight));
    area.removeFromTop (28);

    caption.setBounds (area.removeFromBottom (16));
    readout.setBounds (area.removeFromBottom (20).reduced (16, 1));
    area.removeFromBottom (4);

    auto left = area.removeFromLeft (38);
    muteButton.setBounds (left.removeFromTop (34 + ConsoleButton::labelHeight));
    left.removeFromTop (10);
    meter.setBounds (left.reduced (8, 0));

    fader.setBounds (area);
}

//==============================================================================
MicFxStrip::MicFxStrip (MixerEngine& e) : engine (e), meter (e.getMicFx().meter)
{
    icon.setIconId ("mic", Theme::input);
    icon.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (icon);

    title.setText ("Mic FX", dontSendNotification);
    title.setFont (FontOptions (14.0f, Font::bold));
    addAndMakeVisible (title);

    info.setText ("Your mic with its FX, straight to Discord / OBS. No fader: set gain on your interface.",
                  dontSendNotification);
    info.setFont (FontOptions (10.5f));
    info.setColour (Label::textColourId, Theme::textDim);
    info.setJustificationType (Justification::topLeft);
    addAndMakeVisible (info);

    routeButton.onClick = [this] { if (onOpenRouting) onOpenRouting(); };
    addAndMakeVisible (routeButton);

    addAndMakeVisible (meter);

    muteButton.setTooltip ("Mute your mic everywhere (Discord/OBS and your own monitor)  -  Cmd+Shift+M");
    muteButton.onClick = [this] { engine.setMicMuted (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    refreshRouting();
}

void MicFxStrip::updateMeters()
{
    meter.update();
    if (muteButton.getToggleState() != engine.isMicMuted())
        muteButton.setToggleState (engine.isMicMuted(), dontSendNotification);
}

void MicFxStrip::refreshRouting()
{
    auto& mic = engine.getMicFx();
    const bool ok = mic.device != nullptr;
    routeButton.setButtonText (ok ? mic.deviceName : String ("Not connected"));
    routeButton.setTooltip (mic.status);
    routeButton.setColour (TextButton::textColourOffId, ok ? Theme::text : Theme::mute);
}

void MicFxStrip::paint (Graphics& g)
{
    paintStripPanel (g, getLocalBounds().toFloat(), Theme::input, false);
}

void MicFxStrip::resized()
{
    auto area = getLocalBounds().reduced (8, 0);
    area.removeFromTop (12);
    area.removeFromBottom (20);

    auto header = area.removeFromTop (28);
    icon.setBounds (header.removeFromLeft (28));
    header.removeFromLeft (4);
    title.setBounds (header);
    area.removeFromTop (4);

    routeButton.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    info.setBounds (area.removeFromTop (64));

    auto bottom = area.removeFromBottom (54 + ConsoleButton::labelHeight);
    muteButton.setBounds (bottom.withSizeKeepingCentre (56, bottom.getHeight()));
    area.removeFromBottom (10);
    meter.setBounds (area.withSizeKeepingCentre (18, area.getHeight()));
}
