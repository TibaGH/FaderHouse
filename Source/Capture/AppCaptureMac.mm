// macOS per-app capture via Core Audio process taps (macOS 14.2+).
//
// For every mixer channel we create one private tap:
//   - App channel:    stereo mix-down of the matched processes, muted while tapped
//   - System channel: global tap that EXCLUDES Faderhouse itself and every process
//                     that is mapped to another channel, also muted while tapped
// Each tap lives in its own private aggregate device whose IOProc pushes the
// audio into the channel's ClockBridge. When Faderhouse stops or quits, the taps
// go away and every app plays normally again.
//
// Compiled without ARC (JUCE default), so Objective-C objects are retained/released manually.

#include "AppCapture.h"
#include "../Engine/ClockBridge.h"

#include <juce_events/juce_events.h>

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AudioHardwareTapping.h>
#include <CoreAudio/CATapDescription.h>
#include <CoreAudio/CoreAudio.h>
#include <libproc.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace
{
using juce::String;

AudioObjectPropertyAddress globalAddress (AudioObjectPropertySelector selector)
{
    return { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
}

std::vector<AudioObjectID> readProcessObjects()
{
    auto a = globalAddress (kAudioHardwarePropertyProcessObjectList);
    UInt32 size = 0;

    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr || size == 0)
        return {};

    std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr)
        return {};

    ids.resize (size / sizeof (AudioObjectID));
    return ids;
}

String readString (AudioObjectID obj, AudioObjectPropertySelector selector)
{
    auto a = globalAddress (selector);
    CFStringRef str = nullptr;
    UInt32 size = sizeof (CFStringRef);

    if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, &str) != noErr || str == nullptr)
        return {};

    auto result = String::fromCFString (str);
    CFRelease (str);
    return result;
}

template <typename T>
T readValue (AudioObjectID obj, AudioObjectPropertySelector selector, T fallback)
{
    auto a = globalAddress (selector);
    T value = fallback;
    UInt32 size = sizeof (T);

    if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, &value) != noErr)
        return fallback;

    return value;
}

AudioObjectID processObjectForPid (pid_t pid)
{
    auto a = globalAddress (kAudioHardwarePropertyTranslatePIDToProcessObject);
    AudioObjectID obj = kAudioObjectUnknown;
    UInt32 size = sizeof (obj);

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, sizeof (pid), &pid, &size, &obj) != noErr)
        return kAudioObjectUnknown;

    return obj;
}

String executableName (pid_t pid)
{
    char name[2 * MAXCOMLEN + 1] = {};
    if (proc_name (pid, name, sizeof (name)) > 0)
        return String::fromUTF8 (name);
    return {};
}

pid_t parentPid (pid_t pid)
{
    struct proc_bsdinfo info;
    if (proc_pidinfo (pid, PROC_PIDTBSDINFO, 0, &info, sizeof (info)) == (int) sizeof (info))
        return (pid_t) info.pbi_ppid;
    return 0;
}

String bundleIdForPid (pid_t pid)
{
    @autoreleasepool
    {
        if (auto* app = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
            if (app.bundleIdentifier != nil)
                return String::fromUTF8 ([app.bundleIdentifier UTF8String]);
    }
    return {};
}

bool isRegularAppPid (pid_t pid)
{
    @autoreleasepool
    {
        if (auto* app = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
            return app.activationPolicy == NSApplicationActivationPolicyRegular;
    }
    return false;
}

String displayNameFor (const String& rootId, pid_t pid)
{
    if (rootId.startsWithIgnoreCase ("com.apple.WebKit"))
        return "Safari / WebKit";

    @autoreleasepool
    {
        if (! rootId.startsWith ("exe:"))
        {
            NSArray<NSRunningApplication*>* apps =
                [NSRunningApplication runningApplicationsWithBundleIdentifier: [NSString stringWithUTF8String: rootId.toRawUTF8()]];

            if (apps.count > 0 && apps.firstObject.localizedName != nil)
                return String::fromUTF8 ([apps.firstObject.localizedName UTF8String]);
        }

        if (auto* app = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
            if (app.localizedName != nil)
                return String::fromUTF8 ([app.localizedName UTF8String]);
    }

    return rootId.startsWith ("exe:") ? rootId.substring (4) : rootId;
}

struct ProcEntry
{
    AudioObjectID object = kAudioObjectUnknown;
    pid_t pid = 0;
    String appId;        // bundle id or exe:<name>
    String parentAppId;  // app id of the parent process (catches helpers with odd bundle ids)
    bool outputting = false;
    bool isRegularApp = false;        // a normal app with a Dock icon
    bool parentIsRegularApp = false;  // a helper of such an app
};

//==============================================================================
struct Tap
{
    String sourceId;
    bool isSystem = false;
    std::atomic<ClockBridge*> bridge { nullptr };

    CATapDescription* description = nil;
    AudioObjectID tapId = kAudioObjectUnknown;
    AudioObjectID aggregateId = kAudioObjectUnknown;
    AudioDeviceIOProcID procId = nullptr;
    std::set<AudioObjectID> processes;
    double rate = 0.0;
    String error;
    juce::uint32 retryAt = 0;

    bool isRunning() const { return procId != nullptr; }
};

OSStatus tapIOProc (AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                    const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* clientData)
{
    auto* tap = static_cast<Tap*> (clientData);
    auto* bridge = tap->bridge.load (std::memory_order_acquire);

    if (bridge == nullptr || input == nullptr || input->mNumberBuffers == 0)
        return noErr;

    const AudioBuffer& first = input->mBuffers[0];
    if (first.mData == nullptr || first.mNumberChannels == 0)
        return noErr;

    if (input->mNumberBuffers == 1)
    {
        // interleaved (the normal case for a stereo mix-down tap)
        const int channels = (int) first.mNumberChannels;
        const int frames = (int) (first.mDataByteSize / (sizeof (float) * (UInt32) channels));
        bridge->pushInterleaved (static_cast<const float*> (first.mData), channels, frames);
    }
    else
    {
        // non-interleaved: one buffer per channel
        const int frames = (int) (first.mDataByteSize / sizeof (float));
        const float* l = static_cast<const float*> (first.mData);
        const float* r = static_cast<const float*> (input->mBuffers[1].mData);
        bridge->push (l, r != nullptr ? r : l, frames);
    }

    return noErr;
}

NSArray<NSNumber*>* toNSArray (const std::set<AudioObjectID>& ids)
{
    NSMutableArray<NSNumber*>* array = [NSMutableArray arrayWithCapacity: ids.size()];
    for (auto objectId : ids)
        [array addObject: [NSNumber numberWithUnsignedInt: objectId]];
    return array;
}

//==============================================================================
class MacCaptureBackend final : public AppCaptureBackend,
                                private juce::Timer
{
public:
    MacCaptureBackend()
    {
        ownPid = getpid();
        startTimer (1000);
    }

    ~MacCaptureBackend() override
    {
        stopTimer();
        stopAll();
    }

    // The deployment target is macOS 14.2, the first version with process taps.
    bool isSupported() const override { return true; }

    String getStatusText() const override
    {
        if (! isSupported())
            return "Per-app capture needs macOS 14.2 or newer";

        int running = 0;
        String firstError;

        for (auto& [sourceId, tap] : taps)
        {
            if (tap->isRunning()) ++running;
            if (firstError.isEmpty() && tap->error.isNotEmpty()) firstError = tap->error;
        }

        if (firstError.isNotEmpty())
            return "Capture problem: " + firstError;

        if (waitingForOwnProcess)
            return "Starting capture...";

        return String (running) + (running == 1 ? " capture active" : " captures active");
    }

    std::vector<AudioProcessInfo> listAudioProcesses() override
    {
        std::map<String, AudioProcessInfo> byRoot;

        for (auto& p : enumerate())
        {
            // Real apps stand on their own; their helper processes are grouped under them.
            const bool webKit = p.appId.startsWithIgnoreCase ("com.apple.WebKit");
            String root = AppCaptureBackend::rootAppId (p.appId);

            if (! p.isRegularApp && p.parentIsRegularApp && p.parentAppId.isNotEmpty())
                root = AppCaptureBackend::rootAppId (p.parentAppId);
            else if (root.startsWith ("exe:") && p.parentAppId.isNotEmpty() && ! p.parentAppId.startsWith ("exe:"))
                root = AppCaptureBackend::rootAppId (p.parentAppId);

            auto& info = byRoot[root];
            if (info.appId.isEmpty())
            {
                info.appId = root;
                info.pid = (int) p.pid;
                info.displayName = displayNameFor (root, p.pid);
            }
            info.isOutputting = info.isOutputting || p.outputting;
            info.isApp = info.isApp || p.isRegularApp || p.parentIsRegularApp || webKit;
        }

        std::vector<AudioProcessInfo> result;
        for (auto& [root, info] : byRoot)
            result.push_back (info);

        std::sort (result.begin(), result.end(), [] (auto& a, auto& b)
        {
            if (a.isOutputting != b.isOutputting) return a.isOutputting;
            return a.displayName.compareIgnoreCase (b.displayName) < 0;
        });

        return result;
    }

    void setRoutes (const std::vector<Route>& newRoutes) override
    {
        routes = newRoutes;

        // Tear down taps whose channel disappeared.
        for (auto it = taps.begin(); it != taps.end();)
        {
            const bool stillThere = std::any_of (routes.begin(), routes.end(),
                                                 [&] (const Route& r) { return r.sourceId == it->first; });
            if (! stillThere)
            {
                destroyTap (*it->second);
                it = taps.erase (it);
            }
            else
            {
                ++it;
            }
        }

        refresh();
    }

    void setTargetSampleRate (double sampleRate) override
    {
        if (sampleRate <= 0 || sampleRate == targetRate)
            return;

        targetRate = sampleRate;

        for (auto& [sourceId, tap] : taps)
            if (tap->aggregateId != kAudioObjectUnknown)
                applyRate (*tap);
    }

    void stopAll() override
    {
        for (auto& [sourceId, tap] : taps)
            destroyTap (*tap);

        taps.clear();
    }

private:
    //==========================================================================
    void timerCallback() override { refresh(); }

    std::vector<ProcEntry> enumerate()
    {
        std::vector<ProcEntry> result;
        ownObjects.clear();

        for (auto obj : readProcessObjects())
        {
            ProcEntry e;
            e.object = obj;
            e.pid = readValue<pid_t> (obj, kAudioProcessPropertyPID, -1);

            if (e.pid <= 0)
                continue;

            if (e.pid == ownPid)
            {
                ownObjects.insert (obj);
                continue;
            }

            e.appId = readString (obj, kAudioProcessPropertyBundleID);
            if (e.appId.isEmpty())
                e.appId = "exe:" + executableName (e.pid);

            e.outputting = readValue<UInt32> (obj, kAudioProcessPropertyIsRunningOutput, 0) != 0;
            e.isRegularApp = isRegularAppPid (e.pid);

            const pid_t ppid = parentPid (e.pid);
            if (ppid > 1)
            {
                auto it = parentCache.find (ppid);
                if (it == parentCache.end())
                {
                    String parentId = bundleIdForPid (ppid);
                    if (parentId.isEmpty())
                        parentId = "exe:" + executableName (ppid);
                    it = parentCache.emplace (ppid, parentId).first;
                    parentRegular[ppid] = isRegularAppPid (ppid);
                }
                e.parentAppId = it->second;
                e.parentIsRegularApp = parentRegular[ppid];
            }

            result.push_back (e);
        }

        if (parentCache.size() > 512)
        {
            parentCache.clear();
            parentRegular.clear();
        }

        // Our own process object only exists once we have opened an audio device.
        if (ownObjects.empty())
        {
            const auto own = processObjectForPid (ownPid);
            if (own != kAudioObjectUnknown)
                ownObjects.insert (own);
        }

        return result;
    }

    void refresh()
    {
        if (! isSupported() || routes.empty())
            return;

        {
            const auto procs = enumerate();
            std::set<AudioObjectID> assigned;
            std::map<String, std::set<AudioObjectID>> wanted;

            // App channels first; the first channel that claims a process gets it.
            for (auto& route : routes)
            {
                if (route.isSystem)
                    continue;

                auto& set = wanted[route.sourceId];

                for (auto& p : procs)
                {
                    if (assigned.count (p.object) > 0)
                        continue;

                    for (auto& routeAppId : route.appIds)
                    {
                        if (AppCaptureBackend::appIdMatches (p.appId, routeAppId) || AppCaptureBackend::appIdMatches (p.parentAppId, routeAppId))
                        {
                            set.insert (p.object);
                            assigned.insert (p.object);
                            break;
                        }
                    }
                }
            }

            // System channel: everything except Faderhouse itself and the mapped apps.
            std::set<AudioObjectID> systemExclude = assigned;
            systemExclude.insert (ownObjects.begin(), ownObjects.end());

            waitingForOwnProcess = false;

            for (auto& route : routes)
            {
                auto& tapPtr = taps[route.sourceId];
                if (tapPtr == nullptr)
                {
                    tapPtr = std::make_unique<Tap>();
                    tapPtr->sourceId = route.sourceId;
                }

                auto& tap = *tapPtr;
                tap.isSystem = route.isSystem;
                tap.bridge.store (route.bridge, std::memory_order_release);

                if (route.isSystem)
                {
                    // Never start a global tap before Faderhouse's own output can be excluded,
                    // otherwise we would capture (and mute) ourselves -> feedback.
                    if (ownObjects.empty())
                    {
                        waitingForOwnProcess = true;
                        destroyTap (tap);
                        continue;
                    }

                    applyProcesses (tap, systemExclude);
                }
                else
                {
                    auto& set = wanted[route.sourceId];

                    if (set.empty())
                        destroyTap (tap); // nothing to capture right now; the app plays normally
                    else
                        applyProcesses (tap, set);
                }
            }
        }
    }

    //==========================================================================
    void applyProcesses (Tap& tap, const std::set<AudioObjectID>& processes)
    {
        if (tap.isRunning() && tap.processes == processes)
            return;

        if (tap.isRunning())
        {
            // Update the live tap; recreate it if the system refuses.
            @autoreleasepool
            {
                [tap.description setProcesses: toNSArray (processes)];

                auto a = globalAddress (kAudioTapPropertyDescription);
                CATapDescription* desc = tap.description;
                const OSStatus err = AudioObjectSetPropertyData (tap.tapId, &a, 0, nullptr, sizeof (CATapDescription*), &desc);

                if (err == noErr)
                {
                    tap.processes = processes;
                    return;
                }
            }

            destroyTap (tap);
        }

        // After a failure, don't hammer Core Audio (or the permission prompt) every second.
        const auto now = juce::Time::getMillisecondCounter();
        if (now < tap.retryAt)
            return;

        createTap (tap, processes);

        if (! tap.isRunning())
            tap.retryAt = now + 10000;
    }

    void createTap (Tap& tap, const std::set<AudioObjectID>& processes)
    {
        tap.error.clear();

        @autoreleasepool
        {
            NSArray<NSNumber*>* list = toNSArray (processes);

            CATapDescription* desc = tap.isSystem
                ? [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses: list]
                : [[CATapDescription alloc] initStereoMixdownOfProcesses: list];

            if (desc == nil)
            {
                tap.error = "could not create tap description";
                return;
            }

            [desc setName: @"Faderhouse"];
            [desc setPrivate: YES];
            [desc setMuteBehavior: CATapMutedWhenTapped];

            AudioObjectID tapId = kAudioObjectUnknown;
            OSStatus err = AudioHardwareCreateProcessTap (desc, &tapId);

            if (err != noErr || tapId == kAudioObjectUnknown)
            {
                [desc release];
                tap.error = "AudioHardwareCreateProcessTap failed (" + String ((int) err) + ")";
                return;
            }

            tap.description = desc; // keeps the +1 from alloc
            tap.tapId = tapId;

            // The tap must deliver 32-bit float.
            const auto format = readValue<AudioStreamBasicDescription> (tapId, kAudioTapPropertyFormat, AudioStreamBasicDescription {});
            if ((format.mFormatFlags & kAudioFormatFlagIsFloat) == 0 || format.mBitsPerChannel != 32)
            {
                tap.error = "unexpected tap format";
                destroyTap (tap);
                return;
            }

            NSString* tapUid = [[desc UUID] UUIDString];
            NSDictionary* aggregate = @{
                @kAudioAggregateDeviceNameKey:         @"Faderhouse Tap",
                @kAudioAggregateDeviceUIDKey:          [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceIsPrivateKey:    @YES,
                @kAudioAggregateDeviceTapAutoStartKey: @NO,
                @kAudioAggregateDeviceTapListKey:      @[ @{ @kAudioSubTapUIDKey: tapUid,
                                                             @kAudioSubTapDriftCompensationKey: @YES } ]
            };

            AudioObjectID aggregateId = kAudioObjectUnknown;
            err = AudioHardwareCreateAggregateDevice ((CFDictionaryRef) aggregate, &aggregateId);

            if (err != noErr)
            {
                tap.error = "could not create aggregate device (" + String ((int) err) + ")";
                destroyTap (tap);
                return;
            }

            tap.aggregateId = aggregateId;

            UInt32 frames = 256;
            auto bufferAddress = globalAddress (kAudioDevicePropertyBufferFrameSize);
            AudioObjectSetPropertyData (aggregateId, &bufferAddress, 0, nullptr, sizeof (frames), &frames);

            applyRate (tap);

            // The first call triggers the macOS "system audio recording" permission prompt.
            AudioDeviceIOProcID procId = nullptr;
            err = AudioDeviceCreateIOProcID (aggregateId, tapIOProc, &tap, &procId);

            if (err != noErr || procId == nullptr)
            {
                tap.error = "could not attach to tap (" + String ((int) err) + ") - check System Audio Recording permission";
                destroyTap (tap);
                return;
            }

            tap.procId = procId;

            if (auto* bridge = tap.bridge.load())
                bridge->requestReset();

            err = AudioDeviceStart (aggregateId, procId);

            if (err != noErr)
            {
                tap.error = "could not start tap (" + String ((int) err) + ")";
                destroyTap (tap);
                return;
            }

            tap.processes = processes;
        }
    }

    void applyRate (Tap& tap)
    {
        if (targetRate > 0)
        {
            Float64 rate = targetRate;
            auto a = globalAddress (kAudioDevicePropertyNominalSampleRate);
            AudioObjectSetPropertyData (tap.aggregateId, &a, 0, nullptr, sizeof (rate), &rate);
        }

        tap.rate = readValue<Float64> (tap.aggregateId, kAudioDevicePropertyNominalSampleRate, targetRate > 0 ? targetRate : 48000.0);

        if (auto* bridge = tap.bridge.load())
        {
            bridge->setProducerRate (tap.rate);
            bridge->requestReset();
        }
    }

    void destroyTap (Tap& tap)
    {
        if (tap.aggregateId != kAudioObjectUnknown)
        {
            if (tap.procId != nullptr)
            {
                AudioDeviceStop (tap.aggregateId, tap.procId);
                AudioDeviceDestroyIOProcID (tap.aggregateId, tap.procId); // waits for a running callback
                tap.procId = nullptr;
            }

            AudioHardwareDestroyAggregateDevice (tap.aggregateId);
            tap.aggregateId = kAudioObjectUnknown;
        }

        if (tap.tapId != kAudioObjectUnknown)
        {
            AudioHardwareDestroyProcessTap (tap.tapId);
            tap.tapId = kAudioObjectUnknown;
        }

        if (tap.description != nil)
        {
            [tap.description release];
            tap.description = nil;
        }

        tap.processes.clear();
    }

    //==========================================================================
    pid_t ownPid = 0;
    std::set<AudioObjectID> ownObjects;
    std::map<pid_t, String> parentCache;
    std::map<pid_t, bool> parentRegular;
    std::vector<Route> routes;
    std::map<String, std::unique_ptr<Tap>> taps;
    double targetRate = 0.0;
    bool waitingForOwnProcess = false;
};
} // namespace

std::unique_ptr<AppCaptureBackend> AppCaptureBackend::create()
{
    return std::make_unique<MacCaptureBackend>();
}
