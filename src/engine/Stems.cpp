#include "Stems.h"

#if GALPON_WITH_STEMS
 #include <model.hpp>
#endif
#include <thread>

namespace galpon
{
namespace
{
    constexpr double demucsRate = 44100.0;

    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double from, double to, int channels)
    {
        if (std::abs (from - to) < 0.5 && in.getNumChannels() == channels)
            return in;
        const double ratio = from / to;
        const int outLen = (int) std::ceil (in.getNumSamples() / ratio);
        juce::AudioBuffer<float> out (channels, outLen);
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::LagrangeInterpolator interp;
            const int src = juce::jmin (ch, in.getNumChannels() - 1);
            interp.process (ratio, in.getReadPointer (src), out.getWritePointer (ch), outLen, in.getNumSamples(), 0);
        }
        return out;
    }

   #if GALPON_WITH_STEMS
    // The network is loaded once and shared (it's read-only while separating)
    std::shared_ptr<demucscpp::demucs_model> loadedModel;
    juce::CriticalSection modelLock;
   #endif
}

//==============================================================================
class StemSeparator::Worker : public juce::Thread
{
public:
    Worker (StemSeparator& o, juce::AudioBuffer<float> a, double sr)
        : juce::Thread ("Galpon stems"), owner (o), audio (std::move (a)), rate (sr) {}

    ~Worker() override { stopThread (2000); }

    void run() override
    {
        auto fail = [this] (const juce::String& e)
        {
            { const juce::ScopedLock sl (owner.lock); owner.errorText = e; }
            owner.phaseValue = Failed;
        };

       #if ! GALPON_WITH_STEMS
        fail (juce::String (u8"Esta versión se compiló sin separación de pistas"));
       #else
        // 1. The model, the first time
        auto model = modelFile();
        if (! model.existsAsFile())
        {
            owner.phaseValue = Downloading;
            auto r = download (model);
            if (r.failed()) { fail (r.getErrorMessage()); return; }
        }
        if (threadShouldExit()) return;

        // 2. Load it (once per session)
        owner.phaseValue = Loading;
        std::shared_ptr<demucscpp::demucs_model> net;
        {
            const juce::ScopedLock sl (modelLock);
            if (loadedModel == nullptr)
            {
                auto m = std::make_shared<demucscpp::demucs_model>();
                if (! demucscpp::load_demucs_model (model.getFullPathName().toStdString(), m.get()))
                {
                    model.deleteFile();   // probably a broken download: get it again next time
                    fail (juce::String (u8"No se pudo cargar el modelo de separación. Prueba de nuevo."));
                    return;
                }
                loadedModel = m;
            }
            net = loadedModel;
        }

        // 3. Separate, in parallel pieces with overlaps (like demucs.cpp's multithreaded driver)
        owner.phaseValue = Separating;
        owner.progressValue = 0.0f;
        auto in = resample (audio, rate, demucsRate, 2);
        const int total = in.getNumSamples();
        const int overlap = (int) (demucsRate * 0.75);
        const int cores = (int) juce::jmax (1u, std::thread::hardware_concurrency());
        const int pieces = juce::jlimit (1, juce::jmax (1, cores / 2), total / (int) (demucsRate * 15.0) + 1);
        const int pieceLen = (total + pieces - 1) / pieces;

        // Everything the worker threads touch is shared, so a cancelled job can simply be
        // abandoned: its threads finish on their own and free it (the network can't be interrupted).
        struct Shared
        {
            juce::AudioBuffer<float> in;
            std::shared_ptr<demucscpp::demucs_model> net;
            std::vector<Eigen::Tensor3dXf> outputs;
            std::unique_ptr<std::atomic<float>[]> partial;
            std::unique_ptr<std::atomic<bool>[]> finished;
        };
        auto shared = std::make_shared<Shared>();
        shared->in = std::move (in);
        shared->net = net;
        shared->outputs.resize ((size_t) pieces);
        shared->partial.reset (new std::atomic<float>[(size_t) pieces]);
        shared->finished.reset (new std::atomic<bool>[(size_t) pieces]);
        std::vector<int> starts ((size_t) pieces);
        for (int k = 0; k < pieces; ++k)
        {
            shared->partial[(size_t) k] = 0.0f;
            shared->finished[(size_t) k] = false;
            const int a = juce::jmax (0, k * pieceLen - (k > 0 ? overlap : 0));
            const int b = juce::jmin (total, (k + 1) * pieceLen + (k < pieces - 1 ? overlap : 0));
            starts[(size_t) k] = a;
            std::thread ([shared, k, a, b]
            {
                Eigen::MatrixXf seg (2, b - a);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = a; i < b; ++i) seg (ch, i - a) = shared->in.getSample (ch, i);
                auto* partial = shared->partial.get();
                shared->outputs[(size_t) k] = demucscpp::demucs_inference (*shared->net, seg,
                    [partial, k] (float p, const std::string&) { partial[(size_t) k] = p; });
                shared->finished[(size_t) k] = true;
            }).detach();
        }
        for (;;)
        {
            float sum = 0;
            bool all = true;
            for (int k = 0; k < pieces; ++k) { sum += shared->partial[(size_t) k]; all &= shared->finished[(size_t) k].load(); }
            owner.progressValue = juce::jlimit (0.0f, 0.99f, sum / (float) pieces);
            if (all) break;
            if (threadShouldExit()) return;
            juce::Thread::sleep (200);
        }
        auto& outputs = shared->outputs;

        // overlap-add with linear crossfades
        std::array<juce::AudioBuffer<float>, numStems> stems;
        for (auto& s : stems) { s.setSize (2, total); s.clear(); }
        for (int k = 0; k < pieces; ++k)
        {
            const auto& o = outputs[(size_t) k];
            const int a = starts[(size_t) k];
            const int len = (int) o.dimension (2);
            for (int s = 0; s < numStems && s < (int) o.dimension (0); ++s)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < len && a + i < total; ++i)
                    {
                        float w = 1.0f;
                        if (k > 0 && i < 2 * overlap) w = juce::jlimit (0.0f, 1.0f, (float) i / (2.0f * overlap));
                        if (k < pieces - 1 && i > len - 2 * overlap) w = juce::jmin (w, juce::jlimit (0.0f, 1.0f, (float) (len - i) / (2.0f * overlap)));
                        stems[(size_t) s].addSample (ch, a + i, o (s, ch, i) * w);
                    }
        }
        // back to the song's rate and length
        {
            const juce::ScopedLock sl (owner.lock);
            for (int s = 0; s < numStems; ++s)
            {
                auto back = resample (stems[(size_t) s], demucsRate, rate, 2);
                owner.result[(size_t) s].setSize (2, audio.getNumSamples());
                owner.result[(size_t) s].clear();
                for (int ch = 0; ch < 2; ++ch)
                    owner.result[(size_t) s].copyFrom (ch, 0, back, ch, 0, juce::jmin (back.getNumSamples(), audio.getNumSamples()));
            }
        }
        owner.progressValue = 1.0f;
        owner.phaseValue = Done;
       #endif
    }

private:
    juce::Result download (const juce::File& dest)
    {
        dest.getParentDirectory().createDirectory();
        auto temp = dest.getSiblingFile (dest.getFileName() + ".parcial");
        auto stream = juce::URL (modelUrl()).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                    .withConnectionTimeoutMs (20000)
                                                                    .withNumRedirectsToFollow (8));
        if (stream == nullptr) return juce::Result::fail (juce::String (u8"No se pudo bajar el modelo de separación. ¿Hay internet?"));
        const auto length = stream->getTotalLength();
        juce::FileOutputStream out (temp);
        if (! out.openedOk()) return juce::Result::fail ("No se pudo escribir " + temp.getFullPathName());
        out.setPosition (0); out.truncate();
        juce::HeapBlock<char> buf (1 << 16);
        juce::int64 got = 0;
        while (! stream->isExhausted() && ! threadShouldExit())
        {
            const int n = stream->read (buf.get(), 1 << 16);
            if (n <= 0) break;
            out.write (buf.get(), (size_t) n);
            got += n;
            if (length > 0) owner.progressValue = (float) got / (float) length;
        }
        out.flush();
        if (threadShouldExit()) { temp.deleteFile(); return juce::Result::fail ("Cancelado"); }
        if (got < 10 * 1000 * 1000 || (length > 0 && got != length))
        {
            temp.deleteFile();
            return juce::Result::fail (juce::String (u8"La descarga del modelo se cortó. Prueba de nuevo."));
        }
        if (! temp.moveFileTo (dest)) return juce::Result::fail ("No se pudo guardar el modelo");
        return juce::Result::ok();
    }

    StemSeparator& owner;
    juce::AudioBuffer<float> audio;
    double rate;
};

//==============================================================================
StemSeparator::StemSeparator() = default;
StemSeparator::~StemSeparator() { worker.reset(); }

juce::String StemSeparator::stemName (int index)
{
    // demucs order: drums, bass, other, vocals
    static const char* names[] = { "Batería", "Bajo", "Otros", "Voz" };
    return juce::String (juce::CharPointer_UTF8 (names[juce::jlimit (0, numStems - 1, index)]));
}

juce::File StemSeparator::modelFile()
{
    auto base = juce::SystemStats::getEnvironmentVariable ("GALPON_MODELS_DIR", {});
    juce::File dir = base.isNotEmpty() ? juce::File (base)
                                       : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Galpon").getChildFile ("Modelos");
    return dir.getChildFile ("ggml-model-htdemucs-4s-f16.bin");
}

juce::String StemSeparator::modelUrl()
{
    // demucs.cpp's conversion of Meta's htdemucs weights (MIT)
    return "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/ggml-model-htdemucs-4s-f16.bin";
}

juce::Result StemSeparator::start (const juce::AudioBuffer<float>& audio, double sampleRate, const Job& j)
{
    const auto p = phase();
    if (p == Downloading || p == Loading || p == Separating)
        return juce::Result::fail (juce::String (u8"Ya estoy separando otra región: espera a que termine"));
    if (audio.getNumSamples() < (int) (sampleRate * 0.5))
        return juce::Result::fail (juce::String (u8"La región es muy corta para separarla"));
    worker.reset();
    currentJob = j;
    progressValue = 0.0f;
    phaseValue = Loading;
    { const juce::ScopedLock sl (lock); errorText.clear(); }
    worker = std::make_unique<Worker> (*this, audio, sampleRate);
    worker->startThread (juce::Thread::Priority::low);
    return juce::Result::ok();
}

void StemSeparator::cancel()
{
    if (worker != nullptr) worker->signalThreadShouldExit();
    phaseValue = Idle;
}

juce::String StemSeparator::error() const
{
    const juce::ScopedLock sl (lock);
    return errorText;
}

bool StemSeparator::takeResult (std::array<juce::AudioBuffer<float>, numStems>& out)
{
    if (phase() != Done) return false;
    {
        const juce::ScopedLock sl (lock);
        for (int s = 0; s < numStems; ++s) out[(size_t) s] = std::move (result[(size_t) s]);
    }
    phaseValue = Idle;
    return true;
}

} // namespace galpon
