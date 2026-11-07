#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace galpon
{

/** Writes one recording to disk without ever blocking the audio thread, and without ever
    losing audio quietly:
     - a 20-second buffer between the audio thread and the disk (slow disks, busy computers)
     - 32-bit float WAV: input gain can never clip the file
     - the file header is updated every second, so if the computer crashes or loses power the
       recording up to that second is still a valid WAV (and `repairWav` fixes the rest)
     - if the buffer ever fills up or the disk fails, it's counted and reported, never hidden */
class TakeWriter : private juce::TimeSliceClient
{
public:
    TakeWriter (juce::TimeSliceThread&, const juce::File& file, double sampleRate, int numChannels);
    ~TakeWriter() override;

    bool openedOk() const noexcept { return writer != nullptr; }

    /** Audio thread. */
    void write (const float* const* channels, int numSamples) noexcept;

    /** Message thread: writes everything still buffered and closes the file. */
    void finish();

    juce::int64 droppedSamples() const noexcept { return dropped.load(); }
    bool diskFailed() const noexcept           { return failed.load(); }
    juce::int64 samplesWritten() const noexcept { return written.load(); }

    /** Bytes each second of recording takes (for the free-space check). */
    static juce::int64 bytesPerSecond (double sampleRate, int numChannels) { return (juce::int64) (sampleRate * numChannels * 4); }

    /** After a crash: makes the WAV's header match the data that actually reached the disk.
        Returns true if the file was fixed (or didn't need fixing). */
    static bool repairWav (const juce::File&);

private:
    int useTimeSlice() override;
    int drain();

    juce::TimeSliceThread& thread;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> buffer;
    int numChannels;
    int samplesPerFlush, flushCounter;
    std::atomic<juce::int64> dropped { 0 }, written { 0 };
    std::atomic<bool> failed { false };
    juce::CriticalSection writeLock;
};

} // namespace galpon
