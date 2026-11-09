#pragma once

#include "Model.h"

namespace galpon
{

struct DrummerStyle
{
    juce::String id, name, who, description, genre;
};

const std::vector<DrummerStyle>& drummerStyles();

/** Parts of a song, as marked on the "Partes" strip. The drummer plays each one differently. */
enum class SectionKind { none = 0, intro, verse, prechorus, chorus, bridge, solo, outro };

SectionKind sectionKindFromId (const juce::String&);
juce::String sectionKindId (SectionKind);
/** Every kind, in menu order: { id, Spanish name }. */
const std::vector<std::pair<juce::String, juce::String>>& sectionKinds();

/** What the drummer knows about one bar of the region. */
struct DrumBar
{
    SectionKind kind = SectionKind::none;
    bool sectionStart = false;   // first bar of a part: crash, new groove
    bool sectionEnd   = false;   // last bar before another part: fill into it
    bool ending       = false;   // the very last bar of the song's ending: one final hit
};

/** Everything the drummer needs to write one region. */
struct DrumRequest
{
    DrummerSettings settings;
    double lengthBeats  = 16.0;
    double beatsPerBar  = 4.0;
    int    seed         = 1;
    bool   fillAtEnd    = true;
    bool   crashAtStart = true;

    /** One entry per bar of the region (empty: no parts marked). */
    std::vector<DrumBar> bars;

    /** What another track plays (the guitar, the bass), so the drummer can react:
        followSteps has one strength (0..1) per 16th note from the region start, and
        followBarEnergy how loud that track is in each bar (0..1, or < 0 when it says nothing).
        followOnsets is the simple form (beats where the other track plays): it's turned into steps. */
    std::vector<float>  followSteps;
    std::vector<float>  followBarEnergy;
    std::vector<double> followOnsets;
};

/** Writes a drum part as MIDI notes (General MIDI drum map). Same input -> same output. */
std::vector<Note> generateDrumPart (const DrumRequest&);

/** Listens to a recording (mono, at `sampleRate`) and returns how strongly something starts on
    each 16th note (0..1) and how loud each bar is (0..1). Used to make the drummer follow a guitar. */
void analyseFollowAudio (const float* mono, int numSamples, double sampleRate, double samplesPerBeat,
                         double beatsPerBar, std::vector<float>& steps, std::vector<float>& barEnergy);

} // namespace galpon
