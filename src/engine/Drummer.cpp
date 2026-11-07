// The AI drummer.
//
// Version 1 is a rule-based generator: every style is a small "brain" made of probability
// patterns for each drum, which the X/Y pad (simple <-> complex, soft <-> loud) bends.
// Randomness is seeded, so the same settings always give the same groove, and "Variar"
// just picks a new seed. It can listen to another track's notes (e.g. the bass) and place
// the kick where the bass plays, and it writes fills at the end of each region.
//
// The output is plain MIDI, so a trained model (e.g. a small groove model) can replace or
// refine this later without touching the rest of the app.

#include "Drummer.h"

namespace galpon
{

namespace gm
{
    constexpr int kick = 36, rim = 37, snare = 38, clap = 39, hatClosed = 42, hatPedal = 44, hatOpen = 46,
                  tomFloor = 43, tomLow = 45, tomMid = 47, tomHigh = 50, crash = 49, ride = 51,
                  tamb = 54, cowbell = 56, shaker = 70;
}

const std::vector<DrummerStyle>& drummerStyles()
{
    static const std::vector<DrummerStyle> styles {
        { "rock",       "Rock de garage", juce::String (u8"Tomás"),  "Directo y fuerte" },
        { "pop",        "Pop",            juce::String (u8"Lucía"),  "Prolijo y bailable" },
        { "funk",       "Funk",           "Nico",   "Mucho groove y notas fantasma" },
        { "cuarteto",   "Cuarteto",       "Ramiro", juce::String (u8"Tunga-tunga cordobés") },
        { "lofi",       "Lo-fi",          "Sol",    "Relajado, con swing" },
        { "reggaeton",  juce::String (u8"Reggaetón"),      "Fede",   "Dembow" },
        { "jazz",       "Jazz suave",     "Ana",    "Ride con swing y escobillas" },
        { "electronic", juce::String (u8"Electrónica"),    "Max",    "Caja de ritmos, bombo en negras" },
    };
    return styles;
}

namespace
{
    using Pattern = std::array<float, 16>;   // probability per 16th note of a 4/4 bar

    struct StyleBrain
    {
        Pattern kickSimple, kickComplex;
        Pattern snareSimple, snareComplex;     // values < 0.5 at complex end become ghost notes
        int  snareNote = gm::snare;
        bool clapLayer = false;
        bool rideWhenLoud = true;
        bool jazzRide = false;
        bool openHatOffbeats = false;
        float extraSwing = 0.0f;
        int  percNote = gm::tamb;
        Pattern perc;
    };

    StyleBrain brainFor (const juce::String& id)
    {
        StyleBrain b;
        //                          1     e     &     a     2     e     &     a     3     e     &     a     4     e     &     a
        if (id == "pop")
        {
            b.kickSimple   = { 1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0 };
            b.kickComplex  = { 1.0f, 0,    0,    0.3f, 0,    0,    0.7f, 0,    1.0f, 0,    0.6f, 0,    0,    0,    0.3f, 0 };
            b.snareSimple  = { 0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0 };
            b.snareComplex = { 0,    0,    0,    0,    1.0f, 0,    0,    0.2f, 0,    0,    0,    0,    1.0f, 0,    0,    0.3f };
            b.clapLayer = true;
            b.percNote = gm::tamb;
            b.perc = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
        }
        else if (id == "funk")
        {
            b.kickSimple   = { 1.0f, 0,    0,    0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0 };
            b.kickComplex  = { 1.0f, 0,    0,    0.8f, 0,    0,    0.7f, 0,    0,    0,    1.0f, 0,    0,    0.5f, 0,    0 };
            b.snareSimple  = { 0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0 };
            b.snareComplex = { 0,    0.2f, 0,    0.25f,1.0f, 0,    0.3f, 0.25f,0,    0.3f, 0,    0.25f,1.0f, 0,    0.3f, 0.2f };
            b.rideWhenLoud = false;
            b.openHatOffbeats = false;
            b.percNote = gm::cowbell;
            b.perc = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
        }
        else if (id == "cuarteto")
        {
            // Bombo en cada tiempo, y el "tunga" de la caja/rim en los contratiempos
            b.kickSimple   = { 1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0 };
            b.kickComplex  = { 1.0f, 0,    0,    0.3f, 1.0f, 0,    0,    0.3f, 1.0f, 0,    0,    0.3f, 1.0f, 0,    0,    0.4f };
            b.snareSimple  = { 0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0 };
            b.snareComplex = { 0,    0,    1.0f, 0.4f, 0,    0,    1.0f, 0.4f, 0,    0,    1.0f, 0.4f, 0,    0,    1.0f, 0.5f };
            b.snareNote = gm::rim;
            b.rideWhenLoud = false;
            b.percNote = gm::shaker;   // la güira
            b.perc = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        }
        else if (id == "lofi")
        {
            b.kickSimple   = { 1.0f, 0,    0,    0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0 };
            b.kickComplex  = { 1.0f, 0,    0,    0,    0,    0,    0,    0.6f, 0,    0,    1.0f, 0,    0,    0,    0.4f, 0 };
            b.snareSimple  = { 0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0 };
            b.snareComplex = { 0,    0,    0,    0,    1.0f, 0,    0,    0.2f, 0,    0,    0,    0,    1.0f, 0,    0.2f, 0 };
            b.rideWhenLoud = false;
            b.extraSwing = 0.45f;
            b.percNote = gm::shaker;
            b.perc = { 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        }
        else if (id == "reggaeton")
        {
            b.kickSimple   = { 1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0 };
            b.kickComplex  = b.kickSimple;
            b.snareSimple  = { 0,    0,    0,    1.0f, 0,    0,    1.0f, 0,    0,    0,    0,    1.0f, 0,    0,    1.0f, 0 };
            b.snareComplex = { 0,    0,    0,    1.0f, 0,    0,    1.0f, 0.3f, 0,    0,    0,    1.0f, 0,    0.3f, 1.0f, 0 };
            b.clapLayer = true;
            b.rideWhenLoud = false;
            b.percNote = gm::shaker;
            b.perc = { 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        }
        else if (id == "jazz")
        {
            b.kickSimple   = { 0.35f,0,    0,    0,    0.35f,0,    0,    0,    0.35f,0,    0,    0,    0.35f,0,    0,    0 };
            b.kickComplex  = { 0.4f, 0,    0,    0,    0.4f, 0,    0,    0.3f, 0.4f, 0,    0,    0,    0.4f, 0,    0.3f, 0 };
            b.snareSimple  = { 0,    0,    0,    0,    0,    0,    0.15f,0,    0,    0,    0,    0,    0,    0,    0.2f, 0 };
            b.snareComplex = { 0,    0,    0.3f, 0,    0,    0,    0.35f,0,    0,    0,    0.3f, 0,    0,    0,    0.4f, 0 };
            b.jazzRide = true;
            b.rideWhenLoud = false;
            b.extraSwing = 0.65f;
            b.percNote = gm::shaker;
            b.perc = {};
        }
        else if (id == "electronic")
        {
            b.kickSimple   = { 1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0,    1.0f, 0,    0,    0 };
            b.kickComplex  = { 1.0f, 0,    0,    0,    1.0f, 0,    0,    0.4f, 1.0f, 0,    0,    0,    1.0f, 0,    0.5f, 0 };
            b.snareSimple  = { 0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0 };
            b.snareComplex = { 0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0.5f };
            b.snareNote = gm::clap;
            b.openHatOffbeats = true;
            b.rideWhenLoud = false;
            b.percNote = gm::shaker;
            b.perc = { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 };
        }
        else // rock
        {
            b.kickSimple   = { 1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0 };
            b.kickComplex  = { 1.0f, 0,    0,    0,    0,    0,    0,    0.6f, 1.0f, 0,    0.7f, 0,    0,    0,    0.4f, 0 };
            b.snareSimple  = { 0,    0,    0,    0,    1.0f, 0,    0,    0,    0,    0,    0,    0,    1.0f, 0,    0,    0 };
            b.snareComplex = { 0,    0,    0,    0,    1.0f, 0,    0,    0.2f, 0,    0.2f, 0,    0,    1.0f, 0,    0,    0.25f };
            b.percNote = gm::tamb;
            b.perc = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
        }
        return b;
    }

    struct Writer
    {
        std::vector<Note>& out;
        juce::Random& rng;
        const DrummerSettings& s;
        double length;
        float swing;

        void hit (double beat, int pitch, float velocity, double len = 0.1)
        {
            if (beat < 0.0 || beat >= length) return;
            // Swing: push the "e" and "a" 16ths (and for strong swing the "&" 8ths) later.
            const double pos = beat - std::floor (beat);
            const int step = juce::roundToInt (pos * 4.0) % 4;
            if (step == 1 || step == 3) beat += swing * (1.0 / 12.0);
            // Humanise timing and dynamics a little
            const double jitter = (rng.nextDouble() - 0.5) * 0.02 * s.humanize;
            if (beat > 0.001) beat += jitter;
            velocity *= 1.0f + (rng.nextFloat() - 0.5f) * 0.18f * s.humanize;
            out.push_back ({ juce::jlimit (0.0, length - 0.01, beat), len, pitch, juce::jlimit (0.05f, 1.0f, velocity) });
        }
    };

    float lerp (float a, float b, float t) { return a + (b - a) * t; }
} // namespace

std::vector<Note> generateDrumPart (const DrumRequest& req)
{
    std::vector<Note> notes;
    const auto& s = req.settings;
    const auto brain = brainFor (s.style);
    const double bpb = juce::jmax (1.0, req.beatsPerBar);
    const int stepsPerBar = juce::jmax (4, (int) std::round (bpb * 4.0));
    const int numBars = juce::jmax (1, (int) std::ceil (req.lengthBeats / bpb - 1.0e-6));
    const float c = s.complexity, loud = s.loudness;
    const float baseVel = 0.42f + 0.5f * loud;
    const float swing = juce::jlimit (0.0f, 1.0f, s.swing + brain.extraSwing * (1.0f - s.swing));

    // Fills: one at the end of the region, length grows with the "fills" amount
    const double fillBeats = ! req.fillAtEnd || s.fills < 0.05f ? 0.0
                           : (s.fills > 0.7f ? 2.0 : 1.0);
    const double fillStart = req.lengthBeats - fillBeats;

    const float ksDensity  = s.kickSnareLevel / 5.0f;
    const int   cymLevel   = s.cymbalLevel;

    for (int bar = 0; bar < numBars; ++bar)
    {
        juce::Random rng ((juce::int64) req.seed * 7919 + bar * 104729 + s.style.hashCode());
        Writer w { notes, rng, s, req.lengthBeats, swing };
        const double barStart = bar * bpb;
        const bool sectionStart = bar == 0;
        // Every 4th bar is a little busier; every 8th bar the drummer varies more.
        const float barSpice = (bar % 4 == 3 ? 0.15f : 0.0f) + (bar % 8 == 7 ? 0.1f : 0.0f);

        for (int step = 0; step < stepsPerBar; ++step)
        {
            const double beat = barStart + step * 0.25;
            if (beat >= req.lengthBeats) break;
            if (fillBeats > 0.0 && beat >= fillStart) break;   // fill takes over

            const int p = step % 16;
            const bool downbeat = step == 0;
            const bool onBeat = (step % 4) == 0;
            const float accent = onBeat ? 1.0f : 0.82f;

            // ---- kick & snare ----
            if (s.kickSnare)
            {
                float kp = lerp (brain.kickSimple[(size_t) p], brain.kickComplex[(size_t) p], juce::jmin (1.0f, c + barSpice));
                if (! req.followOnsets.empty())
                {
                    // Follow mode: play the kick where the other track plays
                    bool near = false;
                    for (auto o : req.followOnsets)
                        if (std::abs (o - beat) < 0.126) { near = true; break; }
                    kp = near ? 0.92f : (downbeat ? 1.0f : kp * 0.15f);
                }
                if (kp < 1.0f) kp *= ksDensity;
                if (kp > 0.0f && rng.nextFloat() < kp)
                    w.hit (beat, gm::kick, baseVel * (downbeat ? 1.05f : accent));

                const float sp = lerp (brain.snareSimple[(size_t) p], brain.snareComplex[(size_t) p], c);
                const bool backbeat = brain.snareSimple[(size_t) p] >= 0.99f;
                const float prob = backbeat ? 1.0f : sp * ksDensity * (0.6f + c);
                if (prob > 0.0f && rng.nextFloat() < prob)
                {
                    const bool ghost = ! backbeat && brain.snareSimple[(size_t) p] < 0.5f;
                    const float v = ghost ? baseVel * 0.38f : baseVel * 1.02f;
                    w.hit (beat, brain.snareNote, v);
                    if (brain.clapLayer && backbeat && c > 0.45f)
                        w.hit (beat, gm::clap, v * 0.8f);
                }
            }

            // ---- hi-hat / ride ----
            if (s.cymbals)
            {
                if (brain.jazzRide)
                {
                    // ding, ding-da ding, ding-da (the "da" swings via the 16th swing)
                    static constexpr int ridePattern[16] = { 1,0,0,0, 1,0,0,1, 1,0,0,0, 1,0,0,1 };
                    if (ridePattern[p] || (c > 0.6f && p % 2 == 0 && rng.nextFloat() < c - 0.5f))
                        w.hit (beat, gm::ride, baseVel * (onBeat ? 0.85f : 0.65f));
                    if (p == 4 || p == 12)
                        w.hit (beat, gm::hatPedal, baseVel * 0.6f);
                }
                else
                {
                    // Level 1-2 = quarters, 3-4 = eighths, 5-6 = sixteenths, 7-8 = sixteenths with openings
                    const int every = cymLevel <= 2 ? 4 : cymLevel <= 4 ? 2 : 1;
                    const bool useRide = brain.rideWhenLoud && loud > 0.78f;
                    if (step % every == 0)
                    {
                        const bool open = (brain.openHatOffbeats && p % 4 == 2)
                                       || (cymLevel >= 7 && p == 14 && rng.nextFloat() < 0.7f)
                                       || (c > 0.7f && p == 6 && rng.nextFloat() < 0.25f);
                        const int note = useRide ? gm::ride : open ? gm::hatOpen : gm::hatClosed;
                        const float v = baseVel * (onBeat ? 0.85f : (p % 2 == 0 ? 0.7f : 0.55f));
                        w.hit (beat, note, v);
                    }
                }

                if (sectionStart && downbeat && req.crashAtStart && loud > 0.25f)
                    w.hit (beat, gm::crash, baseVel);
            }

            // ---- percussion ----
            if (s.percussion && brain.perc[(size_t) p] > 0.0f)
            {
                const float pp = juce::jmin (1.0f, s.percussionLevel / 5.0f);
                if (rng.nextFloat() < pp)
                    w.hit (beat, brain.percNote, baseVel * (onBeat ? 0.75f : 0.55f));
            }
        }
    }

    // ---- fill ----
    if (fillBeats > 0.0)
    {
        juce::Random rng ((juce::int64) req.seed * 31337 + 17);
        Writer w { notes, rng, s, req.lengthBeats, swing * 0.5f };
        const int steps = (int) (fillBeats * 4.0);
        const int toms[] = { gm::tomHigh, gm::tomMid, gm::tomLow, gm::tomFloor };
        const float density = 0.45f + 0.55f * c;

        for (int i = 0; i < steps; ++i)
        {
            const double beat = fillStart + i * 0.25;
            const float progress = (float) i / (float) steps;
            const bool must = (i % 2 == 0) || rng.nextFloat() < density;
            if (! must) continue;

            int note = gm::snare;
            if (s.toms && progress > 0.25f)
                note = toms[juce::jlimit (0, 3, (int) (progress * 4.0f) - 1 + (rng.nextFloat() < 0.3f ? 1 : 0))];
            if (! s.kickSnare && note == gm::snare)
                note = s.toms ? gm::tomHigh : -1;
            if (note < 0) continue;

            w.hit (beat, note, baseVel * (0.75f + 0.35f * progress));
            if (s.kickSnare && i % 4 == 0)
                w.hit (beat, gm::kick, baseVel * 0.9f);
        }
    }

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    return notes;
}

} // namespace galpon
