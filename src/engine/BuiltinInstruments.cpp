// Galpon's own instruments, all synthesised (no sample libraries needed):
// acoustic-style piano, electric piano, organ, analog-style synth (also bass and pads), drum kit.
// Also the registry of every built-in type and the sound library presets.

#include "Builtins.h"

namespace galpon
{

//==============================================================================
// BuiltinProcessor
//==============================================================================
BuiltinProcessor::BuiltinProcessor (juce::String t, juce::String n, bool isInstrument)
    : juce::AudioProcessor (isInstrument
                              ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)
                              : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      typeId (std::move (t)), displayName (std::move (n)), instrument (isInstrument)
{
}

juce::AudioParameterFloat* BuiltinProcessor::addParam (const juce::String& id, const juce::String& name,
                                                       float min, float max, float def,
                                                       const juce::String& unit, float skewCentre)
{
    juce::NormalisableRange<float> range (min, max);
    if (skewCentre > min && skewCentre < max)
        range.setSkewForCentre (skewCentre);

    // Show values like a person would: "440 Hz", "0.35", "-3.0 dB" (not "0.3500000")
    const auto span = max - min;
    const int decimals = span >= 100.0f ? 0 : span >= 10.0f ? 1 : 2;
    auto attributes = juce::AudioParameterFloatAttributes()
                          .withLabel (unit)
                          .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
    auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attributes);
    auto* raw = p.get();
    addParameter (p.release());
    params[id] = raw;
    return raw;
}

float BuiltinProcessor::get (const juce::String& id) const
{
    auto it = params.find (id);
    return it != params.end() ? it->second->get() : 0.0f;
}

void BuiltinProcessor::setReal (const juce::String& id, float v)
{
    if (auto it = params.find (id); it != params.end())
        it->second->setValueNotifyingHost (it->second->convertTo0to1 (v));
}

juce::Result BuiltinProcessor::setExtra (const juce::String& key, const juce::var&)
{
    return juce::Result::fail (juce::String (u8"Opción desconocida: ") + key);
}

void BuiltinProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("GalponBuiltin");
    xml.setAttribute ("type", typeId);
    for (auto& [id, p] : params)
        xml.setAttribute ("p_" + id, (double) p->get());
    writeExtraState (xml);
    copyXmlToBinary (xml, dest);
}

void BuiltinProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        for (auto& [id, p] : params)
            if (xml->hasAttribute ("p_" + id))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) xml->getDoubleAttribute ("p_" + id)));
        readExtraState (*xml);
    }
}

//==============================================================================
// Registry
//==============================================================================
const std::vector<BuiltinInfo>& builtinTypes()
{
    static const std::vector<BuiltinInfo> types {
        { "piano",   "Piano",               "instrument", juce::String (u8"Piano acústico sintetizado") },
        { "epiano",  juce::String (u8"Piano eléctrico"),     "instrument", juce::String (u8"Sonido de piano eléctrico de los 70") },
        { "organ",   juce::String (u8"Órgano"),              "instrument", juce::String (u8"Órgano de barras con percusión") },
        { "synth",   "Sintetizador",        "instrument", juce::String (u8"Sinte analógico: leads, bajos y pads") },
        { "drums",   juce::String (u8"Batería"),             "instrument", juce::String (u8"Kit de batería sintetizado (mapa General MIDI)") },
        { "sampler", "Sampler",             "instrument", juce::String (u8"Instrumentos grabados de verdad: archivos .sfz o un sonido .wav") },
        { "eq",      "Ecualizador",         "effect", "Graves, medios, agudos y corte de graves" },
        { "comp",    "Compresor",           "effect", "Empareja el volumen" },
        { "gate",    "Puerta de ruido",     "effect", juce::String (u8"Corta el ruido cuando no tocas") },
        { "drive",   juce::String (u8"Distorsión"),          "effect", "Desde un poco de calor hasta fuzz" },
        { "amp",     "Amplificador",        "effect", "Amp de guitarra simple (sin modelos)" },
        { "nam",     "Amp NAM (Tone3000)",  "effect", "Carga capturas .nam de amplificadores y pedales" },
        { "cab",     "Gabinete (IR)",       "effect", "Carga respuestas de impulso .wav de gabinetes" },
        { "chorus",  "Chorus",              "effect", "Ensancha y da movimiento" },
        { "delay",   "Delay",               "effect", "Eco sincronizado al tempo" },
        { "reverb",  "Reverb",              "effect", "Ambiente de sala" },
        { "retune",  juce::String (u8"Afinación virtual"), "effect", juce::String (u8"Tu guitarra suena más grave o más aguda, sin reafinarla") },
    };
    return types;
}

const std::vector<BuiltinPreset>& builtinPresets()
{
    static const std::vector<BuiltinPreset> presets {
        { "piano",  "Piano de cola",      "Teclados", { { "bright", 0.55f }, { "decay", 1.0f } } },
        { "piano",  "Piano brillante",    "Teclados", { { "bright", 0.85f }, { "decay", 0.8f } } },
        { "epiano", juce::String (u8"Piano eléctrico"),    "Teclados", { { "bright", 0.5f }, { "trem", 0.25f }, { "warm", 0.3f } } },
        { "epiano", juce::String (u8"Piano eléctrico sucio"), "Teclados", { { "bright", 0.8f }, { "trem", 0.1f }, { "warm", 0.9f } } },
        { "organ",  juce::String (u8"Órgano"),             "Teclados", { { "low", 0.8f }, { "mid", 0.6f }, { "high", 0.3f }, { "perc", 0.5f } } },
        { "organ",  juce::String (u8"Órgano de iglesia"),  "Teclados", { { "low", 1.0f }, { "mid", 0.9f }, { "high", 0.8f }, { "perc", 0.0f }, { "vib", 0.1f } } },
        { "synth",  "Lead brillante",     "Sintes",   { { "shape", 0.0f }, { "cutoff", 6000.0f }, { "res", 0.3f }, { "envAmt", 0.4f }, { "attack", 0.005f }, { "release", 0.2f } } },
        { "synth",  "Pad nocturno",       "Sintes",   { { "shape", 0.2f }, { "detune", 18.0f }, { "cutoff", 1800.0f }, { "res", 0.15f }, { "attack", 0.8f }, { "release", 1.8f }, { "sustain", 0.9f } } },
        { "synth",  "Cuadrada retro",     "Sintes",   { { "shape", 1.0f }, { "cutoff", 4000.0f }, { "detune", 0.0f }, { "release", 0.15f } } },
        { "synth",  "Bajo sinte",         "Bajos",    { { "shape", 0.3f }, { "cutoff", 700.0f }, { "res", 0.35f }, { "envAmt", 0.6f }, { "sub", 0.7f }, { "decay", 0.3f }, { "sustain", 0.5f }, { "release", 0.08f }, { "detune", 4.0f } } },
        { "synth",  juce::String (u8"Bajo eléctrico"),     "Bajos",    { { "shape", 0.15f }, { "cutoff", 450.0f }, { "res", 0.1f }, { "envAmt", 0.5f }, { "sub", 0.9f }, { "decay", 0.6f }, { "sustain", 0.25f }, { "release", 0.06f }, { "detune", 0.0f } } },
        { "drums",  juce::String (u8"Batería acústica"),   juce::String (u8"Batería"),  { { "tune", 0.0f }, { "tone", 0.5f } } },
        { "drums",  juce::String (u8"Batería electrónica"),juce::String (u8"Batería"),  { { "tune", -3.0f }, { "tone", 0.85f } } },
    };
    return presets;
}

const BuiltinPreset* findPreset (const juce::String& name)
{
    for (auto& p : builtinPresets())
        if (p.name == name)
            return &p;
    return nullptr;
}

std::unique_ptr<juce::AudioProcessor> createBuiltin (const juce::String& type)
{
    if (type == "nam")
        return createNamAmp();
    if (type == "sampler")
        return createSampler();
    if (type == "retune")
        return createRetune();
    if (auto i = createBuiltinInstrument (type))
        return i;
    return createBuiltinEffect (type);
}

void applyPreset (juce::AudioProcessor& p, const BuiltinPreset& preset)
{
    if (auto* b = dynamic_cast<BuiltinProcessor*> (&p))
        for (auto& [id, value] : preset.params)
            b->setReal (id, value);
}

//==============================================================================
// Shared voice helpers
//==============================================================================
namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    struct AnySound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    inline float polyBlep (double t, double dt)
    {
        if (t < dt)        { t /= dt;             return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt)  { t = (t - 1.0) / dt;  return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    struct FastNoise
    {
        uint32_t s = 0x12345678u;
        float next() noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (int32_t) s * (1.0f / 2147483648.0f);
        }
    };

    class VoiceBase : public juce::SynthesiserVoice
    {
    public:
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void pitchWheelMoved (int value) override { bend = std::pow (2.0, (value - 8192) / 8192.0 * 2.0 / 12.0); }
        void controllerMoved (int, int) override {}

    protected:
        double bend = 1.0;
    };

    /** An instrument = juce::Synthesiser + a few global params + post processing. */
    class SynthInstrument : public BuiltinProcessor
    {
    public:
        using BuiltinProcessor::BuiltinProcessor;

        void prepareToPlay (double sr, int) override
        {
            synth.setCurrentPlaybackSampleRate (sr);
            sampleRate = sr;
            prepared (sr);
        }

        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            juce::ScopedNoDenormals noDenormals;
            buffer.clear();
            synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
            post (buffer);
            // -6 dB of headroom so a few tracks together don't clip the master
            buffer.applyGain (0.5f * juce::Decibels::decibelsToGain (get ("volume")));
        }

    protected:
        virtual void prepared (double) {}
        virtual void post (juce::AudioBuffer<float>&) {}

        void addVoices (int count, std::function<juce::SynthesiserVoice*()> make)
        {
            for (int i = 0; i < count; ++i)
                synth.addVoice (make());
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
        }

        juce::Synthesiser synth;
        double sampleRate = 44100.0;
    };

    //==========================================================================
    // Piano: additive strings with inharmonicity, two slightly detuned strings per note
    //==========================================================================
    class PianoInstrument;

    class PianoVoice : public VoiceBase
    {
    public:
        explicit PianoVoice (BuiltinProcessor& p) : owner (p) {}

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            vel = velocity;
            t = 0.0; releasing = false; releaseGain = 1.0f;
            const auto bright = owner.get ("bright");
            const double baseDecay = owner.get ("decay") * juce::jlimit (0.6, 9.0, 9.0 * std::pow (0.5, (note - 21) / 18.0));
            const double B = 0.00012 * std::pow (2.0, (note - 60) / 12.0);
            const double tilt = 0.45 + 0.35 * velocity * (0.5 + bright);

            for (int k = 0; k < numPartials; ++k)
            {
                const int h = k + 1;
                const double f = freq * h * std::sqrt (1.0 + B * h * h);
                partials[k].inc  = f / getSampleRate();
                partials[k].inc2 = partials[k].inc * 1.0007;
                partials[k].phase = partials[k].phase2 = 0.0;
                partials[k].amp   = (f < getSampleRate() * 0.45) ? (float) (std::pow (tilt, k) / std::pow (h, 0.8)) : 0.0f;
                partials[k].decayPerSample = (float) std::exp (-1.0 / ((baseDecay / (1.0 + 0.45 * k)) * getSampleRate()));
                partials[k].env = 1.0f;
            }
            hammer = 0.15f * velocity;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) releasing = true;
            else              clearCurrentNote();
        }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const float releaseMul = (float) std::exp (-1.0 / (0.18 * getSampleRate()));
            auto* l = out.getWritePointer (0, start);
            auto* r = out.getWritePointer (1, start);
            const float pan = juce::jlimit (0.2f, 0.8f, (float) ((getCurrentlyPlayingNote() - 21) / 87.0));

            for (int i = 0; i < num; ++i)
            {
                float s = 0.0f;
                for (int k = 0; k < numPartials; ++k)
                {
                    auto& p = partials[k];
                    if (p.amp <= 0.0f) continue;
                    s += p.amp * p.env * (float) (std::sin (p.phase * twoPi) + std::sin (p.phase2 * twoPi)) * 0.5f;
                    p.phase  += p.inc * bend;  if (p.phase  >= 1.0) p.phase  -= 1.0;
                    p.phase2 += p.inc2 * bend; if (p.phase2 >= 1.0) p.phase2 -= 1.0;
                    p.env *= p.decayPerSample;
                }
                s += hammer * noise.next();
                hammer *= 0.995f;

                if (releasing) releaseGain *= releaseMul;
                s *= vel * releaseGain * 0.35f;
                l[i] += s * (1.0f - pan) * 1.4f;
                r[i] += s * pan * 1.4f;

                if (releaseGain < 0.0005f || partials[0].env < 0.0005f)
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

    private:
        static constexpr int numPartials = 10;
        struct Partial { double phase = 0, phase2 = 0, inc = 0, inc2 = 0; float amp = 0, env = 0, decayPerSample = 1; };
        BuiltinProcessor& owner;
        Partial partials[numPartials];
        double freq = 440.0, t = 0.0;
        float vel = 0.0f, releaseGain = 1.0f, hammer = 0.0f;
        bool releasing = false;
        FastNoise noise;
    };

    class PianoInstrument : public SynthInstrument
    {
    public:
        PianoInstrument() : SynthInstrument ("piano", "Piano", true)
        {
            addParam ("bright", "Brillo", 0.0f, 1.0f, 0.55f);
            addParam ("decay", juce::String (u8"Duración"), 0.3f, 2.0f, 1.0f, "x");
            addParam ("volume", "Volumen", -24.0f, 6.0f, 0.0f, "dB");
            addVoices (16, [this] { return new PianoVoice (*this); });
        }
    };

    //==========================================================================
    // Electric piano: 2-operator FM with a "tine" transient, stereo tremolo
    //==========================================================================
    class EPianoVoice : public VoiceBase
    {
    public:
        explicit EPianoVoice (BuiltinProcessor& p) : owner (p) {}

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            vel = velocity; t = 0.0; pc = pm = pt = 0.0;
            releasing = false; releaseGain = 1.0f;
            decay = juce::jlimit (0.4, 6.0, 4.5 * std::pow (0.5, (note - 36) / 24.0));
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) releasing = true; else clearCurrentNote();
        }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const double sr = getSampleRate();
            const float bright = owner.get ("bright");
            const float warm = owner.get ("warm");
            const float releaseMul = (float) std::exp (-1.0 / (0.12 * sr));
            auto* l = out.getWritePointer (0, start);
            auto* r = out.getWritePointer (1, start);

            for (int i = 0; i < num; ++i)
            {
                const double amp = std::exp (-t / decay);
                const double index = (0.4 + 2.4 * bright * vel) * std::exp (-t / 0.4) + 0.25 * bright;
                const double mod = std::sin (pm * twoPi);
                double s = std::sin (pc * twoPi + index * mod) * amp;
                s += std::sin (pt * twoPi) * std::exp (-t / 0.04) * 0.2 * bright * vel;

                const double drive = 1.0 + 4.0 * warm;
                s = std::tanh (s * drive) / std::tanh (drive);

                pc += freq * bend / sr;        if (pc >= 1.0) pc -= 1.0;
                pm += freq * bend / sr;        if (pm >= 1.0) pm -= 1.0;
                pt += freq * 14.0 * bend / sr; if (pt >= 1.0) pt -= 1.0;
                t += 1.0 / sr;

                if (releasing) releaseGain *= releaseMul;
                const float v = (float) s * vel * releaseGain * 0.3f;
                l[i] += v; r[i] += v;

                if (releaseGain < 0.0005f || amp < 0.0005)
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

    private:
        BuiltinProcessor& owner;
        double freq = 440.0, t = 0.0, pc = 0, pm = 0, pt = 0, decay = 2.0;
        float vel = 0, releaseGain = 1;
        bool releasing = false;
    };

    class EPianoInstrument : public SynthInstrument
    {
    public:
        EPianoInstrument() : SynthInstrument ("epiano", juce::String (u8"Piano eléctrico"), true)
        {
            addParam ("bright", "Brillo", 0.0f, 1.0f, 0.5f);
            addParam ("warm", "Calidez", 0.0f, 1.0f, 0.3f);
            addParam ("trem", juce::String (u8"Trémolo"), 0.0f, 1.0f, 0.25f);
            addParam ("tremRate", juce::String (u8"Velocidad trémolo"), 0.5f, 10.0f, 4.5f, "Hz", 3.0f);
            addParam ("volume", "Volumen", -24.0f, 6.0f, 0.0f, "dB");
            addVoices (16, [this] { return new EPianoVoice (*this); });
        }

    protected:
        void post (juce::AudioBuffer<float>& b) override
        {
            const float depth = get ("trem");
            if (depth <= 0.001f) return;
            const double inc = get ("tremRate") / sampleRate;
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float lfo = 0.5f + 0.5f * (float) std::sin (lfoPhase * twoPi);
                l[i] *= 1.0f - depth * lfo;
                r[i] *= 1.0f - depth * (1.0f - lfo);
                lfoPhase += inc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            }
        }

    private:
        double lfoPhase = 0.0;
    };

    //==========================================================================
    // Organ: drawbar-style additive + percussion + chorus/vibrato
    //==========================================================================
    class OrganVoice : public VoiceBase
    {
    public:
        explicit OrganVoice (BuiltinProcessor& p) : owner (p) {}

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            juce::ignoreUnused (velocity);
            for (auto& ph : phases) ph = 0.0;
            percEnv = 1.0f; attack = 0.0f; releasing = false; releaseGain = 1.0f; click = 0.25f;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) releasing = true; else clearCurrentNote();
        }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            static constexpr double ratios[] = { 0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 };
            const float low = owner.get ("low"), mid = owner.get ("mid"), high = owner.get ("high"), perc = owner.get ("perc");
            const float gains[] = { low, low, mid, mid * 0.8f, high, high * 0.7f, high * 0.6f };
            const double sr = getSampleRate();
            const float releaseMul = (float) std::exp (-1.0 / (0.03 * sr));
            const float percMul = (float) std::exp (-1.0 / (0.25 * sr));
            const float attackInc = (float) (1.0 / (0.004 * sr));
            auto* l = out.getWritePointer (0, start);
            auto* r = out.getWritePointer (1, start);

            for (int i = 0; i < num; ++i)
            {
                double s = 0.0;
                for (int k = 0; k < 7; ++k)
                {
                    const double f = freq * ratios[k] * bend;
                    if (f < sr * 0.45)
                        s += gains[k] * std::sin (phases[k] * twoPi);
                    phases[k] += f / sr; if (phases[k] >= 1.0) phases[k] -= 1.0;
                }
                s += perc * percEnv * std::sin (phases[3] * twoPi) * 1.2;
                s += click * noise.next() * 0.3;
                click *= 0.97f;
                percEnv *= percMul;
                attack = juce::jmin (1.0f, attack + attackInc);
                if (releasing) releaseGain *= releaseMul;

                const float v = (float) s * attack * releaseGain * 0.12f;
                l[i] += v; r[i] += v;

                if (releaseGain < 0.0005f) { clearCurrentNote(); break; }
            }
        }

    private:
        BuiltinProcessor& owner;
        double freq = 440.0, phases[7] {};
        float percEnv = 0, attack = 0, releaseGain = 1, click = 0;
        bool releasing = false;
        FastNoise noise;
    };

    class OrganInstrument : public SynthInstrument
    {
    public:
        OrganInstrument() : SynthInstrument ("organ", juce::String (u8"Órgano"), true)
        {
            addParam ("low", "Graves", 0.0f, 1.0f, 0.8f);
            addParam ("mid", "Medios", 0.0f, 1.0f, 0.6f);
            addParam ("high", "Agudos", 0.0f, 1.0f, 0.3f);
            addParam ("perc", juce::String (u8"Percusión"), 0.0f, 1.0f, 0.4f);
            addParam ("vib", "Vibrato", 0.0f, 1.0f, 0.4f);
            addParam ("volume", "Volumen", -24.0f, 6.0f, 0.0f, "dB");
            addVoices (24, [this] { return new OrganVoice (*this); });
        }

    protected:
        void prepared (double sr) override
        {
            chorus.prepare ({ sr, 4096, 2 });
            chorus.setCentreDelay (6.0f);
            chorus.setFeedback (0.0f);
        }

        void post (juce::AudioBuffer<float>& b) override
        {
            const float v = get ("vib");
            if (v < 0.01f) return;
            chorus.setRate (0.8f + 6.0f * v);
            chorus.setDepth (0.15f + 0.3f * v);
            chorus.setMix (0.5f);
            juce::dsp::AudioBlock<float> block (b);
            chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Chorus<float> chorus;
    };

    //==========================================================================
    // Analog-style synth: 2 band-limited oscillators + sub, resonant filter, envelopes
    //==========================================================================
    class SynthVoice : public VoiceBase
    {
    public:
        explicit SynthVoice (BuiltinProcessor& p) : owner (p)
        {
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            vel = velocity;
            const double sr = getSampleRate();
            if (! prepared || preparedRate != sr)
            {
                filter.prepare ({ sr, 4096, 1 });
                ampEnv.setSampleRate (sr);
                filtEnv.setSampleRate (sr);
                prepared = true; preparedRate = sr;
            }
            filter.reset();
            juce::ADSR::Parameters ap { owner.get ("attack"), owner.get ("decay"), owner.get ("sustain"), owner.get ("release") };
            ampEnv.setParameters (ap);
            filtEnv.setParameters ({ 0.002f, owner.get ("decay") * 1.2f, 0.2f, owner.get ("release") });
            ampEnv.noteOn(); filtEnv.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) { ampEnv.noteOff(); filtEnv.noteOff(); }
            else { ampEnv.reset(); clearCurrentNote(); }
        }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const double sr = getSampleRate();
            const float shape = owner.get ("shape");
            const double detune = std::pow (2.0, owner.get ("detune") / 1200.0);
            const float sub = owner.get ("sub");
            const float cutoff = owner.get ("cutoff");
            const float envAmt = owner.get ("envAmt");
            filter.setResonance (juce::jmap (owner.get ("res"), 0.5f, 6.0f));
            auto* l = out.getWritePointer (0, start);
            auto* r = out.getWritePointer (1, start);

            for (int i = 0; i < num; ++i)
            {
                const double f1 = freq * bend, f2 = f1 * detune;
                const double dt1 = f1 / sr, dt2 = f2 / sr;

                auto osc = [&] (double ph, double dt)
                {
                    const float saw = (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
                    float sq = (ph < 0.5 ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (std::fmod (ph + 0.5, 1.0), dt);
                    return saw * (1.0f - shape) + sq * shape;
                };

                float s = 0.5f * (osc (p1, dt1) + osc (p2, dt2));
                s += sub * (float) std::sin (ps * twoPi);

                p1 += dt1; if (p1 >= 1.0) p1 -= 1.0;
                p2 += dt2; if (p2 >= 1.0) p2 -= 1.0;
                ps += dt1 * 0.5; if (ps >= 1.0) ps -= 1.0;

                if ((i & 15) == 0)
                {
                    const float fe = filtEnv.getNextSample();
                    const float c = juce::jlimit (30.0f, (float) (sr * 0.45), cutoff * std::pow (2.0f, envAmt * 5.0f * fe) * (0.7f + 0.3f * vel));
                    filter.setCutoffFrequency (c);
                }
                s = filter.processSample (0, s);
                const float a = ampEnv.getNextSample();
                const float v = s * a * vel * 0.25f;
                l[i] += v; r[i] += v;

                if (! ampEnv.isActive()) { clearCurrentNote(); break; }
            }
        }

    private:
        BuiltinProcessor& owner;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::ADSR ampEnv, filtEnv;
        double freq = 440.0, p1 = 0, p2 = 0.25, ps = 0, preparedRate = 0;
        float vel = 0;
        bool prepared = false;
    };

    class SynthInstrumentAnalog : public SynthInstrument
    {
    public:
        SynthInstrumentAnalog() : SynthInstrument ("synth", "Sintetizador", true)
        {
            addParam ("shape", "Forma (sierra-cuadrada)", 0.0f, 1.0f, 0.0f);
            addParam ("detune", juce::String (u8"Desafinación"), 0.0f, 40.0f, 8.0f, "cent");
            addParam ("sub", "Sub-oscilador", 0.0f, 1.0f, 0.0f);
            addParam ("cutoff", "Filtro", 40.0f, 18000.0f, 3000.0f, "Hz", 1000.0f);
            addParam ("res", "Resonancia", 0.0f, 1.0f, 0.2f);
            addParam ("envAmt", "Envolvente filtro", 0.0f, 1.0f, 0.3f);
            addParam ("attack", "Ataque", 0.001f, 4.0f, 0.005f, "s", 0.3f);
            addParam ("decay", juce::String (u8"Caída"), 0.01f, 4.0f, 0.4f, "s", 0.5f);
            addParam ("sustain", "Sostenido", 0.0f, 1.0f, 0.7f);
            addParam ("release", juce::String (u8"Liberación"), 0.01f, 6.0f, 0.3f, "s", 0.6f);
            addParam ("volume", "Volumen", -24.0f, 6.0f, 0.0f, "dB");
            addVoices (12, [this] { return new SynthVoice (*this); });
        }
    };

    //==========================================================================
    // Drum kit, General MIDI note map
    //==========================================================================
    enum class Drum { Kick, Snare, Rim, Clap, HatClosed, HatOpen, TomLow, TomMid, TomHigh, Crash, Ride, Tamb, Shaker, Cowbell, None };

    Drum drumForNote (int n)
    {
        switch (n)
        {
            case 35: case 36: return Drum::Kick;
            case 38: case 40: return Drum::Snare;
            case 37:          return Drum::Rim;
            case 39:          return Drum::Clap;
            case 42: case 44: return Drum::HatClosed;
            case 46:          return Drum::HatOpen;
            case 41: case 43: return Drum::TomLow;
            case 45: case 47: return Drum::TomMid;
            case 48: case 50: return Drum::TomHigh;
            case 49: case 52: case 55: case 57: return Drum::Crash;
            case 51: case 53: case 59: return Drum::Ride;
            case 54:          return Drum::Tamb;
            case 69: case 70: case 82: return Drum::Shaker;
            case 56:          return Drum::Cowbell;
            default:          return Drum::None;
        }
    }

    class DrumVoice : public VoiceBase
    {
    public:
        explicit DrumVoice (BuiltinProcessor& p) : owner (p)
        {
            noise.s = (uint32_t) juce::Random::getSystemRandom().nextInt() | 1u;
        }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            type = drumForNote (note);
            if (type == Drum::None) { clearCurrentNote(); return; }
            vel = velocity; t = 0.0; phase = 0.0; hp = lp = bp1 = bp2 = 0.0f;
            for (auto& m : metal) m = 0.0;
            tune = std::pow (2.0, owner.get ("tune") / 12.0);
            tone = owner.get ("tone");
            switch (type)
            {
                case Drum::Kick:      gain = owner.get ("kick");  break;
                case Drum::Snare: case Drum::Rim: case Drum::Clap: gain = owner.get ("snare"); break;
                case Drum::HatClosed: case Drum::HatOpen: case Drum::Crash: case Drum::Ride: gain = owner.get ("cymbals"); break;
                case Drum::TomLow: case Drum::TomMid: case Drum::TomHigh: gain = owner.get ("toms"); break;
                default: gain = owner.get ("perc"); break;
            }
            pan = 0.5f;
            if (type == Drum::HatClosed || type == Drum::HatOpen) pan = 0.38f;
            if (type == Drum::TomHigh) pan = 0.4f;
            if (type == Drum::TomLow)  pan = 0.62f;
            if (type == Drum::Ride)    pan = 0.65f;
            if (type == Drum::Crash)   pan = 0.35f;
            if (type == Drum::Shaker || type == Drum::Tamb) pan = 0.7f;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (! allowTailOff) clearCurrentNote();   // drums ring out; note-off is ignored
        }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const double sr = getSampleRate();
            auto* l = out.getWritePointer (0, start);
            auto* r = out.getWritePointer (1, start);

            for (int i = 0; i < num; ++i)
            {
                const float s = next (sr) * vel * gain;
                l[i] += s * (1.0f - pan) * 2.0f;
                r[i] += s * pan * 2.0f;
                t += 1.0 / sr;
                if (t > maxLength()) { clearCurrentNote(); break; }
            }
        }

    private:
        double maxLength() const
        {
            switch (type)
            {
                case Drum::Crash: return 2.2;
                case Drum::Ride:  return 1.6;
                case Drum::HatOpen: return 0.6;
                case Drum::Kick: case Drum::TomLow: case Drum::TomMid: case Drum::TomHigh: return 0.8;
                default: return 0.5;
            }
        }

        float sine (double f, double sr)
        {
            const float s = (float) std::sin (phase * twoPi);
            phase += f / sr; if (phase >= 1.0) phase -= 1.0;
            return s;
        }

        float metallic (double sr)
        {
            static constexpr double fr[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            float s = 0;
            for (int k = 0; k < 6; ++k)
            {
                s += metal[k] < 0.5 ? 1.0f : -1.0f;
                metal[k] += fr[k] * 1.9 * tune / sr; if (metal[k] >= 1.0) metal[k] -= 1.0;
            }
            return s / 6.0f;
        }

        float highpass (float x, float coeff) { hp = coeff * (hp + x - lastX); lastX = x; return hp; }
        float lowpass (float x, float coeff)  { lp += coeff * (x - lp); return lp; }
        float bandpass (float x, double f, double q, double sr)
        {
            const float g = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin (f, sr * 0.45) / sr);
            const float k = (float) (1.0 / q);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v1 = a1 * (bp1 + g * (x - bp2));
            const float v2 = bp2 + g * v1;
            bp1 = 2 * v1 - bp1; bp2 = 2 * v2 - bp2;
            return v1;
        }

        float next (double sr)
        {
            const double tt = t;
            switch (type)
            {
                case Drum::Kick:
                {
                    const double f = 48.0 * tune + 110.0 * tune * std::exp (-tt / 0.035);
                    const float body = sine (f, sr) * (float) std::exp (-tt / (0.25 + 0.15 * (1.0 - tone)));
                    const float click = (tt < 0.004 ? noise.next() * 0.5f : 0.0f) * tone;
                    return std::tanh ((body + click) * 1.6f);
                }
                case Drum::Snare:
                {
                    const float body = sine (185.0 * tune, sr) * (float) std::exp (-tt / 0.06) * 0.6f;
                    const float n = highpass (noise.next(), 0.92f) * (float) std::exp (-tt / (0.12 + 0.08 * tone));
                    return (body + n * 0.9f) * 0.8f;
                }
                case Drum::Rim:
                    return bandpass (noise.next(), 1700.0 * tune, 4.0, sr) * (float) std::exp (-tt / 0.02) * 2.0f
                         + sine (420.0 * tune, sr) * (float) std::exp (-tt / 0.015) * 0.5f;
                case Drum::Clap:
                {
                    const double burst = std::fmod (tt, 0.011);
                    const float env = tt < 0.035 ? (float) std::exp (-burst / 0.003) : (float) std::exp (-(tt - 0.035) / 0.12);
                    return bandpass (noise.next(), 1100.0, 1.2, sr) * env * 1.4f;
                }
                case Drum::HatClosed:
                case Drum::HatOpen:
                {
                    const double dec = type == Drum::HatClosed ? 0.045 : 0.28;
                    const float s = 0.6f * metallic (sr) + 0.6f * noise.next();
                    return highpass (s, 0.75f + 0.2f * tone) * (float) std::exp (-tt / dec) * 0.7f;
                }
                case Drum::TomLow: case Drum::TomMid: case Drum::TomHigh:
                {
                    const double base = (type == Drum::TomLow ? 95.0 : type == Drum::TomMid ? 135.0 : 180.0) * tune;
                    const double f = base * (1.0 + 0.5 * std::exp (-tt / 0.05));
                    return sine (f, sr) * (float) std::exp (-tt / 0.3) * 0.9f
                         + noise.next() * (float) std::exp (-tt / 0.01) * 0.15f;
                }
                case Drum::Crash:
                {
                    const float s = 0.5f * metallic (sr) + 0.7f * noise.next();
                    return highpass (s, 0.8f) * (float) std::exp (-tt / 0.9) * 0.55f;
                }
                case Drum::Ride:
                {
                    const float bell = (float) std::sin (phase * twoPi) * (float) std::exp (-tt / 0.25) * 0.2f;
                    phase += 2600.0 * tune / sr; if (phase >= 1.0) phase -= 1.0;
                    const float s = 0.7f * metallic (sr) + 0.25f * noise.next();
                    return highpass (s, 0.85f) * (float) std::exp (-tt / 0.6) * 0.4f + bell;
                }
                case Drum::Tamb:
                    return highpass (0.6f * metallic (sr) + noise.next(), 0.9f) * (float) std::exp (-tt / 0.12) * 0.5f;
                case Drum::Shaker:
                {
                    const float env = (float) (tt < 0.012 ? tt / 0.012 : std::exp (-(tt - 0.012) / 0.05));
                    return highpass (noise.next(), 0.9f) * env * 0.45f;
                }
                case Drum::Cowbell:
                {
                    const float a = (phase < 0.5 ? 1.0f : -1.0f);
                    phase += 560.0 * tune / sr; if (phase >= 1.0) phase -= 1.0;
                    const float b = (metal[0] < 0.5 ? 1.0f : -1.0f);
                    metal[0] += 845.0 * tune / sr; if (metal[0] >= 1.0) metal[0] -= 1.0;
                    return bandpass ((a + b) * 0.5f, 800.0, 2.0, sr) * (float) std::exp (-tt / 0.15) * 1.2f;
                }
                case Drum::None: break;
            }
            return 0.0f;
        }

        BuiltinProcessor& owner;
        Drum type = Drum::None;
        double t = 0, phase = 0, metal[6] {}, tune = 1.0;
        float vel = 0, gain = 1, pan = 0.5f, tone = 0.5f;
        float hp = 0, lastX = 0, lp = 0, bp1 = 0, bp2 = 0;
        FastNoise noise;
    };

    class DrumKitInstrument : public SynthInstrument
    {
    public:
        DrumKitInstrument() : SynthInstrument ("drums", juce::String (u8"Batería"), true)
        {
            addParam ("tune", juce::String (u8"Afinación"), -12.0f, 12.0f, 0.0f, "st");
            addParam ("tone", "Tono", 0.0f, 1.0f, 0.5f);
            addParam ("kick", "Bombo", 0.0f, 1.5f, 1.0f);
            addParam ("snare", "Caja", 0.0f, 1.5f, 1.0f);
            addParam ("cymbals", "Platillos", 0.0f, 1.5f, 0.8f);
            addParam ("toms", "Toms", 0.0f, 1.5f, 1.0f);
            addParam ("perc", juce::String (u8"Percusión"), 0.0f, 1.5f, 0.8f);
            addParam ("volume", "Volumen", -24.0f, 6.0f, 0.0f, "dB");
            addVoices (24, [this] { return new DrumVoice (*this); });
        }

    protected:
        void post (juce::AudioBuffer<float>& b) override
        {
            // gentle bus glue so the kit sits together
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    d[i] = std::tanh (d[i] * 0.9f);
            }
        }
    };
} // namespace

std::unique_ptr<BuiltinProcessor> createBuiltinInstrument (const juce::String& type)
{
    if (type == "piano")  return std::make_unique<PianoInstrument>();
    if (type == "epiano") return std::make_unique<EPianoInstrument>();
    if (type == "organ")  return std::make_unique<OrganInstrument>();
    if (type == "synth")  return std::make_unique<SynthInstrumentAnalog>();
    if (type == "drums")  return std::make_unique<DrumKitInstrument>();
    return nullptr;
}

} // namespace galpon
