#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Faderhouse's own voice plugins (EQ, Compressor, Limiter, Gate, Denoise, Voice), built into
    the app as a plugin format called "Faderhouse". They need no scanning and are saved in a
    session like any other plugin. The same code is also built as VST3s (see Plugins/).
*/
namespace BuiltinPlugins
{
    /** Adds the "Faderhouse" format to a format manager. */
    void addFormat (juce::AudioPluginFormatManager&);

    /** Descriptions of all built-in plugins, in menu order. */
    juce::Array<juce::PluginDescription> getTypes();

    /** The one we put on the mic of a new session. */
    juce::PluginDescription getVoiceStrip();
}
