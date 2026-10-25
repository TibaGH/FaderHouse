#include "LaunchAtLogin.h"

namespace LaunchAtLogin
{
bool isSupported()              { return false; }
bool isEnabled()                { return false; }
juce::String setEnabled (bool)  { return "Not available on this platform."; }
}
