// Windows / Linux: no own virtual device. On Windows, Mic FX goes to VB-CABLE (see VirtualMicPanel).

#include "VirtualMicInstaller.h"

namespace VirtualMic
{
    State getState()                    { return State::Unsupported; }
    juce::String getInstalledVersion()  { return {}; }
    juce::String getBundledVersion()    { return {}; }
    juce::String install()              { return "The built-in virtual mic is not available on this platform yet."; }
    juce::String uninstall()            { return {}; }
    void scheduleRelaunch()             {}
}
