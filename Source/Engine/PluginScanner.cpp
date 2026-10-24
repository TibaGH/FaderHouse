// Out-of-process plugin scanning, adapted from the JUCE AudioPluginHost (JUCE licence).

#include "PluginScanner.h"

#include <condition_variable>
#include <mutex>
#include <queue>

using namespace juce;

namespace
{
constexpr const char* processUID = "faderhouse-plugin-scanner";

//==============================================================================
/** Main app side: owns one helper process and talks to it. */
class Superprocess final : private ChildProcessCoordinator
{
public:
    Superprocess()
    {
        launchWorkerProcess (File::getSpecialLocation (File::currentExecutableFile), processUID, 0, 0);
    }

    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state;
        std::unique_ptr<XmlElement> xml;
    };

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 }, [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult = false;

        return { state, std::move (pluginDescription) };
    }

    using ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        pluginDescription = parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<XmlElement> pluginDescription;
    bool connectionLost = false;
    bool gotResult = false;
};

//==============================================================================
class OutOfProcessScanner final : public KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (AudioPluginFormat& format, OwnedArray<PluginDescription>& result,
                             const String& fileOrIdentifier) override
    {
        const bool ok = addPluginDescriptions (format.getName(), fileOrIdentifier, result);

        // One helper per plugin: some plugins crash a moment *after* being scanned (from a
        // leftover timer or callback). A fresh helper makes sure such a crash is never blamed
        // on the next plugin. If the helper died on this one, the plugin is marked as failed.
        superprocess = nullptr;
        return ok;
    }

    void scanFinished() override
    {
        superprocess = nullptr;
    }

private:
    bool addPluginDescriptions (const String& formatName, const String& fileOrIdentifier,
                                OwnedArray<PluginDescription>& result)
    {
        if (superprocess == nullptr)
            superprocess = std::make_unique<Superprocess>();

        MemoryBlock block;
        MemoryOutputStream stream { block, true };
        stream.writeString (formatName);
        stream.writeString (fileOrIdentifier);

        if (! superprocess->sendMessageToWorker (block))
            return false;

        for (;;)
        {
            if (shouldExit())
                return true;

            const auto response = superprocess->getResponse();

            if (response.state == Superprocess::State::timeout)
                continue;

            if (response.xml != nullptr)
            {
                for (const auto* item : response.xml->getChildIterator())
                {
                    auto desc = std::make_unique<PluginDescription>();
                    if (desc->loadFromXml (*item))
                        result.add (std::move (desc));
                }
            }

            return response.state == Superprocess::State::gotResult;
        }
    }

    std::unique_ptr<Superprocess> superprocess;
};

//==============================================================================
/** Helper process side: loads one plugin at a time and reports what it found. */
class ScannerSubprocess final : private ChildProcessWorker,
                                private AsyncUpdater
{
public:
    ScannerSubprocess()
    {
        addDefaultFormatsToManager (formatManager);
    }

    using ChildProcessWorker::initialiseFromCommandLine;

private:
    void handleMessageFromCoordinator (const MemoryBlock& mb) override
    {
        if (mb.isEmpty())
            return;

        const std::lock_guard<std::mutex> lock (mutex);

        if (const auto results = doScan (mb); ! results.isEmpty())
        {
            sendResults (results);
        }
        else
        {
            pendingBlocks.emplace (mb);
            triggerAsyncUpdate();
        }
    }

    void handleConnectionLost() override
    {
        JUCEApplicationBase::quit();
    }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (pendingBlocks.empty())
                return;

            sendResults (doScan (pendingBlocks.front()));
            pendingBlocks.pop();
        }
    }

    OwnedArray<PluginDescription> doScan (const MemoryBlock& block)
    {
        MemoryInputStream stream { block, false };
        const auto formatName = stream.readString();
        const auto identifier = stream.readString();

        PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        pd.uniqueId = pd.deprecatedUid = 0;

        AudioPluginFormat* matchingFormat = nullptr;
        for (auto* format : formatManager.getFormats())
            if (format->getName() == formatName)
                matchingFormat = format;

        OwnedArray<PluginDescription> results;

        if (matchingFormat != nullptr
            && (MessageManager::getInstance()->isThisTheMessageThread()
                || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
        {
            matchingFormat->findAllTypesForFile (results, identifier);
        }

        return results;
    }

    void sendResults (const OwnedArray<PluginDescription>& results)
    {
        XmlElement xml ("LIST");
        for (const auto& desc : results)
            xml.addChildElement (desc->createXml().release());

        const auto str = xml.toString();
        sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
    }

    std::mutex mutex;
    std::queue<MemoryBlock> pendingBlocks;
    AudioPluginFormatManager formatManager;
};

std::unique_ptr<ScannerSubprocess> scannerInstance;
} // namespace

namespace PluginScanner
{
void install (KnownPluginList& list)
{
    list.setCustomScanner (std::make_unique<OutOfProcessScanner>());
}

bool runAsScannerIfRequested (const String& commandLine)
{
    auto scanner = std::make_unique<ScannerSubprocess>();

    if (! scanner->initialiseFromCommandLine (commandLine, processUID))
        return false;

   #if JUCE_MAC
    Process::setDockIconVisible (false);
   #endif

    scannerInstance = std::move (scanner);
    return true;
}
}
