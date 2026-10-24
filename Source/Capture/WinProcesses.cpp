#include "WinProcesses.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <mutex>

#if defined (_MSC_VER)
 #pragma comment (lib, "version.lib")
 #pragma comment (lib, "advapi32.lib")
#endif

using juce::String;

namespace WinProcesses
{
std::map<unsigned long, Process> snapshot()
{
    std::map<unsigned long, Process> result;

    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof (entry);

    if (Process32FirstW (snap, &entry))
    {
        do
        {
            Process p;
            p.pid = entry.th32ProcessID;
            p.parentPid = entry.th32ParentProcessID;
            p.exe = String (entry.szExeFile);
            result[p.pid] = p;
        }
        while (Process32NextW (snap, &entry));
    }

    CloseHandle (snap);
    return result;
}

String imagePath (unsigned long pid)
{
    HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (h == nullptr)
        return {};

    wchar_t buffer[MAX_PATH * 2] {};
    DWORD size = (DWORD) (sizeof (buffer) / sizeof (buffer[0]));
    String result;

    if (QueryFullProcessImageNameW (h, 0, buffer, &size))
        result = String (buffer);

    CloseHandle (h);
    return result;
}

String fileDescription (const String& exePath)
{
    if (exePath.isEmpty())
        return {};

    static std::mutex lock;
    static std::map<String, String> cache;

    {
        const std::lock_guard<std::mutex> l (lock);
        auto it = cache.find (exePath);
        if (it != cache.end())
            return it->second;
    }

    String description;
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW (exePath.toWideCharPointer(), &handle);

    if (size > 0)
    {
        juce::HeapBlock<char> data (size, true);

        if (GetFileVersionInfoW (exePath.toWideCharPointer(), 0, size, data.getData()))
        {
            struct LangAndCodePage { WORD language; WORD codePage; };
            LangAndCodePage* translations = nullptr;
            UINT bytes = 0;

            if (VerQueryValueW (data.getData(), L"\\VarFileInfo\\Translation", (LPVOID*) &translations, &bytes)
                 && translations != nullptr && bytes >= sizeof (LangAndCodePage))
            {
                for (UINT i = 0; i < bytes / sizeof (LangAndCodePage) && description.isEmpty(); ++i)
                {
                    const auto block = "\\StringFileInfo\\"
                                     + String::toHexString ((int) translations[i].language).paddedLeft ('0', 4)
                                     + String::toHexString ((int) translations[i].codePage).paddedLeft ('0', 4)
                                     + "\\FileDescription";
                    wchar_t* value = nullptr;
                    UINT length = 0;

                    if (VerQueryValueW (data.getData(), block.toWideCharPointer(), (LPVOID*) &value, &length)
                         && value != nullptr && length > 0)
                        description = String (value).trim();
                }
            }
        }
    }

    const std::lock_guard<std::mutex> l (lock);
    cache[exePath] = description;
    return description;
}

bool isSystemPath (const String& exePath)
{
    static const String windowsDir = []
    {
        wchar_t buffer[MAX_PATH] {};
        GetWindowsDirectoryW (buffer, MAX_PATH);
        return String (buffer);
    }();

    return windowsDir.isNotEmpty() && exePath.startsWithIgnoreCase (windowsDir + "\\");
}

//==============================================================================
// Processes that play audio on behalf of the app that started them.
static bool isHelperExe (const String& exe)
{
    static const juce::StringArray helpers { "msedgewebview2.exe", "cefsharp.browsersubprocess.exe",
                                             "qtwebengineprocess.exe", "steamwebhelper.exe",
                                             "crashpad_handler.exe" };
    return helpers.contains (exe, true);
}

// Where a parent chain stops: the shell and system processes start everything.
static bool isLauncherExe (const String& exe)
{
    static const juce::StringArray launchers { "explorer.exe", "svchost.exe", "services.exe", "sihost.exe",
                                               "runtimebroker.exe", "wininit.exe", "winlogon.exe",
                                               "smss.exe", "csrss.exe", "system", "cmd.exe",
                                               "powershell.exe", "pwsh.exe", "conhost.exe",
                                               "windowsterminal.exe", "dllhost.exe", "taskhostw.exe" };
    return launchers.contains (exe, true);
}

Identity identify (unsigned long pid, const std::map<unsigned long, Process>& processes)
{
    Identity id;
    id.rootPid = pid;

    auto it = processes.find (pid);
    if (it == processes.end())
        return id;

    const String exe = it->second.exe;
    id.appId = "exe:" + exe;

    // Climb while the parent is the same program (Chrome, Discord, Firefox all spawn copies of themselves).
    unsigned long current = pid;
    for (int depth = 0; depth < 32; ++depth)
    {
        auto self = processes.find (current);
        if (self == processes.end())
            break;

        auto parent = processes.find (self->second.parentPid);
        if (parent == processes.end() || parent->first == current || ! parent->second.exe.equalsIgnoreCase (exe))
            break;

        current = parent->first;
    }

    id.rootPid = current;

    // A web view / browser helper belongs to the app that started it (Teams, Steam...).
    if (isHelperExe (exe))
    {
        auto self = processes.find (current);
        unsigned long up = self != processes.end() ? self->second.parentPid : 0;

        for (int depth = 0; depth < 32 && up != 0; ++depth)
        {
            auto p = processes.find (up);
            if (p == processes.end() || isLauncherExe (p->second.exe))
                break;

            if (! isHelperExe (p->second.exe))
            {
                id.appId = "exe:" + p->second.exe + ".helper." + exe;
                break;
            }

            up = p->second.parentPid;
        }
    }

    return id;
}

String exeNameFromAppId (const String& appId)
{
    auto name = appId.startsWithIgnoreCase ("exe:") ? appId.substring (4) : appId;
    const int helper = name.indexOfIgnoreCase (".helper.");
    return helper > 0 ? name.substring (0, helper) : name;
}

static String appPathFromRegistry (const String& exe)
{
    const String key = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + exe;

    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
    {
        wchar_t buffer[MAX_PATH * 2] {};
        DWORD size = sizeof (buffer);

        if (RegGetValueW (root, key.toWideCharPointer(), nullptr, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
        {
            auto path = String (buffer).unquoted().trim();
            if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
                return path;
        }
    }

    return {};
}

String findExePath (const String& appId)
{
    const auto exe = exeNameFromAppId (appId);
    if (exe.isEmpty())
        return {};

    static std::mutex lock;
    static std::map<String, String> known;          // exe (lower case) -> path
    static std::map<String, juce::uint32> misses;   // exe -> when we last looked in vain

    const auto keyName = exe.toLowerCase();
    const auto now = juce::Time::getMillisecondCounter();

    {
        const std::lock_guard<std::mutex> l (lock);
        auto it = known.find (keyName);
        if (it != known.end() && juce::File (it->second).existsAsFile())
            return it->second;

        // Looking means walking every process: don't do that more than every few seconds per exe.
        auto miss = misses.find (keyName);
        if (miss != misses.end() && now - miss->second < 5000)
            return {};
    }

    String path;

    for (auto& [pid, p] : snapshot())
        if (p.exe.equalsIgnoreCase (exe))
            if ((path = imagePath (pid)).isNotEmpty())
                break;

    if (path.isEmpty())
        path = appPathFromRegistry (exe);

    const std::lock_guard<std::mutex> l (lock);
    if (path.isNotEmpty())
    {
        known[keyName] = path;
        misses.erase (keyName);
    }
    else
    {
        misses[keyName] = now;
    }

    return path;
}
}
