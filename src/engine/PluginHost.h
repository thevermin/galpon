#pragma once

#include "Model.h"

namespace galpon
{

/** Finds and loads AU / VST3 plugins installed on the computer. */
class PluginHost : private juce::Timer
{
public:
    explicit PluginHost (const juce::File& dataFolder);
    ~PluginHost() override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    void startScan (bool rescanEverything = false);
    void stopScan();
    bool isScanning() const noexcept { return ! scanners.empty(); }
    juce::var getStatus() const;
    juce::var listToVar() const;

    std::optional<juce::PluginDescription> find (const juce::String& identifier) const;

    using Callback = std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error)>;
    void createInstance (const juce::PluginDescription&, double sampleRate, int blockSize, Callback);

    std::function<void()> onChange;

private:
    void timerCallback() override;
    void save();

    juce::File listFile, deadMansPedal;
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners;
    juce::String currentlyScanning;
    float progress = 0.0f;
    int formatsTotal = 0;
};

} // namespace galpon
