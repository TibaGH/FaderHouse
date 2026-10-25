#include "LaunchAtLogin.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace LaunchAtLogin
{
bool isSupported() { return true; }

bool isEnabled()
{
    return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
}

juce::String setEnabled (bool enabled)
{
    SMAppService* service = [SMAppService mainAppService];
    NSError* error = nil;
    const BOOL ok = enabled ? [service registerAndReturnError: &error]
                            : [service unregisterAndReturnError: &error];

    if (enabled && service.status == SMAppServiceStatusRequiresApproval)
    {
        [SMAppService openSystemSettingsLoginItems];
        return "macOS wants you to allow this once: switch Faderhouse on under System Settings > General > Login Items.";
    }

    if (! ok && error != nil)
        return juce::String::fromUTF8 (error.localizedDescription.UTF8String);

    return {};
}
}
