// Galpon's own effects. Each one is a regular juce::AudioProcessor, so the engine treats
// them exactly like AU / VST3 plugins.

#include "Builtins.h"

namespace galpon
{
namespace
{
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    class EffectBase : public BuiltinProcessor
    {
    public:
        using BuiltinProcessor::BuiltinProcessor;
        void releaseResources() override {}

    protected:
        double sampleRate = 44100.0;
        int maxBlock = 512;

        double tempo() const
        {
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                    if (auto bpm = pos->getBpm())
                        return *bpm;
            return 120.0;
        }
    };

    //==========================================================================
    class EqEffect : public EffectBase
    {
    public:
        EqEffect() : EffectBase ("eq", "Ecualizador", false)
        {
            addParam ("lowcut", "Corte de graves", 20.0f, 500.0f, 20.0f, "Hz", 100.0f);
            addParam ("low", "Graves", -15.0f, 15.0f, 0.0f, "dB");
            addParam ("mid", "Medios", -15.0f, 15.0f, 0.0f, "dB");
            addParam ("midFreq", "Frecuencia medios", 200.0f, 5000.0f, 1000.0f, "Hz", 1000.0f);
            addParam ("high", "Agudos", -15.0f, 15.0f, 0.0f, "dB");
        }

        void prepareToPlay (double sr, int bs) override
        {
            sampleRate = sr; maxBlock = bs;
            for (auto* f : { &cut, &low, &mid, &high })
                for (auto& ch : *f) { ch.prepare ({ sr, (juce::uint32) bs, 1 }); ch.reset(); }
            lastHash = -1.0f;
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const float hash = get ("lowcut") + 3 * get ("low") + 7 * get ("mid") + 11 * get ("midFreq") + 13 * get ("high");
            if (hash != lastHash)
            {
                lastHash = hash;
                for (int ch = 0; ch < 2; ++ch)
                {
                    *cut[ch].coefficients  = *Coeffs::makeHighPass (sampleRate, get ("lowcut"));
                    *low[ch].coefficients  = *Coeffs::makeLowShelf (sampleRate, 120.0, 0.7, juce::Decibels::decibelsToGain (get ("low")));
                    *mid[ch].coefficients  = *Coeffs::makePeakFilter (sampleRate, get ("midFreq"), 0.9, juce::Decibels::decibelsToGain (get ("mid")));
                    *high[ch].coefficients = *Coeffs::makeHighShelf (sampleRate, 6000.0, 0.7, juce::Decibels::decibelsToGain (get ("high")));
                }
            }
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                const bool useCut = get ("lowcut") > 21.0f;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    float x = d[i];
                    if (useCut) x = cut[ch].processSample (x);
                    x = low[ch].processSample (x);
                    x = mid[ch].processSample (x);
                    d[i] = high[ch].processSample (x);
                }
            }
        }

    private:
        static std::array<Filter, 2> make() { return { Filter (Coeffs::makeAllPass (44100.0, 1000.0)), Filter (Coeffs::makeAllPass (44100.0, 1000.0)) }; }
        std::array<Filter, 2> cut = make(), low = make(), mid = make(), high = make();
        float lastHash = -1.0f;
    };

    //==========================================================================
    class CompEffect : public EffectBase
    {
    public:
        CompEffect() : EffectBase ("comp", "Compresor", false)
        {
            addParam ("threshold", "Umbral", -50.0f, 0.0f, -18.0f, "dB");
            addParam ("ratio", juce::String (u8"Proporción"), 1.0f, 20.0f, 4.0f, ":1", 4.0f);
            addParam ("attack", "Ataque", 0.1f, 100.0f, 10.0f, "ms", 10.0f);
            addParam ("release", juce::String (u8"Liberación"), 10.0f, 1000.0f, 120.0f, "ms", 120.0f);
            addParam ("makeup", "Ganancia", 0.0f, 24.0f, 3.0f, "dB");
        }

        void prepareToPlay (double sr, int bs) override
        {
            comp.prepare ({ sr, (juce::uint32) bs, 2 });
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            comp.setThreshold (get ("threshold"));
            comp.setRatio (get ("ratio"));
            comp.setAttack (get ("attack"));
            comp.setRelease (get ("release"));
            juce::dsp::AudioBlock<float> block (b);
            comp.process (juce::dsp::ProcessContextReplacing<float> (block));
            b.applyGain (juce::Decibels::decibelsToGain (get ("makeup")));
        }

    private:
        juce::dsp::Compressor<float> comp;
    };

    //==========================================================================
    class GateEffect : public EffectBase
    {
    public:
        GateEffect() : EffectBase ("gate", "Puerta de ruido", false)
        {
            addParam ("threshold", "Umbral", -90.0f, -10.0f, -55.0f, "dB");
            addParam ("release", juce::String (u8"Liberación"), 10.0f, 500.0f, 80.0f, "ms", 80.0f);
        }

        void prepareToPlay (double sr, int bs) override { gate.prepare ({ sr, (juce::uint32) bs, 2 }); }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            gate.setThreshold (get ("threshold"));
            gate.setRatio (20.0f);
            gate.setAttack (1.0f);
            gate.setRelease (get ("release"));
            juce::dsp::AudioBlock<float> block (b);
            gate.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::NoiseGate<float> gate;
    };

    //==========================================================================
    /** A guitar speaker in a closed cabinet (a 4x12), as filters: what's left of a distorted signal
        after a real speaker is a hump of mids with a steep drop above ~5 kHz. Without it, distortion
        sounds like bees. Used by the amp and by the cabinet effect when it has no IR loaded. */
    struct SpeakerSim
    {
        std::array<std::array<Filter, 2>, 7> f;
        void prepare (double sr)
        {
            auto set = [&] (int i, Coeffs::Ptr c) { for (auto& ch : f[(size_t) i]) { ch.coefficients = c; ch.reset(); } };
            set (0, Coeffs::makeHighPass (sr, 75.0, 0.8));                                    // small cabs don't go lower
            set (1, Coeffs::makePeakFilter (sr, 115.0, 1.4, juce::Decibels::decibelsToGain (4.0f)));   // the cabinet's thump
            set (2, Coeffs::makePeakFilter (sr, 420.0, 1.0, juce::Decibels::decibelsToGain (-3.0f)));  // less boxiness
            set (3, Coeffs::makePeakFilter (sr, 2400.0, 1.1, juce::Decibels::decibelsToGain (3.5f)));  // the speaker's bite
            set (4, Coeffs::makeLowPass (sr, juce::jmin (5000.0, sr * 0.45), 0.7));             // then a steep fall
            set (5, Coeffs::makeLowPass (sr, juce::jmin (6200.0, sr * 0.45), 1.0));
            set (6, Coeffs::makePeakFilter (sr, juce::jmin (8500.0, sr * 0.4), 2.0, juce::Decibels::decibelsToGain (-8.0f)));
        }
        float process (int ch, float x) noexcept
        {
            for (auto& st : f) x = st[(size_t) ch].processSample (x);
            return x;
        }
    };

    /** Runs a nonlinear stage at 4x the sample rate, so the harmonics it makes don't fold back
        into the audible range as harsh, out-of-tune tones (aliasing). */
    struct Oversampled
    {
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        size_t chunk = 512;
        void prepare (int maxBlock)
        {
            chunk = (size_t) juce::jmax (64, maxBlock);
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            os->initProcessing (chunk);
        }
        template <typename Fn> void process (juce::AudioBuffer<float>& b, Fn&& perSample)
        {
            if (os == nullptr || b.getNumChannels() < 2) return;
            juce::dsp::AudioBlock<float> all (b.getArrayOfWritePointers(), 2, (size_t) b.getNumSamples());
            // in pieces no bigger than what it was prepared for
            for (size_t at = 0; at < all.getNumSamples(); at += chunk)
            {
                auto block = all.getSubBlock (at, juce::jmin (chunk, all.getNumSamples() - at));
                auto up = os->processSamplesUp (block);
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    auto* d = up.getChannelPointer (ch);
                    for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = perSample ((int) ch, d[i]);
                }
                os->processSamplesDown (block);
            }
        }
        float latency() const { return os != nullptr ? os->getLatencyInSamples() : 0.0f; }
    };

    /** A tube-like stage: soft, a little asymmetric (even harmonics, like a triode). */
    inline float tube (float x, float bias) noexcept { return std::tanh (x + bias) - std::tanh (bias); }

    //==========================================================================
    class DriveEffect : public EffectBase
    {
    public:
        DriveEffect() : EffectBase ("drive", juce::String (u8"Distorsión"), false)
        {
            addParam ("drive", juce::String (u8"Saturación"), 0.0f, 1.0f, 0.4f);
            addParam ("tone", "Tono", 500.0f, 12000.0f, 4000.0f, "Hz", 3000.0f);
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 1.0f);
            addParam ("level", "Nivel", -24.0f, 12.0f, -3.0f, "dB");
        }

        void prepareToPlay (double sr, int bs) override
        {
            sampleRate = sr;
            os.prepare (bs);
            setLatencySamples (juce::roundToInt (os.latency()));
            dry.setSize (2, bs);
            for (auto& ch : pre) { ch.coefficients = Coeffs::makeHighPass (sr * 4.0, 650.0, 0.6); ch.reset(); }
            for (auto& ch : post) { ch.coefficients = Coeffs::makeLowPass (sr * 4.0, 7000.0, 0.7); ch.reset(); }
            for (auto& ch : low) { ch.coefficients = Coeffs::makeHighPass (sr, 40.0, 0.7); ch.reset(); }
            lastTone = -1.0f;
            state[0] = state[1] = 0.0f;
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            // Like an overdrive pedal: lows are kept out of the clipping (so it stays tight), the
            // mids drive into soft diodes, the lows come back clean after, then a tone control.
            const int n = b.getNumSamples();
            if (b.getNumChannels() < 2) return;
            const float dv = get ("drive");
            const float drive = 2.0f + 70.0f * dv * dv;
            const float mix = get ("mix"), level = juce::Decibels::decibelsToGain (get ("level"));
            const float tone = get ("tone");
            if (tone != lastTone)
            {
                lastTone = tone;
                toneA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * tone / (float) sampleRate);
            }
            dry.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch) dry.copyFrom (ch, 0, b, ch, 0, n);
            const float norm = 1.0f / std::tanh (juce::jmin (drive * 0.25f, 2.5f));
            os.process (b, [&] (int ch, float x)
            {
                const float mids = pre[(size_t) ch].processSample (x);
                const float lows = x - mids;
                float y = tube (mids * drive, 0.15f) * norm * 0.6f + lows * 0.5f;
                return post[(size_t) ch].processSample (y);
            });
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                auto* dr = dry.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    state[ch] += toneA * (d[i] - state[ch]);
                    const float wet = low[(size_t) ch].processSample (state[ch]);
                    d[i] = (dr[i] * (1.0f - mix) + wet * mix) * level;
                }
            }
        }

    private:
        static std::array<Filter, 2> make() { return { Filter (Coeffs::makeAllPass (44100.0, 1000.0)), Filter (Coeffs::makeAllPass (44100.0, 1000.0)) }; }
        Oversampled os;
        std::array<Filter, 2> pre = make(), post = make(), low = make();
        juce::AudioBuffer<float> dry;
        float state[2] {}, toneA = 0.5f, lastTone = -1.0f;
    };

    //==========================================================================
    /** A guitar amp for people who don't have captures yet: a preamp of tube-like stages run at
        4x the rate (clean at low gain, stacked stages at high gain), a tone stack, a power amp
        that sags a little, and a speaker. */
    class AmpEffect : public EffectBase
    {
    public:
        AmpEffect() : EffectBase ("amp", "Amplificador", false)
        {
            addParam ("gain", "Ganancia", 0.0f, 1.0f, 0.45f);
            addParam ("bass", "Graves", -12.0f, 12.0f, 0.0f, "dB");
            addParam ("mid", "Medios", -12.0f, 12.0f, 0.0f, "dB");
            addParam ("treble", "Agudos", -12.0f, 12.0f, 0.0f, "dB");
            addParam ("cab", "Simular parlante", 0.0f, 1.0f, 1.0f);
            addParam ("master", "Master", -30.0f, 6.0f, -6.0f, "dB");
        }

        void prepareToPlay (double sr, int bs) override
        {
            sampleRate = sr;
            os.prepare (bs);
            setLatencySamples (juce::roundToInt (os.latency()));
            speaker.prepare (sr);
            for (auto* f : { &bass, &mid, &treble, &dc }) for (auto& ch : *f) ch.reset();
            lastHash = -1.0f;
            env[0] = env[1] = 0.0f;
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            if (b.getNumChannels() < 2) return;
            const float gainKnob = get ("gain");
            const float hash = get ("bass") + 3 * get ("mid") + 7 * get ("treble") + 13 * gainKnob;
            if (hash != lastHash)
            {
                lastHash = hash;
                const double osr = sampleRate * 4.0;
                // more gain = tighter: fewer lows into the clipping, so chords don't turn to mud
                const double tight = 70.0 + 500.0 * gainKnob * gainKnob;
                // a high-gain amp scoops the mids a bit; a clean one is flat
                const float scoop = -3.0f * gainKnob;
                for (int ch = 0; ch < 2; ++ch)
                {
                    inHp[(size_t) ch].coefficients  = Coeffs::makeHighPass (osr, tight, 0.7);
                    stage[(size_t) ch].coefficients = Coeffs::makeLowPass (osr, 9000.0, 0.6);     // between the stages, like the tube's capacitance
                    bass[(size_t) ch].coefficients   = Coeffs::makeLowShelf (sampleRate, 160.0, 0.7, juce::Decibels::decibelsToGain (get ("bass") + 1.5f));
                    mid[(size_t) ch].coefficients    = Coeffs::makePeakFilter (sampleRate, 650.0, 0.7, juce::Decibels::decibelsToGain (get ("mid") + scoop));
                    treble[(size_t) ch].coefficients = Coeffs::makeHighShelf (sampleRate, 2800.0, 0.7, juce::Decibels::decibelsToGain (get ("treble")));
                    dc[(size_t) ch].coefficients     = Coeffs::makeHighPass (sampleRate, 25.0, 0.7);
                }
            }
            // gain in stages: a clean amp barely touches the first stage; a high-gain one drives two
            const float g1 = 1.5f + 18.0f * gainKnob;
            const float g2 = gainKnob > 0.35f ? 1.0f + 12.0f * (gainKnob - 0.35f) / 0.65f : 0.0f;
            const float makeup = 0.9f / (0.6f + 0.8f * gainKnob);
            os.process (b, [&] (int ch, float x)
            {
                float y = tube (inHp[(size_t) ch].processSample (x) * g1, 0.2f);
                if (g2 > 0.0f) y = tube (stage[(size_t) ch].processSample (y) * g2 * 1.6f, -0.12f) * 0.8f;
                return y * makeup;
            });
            const bool useCab = get ("cab") > 0.5f;
            const float master = juce::Decibels::decibelsToGain (get ("master"));
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    float x = treble[(size_t) ch].processSample (mid[(size_t) ch].processSample (bass[(size_t) ch].processSample (d[i])));
                    // power amp: compresses a little when hit hard (sag), then soft-clips
                    env[ch] += 0.0005f * (std::abs (x) - env[ch]);
                    x = std::tanh (x * 1.2f / (1.0f + 0.6f * env[ch])) * 0.85f;
                    x = dc[(size_t) ch].processSample (x);
                    if (useCab) x = speaker.process (ch, x) * 1.4f;
                    d[i] = x * master * 2.0f;
                }
            }
        }

    private:
        static std::array<Filter, 2> make() { return { Filter (Coeffs::makeAllPass (44100.0, 1000.0)), Filter (Coeffs::makeAllPass (44100.0, 1000.0)) }; }
        Oversampled os;
        SpeakerSim speaker;
        std::array<Filter, 2> inHp = make(), stage = make(), bass = make(), mid = make(), treble = make(), dc = make();
        float lastHash = -1.0f, env[2] {};
    };

    //==========================================================================
    /** Loads a cabinet impulse response (.wav), e.g. the IRs on Tone3000. */
    class CabEffect : public EffectBase
    {
    public:
        CabEffect() : EffectBase ("cab", "Gabinete (IR)", false)
        {
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 1.0f);
            addParam ("lowcut", "Corte de graves", 20.0f, 300.0f, 70.0f, "Hz", 80.0f);
            addParam ("highcut", "Corte de agudos", 2000.0f, 20000.0f, 9000.0f, "Hz", 7000.0f);
            addParam ("level", "Nivel", -24.0f, 12.0f, 0.0f, "dB");
        }

        void prepareToPlay (double sr, int bs) override
        {
            sampleRate = sr;
            const juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 2 };
            conv.prepare (spec);
            dry.setSize (2, bs);
            for (auto* f : { &lc, &hc }) for (auto& ch : *f) ch.reset();
            speaker.prepare (sr);
            lastHash = -1.0f;
            if (irFile.existsAsFile())
                load (irFile);
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const int n = b.getNumSamples();
            if (! irFile.existsAsFile())
            {
                // no IR: a speaker anyway (an amp capture without a cabinet sounds like bees)
                const float level = juce::Decibels::decibelsToGain (get ("level")) * 1.4f;
                for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    for (int i = 0; i < n; ++i) d[i] = speaker.process (ch, d[i]) * level;
                }
                return;
            }
            const float mix = get ("mix");
            dry.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch) dry.copyFrom (ch, 0, b, juce::jmin (ch, b.getNumChannels() - 1), 0, n);

            juce::dsp::AudioBlock<float> block (b);
            conv.process (juce::dsp::ProcessContextReplacing<float> (block));

            const float hash = get ("lowcut") + 7 * get ("highcut");
            if (hash != lastHash)
            {
                lastHash = hash;
                for (int ch = 0; ch < 2; ++ch)
                {
                    *lc[ch].coefficients = *Coeffs::makeHighPass (sampleRate, get ("lowcut"));
                    *hc[ch].coefficients = *Coeffs::makeLowPass (sampleRate, juce::jmin ((double) get ("highcut"), sampleRate * 0.45));
                }
            }
            const float level = juce::Decibels::decibelsToGain (get ("level"));
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                auto* dr = dry.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    d[i] = (hc[ch].processSample (lc[ch].processSample (d[i])) * mix + dr[i] * (1.0f - mix)) * level;
            }
        }

        juce::var getExtraInfo() const override
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("file", irFile.getFullPathName());
            o->setProperty ("fileName", irFile.getFileName());
            o->setProperty ("loadable", "ir");
            return juce::var (o);
        }

        juce::Result setExtra (const juce::String& key, const juce::var& value) override
        {
            if (key != "file") return EffectBase::setExtra (key, value);
            juce::File f (value.toString());
            if (! f.existsAsFile()) return juce::Result::fail ("No encuentro el archivo " + f.getFullPathName());
            load (f);
            return juce::Result::ok();
        }

    protected:
        void writeExtraState (juce::XmlElement& x) const override { x.setAttribute ("ir", irFile.getFullPathName()); }
        void readExtraState (const juce::XmlElement& x) override
        {
            if (auto path = x.getStringAttribute ("ir"); path.isNotEmpty())
                if (juce::File f (path); f.existsAsFile())
                    load (f);
        }

    private:
        void load (const juce::File& f)
        {
            irFile = f;
            conv.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                                      juce::dsp::Convolution::Normalise::yes);
        }

        static std::array<Filter, 2> make() { return { Filter (Coeffs::makeAllPass (44100.0, 1000.0)), Filter (Coeffs::makeAllPass (44100.0, 1000.0)) }; }
        juce::dsp::Convolution conv;
        SpeakerSim speaker;
        juce::AudioBuffer<float> dry;
        juce::File irFile;
        std::array<Filter, 2> lc = make(), hc = make();
        float lastHash = -1.0f;
    };

    //==========================================================================
    class ChorusEffect : public EffectBase
    {
    public:
        ChorusEffect() : EffectBase ("chorus", "Chorus", false)
        {
            addParam ("rate", "Velocidad", 0.05f, 5.0f, 0.8f, "Hz", 1.0f);
            addParam ("depth", "Profundidad", 0.0f, 1.0f, 0.35f);
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 0.5f);
        }

        void prepareToPlay (double sr, int bs) override { chorus.prepare ({ sr, (juce::uint32) bs, 2 }); chorus.setCentreDelay (8.0f); }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            chorus.setRate (get ("rate"));
            chorus.setDepth (get ("depth"));
            chorus.setMix (get ("mix"));
            juce::dsp::AudioBlock<float> block (b);
            chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Chorus<float> chorus;
    };

    //==========================================================================
    class DelayEffect : public EffectBase
    {
    public:
        DelayEffect() : EffectBase ("delay", "Delay", false)
        {
            // division: 0 = libre (ms), 1 = negra, 2 = corchea, 3 = corchea con punto, 4 = semicorchea, 5 = tresillo
            addParam ("division", "Tiempo (0=libre 1=1/4 2=1/8 3=1/8. 4=1/16 5=tresillo)", 0.0f, 5.0f, 3.0f);
            addParam ("ms", "Tiempo libre", 10.0f, 2000.0f, 350.0f, "ms", 400.0f);
            addParam ("feedback", "Repeticiones", 0.0f, 0.95f, 0.35f);
            addParam ("tone", "Tono", 500.0f, 16000.0f, 5000.0f, "Hz", 3000.0f);
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 0.25f);
        }

        void prepareToPlay (double sr, int) override
        {
            sampleRate = sr;
            line.setSize (2, (int) (sr * 4.1) + 1);
            line.clear();
            writePos = 0;
            lp[0] = lp[1] = 0.0f;
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const int div = juce::roundToInt (get ("division"));
            const double beat = 60.0 / tempo();
            double seconds = get ("ms") / 1000.0;
            switch (div)
            {
                case 1: seconds = beat; break;
                case 2: seconds = beat * 0.5; break;
                case 3: seconds = beat * 0.75; break;
                case 4: seconds = beat * 0.25; break;
                case 5: seconds = beat / 3.0; break;
                default: break;
            }
            const int size = line.getNumSamples();
            const int delay = juce::jlimit (1, size - 1, (int) (seconds * sampleRate));
            const float fb = get ("feedback"), mix = get ("mix");
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * get ("tone") / (float) sampleRate);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const int readPos = (writePos - delay + size) % size;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const int src = juce::jmin (ch, b.getNumChannels() - 1);
                    const float in = b.getSample (src, i);
                    float wet = line.getSample (ch, readPos);
                    lp[ch] += a * (wet - lp[ch]);
                    wet = lp[ch];
                    line.setSample (ch, writePos, in + wet * fb);
                    if (ch < b.getNumChannels())
                        b.setSample (ch, i, in + wet * mix);
                }
                writePos = (writePos + 1) % size;
            }
        }

    private:
        juce::AudioBuffer<float> line;
        int writePos = 0;
        float lp[2] {};
    };

    //==========================================================================
    class ReverbEffect : public EffectBase
    {
    public:
        ReverbEffect() : EffectBase ("reverb", "Reverb", false)
        {
            addParam ("size", juce::String (u8"Tamaño"), 0.0f, 1.0f, 0.5f);
            addParam ("damping", "Opacidad", 0.0f, 1.0f, 0.5f);
            addParam ("width", "Amplitud", 0.0f, 1.0f, 1.0f);
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 0.25f);
        }

        void prepareToPlay (double sr, int bs) override { reverb.prepare ({ sr, (juce::uint32) bs, 2 }); reverb.reset(); }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            juce::dsp::Reverb::Parameters p;
            p.roomSize = get ("size");
            p.damping = get ("damping");
            p.width = get ("width");
            p.wetLevel = get ("mix");
            p.dryLevel = 1.0f - 0.5f * get ("mix");
            reverb.setParameters (p);
            juce::dsp::AudioBlock<float> block (b);
            reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Reverb reverb;
    };
} // namespace

std::unique_ptr<BuiltinProcessor> createBuiltinEffect (const juce::String& type)
{
    if (type == "eq")     return std::make_unique<EqEffect>();
    if (type == "comp")   return std::make_unique<CompEffect>();
    if (type == "gate")   return std::make_unique<GateEffect>();
    if (type == "drive")  return std::make_unique<DriveEffect>();
    if (type == "amp")    return std::make_unique<AmpEffect>();
    if (type == "cab")    return std::make_unique<CabEffect>();
    if (type == "chorus") return std::make_unique<ChorusEffect>();
    if (type == "delay")  return std::make_unique<DelayEffect>();
    if (type == "reverb") return std::make_unique<ReverbEffect>();
    return nullptr;
}

} // namespace galpon
