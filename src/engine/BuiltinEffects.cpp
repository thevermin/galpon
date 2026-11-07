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

        void prepareToPlay (double sr, int) override { sampleRate = sr; state[0] = state[1] = 0.0f; }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const float drive = 1.0f + 60.0f * get ("drive") * get ("drive");
            const float mix = get ("mix");
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * get ("tone") / (float) sampleRate);
            const float level = juce::Decibels::decibelsToGain (get ("level"));
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    float w = std::tanh (d[i] * drive) / std::tanh (juce::jmin (drive, 3.0f));
                    state[ch] += a * (w - state[ch]);
                    d[i] = (d[i] * (1.0f - mix) + state[ch] * mix) * level;
                }
            }
        }

    private:
        float state[2] {};
    };

    //==========================================================================
    /** A simple guitar amp for people who don't have captures yet. */
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

        void prepareToPlay (double sr, int) override
        {
            sampleRate = sr;
            for (auto* f : { &pre, &bass, &mid, &treble, &cab1, &cab2 })
                for (auto& ch : *f) ch.reset();
            lastHash = -1.0f;
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const float hash = get ("bass") + 3 * get ("mid") + 7 * get ("treble");
            if (hash != lastHash)
            {
                lastHash = hash;
                for (int ch = 0; ch < 2; ++ch)
                {
                    *pre[ch].coefficients    = *Coeffs::makeHighPass (sampleRate, 90.0);
                    *bass[ch].coefficients   = *Coeffs::makeLowShelf (sampleRate, 200.0, 0.7, juce::Decibels::decibelsToGain (get ("bass")));
                    *mid[ch].coefficients    = *Coeffs::makePeakFilter (sampleRate, 750.0, 0.8, juce::Decibels::decibelsToGain (get ("mid")));
                    *treble[ch].coefficients = *Coeffs::makeHighShelf (sampleRate, 3000.0, 0.7, juce::Decibels::decibelsToGain (get ("treble")));
                    *cab1[ch].coefficients   = *Coeffs::makeLowPass (sampleRate, 4800.0, 1.1);
                    *cab2[ch].coefficients   = *Coeffs::makePeakFilter (sampleRate, 110.0, 1.2, 1.6);
                }
            }
            const float g = 1.0f + 80.0f * get ("gain") * get ("gain");
            const bool useCab = get ("cab") > 0.5f;
            const float master = juce::Decibels::decibelsToGain (get ("master"));
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    float x = pre[ch].processSample (d[i]) * g;
                    x = std::tanh (x) * 0.7f + std::tanh (x * 0.3f) * 0.3f;      // two soft stages
                    x = treble[ch].processSample (mid[ch].processSample (bass[ch].processSample (x)));
                    if (useCab) x = cab2[ch].processSample (cab1[ch].processSample (x));
                    d[i] = x * master;
                }
            }
        }

    private:
        static std::array<Filter, 2> make() { return { Filter (Coeffs::makeAllPass (44100.0, 1000.0)), Filter (Coeffs::makeAllPass (44100.0, 1000.0)) }; }
        std::array<Filter, 2> pre = make(), bass = make(), mid = make(), treble = make(), cab1 = make(), cab2 = make();
        float lastHash = -1.0f;
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
            lastHash = -1.0f;
            if (irFile.existsAsFile())
                load (irFile);
        }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            if (! irFile.existsAsFile()) return;
            const int n = b.getNumSamples();
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
