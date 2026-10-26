#include "FhDenoise.h"

extern "C"
{
#include "rnnoise.h"
}

namespace fh
{
namespace
{
// Simple FIFO of floats that never allocates after reserve().
struct Fifo
{
    std::vector<float> data;
    size_t start = 0, count = 0;

    void reserve (size_t n)          { data.assign (n, 0.0f); start = count = 0; }
    size_t size() const noexcept     { return count; }
    size_t capacity() const noexcept { return data.size(); }

    void push (const float* x, size_t n) noexcept
    {
        for (size_t i = 0; i < n && count < data.size(); ++i)
            data[(start + count++) % data.size()] = x[i];
    }

    void pushZeros (size_t n) noexcept
    {
        for (size_t i = 0; i < n && count < data.size(); ++i)
            data[(start + count++) % data.size()] = 0.0f;
    }

    // Copies the first n into out (must be <= size) and removes them.
    void pop (float* out, size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
            out[i] = data[(start + i) % data.size()];
        start = (start + n) % data.size();
        count -= n;
    }

    // Copies into a linear scratch without removing.
    void peek (float* out, size_t n) const noexcept
    {
        for (size_t i = 0; i < n; ++i)
            out[i] = data[(start + i) % data.size()];
    }

    void drop (size_t n) noexcept { n = std::min (n, count); start = (start + n) % data.size(); count -= n; }
    void clear() noexcept         { start = count = 0; }
};
}

struct Denoiser::Channel
{
    DenoiseState* state = nullptr;
    Fifo hostIn, in48, out48, hostOut;
    juce::LagrangeInterpolator down, up;
    std::vector<float> scratchIn, scratchOut;
    float frameIn[frameSize] {}, frameOut[frameSize] {};

    Channel()  { state = rnnoise_create (nullptr); }
    ~Channel() { if (state != nullptr) rnnoise_destroy (state); }
};

Denoiser::Denoiser() = default;
Denoiser::~Denoiser() = default;

void Denoiser::prepare (double sampleRate, int maxBlock, int numChannels)
{
    hostRate = sampleRate;
    channels.clear();

    const bool resample = std::abs (hostRate - 48000.0) > 0.5;
    const double perHost = 48000.0 / hostRate;

    // Output is held back by one frame (plus a little slack when resampling), so there is
    // always a processed sample ready. RNNoise itself adds one more frame (its overlap).
    const int bufferLatency = (int) std::ceil (frameSize / perHost) + (resample ? 8 : 0);
    const int rnnoiseDelay = (int) std::round (frameSize / perHost);
    latency = bufferLatency + rnnoiseDelay + (resample ? 4 : 0);   // + the two interpolators' own delay (measured)

    const size_t big = (size_t) (maxBlock * 4 + frameSize * 8 + 64);
    const size_t scratch = (size_t) ((double) big * std::max (1.0, perHost)) + 64;
    for (int ch = 0; ch < juce::jmax (1, numChannels); ++ch)
    {
        auto c = std::make_unique<Channel>();
        c->hostIn.reserve (big);
        c->in48.reserve (scratch);
        c->out48.reserve (scratch);
        c->hostOut.reserve (big);
        c->scratchIn.assign (scratch, 0.0f);
        c->scratchOut.assign (scratch, 0.0f);
        c->hostOut.pushZeros ((size_t) bufferLatency);
        channels.push_back (std::move (c));
    }

    dryDelay.setSize (juce::jmax (1, numChannels), latency + maxBlock + 1);
    dryDelay.clear();
    dryPos = 0;

    amount.reset (hostRate, 0.05);
    amount.setCurrentAndTargetValue (amountTarget.load());
}

void Denoiser::reset()
{
    for (auto& c : channels)
    {
        if (c->state != nullptr) rnnoise_destroy (c->state);
        c->state = rnnoise_create (nullptr);
        c->hostIn.clear(); c->in48.clear(); c->out48.clear(); c->hostOut.clear();
        c->down.reset(); c->up.reset();
    }
    dryDelay.clear();
}

void Denoiser::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const bool resample = std::abs (hostRate - 48000.0) > 0.5;
    const double hostPer48 = hostRate / 48000.0;   // host samples per 48k sample
    float vad = 0.0f;
    bool anyFrame = false;

    amount.setTargetValue (amountTarget.load());

    // Keep a delayed copy of the dry signal so the mix lines up with the processed one.
    const int dSize = dryDelay.getNumSamples();

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& c = *channels[(size_t) ch];
        auto* data = buffer.getWritePointer (ch);

        c.hostIn.push (data, (size_t) n);

        // 1. Host rate -> 48 kHz.
        if (! resample)
        {
            const size_t m = c.hostIn.size();
            c.hostIn.pop (c.scratchIn.data(), m);
            c.in48.push (c.scratchIn.data(), m);
        }
        else
        {
            const size_t avail = c.hostIn.size();
            const int canMake = (int) std::floor (((double) avail - 4.0) / hostPer48);
            if (canMake > 0)
            {
                c.hostIn.peek (c.scratchIn.data(), avail);
                const int used = c.down.process (hostPer48, c.scratchIn.data(), c.scratchOut.data(), canMake);
                c.hostIn.drop ((size_t) used);
                c.in48.push (c.scratchOut.data(), (size_t) canMake);
            }
        }

        // 2. RNNoise per 10 ms frame (it expects 16-bit sized numbers).
        while (c.in48.size() >= (size_t) frameSize)
        {
            c.in48.pop (c.frameIn, frameSize);
            for (float& v : c.frameIn) v *= 32768.0f;
            vad = std::max (vad, rnnoise_process_frame (c.state, c.frameOut, c.frameIn));
            anyFrame = true;
            for (float& v : c.frameOut) v *= (1.0f / 32768.0f);
            c.out48.push (c.frameOut, frameSize);
        }

        // 3. 48 kHz -> host rate.
        if (! resample)
        {
            const size_t m = c.out48.size();
            c.out48.pop (c.scratchOut.data(), m);
            c.hostOut.push (c.scratchOut.data(), m);
        }
        else
        {
            const double per = 1.0 / hostPer48;   // 48k samples per host sample
            const size_t avail = c.out48.size();
            const int canMake = (int) std::floor (((double) avail - 4.0) / per);
            if (canMake > 0)
            {
                c.out48.peek (c.scratchIn.data(), avail);
                const int used = c.up.process (per, c.scratchIn.data(), c.scratchOut.data(), canMake);
                c.out48.drop ((size_t) used);
                c.hostOut.push (c.scratchOut.data(), (size_t) canMake);
            }
        }

        // 4. Out (silence if we ever ran short, which the start-up delay should prevent).
        const size_t ready = std::min ((size_t) n, c.hostOut.size());
        c.hostOut.pop (c.scratchOut.data(), ready);
        for (size_t i = ready; i < (size_t) n; ++i)
            c.scratchOut[i] = 0.0f;

        // Mix with the delayed dry signal.
        auto* delayLine = dryDelay.getWritePointer (ch);
        int pos = dryPos;
        auto mix = amount;   // same ramp for every channel
        for (int i = 0; i < n; ++i)
        {
            const int readPos = (pos - latency + dSize) % dSize;
            delayLine[pos] = data[i];
            const float a = mix.getNextValue();
            data[i] = c.scratchOut[(size_t) i] * a + delayLine[readPos] * (1.0f - a);
            pos = (pos + 1) % dSize;
        }
    }

    amount.skip (n);
    dryPos = (dryPos + n) % dSize;

    if (anyFrame)
        voiceProbability.store (vad);
}
}
