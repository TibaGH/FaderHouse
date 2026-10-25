#pragma once

#include <juce_core/juce_core.h>

/** "Open Faderhouse when you log in" (macOS: Login Items via SMAppService; Windows: the Run key). */
namespace LaunchAtLogin
{
    bool isSupported();
    bool isEnabled();

    /** Returns an error / note for the user, or {} when it simply worked. */
    juce::String setEnabled (bool enabled);

    /** Command-line flag Faderhouse gets when Windows starts it at sign-in (it starts minimised). */
    inline constexpr const char* startupFlag = "--startup";
}
