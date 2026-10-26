#pragma once

// Faderhouse voice DSP: everything the plugins (and later Faderhouse's built-in chain) use.
// Header-only, no allocations on the audio thread after prepare().

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <vector>

namespace fh
{
inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept    { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** One-pole smoothing coefficient for a time constant in ms. */
inline float timeCoef (double sampleRate, float ms) noexcept
{
    return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
}

//==============================================================================
// Biquad (RBJ cookbook), transposed direct form II, up to 2 channels.
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs peak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        return norm (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q), s = 2.0 * std::sqrt (A) * alpha;
        return norm (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
                     (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }

    static BiquadCoeffs highShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q), s = 2.0 * std::sqrt (A) * alpha;
        return norm (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
                     (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }

    static BiquadCoeffs highPass (double fs, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    static BiquadCoeffs lowPass (double fs, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    /** Magnitude response in dB at `freq`. */
    double magnitudeDb (double freq, double fs) const
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (num / den)));
    }

private:
    static BiquadCoeffs norm (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
};

struct Biquad
{
    BiquadCoeffs c;
    double s1[2] {}, s2[2] {};

    void reset() noexcept { s1[0] = s1[1] = s2[0] = s2[1] = 0; }

    inline float process (int ch, float xIn) noexcept
    {
        const double x = xIn;
        const double y = c.b0 * x + s1[ch];
        s1[ch] = c.b1 * x - c.a1 * y + s2[ch];
        s2[ch] = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

//==============================================================================
/** Parametric EQ: high-pass, 5 bands (bell / low shelf / high shelf), low-pass. */
class Equaliser
{
public:
    enum class BandType { Bell = 0, LowShelf, HighShelf };

    struct Band
    {
        bool enabled = true;
        BandType type = BandType::Bell;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
    };

    struct Cut
    {
        bool enabled = false;
        float freq = 80.0f;
        int slope = 12;   // 12 or 24 dB/oct
    };

    static constexpr int numBands = 5;

    struct Settings
    {
        Cut highPass { false, 80.0f, 12 }, lowPass { false, 16000.0f, 12 };
        std::array<Band, numBands> bands;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        current = target;
        rebuild (current);
        reset();
    }

    void reset()
    {
        for (auto& f : filters)
            f.reset();
    }

    /** Message or audio thread; picked up at the next block (smoothly). */
    void setSettings (const Settings& s) { target = s; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        for (int start = 0; start < n; start += chunk)
        {
            const int len = juce::jmin (chunk, n - start);
            stepTowardsTarget();

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = buffer.getWritePointer (ch, start);
                for (int i = 0; i < len; ++i)
                {
                    float x = d[i];
                    for (int f = 0; f < numActive; ++f)
                        x = filters[(size_t) active[(size_t) f]].process (ch, x);
                    d[i] = x;
                }
            }
        }
    }

    /** Total response in dB of a settings set (for the curve display). */
    static double responseDb (const Settings& s, double freq, double fs)
    {
        double db = 0;
        for (auto& c : makeCoeffs (s, fs))
            db += c.magnitudeDb (freq, fs);
        return db;
    }

    /** Response of one band alone (for its coloured area). */
    static double bandResponseDb (const Band& b, double freq, double fs)
    {
        return b.enabled ? bandCoeffs (b, fs).magnitudeDb (freq, fs) : 0.0;
    }

private:
    static constexpr int chunk = 32;
    static constexpr int maxFilters = 2 + 2 + numBands;

    static BiquadCoeffs bandCoeffs (const Band& b, double fs)
    {
        const double f = juce::jlimit (10.0, fs * 0.45, (double) b.freq);
        const double q = juce::jlimit (0.1, 18.0, (double) b.q);
        switch (b.type)
        {
            case BandType::LowShelf:  return BiquadCoeffs::lowShelf (fs, f, q, b.gainDb);
            case BandType::HighShelf: return BiquadCoeffs::highShelf (fs, f, q, b.gainDb);
            case BandType::Bell:
            default:                  return BiquadCoeffs::peak (fs, f, q, b.gainDb);
        }
    }

    struct CoeffList
    {
        std::array<BiquadCoeffs, 2 + 2 + numBands> items;
        int count = 0;
        void push_back (const BiquadCoeffs& c) noexcept { if (count < (int) items.size()) items[(size_t) count++] = c; }
        const BiquadCoeffs* begin() const noexcept { return items.data(); }
        const BiquadCoeffs* end() const noexcept   { return items.data() + count; }
        size_t size() const noexcept               { return (size_t) count; }
        const BiquadCoeffs& operator[] (size_t i) const noexcept { return items[i]; }
    };

    // Fixed-size list: safe on the audio thread (no allocation).
    static CoeffList makeCoeffs (const Settings& s, double fs)
    {
        CoeffList list;
        auto addCut = [&] (const Cut& c, bool high)
        {
            if (! c.enabled)
                return;
            const double f = juce::jlimit (10.0, fs * 0.45, (double) c.freq);
            if (c.slope >= 24)
            {
                // 4th order Butterworth = two biquads with these Qs.
                list.push_back (high ? BiquadCoeffs::highPass (fs, f, 0.5411961) : BiquadCoeffs::lowPass (fs, f, 0.5411961));
                list.push_back (high ? BiquadCoeffs::highPass (fs, f, 1.3065630) : BiquadCoeffs::lowPass (fs, f, 1.3065630));
            }
            else
            {
                list.push_back (high ? BiquadCoeffs::highPass (fs, f, 0.7071068) : BiquadCoeffs::lowPass (fs, f, 0.7071068));
            }
        };

        addCut (s.highPass, true);
        for (auto& b : s.bands)
            if (b.enabled && std::abs (b.gainDb) > 0.01f)
                list.push_back (bandCoeffs (b, fs));
        addCut (s.lowPass, false);
        return list;
    }

    // Moves the running settings a step towards the target (no zipper noise while dragging).
    void stepTowardsTarget()
    {
        bool changed = false;
        auto glide = [&changed] (float& v, float t, float factor, bool logScale)
        {
            if (v == t)
                return;
            float next;
            if (logScale)
                next = std::exp (std::log (v) + (std::log (t) - std::log (v)) * factor);
            else
                next = v + (t - v) * factor;
            if (std::abs (next - t) < (logScale ? t * 0.0005f : 0.005f))
                next = t;
            v = next;
            changed = true;
        };

        constexpr float k = 0.25f;   // per 32-sample chunk: ~4 ms to settle mostly
        auto cut = [&] (Cut& c, const Cut& t)
        {
            if (c.enabled != t.enabled || c.slope != t.slope) { c = t; changed = true; }
            glide (c.freq, t.freq, k, true);
        };
        cut (current.highPass, target.highPass);
        cut (current.lowPass, target.lowPass);

        for (size_t i = 0; i < current.bands.size(); ++i)
        {
            auto& b = current.bands[i];
            const auto& t = target.bands[i];
            if (b.enabled != t.enabled || b.type != t.type) { b.enabled = t.enabled; b.type = t.type; changed = true; }
            glide (b.freq, t.freq, k, true);
            glide (b.gainDb, t.gainDb, k, false);
            glide (b.q, t.q, k, true);
        }

        if (changed)
            rebuild (current);
    }

    void rebuild (const Settings& s)
    {
        const auto list = makeCoeffs (s, fs);
        const int count = juce::jmin ((int) list.size(), maxFilters);

        // Keep each filter's state when only its coefficients move.
        for (int i = 0; i < count; ++i)
        {
            filters[(size_t) i].c = list[(size_t) i];
            active[(size_t) i] = i;
        }

        if (count != numActive)
            for (int i = numActive; i < count; ++i)
                filters[(size_t) i].reset();

        numActive = count;
    }

    double fs = 48000.0;
    Settings target, current;
    std::array<Biquad, maxFilters> filters;
    std::array<int, maxFilters> active {};
    int numActive = 0;
};

//==============================================================================
/** Feed-forward compressor with two easy modes.
      Smart:   one "Amount" knob sets threshold + ratio; fixed attack, auto release,
               make-up gain from the average gain reduction while you talk.
      Leveler: rides your voice towards a target level (like an automatic fader),
               with a fast safety compressor for peaks. */
class VoiceCompressor
{
public:
    enum class Mode { Smart = 0, Leveler };

    struct Settings
    {
        Mode mode = Mode::Smart;
        float amount = 0.4f;        // Smart: 0..1
        float targetDb = -20.0f;    // Leveler: target RMS level
        float strength = 0.7f;      // Leveler: 0..1
        float outputDb = 0.0f;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        attackCoef  = timeCoef (fs, 8.0f);
        releaseFast = timeCoef (fs, 90.0f);
        releaseSlow = timeCoef (fs, 450.0f);
        rmsCoef     = timeCoef (fs, 300.0f);
        speechCoef  = timeCoef (fs, 50.0f);
        makeupCoef  = timeCoef (fs, 1500.0f);
        riseCoef    = timeCoef (fs, 1500.0f);
        fallCoef    = timeCoef (fs, 350.0f);
        outSmooth.reset (fs, 0.05);
        outSmooth.setCurrentAndTargetValue (dbToGain (settings.outputDb));
        detHp = {};
        detHp.c = BiquadCoeffs::highPass (fs, 100.0, 0.7071);
        reset();
    }

    void reset()
    {
        envDb = -120.0f; grDb = 0.0f; slowGrDb = 0.0f; makeupDb = 0.0f;
        rms = 0.0f; speechLevel = 0.0f; levelGainDb = 0.0f;
        detHp.reset();
    }

    void setSettings (const Settings& s) { settings = s; outSmooth.setTargetValue (dbToGain (s.outputDb)); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        const auto s = settings;

        // Smart mode curve.
        const float a = juce::jlimit (0.0f, 1.0f, s.amount);
        const float threshold = -8.0f - 32.0f * a;
        const float ratio = 1.0f + 3.0f * std::sqrt (a);     // 1:1 .. 4:1
        const float knee = 8.0f;

        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f, energy = 0.0f, mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getSample (ch, i);
                peak = std::max (peak, std::abs (x));
                energy += x * x;
                mono += x;
            }
            energy /= (float) juce::jmax (1, numCh);
            mono /= (float) juce::jmax (1, numCh);

            // Level used to decide "you are talking" (ignores rumble below 100 Hz).
            const float hp = detHp.process (0, mono);
            speechLevel = speechCoef * speechLevel + (1.0f - speechCoef) * hp * hp;
            const bool talking = gainToDb (std::sqrt (speechLevel)) > -48.0f;

            float gainDb = 0.0f;

            if (s.mode == Mode::Smart)
            {
                gainDb = -computeGr (peak, threshold, ratio, knee, a > 0.001f);

                // Make-up: give back the average reduction while talking, never in silences.
                if (talking)
                    makeupDb = makeupCoef * makeupDb + (1.0f - makeupCoef) * (-gainDb);
                gainDb += juce::jlimit (0.0f, 18.0f, makeupDb * 0.9f);
            }
            else
            {
                // Leveler: slow automatic fader towards the target, frozen when you're quiet.
                rms = rmsCoef * rms + (1.0f - rmsCoef) * energy;
                if (talking)
                {
                    const float wanted = juce::jlimit (-12.0f, 18.0f, (s.targetDb - gainToDb (std::sqrt (rms)))) * juce::jlimit (0.0f, 1.0f, s.strength);
                    const float coef = wanted > levelGainDb ? riseCoef : fallCoef;
                    levelGainDb = coef * levelGainDb + (1.0f - coef) * wanted;
                }

                // Safety compressor on the levelled signal for peaks.
                const float levelled = peak * dbToGain (levelGainDb);
                const float safety = computeGr (levelled, s.targetDb + 8.0f, 4.0f, 6.0f, true);
                gainDb = levelGainDb - safety;
            }

            maxGr = std::max (maxGr, -std::min (0.0f, gainDb));
            const float g = dbToGain (gainDb) * outSmooth.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * g);

            lastGainDb = gainDb;
        }

        meterGainDb.store (lastGainDb);
        meterReductionDb.store (maxGr);
    }

    /** For the UI: the gain applied right now (positive when the leveler boosts). */
    std::atomic<float> meterGainDb { 0.0f }, meterReductionDb { 0.0f };

private:
    // Gain reduction in dB (positive) from a detector with attack / program-dependent release.
    float computeGr (float peak, float threshold, float ratio, float knee, bool active)
    {
        const float inDb = gainToDb (peak);
        const float coef = inDb > envDb ? attackCoef : releaseFast;
        envDb = coef * envDb + (1.0f - coef) * inDb;

        float target = 0.0f;
        if (active && ratio > 1.0f)
        {
            const float over = envDb - threshold;
            if (2.0f * over < -knee)        target = 0.0f;
            else if (2.0f * over > knee)    target = over * (1.0f - 1.0f / ratio);
            else                            target = (1.0f - 1.0f / ratio) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        }

        // Auto release: long reductions recover slowly, short peaks recover fast.
        slowGrDb = (target > slowGrDb ? 0.0f : releaseSlow) * slowGrDb + (target > slowGrDb ? 1.0f : 1.0f - releaseSlow) * target;
        grDb = std::max (target, slowGrDb * 0.6f);
        return grDb;
    }

    double fs = 48000.0;
    Settings settings;
    float attackCoef = 0, releaseFast = 0, releaseSlow = 0, rmsCoef = 0, speechCoef = 0, makeupCoef = 0, riseCoef = 0, fallCoef = 0;
    float envDb = -120.0f, grDb = 0.0f, slowGrDb = 0.0f, makeupDb = 0.0f, rms = 0.0f, speechLevel = 0.0f, levelGainDb = 0.0f, lastGainDb = 0.0f;
    Biquad detHp;
    juce::LinearSmoothedValue<float> outSmooth { 1.0f };
};

//==============================================================================
/** Downward expander / gate: below the threshold your mic is turned down by `rangeDb`. */
class Gate
{
public:
    struct Settings
    {
        float thresholdDb = -50.0f;
        float rangeDb = 18.0f;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        envAttack  = timeCoef (fs, 1.0f);
        envRelease = timeCoef (fs, 60.0f);
        openCoef   = timeCoef (fs, 2.0f);
        closeCoef  = timeCoef (fs, 150.0f);
        holdSamples = (int) (0.15 * fs);
        sidechain = {};
        sidechain.c = BiquadCoeffs::highPass (fs, 120.0, 0.7071);
        reset();
    }

    void reset() { env = 0.0f; gainDb = 0.0f; holdCounter = 0; open = false; sidechain.reset(); }
    void setSettings (const Settings& s) { settings = s; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        const float thr = settings.thresholdDb, hyst = 4.0f;
        const float range = juce::jlimit (0.0f, 80.0f, settings.rangeDb);

        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                x = std::max (x, std::abs (buffer.getSample (ch, i)));

            const float sc = std::abs (sidechain.process (0, x));
            env = (sc > env ? envAttack : envRelease) * env + (1.0f - (sc > env ? envAttack : envRelease)) * sc;
            const float levelDb = gainToDb (env);

            if (levelDb > thr)                      { open = true; holdCounter = holdSamples; }
            else if (levelDb < thr - hyst)          { if (holdCounter > 0) --holdCounter; else open = false; }
            else if (holdCounter > 0)               --holdCounter;

            const float target = open ? 0.0f : -range;
            const float coef = target > gainDb ? openCoef : closeCoef;
            gainDb = coef * gainDb + (1.0f - coef) * target;

            const float g = dbToGain (gainDb);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
        }

        meterLevelDb.store (gainToDb (env));
        meterOpen.store (open);
        meterGainDb.store (gainDb);
    }

    std::atomic<float> meterLevelDb { -120.0f }, meterGainDb { 0.0f };
    std::atomic<bool> meterOpen { false };

private:
    double fs = 48000.0;
    Settings settings;
    float envAttack = 0, envRelease = 0, openCoef = 0, closeCoef = 0;
    float env = 0.0f, gainDb = 0.0f;
    int holdSamples = 0, holdCounter = 0;
    bool open = false;
    Biquad sidechain;
};

//==============================================================================
/** Look-ahead brickwall limiter with a fixed ceiling. "Push" drives into it. */
class Limiter
{
public:
    struct Settings
    {
        float pushDb = 0.0f;
        float ceilingDb = -1.0f;
    };

    static constexpr float lookaheadMs = 3.0f;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        look = juce::jmax (1, (int) std::round (lookaheadMs * 0.001 * fs));
        releaseCoef = timeCoef (fs, 100.0f);
        delay.setSize (2, look + maxBlock + 1);
        minWindow.assign ((size_t) look + 1, 1.0f);
        box.assign ((size_t) look, 1.0f);
        pushSmooth.reset (fs, 0.05);
        pushSmooth.setCurrentAndTargetValue (dbToGain (settings.pushDb));
        reset();
    }

    int getLatencySamples() const noexcept { return look; }

    void reset()
    {
        delay.clear();
        writePos = 0;
        std::fill (minWindow.begin(), minWindow.end(), 1.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        boxSum = (double) look;
        boxPos = minPos = 0;
        env = 1.0f;
    }

    void setSettings (const Settings& s) { settings = s; pushSmooth.setTargetValue (dbToGain (s.pushDb)); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        const float ceiling = dbToGain (settings.ceilingDb);
        const int size = delay.getNumSamples();
        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float push = pushSmooth.getNextValue();
            float peak = 0.0f;
            float in[2] {};
            for (int ch = 0; ch < numCh; ++ch)
            {
                in[ch] = buffer.getSample (ch, i) * push;
                peak = std::max (peak, std::abs (in[ch]));
            }

            // Gain this sample needs, and the minimum over the look-ahead window.
            const float need = peak > ceiling ? ceiling / peak : 1.0f;
            minWindow[(size_t) minPos] = need;
            minPos = (minPos + 1) % (int) minWindow.size();
            float target = 1.0f;
            for (float v : minWindow)
                target = std::min (target, v);

            // Instant attack, smooth release; then a box filter so gain changes are never clicks.
            env = target < env ? target : env + (target - env) * (1.0f - releaseCoef);
            boxSum += env - box[(size_t) boxPos];
            box[(size_t) boxPos] = env;
            boxPos = (boxPos + 1) % look;
            const float g = (float) (boxSum / look);

            // Delayed signal out.
            const int readPos = (writePos - look + size) % size;
            for (int ch = 0; ch < numCh; ++ch)
            {
                delay.setSample (ch, writePos, in[ch]);
                float y = delay.getSample (ch, readPos) * g;
                y = juce::jlimit (-ceiling, ceiling, y);   // safety net, normally never hit
                buffer.setSample (ch, i, y);
            }
            writePos = (writePos + 1) % size;
            maxGr = std::max (maxGr, -gainToDb (g));
        }

        meterReductionDb.store (maxGr);
    }

    std::atomic<float> meterReductionDb { 0.0f };

private:
    double fs = 48000.0;
    Settings settings;
    int look = 144, writePos = 0, boxPos = 0, minPos = 0;
    float releaseCoef = 0.0f, env = 1.0f;
    double boxSum = 0;
    juce::AudioBuffer<float> delay;
    std::vector<float> minWindow, box;
    juce::LinearSmoothedValue<float> pushSmooth { 1.0f };
};

//==============================================================================
/** Peak + RMS-ish level for meters (UI reads the atomics). */
struct LevelMeter
{
    std::atomic<float> peakDb { -100.0f };

    void measure (const juce::AudioBuffer<float>& b) noexcept
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            p = std::max (p, b.getMagnitude (ch, 0, b.getNumSamples()));
        peakDb.store (gainToDb (p));
    }
};
}
