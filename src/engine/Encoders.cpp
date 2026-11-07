// MP3 (shine, a small free encoder built into Galpon) and M4A/AAC (macOS's own encoder).

#include "Engine.h"

extern "C"
{
#include <layer3.h>
}

namespace galpon
{

juce::Result Engine::encodeMp3 (const juce::File& wav, const juce::File& mp3, int kbps)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
    if (reader == nullptr) return juce::Result::fail ("No se pudo leer el audio para el MP3");

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, std::numeric_limits<int>::max() / 4);
    juce::AudioBuffer<float> audio (channels, length);
    reader->read (&audio, 0, length, 0, true, channels > 1);

    // MP3 only allows 32, 44.1 and 48 kHz (at full quality): convert anything else to 44.1
    int rate = (int) reader->sampleRate;
    if (rate != 32000 && rate != 44100 && rate != 48000)
    {
        const double ratio = reader->sampleRate / 44100.0;
        const int outLen = (int) std::ceil (length / ratio);
        juce::AudioBuffer<float> converted (channels, outLen);
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, audio.getReadPointer (ch), converted.getWritePointer (ch), outLen, length, 0);
        }
        audio = std::move (converted);
        rate = 44100;
    }

    shine_config_t config;
    shine_set_config_mpeg_defaults (&config.mpeg);
    config.wave.samplerate = rate;
    config.wave.channels = channels == 2 ? PCM_STEREO : PCM_MONO;
    config.mpeg.mode = channels == 2 ? JOINT_STEREO : MONO;
    config.mpeg.bitr = kbps;
    if (shine_check_config (config.wave.samplerate, config.mpeg.bitr) < 0)
        return juce::Result::fail ("Esa calidad de MP3 no se puede usar con esta frecuencia");

    shine_t encoder = shine_initialise (&config);
    if (encoder == nullptr) return juce::Result::fail ("No se pudo iniciar el codificador MP3");

    mp3.deleteFile();
    juce::FileOutputStream out (mp3);
    if (out.failedToOpen())
    {
        shine_close (encoder);
        return juce::Result::fail ("No se pudo crear " + mp3.getFullPathName());
    }

    const int perPass = shine_samples_per_pass (encoder);
    std::vector<int16_t> interleaved ((size_t) (perPass * channels));
    const int total = audio.getNumSamples();
    for (int pos = 0; pos < total; pos += perPass)
    {
        for (int i = 0; i < perPass; ++i)
            for (int ch = 0; ch < channels; ++ch)
            {
                const float v = pos + i < total ? audio.getSample (ch, pos + i) : 0.0f;
                interleaved[(size_t) (i * channels + ch)] = (int16_t) juce::jlimit (-32768, 32767, juce::roundToInt (v * 32767.0f));
            }
        int written = 0;
        if (auto* data = shine_encode_buffer_interleaved (encoder, interleaved.data(), &written); data != nullptr && written > 0)
            out.write (data, (size_t) written);
    }
    int written = 0;
    if (auto* data = shine_flush (encoder, &written); data != nullptr && written > 0)
        out.write (data, (size_t) written);
    shine_close (encoder);
    out.flush();
    return out.getStatus().wasOk() ? juce::Result::ok() : juce::Result::fail ("No se pudo terminar de escribir el MP3");
}

juce::Result Engine::encodeM4a (const juce::File& wav, const juce::File& m4a, int kbps)
{
   #if JUCE_MAC
    m4a.deleteFile();
    juce::ChildProcess p;
    juce::StringArray cmd { "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", "-b", juce::String (kbps * 1000),
                            wav.getFullPathName(), m4a.getFullPathName() };
    if (! p.start (cmd)) return juce::Result::fail ("No se pudo iniciar el codificador AAC de macOS");
    p.waitForProcessToFinish (10 * 60 * 1000);
    if (p.getExitCode() != 0 || ! m4a.existsAsFile())
        return juce::Result::fail ("El codificador AAC de macOS no pudo crear el archivo");
    return juce::Result::ok();
   #else
    juce::ignoreUnused (wav, m4a, kbps);
    return juce::Result::fail ("Por ahora M4A solo se puede exportar en Mac. Usa MP3 o WAV.");
   #endif
}

} // namespace galpon
