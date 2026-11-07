// "Sampler": plays real recorded instruments. Loads SFZ instruments (the free, open format used
// by most free sample libraries) or a single audio file spread across the keyboard.
//
// Supported SFZ: #define / #include / $variables, <control> <global> <master> <group> <region>,
// key ranges and note names, velocity layers, round robin (seq_* and lo/hirand), CC conditions
// (evaluated with set_cc defaults), keyswitches, release triggers, choke groups (group/off_by),
// tune/transpose/keytrack, volume/amplitude/pan, offset/end, loops and the ampeg envelope.
// Samples are decoded to 16-bit in memory (half the RAM of floats) off the audio thread.

#include "Builtins.h"
#include <map>

namespace galpon
{
namespace
{
    //==========================================================================
    struct Sample
    {
        std::vector<int16_t> data;     // interleaved
        int channels = 1;
        int length = 0;
        double rate = 44100.0;

        float get (int ch, int i) const noexcept
        {
            if (i < 0 || i >= length) return 0.0f;
            return data[(size_t) i * (size_t) channels + (size_t) juce::jmin (ch, channels - 1)] * (1.0f / 32768.0f);
        }
    };

    struct CcRange { int cc; float lo, hi; };

    struct Region
    {
        std::shared_ptr<const Sample> sample;
        juce::String samplePath;
        int lokey = 0, hikey = 127, keycenter = 60;
        int lovel = 1, hivel = 127;
        float tune = 0, transpose = 0, keytrack = 100;
        float volume = 0, levelVolume = 0, amplitude = 100, veltrack = 100, pan = 0;   // levelVolume: group/master/global_volume
        int offset = 0, end = -1;
        int loopMode = 0;          // 0 no_loop, 1 one_shot, 2 loop_continuous, 3 loop_sustain
        int loopStart = -1, loopEnd = -1;
        float attack = 0.001f, hold = 0, decay = 0, sustain = 100, release = 0.001f, delay = 0;
        bool releaseTrigger = false;
        int seqLength = 1, seqPosition = 1;
        float lorand = 0, hirand = 1;
        long long group = 0, offBy = 0;
        int swLast = -1;
        std::vector<CcRange> cc;
    };

    struct Instrument
    {
        std::vector<Region> regions;
        std::array<float, 128> ccDefaults {};
        int swLo = -1, swHi = -1, swDefault = -1;
        juce::String name;
    };

    //==========================================================================
    int noteFromName (const juce::String& raw)
    {
        auto s = raw.trim().toLowerCase();
        if (s.isEmpty()) return -1;
        if (s.containsOnly ("-0123456789")) return s.getIntValue();
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        const auto c = s[0];
        if (c < 'a' || c > 'g') return -1;
        int note = base[c - 'a'];
        int i = 1;
        if (s[i] == '#') { ++note; ++i; }
        else if (s[i] == 'b' && s.length() > 2) { --note; ++i; }
        const int octave = s.substring (i).getIntValue();
        return juce::jlimit (-1, 127, (octave + 1) * 12 + note);    // c4 = 60
    }

    /** Also finds a note name inside a file name: "Piano_C4.wav", "guitar-a#2.flac" */
    int noteInFileName (const juce::String& fileName)
    {
        auto tokens = juce::StringArray::fromTokens (fileName.upToLastOccurrenceOf (".", false, false), " _-.", "");
        for (int i = tokens.size(); --i >= 0;)
        {
            const auto& t = tokens[i];
            if (t.length() >= 2 && t.length() <= 4 && juce::CharacterFunctions::isLetter (t[0]))
            {
                const int n = noteFromName (t);
                if (n >= 0 && juce::CharacterFunctions::isDigit (t.getLastCharacter())) return n;
            }
        }
        return -1;
    }

    //==========================================================================
    /** SFZ text -> list of (header, opcodes) in order. */
    class SfzParser
    {
    public:
        explicit SfzParser (juce::File rootFile) : root (std::move (rootFile)) {}

        juce::String preprocess()
        {
            juce::String out;
            includeFile (root, out, 0);
            return out;
        }

        struct Section { juce::String header; std::vector<std::pair<juce::String, juce::String>> opcodes; };

        static std::vector<Section> parse (const juce::String& text)
        {
            std::vector<Section> sections;
            auto lines = juce::StringArray::fromLines (text);
            for (auto line : lines)
            {
                int pos = 0;
                while (pos < line.length())
                {
                    while (pos < line.length() && juce::CharacterFunctions::isWhitespace (line[pos])) ++pos;
                    if (pos >= line.length()) break;
                    if (line[pos] == '<')
                    {
                        const int close = line.indexOfChar (pos, '>');
                        if (close < 0) break;
                        sections.push_back ({ line.substring (pos + 1, close).trim().toLowerCase(), {} });
                        pos = close + 1;
                        continue;
                    }
                    const int eq = line.indexOfChar (pos, '=');
                    if (eq < 0) break;
                    const auto name = line.substring (pos, eq).trim().toLowerCase();
                    // the value runs until the next "opcode=" or header (sample names may contain spaces)
                    int end = line.length();
                    for (int k = eq + 1; k < line.length(); ++k)
                    {
                        if (line[k] == '<') { end = k; break; }
                        if (juce::CharacterFunctions::isWhitespace (line[k]))
                        {
                            int j = k;
                            while (j < line.length() && juce::CharacterFunctions::isWhitespace (line[j])) ++j;
                            int w = j;
                            while (w < line.length() && (juce::CharacterFunctions::isLetterOrDigit (line[w]) || line[w] == '_')) ++w;
                            if (w > j && w < line.length() && line[w] == '=') { end = k; break; }
                            if (j < line.length() && line[j] == '<') { end = k; break; }
                        }
                    }
                    if (sections.empty()) sections.push_back ({ "control", {} });
                    sections.back().opcodes.emplace_back (name, line.substring (eq + 1, end).trim());
                    pos = end;
                }
            }
            return sections;
        }

    private:
        void includeFile (const juce::File& f, juce::String& out, int depth)
        {
            if (depth > 16 || ! f.existsAsFile()) return;
            for (auto line : juce::StringArray::fromLines (f.loadFileAsString()))
            {
                // comments
                if (auto c = line.indexOf ("//"); c >= 0) line = line.substring (0, c);
                if (auto c = line.indexOf ("/*"); c >= 0) line = line.substring (0, c);
                auto t = line.trim();
                if (t.startsWith ("#define"))
                {
                    auto rest = t.substring (7).trim();
                    auto name = rest.upToFirstOccurrenceOf (" ", false, false).trim();
                    auto value = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                    if (name.startsWithChar ('$')) defines[name] = expand (value);
                    continue;
                }
                if (t.startsWith ("#include"))
                {
                    auto path = expand (t.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false));
                    includeFile (root.getParentDirectory().getChildFile (path.replaceCharacter ('\\', '/')), out, depth + 1);
                    continue;
                }
                out << expand (line) << "\n";
            }
        }

        juce::String expand (juce::String s) const
        {
            if (! s.containsChar ('$') || defines.empty()) return s;
            // longest names first so $DIR doesn't eat $DIRECTORY
            std::vector<std::pair<juce::String, juce::String>> sorted (defines.begin(), defines.end());
            std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.first.length() > b.first.length(); });
            for (auto& [k, v] : sorted) s = s.replace (k, v);
            return s;
        }

        juce::File root;
        std::map<juce::String, juce::String> defines;
    };

    //==========================================================================
    /** Applies one opcode to a region template. Returns false if unknown (ignored). */
    void applyOpcode (Region& r, Instrument& inst, const juce::String& k, const juce::String& v, juce::String& defaultPath,
                      int& noteOffset)
    {
        const float f = v.getFloatValue();
        if      (k == "sample")          r.samplePath = defaultPath + v.replaceCharacter ('\\', '/');
        else if (k == "default_path")    defaultPath = v.replaceCharacter ('\\', '/');
        else if (k == "lokey")           r.lokey = noteFromName (v) + noteOffset;
        else if (k == "hikey")           r.hikey = noteFromName (v) < 0 ? -1 : noteFromName (v) + noteOffset;
        else if (k == "key")             { r.lokey = r.hikey = r.keycenter = noteFromName (v) + noteOffset; }
        else if (k == "pitch_keycenter") r.keycenter = noteFromName (v) + noteOffset;
        else if (k == "lovel")           r.lovel = v.getIntValue();
        else if (k == "hivel")           r.hivel = v.getIntValue();
        else if (k == "tune" || k == "pitch") r.tune = f;
        else if (k == "transpose")       r.transpose = f;
        else if (k == "pitch_keytrack")  r.keytrack = f;
        else if (k == "volume")          r.volume = f;
        else if (k == "group_volume" || k == "master_volume" || k == "global_volume") r.levelVolume += f;
        else if (k == "amplitude")       r.amplitude = f;
        else if (k == "amp_veltrack")    r.veltrack = f;
        else if (k == "pan")             r.pan = f;
        else if (k == "offset")          r.offset = v.getIntValue();
        else if (k == "end")             r.end = v.getIntValue();
        else if (k == "loop_mode" || k == "loopmode")
            r.loopMode = v == "one_shot" ? 1 : v == "loop_continuous" ? 2 : v == "loop_sustain" ? 3 : 0;
        else if (k == "loop_start" || k == "loopstart") r.loopStart = v.getIntValue();
        else if (k == "loop_end" || k == "loopend")     r.loopEnd = v.getIntValue();
        else if (k == "ampeg_attack")    r.attack = f;
        else if (k == "ampeg_hold")      r.hold = f;
        else if (k == "ampeg_decay")     r.decay = f;
        else if (k == "ampeg_sustain")   r.sustain = f;
        else if (k == "ampeg_release")   r.release = f;
        else if (k == "ampeg_delay")     r.delay = f;
        else if (k == "trigger")         r.releaseTrigger = v == "release";
        else if (k == "seq_length")      r.seqLength = juce::jmax (1, v.getIntValue());
        else if (k == "seq_position")    r.seqPosition = juce::jmax (1, v.getIntValue());
        else if (k == "lorand")          r.lorand = f;
        else if (k == "hirand")          r.hirand = f;
        else if (k == "group")           r.group = v.getLargeIntValue();
        else if (k == "off_by")          r.offBy = v.getLargeIntValue();
        else if (k == "sw_last")         r.swLast = noteFromName (v);
        else if (k == "sw_lokey")        inst.swLo = noteFromName (v);
        else if (k == "sw_hikey")        inst.swHi = noteFromName (v);
        else if (k == "sw_default")      inst.swDefault = noteFromName (v);
        else if (k == "note_offset")     noteOffset = v.getIntValue();
        else if (k.startsWith ("set_hdcc"))  inst.ccDefaults[(size_t) juce::jlimit (0, 127, k.substring (8).getIntValue())] = f * 127.0f;
        else if (k.startsWith ("set_cc"))    inst.ccDefaults[(size_t) juce::jlimit (0, 127, k.substring (6).getIntValue())] = f;
        else if (k.startsWith ("locc") || k.startsWith ("hicc"))
        {
            const int n = juce::jlimit (0, 127, k.substring (4).getIntValue());
            auto it = std::find_if (r.cc.begin(), r.cc.end(), [n] (const CcRange& c) { return c.cc == n; });
            if (it == r.cc.end()) { r.cc.push_back ({ n, 0.0f, 127.0f }); it = r.cc.end() - 1; }
            (k.startsWith ("lo") ? it->lo : it->hi) = f;
        }
    }

    std::unique_ptr<Instrument> parseInstrument (const juce::File& file);
    std::unique_ptr<Instrument> decodeSamples (const juce::File&, std::unique_ptr<Instrument>,
                                               std::function<void (const juce::String&)>, const std::function<bool()>&);

    /** Reads an SFZ (or a single audio file) into an instrument. Runs on a background thread. */
    std::unique_ptr<Instrument> loadInstrument (const juce::File& file, std::function<void (const juce::String&)> status,
                                                const std::function<bool()>& cancelled)
    {
        auto inst = parseInstrument (file);
        return decodeSamples (file, std::move (inst), status, cancelled);
    }

    std::unique_ptr<Instrument> parseInstrument (const juce::File& file)
    {
        auto inst = std::make_unique<Instrument>();
        inst->name = file.getFileNameWithoutExtension();
        inst->ccDefaults.fill (0.0f);
        inst->ccDefaults[7] = 100; inst->ccDefaults[10] = 64; inst->ccDefaults[11] = 127;

        if (! file.hasFileExtension ("sfz"))
        {
            Region r;
            r.samplePath = file.getFileName();
            r.keycenter = noteInFileName (file.getFileName());
            if (r.keycenter < 0) r.keycenter = 60;
            r.release = 0.25f;
            inst->regions.push_back (r);
        }
        else
        {
            SfzParser parser (file);
            auto sections = SfzParser::parse (parser.preprocess());
            // Each level starts from the one above it: <global> -> <master> -> <group> -> <region>
            Region control, global, master, group;
            juce::String defaultPath;
            int noteOffset = 0;
            for (auto& sec : sections)
            {
                const auto& h = sec.header;
                Region region;
                Region* target = nullptr;
                if      (h == "control") target = &control;
                else if (h == "global")  { global = Region(); target = &global; }
                else if (h == "master")  { master = global; target = &master; }
                else if (h == "group")   { group = master; target = &group; }
                else if (h == "region")  { region = group; target = &region; }
                else continue;     // <curve>, <effect>, <midi>… not used

                for (auto& [k, v] : sec.opcodes)
                    applyOpcode (*target, *inst, k, v, defaultPath, noteOffset);

                if (h == "global") { master = global; group = global; }
                else if (h == "master") group = master;
                else if (h == "region" && region.samplePath.isNotEmpty() && ! region.samplePath.containsChar ('*'))
                    inst->regions.push_back (region);    // "*sine" and other generated sounds aren't supported
            }
        }

        return inst;
    }

    std::unique_ptr<Instrument> decodeSamples (const juce::File& file, std::unique_ptr<Instrument> inst,
                                               std::function<void (const juce::String&)> status, const std::function<bool()>& cancelled)
    {
        // Decode the samples (each file once)
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::map<juce::String, std::shared_ptr<const Sample>> cache;
        int done = 0;
        const int total = (int) inst->regions.size();
        for (auto& r : inst->regions)
        {
            if (cancelled()) return nullptr;
            if (++done % 8 == 0 && status) status (juce::String (u8"Cargando muestras ") + juce::String (done) + "/" + juce::String (total) + juce::String (u8"…"));
            auto it = cache.find (r.samplePath);
            if (it != cache.end()) { r.sample = it->second; continue; }
            auto sf = file.getParentDirectory().getChildFile (r.samplePath);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (sf));
            std::shared_ptr<Sample> s;
            if (reader != nullptr)
            {
                s = std::make_shared<Sample>();
                s->channels = juce::jlimit (1, 2, (int) reader->numChannels);
                s->rate = reader->sampleRate;
                // long samples are cut at 30 s (piano notes are long; RAM is not infinite)
                s->length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
                juce::AudioBuffer<float> tmp (s->channels, s->length);
                reader->read (&tmp, 0, s->length, 0, true, s->channels > 1);
                s->data.resize ((size_t) s->length * (size_t) s->channels);
                for (int ch = 0; ch < s->channels; ++ch)
                {
                    auto* src = tmp.getReadPointer (ch);
                    for (int i = 0; i < s->length; ++i)
                        s->data[(size_t) i * (size_t) s->channels + (size_t) ch] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lrint (src[i] * 32767.0f));
                }
                // loop points stored in the file, if the SFZ asked to loop but didn't say where
                if (r.loopMode >= 2 && r.loopStart < 0)
                {
                    const auto& meta = reader->metadataValues;
                    if (meta.getValue ("NumSampleLoops", "0").getIntValue() > 0)
                    {
                        r.loopStart = meta.getValue ("Loop0Start", "-1").getIntValue();
                        r.loopEnd = meta.getValue ("Loop0End", "-1").getIntValue();
                    }
                }
            }
            cache[r.samplePath] = s;
            r.sample = s;
        }
        // keep only playable regions
        inst->regions.erase (std::remove_if (inst->regions.begin(), inst->regions.end(),
                                             [] (const Region& r) { return r.sample == nullptr || r.hikey < 0; }),
                             inst->regions.end());
        return inst;
    }

    //==========================================================================
    struct Voice
    {
        const Region* region = nullptr;
        int note = -1, velocity = 0;
        double pos = 0, inc = 1;
        float gainL = 1, gainR = 1;
        // envelope
        int stage = 0;          // 0 off, 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release
        float env = 0, envTarget = 0;
        double stageLeft = 0;
        float decayCoef = 0, releaseCoef = 0;
        bool held = false;      // key released while the pedal was down
        juce::uint32 age = 0;

        bool active() const noexcept { return stage != 0; }
    };

    class SamplerInstrument : public BuiltinProcessor,
                              private juce::Thread
    {
    public:
        SamplerInstrument() : BuiltinProcessor ("sampler", "Sampler", true), juce::Thread ("Sampler loader")
        {
            addParam ("volume", "Volumen", -30.0f, 12.0f, 0.0f, "dB");
            addParam ("tune", juce::String (u8"Afinación"), -12.0f, 12.0f, 0.0f, "st");
            addParam ("release", juce::String (u8"Liberación"), 0.1f, 4.0f, 1.0f, "x", 1.0f);
            addParam ("bright", "Brillo", 0.0f, 1.0f, 1.0f);
            addParam ("velsens", "Sensibilidad", 0.0f, 1.0f, 1.0f);
        }

        ~SamplerInstrument() override { stopThread (15000); }

        double getTailLengthSeconds() const override { return 4.0; }

        void prepareToPlay (double sr, int) override
        {
            sampleRate = sr;
            for (auto& v : voices) v.stage = 0;
            for (auto& f : tone) { f.reset(); f.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (sr, 20000.0f); }
            lastBright = -1.0f;
            if (file.existsAsFile() && active == nullptr && ! isThreadRunning()) startLoading (file);
        }
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
        {
            juce::ScopedNoDenormals nd;
            b.clear();
            {
                const juce::SpinLock::ScopedTryLockType lock (swapLock);
                if (lock.isLocked() && incoming != nullptr && retired == nullptr)
                {
                    for (auto& v : voices) v.stage = 0;     // voices point into the old instrument
                    retired = std::move (active);
                    active = std::move (incoming);
                    ccValues = active->ccDefaults;
                    keyswitch = active->swDefault;
                }
            }
            if (active == nullptr) { midi.clear(); return; }

            int pos = 0;
            for (const auto meta : midi)
            {
                render (b, pos, meta.samplePosition - pos);
                pos = meta.samplePosition;
                handle (meta.getMessage());
            }
            render (b, pos, b.getNumSamples() - pos);

            const float bright = get ("bright");
            if (bright != lastBright)
            {
                lastBright = bright;
                const float cutoff = 800.0f * std::pow (25.0f, bright);   // 800 Hz … 20 kHz
                for (auto& f : tone) *f.coefficients = *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (sampleRate, juce::jmin (cutoff, (float) sampleRate * 0.45f));
            }
            if (bright < 0.99f)
                for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    for (int i = 0; i < b.getNumSamples(); ++i) d[i] = tone[ch].processSample (d[i]);
                }
            b.applyGain (juce::Decibels::decibelsToGain (get ("volume")));
        }

        juce::var getExtraInfo() const override
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("file", file.getFullPathName());
            o->setProperty ("fileName", file.getFileNameWithoutExtension());
            o->setProperty ("loadable", "sampler");
            o->setProperty ("status", getStatus());
            return juce::var (o);
        }

        juce::Result setExtra (const juce::String& key, const juce::var& value) override
        {
            if (key != "file") return BuiltinProcessor::setExtra (key, value);
            juce::File f (value.toString());
            if (! f.existsAsFile()) return juce::Result::fail ("No encuentro " + f.getFullPathName());
            startLoading (f);
            return juce::Result::ok();
        }

        /** Tests: wait until loaded. */
        bool isLoading() const { return isThreadRunning(); }

    protected:
        void writeExtraState (juce::XmlElement& x) const override { x.setAttribute ("file", file.getFullPathName()); }
        void readExtraState (const juce::XmlElement& x) override
        {
            if (auto path = x.getStringAttribute ("file"); path.isNotEmpty())
                if (juce::File f (path); f.existsAsFile()) startLoading (f);
        }

    private:
        //------------------------------------------------------------------ loading
        void startLoading (const juce::File& f)
        {
            stopThread (15000);
            collectGarbage();
            file = f;
            setStatus (juce::String (u8"Cargando ") + f.getFileNameWithoutExtension() + juce::String (u8"…"));
            startThread();
        }

        void run() override
        {
            auto inst = loadInstrument (file, [this] (const juce::String& s) { setStatus (s); }, [this] { return threadShouldExit(); });
            if (inst == nullptr) return;
            if (inst->regions.empty()) { setStatus (juce::String (u8"No encontré muestras que se puedan usar en ") + file.getFileName()); return; }
            const auto count = inst->regions.size();
            std::unique_ptr<Instrument> toDelete;
            {
                const juce::SpinLock::ScopedLockType lock (swapLock);
                toDelete = std::move (incoming);
                incoming = std::move (inst);
            }
            toDelete.reset();
            collectGarbage();
            setStatus (file.getFileNameWithoutExtension() + " · " + juce::String ((int) count) + " muestras");
        }

        void collectGarbage()
        {
            std::unique_ptr<Instrument> old;
            const juce::SpinLock::ScopedLockType lock (swapLock);
            old = std::move (retired);
        }

        void setStatus (const juce::String& s) { const juce::ScopedLock sl (statusLock); status = s; }
        juce::String getStatus() const         { const juce::ScopedLock sl (statusLock); return status; }

        //------------------------------------------------------------------ playing
        void handle (const juce::MidiMessage& m)
        {
            if (m.isNoteOn())             noteOn (m.getNoteNumber(), m.getVelocity());
            else if (m.isNoteOff())       noteOff (m.getNoteNumber());
            else if (m.isSustainPedalOn())  { sustain = true; ccValues[64] = 127; }
            else if (m.isSustainPedalOff())
            {
                sustain = false; ccValues[64] = 0;
                for (auto& v : voices) if (v.active() && v.held) startRelease (v);
            }
            else if (m.isController())    ccValues[(size_t) m.getControllerNumber()] = (float) m.getControllerValue();
            else if (m.isPitchWheel())    bend = (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& v : voices) if (v.active()) { v.held = false; startRelease (v); }
        }

        bool regionMatches (const Region& r, int note, int vel, float rnd, bool release) const
        {
            if (r.releaseTrigger != release) return false;
            if (note < r.lokey || note > r.hikey || vel < r.lovel || vel > r.hivel) return false;
            if (rnd < r.lorand || rnd >= r.hirand) return false;
            if (r.seqLength > 1 && (seqCounter[(size_t) note] % r.seqLength) + 1 != r.seqPosition) return false;
            if (r.swLast >= 0 && keyswitch >= 0 && r.swLast != keyswitch) return false;
            for (auto& c : r.cc)
            {
                const float v = ccValues[(size_t) c.cc];
                if (v < c.lo || v > c.hi) return false;
            }
            return true;
        }

        void noteOn (int note, int vel)
        {
            if (active->swLo >= 0 && note >= active->swLo && note <= active->swHi) { keyswitch = note; return; }
            lastVelocity[(size_t) note] = vel;
            const float rnd = random.nextFloat();
            for (auto& r : active->regions)
            {
                if (! regionMatches (r, note, vel, rnd, false)) continue;
                // choke groups (an open hi-hat is cut by the closed one)
                if (r.group != 0)
                    for (auto& v : voices)
                        if (v.active() && v.region->offBy == r.group) { v.releaseCoef = std::exp (-1.0f / (0.006f * (float) sampleRate)); v.stage = 6; }
                startVoice (r, note, vel);
            }
            ++seqCounter[(size_t) note];
        }

        void noteOff (int note)
        {
            for (auto& v : voices)
            {
                if (! v.active() || v.note != note || v.stage == 6) continue;
                if (v.region->loopMode == 1) continue;              // one-shot: plays to the end
                if (sustain) v.held = true;
                else startRelease (v);
            }
            const float rnd = random.nextFloat();
            for (auto& r : active->regions)
                if (regionMatches (r, note, lastVelocity[(size_t) note], rnd, true))
                    startVoice (r, note, lastVelocity[(size_t) note]);
        }

        void startVoice (const Region& r, int note, int vel)
        {
            Voice* v = nullptr;
            for (auto& x : voices) if (! x.active()) { v = &x; break; }
            if (v == nullptr)   // steal the oldest
            {
                v = &voices[0];
                for (auto& x : voices) if (x.age < v->age) v = &x;
            }
            const float sens = get ("velsens");
            const float vNorm = vel / 127.0f;
            const float velGain = 1.0f - (r.veltrack / 100.0f) * sens * (1.0f - vNorm * vNorm);
            const float gain = juce::Decibels::decibelsToGain (r.volume + r.levelVolume) * (r.amplitude / 100.0f) * juce::jmax (0.0f, velGain);
            const float p = juce::jlimit (-1.0f, 1.0f, r.pan / 100.0f);
            v->gainL = gain * std::cos ((p + 1.0f) * juce::MathConstants<float>::pi / 4.0f) * 1.4142f;
            v->gainR = gain * std::sin ((p + 1.0f) * juce::MathConstants<float>::pi / 4.0f) * 1.4142f;
            v->region = &r;
            v->note = note;
            v->velocity = vel;
            v->pos = juce::jmax (0, r.offset);
            v->held = false;
            v->age = ++ageCounter;
            v->env = 0.0f;
            v->stage = r.delay > 0 ? 1 : 2;
            v->stageLeft = (v->stage == 1 ? r.delay : juce::jmax (0.0005f, r.attack)) * sampleRate;
            const float relSeconds = juce::jmax (0.005f, r.release * get ("release"));
            v->releaseCoef = std::exp (-1.0f / (relSeconds / 5.0f * (float) sampleRate));
            v->decayCoef = r.decay > 0 ? std::exp (-1.0f / (r.decay / 5.0f * (float) sampleRate)) : 0.0f;
            updatePitch (*v);
        }

        void updatePitch (Voice& v)
        {
            const auto& r = *v.region;
            const double semis = (v.note - r.keycenter) * (r.keytrack / 100.0) + r.transpose + r.tune / 100.0 + get ("tune") + bend;
            v.inc = std::pow (2.0, semis / 12.0) * r.sample->rate / sampleRate;
        }

        void startRelease (Voice& v)
        {
            v.held = false;
            if (v.stage != 0 && v.stage != 6) v.stage = 6;
        }

        void render (juce::AudioBuffer<float>& b, int start, int n)
        {
            if (n <= 0) return;
            auto* L = b.getWritePointer (0);
            auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : nullptr;
            for (auto& v : voices)
            {
                if (! v.active()) continue;
                updatePitch (v);
                const auto& r = *v.region;
                const auto& s = *r.sample;
                const int end = r.end > 0 ? juce::jmin (r.end, s.length - 1) : s.length - 1;
                const bool loops = (r.loopMode == 2 || (r.loopMode == 3 && v.stage != 6)) && r.loopEnd > r.loopStart && r.loopStart >= 0;
                const int lEnd = loops ? juce::jmin (r.loopEnd, end) : end;
                const float sustainLevel = juce::jlimit (0.0f, 1.0f, r.sustain / 100.0f);
                for (int i = 0; i < n; ++i)
                {
                    // envelope
                    switch (v.stage)
                    {
                        case 1: if (--v.stageLeft <= 0) { v.stage = 2; v.stageLeft = juce::jmax (0.0005f, r.attack) * sampleRate; } break;
                        case 2: v.env += 1.0f / (float) juce::jmax (1.0, (double) juce::jmax (0.0005f, r.attack) * sampleRate);
                                if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 3; v.stageLeft = r.hold * sampleRate; } break;
                        case 3: if (--v.stageLeft <= 0) v.stage = r.decay > 0 ? 4 : 5; break;
                        case 4: v.env = sustainLevel + (v.env - sustainLevel) * v.decayCoef;
                                if (v.env - sustainLevel < 1.0e-4f) v.stage = 5; break;
                        case 5: v.env = sustainLevel; break;
                        case 6: v.env *= v.releaseCoef; break;
                        default: break;
                    }
                    if (v.stage == 6 && v.env < 1.0e-4f) { v.stage = 0; break; }
                    if (v.stage == 5 && sustainLevel <= 0.0f) { v.stage = 0; break; }

                    // 4-point interpolation
                    const int idx = (int) v.pos;
                    const float frac = (float) (v.pos - idx);
                    for (int ch = 0; ch < (R != nullptr ? 2 : 1); ++ch)
                    {
                        const int c = juce::jmin (ch, s.channels - 1);
                        const float y0 = s.get (c, idx - 1), y1 = s.get (c, idx), y2 = s.get (c, idx + 1), y3 = s.get (c, idx + 2);
                        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                        const float out = ((c3 * frac + c2) * frac + c1) * frac + y1;
                        if (ch == 0) L[start + i] += out * v.env * v.gainL;
                        else         R[start + i] += out * v.env * v.gainR;
                    }
                    v.pos += v.inc;
                    if (v.pos >= lEnd)
                    {
                        if (loops) v.pos -= (lEnd - r.loopStart);
                        else { v.stage = 0; break; }
                    }
                }
            }
        }

        double sampleRate = 48000.0;
        juce::File file;
        juce::String status { juce::String (u8"Sin instrumento: elige un .sfz o un sonido (.wav)") };
        juce::CriticalSection statusLock;
        juce::SpinLock swapLock;
        std::unique_ptr<Instrument> active, incoming, retired;

        std::array<Voice, 96> voices {};
        std::array<float, 128> ccValues {};
        std::array<int, 128> seqCounter {}, lastVelocity {};
        juce::uint32 ageCounter = 0;
        bool sustain = false;
        float bend = 0.0f;
        int keyswitch = -1;
        juce::Random random { 7 };
        juce::dsp::IIR::Filter<float> tone[2];
        float lastBright = -1.0f;
    };
} // namespace

std::unique_ptr<BuiltinProcessor> createSampler() { return std::make_unique<SamplerInstrument>(); }

juce::var describeSfz (const juce::File& file)
{
    auto inst = parseInstrument (file);
    auto* o = new juce::DynamicObject();
    int lo = 127, hi = 0, missing = 0;
    juce::StringArray files;
    for (auto& r : inst->regions)
    {
        if (r.hikey < 0) continue;
        lo = juce::jmin (lo, r.lokey); hi = juce::jmax (hi, r.hikey);
        if (! files.contains (r.samplePath)) files.add (r.samplePath);
    }
    for (auto& f : files) if (! file.getParentDirectory().getChildFile (f).existsAsFile()) ++missing;
    o->setProperty ("regions", (int) inst->regions.size());
    o->setProperty ("samples", files.size());
    o->setProperty ("missing", missing);
    o->setProperty ("lokey", lo);
    o->setProperty ("hikey", hi);
    o->setProperty ("first", files.isEmpty() ? juce::String() : files[0]);
    return juce::var (o);
}

} // namespace galpon
