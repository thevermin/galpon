// "Afinación virtual": shifts the whole guitar up or down by semitones, so a guitar in standard
// tuning sounds like one tuned down (D standard, Eb, C…). It can never be the same as tuning the
// strings, but it's enough to try a song or learn one quickly.
//
// How: a delay line read slower (or faster) than it is written, which moves every note by exactly
// the same interval, like playing a tape at another speed. Every few hundred milliseconds the
// reading jumps back to keep the delay short; the jump is made where the waveform lines up with
// itself and with a short crossfade, so it doesn't click or warble. A spectral shifter (the one the
// game uses to slow songs down) needs ~80 ms windows to stay in tune; this one stays in tune with
// ~25 ms of delay, which is what lets you play along.

#include "Builtins.h"

namespace galpon
{
namespace
{
    class RetuneEffect : public BuiltinProcessor
    {
    public:
        RetuneEffect() : BuiltinProcessor ("retune", juce::String (u8"Afinación virtual"), false)
        {
            addParam ("semitones", "Semitonos", -7.0f, 7.0f, -2.0f, "st");   // beyond a fifth it stops sounding like a guitar
            addParam ("cents", juce::String (u8"Ajuste fino"), -50.0f, 50.0f, 0.0f, "cents");
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 1.0f);
        }

        void prepareToPlay (double sr, int) override
        {
            fade = juce::jmax (64, (int) std::round (sr * 0.008));          // crossfade at each jump
            search = juce::jmax (64, (int) std::round (sr * 0.016));        // one period of a low B: where to look for a good jump
            lo = (int) std::ceil (fade * maxRatio) + 8;                      // the shortest delay (reading must stay behind writing)
            span = search + (int) std::round (sr * 0.012);                   // how far the delay travels between jumps
            hi = lo + span;
            latency = lo + span / 2;
            setLatencySamples (latency);
            size = juce::nextPowerOfTwo (hi + search + fade * 4 + 16);
            mask = size - 1;
            buf.setSize (2, size);
            buf.clear();
            w = 0;
            heads[0] = { (double) latency, 1.0f, true };
            heads[1] = { (double) latency, 0.0f, false };
            active = 0;
            fading = 0;
        }
        void releaseResources() override {}
        double getTailLengthSeconds() const override { return 0.05; }

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const int n = b.getNumSamples();
            const int chans = juce::jmin (2, b.getNumChannels());
            if (n == 0 || chans < 1 || size == 0) return;
            const double ratio = juce::jlimit (1.0 / maxRatio, maxRatio, std::pow (2.0, (get ("semitones") + get ("cents") / 100.0) / 12.0));
            const float mix = get ("mix");
            const double step = 1.0 - ratio;   // how the delay changes each sample (grows when going down)

            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, w, b.getSample (juce::jmin (ch, chans - 1), i));

                // time to jump? (the active head is about to leave the window)
                if (fading == 0)
                {
                    const auto& a = heads[active];
                    const bool leaving = step > 1.0e-9 ? a.delay + step * fade >= hi
                                       : step < -1.0e-9 ? a.delay + step * fade <= lo
                                       : false;
                    if (leaving) startJump (step, ratio);
                }

                float outL = 0.0f, outR = 0.0f;
                for (auto& h : heads)
                {
                    if (! h.on) continue;
                    const double pos = (double) w - h.delay;
                    outL += h.gain * read (0, pos);
                    outR += h.gain * read (1, pos);
                    h.delay = juce::jlimit ((double) lo - 4.0, (double) (hi + search), h.delay + step);
                }
                if (fading > 0)
                {
                    --fading;
                    const float t = 1.0f - (float) fading / (float) fade;
                    const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t);
                    heads[active].gain = g;
                    heads[1 - active].gain = 1.0f - g;
                    if (fading == 0) heads[1 - active].on = false;
                }
                const float dryL = read (0, (double) w - latency), dryR = read (1, (double) w - latency);
                b.setSample (0, i, outL * mix + dryL * (1.0f - mix));
                if (chans > 1) b.setSample (1, i, outR * mix + dryR * (1.0f - mix));
                w = (w + 1) & mask;
            }
        }

    private:
        struct Head { double delay; float gain; bool on; };

        float read (int ch, double pos) const
        {
            // cubic (Hermite) interpolation
            const double fl = std::floor (pos);
            const float f = (float) (pos - fl);
            const int i = (int) fl;
            const float* d = buf.getReadPointer (ch);
            const float y0 = d[(i - 1) & mask], y1 = d[i & mask], y2 = d[(i + 1) & mask], y3 = d[(i + 2) & mask];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }

        void startJump (double step, double ratio)
        {
            // the old head's next `fade` samples, against candidates at the other end of the window:
            // pick the one that looks most like it (same phase), so the crossfade is seamless
            const auto& a = heads[active];
            auto sample = [this] (double pos) { return read (0, pos) + read (1, pos); };
            double bestScore = -1.0e30;
            double bestDelay = step > 0 ? (double) lo : (double) hi;
            for (int off = 0; off <= search; off += 2)
            {
                const double d0 = step > 0 ? lo + off : hi - off;
                double dot = 0.0, e = 1.0e-9;
                for (int k = 0; k < fade; k += 3)
                {
                    // where each head will read k samples from now (all already written)
                    const float x = sample ((double) w - a.delay + k * ratio);
                    const float y = sample ((double) w - d0 + k * ratio);
                    dot += x * y; e += y * y;
                }
                const double score = dot / std::sqrt (e);
                if (score > bestScore) { bestScore = score; bestDelay = d0; }
            }
            active = 1 - active;
            heads[active] = { bestDelay, 0.0f, true };
            fading = fade;
        }

        static constexpr double maxRatio = 1.4983;   // 7 semitones
        juce::AudioBuffer<float> buf;
        int size = 0, mask = 0, w = 0;
        int fade = 384, search = 768, lo = 600, span = 1300, hi = 1900, latency = 1250;
        Head heads[2] {};
        int active = 0, fading = 0;
    };
}

std::unique_ptr<BuiltinProcessor> createRetune() { return std::make_unique<RetuneEffect>(); }

} // namespace galpon
