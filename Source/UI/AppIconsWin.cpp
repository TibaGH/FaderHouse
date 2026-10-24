// Real app icons and names on Windows: taken from the app's exe (running, or registered in "App Paths").
#include "ChannelIcons.h"
#include "../Capture/WinProcesses.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <map>

#if defined (_MSC_VER)
 #pragma comment (lib, "shell32.lib")
 #pragma comment (lib, "gdi32.lib")
 #pragma comment (lib, "user32.lib")
#endif

using namespace juce;

namespace
{
// Reads an HBITMAP as 32-bit top-down BGRA.
bool readBitmap (HBITMAP bitmap, int width, int height, std::vector<uint8_t>& pixels)
{
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    pixels.assign ((size_t) width * (size_t) height * 4, 0);
    HDC dc = GetDC (nullptr);
    const int lines = GetDIBits (dc, bitmap, 0, (UINT) height, pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC (nullptr, dc);
    return lines == height;
}

Image imageFromIcon (HICON icon)
{
    ICONINFO iconInfo {};
    if (icon == nullptr || ! GetIconInfo (icon, &iconInfo))
        return {};

    Image result;
    BITMAP bm {};

    if (iconInfo.hbmColor != nullptr && GetObject (iconInfo.hbmColor, sizeof (bm), &bm) != 0 && bm.bmWidth > 0 && bm.bmHeight > 0)
    {
        const int w = bm.bmWidth, h = bm.bmHeight;
        std::vector<uint8_t> colour, mask;

        if (readBitmap (iconInfo.hbmColor, w, h, colour))
        {
            bool hasAlpha = false;
            for (size_t i = 3; i < colour.size() && ! hasAlpha; i += 4)
                hasAlpha = colour[i] != 0;

            // Old-style icons without alpha: the AND mask says what is transparent.
            const bool useMask = ! hasAlpha && iconInfo.hbmMask != nullptr && readBitmap (iconInfo.hbmMask, w, h, mask);

            result = Image (Image::ARGB, w, h, true);
            Image::BitmapData data (result, Image::BitmapData::writeOnly);

            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = ((size_t) y * (size_t) w + (size_t) x) * 4;
                    uint8_t alpha = colour[i + 3];

                    if (! hasAlpha)
                        alpha = (useMask && mask[i] != 0) ? 0 : 255;

                    data.setPixelColour (x, y, Colour (colour[i + 2], colour[i + 1], colour[i], alpha));
                }
        }
    }

    if (iconInfo.hbmColor != nullptr) DeleteObject (iconInfo.hbmColor);
    if (iconInfo.hbmMask != nullptr)  DeleteObject (iconInfo.hbmMask);
    return result;
}

Image loadExeIcon (const String& path)
{
    HICON large = nullptr;

    // 64 px if the exe has it (Windows scales the nearest size otherwise).
    if (SUCCEEDED (SHDefExtractIconW (path.toWideCharPointer(), 0, 0, &large, nullptr, MAKELONG (64, 16))) && large != nullptr)
    {
        auto image = imageFromIcon (large);
        DestroyIcon (large);
        if (image.isValid())
            return image;
    }

    SHFILEINFOW info {};
    if (SHGetFileInfoW (path.toWideCharPointer(), 0, &info, sizeof (info), SHGFI_ICON | SHGFI_LARGEICON) != 0 && info.hIcon != nullptr)
    {
        auto image = imageFromIcon (info.hIcon);
        DestroyIcon (info.hIcon);
        return image;
    }

    return {};
}
}

namespace ChannelIcons
{
Image getAppIcon (const String& appId)
{
    static std::map<String, Image> cache;
    static std::map<String, uint32> misses;   // retry now and then: the app may start later

    const auto key = "exe:" + WinProcesses::exeNameFromAppId (appId);

    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    const auto now = Time::getMillisecondCounter();
    if (auto it = misses.find (key); it != misses.end() && now - it->second < 10000)
        return {};

    const auto path = WinProcesses::findExePath (key);
    auto image = path.isNotEmpty() ? loadExeIcon (path) : Image();

    if (image.isValid())
    {
        cache[key] = image;
        misses.erase (key);
    }
    else
    {
        misses[key] = now;
    }

    return image;
}

String getAppName (const String& appId)
{
    const auto path = WinProcesses::findExePath ("exe:" + WinProcesses::exeNameFromAppId (appId));
    return WinProcesses::fileDescription (path);
}
}
