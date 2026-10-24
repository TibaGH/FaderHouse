#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/AppLogo.h"
#include "UI/ChannelIcons.h"
#include "UI/FaderhouseLookAndFeel.h"
using namespace juce;
static void save (const Image& img, const String& path)
{
    File f (path); f.deleteFile(); FileOutputStream out (f); PNGImageFormat().writeImageToStream (img, out);
}
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const String dir = argc > 1 ? argv[1] : ".";
    for (int size : { 1024, 256, 64 })
    {
        Image img (Image::ARGB, size, size, true);
        Graphics g (img);
        AppLogo::draw (g, { 0.0f, 0.0f, (float) size, (float) size });
        save (img, dir + "/icon_" + String (size) + ".png");
    }
    // icon sheet
    const auto& ids = ChannelIcons::getGenericIds();
    Image sheet (Image::ARGB, 12 * 56 + 16, 2 * 64 + 16, true);
    Graphics g (sheet);
    g.fillAll (Colour (0xff1b1e24));
    for (size_t i = 0; i < ids.size(); ++i)
    {
        ChannelIcons::drawGeneric (g, ids[i].id, Rectangle<float> (8.0f + (float) i * 56.0f, 8.0f, 48.0f, 48.0f), Colours::white);
        ChannelIcons::drawGeneric (g, ids[i].id, Rectangle<float> (20.0f + (float) i * 56.0f, 80.0f, 24.0f, 24.0f), Colour (0xff6aa7f0));
    }
    save (sheet, dir + "/icons.png");

    // DMG window background (640x400 points, @1x and @2x).
    for (int scale : { 1, 2 })
    {
        Image bg (Image::RGB, 640 * scale, 400 * scale, true);
        Graphics gb (bg);
        gb.addTransform (AffineTransform::scale ((float) scale));
        gb.setGradientFill (ColourGradient (Colour (0xff2a2c31), 0, 0, Colour (0xff141518), 0, 400, false));
        gb.fillAll();
        gb.setColour (Colours::white.withAlpha (0.015f));
        for (float x = 0; x < 640; x += 4) gb.fillRect (x, 0.0f, 1.0f, 400.0f);

        Path arrow;
        arrow.startNewSubPath (250.0f, 190.0f);
        arrow.lineTo (380.0f, 190.0f);
        gb.setColour (Colour (0xff52c7a6));
        gb.strokePath (arrow, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        Path head;
        head.addTriangle (378.0f, 174.0f, 402.0f, 190.0f, 378.0f, 206.0f);
        gb.fillPath (head);

        gb.setColour (Colour (0xffe7e9ec));
        gb.setFont (FontOptions (20.0f, Font::bold));
        gb.drawText ("Drag Faderhouse to Applications", Rectangle<float> (0, 300, 640, 30), Justification::centred);
        gb.setColour (Colour (0xff8b919b));
        gb.setFont (FontOptions (13.0f));
        gb.drawText ("Then open it from Applications. It offers to install Mic FX on first launch.",
                     Rectangle<float> (0, 330, 640, 24), Justification::centred);

        save (bg, dir + (scale == 1 ? "/dmg-background.png" : "/dmg-background@2x.png"));
    }
    return 0;
}
