#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/AppLogo.h"
using namespace juce;
int previewMain (const String& dir)
{
    for (int v = 0; v < 2; ++v)
    {
        Image img (Image::ARGB, 512, 512, true);
        { Graphics g (img); AppLogo::draw (g, { 0, 0, 512, 512 }, true, v == 1); }
        Image small (Image::ARGB, 64, 64, true);
        { Graphics g (small); AppLogo::draw (g, { 0, 0, 64, 64 }, true, v == 1); }
        File f (dir + "/v3_" + String (v == 1 ? "B_roof" : "A") + ".png"); f.deleteFile();
        FileOutputStream o (f); PNGImageFormat().writeImageToStream (img, o);
        File f2 (dir + "/v3_" + String (v == 1 ? "B_roof" : "A") + "_64.png"); f2.deleteFile();
        FileOutputStream o2 (f2); PNGImageFormat().writeImageToStream (small, o2);
    }
    return 0;
}
