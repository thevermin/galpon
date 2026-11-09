#include "Engine.h"
#include "Analysis.h"
#include "Flex.h"
#include "Drummer.h"
#include "Loops.h"

namespace galpon
{

//==============================================================================
Engine::Engine (const juce::File& appDataFolder, bool openAudioDevices)
    : plugins (appDataFolder.getChildFile ("Plugins")), appData (appDataFolder)
{
    appData.createDirectory();
    audioFormats.registerBasicFormats();
    writerThread.startThread (juce::Thread::Priority::high);
    liveMidi.reset (sampleRate);
    loadSettings();
    newSong();

    if (openAudioDevices)
    {
        std::unique_ptr<juce::XmlElement> saved = juce::XmlDocument::parse (appData.getChildFile ("audio.xml"));
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000.0;      // what most amp captures are made at
        preferred.bufferSize = 256;
        auto error = deviceManager.initialise (64, 2, saved.get(), true, {}, &preferred);
        if (error.isNotEmpty())
            lastError = error;

        deviceManager.addAudioCallback (this);
        for (auto& d : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        deviceManager.addMidiInputDeviceCallback ({}, this);
    }
    else
    {
        prepareOffline (sampleRate, blockSize);
    }

    startTimer (1000);
}

Engine::~Engine()
{
    stopTimer();
    if (recording) stop();
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    saveDeviceSettings();
    saveSettings();
    if (dirty && songFolder != juce::File())
        saveSong();
    deviceManager.closeAudioDevice();
    writerThread.stopThread (4000);

    const juce::ScopedLock sl (lock);
    song.tracks.clear();
    song.masterInserts.clear();
}

void Engine::saveDeviceSettings()
{
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo (appData.getChildFile ("audio.xml"));
}

void Engine::loadSettings()
{
    auto v = juce::JSON::parse (appData.getChildFile ("settings.json"));
    recordOffsetMs = (float) (double) getOrDefault (v, "recordOffsetMs", 0.0);
    metronomeLevel = (float) (double) getOrDefault (v, "metronomeLevel", 0.6);
}

void Engine::saveSettings() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("recordOffsetMs", recordOffsetMs);
    o->setProperty ("metronomeLevel", metronomeLevel);
    appData.getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

int Engine::getLatencyCompensationSamples() const
{
    int samples = juce::roundToInt (recordOffsetMs * 0.001 * sampleRate);
    if (auto* d = deviceManager.getCurrentAudioDevice())
        samples += d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
    return juce::jmax (0, samples);
}

void Engine::prepareOffline (double sr, int bs)
{
    const juce::ScopedLock sl (lock);
    prepareAll (sr, bs);
}

//==============================================================================
// Song files
//==============================================================================
juce::File Engine::defaultSongsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Galpon");
}

juce::File Engine::uniqueSongFolder (const juce::String& name) const
{
    auto base = defaultSongsFolder();
    base.createDirectory();
    const auto clean = juce::File::createLegalFileName (name);
    auto folder = base.getChildFile (clean + ".galpon");
    for (int n = 2; folder.exists(); ++n)
        folder = base.getChildFile (clean + " " + juce::String (n) + ".galpon");
    return folder;
}

void Engine::newSong (const juce::String& name)
{
    stop();
    {
        const juce::ScopedLock sl (lock);
        song.tracks.clear();
        song.masterInserts.clear();
        Song fresh;
        song.name = name.isNotEmpty() ? name : fresh.name;
        song.bpm = fresh.bpm; song.sigNum = fresh.sigNum; song.sigDen = fresh.sigDen;
        song.key = fresh.key; song.minor = fresh.minor;
        song.cycle = false; song.cycleStart = 0; song.cycleEnd = 16;
        song.metronome = true; song.countIn = true; song.masterDb = 0; song.nextId = 1;
        position = 0;
    }
    audioCache.clear();
    undoStack.clear(); redoStack.clear();
    graveyard.clear();
    songFolder = uniqueSongFolder (song.name);   // created on disk only when something is saved or recorded
    dirty = false;
}

juce::var Engine::slotToVar (const Slot& s) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", s.kind);
    o->setProperty ("type", s.type);
    o->setProperty ("name", s.name);
    o->setProperty ("preset", s.preset);
    o->setProperty ("bypass", s.bypass);
    juce::String state = s.savedState;
    if (s.processor != nullptr)
    {
        juce::MemoryBlock mb;
        s.processor->getStateInformation (mb);
        state = mb.toBase64Encoding();
    }
    o->setProperty ("state", state);
    if (s.kind == "plugin")
        if (auto xml = s.description.createXml())
            o->setProperty ("plugin", xml->toString (juce::XmlElement::TextFormat().singleLine()));
    return juce::var (o);
}

void Engine::slotFromVar (Slot& s, const juce::var& v)
{
    s.kind = getOrDefault (v, "kind", "").toString();
    s.type = getOrDefault (v, "type", "").toString();
    s.name = getOrDefault (v, "name", "").toString();
    s.preset = getOrDefault (v, "preset", "").toString();
    s.bypass = (bool) getOrDefault (v, "bypass", false);
    s.savedState = getOrDefault (v, "state", "").toString();

    std::unique_ptr<juce::AudioProcessor> proc;
    if (s.kind == "builtin")
    {
        proc = createBuiltin (s.type);
    }
    else if (s.kind == "plugin")
    {
        if (auto xml = juce::XmlDocument::parse (getOrDefault (v, "plugin", "").toString()))
            s.description.loadFromXml (*xml);
        auto found = plugins.find (s.type);
        auto desc = found ? *found : s.description;
        juce::String error;
        proc = plugins.formats.createPluginInstance (desc, sampleRate, blockSize, error);
        if (proc == nullptr)
        {
            s.name = s.description.name.isNotEmpty() ? s.description.name : s.name;
            lastError = "Falta el plugin \"" + s.name + "\": " + error;
        }
    }

    if (proc != nullptr)
    {
        if (s.savedState.isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (s.savedState))
                proc->setStateInformation (mb.getData(), (int) mb.getSize());
        }
        prepareProcessor (*proc);
        s.processor = adoptProcessor (std::move (proc));
    }
}

juce::Result Engine::saveSong (const juce::File& target)
{
    auto folder = target == juce::File() ? songFolder : target;
    if (folder == juce::File())
        return juce::Result::fail ("No hay carpeta para guardar");

    if (folder != songFolder)
    {
        // "Guardar como": bring the recordings along
        folder.createDirectory();
        if (getAudioFolder().isDirectory())
            getAudioFolder().copyDirectoryTo (folder.getChildFile ("Audio"));
    }
    folder.createDirectory();
    folder.getChildFile ("Audio").createDirectory();

    auto* root = new juce::DynamicObject();
    {
        const juce::ScopedLock sl (lock);
        root->setProperty ("app", "Galpon");
        root->setProperty ("formatVersion", 1);
        root->setProperty ("name", song.name);
        root->setProperty ("bpm", song.bpm);
        root->setProperty ("sigNum", song.sigNum);
        root->setProperty ("sigDen", song.sigDen);
        root->setProperty ("key", song.key);
        root->setProperty ("minor", song.minor);
        root->setProperty ("cycle", song.cycle);
        root->setProperty ("cycleStart", song.cycleStart);
        root->setProperty ("cycleEnd", song.cycleEnd);
        root->setProperty ("metronome", song.metronome);
        root->setProperty ("countIn", song.countIn);
        root->setProperty ("masterDb", song.masterDb);
        root->setProperty ("limiter", song.limiter);
        root->setProperty ("nextId", song.nextId);

        juce::Array<juce::var> tracks;
        for (auto& t : song.tracks)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", t->id);
            o->setProperty ("kind", toString (t->kind));
            o->setProperty ("name", t->name);
            o->setProperty ("colour", t->colour);
            o->setProperty ("volumeDb", t->volumeDb);
            o->setProperty ("pan", t->pan);
            o->setProperty ("mute", t->mute);
            o->setProperty ("solo", t->solo);
            o->setProperty ("monitor", t->monitor);
            o->setProperty ("inputChannel", t->inputChannel);
            o->setProperty ("stereoInput", t->stereoInput);
            o->setProperty ("inputGainDb", t->inputGainDb);
            o->setProperty ("sends", sendsToVar (t->sends));
            o->setProperty ("output", t->output);
            o->setProperty ("automation", automationToVar (t->automation));
            if (! t->instrument.isEmpty())
                o->setProperty ("instrument", slotToVar (t->instrument));
            juce::Array<juce::var> inserts;
            for (auto& s : t->inserts) inserts.add (slotToVar (s));
            o->setProperty ("inserts", inserts);
            o->setProperty ("drummer", drummerToVar (t->drummer));
            juce::Array<juce::var> clips;
            for (auto& c : t->clips) clips.add (clipToVar (c));
            o->setProperty ("clips", clips);
            tracks.add (juce::var (o));
        }
        root->setProperty ("tracks", tracks);

        juce::Array<juce::var> master;
        for (auto& s : song.masterInserts) master.add (slotToVar (s));
        root->setProperty ("master", master);
        root->setProperty ("chords", chordsToVar (song.chords));
        root->setProperty ("sections", sectionsToVar (song.sections));
    }

    {
        juce::Array<juce::var> ignored;
        for (auto& f : ignoredRecordings) ignored.add (f);
        root->setProperty ("ignoredRecordings", ignored);
    }
    auto json = juce::JSON::toString (juce::var (root));
    auto file = folder.getChildFile ("project.json");
    auto temp = folder.getChildFile ("project.json.tmp");
    // keep the previous version: if something ever goes wrong with a save, the song is still there
    if (file.existsAsFile() && juce::JSON::parse (file).isObject())
        file.copyFileTo (folder.getChildFile ("project.backup.json"));
    if (! temp.replaceWithText (json) || ! temp.moveFileTo (file))
        return juce::Result::fail ("No se pudo escribir " + file.getFullPathName());

    songFolder = folder;
    dirty = false;
    return juce::Result::ok();
}

juce::Result Engine::loadSong (const juce::File& folderIn)
{
    auto folder = folderIn.getFileName() == "project.json" ? folderIn.getParentDirectory() : folderIn;
    auto v = juce::JSON::parse (folder.getChildFile ("project.json"));
    bool fromBackup = false;
    if (getOrDefault (v, "app", "").toString() != "Galpon")
    {
        // damaged song file: the previous save is next to it
        v = juce::JSON::parse (folder.getChildFile ("project.backup.json"));
        fromBackup = true;
    }
    if (getOrDefault (v, "app", "").toString() != "Galpon")
        return juce::Result::fail (juce::String (u8"Esa carpeta no tiene una canción de Galpon"));

    stop();
    Song loaded;
    songFolder = folder;
    audioCache.clear();

    loaded.name = getOrDefault (v, "name", juce::String (u8"Canción")).toString();
    loaded.bpm = juce::jlimit (20.0, 400.0, (double) getOrDefault (v, "bpm", 120.0));
    loaded.sigNum = juce::jlimit (1, 16, (int) getOrDefault (v, "sigNum", 4));
    loaded.sigDen = juce::jlimit (1, 16, (int) getOrDefault (v, "sigDen", 4));
    loaded.key = getOrDefault (v, "key", "C").toString();
    loaded.minor = (bool) getOrDefault (v, "minor", false);
    loaded.cycle = (bool) getOrDefault (v, "cycle", false);
    loaded.cycleStart = (double) getOrDefault (v, "cycleStart", 0.0);
    loaded.cycleEnd = (double) getOrDefault (v, "cycleEnd", 16.0);
    loaded.metronome = (bool) getOrDefault (v, "metronome", true);
    loaded.countIn = (bool) getOrDefault (v, "countIn", true);
    loaded.masterDb = (float) (double) getOrDefault (v, "masterDb", 0.0);
    loaded.limiter = (bool) getOrDefault (v, "limiter", true);
    loaded.nextId = (int) getOrDefault (v, "nextId", 1);
    ignoredRecordings.clear();
    if (auto* ig = getOrDefault (v, "ignoredRecordings", {}).getArray())
        for (auto& f : *ig) ignoredRecordings.add (f.toString());
    if (fromBackup)
        lastError = juce::String (u8"El archivo de la canción estaba dañado: abre la copia anterior (puede faltar el último cambio).");

    if (auto* tracks = getOrDefault (v, "tracks", {}).getArray())
    {
        for (auto& tv : *tracks)
        {
            auto t = std::make_unique<Track>();
            t->id = (int) getOrDefault (tv, "id", loaded.nextId);
            loaded.nextId = juce::jmax (loaded.nextId, t->id + 1);
            t->kind = trackKindFromString (getOrDefault (tv, "kind", "instrument").toString());
            t->name = getOrDefault (tv, "name", "Pista").toString();
            t->colour = getOrDefault (tv, "colour", colourForKind (t->kind, t->id)).toString();
            t->volumeDb = (float) (double) getOrDefault (tv, "volumeDb", 0.0);
            t->pan = (float) (double) getOrDefault (tv, "pan", 0.0);
            t->mute = (bool) getOrDefault (tv, "mute", false);
            t->solo = (bool) getOrDefault (tv, "solo", false);
            t->monitor = (bool) getOrDefault (tv, "monitor", false);
            t->inputChannel = (int) getOrDefault (tv, "inputChannel", 0);
            t->stereoInput = (bool) getOrDefault (tv, "stereoInput", false);
            if (auto inst = getOrDefault (tv, "instrument", {}); inst.isObject())
                slotFromVar (t->instrument, inst);
            if (auto* inserts = getOrDefault (tv, "inserts", {}).getArray())
                for (auto& sv : *inserts)
                {
                    Slot s;
                    slotFromVar (s, sv);
                    t->inserts.push_back (std::move (s));
                }
            drummerFromVar (t->drummer, getOrDefault (tv, "drummer", {}));
            if (auto* clips = getOrDefault (tv, "clips", {}).getArray())
                for (auto& cv : *clips)
                {
                    Clip c;
                    clipFromVar (c, cv);
                    loaded.nextId = juce::jmax (loaded.nextId, c.id + 1);
                    if (! c.midi) c.audio = loadAudio (c.audioFile);
                    t->clips.push_back (std::move (c));
                }
            t->inputGainDb = (float) (double) getOrDefault (tv, "inputGainDb", 0.0);
            t->sends = sendsFromVar (getOrDefault (tv, "sends", {}));
            t->output = (int) getOrDefault (tv, "output", 0);
            t->automation = automationFromVar (getOrDefault (tv, "automation", {}));
            prepareTrack (*t);
            loaded.tracks.push_back (std::move (t));
        }
    }
    if (auto* master = getOrDefault (v, "master", {}).getArray())
        for (auto& sv : *master)
        {
            Slot s;
            slotFromVar (s, sv);
            loaded.masterInserts.push_back (std::move (s));
        }

    {
        const juce::ScopedLock sl (lock);
        std::swap (song.tracks, loaded.tracks);
        std::swap (song.masterInserts, loaded.masterInserts);
        song.name = loaded.name; song.bpm = loaded.bpm; song.sigNum = loaded.sigNum; song.sigDen = loaded.sigDen;
        song.key = loaded.key; song.minor = loaded.minor; song.cycle = loaded.cycle;
        song.cycleStart = loaded.cycleStart; song.cycleEnd = loaded.cycleEnd; song.metronome = loaded.metronome;
        song.countIn = loaded.countIn; song.masterDb = loaded.masterDb; song.nextId = loaded.nextId;
        song.limiter = loaded.limiter;
        song.chords = chordsFromVar (getOrDefault (v, "chords", {}));
        song.sections = sectionsFromVar (getOrDefault (v, "sections", {}));
        position = 0;
    }
    // old tracks (now in `loaded`) are destroyed here, outside the lock
    undoStack.clear(); redoStack.clear();
    graveyard.clear();
    dirty = false;
    return juce::Result::ok();
}

//==============================================================================
// Audio files
//==============================================================================
std::shared_ptr<const juce::AudioBuffer<float>> Engine::loadAudio (const juce::String& fileName)
{
    if (fileName.isEmpty()) return nullptr;
    if (auto it = audioCache.find (fileName); it != audioCache.end())
        return it->second;

    auto file = getAudioFolder().getChildFile (fileName);
    std::unique_ptr<juce::AudioFormatReader> reader (audioFormats.createReaderFor (file));
    if (reader == nullptr) return nullptr;

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, std::numeric_limits<int>::max() / 2);
    juce::AudioBuffer<float> raw (channels, length);
    reader->read (&raw, 0, length, 0, true, channels > 1);

    auto result = std::make_shared<juce::AudioBuffer<float>>();
    if (std::abs (reader->sampleRate - sampleRate) < 0.5)
    {
        *result = std::move (raw);
    }
    else
    {
        // Convert to the engine's rate once, when loading
        const double ratio = reader->sampleRate / sampleRate;
        const int outLen = (int) std::ceil (length / ratio);
        result->setSize (channels, outLen);
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, raw.getReadPointer (ch), result->getWritePointer (ch), outLen, length, 0);
        }
    }
    audioCache[fileName] = result;
    return result;
}

void Engine::reloadAudioForSampleRate()
{
    audioCache.clear();
    for (auto& t : song.tracks)
        for (auto& c : t->clips)
            if (! c.midi)
                c.audio = loadAudio (c.audioFile);
}

//==============================================================================
// Device callbacks
//==============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);
    prepareAll (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void Engine::audioDeviceStopped()
{
    // The interface went away (unplugged, sleep, settings changed) while recording:
    // stop writing now and let the message thread close the files cleanly.
    if (recording.load()) deviceLostWhileRecording = true;
}

void Engine::prepareAll (double sr, int bs)
{
    const bool rateChanged = std::abs (sr - sampleRate) > 0.5;
    const double oldRate = sampleRate;
    sampleRate = sr;
    blockSize = juce::jmax (16, bs);
    game.prepare (sr, blockSize);
    scorer.prepare (sr);
    scorerScratch.assign ((size_t) juce::jmax (blockSize, 4096) * 2, 0.0f);

    if (rateChanged)
    {
        position = (juce::int64) (position.load() * sr / oldRate);
        reloadAudioForSampleRate();
    }

    master.setSize (2, blockSize);
    scratch.setSize (32, blockSize);
    liveBlock.ensureSize (4096);
    liveMidi.reset (sr);
    limiter.prepare ({ sr, (juce::uint32) blockSize, 2 });
    limiter.setThreshold (-1.0f);
    limiter.setRelease (120.0f);
    limiter.reset();

    for (auto& t : song.tracks)
    {
        prepareTrack (*t);
        if (t->instrument.processor) prepareProcessor (*t->instrument.processor);
        for (auto& s : t->inserts)
            if (s.processor) prepareProcessor (*s.processor);
    }
    for (auto& s : song.masterInserts)
        if (s.processor) prepareProcessor (*s.processor);
}

void Engine::prepareTrack (Track& t)
{
    t.buffer.setSize (2, blockSize);
    t.midi.ensureSize (8192);
    // up to half a second of plugin delay can be lined up
    t.delayLine.setSize (2, (int) (sampleRate * 0.5) + blockSize + 1);
    t.delayLine.clear();
    t.delayWrite = 0;
}

int Engine::trackLatency (const Track& t) const
{
    int total = 0;
    if (t.kind != TrackKind::Audio && t.instrument.processor != nullptr && ! t.instrument.bypass)
        total += t.instrument.processor->getLatencySamples();
    for (auto& s : t.inserts)
        if (s.processor != nullptr && ! s.bypass)
            total += s.processor->getLatencySamples();
    return juce::jmax (0, total);
}

void Engine::prepareProcessor (juce::AudioProcessor& p)
{
    p.setPlayHead (this);

    // Ask for a stereo main bus where the plugin allows it
    auto layout = p.getBusesLayout();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (p.checkBusesLayoutSupported (layout))
        p.setBusesLayout (layout);

    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    p.setNonRealtime (offline);
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                               float* const* outputs, int numOutputs, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&)
{
    if (offlineBusy.load())
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr) juce::FloatVectorOperations::clear (outputs[ch], numSamples);
        return;
    }

    renderBlock (outputs, numOutputs, inputs, numInputs, numSamples);
}

void Engine::renderBlock (float* const* out, int numOut, const float* const* in, int numIn, int n)
{
    const juce::ScopedTryLock tl (lock);
    if (! tl.isLocked())
    {
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], n);
        return;
    }

    // Some setups (e.g. input and output on different devices) deliver blocks bigger than the
    // size the device reported. Before, those blocks came out silent and the song crawled;
    // now they're rendered in pieces.
    constexpr int maxChannels = 64;
    float* outPtrs[maxChannels] {};
    const float* inPtrs[maxChannels] {};
    numOut = juce::jmin (numOut, maxChannels);
    numIn  = juce::jmin (numIn, maxChannels);

    for (int done = 0; done < n;)
    {
        const int len = juce::jmin (blockSize, n - done);
        for (int ch = 0; ch < numOut; ++ch) outPtrs[ch] = out[ch] != nullptr ? out[ch] + done : nullptr;
        for (int ch = 0; ch < numIn; ++ch)  inPtrs[ch]  = (in != nullptr && in[ch] != nullptr) ? in[ch] + done : nullptr;
        renderLocked (outPtrs, numOut, in != nullptr ? inPtrs : nullptr, numIn, len);
        done += len;
    }
}

void Engine::renderLocked (float* const* out, int numOut, const float* const* in, int numIn, int n)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], n);

    liveBlock.clear();
    if (! offline)
        liveMidi.removeNextBlockOfMessages (liveBlock, n);

    int done = 0;
    bool first = true;
    while (done < n)
    {
        int len = n - done;
        const bool cycling = playing && song.cycle && song.cycleEnd > song.cycleStart + 0.1;
        const auto cycleEndS = (juce::int64) beatsToSamples (song.cycleEnd);
        if (cycling && position < cycleEndS && position + len > cycleEndS)
            len = (int) (cycleEndS - position);

        renderSegment (done, len, in, numIn, first);
        for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::copy (out[ch] + done, master.getReadPointer (ch), len);
        if (numOut == 1 && out[0] != nullptr)
            juce::FloatVectorOperations::add (out[0] + done, master.getReadPointer (1), len);

        if (playing)
        {
            position += len;
            if (cycling && position >= cycleEndS)
            {
                if (recording) closeOpenMidiNotes (song.cycleEnd);   // a held note ends with the pass
                position = (juce::int64) beatsToSamples (song.cycleStart);
                for (auto& t : song.tracks) t->needsAllNotesOff = true;
            }
        }
        done += len;
        first = false;
    }
}

void Engine::processSlot (juce::AudioProcessor& p, juce::AudioBuffer<float>& view, juce::MidiBuffer& midi)
{
    const int n = view.getNumSamples();
    const int numCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    if (numCh == 2)
    {
        p.processBlock (view, midi);
    }
    else if (numCh == 1)
    {
        juce::AudioBuffer<float> mono (view.getArrayOfWritePointers(), 1, n);
        p.processBlock (mono, midi);
        view.copyFrom (1, 0, view, 0, 0, n);
    }
    else if (numCh > 2 && numCh <= scratch.getNumChannels())
    {
        for (int ch = 0; ch < numCh; ++ch)
            if (ch < 2) scratch.copyFrom (ch, 0, view, ch, 0, n);
            else        scratch.clear (ch, 0, n);
        juce::AudioBuffer<float> wide (scratch.getArrayOfWritePointers(), numCh, n);
        p.processBlock (wide, midi);
        for (int ch = 0; ch < 2; ++ch)
            view.copyFrom (ch, 0, scratch, ch, 0, n);
    }
}

void Engine::collectMidi (Track& t, double beat0, double beat1, juce::int64 segStart, int len, juce::MidiBuffer& midi)
{
    auto toOffset = [&] (double beat)
    {
        return juce::jlimit (0, len - 1, (int) (beatsToSamples (beat) - (double) segStart));
    };

    for (int pass = 0; pass < 2; ++pass)    // note-offs first, so repeated notes retrigger cleanly
    {
        for (auto& c : t.clips)
        {
            if (! c.midi || c.end() <= beat0 || c.start >= beat1) continue;
            const double L = juce::jmax (0.25, juce::jmin (c.loopLength, c.length));
            const double local0 = juce::jmax (0.0, beat0 - c.start);
            const double local1 = juce::jmin (c.length, beat1 - c.start);
            const int firstRep = (int) std::floor (juce::jmax (0.0, local0 - L) / L);
            const int lastRep  = (int) std::floor (local1 / L);

            for (int rep = firstRep; rep <= lastRep; ++rep)
            {
                for (auto& note : c.notes)
                {
                    if (note.beat >= L) continue;
                    const double on = rep * L + note.beat;
                    // end notes a hair before the clip/loop boundary so the note-off is never lost
                    const double off = juce::jmin (on + note.length, c.length - 1.0e-6, rep * L + L - 1.0e-6);
                    if (pass == 0)
                    {
                        if (off > on && off >= local0 && off < local1)
                            midi.addEvent (juce::MidiMessage::noteOff (1, note.pitch), toOffset (c.start + off));
                    }
                    else if (on >= local0 && on < local1 && on < c.length)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (1, note.pitch, juce::jlimit (0.01f, 1.0f, note.velocity)),
                                       toOffset (c.start + on));
                    }
                }

                // Pedal, pitch bend and knobs: before the notes that share their time
                if (pass == 0)
                {
                    for (auto& e : c.events)
                    {
                        if (e.beat >= L) continue;
                        const double at = rep * L + e.beat;
                        if (at < local0 || at >= local1 || at >= c.length) continue;
                        midi.addEvent (e.kind == MidiEvent::PitchBend ? juce::MidiMessage::pitchWheel (1, e.value)
                                                                      : juce::MidiMessage::controllerEvent (1, e.number, e.value),
                                       toOffset (c.start + at));
                    }
                }
            }
        }
    }
}

void Engine::mixAudioClips (Track& t, juce::int64 segStart, int len, juce::AudioBuffer<float>& view)
{
    for (auto& c : t.clips)
    {
        if (c.midi || c.audio == nullptr) continue;
        const auto clipStart = (juce::int64) beatsToSamples (c.start);
        const auto clipEnd   = (juce::int64) beatsToSamples (c.end());
        const auto s0 = juce::jmax (segStart, clipStart);
        const auto s1 = juce::jmin (segStart + len, clipEnd);
        if (s1 <= s0) continue;

        const auto& src = *c.audio;
        const auto srcIndex = c.offsetSamples + (s0 - clipStart);
        if (srcIndex < 0 || srcIndex >= src.getNumSamples()) continue;
        const int count = (int) std::min<juce::int64> (s1 - s0, src.getNumSamples() - srcIndex);
        const float gain = juce::Decibels::decibelsToGain (c.gainDb);
        const int dest = (int) (s0 - segStart);

        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, src.getNumChannels() - 1);
            view.addFrom (ch, dest, src, srcCh, (int) srcIndex, count, gain);
        }
    }
}

void Engine::addMetronome (juce::int64 segStart, int len, int)
{
    const bool countingIn = recording && segStart < recordStartSample;
    if (offline || ! playing || ! (song.metronome || countingIn)) return;

    const double bpb = song.beatsPerBar();
    const double beatUnit = 4.0 / song.sigDen;   // click on each beat of the time signature
    auto* l = master.getWritePointer (0);
    auto* r = master.getWritePointer (1);

    for (int i = 0; i < len; ++i)
    {
        const double b0 = samplesToBeats ((double) (segStart + i));
        const double b1 = samplesToBeats ((double) (segStart + i + 1));
        const double k = std::ceil (b0 / beatUnit) * beatUnit;
        if (k >= b0 && k < b1)
        {
            const bool downbeat = std::abs (std::fmod (k, bpb)) < 1.0e-6 || std::abs (std::fmod (k, bpb) - bpb) < 1.0e-6;
            metronomeFreq = downbeat ? 1600.0 : 1050.0;
            metronomeEnv = downbeat ? 1.0 : 0.7;
            metronomePhase = 0.0;
        }
        if (metronomeEnv > 0.0005)
        {
            const float s = (float) (std::sin (metronomePhase * juce::MathConstants<double>::twoPi) * metronomeEnv) * metronomeLevel * 0.5f;
            l[i] += s; r[i] += s;
            metronomePhase += metronomeFreq / sampleRate;
            metronomeEnv *= std::exp (-1.0 / (0.025 * sampleRate));
        }
    }
}

void Engine::renderSegment (int offset, int len, const float* const* in, int numIn, bool firstSegment)
{
    const juce::int64 segStart = position.load();
    const double beat0 = samplesToBeats ((double) segStart);
    const double beat1 = samplesToBeats ((double) (segStart + len));
    const bool isPlayingNow = playing.load();

    PositionInfo info;
    info.setBpm (song.bpm);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { song.sigNum, song.sigDen });
    info.setTimeInSamples (segStart);
    info.setTimeInSeconds ((double) segStart / sampleRate);
    info.setPpqPosition (beat0);
    info.setPpqPositionOfLastBarStart (std::floor (beat0 / song.beatsPerBar()) * song.beatsPerBar());
    info.setBarCount ((juce::int64) std::floor (beat0 / song.beatsPerBar()));
    info.setIsPlaying (isPlayingNow);
    info.setIsRecording (recording.load());
    info.setIsLooping (song.cycle);
    info.setLoopPoints (juce::AudioPlayHead::LoopPoints { song.cycleStart, song.cycleEnd });
    currentPosition = info;

    master.clear (0, 0, len);
    master.clear (1, 0, len);

    bool anySolo = false;
    int maxLat = 0;
    for (auto& t : song.tracks)
    {
        anySolo |= t->solo;
        if (t->kind != TrackKind::Bus)    // buses aren't lined up (their effects are usually latency-free)
            maxLat = juce::jmax (maxLat, trackLatency (*t));
    }
    maxTrackLatency = maxLat;

    const int selected = selectedTrackId.load();
    const bool recordingNow = recording.load() && isPlayingNow && ! deviceLostWhileRecording.load();

    // Buses start empty every block: the tracks pour into them, then they're mixed like any track
    for (auto& t : song.tracks)
        if (t->kind == TrackKind::Bus) t->buffer.clear (0, len);
    auto findBus = [this] (int id) -> Track*
    {
        if (id <= 0) return nullptr;
        for (auto& b : song.tracks)
            if (b->id == id && b->kind == TrackKind::Bus) return b.get();
        return nullptr;
    };

    for (int pass = 0; pass < 2; ++pass)    // 0: tracks, 1: buses
    for (auto& tp : song.tracks)
    {
        auto& t = *tp;
        const bool isBus = t.kind == TrackKind::Bus;
        if (isBus != (pass == 1)) continue;
        juce::AudioBuffer<float> view (t.buffer.getArrayOfWritePointers(), 2, len);
        if (! isBus) view.clear();
        t.midi.clear();

        if (t.needsAllNotesOff.exchange (false))
        {
            t.midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);   // pedal up
            t.midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
            t.midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            t.midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
        }

        if (isPlayingNow)
        {
            if (t.kind == TrackKind::Audio) mixAudioClips (t, segStart, len, view);
            else                            collectMidi (t, beat0, beat1, segStart, len, t.midi);
        }

        // Live MIDI (keyboard, on-screen keys) goes to the selected instrument track
        if (t.id == selected && t.kind != TrackKind::Audio && ! isBus)
        {
            for (const auto meta : liveBlock)
            {
                if (meta.samplePosition < offset || meta.samplePosition >= offset + len) continue;
                const auto msg = meta.getMessage();
                t.midi.addEvent (msg, meta.samplePosition - offset);

                if (recordingNow && segStart + (meta.samplePosition - offset) >= recordStartSample)
                {
                    for (auto& take : midiTakes)
                    {
                        if (take.trackId != t.id) continue;
                        const double beat = samplesToBeats ((double) (segStart + meta.samplePosition - offset));
                        const int p = msg.getNoteNumber();
                        if ((msg.isController() || msg.isPitchWheel()) && take.events.size() < take.events.capacity())
                        {
                            if (msg.isPitchWheel()) take.events.push_back ({ beat, MidiEvent::PitchBend, 0, msg.getPitchWheelValue() });
                            else                    take.events.push_back ({ beat, MidiEvent::Controller, msg.getControllerNumber(), msg.getControllerValue() });
                            continue;
                        }
                        if (msg.isNoteOn())
                        {
                            take.open[(size_t) p] = true;
                            take.openBeat[(size_t) p] = beat;
                            take.openVel[(size_t) p] = msg.getFloatVelocity();
                        }
                        else if (msg.isNoteOff() && take.open[(size_t) p] && take.notes.size() < take.notes.capacity())
                        {
                            take.open[(size_t) p] = false;
                            take.notes.push_back ({ take.openBeat[(size_t) p], juce::jmax (0.02, beat - take.openBeat[(size_t) p]), p, take.openVel[(size_t) p] });
                        }
                    }
                }
            }
        }
        juce::ignoreUnused (firstSegment);

        // Live audio input (monitoring), and the tuner
        if (t.kind == TrackKind::Audio && numIn > 0 && in != nullptr)
        {
            const int chL = juce::jlimit (0, numIn - 1, t.inputChannel);
            const int chR = (t.stereoInput && chL + 1 < numIn) ? chL + 1 : chL;
            const float inGain = juce::Decibels::decibelsToGain (t.inputGainDb);
            if (in[chL] != nullptr)
            {
                // input meter (what will be recorded)
                auto range = juce::FloatVectorOperations::findMinAndMax (in[chL] + offset, len);
                float peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
                if (chR != chL && in[chR] != nullptr)
                {
                    range = juce::FloatVectorOperations::findMinAndMax (in[chR] + offset, len);
                    peak = juce::jmax (peak, std::abs (range.getStart()), std::abs (range.getEnd()));
                }
                peak *= inGain;
                if (peak > t.inputPeak.load()) t.inputPeak = peak;
            }
            if (t.monitor && in[chL] != nullptr)
            {
                const float g = (chL == chR ? 0.7071f : 1.0f) * inGain;
                view.addFrom (0, 0, in[chL] + offset, len, g);
                if (in[chR] != nullptr) view.addFrom (1, 0, in[chR] + offset, len, g);
            }
            if (tunerEnabled && t.id == selected && in[chL] != nullptr)
            {
                int w = tunerWrite.load();
                for (int i = 0; i < len; ++i)
                {
                    tunerRing[(size_t) w] = in[chL][offset + i] * inGain;
                    w = (w + 1) % (int) tunerRing.size();
                }
                tunerWrite = w;
            }
        }

        if (t.kind != TrackKind::Audio && t.instrument.processor != nullptr && ! t.instrument.bypass)
            processSlot (*t.instrument.processor, view, t.midi);

        // automated effect controls: set once per block (only when they change)
        for (auto& lane : t.automation)
        {
            if (lane.kind != AutomationLane::FxParam || ! lane.active()) continue;
            if (lane.a < 0 || lane.a >= (int) t.inserts.size() || t.inserts[(size_t) lane.a].processor == nullptr) continue;
            auto& params = t.inserts[(size_t) lane.a].processor->getParameters();
            if (lane.b < 0 || lane.b >= params.size()) continue;
            const float v = juce::jlimit (0.0f, 1.0f, lane.valueAt (beat0));
            if (std::abs (v - lane.lastApplied) > 1.0e-4f) { params[lane.b]->setValue (v); lane.lastApplied = v; }
        }

        for (auto& s : t.inserts)
            if (s.processor != nullptr && ! s.bypass)
                processSlot (*s.processor, view, emptyMidi);
        emptyMidi.clear();

        // Plugin delay compensation: hold back the tracks whose plugins are faster than the slowest one
        if (! isBus && maxTrackLatency > 0 && t.delayLine.getNumSamples() > len)
        {
            const int size = t.delayLine.getNumSamples();
            const int delay = juce::jlimit (0, size - len - 1, maxTrackLatency - trackLatency (t));
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* line = t.delayLine.getWritePointer (ch);
                auto* d = view.getWritePointer (ch);
                int w = t.delayWrite;
                for (int i = 0; i < len; ++i)
                {
                    line[w] = d[i];
                    int r = w - delay;
                    if (r < 0) r += size;
                    d[i] = line[r];
                    if (++w == size) w = 0;
                }
            }
            t.delayWrite = (t.delayWrite + len) % size;
        }

        bool audible = ! t.mute && (! anySolo || t.solo);
        if (isBus && ! audible && ! t.mute)
        {
            // a bus stays on while a soloed track is feeding it (otherwise solo would kill its reverb)
            for (auto& o : song.tracks)
            {
                if (! o->solo || o->mute || o->kind == TrackKind::Bus) continue;
                bool feeds = o->output == t.id;
                for (auto& snd : o->sends) feeds |= snd.busId == t.id;
                if (feeds) { audible = true; break; }
            }
        }

        auto sendTo = [&] (const Send& snd)
        {
            if (auto* bus = findBus (snd.busId))
            {
                float level = snd.levelDb;
                for (auto& lane : t.automation)
                    if (lane.kind == AutomationLane::SendLevel && lane.a == snd.busId && lane.active()) level = lane.valueAt (beat0);
                const float g = juce::Decibels::decibelsToGain (level, -80.0f);
                if (g <= 0.0f) return;
                bus->buffer.addFrom (0, 0, view, 0, 0, len, g);
                bus->buffer.addFrom (1, 0, view, 1, 0, len, g);
            }
        };
        if (! isBus && audible)
            for (auto& snd : t.sends) if (snd.pre) sendTo (snd);

        // volume and pan, from the fader or from automation (ramped across the block: no zipper noise)
        float vol0 = t.volumeDb, vol1 = t.volumeDb, pan0 = t.pan, pan1 = t.pan;
        for (auto& lane : t.automation)
        {
            if (! lane.active()) continue;
            if (lane.kind == AutomationLane::Volume) { vol0 = lane.valueAt (beat0); vol1 = lane.valueAt (beat1); }
            else if (lane.kind == AutomationLane::Pan) { pan0 = juce::jlimit (-1.0f, 1.0f, lane.valueAt (beat0)); pan1 = juce::jlimit (-1.0f, 1.0f, lane.valueAt (beat1)); }
        }
        if (! isPlayingNow) { vol1 = vol0; pan1 = pan0; }
        const float gain0 = juce::Decibels::decibelsToGain (vol0, -80.0f), gain1 = juce::Decibels::decibelsToGain (vol1, -80.0f);
        const float lg = gain0 * juce::jmin (1.0f, 1.0f - pan0), lg1 = gain1 * juce::jmin (1.0f, 1.0f - pan1);
        const float rg = gain0 * juce::jmin (1.0f, 1.0f + pan0), rg1 = gain1 * juce::jmin (1.0f, 1.0f + pan1);
        if (std::abs (lg - lg1) < 1.0e-6f) view.applyGain (0, 0, len, lg); else view.applyGainRamp (0, 0, len, lg, lg1);
        if (std::abs (rg - rg1) < 1.0e-6f) view.applyGain (1, 0, len, rg); else view.applyGainRamp (1, 0, len, rg, rg1);

        const float pl = view.getMagnitude (0, 0, len), pr = view.getMagnitude (1, 0, len);
        if (pl > t.peakL.load()) t.peakL = pl;
        if (pr > t.peakR.load()) t.peakR = pr;

        if (! isBus && audible)
            for (auto& snd : t.sends) if (! snd.pre) sendTo (snd);

        if (audible)
        {
            auto* dest = isBus ? nullptr : findBus (t.output);
            auto& target = dest != nullptr ? dest->buffer : master;
            target.addFrom (0, 0, view, 0, 0, len);
            target.addFrom (1, 0, view, 1, 0, len);
        }
    }

    juce::AudioBuffer<float> masterView (master.getArrayOfWritePointers(), 2, len);
    if (! offline && game.isLoaded())
    {
        // your guitar, stamped with the song time you were hearing when you played it
        const int ch = scorerInput.load();
        if (ch >= 0 && ch < numIn && in[ch] != nullptr && len <= (int) scorerScratch.size())
        {
            const float g = scorerGain.load();
            for (int i = 0; i < len; ++i) scorerScratch[(size_t) i] = in[ch][offset + i] * g;
            const double sp = game.getSpeed();
            scorer.push (scorerScratch.data(), len, game.positionSeconds() - scorerLatency.load() * sp, sp / sampleRate, game.isPlaying());
        }
        game.render (masterView, len);
    }     // game mode: the song's stems, next to your live guitar
    for (auto& s : song.masterInserts)
        if (s.processor != nullptr && ! s.bypass)
            processSlot (*s.processor, masterView, emptyMidi);
    emptyMidi.clear();
    masterView.applyGain (juce::Decibels::decibelsToGain (song.masterDb, -80.0f));
    if (song.limiter)
    {
        juce::dsp::AudioBlock<float> block (masterView);
        limiter.process (juce::dsp::ProcessContextReplacing<float> (block));
        masterView.applyGain (0.891f);   // JUCE's limiter tops out at 0 dB: leave 1 dB of room (MP3s overshoot)
    }

    addMetronome (segStart, len, offset);

    const float ml = masterView.getMagnitude (0, 0, len), mr = masterView.getMagnitude (1, 0, len);
    if (ml > masterPeakL.load()) masterPeakL = ml;
    if (mr > masterPeakR.load()) masterPeakR = mr;

    // Recording: write the raw input of every armed audio track
    if (recordingNow && in != nullptr && segStart + len > recordStartSample)
    {
        const int skip = (int) std::max<juce::int64> (0, recordStartSample - segStart);
        for (auto& take : audioTakes)
        {
            if (take.writer == nullptr || numIn <= 0) continue;
            auto* tr = song.findTrack (take.trackId);
            const float inGain = tr != nullptr ? juce::Decibels::decibelsToGain (tr->inputGainDb) : 1.0f;
            const float* ptrs[2] = { nullptr, nullptr };
            for (int ch = 0; ch < take.numChannels; ++ch)
            {
                const int src = juce::jlimit (0, numIn - 1, take.channel + ch);
                if (in[src] == nullptr) continue;
                // scratch is free here: all processing for this block is done
                auto* dest = scratch.getWritePointer (ch);
                juce::FloatVectorOperations::copyWithMultiply (dest, in[src] + offset + skip, inGain, len - skip);
                ptrs[ch] = dest;
            }
            if (ptrs[0] != nullptr)
            {
                if (take.numChannels == 2 && ptrs[1] == nullptr) ptrs[1] = ptrs[0];
                take.writer->write (ptrs, len - skip);   // never blocks; anything lost is counted
            }
        }
    }
}

//==============================================================================
// Transport
//==============================================================================
double Engine::getPositionBeats() const noexcept
{
    return (double) position.load() / sampleRate * song.bpm / 60.0;
}

bool Engine::isCountingIn() const noexcept
{
    return recording.load() && position.load() < recordStartSample;
}

void Engine::setPositionBeats (double beats)
{
    const juce::ScopedLock sl (lock);
    position = (juce::int64) beatsToSamples (juce::jmax (0.0, beats));
    for (auto& t : song.tracks) t->needsAllNotesOff = true;
}

void Engine::play()
{
    if (playing) return;
    const juce::ScopedLock sl (lock);
    if (position < 0) position = 0;
    // With the cycle on, start inside it (like GarageBand)
    if (song.cycle && song.cycleEnd > song.cycleStart)
    {
        const auto a = (juce::int64) beatsToSamples (song.cycleStart), b = (juce::int64) beatsToSamples (song.cycleEnd);
        if (position < a || position >= b) position = a;
    }
    playing = true;
}

void Engine::sendAllNotesOff()
{
    for (auto& t : song.tracks) t->needsAllNotesOff = true;
}

void Engine::stop()
{
    bool wasRecording = false;
    {
        const juce::ScopedLock sl (lock);
        wasRecording = recording.exchange (false);
        playing = false;
        if (position < 0) position = 0;
        sendAllNotesOff();
    }
    if (wasRecording)
        finishRecording();
}

double Engine::outputLatencySeconds() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return (d->getOutputLatencyInSamples() + d->getCurrentBufferSizeSamples()) / juce::jmax (1.0, d->getCurrentSampleRate());
    return 0.0;
}

juce::Result Engine::record()
{
    if (recording)
    {
        stop();
        return juce::Result::ok();
    }

    // Nothing armed? Record on the selected track, like most apps do.
    bool anyArmed = false;
    for (auto& t : song.tracks) anyArmed |= t->armed && t->kind == TrackKind::Audio;
    auto* selected = song.findTrack (selectedTrackId);
    autoArmedTrackId = 0;
    if (! anyArmed && selected != nullptr && selected->kind == TrackKind::Audio)
    {
        selected->armed = true;
        autoArmedTrackId = selected->id;
    }

    auto folder = getAudioFolder();
    if (! folder.createDirectory())
        return juce::Result::fail ("No se puede crear la carpeta de grabaciones: " + folder.getFullPathName());

    // Inputs that exist on this interface
    int inputsAvailable = -1;      // -1: no interface open (tests, offline): don't check
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        inputsAvailable = dev->getActiveInputChannels().countNumberOfSetBits();

    std::vector<int> armedAudio;
    juce::int64 bytesPerSecond = 0;
    for (auto& t : song.tracks)
    {
        if (! t->armed || t->kind != TrackKind::Audio) continue;
        const int needed = t->inputChannel + (t->stereoInput ? 2 : 1);
        if (inputsAvailable >= 0 && needed > inputsAvailable)
            return juce::Result::fail ("La pista \"" + t->name + "\" graba de una entrada que esta interfaz no tiene"
                                       + juce::String (u8" (elige otra entrada en la pista o en Ajustes)."));
        armedAudio.push_back (t->id);
        bytesPerSecond += TakeWriter::bytesPerSecond (sampleRate, t->stereoInput ? 2 : 1);
    }

    // Enough disk for the recording? (warn under 30 minutes, refuse under 1 minute)
    if (bytesPerSecond > 0)
    {
        const auto freeBytes = folder.getBytesFreeOnVolume();
        if (freeBytes > 0 && freeBytes < bytesPerSecond * 60 + 50 * 1000 * 1000)
            return juce::Result::fail (juce::String (u8"El disco está casi lleno (") + juce::File::descriptionOfSizeInBytes (freeBytes)
                                       + juce::String (u8" libres): libera espacio antes de grabar."));
        if (freeBytes > 0 && freeBytes < bytesPerSecond * 1800)
            lastError = juce::String (u8"Ojo: queda espacio para unos ") + juce::String ((int) (freeBytes / bytesPerSecond / 60))
                        + juce::String (u8" minutos de grabación.");
    }

    pushUndo();

    std::vector<AudioTake> newAudio;
    std::vector<MidiTake> newMidi;
    for (auto& t : song.tracks)
    {
        // Keyboards and the on-screen keys play the selected track, so that's the one MIDI is recorded on
        if (t->kind != TrackKind::Audio && t->kind != TrackKind::Bus && t.get() == selected)
        {
            MidiTake take;
            take.trackId = t->id;
            take.notes.reserve (8192);
            take.events.reserve (32768);
            newMidi.push_back (std::move (take));
            continue;
        }
        if (! t->armed || t->kind != TrackKind::Audio) continue;
        AudioTake take;
        take.trackId = t->id;
        take.channel = t->inputChannel;
        take.numChannels = t->stereoInput ? 2 : 1;
        take.file = folder.getNonexistentChildFile (juce::File::createLegalFileName (t->name) + " toma", ".wav", false);
        take.writer = std::make_unique<TakeWriter> (writerThread, take.file, sampleRate, take.numChannels);
        if (! take.writer->openedOk())
        {
            for (auto& made : newAudio) { made.writer->finish(); made.file.deleteFile(); }
            take.file.deleteFile();
            return juce::Result::fail ("No se puede escribir en " + take.file.getParentDirectory().getFullPathName());
        }
        newAudio.push_back (std::move (take));
    }

    deviceLostWhileRecording = false;
    const juce::ScopedLock sl (lock);
    audioTakes = std::move (newAudio);
    midiTakes = std::move (newMidi);
    if (position < 0) position = 0;
    // With the cycle on, recording goes round and round the section: every pass becomes a take
    cycleRecording = song.cycle && song.cycleEnd > song.cycleStart + 0.1;
    if (cycleRecording)
        position = (juce::int64) beatsToSamples (song.cycleStart);
    recordStartSample = position.load();
    if (song.countIn && ! playing)
        position = recordStartSample - (juce::int64) beatsToSamples (song.beatsPerBar());
    recording = true;
    playing = true;
    return juce::Result::ok();
}

std::vector<int> Engine::recordingTrackIds() const
{
    std::vector<int> ids;
    if (! recording.load()) return ids;
    const juce::ScopedLock sl (lock);
    for (auto& t : audioTakes) ids.push_back (t.trackId);
    for (auto& t : midiTakes) ids.push_back (t.trackId);
    return ids;
}

juce::int64 Engine::recordingDroppedSamples() const
{
    juce::int64 n = 0;
    for (auto& t : audioTakes) if (t.writer) n += t.writer->droppedSamples();
    return n;
}

bool Engine::recordingDiskFailed() const
{
    for (auto& t : audioTakes) if (t.writer && t.writer->diskFailed()) return true;
    return false;
}

juce::String Engine::checkRecordingHealth()
{
    if (! recording.load()) { deviceLostWhileRecording = false; return {}; }
    if (deviceLostWhileRecording.load())
    {
        stop();
        return juce::String (u8"Se cortó la interfaz de audio en medio de la grabación. Guardé todo lo grabado hasta ese momento.");
    }
    if (recordingDiskFailed())
    {
        stop();
        return juce::String (u8"El disco dejó de aceptar la grabación (¿se llenó?). Guardé lo que se pudo escribir.");
    }
    return {};
}

void Engine::finishRecording()
{
    const double stopBeat = getPositionBeats();
    const double startBeat = samplesToBeats ((double) recordStartSample);
    const int comp = getLatencyCompensationSamples();
    const bool wasCycle = cycleRecording;
    cycleRecording = false;

    std::vector<AudioTake> takes;
    std::vector<MidiTake> mtakes;
    {
        const juce::ScopedLock sl (lock);
        std::swap (takes, audioTakes);
        std::swap (mtakes, midiTakes);
    }

    juce::int64 droppedTotal = 0;
    bool diskFailed = false;
    for (auto& take : takes)
    {
        take.writer->finish();   // writes what's still buffered and closes the file
        droppedTotal += take.writer->droppedSamples();
        diskFailed |= take.writer->diskFailed();
        const auto written = take.writer->samplesWritten();
        take.writer.reset();
        audioCache.erase (take.file.getFileName());
        auto buffer = loadAudio (take.file.getFileName());
        // Only a recording that never really started is thrown away; anything else is kept,
        // even if something went wrong (a file we can't read now can be recovered later).
        if (written <= comp + 64 && (buffer == nullptr || buffer->getNumSamples() <= comp + 64))
        {
            take.file.deleteFile();
            continue;
        }
        if (buffer == nullptr)
        {
            lastError = juce::String (u8"La grabación quedó guardada en ") + take.file.getFullPathName()
                        + juce::String (u8" pero no se pudo abrir. Usa Archivo › Recuperar grabaciones.");
            continue;
        }
        auto* t = song.findTrack (take.trackId);
        if (t == nullptr)
        {
            // the track was deleted while recording: the take goes to a new one
            t = addTrack (TrackKind::Audio, juce::String (u8"Grabación recuperada"));
            t->inputChannel = take.channel;
            t->stereoInput = take.numChannels == 2;
        }

        Clip c;
        c.id = song.newId();
        c.midi = false;
        c.name = "Toma " + juce::String (t->clips.size() + 1);
        c.audioFile = take.file.getFileName();
        c.audio = buffer;
        c.offsetSamples = comp;      // latency compensation: the audio arrived late, so start later in the file
        c.start = startBeat;
        c.length = samplesToBeats ((double) (buffer->getNumSamples() - comp));
        c.sourceBpm = song.bpm;

        if (wasCycle)
        {
            // One file, several passes: each pass is a take of the same region
            const auto passLen = (juce::int64) beatsToSamples (song.cycleEnd - song.cycleStart);
            const auto usable = (juce::int64) buffer->getNumSamples() - comp;
            int passes = (int) (usable / passLen);
            if (usable - passes * passLen > passLen / 4) ++passes;   // keep a last pass if it got going
            if (passes >= 1)
            {
                c.start = song.cycleStart;
                c.length = song.cycleEnd - song.cycleStart;
                for (int k = 0; k < passes; ++k)
                    c.takes.push_back ({ c.audioFile, comp + k * passLen });
                // the last complete pass is usually the best one
                const int fullPasses = (int) (usable / passLen);
                c.activeTake = juce::jmax (0, fullPasses - 1);
                c.offsetSamples = c.takes[(size_t) c.activeTake].offsetSamples;
                if (passes > 1) c.name = juce::String (u8"Tomas (") + juce::String (passes) + ")";
            }
        }

        // Recording over something replaces what was underneath (like a tape)
        std::vector<Clip> kept;
        for (auto& old : t->clips)
        {
            if (old.midi || old.end() <= c.start || old.start >= c.end()) { kept.push_back (old); continue; }
            if (old.start < c.start)
            {
                Clip head = old;
                head.length = c.start - old.start;
                kept.push_back (head);
            }
            if (old.end() > c.end())
            {
                Clip tail = old;
                tail.id = song.newId();
                const double cut = c.end() - old.start;
                tail.start = c.end();
                tail.length = old.end() - c.end();
                tail.offsetSamples = old.offsetSamples + (juce::int64) beatsToSamples (cut);
                kept.push_back (tail);
            }
        }
        kept.push_back (c);
        const juce::ScopedLock sl (lock);
        t->clips = std::move (kept);
    }

    for (auto& take : mtakes)
    {
        auto* t = song.findTrack (take.trackId);
        if (t == nullptr) continue;
        for (int p = 0; p < 128; ++p)
            if (take.open[(size_t) p])
                take.notes.push_back ({ take.openBeat[(size_t) p], juce::jmax (0.05, stopBeat - take.openBeat[(size_t) p]), p, take.openVel[(size_t) p] });
        if (take.notes.empty() && take.events.empty()) continue;

        const double bpb = song.beatsPerBar();
        Clip c;
        c.id = song.newId();
        c.midi = true;
        c.name = juce::String (u8"Grabación");
        c.start = wasCycle ? song.cycleStart : std::floor (startBeat / bpb) * bpb;
        double end = c.start;
        for (auto& n : take.notes)
        {
            end = juce::jmax (end, n.beat + n.length);
            c.notes.push_back ({ juce::jmax (0.0, n.beat - c.start), n.length, n.pitch, n.velocity });
        }
        for (auto& e : take.events)
        {
            end = juce::jmax (end, e.beat);
            c.events.push_back ({ juce::jmax (0.0, e.beat - c.start), e.kind, e.number, e.value });
        }
        // keep the pedal from staying down at the end of the region
        c.length = wasCycle ? song.cycleEnd - song.cycleStart
                            : juce::jmax (bpb, std::ceil ((end - c.start) / bpb) * bpb);
        c.loopLength = c.length;
        std::sort (c.events.begin(), c.events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.beat < b.beat; });
        const juce::ScopedLock sl (lock);
        t->clips.push_back (std::move (c));
    }

    // the track we armed automatically goes back to normal; tracks the user armed stay armed
    if (auto* t = song.findTrack (autoArmedTrackId)) t->armed = false;
    autoArmedTrackId = 0;

    if (droppedTotal > 0)
        lastError = juce::String (u8"Atención: se perdieron ") + juce::String (droppedTotal / sampleRate, 1)
                    + juce::String (u8" s de la grabación porque el disco no daba abasto. Cierra otras apps o graba en un disco más rápido.");
    if (diskFailed)
        lastError = juce::String (u8"El disco tuvo un error al guardar la grabación. Revisa que tenga espacio.");

    dirty = true;
    saveSong();     // right away: a recording is the one thing that can't be redone
    if (onSongChanged) onSongChanged();
}

//==============================================================================
// Tracks
//==============================================================================
Track* Engine::addTrack (TrackKind kind, const juce::String& preset)
{
    auto t = std::make_unique<Track>();
    t->id = song.newId();
    t->kind = kind;
    t->colour = colourForKind (kind, (int) song.tracks.size());
    prepareTrack (*t);

    int sameKind = 1;
    for (auto& other : song.tracks) if (other->kind == kind) ++sameKind;

    switch (kind)
    {
        case TrackKind::Audio:
            t->name = preset.isNotEmpty() ? preset : "Audio " + juce::String (sameKind);
            break;
        case TrackKind::Drummer:
        {
            t->name = "Baterista IA";
            setInstrument (*t, "builtin", "drums", juce::String (u8"Batería acústica"));
            Clip c;
            c.id = song.newId();
            c.name = "Verso";
            c.start = 0.0;
            c.length = song.beatsPerBar() * 8.0;
            c.loopLength = c.length;
            c.seed = juce::Random::getSystemRandom().nextInt (100000);
            t->clips.push_back (c);
            regenerateDrummer (*t);
            break;
        }
        case TrackKind::Bus:
        {
            // A bus is 100 % effect: the dry sound already comes from the tracks themselves
            const auto fx = preset.isNotEmpty() ? preset : juce::String ("reverb");
            t->name = fx == "delay" ? "Delay" : fx == "reverb" ? "Reverb" : "Bus " + juce::String (sameKind);
            if (fx == "reverb" || fx == "delay" || fx == "chorus")
            {
                Slot s;
                s.kind = "builtin"; s.type = fx;
                s.processor = adoptProcessor (createBuiltin (fx));
                if (s.processor != nullptr)
                {
                    s.name = s.processor->getName();
                    if (auto* b = dynamic_cast<BuiltinProcessor*> (s.processor.get()))
                        b->setReal ("mix", 1.0f);
                    prepareProcessor (*s.processor);
                    t->inserts.push_back (std::move (s));
                }
            }
            break;
        }
        case TrackKind::Instrument:
        {
            const auto* p = findPreset (preset.isNotEmpty() ? preset : "Piano de cola");
            if (p == nullptr) p = findPreset ("Piano de cola");
            setInstrument (*t, "builtin", p->type, p->name);
            t->name = p->name;
            break;
        }
    }

    auto* raw = t.get();
    {
        const juce::ScopedLock sl (lock);
        song.tracks.push_back (std::move (t));
    }
    selectedTrackId = raw->id;
    dirty = true;
    return raw;
}

void Engine::deleteTrack (int trackId)
{
    std::unique_ptr<Track> removed;
    {
        const juce::ScopedLock sl (lock);
        const int i = song.indexOfTrack (trackId);
        if (i < 0) return;
        removed = std::move (song.tracks[(size_t) i]);
        song.tracks.erase (song.tracks.begin() + i);
        for (auto& t : song.tracks)
            if (t->drummer.followTrack == trackId) t->drummer.followTrack = 0;
        // sends and outputs pointing at a deleted bus are simply ignored (and come back with undo)
    }
    removed->needsAllNotesOff = true;
    graveyard[trackId] = std::move (removed);   // kept so it can come back with "undo"
    dirty = true;
}

void Engine::moveTrack (int trackId, int newIndex)
{
    const juce::ScopedLock sl (lock);
    const int i = song.indexOfTrack (trackId);
    if (i < 0) return;
    auto t = std::move (song.tracks[(size_t) i]);
    song.tracks.erase (song.tracks.begin() + i);
    newIndex = juce::jlimit (0, (int) song.tracks.size(), newIndex);
    song.tracks.insert (song.tracks.begin() + newIndex, std::move (t));
    dirty = true;
}

std::vector<Slot>* Engine::insertsFor (int trackId)
{
    if (trackId == 0) return &song.masterInserts;
    if (auto* t = song.findTrack (trackId)) return &t->inserts;
    return nullptr;
}

Slot* Engine::findSlot (int trackId, int slotIndex)
{
    if (slotIndex < 0)
    {
        auto* t = song.findTrack (trackId);
        return t != nullptr ? &t->instrument : nullptr;
    }
    auto* list = insertsFor (trackId);
    if (list == nullptr || slotIndex >= (int) list->size()) return nullptr;
    return &(*list)[(size_t) slotIndex];
}

juce::Result Engine::setInstrument (Track& t, const juce::String& kind, const juce::String& id, const juce::String& preset)
{
    if (kind == "builtin")
    {
        auto proc = createBuiltin (id);
        if (proc == nullptr) return juce::Result::fail ("Instrumento desconocido: " + id);
        if (auto* p = findPreset (preset)) applyPreset (*proc, *p);
        prepareProcessor (*proc);

        Slot old;
        {
            const juce::ScopedLock sl (lock);
            old = std::move (t.instrument);
            t.instrument = Slot {};
            t.instrument.kind = "builtin";
            t.instrument.type = id;
            t.instrument.preset = preset;
            t.instrument.name = preset.isNotEmpty() ? preset : proc->getName();
            t.instrument.processor = adoptProcessor (std::move (proc));
            t.needsAllNotesOff = true;
        }
        dirty = true;
        return juce::Result::ok();
    }

    auto desc = plugins.find (id);
    if (! desc) return juce::Result::fail (juce::String (u8"No encuentro ese plugin. Prueba escanear de nuevo."));
    const int trackId = t.id;
    plugins.createInstance (*desc, sampleRate, blockSize,
        [this, trackId, d = *desc] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& error)
        {
            auto* track = song.findTrack (trackId);
            if (inst == nullptr || track == nullptr)
            {
                if (onStatus) onStatus ("No se pudo abrir " + d.name + ": " + error);
                return;
            }
            prepareProcessor (*inst);
            Slot old;
            {
                const juce::ScopedLock sl (lock);
                old = std::move (track->instrument);
                track->instrument = Slot {};
                track->instrument.kind = "plugin";
                track->instrument.type = d.createIdentifierString();
                track->instrument.name = d.name;
                track->instrument.description = d;
                track->instrument.processor = adoptProcessor (std::move (inst));
            }
            dirty = true;
            if (onSongChanged) onSongChanged();
        });
    return juce::Result::ok();
}

juce::Result Engine::addInsert (int trackId, const juce::String& kind, const juce::String& id, int index)
{
    if (insertsFor (trackId) == nullptr) return juce::Result::fail ("Pista inexistente");

    auto install = [this, trackId, index] (Slot slot)
    {
        Slot discard;
        const juce::ScopedLock sl (lock);
        auto* list = insertsFor (trackId);
        if (list == nullptr) return;
        const int at = index < 0 ? (int) list->size() : juce::jlimit (0, (int) list->size(), index);
        list->insert (list->begin() + at, std::move (slot));
        remapFxAutomation (trackId, [at] (int old) { return old >= at ? old + 1 : old; });
        dirty = true;
    };

    if (kind == "builtin")
    {
        auto proc = createBuiltin (id);
        if (proc == nullptr) return juce::Result::fail ("Efecto desconocido: " + id);
        prepareProcessor (*proc);
        Slot s;
        s.kind = "builtin"; s.type = id; s.name = proc->getName();
        s.processor = adoptProcessor (std::move (proc));
        install (std::move (s));
        return juce::Result::ok();
    }

    auto desc = plugins.find (id);
    if (! desc) return juce::Result::fail (juce::String (u8"No encuentro ese plugin. Prueba escanear de nuevo."));
    plugins.createInstance (*desc, sampleRate, blockSize,
        [this, install, d = *desc] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& error)
        {
            if (inst == nullptr) { if (onStatus) onStatus ("No se pudo abrir " + d.name + ": " + error); return; }
            prepareProcessor (*inst);
            Slot s;
            s.kind = "plugin"; s.type = d.createIdentifierString(); s.name = d.name; s.description = d;
            s.processor = adoptProcessor (std::move (inst));
            install (std::move (s));
            if (onSongChanged) onSongChanged();
        });
    return juce::Result::ok();
}

void Engine::removeInsert (int trackId, int index)
{
    Slot removed;
    {
        const juce::ScopedLock sl (lock);
        auto* list = insertsFor (trackId);
        if (list == nullptr || index < 0 || index >= (int) list->size()) return;
        removed = std::move ((*list)[(size_t) index]);
        list->erase (list->begin() + index);
        remapFxAutomation (trackId, [index] (int old) { return old == index ? -1 : (old > index ? old - 1 : old); });
    }
    dirty = true;
}

void Engine::moveInsert (int trackId, int from, int to)
{
    const juce::ScopedLock sl (lock);
    auto* list = insertsFor (trackId);
    if (list == nullptr || from < 0 || from >= (int) list->size()) return;
    auto s = std::move ((*list)[(size_t) from]);
    list->erase (list->begin() + from);
    to = juce::jlimit (0, (int) list->size(), to);
    list->insert (list->begin() + to, std::move (s));
    remapFxAutomation (trackId, [from, to] (int old)
    {
        if (old == from) return to;
        if (from < to && old > from && old <= to) return old - 1;
        if (from > to && old >= to && old < from) return old + 1;
        return old;
    });
    dirty = true;
}

void Engine::remapFxAutomation (int trackId, const std::function<int (int)>& newSlot)
{
    auto* t = song.findTrack (trackId);
    if (t == nullptr) return;
    for (auto it = t->automation.begin(); it != t->automation.end();)
    {
        if (it->kind == AutomationLane::FxParam)
        {
            const int n = newSlot (it->a);
            if (n < 0) { it = t->automation.erase (it); continue; }     // its effect was removed
            it->a = n;
            it->target = "fx:" + juce::String (n) + ":" + juce::String (it->b);
            it->lastApplied = -1.0e9f;
        }
        ++it;
    }
}

void Engine::writeAutomation (Track& t, const juce::String& target, float value)
{
    // Called while playing with "write" on: records the movement, replacing what was there
    const double beat = getPositionBeats();
    auto* lane = t.findLane (target);
    const juce::ScopedLock sl (lock);
    if (lane == nullptr)
    {
        AutomationLane l;
        l.target = target;
        l.parseTarget();
        t.automation.push_back (std::move (l));
        lane = &t.automation.back();
    }
    auto& w = automationWriteState[t.id * 1000 + (int) (target.hashCode() & 0x3ff)];
    const bool continuing = w.active && beat >= w.lastBeat && beat - w.lastBeat < 1.0;
    const double from = continuing ? w.lastBeat : beat;
    auto& pts = lane->points;
    pts.erase (std::remove_if (pts.begin(), pts.end(), [&] (const AutoPoint& p) { return p.beat > from + 1.0e-9 && p.beat <= beat + 1.0e-9; }), pts.end());
    if (! continuing && ! pts.empty())
        pts.push_back ({ juce::jmax (0.0, beat - 1.0e-3), lane->valueAt (beat) });   // keep what was before the touch
    pts.push_back ({ beat, value });
    std::stable_sort (pts.begin(), pts.end(), [] (const AutoPoint& x, const AutoPoint& y) { return x.beat < y.beat; });
    lane->on = true;
    w = { true, beat };
    dirty = true;
}

static bool isDrumTrack (const Track& t);

Track* Engine::drummerFollowTrack (const Track& drummer) const
{
    const int id = drummer.drummer.followTrack;
    if (id > 0) return song.findTrack (id);
    if (id == 0) return nullptr;
    // Automatic: the recorded guitar, else the bass, else any recorded track, else a bass made of notes
    auto hasAudio = [] (const Track& t) { return std::any_of (t.clips.begin(), t.clips.end(), [] (const Clip& c) { return ! c.midi && c.audio != nullptr; }); };
    auto named = [] (const Track& t, std::initializer_list<const char*> words)
    {
        const auto n = t.name.toLowerCase() + " " + t.instrument.type.toLowerCase();
        for (auto* w : words) if (n.contains (w)) return true;
        return false;
    };
    Track* best = nullptr;
    int bestScore = 0;
    for (auto& tp : song.tracks)
    {
        auto& t = *tp;
        if (&t == &drummer || t.kind == TrackKind::Drummer || t.kind == TrackKind::Bus || isDrumTrack (t)) continue;
        int score = 0;
        if (t.kind == TrackKind::Audio && hasAudio (t))
            score = named (t, { "guit", "gtr" }) ? 50 : named (t, { "bajo", "bass" }) ? 40 : named (t, { "voz", "vocal", "voice" }) ? 5 : 20;
        else if (t.kind == TrackKind::Instrument && ! t.clips.empty() && named (t, { "bajo", "bass" }))
            score = 10;
        if (score > bestScore) { bestScore = score; best = &t; }
    }
    return best;
}

void Engine::regenerateDrummer (Track& t, int onlyClipId)
{
    std::vector<std::pair<int, std::vector<Note>>> results;
    const double bpb = song.beatsPerBar();
    auto* follow = drummerFollowTrack (t);

    for (auto& c : t.clips)
    {
        if (! c.midi || c.locked || (onlyClipId != 0 && c.id != onlyClipId)) continue;

        DrumRequest r;
        r.settings = t.drummer;
        r.lengthBeats = c.length;
        r.beatsPerBar = bpb;
        r.seed = c.seed + t.id * 13;
        r.fillAtEnd = c.fill;

        // The parts of the song this region crosses, bar by bar
        if (! song.sections.empty())
        {
            const int bars = juce::jmax (1, (int) std::ceil (c.length / bpb - 1.0e-6));
            auto partAt = [&] (double beat) -> int
            {
                for (int i = 0; i < (int) song.sections.size(); ++i)
                    if (beat >= song.sections[(size_t) i].beat - 1.0e-6 && beat < song.sections[(size_t) i].beat + song.sections[(size_t) i].length - 1.0e-6) return i;
                return -1;
            };
            for (int b = 0; b < bars; ++b)
            {
                const double at = c.start + b * bpb + 0.01;
                const int i = partAt (at), next = partAt (at + bpb), prev = partAt (at - bpb);
                DrumBar db;
                db.kind = i >= 0 ? sectionKindFromId (song.sections[(size_t) i].kind) : SectionKind::none;
                db.sectionStart = i >= 0 && i != prev;
                db.sectionEnd = i >= 0 && next != i;
                db.ending = db.kind == SectionKind::outro && next != i
                         && song.sections[(size_t) i].beat + song.sections[(size_t) i].length <= c.start + (b + 1) * bpb + 0.01;
                r.bars.push_back (db);
            }
        }

        if (follow != nullptr)
        {
            if (follow->kind == TrackKind::Audio)
            {
                // Listen to the recording under this region
                const int n = (int) beatsToSamples (c.length);
                std::vector<float> mono ((size_t) juce::jmax (0, n), 0.0f);
                bool any = false;
                for (auto& fc : follow->clips)
                {
                    if (fc.midi || fc.audio == nullptr || fc.end() <= c.start || fc.start >= c.end()) continue;
                    const auto& buf = *fc.audio;
                    const juce::int64 clipAt = (juce::int64) beatsToSamples (fc.start - c.start);   // where the clip starts, from the region start
                    const juce::int64 clipLen = (juce::int64) beatsToSamples (fc.length);
                    const float g = juce::Decibels::decibelsToGain (fc.gainDb) / (float) juce::jmax (1, buf.getNumChannels());
                    for (juce::int64 i = std::max<juce::int64> (0, clipAt); i < std::min<juce::int64> (n, clipAt + clipLen); ++i)
                    {
                        const juce::int64 src = i - clipAt + std::max<juce::int64> (0, fc.offsetSamples);
                        if (src < 0 || src >= buf.getNumSamples()) continue;
                        float v = 0.0f;
                        for (int ch = 0; ch < buf.getNumChannels(); ++ch) v += buf.getSample (ch, (int) src);
                        mono[(size_t) i] += v * g;
                        any = true;
                    }
                }
                if (any)
                    analyseFollowAudio (mono.data(), n, sampleRate, beatsToSamples (1.0), bpb, r.followSteps, r.followBarEnergy);
            }
            else
            {
                // Notes: where they start, and how hard
                r.followSteps.assign ((size_t) std::ceil (c.length * 4.0) + 1, 0.0f);
                for (auto& fc : follow->clips)
                {
                    if (! fc.midi) continue;
                    const double L = juce::jmax (0.25, juce::jmin (fc.loopLength, fc.length));
                    for (double rep = 0; rep < fc.length; rep += L)
                        for (auto& note : fc.notes)
                        {
                            if (note.beat >= L || rep + note.beat >= fc.length) continue;
                            const double b = fc.start + rep + note.beat - c.start;
                            const long k = std::lround (b * 4.0);
                            if (k >= 0 && k < (long) r.followSteps.size())
                                r.followSteps[(size_t) k] = juce::jmax (r.followSteps[(size_t) k], 0.75f + 0.25f * note.velocity);
                        }
                }
            }
        }
        results.emplace_back (c.id, generateDrumPart (r));
    }

    const juce::ScopedLock sl (lock);
    for (auto& [id, notes] : results)
        if (auto* c = t.findClip (id))
        {
            c->notes = std::move (notes);
            c->loopLength = c->length;
        }
    t.needsAllNotesOff = true;
    dirty = true;
}

void Engine::regenerateAllDrummers()
{
    for (auto& tp : song.tracks)
        if (tp->kind == TrackKind::Drummer) regenerateDrummer (*tp);
}

Clip& Engine::addClip (Track& t, Clip c)
{
    if (c.id == 0) c.id = song.newId();
    const juce::ScopedLock sl (lock);
    t.clips.push_back (std::move (c));
    dirty = true;
    return t.clips.back();
}

juce::Result Engine::importAudio (int trackId, const juce::File& file, double atBeat)
{
    if (! file.existsAsFile()) return juce::Result::fail ("No encuentro el archivo");
    std::unique_ptr<juce::AudioFormatReader> probe (audioFormats.createReaderFor (file));
    if (probe == nullptr) return juce::Result::fail ("Ese formato de audio no se puede abrir");
    probe.reset();

    getAudioFolder().createDirectory();
    auto dest = getAudioFolder().getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);
    if (! file.copyFileTo (dest)) return juce::Result::fail (juce::String (u8"No se pudo copiar el archivo a la canción"));

    auto buffer = loadAudio (dest.getFileName());
    if (buffer == nullptr) return juce::Result::fail ("No se pudo leer el audio");

    pushUndo();
    auto* t = song.findTrack (trackId);
    if (t == nullptr || t->kind != TrackKind::Audio)
        t = addTrack (TrackKind::Audio, file.getFileNameWithoutExtension());

    Clip c;
    c.midi = false;
    c.name = file.getFileNameWithoutExtension();
    c.audioFile = dest.getFileName();
    c.audio = buffer;
    c.start = juce::jmax (0.0, atBeat);
    c.length = samplesToBeats ((double) buffer->getNumSamples());
    c.sourceBpm = song.bpm;
    addClip (*t, std::move (c));
    return juce::Result::ok();
}

juce::Result Engine::importMidiFile (const juce::File& file)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) return juce::Result::fail ("No se pudo leer el archivo MIDI");
    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) return juce::Result::fail (juce::String (u8"Archivo MIDI con formato de tiempo SMPTE (no soportado todavía)"));

    pushUndo();
    if (song.tracks.empty())
    {
        juce::MidiMessageSequence tempos;
        mf.findAllTempoEvents (tempos);
        if (tempos.getNumEvents() > 0)
            song.bpm = juce::jlimit (20.0, 400.0, 60.0 / tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote());
    }

    int created = 0;
    for (int i = 0; i < mf.getNumTracks(); ++i)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (i));
        seq.updateMatchedPairs();
        Clip c;
        c.midi = true;
        bool drums = false;
        double end = 0;
        for (auto* e : seq)
        {
            if (! e->message.isNoteOn()) continue;
            const double on = e->message.getTimeStamp() / tpq;
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : on + 0.25;
            drums |= e->message.getChannel() == 10;
            c.notes.push_back ({ on, juce::jmax (0.02, off - on), e->message.getNoteNumber(), e->message.getFloatVelocity() });
            end = juce::jmax (end, off);
        }
        if (c.notes.empty()) continue;

        auto* t = addTrack (TrackKind::Instrument, drums ? juce::String (u8"Batería acústica") : "Piano de cola");
        c.name = file.getFileNameWithoutExtension();
        c.start = 0;
        const double bpb = song.beatsPerBar();
        c.length = juce::jmax (bpb, std::ceil (end / bpb) * bpb);
        c.loopLength = c.length;
        addClip (*t, std::move (c));
        ++created;
    }
    return created > 0 ? juce::Result::ok() : juce::Result::fail ("El archivo MIDI no tiene notas");
}

juce::Result Engine::addLoop (const juce::String& loopId, double atBeat, int trackId)
{
    const auto* loop = findLoop (loopId);
    if (loop == nullptr) return juce::Result::fail ("Loop desconocido");
    pushUndo();

    auto* t = song.findTrack (trackId);
    if (t == nullptr || t->kind == TrackKind::Audio || t->kind == TrackKind::Bus)
        t = addTrack (TrackKind::Instrument, loop->preset);

    Clip c;
    c.midi = true;
    c.name = loop->name;
    c.start = juce::jmax (0.0, atBeat);
    c.notes = makeLoopNotes (*loop, keyRootFromName (song.key), song.minor, song.beatsPerBar());
    c.length = loop->lengthBeats;
    c.loopLength = loop->lengthBeats;
    addClip (*t, std::move (c));
    return juce::Result::ok();
}

void Engine::liveNote (int pitch, float velocity, bool on)
{
    auto m = on ? juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, pitch), juce::jlimit (0.01f, 1.0f, velocity))
                : juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, pitch));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    liveMidi.addMessageToQueue (m);
}

void Engine::liveMessage (const juce::MidiMessage& m)
{
    auto copy = m;
    copy.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    liveMidi.addMessageToQueue (copy);
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isActiveSense() || m.isMidiClock()) return;
    liveMidi.addMessageToQueue (m);
}

//==============================================================================
// Offline
//==============================================================================
void Engine::renderOffline (juce::AudioBuffer<float>& out, int numSamples)
{
    const juce::ScopedLock sl (lock);
    out.setSize (2, numSamples, false, false, true);
    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (blockSize, numSamples - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        renderLocked (ptrs, 2, nullptr, 0, n);
        done += n;
    }
}



//==============================================================================
// Lost recordings (after a crash, a power cut, or an undone take)
//==============================================================================
juce::StringArray Engine::findLostRecordings()
{
    juce::StringArray used;
    for (auto& t : song.tracks)
        for (auto& c : t->clips)
        {
            used.add (c.audioFile);
            for (auto& tk : c.takes) used.add (tk.audioFile);
            if (c.flex.on) used.add (c.flex.sourceFile);
        }
    for (auto& [id, t] : graveyard)
        for (auto& c : t->clips) used.add (c.audioFile);
    // anything the undo history still points at isn't lost either
    for (auto& snap : undoStack)
        if (auto* tracks = snap["tracks"].getArray())
            for (auto& tv : *tracks)
                if (auto* clips = tv["clips"].getArray())
                    for (auto& cv : *clips) used.add (cv["audioFile"].toString());

    juce::StringArray lost;
    for (auto& f : getAudioFolder().findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        const auto name = f.getFileName();
        if (! name.contains (" toma")) continue;               // only raw recordings
        if (used.contains (name) || ignoredRecordings.contains (name)) continue;
        if (f.getSize() < 4096) continue;                       // empty
        lost.add (name);
    }
    return lost;
}

int Engine::recoverLostRecordings (const juce::StringArray& files)
{
    int recovered = 0;
    for (auto& name : files)
    {
        auto file = getAudioFolder().getChildFile (name);
        if (! file.existsAsFile()) continue;
        TakeWriter::repairWav (file);
        audioCache.erase (name);
        auto buffer = loadAudio (name);
        if (buffer == nullptr || buffer->getNumSamples() == 0) continue;
        auto* t = addTrack (TrackKind::Audio, juce::String (u8"Recuperada: ") + file.getFileNameWithoutExtension());
        Clip c;
        c.midi = false;
        c.name = file.getFileNameWithoutExtension();
        c.audioFile = name;
        c.audio = buffer;
        c.start = 0;
        c.length = samplesToBeats ((double) buffer->getNumSamples());
        c.sourceBpm = song.bpm;
        addClip (*t, std::move (c));
        ++recovered;
    }
    dirty = true;
    return recovered;
}

//==============================================================================
// Chord ID
//==============================================================================
static bool isDrumTrack (const Track& t)
{
    return t.kind == TrackKind::Drummer || (t.instrument.kind == "builtin" && t.instrument.type == "drums");
}

juce::Result Engine::detectChords (int trackId, int clipId)
{
    std::vector<analysis::BeatChroma> chroma;
    double from = 0.0, to = 0.0;
    const double spb = beatsToSamples (1.0);

    auto fromAudio = [&] (const juce::AudioBuffer<float>& buf, juce::int64 offset, int n)
    {
        std::vector<float> mono ((size_t) juce::jmax (0, n), 0.0f);
        const int avail = (int) std::clamp<juce::int64> ((juce::int64) buf.getNumSamples() - offset, 0, n);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            juce::FloatVectorOperations::addWithMultiply (mono.data(), buf.getReadPointer (ch, (int) offset), 1.0f / buf.getNumChannels(), avail);
        chroma = analysis::chromaFromAudio (mono.data(), n, sampleRate, spb);
    };

    if (clipId != 0)
    {
        auto* t = song.findTrack (trackId);
        auto* c = t != nullptr ? t->findClip (clipId) : nullptr;
        if (c == nullptr) return juce::Result::fail ("No encuentro esa región");
        if (isDrumTrack (*t)) return juce::Result::fail (juce::String (u8"Es una batería: no tiene acordes"));
        from = c->start; to = c->end();
        if (c->midi)
        {
            std::vector<Note> notes;
            const double L = juce::jmax (0.25, juce::jmin (c->loopLength, c->length));
            for (int rep = 0; rep * L < c->length; ++rep)
                for (auto n : c->notes)
                {
                    if (n.beat >= L) continue;
                    n.beat += rep * L + c->start;
                    if (n.beat >= to) continue;
                    n.length = juce::jmin (n.length, to - n.beat);
                    notes.push_back (n);
                }
            chroma = analysis::chromaFromNotes (notes, from, to);
        }
        else
        {
            if (c->audio == nullptr) return juce::Result::fail ("La región no tiene audio cargado");
            fromAudio (*c->audio, std::max<juce::int64> (0, c->offsetSamples), (int) beatsToSamples (c->length));
        }
    }
    else
    {
        to = song.lengthInBeats();
        if (to <= 0.0) return juce::Result::fail (juce::String (u8"La canción está vacía"));
        auto mix = renderRange (0.0, to, [] (const Track& t) { return ! isDrumTrack (t); });
        fromAudio (mix, 0, mix.getNumSamples());
    }

    auto found = analysis::decodeChords (chroma, from, (int) std::round (song.beatsPerBar()));
    if (found.empty()) return juce::Result::fail (juce::String (u8"No encontré acordes (¿solo hay batería o silencio?)"));
    for (auto& c : found) c.length = juce::jmin (c.length, to - c.beat);

    const juce::ScopedLock sl (lock);
    std::vector<ChordSpan> kept;
    for (auto& c : song.chords)
    {
        if (c.beat + c.length <= from || c.beat >= to) { kept.push_back (c); continue; }
        if (c.beat < from) { auto a = c; a.length = from - c.beat; kept.push_back (a); }        // part before
        if (c.beat + c.length > to) { auto b = c; b.length = c.beat + c.length - to; b.beat = to; kept.push_back (b); }   // part after
    }
    kept.insert (kept.end(), found.begin(), found.end());
    std::sort (kept.begin(), kept.end(), [] (const ChordSpan& a, const ChordSpan& b) { return a.beat < b.beat; });
    song.chords = std::move (kept);
    dirty = true;
    return juce::Result::ok();
}

Track* Engine::chordsToTrack()
{
    if (song.chords.empty()) return nullptr;
    auto* t = addTrack (TrackKind::Instrument, juce::String (u8"Piano eléctrico"));
    t->name = "Acordes";
    Clip c;
    c.name = "Acordes";
    c.start = std::floor (song.chords.front().beat / song.beatsPerBar()) * song.beatsPerBar();
    const double end = song.chords.back().beat + song.chords.back().length;
    c.length = juce::jmax (song.beatsPerBar(), end - c.start);
    c.loopLength = c.length;
    for (auto& ch : song.chords)
        for (int p : analysis::chordPitches (ch.root, ch.quality, 60))
            c.notes.push_back ({ ch.beat - c.start, juce::jmax (0.25, ch.length - 0.05), p, 0.7f });
    addClip (*t, std::move (c));
    return t;
}

//==============================================================================
// Flex (time and pitch editing of audio regions)
//==============================================================================
static FlexSettings startFlex (const Clip& c, double songBpm)
{
    FlexSettings f = c.flex;
    if (! f.on)
    {
        f.sourceFile = c.audioFile;
        f.sourceOffset = c.offsetSamples;
        f.sourceBpm = c.sourceBpm > 0 ? c.sourceBpm : songBpm;
        f.sourceBeats = c.length;
        f.noteShifts.clear();
    }
    return f;
}

std::shared_ptr<Engine::FlexSource> Engine::flexSourceFor (const FlexSettings& f, bool needPitch, bool needOnsets)
{
    const auto srcLen = (int) std::llround (f.sourceBeats * 60.0 / f.sourceBpm * sampleRate);
    const juce::String key = f.sourceFile + "|" + juce::String (f.sourceOffset) + "|" + juce::String (srcLen);
    auto& entry = flexCache[key];
    if (entry == nullptr)
    {
        auto file = loadAudio (f.sourceFile);
        if (file == nullptr || srcLen <= 0) { flexCache.erase (key); return nullptr; }
        entry = std::make_shared<FlexSource>();
        const int ch = file->getNumChannels();
        entry->audio.setSize (ch, srcLen);
        entry->audio.clear();
        const int avail = (int) std::clamp<juce::int64> ((juce::int64) file->getNumSamples() - f.sourceOffset, 0, srcLen);
        for (int c = 0; c < ch; ++c)
            if (avail > 0) entry->audio.copyFrom (c, 0, *file, c, (int) f.sourceOffset, avail);
        entry->mono.assign ((size_t) srcLen, 0.0f);
        for (int c = 0; c < ch; ++c)
            juce::FloatVectorOperations::addWithMultiply (entry->mono.data(), entry->audio.getReadPointer (c), 1.0f / ch, srcLen);
    }
    if (needPitch && ! entry->pitchDone)
    {
        entry->frames = flex::detectPitch (entry->mono.data(), (int) entry->mono.size(), sampleRate);
        entry->notes = flex::segmentNotes (entry->frames);
        entry->pitchDone = true;
    }
    if (needOnsets && ! entry->onsetsDone)
    {
        entry->onsets = flex::detectOnsets (entry->mono.data(), (int) entry->mono.size(), sampleRate);
        entry->onsetsDone = true;
    }
    return entry;
}

Engine::FlexPlan Engine::planFlex (const Clip& c, const FlexSettings& f, FlexSource& src)
{
    FlexPlan plan;
    plan.srcLen = src.audio.getNumSamples();
    const double ratio = f.followTempo ? f.sourceBpm / song.bpm : 1.0;
    plan.outLen = (int) std::llround (plan.srcLen * ratio);

    // Time: the whole region scales with the tempo; each hit moves towards the grid
    plan.map.push_back ({ 0.0, 0.0 });
    if (f.quantizeGrid > 0 && src.onsetsDone)
    {
        const double minGap = 0.015 * sampleRate;
        for (double t : src.onsets)
        {
            const double s = t * sampleRate;
            const double o = s * ratio;
            const double beat = c.start + samplesToBeats (o);
            const double target = std::round (beat / f.quantizeGrid) * f.quantizeGrid;
            const double oTarget = beatsToSamples (target - c.start);
            const double oNew = o + f.quantizeStrength * (oTarget - o);
            if (s <= plan.map.back().first + minGap || oNew <= plan.map.back().second + minGap) continue;
            if (oNew >= plan.outLen - minGap || s >= plan.srcLen - minGap) continue;
            // don't squeeze or stretch a piece more than 2x (it would sound bad)
            const double ds = s - plan.map.back().first, dout = oNew - plan.map.back().second;
            if (dout / ds > 2.0 || dout / ds < 0.5) continue;
            plan.map.push_back ({ s, oNew });
        }
    }
    plan.map.push_back ({ (double) plan.srcLen, (double) plan.outLen });

    // Pitch: per note, towards the nearest note of the key (keeps vibrato and slides inside notes)
    if (src.pitchDone)
    {
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11 }, minor[] = { 0, 2, 3, 5, 7, 8, 10 };
        static const juce::StringArray keys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int root = juce::jmax (0, keys.indexOf (song.key));
        auto allowed = [&] (int pc)
        {
            if (f.tuneChromatic) return true;
            for (int i = 0; i < 7; ++i) if ((root + (song.minor ? minor[i] : major[i])) % 12 == pc) return true;
            return false;
        };
        for (auto& n : src.notes)
        {
            float corr = 0;
            if (f.tuneStrength > 0)
            {
                int best = (int) std::round (n.pitch);
                for (int d = 0; d <= 6; ++d)
                {
                    const int up = (int) std::round (n.pitch) + d, down = (int) std::round (n.pitch) - d;
                    const bool upOk = allowed (((up % 12) + 12) % 12), downOk = allowed (((down % 12) + 12) % 12);
                    if (upOk && downOk) { best = std::abs (up - n.pitch) < std::abs (down - n.pitch) ? up : down; break; }
                    if (upOk) { best = up; break; }
                    if (downOk) { best = down; break; }
                }
                corr += (best - n.pitch) * f.tuneStrength;
            }
            for (auto& [t, semis] : f.noteShifts)
                if (std::abs (t - n.start) < 0.04) corr += semis;
            plan.noteCorrection.push_back (corr);
            if (std::abs (corr) > 0.005f)
                plan.pitch.push_back ({ n.start * sampleRate, (n.start + n.length) * sampleRate, corr });
        }
    }
    return plan;
}

juce::var Engine::flexAnalyze (int trackId, int clipId)
{
    auto* t = song.findTrack (trackId);
    auto* c = t != nullptr ? t->findClip (clipId) : nullptr;
    if (c == nullptr || c->midi) return {};
    auto f = startFlex (*c, song.bpm);
    auto src = flexSourceFor (f, true, f.quantizeGrid > 0);
    if (src == nullptr) return {};
    auto plan = planFlex (*c, f, *src);

    auto* o = new juce::DynamicObject();
    auto toBeat = [&] (double sourceSeconds) { return samplesToBeats (flex::mapForward (plan.map, sourceSeconds * sampleRate)); };
    juce::Array<juce::var> notes, curve, hits;
    for (size_t i = 0; i < src->notes.size(); ++i)
    {
        auto& n = src->notes[i];
        auto* no = new juce::DynamicObject();
        no->setProperty ("key", n.start);                 // identifies the note for "noteShifts"
        no->setProperty ("start", toBeat (n.start));
        no->setProperty ("length", toBeat (n.start + n.length) - toBeat (n.start));
        no->setProperty ("pitch", n.pitch);
        no->setProperty ("correction", i < plan.noteCorrection.size() ? plan.noteCorrection[i] : 0.0f);
        notes.add (juce::var (no));
    }
    for (size_t i = 0; i < src->frames.size(); i += 2)
        curve.add (juce::Array<juce::var> { toBeat (src->frames[i].time), src->frames[i].midi < 0 ? juce::var() : juce::var (src->frames[i].midi) });
    for (double h : src->onsets) hits.add (toBeat (h));
    o->setProperty ("notes", notes);
    o->setProperty ("curve", curve);
    o->setProperty ("hits", hits);
    o->setProperty ("flex", flexToVar (f));
    return juce::var (o);
}

juce::Result Engine::flexApply (int trackId, int clipId, const juce::var& settings)
{
    auto* t = song.findTrack (trackId);
    auto* c = t != nullptr ? t->findClip (clipId) : nullptr;
    if (c == nullptr || c->midi) return juce::Result::fail ("Flex es para regiones de audio");
    auto f = startFlex (*c, song.bpm);
    flexFromVar (f, settings);
    if (f.sourceBpm <= 0) f.sourceBpm = song.bpm;

    const bool needPitch = f.tuneStrength > 0 || ! f.noteShifts.empty();
    auto src = flexSourceFor (f, needPitch, f.quantizeGrid > 0);
    if (src == nullptr) return juce::Result::fail (juce::String (u8"No encuentro la grabación original de esta región"));
    auto plan = planFlex (*c, f, *src);

    // pitch curve with short ramps between notes so nothing clicks
    const double ramp = 0.015 * sampleRate;
    auto semis = [&plan, ramp] (double s) -> float
    {
        for (auto& seg : plan.pitch)
        {
            if (s < seg.start - ramp || s > seg.end + ramp) continue;
            float w = 1.0f;
            if (s < seg.start) w = (float) ((s - (seg.start - ramp)) / ramp);
            else if (s > seg.end) w = (float) (1.0 - (s - seg.end) / ramp);
            return seg.semitones * juce::jlimit (0.0f, 1.0f, w);
        }
        return 0.0f;
    };
    auto rendered = flex::render (src->audio, sampleRate, plan.map, plan.outLen, semis);

    // a new file next to the original (never overwritten, so undo works)
    auto dest = getAudioFolder().getNonexistentChildFile (juce::File::createLegalFileName (c->name) + " flex", ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (dest);
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                         .withNumChannels (rendered.getNumChannels()).withBitsPerSample (24));
        if (w == nullptr) return juce::Result::fail ("No se pudo escribir " + dest.getFullPathName());
        w->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
    }
    auto audio = loadAudio (dest.getFileName());

    const juce::ScopedLock sl (lock);
    f.on = true;
    c->flex = f;
    c->audioFile = dest.getFileName();
    c->audio = audio;
    c->offsetSamples = 0;
    c->length = samplesToBeats ((double) plan.outLen);
    if (! c->takes.empty() && c->activeTake < (int) c->takes.size())
        c->takes[(size_t) c->activeTake] = { c->audioFile, 0 };
    dirty = true;
    return juce::Result::ok();
}

juce::Result Engine::flexReset (int trackId, int clipId)
{
    auto* t = song.findTrack (trackId);
    auto* c = t != nullptr ? t->findClip (clipId) : nullptr;
    if (c == nullptr || ! c->flex.on) return juce::Result::fail (juce::String (u8"Esta región no tiene Flex"));
    auto audio = loadAudio (c->flex.sourceFile);
    const juce::ScopedLock sl (lock);
    c->audioFile = c->flex.sourceFile;
    c->offsetSamples = c->flex.sourceOffset;
    c->audio = audio;
    c->length = c->flex.sourceBeats * song.bpm / c->flex.sourceBpm;   // same seconds as recorded
    if (! c->takes.empty() && c->activeTake < (int) c->takes.size())
        c->takes[(size_t) c->activeTake] = { c->audioFile, c->offsetSamples };
    c->flex = FlexSettings();
    dirty = true;
    return juce::Result::ok();
}

//==============================================================================
// Offline rendering helpers (export, analysis)
//==============================================================================
juce::int64 Engine::beginOffline (double startBeat)
{
    stop();
    const auto savedPos = position.load();
    offlineBusy = true;
    const juce::ScopedLock sl (lock);
    offline = true;
    for (auto& t : song.tracks)
    {
        if (t->instrument.processor) { t->instrument.processor->setNonRealtime (true); t->instrument.processor->reset(); }
        for (auto& s : t->inserts) if (s.processor) { s.processor->setNonRealtime (true); s.processor->reset(); }
        t->needsAllNotesOff = true;
    }
    for (auto& s : song.masterInserts) if (s.processor) { s.processor->setNonRealtime (true); s.processor->reset(); }
    for (auto& t : song.tracks) { t->delayLine.clear(); t->delayWrite = 0; }
    limiter.reset();
    position = (juce::int64) beatsToSamples (startBeat);
    playing = true;
    return savedPos;
}

void Engine::endOffline (juce::int64 savedPos)
{
    {
        const juce::ScopedLock sl (lock);
        playing = false;
        offline = false;
        position = savedPos;
        for (auto& t : song.tracks)
        {
            if (t->instrument.processor) t->instrument.processor->setNonRealtime (false);
            for (auto& s : t->inserts) if (s.processor) s.processor->setNonRealtime (false);
            t->needsAllNotesOff = true;
        }
        for (auto& s : song.masterInserts) if (s.processor) s.processor->setNonRealtime (false);
    }
    offlineBusy = false;
}

juce::AudioBuffer<float> Engine::renderRange (double startBeat, double endBeat, const std::function<bool (const Track&)>& include)
{
    // Leave out what isn't wanted by muting it for the render (and putting the mutes back after)
    std::vector<std::pair<Track*, bool>> savedMutes;
    std::vector<std::pair<Track*, bool>> savedSolos;
    {
        const juce::ScopedLock sl (lock);
        for (auto& t : song.tracks)
        {
            savedMutes.emplace_back (t.get(), t->mute);
            savedSolos.emplace_back (t.get(), t->solo);
            t->solo = false;
            if (include && t->kind != TrackKind::Bus && ! include (*t)) t->mute = true;
        }
    }
    const bool lim = song.limiter;
    song.limiter = false;
    const auto savedPos = beginOffline (startBeat);
    juce::AudioBuffer<float> out;
    renderOffline (out, (int) std::ceil (beatsToSamples (juce::jmax (0.0, endBeat - startBeat))));
    endOffline (savedPos);
    song.limiter = lim;
    const juce::ScopedLock sl (lock);
    for (auto& [t, m] : savedMutes) t->mute = m;
    for (auto& [t, so] : savedSolos) t->solo = so;
    return out;
}

juce::Result Engine::exportMix (const juce::File& file, int bitDepth, double startBeat, double endBeat,
                                std::function<void (float)> progress)
{
    if (endBeat < 0) endBeat = song.lengthInBeats();
    if (endBeat <= startBeat) return juce::Result::fail (juce::String (u8"La canción está vacía: no hay nada para exportar"));

    // MP3 and M4A: render a WAV first, then encode it
    {
        const auto wantedExt = file.getFileExtension().toLowerCase();
        if (wantedExt == ".mp3" || wantedExt == ".m4a")
        {
            auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("galpon-export", ".wav", false);
            auto r = exportMix (temp, 16, startBeat, endBeat, progress);
            if (r.wasOk())
                r = wantedExt == ".mp3" ? encodeMp3 (temp, file) : encodeM4a (temp, file);
            temp.deleteFile();
            return r;
        }
    }

    const auto savedPos = beginOffline (startBeat);

    std::unique_ptr<juce::AudioFormat> format;
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".aif" || ext == ".aiff") format = std::make_unique<juce::AiffAudioFormat>();
    else if (ext == ".flac")             format = std::make_unique<juce::FlacAudioFormat>();
    else                                 format = std::make_unique<juce::WavAudioFormat>();

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = format->createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate (sampleRate)
                                                      .withNumChannels (2)
                                                      .withBitsPerSample (ext == ".flac" ? juce::jmin (bitDepth, 24) : bitDepth));
    juce::Result result = juce::Result::ok();
    if (writer == nullptr)
    {
        result = juce::Result::fail ("No se pudo crear " + file.getFullPathName());
    }
    else
    {
        const auto total = (juce::int64) beatsToSamples (endBeat - startBeat) + (juce::int64) (sampleRate * 2.0);  // + 2 s of tails
        juce::AudioBuffer<float> block (2, blockSize);
        juce::int64 done = 0;
        while (done < total)
        {
            const int n = (int) std::min<juce::int64> (blockSize, total - done);
            renderOffline (block, n);
            writer->writeFromAudioSampleBuffer (block, 0, n);
            done += n;
            if (progress && (done % (blockSize * 64)) < blockSize) progress ((float) done / (float) total);
        }
        writer.reset();
    }

    endOffline (savedPos);
    return result;
}

//==============================================================================
// Undo
//==============================================================================
juce::var Engine::snapshot() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("bpm", song.bpm);
    o->setProperty ("sigNum", song.sigNum);
    o->setProperty ("sigDen", song.sigDen);
    o->setProperty ("key", song.key);
    o->setProperty ("minor", song.minor);
    o->setProperty ("chords", chordsToVar (song.chords));
    o->setProperty ("sections", sectionsToVar (song.sections));
    juce::Array<juce::var> tracks;
    for (auto& t : song.tracks)
    {
        auto* to = new juce::DynamicObject();
        to->setProperty ("id", t->id);
        to->setProperty ("inputGainDb", t->inputGainDb);
        to->setProperty ("name", t->name);
        to->setProperty ("volumeDb", t->volumeDb);
        to->setProperty ("pan", t->pan);
        to->setProperty ("mute", t->mute);
        to->setProperty ("solo", t->solo);
        to->setProperty ("drummer", drummerToVar (t->drummer));
        to->setProperty ("sends", sendsToVar (t->sends));
        to->setProperty ("output", t->output);
        to->setProperty ("automation", automationToVar (t->automation));
        juce::Array<juce::var> clips;
        for (auto& c : t->clips) clips.add (clipToVar (c));
        to->setProperty ("clips", clips);
        tracks.add (juce::var (to));
    }
    o->setProperty ("tracks", tracks);
    return juce::var (o);
}

void Engine::restore (const juce::var& v)
{
    // 1. Which tracks, in which order (brings back deleted ones, removes added ones)
    if (auto* tracks = getOrDefault (v, "tracks", {}).getArray())
    {
        std::vector<std::unique_ptr<Track>> order;
        std::map<int, std::unique_ptr<Track>> current;
        {
            const juce::ScopedLock sl (lock);
            for (auto& t : song.tracks) current[t->id] = std::move (t);
            song.tracks.clear();
        }
        for (auto& tv : *tracks)
        {
            const int id = (int) getOrDefault (tv, "id", 0);
            if (auto it = current.find (id); it != current.end()) { order.push_back (std::move (it->second)); current.erase (it); }
            else if (auto g = graveyard.find (id); g != graveyard.end())
            {
                prepareTrack (*g->second);
                order.push_back (std::move (g->second));
                graveyard.erase (g);
            }
        }
        for (auto& [id, t] : current) graveyard[id] = std::move (t);   // tracks added after the snapshot
        const juce::ScopedLock sl (lock);
        song.tracks = std::move (order);
    }

    std::vector<std::pair<Track*, std::vector<Clip>>> newClips;
    if (auto* tracks = getOrDefault (v, "tracks", {}).getArray())
        for (auto& tv : *tracks)
            if (auto* t = song.findTrack ((int) getOrDefault (tv, "id", 0)))
            {
                std::vector<Clip> clips;
                if (auto* cl = getOrDefault (tv, "clips", {}).getArray())
                    for (auto& cv : *cl)
                    {
                        Clip c;
                        clipFromVar (c, cv);
                        if (! c.midi) c.audio = loadAudio (c.audioFile);
                        clips.push_back (std::move (c));
                    }
                newClips.emplace_back (t, std::move (clips));
            }

    const juce::ScopedLock sl (lock);
    song.bpm = (double) getOrDefault (v, "bpm", song.bpm);
    song.sigNum = (int) getOrDefault (v, "sigNum", song.sigNum);
    song.sigDen = (int) getOrDefault (v, "sigDen", song.sigDen);
    song.key = getOrDefault (v, "key", song.key).toString();
    song.minor = (bool) getOrDefault (v, "minor", song.minor);
    if (auto cv = getOrDefault (v, "chords", {}); cv.isArray()) song.chords = chordsFromVar (cv);
    song.sections = sectionsFromVar (getOrDefault (v, "sections", {}));
    if (auto* tracks = getOrDefault (v, "tracks", {}).getArray())
        for (auto& tv : *tracks)
            if (auto* t = song.findTrack ((int) getOrDefault (tv, "id", 0)))
            {
                t->name = getOrDefault (tv, "name", t->name).toString();
                t->volumeDb = (float) (double) getOrDefault (tv, "volumeDb", t->volumeDb);
                t->pan = (float) (double) getOrDefault (tv, "pan", t->pan);
                t->mute = (bool) getOrDefault (tv, "mute", t->mute);
                t->solo = (bool) getOrDefault (tv, "solo", t->solo);
                t->inputGainDb = (float) (double) getOrDefault (tv, "inputGainDb", t->inputGainDb);
                drummerFromVar (t->drummer, getOrDefault (tv, "drummer", {}));
                if (auto sv = getOrDefault (tv, "sends", {}); sv.isArray()) t->sends = sendsFromVar (sv);
                t->output = (int) getOrDefault (tv, "output", t->output);
                if (auto av = getOrDefault (tv, "automation", {}); av.isArray()) t->automation = automationFromVar (av);
                t->needsAllNotesOff = true;
            }
    for (auto& [t, clips] : newClips)
        t->clips = std::move (clips);
    dirty = true;
}

void Engine::pushUndo()
{
    undoStack.push_back (snapshot());
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool Engine::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (snapshot());
    auto v = undoStack.back();
    undoStack.pop_back();
    restore (v);
    return true;
}

bool Engine::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (snapshot());
    auto v = redoStack.back();
    redoStack.pop_back();
    restore (v);
    return true;
}

//==============================================================================
// Takes and helpers
//==============================================================================
juce::Result Engine::setActiveTake (Track& t, Clip& c, int index)
{
    if (c.midi || index < 0 || index >= (int) c.takes.size())
        return juce::Result::fail (juce::String (u8"Esa toma no existe"));
    auto& take = c.takes[(size_t) index];
    auto audio = loadAudio (take.audioFile);
    if (audio == nullptr) return juce::Result::fail ("No encuentro el audio de esa toma");
    // The region may have been split or trimmed since it was recorded: keep the same moment
    // of the pass (e.g. the second half of a split region plays the second half of every take)
    juce::int64 shift = 0;
    if (c.activeTake >= 0 && c.activeTake < (int) c.takes.size())
        shift = c.offsetSamples - c.takes[(size_t) c.activeTake].offsetSamples;
    const juce::ScopedLock sl (lock);
    c.activeTake = index;
    c.audioFile = take.audioFile;
    c.offsetSamples = std::max<juce::int64> (0, take.offsetSamples + shift);
    c.audio = audio;
    juce::ignoreUnused (t);
    dirty = true;
    return juce::Result::ok();
}

void Engine::closeOpenMidiNotes (double atBeat)
{
    // audio thread, lock held: only touches vectors with reserved room
    for (auto& take : midiTakes)
        for (int p = 0; p < 128; ++p)
            if (take.open[(size_t) p] && take.notes.size() < take.notes.capacity())
            {
                take.open[(size_t) p] = false;
                take.notes.push_back ({ take.openBeat[(size_t) p], juce::jmax (0.02, atBeat - take.openBeat[(size_t) p]), p, take.openVel[(size_t) p] });
            }
}

//==============================================================================
// Tuner (pitch detection on the selected audio track's input)
//==============================================================================
juce::var Engine::getTunerReading()
{
    constexpr int N = 4096;
    std::array<float, N> buf;
    const int w = tunerWrite.load();
    for (int i = 0; i < N; ++i)
        buf[(size_t) i] = tunerRing[(size_t) ((w + i) % N)];

    auto* o = new juce::DynamicObject();
    float rms = 0;
    for (auto s : buf) rms += s * s;
    rms = std::sqrt (rms / N);
    o->setProperty ("level", rms);
    if (rms < 0.004f) { o->setProperty ("active", false); return juce::var (o); }

    // YIN-style difference function
    const int maxLag = juce::jmin (N / 2, (int) (sampleRate / 30.0));
    const int minLag = juce::jmax (2, (int) (sampleRate / 1500.0));
    std::vector<float> d ((size_t) maxLag + 1, 0.0f);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        float sum = 0;
        for (int i = 0; i < N - maxLag; ++i)
        {
            const float diff = buf[(size_t) i] - buf[(size_t) (i + lag)];
            sum += diff * diff;
        }
        d[(size_t) lag] = sum;
    }
    float running = 0;
    int best = -1;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += d[(size_t) lag];
        const float cmnd = running > 0 ? d[(size_t) lag] * lag / running : 1.0f;
        d[(size_t) lag] = cmnd;
    }
    for (int lag = minLag; lag < maxLag; ++lag)
        if (d[(size_t) lag] < 0.15f)
        {
            while (lag + 1 < maxLag && d[(size_t) lag + 1] < d[(size_t) lag]) ++lag;
            best = lag;
            break;
        }
    if (best < 0) { o->setProperty ("active", false); return juce::var (o); }

    // parabolic interpolation
    double lag = best;
    if (best > 1 && best < maxLag - 1)
    {
        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1.0e-9) lag = best + 0.5 * (a - c) / den;
    }
    const double freq = sampleRate / lag;
    const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
    const int nearest = juce::roundToInt (midi);
    static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    o->setProperty ("active", true);
    o->setProperty ("freq", freq);
    o->setProperty ("note", names[((nearest % 12) + 12) % 12]);
    o->setProperty ("octave", nearest / 12 - 1);
    o->setProperty ("cents", (midi - nearest) * 100.0);
    return juce::var (o);
}

//==============================================================================
void Engine::timerCallback()
{
    // Autosave every 30 s when something changed (never while recording)
    if (dirty && ! recording && ++autosaveCountdown >= 30)
    {
        autosaveCountdown = 0;
        saveSong();
    }
}

} // namespace galpon
