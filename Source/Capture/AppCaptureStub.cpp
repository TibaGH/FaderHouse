// Placeholder backend for platforms where per-app capture isn't implemented yet.
// The rest of the mixer (inputs, plugins, outputs, virtual mic device) still works.
// Windows (phase 3): WASAPI process loopback, see README.

#include "AppCapture.h"

namespace
{
class StubCaptureBackend final : public AppCaptureBackend
{
public:
    bool isSupported() const override                         { return false; }
    juce::String getStatusText() const override               { return "Per-app capture not available on this platform yet"; }
    std::vector<AudioProcessInfo> listAudioProcesses() override { return {}; }
    void setRoutes (const std::vector<Route>&) override       {}
    void setTargetSampleRate (double) override                {}
    void stopAll() override                                   {}
};
}

std::unique_ptr<AppCaptureBackend> AppCaptureBackend::create()
{
    return std::make_unique<StubCaptureBackend>();
}
