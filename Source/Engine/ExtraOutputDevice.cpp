#include "ExtraOutputDevice.h"

using namespace juce;

String ExtraOutputDevice::open (AudioDeviceManager& manager, const String& typeName, const String& deviceName,
                                double preferredSampleRate, int mainBlockSize)
{
    close();
    mainBlock = jmax (32, mainBlockSize);

    AudioIODeviceType* type = nullptr;
    for (auto* t : manager.getAvailableDeviceTypes())
        if (t->getTypeName() == typeName)
            type = t;

    if (type == nullptr)
        return "Device type not available: " + typeName;

    type->scanForDevices();

    if (! type->getDeviceNames (false).contains (deviceName))
        return "Device not found: " + deviceName;

    device.reset (type->createDevice (deviceName, {}));

    if (device == nullptr)
        return "Could not create device: " + deviceName;

    // Prefer the mixer's sample rate so no real resampling happens, and a small buffer for low latency.
    double rate = preferredSampleRate;
    const auto rates = device->getAvailableSampleRates();
    if (! rates.contains (rate) && ! rates.isEmpty())
        rate = rates.contains (48000.0) ? 48000.0 : rates[0];

    int bufferSize = 256;
    const auto sizes = device->getAvailableBufferSizes();
    if (! sizes.isEmpty() && ! sizes.contains (bufferSize))
    {
        bufferSize = sizes[0];
        for (auto s : sizes)
            if (s >= 128 && s <= 512) { bufferSize = s; break; }
    }

    BigInteger outs;
    outs.setRange (0, jmin (2, device->getOutputChannelNames().size()), true);

    const auto error = device->open ({}, outs, rate, bufferSize);
    if (error.isNotEmpty())
    {
        device.reset();
        return error;
    }

    bridge.setProducerRate (preferredSampleRate);
    bridge.setConsumerRate (device->getCurrentSampleRate());
    updateTarget();
    bridge.requestReset();

    device->start (this);
    return {};
}

void ExtraOutputDevice::close()
{
    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }
}

String ExtraOutputDevice::getDescription() const
{
    if (device == nullptr)
        return "not open";

    return device->getName() + " - " + String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz / "
         + String (device->getCurrentBufferSizeSamples()) + " smp";
}

void ExtraOutputDevice::setMainFormat (double sampleRate, int blockSize)
{
    mainBlock = jmax (32, blockSize);
    bridge.setProducerRate (sampleRate);
    updateTarget();
    bridge.requestReset();
}

void ExtraOutputDevice::updateTarget()
{
    const int deviceBlock = device != nullptr ? device->getCurrentBufferSizeSamples() : 256;
    bridge.setTargetFill (jmax (256, 2 * jmax (mainBlock, deviceBlock)));
}

void ExtraOutputDevice::audioDeviceAboutToStart (AudioIODevice* d)
{
    scratchR.assign ((size_t) jmax (4096, d->getCurrentBufferSizeSamples() * 2), 0.0f);
    bridge.setConsumerRate (d->getCurrentSampleRate());
    updateTarget();
}

void ExtraOutputDevice::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                          int numSamples, const AudioIODeviceCallbackContext&)
{
    if (numOutputs <= 0)
        return;

    float* left = outputs[0];
    float* right = numOutputs > 1 ? outputs[1] : (numSamples <= (int) scratchR.size() ? scratchR.data() : nullptr);

    bridge.pull (left, right, numSamples);

    for (int ch = 2; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            FloatVectorOperations::clear (outputs[ch], numSamples);
}
