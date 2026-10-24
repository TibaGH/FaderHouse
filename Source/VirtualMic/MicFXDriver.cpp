// "Mic FX" - a Core Audio HAL plug-in (AudioServerPlugIn) that is a stereo loopback device.
//
// Faderhouse plays the "Mic Out" mix into the device's output side; Discord, OBS, Zoom, ...
// select "Mic FX" as microphone and read the same audio from its input side.
//
// Built on libASPL (MIT, https://github.com/gavv/libASPL), which implements the
// AudioServerPlugIn C interface and the device clock. Installed by Faderhouse itself into
// /Library/Audio/Plug-Ins/HAL; coreaudiod loads it from there.

#include "LoopbackRing.h"

#include <aspl/Driver.hpp>
#include <CoreAudio/AudioServerPlugIn.h>

namespace
{
constexpr UInt32 kSampleRate = 48000;
constexpr UInt32 kChannels = 2;

AudioStreamBasicDescription floatFormat()
{
    AudioStreamBasicDescription f {};
    f.mSampleRate = kSampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f.mBitsPerChannel = 32;
    f.mChannelsPerFrame = kChannels;
    f.mBytesPerFrame = sizeof (Float32) * kChannels;
    f.mFramesPerPacket = 1;
    f.mBytesPerPacket = f.mBytesPerFrame;
    return f;
}

class LoopbackHandler final : public aspl::ControlRequestHandler,
                              public aspl::IORequestHandler
{
public:
    OSStatus OnStartIO() override
    {
        ring.reset();
        return kAudioHardwareNoError;
    }

    // Everything the output side receives (in practice: Faderhouse's "Mic Out" mix).
    void OnWriteMixedOutput (const std::shared_ptr<aspl::Stream>&, Float64, Float64 timestamp,
                             const void* bytes, UInt32 bytesCount) override
    {
        ring.write ((int64_t) timestamp, static_cast<const float*> (bytes),
                    (int64_t) (bytesCount / (sizeof (Float32) * kChannels)));
    }

    // What Discord / OBS get as microphone signal.
    void OnReadClientInput (const std::shared_ptr<aspl::Client>&, const std::shared_ptr<aspl::Stream>&,
                            Float64, Float64 timestamp, void* bytes, UInt32 bytesCount) override
    {
        ring.read ((int64_t) timestamp, static_cast<float*> (bytes),
                   (int64_t) (bytesCount / (sizeof (Float32) * kChannels)));
    }

private:
    LoopbackRing ring;
};

std::shared_ptr<aspl::Driver> createDriver()
{
    auto context = std::make_shared<aspl::Context>();

    aspl::DeviceParameters deviceParams;
    deviceParams.Name = "Mic FX";
    deviceParams.Manufacturer = "Faderhouse";
    deviceParams.DeviceUID = "com.baloghproductions.faderhouse.micfx";
    deviceParams.ModelUID = "com.baloghproductions.faderhouse.micfx.model";
    deviceParams.SampleRate = kSampleRate;
    deviceParams.ChannelCount = kChannels;
    deviceParams.EnableMixing = true;
    deviceParams.CanBeDefault = true;                // may be picked as default mic
    deviceParams.CanBeDefaultForSystemSounds = false; // never used for alert sounds

    auto device = std::make_shared<aspl::Device> (context, deviceParams);

    aspl::StreamParameters out;
    out.Direction = aspl::Direction::Output;
    out.StartingChannel = 1;
    out.Format = floatFormat();
    device->AddStreamAsync (out);

    aspl::StreamParameters in;
    in.Direction = aspl::Direction::Input;
    in.StartingChannel = 1;
    in.Format = floatFormat();
    device->AddStreamAsync (in);

    auto handler = std::make_shared<LoopbackHandler>();
    device->SetControlHandler (handler);
    device->SetIOHandler (handler);

    auto plugin = std::make_shared<aspl::Plugin> (context);
    plugin->AddDevice (device);

    return std::make_shared<aspl::Driver> (context, plugin);
}
} // namespace

extern "C" void* FaderhouseMicFXEntryPoint (CFAllocatorRef, CFUUIDRef typeUUID)
{
    if (! CFEqual (typeUUID, kAudioServerPlugInTypeUUID))
        return nullptr;

    static std::shared_ptr<aspl::Driver> driver = createDriver();
    return driver->GetReference();
}
