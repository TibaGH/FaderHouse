#include "BuiltinPlugins.h"
#include "FhPlugin.h"

using namespace juce;

namespace fh::builtin
{
    AudioPluginInstance* createVoice();
    AudioPluginInstance* createEQ();
    AudioPluginInstance* createCompressor();
    AudioPluginInstance* createLimiter();
    AudioPluginInstance* createGate();
    AudioPluginInstance* createDenoise();
}

namespace
{
struct Entry { const char* name; const char* category; AudioPluginInstance* (*create)(); };

const Entry entries[] {
    { "Faderhouse Voice",      "Voice",       fh::builtin::createVoice },
    { "Faderhouse EQ",         "EQ",          fh::builtin::createEQ },
    { "Faderhouse Compressor", "Dynamics",    fh::builtin::createCompressor },
    { "Faderhouse Limiter",    "Dynamics",    fh::builtin::createLimiter },
    { "Faderhouse Gate",       "Dynamics",    fh::builtin::createGate },
    { "Faderhouse Denoise",    "Restoration", fh::builtin::createDenoise },
};

const Entry* find (const String& identifier)
{
    for (auto& e : entries)
        if (identifier == e.name)
            return &e;
    return nullptr;
}

class BuiltinFormat final : public AudioPluginFormat
{
public:
    String getName() const override { return fh::PluginBase::formatName; }

    void findAllTypesForFile (OwnedArray<PluginDescription>& results, const String& identifier) override
    {
        if (auto* e = find (identifier))
            results.add (new PluginDescription (fh::PluginBase::describe (e->name, e->category)));
    }

    bool fileMightContainThisPluginType (const String& identifier) override        { return find (identifier) != nullptr; }
    String getNameOfPluginFromIdentifier (const String& identifier) override        { return identifier; }
    bool pluginNeedsRescanning (const PluginDescription&) override                  { return false; }
    bool doesPluginStillExist (const PluginDescription& d) override                 { return find (d.fileOrIdentifier) != nullptr; }
    bool canScanForPlugins() const override                                         { return false; }
    bool isTrivialToScan() const override                                           { return true; }
    FileSearchPath getDefaultLocationsToSearch() override                           { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const PluginDescription&) const override { return false; }

    StringArray searchPathsForPlugins (const FileSearchPath&, bool, bool) override
    {
        StringArray names;
        for (auto& e : entries) names.add (e.name);
        return names;
    }

protected:
    void createPluginInstance (const PluginDescription& d, double sampleRate, int blockSize, PluginCreationCallback callback) override
    {
        if (auto* e = find (d.fileOrIdentifier))
        {
            std::unique_ptr<AudioPluginInstance> instance (e->create());
            instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
            callback (std::move (instance), {});
            return;
        }

        callback (nullptr, "Unknown Faderhouse plugin: " + d.fileOrIdentifier);
    }
};
}

namespace BuiltinPlugins
{
void addFormat (AudioPluginFormatManager& manager)
{
    manager.addFormat (std::make_unique<BuiltinFormat>());
}

Array<PluginDescription> getTypes()
{
    Array<PluginDescription> types;
    for (auto& e : entries)
        types.add (fh::PluginBase::describe (e.name, e.category));
    return types;
}

PluginDescription getVoiceStrip()
{
    return fh::PluginBase::describe (entries[0].name, entries[0].category);
}
}
