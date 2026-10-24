#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct Source;

/**
    Channel icons.

    Ids:
      ""             automatic (the icon of the first assigned app, else a generic one)
      "mic", ...     one of the built-in line icons (see getGenericIds)
      "app:<id>"     the real icon of an installed app (bundle id on macOS, exe:Name.exe on Windows)
*/
namespace ChannelIcons
{
    struct Generic { const char* id; const char* name; };
    const std::vector<Generic>& getGenericIds();

    /** Ids of popular apps we offer icons for, if they are installed (running, on Windows). */
    juce::StringArray getPopularAppIds();

    /** Draws a built-in icon into the area. */
    void drawGeneric (juce::Graphics&, const juce::String& id, juce::Rectangle<float> area, juce::Colour colour);

    /** Real app icon (cached). Invalid image when the app isn't installed / on other platforms. */
    juce::Image getAppIcon (const juce::String& appId);

    /** Display name of an installed app ("Discord"), or {} if unknown. */
    juce::String getAppName (const juce::String& appId);

    /** Resolves "" (automatic) for a channel to a concrete id. */
    juce::String resolve (const Source& source);

    /** Draws whatever the id resolves to (app icon image or generic line icon). */
    void draw (juce::Graphics&, const juce::String& resolvedId, juce::Rectangle<float> area, juce::Colour colour);
}

/** Small clickable icon tile used in the strip header. */
class ChannelIconButton final : public juce::Button
{
public:
    ChannelIconButton() : Button ("icon") {}

    void setIconId (const juce::String& resolvedId, juce::Colour c) { iconId = resolvedId; colour = c; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String iconId;
    juce::Colour colour;
};

/** Grid of icons shown in a call-out box. */
class IconPicker final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    IconPicker (const juce::String& currentId, const juce::StringArray& assignedAppIds,
                std::function<void (const juce::String&)> onPick);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Cell { juce::String id, tooltip; juce::Rectangle<int> bounds; };
    int cellAt (juce::Point<int>) const;

    juce::String current;
    std::vector<Cell> genericCells, appCells;
    juce::Rectangle<int> autoBounds;
    std::function<void (const juce::String&)> onPick;
    int hover = -2;
    int appHeaderY = 0;
};
