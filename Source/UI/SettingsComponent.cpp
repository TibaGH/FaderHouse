#include "SettingsComponent.h"
#include "FaderhouseLookAndFeel.h"
#include "VirtualMicPanel.h"

using namespace juce;

//==============================================================================
struct SettingsComponent::RoutingPanel final : public Component
{
    struct Choice
    {
        Output::Target target;
        int mainOffset = 0;
        String type, device;
    };

    explicit RoutingPanel (MixerEngine& e) : engine (e), micPanel (e)
    {
        addAndMakeVisible (micPanel);

        intro.setText ("Each mix goes to a pair of outputs on your interface, or to another device.",
                       dontSendNotification);

        micFxLabel.setText ("Mic FX output", dontSendNotification);
        micFxLabel.setFont (FontOptions (14.0f, Font::bold));
        addAndMakeVisible (micFxLabel);
        micFxStatus.setFont (FontOptions (12.0f));
        micFxStatus.setColour (Label::textColourId, Theme::textDim);
        addAndMakeVisible (micFxStatus);
        micFxChoice.onChange = [this]
        {
            const int id = micFxChoice.getSelectedId();
            if (id <= 1)
                engine.setMicFxNone();
            else if (isPositiveAndBelow (id - 2, (int) choices.size()))
                engine.setMicFxDevice (choices[(size_t) (id - 2)].type, choices[(size_t) (id - 2)].device);
            micFxStatus.setText (engine.getMicFx().status, dontSendNotification);
            if (onChanged) onChanged();
        };
        addAndMakeVisible (micFxChoice);

        mixesLabel.setText ("Mixes", dontSendNotification);
        mixesLabel.setFont (FontOptions (14.0f, Font::bold));
        addAndMakeVisible (mixesLabel);
        intro.setColour (Label::textColourId, Theme::textDim);
        intro.setJustificationType (Justification::topLeft);
        addAndMakeVisible (intro);

        addMixButton.onClick = [this]
        {
            if (auto* o = engine.addOutput ("Mix " + String (engine.getOutputs().size() + 1)))
                engine.setOutputToNone (*o);
            rebuild();
            if (onChanged) onChanged();
        };
        addAndMakeVisible (addMixButton);

        rescanButton.onClick = [this] { rebuild(); };
        addAndMakeVisible (rescanButton);

        rebuild();
    }

    void rebuild()
    {
        micPanel.refresh();
        rows.clear();
        choices.clear();

        // Possible destinations.
        const auto outs = engine.getActiveOutputChannelNames();
        for (int i = 0; i < outs.size(); i += 2)
            choices.push_back ({ Output::Target::Main, i, {}, {} });

        for (auto* type : engine.deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            for (auto& name : type->getDeviceNames (false))
                choices.push_back ({ Output::Target::Device, 0, type->getTypeName(), name });
        }

        // Mic FX device chooser (other devices only: the mic must not go back into your interface outs).
        micFxChoice.clear (dontSendNotification);
        micFxChoice.addItem ("Not connected", 1);
        int micSelected = 1;
        for (size_t i = 0; i < choices.size(); ++i)
        {
            const auto& c = choices[i];
            if (c.target != Output::Target::Device)
                continue;
            micFxChoice.addItem (c.device + "  (" + c.type + ")", (int) i + 2);
            if (c.type == engine.getMicFx().deviceType && c.device == engine.getMicFx().deviceName)
                micSelected = (int) i + 2;
        }
        micFxChoice.setSelectedId (micSelected, dontSendNotification);
        micFxStatus.setText (engine.getMicFx().status, dontSendNotification);

        const bool canRemove = engine.getOutputs().size() > 1;

        for (auto& o : engine.getOutputs())
        {
            auto* row = rows.add (new Row());
            row->output = o.get();

            row->name.setText (o->name, dontSendNotification);
            row->name.setFont (FontOptions (14.0f, Font::bold));
            row->addAndMakeVisible (row->name);

            row->status.setText (o->status, dontSendNotification);
            row->status.setFont (FontOptions (12.0f));
            row->status.setColour (Label::textColourId, Theme::textDim);
            row->addAndMakeVisible (row->status);

            auto& box = row->choice;
            box.addItem ("Not routed", 1);

            box.addSectionHeading ("Audio interface");
            for (size_t i = 0; i < choices.size(); ++i)
            {
                const auto& c = choices[i];
                if (c.target == Output::Target::Main)
                    box.addItem (outs[c.mainOffset] + (c.mainOffset + 1 < outs.size() ? " + " + outs[c.mainOffset + 1] : String()), (int) i + 2);
            }

            box.addSectionHeading ("Other device");
            for (size_t i = 0; i < choices.size(); ++i)
            {
                const auto& c = choices[i];
                if (c.target == Output::Target::Device)
                    box.addItem (c.device + "  (" + c.type + ")", (int) i + 2);
            }

            // current selection
            const auto target = (Output::Target) o->target.load();
            int selected = 1;
            for (size_t i = 0; i < choices.size(); ++i)
            {
                const auto& c = choices[i];
                if (c.target == target
                    && ((target == Output::Target::Main && c.mainOffset == o->mainChannelOffset.load())
                        || (target == Output::Target::Device && c.type == o->deviceType && c.device == o->deviceName)))
                    selected = (int) i + 2;
            }
            box.setSelectedId (selected, dontSendNotification);

            box.onChange = [this, row]
            {
                const int id = row->choice.getSelectedId();
                auto& out = *row->output;

                if (id <= 1)
                {
                    engine.setOutputToNone (out);
                }
                else
                {
                    const auto& c = choices[(size_t) (id - 2)];
                    if (c.target == Output::Target::Main)
                        engine.setOutputToMain (out, c.mainOffset);
                    else
                        engine.setOutputToDevice (out, c.type, c.device);
                }

                row->status.setText (out.status, dontSendNotification);
                if (onChanged) onChanged();
            };

            row->addAndMakeVisible (box);

            row->remove.setButtonText ("Remove");
            row->remove.setEnabled (canRemove);
            row->remove.setTooltip (canRemove ? "Remove this mix" : "You need at least one mix");
            row->remove.onClick = [this, row]
            {
                auto* out = row->output;
                SafePointer<Component> safe (this);
                MessageManager::callAsync ([this, safe, out]
                {
                    if (safe == nullptr) return;
                    engine.removeOutput (out);
                    rebuild();
                    if (onChanged) onChanged();
                });
            };
            row->addAndMakeVisible (row->remove);
            addAndMakeVisible (row);
        }

        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        micPanel.setBounds (area.removeFromTop (70));
        area.removeFromTop (10);

        auto micRow = area.removeFromTop (28);
        micFxLabel.setBounds (micRow.removeFromLeft (140));
        micFxChoice.setBounds (micRow);
        micFxStatus.setBounds (area.removeFromTop (22).withTrimmedLeft (140));
        area.removeFromTop (14);

        mixesLabel.setBounds (area.removeFromTop (22));
        intro.setBounds (area.removeFromTop (22));
        area.removeFromTop (8);

        for (auto* row : rows)
        {
            auto r = area.removeFromTop (52);
            row->setBounds (r);
            auto inner = row->getLocalBounds();
            auto top = inner.removeFromTop (28);
            row->name.setBounds (top.removeFromLeft (140));
            row->remove.setBounds (top.removeFromRight (80));
            top.removeFromRight (8);
            row->choice.setBounds (top);
            row->status.setBounds (inner.withTrimmedLeft (140));
            area.removeFromTop (6);
        }

        auto buttons = area.removeFromTop (28);
        addMixButton.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (8);
        rescanButton.setBounds (buttons.removeFromLeft (140));
    }

    struct Row final : public Component
    {
        Output* output = nullptr;
        Label name, status;
        ComboBox choice;
        TextButton remove;
    };

    MixerEngine& engine;
    VirtualMicPanel micPanel;
    Label micFxLabel, micFxStatus, mixesLabel;
    ComboBox micFxChoice;
    Label intro;
    TextButton addMixButton { "+ Add mix" }, rescanButton { "Rescan devices" };
    OwnedArray<Row> rows;
    std::vector<Choice> choices;
    std::function<void()> onChanged;
};

//==============================================================================
struct SettingsComponent::AboutPanel final : public Component
{
    explicit AboutPanel (Updater* u) : updater (u)
    {
        title.setText ("Faderhouse " + Updater::getCurrentVersion(), dontSendNotification);
        title.setFont (FontOptions (18.0f, Font::bold));
        addAndMakeVisible (title);

        info.setText ("Updates come from github.com/" + String (Updater::repository) + " (Releases).\n"
                      "When a new version is out, an \"Update\" button appears at the top of the mixer.",
                      dontSendNotification);
        info.setColour (Label::textColourId, Theme::textDim);
        info.setJustificationType (Justification::topLeft);
        addAndMakeVisible (info);

        checkNow.setButtonText ("Check for updates now");
        checkNow.onClick = [this] { if (updater != nullptr) updater->check (true); };
        addAndMakeVisible (checkNow);

        autoCheck.setButtonText ("Check for updates automatically");
        autoCheck.setToggleState (updater == nullptr || updater->isAutoCheckEnabled(), dontSendNotification);
        autoCheck.onClick = [this] { if (updater != nullptr) updater->setAutoCheckEnabled (autoCheck.getToggleState()); };
        addAndMakeVisible (autoCheck);

        releases.setButtonText ("Open the releases page");
        releases.setURL (URL ("https://github.com/" + String (Updater::repository) + "/releases"));
        addAndMakeVisible (releases);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (30));
        area.removeFromTop (6);
        info.setBounds (area.removeFromTop (44));
        area.removeFromTop (10);
        checkNow.setBounds (area.removeFromTop (28).removeFromLeft (220));
        area.removeFromTop (10);
        autoCheck.setBounds (area.removeFromTop (24).removeFromLeft (320));
        area.removeFromTop (10);
        releases.setBounds (area.removeFromTop (24).removeFromLeft (220));
    }

    Updater* updater;
    Label title, info;
    TextButton checkNow;
    ToggleButton autoCheck;
    HyperlinkButton releases;
};

//==============================================================================
SettingsComponent::SettingsComponent (MixerEngine& e, Updater* updater) : engine (e)
{
    auto* selector = new AudioDeviceSelectorComponent (engine.deviceManager,
                                                       1, 64,     // inputs
                                                       2, 64,     // outputs
                                                       false, false, false, false);
    tabs.addTab ("Audio interface", Theme::panel, selector, true);

    routing = std::make_unique<RoutingPanel> (engine);
    routing->onChanged = [this] { if (onRoutingChanged) onRoutingChanged(); };
    tabs.addTab ("Mix routing", Theme::panel, routing.get(), false);

    about = std::make_unique<AboutPanel> (updater);
    tabs.addTab ("About & updates", Theme::panel, about.get(), false);

    tabs.setTabBarDepth (32);
    addAndMakeVisible (tabs);

    engine.deviceManager.addChangeListener (this);
    setSize (680, 620);
}

SettingsComponent::~SettingsComponent()
{
    engine.deviceManager.removeChangeListener (this);
}

void SettingsComponent::changeListenerCallback (ChangeBroadcaster*)
{
    routing->rebuild(); // channel names change with the interface
}

void SettingsComponent::paint (Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SettingsComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}
