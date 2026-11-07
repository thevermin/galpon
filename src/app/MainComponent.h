#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "engine/Engine.h"
#include "engine/EngineApi.h"
#include "PluginWindows.h"

namespace galpon
{

/** The window's content: a web view running the interface in /ui, talking to the engine. */
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::Timer,
                      private juce::AudioIODeviceType::Listener
{
public:
    explicit MainComponent (Engine&);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // Menu bar (top of the screen on Mac)
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int itemId, int) override;

    bool confirmQuit();

private:
    void timerCallback() override;
    void audioDeviceListChanged() override;
    juce::StringArray currentDeviceNames();
    juce::StringArray knownDevices;
    void handle (const juce::String& cmd, const juce::var& args, juce::WebBrowserComponent::NativeFunctionCompletion done);
    void choose (const juce::var& args, juce::WebBrowserComponent::NativeFunctionCompletion done);
    void emit (const juce::String& event, const juce::var& payload);
    std::optional<juce::WebBrowserComponent::Resource> serve (const juce::String& url);

    Engine& engine;
    EngineApi api { engine };
    PluginWindows pluginWindows;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    juce::File devUiFolder;
};

} // namespace galpon
