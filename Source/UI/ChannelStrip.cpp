#include "ChannelStrip.h"
#include <map>

using namespace juce;

String prettyAppName (const String& appId)
{
    if (auto name = ChannelIcons::getAppName (appId); name.isNotEmpty())
        return name;

    if (appId.startsWith ("exe:"))
        return appId.substring (4);

    if (appId.startsWithIgnoreCase ("com.apple.WebKit"))
        return "Safari";

    if (appId == "company.thebrowser.Browser")
        return "Arc";

    auto last = appId.fromLastOccurrenceOf (".", false, false);
    return last.isNotEmpty() ? last : appId;
}

void paintStripPanel (Graphics& g, Rectangle<float> b, Colour band, bool highlighted)
{
    b = b.reduced (2.0f);

    ColourGradient metal (Theme::stripTop, b.getX(), b.getY(), Theme::stripBottom, b.getX(), b.getBottom(), false);
    g.setGradientFill (metal);
    g.fillRoundedRectangle (b, 5.0f);

    // fine vertical brushing
    g.setColour (Colours::white.withAlpha (0.012f));
    for (float x = b.getX() + 2.0f; x < b.getRight() - 2.0f; x += 3.0f)
        g.fillRect (x, b.getY() + 6.0f, 1.0f, b.getHeight() - 12.0f);

    g.setColour (band);
    g.fillRoundedRectangle (b.withHeight (4.0f).reduced (6.0f, 0.0f), 2.0f);

    // screws
    auto screw = [&g] (Point<float> c)
    {
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        ColourGradient head (Colour (0xff6b6d72), c.x, c.y - 4.0f, Colour (0xff2b2c30), c.x, c.y + 4.0f, false);
        g.setGradientFill (head);
        g.fillEllipse (c.x - 3.8f, c.y - 3.8f, 7.6f, 7.6f);
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawLine (c.x - 2.4f, c.y - 2.4f, c.x + 2.4f, c.y + 2.4f, 1.1f);
        g.drawLine (c.x - 2.4f, c.y + 2.4f, c.x + 2.4f, c.y - 2.4f, 1.1f);
    };
    screw ({ b.getCentreX(), b.getBottom() - 9.0f });

    g.setColour (highlighted ? Theme::accent : Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (b, 5.0f, highlighted ? 1.5f : 1.0f);
}

//==============================================================================
ChannelStrip::ChannelStrip (Source& s, MixerEngine& e, PluginWindowManager& windows)
    : source (s), engine (e),
      chainEditor (s.chain, e, windows, [this] { return source.name; }),
      meter (s.meter)
{
    iconButton.setTooltip ("Choose an icon");
    iconButton.onClick = [this] { showIconPicker(); };
    addAndMakeVisible (iconButton);

    nameLabel.setFont (FontOptions (14.0f, Font::bold));
    nameLabel.setJustificationType (Justification::centredLeft);
    nameLabel.setEditable (false, true, false);
    nameLabel.setTooltip ("Drag to move the channel, double-click to rename");
    nameLabel.addMouseListener (this, false);
    nameLabel.onTextChange = [this]
    {
        source.name = nameLabel.getText().trim().isEmpty() ? String ("Channel") : nameLabel.getText().trim();
    };
    addAndMakeVisible (nameLabel);

    menuButton.onClick = [this] { showChannelMenu(); };
    addAndMakeVisible (menuButton);

    switch (source.kind)
    {
        case SourceKind::Input:
            inputChoice.onChange = [this]
            {
                const int id = inputChoice.getSelectedId();
                if (id >= 1000) { source.inputChannel.store (id - 1000); source.stereoInput.store (true); }
                else if (id > 0) { source.inputChannel.store (id - 1); source.stereoInput.store (false); }
            };
            addAndMakeVisible (inputChoice);
            break;

        case SourceKind::App:
            appsButton.onClick = [this] { showAppPicker(); };
            appsButton.setTooltip ("Choose which apps play through this channel");
            addAndMakeVisible (appsButton);
            break;

        case SourceKind::System:
            infoLabel.setText ("All other apps", dontSendNotification);
            infoLabel.setColour (Label::textColourId, Theme::textDim);
            infoLabel.setFont (FontOptions (12.0f));
            infoLabel.setJustificationType (Justification::centred);
            addAndMakeVisible (infoLabel);
            break;
    }

    chainEditor.onScanRequested = [this] { if (onScanRequested) onScanRequested(); };
    addAndMakeVisible (chainEditor);
    addAndMakeVisible (meter);

    fader.setAccent (kindColour());
    fader.onValueChange = [this] (double db)
    {
        source.send[(size_t) slot].store (sliderDbToGain ((float) db));
        readout.setText (ConsoleFader::formatDb (db), dontSendNotification);
    };
    addAndMakeVisible (fader);

    muteButton.setTooltip ("Mute this channel in the selected mix");
    muteButton.onClick = [this] { source.sendMute[(size_t) slot].store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    soloButton.setTooltip ("Solo this channel in the selected mix (Alt-click: solo only this one)");
    soloButton.onClick = [this]
    {
        const bool on = soloButton.getToggleState();
        if (on && ModifierKeys::currentModifiers.isAltDown())
            for (auto& other : engine.getSources())
                other->sendSolo[(size_t) slot].store (false);

        source.sendSolo[(size_t) slot].store (on);
        if (auto* parent = getParentComponent())
            parent->repaint();
    };
    addAndMakeVisible (soloButton);

    readout.setFont (FontOptions (12.0f, Font::bold));
    readout.setJustificationType (Justification::centred);
    readout.setColour (Label::textColourId, Theme::text);
    readout.setColour (Label::backgroundColourId, Colours::black.withAlpha (0.45f));
    addAndMakeVisible (readout);

    mixLabel.setFont (FontOptions (10.5f));
    mixLabel.setJustificationType (Justification::centred);
    mixLabel.setColour (Label::textColourId, Theme::textDim);
    addAndMakeVisible (mixLabel);

    refreshFromModel();
}

Colour ChannelStrip::kindColour() const
{
    switch (source.kind)
    {
        case SourceKind::Input:  return Theme::input;
        case SourceKind::App:    return Theme::app;
        case SourceKind::System: return Theme::system;
    }
    return Theme::app;
}

void ChannelStrip::setMix (int newSlot, const String& newMixName)
{
    slot = jlimit (0, kMaxOutputs - 1, newSlot);
    mixName = newMixName;

    const double db = gainToSliderDb (source.send[(size_t) slot].load());
    fader.setValueDb (db, dontSendNotification);
    readout.setText (ConsoleFader::formatDb (db), dontSendNotification);
    muteButton.setToggleState (source.sendMute[(size_t) slot].load(), dontSendNotification);
    soloButton.setToggleState (source.sendSolo[(size_t) slot].load(), dontSendNotification);

    String text = "to " + mixName;
    if (source.kind == SourceKind::Input && source.toMicFx.load())
        text << "  +  Mic FX";
    mixLabel.setText (text, dontSendNotification);
    repaint();
}

void ChannelStrip::updateMeters()
{
    meter.update();

    // Reflect solo changes made on other strips (Alt-click) and the global mic mute.
    const bool solo = source.sendSolo[(size_t) slot].load();
    if (solo != soloButton.getToggleState())
        soloButton.setToggleState (solo, dontSendNotification);

    const bool micMuted = source.kind == SourceKind::Input && engine.isMicMuted();
    if (micMuted != showingMicMuted)
    {
        showingMicMuted = micMuted;
        repaint();
    }
}

void ChannelStrip::updateIcon()
{
    iconButton.setIconId (ChannelIcons::resolve (source), kindColour());
}

void ChannelStrip::refreshFromModel()
{
    nameLabel.setText (source.name, dontSendNotification);
    updateIcon();

    if (source.kind == SourceKind::Input)
        rebuildInputChoices();

    if (source.kind == SourceKind::App)
    {
        StringArray names;
        for (auto& id : source.appIds)
            names.addIfNotAlreadyThere (prettyAppName (id));

        appsButton.setButtonText (names.isEmpty() ? String ("Assign apps...")
                                  : names.size() == 1 ? names[0]
                                  : names[0] + " +" + String (names.size() - 1));
        appsButton.setTooltip (names.isEmpty() ? "Choose which apps play through this channel"
                                               : names.joinIntoString (", "));
    }

    chainEditor.refresh();
}

void ChannelStrip::rebuildInputChoices()
{
    inputChoice.clear (dontSendNotification);
    const auto names = engine.getActiveInputChannelNames();

    if (names.isEmpty())
    {
        inputChoice.setTextWhenNothingSelected ("Enable inputs...");
        inputChoice.setTooltip ("Turn on inputs of your interface in Settings > Audio interface");
        return;
    }

    for (int i = 0; i < names.size(); ++i)
        inputChoice.addItem (names[i], i + 1);

    inputChoice.addSeparator();
    for (int i = 0; i + 1 < names.size(); ++i)
        inputChoice.addItem (names[i] + " + " + names[i + 1] + " (stereo)", 1000 + i);

    const int ch = source.inputChannel.load();
    inputChoice.setSelectedId (source.stereoInput.load() ? 1000 + ch : ch + 1, dontSendNotification);
}

void ChannelStrip::showIconPicker()
{
    SafePointer<ChannelStrip> safe (this);
    auto picker = std::make_unique<IconPicker> (source.icon, source.appIds, [safe] (const String& id)
    {
        if (safe == nullptr)
            return;
        safe->source.icon = id;
        safe->updateIcon();
    });

    CallOutBox::launchAsynchronously (std::move (picker), iconButton.getScreenBounds(), nullptr);
}

void ChannelStrip::showChannelMenu()
{
    PopupMenu menu;
    menu.addItem (1, "Rename");
    menu.addItem (7, "Choose icon...");
    if (source.kind == SourceKind::App)
        menu.addItem (2, "Assign apps...");
    if (source.kind == SourceKind::Input)
        menu.addItem (8, "Send to Mic FX", true, source.toMicFx.load());
    menu.addSeparator();
    menu.addItem (3, "Move left");
    menu.addItem (4, "Move right");
    menu.addSeparator();
    menu.addItem (6, "Remove channel");

    SafePointer<ChannelStrip> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (menuButton), [safe] (int result)
    {
        if (safe == nullptr)
            return;

        auto& self = *safe;
        switch (result)
        {
            case 1: self.nameLabel.showEditor(); break;
            case 2: self.showAppPicker(); break;
            case 3: self.engine.moveSource (&self.source, -1); break;  // UI rebuilds asynchronously
            case 4: self.engine.moveSource (&self.source, 1); break;
            case 7: self.showIconPicker(); break;
            case 8:
                self.source.toMicFx.store (! self.source.toMicFx.load());
                self.setMix (self.slot, self.mixName);
                break;
            case 6:
            {
                auto* eng = &self.engine;
                auto* src = &self.source;
                AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Remove channel",
                                              "Remove \"" + src->name + "\" and its plugins?", "Remove", "Cancel", nullptr,
                                              ModalCallbackFunction::create ([eng, src] (int ok) { if (ok) eng->removeSource (src); }));
                break;
            }
            default: break;
        }
    });
}

void ChannelStrip::showAppPicker()
{
    PopupMenu menu;
    menu.addSectionHeader ("Apps in \"" + source.name + "\"");

    if (! engine.isCaptureSupported())
        menu.addItem (-2, engine.getCaptureStatus(), false);

    const bool showSystem = engine.settings->getBoolValue ("showSystemProcesses", false);

    // Who already owns what (an app can only be in one channel).
    std::map<String, String> owner;
    for (auto& s : engine.getSources())
        if (s.get() != &source)
            for (auto& id : s->appIds)
                owner[id] = s->name;

    const auto running = engine.listAudioProcesses();
    StringArray listed;
    std::vector<String> itemIds;

    auto addEntry = [&] (const String& id, const String& label, bool playing)
    {
        const bool ticked = source.appIds.contains (id);
        String text = label;
        if (playing)
            text << String::fromUTF8 ("   \xe2\x97\x8f");   // dot: currently playing
        if (! ticked && owner.count (id) > 0)
            text << "   - in " << owner[id];

        itemIds.push_back (id);

        PopupMenu::Item item (text);
        item.itemID = (int) itemIds.size();
        item.isTicked = ticked;

        if (auto icon = ChannelIcons::getAppIcon (id); icon.isValid())
        {
            auto drawable = std::make_unique<DrawableImage>();
            drawable->setImage (icon);
            item.image = std::move (drawable);
        }

        menu.addItem (std::move (item));
        listed.add (id);
    };

    int hidden = 0;
    bool appsHeader = false, systemHeader = false;

    for (auto& p : running)
    {
        if (! p.isApp) { ++hidden; continue; }
        if (! appsHeader) { menu.addSectionHeader ("Apps using audio"); appsHeader = true; }
        addEntry (p.appId, p.displayName, p.isOutputting);
    }

    if (showSystem)
    {
        for (auto& p : running)
        {
            if (p.isApp) continue;
            if (! systemHeader) { menu.addSectionHeader ("System processes"); systemHeader = true; }
            addEntry (p.appId, p.displayName, p.isOutputting);
        }
    }

    bool assignedHeader = false;
    for (auto& id : source.appIds)
    {
        if (listed.contains (id))
            continue;

        if (! assignedHeader) { menu.addSectionHeader ("Assigned, not running"); assignedHeader = true; }
        addEntry (id, prettyAppName (id), false);
    }

    if (! appsHeader && ! assignedHeader && ! systemHeader)
        menu.addItem (-3, "No apps are using audio right now - start playing something first", false);

    menu.addSeparator();
    menu.addItem (-4, showSystem ? "Hide system processes" : "Show system processes (" + String (hidden) + ")");
    menu.addItem (-1, "Add app by ID...");

    SafePointer<ChannelStrip> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (appsButton).withStandardItemHeight (22),
                        [safe, itemIds, showSystem] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& self = *safe;

        if (result == -4)
        {
            self.engine.settings->setValue ("showSystemProcesses", ! showSystem);
            self.showAppPicker();
            return;
        }

        if (result == -1)
        {
            auto* w = new AlertWindow ("Add app",
                                       "Enter a bundle id (macOS, e.g. com.hnc.Discord) or exe:Name.exe (Windows).\n"
                                       "Helper processes of an app are included automatically.",
                                       MessageBoxIconType::NoIcon);
            w->addTextEditor ("id", {}, "App ID");
            w->addButton ("Add", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

            w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int ok)
            {
                const auto id = w->getTextEditorContents ("id").trim();
                if (ok == 1 && id.isNotEmpty() && safe != nullptr)
                {
                    auto ids = safe->source.appIds;
                    ids.addIfNotAlreadyThere (id);
                    safe->engine.setAppIds (safe->source, ids);
                    safe->refreshFromModel();
                }
            }), true);
            return;
        }

        if (! isPositiveAndBelow (result - 1, (int) itemIds.size()))
            return;

        const auto& id = itemIds[(size_t) (result - 1)];
        auto ids = self.source.appIds;

        if (ids.contains (id))
        {
            ids.removeString (id);
        }
        else
        {
            // Take it away from any other channel that had it.
            for (auto& s : self.engine.getSources())
            {
                if (s.get() != &self.source && s->appIds.contains (id))
                {
                    auto other = s->appIds;
                    other.removeString (id);
                    self.engine.setAppIds (*s, other);
                }
            }
            ids.add (id);
        }

        self.engine.setAppIds (self.source, ids);
        self.refreshFromModel();

        if (auto* parent = self.getParentComponent())
            for (auto* c : parent->getChildren())
                if (auto* strip = dynamic_cast<ChannelStrip*> (c))
                    strip->refreshFromModel();
    });
}

//==============================================================================
void ChannelStrip::mouseDown (const MouseEvent&)
{
    dragActive = false;
}

void ChannelStrip::mouseDrag (const MouseEvent& e)
{
    if (! dragActive)
    {
        if (e.getDistanceFromDragStart() < 5)
            return;
        dragActive = true;
        setMouseCursor (MouseCursor::DraggingHandCursor);
        if (onDragStart) onDragStart (*this);
    }

    if (onDrag) onDrag (*this);
}

void ChannelStrip::mouseUp (const MouseEvent&)
{
    if (! dragActive)
        return;

    dragActive = false;
    setMouseCursor (MouseCursor::NormalCursor);
    if (onDragEnd) onDragEnd (*this);
}

//==============================================================================
void ChannelStrip::paint (Graphics& g)
{
    paintStripPanel (g, getLocalBounds().toFloat(), kindColour(), false);
}

void ChannelStrip::paintOverChildren (Graphics& g)
{
    if (showingMicMuted)
    {
        const auto r = badgeArea.toFloat();
        g.setColour (Theme::mute.withAlpha (0.9f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Colours::white);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText ("MIC MUTED", r, Justification::centred);
    }
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (8, 0);
    area.removeFromTop (12);
    area.removeFromBottom (20); // screw

    auto header = area.removeFromTop (28);
    iconButton.setBounds (header.removeFromLeft (28));
    header.removeFromLeft (4);
    menuButton.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 22));
    nameLabel.setBounds (header);
    area.removeFromTop (4);

    auto info = area.removeFromTop (24);
    inputChoice.setBounds (info);
    appsButton.setBounds (info);
    infoLabel.setBounds (info);
    area.removeFromTop (6);

    // Bottom up: labels, fixed-size fader section, MIC MUTED badge. The plugin area gets the rest.
    mixLabel.setBounds (area.removeFromBottom (16));
    readout.setBounds (area.removeFromBottom (20).reduced (16, 1));
    area.removeFromBottom (4);

    auto section = area.removeFromBottom (faderSectionHeight);
    badgeArea = area.removeFromBottom (28).reduced (2, 4);
    chainEditor.setBounds (area);

    auto left = section.removeFromLeft (ConsoleButton::preferredWidth);
    muteButton.setBounds (left.removeFromTop (ConsoleButton::preferredHeight));
    left.removeFromTop (2);
    soloButton.setBounds (left.removeFromTop (ConsoleButton::preferredHeight));
    left.removeFromTop (10);
    meter.setBounds (left.reduced (10, 0));

    fader.setBounds (section);
}
