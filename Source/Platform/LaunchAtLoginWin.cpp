#include "LaunchAtLogin.h"

// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run - per user, no admin needed.
namespace
{
const juce::String runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Faderhouse";

juce::String command()
{
    return "\"" + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName()
         + "\" " + LaunchAtLogin::startupFlag;
}
}

namespace LaunchAtLogin
{
bool isSupported() { return true; }

bool isEnabled()
{
    return juce::WindowsRegistry::valueExists (runKey);
}

juce::String setEnabled (bool enabled)
{
    if (enabled)
        return juce::WindowsRegistry::setValue (runKey, command()) ? juce::String() : juce::String ("Couldn't write to the Windows registry.");

    juce::WindowsRegistry::deleteValue (runKey);
    return {};
}

}
