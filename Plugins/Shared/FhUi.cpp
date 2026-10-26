#include "FhUi.h"

using namespace juce;

namespace fh::ui
{
//==============================================================================
Look::Look()
{
    setColour (ResizableWindow::backgroundColourId, Colours::background);
    setColour (Label::textColourId, Colours::text);
    setColour (Slider::textBoxTextColourId, Colours::text);
    setColour (Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (TextButton::buttonColourId, Colours::panelRaised);
    setColour (TextButton::buttonOnColourId, Colours::accent);
    setColour (TextButton::textColourOffId, Colours::textDim);
    setColour (TextButton::textColourOnId, Colours::background);
    setColour (ComboBox::backgroundColourId, Colours::panelRaised);
    setColour (ComboBox::textColourId, Colours::text);
    setColour (ComboBox::outlineColourId, Colours::outline);
    setColour (ComboBox::arrowColourId, Colours::textDim);
    setColour (PopupMenu::backgroundColourId, Colours::panel);
    setColour (PopupMenu::textColourId, Colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, Colours::text);
    setColour (TextEditor::backgroundColourId, Colours::panelRaised);
    setColour (TextEditor::textColourId, Colours::text);
    setColour (TextEditor::outlineColourId, Colours::outline);
    setColour (TooltipWindow::backgroundColourId, Colours::panelRaised);
    setColour (TooltipWindow::textColourId, Colours::text);
}

void Look::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                             float startAngle, float endAngle, Slider& slider)
{
    const auto colour = slider.findColour (Slider::rotarySliderFillColourId);
    auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre (size, size);
    const auto centre = bounds.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = jmax (2.5f, size * 0.07f);

    // Track and value arc (from the middle for bipolar controls).
    Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline);
    g.strokePath (bg, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, jmin (zeroAngle, angle), jmax (zeroAngle, angle), true);
    g.setColour (slider.isEnabled() ? colour : Colours::textDim);
    g.strokePath (arc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    // Knob body.
    const float body = radius - track * 2.2f;
    ColourGradient grad (Colour (0xff3a3f49), centre.x, centre.y - body, Colour (0xff1f2228), centre.x, centre.y + body, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - body, centre.y - body, body * 2, body * 2);
    g.setColour (Colours::background.withAlpha (0.8f));
    g.drawEllipse (centre.x - body, centre.y - body, body * 2, body * 2, 1.2f);

    // Pointer.
    const Point<float> tip (centre.x + std::sin (angle) * body * 0.82f, centre.y - std::cos (angle) * body * 0.82f);
    const Point<float> from (centre.x + std::sin (angle) * body * 0.30f, centre.y - std::cos (angle) * body * 0.30f);
    g.setColour (Colours::text);
    g.drawLine ({ from, tip }, jmax (2.0f, size * 0.035f));
}

void Look::drawButtonBackground (Graphics& g, Button& b, const Colour& colour, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour (TextButton::buttonOnColourId) : colour;
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? fill.brighter (0.2f) : Colours::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void Look::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    Path arrow;
    const float ax = (float) width - 16.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (arrow);
}

Label* Look::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (12.0f));
    l->setColour (Label::textColourId, Colours::textDim);
    l->setColour (Label::outlineWhenEditingColourId, Colours::accent);
    l->setJustificationType (Justification::centred);
    return l;
}

//==============================================================================
Knob::Knob (PluginBase& p, const String& paramId, const String& name, Colour colour)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    slider.setColour (Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);

    label.setText (name, dontSendNotification);
    label.setFont (FontOptions (12.5f, Font::bold));
    label.setColour (Label::textColourId, Colours::text);
    label.setJustificationType (Justification::centred);
    addAndMakeVisible (label);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.state, paramId, slider);

    // Double-click resets to the default.
    if (auto* param = p.state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

//==============================================================================
ChoiceButtons::ChoiceButtons (PluginBase& p, const String& id, const StringArray& labels) : processor (p), paramId (id)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new TextButton (labels[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i]
        {
            processor.setParam (paramId, (float) i);
            timerCallback();
            if (onChange) onChange();
        };
        addAndMakeVisible (b);
    }
    timerCallback();
    startTimerHz (10);
}

void ChoiceButtons::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / jmax (1, buttons.size());
    for (auto* b : buttons)
        b->setBounds (r.removeFromLeft (w).reduced (2, 0));
}

void ChoiceButtons::timerCallback()
{
    const int current = processor.choice (paramId.toRawUTF8());
    if (current == shown)
        return;
    shown = current;
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == current, dontSendNotification);
    if (onChange) onChange();
}

//==============================================================================
ToggleKey::ToggleKey (PluginBase& p, const String& id, const String& caption) : TextButton (caption)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.state, id, *this);
}

//==============================================================================
static Colour levelColour (float db)
{
    return db > -6.0f ? Colours::red : db > -18.0f ? Colours::meterMid : Colours::green;
}

LevelBar::LevelBar (std::function<float()> s, const String& c) : source (std::move (s)), caption (c)
{
    startTimerHz (30);
}

void LevelBar::timerCallback()
{
    const float db = source ? source() : -100.0f;
    shownDb = db > shownDb ? db : jmax (db, shownDb - 1.5f);   // fall ~45 dB/s
    if (db >= holdDb) { holdDb = db; holdCount = 45; }
    else if (--holdCount <= 0) holdDb = jmax (-100.0f, holdDb - 2.0f);
    repaint();
}

void LevelBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom (16.0f);
    g.setColour (Colours::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawText (caption, text, Justification::centred);

    auto bar = r.withSizeKeepingCentre (jmin (14.0f, r.getWidth()), r.getHeight());
    g.setColour (Colours::background);
    g.fillRoundedRectangle (bar, 3.0f);

    auto toY = [&bar] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, bar.getBottom(), bar.getY()); };

    if (shownDb > -60.0f)
    {
        const float top = toY (shownDb);
        ColourGradient grad (Colours::red, bar.getX(), toY (0.0f), Colours::green, bar.getX(), toY (-30.0f), false);
        grad.addColour (0.25, Colours::meterMid);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar.withTop (top), 3.0f);
    }

    if (holdDb > -60.0f)
    {
        g.setColour (levelColour (holdDb));
        g.fillRect (bar.getX(), toY (holdDb) - 1.0f, bar.getWidth(), 2.0f);
    }

    if (thresholdDb)
    {
        const float y = toY (thresholdDb());
        g.setColour (Colours::text);
        g.fillRect (bar.getX() - 4.0f, y - 1.0f, bar.getWidth() + 8.0f, 2.0f);
    }
}

//==============================================================================
GainBar::GainBar (std::function<float()> s, const String& c, float rangeDb, bool bi)
    : source (std::move (s)), caption (c), range (rangeDb), bipolar (bi)
{
    startTimerHz (30);
}

void GainBar::timerCallback()
{
    const float v = source ? source() : 0.0f;
    shown = shown + (v - shown) * 0.35f;
    repaint();
}

void GainBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom (16.0f);
    g.setColour (Colours::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawText (caption, text, Justification::centred);

    auto bar = r.withSizeKeepingCentre (jmin (14.0f, r.getWidth()), r.getHeight());
    g.setColour (Colours::background);
    g.fillRoundedRectangle (bar, 3.0f);

    if (bipolar)
    {
        const float mid = bar.getCentreY();
        const float h = jlimit (-1.0f, 1.0f, shown / range) * bar.getHeight() * 0.5f;
        g.setColour (h > 0 ? Colours::green : Colours::red);
        if (h > 0) g.fillRect (bar.getX(), mid - h, bar.getWidth(), h);
        else       g.fillRect (bar.getX(), mid, bar.getWidth(), -h);
        g.setColour (Colours::textDim);
        g.fillRect (bar.getX() - 3.0f, mid - 0.5f, bar.getWidth() + 6.0f, 1.0f);
    }
    else
    {
        const float h = jlimit (0.0f, 1.0f, shown / range) * bar.getHeight();
        g.setColour (Colours::red.withAlpha (0.9f));
        g.fillRoundedRectangle (bar.withHeight (h), 3.0f);
    }
}

//==============================================================================
Lamp::Lamp (std::function<float()> s, const String& c, Colour col) : source (std::move (s)), caption (c), colour (col)
{
    startTimerHz (30);
}

void Lamp::timerCallback()
{
    const float v = jlimit (0.0f, 1.0f, source ? source() : 0.0f);
    shown = v > shown ? v : shown * 0.85f;
    repaint();
}

void Lamp::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto dot = r.removeFromLeft (r.getHeight()).reduced (3.0f);
    g.setColour (colour.withAlpha (0.15f + 0.85f * shown));
    g.fillEllipse (dot);
    if (shown > 0.3f)
    {
        g.setColour (colour.withAlpha (0.25f * shown));
        g.fillEllipse (dot.expanded (3.0f));
    }
    g.setColour (Colours::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText (caption, r.withTrimmedLeft (6.0f), Justification::centredLeft);
}

//==============================================================================
EditorBase::EditorBase (PluginBase& p, const String& t, const String& s)
    : AudioProcessorEditor (p), base (p), title (t), subtitle (s)
{
    setLookAndFeel (&look);
}

EditorBase::~EditorBase()
{
    setLookAndFeel (nullptr);
}

void EditorBase::paint (Graphics& g)
{
    g.fillAll (Colours::background);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (Colours::panel);
    g.fillRect (header);
    g.setColour (Colours::outline);
    g.fillRect (header.removeFromBottom (1));

    // Small three-fader mark, as in the app icon.
    auto mark = Rectangle<float> (14.0f, 11.0f, 22.0f, 22.0f);
    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (mark, 5.0f);
    for (int i = 0; i < 3; ++i)
    {
        const float x = mark.getX() + 5.0f + (float) i * 6.0f;
        g.setColour (Colours::outline.brighter (0.4f));
        g.fillRect (x, mark.getY() + 4.0f, 1.5f, mark.getHeight() - 8.0f);
        const float capY = mark.getY() + 5.0f + (float) ((i * 5 + 3) % 9);
        g.setColour (i == 1 ? Colours::accent : Colours::text);
        g.fillRoundedRectangle (x - 2.0f, capY, 5.5f, 3.5f, 1.0f);
    }

    g.setColour (Colours::text);
    g.setFont (FontOptions (16.0f, Font::bold));
    g.drawText (title, 46, 6, 400, 20, Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (FontOptions (11.5f));
    g.drawText (subtitle, 46, 24, 500, 14, Justification::centredLeft);
}

void EditorBase::paintGroup (Graphics& g, Rectangle<int> area, const String& groupTitle)
{
    auto r = area.toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (groupTitle.isNotEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (groupTitle.toUpperCase(), area.reduced (12, 6).removeFromTop (14), Justification::centredLeft);
    }
}
}
