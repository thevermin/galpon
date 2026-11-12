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
#include <complex>

namespace galpon
{
namespace
{
    /** Moves single notes, not the whole sound: a phase vocoder that shifts each spectral peak by
        its own amount (Laroche & Dolson's peak shifting). It finds the lowest note or two (the ones a
        drop tuning changes) and moves only their harmonics; everything else moves by `global`.
        A guitar has one pickup for all six strings, so it can't know for sure which string a note
        came from: a note in the range of the low string is taken to be on it. Close, not perfect. */
    class PeakShifter
    {
    public:
        // 4096-sample frames (~85 ms) tell a low E from the A next to it; 2048 (~43 ms) answers faster
        // but blurs two low notes played together. 75 % overlap.
        int order = 12, N = 4096, R = 1024;

        void prepare (double sr, int fftOrder)
        {
            sampleRate = sr;
            order = fftOrder; N = 1 << order; R = N / 4;
            fft = std::make_unique<juce::dsp::FFT> (order);
            window.resize (N);
            for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            reset();
        }
        void reset()
        {
            inFifo.assign ((size_t) N, 0.0f); fill = N - R;
            accum.assign ((size_t) N, 0.0f);
            ready.assign ((size_t) R, 0.0f); readPos = 0;
            prevPeaks.clear();
        }
        /** Semitones for everything, and for the notes on the lowest strings (index 0 = 6th string) */
        void setShifts (float globalSemis, float low6, float low5) { gSemis = globalSemis; r6 = low6; r5 = low5; }

        float process (float x)
        {
            const float y = ready[(size_t) readPos];
            if (++readPos >= R) readPos = 0;
            inFifo[(size_t) fill++] = x;
            if (fill == N)
            {
                processFrame();
                std::copy (inFifo.begin() + R, inFifo.end(), inFifo.begin());
                fill = N - R;
            }
            return y;
        }

    private:
        struct Peak { float outBin; double phase; };

        float peakMag (float hz) const
        {
            const float bin = hz / binHz();
            const int span = std::max (1, (int) std::round (bin * 0.025f));
            const int a = std::max (1, (int) std::round (bin) - span), b = std::min (N / 2 - 1, (int) std::round (bin) + span);
            float m = 0;
            for (int k = a; k <= b; ++k) m = std::max (m, mag[(size_t) k]);
            return m;
        }
        float binHz() const { return (float) sampleRate / (float) N; }
        static float hzOf (int midi) { return 440.0f * std::pow (2.0f, (float) (midi - 69) / 12.0f); }

        /** The lowest note among MIDI pitches [lo, hi] that clearly sounds now (or -1) */
        int lowestNote (int lo, int hi, int not1, float maxMag) const
        {
            std::array<float, 128> score {};
            float best = 0;
            for (int q = lo; q <= hi + 12; ++q)
            {
                float sc = 0;
                for (int h = 1; h <= 8; ++h) { const float f = hzOf (q) * (float) h; if (f > 4000.0f) break; sc += peakMag (f) / std::sqrt ((float) h); }
                score[(size_t) q] = sc;
                best = std::max (best, sc);
            }
            if (best <= 0) return -1;
            for (int q = lo; q <= hi; ++q)
            {
                if (q == not1 || q == not1 + 12 || q == not1 + 7 || q == not1 + 19) continue;
                if (score[(size_t) q] < 0.45f * best) continue;
                // the note itself, not a note an octave below whose harmonics look like it: odd harmonics must be there
                const float f = hzOf (q);
                if (std::max (peakMag (f), peakMag (3 * f)) < 0.12f * maxMag) continue;
                if (score[(size_t) q] < 0.7f * score[(size_t) q + 12] && peakMag (f) < 0.3f * peakMag (2 * f)) continue;
                return q;
            }
            return -1;
        }

        void processFrame()
        {
            std::vector<float> data ((size_t) N * 2, 0.0f);
            for (int i = 0; i < N; ++i) data[(size_t) i] = inFifo[(size_t) i] * window[(size_t) i];
            fft->performRealOnlyForwardTransform (data.data());
            const int half = N / 2;
            mag.assign ((size_t) half + 1, 0.0f);
            ph.assign ((size_t) half + 1, 0.0f);
            float maxMag = 0;
            for (int k = 0; k <= half; ++k)
            {
                const float re = data[(size_t) (2 * k)], im = data[(size_t) (2 * k + 1)];
                mag[(size_t) k] = std::sqrt (re * re + im * im);
                ph[(size_t) k] = std::atan2 (im, re);
                maxMag = std::max (maxMag, mag[(size_t) k]);
            }

            // which notes are on the strings that move differently (standard tuning: 6th = E2 (40), 5th = A2 (45))
            int note6 = -1, note5 = -1;
            if (maxMag > 1.0e-4f)
            {
                if (std::abs (r6) > 0.01f) note6 = lowestNote (40, 47, -100, maxMag);
                if (std::abs (r5) > 0.01f) note5 = lowestNote (45, 52, note6, maxMag);
            }
            const double g = std::pow (2.0, gSemis / 12.0);
            auto ratioFor = [&] (float hz) -> double
            {
                for (auto [note, semis] : { std::pair<int, float> { note6, r6 }, std::pair<int, float> { note5, r5 } })
                {
                    if (note < 0) continue;
                    const float f0 = hzOf (note);
                    const float h = std::round (hz / f0);
                    if (h >= 1 && h <= 16 && std::abs (hz - h * f0) <= std::max (0.03f * h * f0, 0.7f * binHz())) return g * std::pow (2.0, semis / 12.0);
                }
                return g;
            };

            // peaks, and the region of bins that belongs to each (down to the lowest point between peaks)
            std::vector<int> peaks;
            for (int k = 2; k < half - 1; ++k)
                if (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1] && mag[(size_t) k] > maxMag * 1.0e-4f) peaks.push_back (k);
            std::vector<std::complex<float>> Y ((size_t) half + 1, { 0.0f, 0.0f });
            std::vector<Peak> now;
            int regionStart = 0;
            for (size_t i = 0; i < peaks.size(); ++i)
            {
                const int k = peaks[i];
                int regionEnd = half;
                if (i + 1 < peaks.size())
                {
                    regionEnd = k;
                    for (int b = k; b < peaks[i + 1]; ++b) if (mag[(size_t) b] < mag[(size_t) regionEnd]) regionEnd = b;
                }
                // the peak's true frequency (between bins)
                const float a = std::log (mag[(size_t) k - 1] + 1e-12f), b0 = std::log (mag[(size_t) k] + 1e-12f), c = std::log (mag[(size_t) k + 1] + 1e-12f);
                const float den = a - 2 * b0 + c;
                const float kf = (float) k + (std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f);
                const double ratio = ratioFor (kf * binHz());
                if (std::abs (ratio - 1.0) < 1.0e-6)
                {
                    for (int bb = regionStart; bb < regionEnd; ++bb) Y[(size_t) bb] += std::polar (mag[(size_t) bb], ph[(size_t) bb]);
                }
                else
                {
                    const float kNew = (float) (kf * ratio);
                    const int shift = (int) std::lround (kNew - (float) k);
                    // keep its phase running at the new frequency from frame to frame
                    double phase = ph[(size_t) k];
                    for (auto& pp : prevPeaks)
                        if (std::abs (pp.outBin - kNew) < 1.5f) { phase = pp.phase + juce::MathConstants<double>::twoPi * (double) kNew * R / N; break; }
                    phase = std::remainder (phase, juce::MathConstants<double>::twoPi);
                    now.push_back ({ kNew, phase });
                    for (int bb = regionStart; bb < regionEnd; ++bb)
                    {
                        const int dst = bb + shift;
                        if (dst <= 0 || dst >= half) continue;
                        Y[(size_t) dst] += std::polar (mag[(size_t) bb], (float) (phase + ph[(size_t) bb] - ph[(size_t) k]));
                    }
                }
                regionStart = regionEnd;
            }
            prevPeaks = std::move (now);

            for (int k = 0; k <= half; ++k) { data[(size_t) (2 * k)] = Y[(size_t) k].real(); data[(size_t) (2 * k + 1)] = Y[(size_t) k].imag(); }
            for (int k = half + 1; k < N; ++k) { data[(size_t) (2 * k)] = Y[(size_t) (N - k)].real(); data[(size_t) (2 * k + 1)] = -Y[(size_t) (N - k)].imag(); }
            fft->performRealOnlyInverseTransform (data.data());
            const float norm = 1.0f / 1.5f;                       // Hann in, Hann out, 75 % overlap
            for (int i = 0; i < N; ++i) accum[(size_t) i] += data[(size_t) i] * window[(size_t) i] * norm;
            std::copy (accum.begin(), accum.begin() + R, ready.begin());
            std::copy (accum.begin() + R, accum.end(), accum.begin());
            std::fill (accum.end() - R, accum.end(), 0.0f);
            readPos = 0;
        }

        double sampleRate = 48000.0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, inFifo, accum, ready, mag, ph;
        int fill = 0, readPos = 0;
        float gSemis = 0, r6 = 0, r5 = 0;
        std::vector<Peak> prevPeaks;
    };

    class RetuneEffect : public BuiltinProcessor
    {
    public:
        RetuneEffect() : BuiltinProcessor ("retune", juce::String (u8"Afinación virtual"), false)
        {
            addParam ("semitones", "Todas las cuerdas", -7.0f, 7.0f, 0.0f, "st");   // beyond a fifth it stops sounding like a guitar
            addParam ("cents", juce::String (u8"Ajuste fino"), -50.0f, 50.0f, 0.0f, "cents");
            addParam ("mix", "Mezcla", 0.0f, 1.0f, 1.0f);
            // the tuning to sound like, string by string, from a guitar in standard tuning (default: Drop D)
            const char* names[] = { "6ª cuerda", "5ª cuerda", "4ª cuerda", "3ª cuerda", "2ª cuerda", "1ª cuerda" };
            for (int i = 0; i < 6; ++i)
                addParam ("s" + juce::String (6 - i), juce::String::fromUTF8 (names[i]), -7.0f, 7.0f, i == 0 ? -2.0f : 0.0f, "st");
            // per string: better separation of the low notes (~85 ms) or less delay (~43 ms)
            addParam ("fast", juce::String::fromUTF8 ("Menos demora"), 0.0f, 1.0f, 0.0f);
        }

        /** How each string sounds, in semitones from standard (6th string first) */
        std::array<float, 6> strings() const
        {
            std::array<float, 6> e {};
            for (int i = 0; i < 6; ++i) e[(size_t) i] = std::round (get ("semitones") + get ("s" + juce::String (6 - i)));
            return e;
        }

        void prepareToPlay (double sr, int) override
        {
            sampleRate = sr;
            shifterOrder = get ("fast") > 0.5f ? 11 : 12;
            shifter.prepare (sr, shifterOrder);
            dryRing.assign ((size_t) shifter.N, 0.0f); dryPos = 0;
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
            // What all strings share is moved as one (exact, ~25 ms); the low strings that differ from
            // the rest (a drop tuning) are moved note by note (~43 ms)
            const auto e = strings();
            float global = e[5];
            {
                int bestCount = 0;
                for (int i = 5; i >= 2; --i)    // the upper four strings decide
                {
                    int c = 0;
                    for (int j = 2; j < 6; ++j) if (e[(size_t) j] == e[(size_t) i]) ++c;
                    if (c > bestCount) { bestCount = c; global = e[(size_t) i]; }
                }
            }
            const float r6 = e[0] - global, r5 = e[1] - global;
            const float cents = get ("cents") / 100.0f;
            const bool perString = std::abs (r6) > 0.01f || std::abs (r5) > 0.01f;
            if (perString && (get ("fast") > 0.5f ? 11 : 12) != shifterOrder)
            {
                shifterOrder = get ("fast") > 0.5f ? 11 : 12;
                shifter.prepare (sampleRate, shifterOrder);
                dryRing.assign ((size_t) shifter.N, 0.0f); dryPos = 0;
            }
            const int wantLatency = perString ? shifter.N : latency;
            if (getLatencySamples() != wantLatency) setLatencySamples (wantLatency);
            if (perString)
            {
                shifter.setShifts (global + cents, r6, r5);
                const float mixPv = get ("mix");
                for (int i = 0; i < n; ++i)
                {
                    const float x = chans > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i);
                    const float dry = dryRing[(size_t) dryPos];
                    dryRing[(size_t) dryPos] = x;
                    dryPos = (dryPos + 1) % (int) dryRing.size();
                    const float y = shifter.process (x) * mixPv + dry * (1.0f - mixPv);
                    for (int ch = 0; ch < chans; ++ch) b.setSample (ch, i, y);
                }
                return;
            }
            const double ratio = juce::jlimit (1.0 / maxRatio, maxRatio, std::pow (2.0, (global + cents) / 12.0));
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

        PeakShifter shifter;
        int shifterOrder = 12;
        double sampleRate = 48000.0;
        std::vector<float> dryRing;
        int dryPos = 0;
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
