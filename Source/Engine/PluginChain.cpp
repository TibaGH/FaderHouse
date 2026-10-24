#include "PluginChain.h"

using namespace juce;

PluginChain::PluginChain()
{
    slots.reserve (maxSlots);
    midi.ensureSize (256);
}

PluginChain::~PluginChain()
{
    clear();
}

void PluginChain::prepareSlot (Slot& slot)
{
    auto& p = *slot.plugin;

    // Ask for stereo on the main buses; leave side-chains etc. alone.
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.size() > 0)  layout.inputBuses.getReference (0)  = AudioChannelSet::stereo();
    if (layout.outputBuses.size() > 0) layout.outputBuses.getReference (0) = AudioChannelSet::stereo();
    p.setBusesLayout (layout); // if the plugin refuses, we adapt to whatever it has below

    p.setPlayHead (nullptr);
    p.setNonRealtime (false);
    p.prepareToPlay (currentSampleRate, currentBlockSize);

    slot.numIns  = p.getTotalNumInputChannels();
    slot.numOuts = p.getTotalNumOutputChannels();
    slot.io.setSize (jmax (2, slot.numIns, slot.numOuts), currentBlockSize, false, true, false);
}

void PluginChain::prepare (double sampleRate, int maxBlockSize)
{
    const GenericScopedLock<SpinLock> sl (lock);

    currentSampleRate = sampleRate;
    currentBlockSize = jmax (32, maxBlockSize);

    for (auto& s : slots)
        prepareSlot (*s);

    prepared = true;
}

void PluginChain::releaseResources()
{
    const GenericScopedLock<SpinLock> sl (lock);

    for (auto& s : slots)
        s->plugin->releaseResources();

    prepared = false;
}

int PluginChain::add (std::unique_ptr<AudioPluginInstance> plugin)
{
    if (plugin == nullptr || (int) slots.size() >= maxSlots)
        return -1;

    auto slot = std::make_unique<Slot>();
    slot->plugin = std::move (plugin);

    // Prepare outside the lock so the audio thread keeps running while a heavy plugin initialises.
    prepareSlot (*slot);

    const GenericScopedLock<SpinLock> sl (lock);
    slots.push_back (std::move (slot));
    return (int) slots.size() - 1;
}

std::unique_ptr<AudioPluginInstance> PluginChain::remove (int index)
{
    std::unique_ptr<Slot> removed;

    {
        const GenericScopedLock<SpinLock> sl (lock);

        if (! isPositiveAndBelow (index, (int) slots.size()))
            return {};

        removed = std::move (slots[(size_t) index]);
        slots.erase (slots.begin() + index);
    }

    removed->plugin->releaseResources();
    return std::move (removed->plugin);
}

void PluginChain::move (int from, int to)
{
    const GenericScopedLock<SpinLock> sl (lock);

    if (! isPositiveAndBelow (from, (int) slots.size()) || ! isPositiveAndBelow (to, (int) slots.size()) || from == to)
        return;

    auto s = std::move (slots[(size_t) from]);
    slots.erase (slots.begin() + from);
    slots.insert (slots.begin() + to, std::move (s));
}

void PluginChain::clear()
{
    while (size() > 0)
        remove (size() - 1);
}

AudioPluginInstance* PluginChain::get (int index) const noexcept
{
    return isPositiveAndBelow (index, (int) slots.size()) ? slots[(size_t) index]->plugin.get() : nullptr;
}

bool PluginChain::isBypassed (int index) const noexcept
{
    return isPositiveAndBelow (index, (int) slots.size()) && slots[(size_t) index]->bypass.load();
}

void PluginChain::setBypassed (int index, bool shouldBypass) noexcept
{
    if (isPositiveAndBelow (index, (int) slots.size()))
        slots[(size_t) index]->bypass.store (shouldBypass);
}

int PluginChain::getTotalLatencySamples() const
{
    int total = 0;
    for (auto& s : slots)
        if (! s->bypass.load())
            total += s->plugin->getLatencySamples();
    return total;
}

void PluginChain::process (AudioBuffer<float>& buffer, int numSamples) noexcept
{
    const GenericScopedTryLock<SpinLock> sl (lock);

    if (! sl.isLocked() || ! prepared || slots.empty() || buffer.getNumChannels() < 2)
        return;

    for (auto& s : slots)
    {
        if (s->bypass.load (std::memory_order_relaxed) || numSamples > s->io.getNumSamples())
            continue;

        auto& io = s->io;

        // Feed the plugin's main input.
        if (s->numIns == 1)
        {
            io.copyFrom (0, 0, buffer, 0, 0, numSamples);
            io.addFrom  (0, 0, buffer, 1, 0, numSamples);
            io.applyGain (0, 0, numSamples, 0.5f);
            for (int ch = 1; ch < io.getNumChannels(); ++ch)
                io.clear (ch, 0, numSamples);
        }
        else
        {
            io.copyFrom (0, 0, buffer, 0, 0, numSamples);
            io.copyFrom (1, 0, buffer, 1, 0, numSamples);
            for (int ch = 2; ch < io.getNumChannels(); ++ch)
                io.clear (ch, 0, numSamples);
        }

        AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), numSamples);
        midi.clear();
        s->plugin->processBlock (view, midi);

        if (s->numOuts >= 2)
        {
            buffer.copyFrom (0, 0, io, 0, 0, numSamples);
            buffer.copyFrom (1, 0, io, 1, 0, numSamples);
        }
        else if (s->numOuts == 1)
        {
            buffer.copyFrom (0, 0, io, 0, 0, numSamples);
            buffer.copyFrom (1, 0, io, 0, 0, numSamples);
        }
    }
}

//==============================================================================
std::unique_ptr<XmlElement> PluginChain::createXml() const
{
    auto xml = std::make_unique<XmlElement> ("Chain");

    for (auto& s : slots)
    {
        auto* p = xml->createNewChildElement ("Plugin");
        p->setAttribute ("bypass", s->bypass.load());
        p->addChildElement (s->plugin->getPluginDescription().createXml().release());

        MemoryBlock state;
        s->plugin->getStateInformation (state);
        p->createNewChildElement ("State")->addTextElement (state.toBase64Encoding());
    }

    return xml;
}

void PluginChain::restoreFromXml (const XmlElement& xml, AudioPluginFormatManager& formats, StringArray& errors)
{
    clear();

    for (auto* p : xml.getChildWithTagNameIterator ("Plugin"))
    {
        auto* descXml = p->getChildByName ("PLUGIN");
        if (descXml == nullptr)
            continue;

        PluginDescription desc;
        if (! desc.loadFromXml (*descXml))
            continue;

        String error;
        auto instance = formats.createPluginInstance (desc, currentSampleRate, currentBlockSize, error);

        if (instance == nullptr)
        {
            errors.add (desc.name + ": " + error);
            continue;
        }

        if (auto* stateXml = p->getChildByName ("State"))
        {
            MemoryBlock state;
            if (state.fromBase64Encoding (stateXml->getAllSubText()) && state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());
        }

        const int index = add (std::move (instance));
        setBypassed (index, p->getBoolAttribute ("bypass"));
    }
}
