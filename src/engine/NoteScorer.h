#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

namespace galpon
{

/** Game mode: listens to your guitar and judges each note or chord of the chart.
    The audio thread only copies the input (with the song time each sample belongs to) into a ring;
    a background thread looks at the moment each note was due: was there an attack near it,
    were the right pitches sounding, and did a long note keep sounding.

    It asks "did the expected notes sound?", which is far more reliable than transcribing
    whatever comes in (that's how this kind of game works). */
class NoteScorer : private juce::Thread
{
public:
    NoteScorer();
    ~NoteScorer() override;

    struct Event
    {
        double t = 0;                    // song seconds
        std::vector<int> pitches;        // MIDI notes (already with tuning and capo)
        double sus = 0;                  // seconds to hold (0 = short)
        bool legato = false;             // hammer-on, pull-off, tap, slide arrival: no new pick attack
        bool muted = false;              // dead note: only the attack counts
    };

    struct Judgment
    {
        int index = 0;                   // event index
        int pass = 0;                    // goes up every time the song jumps (loop, seek)
        int kind = 0;                    // 0 = the attack, 1 = how long a held note lasted
        bool hit = false;
        float timingMs = 0;              // + late, - early (only when hit)
        float value = 0;                 // kind 0: share of the chord's notes heard; kind 1: share of the hold
    };

    /** Message thread. cents: the whole song's tuning offset (A = 440 · 2^(cents/1200)). */
    void setEvents (std::vector<Event> events, float cents);
    void clearEvents();
    void setEnabled (bool on);
    bool isEnabled() const noexcept { return enabled.load(); }

    void prepare (double sampleRate);

    /** Audio thread. songTime0: song time (as the player heard it) of in[0]; step: song seconds per sample. */
    void push (const float* in, int n, double songTime0, double step, bool playing);

    /** Message thread: the judgments since the last call. */
    std::vector<Judgment> take();
    /** Loudness of the guitar input lately (0..1), to show "te escucho". */
    float inputLevel() const noexcept { return level.load(); }

    /** For tests: do one round of work right now, on this thread. */
    void processNow() { work(); }
    /** Tests: don't start the background thread. */
    void setManual (bool m) { manual = m; }

    // Tolerances (seconds of real time).
    static constexpr double early = 0.12, late = 0.15;

private:
    void run() override;
    void work();
    void judge (const Event& e, int index);
    void judgeSustain (int index);

    // ring of input samples and the song time of each
    static constexpr int ringSize = 1 << 18;     // ~5.5 s at 48 kHz
    std::vector<float> ring;
    std::vector<double> times;
    std::atomic<juce::int64> written { 0 };
    std::atomic<int> jumps { 0 };
    std::atomic<juce::int64> jumpAt { 0 };       // sample counter where the last jump happened
    double expectedNext = -1.0e9;
    std::atomic<double> lastStep { 1.0 / 48000.0 };
    float levelAcc = 0; int levelCount = 0;
    std::atomic<float> level { 0 };

    double sampleRate = 48000;
    std::atomic<bool> enabled { true };
    bool manual = false;

    juce::CriticalSection eventLock;
    std::vector<Event> events;
    double freqScale = 1.0;
    int next = 0, pass = 0, seenJumps = 0;
    juce::int64 validFrom = 0;
    struct PendingSustain { int index; double from, to; std::vector<int> pitches; };
    std::vector<PendingSustain> pendingSustains;

    juce::CriticalSection outLock;
    std::vector<Judgment> out;

    // analysis helpers (worker thread only)
    juce::int64 indexForTime (double t) const;   // first sample whose time >= t (or -1)
    void spectrum (juce::int64 start, int len, std::vector<float>& mag);
    float harmonicSum (const std::vector<float>& mag, int fftSize, float f0) const;
    int countHeard (const std::vector<float>& mag, int fftSize, const std::vector<int>& pitches, float need) const;
    std::unique_ptr<juce::dsp::FFT> fft1k, fft8k, fft16k;
    std::vector<float> work1, work2;
};

} // namespace galpon
