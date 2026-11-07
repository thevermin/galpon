#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>

namespace galpon
{

/** "Separar en pistas" (like Logic's Stem Splitter): splits a recording into voice, drums,
    bass and the rest, with Demucs v4 running on the computer (no internet needed after the
    first time, when the ~84 MB model is downloaded). Runs in the background. */
class StemSeparator
{
public:
    StemSeparator();
    ~StemSeparator();

    static constexpr int numStems = 4;
    static juce::String stemName (int index);     // "Batería", "Bajo", "Otros", "Voz"
    static juce::File modelFile();
    static juce::String modelUrl();

    enum Phase { Idle = 0, Downloading, Loading, Separating, Done, Failed };

    struct Job
    {
        int trackId = 0, clipId = 0;
        double startBeat = 0, lengthBeats = 0;
        juce::String name;
    };

    /** Starts separating (audio at `sampleRate`, any channel count). Fails if a job is running. */
    juce::Result start (const juce::AudioBuffer<float>& audio, double sampleRate, const Job&);
    void cancel();

    Phase phase() const noexcept       { return (Phase) phaseValue.load(); }
    float progress() const noexcept    { return progressValue.load(); }
    juce::String error() const;
    const Job& job() const noexcept    { return currentJob; }

    /** When Done: hands over the stems (same sample rate as the input) and goes back to Idle. */
    bool takeResult (std::array<juce::AudioBuffer<float>, numStems>& out);

private:
    class Worker;
    std::unique_ptr<Worker> worker;
    std::atomic<int> phaseValue { Idle };
    std::atomic<float> progressValue { 0.0f };
    mutable juce::CriticalSection lock;
    juce::String errorText;
    Job currentJob;
    std::array<juce::AudioBuffer<float>, numStems> result;
    friend class Worker;
};

} // namespace galpon
