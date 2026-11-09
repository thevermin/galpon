// The AI drummer.
//
// A rule-based drummer: every style is a small "brain" of probability patterns for each drum,
// which the X/Y pad (simple <-> complex, soft <-> loud) bends. Like a person, it picks a groove
// for each part of the song and keeps it, varies it at the end of each phrase, plays fills into
// the next part and lands them on a crash. Parts marked on the "Partes" strip (intro, chorus,
// bridge…) change how it plays, and it can listen to another track — the bass notes or a
// recorded guitar — to hit with its accents and follow how hard it's played.
// Randomness is seeded: the same settings give the same part, and "Variar" picks a new seed.
//
// The output is plain MIDI, so a trained model can replace or refine this later.

#include "Drummer.h"
#include <map>

namespace galpon
{

namespace gm
{
    constexpr int kick = 36, rim = 37, snare = 38, clap = 39, hatClosed = 42, hatPedal = 44, hatOpen = 46,
                  tomFloor = 43, tomLow = 45, tomMid = 47, tomHigh = 50, crash = 49, ride = 51,
                  tamb = 54, cowbell = 56, shaker = 70;
}

namespace
{
    using Pattern = std::array<float, 16>;   // probability per 16th note of a 4/4 bar

    // One drummer style, written the way a drummer would chart it: one character per 16th note of a
    // 4/4 bar. Kick and snare: X = always, x = often, o = sometimes (on the snare: a ghost note).
    // Hats: x closed, X closed accented, o open, r ride, R ride accented, b ride bell, c crash,
    // p hi-hat foot, g soft closed. "S" is the simple version, "C" the complex one (the X/Y pad).
    struct StyleDef
    {
        const char* id; const char* name; const char* genre; const char* who; const char* desc;
        const char* kickS; const char* kickC; const char* snareS; const char* snareC;
        const char* hatS; const char* hatC; const char* perc;
        int   snareNote, percNote;
        float swing, laidBack, machine, vel;   // vel: how hard it hits (1 = medium; rock and metal hit hard)
        unsigned flags;
    };
    enum : unsigned { RideLoud = 1, JazzRide = 2, Clap = 4, NoHalfTime = 8, DoubleKick = 16, Blast = 32, TrapRolls = 64, UpbeatHats = 128 };

    //                                    1   e   &   a   2   e   &   a   3   e   &   a   4   e   &   a
    // (plain UTF-8 text: read with juce::String::fromUTF8)
    const StyleDef styleDefs[] = {
        // ---- Rock
        { "rock", "Rock de garage", "Rock", "Tomás", "Directo y fuerte",
          "X.......X.......", "X......xX.x...o.", "....X.......X...", "....X..o.o..X..o",
          "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.o.", "....X.......X...", gm::snare, gm::tamb, 0, 0.3f, 0, 1.18f, RideLoud },
        { "classicrock", "Rock clásico", "Rock", "Beto", "Groove de los 70, con aire",
          "X.......X.x.....", "X.x.....X.x...x.", "....X.......X...", "....X..o....X..o",
          "X.x.X.x.X.x.X.x.", "XxxxXxxxXxxxXxxx", "................", gm::snare, gm::tamb, 0, 0.45f, 0, 1.12f, RideLoud },
        { "hardrock", "Hard rock", "Rock", "Vicky", "Pesado, con ride y platillos",
          "X.....x.X.x.....", "X.x...x.X.x...x.", "....X.......X...", "....X.......X..o",
          "X.x.X.x.X.x.X.x.", "R.r.R.r.R.r.R.r.", "................", gm::snare, gm::tamb, 0, 0.35f, 0, 1.24f, RideLoud },
        { "punk", "Punk", "Rock", "Juancho", "Rápido y sin frenos",
          "X...X...X...X...", "X.X.X...X.X.X.x.", "..X...X...X...X.", "..X...X...X...X.",
          "X.X.X.X.X.X.X.X.", "c.X.X.X.c.X.X.X.", "................", gm::snare, gm::tamb, 0, 0.05f, 0, 1.25f, RideLoud | NoHalfTime },
        { "poppunk", "Pop punk", "Rock", "Mica", "Enérgico, con bombo en corcheas",
          "X.x.....X.x.....", "X.x...x.X.x.x...", "....X.......X...", "....X......xX...",
          "X.X.X.X.X.X.X.X.", "R.R.R.R.R.R.R.R.", "................", gm::snare, gm::tamb, 0, 0.1f, 0, 1.22f, RideLoud },
        { "grunge", "Grunge", "Rock", "Leo", "Charles medio abierto, golpe pesado",
          "X.......X.x.....", "X.....x.X.x...x.", "....X.......X...", "....X.......X..o",
          "o.x.o.x.o.x.o.x.", "o.x.o.x.o.x.o.x.", "................", gm::snare, gm::tamb, 0, 0.55f, 0, 1.22f, RideLoud },
        { "indie", "Indie rock", "Rock", "Paz", "Charles en semicorcheas, bombo que se mueve",
          "X..x..x.X.......", "X..x..x.X.x..x..", "....X.......X...", "....X..o....X...",
          "xgxgxgxgxgxgxgxg", "XxxxXxxxXxxxXxxx", "................", gm::snare, gm::tamb, 0, 0.25f, 0, 1.08f, RideLoud },

        // ---- Metal
        { "heavymetal", "Heavy metal", "Metal", "Bruno", "Galope y ride",
          "X.xxX.xxX.xxX.xx", "X.xxX.xxX.xxX.xx", "....X.......X...", "....X.......X...",
          "R.r.R.r.R.r.R.r.", "c.r.R.r.c.r.R.r.", "................", gm::snare, gm::tamb, 0, 0.05f, 0.1f, 1.28f, RideLoud | NoHalfTime },
        { "thrash", "Thrash", "Metal", "Sole", "Skank veloz; complejo: doble bombo",
          "X...X...X...X...", "XXXXXXXXXXXXXXXX", "..X...X...X...X.", "....X.......X...",
          "c.x.c.x.c.x.c.x.", "R.R.R.R.R.R.R.R.", "................", gm::snare, gm::tamb, 0, 0, 0.15f, 1.3f, RideLoud | NoHalfTime | DoubleKick },
        { "modernmetal", "Metal moderno", "Metal", "Ivo", "Medio tiempo pesado con ráfagas de doble bombo",
          "X..X..X...X.X...", "XXXX..X.XXXX..X.", "........X.......", "........X.......",
          "c...c...c...c...", "c.r.c.r.c.r.c.r.", "................", gm::snare, gm::tamb, 0, 0, 0.25f, 1.3f, RideLoud | NoHalfTime | DoubleKick },
        { "deathmetal", "Death metal", "Metal", "Rafa", "Doble bombo; complejo: blast beat",
          "XXXXXXXXXXXXXXXX", "XXXXXXXXXXXXXXXX", "....X.......X...", "....X.......X...",
          "R.R.R.R.R.R.R.R.", "c.c.c.c.c.c.c.c.", "................", gm::snare, gm::tamb, 0, 0, 0.3f, 1.3f, RideLoud | NoHalfTime | Blast },
        { "metalcore", "Metalcore", "Metal", "Tefi", "Breakdowns: bombo con la guitarra, caja en el 3",
          "X..X..X.X..X..X.", "X..XX.X.X..XX.X.", "........X.......", "....X.......X...",
          "c...c...c...c...", "c.x.c.x.c.x.c.x.", "................", gm::snare, gm::tamb, 0, 0, 0.25f, 1.3f, RideLoud | NoHalfTime },
        { "doom", "Doom / stoner", "Metal", "Gonzalo", "Lento, enorme",
          "X.......X.x.....", "X.....x.X.x.....", "........X.......", "........X......o",
          "R...R...R...R...", "R.r.R.r.R.r.R.r.", "................", gm::snare, gm::tamb, 0, 0.8f, 0, 1.22f, RideLoud | NoHalfTime },

        // ---- Pop
        { "pop", "Pop", "Pop", "Lucía", "Prolijo y bailable",
          "X.......X.......", "X..x..x.X.x...x.", "....X.......X...", "....X..o....X..o",
          "X.x.X.x.X.x.X.x.", "XgxgXgxgXgxgXgxg", "....X.......X...", gm::snare, gm::tamb, 0, 0.15f, 0.2f, 1.1f, Clap | UpbeatHats },
        { "poprock", "Pop rock", "Pop", "Santi", "Para cantar arriba",
          "X.....x.X.x.....", "X.x...x.X.x...x.", "....X.......X...", "....X.......X..o",
          "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.o.", "....X.......X...", gm::snare, gm::tamb, 0, 0.25f, 0, 1.14f, RideLoud },
        { "dancepop", "Dance pop", "Pop", "Caro", "Bombo en negras, charles abierto en el contratiempo",
          "X...X...X...X...", "X...X...X...X..x", "....X.......X...", "....X.......X..o",
          "..o...o...o...o.", "xxoxxxoxxxoxxxox", "..x...x...x...x.", gm::snare, gm::shaker, 0, 0.05f, 0.5f, 1.1f, Clap | NoHalfTime },
        { "ballad", "Balada", "Pop", "Inés", "Suave, con aro en la caja",
          "X.......X.......", "X......xX.......", "....X.......X...", "....X.......X...",
          "x.x.x.x.x.x.x.x.", "r.r.r.r.r.r.r.r.", "................", gm::rim, gm::tamb, 0, 0.5f, 0, 0.85f, 0 },

        // ---- Funk y soul
        { "funk", "Funk", "Funk y soul", "Nico", "Mucho groove y notas fantasma",
          "X.........X.....", "X..x..x...X..x..", "....X.......X...", ".o.oX.oo.o.oX.oo",
          "XxxxXxxxXxxxXxxx", "XxxxXxxoXxxxXxxo", "X.....X...X.....", gm::snare, gm::cowbell, 0, 0, 0, 1.08f, NoHalfTime },
        { "motown", "Soul / Motown", "Funk y soul", "Rita", "Caja en todos los tiempos, pandereta",
          "X.x...x.X.x...x.", "X.x...x.X.x...xx", "X...X...X...X...", "X...X...X...X..o",
          "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.x.", "....X.......X...", gm::snare, gm::tamb, 0, 0.3f, 0, 1.0f, NoHalfTime },
        { "disco", "Disco", "Funk y soul", "Dani", "Bombo en negras y charles que se abre",
          "X...X...X...X...", "X...X...X..xX...", "....X.......X...", "....X..o....X...",
          "x.o.x.o.x.o.x.o.", "xxoxxxoxxxoxxxox", "................", gm::snare, gm::tamb, 0, 0.1f, 0.2f, 1.05f, NoHalfTime | UpbeatHats },
        { "neosoul", "R&B / neo soul", "Funk y soul", "Kiara", "Arrastrado y suave",
          "X......x..X.....", "X......x..X..x..", "....X.......X...", "....X..o....X..o",
          "x.x.x.x.x.x.x.x.", "xgxgxgxgxgxgxgxg", "................", gm::snare, gm::shaker, 0.35f, 0.9f, 0, 0.95f, Clap },

        // ---- Hip hop y electrónica
        { "boombap", "Hip hop boom bap", "Hip hop y electrónica", "Mati", "Pesado y con swing",
          "X......xX.x.....", "X......xX.x...x.", "....X.......X...", "....X.......X..o",
          "x.x.x.x.x.x.x.x.", "xgxgxgxgxgxgxgxg", "................", gm::snare, gm::shaker, 0.3f, 0.6f, 0.3f, 1.1f, NoHalfTime },
        { "trap", "Trap", "Hip hop y electrónica", "Lauti", "Caja en el 3 y charles con redobles",
          "X.....x...X.....", "X.....x...X..x..", "........X.......", "........X.......",
          "xxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxx", "................", gm::clap, gm::shaker, 0, 0, 0.8f, 1.1f, NoHalfTime | TrapRolls },
        { "lofi", "Lo-fi", "Hip hop y electrónica", "Sol", "Relajado, con swing",
          "X.........X.....", "X......x..X...x.", "....X.......X...", "....X..o....X.o.",
          "x.x.x.x.x.x.x.x.", "xgxgxgxgxgxgxgxg", "..x...x...x...x.", gm::snare, gm::shaker, 0.45f, 0.9f, 0.2f, 0.9f, 0 },
        { "electronic", "House / electrónica", "Hip hop y electrónica", "Max", "Bombo en negras, caja de ritmos",
          "X...X...X...X...", "X...X...X...X..x", "....X.......X...", "....X.......X..x",
          "..o...o...o...o.", "xxoxxxoxxxoxxxox", ".x.x.x.x.x.x.x.x", gm::clap, gm::shaker, 0, 0, 1.0f, 1.05f, NoHalfTime | UpbeatHats },
        { "dnb", "Drum and bass", "Hip hop y electrónica", "Fran", "Rápido y quebrado",
          "X.........X.....", "X.........X.x...", "....X.......X...", "....X..o.o..X..o",
          "x.x.x.x.x.x.x.x.", "xxxxxxxxxxxxxxxx", "................", gm::snare, gm::shaker, 0, 0, 0.7f, 1.1f, NoHalfTime },
        { "reggaeton", "Reggaetón", "Hip hop y electrónica", "Fede", "Dembow",
          "X...X...X...X...", "X...X...X...X...", "...X..X....X..X.", "...X..X.o..X.oX.",
          "x.x.x.x.x.x.x.x.", "xxxxxxxxxxxxxxxx", "..x...x...x...x.", gm::snare, gm::shaker, 0, 0, 0.6f, 1.08f, Clap | NoHalfTime },

        // ---- Latino
        { "cuarteto", "Cuarteto", "Latino", "Ramiro", "Tunga-tunga cordobés",
          "X...X...X...X...", "X..oX..oX..oX..o", "..X...X...X...X.", "..Xo..Xo..Xo..Xo",
          "................", "................", "XXXXXXXXXXXXXXXX", gm::rim, gm::shaker, 0, 0, 0, 1.05f, NoHalfTime },
        { "cumbia", "Cumbia", "Latino", "Nahuel", "Güira y aro en el contratiempo",
          "X.......X.......", "X......xX......x", "..x...x...x...x.", "..X...X...X...X.",
          "x.x.x.x.x.x.x.x.", "xgxgxgxgxgxgxgxg", "XxxxXxxxXxxxXxxx", gm::rim, gm::shaker, 0, 0, 0, 1.0f, NoHalfTime },
        { "bossa", "Bossa nova", "Latino", "Lucas", "Bombo suave y clave en el aro",
          "X..xX..xX..xX..x", "X..xX..xX..xX..x", "X..X..X...X..X..", "X..X..X...X..X..",
          "x.x.x.x.x.x.x.x.", "r.r.r.r.r.r.r.r.", "................", gm::rim, gm::shaker, 0, 0.3f, 0, 0.75f, NoHalfTime },
        { "samba", "Samba", "Latino", "Carla", "Surdo en el bombo y caja picada",
          "X..xX..xX..xX..x", "X..xX..xX..xX..x", "X.oXo.X.X.oXo.X.", "XooXooXoXooXooXo",
          "XxxxXxxxXxxxXxxx", "XxxxXxxxXxxxXxxx", "................", gm::snare, gm::shaker, 0, 0, 0, 1.0f, NoHalfTime },
        { "reggae", "Reggae", "Latino", "Toto", "One drop: bombo y aro en el 3",
          "........X.......", "........X......x", "........X.......", "........X.....o.",
          "x.x.x.x.x.x.x.x.", "x.xxx.xxx.xxx.xx", "................", gm::rim, gm::shaker, 0.5f, 0.6f, 0, 0.95f, NoHalfTime },
        { "ska", "Ska", "Latino", "Pipa", "Charles en el contratiempo",
          "X.......X.......", "X.....x.X.......", "....X.......X...", "....X.......X..o",
          "..o...o...o...o.", "x.o.x.o.x.o.x.o.", "................", gm::snare, gm::shaker, 0, 0.1f, 0, 1.1f, NoHalfTime | UpbeatHats },

        // ---- Jazz y blues
        { "jazz", "Jazz suave", "Jazz y blues", "Ana", "Ride con swing y escobillas",
          "x...x...x...x...", "x...x..ox...x.o.", "......o.......o.", "..o...o...o...o.",
          "................", "................", "................", gm::snare, gm::shaker, 0.65f, 0.4f, 0, 0.75f, JazzRide | NoHalfTime },
        { "blues", "Blues shuffle", "Jazz y blues", "Willy", "Shuffle con el charles",
          "X.......X.......", "X.....x.X.....x.", "....X.......X...", "....X..o....X..o",
          "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.x.", "................", gm::snare, gm::shaker, 0.7f, 0.5f, 0, 1.0f, NoHalfTime },

        // ---- Country y folk
        { "country", "Country", "Country y folk", "Rodo", "Train beat: caja picada con acentos",
          "X.......X.......", "X...X...X...X...", "....X.......X...", "ooooXoooooooXooo",
          "................", "x.x.x.x.x.x.x.x.", "................", gm::snare, gm::tamb, 0, 0.1f, 0, 0.95f, NoHalfTime },
        { "folk", "Folk / acústico", "Country y folk", "Malena", "Suave, con aro y pandereta",
          "X.......X.......", "X......xX.......", "....X.......X...", "....X.......X...",
          "x...x...x...x...", "x.x.x.x.x.x.x.x.", "..X...X...X...X.", gm::rim, gm::tamb, 0, 0.4f, 0, 0.8f, 0 },
    };

    Pattern hitPattern (const char* s)
    {
        Pattern p {};
        for (int i = 0; i < 16 && s[i]; ++i)
            p[(size_t) i] = s[i] == 'X' ? 1.0f : s[i] == 'x' ? 0.7f : s[i] == 'o' ? 0.35f : 0.0f;
        return p;
    }
} // namespace

const std::vector<DrummerStyle>& drummerStyles()
{
    static const std::vector<DrummerStyle> styles = []
    {
        std::vector<DrummerStyle> v;
        for (auto& d : styleDefs)
            v.push_back ({ d.id, juce::String::fromUTF8 (d.name), juce::String::fromUTF8 (d.who), juce::String::fromUTF8 (d.desc), juce::String::fromUTF8 (d.genre) });
        return v;
    }();
    return styles;
}

namespace
{
    enum HatCode : uint8_t { HNone = 0, HClosed, HAccent, HOpen, HRide, HRideAcc, HBell, HCrash, HPedal, HGhost };

    struct StyleBrain
    {
        Pattern kickSimple {}, kickComplex {}, snareSimple {}, snareComplex {}, perc {};
        std::array<uint8_t, 16> hatS {}, hatC {};
        int  snareNote = gm::snare, percNote = gm::tamb;
        bool clapLayer = false, rideWhenLoud = false, jazzRide = false, openHatOffbeats = false;
        bool halfTimeBridge = true, upbeatHats = false, doubleKick = false, blast = false, trapRolls = false, hatsCanFill = false;
        float extraSwing = 0, laidBack = 0.3f, machine = 0, vel = 1.0f;
    };

    std::array<uint8_t, 16> hatPattern (const char* s)
    {
        std::array<uint8_t, 16> p {};
        for (int i = 0; i < 16 && s[i]; ++i)
            switch (s[i])
            {
                case 'x': p[(size_t) i] = HClosed; break;  case 'X': p[(size_t) i] = HAccent; break;
                case 'o': p[(size_t) i] = HOpen; break;    case 'r': p[(size_t) i] = HRide; break;
                case 'R': p[(size_t) i] = HRideAcc; break; case 'b': p[(size_t) i] = HBell; break;
                case 'c': p[(size_t) i] = HCrash; break;   case 'p': p[(size_t) i] = HPedal; break;
                case 'g': p[(size_t) i] = HGhost; break;   default: break;
            }
        return p;
    }

    StyleBrain brainFor (const juce::String& id)
    {
        const StyleDef* d = &styleDefs[0];
        for (auto& x : styleDefs) if (id == x.id) { d = &x; break; }
        StyleBrain b;
        b.kickSimple = hitPattern (d->kickS);   b.kickComplex = hitPattern (d->kickC);
        b.snareSimple = hitPattern (d->snareS); b.snareComplex = hitPattern (d->snareC);
        b.perc = hitPattern (d->perc);
        b.hatS = hatPattern (d->hatS);          b.hatC = hatPattern (d->hatC);
        b.snareNote = d->snareNote;  b.percNote = d->percNote;
        b.extraSwing = d->swing;     b.laidBack = d->laidBack;  b.machine = d->machine;  b.vel = d->vel;
        b.rideWhenLoud = d->flags & RideLoud;   b.jazzRide = d->flags & JazzRide;  b.clapLayer = d->flags & Clap;
        b.halfTimeBridge = ! (d->flags & NoHalfTime);
        b.doubleKick = d->flags & DoubleKick;   b.blast = d->flags & Blast;        b.trapRolls = d->flags & TrapRolls;
        b.upbeatHats = d->flags & UpbeatHats;
        // closed hats on the eighths: the "level" can add more of them
        for (int i = 0; i < 16; i += 2) if (b.hatS[(size_t) i] == HClosed || b.hatS[(size_t) i] == HAccent) b.hatsCanFill = true;
        return b;
    }

    // ------------------------------------------------------------------ writing notes like a person
    struct Writer
    {
        std::vector<Note>& out;
        juce::Random& rng;
        const DrummerSettings& s;
        const StyleBrain& brain;
        double length;
        float swing;

        float gauss() { return (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) / 1.5f; }   // -1..1, mostly near 0

        void hit (double beat, int pitch, float velocity, double len = 0.1)
        {
            if (beat < 0.0 || beat >= length || velocity <= 0.0f) return;
            // Swing: push the "e" and "a" 16ths later
            const double pos = beat - std::floor (beat);
            const int step = juce::roundToInt (pos * 4.0) % 4;
            if (step == 1 || step == 3) beat += swing * (1.0 / 12.0);

            // A person isn't a grid: a little loose, the snare sits back a hair, the hats lead
            const float human = s.humanize * (1.0f - brain.machine);
            double feel = 0.0;
            if (pitch == gm::snare || pitch == gm::rim || pitch == gm::clap) feel = 0.012 * brain.laidBack;
            else if (pitch == gm::hatClosed || pitch == gm::hatOpen || pitch == gm::ride) feel = -0.004;
            beat += (feel + gauss() * 0.014) * human;
            velocity *= 1.0f + gauss() * (brain.vel > 1.15f ? 0.08f : 0.14f) * juce::jmax (0.15f, s.humanize);
            out.push_back ({ juce::jlimit (0.0, length - 0.01, beat), len, pitch, juce::jlimit (0.04f, 1.0f, velocity) });
        }
    };

    float lerp (float a, float b, float t) { return a + (b - a) * t; }

    // ------------------------------------------------------------------ how each part of a song feels
    struct Feel
    {
        float dComplex = 0, dLoud = 0;
        bool ride = false, floorTom = false, halfTime = false, sparseHats = false, percussion = true, moreOpenHats = false;
    };

    Feel feelFor (SectionKind k, const StyleBrain& b, const DrummerSettings& s)
    {
        Feel f;
        const bool canHalf = b.halfTimeBridge;
        switch (k)
        {
            case SectionKind::intro:     f.dComplex = -0.25f; f.dLoud = -0.15f; f.sparseHats = true; f.percussion = false; break;
            case SectionKind::prechorus: f.dComplex = 0.1f;   f.dLoud = 0.08f; break;
            case SectionKind::chorus:    f.dComplex = 0.15f;  f.dLoud = 0.2f;  f.ride = b.rideWhenLoud && s.loudness > 0.3f; f.moreOpenHats = true; break;
            case SectionKind::bridge:    f.dComplex = -0.05f; f.dLoud = -0.05f; f.halfTime = canHalf; f.floorTom = canHalf && s.toms && b.rideWhenLoud; break;
            case SectionKind::solo:      f.dComplex = 0.2f;   f.dLoud = 0.15f; f.ride = b.rideWhenLoud; f.moreOpenHats = true; break;
            case SectionKind::outro:     f.dComplex = 0.1f;   f.dLoud = 0.15f; f.ride = b.rideWhenLoud; break;
            case SectionKind::verse:
            case SectionKind::none:      break;
        }
        return f;
    }

    // ------------------------------------------------------------------ the groove of one part
    // A drummer picks a groove for each part and keeps it, changing a little at the end of each
    // phrase. So the groove is drawn once per part (A), with a variation (B) for every 4th bar.
    struct GrooveBar
    {
        std::array<float, 16> kick {}, snare {};   // velocity factor, 0 = nothing
        std::array<uint8_t, 16> hatOpen {};
    };
    struct Groove { GrooveBar a, b; int hatAccent = 0; };

    Groove makeGroove (const StyleBrain& brain, const DrummerSettings& s, const Feel& feel, float c, juce::Random& rng)
    {
        Groove g;
        const float density = s.kickSnareLevel / 5.0f;
        for (int p = 0; p < 16; ++p)
        {
            float kp = lerp (brain.kickSimple[(size_t) p], brain.kickComplex[(size_t) p], c);
            if (kp < 1.0f) kp *= density;
            if (kp >= 1.0f || (kp > 0.0f && rng.nextFloat() < kp)) g.a.kick[(size_t) p] = p == 0 ? 1.05f : (p % 4 == 0 ? 1.0f : 0.88f);

            const bool backbeat = brain.snareSimple[(size_t) p] >= 0.99f;
            const float sp = lerp (brain.snareSimple[(size_t) p], brain.snareComplex[(size_t) p], c);
            if (backbeat) g.a.snare[(size_t) p] = 1.0f;
            else if (sp > 0.0f && rng.nextFloat() < sp * density * (0.6f + c))
                g.a.snare[(size_t) p] = brain.snareSimple[(size_t) p] < 0.5f ? 0.3f + 0.1f * rng.nextFloat() : 0.9f;   // ghost or accent
            // Ghost notes make a rock/pop beat sound played instead of programmed
            else if (brain.rideWhenLoud && c > 0.3f && p % 2 == 1 && rng.nextFloat() < (c - 0.3f) * 0.35f)
                g.a.snare[(size_t) p] = 0.22f + 0.08f * rng.nextFloat();
        }

        if (feel.halfTime)
        {
            // Half time: the snare only on 3, a lighter kick
            for (int p = 0; p < 16; ++p) if (g.a.snare[(size_t) p] >= 0.8f) g.a.snare[(size_t) p] = 0;
            g.a.snare[8] = 1.05f;
            for (int p = 1; p < 16; ++p) if (p != 10 && p != 14) g.a.kick[(size_t) p] = 0;
            g.a.kick[0] = 1.05f;
        }

        // Metal: double kick under everything when it gets complex; a blast beat at the top
        if (brain.doubleKick && c >= 0.6f) for (int p = 0; p < 16; ++p) g.a.kick[(size_t) p] = juce::jmax (g.a.kick[(size_t) p], 0.82f);
        if (brain.blast && c >= 0.65f)
            for (int p = 0; p < 16; ++p)
            {
                g.a.kick[(size_t) p] = p % 2 == 0 ? 0.9f : 0.0f;
                g.a.snare[(size_t) p] = p % 2 == 1 ? 0.85f : 0.0f;
            }
        if (brain.openHatOffbeats) for (int p = 2; p < 16; p += 4) g.a.hatOpen[(size_t) p] = 1;
        if (feel.moreOpenHats && rng.nextFloat() < 0.6f) g.a.hatOpen[14] = 1;

        // Variation for the end of each phrase: an extra kick or two, a pickup on the snare, an opening
        g.b = g.a;
        const int extraKicks[] = { 7, 10, 13, 14, 15 };
        for (int i = 0, n = 1 + (rng.nextFloat() < c ? 1 : 0); i < n; ++i)
        {
            const int p = extraKicks[rng.nextInt (5)];
            if (g.b.snare[(size_t) p] < 0.8f) g.b.kick[(size_t) p] = 0.85f;
        }
        if (! feel.halfTime && rng.nextFloat() < 0.6f) g.b.snare[15] = juce::jmax (g.b.snare[15], 0.45f + 0.3f * c);
        if (rng.nextFloat() < 0.5f) g.b.hatOpen[14] = 1;

        g.hatAccent = brain.upbeatHats ? 1 : rng.nextInt (3) == 0 ? 2 : 0;
        return g;
    }

    // ------------------------------------------------------------------ fills
    void writeFill (Writer& w, juce::Random& rng, const DrummerSettings& s, double start, double beats,
                    float vel, float c, int type)
    {
        const int steps = juce::jmax (1, (int) std::round (beats * 4.0));
        const int toms[] = { gm::tomHigh, gm::tomMid, gm::tomLow, gm::tomFloor };
        auto tomOr = [&] (int idx) { return s.toms ? toms[juce::jlimit (0, 3, idx)] : (s.kickSnare ? gm::snare : -1); };
        auto snareOr = [&]() { return s.kickSnare ? gm::snare : (s.toms ? gm::tomHigh : -1); };
        auto put = [&] (double b, int note, float v) { if (note >= 0) w.hit (b, note, v); };

        if (beats <= 0.5)
        {
            // pickup: two quick notes into the next bar
            for (int i = 0; i < steps; ++i)
                put (start + i * 0.25, i == steps - 1 && s.toms && rng.nextFloat() < 0.4f ? gm::tomHigh : snareOr(), vel * (0.55f + 0.25f * i));
            return;
        }

        for (int i = 0; i < steps; ++i)
        {
            const double b = start + i * 0.25;
            const float progress = (float) i / (float) steps;
            const float v = vel * (0.72f + 0.38f * progress);
            const bool onBeat = i % 4 == 0;
            switch (type)
            {
                case 1: // snare roll that grows
                    if (i % 2 == 0 || rng.nextFloat() < 0.5f + 0.5f * c) put (b, snareOr(), vel * (0.45f + 0.65f * progress));
                    break;
                case 2: // eighths around the kit
                    if (i % 2 == 0) put (b, i < steps / 2 ? snareOr() : tomOr ((int) (progress * 4.0f)), v);
                    break;
                case 3: // syncopated: snare, tom, floor
                    if (i % 4 == 0) put (b, snareOr(), v);
                    else if (i % 4 == 2) put (b, tomOr (1 + (i / 4) % 2), v * 0.95f);
                    else if (i % 4 == 3) put (b, tomOr (3), v * 0.9f);
                    break;
                case 4: // snare and kick together, like a band hit
                    if (i % 2 == 0) { put (b, snareOr(), v); if (s.kickSnare) w.hit (b, gm::kick, v * 0.95f); }
                    break;
                default: // down the toms
                {
                    const bool play = i % 2 == 0 || rng.nextFloat() < 0.45f + 0.55f * c;
                    if (play) put (b, progress < 0.25f ? snareOr() : tomOr ((int) (progress * 4.0f) - 1 + (rng.nextFloat() < 0.3f ? 1 : 0)), v);
                    break;
                }
            }
            if (s.kickSnare && onBeat && type != 4) w.hit (b, gm::kick, vel * 0.85f);
        }
    }
} // namespace

//==============================================================================
const std::vector<std::pair<juce::String, juce::String>>& sectionKinds()
{
    static const std::vector<std::pair<juce::String, juce::String>> kinds {
        { "intro", "Intro" }, { "verse", "Estrofa" }, { "prechorus", "Pre-estribillo" }, { "chorus", "Estribillo" },
        { "bridge", "Puente" }, { "solo", "Solo" }, { "outro", "Final" },
    };
    return kinds;
}

SectionKind sectionKindFromId (const juce::String& id)
{
    const auto& k = sectionKinds();
    for (size_t i = 0; i < k.size(); ++i)
        if (k[i].first == id) return (SectionKind) (i + 1);
    return SectionKind::none;
}

juce::String sectionKindId (SectionKind k)
{
    const int i = (int) k - 1;
    return i >= 0 && i < (int) sectionKinds().size() ? sectionKinds()[(size_t) i].first : juce::String();
}

std::vector<Note> generateDrumPart (const DrumRequest& req)
{
    std::vector<Note> notes;
    const auto& s = req.settings;
    const auto brain = brainFor (s.style);
    const double bpb = juce::jmax (1.0, req.beatsPerBar);
    const int stepsPerBar = juce::jmax (4, (int) std::round (bpb * 4.0));
    const int numBars = juce::jmax (1, (int) std::ceil (req.lengthBeats / bpb - 1.0e-6));
    const float swing = juce::jlimit (0.0f, 1.0f, s.swing + brain.extraSwing * (1.0f - s.swing));
    const int cymLevel = s.cymbalLevel;

    // What the other track plays, per 16th note
    std::vector<float> follow = req.followSteps;
    if (follow.empty() && ! req.followOnsets.empty())
    {
        follow.assign ((size_t) std::ceil (req.lengthBeats * 4.0) + 1, 0.0f);
        for (auto o : req.followOnsets)
            if (o >= 0.0 && o < req.lengthBeats) follow[(size_t) std::lround (o * 4.0) % follow.size()] = 1.0f;
    }
    const bool following = ! follow.empty();
    auto followAt = [&] (double beat) -> float
    {
        const long i = std::lround (beat * 4.0);
        return following && i >= 0 && i < (long) follow.size() ? follow[(size_t) i] : 0.0f;
    };

    std::map<int, Groove> grooves;   // one per kind of part, so a chorus sounds like the other choruses
    int barInPart = 0;
    bool crashNext = req.crashAtStart;
    SectionKind prevKind = SectionKind::none;

    for (int bar = 0; bar < numBars; ++bar)
    {
        const DrumBar info = bar < (int) req.bars.size() ? req.bars[(size_t) bar] : DrumBar {};
        const double barStart = bar * bpb;
        const double barEnd = juce::jmin (req.lengthBeats, barStart + bpb);
        if (bar > 0 && (info.sectionStart || info.kind != prevKind)) { barInPart = 0; crashNext = crashNext || info.sectionStart; }
        prevKind = info.kind;

        juce::Random rng ((juce::int64) req.seed * 7919 + bar * 104729 + s.style.hashCode());
        Writer w { notes, rng, s, brain, req.lengthBeats, swing };
        const Feel feel = feelFor (info.kind, brain, s);

        // How loud the guitar (or whatever it follows) is in this bar
        const float energy = bar < (int) req.followBarEnergy.size() ? req.followBarEnergy[(size_t) bar] : -1.0f;
        float c = s.complexity + feel.dComplex, loud = s.loudness + feel.dLoud;
        if (energy >= 0.0f)
        {
            if (energy < 0.06f) { c -= 0.3f; loud -= 0.2f; }            // the guitar stops: the drums back off
            else { c += (energy - 0.5f) * 0.3f; loud += (energy - 0.5f) * 0.35f; }
        }
        c = juce::jlimit (0.0f, 1.0f, c);
        loud = juce::jlimit (0.0f, 1.0f, loud);
        const float baseVel = (0.4f + 0.52f * loud) * brain.vel * (1.0f + w.gauss() * 0.04f);

        // Busy strumming asks for busier hats
        int busy = 0;
        if (following) for (int i = 0; i < stepsPerBar; ++i) busy += followAt (barStart + i * 0.25) > 0.35f;
        const float busyness = following ? busy / (float) stepsPerBar : -1.0f;

        const int kindKey = (int) info.kind;
        if (grooves.find (kindKey) == grooves.end())
        {
            juce::Random grng ((juce::int64) req.seed * 6007 + kindKey * 1013 + s.style.hashCode());
            grooves[kindKey] = makeGroove (brain, s, feel, juce::jlimit (0.0f, 1.0f, s.complexity + feel.dComplex), grng);
        }
        const Groove& groove = grooves[kindKey];
        const bool variation = barInPart % 4 == 3 || (c > 0.7f && barInPart % 2 == 1 && rng.nextFloat() < 0.3f);
        const GrooveBar& gb = variation ? groove.b : groove.a;

        // ---- the last bar of the song: one big hit and let it ring ----
        if (info.ending)
        {
            if (s.kickSnare) { w.hit (barStart, gm::kick, baseVel * 1.1f); if (loud > 0.35f) w.hit (barStart, gm::snare, baseVel); }
            if (s.cymbals) w.hit (barStart, gm::crash, baseVel * 1.1f, 2.0);
            crashNext = false;
            ++barInPart;
            continue;
        }

        // ---- fills: into the next part, at the end of the region, and little pickups every 8 bars ----
        double fillBeats = 0.0;
        const bool lastBar = bar == numBars - 1;
        const bool partEnds = (info.sectionEnd && ! lastBar) || (lastBar && req.fillAtEnd);
        if (partEnds && s.fills >= 0.05f)
            fillBeats = s.fills > 0.9f && bpb >= 4.0 ? bpb : s.fills > 0.55f ? 2.0 : 1.0;
        else if (! lastBar && barInPart % 8 == 7 && s.fills > 0.4f)
            fillBeats = 0.5;
        if (req.bars.empty() && ! lastBar && fillBeats > 0.5) fillBeats = 0.5;   // no parts marked: only the end of the region gets a real fill
        const double fillStart = juce::jmax (barStart, barEnd - fillBeats);

        bool stabbed = false;
        for (int step = 0; step < stepsPerBar; ++step)
        {
            const double beat = barStart + step * 0.25;
            if (beat >= req.lengthBeats) break;
            if (fillBeats > 0.0 && beat >= fillStart - 1.0e-9) break;   // the fill takes over

            const int p = step % 16;
            const bool downbeat = step == 0;
            const bool onBeat = (step % 4) == 0;
            const float st = followAt (beat);
            bool crashHere = false;

            // ---- kick & snare ----
            if (s.kickSnare)
            {
                float kv = gb.kick[(size_t) p];
                if (following)
                {
                    // Play the kick with the guitar's strong hits; more of them when it's complex
                    if (st > 0.6f && (step % 2 == 0 || c > 0.55f || st > 0.8f)) kv = juce::jmax (kv, 0.75f + 0.3f * st);
                    else if (kv > 0.0f && ! downbeat && st < 0.05f && energy > 0.2f && rng.nextFloat() < 0.35f) kv = 0.0f;
                    // A stab after a gap (the band hits together): kick + crash
                    const bool gap = followAt (beat - 0.25) < 0.25f && followAt (beat - 0.5) < 0.25f && followAt (beat - 0.75) < 0.25f;
                    if (! stabbed && ! downbeat && st > 0.88f && gap && loud > 0.45f) { kv = 1.05f; crashHere = s.cymbals; stabbed = true; }
                    if (downbeat && st > 0.85f && bar > 0 && bar - 1 < (int) req.followBarEnergy.size() && req.followBarEnergy[(size_t) bar - 1] < 0.1f)
                        crashHere = s.cymbals;   // the guitar comes back in
                }
                if (downbeat && crashNext && brain.kickSimple[0] > 0.0f) kv = juce::jmax (kv, 1.05f);   // (not in a one drop)
                if (kv > 0.0f) w.hit (beat, gm::kick, baseVel * kv);

                const float sv = gb.snare[(size_t) p];
                if (sv > 0.0f)
                {
                    const bool soft = loud < 0.3f && sv >= 0.8f && brain.rideWhenLoud;   // very soft: cross-stick instead of the snare
                    w.hit (beat, soft ? gm::rim : brain.snareNote, baseVel * sv * (sv >= 0.8f ? 1.02f : 1.0f));
                    if (brain.clapLayer && sv >= 0.99f && c > 0.45f) w.hit (beat, gm::clap, baseVel * 0.8f);
                }
            }

            // ---- hi-hat / ride / floor tom ----
            if (s.cymbals)
            {
                if ((downbeat && crashNext && loud > 0.2f) || crashHere)
                {
                    w.hit (beat, gm::crash, baseVel * (crashHere ? 1.0f : 0.95f), 1.5);
                }
                else if (brain.jazzRide)
                {
                    static constexpr int ridePattern[16] = { 1,0,0,0, 1,0,0,1, 1,0,0,0, 1,0,0,1 };
                    if (ridePattern[p] || (c > 0.6f && p % 2 == 0 && rng.nextFloat() < c - 0.5f))
                        w.hit (beat, gm::ride, baseVel * (onBeat ? 0.85f : 0.62f));
                    if (p == 4 || p == 12) w.hit (beat, gm::hatPedal, baseVel * 0.55f);
                }
                else
                {
                    // The style's own hi-hat/ride part; the "level" thins it out (1-2) or fills it (6+)
                    int code = (c >= 0.55f ? brain.hatC : brain.hatS)[(size_t) p];
                    if (cymLevel <= 2 && p % 4 != 0 && code != HOpen) code = HNone;
                    if ((feel.sparseHats || (busyness >= 0.0f && busyness < 0.1f && energy < 0.15f)) && p % 2 == 1) code = HNone;
                    if (brain.hatsCanFill && code == HNone && (cymLevel >= 6 || (busyness > 0.5f && p % 2 == 0))) code = p % 2 == 0 ? HClosed : HGhost;
                    if (code != HNone && gb.hatOpen[(size_t) p] && (code == HClosed || code == HAccent)) code = HOpen;
                    if (code != HNone && cymLevel >= 7 && p == 14 && (code == HClosed || code == HAccent)) code = HOpen;
                    const bool useRide = feel.ride || (brain.rideWhenLoud && loud > 0.85f);
                    if (useRide && (code == HClosed || code == HAccent || code == HGhost || code == HOpen)) code = (onBeat || code == HAccent) ? HRideAcc : HRide;
                    if (code != HNone)
                    {
                        float v = 0.66f;
                        int note = gm::hatClosed;
                        switch (code)
                        {
                            case HAccent:  v = 0.88f; break;
                            case HGhost:   v = 0.42f; break;
                            case HOpen:    v = 0.8f;  note = gm::hatOpen; break;
                            case HRide:    v = 0.7f;  note = gm::ride; break;
                            case HRideAcc: v = 0.86f; note = gm::ride; break;
                            case HBell:    v = 0.85f; note = 53; break;
                            case HCrash:   v = 0.88f; note = gm::crash; break;
                            case HPedal:   v = 0.5f;  note = gm::hatPedal; break;
                            default:       v = onBeat ? 0.72f : 0.62f; break;
                        }
                        if (groove.hatAccent == 1 && (code == HClosed || code == HGhost)) v *= p % 4 == 2 ? 1.25f : 0.85f;   // accent on the "&"
                        if (feel.floorTom && s.toms && p % 2 == 0 && note != gm::crash)
                            w.hit (beat, gm::tomFloor, baseVel * 0.7f);
                        else
                            w.hit (beat, note, baseVel * v, note == gm::crash ? 1.5 : 0.1);
                        // the foot closes an open hat on the next beat
                        if (note == gm::hatOpen && p + 2 < 16 && (p + 2) % 4 == 0 && (c >= 0.55f ? brain.hatC : brain.hatS)[(size_t) p + 2] == HNone)
                            w.hit (beat + 0.5, gm::hatPedal, baseVel * 0.45f);
                        // trap: quick rolls on the hats at the end of the phrase
                        if (brain.trapRolls && note == gm::hatClosed && (variation ? p >= 8 : p >= 12) && rng.nextFloat() < 0.35f + 0.4f * c)
                        {
                            const int n = rng.nextFloat() < 0.5f ? 2 : 3;
                            for (int k = 1; k < n; ++k) w.hit (beat + k * 0.25 / n, gm::hatClosed, baseVel * v * (0.6f + 0.15f * k));
                        }
                    }
                }
            }

            // ---- percussion ----
            if (s.percussion && feel.percussion && brain.perc[(size_t) p] > 0.0f)
            {
                const float pp = juce::jmin (1.0f, s.percussionLevel / 5.0f);
                if (rng.nextFloat() < pp) w.hit (beat, brain.percNote, baseVel * (onBeat ? 0.72f : 0.52f));
            }
        }
        crashNext = false;

        if (fillBeats > 0.0)
        {
            juce::Random frng ((juce::int64) req.seed * 31337 + bar * 97 + 17);
            Writer fw { notes, frng, s, brain, req.lengthBeats, swing * 0.5f };
            int type = frng.nextInt (5);
            if (info.kind == SectionKind::prechorus && frng.nextFloat() < 0.6f) type = 1;
            if (! brain.rideWhenLoud && type == 4) type = 0;
            writeFill (fw, frng, s, fillStart, barEnd - fillStart, baseVel, c, type);
            if (fillBeats >= 1.0) crashNext = true;   // land the fill on a crash
        }
        ++barInPart;
    }

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    return notes;
}

//==============================================================================
void analyseFollowAudio (const float* x, int n, double sampleRate, double samplesPerBeat, double beatsPerBar,
                         std::vector<float>& steps, std::vector<float>& barEnergy)
{
    steps.clear(); barEnergy.clear();
    if (x == nullptr || n <= 0 || sampleRate <= 0 || samplesPerBeat <= 0) return;
    const double stepSamples = samplesPerBeat / 4.0;
    const int numSteps = (int) std::ceil (n / stepSamples);
    const int barSamples = (int) std::round (samplesPerBeat * beatsPerBar);
    const int numBars = juce::jmax (1, (int) std::ceil (n / (double) barSamples));
    steps.assign ((size_t) numSteps, 0.0f);
    barEnergy.assign ((size_t) numBars, 0.0f);

    // Loudness per bar
    float maxRms = 0.0f;
    for (int b = 0; b < numBars; ++b)
    {
        const int a = b * barSamples, e = juce::jmin (n, a + barSamples);
        double sum = 0.0;
        for (int i = a; i < e; ++i) sum += (double) x[i] * x[i];
        barEnergy[(size_t) b] = e > a ? (float) std::sqrt (sum / (e - a)) : 0.0f;
        maxRms = juce::jmax (maxRms, barEnergy[(size_t) b]);
    }
    if (maxRms < 1.0e-4f) { std::fill (barEnergy.begin(), barEnergy.end(), 0.0f); return; }   // silence
    for (auto& e : barEnergy) e = std::pow (e / maxRms, 0.7f);

    // Where notes start: rises in the (brightened) energy, every 5 ms
    const int hop = juce::jmax (32, (int) std::round (sampleRate * 0.005));
    const int numHops = n / hop;
    std::vector<float> logE ((size_t) numHops, -12.0f), odf ((size_t) numHops, 0.0f);
    float top = -12.0f, prev = 0.0f;
    for (int h = 0; h < numHops; ++h)
    {
        double sum = 0.0;
        for (int i = h * hop; i < (h + 1) * hop; ++i) { const float d = x[i] - prev; prev = x[i]; sum += (double) d * d; }
        logE[(size_t) h] = (float) std::log10 (1.0e-12 + sum / hop);
        top = juce::jmax (top, logE[(size_t) h]);
    }
    for (int h = 3; h < numHops; ++h)
    {
        if (logE[(size_t) h] < top - 4.5f) continue;   // more than 45 dB down: noise
        const float before = (logE[(size_t) h - 1] + logE[(size_t) h - 2] + logE[(size_t) h - 3]) / 3.0f;
        odf[(size_t) h] = juce::jmax (0.0f, logE[(size_t) h] - before);
    }

    // Strongest start near each 16th (people aren't exact: ±35 ms)
    const int win = juce::jmax (1, (int) std::round (0.035 * sampleRate / hop));
    std::vector<float> nonZero;
    for (int k = 0; k < numSteps; ++k)
    {
        const int c = (int) std::round (k * stepSamples / hop);
        float m = 0.0f;
        for (int h = juce::jmax (0, c - win); h <= juce::jmin (numHops - 1, c + win); ++h) m = juce::jmax (m, odf[(size_t) h]);
        steps[(size_t) k] = m;
        if (m > 0.0f) nonZero.push_back (m);
    }
    if (nonZero.empty()) return;
    std::sort (nonZero.begin(), nonZero.end());
    const float ref = juce::jmax (0.05f, nonZero[(size_t) ((nonZero.size() - 1) * 0.92)]);
    for (auto& v : steps) v = juce::jlimit (0.0f, 1.0f, v / ref);
}

} // namespace galpon
