#pragma once

// Noise suppression with RNNoise (Xiph, BSD-3): a small neural network trained to keep
// speech and remove steady and non-steady noise (fans, keyboards, traffic...).
// RNNoise works on 48 kHz mono frames of 10 ms, so other sample rates are converted.

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace fh
{
class Denoiser
{
public:
    Denoiser();
    ~Denoiser();

    void prepare (double sampleRate, int maxBlock, int numChannels);
    void reset();

    /** 0 = off (dry), 1 = full noise suppression. */
    void setAmount (float value) noexcept { amountTarget.store (juce::jlimit (0.0f, 1.0f, value)); }

    void process (juce::AudioBuffer<float>& buffer);

    int getLatencySamples() const noexcept { return latency; }

    /** RNNoise's "voice present" probability of the last frame (0..1), for a meter. */
    std::atomic<float> voiceProbability { 0.0f };

    static constexpr int frameSize = 480;   // 10 ms at 48 kHz

private:
    struct Channel;
    std::vector<std::unique_ptr<Channel>> channels;

    double hostRate = 48000.0;
    int latency = 0;
    std::atomic<float> amountTarget { 1.0f };
    juce::LinearSmoothedValue<float> amount { 1.0f };
    juce::AudioBuffer<float> dryDelay;
    int dryPos = 0;
};
}
