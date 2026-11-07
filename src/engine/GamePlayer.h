#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace galpon
{

/** Plays a game song's stems (from a .feedpak) next to the studio: choose which stems sound,
    slow down without changing pitch, repeat a part (A–B). Your guitar keeps coming in live
    through its studio track, with its sound. Message thread for control, audio thread for render. */
class GamePlayer
{
public:
    GamePlayer();
    ~GamePlayer();

    struct StemSource
    {
        juce::String id, name;
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        double sampleRate = 48000.0;
        bool enabled = true;
    };

    /** Message thread. Takes the decoded stems; takes audioLock only to swap them in. */
    void load (std::vector<StemSource> stems, double lengthSeconds, juce::CriticalSection& audioLock);
    void close();
    bool isLoaded() const noexcept { return loaded.load(); }

    void prepare (double sampleRate, int maxBlock);

    void play()  { ended = false; playing = true; }
    void pause() { playing = false; }
    bool isPlaying() const noexcept { return playing.load(); }
    bool hasEnded() const noexcept   { return ended.load(); }
    void seek (double seconds);
    void setSpeed (float s);
    float getSpeed() const noexcept  { return speed.load(); }
    void setLoop (bool on, double a, double b);
    bool setStemEnabled (const juce::String& id, bool on);
    bool setStemGain (const juce::String& id, float gain);       // 0..1.5
    void setMasterGain (float g) { masterGain = juce::jlimit (0.0f, 1.5f, g); }
    float getMasterGain() const noexcept { return masterGain.load(); }
    juce::var stemsInfo() const;
    double length() const noexcept   { return lengthSec; }

    /** Where the song is, in seconds, as it's heard (the stretcher's delay taken out). */
    double positionSeconds() const;

    /** Audio thread: adds the game's audio into `out` (stereo, len samples). */
    void render (juce::AudioBuffer<float>& out, int len);

private:
    struct Stem
    {
        juce::String id, name;
        std::shared_ptr<juce::AudioBuffer<float>> original;
        double originalRate = 48000.0;
        juce::AudioBuffer<float> audio;          // at the engine's rate
        double audioRate = 0;
        std::atomic<bool> enabled { true };
        std::atomic<float> gain { 1.0f };
        bool isFull = false;
    };
    void resampleAll (std::vector<std::unique_ptr<Stem>>& list);
    void mixInto (float* l, float* r, juce::int64 from, int n);   // with loop wrapping, advances nothing

    std::vector<std::unique_ptr<Stem>> stems;
    double sampleRate = 48000.0, lengthSec = 0;
    int maxBlock = 512;
    juce::int64 lengthSamples = 0;
    std::atomic<bool> loaded { false }, playing { false }, ended { false };
    std::atomic<juce::int64> pos { 0 };
    std::atomic<float> speed { 1.0f };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<bool> loopOn { false };
    std::atomic<juce::int64> loopA { 0 }, loopB { 0 };
    std::atomic<bool> resetStretch { true };
    double inputDebt = 0;

    struct Stretch;
    std::unique_ptr<Stretch> stretch;
    juce::AudioBuffer<float> scratch;
};

} // namespace galpon
