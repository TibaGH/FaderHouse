#include "FaderhouseLookAndFeel.h"

using namespace juce;

FaderhouseLookAndFeel::FaderhouseLookAndFeel()
    : LookAndFeel_V4 (LookAndFeel_V4::ColourScheme {
          Theme::panel,          // windowBackground
          Theme::background,     // widgetBackground
          Theme::panelRaised,    // menuBackground
          Theme::outline,        // outline
          Theme::text,           // defaultText
          Theme::panelRaised,    // defaultFill
          Theme::text,           // highlightedText
          Theme::accent,         // highlightedFill
          Theme::text })         // menuText
{
    setColour (ResizableWindow::backgroundColourId, Theme::background);
    setColour (TextButton::buttonColourId, Theme::panelRaised);
    setColour (TextButton::buttonOnColourId, Theme::accent);
    setColour (TextButton::textColourOffId, Theme::text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (ComboBox::backgroundColourId, Theme::panelRaised);
    setColour (ComboBox::outlineColourId, Theme::outline);
    setColour (Label::textColourId, Theme::text);
    setColour (Slider::textBoxTextColourId, Theme::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.85f));
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (ScrollBar::thumbColourId, Theme::outline.brighter (0.3f));
    setColour (TooltipWindow::backgroundColourId, Theme::panelRaised);
}

void FaderhouseLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           Slider::SliderStyle style, Slider& slider)
{
    if (style != Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float cx = bounds.getCentreX();

    // groove
    g.setColour (Theme::background);
    g.fillRoundedRectangle (cx - 2.5f, bounds.getY(), 5.0f, bounds.getHeight(), 2.5f);

    // 0 dB tick
    const double zeroDb = slider.valueToProportionOfLength (0.0);
    const float zeroY = bounds.getBottom() - (float) zeroDb * bounds.getHeight();
    g.setColour (Theme::textDim.withAlpha (0.6f));
    g.drawHorizontalLine ((int) zeroY, cx - 12.0f, cx + 12.0f);

    // filled part
    g.setColour (slider.findColour (Slider::trackColourId));
    g.fillRoundedRectangle (cx - 2.5f, sliderPos, 5.0f, bounds.getBottom() - sliderPos, 2.5f);

    // cap
    const auto cap = Rectangle<float> (0, 0, jmin (34.0f, (float) width - 4.0f), 16.0f).withCentre ({ cx, sliderPos });
    g.setColour (Theme::panelRaised.brighter (0.25f));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (Theme::text);
    g.drawHorizontalLine ((int) cap.getCentreY(), cap.getX() + 4.0f, cap.getRight() - 4.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);
}

void FaderhouseLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour& backgroundColour,
                                               bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto base = backgroundColour;

    if (isButtonDown)           base = base.brighter (0.15f);
    else if (isMouseOverButton) base = base.brighter (0.07f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, 4.0f);

    if (! button.getToggleState())
    {
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }
}

Font FaderhouseLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return FontOptions (jmin (14.0f, (float) buttonHeight * 0.55f));
}

Font FaderhouseLookAndFeel::getLabelFont (Label& label)
{
    return label.getFont();
}
