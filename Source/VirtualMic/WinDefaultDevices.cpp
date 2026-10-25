// Windows: puts the default sound devices right once VB-CABLE (our Mic FX device) is installed.
//
//  - Windows sometimes makes "CABLE Input" the default OUTPUT after installing VB-CABLE. Then
//    every app would play into your mic. We move the default output back to a real device.
//  - Once, we make "CABLE Output" the default MICROPHONE (and communications mic), so Discord,
//    OBS, Zoom... set to "Default" get your processed mic without touching their settings.
//
// Setting a default device has no public API; this uses the IPolicyConfig interface that
// Windows' own Sound settings use (also used by tools such as EarTrumpet and SoundSwitch).

#include "WinDefaultDevices.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#if defined (_MSC_VER)
 #pragma comment (lib, "ole32.lib")
#endif

using juce::String;

namespace
{
constexpr GUID makeGuid (unsigned long a, unsigned short b, unsigned short c, unsigned char d0, unsigned char d1,
                         unsigned char d2, unsigned char d3, unsigned char d4, unsigned char d5, unsigned char d6, unsigned char d7)
{
    return GUID { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } };
}

constexpr GUID kCLSID_MMDeviceEnum      = makeGuid (0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
constexpr GUID kIID_IMMDeviceEnumerator = makeGuid (0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
constexpr GUID kCLSID_PolicyConfig      = makeGuid (0x870af99c, 0x171d, 0x4f9e, 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9);
constexpr GUID kIID_IPolicyConfig       = makeGuid (0xf8679f50, 0x850a, 0x41cf, 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8);
constexpr PROPERTYKEY kPKEY_FriendlyName { makeGuid (0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0), 14 };

// Undocumented; only the method order matters (we call SetDefaultEndpoint).
struct IPolicyConfig : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat (PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat (PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat (PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat (PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod (PCWSTR, INT, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue (PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue (PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint (PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility (PCWSTR, INT) = 0;
};

template <typename T>
struct Com
{
    T* p = nullptr;
    ~Com()                  { if (p != nullptr) p->Release(); }
    T** put()               { if (p != nullptr) { p->Release(); p = nullptr; } return &p; }
    void** putVoid()        { return reinterpret_cast<void**> (put()); }
    T* operator->() const   { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct Endpoint { String id, name; };

String readId (IMMDevice* d)
{
    LPWSTR id = nullptr;
    String s;
    if (d != nullptr && SUCCEEDED (d->GetId (&id)) && id != nullptr) { s = String (id); CoTaskMemFree (id); }
    return s;
}

String readName (IMMDevice* d)
{
    Com<IPropertyStore> props;
    if (d == nullptr || FAILED (d->OpenPropertyStore (STGM_READ, props.put())))
        return {};

    PROPVARIANT v;
    PropVariantInit (&v);
    String s;
    if (SUCCEEDED (props->GetValue (kPKEY_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal != nullptr)
        s = String (v.pwszVal);
    PropVariantClear (&v);
    return s;
}

std::vector<Endpoint> listEndpoints (IMMDeviceEnumerator* e, EDataFlow flow)
{
    std::vector<Endpoint> result;
    Com<IMMDeviceCollection> devices;
    if (FAILED (e->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE, devices.put())))
        return result;

    UINT count = 0;
    devices->GetCount (&count);
    for (UINT i = 0; i < count; ++i)
    {
        Com<IMMDevice> d;
        if (SUCCEEDED (devices->Item (i, d.put())))
            result.push_back ({ readId (d.p), readName (d.p) });
    }
    return result;
}

Endpoint defaultEndpoint (IMMDeviceEnumerator* e, EDataFlow flow)
{
    Com<IMMDevice> d;
    if (FAILED (e->GetDefaultAudioEndpoint (flow, eConsole, d.put())))
        return {};
    return { readId (d.p), readName (d.p) };
}

bool isVirtualCable (const String& name)
{
    return name.containsIgnoreCase ("VB-Audio") || name.startsWithIgnoreCase ("CABLE ");
}

bool setDefault (const String& id)
{
    Com<IPolicyConfig> policy;
    if (FAILED (CoCreateInstance (kCLSID_PolicyConfig, nullptr, CLSCTX_ALL, kIID_IPolicyConfig, policy.putVoid())))
        return false;

    bool ok = true;
    for (ERole role : { eConsole, eMultimedia, eCommunications })
        ok = SUCCEEDED (policy->SetDefaultEndpoint (id.toWideCharPointer(), role)) && ok;
    return ok;
}
}

namespace WinDefaultDevices
{
String fixDefaults (bool makeCableTheDefaultMic)
{
    const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    String report;

    {
        Com<IMMDeviceEnumerator> e;
        if (SUCCEEDED (CoCreateInstance (kCLSID_MMDeviceEnum, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, e.putVoid())))
        {
            // 1. Never leave the virtual cable as the default output.
            const auto output = defaultEndpoint (e.p, eRender);
            if (isVirtualCable (output.name))
            {
                for (auto& candidate : listEndpoints (e.p, eRender))
                    if (! isVirtualCable (candidate.name) && setDefault (candidate.id))
                    {
                        report << "Windows output set back to \"" << candidate.name << "\". ";
                        break;
                    }
            }

            // 2. Processed mic as the Windows default microphone.
            if (makeCableTheDefaultMic)
            {
                const auto input = defaultEndpoint (e.p, eCapture);
                if (! isVirtualCable (input.name))
                    for (auto& candidate : listEndpoints (e.p, eCapture))
                        if (candidate.name.startsWithIgnoreCase ("CABLE Output") && setDefault (candidate.id))
                        {
                            report << "Default microphone set to \"" << candidate.name << "\". ";
                            break;
                        }
            }
        }
    }

    if (SUCCEEDED (init))
        CoUninitialize();

    return report.trim();
}

bool isCableInstalled()
{
    const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    bool found = false;

    {
        Com<IMMDeviceEnumerator> e;
        if (SUCCEEDED (CoCreateInstance (kCLSID_MMDeviceEnum, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, e.putVoid())))
            for (auto& d : listEndpoints (e.p, eCapture))
                found = found || d.name.startsWithIgnoreCase ("CABLE Output");
    }

    if (SUCCEEDED (init))
        CoUninitialize();

    return found;
}
}
