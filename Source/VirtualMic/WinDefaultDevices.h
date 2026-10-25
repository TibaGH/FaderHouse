#pragma once

#include <juce_core/juce_core.h>

/** Windows only: default sound devices around VB-CABLE (the Mic FX device on Windows). */
namespace WinDefaultDevices
{
    /** Moves the default output off the virtual cable, and (if asked) makes "CABLE Output"
        the default microphone. Returns what was changed, for the log / a notice. */
    juce::String fixDefaults (bool makeCableTheDefaultMic);

    /** True when VB-CABLE's "CABLE Output" recording device exists. */
    bool isCableInstalled();
}
