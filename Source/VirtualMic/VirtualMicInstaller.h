#pragma once

#include <juce_core/juce_core.h>

/** Installs / removes the bundled "Mic FX" virtual device. macOS only for now. */
namespace VirtualMic
{
    inline constexpr const char* deviceName = "Mic FX";

    enum class State
    {
        Unsupported,       // platform without our own driver (yet)
        NotBundled,        // driver missing from the app bundle (build problem)
        NotInstalled,
        UpdateAvailable,   // installed version differs from the one in the app
        Installed
    };

    State getState();
    juce::String getInstalledVersion();
    juce::String getBundledVersion();

    /** Asks for the admin password, copies the driver into place and restarts the audio system.
        Blocks until done. Returns an error message, or an empty string on success. */
    juce::String install();
    juce::String uninstall();

    /** Starts a fresh copy of Faderhouse after a short delay (call, then quit). */
    void scheduleRelaunch();
}
