#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "ClockBridge.h"

/**
    A second (or third...) output device next to the main audio interface,
    e.g. the virtual "BlackHole 2ch" device that Discord/OBS use as microphone.

    The main mixer callback pushes audio in; this device's own callback pulls
    it out through a ClockBridge, so both devices may run on different clocks.
*/
class ExtraOutputDevice final : private juce::AudioIODeviceCallback
{
public:
    ExtraOutputDevice() = default;
    ~ExtraOutputDevice() override { close(); }

    /** Opens deviceName from the given device type (e.g. "CoreAudio", "Windows Audio"). */
    juce::String open (juce::AudioDeviceManager& manager, const juce::String& typeName,
                       const juce::String& deviceName, double preferredSampleRate, int mainBlockSize);
    void close();

    bool isOpen() const noexcept                { return device != nullptr && device->isPlaying(); }
    juce::String getDescription() const;

    /** Called from the main audio thread. */
    void push (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        bridge.push (buffer.getReadPointer (0), buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0), numSamples);
    }

    void setMainFormat (double sampleRate, int blockSize);

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    void updateTarget();

    std::unique_ptr<juce::AudioIODevice> device;
    ClockBridge bridge;
    std::vector<float> scratchR;
    int mainBlock = 512;
};
