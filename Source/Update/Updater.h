#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <functional>
#include <memory>

/**
    In-app updates from GitHub Releases (github.com/TibaGH/faderhouse).

    - check(): asks the GitHub API for the latest release and compares its version tag
      (v1.2.3) with the running app.
    - downloadAndInstall(): downloads the installer for this platform (macOS .pkg, Windows
      -Setup.exe), verifies it against SHA256SUMS.txt from the same release, starts it and
      quits so the app can be replaced.

    All network work happens on a background thread; callbacks arrive on the message thread.
*/
class Updater final : private juce::Timer
{
public:
    struct Release
    {
        juce::String version;      // "0.4.0"
        juce::String notes;        // release notes (markdown)
        juce::String pageUrl;      // release page on GitHub
        juce::String packageName;  // Faderhouse-0.4.0.pkg / Faderhouse-0.4.0-Setup.exe
        juce::String packageUrl;
        juce::String checksumsUrl; // SHA256SUMS.txt
        juce::int64 packageSize = 0;
    };

    static constexpr const char* repository = "TibaGH/faderhouse";

    explicit Updater (juce::PropertiesFile& settings);
    ~Updater() override;

    /** Starts a check. If userInitiated, onNoUpdate/onError are called too (otherwise silent). */
    void check (bool userInitiated);

    std::function<void (const Release&)> onUpdateAvailable;
    std::function<void (const juce::String& message)> onNoUpdate;
    std::function<void (const juce::String& message)> onError;

    /** Downloads, verifies and opens the installer. progress is 0..1. done(error): empty on success. */
    void downloadAndInstall (const Release& release,
                             std::function<void (double progress)> progress,
                             std::function<void (const juce::String& error)> done);

    void cancelDownload()                      { cancel->store (true); }

    bool isAutoCheckEnabled() const;
    void setAutoCheckEnabled (bool);
    void skipVersion (const juce::String& version);

    static juce::String getCurrentVersion();

    /** <0 if a is older than b, 0 if equal, >0 if newer. "v" prefixes are ignored. */
    static int compareVersions (const juce::String& a, const juce::String& b);

private:
    void timerCallback() override;

    juce::PropertiesFile& settings;
    std::shared_ptr<std::atomic<bool>> alive;
    std::atomic<bool> busy { false };
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>> (false);
};
