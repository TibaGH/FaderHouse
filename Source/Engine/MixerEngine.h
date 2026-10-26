#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "ClockBridge.h"
#include "ExtraOutputDevice.h"
#include "LevelMeter.h"
#include "PluginChain.h"
#include "../Capture/AppCapture.h"

constexpr int kMaxOutputs = 8;

enum class SourceKind { Input, App, System };

/** One mixer channel: a hardware input (mic), a group of apps, or "everything else". */
struct Source
{
    Source (SourceKind k, const juce::String& n);

    juce::String id;
    SourceKind kind;
    juce::String name;
    juce::StringArray appIds;                 // App channels: apps mapped here (message thread)
    juce::String icon;                        // icon id ("" = automatic), see ChannelIcons

    std::atomic<int>  inputChannel { 0 };     // Input channels: index into the device's active inputs
    std::atomic<bool> stereoInput { false };  // use inputChannel and inputChannel + 1
    std::atomic<float> inputGain { 1.0f };    // input trim (linear)
    std::atomic<bool> mute { false };         // mutes the channel in every mix
    std::atomic<bool> toMicFx { false };      // Input channels: feeds the Mic FX device (post inserts)

    std::array<std::atomic<float>, kMaxOutputs> send;      // fader per mix (linear gain)
    std::array<std::atomic<bool>,  kMaxOutputs> sendMute;
    std::array<std::atomic<bool>,  kMaxOutputs> sendSolo;

    PluginChain chain;
    ClockBridge bridge;                       // App / System: captured audio arrives here
    LevelMeter meter;                         // post-insert, pre-fader

    // audio thread only
    juce::AudioBuffer<float> work;
    std::array<juce::SmoothedValue<float>, kMaxOutputs> sendSmoothed;
    juce::SmoothedValue<float> trimSmoothed;
};

/** One mix bus that goes to a physical output pair or to another device (e.g. the virtual mic). */
struct Output
{
    enum class Target { None = 0, Main = 1, Device = 2 };

    explicit Output (int slotIndex, const juce::String& n) : slot (slotIndex), name (n) {}

    const int slot;
    juce::String name;

    std::atomic<int> target { (int) Target::None };
    std::atomic<int> mainChannelOffset { 0 };      // Target::Main: first output channel (0 = out 1/2)
    juce::String deviceType, deviceName;           // Target::Device (message thread)
    juce::String status;                           // message thread

    std::atomic<float> level { 1.0f };
    std::atomic<bool> mute { false };

    PluginChain chain;
    LevelMeter meter;
    std::unique_ptr<ExtraOutputDevice> device;     // swapped under the engine lock

    // audio thread only
    juce::AudioBuffer<float> mix;
    juce::SmoothedValue<float> levelSmoothed;
};

/** The fixed direct out of the mic channel(s), post inserts, to the "Mic FX" virtual device. No faders. */
struct MicFxOutput
{
    juce::String deviceType, deviceName;           // message thread
    juce::String status;                           // message thread
    std::unique_ptr<ExtraOutputDevice> device;     // swapped under the engine lock
    LevelMeter meter;

    // audio thread only
    juce::AudioBuffer<float> mix;
    juce::SmoothedValue<float> gainSmoothed;
};

//==============================================================================
class MixerEngine final : public juce::AudioIODeviceCallback,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    MixerEngine();
    ~MixerEngine() override;

    void initialise();

    /** The user is choosing a device themselves: stop waiting for the saved one to appear. */
    void stopWaitingForSavedDevice()   { waitForSavedDevice.stopTimer(); }
    void shutdown();

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<juce::PropertiesFile> settings;

    // ---- structure (message thread) ----
    const std::vector<std::unique_ptr<Source>>& getSources() const noexcept { return sources; }
    const std::vector<std::unique_ptr<Output>>& getOutputs() const noexcept { return outputs; }

    Source* addSource (SourceKind kind, const juce::String& name);
    void removeSource (Source* source);
    void moveSource (Source* source, int delta);
    void moveSourceTo (Source* source, int newIndex);

    Output* addOutput (const juce::String& name);   // nullptr when all slots are used
    void removeOutput (Output* output);
    Output* findOutputBySlot (int slot) const;

    void setOutputToMain (Output& output, int channelOffset);
    void setOutputToDevice (Output& output, const juce::String& typeName, const juce::String& deviceName);
    void setOutputToNone (Output& output);

    void setAppIds (Source& source, const juce::StringArray& ids);

    // ---- Mic FX (direct out of the mic) ----
    MicFxOutput& getMicFx() noexcept                { return micFx; }
    void setMicFxDevice (const juce::String& typeName, const juce::String& deviceName);
    void setMicFxNone();
    /** Global mic mute: silences every input channel everywhere (Mic FX and all mixes). */
    void setMicMuted (bool shouldBeMuted)           { micMuted.store (shouldBeMuted); }
    bool isMicMuted() const noexcept                { return micMuted.load(); }

    // ---- plugins (message thread) ----
    bool addPlugin (PluginChain& chain, const juce::PluginDescription& description, juce::String& error);
    void removePlugin (PluginChain& chain, int index);
    std::function<void (juce::AudioProcessor*)> onPluginAboutToBeDeleted;   // UI closes the editor window

    // ---- capture ----
    std::vector<AudioProcessInfo> listAudioProcesses();
    juce::String getCaptureStatus() const;
    juce::String getCaptureWarning() const;
    bool isCaptureSupported() const;

    // ---- misc ----
    double getSampleRate() const noexcept   { return sampleRate; }
    int getBlockSize() const noexcept       { return blockSize; }
    juce::StringArray getActiveInputChannelNames() const;
    juce::StringArray getActiveOutputChannelNames() const;
    juce::File getSessionFile() const;

    /** Finds our own "Mic FX" device, else another known virtual cable. */
    bool findVirtualMicDevice (juce::String& typeName, juce::String& deviceName);

    /** Connects the mic direct out to Mic FX if it isn't connected to a working device yet. */
    void autoRouteVirtualMic();

    void saveSession();
    void loadSession();
    juce::StringArray lastLoadErrors;

    /** Called (message thread) when channels/outputs were added, removed or reordered. */
    std::function<void()> onStructureChanged;

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void processChunk (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                       int offset, int numSamples) noexcept;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void prepareSource (Source& s);
    void prepareOutput (Output& o);
    void prepareMicFx();
    void pushRoutes();
    void structureChanged();
    void createDefaultSession();
    void clearSession();

    std::unique_ptr<juce::XmlElement> createSessionXml() const;
    void restoreSession (const juce::XmlElement& xml);

    juce::SpinLock stateLock;  // guards the vectors below and Output::device against the audio thread
    std::vector<std::unique_ptr<Source>> sources;
    std::vector<std::unique_ptr<Output>> outputs;

    std::unique_ptr<AppCaptureBackend> capture;

    // Right after logging in, a USB/Thunderbolt interface may not be there yet. Then we keep
    // trying the saved device for a while instead of settling for (and saving) another one.
    struct WaitForSavedDevice final : public juce::Timer
    {
        explicit WaitForSavedDevice (MixerEngine& e) : engine (e) {}
        void timerCallback() override { engine.retrySavedDevice(); }
        MixerEngine& engine;
    };
    void retrySavedDevice();
    bool isSavedDeviceOpen() const;
    WaitForSavedDevice waitForSavedDevice { *this };
    std::unique_ptr<juce::XmlElement> savedDeviceState;
    juce::uint32 waitForDeviceUntil = 0;

    std::shared_ptr<int> lifetime = std::make_shared<int> (0);   // async callbacks check this is still alive
    MicFxOutput micFx;
    std::atomic<bool> micMuted { false };

    double sampleRate = 48000.0;
    int blockSize = 512;
    std::atomic<bool> running { false };

};

inline float gainToSliderDb (float gain)   { return juce::Decibels::gainToDecibels (gain, -60.0f); }
inline float sliderDbToGain (float db)     { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db); }
