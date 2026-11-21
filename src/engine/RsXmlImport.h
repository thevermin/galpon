#pragma once

#include <juce_graphics/juce_graphics.h>

namespace galpon
{

/** Turns songs that are already unpacked (Rocksmith-style arrangement XML files, as the free
    community toolkit writes them for custom songs, plus their decoded audio) into .feedpak packs.
    It only reads plain XML, audio and images: it doesn't open or decrypt any game archive. */
namespace RsXmlImport
{
    struct Result
    {
        juce::File source;       // the song's folder
        juce::File created;      // the .feedpak written (empty if it failed)
        juce::String title, artist, error;
        juce::StringArray warnings, arrangements;
    };

    /** Every song folder under `root` (itself, or its subfolders): a folder with arrangement XML
        files (directly, or in songs/arr as unpacked). */
    juce::Array<juce::File> findSongs (const juce::File& root);

    /** Converts one song folder into `destFolder` ("Artist - Title.feedpak"). */
    Result convert (const juce::File& songFolder, const juce::File& destFolder);

    /** The cover: decodes a DDS image (DXT1, DXT3, DXT5 or plain 32-bit). */
    juce::Image decodeDds (const juce::MemoryBlock& data);
}

} // namespace galpon
