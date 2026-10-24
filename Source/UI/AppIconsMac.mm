// Real app icons and names from macOS (NSWorkspace), for channel icons and the app picker.

#include "ChannelIcons.h"

#import <AppKit/AppKit.h>
#include <map>

namespace
{
struct Entry { juce::Image image; juce::String name; bool looked = false; };
std::map<juce::String, Entry>& cache() { static std::map<juce::String, Entry> c; return c; }

Entry& lookup (const juce::String& appId)
{
    auto& e = cache()[appId];
    if (e.looked)
        return e;

    e.looked = true;
    if (appId.isEmpty() || appId.startsWith ("exe:"))
        return e;

    @autoreleasepool
    {
        NSString* bundleId = [NSString stringWithUTF8String: appId.toRawUTF8()];
        NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: bundleId];

        // Helper / XPC processes (e.g. com.apple.WebKit.GPU) aren't apps: use the running instance if any.
        NSImage* icon = nil;
        if (url != nil)
        {
            icon = [[NSWorkspace sharedWorkspace] iconForFile: [url path]];
            NSString* displayName = [[NSFileManager defaultManager] displayNameAtPath: [url path]];
            if (displayName != nil)
                e.name = juce::String::fromUTF8 ([displayName UTF8String]).upToLastOccurrenceOf (".app", false, true);
        }
        else
        {
            NSArray<NSRunningApplication*>* running = [NSRunningApplication runningApplicationsWithBundleIdentifier: bundleId];
            if (running.count > 0)
            {
                icon = running.firstObject.icon;
                if (running.firstObject.localizedName != nil)
                    e.name = juce::String::fromUTF8 ([running.firstObject.localizedName UTF8String]);
            }
        }

        if (icon == nil)
            return e;

        // Render at 128 px into a bitmap, hand the PNG to JUCE.
        const int px = 128;
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
                                                                        pixelsWide: px
                                                                        pixelsHigh: px
                                                                     bitsPerSample: 8
                                                                   samplesPerPixel: 4
                                                                          hasAlpha: YES
                                                                          isPlanar: NO
                                                                    colorSpaceName: NSCalibratedRGBColorSpace
                                                                       bytesPerRow: 0
                                                                      bitsPerPixel: 0];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext: [NSGraphicsContext graphicsContextWithBitmapImageRep: rep]];
        [icon drawInRect: NSMakeRect (0, 0, px, px) fromRect: NSZeroRect operation: NSCompositingOperationCopy fraction: 1.0];
        [NSGraphicsContext restoreGraphicsState];

        NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
        if (png != nil)
            e.image = juce::ImageFileFormat::loadFrom ([png bytes], (size_t) [png length]);

        [rep release];
    }

    return e;
}
} // namespace

namespace ChannelIcons
{
juce::Image getAppIcon (const juce::String& appId) { return lookup (appId).image; }
juce::String getAppName (const juce::String& appId) { return lookup (appId).name; }
}
