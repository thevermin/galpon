#pragma once

#include "Model.h"

namespace galpon
{

/** Galpon's own MIDI loops (written by us, so they're free to use in any song).
    They follow the song's key and tempo automatically. */
struct LoopInfo
{
    juce::String id, name, category, preset, colour;
    double lengthBeats = 8.0;
    bool drums = false;
};

const std::vector<LoopInfo>& loopLibrary();
const LoopInfo* findLoop (const juce::String& id);

/** Notes for a loop, transposed to `keyRoot` (0 = C ... 11 = B) and minor/major. */
std::vector<Note> makeLoopNotes (const LoopInfo&, int keyRoot, bool minor, double beatsPerBar);

int keyRootFromName (const juce::String& key);   // "C", "C#", "Db", ...

} // namespace galpon
