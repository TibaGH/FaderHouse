#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

/**
    An insert chain of hosted VST3 / AU plugins, processed in stereo.

    Edits happen on the message thread under a spin lock. The audio thread only
    try-locks: if an edit is in progress it lets that one block pass through dry
    instead of waiting, so the audio thread never blocks.
*/
class PluginChain
{
public:
    static constexpr int maxSlots = 16;

    PluginChain();
    ~PluginChain();

    // ---- message thread ----
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();

    /** Takes ownership. Returns the index where it was inserted, or -1 when the chain is full. */
    int add (std::unique_ptr<juce::AudioPluginInstance> plugin);
    std::unique_ptr<juce::AudioPluginInstance> remove (int index);
    void move (int from, int to);
    void clear();

    int size() const noexcept                        { return (int) slots.size(); }
    juce::AudioPluginInstance* get (int index) const noexcept;
    bool isBypassed (int index) const noexcept;
    void setBypassed (int index, bool shouldBypass) noexcept;
    int getTotalLatencySamples() const;

    std::unique_ptr<juce::XmlElement> createXml() const;
    void restoreFromXml (const juce::XmlElement& xml, juce::AudioPluginFormatManager& formats,
                         juce::StringArray& errors);

    // ---- audio thread ----
    /** Processes the first two channels of the buffer in place. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::atomic<bool> bypass { false };
        juce::AudioBuffer<float> io;
        int numIns = 2, numOuts = 2;
    };

    void prepareSlot (Slot& slot);

    std::vector<std::unique_ptr<Slot>> slots;
    juce::MidiBuffer midi;
    juce::SpinLock lock;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE (PluginChain)
};
