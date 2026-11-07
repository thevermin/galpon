#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace galpon
{

/** Floating windows that show a plugin's own interface. */
class PluginWindows
{
public:
    PluginWindows();
    ~PluginWindows();

    void open (juce::AudioProcessor&, const juce::String& title);
    void closeFor (juce::AudioProcessor&);
    void closeAll();

private:
    struct Window;
    std::vector<std::unique_ptr<Window>> windows;
};

} // namespace galpon
