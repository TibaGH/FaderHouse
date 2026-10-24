#include "PluginChainEditor.h"

using namespace juce;

PluginChainEditor::PluginChainEditor (PluginChain& c, MixerEngine& e, PluginWindowManager& w,
                                      std::function<String()> ctx)
    : chain (c), engine (e), windows (w), contextName (std::move (ctx))
{
    addButton.setTooltip ("Add a VST3 / AU plugin");
    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    // Slots scroll when there are more plugins than fit; the faders below keep their size.
    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    refresh();
}

void PluginChainEditor::refresh()
{
    slots.clear();
    removeButtons.clear();

    for (int i = 0; i < chain.size(); ++i)
    {
        auto* b = slots.add (new SlotButton());
        auto* p = chain.get (i);
        const bool bypassed = chain.isBypassed (i);

        b->setButtonText (p != nullptr ? p->getName() : String ("?"));
        b->setColour (TextButton::buttonColourId, bypassed ? Theme::background : Theme::panelRaised.brighter (0.08f));
        b->setColour (TextButton::textColourOffId, bypassed ? Theme::textDim : Theme::text);
        b->setTooltip (bypassed ? "Bypassed - right-click for options" : "Click to open, right-click for options");
        b->onClick = [this, i] { openEditor (i); };
        b->onMouseUp = [this, i] (const MouseEvent&) { showSlotMenu (i); };
        rowsHolder.addAndMakeVisible (b);

        auto* x = removeButtons.add (new TextButton (String::fromUTF8 ("\xc3\x97")));   // multiplication sign
        x->setTooltip ("Remove " + b->getButtonText());
        x->setColour (TextButton::buttonColourId, Colours::transparentBlack);
        x->setColour (TextButton::textColourOffId, Theme::textDim);
        x->setConnectedEdges (Button::ConnectedOnLeft);
        x->onClick = [this, i]
        {
            // Defer: removing destroys this button.
            SafePointer<PluginChainEditor> safe (this);
            MessageManager::callAsync ([safe, i]
            {
                if (safe == nullptr) return;
                safe->engine.removePlugin (safe->chain, i);
                safe->refresh();
            });
        };
        rowsHolder.addAndMakeVisible (x);
    }

    addButton.setEnabled (chain.size() < PluginChain::maxSlots);
    resized();
    repaint();
}

void PluginChainEditor::paint (Graphics& g)
{
    g.setColour (Theme::background.withAlpha (0.6f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

void PluginChainEditor::resized()
{
    auto area = getLocalBounds().reduced (3);
    addButton.setBounds (area.removeFromBottom (rowHeight));
    area.removeFromBottom (2);
    viewport.setBounds (area);

    const bool needsScroll = slots.size() * (rowHeight + 2) > area.getHeight();
    const int width = area.getWidth() - (needsScroll ? viewport.getScrollBarThickness() + 2 : 0);
    rowsHolder.setSize (width, jmax (area.getHeight(), slots.size() * (rowHeight + 2)));

    for (int i = 0; i < slots.size(); ++i)
    {
        auto row = Rectangle<int> (0, i * (rowHeight + 2), width, rowHeight);
        removeButtons[i]->setBounds (row.removeFromRight (18));
        slots[i]->setBounds (row);
        slots[i]->setConnectedEdges (Button::ConnectedOnRight);
    }
}

void PluginChainEditor::openEditor (int index)
{
    if (auto* p = chain.get (index))
        windows.show (*p, contextName ? contextName() : String());
}

void PluginChainEditor::showAddMenu()
{
    PopupMenu menu;
    const auto types = engine.knownPlugins.getTypes();

    if (types.isEmpty())
    {
        menu.addItem ("No plugins scanned yet - scan now...", [this] { if (onScanRequested) onScanRequested(); });
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (addButton));
        return;
    }

    KnownPluginList::addToMenu (menu, types, KnownPluginList::sortByManufacturer);
    menu.addSeparator();
    menu.addItem (-1, "Scan for new plugins...");

    SafePointer<PluginChainEditor> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (addButton),
                        [safe, types] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == -1)
        {
            if (safe->onScanRequested) safe->onScanRequested();
            return;
        }

        const int index = KnownPluginList::getIndexChosenByMenu (types, result);
        if (! isPositiveAndBelow (index, types.size()))
            return;

        String error;
        if (! safe->engine.addPlugin (safe->chain, types.getReference (index), error))
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not load plugin", error);
            return;
        }

        safe->refresh();
        safe->openEditor (safe->chain.size() - 1);
    });
}

void PluginChainEditor::showSlotMenu (int index)
{
    if (! isPositiveAndBelow (index, chain.size()))
        return;

    PopupMenu menu;
    menu.addItem (1, "Open editor");
    menu.addItem (2, "Bypass", true, chain.isBypassed (index));
    menu.addSeparator();
    menu.addItem (3, "Move up", index > 0);
    menu.addItem (4, "Move down", index < chain.size() - 1);
    menu.addSeparator();
    menu.addItem (5, "Remove");

    if (const int latency = chain.getTotalLatencySamples(); latency > 0)
        menu.addSectionHeader ("Chain latency: " + String (latency) + " samples");

    SafePointer<PluginChainEditor> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (slots[index]), [safe, index] (int result)
    {
        if (safe == nullptr)
            return;

        auto& self = *safe;
        switch (result)
        {
            case 1: self.openEditor (index); break;
            case 2: self.chain.setBypassed (index, ! self.chain.isBypassed (index)); break;
            case 3: self.chain.move (index, index - 1); break;
            case 4: self.chain.move (index, index + 1); break;
            case 5: self.engine.removePlugin (self.chain, index); break;
            default: return;
        }

        self.refresh();
    });
}
