#include "Updater.h"
#include <juce_cryptography/juce_cryptography.h>

using namespace juce;

namespace
{
constexpr int checkIntervalHours = 24;

String httpGet (const String& url, int* status = nullptr)
{
    int code = 0;
    auto stream = URL (url).createInputStream (
        URL::InputStreamOptions (URL::ParameterHandling::inAddress)
            .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: Faderhouse")
            .withConnectionTimeoutMs (15000)
            .withNumRedirectsToFollow (5)
            .withStatusCode (&code));

    if (status != nullptr)
        *status = code;

    return stream != nullptr ? stream->readEntireStreamAsString() : String();
}
} // namespace

Updater::Updater (PropertiesFile& s) : settings (s), alive (std::make_shared<std::atomic<bool>> (true))
{
    // First automatic check shortly after launch, then once a day while running.
    startTimer (8000);
}

Updater::~Updater()
{
    stopTimer();
    alive->store (false);
    cancel->store (true);
}

String Updater::getCurrentVersion()
{
   #ifdef JUCE_APPLICATION_VERSION_STRING
    return JUCE_APPLICATION_VERSION_STRING;
   #else
    return "0.0.0";
   #endif
}

int Updater::compareVersions (const String& a, const String& b)
{
    auto parts = [] (String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        StringArray p;
        p.addTokens (v.upToFirstOccurrenceOf ("-", false, false), ".", {});
        while (p.size() < 3)
            p.add ("0");
        return p;
    };

    const auto pa = parts (a), pb = parts (b);
    for (int i = 0; i < 3; ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

bool Updater::isAutoCheckEnabled() const        { return settings.getBoolValue ("autoCheckUpdates", true); }
void Updater::setAutoCheckEnabled (bool on)     { settings.setValue ("autoCheckUpdates", on); settings.saveIfNeeded(); }
void Updater::skipVersion (const String& v)     { settings.setValue ("skippedUpdateVersion", v); settings.saveIfNeeded(); }

void Updater::timerCallback()
{
    startTimer (checkIntervalHours * 60 * 60 * 1000);

    if (isAutoCheckEnabled())
        check (false);
}

void Updater::check (bool userInitiated)
{
    if (busy.exchange (true))
        return;

    auto flag = alive;
    const auto skipped = settings.getValue ("skippedUpdateVersion");

    Thread::launch ([this, flag, userInitiated, skipped]
    {
        int status = 0;
        const auto text = httpGet ("https://api.github.com/repos/" + String (repository) + "/releases/latest", &status);
        const auto json = JSON::parse (text);

        Release r;
        String error;

        if (text.isEmpty())
            error = "Could not reach GitHub. Check your internet connection.";
        else if (status == 404)
            error = "No releases have been published yet.";
        else if (! json.isObject())
            error = "Unexpected answer from GitHub (HTTP " + String (status) + ").";
        else
        {
            r.version = json["tag_name"].toString().trimCharactersAtStart ("vV");
            r.notes = json["body"].toString();
            r.pageUrl = json["html_url"].toString();

            if (auto* assets = json["assets"].getArray())
            {
                for (auto& asset : *assets)
                {
                    const auto name = asset["name"].toString();
                    const auto url = asset["browser_download_url"].toString();

                    if (name.endsWithIgnoreCase (".pkg"))
                    {
                        r.packageName = name;
                        r.packageUrl = url;
                        r.packageSize = (int64) asset["size"];
                    }
                    else if (name.equalsIgnoreCase ("SHA256SUMS.txt"))
                    {
                        r.checksumsUrl = url;
                    }
                }
            }
        }

        MessageManager::callAsync ([this, flag, userInitiated, skipped, r, error]
        {
            if (! flag->load())
                return;

            busy.store (false);
            settings.setValue ("lastUpdateCheck", Time::getCurrentTime().toISO8601 (true));

            if (error.isNotEmpty())
            {
                if (userInitiated && onError) onError (error);
                return;
            }

            const bool newer = compareVersions (r.version, getCurrentVersion()) > 0;
            const bool installable = r.packageUrl.isNotEmpty() && r.checksumsUrl.isNotEmpty();

            if (newer && installable && (userInitiated || r.version != skipped))
            {
                if (onUpdateAvailable) onUpdateAvailable (r);
            }
            else if (userInitiated && onNoUpdate)
            {
                onNoUpdate (newer ? "Faderhouse " + r.version + " is out, but its installer isn't available yet. Try again in a few minutes."
                                  : "You have the latest version (" + getCurrentVersion() + ").");
            }
        });
    });
}

void Updater::downloadAndInstall (const Release& r, std::function<void (double)> progress,
                                  std::function<void (const String&)> done)
{
    cancel = std::make_shared<std::atomic<bool>> (false);
    auto flag = alive;
    auto cancelled = cancel;

    Thread::launch ([flag, cancelled, r, progress, done]
    {
        auto finish = [flag, done] (const String& error)
        {
            MessageManager::callAsync ([flag, done, error] { if (flag->load() && done) done (error); });
        };

        // 1. Expected checksum
        String expected;
        {
            StringArray lines;
            lines.addLines (httpGet (r.checksumsUrl));
            for (auto& line : lines)
                if (line.trim().endsWith (r.packageName))
                    expected = line.trim().upToFirstOccurrenceOf (" ", false, false).trim();
        }

        if (expected.length() != 64)
            return finish ("Couldn't get the checksum for " + r.packageName + ", so the download can't be verified.");

        // 2. Download the installer
        auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Faderhouse Update");
        dir.createDirectory();
        auto file = dir.getChildFile (r.packageName);
        file.deleteFile();

        int status = 0;
        auto in = URL (r.packageUrl).createInputStream (
            URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                .withExtraHeaders ("User-Agent: Faderhouse")
                .withConnectionTimeoutMs (20000)
                .withNumRedirectsToFollow (5)
                .withStatusCode (&status));

        if (in == nullptr)
            return finish ("Download failed (HTTP " + String (status) + ").");

        {
            FileOutputStream out (file);
            if (! out.openedOk())
                return finish ("Can't write to " + file.getFullPathName());

            const int64 total = r.packageSize > 0 ? r.packageSize : in->getTotalLength();
            HeapBlock<char> buffer (65536);
            int64 received = 0;

            while (! in->isExhausted())
            {
                if (cancelled->load() || ! flag->load())
                    return finish ("Cancelled.");

                const int n = in->read (buffer, 65536);
                if (n <= 0)
                    break;

                out.write (buffer, (size_t) n);
                received += n;

                if (total > 0 && progress)
                    MessageManager::callAsync ([flag, progress, p = (double) received / (double) total]
                                               { if (flag->load()) progress (p); });
            }
        }

        // 3. Verify
        const auto actual = SHA256 (file).toHexString();
        if (! actual.equalsIgnoreCase (expected))
        {
            file.deleteFile();
            return finish ("The download is damaged (checksum mismatch). Please try again.");
        }

        // 4. Open it in the macOS Installer; the caller quits the app.
        MessageManager::callAsync ([flag, done, file]
        {
            if (! flag->load())
                return;

            const bool opened = file.startAsProcess();
            if (done) done (opened ? String() : "Couldn't open the installer: " + file.getFullPathName());
        });
    });
}
