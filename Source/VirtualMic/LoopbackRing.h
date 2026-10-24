#pragma once

// Ring buffer used inside the "Mic FX" driver.
//
// Both sides run on the virtual device's own clock, so positions are simply the
// device sample time: Faderhouse writes its mix at output time T, and Discord/OBS
// read input time T a little later. No drift, no resampling needed here.
//
// Frames that were never written (Faderhouse not running, or a gap after a pause)
// read as silence instead of stale audio.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

class LoopbackRing
{
public:
    static constexpr int64_t kFrames = 1 << 16;   // ~1.4 s at 48 kHz, power of two
    static constexpr int kChannels = 2;

    LoopbackRing() : data ((size_t) (kFrames * kChannels), 0.0f) {}

    /** Writes interleaved stereo float frames at device sample time `start`. */
    void write (int64_t start, const float* frames, int64_t numFrames) noexcept
    {
        if (numFrames <= 0)
            return;

        const int64_t prev = writtenUpTo.load (std::memory_order_relaxed);

        // Silence any gap since the last write so old audio can't reappear.
        if (prev >= 0 && start > prev)
            clearRange (prev, std::min (start - prev, kFrames));
        else if (prev < 0)
            std::fill (data.begin(), data.end(), 0.0f);

        for (int64_t i = 0; i < numFrames; ++i)
        {
            const auto idx = (size_t) (((start + i) & (kFrames - 1)) * kChannels);
            data[idx]     = frames[i * kChannels];
            data[idx + 1] = frames[i * kChannels + 1];
        }

        const int64_t end = start + numFrames;
        if (end > prev)
            writtenUpTo.store (end, std::memory_order_release);
        else if (end < prev - kFrames)          // device clock restarted from zero
            writtenUpTo.store (end, std::memory_order_release);
    }

    /** Reads interleaved stereo float frames at device sample time `start`. */
    void read (int64_t start, float* frames, int64_t numFrames) const noexcept
    {
        const int64_t end = writtenUpTo.load (std::memory_order_acquire);
        const int64_t oldest = end - (kFrames - 4096);   // keep clear of the write head's wrap

        for (int64_t i = 0; i < numFrames; ++i)
        {
            const int64_t t = start + i;

            if (t < end && t >= oldest && end >= 0)
            {
                const auto idx = (size_t) ((t & (kFrames - 1)) * kChannels);
                frames[i * kChannels]     = data[idx];
                frames[i * kChannels + 1] = data[idx + 1];
            }
            else
            {
                frames[i * kChannels]     = 0.0f;
                frames[i * kChannels + 1] = 0.0f;
            }
        }
    }

    void reset() noexcept { writtenUpTo.store (-1); }

private:
    void clearRange (int64_t start, int64_t numFrames) noexcept
    {
        for (int64_t i = 0; i < numFrames; ++i)
        {
            const auto idx = (size_t) (((start + i) & (kFrames - 1)) * kChannels);
            data[idx] = data[idx + 1] = 0.0f;
        }
    }

    std::vector<float> data;
    std::atomic<int64_t> writtenUpTo { -1 };
};
