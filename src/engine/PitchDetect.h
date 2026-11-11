#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace galpon
{

/** The pitch of a single plucked string (for the tuner): YIN, with the tolerances a real guitar needs
    (strong harmonics, a decaying note, some hum). Returns the frequency in Hz, or 0 when there isn't
    a clear note. `level` gets the RMS of the buffer. */
inline double detectPitch (const float* x, int n, double sampleRate, float& level)
{
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += (double) x[i] * x[i];
    level = (float) std::sqrt (sum / std::max (1, n));
    if (level < 0.0012f || n < 256) return 0.0;   // ~ -58 dBFS: a guitar straight into an interface is quieter than people think

    // remove DC and normalise, so quiet inputs work as well as loud ones
    double mean = 0;
    for (int i = 0; i < n; ++i) mean += x[i];
    mean /= n;
    std::vector<float> b ((size_t) n);
    for (int i = 0; i < n; ++i) b[(size_t) i] = (float) ((x[i] - mean) / level);

    const int maxLag = std::min (n / 2, (int) (sampleRate / 27.0));    // down to a low A on a 5-string bass (~31 Hz)… and drop tunings
    const int minLag = std::max (2, (int) (sampleRate / 1400.0));
    const int win = n - maxLag;
    std::vector<float> d ((size_t) maxLag + 2, 1.0f);
    std::vector<double> raw ((size_t) maxLag + 2, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (int i = 0; i < win; ++i) { const double df = b[(size_t) i] - b[(size_t) (i + lag)]; s += df * df; }
        raw[(size_t) lag] = s;
    }
    double running = 0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += raw[(size_t) lag];
        d[(size_t) lag] = running > 0 ? (float) (raw[(size_t) lag] * lag / running) : 1.0f;
    }

    // the first dip under the threshold (the fundamental, not a harmonic); if none, the deepest dip
    int best = -1;
    for (const float threshold : { 0.12f, 0.2f, 0.3f })
    {
        for (int lag = minLag; lag < maxLag; ++lag)
            if (d[(size_t) lag] < threshold)
            {
                while (lag + 1 < maxLag && d[(size_t) lag + 1] < d[(size_t) lag]) ++lag;
                best = lag;
                break;
            }
        if (best > 0) break;
    }
    if (best < 0)
    {
        float m = 1.0f;
        for (int lag = minLag; lag < maxLag; ++lag) if (d[(size_t) lag] < m) { m = d[(size_t) lag]; best = lag; }
        if (m > 0.45f) return 0.0;
    }

    double lag = best;
    if (best > 1 && best < maxLag - 1)
    {
        const double a = d[(size_t) best - 1], c0 = d[(size_t) best], c = d[(size_t) best + 1];
        const double den = a - 2 * c0 + c;
        if (std::abs (den) > 1.0e-9) lag = best + 0.5 * (a - c) / den;
    }
    return sampleRate / lag;
}

} // namespace galpon
