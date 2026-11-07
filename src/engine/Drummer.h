#pragma once

#include "Model.h"

namespace galpon
{

struct DrummerStyle
{
    juce::String id, name, who, description;
};

const std::vector<DrummerStyle>& drummerStyles();

/** Everything the drummer needs to write one region. */
struct DrumRequest
{
    DrummerSettings settings;
    double lengthBeats  = 16.0;
    double beatsPerBar  = 4.0;
    int    seed         = 1;
    bool   fillAtEnd    = true;
    bool   crashAtStart = true;
    std::vector<double> followOnsets;   // beats relative to the region start (e.g. bass notes)
};

/** Writes a drum part as MIDI notes (General MIDI drum map). Same input -> same output. */
std::vector<Note> generateDrumPart (const DrumRequest&);

} // namespace galpon
