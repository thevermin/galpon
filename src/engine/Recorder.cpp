#include "Recorder.h"

namespace galpon
{

TakeWriter::TakeWriter (juce::TimeSliceThread& t, const juce::File& file, double sampleRate, int channels)
    : thread (t),
      fifo ((int) (sampleRate * 20.0) + 1),     // an AbstractFifo holds one less than its size
      numChannels (juce::jmax (1, channels)),
      samplesPerFlush ((int) sampleRate),
      flushCounter ((int) sampleRate)
{
    buffer.setSize (numChannels, fifo.getTotalSize());
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen()) return;
    juce::WavAudioFormat wav;
    writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                              .withSampleRate (sampleRate)
                                              .withNumChannels (numChannels)
                                              .withBitsPerSample (32)
                                              .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer != nullptr)
    {
        writer->flush();      // a valid (empty) WAV from the very first moment
        thread.addTimeSliceClient (this);
    }
}

TakeWriter::~TakeWriter()
{
    finish();
}

void TakeWriter::write (const float* const* channels, int numSamples) noexcept
{
    if (writer == nullptr || numSamples <= 0) return;
    if (fifo.getFreeSpace() < numSamples)
    {
        dropped += numSamples;      // reported to the user; never blocks the audio
        return;
    }
    int s1, n1, s2, n2;
    fifo.prepareToWrite (numSamples, s1, n1, s2, n2);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = channels[juce::jmin (ch, numChannels - 1)];
        if (src == nullptr) src = channels[0];
        if (n1 > 0) buffer.copyFrom (ch, s1, src, n1);
        if (n2 > 0) buffer.copyFrom (ch, s2, src + n1, n2);
    }
    fifo.finishedWrite (n1 + n2);
}

int TakeWriter::drain()
{
    const juce::ScopedLock sl (writeLock);
    if (writer == nullptr) return 0;
    const int ready = fifo.getNumReady();
    if (ready <= 0) return 0;
    int s1, n1, s2, n2;
    fifo.prepareToRead (ready, s1, n1, s2, n2);
    bool ok = true;
    if (n1 > 0) ok &= writer->writeFromAudioSampleBuffer (buffer, s1, n1);
    if (n2 > 0) ok &= writer->writeFromAudioSampleBuffer (buffer, s2, n2);
    fifo.finishedRead (n1 + n2);
    if (! ok) failed = true;
    written += n1 + n2;
    flushCounter -= n1 + n2;
    if (flushCounter <= 0)
    {
        flushCounter = samplesPerFlush;
        if (! writer->flush()) failed = true;    // keeps the header up to date
    }
    return n1 + n2;
}

int TakeWriter::useTimeSlice()
{
    return drain() > 0 ? 1 : 20;
}

void TakeWriter::finish()
{
    if (writer == nullptr) return;
    thread.removeTimeSliceClient (this);
    while (drain() > 0) {}
    const juce::ScopedLock sl (writeLock);
    if (! writer->flush()) failed = true;
    writer.reset();     // closes the file (and writes the final header)
}

//==============================================================================
bool TakeWriter::repairWav (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return false;
    const auto total = in.getTotalLength();
    char id[4];
    if (in.read (id, 4) != 4 || memcmp (id, "RIFF", 4) != 0) return false;
    in.readInt();
    if (in.read (id, 4) != 4 || memcmp (id, "WAVE", 4) != 0) return false;
    // walk the chunks until "data"
    int blockAlign = 1;
    while (! in.isExhausted())
    {
        const auto chunkStart = in.getPosition();
        if (in.read (id, 4) != 4) return false;
        const auto size = (juce::uint32) in.readInt();
        if (memcmp (id, "data", 4) == 0)
        {
            const auto dataStart = chunkStart + 8;
            auto actual = total - dataStart;
            actual -= actual % juce::jmax (1, blockAlign);     // only whole sample frames
            if (actual <= 0) return false;
            if ((juce::int64) size == actual) return true;            // already fine
            if ((juce::int64) size > actual && size != 0xffffffffu && size != 0) return true;  // not ours to touch
            // header says less than what's there: a recording that was cut off
            juce::FileOutputStream out (file);
            if (! out.openedOk()) return false;
            out.setPosition (4);
            out.writeInt ((int) (dataStart + actual - 8));
            out.setPosition (chunkStart + 4);
            out.writeInt ((int) actual);
            out.flush();
            return true;
        }
        if (memcmp (id, "ds64", 4) == 0) return true;   // 64-bit WAV: leave alone
        if (memcmp (id, "fmt ", 4) == 0 && size >= 16)
        {
            in.readShort(); in.readShort(); in.readInt(); in.readInt();
            blockAlign = juce::jmax (1, (int) (juce::uint16) in.readShort());
        }
        in.setPosition (chunkStart + 8 + size + (size & 1));
    }
    return false;
}

} // namespace galpon
