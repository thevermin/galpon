#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>

namespace galpon
{

/** Reads a .feedpak song (the open format of feedBack, formerly Slopsmith; spec: CC0):
    a zip file or a "*.feedpak" folder with manifest.yaml, arrangements (JSON / JSONC),
    audio stems and optional side files. Galpon only reads; it never writes into packs. */
class Feedpak
{
public:
    /** Opens the pack. Fails with a readable reason. */
    juce::Result open (const juce::File& fileOrFolder);

    const juce::var& manifest() const noexcept { return manifestVar; }
    juce::File source() const { return location; }

    /** Text of a file referenced by the manifest (safe path, inside the pack). */
    juce::Result readText (const juce::String& path, juce::String& out) const;
    /** Raw bytes. */
    juce::Result readBytes (const juce::String& path, juce::MemoryBlock& out) const;
    /** JSON or JSONC (comments stripped as the spec requires). */
    juce::var readJson (const juce::String& path, juce::String& error) const;
    /** Decodes an audio stem. */
    std::unique_ptr<juce::AudioBuffer<float>> readAudio (const juce::String& path, double& sampleRate, juce::String& error) const;

    /** For the library: title, artist, album, duration, arrangements (id, name, type, tuning, playable),
        stems, cover (as a data: URL, if small), path. */
    juce::var summary (bool withCover) const;

    /** Everything the game needs: manifest, arrangements (parsed), beats/sections (song timeline
        merged over the arrangement's, per the spec), lyrics. */
    juce::var loadForGame (juce::String& error) const;

    /** feedpak paths: POSIX, relative, no "..", no empty segments, no ':' or '\'. */
    static bool isSafePath (const juce::String&);
    static juce::String stripJsonComments (const juce::String&);

private:
    juce::File location;
    bool isZip = false;
    juce::String zipPrefix;                   // packs zipped with a top folder
    std::unique_ptr<juce::ZipFile> zip;
    std::map<juce::String, int> zipIndex;     // normalised name ("a/b.json") -> entry
    juce::String manifestName { "manifest.yaml" };
    juce::var manifestVar;
};

} // namespace galpon
