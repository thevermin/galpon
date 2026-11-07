#include "PluginHost.h"

namespace galpon
{

PluginHost::PluginHost (const juce::File& dataFolder)
{
    juce::addDefaultFormatsToManager (formats);
    dataFolder.createDirectory();
    listFile      = dataFolder.getChildFile ("plugins.xml");
    deadMansPedal = dataFolder.getChildFile ("plugin-scan-in-progress.txt");

    if (auto xml = juce::XmlDocument::parse (listFile))
        known.recreateFromXml (*xml);
}

PluginHost::~PluginHost()
{
    stopScan();
}

void PluginHost::startScan (bool rescanEverything)
{
    stopScan();
    if (rescanEverything)
        known.clear();

    for (auto* format : formats.getFormats())
    {
        auto path = format->getDefaultLocationsToSearch();
        scanners.push_back (std::make_unique<juce::PluginDirectoryScanner> (known, *format, path, true, deadMansPedal, false));
    }
    formatsTotal = (int) scanners.size();
    progress = 0.0f;
    startTimer (20);
    if (onChange) onChange();
}

void PluginHost::stopScan()
{
    stopTimer();
    if (! scanners.empty())
    {
        scanners.clear();
        save();
    }
    currentlyScanning.clear();
}

void PluginHost::timerCallback()
{
    // One plugin per tick keeps the window responsive while scanning.
    if (scanners.empty())
    {
        stopTimer();
        return;
    }

    auto& scanner = *scanners.front();
    juce::String name;
    const bool more = scanner.scanNextFile (true, name);
    currentlyScanning = name;
    const int done = formatsTotal - (int) scanners.size();
    progress = (done + scanner.getProgress()) / (float) juce::jmax (1, formatsTotal);

    if (! more)
    {
        scanners.erase (scanners.begin());
        if (scanners.empty())
        {
            stopTimer();
            currentlyScanning.clear();
            progress = 1.0f;
            save();
        }
    }
    if (onChange) onChange();
}

void PluginHost::save()
{
    if (auto xml = known.createXml())
        xml->writeTo (listFile);
}

juce::var PluginHost::getStatus() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("scanning", isScanning());
    o->setProperty ("progress", progress);
    o->setProperty ("current", currentlyScanning);
    o->setProperty ("count", known.getNumTypes());
    juce::StringArray formatNames;
    for (auto* f : formats.getFormats()) formatNames.add (f->getName());
    o->setProperty ("formats", formatNames.joinIntoString (", "));
    return juce::var (o);
}

juce::var PluginHost::listToVar() const
{
    // Apple ships system building blocks as AUs (mixers, players, network, speech...): not useful as effects
    static const juce::StringArray hidden { "AUMixer", "AUMixer3D", "AUMultiChannelMixer", "AUMatrixMixer", "AUSpatialMixer",
        "AUVectorPanner", "AUSphericalHeadPanner", "HRTFPanner", "AUSoundFieldPanner", "AUMultiSplitter", "AUSpeechSynthesis",
        "AUScheduledSoundPlayer", "AUNetReceive", "AUNetSend", "AUAudioFilePlayer", "AURoundTripAAC", "AUSampleDelay",
        "AUConverter", "AUMerger", "AUSplitter", "AUDeferredRenderer", "AURogerBeep" };

    juce::Array<juce::var> list;
    for (auto& d : known.getTypes())
    {
        if (d.manufacturerName == "Apple" && hidden.contains (d.name)) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", d.createIdentifierString());
        o->setProperty ("name", d.name);
        o->setProperty ("maker", d.manufacturerName);
        o->setProperty ("format", d.pluginFormatName);
        o->setProperty ("instrument", d.isInstrument);
        o->setProperty ("category", d.category);
        list.add (juce::var (o));
    }
    return list;
}

std::optional<juce::PluginDescription> PluginHost::find (const juce::String& identifier) const
{
    for (auto& d : known.getTypes())
        if (d.createIdentifierString() == identifier || d.fileOrIdentifier == identifier)
            return d;
    return std::nullopt;
}

void PluginHost::createInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize, Callback cb)
{
    formats.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [cb] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            cb (std::move (instance), error);
        });
}

} // namespace galpon
