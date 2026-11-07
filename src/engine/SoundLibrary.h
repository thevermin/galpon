#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <map>
#include <memory>

namespace galpon
{

/** Free, real sampled instruments (SFZ) that Galpon can download and install with one click.
    They're downloaded from their authors' public repositories into Música/Galpon/Sonidos. */
class SoundLibrary
{
public:
    struct Sound
    {
        juce::String id, name, group, description, author, licence, size;
        juce::String zipUrl, folderInZip, sfz;    // what to download, and the instrument file inside it
        bool drums = false;
    };

    SoundLibrary();
    ~SoundLibrary();

    static const std::vector<Sound>& catalog();
    static const Sound* find (const juce::String& id);
    static juce::File soundsFolder();

    /** Where the instrument's .sfz is once installed (empty file if it isn't). */
    juce::File installedFile (const juce::String& id) const;

    /** Starts downloading (and then unpacking) in the background. */
    juce::Result download (const juce::String& id);
    void cancel (const juce::String& id);

    /** For the interface: every sound with installed / progress / status. */
    juce::var describe() const;
    /** Downloads in progress (for the tick): [{id, progress, status, done, error}] */
    juce::var progress();

    /** Unpacks a downloaded zip into the sound's folder (also used by tests). */
    static juce::Result install (const Sound&, const juce::File& zip, std::function<void (float)> progress = {});

private:
    struct Job;
    std::map<juce::String, std::unique_ptr<Job>> jobs;
};

} // namespace galpon
