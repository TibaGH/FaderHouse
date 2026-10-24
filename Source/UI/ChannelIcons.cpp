#include "ChannelIcons.h"
#include "FaderhouseLookAndFeel.h"
#include "../Engine/MixerEngine.h"

using namespace juce;

namespace ChannelIcons
{
const std::vector<Generic>& getGenericIds()
{
    static const std::vector<Generic> ids {
        { "mic", "Microphone" }, { "headset", "Voice chat" }, { "chat", "Chat" },
        { "headphones", "Headphones" }, { "music", "Music" }, { "audio", "Audio" },
        { "game", "Game" }, { "browser", "Browser" }, { "desktop", "Desktop" },
        { "speaker", "Speaker" }, { "video", "Video" }, { "bell", "Notifications" }
    };
    return ids;
}

StringArray getPopularAppIds()
{
   #if JUCE_MAC
    return { "com.hnc.Discord", "com.spotify.client", "com.tidal.desktop", "com.apple.Music",
             "com.google.Chrome", "com.apple.Safari", "org.mozilla.firefox", "company.thebrowser.Browser",
             "com.valvesoftware.steam", "us.zoom.xos", "com.microsoft.teams2", "net.whatsapp.WhatsApp",
             "com.tinyspeck.slackmacgap", "com.obsproject.obs-studio", "com.avid.ProTools",
             "com.apple.FaceTime", "com.apple.TV", "com.apple.podcasts" };
   #elif JUCE_WINDOWS
    return { "exe:Discord.exe", "exe:Spotify.exe", "exe:TIDAL.exe", "exe:chrome.exe", "exe:msedge.exe",
             "exe:firefox.exe", "exe:brave.exe", "exe:opera.exe", "exe:steam.exe", "exe:Zoom.exe",
             "exe:ms-teams.exe", "exe:WhatsApp.exe", "exe:slack.exe", "exe:obs64.exe", "exe:vlc.exe",
             "exe:ProTools.exe", "exe:iTunes.exe", "exe:AppleMusic.exe" };
   #else
    return {};
   #endif
}

//==============================================================================
static Path makeGenericPath (const String& id, bool& filledParts, Path& filled)
{
    using M = MathConstants<float>;
    Path p;
    filledParts = false;

    auto line = [&p] (float x1, float y1, float x2, float y2) { p.startNewSubPath (x1, y1); p.lineTo (x2, y2); };

    if (id == "mic")
    {
        p.addRoundedRectangle (9.0f, 2.5f, 6.0f, 11.5f, 3.0f);
        p.addCentredArc (12.0f, 10.5f, 6.5f, 6.5f, 0.0f, M::halfPi, M::halfPi * 3.0f, true);
        line (12.0f, 17.0f, 12.0f, 21.0f);
        line (8.5f, 21.0f, 15.5f, 21.0f);
    }
    else if (id == "headphones" || id == "headset")
    {
        p.addCentredArc (12.0f, 13.0f, 8.5f, 8.5f, 0.0f, -M::halfPi, M::halfPi, true);
        filledParts = true;
        filled.addRoundedRectangle (2.5f, 12.5f, 4.5f, 7.5f, 2.0f);
        filled.addRoundedRectangle (17.0f, 12.5f, 4.5f, 7.5f, 2.0f);

        if (id == "headset")
        {
            p.startNewSubPath (5.0f, 20.0f);
            p.quadraticTo (6.0f, 22.5f, 10.5f, 22.0f);
            filled.addEllipse (10.0f, 20.6f, 3.0f, 2.8f);
        }
    }
    else if (id == "chat")
    {
        p.addRoundedRectangle (3.0f, 4.0f, 18.0f, 12.5f, 3.5f);
        p.startNewSubPath (7.5f, 16.5f);
        p.lineTo (6.5f, 20.5f);
        p.lineTo (11.5f, 16.5f);
        filledParts = true;
        for (float x : { 8.0f, 12.0f, 16.0f })
            filled.addEllipse (x - 1.1f, 9.1f, 2.2f, 2.2f);
    }
    else if (id == "music")
    {
        line (9.0f, 17.0f, 9.0f, 5.5f);
        line (19.0f, 15.0f, 19.0f, 3.5f);
        p.startNewSubPath (9.0f, 5.5f);
        p.lineTo (19.0f, 3.5f);
        line (9.0f, 8.5f, 19.0f, 6.5f);
        filledParts = true;
        filled.addEllipse (4.0f, 15.0f, 5.6f, 4.2f);
        filled.addEllipse (14.0f, 13.0f, 5.6f, 4.2f);
    }
    else if (id == "audio")
    {
        const float heights[] = { 4.0f, 10.0f, 16.0f, 8.0f, 13.0f, 6.0f, 3.0f };
        for (int i = 0; i < 7; ++i)
        {
            const float x = 3.0f + (float) i * 3.0f;
            line (x, 12.0f - heights[i] * 0.5f, x, 12.0f + heights[i] * 0.5f);
        }
    }
    else if (id == "game")
    {
        p.startNewSubPath (7.0f, 7.5f);
        p.lineTo (17.0f, 7.5f);
        p.cubicTo (20.5f, 7.5f, 22.0f, 11.0f, 22.0f, 15.0f);
        p.cubicTo (22.0f, 18.5f, 19.5f, 19.5f, 17.5f, 17.5f);
        p.lineTo (15.5f, 15.5f);
        p.lineTo (8.5f, 15.5f);
        p.lineTo (6.5f, 17.5f);
        p.cubicTo (4.5f, 19.5f, 2.0f, 18.5f, 2.0f, 15.0f);
        p.cubicTo (2.0f, 11.0f, 3.5f, 7.5f, 7.0f, 7.5f);
        p.closeSubPath();
        line (5.5f, 11.5f, 9.5f, 11.5f);
        line (7.5f, 9.5f, 7.5f, 13.5f);
        filledParts = true;
        filled.addEllipse (15.0f, 9.2f, 2.4f, 2.4f);
        filled.addEllipse (17.6f, 11.8f, 2.4f, 2.4f);
    }
    else if (id == "browser")
    {
        p.addEllipse (3.0f, 3.0f, 18.0f, 18.0f);
        p.addEllipse (8.0f, 3.0f, 8.0f, 18.0f);
        line (3.0f, 12.0f, 21.0f, 12.0f);
        line (4.5f, 7.5f, 19.5f, 7.5f);
        line (4.5f, 16.5f, 19.5f, 16.5f);
    }
    else if (id == "desktop")
    {
        p.addRoundedRectangle (2.5f, 4.0f, 19.0f, 12.5f, 1.5f);
        line (12.0f, 16.5f, 12.0f, 20.0f);
        line (8.0f, 20.5f, 16.0f, 20.5f);
    }
    else if (id == "speaker")
    {
        p.startNewSubPath (3.5f, 9.0f);
        p.lineTo (7.5f, 9.0f);
        p.lineTo (12.5f, 4.5f);
        p.lineTo (12.5f, 19.5f);
        p.lineTo (7.5f, 15.0f);
        p.lineTo (3.5f, 15.0f);
        p.closeSubPath();
        p.addCentredArc (12.5f, 12.0f, 4.0f, 4.0f, 0.0f, M::pi * 0.25f, M::pi * 0.75f, true);
        p.addCentredArc (12.5f, 12.0f, 7.5f, 7.5f, 0.0f, M::pi * 0.22f, M::pi * 0.78f, true);
    }
    else if (id == "video")
    {
        p.addRoundedRectangle (2.5f, 6.5f, 13.0f, 11.0f, 2.0f);
        p.startNewSubPath (15.5f, 10.5f);
        p.lineTo (21.5f, 7.0f);
        p.lineTo (21.5f, 17.0f);
        p.lineTo (15.5f, 13.5f);
    }
    else if (id == "bell")
    {
        p.startNewSubPath (6.5f, 16.5f);
        p.lineTo (6.5f, 11.0f);
        p.cubicTo (6.5f, 7.5f, 8.8f, 5.0f, 12.0f, 5.0f);
        p.cubicTo (15.2f, 5.0f, 17.5f, 7.5f, 17.5f, 11.0f);
        p.lineTo (17.5f, 16.5f);
        line (4.5f, 16.5f, 19.5f, 16.5f);
        line (12.0f, 2.5f, 12.0f, 5.0f);
        p.addCentredArc (12.0f, 18.5f, 2.0f, 2.0f, 0.0f, M::halfPi, M::halfPi * 3.0f, true);
    }
    else
    {
        p.addEllipse (5.0f, 5.0f, 14.0f, 14.0f);
    }

    return p;
}

void drawGeneric (Graphics& g, const String& id, Rectangle<float> area, Colour colour)
{
    bool hasFilled = false;
    Path filled;
    auto stroke = makeGenericPath (id, hasFilled, filled);

    const float size = jmin (area.getWidth(), area.getHeight());
    const float scale = size / 24.0f;
    const auto t = AffineTransform::scale (scale).translated (area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f);

    g.setColour (colour);
    g.strokePath (stroke, PathStrokeType (1.7f * scale, PathStrokeType::curved, PathStrokeType::rounded), t);
    if (hasFilled)
        g.fillPath (filled, t);
}

String resolve (const Source& source)
{
    if (source.icon.isNotEmpty())
        return source.icon;

    if (source.kind == SourceKind::App)
        for (auto& appId : source.appIds)
            if (getAppIcon (appId).isValid())
                return "app:" + appId;

    switch (source.kind)
    {
        case SourceKind::Input:  return "mic";
        case SourceKind::System: return "desktop";
        case SourceKind::App:    return "audio";
    }
    return "audio";
}

void draw (Graphics& g, const String& resolvedId, Rectangle<float> area, Colour colour)
{
    if (resolvedId.startsWith ("app:"))
    {
        auto img = getAppIcon (resolvedId.substring (4));
        if (img.isValid())
        {
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (img, area, RectanglePlacement::centred);
            return;
        }
        drawGeneric (g, "audio", area, colour);
        return;
    }

    drawGeneric (g, resolvedId, area, colour);
}
} // namespace ChannelIcons

//==============================================================================
void ChannelIconButton::paintButton (Graphics& g, bool over, bool)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool isApp = iconId.startsWith ("app:");

    if (! isApp)
    {
        g.setColour (colour.withAlpha (over ? 0.28f : 0.18f));
        g.fillRoundedRectangle (b, 5.0f);
        ChannelIcons::draw (g, iconId, b.reduced (b.getWidth() * 0.2f), colour.brighter (0.3f));
    }
    else
    {
        ChannelIcons::draw (g, iconId, b, colour);
        if (over)
        {
            g.setColour (Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (b, 5.0f);
        }
    }
}

//==============================================================================
IconPicker::IconPicker (const String& currentId, const StringArray& assignedAppIds, std::function<void (const String&)> pick)
    : current (currentId), onPick (std::move (pick))
{
    for (auto& g : ChannelIcons::getGenericIds())
        genericCells.push_back ({ g.id, g.name, {} });

    StringArray apps;
    for (auto& id : assignedAppIds)
        apps.addIfNotAlreadyThere (id);
    for (auto& id : ChannelIcons::getPopularAppIds())
        apps.addIfNotAlreadyThere (id);

    for (auto& id : apps)
        if (ChannelIcons::getAppIcon (id).isValid())
        {
            auto name = ChannelIcons::getAppName (id);
            appCells.push_back ({ "app:" + id, name.isNotEmpty() ? name : id, {} });
        }

    setMouseClickGrabsKeyboardFocus (false);

    constexpr int cell = 40, cols = 6, pad = 10;
    const int genericRows = ((int) genericCells.size() + cols - 1) / cols;
    const int appRows = ((int) appCells.size() + cols - 1) / cols;
    const int width = pad * 2 + cols * cell;
    int height = pad + 26 + genericRows * cell + 8;
    if (appRows > 0)
        height += 22 + appRows * cell + 8;
    setSize (width, height + pad);
}

void IconPicker::resized()
{
    constexpr int cell = 40, cols = 6, pad = 10;
    autoBounds = { pad, pad, getWidth() - 2 * pad, 22 };

    int y = pad + 26;
    for (size_t i = 0; i < genericCells.size(); ++i)
        genericCells[i].bounds = { pad + (int) (i % cols) * cell, y + (int) (i / cols) * cell, cell, cell };

    y += (((int) genericCells.size() + cols - 1) / cols) * cell + 8;
    appHeaderY = y;
    y += 22;

    for (size_t i = 0; i < appCells.size(); ++i)
        appCells[i].bounds = { pad + (int) (i % cols) * cell, y + (int) (i / cols) * cell, cell, cell };
}

int IconPicker::cellAt (Point<int> p) const
{
    if (autoBounds.contains (p)) return -1;
    for (size_t i = 0; i < genericCells.size(); ++i)
        if (genericCells[i].bounds.contains (p)) return (int) i;
    for (size_t i = 0; i < appCells.size(); ++i)
        if (appCells[i].bounds.contains (p)) return 1000 + (int) i;
    return -2;
}

void IconPicker::paint (Graphics& g)
{
    // "Automatic" row
    g.setColour (hover == -1 ? Theme::panelRaised.brighter (0.15f) : Theme::panelRaised);
    g.fillRoundedRectangle (autoBounds.toFloat(), 4.0f);
    g.setColour (current.isEmpty() ? Theme::accent : Theme::text);
    g.setFont (FontOptions (12.0f));
    g.drawText ("Automatic (app icon)", autoBounds, Justification::centred);

    auto drawCell = [&] (const Cell& c, int index)
    {
        auto r = c.bounds.toFloat().reduced (3.0f);
        if (c.id == current)
        {
            g.setColour (Theme::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        else if (hover == index)
        {
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        ChannelIcons::draw (g, c.id, r.reduced (c.id.startsWith ("app:") ? 2.0f : 7.0f), Theme::text);
    };

    for (size_t i = 0; i < genericCells.size(); ++i)
        drawCell (genericCells[i], (int) i);

    if (! appCells.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText ("APP ICONS", 10, appHeaderY, getWidth() - 20, 20, Justification::centredLeft);

        for (size_t i = 0; i < appCells.size(); ++i)
            drawCell (appCells[i], 1000 + (int) i);
    }
}

void IconPicker::mouseMove (const MouseEvent& e)
{
    const int h = cellAt (e.getPosition());
    if (h != hover)
    {
        hover = h;
        if (h >= 1000)                setTooltip (appCells[(size_t) (h - 1000)].tooltip);
        else if (h >= 0)              setTooltip (genericCells[(size_t) h].tooltip);
        else                          setTooltip ({});
        repaint();
    }
}

void IconPicker::mouseExit (const MouseEvent&)
{
    hover = -2;
    repaint();
}

void IconPicker::mouseUp (const MouseEvent& e)
{
    const int c = cellAt (e.getPosition());
    String id;

    if (c == -2)       return;
    if (c >= 1000)     id = appCells[(size_t) (c - 1000)].id;
    else if (c >= 0)   id = genericCells[(size_t) c].id;

    if (onPick)
        onPick (id);

    if (auto* box = findParentComponentOfClass<CallOutBox>())
        box->dismiss();
}
