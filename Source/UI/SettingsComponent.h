#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Engine/MixerEngine.h"
#include "../Update/Updater.h"

/** Audio interface selection + where each mix goes. */
class SettingsComponent final : public juce::Component,
                                private juce::ChangeListener
{
public:
    SettingsComponent (MixerEngine& engine, Updater* updater);
    ~SettingsComponent() override;

    std::function<void()> onRoutingChanged;
    void showTab (int index) { tabs.setCurrentTabIndex (juce::jlimit (0, tabs.getNumTabs() - 1, index)); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct RoutingPanel;
    struct AboutPanel;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    MixerEngine& engine;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<RoutingPanel> routing;
    std::unique_ptr<AboutPanel> about;
};
