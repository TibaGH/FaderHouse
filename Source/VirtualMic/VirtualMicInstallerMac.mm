// Installs the bundled FaderhouseMicFX.driver into /Library/Audio/Plug-Ins/HAL.
// Writing there needs admin rights, so we use the standard macOS password prompt
// (AppleScript "do shell script ... with administrator privileges").
// Afterwards coreaudiod is restarted so it picks up the new device.

#include "VirtualMicInstaller.h"

#import <Foundation/Foundation.h>
#include <cstdlib>

namespace
{
const char* const kHalDir = "/Library/Audio/Plug-Ins/HAL";
const char* const kDriverName = "FaderhouseMicFX.driver";

juce::File appBundle()
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile);
}

juce::File bundledDriver()
{
    return appBundle().getChildFile ("Contents/Resources").getChildFile (kDriverName);
}

juce::File installedDriver()
{
    return juce::File (kHalDir).getChildFile (kDriverName);
}

juce::String bundleVersion (const juce::File& bundle)
{
    auto plist = bundle.getChildFile ("Contents/Info.plist");
    if (! plist.existsAsFile())
        return {};

    @autoreleasepool
    {
        NSDictionary* dict = [NSDictionary dictionaryWithContentsOfFile: [NSString stringWithUTF8String: plist.getFullPathName().toRawUTF8()]];
        id version = [dict objectForKey: @"CFBundleVersion"];
        if ([version isKindOfClass: [NSString class]])
            return juce::String::fromUTF8 ([(NSString*) version UTF8String]);
    }
    return {};
}

juce::String shellQuote (const juce::String& s)
{
    return "'" + s.replace ("'", "'\\''") + "'";
}

/** Runs a shell command as root after the macOS admin prompt. Returns an error or "". */
juce::String runAsAdmin (const juce::String& command, const juce::String& prompt)
{
    // Escape for an AppleScript string literal.
    const auto escaped = command.replace ("\\", "\\\\").replace ("\"", "\\\"");
    const auto source = "do shell script \"" + escaped + "\" with prompt \"" + prompt + "\" with administrator privileges";

    @autoreleasepool
    {
        NSAppleScript* script = [[NSAppleScript alloc] initWithSource: [NSString stringWithUTF8String: source.toRawUTF8()]];
        NSDictionary* errorInfo = nil;
        NSAppleEventDescriptor* result = [script executeAndReturnError: &errorInfo];
        [script release];

        if (result != nil)
            return {};

        NSNumber* code = [errorInfo objectForKey: NSAppleScriptErrorNumber];
        if (code != nil && [code intValue] == -128)
            return "Cancelled.";

        NSString* message = [errorInfo objectForKey: NSAppleScriptErrorMessage];
        return message != nil ? juce::String::fromUTF8 ([message UTF8String]) : juce::String ("Unknown error");
    }
}

// coreaudiod is restarted by launchd automatically after it is stopped.
const char* const kRestartAudio = "(killall coreaudiod || launchctl kickstart -k system/com.apple.audio.coreaudiod || true)";
} // namespace

namespace VirtualMic
{
State getState()
{
    if (! bundledDriver().isDirectory())
        return State::NotBundled;

    if (! installedDriver().isDirectory())
        return State::NotInstalled;

    return getInstalledVersion() == getBundledVersion() ? State::Installed : State::UpdateAvailable;
}

juce::String getInstalledVersion() { return bundleVersion (installedDriver()); }
juce::String getBundledVersion()   { return bundleVersion (bundledDriver()); }

juce::String install()
{
    const auto src = bundledDriver();
    if (! src.isDirectory())
        return "FaderhouseMicFX.driver is missing from the app bundle - rebuild Faderhouse.";

    const auto dst = installedDriver();

    juce::String cmd;
    cmd << "mkdir -p " << shellQuote (kHalDir)
        << " && rm -rf " << shellQuote (juce::String (kHalDir) + "/MixwerkMic.driver")   // early test builds
        << " && rm -rf " << shellQuote (dst.getFullPathName())
        << " && cp -R " << shellQuote (src.getFullPathName()) << " " << shellQuote (dst.getFullPathName())
        << " && chown -R root:wheel " << shellQuote (dst.getFullPathName())
        << " && chmod -R go-w " << shellQuote (dst.getFullPathName())
        << " && (xattr -dr com.apple.quarantine " << shellQuote (dst.getFullPathName()) << " || true)"
        << " && " << kRestartAudio;

    return runAsAdmin (cmd, "Faderhouse wants to install its virtual microphone \\\"Mic FX\\\".");
}

juce::String uninstall()
{
    if (! installedDriver().isDirectory())
        return {};

    juce::String cmd;
    cmd << "rm -rf " << shellQuote (installedDriver().getFullPathName()) << " && " << kRestartAudio;
    return runAsAdmin (cmd, "Faderhouse wants to remove its virtual microphone.");
}

void scheduleRelaunch()
{
    // Detached shell: waits for this instance to quit, then opens the app again.
    const auto cmd = "(sleep 2; /usr/bin/open " + shellQuote (appBundle().getFullPathName()) + ") >/dev/null 2>&1 &";
    std::system (cmd.toRawUTF8());
}
}
