// Flex: pitch detection (YIN), note and onset detection, and rendering audio through a time map
// and a pitch curve with Signalsmith Stretch (MIT).

#include "Flex.h"
#include <juce_dsp/juce_dsp.h>
#include <signalsmith-stretch.h>

namespace galpon::flex
{

//==============================================================================
std::vector<PitchFrame> detectPitch (const float* mono, int numSamples, double sampleRate)
{
    std::vector<PitchFrame> frames;
    if (numSamples <= 0) return frames;

    // Work at ~12 kHz: plenty for voices and instruments, and 16x cheaper
    const int D = juce::jmax (1, (int) std::round (sampleRate / 12000.0));
    const double fs = sampleRate / D;
    std::vector<float> x ((size_t) (numSamples / D + 1));
    {
        juce::dsp::IIR::Filter<float> lp1, lp2;
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, fs * 0.42);
        lp1.coefficients = c; lp2.coefficients = c;
        int k = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            const float y = lp2.processSample (lp1.processSample (mono[i]));
            if (i % D == 0 && k < (int) x.size()) x[(size_t) k++] = y;
        }
        x.resize ((size_t) k);
    }

    const int W = (int) (fs * 0.033);                // analysis window
    const int minLag = (int) (fs / 1000.0), maxLag = (int) (fs / 60.0);
    const int hop = juce::jmax (1, (int) (fs * 0.005));
    const int n = (int) x.size();
    std::vector<float> d ((size_t) maxLag + 2), cmnd ((size_t) maxLag + 2);

    float maxRms = 0;
    std::vector<float> rmsOf;
    for (int start = 0; start + W + maxLag < n; start += hop)
    {
        double e = 0;
        for (int j = 0; j < W; ++j) e += x[(size_t) (start + j)] * x[(size_t) (start + j)];
        rmsOf.push_back ((float) std::sqrt (e / W));
        maxRms = juce::jmax (maxRms, rmsOf.back());
    }

    int f = 0;
    for (int start = 0; start + W + maxLag < n; start += hop, ++f)
    {
        PitchFrame pf;
        pf.time = (start + W / 2) / fs;
        if (rmsOf[(size_t) f] < 0.002f || rmsOf[(size_t) f] < maxRms * 0.02f) { frames.push_back (pf); continue; }

        const float* a = x.data() + start;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float s = 0;
            const float* b = a + tau;
            for (int j = 0; j < W; ++j) { const float diff = a[j] - b[j]; s += diff * diff; }
            d[(size_t) tau] = s;
        }
        float running = 0;
        cmnd[0] = 1;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0 ? d[(size_t) tau] * tau / running : 1.0f;
        }
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // no clear dip: take the global minimum if it's reasonably periodic
            best = minLag;
            for (int tau = minLag; tau < maxLag; ++tau) if (cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;
            if (cmnd[(size_t) best] > 0.3f) { frames.push_back (pf); continue; }
        }
        // parabolic interpolation for sub-sample accuracy
        double lag = best;
        if (best > 1 && best < maxLag - 1)
        {
            const double s0 = cmnd[(size_t) best - 1], s1 = cmnd[(size_t) best], s2 = cmnd[(size_t) best + 1];
            const double den = s0 - 2 * s1 + s2;
            if (std::abs (den) > 1.0e-9) lag += 0.5 * (s0 - s2) / den;
        }
        const double hz = fs / lag;
        pf.midi = (float) (69.0 + 12.0 * std::log2 (hz / 440.0));
        frames.push_back (pf);
    }
    return frames;
}

std::vector<PitchNote> segmentNotes (const std::vector<PitchFrame>& frames)
{
    std::vector<PitchNote> notes;
    std::vector<float> current;
    double start = 0, lastTime = 0;
    auto close = [&]
    {
        if (current.size() >= 2 && lastTime - start >= 0.06)
        {
            auto sorted = current;
            std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
            notes.push_back ({ start, lastTime - start, sorted[sorted.size() / 2] });
        }
        current.clear();
    };
    for (auto& f : frames)
    {
        if (f.midi < 0)
        {
            if (! current.empty() && f.time - lastTime > 0.03) close();
            continue;
        }
        if (! current.empty())
        {
            // compare with the recent average: vibrato stays in the note, a new note doesn't
            float avg = 0;
            const size_t k = std::min<size_t> (current.size(), 6);
            for (size_t i = current.size() - k; i < current.size(); ++i) avg += current[i];
            avg /= (float) k;
            if (std::abs (f.midi - avg) > 0.75f) close();
        }
        if (current.empty()) start = f.time;
        current.push_back (f.midi);
        lastTime = f.time;
    }
    close();
    return notes;
}

//==============================================================================
std::vector<double> detectOnsets (const float* mono, int numSamples, double sampleRate)
{
    std::vector<double> onsets;
    constexpr int order = 10, size = 1 << order, hop = 256;
    if (numSamples < size) return onsets;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buf ((size_t) size * 2), prev ((size_t) size / 2, 0.0f);
    std::vector<float> flux;
    std::vector<float> energy;
    for (int start = 0; start + size < numSamples; start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        std::copy (mono + start, mono + start + size, buf.begin());
        win.multiplyWithWindowingTable (buf.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float fl = 0, en = 0;
        for (int k = 1; k < size / 2; ++k)
        {
            const float m = std::log1p (100.0f * buf[(size_t) k]);
            fl += juce::jmax (0.0f, m - prev[(size_t) k]);
            prev[(size_t) k] = m;
            en += buf[(size_t) k];
        }
        flux.push_back (fl);
        energy.push_back (en);
    }
    const int n = (int) flux.size();
    float maxFlux = 0;
    for (auto v : flux) maxFlux = juce::jmax (maxFlux, v);
    if (maxFlux <= 0) return onsets;
    double lastOnset = -1.0;
    for (int i = 1; i < n - 1; ++i)
    {
        // local peak, clearly above the neighbourhood average
        bool peak = true;
        for (int j = juce::jmax (0, i - 3); j <= juce::jmin (n - 1, i + 3); ++j) if (flux[(size_t) j] > flux[(size_t) i]) { peak = false; break; }
        if (! peak) continue;
        double avg = 0; int cnt = 0;
        for (int j = juce::jmax (0, i - 12); j <= juce::jmin (n - 1, i + 6); ++j) { avg += flux[(size_t) j]; ++cnt; }
        avg /= juce::jmax (1, cnt);
        if (flux[(size_t) i] < avg * 1.5 + maxFlux * 0.08) continue;
        // the hit happens where the frame starts to rise (window centre minus a little)
        const double t = (i * hop + size * 0.5 - hop) / sampleRate;
        if (lastOnset >= 0 && t - lastOnset < 0.07) continue;
        onsets.push_back (juce::jmax (0.0, t));
        lastOnset = t;
    }
    return onsets;
}

//==============================================================================
double mapForward (const TimeMap& m, double s)
{
    if (m.size() < 2) return s;
    size_t i = 1;
    while (i < m.size() - 1 && m[i].first < s) ++i;
    const auto& a = m[i - 1];
    const auto& b = m[i];
    const double span = b.first - a.first;
    return span > 0 ? a.second + (s - a.first) * (b.second - a.second) / span : a.second;
}

double mapBackward (const TimeMap& m, double o)
{
    if (m.size() < 2) return o;
    size_t i = 1;
    while (i < m.size() - 1 && m[i].second < o) ++i;
    const auto& a = m[i - 1];
    const auto& b = m[i];
    const double span = b.second - a.second;
    return span > 0 ? a.first + (o - a.second) * (b.first - a.first) / span : a.first;
}

juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, double sampleRate, const TimeMap& map, int outputLength,
                                 const std::function<float (double)>& semitonesAt)
{
    const int channels = juce::jlimit (1, 2, source.getNumChannels());
    const int srcLen = source.getNumSamples();
    juce::AudioBuffer<float> out (channels, juce::jmax (1, outputLength));
    out.clear();

    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (channels, (float) sampleRate);
    st.setFormantFactor (1.0f, true);           // keep the voice's character when changing its pitch
    const int inLat = st.inputLatency(), outLat = st.outputLatency();

    constexpr int block = 256;
    const int maxIn = block * 8 + 64;
    std::vector<std::vector<float>> in ((size_t) channels, std::vector<float> ((size_t) maxIn));
    std::vector<std::vector<float>> o ((size_t) channels, std::vector<float> ((size_t) block));

    juce::int64 inPos = 0;
    const juce::int64 total = (juce::int64) outputLength + outLat;
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<juce::int64> (block, total - pos);
        // how much input should have gone in once these outputs come out
        // the stretcher outputs input[fed - inputLatency] after outputLatency more samples, so feed
        // up to where output (pos + n) should be, plus the input latency; the first outLat outputs are dropped
        double want = mapBackward (map, (double) (pos + n)) + inLat;
        auto target = (juce::int64) std::llround (want);
        target = std::clamp<juce::int64> (target, inPos, inPos + maxIn);
        const int k = (int) (target - inPos);

        const double srcPos = juce::jlimit (0.0, (double) juce::jmax (0, srcLen - 1), (double) (inPos - inLat));
        st.setTransposeSemitones (semitonesAt ? semitonesAt (srcPos) : 0.0f);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* src = source.getReadPointer (juce::jmin (ch, source.getNumChannels() - 1));
            for (int i = 0; i < k; ++i)
            {
                const juce::int64 s = inPos + i;
                in[(size_t) ch][(size_t) i] = (s >= 0 && s < srcLen) ? src[s] : 0.0f;
            }
        }
        st.process (in, k, o, n);
        inPos += k;

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 dst = pos + i - outLat;
                if (dst >= 0 && dst < outputLength) out.setSample (ch, (int) dst, o[(size_t) ch][(size_t) i]);
            }
    }
    return out;
}

} // namespace galpon::flex
