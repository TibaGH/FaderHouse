#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

/**
    Lock-free single-producer / single-consumer stereo FIFO that connects two
    audio devices running on independent clocks (e.g. an app tap and your audio
    interface, or the interface and a virtual mic device).

    The consumer side resamples with a high-quality windowed-sinc interpolator
    and nudges the ratio slightly (max +/-0.5%) to keep the FIFO around its target
    fill level. That absorbs clock drift without clicks or audible pitch change.
*/
class ClockBridge
{
public:
    explicit ClockBridge (int capacityFrames = 1 << 15)
        : fifo (capacityFrames),
          storage (2, capacityFrames),
          scratch (2, capacityFrames)
    {
        storage.clear();
        scratch.clear();
    }

    //==========================================================================
    // Configuration (any thread)
    void setProducerRate (double r) noexcept   { if (r > 0) producerRate.store (r); }
    void setConsumerRate (double r) noexcept   { if (r > 0) consumerRate.store (r); }
    void setTargetFill (int frames) noexcept   { targetFill.store (juce::jlimit (64, fifo.getTotalSize() / 4, frames)); }
    void requestReset() noexcept               { resetRequested.store (true); }

    int getFill() const noexcept               { return fifo.getNumReady(); }
    int getTargetFill() const noexcept         { return targetFill.load(); }
    int getUnderruns() const noexcept          { return underruns.load(); }
    int getOverruns() const noexcept           { return overruns.load(); }

    //==========================================================================
    // Producer thread
    void pushInterleaved (const float* data, int numChannels, int numFrames) noexcept
    {
        if (data == nullptr || numChannels <= 0 || numFrames <= 0)
            return;

        int s1, n1, s2, n2;
        fifo.prepareToWrite (numFrames, s1, n1, s2, n2);

        auto* l = storage.getWritePointer (0);
        auto* r = storage.getWritePointer (1);
        const int rOff = numChannels > 1 ? 1 : 0;

        for (int i = 0; i < n1; ++i)
        {
            l[s1 + i] = data[i * numChannels];
            r[s1 + i] = data[i * numChannels + rOff];
        }

        for (int i = 0; i < n2; ++i)
        {
            const int f = n1 + i;
            l[s2 + i] = data[f * numChannels];
            r[s2 + i] = data[f * numChannels + rOff];
        }

        fifo.finishedWrite (n1 + n2);

        if (n1 + n2 < numFrames)
            overruns.fetch_add (1);
    }

    void push (const float* left, const float* right, int numFrames) noexcept
    {
        if (left == nullptr || numFrames <= 0)
            return;

        if (right == nullptr)
            right = left;

        int s1, n1, s2, n2;
        fifo.prepareToWrite (numFrames, s1, n1, s2, n2);

        if (n1 > 0)
        {
            storage.copyFrom (0, s1, left, n1);
            storage.copyFrom (1, s1, right, n1);
        }

        if (n2 > 0)
        {
            storage.copyFrom (0, s2, left + n1, n2);
            storage.copyFrom (1, s2, right + n1, n2);
        }

        fifo.finishedWrite (n1 + n2);

        if (n1 + n2 < numFrames)
            overruns.fetch_add (1);
    }

    //==========================================================================
    // Consumer thread. Always writes numFrames samples (silence when starved).
    void pull (float* left, float* right, int numFrames) noexcept
    {
        if (numFrames <= 0)
            return;

        const int target = targetFill.load();

        if (resetRequested.exchange (false))
        {
            fifo.finishedRead (fifo.getNumReady());
            primed = false;
            interpL.reset();
            interpR.reset();
        }

        int ready = fifo.getNumReady();

        if (! primed)
        {
            if (ready < target)
            {
                silence (left, right, numFrames);
                return;
            }

            primed = true;
            fillAverage = (double) ready;
            interpL.reset();
            interpR.reset();
        }

        // Way too much buffered (e.g. we were stopped while the producer kept going): drop the excess.
        if (ready > target * 4)
        {
            fifo.finishedRead (ready - target);
            ready = target;
            fillAverage = (double) target;
        }

        fillAverage += 0.01 * ((double) ready - fillAverage);

        const double pr = producerRate.load();
        const double cr = consumerRate.load();
        const double base = (pr > 0 && cr > 0) ? pr / cr : 1.0;
        const double error = (fillAverage - (double) target) / (double) target;
        const double ratio = base * (1.0 + juce::jlimit (-0.005, 0.005, error * 0.004));

        const int needed = (int) std::ceil (numFrames * ratio) + 4;

        if (needed > scratch.getNumSamples() || ready < needed)
        {
            underruns.fetch_add (1);
            primed = false;
            silence (left, right, numFrames);
            return;
        }

        int s1, n1, s2, n2;
        fifo.prepareToRead (needed, s1, n1, s2, n2);

        if (n1 > 0)
        {
            scratch.copyFrom (0, 0, storage, 0, s1, n1);
            scratch.copyFrom (1, 0, storage, 1, s1, n1);
        }

        if (n2 > 0)
        {
            scratch.copyFrom (0, n1, storage, 0, s2, n2);
            scratch.copyFrom (1, n1, storage, 1, s2, n2);
        }

        const int used = interpL.process (ratio, scratch.getReadPointer (0), left, numFrames);
        if (right != nullptr)
            interpR.process (ratio, scratch.getReadPointer (1), right, numFrames);

        fifo.finishedRead (juce::jmin (used, ready));
    }

private:
    static void silence (float* l, float* r, int n) noexcept
    {
        if (l != nullptr) juce::FloatVectorOperations::clear (l, n);
        if (r != nullptr) juce::FloatVectorOperations::clear (r, n);
    }

    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> storage, scratch;
    juce::WindowedSincInterpolator interpL, interpR;

    std::atomic<double> producerRate { 48000.0 }, consumerRate { 48000.0 };
    std::atomic<int> targetFill { 1024 };
    std::atomic<bool> resetRequested { false };
    std::atomic<int> underruns { 0 }, overruns { 0 };

    // consumer-thread state
    bool primed = false;
    double fillAverage = 0.0;

    JUCE_DECLARE_NON_COPYABLE (ClockBridge)
};
