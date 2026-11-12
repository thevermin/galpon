// Engine tests: everything runs offline (no audio interface needed).
// Run: ./GalponTests   (exit code 0 = all good)

#include <set>
#include <thread>
#include "engine/Engine.h"
#include "engine/EngineApi.h"
#include "engine/Drummer.h"
#include "engine/Loops.h"
#include "engine/Analysis.h"
#include "engine/SoundLibrary.h"
#include "engine/Flex.h"
#include "engine/Stems.h"
#include "engine/Recorder.h"
#include "engine/Yaml.h"
#include "engine/Feedpak.h"
#include "engine/GamePlayer.h"
#include "engine/NoteScorer.h"
#include "engine/ToneLibrary.h"
#include "engine/PitchDetect.h"

using namespace galpon;

static int failures = 0, checks = 0;

#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FALLA: " << msg << "  (" #cond ")\n"; } } while (0)

static float rms (const juce::AudioBuffer<float>& b)
{
    return juce::jmax (b.getRMSLevel (0, 0, b.getNumSamples()), b.getRMSLevel (1, 0, b.getNumSamples()));
}

static bool finite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

static juce::File tempRoot()
{
    static int counter = 0;
    auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("galpon-tests");
    if (counter == 0) base.deleteRecursively();
    auto f = base.getChildFile ("run" + juce::String (++counter));
    f.createDirectory();
    return f;
}

static void setEnv (const char* name, const juce::String& value)
{
   #if JUCE_WINDOWS
    _wputenv_s (juce::String (name).toWideCharPointer(), value.toWideCharPointer());
   #else
    setenv (name, value.toRawUTF8(), 1);
   #endif
}

struct TestEngine
{
    juce::File root = tempRoot();
    Engine engine { root.getChildFile ("appdata"), false };
    EngineApi api { engine };

    TestEngine()
    {
        engine.prepareOffline (48000.0, 256);
        // keep songs out of the user's Music folder while testing
        engine.songFolder = root.getChildFile ("Song.galpon");
        engine.getAudioFolder().createDirectory();
        engine.song.metronome = false;
    }

    juce::AudioBuffer<float> render (double beats)
    {
        juce::AudioBuffer<float> out;
        engine.renderOffline (out, (int) engine.beatsToSamples (beats));
        return out;
    }
};

static void testBuiltins()
{
    std::cout << "Instrumentos y efectos propios\n";
    for (auto& info : builtinTypes())
    {
        auto p = createBuiltin (info.type);
        CHECK (p != nullptr, "crear " << info.type);
        if (p == nullptr) continue;
        p->setRateAndBufferSizeDetails (48000.0, 256);
        p->prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        float energy = 0;
        bool ok = true;
        juce::Random rng (1);
        for (int block = 0; block < 200; ++block)
        {
            buf.clear();
            midi.clear();
            if (info.category == "instrument")
            {
                if (block == 0)  { midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0); midi.addEvent (juce::MidiMessage::noteOn (1, 38, 0.9f), 10);
                                   midi.addEvent (juce::MidiMessage::noteOn (1, 42, 0.9f), 20); midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 30); }
                if (block == 60)   midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            }
            else
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        buf.setSample (ch, i, 0.3f * std::sin ((block * 256 + i) * 0.05f) + 0.05f * (rng.nextFloat() - 0.5f));
            }
            p->processBlock (buf, midi);
            ok &= finite (buf);
            energy += rms (buf);
        }
        CHECK (ok, info.type << " produce valores válidos");
        CHECK (energy > 0.01f || info.type == "sampler", info.type << " produce sonido (energía " << energy << ")");   // the sampler is silent until it has an instrument

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = createBuiltin (info.type);
        q->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (state.getSize() > 0, info.type << " guarda su estado");
    }
}

static void testPlayback()
{
    std::cout << "Reproducción de pistas MIDI\n";
    TestEngine t;
    auto* track = t.engine.addTrack (TrackKind::Instrument, juce::String (u8"Piano eléctrico"));
    CHECK (track->instrument.processor != nullptr, "la pista tiene instrumento");

    Clip c;
    c.start = 0; c.length = 4; c.loopLength = 4;
    c.notes = { { 0.0, 1.0, 60, 0.9f }, { 0.0, 1.0, 64, 0.9f }, { 0.0, 1.0, 67, 0.9f }, { 2.0, 1.0, 72, 0.8f } };
    t.engine.addClip (*track, c);

    // Silent while stopped
    auto silent = t.render (1.0);
    CHECK (rms (silent) < 1.0e-6f, "en silencio cuando está detenido");

    t.engine.setPositionBeats (0);
    t.engine.play();
    auto out = t.render (4.0);
    CHECK (finite (out), "audio válido");
    CHECK (rms (out) > 0.01f, "suena al reproducir (rms " << rms (out) << ")");
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 4.0f, "no satura de forma absurda");
    CHECK (std::abs (t.engine.getPositionBeats() - 4.0) < 0.01, "el cursor avanzó 4 tiempos");

    // A device that sends blocks 4x bigger than it promised must still play at the right speed
    t.engine.setPositionBeats (0);
    {
        juce::AudioBuffer<float> big (2, 1024);
        for (int i = 0; i < 47; ++i)
            t.engine.renderBlock (big.getArrayOfWritePointers(), 2, nullptr, 0, 1024);
        CHECK (std::abs (t.engine.getPositionBeats() - t.engine.samplesToBeats (47.0 * 1024)) < 0.01, "bloques grandes no frenan la canción");
        CHECK (rms (big) > 0.001f, "bloques grandes suenan");
    }
    t.engine.setPositionBeats (0);

    // Mute
    t.api.call ("setTrack", juce::JSON::parse (R"({"trackId":)" + juce::String (track->id) + R"(,"mute":true})"));
    t.engine.setPositionBeats (0);
    auto muted = t.render (2.0);
    CHECK (rms (muted) < 1.0e-6f, "silenciar la pista funciona");

    // Cycle
    t.api.call ("setTrack", juce::JSON::parse (R"({"trackId":)" + juce::String (track->id) + R"(,"mute":false})"));
    t.api.call ("setSong", juce::JSON::parse (R"({"cycle":true,"cycleStart":0,"cycleEnd":2})"));
    t.engine.setPositionBeats (0);
    t.render (3.0);
    CHECK (std::abs (t.engine.getPositionBeats() - 1.0) < 0.02, "el ciclo vuelve al inicio (está en " << t.engine.getPositionBeats() << ")");
    t.engine.stop();
}

static void testDrummer()
{
    std::cout << "Baterista IA\n";
    for (auto& style : drummerStyles())
    {
        DrumRequest r;
        r.settings.style = style.id;
        r.lengthBeats = 32;
        r.seed = 7;
        auto notes = generateDrumPart (r);
        int kicks = 0, snares = 0;
        bool inRange = true;
        for (auto& n : notes)
        {
            kicks += n.pitch == 36;
            snares += n.pitch == 38 || n.pitch == 37 || n.pitch == 39;
            inRange &= n.beat >= 0 && n.beat < 32;
        }
        CHECK (notes.size() > 40, style.id << " toca bastante (" << notes.size() << " golpes)");
        CHECK (kicks > 0 && snares > 0, style.id << " usa bombo y caja");
        CHECK (inRange, style.id << " no se sale de la región");

        auto again = generateDrumPart (r);
        bool same = again.size() == notes.size();
        for (size_t i = 0; same && i < notes.size(); ++i)
            same = notes[i].pitch == again[i].pitch && std::abs (notes[i].beat - again[i].beat) < 1e-9;
        CHECK (same, style.id << " es reproducible con la misma semilla");
    }

    // Complexity and loudness change the output
    DrumRequest simple, complex;
    simple.settings.complexity = 0.0f; simple.settings.loudness = 0.2f; simple.lengthBeats = 32;
    complex.settings.complexity = 1.0f; complex.settings.loudness = 0.9f; complex.lengthBeats = 32;
    auto a = generateDrumPart (simple), b = generateDrumPart (complex);
    float va = 0, vb = 0;
    for (auto& n : a) va += n.velocity;
    for (auto& n : b) vb += n.velocity;
    CHECK (b.size() > a.size(), "más complejo = más golpes (" << a.size() << " vs " << b.size() << ")");
    CHECK (vb / b.size() > va / a.size(), "más fuerte = más intensidad");

    // Following the bass
    DrumRequest follow;
    follow.lengthBeats = 8; follow.settings.complexity = 0.2f;
    follow.followOnsets = { 0.0, 1.5, 2.75, 4.0, 5.5, 6.75 };
    auto f = generateDrumPart (follow);
    int matched = 0;
    for (auto& n : f)
        if (n.pitch == 36)
            for (auto o : follow.followOnsets)
                if (std::abs (n.beat - o) < 0.1) { ++matched; break; }
    CHECK (matched >= 4, "el bombo sigue al bajo (" << matched << " coincidencias)");

    // Through the engine
    TestEngine t;
    auto* d = t.engine.addTrack (TrackKind::Drummer);
    auto allNotes = [&] { size_t n = 0; for (auto& c : d->clips) n += c.notes.size(); return n; };
    CHECK (d->clips.size() >= 6 && allNotes() > 100, "la pista de baterista arranca con una canción armada por partes");
    const auto before = allNotes();
    t.api.call ("setDrummer", juce::JSON::parse (R"({"trackId":)" + juce::String (d->id) + R"(,"settings":{"complexity":1.0,"cymbalLevel":6}})"));
    CHECK (allNotes() > before, "cambiar ajustes regenera el patrón");
    t.engine.setPositionBeats (0);
    t.engine.play();
    auto out = t.render (8.0);
    CHECK (rms (out) > 0.01f && finite (out), "la batería suena");
    t.engine.stop();
}

// A plucked guitar-ish note (decaying, bright) at `at` seconds
static void addPluck (std::vector<float>& x, double sr, double at, float amp, float freq = 196.0f)
{
    const int a = (int) (at * sr);
    for (int i = 0; i < (int) (0.25 * sr) && a + i < (int) x.size(); ++i)
    {
        const float t = i / (float) sr;
        x[(size_t) (a + i)] += amp * std::exp (-t * 14.0f) * (std::sin (juce::MathConstants<float>::twoPi * freq * t)
                                + 0.5f * std::sin (juce::MathConstants<float>::twoPi * freq * 3.0f * t));
    }
}

static void testDrummerParts()
{
    std::cout << "Baterista: groove, partes de la canción y seguir a la guitarra\n";
    auto steps16 = [] (const std::vector<Note>& notes, int pitch, double from, double to)
    {
        std::set<int> out;
        for (auto& n : notes) if (n.pitch == pitch && n.beat >= from - 0.05 && n.beat < to - 0.05) out.insert ((int) std::lround ((n.beat - from) * 4.0));
        return out;
    };

    // Same groove bar after bar (a person repeats it), varied at the end of the phrase
    {
        DrumRequest r; r.lengthBeats = 32; r.seed = 11; r.settings.complexity = 0.6f; r.fillAtEnd = false;
        auto n = generateDrumPart (r);
        CHECK (steps16 (n, 36, 0, 4) == steps16 (n, 36, 4, 8) && steps16 (n, 38, 4, 8) == steps16 (n, 38, 8, 12),
               "el bombo y la caja repiten el mismo groove compás a compás");
    }
    // Different seeds give different grooves
    {
        std::set<std::set<int>> kicks;
        for (int seed = 1; seed <= 12; ++seed)
        {
            DrumRequest r; r.lengthBeats = 8; r.seed = seed; r.settings.complexity = 0.7f;
            kicks.insert (steps16 (generateDrumPart (r), 36, 0, 4));
        }
        CHECK (kicks.size() >= 3, "generar de nuevo da grooves distintos (" << kicks.size() << " de 12)");
    }

    // Parts: verse (4 bars), chorus (4), bridge (4), outro (2)
    DrumRequest r; r.lengthBeats = 56; r.seed = 5; r.settings.style = "rock"; r.settings.fills = 0.5f; r.fillAtEnd = false;
    auto kinds = std::vector<SectionKind> { SectionKind::verse, SectionKind::chorus, SectionKind::bridge, SectionKind::outro };
    const int lens[] = { 4, 4, 4, 2 };
    for (int k = 0; k < 4; ++k)
        for (int b = 0; b < lens[k]; ++b)
        {
            DrumBar db; db.kind = kinds[(size_t) k]; db.sectionStart = b == 0; db.sectionEnd = b == lens[k] - 1;
            db.ending = k == 3 && b == lens[k] - 1;
            r.bars.push_back (db);
        }
    auto n = generateDrumPart (r);
    auto avgVel = [&] (double from, double to) { float v = 0; int c = 0; for (auto& x : n) if (x.beat >= from && x.beat < to && (x.pitch == 36 || x.pitch == 38) && x.velocity > 0.55f) { v += x.velocity; ++c; } return c ? v / c : 0.0f; };
    CHECK (avgVel (16, 28) > avgVel (0, 12) + 0.04f, "el estribillo suena más fuerte que la estrofa (golpes de bombo y caja)");
    CHECK (! steps16 (n, 49, 16, 20).empty() && ! steps16 (n, 49, 32, 36).empty(), "entra con un platillo en cada parte nueva");
    CHECK (! steps16 (n, 51, 16, 32).empty(), "en el estribillo pasa al ride");
    auto bridgeSnare = steps16 (n, 38, 36, 40);
    CHECK (bridgeSnare.count (8) == 1 && bridgeSnare.count (4) == 0, "el puente va a medio tiempo (caja en el 3)");
    int tomsBeforeChorus = 0;
    for (auto& x : n) tomsBeforeChorus += x.beat >= 12 && x.beat < 16 && (x.pitch == 43 || x.pitch == 45 || x.pitch == 47 || x.pitch == 50 || x.pitch == 38);
    CHECK (tomsBeforeChorus >= 2, "hay un redoble antes del estribillo");
    bool onlyStart = true; int lastBarHits = 0;
    for (auto& x : n) if (x.beat >= 51.95) { ++lastBarHits; onlyStart &= x.beat < 52.1; }
    CHECK (lastBarHits >= 2 && onlyStart, "el final termina con un golpe en el último compás");

    // Listening to a guitar: 8 bars at 120 bpm, hits on 1, the "a" of 2 and the "&" of 3; bars 5-6 silent
    {
        const double sr = 48000, spb = sr / 2.0;   // 120 bpm
        std::vector<float> g ((size_t) (spb * 32), 0.0f);
        for (int bar = 0; bar < 8; ++bar)
        {
            if (bar == 4 || bar == 5) continue;
            for (double b : { 0.0, 1.75, 2.5 }) addPluck (g, sr, (bar * 4 + b) * 0.5 + 0.008, 0.6f);
            for (double b : { 1.0, 3.0 }) addPluck (g, sr, (bar * 4 + b) * 0.5, 0.12f);
        }
        std::vector<float> steps, energy;
        analyseFollowAudio (g.data(), (int) g.size(), sr, spb, 4.0, steps, energy);
        CHECK (steps.size() >= 128 && energy.size() == 8, "analiza la guitarra por semicorchea y por compás");
        CHECK (steps[7] > 0.6f && steps[10] > 0.6f && steps[5] < 0.3f && steps[13] < 0.3f, "encuentra dónde ataca la guitarra");
        CHECK (energy[4] < 0.05f && energy[0] > 0.8f, "sabe en qué compases la guitarra no toca");

        DrumRequest f; f.lengthBeats = 32; f.seed = 3; f.settings.complexity = 0.3f; f.followSteps = steps; f.followBarEnergy = energy;
        auto fn = generateDrumPart (f);
        int withGuitar = 0;
        for (int bar = 0; bar < 4; ++bar)
        {
            auto k = steps16 (fn, 36, bar * 4, bar * 4 + 4);
            withGuitar += k.count (7) + k.count (10);
        }
        CHECK (withGuitar >= 6, "el bombo marca los golpes de la guitarra (" << withGuitar << " de 8)");
        float quiet = 0, played = 0; int nq = 0, np = 0;
        for (auto& x : fn) { if (x.beat >= 16 && x.beat < 24) { quiet += x.velocity; ++nq; } if (x.beat < 8) { played += x.velocity; ++np; } }
        CHECK (nq == 0 || quiet / nq < played / np, "cuando la guitarra para, la batería baja");

        // Through the engine: an audio track named "Guitarra" is followed automatically
        TestEngine t;
        auto* gt = t.engine.addTrack (TrackKind::Audio);
        gt->name = "Guitarra";
        Clip c; c.midi = false;
        auto buf = std::make_shared<juce::AudioBuffer<float>> (1, (int) g.size());
        buf->copyFrom (0, 0, g.data(), (int) g.size());
        c.audio = buf; c.start = 0; c.length = 32;
        t.engine.addClip (*gt, c);
        t.engine.song.bpm = 120;
        auto* d = t.engine.addTrack (TrackKind::Drummer);
        CHECK (t.engine.drummerFollowTrack (*d) == gt, "el baterista escucha la guitarra grabada solo");
        auto state = t.api.getState();
        bool listed = false;
        for (auto& tv : *state["tracks"].getArray()) if ((int) tv["id"] == d->id) listed = tv["listensTo"].isArray() && (int) tv["listensTo"][0] == gt->id;
        CHECK (listed, "la interfaz muestra a quién escucha");

        // Parts from the API
        const auto json = R"({"sections":[{"beat":0,"length":16,"kind":"verse"},{"beat":16,"length":16,"kind":"chorus"},{"beat":30,"length":8,"kind":"nada"}]})";
        auto res = t.api.call ("setSections", juce::JSON::parse (json));
        CHECK (! res.isVoid() && t.engine.song.sections.size() == 3 && t.engine.song.sections[1].kind == "chorus"
               && std::abs (t.engine.song.sections[1].length - 14.0) < 1e-9 && t.engine.song.sections[2].kind == "verse",
               "se marcan partes (sin superponerse; tipos raros pasan a estrofa)");
        CHECK (t.api.getState()["sections"].size() == 3 && t.api.getState()["sectionKinds"].size() == 7, "la interfaz recibe las partes");
        t.api.call ("undo", {});
        CHECK (t.engine.song.sections.empty(), "deshacer quita las partes");
    }
}

static void testDrummerArrangement()
{
    std::cout << "Baterista: la canción por partes\n";
    // The plan always fills exactly the bars it's given
    for (int bars : { 1, 4, 8, 13, 16, 40, 56, 100 })
    {
        int sum = 0;
        auto plan = Engine::songPlan (bars);
        for (auto& p : plan) sum += p.second;
        CHECK (sum == bars && ! plan.empty(), "el plan de " << bars << " compases suma justo (" << sum << ")");
    }
    auto plan = Engine::songPlan (56);
    CHECK (plan.size() == 8 && plan.front().first == "intro" && plan.back().first == "outro" && plan[2].first == "chorus" && plan[5].first == "bridge",
           "una canción completa: intro, estrofa, estribillo, estrofa, estribillo, puente, estribillo, final");

    TestEngine t;
    auto* d = t.engine.addTrack (TrackKind::Drummer);
    const double bpb = t.engine.song.beatsPerBar();
    double end = 0;
    for (auto& c : d->clips) end = juce::jmax (end, c.end());
    CHECK (std::abs (end - 16 * bpb) < 1e-6, "una pista nueva arma la canción en 16 compases (no más larga)");
    bool contiguous = true;
    for (size_t i = 1; i < d->clips.size(); ++i) contiguous &= std::abs (d->clips[i].start - d->clips[i - 1].end()) < 1e-6;
    CHECK (contiguous, "las partes van una detrás de la otra");
    CHECK (d->clips[2].part == "chorus" && d->clips[2].name.startsWith (juce::String (u8"Estribillo")), "cada región dice qué parte es");

    // the same kind of part plays the same groove
    auto kicks = [] (const Clip& c) { std::vector<int> k; for (auto& n : c.notes) if (n.pitch == 36 && n.beat < 4) k.push_back ((int) std::lround (n.beat * 4)); return k; };
    Clip* ch1 = nullptr; Clip* ch2 = nullptr;
    for (auto& c : d->clips) if (c.part == "chorus") { if (! ch1) ch1 = &c; else if (! ch2) ch2 = &c; }
    CHECK (ch1 && ch2 && kicks (*ch1) == kicks (*ch2), "los estribillos tocan el mismo groove");

    // The ending: one last hit
    auto& last = d->clips.back();
    bool hitOnly = true;
    for (auto& n : last.notes) if (n.beat >= last.length - bpb + 0.1) hitOnly = false;
    CHECK (last.part == "outro" && hitOnly, "el final termina con un golpe");

    // Stretching a part moves the next ones and doesn't rewrite them
    const int id = d->clips[1].id;
    const double nextStart = d->clips[2].start;
    const auto nextNotes = d->clips[2].notes.size();
    const auto lastStart = d->clips.back().start;
    t.api.call ("setClip", juce::JSON::parse ("{\"trackId\":" + juce::String (d->id) + ",\"clipId\":" + juce::String (id) + ",\"length\":" + juce::String (d->clips[1].length + 2 * bpb) + "}"));
    CHECK (std::abs (d->clips[2].start - (nextStart + 2 * bpb)) < 1e-6 && std::abs (d->clips.back().start - (lastStart + 2 * bpb)) < 1e-6,
           "alargar una parte corre las siguientes");
    CHECK (d->clips[2].notes.size() == nextNotes, "las otras partes no cambian");

    // Regenerating one part leaves the others alone
    std::vector<size_t> sizes; std::vector<int> seeds;
    for (auto& c : d->clips) { sizes.push_back (c.notes.size()); seeds.push_back (c.seed); }
    t.api.call ("varyDrummer", juce::JSON::parse ("{\"trackId\":" + juce::String (d->id) + ",\"clipId\":" + juce::String (d->clips[3].id) + "}"));
    bool others = true;
    for (size_t i = 0; i < d->clips.size(); ++i) if (i != 3) others &= d->clips[i].seed == seeds[i] && d->clips[i].notes.size() == sizes[i];
    CHECK (others && d->clips[3].seed != seeds[3], "generar una parte de nuevo no toca las demás");

    // Changing what a part is
    t.api.call ("setClip", juce::JSON::parse ("{\"trackId\":" + juce::String (d->id) + ",\"clipId\":" + juce::String (d->clips[1].id) + ",\"part\":\"bridge\"}"));
    CHECK (d->clips[1].part == "bridge", "se puede cambiar qué parte es una región");

    // Re-arranging keeps the same stretch of time
    double from = 1e9, to = 0;
    for (auto& c : d->clips) { from = juce::jmin (from, c.start); to = juce::jmax (to, c.end()); }
    t.api.call ("arrangeDrummer", juce::JSON::parse ("{\"trackId\":" + juce::String (d->id) + "}"));
    double from2 = 1e9, to2 = 0;
    for (auto& c : d->clips) { from2 = juce::jmin (from2, c.start); to2 = juce::jmax (to2, c.end()); }
    CHECK (std::abs (from - from2) < 1e-6 && std::abs (to - to2) < 1e-6 && d->clips.size() == 8, "armar de nuevo usa el mismo tramo");

    // Saved with the song
    auto v = clipToVar (d->clips[2]);
    Clip back; clipFromVar (back, v);
    CHECK (back.part == d->clips[2].part, "la parte se guarda con la región");

    // The real kit: Galpon writes a program with the toms on the drummer's keys
    auto kitDir = t.root.getChildFile ("kit/Programs");
    kitDir.getChildFile ("keymaps").createDirectory();
    kitDir.getChildFile ("keymaps/keymap_basic.sfz").replaceWithText ("#define $LTOM_CENTER_KEY 41\n#define $LTOM_OFFCENTER_KEY 43\n#define $LTOM_RIMSHOT_KEY 45\n#define $LTOM_CROSSSTICK_KEY 47\n#define $HTOM_CENTER_KEY 48\n#define $HTOM_OFFCENTER_KEY 50\n#define $SNARE_CENTER_KEY 38\n");
    kitDir.getChildFile ("01-basic-kit.sfz").replaceWithText ("#include \"keymaps/keymap_basic.sfz\"\n<region> key=$SNARE_CENTER_KEY sample=x.wav\n");
    auto kit = SoundLibrary::drumKitForDrummer (kitDir.getChildFile ("01-basic-kit.sfz"));
    auto map = kitDir.getChildFile ("keymaps/keymap_galpon.sfz").loadFileAsString();
    CHECK (kit.existsAsFile() && kit.loadFileAsString().contains ("keymap_galpon.sfz") && map.contains ("$LTOM_CENTER_KEY 45") && map.contains ("$HTOM_OFFCENTER_KEY 47")
           && map.contains ("$SNARE_CENTER_KEY 38"), "la batería real queda con los toms donde los toca el baterista");
}

static void testDrummerStyles()
{
    std::cout << "Baterista: estilos por género\n";
    std::set<juce::String> genres;
    for (auto& st : drummerStyles()) genres.insert (st.genre);
    CHECK (drummerStyles().size() >= 30 && genres.size() >= 7 && genres.count ("Metal"), "hay muchos estilos, agrupados por género (" << drummerStyles().size() << " en " << genres.size() << ")");

    auto make = [] (const char* id, float c, float loud = 0.6f)
    {
        DrumRequest r; r.settings.style = id; r.settings.complexity = c; r.settings.loudness = loud; r.lengthBeats = 16; r.seed = 4; r.fillAtEnd = false;
        return generateDrumPart (r);
    };
    auto avg = [] (const std::vector<Note>& n, std::initializer_list<int> pitches)
    {
        float v = 0; int k = 0;
        for (auto& x : n) for (int pch : pitches) if (x.pitch == pch && x.velocity > 0.5f) { v += x.velocity; ++k; }
        return k ? v / k : 0.0f;
    };
    const float rock = avg (make ("rock", 0.4f), { 36, 38 }), ballad = avg (make ("ballad", 0.4f), { 36, 37, 38 }), metal = avg (make ("heavymetal", 0.4f), { 36, 38 });
    CHECK (rock > 0.8f && metal >= rock && ballad < rock - 0.1f, "el rock y el metal pegan fuerte; la balada, suave (" << ballad << " / " << rock << " / " << metal << ")");

    int kicks = 0; for (auto& x : make ("thrash", 0.9f)) kicks += x.pitch == 36 && x.beat < 4;
    CHECK (kicks >= 14, "thrash complejo: doble bombo (" << kicks << " bombos en un compás)");
    int snares = 0; for (auto& x : make ("deathmetal", 0.9f)) snares += x.pitch == 38 && x.beat < 4;
    CHECK (snares >= 7, "death metal complejo: blast beat (" << snares << " cajas en un compás)");
    int fast = 0; auto tr = make ("trap", 0.8f);
    for (size_t i = 1; i < tr.size(); ++i) if (tr[i].pitch == 42 && tr[i - 1].pitch == 42 && tr[i].beat - tr[i - 1].beat < 0.16) ++fast;
    CHECK (fast > 0, "trap: redobles en el charles");
    bool oneDrop = true; for (auto& x : make ("reggae", 0.1f)) if (x.pitch == 36 && x.beat < 4 && std::abs (x.beat - 2.0) > 0.1) oneDrop = false;
    CHECK (oneDrop, "reggae: el bombo cae en el 3 (one drop)");
}

static void testRealDrums()
{
    std::cout << "Baterista con la batería real (muestras)\n";
    // A tiny stand-in for the downloaded kit, in a sounds folder of its own
    auto sounds = tempRoot().getChildFile ("SonidosKit");
    auto programs = sounds.getChildFile ("virtuosity-drums/virtuosity_drums-master/Programs");
    programs.getChildFile ("keymaps").createDirectory();
    programs.getChildFile ("keymaps/keymap_basic.sfz").replaceWithText ("#define $KICK_KEY 36\n#define $SNARE_KEY 38\n#define $HH_KEY 42\n#define $LTOM_CENTER_KEY 41\n");
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (programs.getChildFile ("hit.wav"));
        auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 9600);
        for (int i = 0; i < 9600; ++i) b.setSample (0, i, 0.6f * std::exp (-i / 2000.0f) * std::sin (i * 0.05f));
        w->writeFromAudioSampleBuffer (b, 0, 9600);
    }
    programs.getChildFile ("01-basic-kit.sfz").replaceWithText ("#include \"keymaps/keymap_basic.sfz\"\n<region> key=$KICK_KEY sample=hit.wav\n<region> key=$SNARE_KEY sample=hit.wav\n<region> key=$HH_KEY sample=hit.wav\n");
    const auto before = juce::SystemStats::getEnvironmentVariable ("GALPON_SOUNDS_DIR", {});
    setEnv ("GALPON_SOUNDS_DIR", sounds.getFullPathName());
    {
        TestEngine t;
        t.api.call ("addTrack", juce::JSON::parse (R"({"kind":"drummer"})"));
        auto* d = t.engine.song.tracks[0].get();
        CHECK (d->instrument.name.contains ("real"), "un baterista nuevo usa la batería real si está descargada");
        CHECK (programs.getChildFile ("galpon-kit.sfz").existsAsFile(), "con el kit de Galpon (toms en su lugar)");
        t.api.call ("addTrack", juce::JSON::parse (R"({"kind":"instrument"})"));
        t.api.call ("undo", {});
        t.api.call ("undo", {});
        t.api.call ("redo", {});
        t.engine.setPositionBeats (0);
        t.engine.play();
        auto out = t.render (8.0);   // waits for the samples to load before rendering
        CHECK (finite (out) && rms (out) > 0.001f, "la batería real suena (también después de deshacer y rehacer)");
        t.engine.stop();
    }
    setEnv ("GALPON_SOUNDS_DIR", before);
}

static void pluck (std::vector<float>& out, double sr, double at, int midi, double dur, float amp, juce::uint32 seed);

static void testGuitarTone()
{
    std::cout << "Amplificador, distorsión y parlante: cómo suenan\n";
    const double sr = 48000;
    std::vector<float> di ((size_t) (sr * 2.0), 0.0f);
    for (int m : { 40, 47, 52 }) pluck (di, sr, 0.0, m, 2.0, 0.25f, (juce::uint32) m);   // E5 power chord, a DI-level guitar

    auto run = [&] (const char* type, std::initializer_list<std::pair<const char*, float>> params)
    {
        auto fx = createBuiltin (type);
        auto* b = dynamic_cast<BuiltinProcessor*> (fx.get());
        for (auto& [k, v] : params) b->setReal (k, v);
        fx->prepareToPlay (sr, 256);
        juce::AudioBuffer<float> buf (2, (int) di.size());
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, di.data(), (int) di.size());
        juce::MidiBuffer midi;
        for (int i = 0; i < buf.getNumSamples(); i += 256)
        {
            juce::AudioBuffer<float> blk (buf.getArrayOfWritePointers(), 2, i, juce::jmin (256, buf.getNumSamples() - i));
            fx->processBlock (blk, midi);
        }
        return buf;
    };
    auto level = [] (const juce::AudioBuffer<float>& b) { return juce::Decibels::gainToDecibels (b.getRMSLevel (0, 0, b.getNumSamples())); };
    auto fizz = [&] (const juce::AudioBuffer<float>& b)
    {
        // share of the energy above 7 kHz: what sounds like bees
        juce::dsp::IIR::Filter<float> hp (juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 7000.0, 0.7));
        juce::dsp::IIR::Filter<float> hp2 (juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 7000.0, 0.7));
        double hi = 0, all = 0;
        for (int i = 0; i < b.getNumSamples(); ++i) { const float x = b.getSample (0, i); const float h = hp2.processSample (hp.processSample (x)); hi += h * h; all += x * x; }
        return all > 0 ? hi / all : 0.0;
    };
    auto crest = [] (const juce::AudioBuffer<float>& b)
    {
        float peak = b.getMagnitude (0, 0, b.getNumSamples() / 2);
        float rms = b.getRMSLevel (0, 0, b.getNumSamples() / 2);
        return rms > 0 ? peak / rms : 0.0f;
    };

    auto clean = run ("amp", { { "gain", 0.15f } }), crunch = run ("amp", { { "gain", 0.55f } }), high = run ("amp", { { "gain", 0.9f } });
    CHECK (finite (clean) && finite (high), "el amplificador no da valores raros");
    const float lc = level (clean), lh = level (high), lm = level (crunch);
    CHECK (std::abs (lh - lc) < 8.0f && std::abs (lm - lc) < 8.0f && lc > -30.0f && lh < -1.0f,
           "el volumen se mantiene parejo al subir la ganancia (" << lc << " / " << lm << " / " << lh << " dB)");
    CHECK (crest (high) < crest (clean) * 0.7f, "más ganancia, más saturado (cresta " << crest (clean) << " -> " << crest (high) << ")");
    CHECK (fizz (high) < 0.02 && fizz (crunch) < 0.02, "con parlante, la distorsión no zumba (agudos sobre 7 kHz: " << fizz (high) * 100 << " %)");

    auto drive = run ("drive", { { "drive", 0.8f } });
    CHECK (finite (drive) && level (drive) > -30.0f && level (drive) < 0.0f, "el pedal de distorsión suena a un nivel razonable");

    // a cabinet with no IR still behaves like a speaker
    juce::AudioBuffer<float> noise (2, (int) sr);
    juce::Random r (3);
    for (int i = 0; i < noise.getNumSamples(); ++i) { const float v = r.nextFloat() * 2 - 1; noise.setSample (0, i, v * 0.3f); noise.setSample (1, i, v * 0.3f); }
    const double before = fizz (noise);
    auto cab = createBuiltin ("cab");
    cab->prepareToPlay (sr, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < noise.getNumSamples(); i += 512)
    {
        juce::AudioBuffer<float> blk (noise.getArrayOfWritePointers(), 2, i, juce::jmin (512, noise.getNumSamples() - i));
        cab->processBlock (blk, midi);
    }
    CHECK (fizz (noise) < before * 0.15, "el gabinete sin archivo IR igual corta los agudos como un parlante");
}

// GALPON_RENDER_CHAIN=folder: writes a plucked low E through gate+amp+reverb (for listening/analysis)
static void renderChainForAnalysis()
{
    auto out = juce::SystemStats::getEnvironmentVariable ("GALPON_RENDER_CHAIN", {});
    if (out.isEmpty()) return;
    const double sr = 48000;
    for (auto chainName : { "amp", "gate+amp", "gate+amp+reverb", "gate" })
    {
        std::vector<std::unique_ptr<juce::AudioProcessor>> fx;
        for (auto& t : juce::StringArray::fromTokens (chainName, "+", ""))
        {
            auto p = createBuiltin (t);
            p->prepareToPlay (sr, 128);
            fx.push_back (std::move (p));
        }
        const int n = (int) (sr * 6);
        juce::AudioBuffer<float> buf (2, n);
        juce::Random rnd (3);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double y = 0;
            for (int k = 1; k <= 10; ++k) y += std::exp (-t * (0.5 + 0.3 * k)) / k * std::sin (juce::MathConstants<double>::twoPi * 82.41 * k * t);
            y *= std::exp (-t * 0.4) * 0.12;   // a DI guitar: peaks around -20 dBFS, decaying to the noise
            y += 0.0003 * (rnd.nextFloat() * 2 - 1);   // the interface's hiss (~ -70 dB)
            buf.setSample (0, i, (float) y); buf.setSample (1, i, (float) y);
        }
        juce::MidiBuffer midi;
        for (int i = 0; i < n; i += 128)
        {
            juce::AudioBuffer<float> blk (buf.getArrayOfWritePointers(), 2, i, juce::jmin (128, n - i));
            for (auto& p : fx) p->processBlock (blk, midi);
        }
        juce::File f = juce::File (out).getChildFile (juce::String (chainName).replace ("+", "_") + ".wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        if (auto* os = f.createOutputStream().release())
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os, sr, 1, 24, {}, 0)))
                w->writeFromAudioSampleBuffer (buf, 0, n);
    }
}

static void testGuitarGate()
{
    std::cout << "Puerta de ruido para guitarra\n";
    const double sr = 48000;
    auto gate = createBuiltin ("gate");
    gate->prepareToPlay (sr, 128);
    juce::Random rnd (5);
    // 5 s of an interface with hum and hiss (~ -60 dB), with a low E plucked at 2 s that dies away
    const int n = (int) (sr * 9);
    juce::AudioBuffer<float> buf (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double y = 0.0006 * (rnd.nextFloat() * 2 - 1) + 0.0004 * std::sin (juce::MathConstants<double>::twoPi * 50 * t);
        if (t > 5.0) { const double tt = t - 5.0; for (int k = 1; k <= 6; ++k) y += 0.15 / k * std::exp (-tt * (0.9 + 0.3 * k)) * std::sin (juce::MathConstants<double>::twoPi * 82.41 * k * tt); }
        buf.setSample (0, i, (float) y); buf.setSample (1, i, (float) y);
    }
    juce::AudioBuffer<float> in; in.makeCopyOf (buf);
    juce::MidiBuffer midi;
    for (int i = 0; i < n; i += 128) { juce::AudioBuffer<float> blk (buf.getArrayOfWritePointers(), 2, i, juce::jmin (128, n - i)); gate->processBlock (blk, midi); }
    auto rmsDb = [&] (const juce::AudioBuffer<float>& b, double a, double z) { return juce::Decibels::gainToDecibels (b.getRMSLevel (0, (int) (a * sr), (int) ((z - a) * sr)), -120.0f); };
    CHECK (rmsDb (buf, 4.0, 4.9) < -85.0f, "entre notas silencia el ruido de la entrada (" << rmsDb (buf, 4.0, 4.9) << " dB)");
    CHECK (std::abs (rmsDb (buf, 5.05, 6.0) - rmsDb (in, 5.05, 6.0)) < 0.5f, "la nota pasa entera");
    // while the note dies: no fluttering (open-shut-open), which sounds like crackle
    int flips = 0; bool wasOn = true;
    for (int i = (int) (5.05 * sr); i < n; i += 48)
    {
        const float ratio = std::abs (in.getSample (0, i)) > 1.0e-4f ? std::abs (buf.getSample (0, i) / in.getSample (0, i)) : (wasOn ? 1.0f : 0.0f);
        const bool on = ratio > 0.5f;
        if (on != wasOn) ++flips;
        wasOn = on;
    }
    CHECK (flips <= 1, "al apagarse la nota se cierra una sola vez, sin titilar (" << flips << " cambios)");
}

static void testTunerPitch()
{
    std::cout << "Afinador: detecta cada cuerda\n";
    const double sr = 48000;
    juce::Random rnd (7);
    // a plucked string: strong harmonics (the 2nd often louder than the fundamental), decaying, a bit of noise and hum
    auto pluck = [&] (double f, float amp, double ageSec)
    {
        std::vector<float> v (4096);
        for (int i = 0; i < (int) v.size(); ++i)
        {
            const double t = ageSec + i / sr;
            double y = 0;
            const double hs[] = { 0.6, 1.0, 0.7, 0.45, 0.3, 0.2, 0.12, 0.08 };
            for (int k = 0; k < 8; ++k) y += hs[k] * std::exp (-t * (0.6 + k * 0.4)) * std::sin (juce::MathConstants<double>::twoPi * f * (k + 1) * t * (1.0 + 0.0001 * k * k) + k);   // real strings: a little inharmonic
            y += 0.02 * std::sin (juce::MathConstants<double>::twoPi * 50.0 * t) + 0.01 * (rnd.nextFloat() * 2 - 1);
            v[(size_t) i] = (float) (amp * y / 3.0);
        }
        return v;
    };
    const int strings[] = { 40, 45, 50, 55, 59, 64, 38, 36, 28, 33 };   // a guitar, Drop D / Drop C low strings, a bass
    for (int m : strings)
        for (float amp : { 0.5f, 0.02f })   // loud, and a quiet guitar straight into the interface
            for (double age : { 0.05, 1.2 })
            {
                if (amp < 0.1f && age > 1.0) continue;   // a quiet note a second later is under the noise of any interface
                const double f = 440.0 * std::pow (2.0, (m - 69) / 12.0);
                float level = 0;
                auto v = pluck (f, amp, age);
                const double got = detectPitch (v.data(), (int) v.size(), sr, level);
                const double cents = got > 0 ? 1200.0 * std::log2 (got / f) : 9999.0;
                CHECK (std::abs (cents) < 5.0, "cuerda " << m << " (" << juce::String (f, 1) << " Hz, volumen " << amp << ", " << age << " s después): " << juce::String (got, 2) << " Hz, " << juce::String (cents, 1) << " cents");
            }
    std::vector<float> silence (4096, 0.0f);
    float lv = 0;
    CHECK (detectPitch (silence.data(), 4096, sr, lv) == 0.0, "en silencio no inventa notas");
}

static void testRetuneAndPresets()
{
    std::cout << "Afinación virtual y sonidos listos\n";
    const double sr = 48000;
    auto pitchAfter = [&] (float semis)
    {
        auto fx = createBuiltin ("retune");
        auto* b = dynamic_cast<BuiltinProcessor*> (fx.get());
        b->setReal ("semitones", semis);
        fx->prepareToPlay (sr, 256);
        juce::AudioBuffer<float> buf (2, (int) sr);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float ph = juce::MathConstants<float>::twoPi * 220.0f * i / (float) sr;
            const float v = 0.25f * std::sin (ph) + 0.15f * std::sin (2 * ph + 0.3f) + 0.1f * std::sin (3 * ph + 1.1f) + 0.05f * std::sin (4 * ph);   // a string: harmonics, not a pure tone
            buf.setSample (0, i, v); buf.setSample (1, i, v);
        }
        juce::MidiBuffer midi;
        for (int i = 0; i < buf.getNumSamples(); i += 256)
        {
            juce::AudioBuffer<float> blk (buf.getArrayOfWritePointers(), 2, i, juce::jmin (256, buf.getNumSamples() - i));
            fx->processBlock (blk, midi);
        }
        // the strongest frequency in the second half (a fine scan, so it isn't limited to FFT bins)
        const int from = buf.getNumSamples() / 2, len = buf.getNumSamples() - from;
        double f = 0.0, best = -1.0;
        for (double hz = 80.0; hz < 300.0; hz += 0.25)
        {
            double re = 0, im = 0;
            for (int i = 0; i < len; i += 2)
            {
                const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / len);
                const double ph = juce::MathConstants<double>::twoPi * hz * i / sr;
                re += w * buf.getSample (0, from + i) * std::cos (ph); im += w * buf.getSample (0, from + i) * std::sin (ph);
            }
            if (re * re + im * im > best) { best = re * re + im * im; f = hz; }
        }
        return std::make_pair (f, fx->getLatencySamples());
    };
    auto [down2, lat] = pitchAfter (-2.0f);
    auto [down12, lat12] = pitchAfter (-7.0f);
    auto [down5, lat5] = pitchAfter (-5.0f);
    auto [up3, lat3] = pitchAfter (3.0f);
    juce::ignoreUnused (lat12, lat5, lat3);
    CHECK (std::abs (up3 - 220.0 * std::pow (2.0, 3.0 / 12.0)) < 2.0, "tres semitonos arriba: " << up3 << " Hz");
    CHECK (std::abs (down5 - 220.0 * std::pow (2.0, -5.0 / 12.0)) < 2.0, "cinco semitonos abajo (Drop A): " << down5 << " Hz");
    const double want2 = 220.0 * std::pow (2.0, -2.0 / 12.0);
    CHECK (std::abs (down2 - want2) < 2.0, "dos semitonos abajo: 220 Hz suena a " << down2 << " Hz (debería " << want2 << ")");
    CHECK (std::abs (down12 - 220.0 * std::pow (2.0, -7.0 / 12.0)) < 2.0, "siete semitonos abajo (lo máximo): " << down12 << " Hz");
    CHECK (lat > 0 && lat < (int) (sr * 0.06), "la afinación virtual agrega poca demora (" << lat * 1000.0 / sr << " ms)");

    // presets: all exist as effects, and the closest one makes sense
    for (auto& p : tonePresets())
        for (auto& st : p.chain)
            CHECK (createBuiltin (st.type) != nullptr, p.id << " usa un efecto que existe (" << st.type << ")");
    CHECK (closestTonePreset (0.05f, "", false).id == "clean", "una guitarra limpia elige el limpio");
    CHECK (closestTonePreset (0.95f, "", false).drive > 0.85f, "una guitarra muy distorsionada elige alta ganancia");
    CHECK (closestTonePreset (-1.0f, "acoustic_strum", false).id == "acoustic", "el nombre «acoustic» elige el acústico");
    CHECK (closestTonePreset (-1.0f, "Solo", false).lead, "un solo elige un sonido de solo");
    CHECK (closestTonePreset (0.1f, "", true).bass, "el bajo elige un sonido de bajo");

    TestEngine t;
    auto* a = t.engine.addTrack (TrackKind::Audio);
    auto r = t.api.call ("applyTonePreset", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"id\":\"hardrock\"}"));
    juce::StringArray types;
    for (auto& ins : a->inserts) if (auto* bp = dynamic_cast<BuiltinProcessor*> (ins.processor.get())) types.add (bp->getTypeId());
    CHECK (types.contains ("amp") && types.contains ("gate"), "aplicar «Hard rock» pone la cadena en la pista (" << types.joinIntoString (", ") << ")");
    auto bad = t.api.call ("applyTonePreset", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"id\":\"nada\"}"));
    CHECK (bad.hasProperty ("error"), "un sonido que no existe da error");
    t.api.call ("setRetune", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"semitones\":-2}"));
    auto firstType = [&] { auto* bp = a->inserts.empty() ? nullptr : dynamic_cast<BuiltinProcessor*> (a->inserts[0].processor.get()); return bp ? bp->getTypeId() + ":" + juce::String (bp->get ("semitones")) : juce::String(); };
    CHECK (firstType() == "retune:-2", "la afinación virtual va primera en la cadena (" << firstType() << ")");
    t.api.call ("setRetune", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"semitones\":-4}"));
    int count = 0; for (auto& ins : a->inserts) if (auto* bp = dynamic_cast<BuiltinProcessor*> (ins.processor.get()); bp && bp->getTypeId() == "retune") ++count;
    CHECK (count == 1 && firstType() == "retune:-4", "cambiarla no agrega otra");
    t.api.call ("setRetune", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"semitones\":0}"));
    CHECK (firstType() != "retune:-4" && ! firstType().startsWith ("retune"), "en 0 se saca");
}

static void testRecording()
{
    std::cout << "Grabación de audio y MIDI\n";
    TestEngine t;
    auto* a = t.engine.addTrack (TrackKind::Audio);
    t.api.call ("setTrack", juce::JSON::parse (R"({"trackId":)" + juce::String (a->id) + R"(,"armed":true})"));
    t.engine.song.countIn = false;
    t.engine.setPositionBeats (4.0);
    t.engine.record();
    CHECK (t.engine.isRecording(), "grabando");
    {
        auto tick = t.api.getTick();
        auto rec = tick["rec"];
        CHECK (rec.isObject() && std::abs ((double) rec["start"] - 4.0) < 1e-6
               && rec["tracks"].size() >= 1 && rec["tracks"].getArray()->contains (a->id),
               "la interfaz sabe qué pistas se graban y desde dónde (para dibujar la toma)");
    }

    const int n = 256;
    juce::AudioBuffer<float> in (2, n), out (2, n);
    int total = 0;
    for (int block = 0; block < 400; ++block)
    {
        for (int i = 0; i < n; ++i)
        {
            const float s = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (total + i) / 48000.0f);
            in.setSample (0, i, s); in.setSample (1, i, s);
        }
        t.engine.renderBlock (out.getArrayOfWritePointers(), 2, in.getArrayOfReadPointers(), 2, n);
        total += n;
    }
    t.engine.stop();
    CHECK (a->clips.size() == 1, "quedó una toma grabada");
    if (! a->clips.empty())
    {
        auto& c = a->clips[0];
        CHECK (! c.midi && c.audio != nullptr, "la toma tiene audio");
        CHECK (std::abs (c.start - 4.0) < 0.01, "empieza donde estaba el cursor");
        CHECK (c.audio != nullptr && std::abs (c.audio->getNumSamples() - total) < 512, "duración correcta (" << (c.audio ? c.audio->getNumSamples() : 0) << " vs " << total << ")");
        CHECK (c.audio != nullptr && c.audio->getRMSLevel (0, 0, c.audio->getNumSamples()) > 0.3f, "se grabó la señal");
        CHECK (t.engine.getAudioFolder().getChildFile (c.audioFile).existsAsFile(), "el archivo WAV existe");
    }

    // MIDI recording from the on-screen keyboard
    auto* k = t.engine.addTrack (TrackKind::Instrument, "Piano de cola");
    k->armed = true;
    t.engine.selectedTrackId = k->id;
    t.engine.setPositionBeats (0);
    t.engine.record();
    for (int block = 0; block < 100; ++block)
    {
        if (block == 10) t.engine.liveNote (60, 0.8f, true);
        if (block == 50) t.engine.liveNote (60, 0.0f, false);
        t.engine.renderBlock (out.getArrayOfWritePointers(), 2, nullptr, 0, n);
        juce::Thread::sleep (2);   // live notes are timestamped in real time
    }
    t.engine.stop();
    CHECK (k->clips.size() == 1 && k->clips[0].notes.size() == 1, "se grabó la nota MIDI");
}

static void feedInput (TestEngine& t, int blocks, float amplitude, int& total)
{
    const int n = 256;
    juce::AudioBuffer<float> in (2, n), out (2, n);
    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < n; ++i)
        {
            const float v = amplitude * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (total + i) / 48000.0f);
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        t.engine.renderBlock (out.getArrayOfWritePointers(), 2, in.getArrayOfReadPointers(), 2, n);
        total += n;
    }
}

static void testRecordingSafety()
{
    std::cout << "Grabación segura\n";
    // 1. Input gain can't clip the file (32-bit float WAV)
    {
        TestEngine t;
        auto* a = t.engine.addTrack (TrackKind::Audio);
        a->armed = true;
        a->inputGainDb = 12.0f;
        t.engine.song.countIn = false;
        CHECK (t.engine.record().wasOk(), "empezar a grabar");
        int total = 0;
        feedInput (t, 200, 0.5f, total);
        t.engine.stop();
        CHECK (a->clips.size() == 1 && a->clips[0].audio != nullptr && a->clips[0].audio->getMagnitude (0, 0, a->clips[0].audio->getNumSamples()) > 1.5f,
               "con ganancia alta el archivo guarda la onda entera, sin recortar");
        CHECK (a->armed, "la pista sigue armada para la próxima toma");
        // 2. Saved right away
        auto project = juce::JSON::parse (t.engine.songFolder.getChildFile ("project.json"));
        CHECK (juce::JSON::toString (project).contains (a->clips[0].audioFile), "la canción se guarda apenas termina la toma");
    }
    // 3. The file on disk is valid while recording (if the computer crashed now, it's there)
    {
        TestEngine t;
        auto* a = t.engine.addTrack (TrackKind::Audio);
        a->armed = true;
        t.engine.song.countIn = false;
        t.engine.record();
        int total = 0;
        const auto start = juce::Time::getMillisecondCounter();
        while (juce::Time::getMillisecondCounter() - start < 2600) { feedInput (t, 19, 0.3f, total); juce::Thread::sleep (100); }   // ~real time
        auto files = t.engine.getAudioFolder().findChildFiles (juce::File::findFiles, false, "*toma*.wav");
        CHECK (files.size() == 1, "el archivo de la toma existe mientras se graba");
        if (files.size() == 1)
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (files[0]));
            CHECK (r != nullptr && r->lengthInSamples > 48000, "y ya se puede leer: " << (r ? r->lengthInSamples : 0) << " muestras a salvo");
        }
        // 4. The interface disappears mid-take: recording stops, everything so far is kept
        static_cast<juce::AudioIODeviceCallback&> (t.engine).audioDeviceStopped();
        feedInput (t, 20, 0.3f, total);          // nothing more should be written
        auto msg = t.engine.checkRecordingHealth();
        CHECK (msg.contains ("interfaz") && ! t.engine.isRecording(), "si se corta la interfaz, para y avisa");
        CHECK (a->clips.size() == 1 && a->clips[0].audio != nullptr && a->clips[0].audio->getNumSamples() > 48000 * 2,
               "y la toma queda en la pista (" << (a->clips.empty() || ! a->clips[0].audio ? 0 : a->clips[0].audio->getNumSamples()) << ")");
    }
    // 5. The track is deleted while recording: the take goes to a new track, not to the bin
    {
        TestEngine t;
        auto* a = t.engine.addTrack (TrackKind::Audio);
        a->armed = true;
        t.engine.song.countIn = false;
        t.engine.record();
        int total = 0;
        feedInput (t, 100, 0.3f, total);
        auto refused = t.api.call ("deleteTrack", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + "}"));
        CHECK (! (bool) refused["ok"], "no deja borrar la pista que está grabando");
        CHECK (! (bool) t.api.call ("undo", {})["ok"] && ! (bool) t.api.call ("newSong", {})["ok"], "ni deshacer ni cambiar de canción en medio de una toma");
        t.engine.deleteTrack (a->id);       // and even if it happened anyway…
        t.engine.stop();
        bool found = false;
        for (auto& tr : t.engine.song.tracks) found |= ! tr->clips.empty() && ! tr->clips[0].midi;
        CHECK (found, "borrar la pista mientras graba no borra la toma");
    }
    // 6. A recording that isn't in the song (crash before saving, or undone) can be recovered
    {
        TestEngine t;
        auto* a = t.engine.addTrack (TrackKind::Audio);
        a->armed = true;
        t.engine.song.countIn = false;
        t.engine.record();
        int total = 0;
        feedInput (t, 200, 0.3f, total);
        t.engine.stop();
        const auto file = a->clips[0].audioFile;
        // simulate the crash: the song as it was before the take
        t.api.call ("undo", {});
        t.engine.saveSong();
        t.engine.clearHistory();
        auto lost = t.engine.findLostRecordings();
        CHECK (lost.contains (file), "encuentra la grabación perdida");
        // a cut-off file (header never updated) is repaired
        {
            juce::FileOutputStream fos (t.engine.getAudioFolder().getChildFile (file));
            fos.setPosition (40); fos.writeInt (0);     // data chunk size = 0, like a crash at the start
        }
        t.engine.clearAudioCache();
        const int n = t.engine.recoverLostRecordings (lost);
        auto* rec = t.engine.song.tracks.back().get();
        CHECK (n == 1 && rec->clips.size() == 1 && rec->clips[0].audio != nullptr && std::abs (rec->clips[0].audio->getNumSamples() - total) < 600,
               "la recupera entera aunque el archivo haya quedado cortado (" << (rec->clips.empty() || ! rec->clips[0].audio ? 0 : rec->clips[0].audio->getNumSamples()) << " de " << total << ")");
        CHECK (t.engine.findLostRecordings().isEmpty(), "ya no figura como perdida");
        t.engine.ignoredRecordings.add ("x toma.wav");
        CHECK (t.engine.saveSong().wasOk() && t.engine.loadSong (t.engine.songFolder).wasOk() && t.engine.ignoredRecordings.contains ("x toma.wav"), "recuerda las que no quisiste recuperar");
    }
    // 7. A damaged song file opens from the copy of the previous save
    {
        TestEngine t;
        t.engine.addTrack (TrackKind::Audio);
        t.engine.saveSong();
        t.engine.addTrack (TrackKind::Drummer);
        t.engine.saveSong();
        t.engine.songFolder.getChildFile ("project.json").replaceWithText ("{ roto");
        auto r = t.engine.loadSong (t.engine.songFolder);
        CHECK (r.wasOk() && t.engine.song.tracks.size() == 1, "un archivo de canción dañado se abre desde la copia anterior");
    }
    // 8. The buffer between audio and disk never blocks: if it overflows, it's counted
    {
        juce::TimeSliceThread stalled ("stalled");      // never started: a disk that doesn't answer
        auto f = tempRoot().getChildFile ("lento.wav");
        TakeWriter w (stalled, f, 48000.0, 1);
        juce::AudioBuffer<float> b (1, 48000);
        b.clear();
        const float* ptr[1] = { b.getReadPointer (0) };
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 25; ++i) w.write (ptr, 48000);           // 25 s with nothing reaching the disk
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        CHECK (w.droppedSamples() == 5 * 48000 && ms < 200, "con el disco trabado: aguanta 20 s y cuenta lo perdido sin frenar el audio");
        w.finish();
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        CHECK (r != nullptr && r->lengthInSamples == 20 * 48000, "y al terminar escribe los 20 s que tenía guardados");
    }
    // 9. An input that doesn't exist on the interface is caught before recording, not after
    {
        TestEngine t;
        auto* a = t.engine.addTrack (TrackKind::Audio);
        a->armed = true;
        auto r = t.api.call ("record", {});
        CHECK ((bool) r["ok"], "sin interfaz abierta (pruebas) se puede grabar");
        t.engine.stop();
    }
}

static void testSaveLoadUndoExport()
{
    std::cout << "Guardar, abrir, deshacer y exportar\n";
    TestEngine t;
    auto* piano = t.engine.addTrack (TrackKind::Instrument, juce::String (u8"Piano eléctrico"));
    t.engine.addLoop ("keys-pop", 0, piano->id);
    auto* d = t.engine.addTrack (TrackKind::Drummer);
    t.api.call ("addInsert", juce::JSON::parse (R"({"trackId":)" + juce::String (piano->id) + R"(,"kind":"builtin","id":"reverb"})"));
    t.api.call ("setParam", juce::JSON::parse (R"({"trackId":)" + juce::String (piano->id) + R"(,"slot":0,"index":0,"value":0.9})"));
    t.api.call ("setSong", juce::JSON::parse (R"({"bpm":97,"key":"D","minor":true})"));
    CHECK (piano->inserts.size() == 1, "se agregó la reverb");

    auto saved = t.engine.saveSong (t.root.getChildFile ("Guardada.galpon"));
    CHECK (saved.wasOk(), "guardar: " << saved.getErrorMessage());

    TestEngine u;
    auto loaded = u.engine.loadSong (t.root.getChildFile ("Guardada.galpon"));
    CHECK (loaded.wasOk(), "abrir: " << loaded.getErrorMessage());
    CHECK (u.engine.song.tracks.size() == 2, "se recuperaron las pistas");
    CHECK (std::abs (u.engine.song.bpm - 97.0) < 1e-6 && u.engine.song.key == "D" && u.engine.song.minor, "tempo y tono");
    if (u.engine.song.tracks.size() == 2)
    {
        auto& p = *u.engine.song.tracks[0];
        CHECK (p.inserts.size() == 1 && p.inserts[0].processor != nullptr, "efecto recuperado");
        if (! p.inserts.empty() && p.inserts[0].processor)
            CHECK (std::abs (p.inserts[0].processor->getParameters()[0]->getValue() - 0.9f) < 0.01f, "valor del efecto recuperado");
        CHECK (p.clips.size() == 1 && p.clips[0].notes.size() == piano->clips[0].notes.size(), "notas recuperadas");
        CHECK (u.engine.song.tracks[1]->kind == TrackKind::Drummer, "pista de baterista recuperada");
    }

    // Undo / redo a clip move
    const int clipId = piano->clips[0].id;
    t.api.call ("setClip", juce::JSON::parse (R"({"trackId":)" + juce::String (piano->id) + R"(,"clipId":)" + juce::String (clipId) + R"(,"start":8})"));
    CHECK (std::abs (piano->clips[0].start - 8) < 1e-9, "mover región");
    t.api.call ("undo", {});
    CHECK (std::abs (piano->clips[0].start) < 1e-9, "deshacer vuelve la región a su lugar");
    t.api.call ("redo", {});
    CHECK (std::abs (piano->clips[0].start - 8) < 1e-9, "rehacer");

    // Split
    t.api.call ("splitClip", juce::JSON::parse (R"({"trackId":)" + juce::String (piano->id) + R"(,"clipId":)" + juce::String (clipId) + R"(,"beat":12})"));
    CHECK (piano->clips.size() == 2, "dividir una región");

    // Export
    auto wav = t.root.getChildFile ("mezcla.wav");
    auto r = t.engine.exportMix (wav, 24);
    CHECK (r.wasOk(), "exportar: " << r.getErrorMessage());
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
    CHECK (reader != nullptr, "el WAV exportado se puede leer");
    if (reader)
    {
        const double expected = t.engine.beatsToSamples (t.engine.song.lengthInBeats()) + 2.0 * 48000.0;
        CHECK (std::abs ((double) reader->lengthInSamples - expected) < 600, "duración del export");
        juce::AudioBuffer<float> b (2, (int) reader->lengthInSamples);
        reader->read (&b, 0, b.getNumSamples(), 0, true, true);
        CHECK (rms (b) > 0.005f && finite (b), "el export tiene sonido");
    }
    juce::ignoreUnused (d);
}

static void testImports()
{
    std::cout << "Importar audio y MIDI, loops\n";
    TestEngine t;

    // Make a 44.1 kHz WAV to check sample-rate conversion on import
    auto wav = t.root.getChildFile ("guitarra.wav");
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (wav);
        auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (16));
        juce::AudioBuffer<float> b (1, 44100);
        for (int i = 0; i < 44100; ++i) b.setSample (0, i, 0.4f * std::sin (i * 0.06f));
        w->writeFromAudioSampleBuffer (b, 0, 44100);
    }
    auto r = t.engine.importAudio (0, wav, 2.0);
    CHECK (r.wasOk(), "importar audio: " << r.getErrorMessage());
    auto* a = t.engine.song.tracks.empty() ? nullptr : t.engine.song.tracks.back().get();
    CHECK (a != nullptr && a->clips.size() == 1 && a->clips[0].audio != nullptr, "región de audio creada");
    if (a && ! a->clips.empty() && a->clips[0].audio)
        CHECK (std::abs (a->clips[0].audio->getNumSamples() - 48000) < 10, "convertido a 48 kHz (" << a->clips[0].audio->getNumSamples() << ")");

    // MIDI file
    auto mid = t.root.getChildFile ("tema.mid");
    {
        juce::MidiMessageSequence seq;
        for (int i = 0; i < 8; ++i)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, 60 + i, 0.8f), i * 480.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, 60 + i), i * 480.0 + 240.0);
        }
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (480);
        mf.addTrack (seq);
        juce::FileOutputStream os (mid);
        mf.writeTo (os);
    }
    r = t.engine.importMidiFile (mid);
    CHECK (r.wasOk(), "importar MIDI: " << r.getErrorMessage());
    auto* m = t.engine.song.tracks.back().get();
    CHECK (m->clips.size() == 1 && m->clips[0].notes.size() == 8, "8 notas importadas");
    if (! m->clips.empty() && m->clips[0].notes.size() == 8)
        CHECK (std::abs (m->clips[0].notes[3].beat - 3.0) < 1e-6 && std::abs (m->clips[0].notes[3].length - 0.5) < 1e-6, "tiempos correctos");

    for (auto& loop : loopLibrary())
    {
        auto notes = makeLoopNotes (loop, keyRootFromName ("E"), true, 4.0);
        CHECK (! notes.empty(), "loop " << loop.id << " tiene notas");
    }
}

static void testTuner()
{
    std::cout << "Afinador\n";
    TestEngine t;
    auto* a = t.engine.addTrack (TrackKind::Audio);
    t.engine.selectedTrackId = a->id;
    t.engine.tunerEnabled = true;
    const int n = 256;
    juce::AudioBuffer<float> in (1, n), out (2, n);
    for (int block = 0, total = 0; block < 40; ++block, total += n)
    {
        for (int i = 0; i < n; ++i)
            in.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (total + i) / 48000.0f));
        t.engine.renderBlock (out.getArrayOfWritePointers(), 2, in.getArrayOfReadPointers(), 1, n);
    }
    auto reading = t.engine.getTunerReading();
    CHECK ((bool) reading["active"], "detecta una nota");
    CHECK (reading["note"].toString() == "La" && (int) reading["octave"] == 2, "110 Hz es La2 (dio " << reading["note"].toString() << (int) reading["octave"] << ")");
    CHECK (std::abs ((double) reading["cents"]) < 5.0, "afinado (" << (double) reading["cents"] << " cents)");
}

static void testNam()
{
   #if GALPON_WITH_NAM && defined (GALPON_NAM_EXAMPLE)
    std::cout << "Amp NAM (capturas tipo Tone3000)\n";
    auto p = createBuiltin ("nam");
    auto* b = dynamic_cast<BuiltinProcessor*> (p.get());
    p->setRateAndBufferSizeDetails (48000.0, 256);
    p->prepareToPlay (48000.0, 256);
    auto r = b->setExtra ("file", juce::String (GALPON_NAM_EXAMPLE));
    CHECK (r.wasOk(), "cargar modelo: " << r.getErrorMessage());

    // wait for the background loader
    for (int i = 0; i < 400; ++i)
    {
        if (! b->getExtraInfo()["status"].toString().startsWith ("Cargando")) break;
        juce::Thread::sleep (10);
    }
    CHECK (! b->getExtraInfo()["status"].toString().startsWith ("Error"), "estado: " << b->getExtraInfo()["status"].toString());

    juce::AudioBuffer<float> buf (2, 256), dry (2, 256);
    juce::MidiBuffer midi;
    float diff = 0;
    bool ok = true;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const float s = 0.2f * std::sin ((block * 256 + i) * 0.03f);
            buf.setSample (0, i, s); buf.setSample (1, i, s); dry.setSample (0, i, s);
        }
        p->processBlock (buf, midi);
        ok &= finite (buf);
        if (block > 50)
            for (int i = 0; i < 256; ++i) diff += std::abs (buf.getSample (0, i) - dry.getSample (0, i));
    }
    CHECK (ok, "salida válida");
    CHECK (diff > 1.0f, "el modelo cambia el sonido (dif " << diff << ")");

    // Interface at 44.1 kHz, model trained at 48 kHz: it must be converted, without clicks
    auto runAt = [] (double sr, std::vector<float>& outv, juce::String& status, int& latency)
    {
        auto q = createBuiltin ("nam");
        auto* qb = dynamic_cast<BuiltinProcessor*> (q.get());
        q->setRateAndBufferSizeDetails (sr, 256);
        q->prepareToPlay (sr, 256);
        qb->setReal ("gate", -100.0f);
        qb->setExtra ("file", juce::String (GALPON_NAM_EXAMPLE));
        for (int i = 0; i < 400 && qb->getExtraInfo()["status"].toString().startsWith ("Cargando"); ++i)
            juce::Thread::sleep (10);
        juce::MidiBuffer m;
        const int sizes[] = { 256, 100, 37, 256, 191, 1 };    // hosts don't always send full blocks
        juce::int64 pos = 0;
        for (int blk = 0; pos < (juce::int64) (sr * 2); ++blk)
        {
            const int len = sizes[blk % 6];
            juce::AudioBuffer<float> bb (2, len);
            for (int i = 0; i < len; ++i)
            {
                const float s = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (pos + i) / sr);
                bb.setSample (0, i, s); bb.setSample (1, i, s);
            }
            q->processBlock (bb, m);
            for (int i = 0; i < len; ++i) outv.push_back (bb.getSample (0, i));
            pos += len;
        }
        status = qb->getExtraInfo()["status"].toString();
        latency = q->getLatencySamples();
    };
    std::vector<float> o48, o44;
    juce::String st48, st44;
    int lat48 = 0, lat44 = 0;
    runAt (48000.0, o48, st48, lat48);
    runAt (44100.0, o44, st44, lat44);
    auto rmsOf = [] (const std::vector<float>& v, size_t from)
    {
        double a = 0; for (size_t i = from; i < v.size(); ++i) a += v[i] * v[i];
        return std::sqrt (a / (double) (v.size() - from));
    };
    auto worstJump = [] (const std::vector<float>& v, size_t from)
    {
        double w = 0; for (size_t i = from + 2; i < v.size(); ++i) w = std::max (w, (double) std::abs (v[i] - 2 * v[i - 1] + v[i - 2]));
        return w;
    };
    CHECK (st44.contains ("convirtiendo") && ! st48.contains ("convirtiendo"), "convierte solo cuando hace falta (" << st44 << ")");
    CHECK (lat48 == 0 && lat44 > 0 && lat44 < 600, "informa la demora de la conversión (" << lat44 << ")");
    const double r48 = rmsOf (o48, 20000), r44 = rmsOf (o44, 20000);
    CHECK (r44 > 0.0 && std::abs (juce::Decibels::gainToDecibels (r44 / r48)) < 1.5,
           "suena igual de fuerte a 44,1 y a 48 kHz (" << r44 << " vs " << r48 << ")");
    CHECK (worstJump (o44, 20000) < worstJump (o48, 20000) * 2.0 + 0.01,
           "sin chasquidos al convertir (" << worstJump (o44, 20000) << " vs " << worstJump (o48, 20000) << ")");
   #endif
}

//==============================================================================
// 0.2
//==============================================================================
static void feed (TestEngine& t, int blocks, float freq, float amp, int& total, int n = 256, bool realTimeish = false)
{
    juce::AudioBuffer<float> in (2, n), out (2, n);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < n; ++i)
        {
            const float s = amp * std::sin (juce::MathConstants<float>::twoPi * freq * (float) (total + i) / 48000.0f);
            in.setSample (0, i, s); in.setSample (1, i, s);
        }
        t.engine.renderBlock (out.getArrayOfWritePointers(), 2, in.getArrayOfReadPointers(), 2, n);
        total += n;
        if (realTimeish && b % 8 == 0) juce::Thread::sleep (1);   // let the disk writer keep up
    }
}

static void testInputGain()
{
    std::cout << "Ganancia y medidor de entrada\n";
    TestEngine t;
    auto* a = t.engine.addTrack (TrackKind::Audio);
    t.api.call ("setTrack", juce::JSON::parse (R"({"trackId":)" + juce::String (a->id) + R"(,"inputGainDb":12,"armed":true})"));
    CHECK (std::abs (a->inputGainDb - 12.0f) < 0.01f, "se guarda la ganancia");
    t.engine.song.countIn = false;
    int total = 0;
    feed (t, 4, 220.0f, 0.05f, total);
    const float meter = a->inputPeak.load();
    CHECK (meter > 0.15f && meter < 0.25f, "el medidor muestra la entrada con ganancia (" << meter << ")");

    t.engine.record();
    feed (t, 200, 220.0f, 0.05f, total);
    t.engine.stop();
    if (! a->clips.empty() && a->clips[0].audio)
    {
        const float peak = a->clips[0].audio->getMagnitude (0, 0, a->clips[0].audio->getNumSamples());
        CHECK (peak > 0.17f && peak < 0.22f, "la toma se grabó con +12 dB (pico " << peak << ")");
    }
    else CHECK (false, "hay toma");
}

static void testMidiControllers()
{
    std::cout << "MIDI: pedal, pitch bend y perillas\n";
    TestEngine t;
    auto* k = t.engine.addTrack (TrackKind::Instrument, "Piano de cola");
    t.engine.selectedTrackId = k->id;
    t.engine.song.countIn = false;
    t.engine.record();
    juce::AudioBuffer<float> out (2, 256);
    for (int b = 0; b < 120; ++b)
    {
        if (b == 5)  t.engine.liveNote (60, 0.8f, true);
        if (b == 10) t.engine.liveMessage (juce::MidiMessage::controllerEvent (1, 64, 127));
        if (b == 20) t.engine.liveNote (60, 0.0f, false);
        if (b == 30) t.engine.liveMessage (juce::MidiMessage::pitchWheel (1, 12000));
        if (b == 40) t.engine.liveMessage (juce::MidiMessage::controllerEvent (1, 64, 0));
        t.engine.renderBlock (out.getArrayOfWritePointers(), 2, nullptr, 0, 256);
        juce::Thread::sleep (2);
    }
    t.engine.stop();
    CHECK (k->clips.size() == 1, "una región grabada");
    if (k->clips.size() == 1)
    {
        auto& c = k->clips[0];
        int sustain = 0, bends = 0;
        for (auto& e : c.events) { sustain += e.kind == MidiEvent::Controller && e.number == 64; bends += e.kind == MidiEvent::PitchBend; }
        CHECK (sustain == 2, "pedal abajo y arriba grabados (" << sustain << ")");
        CHECK (bends == 1, "pitch bend grabado");

        // Played back: the instrument receives them
        juce::MidiBuffer collected;
        t.engine.setPositionBeats (c.start);
        t.engine.play();
        auto saved = t.engine.saveSong (t.root.getChildFile ("Midi.galpon"));
        CHECK (saved.wasOk(), "guardar con controladores");
        TestEngine u;
        u.engine.loadSong (t.root.getChildFile ("Midi.galpon"));
        CHECK (! u.engine.song.tracks.empty() && u.engine.song.tracks[0]->clips[0].events.size() == c.events.size(), "los controladores se guardan y se abren");
        t.engine.stop();
    }
}

static void testCycleTakes()
{
    std::cout << "Grabar en ciclo: tomas\n";
    TestEngine t;
    auto* a = t.engine.addTrack (TrackKind::Audio);
    a->armed = true;
    t.engine.song.countIn = false;
    t.engine.song.cycle = true;
    t.engine.song.cycleStart = 4.0;
    t.engine.song.cycleEnd = 8.0;            // 2 s at 120 bpm
    t.engine.record();
    int total = 0;
    feed (t, (int) (48000 * 2 * 3.2 / 256), 330.0f, 0.3f, total, 256, true);   // a bit more than 3 passes
    t.engine.stop();
    CHECK (a->clips.size() == 1, "una región");
    if (a->clips.size() == 1)
    {
        auto& c = a->clips[0];
        CHECK (c.takes.size() == 3, "3 tomas (" << c.takes.size() << ")");
        CHECK (std::abs (c.start - 4.0) < 1e-6 && std::abs (c.length - 4.0) < 1e-6, "la región cubre el ciclo");
        CHECK (c.activeTake == 2, "elige la última pasada completa");
        auto r = t.engine.setActiveTake (*a, c, 0);
        CHECK (r.wasOk() && c.offsetSamples == c.takes[0].offsetSamples, "cambiar de toma");
        // comping: split the region in half and use another take for the second half
        const int clipId = c.id;
        t.api.call ("splitClip", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"clipId\":" + juce::String (clipId) + ",\"beat\":6}"));
        CHECK (a->clips.size() == 2, "dividir la región de tomas");
        if (a->clips.size() == 2)
        {
            auto& right = a->clips[0].start > 5.0 ? a->clips[0] : a->clips[1];
            const auto half = (juce::int64) t.engine.beatsToSamples (2.0);
            r = t.engine.setActiveTake (*a, right, 1);
            CHECK (r.wasOk() && right.offsetSamples == right.takes[1].offsetSamples + half,
                   "la segunda mitad toca la segunda mitad de la otra toma (" << right.offsetSamples << " vs " << right.takes[1].offsetSamples + half << ")");
            auto& left = a->clips[0].start < 5.0 ? a->clips[0] : a->clips[1];
            r = t.engine.setActiveTake (*a, left, 2);
            CHECK (r.wasOk() && left.offsetSamples == left.takes[2].offsetSamples, "y la primera mitad, la primera mitad");
        }
    }
}

/** A fake plugin that delays audio by 100 samples and says so, like a "lookahead" plugin. */
struct LatencyProcessor : juce::AudioProcessor
{
    LatencyProcessor() : AudioProcessor (BusesProperties().withInput ("in", juce::AudioChannelSet::stereo()).withOutput ("out", juce::AudioChannelSet::stereo()))
    { setLatencySamples (100); }
    const juce::String getName() const override { return "Demora"; }
    void prepareToPlay (double, int) override { line.setSize (2, 100); line.clear(); pos = 0; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < 2; ++ch) { const float x = b.getSample (ch, i); b.setSample (ch, i, line.getSample (ch, pos)); line.setSample (ch, pos, x); }
            pos = (pos + 1) % 100;
        }
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioBuffer<float> line;
    int pos = 0;
};

static void testDelayCompensationAndLimiter()
{
    std::cout << "Compensación de demora y limitador\n";
    TestEngine t;
    // the same click on two tracks, one of them through a slow plugin
    auto clickFile = t.engine.getAudioFolder().getChildFile ("click.wav");
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (clickFile);
        auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 4800);
        b.clear();
        b.setSample (0, 1000, 0.4f);
        w->writeFromAudioSampleBuffer (b, 0, 4800);
    }
    for (int i = 0; i < 2; ++i)
    {
        auto* a = t.engine.addTrack (TrackKind::Audio);
        Clip c;
        c.midi = false; c.audioFile = "click.wav"; c.audio = t.engine.loadAudio ("click.wav");
        c.start = 0; c.length = t.engine.samplesToBeats (4800);
        t.engine.addClip (*a, c);
        if (i == 1)
        {
            Slot s;
            s.kind = "builtin"; s.type = "test"; s.name = "Demora";
            auto p = std::make_unique<LatencyProcessor>();
            p->prepareToPlay (48000, 256);
            s.processor = adoptProcessor (std::move (p));
            a->inserts.push_back (std::move (s));
        }
    }
    t.engine.song.limiter = false;
    t.engine.setPositionBeats (0);
    t.engine.play();
    juce::AudioBuffer<float> out;
    t.engine.renderOffline (out, 4800);
    // aligned: one click of 0.8 (0.4 + 0.4, both at the same sample); not aligned: two clicks of 0.4
    CHECK (out.getMagnitude (0, 0, 4800) > 0.75f, "las dos pistas quedan alineadas (pico " << out.getMagnitude (0, 0, 4800) << ")");
    t.engine.stop();

    // limiter: a very loud mix stays under 0 dB
    TestEngine u;
    for (int i = 0; i < 4; ++i)
    {
        auto* d = u.engine.addTrack (TrackKind::Drummer);
        d->volumeDb = 12.0f;
    }
    u.engine.song.masterDb = 6.0f;
    u.engine.setPositionBeats (0);
    u.engine.play();
    juce::AudioBuffer<float> loud;
    u.engine.renderOffline (loud, 48000 * 4);
    CHECK (loud.getMagnitude (0, 0, loud.getNumSamples()) <= 0.95f, "el limitador evita la saturación (pico " << loud.getMagnitude (0, 0, loud.getNumSamples()) << ")");
}

static void testUndoTracks()
{
    std::cout << "Deshacer pistas\n";
    TestEngine t;
    t.api.call ("addTrack", juce::JSON::parse (R"({"kind":"drummer"})"));
    t.api.call ("addTrack", juce::JSON::parse (R"({"kind":"instrument","preset":"Órgano"})"));
    const int organ = t.engine.song.tracks[1]->id;
    t.api.call ("deleteTrack", juce::JSON::parse (R"({"id":)" + juce::String (organ) + "}"));
    CHECK (t.engine.song.tracks.size() == 1, "borrar");
    t.api.call ("undo", {});
    CHECK (t.engine.song.tracks.size() == 2 && t.engine.song.tracks[1]->id == organ && t.engine.song.tracks[1]->instrument.processor != nullptr, "deshacer trae la pista con su sonido");
    t.api.call ("undo", {});
    CHECK (t.engine.song.tracks.size() == 1, "deshacer el agregado de una pista");
    t.api.call ("redo", {});
    CHECK (t.engine.song.tracks.size() == 2, "rehacer");
    t.engine.setPositionBeats (0);
    t.engine.play();
    auto out = t.render (4.0);
    CHECK (finite (out) && rms (out) > 0.001f, "sigue sonando después de deshacer");
    t.engine.stop();
}

static void testExportFormats()
{
    std::cout << "Exportar MP3\n";
    TestEngine t;
    t.engine.addTrack (TrackKind::Drummer);
    auto mp3 = t.root.getChildFile ("tema.mp3");
    auto r = t.engine.exportMix (mp3);
    CHECK (r.wasOk(), "exportar mp3: " << r.getErrorMessage());
    juce::MemoryBlock mb;
    mp3.loadFileAsData (mb);
    const auto* b = static_cast<const unsigned char*> (mb.getData());
    CHECK (mb.getSize() > 50000 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0, "es un MP3 válido (" << (int) mb.getSize() << " bytes)");
}

static void writeClick (const juce::File& f, int at, float value, int total = 4800)
{
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
    juce::AudioBuffer<float> b (1, total);
    b.clear();
    b.setSample (0, at, value);
    w->writeFromAudioSampleBuffer (b, 0, total);
}

static void testSendsAndBuses()
{
    std::cout << "Envíos y buses\n";
    TestEngine t;
    t.engine.song.limiter = false;
    writeClick (t.engine.getAudioFolder().getChildFile ("click.wav"), 1000, 0.4f);
    auto* a = t.engine.addTrack (TrackKind::Audio);
    Clip c;
    c.midi = false; c.audioFile = "click.wav"; c.audio = t.engine.loadAudio ("click.wav");
    c.start = 0; c.length = t.engine.samplesToBeats (4800);
    t.engine.addClip (*a, c);
    const int aid = a->id;
    const juce::String tid (aid);

    auto peak = [&]
    {
        t.engine.setPositionBeats (0);
        t.engine.play();
        juce::AudioBuffer<float> out;
        t.engine.renderOffline (out, 4800);
        t.engine.stop();
        return out.getMagnitude (0, 0, 4800);
    };
    auto call = [&] (const char* cmd, const juce::String& json) { return t.api.call (cmd, juce::JSON::parse (json)); };

    CHECK (std::abs (peak() - 0.4f) < 0.01f, "sin envíos suena la pista sola");
    auto r = call ("addTrack", R"({"kind":"bus","preset":"none"})");
    const int busId = (int) r["data"];
    auto* bus = t.engine.song.findTrack (busId);
    CHECK (bus != nullptr && bus->kind == TrackKind::Bus && bus->inserts.empty(), "crear un bus vacío");
    const juce::String bid (busId);

    call ("setSend", "{\"trackId\":" + tid + ",\"busId\":" + bid + ",\"levelDb\":0}");
    CHECK (a->sends.size() == 1, "agregar envío");
    CHECK (std::abs (peak() - 0.8f) < 0.01f, "el envío suma el bus a la pista (pico " << peak() << ")");

    call ("setTrack", "{\"trackId\":" + tid + ",\"volumeDb\":-80}");
    CHECK (peak() < 0.001f, "envío post-fader: bajar el volumen apaga el envío");
    call ("setSend", "{\"trackId\":" + tid + ",\"busId\":" + bid + ",\"pre\":true}");
    CHECK (std::abs (peak() - 0.4f) < 0.01f, "envío pre-fader: sigue mandando con el volumen abajo");

    call ("setTrack", "{\"trackId\":" + tid + ",\"mute\":true}");
    CHECK (peak() < 0.001f, "silenciar la pista también silencia sus envíos");
    call ("setTrack", "{\"trackId\":" + tid + ",\"mute\":false,\"volumeDb\":0}");
    call ("setSend", "{\"trackId\":" + tid + ",\"busId\":" + bid + ",\"remove\":true}");
    CHECK (a->sends.empty(), "quitar envío");

    call ("setTrack", "{\"trackId\":" + tid + ",\"output\":" + bid + "}");
    CHECK (a->output == busId && std::abs (peak() - 0.4f) < 0.01f, "salida de la pista a un bus");
    call ("setTrack", "{\"trackId\":" + bid + ",\"volumeDb\":-6.0206}");
    CHECK (std::abs (peak() - 0.2f) < 0.01f, "el volumen del bus afecta lo que le llega (" << peak() << ")");
    call ("setTrack", "{\"trackId\":" + bid + ",\"volumeDb\":0,\"mute\":true}");
    CHECK (peak() < 0.001f, "silenciar el bus");
    call ("setTrack", "{\"trackId\":" + bid + ",\"mute\":false}");

    // a second track, soloed elsewhere: the bus must keep playing what the soloed track sends
    auto* other = t.engine.addTrack (TrackKind::Instrument);
    juce::ignoreUnused (other);
    call ("setTrack", "{\"trackId\":" + tid + ",\"solo\":true}");
    CHECK (std::abs (peak() - 0.4f) < 0.01f, "con solo, el bus que recibe la pista sigue sonando");
    call ("setTrack", "{\"trackId\":" + tid + ",\"solo\":false,\"output\":0}");

    // saved and loaded
    call ("setSend", "{\"trackId\":" + tid + ",\"busId\":" + bid + ",\"levelDb\":-3,\"pre\":true}");
    CHECK (t.engine.saveSong().wasOk(), "guardar");
    CHECK (t.engine.loadSong (t.engine.songFolder).wasOk(), "abrir");
    auto* a2 = t.engine.song.findTrack (aid);
    CHECK (a2 != nullptr && a2->sends.size() == 1 && a2->sends[0].busId == busId && a2->sends[0].pre
           && std::abs (a2->sends[0].levelDb + 3.0f) < 0.01f, "los envíos se guardan");
    CHECK (t.engine.song.findTrack (busId) != nullptr && t.engine.song.findTrack (busId)->kind == TrackKind::Bus, "el bus se guarda");

    // undo
    call ("setSend", "{\"trackId\":" + tid + ",\"busId\":" + bid + ",\"remove\":true}");
    t.api.call ("undo", {});
    CHECK (t.engine.song.findTrack (aid)->sends.size() == 1, "deshacer trae el envío");

    // buses refuse regions and instruments
    CHECK (! (bool) call ("addClip", "{\"trackId\":" + bid + "}")["ok"], "un bus no acepta regiones");

    // a reverb bus adds a tail
    TestEngine u;
    u.engine.song.limiter = false;
    auto* keys = u.engine.addTrack (TrackKind::Instrument, juce::String (u8"Piano eléctrico"));
    Clip k; k.start = 0; k.length = 1; k.loopLength = 1; k.notes = { { 0.0, 0.5, 60, 0.9f } };
    u.engine.addClip (*keys, k);
    auto tail = [&]
    {
        u.engine.setPositionBeats (0);
        u.engine.play();
        juce::AudioBuffer<float> out;
        u.engine.renderOffline (out, (int) u.engine.beatsToSamples (4.0));
        u.engine.stop();
        const int from = (int) u.engine.beatsToSamples (2.5);
        return out.getRMSLevel (0, from, out.getNumSamples() - from);
    };
    const float dry = tail();
    auto* rev = u.engine.addTrack (TrackKind::Bus, "reverb");
    CHECK (rev->name == "Reverb" && rev->inserts.size() == 1, "bus de reverb listo");
    u.api.call ("setSend", juce::JSON::parse ("{\"trackId\":" + juce::String (keys->id) + ",\"busId\":" + juce::String (rev->id) + ",\"levelDb\":0}"));
    const float wet = tail();
    CHECK (wet > dry * 2.0f + 1.0e-5f, "la reverb compartida agrega cola (" << dry << " -> " << wet << ")");
}

static void testChords()
{
    std::cout << "Identificar acordes\n";
    TestEngine t;
    t.engine.song.bpm = 120.0;
    const double sr = 48000.0, spb = 24000.0;   // 120 bpm
    // C - Am - F - G7, a bar each, played on a "guitar" with harmonics and a bass note
    const int prog[4][5] = { { 48, 60, 64, 67, -1 }, { 45, 57, 60, 64, -1 }, { 41, 57, 60, 65, -1 }, { 43, 59, 62, 65, 67 } };
    const int total = (int) (spb * 16);
    juce::AudioBuffer<float> audio (1, total);
    audio.clear();
    juce::Random rng (3);
    for (int bar = 0; bar < 4; ++bar)
        for (int k = 0; k < 5; ++k)
        {
            if (prog[bar][k] < 0) continue;
            const double f0 = 440.0 * std::pow (2.0, (prog[bar][k] - 69) / 12.0);
            for (int strum = 0; strum < 4; ++strum)            // strummed on every beat
            {
                const int start = (int) ((bar * 4 + strum) * spb);
                for (int i = 0; i < (int) spb; ++i)
                {
                    const double env = std::exp (-3.0 * i / sr);
                    double v = 0;
                    for (int h = 1; h <= 5; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * i / sr) / (h * h);
                    audio.addSample (0, start + i, (float) (0.08 * env * v));
                }
            }
        }
    for (int i = 0; i < total; ++i) audio.addSample (0, i, (rng.nextFloat() - 0.5f) * 0.002f);   // a little noise
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (t.engine.getAudioFolder().getChildFile ("prog.wav"));
        auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (audio, 0, total);
    }
    auto* a = t.engine.addTrack (TrackKind::Audio);
    Clip c;
    c.midi = false; c.audioFile = "prog.wav"; c.audio = t.engine.loadAudio ("prog.wav");
    c.start = 4; c.length = 16;      // starts at bar 2
    auto& added = t.engine.addClip (*a, c);

    auto r = t.api.call ("detectChords", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"clipId\":" + juce::String (added.id) + "}"));
    CHECK ((bool) r["ok"], "identificar acordes de audio: " << r["error"].toString());
    auto names = [&]
    {
        juce::StringArray out;
        for (auto& ch : t.engine.song.chords) out.add (juce::String (ch.beat) + ":" + analysis::chordName (ch.root, ch.quality));
        return out.joinIntoString (" ");
    };
    CHECK (names() == "4:Do 8:Lam 12:Fa 16:Sol7", "acordes de una guitarra: " << names());

    // MIDI region: Dm - G - C (whole bar each), with the chord track replacing only that range
    auto* keys = t.engine.addTrack (TrackKind::Instrument, juce::String (u8"Piano eléctrico"));
    Clip m;
    m.start = 20; m.length = 12; m.loopLength = 12;
    const int midiProg[3][4] = { { 50, 62, 65, 69 }, { 43, 59, 62, 67 }, { 48, 60, 64, 67 } };
    for (int bar = 0; bar < 3; ++bar)
        for (int k = 0; k < 4; ++k)
            m.notes.push_back ({ bar * 4.0, 3.9, midiProg[bar][k], 0.8f });
    auto& mc = t.engine.addClip (*keys, m);
    r = t.api.call ("detectChords", juce::JSON::parse ("{\"trackId\":" + juce::String (keys->id) + ",\"clipId\":" + juce::String (mc.id) + "}"));
    CHECK ((bool) r["ok"] && names() == "4:Do 8:Lam 12:Fa 16:Sol7 20:Rem 24:Sol 28:Do", "acordes de MIDI: " << names());

    // undo / save
    t.api.call ("undo", {});
    CHECK (t.engine.song.chords.size() == 4, "deshacer la detección");
    t.api.call ("redo", {});
    CHECK (t.engine.saveSong().wasOk() && t.engine.loadSong (t.engine.songFolder).wasOk() && t.engine.song.chords.size() == 7, "los acordes se guardan");

    // whole song (renders the mix without drums)
    t.engine.addTrack (TrackKind::Drummer);
    r = t.api.call ("clearChords", {});
    r = t.api.call ("detectChords", juce::JSON::parse ("{}"));
    CHECK ((bool) r["ok"] && t.engine.song.chords.size() >= 6, "acordes de toda la canción: " << names());

    // a chord track to play them
    r = t.api.call ("chordsToTrack", {});
    auto* ct = t.engine.song.findTrack ((int) r["data"]);
    CHECK (ct != nullptr && ct->clips.size() == 1 && ct->clips[0].notes.size() >= 24, "crear pista con los acordes");
}

static double zeroCrossFreq (const juce::AudioBuffer<float>& b, int from, int to, double sr)
{
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f) { ++crossings; if (first < 0) first = i; last = i; }
    return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
}

static void writeSine (const juce::File& f, double freq, double seconds, double sr = 44100.0)
{
    juce::WavAudioFormat fmt;
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (16));
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static void testSampler()
{
    std::cout << "Sampler (SFZ)\n";
    auto dir = tempRoot().getChildFile ("Inst");
    writeSine (dir.getChildFile ("Samples/soft C4.wav"), 261.63, 2.0);
    writeSine (dir.getChildFile ("Samples/loud C4.wav"), 523.25, 2.0);   // an octave up, so we can tell the layers apart
    writeSine (dir.getChildFile ("Samples/A5.wav"), 880.0, 2.0);
    dir.getChildFile ("map.txt").replaceWithText (
        "<region> lokey=48 hikey=71 pitch_keycenter=c4 sample=$LAYER C4.wav\n");
    dir.getChildFile ("Test.sfz").replaceWithText (
        "// test instrument\n"
        "#define $EXT wav\n"
        "<control> default_path=Samples/\n"
        "<global> ampeg_release=0.05 set_cc20=100\n"
        "<group> hivel=63\n#define $LAYER soft\n#include \"map.txt\"\n"
        "<group> lovel=64 locc20=90\n#define $LAYER loud\n#include \"map.txt\"\n"
        "<group> lovel=64 hicc20=50 volume=-60\n#define $LAYER soft\n#include \"map.txt\"\n"   // cc20 is 100: never plays
        "<group>\n<region> lokey=72 hikey=96 pitch_keycenter=a5 sample=A5.$EXT  // high notes\n");

    auto p = createBuiltin ("sampler");
    auto* b = dynamic_cast<BuiltinProcessor*> (p.get());
    p->setRateAndBufferSizeDetails (48000.0, 256);
    p->prepareToPlay (48000.0, 256);
    CHECK (b->setExtra ("file", dir.getChildFile ("Test.sfz").getFullPathName()).wasOk(), "cargar SFZ");
    for (int i = 0; i < 500 && b->getExtraInfo()["status"].toString().startsWith ("Cargando"); ++i) juce::Thread::sleep (10);
    CHECK (b->getExtraInfo()["status"].toString().contains ("4 muestras"), "regiones leídas: " << b->getExtraInfo()["status"].toString());

    auto play = [&] (int note, int vel)
    {
        juce::AudioBuffer<float> out (2, 256 * 188);
        out.clear();
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            juce::AudioBuffer<float> blk (2, 256);
            juce::MidiBuffer m;
            if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), 0);
            if (pos == 256 * 100) m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            p->processBlock (blk, m);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, 256);
        }
        return out;
    };
    auto soft = play (62, 40);                      // D4 from the C4 sample, soft layer
    const double f1 = zeroCrossFreq (soft, 2000, 20000, 48000.0);
    CHECK (std::abs (f1 - 293.66) < 2.0, "afina la muestra a la nota tocada (" << f1 << " Hz)");
    auto loud = play (60, 110);                     // loud layer (octave sample)
    const double f2 = zeroCrossFreq (loud, 2000, 20000, 48000.0);
    CHECK (std::abs (f2 - 523.25) < 3.0, "capa de velocidad fuerte (" << f2 << " Hz)");
    CHECK (rms (loud) > rms (soft), "más fuerte con más velocidad");
    auto high = play (81, 100);
    CHECK (std::abs (zeroCrossFreq (high, 2000, 20000, 48000.0) - 880.0) < 4.0, "región aguda");
    // after the note off + release, silence
    CHECK (high.getMagnitude (0, 40000, 8000) < 1.0e-3f, "se apaga al soltar");

    // a single sound file: the note name in the file name is where it sits
    auto q = createBuiltin ("sampler");
    auto* qb = dynamic_cast<BuiltinProcessor*> (q.get());
    q->setRateAndBufferSizeDetails (48000.0, 256);
    q->prepareToPlay (48000.0, 256);
    qb->setExtra ("file", dir.getChildFile ("Samples/A5.wav").getFullPathName());
    for (int i = 0; i < 500 && qb->getExtraInfo()["status"].toString().startsWith ("Cargando"); ++i) juce::Thread::sleep (10);
    juce::AudioBuffer<float> blk (2, 4096);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);   // A4: an octave below the sample
    q->processBlock (blk, m);
    juce::AudioBuffer<float> more (2, 8192);
    juce::MidiBuffer none;
    q->processBlock (more, none);
    CHECK (std::abs (zeroCrossFreq (more, 0, 8192, 48000.0) - 440.0) < 3.0, "un .wav suelto se reparte por el teclado");

    // Installing a downloaded library (the zip GitHub gives): unpacked into the sounds folder, then playable
    {
        auto soundsDir = tempRoot().getChildFile ("Sonidos");
        setEnv ("GALPON_SOUNDS_DIR", soundsDir.getFullPathName());
        SoundLibrary::Sound fake;
        fake.id = "test-inst"; fake.folderInZip = "Inst-master"; fake.sfz = "Test.sfz";
        auto zipFile = tempRoot().getChildFile ("inst.zip");
        {
            juce::ZipFile::Builder builder;
            for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
                builder.addFile (f, 0, "Inst-master/" + f.getRelativePathFrom (dir).replaceCharacter ('\\', '/'));
            juce::FileOutputStream os (zipFile);
            builder.writeToStream (os, nullptr);
        }
        auto r = SoundLibrary::install (fake, zipFile);
        CHECK (r.wasOk() && soundsDir.getChildFile ("test-inst/Inst-master/Test.sfz").existsAsFile(), "instalar una biblioteca descargada: " << r.getErrorMessage());
        CHECK (SoundLibrary::install (fake, tempRoot().getChildFile ("no-existe.zip")).failed(), "un zip roto da error claro");

        TestEngine t;
        auto res = t.api.call ("useSound", juce::JSON::parse ("{\"file\":" + juce::JSON::toString (soundsDir.getChildFile ("test-inst/Inst-master/Test.sfz").getFullPathName()) + "}"));
        CHECK ((bool) res["ok"], "usar un instrumento SFZ en una pista nueva: " << res["error"].toString());
        auto* tr = t.engine.song.findTrack ((int) res["data"]);
        CHECK (tr != nullptr && tr->instrument.type == "sampler", "la pista tiene el sampler");
        const int samplerTrackId = tr->id;
        juce::Thread::sleep (300);
        Clip c; c.start = 0; c.length = 2; c.loopLength = 2; c.notes = { { 0.0, 1.0, 60, 0.9f } };
        t.engine.addClip (*tr, c);
        t.engine.setPositionBeats (0);
        t.engine.play();
        auto out = t.render (2.0);
        t.engine.stop();
        CHECK (rms (out) > 0.01f, "la pista con sampler suena (rms " << rms (out) << ")");
        // the sampler's file is saved with the song
        CHECK (t.engine.saveSong().wasOk() && t.engine.loadSong (t.engine.songFolder).wasOk(), "guardar y abrir");
        auto* again = t.engine.song.findTrack (samplerTrackId);
        auto* sb = again != nullptr ? dynamic_cast<BuiltinProcessor*> (again->instrument.processor.get()) : nullptr;
        CHECK (sb != nullptr && sb->getExtraInfo()["file"].toString().endsWith ("Test.sfz"), "el sampler recuerda su instrumento: " << (again ? again->instrument.type + " / " + (sb ? juce::JSON::toString (sb->getExtraInfo(), true) : juce::String ("sin proceso")) : juce::String ("sin pista")));
        CHECK (SoundLibrary::catalog().size() >= 4 && SoundLibrary::find ("splendid-piano") != nullptr, "catálogo de sonidos reales");
    }

    // Real libraries' mappings (no samples needed): GALPON_SFZ_PARSE="a.sfz;b.sfz"
    for (auto& path : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("GALPON_SFZ_PARSE", {}), ";", ""))
        std::cout << "  " << juce::File (path).getFileName() << ": " << juce::JSON::toString (describeSfz (juce::File (path)), true) << "\n";

    // A real library, if there's one on this machine (GALPON_SFZ=/path/to/file.sfz)
    if (auto real = juce::SystemStats::getEnvironmentVariable ("GALPON_SFZ", {}); real.isNotEmpty())
    {
        auto r = createBuiltin ("sampler");
        auto* rb = dynamic_cast<BuiltinProcessor*> (r.get());
        r->setRateAndBufferSizeDetails (48000.0, 256);
        r->prepareToPlay (48000.0, 256);
        const auto t0 = juce::Time::getMillisecondCounter();
        rb->setExtra ("file", real);
        for (int i = 0; i < 12000 && rb->getExtraInfo()["status"].toString().startsWith ("Cargando"); ++i) juce::Thread::sleep (10);
        std::cout << "  " << rb->getExtraInfo()["status"].toString() << " en " << (juce::Time::getMillisecondCounter() - t0) << " ms\n";
        juce::AudioBuffer<float> out (2, 256);
        float peak = 0;
        for (int k = 0; k < 200; ++k)
        {
            juce::MidiBuffer mm;
            if (k == 0) { mm.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 0); mm.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 0); }
            r->processBlock (out, mm);
            peak = juce::jmax (peak, out.getMagnitude (0, 256));
        }
        CHECK (peak > 0.01f && finite (out), "la biblioteca real suena (pico " << peak << ")");
    }
}

static void writeBuffer (const juce::File& f, const juce::AudioBuffer<float>& b, double sr = 48000.0)
{
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static float medianPitch (const juce::AudioBuffer<float>& b, double from, double to, double sr)
{
    auto frames = flex::detectPitch (b.getReadPointer (0), b.getNumSamples(), sr);
    std::vector<float> v;
    for (auto& f : frames) if (f.midi > 0 && f.time >= from && f.time <= to) v.push_back (f.midi);
    if (v.empty()) return -1;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

static void testFlex()
{
    std::cout << "Flex: afinar y cuantizar audio\n";
    const double sr = 48000.0;
    TestEngine t;
    t.engine.song.bpm = 120.0;
    t.engine.song.key = "C";
    t.engine.song.minor = false;

    // A "singer": a bit sharp on A3, flat on C4 (with harmonics like a voice, and some vibrato)
    juce::AudioBuffer<float> voice (1, (int) (sr * 2.0));
    voice.clear();
    const double notes[2][3] = { { 0.0, 0.9, 57.40 }, { 1.0, 1.9, 59.70 } };   // start s, end s, midi
    double phase = 0;
    for (int i = 0; i < voice.getNumSamples(); ++i)
    {
        const double tt = i / sr;
        for (auto& n : notes)
            if (tt >= n[0] && tt < n[1])
            {
                const double midi = n[2] + 0.12 * std::sin (2 * juce::MathConstants<double>::pi * 5.0 * tt);
                phase += 440.0 * std::pow (2.0, (midi - 69) / 12.0) / sr;
                float v = 0;
                for (int h = 1; h <= 6; ++h) v += (float) std::sin (2 * juce::MathConstants<double>::pi * phase * h) / (float) h;
                const double env = juce::jmin (1.0, (tt - n[0]) / 0.02, (n[1] - tt) / 0.02);
                voice.setSample (0, i, (float) (0.25 * env) * v);
            }
    }
    writeBuffer (t.engine.getAudioFolder().getChildFile ("voz.wav"), voice);
    auto* a = t.engine.addTrack (TrackKind::Audio);
    Clip c;
    c.midi = false; c.audioFile = "voz.wav"; c.audio = t.engine.loadAudio ("voz.wav");
    c.start = 0; c.length = t.engine.samplesToBeats (voice.getNumSamples()); c.sourceBpm = 120.0;
    const int clipId = t.engine.addClip (*a, c).id;
    const juce::String ids = "\"trackId\":" + juce::String (a->id) + ",\"clipId\":" + juce::String (clipId);

    auto an = t.api.call ("flexAnalyze", juce::JSON::parse ("{" + ids + "}"));
    auto* notesArr = an["data"]["notes"].getArray();
    CHECK (notesArr != nullptr && notesArr->size() == 2, "encuentra las 2 notas cantadas (" << (notesArr ? notesArr->size() : 0) << ")");
    if (notesArr != nullptr && notesArr->size() == 2)
    {
        CHECK (std::abs ((double) (*notesArr)[0]["pitch"] - 57.4) < 0.15 && std::abs ((double) (*notesArr)[1]["pitch"] - 59.7) < 0.15,
               "mide la afinación de cada nota (" << (double) (*notesArr)[0]["pitch"] << ", " << (double) (*notesArr)[1]["pitch"] << ")");
    }

    auto r = t.api.call ("flexApply", juce::JSON::parse ("{" + ids + ",\"settings\":{\"tuneStrength\":1}}"));
    CHECK ((bool) r["ok"], "afinar: " << r["error"].toString());
    auto* clip = t.engine.song.findTrack (a->id)->findClip (clipId);
    CHECK (clip->flex.on && clip->audioFile != "voz.wav" && clip->audio != nullptr, "la región usa la versión afinada");
    const float p1 = medianPitch (*clip->audio, 0.2, 0.7, sr), p2 = medianPitch (*clip->audio, 1.2, 1.7, sr);
    CHECK (std::abs (p1 - 57.0f) < 0.12f && std::abs (p2 - 60.0f) < 0.12f, "notas afinadas a La3 y Do4 (" << p1 << ", " << p2 << ")");
    CHECK (std::abs (clip->audio->getNumSamples() - voice.getNumSamples()) < 64, "afinar no cambia la duración");

    // drag the first note up two semitones (on top of the tuning)
    const double key0 = (double) (*notesArr)[0]["key"];
    r = t.api.call ("flexApply", juce::JSON::parse ("{" + ids + ",\"settings\":{\"tuneStrength\":1,\"noteShifts\":[[" + juce::String (key0) + ",2]]}}"));
    clip = t.engine.song.findTrack (a->id)->findClip (clipId);
    const float p1b = medianPitch (*clip->audio, 0.2, 0.7, sr);
    CHECK ((bool) r["ok"] && std::abs (p1b - 59.0f) < 0.15f, "mover una nota a mano (" << p1b << ")");

    // back to the original
    t.api.call ("flexReset", juce::JSON::parse ("{" + ids + "}"));
    clip = t.engine.song.findTrack (a->id)->findClip (clipId);
    CHECK (clip->audioFile == "voz.wav" && ! clip->flex.on, "quitar Flex vuelve a la grabación original");
    t.api.call ("undo", {});
    clip = t.engine.song.findTrack (a->id)->findClip (clipId);
    CHECK (clip->flex.on && clip->audioFile != "voz.wav", "deshacer vuelve a la versión editada");
    t.api.call ("flexReset", juce::JSON::parse ("{" + ids + "}"));

    // Drums played a little early/late: quantize them to the beat
    juce::AudioBuffer<float> hits (1, (int) (sr * 2.6));
    hits.clear();
    const double played[] = { 0.53, 0.97, 1.56, 2.02 };     // ideal: 0.5, 1.0, 1.5, 2.0 s (quarter notes at 120)
    juce::Random rng (5);
    for (double h : played)
        for (int i = 0; i < (int) (0.12 * sr); ++i)
            hits.addSample (0, (int) (h * sr) + i, (rng.nextFloat() * 2 - 1) * 0.6f * (float) std::exp (-i / (0.02 * sr)));
    writeBuffer (t.engine.getAudioFolder().getChildFile ("golpes.wav"), hits);
    auto* d = t.engine.addTrack (TrackKind::Audio);
    Clip dc;
    dc.midi = false; dc.audioFile = "golpes.wav"; dc.audio = t.engine.loadAudio ("golpes.wav");
    dc.start = 0; dc.length = t.engine.samplesToBeats (hits.getNumSamples()); dc.sourceBpm = 120.0;
    const int dId = t.engine.addClip (*d, dc).id;
    const int dTrack = d->id;
    const juce::String dids = "\"trackId\":" + juce::String (d->id) + ",\"clipId\":" + juce::String (dId);
    r = t.api.call ("flexApply", juce::JSON::parse ("{" + dids + ",\"settings\":{\"quantizeGrid\":1,\"quantizeStrength\":1}}"));
    CHECK ((bool) r["ok"], "cuantizar audio: " << r["error"].toString());
    auto* dclip = t.engine.song.findTrack (d->id)->findClip (dId);
    auto found = flex::detectOnsets (dclip->audio->getReadPointer (0), dclip->audio->getNumSamples(), sr);
    auto foundBefore = flex::detectOnsets (hits.getReadPointer (0), hits.getNumSamples(), sr);
    juce::String list;
    double worst = 0;
    for (size_t i = 0; i < found.size(); ++i)
    {
        list << juce::String (found[i], 3) << " ";
        worst = juce::jmax (worst, std::abs (found[i] - std::round (found[i] * 2.0) / 2.0));
    }
    double worstBefore = 0;
    for (auto x : foundBefore) worstBefore = juce::jmax (worstBefore, std::abs (x - std::round (x * 2.0) / 2.0));
    CHECK (found.size() == 4 && worst < 0.012 && worst < worstBefore / 2, "los golpes caen en el tiempo (" << list << "; antes desfasados " << worstBefore << " s)");

    // the song gets slower: the region follows the tempo
    t.engine.song.bpm = 100.0;
    r = t.api.call ("flexApply", juce::JSON::parse ("{" + dids + ",\"settings\":{\"followTempo\":true,\"quantizeGrid\":1}}"));
    dclip = t.engine.song.findTrack (d->id)->findClip (dId);
    const double seconds = dclip->audio->getNumSamples() / sr;
    CHECK ((bool) r["ok"] && std::abs (seconds - 2.6 * 1.2) < 0.01, "sigue el tempo: 20 % más lento (" << seconds << " s)");
    found = flex::detectOnsets (dclip->audio->getReadPointer (0), dclip->audio->getNumSamples(), sr);
    worst = 0;
    for (auto x : found) worst = juce::jmax (worst, std::abs (x - std::round (x / 0.6) * 0.6));   // quarter = 0.6 s at 100 bpm
    CHECK (found.size() == 4 && worst < 0.015, "y sigue cuantizado al nuevo tempo (peor " << worst << " s)");
    CHECK (t.engine.saveSong().wasOk() && t.engine.loadSong (t.engine.songFolder).wasOk()
           && t.engine.song.findTrack (dTrack)->findClip (dId)->flex.followTempo, "los ajustes de Flex se guardan");
}

static void testStems()
{
    std::cout << "Separar en pistas\n";
    TestEngine t;
    writeClick (t.engine.getAudioFolder().getChildFile ("corto.wav"), 100, 0.5f, 4800);
    auto* a = t.engine.addTrack (TrackKind::Audio);
    Clip c;
    c.midi = false; c.audioFile = "corto.wav"; c.audio = t.engine.loadAudio ("corto.wav");
    c.start = 0; c.length = t.engine.samplesToBeats (4800);
    const int id = t.engine.addClip (*a, c).id;
    auto r = t.api.call ("separateStems", juce::JSON::parse ("{\"trackId\":" + juce::String (a->id) + ",\"clipId\":" + juce::String (id) + "}"));
    CHECK (! (bool) r["ok"] && r["error"].toString().contains ("corta"), "una región muy corta da un aviso claro");
    auto* drummer = t.engine.addTrack (TrackKind::Drummer);
    r = t.api.call ("separateStems", juce::JSON::parse ("{\"trackId\":" + juce::String (drummer->id) + ",\"clipId\":" + juce::String (drummer->clips[0].id) + "}"));
    CHECK (! (bool) r["ok"], "solo regiones de audio");
    CHECK (StemSeparator::stemName (3) == juce::String (juce::CharPointer_UTF8 ("Voz")) && StemSeparator::modelUrl().startsWith ("https://"), "nombres y modelo");
    // The real network is run only when asked: GALPON_STEMS_TEST=1 (needs the model, minutes of CPU)
}

/** GALPON_DEMO_OUT=/path/demo.wav ./GalponTests  -> also renders a short demo song to listen to. */
static void renderDemo (const juce::String& path)
{
    TestEngine t;
    t.engine.song.bpm = 96;
    t.engine.song.key = "A";
    t.engine.song.minor = true;
    auto* drums = t.engine.addTrack (TrackKind::Drummer);
    drums->drummer.style = "funk";
    drums->drummer.complexity = 0.55f;
    drums->drummer.percussion = true;
    drums->clips[0].length = 32;
    auto* bass = t.engine.addTrack (TrackKind::Instrument, juce::String (u8"Bajo eléctrico"));
    t.engine.addLoop ("bass-walk", 0, bass->id);
    bass->clips[0].length = 32;
    drums->drummer.followTrack = bass->id;
    t.engine.regenerateDrummer (*drums);
    auto* keys = t.engine.addTrack (TrackKind::Instrument, juce::String (u8"Piano eléctrico"));
    t.engine.addLoop ("keys-ep", 0, keys->id);
    keys->clips[0].length = 32;
    t.engine.addInsert (keys->id, "builtin", "reverb");
    keys->volumeDb = -4.0f;
    auto r = t.engine.exportMix (juce::File (path), 24, 0, 32);
    std::cout << "Demo: " << (r.wasOk() ? path : r.getErrorMessage()) << "\n";
}

static void testAutomation()
{
    std::cout << "Automatización\n";
    TestEngine t;
    t.engine.song.limiter = false;
    t.engine.song.bpm = 120.0;
    // a steady tone, 8 beats (4 s)
    juce::AudioBuffer<float> tone (1, 48000 * 4);
    for (int i = 0; i < tone.getNumSamples(); ++i) tone.setSample (0, i, 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 200.0 * i / 48000.0));
    writeBuffer (t.engine.getAudioFolder().getChildFile ("tono.wav"), tone);
    auto* a = t.engine.addTrack (TrackKind::Audio);
    const int aid = a->id;
    Clip c; c.midi = false; c.audioFile = "tono.wav"; c.audio = t.engine.loadAudio ("tono.wav"); c.start = 0; c.length = 8;
    t.engine.addClip (*a, c);
    const juce::String T (aid);
    auto call = [&] (const char* cmd, const juce::String& json) { return t.api.call (cmd, juce::JSON::parse (json)); };
    auto renderAll = [&]
    {
        t.engine.setPositionBeats (0);
        t.engine.play();
        auto out = t.render (8.0);
        t.engine.stop();
        return out;
    };
    auto rmsAt = [] (const juce::AudioBuffer<float>& b, int ch, double beat) { return b.getRMSLevel (ch, (int) (beat * 24000), 2400); };

    // volume: a fade-out from beat 2 to 6
    auto r = call ("setAutomation", "{\"trackId\":" + T + ",\"target\":\"volume\",\"points\":[[2,0],[6,-60]]}");
    CHECK ((bool) r["ok"], "crear automatización de volumen");
    auto out = renderAll();
    const float full = rmsAt (out, 0, 1.0), mid = rmsAt (out, 0, 4.0), end = rmsAt (out, 0, 7.0);
    CHECK (std::abs (juce::Decibels::gainToDecibels (mid / full) + 30.0f) < 1.5f, "a mitad del fundido está a -30 dB (" << juce::Decibels::gainToDecibels (mid / full) << ")");
    CHECK (end < full * 0.002f, "al final del fundido no suena");
    // smooth: no jumps from one sample to the next bigger than the tone itself makes
    float worst = 0;
    for (int i = 48000; i < 48000 * 3; ++i) worst = juce::jmax (worst, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
    CHECK (worst < 0.0132f, "el fundido es suave, sin escalones (" << worst << "; el tono solo da 0,0131)");

    // pan: hard left at beat 1, hard right at beat 7
    call ("removeAutomation", "{\"trackId\":" + T + ",\"target\":\"volume\"}");
    call ("setAutomation", "{\"trackId\":" + T + ",\"target\":\"pan\",\"points\":[[1,-1],[7,1]]}");
    out = renderAll();
    CHECK (rmsAt (out, 1, 0.5) < 0.01f && rmsAt (out, 0, 7.5) < 0.01f && rmsAt (out, 0, 0.5) > 0.2f, "el paneo va de izquierda a derecha");
    call ("removeAutomation", "{\"trackId\":" + T + ",\"target\":\"pan\"}");

    // an effect control: the drive opens up over the song
    call ("addInsert", "{\"trackId\":" + T + ",\"kind\":\"builtin\",\"id\":\"eq\"}");
    call ("addInsert", "{\"trackId\":" + T + ",\"kind\":\"builtin\",\"id\":\"drive\"}");
    call ("setAutomation", "{\"trackId\":" + T + ",\"target\":\"fx:1:0\",\"points\":[[0,0.1],[8,0.9]]}");
    t.engine.setPositionBeats (0);
    t.engine.play();
    t.render (4.0);
    auto* drive = t.engine.song.findTrack (aid)->inserts[1].processor.get();
    const float atHalf = drive->getParameters()[0]->getValue();
    t.engine.stop();
    CHECK (std::abs (atHalf - 0.5f) < 0.03f, "el control del efecto sigue la curva (" << atHalf << ")");
    // moving the effect keeps its automation attached to it
    call ("moveInsert", "{\"trackId\":" + T + ",\"from\":1,\"to\":0}");
    CHECK (t.engine.song.findTrack (aid)->findLane ("fx:0:0") != nullptr && t.engine.song.findTrack (aid)->findLane ("fx:1:0") == nullptr,
           "mover el efecto mueve su automatización");
    call ("removeInsert", "{\"trackId\":" + T + ",\"index\":0}");
    CHECK (t.engine.song.findTrack (aid)->findLane ("fx:0:0") == nullptr, "borrar el efecto borra su automatización");

    // recording movements: write on, play, move the fader
    call ("setTrack", "{\"trackId\":" + T + ",\"autoWrite\":true}");
    t.engine.setPositionBeats (0);
    t.engine.play();
    for (int step = 0; step < 8; ++step)
    {
        call ("setTrack", "{\"trackId\":" + T + ",\"volumeDb\":" + juce::String (-step * 3) + "}");
        t.render (0.5);
    }
    t.api.call ("stop", {});
    auto* lane = t.engine.song.findTrack (aid)->findLane ("volume");
    CHECK (lane != nullptr && lane->points.size() >= 8, "grabar los movimientos del volumen (" << (lane ? (int) lane->points.size() : 0) << " puntos)");
    if (lane != nullptr) CHECK (std::abs (lane->valueAt (3.6) + 21.0f) < 3.5f, "la curva grabada sigue al movimiento (" << lane->valueAt (3.6) << " dB)");
    call ("setTrack", "{\"trackId\":" + T + ",\"autoWrite\":false,\"volumeDb\":0}");
    out = renderAll();
    CHECK (rmsAt (out, 0, 3.6) < rmsAt (out, 0, 0.2) * 0.2f, "y al reproducir hace lo grabado aunque el fader esté arriba");

    // saved, undone
    CHECK (t.engine.saveSong().wasOk() && t.engine.loadSong (t.engine.songFolder).wasOk()
           && t.engine.song.findTrack (aid)->findLane ("volume") != nullptr
           && t.engine.song.findTrack (aid)->findLane ("volume")->kind == AutomationLane::Volume, "la automatización se guarda");
    call ("removeAutomation", "{\"trackId\":" + T + ",\"target\":\"volume\"}");
    t.api.call ("undo", {});
    CHECK (t.engine.song.findTrack (aid)->findLane ("volume") != nullptr, "deshacer trae la curva borrada");
    CHECK (! (bool) call ("setAutomation", "{\"trackId\":" + T + ",\"target\":\"cualquiera\",\"points\":[]}")["ok"], "control desconocido da error");

    // send automation into a reverb bus
    auto* bus = t.engine.addTrack (TrackKind::Bus, "none");
    const juce::String B (bus->id);
    call ("removeAutomation", "{\"trackId\":" + T + ",\"target\":\"volume\"}");
    call ("setSend", "{\"trackId\":" + T + ",\"busId\":" + B + ",\"levelDb\":0}");
    call ("setAutomation", "{\"trackId\":" + T + ",\"target\":\"send:" + B + "\",\"points\":[[0,-80],[4,-80],[4.01,0]]}");
    out = renderAll();
    CHECK (rmsAt (out, 0, 6.0) > rmsAt (out, 0, 2.0) * 1.8f, "el envío se abre en el compás 2 (" << rmsAt (out, 0, 2.0) << " → " << rmsAt (out, 0, 6.0) << ")");
}

/** A plucked string (Karplus-Strong), close enough to a guitar for the detector. */
static void pluck (std::vector<float>& out, double sr, double at, int midi, double dur, float amp = 0.3f, juce::uint32 seed = 1);
static void pluck (std::vector<float>& out, double sr, double at, int midi, double dur, float amp, juce::uint32 seed)
{
    const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
    const int period = std::max (2, (int) std::round (sr / f));
    std::vector<float> line ((size_t) period);
    juce::Random rnd ((juce::int64) seed * 7919 + midi);
    for (auto& v : line) v = rnd.nextFloat() * 2.0f - 1.0f;
    const int start = (int) (at * sr), n = (int) (dur * sr);
    float prev = 0;
    for (int i = 0; i < n && start + i < (int) out.size(); ++i)
    {
        const size_t k = (size_t) (i % period);
        const float y = line[k];
        out[(size_t) (start + i)] += y * amp;
        const float nxt = line[(k + 1) % (size_t) period];
        line[k] = 0.996f * 0.5f * (y + nxt);
        prev = y;
    }
    juce::ignoreUnused (prev);
}

static std::vector<NoteScorer::Judgment> runScorer (const std::vector<float>& audio, std::vector<NoteScorer::Event> events, double speed = 1.0)
{
    const double sr = 48000;
    NoteScorer sc;
    sc.setManual (true);
    sc.prepare (sr);
    // one block first, so the events start "now"
    std::vector<float> silence (256, 0.0f);
    sc.push (silence.data(), 256, -256 * speed / sr, speed / sr, true);
    sc.setEvents (std::move (events), 0);
    sc.processNow();
    double t = 0;                          // audio sample 0 is song time 0
    for (size_t i = 0; i < audio.size(); i += 256)
    {
        const int n = (int) std::min<size_t> (256, audio.size() - i);
        sc.push (audio.data() + i, n, t, speed / sr, true);
        t += n * speed / sr;
        if ((i / 256) % 8 == 0) sc.processNow();
    }
    for (int k = 0; k < 4; ++k) sc.processNow();
    return sc.take();
}

static void testNoteDetection()
{
    std::cout << "Modo juego: detección de notas\n";
    const double sr = 48000;
    juce::Random noise (5);
    auto blank = [&] (double seconds) { std::vector<float> a ((size_t) (seconds * sr)); for (auto& v : a) v = (noise.nextFloat() - 0.5f) * 0.0004f; return a; };
    auto ev = [] (double t, std::vector<int> p, double sus = 0) { NoteScorer::Event e; e.t = t; e.pitches = std::move (p); e.sus = sus; return e; };
    auto attacks = [] (const std::vector<NoteScorer::Judgment>& js) { std::vector<NoteScorer::Judgment> r; for (auto& j : js) if (j.kind == 0) r.push_back (j); return r; };

    // right notes, on time, across the neck (low E of a bass too)
    {
        const std::vector<int> notes { 40, 45, 50, 55, 59, 64, 69, 76, 33, 28 };
        auto a = blank (notes.size() * 0.6 + 1.5);
        std::vector<NoteScorer::Event> es;
        for (size_t i = 0; i < notes.size(); ++i) { pluck (a, sr, 0.5 + i * 0.6, notes[i], 0.55); es.push_back (ev (0.5 + i * 0.6, { notes[i] })); }
        auto js = attacks (runScorer (a, es));
        int hits = 0; float worst = 0;
        for (auto& j : js) if (j.hit) { ++hits; worst = std::max (worst, std::abs (j.timingMs)); }
        CHECK (js.size() == notes.size() && hits == (int) notes.size(), "notas correctas: las reconoce (" << hits << " de " << notes.size() << ")");
        CHECK (worst < 12.0f, "y a tiempo (error máximo " << worst << " ms)");
    }
    // late and early
    {
        auto a = blank (3);
        pluck (a, sr, 0.5 + 0.060, 52, 0.6);
        pluck (a, sr, 1.5 - 0.050, 57, 0.6);
        auto js = attacks (runScorer (a, { ev (0.5, { 52 }), ev (1.5, { 57 }) }));
        CHECK (js.size() == 2 && js[0].hit && std::abs (js[0].timingMs - 60) < 10 && js[1].hit && std::abs (js[1].timingMs + 50) < 10,
               "mide si fue tarde o temprano (" << (js.size() > 1 ? js[0].timingMs : 0) << ", " << (js.size() > 1 ? js[1].timingMs : 0) << " ms)");
        a = blank (3);
        pluck (a, sr, 0.5 + 0.30, 52, 0.6);
        js = attacks (runScorer (a, { ev (0.5, { 52 }) }));
        CHECK (js.size() == 1 && ! js[0].hit, "muy tarde cuenta como errada");
    }
    // wrong notes and silence
    {
        int falseHits = 0, total = 0;
        for (int off : { 1, -1, 2, -2, 5, 7, -5, 12 })
            for (int target : { 40, 47, 55, 64 })
            {
                auto a = blank (1.5);
                pluck (a, sr, 0.5, target + off, 0.8);
                auto js = attacks (runScorer (a, { ev (0.5, { target }) }));
                ++total;
                if (js.size() == 1 && js[0].hit) { ++falseHits; std::cout << "    (aceptó " << target + off << " por " << target << ")\n"; }
            }
        CHECK (falseHits <= 1, "notas equivocadas: no las da por buenas (" << falseHits << " de " << total << ")");
        auto js = attacks (runScorer (blank (1.5), { ev (0.5, { 45 }) }));
        CHECK (js.size() == 1 && ! js[0].hit, "si no tocas nada, es errada");
    }
    // chords, strummed
    {
        const std::vector<int> E { 40, 47, 52, 56, 59, 64 }, A { 45, 52, 57, 61, 64 }, G { 43, 47, 50, 55, 59, 67 };
        auto strum = [&] (std::vector<float>& a, double at, const std::vector<int>& c) { for (size_t i = 0; i < c.size(); ++i) pluck (a, sr, at + i * 0.008, c[i], 1.0, 0.18f, (juce::uint32) i + 3); };
        auto a = blank (4.5);
        strum (a, 0.5, E); strum (a, 1.5, A); strum (a, 2.5, G); strum (a, 3.5, A);
        auto js = attacks (runScorer (a, { ev (0.5, E), ev (1.5, A), ev (2.5, G), ev (3.5, E) }));
        CHECK (js.size() == 4 && js[0].hit && js[1].hit && js[2].hit, "acordes rasgueados: los reconoce");
        CHECK (js.size() == 4 && ! js[3].hit, "otro acorde en lugar del pedido: errado (" << (js.size() == 4 ? js[3].value : -1) << ")");
    }
    // the same note again while it still rings
    {
        auto a = blank (2.5);
        std::vector<NoteScorer::Event> es;
        for (int i = 0; i < 6; ++i) { pluck (a, sr, 0.5 + i * 0.25, 45, 0.6, 0.3f, (juce::uint32) i); es.push_back (ev (0.5 + i * 0.25, { 45 })); }
        auto js = attacks (runScorer (a, es));
        int hits = 0;
        for (auto& j : js) hits += j.hit ? 1 : 0;
        CHECK (hits == 6, "la misma nota repetida rápido (" << hits << " de 6)");
    }
    // holding a long note
    {
        auto a = blank (3.5);
        pluck (a, sr, 0.5, 50, 2.2);
        auto b = blank (3.5);
        pluck (b, sr, 0.5, 50, 0.3);
        auto ja = runScorer (a, { ev (0.5, { 50 }, 1.8) });
        auto jb = runScorer (b, { ev (0.5, { 50 }, 1.8) });
        auto sus = [] (const std::vector<NoteScorer::Judgment>& js) { for (auto& j : js) if (j.kind == 1) return j.value; return -1.0f; };
        CHECK (sus (ja) > 0.8f && sus (jb) >= 0 && sus (jb) < 0.4f, "nota larga: sabe si la mantuviste (" << sus (ja) << " vs " << sus (jb) << ")");
    }
    // at half speed the song time runs slower than the audio
    {
        auto a = blank (3);
        pluck (a, sr, 1.0, 55, 0.7);           // audio second 1.0 = song time 0.5 at half speed
        const double songT = 0.5;
        auto js = attacks (runScorer (a, { ev (songT, { 55 }) }, 0.5));
        CHECK (js.size() == 1 && js[0].hit && std::abs (js[0].timingMs) < 15, "funciona en cámara lenta (" << (js.empty() ? 0 : js[0].timingMs) << " ms)");
    }
}

static bool sameVar (const juce::var& a, const juce::var& b, juce::String& where)
{
    if (b.isVoid() || b.isUndefined()) { if (! (a.isVoid() || a.isUndefined())) { where = "esperaba null, vino " + juce::JSON::toString (a, true); return false; } return true; }
    if (b.isBool()) { if (! a.isBool() || (bool) a != (bool) b) { where = "bool " + b.toString() + " vs " + juce::JSON::toString (a, true); return false; } return true; }
    if (b.isInt() || b.isInt64() || b.isDouble())
    {
        if (! (a.isInt() || a.isInt64() || a.isDouble()) || std::abs ((double) a - (double) b) > 1e-9 * std::max (1.0, std::abs ((double) b))) { where = "número " + b.toString() + " vs " + juce::JSON::toString (a, true); return false; }
        return true;
    }
    if (b.isString()) { if (! a.isString() || a.toString() != b.toString()) { where = "texto «" + b.toString() + "» vs " + juce::JSON::toString (a, true); return false; } return true; }
    if (b.isArray())
    {
        if (! a.isArray() || a.size() != b.size()) { where = "lista de " + juce::String (b.size()) + " vs " + juce::JSON::toString (a, true); return false; }
        for (int i = 0; i < b.size(); ++i) if (! sameVar (a[i], b[i], where)) return false;
        return true;
    }
    if (auto* bo = b.getDynamicObject())
    {
        auto* ao = a.getDynamicObject();
        if (ao == nullptr || ao->getProperties().size() != bo->getProperties().size()) { where = "objeto " + juce::JSON::toString (b, true) + " vs " + juce::JSON::toString (a, true); return false; }
        for (auto& kv : bo->getProperties())
        {
            if (! ao->hasProperty (kv.name)) { where = "falta la clave " + kv.name.toString(); return false; }
            if (! sameVar (ao->getProperty (kv.name), kv.value, where)) { where = kv.name.toString() + ": " + where; return false; }
        }
        return true;
    }
    return true;
}

/** Manifests as real tools write them (PyYAML), from GALPON_YAML_DIR: same data as the .json next to each. */
static void testYamlCorpus()
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("GALPON_YAML_DIR", {});
    if (dir.isEmpty()) return;
    std::cout << "YAML: manifiestos de verdad\n";
    int okCount = 0, total = 0, shown = 0;
    for (auto& f : juce::File (dir).findChildFiles (juce::File::findFiles, false, "*.yaml"))
    {
        ++total;
        juce::String err;
        auto v = parseYaml (f.loadFileAsString(), err);
        auto expect = juce::JSON::parse (f.withFileExtension ("json").loadFileAsString());
        juce::String where;
        if (err.isEmpty() && sameVar (v, expect, where)) ++okCount;
        else if (shown++ < 12) std::cout << "    " << f.getFileName() << ": " << (err.isNotEmpty() ? err : where) << "\n";
    }
    CHECK (okCount == total, "lee igual que PyYAML (" << okCount << " de " << total << ")");
}

static void testFeedpak()
{
    std::cout << "Modo juego: .feedpak\n";
    // YAML: the bits manifests use
    {
        juce::String err;
        auto v = parseYaml (
            "# comment\n"
            "title: \"Black Hole: Sun\"   # quoted, with a colon\n"
            "artist: Plain text here\n"
            "year: 1994\n"
            "duration: 320.5\n"
            "genres:\n  - \"Rock\"\n  - Grunge\n"
            "authors:\n  - name: Jane\n    role: transcriber\n  - name: Bob\n"
            "arrangements:\n  - id: lead\n    tuning: [0, -2, 0, 0, 0, 0]\n    tones:\n      base: \"Clean\"\n      base_rig: clean\n"
            "list_at_same_indent:\n- a\n- b\n"
            "flow_map: {a: 1, b: 'it''s'}\n"
            "notes: |\n  line one\n  line two\n"
            "empty:\n"
            "on_flag: true\n", err);
        CHECK (err.isEmpty(), "YAML sin errores: " << err);
        CHECK (v["title"].toString() == "Black Hole: Sun" && v["artist"].toString() == "Plain text here", "textos con comillas y sin");
        CHECK ((int) v["year"] == 1994 && std::abs ((double) v["duration"] - 320.5) < 1e-9, "números");
        CHECK (v["genres"].size() == 2 && v["genres"][1].toString() == "Grunge", "listas");
        CHECK (v["authors"].size() == 2 && v["authors"][0]["role"].toString() == "transcriber" && v["authors"][1]["name"].toString() == "Bob", "listas de objetos");
        CHECK (v["arrangements"][0]["tuning"].size() == 6 && (int) v["arrangements"][0]["tuning"][1] == -2, "listas en una línea");
        CHECK (v["arrangements"][0]["tones"]["base_rig"].toString() == "clean", "objetos anidados");
        CHECK (v["list_at_same_indent"].size() == 2, "lista a la misma sangría que la clave");
        CHECK (v["flow_map"]["b"].toString() == "it's" && (int) v["flow_map"]["a"] == 1, "objetos en una línea");
        CHECK (v["notes"].toString() == "line one\nline two\n", "bloques de texto |");
        CHECK (v["empty"].isVoid() && (bool) v["on_flag"], "vacío y booleanos");
    }
    CHECK (Feedpak::isSafePath ("stems/full.ogg") && ! Feedpak::isSafePath ("../x.ogg") && ! Feedpak::isSafePath ("/etc/passwd")
           && ! Feedpak::isSafePath ("a//b") && ! Feedpak::isSafePath ("C:/x") && ! Feedpak::isSafePath ("a\\b"), "rutas inseguras rechazadas");
    CHECK (juce::JSON::parse (Feedpak::stripJsonComments ("{\"a\": \"http://x\" // c\n, /* b */ \"b\": 2}"))["b"].toString() == "2", "JSONC sin comentarios");

    const auto data = juce::File (GALPON_TEST_DATA_DIR);
    // folder form
    Feedpak ext;
    auto r = ext.open (data.getChildFile ("extended.feedpak"));
    CHECK (r.wasOk(), "abrir un .feedpak carpeta: " << r.getErrorMessage());
    CHECK (ext.manifest()["title"].toString() == "Extended Demo" && ext.manifest()["stems"].size() == 5, "manifiesto del ejemplo de la especificación");
    juce::String err;
    auto game = ext.loadForGame (err);
    auto* arrs = game["arrangements"].getDynamicObject();
    CHECK (arrs != nullptr && arrs->hasProperty ("lead") && arrs->hasProperty ("bass") && ! arrs->hasProperty ("drums_kit") && ! arrs->hasProperty ("keys"),
           "carga guitarra y bajo (no la batería ni el piano)");
    CHECK (game["arrangements"]["lead"]["notes"].size() == 6 && game["arrangements"]["lead"]["chords"].size() == 1, "notas y acordes");
    CHECK (game["beats"].size() == 9 && game["sections"].size() >= 1 && game["tempos"].size() == 2, "compases y partes de song_timeline");
    CHECK (game["lyrics"].size() == 6, "letra");
    auto summary = ext.summary (true);
    CHECK (summary["cover"].toString().startsWith ("data:image/png;base64,"), "portada");
    double sr = 0;
    auto audio = ext.readAudio ("stems/guitar.ogg", sr, err);
    CHECK (audio != nullptr && sr > 0 && audio->getNumSamples() > 0, "lee audio OGG: " << err);

    // zip form, with and without a top folder
    auto zipWith = [&] (const juce::String& prefix, const juce::File& out)
    {
        juce::ZipFile::Builder b;
        auto dir = data.getChildFile ("minimal.feedpak");
        for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
            b.addFile (f, 6, prefix + f.getRelativePathFrom (dir).replaceCharacter ('\\', '/'));
        juce::FileOutputStream os (out);
        os.setPosition (0); os.truncate();
        b.writeToStream (os, nullptr);
    };
    auto z1 = tempRoot().getChildFile ("cancion.feedpak"), z2 = tempRoot().getChildFile ("con carpeta.feedpak");
    zipWith ("", z1);
    zipWith ("minimal.feedpak/", z2);
    Feedpak a, b;
    CHECK (a.open (z1).wasOk() && b.open (z2).wasOk() && a.manifest()["title"].toString() == "Untitled Demo" && b.manifest()["title"].toString() == "Untitled Demo",
           "abrir .feedpak comprimidos");
    juce::MemoryBlock mb;
    CHECK (a.readBytes ("../../etc/passwd", mb).failed(), "no sale del paquete");
    Feedpak bad;
    CHECK (bad.open (tempRoot().getChildFile ("no-existe.feedpak")).failed(), "error claro si no existe");

    // the player
    {
        GamePlayer gp;
        gp.prepare (48000.0, 256);
        auto tone = [] (float v, int n) { auto b2 = std::make_shared<juce::AudioBuffer<float>> (2, n); for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (b2->getWritePointer (ch), v, n); return b2; };
        std::vector<GamePlayer::StemSource> src;
        src.push_back ({ "full", "Mezcla", tone (0.5f, 48000 * 4), 48000.0, false });
        src.push_back ({ "guitar", "Guitarra", tone (0.1f, 48000 * 4), 48000.0, true });
        src.push_back ({ "drums", "Batería", tone (0.2f, 48000 * 4), 48000.0, true });
        juce::CriticalSection cs;
        gp.load (std::move (src), 4.0, cs);
        auto block = [&]
        {
            juce::AudioBuffer<float> out (2, 256);
            out.clear();
            gp.render (out, 256);
            return out.getSample (0, 100);
        };
        gp.play();
        CHECK (std::abs (block() - 0.5f) < 1e-5f, "con todo prendido suena la mezcla original, no la suma");
        gp.setStemEnabled ("guitar", false);
        CHECK (std::abs (block() - 0.2f) < 1e-5f, "sin la guitarra suenan los otros stems");
        gp.setStemEnabled ("guitar", true);
        gp.setStemGain ("drums", 0.5f);
        CHECK (std::abs (block() - 0.2f) < 1e-5f, "con un instrumento más bajo, mezcla los stems (0.1 + 0.2·0.5)");
        gp.setStemGain ("drums", 1.0f);
        gp.setMasterGain (0.5f);
        CHECK (std::abs (block() - 0.25f) < 1e-5f, "el volumen general baja la mezcla original");
        gp.setMasterGain (1.0f);
        gp.seek (1.0);
        for (int i = 0; i < 187; ++i) block();     // ~1 s
        CHECK (std::abs (gp.positionSeconds() - 2.0) < 0.02, "avanza en tiempo real (" << gp.positionSeconds() << ")");
        gp.setSpeed (0.5f);
        const double before = gp.positionSeconds();
        for (int i = 0; i < 375; ++i) block();     // 2 s of output
        CHECK (std::abs ((gp.positionSeconds() - before) - 1.0) < 0.1, "a la mitad de velocidad, 2 s tocan 1 s de canción (" << gp.positionSeconds() - before << ")");
        gp.setSpeed (1.0f);
        gp.setLoop (true, 1.0, 2.0);
        for (int i = 0; i < 600; ++i) block();
        CHECK (gp.positionSeconds() >= 1.0 && gp.positionSeconds() < 2.0 && gp.isPlaying(), "el loop A-B se repite (" << gp.positionSeconds() << ")");
        gp.setLoop (false, 0, 0);
        gp.seek (3.9);
        for (int i = 0; i < 40; ++i) block();
        CHECK (gp.hasEnded() && ! gp.isPlaying(), "al final para y avisa");
    }

    // through the API, with a library folder
    {
        auto lib = tempRoot().getChildFile ("GalponJuego");
        setEnv ("GALPON_GAME_DIR", lib.getFullPathName());
        TestEngine t;
        auto r2 = t.api.call ("gameImport", juce::JSON::parse ("{\"paths\":[" + juce::JSON::toString (data.getChildFile ("extended.feedpak").getFullPathName()) + ","
                                                               + juce::JSON::toString (z1.getFullPathName()) + "]}"));
        CHECK ((bool) r2["ok"] && (int) r2["data"]["count"] == 2, "importar canciones a la biblioteca");
        auto libv = t.api.call ("gameLibrary", {});
        CHECK ((bool) libv["ok"] && libv["data"]["songs"].size() == 2, "la biblioteca las lista (" << libv["data"]["songs"].size() << ")");
        auto opened = t.api.call ("gameOpen", juce::JSON::parse ("{\"path\":" + juce::JSON::toString (lib.getChildFile ("Juego/extended.feedpak").getFullPathName()) + "}"));
        CHECK ((bool) opened["ok"] && opened["data"]["stems"].size() == 5, "abrir una canción para tocar: " << opened["error"].toString());
        CHECK ((bool) t.api.call ("gamePlay", {})["ok"] && t.engine.game.isPlaying(), "tocar");
        auto tick = t.api.call ("getTick", {});
        CHECK (tick["data"]["game"].isObject(), "el reloj del juego llega a la interfaz");
        auto g = t.api.call ("gameGuitar", juce::JSON::parse ("{\"create\":true}"));
        auto* gt = t.engine.song.findTrack ((int) g["data"]["trackId"]);
        CHECK (gt != nullptr && gt->monitor && gt->inserts.size() == 4, "pista para tu guitarra con su sonido");
        // the song's own sound, per part
        const int tracksBefore = (int) t.engine.song.tracks.size();
        auto tone = t.api.call ("gameTone", juce::JSON::parse ("{\"on\":true,\"arrangement\":\"lead\"}"));
        CHECK ((bool) tone["ok"], "sonido automático: " << tone["error"].toString());
        auto* st = t.engine.song.findTrack ((int) tone["data"]["trackId"]);
        CHECK (st != nullptr && st->monitor && ! gt->monitor && (int) t.engine.song.tracks.size() == tracksBefore + 1,
               "suena por una pista aparte y tu pista queda como estaba");
        CHECK (tone["data"]["tones"].size() == 2 && tone["data"]["tones"][1]["name"].toString() == "Lead Drive", "arma un sonido por cada tono de la canción");
        int active = 0;
        if (st != nullptr) for (auto& sl : st->inserts) active += sl.bypass ? 0 : 1;
        CHECK (st != nullptr && st->inserts.size() > 4 && active < (int) st->inserts.size(), "solo el sonido de la parte actual está prendido");
        // a sound picked by hand for a tone: Galpon's high-gain preset (boost + amp)
        {
            const auto key = tone["data"]["tones"][1]["key"].toString();
            auto obj = std::make_unique<juce::DynamicObject>();
            auto* pre = new juce::DynamicObject(); pre->setProperty ("preset", "highgain");
            auto* assign = new juce::DynamicObject(); assign->setProperty (key, juce::var (pre));
            obj->setProperty ("on", true); obj->setProperty ("arrangement", "lead"); obj->setProperty ("assign", juce::var (assign));
            auto picked = t.api.call ("gameTone", juce::var (obj.release()));
            auto* pt = t.engine.song.findTrack ((int) picked["data"]["trackId"]);
            juce::StringArray kinds;
            if (pt != nullptr) for (auto& sl : pt->inserts) if (auto* b = dynamic_cast<BuiltinProcessor*> (sl.processor.get())) kinds.add (b->getTypeId());
            CHECK ((bool) picked["ok"] && picked["data"]["tones"][1]["chain"].toString().containsIgnoreCase ("distors"),
                   "se puede elegir un sonido de Galpon para una parte (" << picked["data"]["tones"][1]["chain"].toString() << ")");
        }
        t.api.call ("gameTone", juce::JSON::parse ("{\"on\":false}"));
        // Rigs written like the ones converted from Rocksmith: gear by name, knobs out of 100
        {
            auto dir = tempRoot().getChildFile ("rs-style.feedpak");
            dir.deleteRecursively();
            data.getChildFile ("extended.feedpak").copyDirectoryTo (dir);
            dir.getChildFile ("rigs.json").replaceWithText (R"({ "version": 1, "rigs": [
              { "id": "clean-rhythm", "name": "x_clean", "instrument": "guitar", "blocks": [
                { "id": "amp", "role": "amp", "name": "Amp_MarshallDSL100H", "params": { "Gain": 1.0, "Bass": 75.0, "Mid": 38.0, "Treble": 66.0 } },
                { "id": "cab", "role": "cab", "name": "Cab_Marshall1960a_57_Edge", "params": {} } ] },
              { "id": "lead-drive", "name": "x_dist", "instrument": "guitar", "blocks": [
                { "id": "eq", "role": "filter", "name": "Pedal_EQ5", "params": { "Pedal_EQ5_63": -3.0, "Pedal_EQ5_250": -2.0, "Pedal_EQ5_750": 2.0, "Pedal_EQ5_2200": 1.0, "Pedal_EQ5_5700": 0.0 } },
                { "id": "amp", "role": "amp", "name": "Amp_MarshallJCM800", "params": { "Gain": 35.0, "Bass": 100.0, "Mid": 100.0, "Treble": 100.0 } },
                { "id": "dly", "role": "delay", "name": "Pedal_ValveEcho", "params": { "Time": 450.0, "Feedback": 35.0, "Mix": 35.0 } } ] } ] })");
            auto op = t.api.call ("gameOpen", juce::JSON::parse ("{\"path\":" + juce::JSON::toString (dir.getFullPathName()) + "}"));
            auto rs = t.api.call ("gameTone", juce::JSON::parse ("{\"on\":true,\"arrangement\":\"lead\"}"));
            auto* rt = t.engine.song.findTrack ((int) rs["data"]["trackId"]);
            std::vector<float> gains; float delayMs = 0;
            if (rt != nullptr)
                for (auto& sl : rt->inserts)
                    if (auto* b = dynamic_cast<BuiltinProcessor*> (sl.processor.get()))
                    {
                        if (b->getTypeId() == "amp") gains.push_back (b->get ("gain"));
                        if (b->getTypeId() == "delay") delayMs = b->get ("ms");
                    }
            CHECK ((bool) op["ok"] && (bool) rs["ok"] && gains.size() == 2, "lee los equipos con nombres y perillas de 0 a 100: " << rs["error"].toString() << op["error"].toString());
            if (gains.size() == 2)
            {
                CHECK (gains[0] < 0.15f, "un DSL100H con la ganancia en 1 suena limpio (" << gains[0] << ")");
                CHECK (gains[1] > 0.5f && gains[1] < 0.8f, "un JCM800 en 35 ya es rock (" << gains[1] << ")");
            }
            CHECK (std::abs (delayMs - 450.0f) < 1.0f, "el eco de la canción con su tiempo (" << delayMs << " ms)");
            CHECK (rs["data"]["tones"][1]["chain"].toString().contains ("JCM800"), "dice qué equipo usa (" << rs["data"]["tones"][1]["chain"].toString() << ")");
            t.api.call ("gameTone", juce::JSON::parse ("{\"on\":false}"));
        }
        CHECK ((int) t.engine.song.tracks.size() == tracksBefore && t.engine.song.findTrack (gt->id)->monitor, "apagarlo deja todo como antes");
        CHECK ((bool) t.api.call ("gameStemVolume", juce::JSON::parse ("{\"id\":\"drums\",\"gain\":0.5}"))["ok"]
               && (bool) t.api.call ("gameVolume", juce::JSON::parse ("{\"gain\":0.8}"))["ok"], "volumen de cada instrumento y de la canción");
        auto notes = t.api.call ("gameNotes", juce::JSON::parse ("{\"events\":[{\"t\":0.5,\"p\":[40]},{\"t\":1.0,\"p\":[45,52],\"sus\":0.5}],\"cents\":0}"));
        CHECK ((bool) notes["ok"] && (bool) notes["data"]["listening"], "escucha tu guitarra por la entrada de su pista");
        CHECK ((bool) t.api.call ("gameClose", {})["ok"] && ! t.engine.game.isLoaded(), "cerrar");
    }
}

/** Clicking around like a person (fast) while the audio keeps running on another thread.
    Catches crashes, deadlocks and garbage audio. GALPON_FUZZ=N for a longer run. */
static void testStress()
{
    std::cout << "Estrés: comandos al azar con el audio andando\n";
    TestEngine t;
    t.engine.song.limiter = true;
    writeClick (t.engine.getAudioFolder().getChildFile ("c.wav"), 2000, 0.5f, 96000);
    std::atomic<bool> run { true };
    std::atomic<int> blocks { 0 }, badBlocks { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> in (2, 256), out (2, 256);
        juce::Random r (1);
        while (run)
        {
            for (int i = 0; i < 256; ++i) { in.setSample (0, i, r.nextFloat() * 0.2f - 0.1f); in.setSample (1, i, in.getSample (0, i)); }
            t.engine.renderBlock (out.getArrayOfWritePointers(), 2, in.getArrayOfReadPointers(), 2, 256);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    if (! std::isfinite (out.getSample (ch, i)) || std::abs (out.getSample (ch, i)) > 4.0f) { ++badBlocks; ch = 2; break; }
            ++blocks;
            std::this_thread::sleep_for (std::chrono::microseconds (300));
        }
    });

    juce::Random rng (42);
    auto ids = [&] (bool clips)
    {
        std::vector<std::pair<int, int>> v;
        for (auto& tr : t.engine.song.tracks)
        {
            if (! clips) v.push_back ({ tr->id, 0 });
            else for (auto& c : tr->clips) v.push_back ({ tr->id, c.id });
        }
        return v;
    };
    auto pick = [&] (const std::vector<std::pair<int, int>>& v) { return v.empty() ? std::pair<int, int> { 0, 0 } : v[(size_t) rng.nextInt ((int) v.size())]; };
    auto J = [] (const juce::String& s) { return juce::JSON::parse (s); };
    int iterations = juce::SystemStats::getEnvironmentVariable ("GALPON_FUZZ", "400").getIntValue();
    int errorsRaised = 0;
    const char* kinds[] = { "audio", "instrument", "drummer", "bus" };
    const char* fx[] = { "eq", "comp", "gate", "drive", "amp", "cab", "chorus", "delay", "reverb", "nam" };
    for (int i = 0; i < iterations; ++i)
    {
        const auto tr = pick (ids (false));
        const auto cl = pick (ids (true));
        const juce::String T (tr.first), C (cl.second), TC (cl.first);
        juce::var r;
        switch (rng.nextInt (26))
        {
            case 0: r = t.api.call ("addTrack", J (juce::String ("{\"kind\":\"") + kinds[rng.nextInt (4)] + "\"}")); break;
            case 1: if (t.engine.song.tracks.size() > 3) r = t.api.call ("deleteTrack", J ("{\"trackId\":" + T + "}")); break;
            case 2: r = t.api.call ("addInsert", J ("{\"trackId\":" + T + ",\"kind\":\"builtin\",\"id\":\"" + fx[rng.nextInt (10)] + "\"}")); break;
            case 3: r = t.api.call ("removeInsert", J ("{\"trackId\":" + T + ",\"slot\":0}")); break;
            case 4: r = t.api.call ("setTrack", J ("{\"trackId\":" + T + ",\"volumeDb\":" + juce::String (rng.nextFloat() * 20 - 30) + ",\"pan\":" + juce::String (rng.nextFloat() * 2 - 1) + "}")); break;
            case 5: r = t.api.call ("setTrack", J ("{\"trackId\":" + T + ",\"mute\":" + (rng.nextBool() ? "true" : "false") + ",\"solo\":" + (rng.nextBool() ? "true" : "false") + "}")); break;
            case 6: r = t.api.call ("togglePlay", {}); break;
            case 7: r = t.api.call ("setPosition", J ("{\"beat\":" + juce::String (rng.nextFloat() * 32) + "}")); break;
            case 8: r = t.api.call ("undo", {}); break;
            case 9: r = t.api.call ("redo", {}); break;
            case 10: r = t.api.call ("setSong", J ("{\"bpm\":" + juce::String (60 + rng.nextInt (120)) + "}")); break;
            case 11: r = t.api.call ("addClip", J ("{\"trackId\":" + T + ",\"start\":" + juce::String (rng.nextInt (16)) + ",\"length\":4}")); break;
            case 12: r = t.api.call ("moveClip", J ("{\"trackId\":" + TC + ",\"clipId\":" + C + ",\"start\":" + juce::String (rng.nextFloat() * 20) + "}")); break;
            case 13: r = t.api.call ("splitClip", J ("{\"trackId\":" + TC + ",\"clipId\":" + C + ",\"beat\":" + juce::String (rng.nextFloat() * 20) + "}")); break;
            case 14: r = t.api.call ("duplicateClip", J ("{\"trackId\":" + TC + ",\"clipId\":" + C + "}")); break;
            case 15: r = t.api.call ("deleteClip", J ("{\"trackId\":" + TC + ",\"clipId\":" + C + "}")); break;
            case 16: { auto b = pick (ids (false)); r = t.api.call ("setSend", J ("{\"trackId\":" + T + ",\"busId\":" + juce::String (b.first) + ",\"levelDb\":-6}")); break; }
            case 17: { auto b = pick (ids (false)); r = t.api.call ("setTrack", J ("{\"trackId\":" + T + ",\"output\":" + juce::String (b.first) + "}")); break; }
            case 18: r = t.api.call ("record", {}); break;
            case 19: r = t.api.call ("setTrack", J ("{\"trackId\":" + T + ",\"armed\":" + (rng.nextBool() ? "true" : "false") + "}")); break;
            case 20: r = t.api.call ("importAudio", J ("{\"trackId\":" + T + ",\"path\":" + juce::JSON::toString (t.engine.getAudioFolder().getChildFile ("c.wav").getFullPathName()) + ",\"beat\":" + juce::String (rng.nextInt (16)) + "}")); break;
            case 21: r = t.api.call ("addLoop", J ("{\"id\":\"keys-pop\",\"beat\":" + juce::String (rng.nextInt (16)) + "}")); break;
            case 22: r = t.api.call ("setSong", J (juce::String ("{\"cycle\":") + (rng.nextBool() ? "true" : "false") + ",\"cycleStart\":0,\"cycleEnd\":8}")); break;
            case 23: r = t.api.call ("moveTrack", J ("{\"trackId\":" + T + ",\"index\":" + juce::String (rng.nextInt (4)) + "}")); break;
            case 24: r = t.api.call ("getState", {}); break;
            case 25: r = t.api.call ("getTick", {}); break;
        }
        if (r.isObject() && ! (bool) r["ok"]) ++errorsRaised;
        if (i % 50 == 0) t.api.call ("getState", {});
        std::this_thread::sleep_for (std::chrono::microseconds (500));
    }
    if (t.engine.isRecording()) t.engine.stop();
    run = false;
    audio.join();
    CHECK (blocks > 100, "el audio siguió andando (" << blocks.load() << " bloques)");
    CHECK (badBlocks == 0, "nunca salió ruido roto ni valores inválidos (" << badBlocks.load() << " bloques malos)");
    CHECK (t.engine.saveSong().wasOk() && t.engine.loadSong (t.engine.songFolder).wasOk(), "la canción se guarda y abre después del lío");
    std::cout << "  " << iterations << " acciones, " << errorsRaised << " rechazadas con aviso\n";
}

/** Game mode with the audio running: open and close songs, play, jump, change speed, loop,
    listen, switch sounds, all at random, as fast as a person never would. */
static void testGameStress()
{
    std::cout << "Estrés del juego: abrir canciones y tocar con el audio andando\n";
    auto lib = tempRoot().getChildFile ("JuegoEstres");
    lib.deleteRecursively();
    setEnv ("GALPON_GAME_DIR", lib.getFullPathName());
    TestEngine t;
    const auto data = juce::File (GALPON_TEST_DATA_DIR);
    // a zipped copy of each example as well
    juce::StringArray packs;
    for (auto name : { "extended.feedpak", "minimal.feedpak" })
    {
        auto dir = data.getChildFile (name);
        packs.add (dir.getFullPathName());
        juce::ZipFile::Builder b;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
            b.addFile (f, 6, f.getRelativePathFrom (dir).replaceCharacter ('\\', '/'));
        auto z = tempRoot().getChildFile (juce::String ("z-") + name);
        z.deleteFile();
        juce::FileOutputStream os (z);
        b.writeToStream (os, nullptr);
        packs.add (z.getFullPathName());
    }
    std::atomic<bool> run { true };
    std::atomic<int> blocks { 0 }, badBlocks { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> in (2, 256), out (2, 256);
        juce::Random r (3);
        int k = 0;
        while (run)
        {
            for (int i = 0; i < 256; ++i, ++k) { const float v = 0.2f * std::sin (k * 0.0367f) + (r.nextFloat() - 0.5f) * 0.01f; in.setSample (0, i, v); in.setSample (1, i, v); }
            t.engine.renderBlock (out.getArrayOfWritePointers(), 2, in.getArrayOfReadPointers(), 2, 256);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    if (! std::isfinite (out.getSample (ch, i)) || std::abs (out.getSample (ch, i)) > 8.0f) { ++badBlocks; ch = 2; break; }
            ++blocks;
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });
    auto J = [] (const juce::String& x) { return juce::JSON::parse (x); };
    juce::Random rng (7);
    const int iterations = juce::SystemStats::getEnvironmentVariable ("GALPON_FUZZ", "400").getIntValue();
    int opened = 0;
    for (int i = 0; i < iterations; ++i)
    {
        juce::var r;
        switch (rng.nextInt (18))
        {
            case 0: case 1:
                r = t.api.call ("gameClose", {});
                r = t.api.call ("gameOpen", J ("{\"path\":" + juce::JSON::toString (packs[rng.nextInt (packs.size())]) + "}"));
                if ((bool) r["ok"]) ++opened;
                break;
            case 2: r = t.api.call ("gameOpen", J ("{\"path\":" + juce::JSON::toString (packs[rng.nextInt (packs.size())]) + "}")); break;   // without closing first
            case 3: r = t.api.call ("gameToggle", {}); break;
            case 4: r = t.api.call ("gameSeek", J ("{\"time\":" + juce::String (rng.nextFloat() * 6 - 1) + "}")); break;
            case 5: r = t.api.call ("gameSpeed", J ("{\"speed\":" + juce::String (0.2f + rng.nextFloat()) + "}")); break;
            case 6: r = t.api.call ("gameLoop", J ("{\"on\":" + juce::String (rng.nextBool() ? "true" : "false") + ",\"a\":" + juce::String (rng.nextFloat() * 3) + ",\"b\":" + juce::String (rng.nextFloat() * 5) + "}")); break;
            case 7: r = t.api.call ("gameStem", J (juce::String ("{\"id\":\"") + (rng.nextBool() ? "guitar" : "full") + "\",\"on\":" + (rng.nextBool() ? "true" : "false") + "}")); break;
            case 8: r = t.api.call ("gameStemVolume", J ("{\"id\":\"drums\",\"gain\":" + juce::String (rng.nextFloat() * 1.5f) + "}")); break;
            case 9: r = t.api.call ("gameGuitar", J (juce::String ("{\"create\":") + (rng.nextBool() ? "true" : "false") + "}")); break;
            case 10: r = t.api.call ("gameNotes", J ("{\"events\":[{\"t\":0.5,\"p\":[40]},{\"t\":1.0,\"p\":[45,52],\"sus\":0.6},{\"t\":1.5,\"p\":[47],\"l\":true},{\"t\":2.0,\"p\":[0, 200]},{\"t\":2.2,\"p\":[]}],\"cents\":-20}")); break;
            case 11: r = t.api.call ("gameTone", J (juce::String ("{\"on\":") + (rng.nextInt (3) ? "true" : "false") + ",\"arrangement\":\"" + (rng.nextBool() ? "lead" : "bass") + "\"}")); break;
            case 12: r = t.api.call ("getTick", {}); break;
            case 13: r = t.api.call ("gameDetectOffset", J ("{\"offsetMs\":" + juce::String (rng.nextInt (300) - 100) + "}")); break;
            case 14: r = t.api.call ("togglePlay", {}); break;           // the studio's play while the game is open
            case 15: r = t.api.call ("gameVolume", J ("{\"gain\":" + juce::String (rng.nextFloat() * 1.5f) + "}")); break;
            case 16: r = t.api.call ("gameLibrary", {}); break;
            case 17: if (rng.nextInt (6) == 0) { const juce::ScopedLock sl (t.engine.lock); t.engine.game.prepare (rng.nextBool() ? 44100.0 : 48000.0, 256); } break;
        }
        if (i % 7 == 0) t.api.call ("getTick", {});
        std::this_thread::sleep_for (std::chrono::milliseconds (rng.nextInt (4)));
    }
    t.api.call ("gameClose", {});
    run = false;
    audio.join();
    CHECK (opened > 10, "abrió canciones muchas veces (" << opened << ")");
    CHECK (blocks > 100 && badBlocks == 0, "el audio siguió sano (" << blocks.load() << " bloques, " << badBlocks.load() << " malos)");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // never use the instruments downloaded on this computer (e.g. the real drum kit): tests must give the same result anywhere
    setEnv ("GALPON_SOUNDS_DIR", tempRoot().getChildFile ("SonidosVacio").getFullPathName());
    if (auto demo = juce::SystemStats::getEnvironmentVariable ("GALPON_DEMO_OUT", {}); demo.isNotEmpty())
        renderDemo (demo);
    testBuiltins();
    testPlayback();
    testDrummer();
    testDrummerParts();
    testDrummerArrangement();
    testDrummerStyles();
    testRealDrums();
    testGuitarTone();
    testRetuneAndPresets();
    testTunerPitch();
    testGuitarGate();
    renderChainForAnalysis();
    testRecording();
    testRecordingSafety();
    testSaveLoadUndoExport();
    testImports();
    testTuner();
    testNam();
    testInputGain();
    testMidiControllers();
    testCycleTakes();
    testDelayCompensationAndLimiter();
    testUndoTracks();
    testExportFormats();
    testSendsAndBuses();
    testChords();
    testSampler();
    testFlex();
    testStems();
    testAutomation();
    testFeedpak();
    testYamlCorpus();
    testNoteDetection();
    testStress();
    testGameStress();
    std::cout << "\n" << (checks - failures) << " de " << checks << " pruebas OK\n";
    return failures == 0 ? 0 : 1;
}
