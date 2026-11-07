// "Amp NAM": loads Neural Amp Modeler captures (.nam), the format Tone3000 shares.
// Model loading happens off the audio thread; the audio thread swaps the new model in
// with a try-lock so it never waits.

#include "Builtins.h"

#if GALPON_WITH_NAM
 #include <NAM/dsp.h>
 #include <NAM/get_dsp.h>
#endif

namespace galpon
{
namespace
{
    class NamAmp : public BuiltinProcessor,
                   private juce::Thread
    {
    public:
        NamAmp() : BuiltinProcessor ("nam", "Amp NAM (Tone3000)", false), juce::Thread ("NAM loader")
        {
            addParam ("input", "Entrada", -24.0f, 24.0f, 0.0f, "dB");
            addParam ("output", "Salida", -40.0f, 12.0f, 0.0f, "dB");
            addParam ("normalize", "Igualar volumen", 0.0f, 1.0f, 1.0f);
            // Same default as the NAM plugin: a gentle gate keeps hiss and hum out of high-gain models
            addParam ("gate", "Puerta de ruido", -100.0f, -20.0f, -80.0f, "dB");
            addParam ("bass", "Graves", -12.0f, 12.0f, 0.0f, "dB");
            addParam ("treble", "Agudos", -12.0f, 12.0f, 0.0f, "dB");
        }

        ~NamAmp() override { stopThread (10000); }

        void prepareToPlay (double sr, int bs) override
        {
            sampleRate = sr; maxBlock = bs;
            mono.setSize (1, bs);
            resetResampler (modelRate.load());
            gate.prepare ({ sr, (juce::uint32) bs, 1 });
            for (auto* f : { &bassF, &trebleF }) f->reset();
            lastHash = -1.0f;
            if (modelFile.existsAsFile())
                startLoading (modelFile);
        }

        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            const int n = b.getNumSamples();
            if (mono.getNumSamples() < n) return;    // host broke its promise about block size

            // Take any freshly loaded model (never blocks). The old one is deleted off the audio thread.
            {
                const juce::SpinLock::ScopedTryLockType lock (swapLock);
                if (lock.isLocked() && incoming != nullptr && retired == nullptr)
                {
                    retired = std::move (active);
                    active  = std::move (incoming);
                }
            }

            auto* m = mono.getWritePointer (0);
            const float inGain = juce::Decibels::decibelsToGain (get ("input"));
            for (int i = 0; i < n; ++i)
                m[i] = b.getSample (0, i) * inGain;   // amps are mono: use the left/first input

            if (get ("gate") > -99.0f)
            {
                gate.setThreshold (get ("gate")); gate.setRatio (20.0f); gate.setAttack (1.0f); gate.setRelease (60.0f);
                juce::dsp::AudioBlock<float> block (mono);
                auto sub = block.getSubBlock (0, (size_t) n);
                gate.process (juce::dsp::ProcessContextReplacing<float> (sub));
            }

            float outGain = juce::Decibels::decibelsToGain (get ("output"));

           #if GALPON_WITH_NAM
            if (active != nullptr)
            {
                if (active.get() != resampledFor)
                {
                    resampledFor = active.get();
                    resetResampler (modelRate.load());
                }
                if (needsResampling) processResampled (m, n);
                else
                {
                    float* in[1]  = { m };
                    float* out[1] = { m };
                    active->process (in, out, n);
                }
                if (get ("normalize") > 0.5f && active->HasLoudness())
                    outGain *= juce::Decibels::decibelsToGain ((float) (-18.0 - active->GetLoudness()));
            }
           #endif

            const float hash = get ("bass") + 5 * get ("treble");
            if (hash != lastHash)
            {
                lastHash = hash;
                *bassF.coefficients   = *Coeffs::makeLowShelf (sampleRate, 150.0, 0.7, juce::Decibels::decibelsToGain (get ("bass")));
                *trebleF.coefficients = *Coeffs::makeHighShelf (sampleRate, 3500.0, 0.7, juce::Decibels::decibelsToGain (get ("treble")));
            }
            for (int i = 0; i < n; ++i)
                m[i] = trebleF.processSample (bassF.processSample (m[i])) * outGain;

            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.copyFrom (ch, 0, mono, 0, 0, n);
        }

        juce::var getExtraInfo() const override
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("file", modelFile.getFullPathName());
            o->setProperty ("fileName", modelFile.getFileNameWithoutExtension());
            o->setProperty ("loadable", "nam");
            o->setProperty ("status", getStatus());
            o->setProperty ("gearType", gearType());
            o->setProperty ("needsCab", needsCab.load());
            o->setProperty ("resampling", needsResampling);
           #if ! GALPON_WITH_NAM
            o->setProperty ("status", juce::String (u8"Esta versión se compiló sin soporte NAM"));
           #endif
            return juce::var (o);
        }

        juce::Result setExtra (const juce::String& key, const juce::var& value) override
        {
            if (key != "file") return BuiltinProcessor::setExtra (key, value);
            juce::File f (value.toString());
            if (! f.existsAsFile()) return juce::Result::fail ("No encuentro " + f.getFullPathName());
            startLoading (f);
            return juce::Result::ok();
        }

    protected:
        void writeExtraState (juce::XmlElement& x) const override { x.setAttribute ("model", modelFile.getFullPathName()); }
        void readExtraState (const juce::XmlElement& x) override
        {
            if (auto path = x.getStringAttribute ("model"); path.isNotEmpty())
                if (juce::File f (path); f.existsAsFile())
                    startLoading (f);
        }

    private:
        using Coeffs = juce::dsp::IIR::Coefficients<float>;

        void startLoading (const juce::File& f)
        {
            stopThread (10000);
            collectGarbage();
            modelFile = f;
            setStatus ("Cargando " + f.getFileNameWithoutExtension() + juce::String (u8"…"));
            startThread();
        }

        void run() override
        {
           #if GALPON_WITH_NAM
            try
            {
                auto model = nam::get_dsp (std::filesystem::path (modelFile.getFullPathName().toStdString()));
                if (model == nullptr) { setStatus ("No se pudo leer el modelo"); return; }

                const double expected = model->GetExpectedSampleRate();
                const bool anyRate = model->SupportsArbitrarySampleRate();
                // Models are trained at one rate (almost always 48 kHz). Running them at another
                // rate without converting changes how they sound, so convert unless the model adapts.
                const double runRate = (expected > 0 && ! anyRate) ? expected : sampleRate;
                model->Reset (runRate, juce::jmax (maxBlock, 32) * 2 + 64);
                readMetadata();

                std::unique_ptr<nam::DSP> toDelete;
                {
                    const juce::SpinLock::ScopedLockType lock (swapLock);
                    toDelete = std::move (incoming);     // a model that never got used
                    incoming = std::move (model);
                    modelRate = runRate;
                }
                toDelete.reset();
                collectGarbage();

                juce::String s = modelFile.getFileNameWithoutExtension();
                if (std::abs (runRate - sampleRate) > 1.0)
                    s += juce::String (u8" · convirtiendo ") + juce::String (sampleRate / 1000.0, 1) + juce::String (u8" → ")
                         + juce::String (runRate / 1000.0, 1) + " kHz";
                if (needsCab.load())
                    s += juce::String (u8" · es solo el amplificador: necesita un gabinete después");
                setStatus (s);
            }
            catch (const std::exception& e)
            {
                setStatus (juce::String ("Error al cargar: ") + e.what());
            }
           #endif
        }

        /** Reads the capture's description (TONE3000 fills in what gear it is). */
        void readMetadata()
        {
            juce::String gear;
            if (auto json = juce::JSON::parse (modelFile); json.isObject())
                gear = json["metadata"]["gear_type"].toString();
            {
                const juce::ScopedLock sl (statusLock);
                gearTypeValue = gear;
            }
            // Heads, preamps and pedal+head captures have no speaker in them: straight to the
            // headphones they sound like fizz/static. Full rigs ("amp_cab", "amp_pedal_cab") don't.
            needsCab = gear == "amp" || gear == "preamp" || gear == "pedal_amp";
        }

        juce::String gearType() const
        {
            const juce::ScopedLock sl (statusLock);
            return gearTypeValue;
        }

        /** Sets up the stream converter (device rate <-> model rate). Not on the audio thread
            unless the sizes already fit (they're allocated generously in prepareToPlay). */
        void resetResampler (double rate)
        {
            needsResampling = rate > 0 && std::abs (rate - sampleRate) > 1.0;
            const double ratio = needsResampling ? rate / sampleRate : 1.0;
            // sized for any rate up to 4x the device's, so a model swap never allocates on the audio thread
            const int modelBlock = (int) std::ceil (maxBlock * juce::jmax (4.0, ratio)) + 8;
            const int cap = (modelBlock + maxBlock) * 4 + 256;
            if ((int) inFifo.size() < cap)  inFifo.assign ((size_t) cap, 0.0f);
            if ((int) midFifo.size() < cap) midFifo.assign ((size_t) cap, 0.0f);
            if ((int) modelBuf.size() < cap) modelBuf.assign ((size_t) cap, 0.0f);
            up.reset(); down.reset();
            inCount = 0;
            // Prime the second stage so it never runs dry; this is the converter's latency.
            midPrime = (int) std::ceil (maxBlock / sampleRate * rate) + 16;
            midCount = needsResampling ? midPrime : 0;
            std::fill (midFifo.begin(), midFifo.end(), 0.0f);
            const double lpRate = juce::jmax (rate, 1.0);
            for (auto& f : antiAlias)
                *f.coefficients = *Coeffs::makeLowPass (lpRate, juce::jmin (19000.0, sampleRate * 0.45), 0.707);
            for (auto& f : antiAlias) f.reset();
            setLatencySamples (needsResampling ? (int) std::round (midPrime * sampleRate / rate) + 2 : 0);
        }

       #if GALPON_WITH_NAM
        void processResampled (float* m, int n)
        {
            const double rate = modelRate.load();
            const double upSpeed = sampleRate / rate, downSpeed = rate / sampleRate;

            // 1) device rate -> model rate
            if (inCount + n > (int) inFifo.size()) inCount = 0;          // can't happen; stay safe
            std::copy (m, m + n, inFifo.begin() + inCount);
            inCount += n;
            int k = (int) std::floor ((inCount - 3) / upSpeed);
            k = juce::jlimit (0, juce::jmin ((int) modelBuf.size(), (int) midFifo.size() - midCount), k);
            int used = k > 0 ? up.process (upSpeed, inFifo.data(), modelBuf.data(), k) : 0;
            used = juce::jlimit (0, inCount, used);
            std::copy (inFifo.begin() + used, inFifo.begin() + inCount, inFifo.begin());
            inCount -= used;

            // 2) the amp itself, at the rate it was trained at
            if (k > 0)
            {
                float* in[1]  = { modelBuf.data() };
                float* out[1] = { modelBuf.data() };
                active->process (in, out, k);
                for (int i = 0; i < k; ++i)
                    modelBuf[(size_t) i] = antiAlias[1].processSample (antiAlias[0].processSample (modelBuf[(size_t) i]));
                std::copy (modelBuf.begin(), modelBuf.begin() + k, midFifo.begin() + midCount);
                midCount += k;
            }

            // 3) model rate -> device rate
            const int need = (int) std::ceil (n * downSpeed) + 3;
            if (midCount < need)
            {
                // ran dry (shouldn't): output silence for this block rather than garbage
                juce::FloatVectorOperations::clear (m, n);
                return;
            }
            int used2 = down.process (downSpeed, midFifo.data(), m, n);
            used2 = juce::jlimit (0, midCount, used2);
            std::copy (midFifo.begin() + used2, midFifo.begin() + midCount, midFifo.begin());
            midCount -= used2;
        }
       #endif

        void setStatus (const juce::String& s)
        {
            const juce::ScopedLock sl (statusLock);
            status = s;
        }

        juce::String getStatus() const
        {
            const juce::ScopedLock sl (statusLock);
            return status;
        }

        /** Deletes a model the audio thread has finished with (call off the audio thread). */
        void collectGarbage()
        {
           #if GALPON_WITH_NAM
            std::unique_ptr<nam::DSP> old;
            {
                const juce::SpinLock::ScopedLockType lock (swapLock);
                old = std::move (retired);
            }
           #endif
        }

        double sampleRate = 48000.0;
        int maxBlock = 512;
        juce::AudioBuffer<float> mono;
        juce::dsp::NoiseGate<float> gate;
        juce::dsp::IIR::Filter<float> bassF { Coeffs::makeAllPass (48000.0, 1000.0) }, trebleF { Coeffs::makeAllPass (48000.0, 1000.0) };
        float lastHash = -1.0f;
        juce::File modelFile;
        juce::String gearTypeValue;
        std::atomic<bool> needsCab { false };
        std::atomic<double> modelRate { 0.0 };
        bool needsResampling = false;
        const void* resampledFor = nullptr;
        std::vector<float> inFifo, midFifo, modelBuf;
        int inCount = 0, midCount = 0, midPrime = 0;
        juce::WindowedSincInterpolator up, down;
        juce::dsp::IIR::Filter<float> antiAlias[2] { juce::dsp::IIR::Filter<float> { Coeffs::makeAllPass (48000.0, 1000.0) },
                                                     juce::dsp::IIR::Filter<float> { Coeffs::makeAllPass (48000.0, 1000.0) } };
        juce::String status { juce::String (u8"Sin modelo: elige un archivo .nam") };
        juce::CriticalSection statusLock;
        juce::SpinLock swapLock;
       #if GALPON_WITH_NAM
        std::unique_ptr<nam::DSP> active, incoming, retired;
       #else
        std::unique_ptr<int> active, incoming, retired;
       #endif
    };
} // namespace

std::unique_ptr<BuiltinProcessor> createNamAmp() { return std::make_unique<NamAmp>(); }

} // namespace galpon
