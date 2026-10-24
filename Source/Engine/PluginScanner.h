#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Out-of-process plugin scanning.

    Every plugin is probed in a separate helper process (Faderhouse started with a special
    command line). If a plugin crashes or hangs while being scanned, only that helper dies:
    the plugin is marked as failed and the scan simply continues with the next one.
    Adapted from the JUCE AudioPluginHost.
*/
namespace PluginScanner
{
    /** Installs the out-of-process scanner on the list. */
    void install (juce::KnownPluginList& list);

    /** Call first thing in JUCEApplication::initialise. Returns true if this process is a
        scanner helper; the app must then not create any windows or audio devices. */
    bool runAsScannerIfRequested (const juce::String& commandLine);
}
