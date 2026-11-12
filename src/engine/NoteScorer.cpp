#include "NoteScorer.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace galpon
{

namespace
{
    constexpr float levelFloor = 0.0025f;   // ~ -52 dBFS: below this there's nothing being played
    constexpr int envBlock = 64;            // onset envelope resolution (~1.3 ms)
    float midiToHz (int p, double scale) { return (float) (440.0 * scale * std::pow (2.0, (p - 69) / 12.0)); }
}

NoteScorer::NoteScorer() : juce::Thread ("Galpon detector de notas")
{
    ring.assign ((size_t) ringSize, 0.0f);
    times.assign ((size_t) ringSize, 0.0);
    fft1k = std::make_unique<juce::dsp::FFT> (10);
    fft4k = std::make_unique<juce::dsp::FFT> (12);
    fft8k = std::make_unique<juce::dsp::FFT> (13);
    fft16k = std::make_unique<juce::dsp::FFT> (14);
}

NoteScorer::~NoteScorer() { stopThread (2000); }

void NoteScorer::prepare (double sr) { sampleRate = sr > 0 ? sr : 48000.0; }

void NoteScorer::setEvents (std::vector<Event> ev, float cents)
{
    {
        const juce::ScopedLock sl (eventLock);
        events = std::move (ev);
        std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.t < b.t; });
        freqScale = std::pow (2.0, cents / 1200.0);
        seenJumps = -1;          // start over from wherever the song is now
        pendingSustains.clear();
    }
    { const juce::ScopedLock sl (outLock); out.clear(); }
    if (! manual && ! isThreadRunning()) startThread (juce::Thread::Priority::low);
}

void NoteScorer::clearEvents()
{
    if (! manual) stopThread (2000);
    const juce::ScopedLock sl (eventLock);
    events.clear();
    pendingSustains.clear();
}

void NoteScorer::setEnabled (bool on) { enabled = on; }

void NoteScorer::push (const float* in, int n, double songTime0, double step, bool playing)
{
    if (in == nullptr || n <= 0) return;
    float sum = 0;
    for (int i = 0; i < n; ++i) sum += in[i] * in[i];
    const float rms = std::sqrt (sum / (float) n);
    level.store (std::max (rms, level.load() * 0.97f));
    if (! playing) return;

    auto w = written.load (std::memory_order_relaxed);
    // a loop, a seek or a new song: the samples before don't continue these
    if (std::abs (songTime0 - expectedNext) > 0.02 + std::abs (step) * 4)
    {
        jumpAt.store (w, std::memory_order_relaxed);
        jumps.fetch_add (1, std::memory_order_release);
    }
    expectedNext = songTime0 + step * n;
    lastStep.store (step);
    for (int i = 0; i < n; ++i)
    {
        const auto k = (size_t) ((w + i) & (ringSize - 1));
        ring[k] = in[i];
        times[k] = songTime0 + step * i;
    }
    written.store (w + n, std::memory_order_release);
}

std::vector<NoteScorer::Judgment> NoteScorer::take()
{
    const juce::ScopedLock sl (outLock);
    std::vector<Judgment> r;
    r.swap (out);
    return r;
}

void NoteScorer::run()
{
    while (! threadShouldExit())
    {
        work();
        wait (12);
    }
}

juce::int64 NoteScorer::indexForTime (double t) const
{
    const auto w = written.load (std::memory_order_acquire);
    juce::int64 lo = std::max<juce::int64> (validFrom, w - ringSize + 1), hi = w;
    if (lo >= hi) return -1;
    if (times[(size_t) ((hi - 1) & (ringSize - 1))] < t) return -1;    // not there yet
    while (lo < hi)
    {
        const auto m = lo + (hi - lo) / 2;
        if (times[(size_t) (m & (ringSize - 1))] < t) lo = m + 1; else hi = m;
    }
    return lo;
}

void NoteScorer::work()
{
    const juce::ScopedLock sl (eventLock);
    const auto w = written.load (std::memory_order_acquire);
    const int j = jumps.load (std::memory_order_acquire);
    if (j != seenJumps)
    {
        // judge from the jump on: notes due before it were never heard
        const bool fresh = seenJumps == -1;
        seenJumps = j;
        validFrom = fresh ? std::max<juce::int64> (jumpAt.load(), w > 0 ? w - 1 : 0) : jumpAt.load();
        ++pass;
        pendingSustains.clear();
        const double t0 = validFrom < w ? times[(size_t) (validFrom & (ringSize - 1))] : 1.0e12;
        next = (int) (std::lower_bound (events.begin(), events.end(), t0 + 0.02,
                                        [] (const Event& e, double t) { return e.t < t; }) - events.begin());
        done.assign (events.size(), 0);
        for (int k = 0; k < next; ++k) done[(size_t) k] = 1;
    }
    if (! enabled.load() || w <= validFrom + 2) return;

    const double speed = std::max (0.1, lastStep.load() * sampleRate);
    if (done.size() != events.size()) done.assign (events.size(), 0);

    // A hit is reported as soon as the attack and enough of the note have been heard (~0.1 s), not
    // when the note's whole time window is over: that made every hit show up almost half a second late.
    // A miss can only be decided when the window is over.
    while (next < (int) events.size() && done[(size_t) next]) ++next;
    for (int k = next; k < (int) events.size(); ++k)
    {
        if (done[(size_t) k]) continue;
        const auto& e = events[(size_t) k];
        const auto iEnd = indexForTime (e.t + late * speed);
        if (iEnd >= 0)
        {
            if (judge (e, k, iEnd, true)) done[(size_t) k] = 1;
            continue;
        }
        if (indexForTime (e.t - early * speed) < 0) break;     // this one (and the rest) haven't started
        if (judge (e, k, w - envBlock * 7, false)) done[(size_t) k] = 1;
    }
    while (next < (int) events.size() && done[(size_t) next]) ++next;
    for (size_t k = 0; k < pendingSustains.size();)
    {
        const auto i = indexForTime (pendingSustains[k].to);
        if (i >= 0 && w - i > 4096 + 64) { judgeSustain ((int) k); pendingSustains.erase (pendingSustains.begin() + (long) k); }
        else ++k;
    }
}

void NoteScorer::spectrum (juce::int64 start, int len, std::vector<float>& mag)
{
    const int fftSize = len * 2;
    // the FFT is twice the window (zero-padded); each size has its own
    auto& f = len <= 512 ? *fft1k : len <= 2048 ? *fft4k : len <= 4096 ? *fft8k : *fft16k;
    work1.assign ((size_t) fftSize * 2, 0.0f);
    for (int i = 0; i < len; ++i)
    {
        const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (len - 1));
        work1[(size_t) i] = ring[(size_t) ((start + i) & (ringSize - 1))] * win;
    }
    f.performFrequencyOnlyForwardTransform (work1.data(), true);
    mag.assign (work1.begin(), work1.begin() + fftSize / 2);
    for (auto& m : mag) m = std::sqrt (m);        // compress: one loud partial shouldn't decide alone
}

float NoteScorer::harmonicSum (const std::vector<float>& mag, int fftSize, float f0) const
{
    const float binHz = (float) sampleRate / (float) fftSize;
    float sum = 0;
    for (int h = 1; h <= 8; ++h)
    {
        const float f = f0 * (float) h;
        if (f > 5000.0f) break;
        const float c = f / binHz;
        const int span = std::max (1, (int) std::round (c * 0.02f));      // ±35 cents
        const int lo = std::max (1, (int) std::round (c) - span), hi = std::min ((int) mag.size() - 1, (int) std::round (c) + span);
        float peak = 0;
        for (int k = lo; k <= hi; ++k) peak = std::max (peak, mag[(size_t) k]);
        sum += peak / std::sqrt ((float) h);
    }
    return sum;
}

int NoteScorer::countHeard (const std::vector<float>& mag, int fftSize, const std::vector<int>& pitches, float need) const
{
    std::array<float, 128> hs {};
    for (int q = 23; q <= 100; ++q) hs[(size_t) q] = harmonicSum (mag, fftSize, midiToHz (q, freqScale));
    const float binHz = (float) sampleRate / (float) fftSize;
    auto peakAt = [&] (float f)
    {
        const float c = f / binHz;
        const int span = std::max (1, (int) std::round (c * 0.02f));
        float pk = 0;
        for (int k = std::max (1, (int) std::round (c) - span); k <= std::min ((int) mag.size() - 1, (int) std::round (c) + span); ++k) pk = std::max (pk, mag[(size_t) k]);
        return pk;
    };
    int heard = 0;
    for (auto p : pitches)
    {
        if (p < 23 || p > 100) continue;
        // the strongest other note, leaving out this one's octaves above (a weak fundamental makes those look strong)
        // (in a chord the other notes, octaves included, are supposed to sound: compare with the loudest one)
        const bool chord = pitches.size() > 1;
        float rival = 0;
        for (int q = 23; q <= 100; ++q)
            if (chord || (q != p && q != p + 12 && q != p + 24)) rival = std::max (rival, hs[(size_t) q]);
        if (hs[(size_t) p] < need * std::max (rival, hs[(size_t) p])) continue;
        if (chord) { ++heard; continue; }
        // the octave above sounds stronger: is the real fundamental there, or was it played an octave up?
        const float up = std::max (p + 12 <= 100 ? hs[(size_t) p + 12] : 0.0f, p + 24 <= 100 ? hs[(size_t) p + 24] : 0.0f);
        if (up > hs[(size_t) p])
        {
            const float f0 = midiToHz (p, freqScale);
            const float odd = (peakAt (f0) + peakAt (3 * f0)) * 0.5f;
            const float between = (peakAt (1.5f * f0) + peakAt (2.5f * f0) + peakAt (3.5f * f0)) / 3.0f;
            if (odd < 2.0f * between) continue;
        }
        ++heard;
    }
    return heard;
}

bool NoteScorer::judge (const Event& e, int index, juce::int64 iEnd, bool final)
{
    Judgment j;
    j.index = index;
    j.pass = pass;
    const double speed = std::max (0.1, lastStep.load() * sampleRate);
    auto iStart = indexForTime (e.t - early * speed);
    const auto lo = std::max<juce::int64> (validFrom, written.load() - ringSize + 1);
    if (iStart < 0) iStart = lo;
    iStart = std::max (iStart, lo);

    // --- attack: the high-passed energy jumps (also catches re-picking a string that's still ringing)
    juce::int64 onset = -1;
    {
        const int history = 10;
        const auto from = std::max (lo + 1, iStart - envBlock * history);
        const int blocks = (int) ((iEnd - from) / envBlock);
        std::vector<float> env ((size_t) std::max (0, blocks), 0.0f), raw ((size_t) std::max (0, blocks), 0.0f);
        for (int b = 0; b < blocks; ++b)
        {
            float s = 0, r = 0;
            for (int i = 0; i < envBlock; ++i)
            {
                const auto k = from + b * envBlock + i;
                const float x = ring[(size_t) (k & (ringSize - 1))], xp = ring[(size_t) ((k - 1) & (ringSize - 1))];
                s += (x - xp) * (x - xp);
                r += x * x;
            }
            env[(size_t) b] = s / envBlock;
            raw[(size_t) b] = std::sqrt (r / envBlock);
        }
        float best = 0;
        for (int b = 2; b < blocks; ++b)
        {
            const auto at = from + (juce::int64) b * envBlock;
            if (at < iStart) continue;
            float before = 0; int nb = 0;
            for (int k = std::max (0, b - history); k <= b - 2; ++k) { before += env[(size_t) k]; ++nb; }
            before = nb > 0 ? before / (float) nb : 0.0f;
            // look a little ahead: the attack peaks over a few ms
            float after = std::max (env[(size_t) b], b + 1 < blocks ? env[(size_t) b + 1] : 0.0f);
            float peakLevel = raw[(size_t) b];
            for (int k = b; k < std::min (blocks, b + 6); ++k) peakLevel = std::max (peakLevel, raw[(size_t) k]);
            if (peakLevel < levelFloor) continue;
            const float strength = after / (before + 1.0e-9f);
            if (strength > 4.0f && strength > best) { best = strength; onset = at; }
        }
    }

    // --- pitch: does the expected harmonic pattern stand out from every other note's?
    int lowest = 128;
    for (auto p : e.pitches) lowest = std::min (lowest, p);
    const float lowHz = midiToHz (lowest, freqScale);
    const int len = lowHz < 100.0f ? 8192 : lowHz < 190.0f ? 4096 : 2048;   // as short as the note allows: it decides how soon a hit shows up
    if (! final && onset < 0 && ! e.legato) return false;    // no attack yet
    const auto noteAt = onset >= 0 ? onset : indexForTime (e.t);
    if (noteAt < 0) return false;
    auto pitchAt = noteAt + (juce::int64) (0.03 * sampleRate);
    pitchAt = std::max (pitchAt, lo);
    if (pitchAt + len > written.load()) return false;         // not enough of the note heard yet
    int heard = 0;
    float frameRms = 0;
    for (int i = 0; i < len; ++i) { const float x = ring[(size_t) ((pitchAt + i) & (ringSize - 1))]; frameRms += x * x; }
    frameRms = std::sqrt (frameRms / (float) len);
    if (frameRms >= levelFloor && ! e.muted)
    {
        std::vector<float> mag;
        spectrum (pitchAt, len, mag);
        const int fftSize = len * 2;
        const float need = e.pitches.size() <= 1 ? 0.8f : 0.45f;
        heard = countHeard (mag, fftSize, e.pitches, need);
    }

    const int total = std::max (1, (int) e.pitches.size());
    const bool attackOk = onset >= 0 || e.legato;
    bool pitchOk;
    if (e.muted) pitchOk = true;
    else if (total == 1) pitchOk = heard == 1;
    else pitchOk = heard * 10 >= total * 6;          // most of the chord
    j.hit = attackOk && pitchOk && (onset >= 0 || frameRms >= levelFloor);
    j.value = e.muted ? (onset >= 0 ? 1.0f : 0.0f) : (float) heard / (float) total;
    if (j.hit && onset >= 0)
        j.timingMs = (float) ((times[(size_t) (onset & (ringSize - 1))] - e.t) / speed * 1000.0);
    if (! final && ! j.hit) return false;                     // maybe later in its window
    {
        const juce::ScopedLock sl (outLock);
        out.push_back (j);
    }
    if (j.hit && e.sus >= 0.35 * speed && ! e.muted)
        pendingSustains.push_back ({ index, e.t + 0.15 * speed, e.t + e.sus - 0.05 * speed, e.pitches });
    return true;
}

void NoteScorer::judgeSustain (int k)
{
    const auto ps = pendingSustains[(size_t) k];
    const auto a = indexForTime (ps.from), b = indexForTime (ps.to);
    Judgment j;
    j.index = ps.index;
    j.pass = pass;
    j.kind = 1;
    if (a < 0 || b <= a)
    {
        j.hit = true; j.value = 1;
    }
    else
    {
        const int len = 4096, stepS = (int) (0.1 * sampleRate);
        int frames = 0, ok = 0;
        std::vector<float> mag;
        for (auto s = a; s + len <= b + len / 2 && frames < 80; s += stepS)
        {
            ++frames;
            float r = 0;
            for (int i = 0; i < len; i += 4) { const float x = ring[(size_t) ((s + i) & (ringSize - 1))]; r += x * x; }
            if (std::sqrt (r / (float) (len / 4)) < levelFloor * 0.5f) continue;
            spectrum (s, len, mag);
            const int heard = countHeard (mag, len * 2, ps.pitches, 0.5f);
            if (heard * 2 >= (int) ps.pitches.size()) ++ok;
        }
        j.value = frames > 0 ? (float) ok / (float) frames : 1.0f;
        j.hit = j.value >= 0.6f;
    }
    const juce::ScopedLock sl (outLock);
    out.push_back (j);
}

} // namespace galpon
