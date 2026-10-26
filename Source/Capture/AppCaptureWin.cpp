// Windows per-app capture via WASAPI process loopback (Windows 11 / build 20348+).
//
// For every app that is mapped to a mixer channel we open a process-loopback client on
// its process tree ("include target process tree"). The System channel gets one client
// per other process that has an audio session. A single capture thread reads every
// client, lines them up on a steady clock with a small jitter buffer, mixes the clients
// of each channel and pushes the result into that channel's ClockBridge.
//
// Windows has no "mute while tapped" like macOS. Process loopback captures an app's audio
// before the endpoint volume, so while Faderhouse plays through a different output (ASIO,
// or another device) we mute the Windows default output device: the apps go silent there
// and are only heard through Faderhouse. The mute is undone when Faderhouse stops, and on
// the next start if Faderhouse ever crashed.

#include "AppCapture.h"
#include "WinProcesses.h"
#include "../Engine/ClockBridge.h"

#include <juce_events/juce_events.h>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#if defined (_MSC_VER)
 #pragma comment (lib, "ole32.lib")
#endif

using juce::String;

namespace
{
//==============================================================================
// Our own copies of the GUIDs and process-loopback types, so this builds with any SDK.
constexpr GUID makeGuid (unsigned long a, unsigned short b, unsigned short c, unsigned char d0, unsigned char d1,
                         unsigned char d2, unsigned char d3, unsigned char d4, unsigned char d5, unsigned char d6, unsigned char d7)
{
    return GUID { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } };
}

constexpr GUID kIID_IUnknown            = makeGuid (0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
constexpr GUID kIID_IAgileObject        = makeGuid (0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
constexpr GUID kIID_ActivateHandler     = makeGuid (0x41D949AB, 0x9862, 0x444A, 0x80, 0xF6, 0xC2, 0x61, 0x33, 0x4D, 0xA5, 0xEB);
constexpr GUID kIID_IAudioClient        = makeGuid (0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
constexpr GUID kIID_IAudioCaptureClient = makeGuid (0xC8ADBD64, 0xE71E, 0x48a0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
constexpr GUID kCLSID_MMDeviceEnum      = makeGuid (0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
constexpr GUID kIID_IMMDeviceEnumerator = makeGuid (0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
constexpr GUID kIID_SessionManager2     = makeGuid (0x77AA99A0, 0x1BD6, 0x484F, 0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F);
constexpr GUID kIID_SessionControl2     = makeGuid (0xbfb7ff88, 0x7239, 0x4fc9, 0x8f, 0xa2, 0x07, 0xc9, 0x50, 0xbe, 0x9c, 0x6d);
constexpr GUID kIID_EndpointVolume      = makeGuid (0x5CDF2C82, 0x841E, 0x4546, 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A);

constexpr PROPERTYKEY kPKEY_FriendlyName { makeGuid (0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0), 14 };

enum FhActivationType : int { FH_ACTIVATION_DEFAULT = 0, FH_ACTIVATION_PROCESS_LOOPBACK = 1 };
enum FhLoopbackMode : int   { FH_LOOPBACK_INCLUDE_TREE = 0, FH_LOOPBACK_EXCLUDE_TREE = 1 };

struct FhProcessLoopbackParams { DWORD targetProcessId; FhLoopbackMode mode; };
struct FhActivationParams      { FhActivationType type; FhProcessLoopbackParams loopback; };

constexpr const wchar_t* kProcessLoopbackDevice = L"VAD\\Process_Loopback";

using ActivateFn = HRESULT (WINAPI*) (LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*,
                                      IActivateAudioInterfaceAsyncOperation**);

ActivateFn getActivateFunction()
{
    static ActivateFn fn = []
    {
        HMODULE lib = LoadLibraryW (L"Mmdevapi.dll");
        return lib != nullptr ? reinterpret_cast<ActivateFn> (reinterpret_cast<void*> (GetProcAddress (lib, "ActivateAudioInterfaceAsync")))
                              : nullptr;
    }();
    return fn;
}

bool windowsSupportsProcessLoopback()
{
    using RtlGetVersionFn = LONG (WINAPI*) (OSVERSIONINFOW*);
    static const bool supported = []
    {
        HMODULE ntdll = GetModuleHandleW (L"ntdll.dll");
        if (ntdll == nullptr)
            return false;

        auto fn = reinterpret_cast<RtlGetVersionFn> (reinterpret_cast<void*> (GetProcAddress (ntdll, "RtlGetVersion")));
        OSVERSIONINFOW info {};
        info.dwOSVersionInfoSize = sizeof (info);

        if (fn == nullptr || fn (&info) != 0)
            return false;

        return info.dwMajorVersion > 10 || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 20348);
    }();
    return supported && getActivateFunction() != nullptr;
}

//==============================================================================
template <typename T>
struct Com
{
    T* p = nullptr;

    Com() = default;
    ~Com()                              { reset(); }
    Com (const Com&) = delete;
    Com& operator= (const Com&) = delete;
    Com (Com&& o) noexcept : p (o.p)    { o.p = nullptr; }
    Com& operator= (Com&& o) noexcept   { if (this != &o) { reset(); p = o.p; o.p = nullptr; } return *this; }

    void reset()                        { if (p != nullptr) { p->Release(); p = nullptr; } }
    T** put()                           { reset(); return &p; }
    void** putVoid()                    { return reinterpret_cast<void**> (put()); }
    T* operator->() const               { return p; }
    explicit operator bool() const      { return p != nullptr; }
};

struct ComThread
{
    explicit ComThread (DWORD model)    { const auto hr = CoInitializeEx (nullptr, model); ok = SUCCEEDED (hr); }
    ~ComThread()                        { if (ok) CoUninitialize(); }
    bool ok = false;
};

Com<IMMDeviceEnumerator> makeEnumerator()
{
    Com<IMMDeviceEnumerator> e;
    CoCreateInstance (kCLSID_MMDeviceEnum, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, e.putVoid());
    return e;
}

String deviceId (IMMDevice* device)
{
    LPWSTR id = nullptr;
    String result;
    if (device != nullptr && SUCCEEDED (device->GetId (&id)) && id != nullptr)
    {
        result = String (id);
        CoTaskMemFree (id);
    }
    return result;
}

String friendlyName (IMMDevice* device)
{
    Com<IPropertyStore> props;
    if (device == nullptr || FAILED (device->OpenPropertyStore (STGM_READ, props.put())))
        return {};

    PROPVARIANT value;
    PropVariantInit (&value);
    String result;

    if (SUCCEEDED (props->GetValue (kPKEY_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
        result = String (value.pwszVal);

    PropVariantClear (&value);
    return result;
}

//==============================================================================
/** Every process with an audio session on any active output device. */
struct SessionProcess { DWORD pid = 0; bool active = false; };

std::map<DWORD, SessionProcess> enumerateSessions (IMMDeviceEnumerator* enumerator)
{
    std::map<DWORD, SessionProcess> result;

    Com<IMMDeviceCollection> devices;
    if (enumerator == nullptr || FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, devices.put())))
        return result;

    UINT count = 0;
    devices->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        Com<IMMDevice> device;
        if (FAILED (devices->Item (i, device.put())))
            continue;

        Com<IAudioSessionManager2> manager;
        if (FAILED (device->Activate (kIID_SessionManager2, CLSCTX_ALL, nullptr, manager.putVoid())))
            continue;

        Com<IAudioSessionEnumerator> sessions;
        if (FAILED (manager->GetSessionEnumerator (sessions.put())))
            continue;

        int n = 0;
        sessions->GetCount (&n);

        for (int s = 0; s < n; ++s)
        {
            Com<IAudioSessionControl> control;
            if (FAILED (sessions->GetSession (s, control.put())))
                continue;

            Com<IAudioSessionControl2> control2;
            if (FAILED (control->QueryInterface (kIID_SessionControl2, control2.putVoid())))
                continue;

            if (control2->IsSystemSoundsSession() == S_OK)
                continue;

            DWORD pid = 0;
            if (FAILED (control2->GetProcessId (&pid)) || pid == 0)
                continue;

            AudioSessionState state = AudioSessionStateInactive;
            control2->GetState (&state);

            auto& entry = result[pid];
            entry.pid = pid;
            entry.active = entry.active || state == AudioSessionStateActive;
        }
    }

    return result;
}

//==============================================================================
/** Completion handler for ActivateAudioInterfaceAsync (must be agile). */
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler,
                                public IAgileObject
{
public:
    ActivationHandler()  { done = CreateEventW (nullptr, TRUE, FALSE, nullptr); }
    ~ActivationHandler() { if (done != nullptr) CloseHandle (done); }

    ULONG STDMETHODCALLTYPE AddRef() override   { return (ULONG) ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto r = --refs;
        if (r == 0)
            delete this;
        return (ULONG) r;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;

        if (IsEqualGUID (iid, kIID_IUnknown) || IsEqualGUID (iid, kIID_ActivateHandler))
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*> (this);
        else if (IsEqualGUID (iid, kIID_IAgileObject))
            *out = static_cast<IAgileObject*> (this);
        else
        {
            *out = nullptr;
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted (IActivateAudioInterfaceAsyncOperation* operation) override
    {
        HRESULT activateResult = E_FAIL;
        IUnknown* unknown = nullptr;

        if (operation != nullptr && SUCCEEDED (operation->GetActivateResult (&activateResult, &unknown)) && SUCCEEDED (activateResult) && unknown != nullptr)
        {
            unknown->QueryInterface (kIID_IAudioClient, reinterpret_cast<void**> (&client));
            unknown->Release();
        }
        else if (unknown != nullptr)
        {
            unknown->Release();
        }

        result = SUCCEEDED (activateResult) && client != nullptr ? S_OK : (FAILED (activateResult) ? activateResult : E_FAIL);
        SetEvent (done);
        return S_OK;
    }

    HANDLE done = nullptr;
    HRESULT result = E_PENDING;
    IAudioClient* client = nullptr;   // ownership passes to whoever takes it

private:
    std::atomic<long> refs { 1 };
};

//==============================================================================
/** One process-loopback stream, buffered on our side as interleaved stereo float. */
struct LoopbackClient
{
    DWORD pid = 0;
    double rate = 48000.0;
    bool isFloat = true;
    int channels = 2;

    Com<IAudioClient> client;
    Com<IAudioCaptureClient> capture;

    std::vector<float> buffer;   // interleaved stereo, oldest first
    bool primed = false;
    bool dead = false;

    ~LoopbackClient()
    {
        if (client)
            client->Stop();
    }

    int bufferedFrames() const noexcept { return (int) buffer.size() / 2; }

    /** Opens a process-loopback stream on the process tree of `pid`. */
    static std::unique_ptr<LoopbackClient> open (DWORD pid, double sampleRate, HRESULT& error)
    {
        // Process loopback has no mix format of its own (GetMixFormat is not implemented),
        // so ask for ours and let Windows convert. Float first, 16-bit as a fallback.
        // A failed Initialize leaves a client unusable, so the fallback activates a fresh one.
        if (auto c = open (pid, sampleRate, true, error))
            return c;

        if (error == HRESULT_FROM_WIN32 (ERROR_TIMEOUT) || error == E_NOTIMPL)
            return {};

        return open (pid, sampleRate, false, error);
    }

    static std::unique_ptr<LoopbackClient> open (DWORD pid, double sampleRate, bool asFloat, HRESULT& error)
    {
        auto activate = getActivateFunction();
        if (activate == nullptr)
        {
            error = E_NOTIMPL;
            return {};
        }

        FhActivationParams params {};
        params.type = FH_ACTIVATION_PROCESS_LOOPBACK;
        params.loopback.targetProcessId = pid;
        params.loopback.mode = FH_LOOPBACK_INCLUDE_TREE;

        PROPVARIANT pv;
        PropVariantInit (&pv);
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof (params);
        pv.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

        auto* handler = new ActivationHandler();
        Com<IActivateAudioInterfaceAsyncOperation> operation;
        error = activate (kProcessLoopbackDevice, kIID_IAudioClient, &pv, handler, operation.put());

        if (FAILED (error))
        {
            handler->Release();
            return {};
        }

        if (WaitForSingleObject (handler->done, 3000) != WAIT_OBJECT_0)
        {
            error = HRESULT_FROM_WIN32 (ERROR_TIMEOUT);
            handler->Release();   // the operation holds its own reference until it completes
            return {};
        }

        auto c = std::make_unique<LoopbackClient>();
        c->pid = pid;
        c->rate = sampleRate;
        c->isFloat = asFloat;
        c->client.p = handler->client;
        handler->client = nullptr;
        error = handler->result;
        handler->Release();

        if (FAILED (error) || ! c->client)
            return {};

        WAVEFORMATEX format {};
        format.wFormatTag = asFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = (DWORD) juce::roundToInt (sampleRate);
        format.wBitsPerSample = asFloat ? 32 : 16;
        format.nBlockAlign = (WORD) (format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                          | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

        if (FAILED (error = c->client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, 2000000 /* 200 ms */, 0, &format, nullptr)))
            return {};

        if (FAILED (error = c->client->GetService (kIID_IAudioCaptureClient, c->capture.putVoid())))
            return {};

        if (FAILED (error = c->client->Start()))
            return {};

        return c;
    }

    /** Moves everything Windows has for us into `buffer`. Capture thread. */
    void read()
    {
        if (dead || ! capture)
            return;

        for (int guard = 0; guard < 64; ++guard)
        {
            UINT32 packet = 0;
            HRESULT hr = capture->GetNextPacketSize (&packet);

            if (FAILED (hr))
            {
                dead = true;
                return;
            }

            if (packet == 0)
                return;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            if (FAILED (hr = capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
            {
                dead = (hr != AUDCLNT_S_BUFFER_EMPTY);
                return;
            }

            const size_t start = buffer.size();
            buffer.resize (start + (size_t) frames * 2);
            float* out = buffer.data() + start;

            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
            {
                std::fill (out, out + (size_t) frames * 2, 0.0f);
            }
            else if (isFloat)
            {
                std::memcpy (out, data, (size_t) frames * 2 * sizeof (float));
            }
            else
            {
                auto* in = reinterpret_cast<const int16_t*> (data);
                for (size_t i = 0; i < (size_t) frames * 2; ++i)
                    out[i] = (float) in[i] * (1.0f / 32768.0f);
            }

            capture->ReleaseBuffer (frames);
        }
    }
};

//==============================================================================
/** Mutes the Windows default output while the apps are heard through Faderhouse. */
class EndpointMuter
{
public:
    static juce::File stateFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Faderhouse").getChildFile ("muted-output.txt");
    }

    /** Undo a mute left behind by a crashed run. */
    static void recoverAfterCrash (IMMDeviceEnumerator* enumerator)
    {
        auto file = stateFile();
        if (! file.existsAsFile())
            return;

        const auto id = file.loadFileAsString().trim();
        file.deleteFile();

        if (enumerator == nullptr || id.isEmpty())
            return;

        Com<IMMDevice> device;
        if (SUCCEEDED (enumerator->GetDevice (id.toWideCharPointer(), device.put())))
            setMute (device.p, false);
    }

    /** Called regularly on the capture thread. */
    void update (IMMDeviceEnumerator* enumerator, bool wantMuted)
    {
        if (enumerator == nullptr)
            return;

        Com<IMMDevice> current;
        String currentId;
        if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, current.put())))
            currentId = deviceId (current.p);

        // Default device changed, or we no longer want the mute: give the old one back.
        if (mutedId.isNotEmpty() && (! wantMuted || mutedId != currentId))
            release (enumerator);

        if (! wantMuted || currentId.isEmpty())
            return;

        if (mutedId.isEmpty())
        {
            if (getMute (current.p))
                return;   // the user had it muted already: leave it alone and don't claim it

            mutedId = currentId;
            stateFile().getParentDirectory().createDirectory();
            stateFile().replaceWithText (mutedId);
        }

        // Keep it muted while we mix (otherwise you'd hear every app twice).
        if (! getMute (current.p))
            setMute (current.p, true);

        muteFailed = ! getMute (current.p);
    }

    bool didMuteFail() const noexcept { return muteFailed; }

    void release (IMMDeviceEnumerator* enumerator)
    {
        if (mutedId.isEmpty())
            return;

        Com<IMMDevice> device;
        if (enumerator != nullptr && SUCCEEDED (enumerator->GetDevice (mutedId.toWideCharPointer(), device.put())))
            setMute (device.p, false);

        mutedId = {};
        muteFailed = false;
        stateFile().deleteFile();
    }

    bool isMuting() const noexcept { return mutedId.isNotEmpty(); }

private:
    static bool getMute (IMMDevice* device)
    {
        Com<IAudioEndpointVolume> volume;
        BOOL muted = FALSE;
        if (device != nullptr && SUCCEEDED (device->Activate (kIID_EndpointVolume, CLSCTX_ALL, nullptr, volume.putVoid())))
            volume->GetMute (&muted);
        return muted != FALSE;
    }

    static void setMute (IMMDevice* device, bool mute)
    {
        Com<IAudioEndpointVolume> volume;
        if (device != nullptr && SUCCEEDED (device->Activate (kIID_EndpointVolume, CLSCTX_ALL, nullptr, volume.putVoid())))
            volume->SetMute (mute ? TRUE : FALSE, nullptr);
    }

    String mutedId;
    bool muteFailed = false;
};

//==============================================================================
class WinCaptureBackend final : public AppCaptureBackend
{
public:
    WinCaptureBackend()
    {
        ownPid = GetCurrentProcessId();
        supported = windowsSupportsProcessLoopback();
        wake = CreateEventW (nullptr, FALSE, FALSE, nullptr);

        // Even without process loopback support, undo a mute a crashed run left behind.
        thread = std::thread ([this] { run(); });
    }

    ~WinCaptureBackend() override
    {
        quit.store (true);
        SetEvent (wake);
        if (thread.joinable())
            thread.join();
        CloseHandle (wake);
    }

    bool isSupported() const override { return supported; }

    String getWarning() const override
    {
        const std::lock_guard<std::mutex> l (statusLock);
        return warning;
    }

    String getStatusText() const override
    {
        if (! supported)
            return "Per-app capture needs Windows 11 (build 20348 or newer)";

        const std::lock_guard<std::mutex> l (statusLock);
        return status;
    }

    std::vector<AudioProcessInfo> listAudioProcesses() override
    {
        ComThread com (COINIT_APARTMENTTHREADED);   // usually already initialised by JUCE: harmless
        auto enumerator = makeEnumerator();
        const auto sessions = enumerateSessions (enumerator.p);
        const auto processes = WinProcesses::snapshot();

        std::vector<AudioProcessInfo> result;

        for (auto& [pid, session] : sessions)
        {
            if (pid == ownPid || isOwnChild (pid, processes))
                continue;

            auto id = WinProcesses::identify (pid, processes);
            if (id.appId.isEmpty())
                continue;

            const auto path = WinProcesses::imagePath (pid);

            AudioProcessInfo info;
            info.appId = id.appId;
            info.pid = (int) pid;
            info.isOutputting = session.active;
            info.isApp = path.isEmpty() || ! WinProcesses::isSystemPath (path);

            const auto rootExe = WinProcesses::exeNameFromAppId (id.appId);
            const auto rootPath = rootExe.equalsIgnoreCase (processes.count (pid) ? processes.at (pid).exe : String())
                                    ? path : WinProcesses::findExePath (id.appId);
            info.displayName = WinProcesses::fileDescription (rootPath);
            if (info.displayName.isEmpty())
                info.displayName = rootExe.upToLastOccurrenceOf (".", false, false);

            result.push_back (info);
        }

        return result;
    }

    void setRoutes (const std::vector<Route>& newRoutes) override
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            routes.clear();
            for (auto& r : newRoutes)
                if (r.bridge != nullptr)
                    routes[r.sourceId] = r;

            // Clients of channels that are gone must never touch their bridge again.
            for (auto it = clients.begin(); it != clients.end();)
                it = routes.count (it->first.first) == 0 ? clients.erase (it) : std::next (it);

            for (auto& [sourceId, r] : routes)
                r.bridge->setProducerRate (targetRate.load());
        }

        SetEvent (wake);
    }

    void setTargetSampleRate (double sampleRate) override
    {
        if (sampleRate <= 0 || std::abs (sampleRate - targetRate.load()) < 0.5)
            return;

        targetRate.store (sampleRate);
        reopenAll.store (true);
        SetEvent (wake);
    }

    void setMainOutputDevice (const String& typeName, const String& deviceName) override
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            mainType = typeName;
            mainName = deviceName;
        }
        SetEvent (wake);
    }

    void stopAll() override
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            routes.clear();
            clients.clear();
        }
        SetEvent (wake);
    }

private:
    using ClientKey = std::pair<String, DWORD>;   // (channel id, process tree root)

    bool isOwnChild (DWORD pid, const std::map<unsigned long, WinProcesses::Process>& processes) const
    {
        auto it = processes.find (pid);
        return it != processes.end() && it->second.parentPid == ownPid;
    }

    //==========================================================================
    // Two threads: the pump reads and mixes every few milliseconds and must never wait;
    // the reconciler works out what to capture and opens clients (which can take a moment).
    void run()
    {
        ComThread com (COINIT_MULTITHREADED);
        auto enumerator = makeEnumerator();
        EndpointMuter::recoverAfterCrash (enumerator.p);

        if (! supported)
        {
            while (! quit.load())   // nothing to do until we're destroyed
                WaitForSingleObject (wake, INFINITE);
            return;
        }

        std::thread pumpThread ([this] { runPump(); });

        EndpointMuter muter;

        while (! quit.load())
        {
            WaitForSingleObject (wake, 750);
            if (! quit.load())
                reconcile (enumerator.p, muter);
        }

        pumpThread.join();

        {
            const std::lock_guard<std::mutex> l (lock);
            clients.clear();
        }
        muter.release (enumerator.p);
    }

    void runPump()
    {
        ComThread com (COINIT_MULTITHREADED);
        auto lastTicks = juce::Time::getHighResolutionTicks();
        double framesOwed = 0.0;

        while (! quit.load())
        {
            Sleep (4);

            // How many frames are due since last time, on a steady clock.
            const auto ticks = juce::Time::getHighResolutionTicks();
            const double seconds = juce::Time::highResolutionTicksToSeconds (ticks - lastTicks);
            lastTicks = ticks;

            const double rate = targetRate.load();
            framesOwed = juce::jmin (framesOwed + seconds * rate, rate * 0.1);
            const int due = (int) framesOwed;
            framesOwed -= due;

            pump (due, rate);
        }
    }

    /** Reads every client, mixes per channel and feeds the bridges. Capture thread. */
    void pump (int due, double rate)
    {
        const std::lock_guard<std::mutex> l (lock);

        for (auto& [key, c] : clients)
            c->read();

        if (due <= 0)
            return;

        const int prefill = juce::roundToInt (rate * 0.03);   // 30 ms jitter buffer
        const int maxFill = juce::roundToInt (rate * 0.15);

        mixBuffer.resize ((size_t) due * 2);

        for (auto& [sourceId, route] : routes)
        {
            std::fill (mixBuffer.begin(), mixBuffer.end(), 0.0f);

            for (auto it = clients.lower_bound ({ sourceId, 0 }); it != clients.end() && it->first.first == sourceId; ++it)
            {
                auto& c = *it->second;
                int available = c.bufferedFrames();

                if (available > maxFill)
                {
                    // Too far behind (e.g. after a hiccup): drop the oldest audio, keep latency low.
                    const int drop = available - prefill;
                    c.buffer.erase (c.buffer.begin(), c.buffer.begin() + (std::ptrdiff_t) drop * 2);
                    available = prefill;
                }

                if (! c.primed)
                {
                    if (available < prefill)
                        continue;
                    c.primed = true;
                }

                const int take = juce::jmin (available, due);
                const float* src = c.buffer.data();
                for (int i = 0; i < take * 2; ++i)
                    mixBuffer[(size_t) i] += src[i];

                c.buffer.erase (c.buffer.begin(), c.buffer.begin() + (std::ptrdiff_t) take * 2);

                if (take < due)
                    c.primed = false;   // ran dry (app stopped playing): wait for a full jitter buffer again
            }

            route.bridge->pushInterleaved (mixBuffer.data(), 2, due);
        }
    }

    /** Works out which process trees every channel should capture and opens/closes clients. */
    void reconcile (IMMDeviceEnumerator* enumerator, EndpointMuter& muter)
    {
        const double rate = targetRate.load();
        const bool reopen = reopenAll.exchange (false);

        std::map<String, Route> routesCopy;
        String type, name;
        {
            const std::lock_guard<std::mutex> l (lock);
            routesCopy = routes;
            type = mainType;
            name = mainName;

            if (reopen)
                clients.clear();

            for (auto& [id, r] : routes)
                r.bridge->setProducerRate (rate);
        }

        const auto processes = WinProcesses::snapshot();
        const auto sessions = enumerateSessions (enumerator);

        std::map<DWORD, WinProcesses::Identity> identities;

        auto identityOf = [&] (DWORD pid) -> const WinProcesses::Identity&
        {
            auto it = identities.find (pid);
            if (it == identities.end())
                it = identities.emplace (pid, WinProcesses::identify (pid, processes)).first;
            return it->second;
        };

        // A process-loopback client captures a whole process TREE. So a tree must not be
        // captured when Faderhouse itself, or an app of another channel, runs inside it
        // (you'd hear that audio twice, or Faderhouse would capture its own output).
        auto isDescendant = [&] (DWORD child, DWORD ancestor)
        {
            DWORD current = child;
            for (int depth = 0; depth < 64; ++depth)
            {
                auto it = processes.find (current);
                if (it == processes.end() || it->second.parentPid == current || it->second.parentPid == 0)
                    return false;
                current = it->second.parentPid;
                if (current == ancestor)
                    return true;
            }
            return false;
        };

        int skipped = 0;

        // 1. App channels: the process trees of the assigned apps.
        std::map<String, std::set<DWORD>> appRoots;
        for (auto& [sourceId, route] : routesCopy)
        {
            if (route.isSystem || route.appIds.isEmpty())
                continue;

            auto& roots = appRoots[sourceId];

            for (auto& [pid, p] : processes)
            {
                if (pid == ownPid || pid == 0 || isOwnChild (pid, processes))
                    continue;

                // Cheap pre-filter on the exe name before working out the full identity.
                bool candidate = isHelperName (p.exe);
                for (auto& appId : route.appIds)
                    candidate = candidate || p.exe.equalsIgnoreCase (WinProcesses::exeNameFromAppId (appId));

                if (! candidate)
                    continue;

                const auto& id = identityOf (pid);
                for (auto& appId : route.appIds)
                    if (appIdMatches (id.appId, appId))
                        roots.insert (id.rootPid);
            }
        }

        std::set<DWORD> allAppRoots;
        for (auto& [sourceId, roots] : appRoots)
            allAppRoots.insert (roots.begin(), roots.end());

        std::set<ClientKey> wanted;

        for (auto& [sourceId, roots] : appRoots)
        {
            for (auto root : roots)
            {
                bool coveredByAncestor = false, conflict = isDescendant (ownPid, root);

                for (auto other : roots)
                    coveredByAncestor = coveredByAncestor || (other != root && isDescendant (root, other));

                for (auto& [otherId, otherRoots] : appRoots)
                    if (otherId != sourceId)
                        for (auto other : otherRoots)
                            conflict = conflict || (other != root && isDescendant (other, root));

                if (conflict)
                    ++skipped;
                else if (! coveredByAncestor)
                    wanted.insert ({ sourceId, root });
            }
        }

        // 2. The System channel: every other process tree that has an audio session.
        for (auto& [sourceId, route] : routesCopy)
        {
            if (! route.isSystem)
                continue;

            std::set<DWORD> candidates;

            for (auto& [pid, session] : sessions)
            {
                if (pid == ownPid || processes.count (pid) == 0 || isOwnChild (pid, processes))
                    continue;

                const auto root = identityOf (pid).rootPid;
                if (root == ownPid || allAppRoots.count (root) != 0)
                    continue;

                bool insideAppTree = false;
                for (auto appRoot : allAppRoots)
                    insideAppTree = insideAppTree || isDescendant (root, appRoot);

                if (! insideAppTree)
                    candidates.insert (root);
            }

            for (auto root : candidates)
            {
                bool conflict = isDescendant (ownPid, root), coveredByAncestor = false;

                for (auto appRoot : allAppRoots)
                    conflict = conflict || isDescendant (appRoot, root);

                for (auto other : candidates)
                    coveredByAncestor = coveredByAncestor || (other != root && isDescendant (root, other));

                // (A conflicting tree here is usually the shell that started everything: leave it out quietly.)
                if (! conflict && ! coveredByAncestor)
                    wanted.insert ({ sourceId, root });
            }
        }

        // Close what is no longer wanted.
        {
            const std::lock_guard<std::mutex> l (lock);
            for (auto it = clients.begin(); it != clients.end();)
                it = (wanted.count (it->first) == 0 || it->second->dead) ? clients.erase (it) : std::next (it);
        }

        // Open what is missing (activation can take a moment, so outside the lock).
        int failedCount = 0;
        const auto nowMs = juce::Time::getMillisecondCounter();

        for (auto& key : wanted)
        {
            {
                const std::lock_guard<std::mutex> l (lock);
                if (clients.count (key) != 0 || routes.count (key.first) == 0)
                    continue;
            }

            auto retry = retryAfter.find (key.second);
            if (retry != retryAfter.end() && (int) (retry->second - nowMs) > 0)
            {
                ++failedCount;
                continue;
            }

            HRESULT error = S_OK;
            auto client = LoopbackClient::open (key.second, rate, error);

            if (client == nullptr)
            {
                retryAfter[key.second] = nowMs + 5000;
                lastError = error;
                ++failedCount;
                continue;
            }

            retryAfter.erase (key.second);

            const std::lock_guard<std::mutex> l (lock);
            if (routes.count (key.first) != 0)   // the channel may have been removed meanwhile
                clients[key] = std::move (client);
        }

        // Forget retry entries of processes that are gone.
        for (auto it = retryAfter.begin(); it != retryAfter.end();)
            it = processes.count (it->first) == 0 ? retryAfter.erase (it) : std::next (it);

        // Silence the apps' normal output while Faderhouse plays them somewhere else.
        Com<IMMDevice> defaultDevice;
        String defaultName;
        if (enumerator != nullptr && SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, defaultDevice.put())))
            defaultName = friendlyName (defaultDevice.p);

        // Never mute the virtual cable that carries Mic FX (Faderhouse writes into it).
        const bool defaultIsVirtualMic = defaultName.startsWithIgnoreCase ("CABLE") || defaultName.containsIgnoreCase ("VB-Audio")
                                      || defaultName.startsWithIgnoreCase ("Mic FX");
        const bool hasRoutes = ! routesCopy.empty();
        const bool playsElsewhere = ! defaultIsVirtualMic
                                 && (type.equalsIgnoreCase ("ASIO")
                                     || (name.isNotEmpty() && defaultName.isNotEmpty() && ! name.equalsIgnoreCase (defaultName)));
        muter.update (enumerator, hasRoutes && playsElsewhere);

        // Status line.
        std::set<DWORD> capturing;
        {
            const std::lock_guard<std::mutex> l (lock);
            for (auto& [key, c] : clients)
                capturing.insert (key.second);
        }

        String text;
        text << (int) capturing.size() << (capturing.size() == 1 ? " app captured" : " apps captured");

        String warn;
        if (muter.isMuting() && muter.didMuteFail())
            warn = "Faderhouse could not mute the Windows output \"" + defaultName + "\", so you may hear your apps twice "
                   "(once directly, once through Faderhouse, a little later).\n\nMute it yourself in the Windows volume "
                   "controls, or set Windows' output to a device you don't listen to.";
        else if (hasRoutes && ! playsElsewhere && ! defaultIsVirtualMic && defaultName.isNotEmpty())
            warn = "Windows plays every app directly on \"" + defaultName + "\", and Faderhouse plays them on the same "
                   "device too. So you hear your apps twice, the second time a little later.\n\nFix it in one of two ways:\n"
                   "  - In Faderhouse Settings > Audio interface, choose the ASIO driver of your interface "
                   "(Faderhouse then mutes the Windows output for you), or\n"
                   "  - In Windows' sound settings, set the output to a device you don't listen to.";
        {
            const std::lock_guard<std::mutex> l (statusLock);
            warning = warn;
        }

        if (muter.isMuting())
            text << " - Windows output \"" << defaultName << "\" muted, apps play through Faderhouse";
        else if (hasRoutes && ! playsElsewhere)
            text << " - apps also play directly on \"" << defaultName
                 << "\": use an ASIO driver or another output device in Settings";

        if (skipped > 0)
            text << " - " << skipped << " skipped (they started Faderhouse or an app of another channel)";

        if (failedCount > 0)
            text << " - " << failedCount << " could not be captured (0x"
                 << String::toHexString ((int) lastError).toUpperCase() << ")";

        const std::lock_guard<std::mutex> l (statusLock);
        status = text;
    }

    static bool isHelperName (const String& exe)
    {
        return exe.equalsIgnoreCase ("msedgewebview2.exe") || exe.equalsIgnoreCase ("steamwebhelper.exe")
            || exe.equalsIgnoreCase ("cefsharp.browsersubprocess.exe") || exe.equalsIgnoreCase ("qtwebengineprocess.exe");
    }

    //==========================================================================
    DWORD ownPid = 0;
    bool supported = false;
    HANDLE wake = nullptr;
    std::thread thread;
    std::atomic<bool> quit { false }, reopenAll { false };
    std::atomic<double> targetRate { 48000.0 };

    std::mutex lock;                                    // routes, clients, main device
    std::map<String, Route> routes;
    std::map<ClientKey, std::unique_ptr<LoopbackClient>> clients;
    String mainType, mainName;
    std::vector<float> mixBuffer;

    // Capture thread only.
    std::map<DWORD, uint32_t> retryAfter;
    HRESULT lastError = S_OK;

    mutable std::mutex statusLock;
    String status { "Starting per-app capture..." }, warning;
};
}

std::unique_ptr<AppCaptureBackend> AppCaptureBackend::create()
{
    return std::make_unique<WinCaptureBackend>();
}
