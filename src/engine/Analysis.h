#pragma once

#include "Model.h"

namespace galpon
{

/** Music analysis that runs off the audio thread: chord recognition (like Logic's Chord ID). */
namespace analysis
{
    /** One chord per beat (12 pitch-class energies), plus which note is lowest. */
    struct BeatChroma
    {
        std::array<float, 12> chroma {};
        std::array<float, 12> bass {};
        float energy = 0.0f;
    };

    /** Chroma per beat from audio (mono, any sample rate). Beat 0 = sample 0. */
    std::vector<BeatChroma> chromaFromAudio (const float* mono, int numSamples, double sampleRate, double samplesPerBeat);

    /** Chroma per beat from MIDI notes (beats relative to `startBeat`). Drums should be left out. */
    std::vector<BeatChroma> chromaFromNotes (const std::vector<Note>& notes, double startBeat, double endBeat);

    /** Finds the chord sequence (smoothed so it changes on strong beats, like a musician would write it). */
    std::vector<ChordSpan> decodeChords (const std::vector<BeatChroma>&, double startBeat, int beatsPerBar);

    /** "Do", "Lam", "Sol7", "Fa#m7"  (spanish = false: "C", "Am", "G7", "F#m7"). */
    juce::String chordName (int root, int quality, bool spanish = true);

    /** Notes for a chord (for turning detected chords into a playable track). */
    std::vector<int> chordPitches (int root, int quality, int octaveBase = 48);
}

} // namespace galpon
