// Saving / loading the mixer layout (channels, app mappings, faders, plugin chains with their state).

#include "MixerEngine.h"

using namespace juce;

namespace
{
String kindToString (SourceKind k)
{
    switch (k)
    {
        case SourceKind::Input:  return "input";
        case SourceKind::App:    return "app";
        case SourceKind::System: return "system";
    }
    return "app";
}

SourceKind kindFromString (const String& s)
{
    if (s == "input")  return SourceKind::Input;
    if (s == "system") return SourceKind::System;
    return SourceKind::App;
}
} // namespace

File MixerEngine::getSessionFile() const
{
    return settings->getFile().getSiblingFile ("session.xml");
}

std::unique_ptr<XmlElement> MixerEngine::createSessionXml() const
{
    auto xml = std::make_unique<XmlElement> ("FaderhouseSession");
    xml->setAttribute ("version", 2);

    auto* mic = xml->createNewChildElement ("MicFx");
    mic->setAttribute ("deviceType", micFx.deviceType);
    mic->setAttribute ("deviceName", micFx.deviceName);
    mic->setAttribute ("muted", micMuted.load());

    auto* srcs = xml->createNewChildElement ("Sources");
    for (auto& s : sources)
    {
        auto* e = srcs->createNewChildElement ("Source");
        e->setAttribute ("id", s->id);
        e->setAttribute ("kind", kindToString (s->kind));
        e->setAttribute ("name", s->name);
        e->setAttribute ("inputChannel", s->inputChannel.load());
        e->setAttribute ("stereoInput", s->stereoInput.load());
        e->setAttribute ("trim", (double) s->inputGain.load());
        e->setAttribute ("mute", s->mute.load());
        e->setAttribute ("toMicFx", s->toMicFx.load());
        e->setAttribute ("icon", s->icon);

        auto* apps = e->createNewChildElement ("Apps");
        for (auto& id : s->appIds)
            apps->createNewChildElement ("App")->setAttribute ("id", id);

        auto* sends = e->createNewChildElement ("Sends");
        for (int i = 0; i < kMaxOutputs; ++i)
        {
            auto* se = sends->createNewChildElement ("Send");
            se->setAttribute ("slot", i);
            se->setAttribute ("level", (double) s->send[(size_t) i].load());
            se->setAttribute ("mute", s->sendMute[(size_t) i].load());
            se->setAttribute ("solo", s->sendSolo[(size_t) i].load());
        }

        e->addChildElement (s->chain.createXml().release());
    }

    auto* outs = xml->createNewChildElement ("Outputs");
    for (auto& o : outputs)
    {
        auto* e = outs->createNewChildElement ("Output");
        e->setAttribute ("slot", o->slot);
        e->setAttribute ("name", o->name);
        e->setAttribute ("target", o->target.load());
        e->setAttribute ("mainChannelOffset", o->mainChannelOffset.load());
        e->setAttribute ("deviceType", o->deviceType);
        e->setAttribute ("deviceName", o->deviceName);
        e->setAttribute ("level", (double) o->level.load());
        e->setAttribute ("mute", o->mute.load());
        e->addChildElement (o->chain.createXml().release());
    }

    return xml;
}

void MixerEngine::saveSession()
{
    if (auto xml = createSessionXml())
    {
        auto file = getSessionFile();
        file.getParentDirectory().createDirectory();
        xml->writeTo (file);
    }

    settings->saveIfNeeded();
}

void MixerEngine::clearSession()
{
    if (capture != nullptr)
        capture->setRoutes ({});

    std::vector<std::unique_ptr<Source>> oldSources;
    std::vector<std::unique_ptr<Output>> oldOutputs;

    {
        const GenericScopedLock<SpinLock> sl (stateLock);
        oldSources.swap (sources);
        oldOutputs.swap (outputs);
    }

    for (auto& s : oldSources)
        for (int i = s->chain.size(); --i >= 0;)
            removePlugin (s->chain, i);

    for (auto& o : oldOutputs)
        for (int i = o->chain.size(); --i >= 0;)
            removePlugin (o->chain, i);
}

void MixerEngine::restoreSession (const XmlElement& xml)
{
    clearSession();
    lastLoadErrors.clear();

    String micType, micName;
    if (auto* mic = xml.getChildByName ("MicFx"))
    {
        micType = mic->getStringAttribute ("deviceType");
        micName = mic->getStringAttribute ("deviceName");
        micMuted.store (mic->getBoolAttribute ("muted"));
    }

    // Outputs first, so that slots exist when the sources' sends are applied.
    if (auto* outs = xml.getChildByName ("Outputs"))
    {
        for (auto* e : outs->getChildWithTagNameIterator ("Output"))
        {
            const int slot = e->getIntAttribute ("slot", -1);
            if (! isPositiveAndBelow (slot, kMaxOutputs) || findOutputBySlot (slot) != nullptr)
                continue;

            // Sessions from v0.1 had "Mic Out" as a normal mix; it is now the fixed Mic FX direct out.
            if (e->getStringAttribute ("name").equalsIgnoreCase ("Mic Out"))
            {
                if (micName.isEmpty() && (Output::Target) e->getIntAttribute ("target", 0) == Output::Target::Device)
                {
                    micType = e->getStringAttribute ("deviceType");
                    micName = e->getStringAttribute ("deviceName");
                }
                continue;
            }

            auto o = std::make_unique<Output> (slot, e->getStringAttribute ("name", "Output"));
            o->level.store ((float) e->getDoubleAttribute ("level", 1.0));
            o->mute.store (e->getBoolAttribute ("mute"));
            prepareOutput (*o);

            if (auto* chainXml = e->getChildByName ("Chain"))
                o->chain.restoreFromXml (*chainXml, formatManager, lastLoadErrors);

            auto* raw = o.get();
            {
                const GenericScopedLock<SpinLock> sl (stateLock);
                outputs.push_back (std::move (o));
            }

            switch ((Output::Target) e->getIntAttribute ("target", 0))
            {
                case Output::Target::Main:
                    setOutputToMain (*raw, e->getIntAttribute ("mainChannelOffset", 0));
                    break;

                case Output::Target::Device:
                    setOutputToDevice (*raw, e->getStringAttribute ("deviceType"), e->getStringAttribute ("deviceName"));
                    break;

                case Output::Target::None:
                    setOutputToNone (*raw);
                    break;
            }
        }
    }

    if (auto* srcs = xml.getChildByName ("Sources"))
    {
        for (auto* e : srcs->getChildWithTagNameIterator ("Source"))
        {
            auto s = std::make_unique<Source> (kindFromString (e->getStringAttribute ("kind")),
                                               e->getStringAttribute ("name", "Channel"));

            if (e->getStringAttribute ("id").isNotEmpty())
                s->id = e->getStringAttribute ("id");

            s->inputChannel.store (e->getIntAttribute ("inputChannel", 0));
            s->stereoInput.store (e->getBoolAttribute ("stereoInput"));
            s->inputGain.store ((float) e->getDoubleAttribute ("trim", 1.0));
            s->mute.store (e->getBoolAttribute ("mute"));
            s->toMicFx.store (e->getBoolAttribute ("toMicFx", s->kind == SourceKind::Input));
            s->icon = e->getStringAttribute ("icon");

            if (auto* apps = e->getChildByName ("Apps"))
                for (auto* a : apps->getChildWithTagNameIterator ("App"))
                    s->appIds.addIfNotAlreadyThere (a->getStringAttribute ("id"));

            if (auto* sends = e->getChildByName ("Sends"))
            {
                for (auto* se : sends->getChildWithTagNameIterator ("Send"))
                {
                    const int slot = se->getIntAttribute ("slot", -1);
                    if (isPositiveAndBelow (slot, kMaxOutputs))
                    {
                        s->send[(size_t) slot].store ((float) se->getDoubleAttribute ("level", 0.0));
                        s->sendMute[(size_t) slot].store (se->getBoolAttribute ("mute"));
                        s->sendSolo[(size_t) slot].store (se->getBoolAttribute ("solo"));
                    }
                }
            }

            prepareSource (*s);

            if (auto* chainXml = e->getChildByName ("Chain"))
                s->chain.restoreFromXml (*chainXml, formatManager, lastLoadErrors);

            const GenericScopedLock<SpinLock> sl (stateLock);
            sources.push_back (std::move (s));
        }
    }

    micFx.deviceType = micType;
    micFx.deviceName = micName;
    if (micName.isNotEmpty())
        setMicFxDevice (micType, micName);

    pushRoutes();
    structureChanged();
}

void MixerEngine::loadSession()
{
    auto file = getSessionFile();

    if (file.existsAsFile())
    {
        if (auto xml = parseXML (file))
        {
            if (xml->hasTagName ("FaderhouseSession"))
            {
                restoreSession (*xml);

                if (! sources.empty() || ! outputs.empty())
                    return;
            }
        }
    }

    createDefaultSession();
}

void MixerEngine::createDefaultSession()
{
    clearSession();

    // The mix you hear (interface outputs 1/2). The processed mic goes to Mic FX separately.
    if (auto* monitor = addOutput ("Monitor"))
        setOutputToMain (*monitor, 0);

    if (auto* mic = addSource (SourceKind::Input, "Mic"))
    {
        mic->toMicFx.store (true);
        mic->icon = "mic";
    }

   #if JUCE_MAC
    if (auto* discord = addSource (SourceKind::App, "Discord"))
        setAppIds (*discord, { "com.hnc.Discord" });

    if (auto* browser = addSource (SourceKind::App, "Browser"))
        setAppIds (*browser, { "com.google.Chrome", "com.apple.WebKit.GPU", "org.mozilla.firefox", "company.thebrowser.Browser", "com.brave.Browser" });

    if (auto* music = addSource (SourceKind::App, "Music"))
        setAppIds (*music, { "com.tidal.desktop", "com.spotify.client", "com.apple.Music" });

    if (auto* game = addSource (SourceKind::App, "Game"))
        game->icon = "game";
   #else
    if (auto* discord = addSource (SourceKind::App, "Discord"))
        setAppIds (*discord, { "exe:Discord.exe" });

    if (auto* browser = addSource (SourceKind::App, "Browser"))
        setAppIds (*browser, { "exe:chrome.exe", "exe:msedge.exe", "exe:firefox.exe" });

    if (auto* game = addSource (SourceKind::App, "Game"))
        game->icon = "game";
   #endif

    if (auto* system = addSource (SourceKind::System, "System"))
        system->icon = "desktop";

    // You don't hear your own mic by default; raise its Monitor fader to taste.
    for (auto& s : sources)
        s->send[0].store (s->kind == SourceKind::Input ? 0.0f : 1.0f);
}
