#include "ToneLibrary.h"

namespace galpon
{
namespace
{
    // Amp knobs as players write them (0-10, 5 = flat) to Galpon's EQ in dB
    float k (float v) { return (v - 5.0f) * 2.4f; }

    using S = TonePreset::Step;
    S gate (float threshold = -60.0f)   { return { "gate", { { "threshold", threshold } } }; }
    S amp (float gain, float bass, float mid, float treble, float master = -8.0f)
    { return { "amp", { { "gain", gain }, { "bass", k (bass) }, { "mid", k (mid) }, { "treble", k (treble) }, { "cab", 1.0f }, { "master", master } } }; }
    S boost (float drive, float tone = 3800.0f, float level = 4.0f) { return { "drive", { { "drive", drive }, { "tone", tone }, { "level", level }, { "mix", 1.0f } } }; }
    S fuzz (float drive, float tone = 2500.0f) { return { "drive", { { "drive", drive }, { "tone", tone }, { "level", 0.0f }, { "mix", 1.0f } } }; }
    S comp (float threshold = -24.0f, float ratio = 4.0f) { return { "comp", { { "threshold", threshold }, { "ratio", ratio }, { "attack", 8.0f }, { "release", 150.0f }, { "makeup", 4.0f } } }; }
    S chorus (float mix = 0.4f, float depth = 0.35f, float rate = 0.6f) { return { "chorus", { { "rate", rate }, { "depth", depth }, { "mix", mix } } }; }
    S delay (float ms, float feedback, float mix) { return { "delay", { { "division", 0.0f }, { "ms", ms }, { "feedback", feedback }, { "mix", mix } } }; }
    S reverb (float mix, float size = 0.5f) { return { "reverb", { { "size", size }, { "mix", mix } } }; }
    S eq (float lowcut, float low, float mid, float high) { return { "eq", { { "lowcut", lowcut }, { "low", low }, { "mid", mid }, { "high", high } } }; }
}

const std::vector<TonePreset>& tonePresets()
{
    static const std::vector<TonePreset> list = []
    {
        std::vector<TonePreset> v;
        auto add = [&v] (const char* id, const char* name, const char* group, const char* desc, float drive, std::vector<S> chain, bool lead = false, bool bass = false)
        {
            TonePreset p;
            p.id = id; p.name = juce::String::fromUTF8 (name); p.group = juce::String::fromUTF8 (group); p.description = juce::String::fromUTF8 (desc);
            p.drive = drive; p.chain = std::move (chain); p.lead = lead; p.bass = bass;
            v.push_back (std::move (p));
        };
        // ---- Clean
        add ("clean", "Limpio", "Limpios", "Un amplificador americano limpio, con algo de reverb", 0.05f,
             { amp (0.08f, 5, 6, 7), reverb (0.2f, 0.45f) });
        add ("chorus", "Limpio con chorus", "Limpios", "Brillante y ancho, de los 80 y 90", 0.05f,
             { amp (0.08f, 5, 6, 7), chorus (0.45f), reverb (0.22f, 0.5f) });
        add ("ambient", "Limpio con delay", "Limpios", "Arpegios con eco y mucho espacio", 0.08f,
             { comp (-26.0f, 3.0f), amp (0.1f, 4, 6, 7), delay (380.0f, 0.4f, 0.28f), reverb (0.3f, 0.7f) });
        add ("funk", "Funk", "Limpios", "Seco, brillante y comprimido, para rasguear con púa", 0.04f,
             { comp (-28.0f, 5.0f), amp (0.06f, 4, 6, 8) });
        add ("jazz", "Jazz", "Limpios", "Cálido y redondo, sin agudos", 0.03f,
             { amp (0.06f, 6, 5, 3), reverb (0.15f, 0.4f) });
        add ("acoustic", "Acústico", "Limpios", "Lo más parecido a una guitarra acústica", 0.02f,
             { eq (90.0f, -3.0f, -4.0f, 5.0f), comp (-22.0f, 3.0f), amp (0.03f, 4, 5, 8), chorus (0.2f, 0.15f, 0.4f), reverb (0.25f, 0.4f) });
        // ---- Crunch
        add ("crunch", "Crunch", "Crunch", "Rock clásico: el amplificador apenas saturado", 0.38f,
             { amp (0.42f, 5, 8, 5), reverb (0.1f, 0.3f) });
        add ("british", "Crunch británico", "Crunch", "Brillante y con cuerpo, tipo AC30 (Beatles, Oasis, indie)", 0.32f,
             { amp (0.36f, 4, 7, 7), reverb (0.12f, 0.35f) });
        add ("blues", "Blues", "Crunch", "Saturación suave que limpia al tocar despacio", 0.3f,
             { boost (0.15f, 3200.0f, 3.0f), amp (0.3f, 6, 7, 5), reverb (0.18f, 0.45f) });
        // ---- Distortion
        add ("hardrock", "Hard rock", "Distorsión", "Un JCM800 con el volumen arriba (AC/DC, Guns, Audioslave)", 0.62f,
             { gate (-62.0f), amp (0.66f, 6, 5, 6), reverb (0.08f, 0.3f) });
        add ("grunge", "Grunge", "Distorsión", "Sucio, grueso y con medios (Nirvana, Pearl Jam)", 0.7f,
             { gate (-60.0f), fuzz (0.55f, 2600.0f), amp (0.5f, 7, 6, 5) });
        add ("punk", "Punk", "Distorsión", "Mucha distorsión, rasgueos rápidos", 0.72f,
             { gate (-58.0f), amp (0.78f, 6, 6, 7) });
        add ("stoner", "Fuzz", "Distorsión", "Un fuzz enorme y lento (Black Sabbath, Queens of the Stone Age)", 0.82f,
             { fuzz (0.85f, 2200.0f), amp (0.45f, 8, 6, 4) });
        // ---- High gain
        add ("highgain", "Alta ganancia", "Alta ganancia", "Metal: un boost delante de un amplificador saturado", 0.92f,
             { gate (-55.0f), boost (0.12f), amp (0.95f, 7, 4, 5) });
        add ("modernmetal", "Metal moderno", "Alta ganancia", "Ajustado y con los medios recortados (djent, metalcore)", 0.95f,
             { gate (-52.0f), boost (0.08f, 4200.0f, 6.0f), amp (1.0f, 6, 3, 6), eq (90.0f, 0.0f, -3.0f, 1.0f) });
        // ---- Lead
        add ("lead", "Solo", "Solos", "Un poco más de ganancia, delay y reverb", 0.7f,
             { gate (-60.0f), boost (0.25f, 3500.0f, 2.0f), amp (0.62f, 3, 7, 5), delay (380.0f, 0.28f, 0.2f), reverb (0.18f, 0.5f) }, true);
        add ("bluelead", "Solo blues", "Solos", "Cálido, cantado, con reverb", 0.45f,
             { boost (0.3f, 3000.0f, 3.0f), amp (0.42f, 5, 8, 4), reverb (0.25f, 0.55f) }, true);
        add ("bigsolo", "Solo con mucho eco", "Solos", "Para solos lentos y épicos (Pink Floyd, Slash)", 0.65f,
             { gate (-60.0f), boost (0.25f, 3400.0f, 2.0f), amp (0.6f, 4, 7, 5), delay (480.0f, 0.45f, 0.3f), reverb (0.3f, 0.75f) }, true);
        // ---- Bass
        add ("bass", "Bajo", "Bajo", "Limpio, con cuerpo", 0.05f,
             { comp (-24.0f, 4.0f), amp (0.12f, 7, 5, 4, -6.0f) }, false, true);
        add ("bassdrive", "Bajo saturado", "Bajo", "Con algo de distorsión, para rock", 0.5f,
             { comp (-22.0f, 4.0f), amp (0.45f, 7, 6, 5, -8.0f) }, false, true);
        return v;
    }();
    return list;
}

const TonePreset* findTonePreset (const juce::String& id)
{
    for (auto& p : tonePresets()) if (p.id == id) return &p;
    return nullptr;
}

const TonePreset& closestTonePreset (float heardDrive, const juce::String& toneName, bool bass)
{
    const auto n = toneName.toLowerCase();
    auto says = [&n] (std::initializer_list<const char*> words) { for (auto* w : words) if (n.contains (w)) return true; return false; };
    auto byId = [] (const char* id) -> const TonePreset& { return *findTonePreset (id); };

    if (bass) return heardDrive > 0.4f || says ({ "dist", "drive", "fuzz", "dirty" }) ? byId ("bassdrive") : byId ("bass");

    // What the name says, when it says something
    if (says ({ "acoustic", "acust", "acúst" })) return byId ("acoustic");
    const bool lead = says ({ "lead", "solo" });
    const bool clean = says ({ "clean", "limpi" });
    if (clean) return says ({ "chorus" }) ? byId ("chorus") : says ({ "delay", "ambient", "verb" }) ? byId ("ambient") : says ({ "funk" }) ? byId ("funk") : byId ("clean");
    if (says ({ "metal", "djent", "brutal" })) return byId ("modernmetal");
    if (says ({ "fuzz", "stoner" })) return byId ("stoner");
    if (says ({ "crunch" })) return byId ("crunch");

    // Otherwise by ear: the preset whose distortion is closest to what the song's guitar sounds like
    float d = heardDrive;
    if (d < 0.0f)
        d = says ({ "dist", "heavy", "gain", "drive", "riff" }) ? 0.75f : lead ? 0.65f : 0.5f;   // no guitar to listen to: rock crunch
    const TonePreset* best = nullptr;
    float bestDist = 1e9f;
    for (auto& p : tonePresets())
    {
        if (p.bass || p.lead != lead) continue;
        if (p.id == "funk" || p.id == "jazz" || p.id == "acoustic" || p.id == "ambient" || p.id == "stoner" || p.id == "grunge" || p.id == "british" || p.id == "blues") continue;   // only picked when asked for
        const float dist = std::abs (p.drive - d);
        if (dist < bestDist) { bestDist = dist; best = &p; }
    }
    return best != nullptr ? *best : byId ("crunch");
}

} // namespace galpon
