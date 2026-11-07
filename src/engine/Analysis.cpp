// Chord recognition: pitch-class energy ("chroma") per beat, compared with chord shapes,
// then smoothed with a Viterbi pass so chords change where a musician would write them.

#include "Analysis.h"

namespace galpon::analysis
{
namespace
{
    constexpr int numQualities = 4;   // maj, min, 7, m7
    const int intervals[numQualities][4] = { { 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 } };
    // triads first: a 7th has to be clearly there before we call it
    const float qualityBias[numQualities] = { 0.0f, 0.0f, -0.045f, -0.045f };

    void normalise (std::array<float, 12>& c)
    {
        float n = 0;
        for (auto v : c) n += v * v;
        n = std::sqrt (n);
        if (n > 1.0e-9f) for (auto& v : c) v /= n;
    }

    /** How well a chroma vector matches a chord shape (cosine similarity of weighted templates). */
    float matchScore (const BeatChroma& b, int root, int q)
    {
        std::array<float, 12> t {};
        t[(size_t) root] = 1.0f;
        for (int k = 1; k < 4; ++k)
            if (intervals[q][k] >= 0) t[(size_t) ((root + intervals[q][k]) % 12)] = (k == 3 ? 0.75f : (k == 2 ? 0.9f : 1.0f));
        normalise (t);
        float dot = 0;
        for (int i = 0; i < 12; ++i) dot += t[(size_t) i] * b.chroma[(size_t) i];
        // the lowest note usually is the root
        float bassTotal = 0;
        for (auto v : b.bass) bassTotal += v;
        const float bassRoot = bassTotal > 1.0e-9f ? b.bass[(size_t) root] / bassTotal : 0.0f;
        return dot + 0.18f * bassRoot + qualityBias[q];
    }
}

//==============================================================================
std::vector<BeatChroma> chromaFromAudio (const float* mono, int numSamples, double sampleRate, double samplesPerBeat)
{
    std::vector<BeatChroma> beats;
    if (numSamples <= 0 || samplesPerBeat < 16.0) return beats;
    const int numBeats = (int) std::ceil (numSamples / samplesPerBeat);
    beats.resize ((size_t) numBeats);

    // ~6 Hz bins at 48 kHz: enough to tell low notes apart
    const int order = sampleRate > 60000.0 ? 14 : 13;
    const int size = 1 << order;
    const int hop = size / 4;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buf ((size_t) size * 2);

    // pitch class of every FFT bin (precomputed), -1 outside the useful range
    std::vector<int> pcOf ((size_t) size / 2, -1);
    std::vector<float> weightOf ((size_t) size / 2, 0.0f);
    std::vector<bool> isBass ((size_t) size / 2, false);
    for (int k = 1; k < size / 2; ++k)
    {
        const double f = k * sampleRate / size;
        if (f < 50.0 || f > 2500.0) continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (midi);
        if (std::abs (midi - nearest) > 0.35) continue;    // between notes: noise, not pitch
        pcOf[(size_t) k] = ((int) nearest % 12 + 12) % 12;
        weightOf[(size_t) k] = 1.0f - (float) std::abs (midi - nearest) * 1.5f;
        isBass[(size_t) k] = f < 260.0;
    }

    for (int start = -size / 2; start < numSamples; start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        float energy = 0;
        for (int i = 0; i < size; ++i)
        {
            const int s = start + i;
            if (s >= 0 && s < numSamples) { buf[(size_t) i] = mono[s]; energy += mono[s] * mono[s]; }
        }
        if (energy < 1.0e-8f) continue;
        window.multiplyWithWindowingTable (buf.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        const double centre = start + size / 2;
        const int beat = (int) std::floor (centre / samplesPerBeat);
        if (beat < 0 || beat >= numBeats) continue;
        auto& b = beats[(size_t) beat];
        for (int k = 1; k < size / 2; ++k)
        {
            const int pc = pcOf[(size_t) k];
            if (pc < 0) continue;
            const float m = std::sqrt (buf[(size_t) k]) * weightOf[(size_t) k];   // compress: quiet notes still count
            b.chroma[(size_t) pc] += m;
            if (isBass[(size_t) k]) b.bass[(size_t) pc] += m;
        }
        b.energy += energy;
    }

    for (auto& b : beats) normalise (b.chroma);
    return beats;
}

std::vector<BeatChroma> chromaFromNotes (const std::vector<Note>& notes, double startBeat, double endBeat)
{
    std::vector<BeatChroma> beats;
    const int numBeats = (int) std::ceil (endBeat - startBeat - 1.0e-6);
    if (numBeats <= 0) return beats;
    beats.resize ((size_t) numBeats);
    std::vector<int> lowest ((size_t) numBeats, 128);

    for (auto& n : notes)
    {
        const double a = n.beat - startBeat, e = a + n.length;
        for (int b = juce::jmax (0, (int) std::floor (a)); b < numBeats && b < e; ++b)
        {
            const double overlap = juce::jmin (e, (double) b + 1) - juce::jmax (a, (double) b);
            if (overlap <= 0) continue;
            auto& bc = beats[(size_t) b];
            bc.chroma[(size_t) (n.pitch % 12)] += (float) overlap * (0.5f + n.velocity);
            bc.energy += (float) overlap;
            lowest[(size_t) b] = juce::jmin (lowest[(size_t) b], n.pitch);
        }
    }
    for (int b = 0; b < numBeats; ++b)
    {
        if (lowest[(size_t) b] < 128) beats[(size_t) b].bass[(size_t) (lowest[(size_t) b] % 12)] = 1.0f;
        normalise (beats[(size_t) b].chroma);
    }
    return beats;
}

//==============================================================================
std::vector<ChordSpan> decodeChords (const std::vector<BeatChroma>& beats, double startBeat, int beatsPerBar)
{
    std::vector<ChordSpan> out;
    const int n = (int) beats.size();
    if (n == 0) return out;
    beatsPerBar = juce::jmax (1, beatsPerBar);

    // States 0..47 = chords, 48 = no chord (silence)
    constexpr int numStates = 12 * numQualities + 1;
    float maxEnergy = 0;
    for (auto& b : beats) maxEnergy = juce::jmax (maxEnergy, b.energy);

    std::vector<std::array<float, numStates>> score ((size_t) n);
    std::vector<std::array<int, numStates>> from ((size_t) n);
    std::array<float, numStates> emit {};

    auto emission = [&] (int t)
    {
        const auto& b = beats[(size_t) t];
        const bool silent = b.energy <= maxEnergy * 1.0e-4f || b.energy <= 0.0f;
        for (int s = 0; s < numStates - 1; ++s)
            emit[(size_t) s] = silent ? -1.0f : matchScore (b, s % 12, s / 12);
        emit[numStates - 1] = silent ? 1.0f : 0.25f;
    };

    emission (0);
    score[0] = emit;
    for (int t = 1; t < n; ++t)
    {
        emission (t);
        // changing chord costs less on a downbeat, a bit less mid-bar, most on weak beats
        const double absBeat = startBeat + t;
        const double inBar = std::fmod (absBeat, (double) beatsPerBar);
        float changeCost = 0.30f;
        if (std::abs (inBar) < 1.0e-6) changeCost = 0.06f;
        else if (beatsPerBar % 2 == 0 && std::abs (inBar - beatsPerBar / 2) < 1.0e-6) changeCost = 0.16f;

        int bestPrev = 0;
        for (int s = 1; s < numStates; ++s) if (score[(size_t) t - 1][(size_t) s] > score[(size_t) t - 1][(size_t) bestPrev]) bestPrev = s;
        const float bestPrevScore = score[(size_t) t - 1][(size_t) bestPrev];
        for (int s = 0; s < numStates; ++s)
        {
            const float stay = score[(size_t) t - 1][(size_t) s];
            const float move = bestPrevScore - changeCost;
            if (stay >= move) { score[(size_t) t][(size_t) s] = stay + emit[(size_t) s]; from[(size_t) t][(size_t) s] = s; }
            else              { score[(size_t) t][(size_t) s] = move + emit[(size_t) s]; from[(size_t) t][(size_t) s] = bestPrev; }
        }
    }

    std::vector<int> path ((size_t) n);
    int s = 0;
    for (int k = 1; k < numStates; ++k) if (score[(size_t) n - 1][(size_t) k] > score[(size_t) n - 1][(size_t) s]) s = k;
    for (int t = n - 1; t >= 0; --t) { path[(size_t) t] = s; s = from[(size_t) t][(size_t) s]; }

    for (int t = 0; t < n;)
    {
        int e = t + 1;
        while (e < n && path[(size_t) e] == path[(size_t) t]) ++e;
        const int st = path[(size_t) t];
        if (st != numStates - 1)
            out.push_back ({ startBeat + t, (double) (e - t), st % 12, st / 12 });
        t = e;
    }
    return out;
}

//==============================================================================
juce::String chordName (int root, int quality, bool spanish)
{
    static const char* es[] = { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    static const char* en[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* suffix[] = { "", "m", "7", "m7" };
    root = (root % 12 + 12) % 12;
    return juce::String (spanish ? es[root] : en[root]) + suffix[juce::jlimit (0, 3, quality)];
}

std::vector<int> chordPitches (int root, int quality, int octaveBase)
{
    std::vector<int> p;
    root = (root % 12 + 12) % 12;
    // close voicing around middle C, root in the bass an octave lower
    p.push_back (octaveBase - 12 + root);
    for (int k = 0; k < 4; ++k)
    {
        const int iv = intervals[juce::jlimit (0, 3, quality)][k];
        if (iv < 0) continue;
        int pitch = octaveBase + root + iv;
        if (pitch > octaveBase + 16) pitch -= 12;
        p.push_back (pitch);
    }
    return p;
}

} // namespace galpon::analysis
