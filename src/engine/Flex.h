#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

namespace galpon::flex
{
    /** Pitch every few milliseconds (seconds from the start of the audio). midi < 0 = no pitch. */
    struct PitchFrame { double time = 0; float midi = -1.0f; };

    /** A sung or played note found in the pitch curve. */
    struct PitchNote { double start = 0, length = 0; float pitch = 0; };

    std::vector<PitchFrame> detectPitch (const float* mono, int numSamples, double sampleRate);
    std::vector<PitchNote>  segmentNotes (const std::vector<PitchFrame>&);

    /** Note starts / hits (seconds). */
    std::vector<double> detectOnsets (const float* mono, int numSamples, double sampleRate);

    /** Where each point of the source ends up: (source sample, output sample), increasing. */
    using TimeMap = std::vector<std::pair<double, double>>;

    double mapForward (const TimeMap&, double sourceSample);
    double mapBackward (const TimeMap&, double outputSample);

    /** Renders `source` through a time map and a pitch curve (semitones as a function of the
        source position, in samples). Formants are kept so voices don't sound like chipmunks. */
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, double sampleRate, const TimeMap&, int outputLength,
                                     const std::function<float (double sourceSample)>& semitonesAt);
}
