#include "VirtualMicPanel.h"
#include "FaderhouseLookAndFeel.h"
#include "../Engine/MixerEngine.h"

using namespace juce;

VirtualMicPanel::VirtualMicPanel (MixerEngine& e) : engine (e)
{
    title.setText ("Virtual microphone \"Mic FX\"", dontSendNotification);
    title.setFont (FontOptions (14.0f, Font::bold));
    addAndMakeVisible (title);

    status.setColour (Label::textColourId, Theme::textDim);
    status.setJustificationType (Justification::topLeft);
    addAndMakeVisible (status);

    installButton.onClick = [this]
    {
        if (VirtualMic::getState() == VirtualMic::State::Unsupported)
            URL (VirtualMic::windowsCableUrl).launchInDefaultBrowser();
        else
            runInstall (engine);
    };
    removeButton.onClick  = [this] { runUninstall (engine); };
    addAndMakeVisible (installButton);
    addAndMakeVisible (removeButton);

    refresh();
}

void VirtualMicPanel::refresh()
{
    using VirtualMic::State;
    const auto state = VirtualMic::getState();

    String text;
    String buttonText = "Install";
    bool canInstall = true, canRemove = false;

    switch (state)
    {
        case State::Unsupported:
        {
           #if JUCE_WINDOWS
            // Windows only loads audio drivers signed by Microsoft, so Faderhouse uses the free
            // VB-CABLE driver as its Mic FX device.
            String type, name;
            if (engine.findVirtualMicDevice (type, name))
            {
                text = "Using \"" + name + "\" (VB-CABLE by VB-Audio, donationware: www.vb-cable.com) as Mic FX. "
                       "\"CABLE Output\" is your Windows default microphone; Discord / OBS on \"Default\" use it.";
                buttonText = "VB-CABLE website";
            }
            else
            {
                text = "Mic FX on Windows uses the free VB-CABLE driver (normally installed with Faderhouse). "
                       "Install it, restart your PC, then choose \"CABLE Output\" as microphone in Discord / OBS.";
                buttonText = "Get VB-CABLE";
            }
           #else
            text = "Not available on this platform yet. Use a virtual cable for now.";
            canInstall = false;
           #endif
            break;
        }
        case State::NotBundled:
            text = "The driver is missing from the app bundle. Rebuild Faderhouse with build.sh.";
            canInstall = false;
            break;
        case State::NotInstalled:
            text = "Not installed. Installing needs your admin password once; Faderhouse restarts afterwards.";
            break;
        case State::UpdateAvailable:
            text = "Installed (" + VirtualMic::getInstalledVersion() + "), this Faderhouse has version "
                 + VirtualMic::getBundledVersion() + ".";
            buttonText = "Update";
            canRemove = true;
            break;
        case State::Installed:
            text = "Installed (" + VirtualMic::getInstalledVersion() + "). Choose \"Mic FX\" as microphone in Discord / OBS.";
            buttonText = "Reinstall";
            canRemove = true;
            break;
    }

    status.setText (text, dontSendNotification);
    installButton.setButtonText (buttonText);
    installButton.setEnabled (canInstall);
    removeButton.setEnabled (canRemove);
}

void VirtualMicPanel::paint (Graphics& g)
{
    g.setColour (Theme::panelRaised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void VirtualMicPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    auto buttons = area.removeFromRight (230);
    title.setBounds (area.removeFromTop (20));
    status.setBounds (area);

    buttons = buttons.withSizeKeepingCentre (buttons.getWidth(), 26);
    removeButton.setVisible (VirtualMic::getState() != VirtualMic::State::Unsupported);
    if (removeButton.isVisible())
    {
        removeButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (8);
    }
    installButton.setBounds (buttons);
}

void VirtualMicPanel::runInstall (MixerEngine& engine)
{
    engine.saveSession();
    const auto error = VirtualMic::install();

    if (error == "Cancelled.")
        return;

    if (error.isNotEmpty())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Installation failed", error);
        return;
    }

    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Mic FX installed",
        "The audio system was restarted to load the new device, so Faderhouse restarts now too.\n\n"
        "Then pick \"Mic FX\" as input device in Discord / OBS.",
        "Restart Faderhouse", nullptr,
        ModalCallbackFunction::create ([] (int)
        {
            VirtualMic::scheduleRelaunch();
            JUCEApplication::getInstance()->systemRequestedQuit();
        }));
}

void VirtualMicPanel::runUninstall (MixerEngine& engine)
{
    engine.saveSession();
    const auto error = VirtualMic::uninstall();

    if (error == "Cancelled.")
        return;

    if (error.isNotEmpty())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Removal failed", error);
        return;
    }

    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Mic FX removed",
        "The audio system was restarted, so Faderhouse restarts now too.", "Restart Faderhouse", nullptr,
        ModalCallbackFunction::create ([] (int)
        {
            VirtualMic::scheduleRelaunch();
            JUCEApplication::getInstance()->systemRequestedQuit();
        }));
}
