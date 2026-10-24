#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

class ClockBridge;

/** A process that is currently a client of the system audio engine. */
struct AudioProcessInfo
{
    juce::String appId;        // bundle id on macOS ("com.hnc.Discord"), "exe:<name>" otherwise
    juce::String displayName;  // human readable ("Discord")
    int pid = 0;
    bool isOutputting = false;  // currently playing audio
    bool isApp = false;         // a real app (or its helper) rather than a system service
};

/**
    Platform layer that captures the audio of individual apps and delivers it
    into ClockBridges, while silencing the app's normal output.

    macOS: Core Audio process taps (AppCaptureMac.mm)
    Windows: WASAPI process loopback (to do, phase 3)
*/
class AppCaptureBackend
{
public:
    struct Route
    {
        juce::String sourceId;      // stable id of the mixer channel
        juce::StringArray appIds;   // apps mapped to this channel (ignored when isSystem)
        bool isSystem = false;      // "everything not mapped elsewhere"
        ClockBridge* bridge = nullptr;
    };

    virtual ~AppCaptureBackend() = default;

    static std::unique_ptr<AppCaptureBackend> create();

    /** False when the OS doesn't support per-app capture (or not implemented yet). */
    virtual bool isSupported() const = 0;

    /** One line for the status bar, e.g. "3 apps captured" or an error. */
    virtual juce::String getStatusText() const = 0;

    /** Running audio clients, for the "assign apps" picker. Message thread. */
    virtual std::vector<AudioProcessInfo> listAudioProcesses() = 0;

    /** Replaces the full routing table. Message thread. Diffed internally. */
    virtual void setRoutes (const std::vector<Route>& routes) = 0;

    /** Sample rate the mixer runs at; capture is delivered as close to it as possible. */
    virtual void setTargetSampleRate (double sampleRate) = 0;

    /** Stops all captures; apps go back to playing normally. Message thread. */
    virtual void stopAll() = 0;

    /** True if `processAppId` belongs to the app the user picked (same id or a helper/sub-bundle of it). */
    static bool appIdMatches (const juce::String& processAppId, const juce::String& routeAppId)
    {
        if (processAppId.isEmpty() || routeAppId.isEmpty())
            return false;

        return processAppId.equalsIgnoreCase (routeAppId)
            || processAppId.startsWithIgnoreCase (routeAppId + ".");
    }

    /** Strips helper suffixes so "com.google.Chrome.helper.Renderer" groups under "com.google.Chrome". */
    static juce::String rootAppId (const juce::String& appId)
    {
        const int helper = appId.indexOfIgnoreCase (".helper");
        return helper > 0 ? appId.substring (0, helper) : appId;
    }
};
