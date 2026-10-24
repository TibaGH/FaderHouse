#pragma once

// Windows helpers shared by the per-app capture (AppCaptureWin.cpp) and the app icons
// (AppIconsWin.cpp): who is running, what their exe is called and how they are related.

#include <juce_core/juce_core.h>
#include <map>

namespace WinProcesses
{
    struct Process
    {
        unsigned long pid = 0;
        unsigned long parentPid = 0;
        juce::String exe;          // "Discord.exe"
    };

    /** All running processes, keyed by pid. */
    std::map<unsigned long, Process> snapshot();

    /** Full path of a running process's exe, or {} when it can't be opened. */
    juce::String imagePath (unsigned long pid);

    /** "FileDescription" from the exe's version info ("Discord"), or {} (cached). */
    juce::String fileDescription (const juce::String& exePath);

    /** True for exes inside the Windows folder (services, shell, audio engine...). */
    bool isSystemPath (const juce::String& exePath);

    /** How Faderhouse identifies a process. */
    struct Identity
    {
        juce::String appId;        // "exe:Discord.exe", or "exe:Teams.exe.helper.msedgewebview2.exe"
        unsigned long rootPid = 0; // top of the chain of same-named processes: capture this tree
    };

    Identity identify (unsigned long pid, const std::map<unsigned long, Process>& processes);

    /** Path of the exe behind an app id: from a running process, else the "App Paths" registry. */
    juce::String findExePath (const juce::String& appId);

    /** "exe:Discord.exe" / "exe:Teams.exe.helper.x.exe" -> "Discord.exe" / "Teams.exe". */
    juce::String exeNameFromAppId (const juce::String& appId);
}
