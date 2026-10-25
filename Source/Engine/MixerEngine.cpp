#include "MixerEngine.h"
#if JUCE_WINDOWS
 #include "../VirtualMic/WinDefaultDevices.h"
#endif
#include "PluginScanner.h"

using namespace juce;

//==============================================================================
Source::Source (SourceKind k, const String& n)
    : id (Uuid().toString()), kind (k), name (n)
{
    for (int i = 0; i < kMaxOutputs; ++i)
    {
        send[(size_t) i].store (0.0f);
        sendMute[(size_t) i].store (false);
        sendSolo[(size_t) i].store (false);
    }
}

//==============================================================================
MixerEngine::MixerEngine()
{
    addDefaultFormatsToManager (formatManager);

    PropertiesFile::Options options;
    options.applicationName = "Faderhouse";
    options.folderName = "Faderhouse";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";

   #if JUCE_MAC
    // One-time migration from the early "Mixwerk" builds.
    {
        auto support = File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Application Support");
        auto oldDir = support.getChildFile ("Mixwerk");
        auto newDir = support.getChildFile ("Faderhouse");
        if (oldDir.isDirectory() && ! newDir.exists())
        {
            newDir.createDirectory();
            oldDir.getChildFile ("Mixwerk.settings").copyFileTo (newDir.getChildFile ("Faderhouse.settings"));
            oldDir.getChildFile ("session.xml").copyFileTo (newDir.getChildFile ("session.xml"));
            oldDir.getChildFile ("RecentlyCrashedPluginsList").copyFileTo (newDir.getChildFile ("RecentlyCrashedPluginsList"));
        }
    }
   #endif

    settings = std::make_unique<PropertiesFile> (options);

    if (auto xml = settings->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);

    // Scan every plugin in a separate helper process, so a crashing plugin can't take Faderhouse down.
    PluginScanner::install (knownPlugins);
}

MixerEngine::~MixerEngine()
{
    shutdown();
}

void MixerEngine::initialise()
{
   #if JUCE_WINDOWS
    // VB-CABLE carries Mic FX on Windows: keep it from being the default output, and make it
    // the default microphone once, so Discord / OBS on "Default" get the processed mic.
    {
        const bool micDone = settings->getBoolValue ("windowsDefaultMicSet", false);
        const auto changes = WinDefaultDevices::fixDefaults (! micDone);
        if (! micDone && WinDefaultDevices::isCableInstalled())
        {
            settings->setValue ("windowsDefaultMicSet", true);
            settings->saveIfNeeded();
        }
        if (changes.isNotEmpty())
            Logger::writeToLog ("Faderhouse: " + changes);
    }
   #endif

    capture = AppCaptureBackend::create();

    auto savedDevice = settings->getXmlValue ("audioDevice");
    const auto error = deviceManager.initialise (2, 2, savedDevice.get(), true);
    if (error.isNotEmpty())
        DBG ("Audio device error: " << error);
    ignoreUnused (error);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
    }

    loadSession();
    autoRouteVirtualMic();

    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (this);
    capture->setTargetSampleRate (sampleRate);

    startTimer (30 * 1000); // autosave
}

void MixerEngine::shutdown()
{
    if (capture == nullptr)
        return;

    stopTimer();
    saveSession();

    deviceManager.removeAudioCallback (this);
    deviceManager.removeChangeListener (this);
    capture->stopAll();
    capture.reset();

    clearSession();
    micFx.device.reset();
    deviceManager.closeAudioDevice();
}

//==============================================================================
void MixerEngine::audioDeviceAboutToStart (AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    blockSize = jmax (32, device->getCurrentBufferSizeSamples());

    {
        const GenericScopedLock<SpinLock> sl (stateLock);

        for (auto& s : sources) prepareSource (*s);
        for (auto& o : outputs) prepareOutput (*o);
        prepareMicFx();
    }

    running.store (true);

    const double sr = sampleRate;
    const int bs = blockSize;
    const String typeName = device->getTypeName(), deviceName = device->getName();
    MessageManager::callAsync ([this, sr, bs, typeName, deviceName, alive = std::weak_ptr<int> (lifetime)]
    {
        if (alive.expired())
            return;

        if (capture != nullptr)
        {
            capture->setTargetSampleRate (sr);
            capture->setMainOutputDevice (typeName, deviceName);
        }

        for (auto& o : outputs)
            if (o->device != nullptr)
                o->device->setMainFormat (sr, bs);

        if (micFx.device != nullptr)
            micFx.device->setMainFormat (sr, bs);
    });
}

void MixerEngine::audioDeviceStopped()
{
    running.store (false);

    // Nothing plays the mix now: let the apps be heard normally again (Windows un-mutes its output).
    MessageManager::callAsync ([this, alive = std::weak_ptr<int> (lifetime)]
    {
        if (alive.expired())
            return;

        if (capture != nullptr && deviceManager.getCurrentAudioDevice() == nullptr)
            capture->setMainOutputDevice ({}, {});
    });
}

void MixerEngine::prepareSource (Source& s)
{
    s.work.setSize (2, blockSize, false, true, false);
    s.chain.prepare (sampleRate, blockSize);
    s.bridge.setConsumerRate (sampleRate);
    s.bridge.setTargetFill (jmax (768, 3 * blockSize));
    s.bridge.requestReset();

    s.trimSmoothed.reset (sampleRate, 0.02);
    s.trimSmoothed.setCurrentAndTargetValue (s.inputGain.load());

    for (int i = 0; i < kMaxOutputs; ++i)
    {
        auto& sm = s.sendSmoothed[(size_t) i];
        sm.reset (sampleRate, 0.03);
        sm.setCurrentAndTargetValue (s.sendMute[(size_t) i].load() ? 0.0f : s.send[(size_t) i].load());
    }
}

void MixerEngine::prepareMicFx()
{
    micFx.mix.setSize (2, blockSize, false, true, false);
    micFx.gainSmoothed.reset (sampleRate, 0.015);
    micFx.gainSmoothed.setCurrentAndTargetValue (micMuted.load() ? 0.0f : 1.0f);
}

void MixerEngine::prepareOutput (Output& o)
{
    o.mix.setSize (2, blockSize, false, true, false);
    o.chain.prepare (sampleRate, blockSize);
    o.levelSmoothed.reset (sampleRate, 0.03);
    o.levelSmoothed.setCurrentAndTargetValue (o.mute.load() ? 0.0f : o.level.load());
}

//==============================================================================
void MixerEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outs, int numOutputs,
                                                    int numSamples, const AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outs[ch] != nullptr)
            FloatVectorOperations::clear (outs[ch], numSamples);

    const GenericScopedTryLock<SpinLock> sl (stateLock);
    if (! sl.isLocked())
        return; // the UI is changing the channel list right now; one silent block

    for (int done = 0; done < numSamples;)
    {
        const int n = jmin (blockSize, numSamples - done);
        processChunk (inputs, numInputs, outs, numOutputs, done, n);
        done += n;
    }
}

void MixerEngine::processChunk (const float* const* inputs, int numInputs, float* const* outs, int numOutputs,
                                int offset, int n) noexcept
{
    for (auto& o : outputs)
        o->mix.clear (0, n);

    const bool micFxActive = micFx.mix.getNumSamples() >= n;
    if (micFxActive)
        micFx.mix.clear (0, n);

    const bool globalMicMute = micMuted.load (std::memory_order_relaxed);

    // Solo is per mix: if anything is soloed in a mix, only soloed channels play in it.
    std::array<bool, kMaxOutputs> anySolo {};
    for (auto& sp : sources)
        for (size_t i = 0; i < (size_t) kMaxOutputs; ++i)
            anySolo[i] = anySolo[i] || sp->sendSolo[i].load (std::memory_order_relaxed);

    for (auto& sp : sources)
    {
        auto& s = *sp;
        auto& w = s.work;

        if (w.getNumSamples() < n)
            continue;

        // 1. Get the channel's audio.
        switch (s.kind)
        {
            case SourceKind::Input:
            {
                const int ch = s.inputChannel.load();

                if (isPositiveAndBelow (ch, numInputs) && inputs[ch] != nullptr)
                {
                    const int chR = (s.stereoInput.load() && ch + 1 < numInputs && inputs[ch + 1] != nullptr) ? ch + 1 : ch;
                    w.copyFrom (0, 0, inputs[ch] + offset, n);
                    w.copyFrom (1, 0, inputs[chR] + offset, n);
                }
                else
                {
                    w.clear (0, n);
                }
                break;
            }

            case SourceKind::App:
            case SourceKind::System:
                s.bridge.pull (w.getWritePointer (0), w.getWritePointer (1), n);
                break;
        }

        // 2. Trim, inserts, meter.
        s.trimSmoothed.setTargetValue (s.inputGain.load (std::memory_order_relaxed));
        const float trimStart = s.trimSmoothed.getCurrentValue();
        s.trimSmoothed.skip (n);
        const float trimEnd = s.trimSmoothed.getCurrentValue();
        if (! approximatelyEqual (trimStart, 1.0f) || ! approximatelyEqual (trimEnd, 1.0f))
            w.applyGainRamp (0, n, trimStart, trimEnd);

        s.chain.process (w, n);
        s.meter.push (w, n);

        // 3. Mic direct out (post inserts, no fader) to the Mic FX device.
        const bool isMic = s.kind == SourceKind::Input;

        if (micFxActive && isMic && s.toMicFx.load (std::memory_order_relaxed))
        {
            micFx.mix.addFrom (0, 0, w, 0, 0, n);
            micFx.mix.addFrom (1, 0, w, 1, 0, n);
        }

        // 4. Send to every mix with its own fader.
        const bool channelMuted = s.mute.load (std::memory_order_relaxed) || (isMic && globalMicMute);

        for (auto& op : outputs)
        {
            const auto slot = (size_t) op->slot;
            auto& sm = s.sendSmoothed[slot];
            const bool soloedOut = anySolo[slot] && ! s.sendSolo[slot].load (std::memory_order_relaxed);
            sm.setTargetValue ((channelMuted || soloedOut || s.sendMute[slot].load (std::memory_order_relaxed))
                                   ? 0.0f : s.send[slot].load (std::memory_order_relaxed));

            const float g0 = sm.getCurrentValue();
            sm.skip (n);
            const float g1 = sm.getCurrentValue();

            if (g0 <= 0.0f && g1 <= 0.0f)
                continue;

            op->mix.addFromWithRamp (0, 0, w.getReadPointer (0), n, g0, g1);
            op->mix.addFromWithRamp (1, 0, w.getReadPointer (1), n, g0, g1);
        }
    }

    // Mic FX: global mic mute (smoothed), meter, out to the virtual device.
    if (micFxActive)
    {
        micFx.gainSmoothed.setTargetValue (globalMicMute ? 0.0f : 1.0f);
        const float g0 = micFx.gainSmoothed.getCurrentValue();
        micFx.gainSmoothed.skip (n);
        const float g1 = micFx.gainSmoothed.getCurrentValue();
        if (! approximatelyEqual (g0, 1.0f) || ! approximatelyEqual (g1, 1.0f))
            micFx.mix.applyGainRamp (0, n, g0, g1);

        micFx.meter.push (micFx.mix, n);

        if (micFx.device != nullptr)
            micFx.device->push (micFx.mix, n);
    }

    // 5. Mix bus: inserts, master fader, meter, destination.
    for (auto& op : outputs)
    {
        auto& o = *op;

        if (o.mix.getNumSamples() < n)
            continue;

        o.chain.process (o.mix, n);

        o.levelSmoothed.setTargetValue (o.mute.load (std::memory_order_relaxed) ? 0.0f : o.level.load (std::memory_order_relaxed));
        const float g0 = o.levelSmoothed.getCurrentValue();
        o.levelSmoothed.skip (n);
        const float g1 = o.levelSmoothed.getCurrentValue();
        o.mix.applyGainRamp (0, n, g0, g1);

        o.meter.push (o.mix, n);

        switch ((Output::Target) o.target.load (std::memory_order_relaxed))
        {
            case Output::Target::Main:
            {
                const int first = o.mainChannelOffset.load (std::memory_order_relaxed);
                for (int ch = 0; ch < 2; ++ch)
                    if (isPositiveAndBelow (first + ch, numOutputs) && outs[first + ch] != nullptr)
                        FloatVectorOperations::add (outs[first + ch] + offset, o.mix.getReadPointer (ch), n);
                break;
            }

            case Output::Target::Device:
                if (o.device != nullptr)
                    o.device->push (o.mix, n);
                break;

            case Output::Target::None:
                break;
        }
    }
}

//==============================================================================
Source* MixerEngine::addSource (SourceKind kind, const String& name)
{
    auto s = std::make_unique<Source> (kind, name);

    // Sensible defaults: inputs (mic) go to the virtual mic, apps go to your headphones.
    for (auto& o : outputs)
    {
        const bool toDevice = (Output::Target) o->target.load() == Output::Target::Device;
        const bool isInput = kind == SourceKind::Input;
        s->send[(size_t) o->slot].store (isInput == toDevice ? 1.0f : 0.0f);
    }

    prepareSource (*s);
    auto* raw = s.get();

    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        sources.push_back (std::move (s));
    }

    if (kind != SourceKind::Input)
        pushRoutes();

    structureChanged();
    return raw;
}

void MixerEngine::removeSource (Source* source)
{
    std::unique_ptr<Source> removed;

    {
        // Stop capturing into this channel's bridge before the channel goes away.
        std::vector<AppCaptureBackend::Route> routes;
        for (auto& s : sources)
            if (s.get() != source && s->kind != SourceKind::Input)
                routes.push_back ({ s->id, s->appIds, s->kind == SourceKind::System, &s->bridge });

        if (capture != nullptr)
            capture->setRoutes (routes);
    }

    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        auto it = std::find_if (sources.begin(), sources.end(), [source] (auto& s) { return s.get() == source; });
        if (it == sources.end())
            return;

        removed = std::move (*it);
        sources.erase (it);
    }

    for (int i = removed->chain.size(); --i >= 0;)
        removePlugin (removed->chain, i);

    removed.reset();
    structureChanged();
}

void MixerEngine::moveSource (Source* source, int delta)
{
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        auto it = std::find_if (sources.begin(), sources.end(), [source] (auto& s) { return s.get() == source; });
        if (it == sources.end())
            return;

        const int from = (int) std::distance (sources.begin(), it);
        const int to = jlimit (0, (int) sources.size() - 1, from + delta);
        if (from == to)
            return;

        std::swap (sources[(size_t) from], sources[(size_t) to]);
    }

    pushRoutes(); // order decides which App channel wins when two claim the same app
    structureChanged();
}

void MixerEngine::moveSourceTo (Source* source, int newIndex)
{
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        auto it = std::find_if (sources.begin(), sources.end(), [source] (auto& s) { return s.get() == source; });
        if (it == sources.end())
            return;

        const int from = (int) std::distance (sources.begin(), it);
        const int to = jlimit (0, (int) sources.size() - 1, newIndex);
        if (from == to)
            return;

        auto moved = std::move (sources[(size_t) from]);
        sources.erase (sources.begin() + from);
        sources.insert (sources.begin() + to, std::move (moved));
    }

    pushRoutes();
    structureChanged();
}

Output* MixerEngine::addOutput (const String& name)
{
    int slot = -1;
    for (int i = 0; i < kMaxOutputs && slot < 0; ++i)
        if (findOutputBySlot (i) == nullptr)
            slot = i;

    if (slot < 0)
        return nullptr;

    auto o = std::make_unique<Output> (slot, name);
    prepareOutput (*o);

    for (auto& s : sources)
    {
        s->send[(size_t) slot].store (s->kind == SourceKind::Input ? 0.0f : 1.0f);
        s->sendMute[(size_t) slot].store (false);
    }

    auto* raw = o.get();
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        outputs.push_back (std::move (o));
    }

    structureChanged();
    return raw;
}

void MixerEngine::removeOutput (Output* output)
{
    std::unique_ptr<Output> removed;

    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        auto it = std::find_if (outputs.begin(), outputs.end(), [output] (auto& o) { return o.get() == output; });
        if (it == outputs.end())
            return;

        removed = std::move (*it);
        outputs.erase (it);
    }

    for (int i = removed->chain.size(); --i >= 0;)
        removePlugin (removed->chain, i);

    removed.reset(); // closes its extra device
    structureChanged();
}

Output* MixerEngine::findOutputBySlot (int slot) const
{
    for (auto& o : outputs)
        if (o->slot == slot)
            return o.get();
    return nullptr;
}

void MixerEngine::setOutputToMain (Output& output, int channelOffset)
{
    std::unique_ptr<ExtraOutputDevice> old;
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        old = std::move (output.device);
        output.mainChannelOffset.store (jmax (0, channelOffset));
        output.target.store ((int) Output::Target::Main);
    }

    old.reset();
    output.deviceType.clear();
    output.deviceName.clear();

    const auto names = getActiveOutputChannelNames();
    output.status = isPositiveAndBelow (channelOffset, names.size())
                        ? "Main: " + names[channelOffset] + (channelOffset + 1 < names.size() ? " + " + names[channelOffset + 1] : String())
                        : "Main: channel " + String (channelOffset + 1) + " (not active)";
}

void MixerEngine::setOutputToDevice (Output& output, const String& typeName, const String& deviceName)
{
    auto device = std::make_unique<ExtraOutputDevice>();
    const auto error = device->open (deviceManager, typeName, deviceName, sampleRate, blockSize);

    output.deviceType = typeName;
    output.deviceName = deviceName;

    std::unique_ptr<ExtraOutputDevice> old;
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        old = std::move (output.device);

        if (error.isEmpty())
            output.device = std::move (device);

        output.target.store ((int) Output::Target::Device);
    }

    old.reset();
    output.status = error.isEmpty() ? output.device->getDescription() : "Error: " + error;
}

void MixerEngine::setOutputToNone (Output& output)
{
    std::unique_ptr<ExtraOutputDevice> old;
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        old = std::move (output.device);
        output.target.store ((int) Output::Target::None);
    }

    old.reset();
    output.deviceType.clear();
    output.deviceName.clear();
    output.status = "Not routed";
}

void MixerEngine::setMicFxDevice (const String& typeName, const String& deviceName)
{
    auto device = std::make_unique<ExtraOutputDevice>();
    const auto error = device->open (deviceManager, typeName, deviceName, sampleRate, blockSize);

    micFx.deviceType = typeName;
    micFx.deviceName = deviceName;

    std::unique_ptr<ExtraOutputDevice> old;
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        old = std::move (micFx.device);
        if (error.isEmpty())
            micFx.device = std::move (device);
    }

    old.reset();
    micFx.status = error.isEmpty() ? micFx.device->getDescription() : "Error: " + error;
}

void MixerEngine::setMicFxNone()
{
    std::unique_ptr<ExtraOutputDevice> old;
    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        old = std::move (micFx.device);
    }

    old.reset();
    micFx.deviceType.clear();
    micFx.deviceName.clear();
    micFx.status = "Not connected";
}

void MixerEngine::setAppIds (Source& source, const StringArray& ids)
{
    source.appIds = ids;
    pushRoutes();
}

void MixerEngine::pushRoutes()
{
    if (capture == nullptr)
        return;

    std::vector<AppCaptureBackend::Route> routes;
    for (auto& s : sources)
        if (s->kind != SourceKind::Input)
            routes.push_back ({ s->id, s->appIds, s->kind == SourceKind::System, &s->bridge });

    capture->setRoutes (routes);
}

void MixerEngine::structureChanged()
{
    if (onStructureChanged != nullptr)
        onStructureChanged();
}

//==============================================================================
bool MixerEngine::addPlugin (PluginChain& chain, const PluginDescription& description, String& error)
{
    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, error);

    if (instance == nullptr)
        return false;

    if (chain.add (std::move (instance)) < 0)
    {
        error = "This chain is full (" + String (PluginChain::maxSlots) + " plugins)";
        return false;
    }

    return true;
}

void MixerEngine::removePlugin (PluginChain& chain, int index)
{
    if (auto* p = chain.get (index))
        if (onPluginAboutToBeDeleted != nullptr)
            onPluginAboutToBeDeleted (p);

    chain.remove (index); // destroyed here, on the message thread
}

//==============================================================================
std::vector<AudioProcessInfo> MixerEngine::listAudioProcesses()
{
    return capture != nullptr ? capture->listAudioProcesses() : std::vector<AudioProcessInfo>{};
}

String MixerEngine::getCaptureStatus() const
{
    return capture != nullptr ? capture->getStatusText() : String();
}

bool MixerEngine::isCaptureSupported() const
{
    return capture != nullptr && capture->isSupported();
}

StringArray MixerEngine::getActiveInputChannelNames() const
{
    StringArray result;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        const auto names = d->getInputChannelNames();
        const auto active = d->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }
    return result;
}

StringArray MixerEngine::getActiveOutputChannelNames() const
{
    StringArray result;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        const auto names = d->getOutputChannelNames();
        const auto active = d->getActiveOutputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }
    return result;
}

//==============================================================================
bool MixerEngine::findVirtualMicDevice (String& typeName, String& deviceName)
{
    const StringArray preferred { "Mic FX", "BlackHole", "CABLE Input" };

    for (auto& wanted : preferred)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            for (auto& name : type->getDeviceNames (false))
            {
                if (name.startsWithIgnoreCase (wanted))
                {
                    typeName = type->getTypeName();
                    deviceName = name;
                    return true;
                }
            }
        }
    }

    return false;
}

void MixerEngine::autoRouteVirtualMic()
{
    if (micFx.device != nullptr)
        return;

    // Reconnect to the saved device if it is back, otherwise find Mic FX (or another virtual cable).
    if (micFx.deviceName.isNotEmpty())
    {
        setMicFxDevice (micFx.deviceType, micFx.deviceName);
        if (micFx.device != nullptr)
            return;
    }

    String type, name;
    if (findVirtualMicDevice (type, name))
        setMicFxDevice (type, name);
    else
       #if JUCE_WINDOWS
        micFx.status = "Install VB-CABLE (see Settings)";
       #else
        micFx.status = "Install \"Mic FX\" in Settings";
       #endif
}

//==============================================================================
void MixerEngine::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            settings->setValue ("knownPlugins", xml.get());
        settings->saveIfNeeded();
        return;
    }

    if (source == &deviceManager)
    {
        if (auto xml = deviceManager.createStateXml())
            settings->setValue ("audioDevice", xml.get());
        settings->saveIfNeeded();

        // Channel names/offsets may have changed.
        for (auto& o : outputs)
            if ((Output::Target) o->target.load() == Output::Target::Main)
                setOutputToMain (*o, o->mainChannelOffset.load());

        structureChanged();
    }
}

void MixerEngine::timerCallback()
{
    saveSession();
}
