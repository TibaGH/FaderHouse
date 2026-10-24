// Windows / Linux: no own virtual device yet (phase 3). On Windows use VB-CABLE meanwhile.

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
