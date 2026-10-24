#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

/** Peak hold written by the audio thread, read-and-reset by the UI timer. */
struct LevelMeter
{
    void push (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if (buffer.getNumChannels() == 0 || numSamples <= 0)
            return;

        const float pl = buffer.getMagnitude (0, 0, numSamples);
        const float pr = buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, numSamples) : pl;

        if (pl > left.load (std::memory_order_relaxed))  left.store (pl, std::memory_order_relaxed);
        if (pr > right.load (std::memory_order_relaxed)) right.store (pr, std::memory_order_relaxed);
    }

    void clear() noexcept { left.store (0.0f); right.store (0.0f); }

    std::pair<float, float> takePeaks() noexcept
    {
        return { left.exchange (0.0f), right.exchange (0.0f) };
    }

    std::atomic<float> left { 0.0f }, right { 0.0f };
};
