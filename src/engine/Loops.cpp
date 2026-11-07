#include "Loops.h"
#include "Drummer.h"

namespace galpon
{

const std::vector<LoopInfo>& loopLibrary()
{
    static const std::vector<LoopInfo> loops {
        { "drums-rock",     "Rock directo",        juce::String (u8"Batería"),  juce::String (u8"Batería acústica"),   "#E6A84A", 8.0, true },
        { "drums-funk",     "Groove funky",        juce::String (u8"Batería"),  juce::String (u8"Batería acústica"),   "#E6A84A", 8.0, true },
        { "drums-cuarteto", "Tunga-tunga",         juce::String (u8"Batería"),  juce::String (u8"Batería acústica"),   "#E6A84A", 8.0, true },
        { "drums-lofi",     "Lo-fi perezoso",      juce::String (u8"Batería"),  juce::String (u8"Batería electrónica"),"#E6A84A", 8.0, true },
        { "drums-electro",  "Cuatro en el piso",   juce::String (u8"Batería"),  juce::String (u8"Batería electrónica"),"#E6A84A", 8.0, true },
        { "bass-walk",      "Bajo caminante",      "Bajo",     juce::String (u8"Bajo eléctrico"),     "#60A5FA", 8.0 },
        { "bass-octaves",   juce::String (u8"Bajo saltarín"),       "Bajo",     "Bajo sinte",         "#60A5FA", 8.0 },
        { "keys-pop",       "Acordes pop",         "Teclados", "Piano de cola",      "#4ADE80", 16.0 },
        { "keys-ep",        "Acordes soul",        "Teclados", juce::String (u8"Piano eléctrico"),    "#4ADE80", 16.0 },
        { "pad-night",      "Pad nocturno",        "Teclados", "Pad nocturno",       "#4ADE80", 16.0 },
        { "synth-arp",      "Arpegio brillante",   "Sintes",   "Lead brillante",     "#C084FC", 8.0 },
        { "organ-stabs",    juce::String (u8"Órgano rítmico"),      "Teclados", juce::String (u8"Órgano"),             "#4ADE80", 8.0 },
    };
    return loops;
}

const LoopInfo* findLoop (const juce::String& id)
{
    for (auto& l : loopLibrary())
        if (l.id == id) return &l;
    return nullptr;
}

int keyRootFromName (const juce::String& key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        if (key.equalsIgnoreCase (names[i]) || key.equalsIgnoreCase (flats[i]))
            return i;
    return 0;
}

namespace
{
    // Scale degree (0-based) -> semitones
    int degree (int d, bool minor)
    {
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorS[] = { 0, 2, 3, 5, 7, 8, 10 };
        const int oct = (int) std::floor (d / 7.0);
        const int idx = ((d % 7) + 7) % 7;
        return oct * 12 + (minor ? minorS[idx] : major[idx]);
    }

    /** Triad (or 7th) on scale degree `root`. */
    std::vector<int> chord (int root, bool minor, bool seventh = false)
    {
        std::vector<int> c { degree (root, minor), degree (root + 2, minor), degree (root + 4, minor) };
        if (seventh) c.push_back (degree (root + 6, minor));
        return c;
    }

    void add (std::vector<Note>& n, double beat, double len, int pitch, float vel)
    {
        n.push_back ({ beat, len, juce::jlimit (0, 127, pitch), vel });
    }
}

std::vector<Note> makeLoopNotes (const LoopInfo& loop, int keyRoot, bool minor, double beatsPerBar)
{
    std::vector<Note> n;

    if (loop.drums)
    {
        DrumRequest r;
        r.lengthBeats = loop.lengthBeats;
        r.beatsPerBar = beatsPerBar;
        r.seed = 3;
        r.fillAtEnd = false;
        r.crashAtStart = false;
        r.settings.style = loop.id == "drums-funk" ? "funk" : loop.id == "drums-cuarteto" ? "cuarteto"
                         : loop.id == "drums-lofi" ? "lofi" : loop.id == "drums-electro" ? "electronic" : "rock";
        r.settings.complexity = 0.5f;
        r.settings.humanize = 0.3f;
        return generateDrumPart (r);
    }

    const int bass = 36 + keyRoot;    // C2
    const int mid  = 60 + keyRoot;    // C4
    const int progression[] = { 0, 5, 3, 4 };   // I - vi - IV - V (i - VI - iv - v in minor)

    if (loop.id == "bass-walk")
    {
        for (int bar = 0; bar < 2; ++bar)
        {
            const int r = progression[bar * 2];
            for (int b = 0; b < 4; ++b)
                add (n, bar * 4 + b, 0.9, bass + degree (r + (b == 0 ? 0 : b == 1 ? 2 : b == 2 ? 4 : 5), minor), b == 0 ? 0.9f : 0.75f);
        }
    }
    else if (loop.id == "bass-octaves")
    {
        for (int bar = 0; bar < 2; ++bar)
        {
            const int r = progression[bar * 2];
            for (int e = 0; e < 8; ++e)
                add (n, bar * 4 + e * 0.5, 0.35, bass + degree (r, minor) + (e % 2 ? 12 : 0), e % 2 ? 0.65f : 0.9f);
        }
    }
    else if (loop.id == "keys-pop")
    {
        for (int bar = 0; bar < 4; ++bar)
        {
            const auto c = chord (progression[bar], minor);
            for (int hit : { 0, 2 })
                for (auto p : c)
                    add (n, bar * 4 + hit, 1.8, mid + p - 12 * (p > 7 ? 1 : 0), 0.7f);
            add (n, bar * 4, 3.8, bass + 12 + degree (progression[bar], minor), 0.6f);
        }
    }
    else if (loop.id == "keys-ep")
    {
        for (int bar = 0; bar < 4; ++bar)
        {
            const auto c = chord (progression[bar], minor, true);
            for (double hit : { 0.0, 1.5, 2.5 })
                for (auto p : c)
                    add (n, bar * 4 + hit, hit == 0.0 ? 1.3 : 0.8, mid - 12 + p, hit == 0.0 ? 0.75f : 0.55f);
        }
    }
    else if (loop.id == "pad-night")
    {
        for (int bar = 0; bar < 4; ++bar)
            for (auto p : chord (progression[bar], minor, true))
                add (n, bar * 4, 4.0, mid - 12 + p, 0.6f);
    }
    else if (loop.id == "synth-arp")
    {
        for (int bar = 0; bar < 2; ++bar)
        {
            const auto c = chord (progression[bar * 2], minor);
            const int order[] = { 0, 1, 2, 1 };
            for (int s = 0; s < 16; ++s)
                add (n, bar * 4 + s * 0.25, 0.2, mid + c[(size_t) order[s % 4]] + (s >= 8 ? 12 : 0), s % 4 == 0 ? 0.85f : 0.6f);
        }
    }
    else if (loop.id == "organ-stabs")
    {
        for (int bar = 0; bar < 2; ++bar)
        {
            const auto c = chord (progression[bar * 2 + 1], minor);
            for (double hit : { 0.5, 1.5, 2.5, 3.5 })
                for (auto p : c)
                    add (n, bar * 4 + hit, 0.3, mid + p, 0.7f);
        }
    }
    return n;
}

} // namespace galpon
