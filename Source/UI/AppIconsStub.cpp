// App icons aren't available on this platform yet (Windows: phase 3).
#include "ChannelIcons.h"

namespace ChannelIcons
{
juce::Image getAppIcon (const juce::String&)  { return {}; }
juce::String getAppName (const juce::String&) { return {}; }
}
