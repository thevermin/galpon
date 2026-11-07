#include "GamePlayer.h"
#include <signalsmith-stretch.h>

namespace galpon
{

struct GamePlayer::Stretch
{
    signalsmith::stretch::SignalsmithStretch<float> st;
    std::vector<std::vector<float>> in, out;
    int inLat = 0, outLat = 0;
};

GamePlayer::GamePlayer() : stretch (std::make_unique<Stretch>()) {}
GamePlayer::~GamePlayer() = default;

void GamePlayer::load (std::vector<StemSource> sources, double seconds, juce::CriticalSection& audioLock)
{
    // the slow part (resampling) first, without stopping the audio
    std::vector<std::unique_ptr<Stem>> fresh;
    for (auto& s : sources)
    {
        auto st = std::make_unique<Stem>();
        st->id = s.id;
        st->name = s.name;
        st->original = std::move (s.audio);
        st->originalRate = s.sampleRate;
        st->enabled = s.enabled;
        st->isFull = s.id == "full";
        fresh.push_back (std::move (st));
    }
    sources.clear();
    resampleAll (fresh);
    juce::int64 longest = 0;
    for (auto& s : fresh) longest = std::max<juce::int64> (longest, s->audio.getNumSamples());

    std::vector<std::unique_ptr<Stem>> old;
    {
        const juce::ScopedLock sl (audioLock);
        playing = false;
        ended = false;
        old.swap (stems);
        stems.swap (fresh);
        // the real length is the longest stem (the manifest's duration is a hint)
        lengthSamples = longest > 0 ? longest : (juce::int64) (seconds * sampleRate);
        lengthSec = (double) lengthSamples / sampleRate;
        pos = 0;
        loopOn = false;
        resetStretch = true;
        loaded = ! stems.empty();
    }
    // the old song's audio is freed here, outside the lock
}

void GamePlayer::close()
{
    playing = false;
    loaded = false;
    stems.clear();
    lengthSamples = 0;
}

void GamePlayer::prepare (double sr, int block)
{
    const bool changed = std::abs (sr - sampleRate) > 0.5;
    const double ratio = sr / sampleRate;
    sampleRate = sr;
    maxBlock = juce::jmax (block, 64);
    scratch.setSize (2, maxBlock * 4 + 64);
    stretch->st.presetDefault (2, (float) sr);
    stretch->inLat = stretch->st.inputLatency();
    stretch->outLat = stretch->st.outputLatency();
    stretch->in.assign (2, std::vector<float> ((size_t) maxBlock * 4 + 64));
    stretch->out.assign (2, std::vector<float> ((size_t) maxBlock + 64));
    resetStretch = true;
    if (changed && ! stems.empty())
    {
        pos = (juce::int64) (pos.load() * ratio);
        loopA = (juce::int64) (loopA.load() * ratio);
        loopB = (juce::int64) (loopB.load() * ratio);
        resampleAll (stems);
        lengthSamples = (juce::int64) (lengthSec * sampleRate);
    }
}

void GamePlayer::resampleAll (std::vector<std::unique_ptr<Stem>>& list)
{
    // Only one copy of each stem stays in memory, at the engine's rate: a long song with
    // many stems is already hundreds of megabytes.
    for (auto& s : list)
    {
        std::shared_ptr<juce::AudioBuffer<float>> src = s->original;
        double srcRate = s->originalRate;
        if (src == nullptr)
        {
            if (s->audio.getNumSamples() == 0 || std::abs (s->audioRate - sampleRate) < 0.5) continue;
            src = std::make_shared<juce::AudioBuffer<float>> (std::move (s->audio));
            srcRate = s->audioRate;
        }
        s->original.reset();
        if (std::abs (srcRate - sampleRate) < 0.5)
        {
            if (src.use_count() == 1) s->audio = std::move (*src);
            else s->audio.makeCopyOf (*src);
            s->audioRate = sampleRate;
            continue;
        }
        const double ratio = srcRate / sampleRate;
        const int outLen = (int) std::ceil (src->getNumSamples() / ratio);
        juce::AudioBuffer<float> out (src->getNumChannels(), outLen);
        for (int ch = 0; ch < src->getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, src->getReadPointer (ch), out.getWritePointer (ch), outLen, src->getNumSamples(), 0);
        }
        src.reset();
        s->audio = std::move (out);
        s->audioRate = sampleRate;
    }
}

void GamePlayer::seek (double seconds)
{
    pos = (juce::int64) juce::jlimit (0.0, (double) lengthSamples, seconds * sampleRate);
    ended = false;
    resetStretch = true;
}

void GamePlayer::setSpeed (float s)
{
    s = juce::jlimit (0.25f, 1.5f, s);
    if (std::abs (s - speed.load()) > 1.0e-4f) resetStretch = true;
    speed = s;
}

void GamePlayer::setLoop (bool on, double a, double b)
{
    if (b < a) std::swap (a, b);
    loopA = (juce::int64) (juce::jmax (0.0, a) * sampleRate);
    loopB = (juce::int64) (juce::jmax (0.0, b) * sampleRate);
    loopOn = on && loopB.load() - loopA.load() > (juce::int64) (sampleRate * 0.25);
    if (loopOn && (pos.load() < loopA.load() || pos.load() >= loopB.load()))
    {
        pos = loopA.load();
        resetStretch = true;
    }
}

bool GamePlayer::setStemEnabled (const juce::String& id, bool on)
{
    for (auto& s : stems)
        if (s->id == id) { s->enabled = on; return true; }
    return false;
}

bool GamePlayer::setStemGain (const juce::String& id, float g)
{
    for (auto& s : stems)
        if (s->id == id) { s->gain = juce::jlimit (0.0f, 1.5f, g); return true; }
    return false;
}

juce::var GamePlayer::stemsInfo() const
{
    juce::Array<juce::var> list;
    for (auto& s : stems)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", s->id);
        o->setProperty ("name", s->name);
        o->setProperty ("enabled", s->enabled.load());
        o->setProperty ("gain", (double) s->gain.load());
        o->setProperty ("full", s->isFull);
        list.add (juce::var (o));
    }
    return list;
}

double GamePlayer::positionSeconds() const
{
    double p = (double) pos.load();
    if (std::abs (speed.load() - 1.0f) > 1.0e-3f)
        p -= stretch->inLat + stretch->outLat * speed.load();   // what's coming out now went in this long ago
    return juce::jmax (0.0, p / sampleRate);
}

void GamePlayer::mixInto (float* l, float* r, juce::int64 from, int n)
{
    // The full mixdown is the song as recorded: use it whenever every instrument is on at the same level
    // (spec §5.3); never add it to the separated stems (that would double everything).
    bool anyInstrument = false, allSame = true, fullOn = false;
    float common = -1.0f;
    Stem* full = nullptr;
    for (auto& s : stems)
    {
        if (s->isFull) { full = s.get(); fullOn = s->enabled; continue; }
        if (s->id == "click") continue;
        anyInstrument = true;
        const float g = s->enabled.load() ? s->gain.load() : 0.0f;
        if (common < 0) common = g;
        allSame &= std::abs (g - common) < 1.0e-3f;
    }
    const bool useFull = full != nullptr && ((anyInstrument && allSame && common > 0) || (! anyInstrument && fullOn));
    const float master = masterGain.load();

    const auto A = loopA.load(), B = loopB.load();
    const bool looping = loopOn.load() && B > A;
    for (auto& s : stems)
    {
        const bool isClick = s->id == "click";
        if (s->isFull ? ! useFull : (! s->enabled.load() || (useFull && ! isClick))) continue;
        const float g = master * (s->isFull ? (anyInstrument ? common : s->gain.load()) : s->gain.load());
        if (g <= 0.0f) continue;
        const auto& a = s->audio;
        const int chs = a.getNumChannels();
        const int len = a.getNumSamples();
        auto* L = a.getReadPointer (0);
        auto* R = a.getReadPointer (chs > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            auto idx = from + i;
            if (looping && idx >= B) idx = A + (idx - B) % (B - A);
            if (idx < 0 || idx >= len) continue;
            l[i] += L[idx] * g;
            r[i] += R[idx] * g;
        }
    }
}

void GamePlayer::render (juce::AudioBuffer<float>& out, int len)
{
    if (! loaded.load() || ! playing.load() || len <= 0 || out.getNumChannels() < 2) return;
    if (len > maxBlock)
    {
        // a bigger block than the device promised: do it in pieces that fit the buffers
        for (int o = 0; o < len; o += maxBlock)
        {
            juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, o, juce::jmin (maxBlock, len - o));
            render (part, part.getNumSamples());
        }
        return;
    }
    auto wrap = [this] (juce::int64 p)
    {
        const auto A = loopA.load(), B = loopB.load();
        if (loopOn.load() && B > A && p >= B) return A + (p - B) % (B - A);
        return p;
    };
    const float sp = speed.load();
    auto* outL = out.getWritePointer (0);
    auto* outR = out.getWritePointer (1);

    if (std::abs (sp - 1.0f) < 1.0e-3f)
    {
        scratch.clear (0, len);
        mixInto (scratch.getWritePointer (0), scratch.getWritePointer (1), pos.load(), len);
        juce::FloatVectorOperations::add (outL, scratch.getReadPointer (0), len);
        juce::FloatVectorOperations::add (outR, scratch.getReadPointer (1), len);
        pos = wrap (pos.load() + len);
    }
    else
    {
        auto& S = *stretch;
        if (resetStretch.exchange (false)) { S.st.reset(); inputDebt = 0; }
        inputDebt += len * (double) sp;
        const int k = juce::jlimit (0, (int) S.in[0].size(), (int) inputDebt);
        inputDebt -= k;
        std::fill (S.in[0].begin(), S.in[0].begin() + k, 0.0f);
        std::fill (S.in[1].begin(), S.in[1].begin() + k, 0.0f);
        mixInto (S.in[0].data(), S.in[1].data(), pos.load(), k);
        const int n = juce::jmin (len, (int) S.out[0].size());
        S.st.process (S.in, k, S.out, n);
        juce::FloatVectorOperations::add (outL, S.out[0].data(), n);
        juce::FloatVectorOperations::add (outR, S.out[1].data(), n);
        pos = wrap (pos.load() + k);
    }

    if (! loopOn.load() && pos.load() >= lengthSamples)
    {
        playing = false;
        ended = true;
        pos = lengthSamples;
    }
}

} // namespace galpon
