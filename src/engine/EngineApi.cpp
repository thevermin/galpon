#include "EngineApi.h"
#include "Drummer.h"
#include "Loops.h"
#include "Analysis.h"

namespace galpon
{

namespace
{
    juce::var ok (juce::var extra = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", true);
        if (! extra.isVoid()) o->setProperty ("data", extra);
        return juce::var (o);
    }

    juce::var fail (const juce::String& message)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", false);
        o->setProperty ("error", message);
        return juce::var (o);
    }

    juce::var fromResult (const juce::Result& r) { return r.wasOk() ? ok() : fail (r.getErrorMessage()); }

    juce::var arg (const juce::var& args, const char* key, const juce::var& def = {})
    {
        return getOrDefault (args, key, def);
    }

    bool has (const juce::var& args, const char* key)
    {
        auto* o = args.getDynamicObject();
        return o != nullptr && o->hasProperty (key);
    }
}

EngineApi::EngineApi (Engine& e) : engine (e) {}

//==============================================================================
juce::var EngineApi::slotInfo (const Slot& s) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", s.kind);
    o->setProperty ("type", s.type);
    o->setProperty ("name", s.name);
    o->setProperty ("preset", s.preset);
    o->setProperty ("bypass", s.bypass);
    o->setProperty ("missing", s.processor == nullptr);
    o->setProperty ("hasEditor", s.processor != nullptr && s.processor->hasEditor());
    if (auto* b = dynamic_cast<BuiltinProcessor*> (s.processor.get()))
        o->setProperty ("extra", b->getExtraInfo());
    if (s.processor != nullptr)
        o->setProperty ("latency", s.processor->getLatencySamples());
    return juce::var (o);
}

juce::var EngineApi::getState()
{
    auto& song = engine.song;
    auto* root = new juce::DynamicObject();

    auto* s = new juce::DynamicObject();
    s->setProperty ("name", song.name);
    s->setProperty ("bpm", song.bpm);
    s->setProperty ("sigNum", song.sigNum);
    s->setProperty ("sigDen", song.sigDen);
    s->setProperty ("key", song.key);
    s->setProperty ("minor", song.minor);
    s->setProperty ("cycle", song.cycle);
    s->setProperty ("cycleStart", song.cycleStart);
    s->setProperty ("cycleEnd", song.cycleEnd);
    s->setProperty ("metronome", song.metronome);
    s->setProperty ("countIn", song.countIn);
    s->setProperty ("masterDb", song.masterDb);
    s->setProperty ("limiter", song.limiter);
    s->setProperty ("length", song.lengthInBeats());
    root->setProperty ("song", juce::var (s));

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
        o->setProperty ("armed", t->armed);
        o->setProperty ("monitor", t->monitor);
        o->setProperty ("inputChannel", t->inputChannel);
        o->setProperty ("stereoInput", t->stereoInput);
        o->setProperty ("inputGainDb", t->inputGainDb);
        o->setProperty ("sends", sendsToVar (t->sends));
        o->setProperty ("output", t->output);
        o->setProperty ("automation", automationToVar (t->automation));
        o->setProperty ("autoWrite", t->autoWrite);
        if (! t->instrument.isEmpty())
            o->setProperty ("instrument", slotInfo (t->instrument));
        juce::Array<juce::var> inserts;
        for (auto& sl : t->inserts) inserts.add (slotInfo (sl));
        o->setProperty ("inserts", inserts);
        o->setProperty ("drummer", drummerToVar (t->drummer));
        if (t->kind == TrackKind::Drummer)
            if (auto* f = engine.drummerFollowTrack (*t))
                o->setProperty ("listensTo", juce::var (juce::Array<juce::var> { f->id, f->name, f->kind == TrackKind::Audio }));
        juce::Array<juce::var> clips;
        for (auto& c : t->clips) clips.add (clipToVar (c));
        o->setProperty ("clips", clips);
        tracks.add (juce::var (o));
    }
    root->setProperty ("tracks", tracks);

    juce::Array<juce::var> chords;
    for (auto& c : song.chords)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("beat", c.beat);
        o->setProperty ("length", c.length);
        o->setProperty ("root", c.root);
        o->setProperty ("quality", c.quality);
        o->setProperty ("name", analysis::chordName (c.root, c.quality, true));
        o->setProperty ("en", analysis::chordName (c.root, c.quality, false));
        chords.add (juce::var (o));
    }
    root->setProperty ("chords", chords);
    root->setProperty ("sections", sectionsToVar (song.sections));
    {
        juce::Array<juce::var> kinds;
        for (auto& [id, name] : sectionKinds()) kinds.add (juce::Array<juce::var> { id, name });
        root->setProperty ("sectionKinds", kinds);
    }

    juce::Array<juce::var> master;
    for (auto& sl : song.masterInserts) master.add (slotInfo (sl));
    root->setProperty ("master", master);
    root->setProperty ("selected", engine.selectedTrackId.load());
    root->setProperty ("folder", engine.songFolder.getFullPathName());
    root->setProperty ("dirty", engine.dirty);
    root->setProperty ("sampleRate", engine.getSampleRate());
    root->setProperty ("version", stateVersion);
    return juce::var (root);
}

void EngineApi::applyStems()
{
    std::array<juce::AudioBuffer<float>, StemSeparator::numStems> out;
    if (! stems.takeResult (out)) return;
    const auto job = stems.job();
    engine.pushUndo();
    const int order[] = { 3, 0, 1, 2 };    // Voz, Batería, Bajo, Otros
    int firstId = 0;
    for (int k : order)
    {
        const auto name = StemSeparator::stemName (k);
        auto file = engine.getAudioFolder().getNonexistentChildFile (juce::File::createLegalFileName (job.name + " - " + name), ".wav", false);
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (file);
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (engine.getSampleRate()).withNumChannels (2).withBitsPerSample (24));
            if (w == nullptr) continue;
            w->writeFromAudioSampleBuffer (out[(size_t) k], 0, out[(size_t) k].getNumSamples());
        }
        auto* t = engine.addTrack (TrackKind::Audio, name + " (" + job.name + ")");
        Clip c;
        c.midi = false;
        c.name = name;
        c.audioFile = file.getFileName();
        c.audio = engine.loadAudio (c.audioFile);
        c.start = job.startBeat;
        c.length = job.lengthBeats;
        c.sourceBpm = engine.song.bpm;
        engine.addClip (*t, std::move (c));
        if (firstId == 0) firstId = t->id;
    }
    if (auto* original = engine.song.findTrack (job.trackId))
    {
        const juce::ScopedLock sl (engine.lock);
        original->mute = true;      // the original stays (muted), so nothing is lost
    }
    if (firstId != 0) engine.selectedTrackId = firstId;
    engine.dirty = true;
    touch();
}

juce::Result EngineApi::useRealDrums (Track& t)
{
    const auto file = sounds.installedFile ("virtuosity-drums");
    if (! file.existsAsFile()) return juce::Result::fail (juce::String (u8"La batería acústica real no está descargada"));
    auto r = engine.setInstrument (t, "builtin", "sampler", {});
    if (r.wasOk() && t.instrument.processor != nullptr)
    {
        if (auto* b = dynamic_cast<BuiltinProcessor*> (t.instrument.processor.get()))
            r = b->setExtra ("file", file.getFullPathName());
        t.instrument.name = juce::String (u8"Batería acústica real");
    }
    return r;
}

juce::var EngineApi::getTick()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("beat", engine.getPositionBeats());
    o->setProperty ("playing", engine.isPlaying());
    o->setProperty ("recording", engine.isRecording());
    o->setProperty ("countIn", engine.isCountingIn());
    if (engine.isRecording())
    {
        // so the interface can draw the take growing while it's recorded
        auto* r = new juce::DynamicObject();
        juce::Array<juce::var> ids;
        for (int id : engine.recordingTrackIds()) ids.add (id);
        r->setProperty ("tracks", ids);
        r->setProperty ("start", engine.recordStartBeats());
        o->setProperty ("rec", juce::var (r));
    }
    o->setProperty ("cpu", engine.getCpu());
    if (auto* d = engine.deviceManager.getCurrentAudioDevice())
        o->setProperty ("xruns", d->getXRunCount());   // times the audio couldn't be computed in time (clicks)
    o->setProperty ("version", stateVersion);

    juce::Array<juce::var> meters, inputs;
    for (auto& t : engine.song.tracks)
    {
        juce::Array<juce::var> m { (double) t->peakL.exchange (0.0f), (double) t->peakR.exchange (0.0f) };
        meters.add (m);
        inputs.add ((double) t->inputPeak.exchange (0.0f));
    }
    o->setProperty ("meters", meters);
    o->setProperty ("inputs", inputs);
    auto mm = engine.getMasterMeter();
    o->setProperty ("master", juce::Array<juce::var> { (double) mm.l, (double) mm.r });
    engine.resetMasterMeter();
    if (engine.tunerEnabled)
        o->setProperty ("tuner", engine.getTunerReading());
    if (auto dl = sounds.progress(); dl.size() > 0)
        o->setProperty ("downloads", dl);
    if (engine.game.isLoaded())
    {
        auto* g = new juce::DynamicObject();
        g->setProperty ("pos", engine.game.positionSeconds());
        g->setProperty ("playing", engine.game.isPlaying());
        g->setProperty ("ended", engine.game.hasEnded());
        g->setProperty ("speed", engine.game.getSpeed());
        g->setProperty ("latency", engine.outputLatencySeconds());
        g->setProperty ("now", juce::Time::getMillisecondCounterHiRes());
        g->setProperty ("level", (double) engine.scorer.inputLevel());
        g->setProperty ("listening", engine.scorerInput.load() >= 0);
        juce::Array<juce::var> js;
        for (auto& j : engine.scorer.take())
            js.add (juce::Array<juce::var> { j.index, j.pass, j.kind, j.hit, (double) j.timingMs, (double) j.value });
        if (! js.isEmpty()) g->setProperty ("judgments", js);
        applyGameTone();
        if (gameToneOn && gameActiveRig >= 0 && gameActiveRig < (int) gameRigs.size())
            g->setProperty ("tone", gameRigs[(size_t) gameActiveRig].name);
        o->setProperty ("game", juce::var (g));
    }
    if (stems.phase() != StemSeparator::Idle)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("phase", (int) stems.phase());
        so->setProperty ("progress", (double) stems.progress());
        so->setProperty ("name", stems.job().name);
        so->setProperty ("error", stems.error());
        o->setProperty ("stems", juce::var (so));
        if (stems.phase() == StemSeparator::Done) applyStems();
        else if (stems.phase() == StemSeparator::Failed) stems.cancel();   // reported once
    }
    if (auto problem = engine.checkRecordingHealth(); problem.isNotEmpty())
    {
        engine.lastError = problem;
        touch();
    }
    if (engine.isRecording())
        o->setProperty ("recLost", (double) engine.recordingDroppedSamples() / engine.getSampleRate());
    if (engine.lastError.isNotEmpty())
    {
        o->setProperty ("error", engine.lastError);
        engine.lastError.clear();
    }
    return juce::var (o);
}

//==============================================================================
juce::var EngineApi::getAudioSetup()
{
    auto& dm = engine.deviceManager;
    auto* o = new juce::DynamicObject();

    juce::Array<juce::var> types;
    for (auto* t : dm.getAvailableDeviceTypes()) types.add (t->getTypeName());
    o->setProperty ("types", types);
    o->setProperty ("type", dm.getCurrentAudioDeviceType());

    if (auto* type = dm.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        juce::Array<juce::var> ins, outs;
        for (auto& n : type->getDeviceNames (true))  ins.add (n);
        for (auto& n : type->getDeviceNames (false)) outs.add (n);
        o->setProperty ("inputDevices", ins);
        o->setProperty ("outputDevices", outs);
    }

    auto setup = dm.getAudioDeviceSetup();
    o->setProperty ("inputDevice", setup.inputDeviceName);
    o->setProperty ("outputDevice", setup.outputDeviceName);

    if (auto* d = dm.getCurrentAudioDevice())
    {
        o->setProperty ("sampleRate", d->getCurrentSampleRate());
        o->setProperty ("bufferSize", d->getCurrentBufferSizeSamples());
        juce::Array<juce::var> rates, sizes, inNames, outNames, activeIns, activeOuts;
        for (auto r : d->getAvailableSampleRates()) rates.add (r);
        for (auto b : d->getAvailableBufferSizes()) sizes.add (b);
        auto allIns = d->getInputChannelNames(), allOuts = d->getOutputChannelNames();
        // Some Macs name channels just "1", "2": make them readable
        for (auto& n : allIns)  if (n.trim().containsOnly ("0123456789")) n = juce::String (u8"Entrada ") + n.trim();
        for (auto& n : allOuts) if (n.trim().containsOnly ("0123456789")) n = juce::String (u8"Salida ") + n.trim();
        auto activeInBits = d->getActiveInputChannels(), activeOutBits = d->getActiveOutputChannels();
        for (int i = 0; i < allIns.size(); ++i)
        {
            inNames.add (allIns[i]);
            activeIns.add (activeInBits[i]);
        }
        for (int i = 0; i < allOuts.size(); ++i)
        {
            outNames.add (allOuts[i]);
            activeOuts.add (activeOutBits[i]);
        }
        o->setProperty ("sampleRates", rates);
        o->setProperty ("bufferSizes", sizes);
        o->setProperty ("inputChannels", inNames);
        o->setProperty ("outputChannels", outNames);
        o->setProperty ("inputActive", activeIns);
        o->setProperty ("outputActive", activeOuts);

        // The names of the channels tracks can record from (only active ones, in order)
        juce::Array<juce::var> sources;
        for (int i = 0; i < allIns.size(); ++i)
            if (activeInBits[i]) sources.add (allIns[i]);
        o->setProperty ("sources", sources);

        const double sr = d->getCurrentSampleRate();
        const int inLat = d->getInputLatencyInSamples(), outLat = d->getOutputLatencyInSamples();
        o->setProperty ("inputLatencyMs", inLat * 1000.0 / sr);
        o->setProperty ("outputLatencyMs", outLat * 1000.0 / sr);
        o->setProperty ("bufferMs", d->getCurrentBufferSizeSamples() * 1000.0 / sr);
        o->setProperty ("roundTripMs", (inLat + outLat + 2 * d->getCurrentBufferSizeSamples()) * 1000.0 / sr);
        o->setProperty ("deviceOpen", true);
    }
    else
    {
        o->setProperty ("deviceOpen", false);
    }

    o->setProperty ("recordOffsetMs", engine.recordOffsetMs);
    o->setProperty ("metronomeLevel", engine.metronomeLevel);
    o->setProperty ("compensationMs", engine.getLatencyCompensationSamples() * 1000.0 / engine.getSampleRate());

    juce::Array<juce::var> midi;
    for (auto& m : juce::MidiInput::getAvailableDevices())
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("id", m.identifier);
        mo->setProperty ("name", m.name);
        mo->setProperty ("enabled", dm.isMidiInputDeviceEnabled (m.identifier));
        midi.add (juce::var (mo));
    }
    o->setProperty ("midiInputs", midi);
    return juce::var (o);
}

juce::Result EngineApi::setAudioSetup (const juce::var& args)
{
    auto& dm = engine.deviceManager;
    if (has (args, "type") && arg (args, "type").toString() != dm.getCurrentAudioDeviceType())
        dm.setCurrentAudioDeviceType (arg (args, "type").toString(), true);

    auto setup = dm.getAudioDeviceSetup();
    if (has (args, "inputDevice"))  setup.inputDeviceName  = arg (args, "inputDevice").toString();
    if (has (args, "outputDevice")) setup.outputDeviceName = arg (args, "outputDevice").toString();
    if (has (args, "sampleRate"))   setup.sampleRate = (double) arg (args, "sampleRate");
    if (has (args, "bufferSize"))   setup.bufferSize = (int) arg (args, "bufferSize");

    if (auto* list = arg (args, "inputActive").getArray())
    {
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        for (int i = 0; i < list->size(); ++i)
            setup.inputChannels.setBit (i, (bool) (*list)[i]);
    }
    if (auto* list = arg (args, "outputActive").getArray())
    {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        for (int i = 0; i < list->size(); ++i)
            setup.outputChannels.setBit (i, (bool) (*list)[i]);
    }
    if (has (args, "inputDevice") && ! has (args, "inputActive"))
        setup.useDefaultInputChannels = true;
    if (has (args, "outputDevice") && ! has (args, "outputActive"))
        setup.useDefaultOutputChannels = true;

    auto error = dm.setAudioDeviceSetup (setup, true);
    engine.saveDeviceSettings();
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
}

//==============================================================================
juce::var EngineApi::paramsFor (int trackId, int slot)
{
    auto* s = engine.findSlot (trackId, slot);
    if (s == nullptr || s->processor == nullptr) return fail ("No hay nada en ese lugar");

    juce::Array<juce::var> list;
    auto& params = s->processor->getParameters();
    const int limit = juce::jmin (params.size(), 96);
    for (int i = 0; i < limit; ++i)
    {
        auto* p = params[i];
        if (! p->isAutomatable() && dynamic_cast<BuiltinProcessor*> (s->processor.get()) == nullptr) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("index", i);
        o->setProperty ("name", p->getName (48));
        o->setProperty ("value", p->getValue());
        o->setProperty ("text", p->getCurrentValueAsText());
        o->setProperty ("label", p->getLabel());
        o->setProperty ("steps", p->getNumSteps());
        list.add (juce::var (o));
    }
    auto* r = new juce::DynamicObject();
    r->setProperty ("params", list);
    r->setProperty ("total", params.size());
    r->setProperty ("slot", slotInfo (*s));
    return ok (juce::var (r));
}

juce::var EngineApi::waveform (const juce::String& file, int points)
{
    const auto key = file + ":" + juce::String (points);
    if (auto it = waveformCache.find (key); it != waveformCache.end())
        return it->second;

    auto buffer = engine.loadAudio (file);
    if (buffer == nullptr) return {};
    points = juce::jlimit (16, 20000, points);
    const int n = buffer->getNumSamples();
    juce::Array<juce::var> peaks;
    for (int i = 0; i < points; ++i)
    {
        const int a = (int) ((juce::int64) n * i / points), b = juce::jmax (a + 1, (int) ((juce::int64) n * (i + 1) / points));
        float mag = 0;
        for (int ch = 0; ch < buffer->getNumChannels(); ++ch)
            mag = juce::jmax (mag, buffer->getMagnitude (ch, a, juce::jmin (b, n) - a));
        peaks.add (juce::roundToInt (mag * 1000.0f) / 1000.0);
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("peaks", peaks);
    o->setProperty ("samples", n);
    o->setProperty ("sampleRate", engine.getSampleRate());
    waveformCache[key] = juce::var (o);
    return waveformCache[key];
}

juce::var EngineApi::library()
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> presets, effects, instruments, styles, loops;
    for (auto& p : builtinPresets())
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("name", p.name);
        po->setProperty ("type", p.type);
        po->setProperty ("group", p.group);
        presets.add (juce::var (po));
    }
    for (auto& t : builtinTypes())
    {
        auto* to = new juce::DynamicObject();
        to->setProperty ("type", t.type);
        to->setProperty ("name", t.name);
        to->setProperty ("description", t.description);
        (t.category == "effect" ? effects : instruments).add (juce::var (to));
    }
    for (auto& st : drummerStyles())
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", st.id);
        so->setProperty ("name", st.name);
        so->setProperty ("who", st.who);
        so->setProperty ("description", st.description);
        styles.add (juce::var (so));
    }
    for (auto& l : loopLibrary())
    {
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("id", l.id);
        lo->setProperty ("name", l.name);
        lo->setProperty ("category", l.category);
        lo->setProperty ("colour", l.colour);
        lo->setProperty ("length", l.lengthBeats);
        lo->setProperty ("preset", l.preset);
        loops.add (juce::var (lo));
    }
    o->setProperty ("presets", presets);
    o->setProperty ("effects", effects);
    o->setProperty ("instruments", instruments);
    o->setProperty ("drummerStyles", styles);
    o->setProperty ("loops", loops);
    o->setProperty ("sounds", sounds.describe());
    o->setProperty ("plugins", engine.plugins.listToVar());
    o->setProperty ("pluginStatus", engine.plugins.getStatus());
    return juce::var (o);
}

juce::var EngineApi::recentSongs()
{
    juce::Array<juce::File> folders;
    for (auto& entry : juce::RangedDirectoryIterator (Engine::defaultSongsFolder(), false, "*.galpon", juce::File::findDirectories))
        if (entry.getFile().getChildFile ("project.json").existsAsFile())
            folders.add (entry.getFile());

    std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getChildFile ("project.json").getLastModificationTime() > b.getChildFile ("project.json").getLastModificationTime();
    });

    juce::Array<juce::var> list;
    for (auto& f : folders)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", f.getFullPathName());
        o->setProperty ("name", f.getFileNameWithoutExtension());
        o->setProperty ("modified", f.getChildFile ("project.json").getLastModificationTime().toMilliseconds());
        list.add (juce::var (o));
    }
    return list;
}

//==============================================================================
namespace
{
    // a tiny ring of the last command names: written on the message thread, read only when crashing
    constexpr int recentSize = 24;
    char recent[recentSize][40] {};
    std::atomic<int> recentNext { 0 };
}

juce::String EngineApi::recentCommands()
{
    juce::StringArray list;
    const int n = recentNext.load();
    for (int i = juce::jmax (0, n - recentSize); i < n; ++i) list.add (juce::String (recent[i % recentSize]));
    return list.joinIntoString (", ");
}

juce::var EngineApi::call (const juce::String& cmd, const juce::var& args)
{
    if (cmd != "getTick")
    {
        const int i = recentNext.load() % recentSize;
        cmd.copyToUTF8 (recent[i], sizeof (recent[i]));
        recentNext.fetch_add (1);
    }
    try
    {
        return callUnchecked (cmd, args);
    }
    catch (const std::bad_alloc&)
    {
        return fail (juce::String (u8"No alcanzó la memoria para hacer eso (") + cmd + juce::String (u8"). Cierra otras apps o usa una canción más corta."));
    }
    catch (const std::exception& e)
    {
        return fail (juce::String (u8"Algo falló adentro de Galpon (") + cmd + "): " + juce::String (e.what()));
    }
    catch (...)
    {
        return fail (juce::String (u8"Algo falló adentro de Galpon (") + cmd + ")");
    }
}

juce::var EngineApi::callUnchecked (const juce::String& cmd, const juce::var& args)
{
    auto& song = engine.song;
    auto trackArg = [&]() -> Track* { return song.findTrack ((int) arg (args, "trackId", arg (args, "id", 0))); };
    auto changed = [this] { engine.dirty = true; touch(); };

    // While recording, nothing may pull the floor out from under the take
    static const juce::StringArray notWhileRecording { "undo", "redo", "newSong", "loadSong", "setAudioSetup", "export",
                                                       "flexApply", "flexReset", "recoverRecordings", "scanPlugins", "detectChords" };
    if (engine.isRecording() && notWhileRecording.contains (cmd))
        return fail (juce::String (u8"Primero detén la grabación (así no se pierde nada)."));
    // the take is laid out on the timeline when it ends: time itself mustn't change under it
    if (engine.isRecording() && (cmd == "setPosition" || (cmd == "setSong" && (has (args, "bpm") || has (args, "sigNum") || has (args, "sigDen")
                                                                               || has (args, "cycle") || has (args, "cycleStart") || has (args, "cycleEnd")))))
        return fail (juce::String (u8"Mientras grabas no se puede mover el cursor ni cambiar el tempo o el ciclo."));
    if (cmd == "crashReport")
    {
        // left by the crash handler when Galpon closed by itself last time
        auto f = engine.getAppDataFolder().getChildFile ("cierre-inesperado.txt");
        if (! f.existsAsFile()) return ok();
        auto* o = new juce::DynamicObject();
        o->setProperty ("text", f.loadFileAsString().substring (0, 20000));
        auto seen = f.getSiblingFile ("cierre-inesperado-" + f.getLastModificationTime().formatted ("%Y%m%d-%H%M%S") + ".txt");
        f.moveFileTo (seen);
        o->setProperty ("path", seen.getFullPathName());
        return ok (juce::var (o));
    }
    if (cmd == "revealFile") { juce::File (arg (args, "path").toString()).revealToUser(); return ok(); }
    if (cmd.startsWith ("game")) return gameCall (cmd, args);
    if (cmd == "t3Status")     return ok (tone3000().status());
    if (cmd == "t3Connect")    return fromResult (tone3000().connect (arg (args, "key").toString()));
    if (cmd == "t3Disconnect") { tone3000().disconnect(); return ok(); }
    if (cmd == "t3Refresh")    { tone3000().refresh(); return ok(); }
    if (cmd == "t3Download")   { tone3000().download (arg (args, "toneId").toString()); return ok(); }
    if (engine.isRecording() && cmd == "deleteTrack")
        if (auto* t = trackArg(); t != nullptr && t->armed)
            return fail (juce::String (u8"Esa pista está grabando: detén la grabación antes de borrarla."));

    //---------------------------------------------------------------- reading
    if (cmd == "getState")       return ok (getState());
    if (cmd == "getTick")        return ok (getTick());
    if (cmd == "getAudioSetup")  return ok (getAudioSetup());
    if (cmd == "getLibrary")     return ok (library());
    if (cmd == "recentSongs")    return ok (recentSongs());
    if (cmd == "getParams")      return paramsFor ((int) arg (args, "trackId", 0), (int) arg (args, "slot", -1));
    if (cmd == "getWaveform")    return ok (waveform (arg (args, "file").toString(), (int) arg (args, "points", 400)));
    if (cmd == "pluginStatus")   return ok (engine.plugins.getStatus());

    //---------------------------------------------------------------- transport
    if (cmd == "play")       { engine.game.pause(); engine.play(); return ok(); }
    // (finishing a recording updates the song by itself, so play/stop don't redraw everything)
    if (cmd == "stop")       { engine.stop(); engine.endAutomationWrites(); return ok(); }
    if (cmd == "togglePlay") { if (engine.isPlaying()) { engine.stop(); engine.endAutomationWrites(); } else { engine.game.pause(); engine.play(); } return ok(); }
    if (cmd == "record")     { engine.game.pause(); auto r = engine.record(); touch(); return fromResult (r); }
    if (cmd == "findLostRecordings")
    {
        juce::Array<juce::var> list;
        for (auto& f : engine.findLostRecordings()) list.add (f);
        return ok (list);
    }
    if (cmd == "recoverRecordings")
    {
        juce::StringArray files;
        if (auto* a = arg (args, "files", {}).getArray()) for (auto& f : *a) files.add (f.toString());
        if ((bool) arg (args, "ignore", false))
        {
            engine.ignoredRecordings.addArray (files);
            engine.dirty = true;
            engine.saveSong();
            return ok();
        }
        engine.pushUndo();
        const int n = engine.recoverLostRecordings (files);
        engine.saveSong();
        changed();
        return ok (n);
    }
    if (cmd == "setPosition"){ engine.setPositionBeats ((double) arg (args, "beat", 0.0)); return ok(); }

    //---------------------------------------------------------------- song
    if (cmd == "setSong")
    {
        const bool undoable = has (args, "bpm") || has (args, "sigNum") || has (args, "key") || has (args, "minor");
        if (undoable) engine.pushUndo();
        const juce::ScopedLock sl (engine.lock);
        if (has (args, "name"))       song.name = arg (args, "name").toString();
        if (has (args, "bpm"))        song.bpm = juce::jlimit (20.0, 400.0, (double) arg (args, "bpm"));
        if (has (args, "sigNum"))     song.sigNum = juce::jlimit (1, 16, (int) arg (args, "sigNum"));
        if (has (args, "sigDen"))     song.sigDen = juce::jlimit (1, 16, (int) arg (args, "sigDen"));
        if (has (args, "key"))        song.key = arg (args, "key").toString();
        if (has (args, "minor"))      song.minor = (bool) arg (args, "minor");
        if (has (args, "cycle"))      song.cycle = (bool) arg (args, "cycle");
        if (has (args, "cycleStart")) song.cycleStart = juce::jmax (0.0, (double) arg (args, "cycleStart"));
        if (has (args, "cycleEnd"))   song.cycleEnd = juce::jmax (song.cycleStart + 0.25, (double) arg (args, "cycleEnd"));
        if (has (args, "metronome"))  song.metronome = (bool) arg (args, "metronome");
        if (has (args, "countIn"))    song.countIn = (bool) arg (args, "countIn");
        if (has (args, "masterDb"))   song.masterDb = juce::jlimit (-60.0f, 12.0f, (float) (double) arg (args, "masterDb"));
        if (has (args, "limiter"))    song.limiter = (bool) arg (args, "limiter");
        engine.dirty = true;
        if (! (has (args, "masterDb") && args.getDynamicObject()->getProperties().size() == 1))
            touch();   // a master-volume drag alone doesn't need the whole interface redrawn
        return ok();
    }

    //---------------------------------------------------------------- tracks
    if (cmd == "addTrack")
    {
        engine.pushUndo();
        auto* t = engine.addTrack (trackKindFromString (arg (args, "kind", "instrument").toString()), arg (args, "preset", "").toString());
        // The drummer plays the real (recorded) kit when it's been downloaded
        if (t->kind == TrackKind::Drummer) useRealDrums (*t);
        changed();
        return ok (t->id);
    }
    if (cmd == "deleteTrack") { engine.pushUndo(); engine.deleteTrack ((int) arg (args, "id", 0)); changed(); return ok(); }
    if (cmd == "moveTrack")   { engine.pushUndo(); engine.moveTrack ((int) arg (args, "id", 0), (int) arg (args, "index", 0)); changed(); return ok(); }
    if (cmd == "selectTrack") { engine.selectedTrackId = (int) arg (args, "id", 0); touch(); return ok(); }
    if (cmd == "setTrack")
    {
        auto* t = trackArg();
        if (t == nullptr) return fail ("Pista inexistente");
        // Volume and pan arrive many times per second while dragging: no undo step, no redraw
        const bool mixOnly = (has (args, "volumeDb") || has (args, "pan") || has (args, "inputGainDb")) && args.getDynamicObject()->getProperties().size() == 2;
        if (has (args, "name")) engine.pushUndo();
        const juce::ScopedLock sl (engine.lock);
        if (has (args, "name"))         t->name = arg (args, "name").toString();
        if (has (args, "colour"))       t->colour = arg (args, "colour").toString();
        if (has (args, "volumeDb"))     t->volumeDb = juce::jlimit (-80.0f, 12.0f, (float) (double) arg (args, "volumeDb"));
        if (has (args, "pan"))          t->pan = juce::jlimit (-1.0f, 1.0f, (float) (double) arg (args, "pan"));
        if (has (args, "autoWrite"))    t->autoWrite = (bool) arg (args, "autoWrite");
        if (has (args, "mute"))         t->mute = (bool) arg (args, "mute");
        if (has (args, "solo"))         t->solo = (bool) arg (args, "solo");
        if (has (args, "armed"))        t->armed = (bool) arg (args, "armed");
        if (has (args, "monitor"))      t->monitor = (bool) arg (args, "monitor");
        if (has (args, "inputChannel")) t->inputChannel = juce::jmax (0, (int) arg (args, "inputChannel"));
        if (has (args, "stereoInput"))  t->stereoInput = (bool) arg (args, "stereoInput");
        if (has (args, "inputGainDb"))  t->inputGainDb = juce::jlimit (-24.0f, 40.0f, (float) (double) arg (args, "inputGainDb"));
        if (has (args, "output"))
        {
            const int out = (int) arg (args, "output");
            auto* bus = engine.song.findTrack (out);
            t->output = (t->kind != TrackKind::Bus && bus != nullptr && bus->kind == TrackKind::Bus) ? out : 0;
        }
        engine.dirty = true;
        // "write" on and playing: the movement becomes automation (the lock is re-entrant)
        if (t->autoWrite && engine.isPlaying())
        {
            if (has (args, "volumeDb")) engine.writeAutomation (*t, "volume", t->volumeDb);
            if (has (args, "pan"))      engine.writeAutomation (*t, "pan", t->pan);
        }
        if (! mixOnly) touch();
        return ok();
    }
    if (cmd == "setSend")
    {
        // { trackId, busId, levelDb?, pre?, remove? } — creates the send if it isn't there yet
        auto* t = trackArg();
        const int busId = (int) arg (args, "busId", 0);
        auto* bus = engine.song.findTrack (busId);
        if (t == nullptr || t->kind == TrackKind::Bus) return fail ("Esa pista no puede enviar");
        if (bus == nullptr || bus->kind != TrackKind::Bus) return fail ("Ese bus no existe");
        const bool drag = has (args, "levelDb") && args.getDynamicObject()->getProperties().size() == 3;
        if (! drag) engine.pushUndo();
        const juce::ScopedLock sl (engine.lock);
        auto it = std::find_if (t->sends.begin(), t->sends.end(), [&] (const Send& x) { return x.busId == busId; });
        if ((bool) arg (args, "remove", false))
        {
            if (it != t->sends.end()) t->sends.erase (it);
        }
        else
        {
            if (it == t->sends.end())
            {
                if (t->sends.size() >= 8) return fail (juce::String (u8"Máximo 8 envíos por pista"));
                Send snd; snd.busId = busId;
                t->sends.push_back (snd);   // safe: the audio thread can't be mixing while we hold the lock
                it = t->sends.end() - 1;
            }
            if (has (args, "levelDb")) it->levelDb = juce::jlimit (-80.0f, 6.0f, (float) (double) arg (args, "levelDb"));
            if (has (args, "levelDb") && t->autoWrite && engine.isPlaying())
                engine.writeAutomation (*t, "send:" + juce::String (busId), it->levelDb);
            if (has (args, "pre"))     it->pre = (bool) arg (args, "pre");
        }
        engine.dirty = true;
        if (! drag) touch();
        return ok();
    }

    //---------------------------------------------------------------- real instruments (sampler)
    if (cmd == "downloadSound") return fromResult (sounds.download (arg (args, "id").toString()));
    if (cmd == "cancelDownload") { sounds.cancel (arg (args, "id").toString()); return ok(); }
    if (cmd == "useRealDrums")
    {
        // { trackId }: put the recorded acoustic kit on the drummer (it must be downloaded)
        auto* t = trackArg();
        if (t == nullptr) return fail ("Pista inexistente");
        if (! sounds.installedFile ("virtuosity-drums").existsAsFile()) return fail (juce::String (u8"Primero hay que descargar la batería acústica real"));
        engine.pushUndo();
        auto r = useRealDrums (*t);
        changed();
        return r.wasOk() ? ok() : fail (r.getErrorMessage());
    }
    if (cmd == "useSound")
    {
        // { id } or { file } (any .sfz/.wav), optionally { trackId } to put it on an existing track
        auto* snd = SoundLibrary::find (arg (args, "id").toString());
        juce::File file = snd != nullptr ? sounds.installedFile (snd->id) : juce::File (arg (args, "file").toString());
        if (! file.existsAsFile()) return fail (snd != nullptr ? juce::String (u8"Todavía no está descargado") : juce::String (u8"No encuentro el archivo"));
        engine.pushUndo();
        Track* t = trackArg();
        if (t == nullptr || t->kind == TrackKind::Audio || t->kind == TrackKind::Bus)
        {
            t = engine.addTrack (TrackKind::Instrument);
            t->name = snd != nullptr ? snd->name : file.getFileNameWithoutExtension();
        }
        auto r = engine.setInstrument (*t, "builtin", "sampler", {});
        if (r.wasOk() && t->instrument.processor != nullptr)
        {
            if (auto* b = dynamic_cast<BuiltinProcessor*> (t->instrument.processor.get()))
                r = b->setExtra ("file", file.getFullPathName());
            t->instrument.name = snd != nullptr ? snd->name : file.getFileNameWithoutExtension();
        }
        engine.selectedTrackId = t->id;
        changed();
        return r.wasOk() ? ok (t->id) : fail (r.getErrorMessage());
    }

    //---------------------------------------------------------------- flex (audio time & pitch)
    if (cmd == "flexAnalyze")
    {
        auto v = engine.flexAnalyze ((int) arg (args, "trackId", 0), (int) arg (args, "clipId", 0));
        return v.isObject() ? ok (v) : fail (juce::String (u8"No pude analizar esa región"));
    }
    if (cmd == "flexApply")
    {
        engine.pushUndo();
        auto r = engine.flexApply ((int) arg (args, "trackId", 0), (int) arg (args, "clipId", 0), arg (args, "settings", {}));
        changed();
        return fromResult (r);
    }
    if (cmd == "flexReset")
    {
        engine.pushUndo();
        auto r = engine.flexReset ((int) arg (args, "trackId", 0), (int) arg (args, "clipId", 0));
        changed();
        return fromResult (r);
    }

    //---------------------------------------------------------------- automation
    if (cmd == "setAutomation")
    {
        // { trackId, target, points: [[beat, value]…], on?, drag? }  replaces the lane's points
        auto* t = trackArg();
        const auto target = arg (args, "target", "").toString();
        if (t == nullptr || target.isEmpty()) return fail (juce::String (u8"Pista o control inexistente"));
        const bool drag = (bool) arg (args, "drag", false);
        if (! drag) engine.pushUndo();
        auto* o = new juce::DynamicObject();
        o->setProperty ("target", target);
        o->setProperty ("on", arg (args, "on", true));
        o->setProperty ("points", arg (args, "points", juce::Array<juce::var>()));
        auto parsed = automationFromVar (juce::Array<juce::var> { juce::var (o) });
        if (parsed.empty() || parsed[0].kind == AutomationLane::Unknown) return fail (juce::String (u8"Control desconocido: ") + target);
        {
            const juce::ScopedLock sl (engine.lock);
            if (auto* lane = t->findLane (target)) *lane = std::move (parsed[0]);
            else t->automation.push_back (std::move (parsed[0]));
        }
        engine.dirty = true;
        if (! drag) touch();
        return ok();
    }
    if (cmd == "removeAutomation")
    {
        auto* t = trackArg();
        if (t == nullptr) return fail ("Pista inexistente");
        engine.pushUndo();
        const auto target = arg (args, "target", "").toString();
        {
            const juce::ScopedLock sl (engine.lock);
            t->automation.erase (std::remove_if (t->automation.begin(), t->automation.end(),
                                                 [&] (const AutomationLane& l) { return l.target == target; }), t->automation.end());
        }
        changed();
        return ok();
    }

    //---------------------------------------------------------------- stems
    if (cmd == "separateStems")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr || c->midi || c->audio == nullptr) return fail (juce::String (u8"Elige una región de audio"));
        const int n = (int) engine.beatsToSamples (c->length);
        juce::AudioBuffer<float> audio (c->audio->getNumChannels(), juce::jmax (1, n));
        audio.clear();
        const int avail = (int) std::clamp<juce::int64> ((juce::int64) c->audio->getNumSamples() - c->offsetSamples, 0, n);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            if (avail > 0) audio.copyFrom (ch, 0, *c->audio, ch, (int) c->offsetSamples, avail);
        audio.applyGain (juce::Decibels::decibelsToGain (c->gainDb));
        StemSeparator::Job job { t->id, c->id, c->start, c->length, c->name };
        auto r = stems.start (audio, engine.getSampleRate(), job);
        touch();
        return fromResult (r);
    }
    if (cmd == "cancelStems") { stems.cancel(); return ok(); }

    //---------------------------------------------------------------- chords
    if (cmd == "detectChords")
    {
        engine.pushUndo();
        auto r = engine.detectChords ((int) arg (args, "trackId", 0), (int) arg (args, "clipId", 0));
        changed();
        if (r.failed()) return fail (r.getErrorMessage());
        return ok ((int) engine.song.chords.size());
    }
    if (cmd == "setChord")
    {
        const int i = (int) arg (args, "index", -1);
        if (i < 0 || i >= (int) engine.song.chords.size()) return fail ("Acorde inexistente");
        engine.pushUndo();
        {
            const juce::ScopedLock sl (engine.lock);
            auto& c = engine.song.chords[(size_t) i];
            if ((bool) arg (args, "remove", false)) engine.song.chords.erase (engine.song.chords.begin() + i);
            else
            {
                if (has (args, "root"))    c.root = ((int) arg (args, "root") % 12 + 12) % 12;
                if (has (args, "quality")) c.quality = juce::jlimit (0, 3, (int) arg (args, "quality"));
            }
        }
        engine.dirty = true;
        changed();
        return ok();
    }
    //---------------------------------------------------------------- parts of the song
    if (cmd == "setSections")
    {
        // The whole list at once (the interface edits a copy): [{beat, length, kind}]
        if (! (bool) arg (args, "noUndo", false)) engine.pushUndo();
        auto list = sectionsFromVar (arg (args, "sections"));
        {
            const juce::ScopedLock sl (engine.lock);
            engine.song.sections = std::move (list);
        }
        engine.regenerateAllDrummers();
        engine.dirty = true;
        changed();
        return ok();
    }
    if (cmd == "clearChords") { engine.pushUndo(); { const juce::ScopedLock sl (engine.lock); engine.song.chords.clear(); } engine.dirty = true; changed(); return ok(); }
    if (cmd == "chordsToTrack")
    {
        engine.pushUndo();
        auto* t = engine.chordsToTrack();
        changed();
        return t != nullptr ? ok (t->id) : fail (juce::String (u8"Todavía no hay acordes: primero identifícalos"));
    }

    //---------------------------------------------------------------- instruments & effects
    if (cmd == "setInstrument")
    {
        auto* t = trackArg();
        if (t == nullptr || t->kind == TrackKind::Audio || t->kind == TrackKind::Bus) return fail (juce::String (u8"Elige una pista de instrumento"));
        auto kind = arg (args, "kind", "builtin").toString();
        auto id = arg (args, "id", "").toString();
        auto preset = arg (args, "preset", "").toString();
        if (kind == "builtin" && id.isEmpty())
            if (auto* p = findPreset (preset)) id = p->type;
        auto r = engine.setInstrument (*t, kind, id, preset);
        if (r.wasOk() && preset.isNotEmpty() && t->kind == TrackKind::Instrument && (bool) arg (args, "rename", true))
            t->name = preset;
        changed();
        return fromResult (r);
    }
    if (cmd == "addInsert")
    {
        auto r = engine.addInsert ((int) arg (args, "trackId", 0), arg (args, "kind", "builtin").toString(),
                                   arg (args, "id", "").toString(), (int) arg (args, "index", -1));
        changed();
        return fromResult (r);
    }
    if (cmd == "removeInsert") { engine.removeInsert ((int) arg (args, "trackId", 0), (int) arg (args, "index", 0)); changed(); return ok(); }
    if (cmd == "moveInsert")   { engine.moveInsert ((int) arg (args, "trackId", 0), (int) arg (args, "from", 0), (int) arg (args, "to", 0)); changed(); return ok(); }
    if (cmd == "setBypass")
    {
        auto* s = engine.findSlot ((int) arg (args, "trackId", 0), (int) arg (args, "slot", -1));
        if (s == nullptr) return fail ("No hay nada en ese lugar");
        const juce::ScopedLock sl (engine.lock);
        s->bypass = (bool) arg (args, "bypass", false);
        changed();
        return ok();
    }
    if (cmd == "setParam")
    {
        auto* s = engine.findSlot ((int) arg (args, "trackId", 0), (int) arg (args, "slot", -1));
        if (s == nullptr || s->processor == nullptr) return fail ("No hay nada en ese lugar");
        auto& params = s->processor->getParameters();
        const int i = (int) arg (args, "index", -1);
        if (i < 0 || i >= params.size()) return fail (juce::String (u8"Parámetro inexistente"));
        params[i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (double) arg (args, "value", 0.0)));
        const int slotIndex = (int) arg (args, "slot", -1);
        if (auto* tr = engine.song.findTrack ((int) arg (args, "trackId", 0)); tr != nullptr && slotIndex >= 0 && tr->autoWrite && engine.isPlaying())
            engine.writeAutomation (*tr, "fx:" + juce::String (slotIndex) + ":" + juce::String (i), params[i]->getValue());
        engine.dirty = true;
        return ok (params[i]->getCurrentValueAsText());
    }
    if (cmd == "setExtra")
    {
        auto* s = engine.findSlot ((int) arg (args, "trackId", 0), (int) arg (args, "slot", -1));
        auto* b = s != nullptr ? dynamic_cast<BuiltinProcessor*> (s->processor.get()) : nullptr;
        if (b == nullptr) return fail (juce::String (u8"Ese efecto no tiene esa opción"));
        auto r = b->setExtra (arg (args, "key").toString(), arg (args, "value"));
        changed();
        return fromResult (r);
    }
    if (cmd == "openEditor")
    {
        if (! openPluginEditor) return fail ("No disponible");
        return fromResult (openPluginEditor ((int) arg (args, "trackId", 0), (int) arg (args, "slot", -1)));
    }

    //---------------------------------------------------------------- clips
    if (cmd == "addClip")
    {
        auto* t = trackArg();
        if (t == nullptr) return fail ("Pista inexistente");
        if (t->kind == TrackKind::Bus) return fail (juce::String (u8"Los buses no tienen regiones: mándales sonido con los envíos"));
        engine.pushUndo();
        Clip c;
        c.midi = true;
        c.start = juce::jmax (0.0, (double) arg (args, "start", 0.0));
        c.length = juce::jmax (0.25, (double) arg (args, "length", song.beatsPerBar() * (t->kind == TrackKind::Drummer ? 8 : 4)));
        c.loopLength = c.length;
        c.name = t->kind == TrackKind::Drummer ? juce::String (u8"Sección") : juce::String (u8"Región MIDI");
        c.seed = juce::Random::getSystemRandom().nextInt (100000);
        auto& added = engine.addClip (*t, std::move (c));
        const int id = added.id;
        if (t->kind == TrackKind::Drummer) engine.regenerateDrummer (*t, id);
        changed();
        return ok (id);
    }
    if (cmd == "setClip" || cmd == "moveClip")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr) return fail (juce::String (u8"Región inexistente"));
        if (! (bool) arg (args, "noUndo", false)) engine.pushUndo();
        bool regen = false;
        {
            const juce::ScopedLock sl (engine.lock);
            if (has (args, "start"))      c->start = juce::jmax (0.0, (double) arg (args, "start"));
            if (has (args, "length"))
            {
                const double oldEnd = c->end();
                c->length = juce::jmax (0.0625, (double) arg (args, "length"));
                regen = true;
                // Drummer parts sit one after the other: making one longer or shorter moves the
                // ones after it, which keep their rhythm (they aren't rewritten)
                if (t->kind == TrackKind::Drummer && (bool) arg (args, "ripple", true) && ! has (args, "start"))
                {
                    const double delta = c->end() - oldEnd;
                    for (auto& other : t->clips)
                        if (&other != c && other.start >= oldEnd - 1.0e-6)
                            other.start = juce::jmax (0.0, other.start + delta);
                    t->needsAllNotesOff = true;
                }
            }
            if (has (args, "part"))     { c->part = arg (args, "part").toString(); c->locked = false; regen = true; }
            if (has (args, "name"))       c->name = arg (args, "name").toString();
            if (has (args, "gainDb"))     c->gainDb = juce::jlimit (-60.0f, 24.0f, (float) (double) arg (args, "gainDb"));
            if (has (args, "loopLength")) c->loopLength = juce::jmax (0.25, (double) arg (args, "loopLength"));
            if (has (args, "fill"))     { c->fill = (bool) arg (args, "fill"); regen = true; }
            if (has (args, "offsetSamples")) c->offsetSamples = std::max<juce::int64> (0, (juce::int64) (double) arg (args, "offsetSamples"));
            if (c->midi && has (args, "length") && ! has (args, "loopLength") && t->kind != TrackKind::Drummer
                && ! (bool) arg (args, "loop", false))
                c->loopLength = juce::jmax (c->loopLength, c->length);
        }
        // Move to another track (same kind only)
        if (has (args, "toTrackId"))
        {
            auto* dest = song.findTrack ((int) arg (args, "toTrackId"));
            if (dest != nullptr && dest != t && dest->kind != TrackKind::Bus && (dest->kind == TrackKind::Audio) == (t->kind == TrackKind::Audio))
            {
                const juce::ScopedLock sl (engine.lock);
                Clip moved = *c;
                t->clips.erase (std::remove_if (t->clips.begin(), t->clips.end(), [&] (const Clip& x) { return x.id == moved.id; }), t->clips.end());
                dest->clips.push_back (std::move (moved));
                t->needsAllNotesOff = true;
                t = dest;
            }
        }
        if (regen && t->kind == TrackKind::Drummer) engine.regenerateDrummer (*t, (int) arg (args, "clipId", 0));
        changed();
        return ok();
    }
    if (cmd == "deleteClip")
    {
        auto* t = trackArg();
        if (t == nullptr) return fail ("Pista inexistente");
        engine.pushUndo();
        const int id = (int) arg (args, "clipId", 0);
        const juce::ScopedLock sl (engine.lock);
        t->clips.erase (std::remove_if (t->clips.begin(), t->clips.end(), [id] (const Clip& c) { return c.id == id; }), t->clips.end());
        t->needsAllNotesOff = true;
        changed();
        return ok();
    }
    if (cmd == "duplicateClip")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr) return fail (juce::String (u8"Región inexistente"));
        engine.pushUndo();
        Clip copy = *c;
        copy.id = 0;
        copy.start = has (args, "start") ? (double) arg (args, "start") : c->end();
        copy.seed = c->seed + 1;
        const int newId = engine.addClip (*t, std::move (copy)).id;
        if (t->kind == TrackKind::Drummer) engine.regenerateDrummer (*t, newId);
        changed();
        return ok (newId);
    }
    if (cmd == "splitClip")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        const double at = (double) arg (args, "beat", 0.0);
        if (c == nullptr || at <= c->start + 0.01 || at >= c->end() - 0.01) return fail (juce::String (u8"Pon el cursor dentro de la región para dividirla"));
        engine.pushUndo();
        Clip right = *c;
        right.id = song.newId();
        const double cut = at - c->start;
        right.start = at;
        right.length = c->length - cut;
        if (c->midi)
        {
            right.notes.clear();
            std::vector<Note> left;
            const double L = juce::jmax (0.25, juce::jmin (c->loopLength, c->length));
            // unroll loops so both halves keep exactly what was heard
            for (double rep = 0; rep < c->length; rep += L)
                for (auto& n : c->notes)
                {
                    if (n.beat >= L) continue;
                    const double b = rep + n.beat;
                    if (b >= c->length) continue;
                    Note copy = n;
                    if (b < cut) { copy.beat = b; copy.length = juce::jmin (n.length, cut - b); left.push_back (copy); }
                    else         { copy.beat = b - cut; right.notes.push_back (copy); }
                }
            right.loopLength = right.length;
            right.locked = true;
            const juce::ScopedLock sl (engine.lock);
            c->notes = std::move (left);
            c->loopLength = cut;
            c->length = cut;
            c->locked = true;
        }
        else
        {
            right.offsetSamples = c->offsetSamples + (juce::int64) engine.beatsToSamples (cut);
            const juce::ScopedLock sl (engine.lock);
            c->length = cut;
        }
        engine.addClip (*t, std::move (right));
        changed();
        return ok();
    }
    if (cmd == "setNotes")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr || ! c->midi) return fail (juce::String (u8"Región MIDI inexistente"));
        if (! (bool) arg (args, "noUndo", false)) engine.pushUndo();
        std::vector<Note> notes;
        if (auto* list = arg (args, "notes").getArray())
            for (auto& n : *list) notes.push_back (noteFromVar (n));
        const juce::ScopedLock sl (engine.lock);
        c->notes = std::move (notes);
        c->locked = true;       // edited by hand: the drummer won't overwrite it
        t->needsAllNotesOff = true;
        changed();
        return ok();
    }
    if (cmd == "quantize")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr || ! c->midi) return fail (juce::String (u8"Región MIDI inexistente"));
        engine.pushUndo();
        const double grid = juce::jmax (0.0625, (double) arg (args, "grid", 0.25));
        const double strength = juce::jlimit (0.0, 1.0, (double) arg (args, "strength", 1.0));
        const juce::ScopedLock sl (engine.lock);
        for (auto& n : c->notes)
            n.beat += (std::round (n.beat / grid) * grid - n.beat) * strength;
        changed();
        return ok();
    }
    if (cmd == "transposeClip")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr || ! c->midi) return fail (juce::String (u8"Región MIDI inexistente"));
        engine.pushUndo();
        const int semis = (int) arg (args, "semitones", 0);
        const juce::ScopedLock sl (engine.lock);
        for (auto& n : c->notes) n.pitch = juce::jlimit (0, 127, n.pitch + semis);
        t->needsAllNotesOff = true;
        changed();
        return ok();
    }

    if (cmd == "setTake")
    {
        auto* t = trackArg();
        auto* c = t != nullptr ? t->findClip ((int) arg (args, "clipId", 0)) : nullptr;
        if (c == nullptr) return fail (juce::String (u8"Región inexistente"));
        engine.pushUndo();
        auto r = engine.setActiveTake (*t, *c, (int) arg (args, "take", 0));
        changed();
        return fromResult (r);
    }

    //---------------------------------------------------------------- drummer
    if (cmd == "setDrummer")
    {
        auto* t = trackArg();
        if (t == nullptr || t->kind != TrackKind::Drummer) return fail (juce::String (u8"Elige la pista del baterista"));
        if (! (bool) arg (args, "noUndo", false)) engine.pushUndo();
        DrummerSettings d = t->drummer;
        drummerFromVar (d, arg (args, "settings"));
        {
            const juce::ScopedLock sl (engine.lock);
            t->drummer = d;
        }
        engine.regenerateDrummer (*t, (int) arg (args, "clipId", 0));
        changed();
        return ok();
    }
    if (cmd == "arrangeDrummer")
    {
        // Split the drummer's regions into the parts of a whole song, over the same stretch of time
        auto* t = trackArg();
        if (t == nullptr || t->kind != TrackKind::Drummer) return fail (juce::String (u8"Elige la pista del baterista"));
        engine.pushUndo();
        const double bpb = song.beatsPerBar();
        double from = 0, to = 0;
        if (! t->clips.empty())
        {
            from = 1e18;
            for (auto& c : t->clips) { from = juce::jmin (from, c.start); to = juce::jmax (to, c.end()); }
        }
        if (to - from < bpb) { from = 0; to = 16 * bpb; }
        from = std::floor (from / bpb + 1.0e-6) * bpb;
        to = from + juce::jmax (1.0, std::round ((to - from) / bpb)) * bpb;
        engine.arrangeDrummer (*t, from, to - from);
        changed();
        return ok();
    }
    if (cmd == "varyDrummer")
    {
        auto* t = trackArg();
        if (t == nullptr || t->kind != TrackKind::Drummer) return fail (juce::String (u8"Elige la pista del baterista"));
        engine.pushUndo();
        const int only = (int) arg (args, "clipId", 0);
        {
            const juce::ScopedLock sl (engine.lock);
            for (auto& c : t->clips)
                if (only == 0 || c.id == only)
                {
                    c.seed = juce::Random::getSystemRandom().nextInt (100000);
                    if ((bool) arg (args, "unlock", false)) c.locked = false;
                }
        }
        engine.regenerateDrummer (*t, only);
        changed();
        return ok();
    }

    //---------------------------------------------------------------- loops
    if (cmd == "addLoop")
    {
        auto r = engine.addLoop (arg (args, "loopId").toString(), (double) arg (args, "beat", 0.0), (int) arg (args, "trackId", 0));
        changed();
        return fromResult (r);
    }

    //---------------------------------------------------------------- files
    // Never lose work when switching songs: the current one is saved first
    if ((cmd == "newSong" || cmd == "loadSong") && engine.dirty && ! song.tracks.empty())
        engine.saveSong();

    if (cmd == "newSong")   { engine.newSong (arg (args, "name", "").toString()); waveformCache.clear(); changed(); engine.dirty = false; return ok(); }
    if (cmd == "saveSong")
    {
        auto path = arg (args, "path", "").toString();
        auto r = engine.saveSong (path.isNotEmpty() ? juce::File (path) : juce::File());
        touch();
        return fromResult (r);
    }
    if (cmd == "loadSong")
    {
        auto r = engine.loadSong (juce::File (arg (args, "path").toString()));
        waveformCache.clear();
        touch();
        return fromResult (r);
    }
    if (cmd == "importAudio")
    {
        auto r = engine.importAudio ((int) arg (args, "trackId", 0), juce::File (arg (args, "path").toString()), (double) arg (args, "beat", 0.0));
        changed();
        return fromResult (r);
    }
    if (cmd == "importMidi") { auto r = engine.importMidiFile (juce::File (arg (args, "path").toString())); changed(); return fromResult (r); }
    if (cmd == "export")
    {
        auto r = engine.exportMix (juce::File (arg (args, "path").toString()), (int) arg (args, "bitDepth", 24));
        return fromResult (r);
    }

    //---------------------------------------------------------------- undo
    if (cmd == "undo") { const bool done = engine.undo(); touch(); return done ? ok() : fail ("Nada para deshacer"); }
    if (cmd == "redo") { const bool done = engine.redo(); touch(); return done ? ok() : fail ("Nada para rehacer"); }

    //---------------------------------------------------------------- live playing
    if (cmd == "noteOn")  { engine.liveNote ((int) arg (args, "pitch", 60), (float) (double) arg (args, "velocity", 0.8), true); return ok(); }
    if (cmd == "noteOff") { engine.liveNote ((int) arg (args, "pitch", 60), 0.0f, false); return ok(); }

    //---------------------------------------------------------------- devices
    if (cmd == "setAudioSetup") { auto r = setAudioSetup (args); return r.wasOk() ? ok (getAudioSetup()) : fail (r.getErrorMessage()); }
    if (cmd == "setPrefs")
    {
        if (has (args, "recordOffsetMs")) engine.recordOffsetMs = juce::jlimit (-200.0f, 500.0f, (float) (double) arg (args, "recordOffsetMs"));
        if (has (args, "metronomeLevel")) engine.metronomeLevel = juce::jlimit (0.0f, 1.0f, (float) (double) arg (args, "metronomeLevel"));
        return ok();
    }
    if (cmd == "setMidiInput")
    {
        engine.deviceManager.setMidiInputDeviceEnabled (arg (args, "id").toString(), (bool) arg (args, "enabled", true));
        return ok();
    }
    if (cmd == "setTuner")   { engine.tunerEnabled = (bool) arg (args, "on", false); return ok(); }

    //---------------------------------------------------------------- plugins
    if (cmd == "scanPlugins") { engine.plugins.startScan ((bool) arg (args, "full", false)); return ok(); }
    if (cmd == "listPlugins") { return ok (engine.plugins.listToVar()); }

    return fail ("Comando desconocido: " + cmd);
}

//==============================================================================
// Game mode (.feedpak songs)
//==============================================================================
juce::Array<juce::File> EngineApi::gameFolders() const
{
    juce::Array<juce::File> folders;
    auto base = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Galpon");
    if (auto custom = juce::SystemStats::getEnvironmentVariable ("GALPON_GAME_DIR", {}); custom.isNotEmpty())
        base = juce::File (custom);
    folders.add (base.getChildFile ("Juego"));
    for (auto line : juce::StringArray::fromLines (base.getChildFile ("juego-carpetas.txt").loadFileAsString()))
        if (line.trim().isNotEmpty() && juce::File::isAbsolutePath (line.trim())) folders.addIfNotAlreadyThere (juce::File (line.trim()));
    return folders;
}

juce::var EngineApi::gameCall (const juce::String& cmd, const juce::var& args)
{
    auto& game = engine.game;
    if (cmd == "gameLibrary")
    {
        // every .feedpak (or older .sloppak) zip file or folder in the game folders, two levels deep
        juce::Array<juce::var> songs, folders;
        juce::StringArray seen;
        for (auto& folder : gameFolders())
        {
            folders.add (folder.getFullPathName());
            if (! folder.isDirectory()) { folder.createDirectory(); continue; }
            juce::Array<juce::File> found;
            std::function<void (const juce::File&, int)> scan = [&] (const juce::File& dir, int depth)
            {
                for (auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false, "*", juce::File::FollowSymlinks::no))
                {
                    if (f.getFileName().startsWithChar ('.')) continue;
                    if (f.hasFileExtension ("feedpak;sloppak")) found.add (f);
                    else if (f.isDirectory() && depth < 2) scan (f, depth + 1);
                }
            };
            scan (folder, 0);
            for (auto& f : found)
            {
                const auto key = f.getFullPathName();
                if (seen.contains (key)) continue;
                seen.add (key);
                auto& cache = gameSummaryCache[key];
                const auto size = f.isDirectory() ? 0 : f.getSize();
                const auto mod = f.isDirectory() ? f.getChildFile ("manifest.yaml").getLastModificationTime() : f.getLastModificationTime();
                if (cache.summary.isVoid() || cache.size != size || cache.modified != mod)
                {
                    Feedpak p;
                    auto r = p.open (f);
                    if (r.failed())
                    {
                        auto* bad = new juce::DynamicObject();
                        bad->setProperty ("path", key);
                        bad->setProperty ("title", f.getFileNameWithoutExtension());
                        bad->setProperty ("error", r.getErrorMessage());
                        cache.summary = juce::var (bad);
                    }
                    else cache.summary = p.summary (true);
                    cache.size = size;
                    cache.modified = mod;
                }
                songs.add (cache.summary);
            }
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("songs", songs);
        o->setProperty ("folders", folders);
        return ok (juce::var (o));
    }
    if (cmd == "gameAddFolder")
    {
        juce::File folder (arg (args, "path").toString());
        if (! folder.isDirectory()) return fail ("Esa carpeta no existe");
        auto list = gameFolders();
        auto settings = list[0].getParentDirectory().getChildFile ("juego-carpetas.txt");
        juce::StringArray lines = juce::StringArray::fromLines (settings.loadFileAsString());
        lines.removeEmptyStrings();
        lines.addIfNotAlreadyThere (folder.getFullPathName());
        settings.getParentDirectory().createDirectory();
        settings.replaceWithText (lines.joinIntoString ("\n"));
        return ok();
    }
    if (cmd == "gameImport")
    {
        // copies .feedpak files (or folders) into Música › Galpon › Juego
        auto dest = gameFolders()[0];
        dest.createDirectory();
        int n = 0;
        juce::String firstError;
        if (auto* paths = arg (args, "paths", {}).getArray())
            for (auto& pv : *paths)
            {
                juce::File src (pv.toString());
                Feedpak test;
                if (auto r = test.open (src); r.failed()) { if (firstError.isEmpty()) firstError = src.getFileName() + ": " + r.getErrorMessage(); continue; }
                auto target = dest.getChildFile (src.getFileName());
                if (target == src) { ++n; continue; }
                if (target.exists()) target = dest.getNonexistentChildFile (src.getFileNameWithoutExtension(), src.getFileExtension(), false);
                const bool copied = src.isDirectory() ? src.copyDirectoryTo (target) : src.copyFileTo (target);
                if (copied) ++n;
                else if (firstError.isEmpty()) firstError = "No se pudo copiar " + src.getFileName();
            }
        if (n == 0 && firstError.isNotEmpty()) return fail (firstError);
        auto* o = new juce::DynamicObject();
        o->setProperty ("count", n);
        o->setProperty ("error", firstError);
        return ok (juce::var (o));
    }
    if (cmd == "gameOpen")
    {
        // let go of the song that was open first: two songs' audio at once is a lot of memory
        engine.scorer.clearEvents();
        restoreGameTone();
        {
            const juce::ScopedLock sl (engine.lock);
            game.close();
        }
        gamePack.reset();
        auto pack = std::make_unique<Feedpak>();
        if (auto r = pack->open (juce::File (arg (args, "path").toString())); r.failed()) return fail (r.getErrorMessage());
        juce::String error;
        auto data = pack->loadForGame (error);
        if (data["arrangements"].getDynamicObject() == nullptr || data["arrangements"].getDynamicObject()->getProperties().isEmpty())
            return fail (error.isNotEmpty() ? error : juce::String (u8"Esta canción no tiene partes de guitarra o bajo para tocar"));
        // decode the stems (the studio keeps working: this is the message thread, a second or two)
        std::vector<GamePlayer::StemSource> sources;
        juce::StringArray problems;
        if (auto* stemList = pack->manifest()["stems"].getArray())
            for (auto& sv : *stemList)
            {
                GamePlayer::StemSource s;
                s.id = sv["id"].toString();
                s.name = sv["name"].isVoid() ? s.id : sv["name"].toString();
                const auto& dv = sv["default"];
                const auto def = dv.toString().trim().toLowerCase();
                s.enabled = dv.isVoid() || (dv.isBool() ? (bool) dv : ! (def == "off" || def == "false" || def == "0" || def == "no"));
                juce::String err;
                std::shared_ptr<juce::AudioBuffer<float>> audio (pack->readAudio (sv["file"].toString(), s.sampleRate, err).release());
                if (audio == nullptr) { problems.add (s.name + ": " + err); continue; }
                s.audio = audio;
                sources.push_back (std::move (s));
            }
        // older packs keep the full mix under "original_audio:" instead of a "full" stem
        if (std::none_of (sources.begin(), sources.end(), [] (auto& x) { return x.id == "full"; }))
            if (const auto legacy = pack->manifest()["original_audio"].toString().trim(); legacy.isNotEmpty())
            {
                GamePlayer::StemSource s;
                s.id = "full"; s.name = "full"; s.enabled = sources.empty();
                juce::String err;
                std::shared_ptr<juce::AudioBuffer<float>> audio (pack->readAudio (legacy, s.sampleRate, err).release());
                if (audio != nullptr) { s.audio = audio; sources.push_back (std::move (s)); }
            }
        if (sources.empty())
            return fail (juce::String (u8"No pude leer el audio de la canción") + (problems.isEmpty() ? juce::String() : ": " + problems[0]));
        // if the pack marks nothing as on, play the full mix (or everything)
        bool anyOn = false;
        for (auto& s : sources) anyOn |= s.enabled;
        if (! anyOn) for (auto& s : sources) s.enabled = s.id == "full" || std::none_of (sources.begin(), sources.end(), [] (auto& x) { return x.id == "full"; });
        engine.stop();
        game.load (std::move (sources), (double) pack->manifest()["duration"], engine.lock);
        gamePack = std::move (pack);
        gameData = data;
        restoreGameTone();
        if (auto* d = data.getDynamicObject())
        {
            d->setProperty ("stems", game.stemsInfo());
            d->setProperty ("length", game.length());
            d->setProperty ("problems", problems.joinIntoString ("\n"));
        }
        return ok (data);
    }
    if (cmd == "gameClose")
    {
        engine.scorer.clearEvents();
        engine.scorerInput = -1;
        restoreGameTone();
        const juce::ScopedLock sl (engine.lock);
        game.close();
        gamePack.reset();
        return ok();
    }
    if (! game.isLoaded()) return fail ("No hay ninguna canción abierta en el juego");
    if (cmd == "gamePlay")
    {
        if (engine.isRecording()) return fail (juce::String (u8"Primero detén la grabación del estudio"));
        engine.stop();     // the studio's song doesn't play under the game
        if (game.hasEnded()) game.seek (0);
        game.play();
        return ok();
    }
    if (cmd == "gamePause") { game.pause(); return ok(); }
    if (cmd == "gameToggle") { if (game.isPlaying()) game.pause(); else { engine.stop(); if (game.hasEnded()) game.seek (0); game.play(); } return ok(); }
    if (cmd == "gameSeek")  { game.seek ((double) arg (args, "time", 0.0)); return ok(); }
    if (cmd == "gameSpeed") { game.setSpeed ((float) (double) arg (args, "speed", 1.0)); return ok(); }
    if (cmd == "gameLoop")  { game.setLoop ((bool) arg (args, "on", false), (double) arg (args, "a", 0.0), (double) arg (args, "b", 0.0)); return ok(); }
    if (cmd == "gameVolume") { game.setMasterGain ((float) (double) arg (args, "gain", 1.0)); return ok(); }
    if (cmd == "gameStemVolume")
    {
        if (! game.setStemGain (arg (args, "id").toString(), (float) (double) arg (args, "gain", 1.0))) return fail ("Ese audio no existe");
        return ok();
    }
    if (cmd == "gameStem")
    {
        if (! game.setStemEnabled (arg (args, "id").toString(), (bool) arg (args, "on", true))) return fail ("Ese audio no existe");
        return ok (game.stemsInfo());
    }
    if (cmd == "gameNotes")
    {
        // the chart to listen for: [{t, p:[midi…], sus, l (legato), m (muted)}]
        std::vector<NoteScorer::Event> events;
        if (auto* list = arg (args, "events", {}).getArray())
            for (auto& v : *list)
            {
                NoteScorer::Event e;
                e.t = (double) v["t"];
                if (auto* ps = v["p"].getArray()) for (auto& pv : *ps) e.pitches.push_back ((int) pv);
                if (e.pitches.empty()) continue;
                e.sus = (double) v["sus"];
                e.legato = (bool) v["l"];
                e.muted = (bool) v["m"];
                events.push_back (std::move (e));
            }
        gameDetectOffsetMs = (double) arg (args, "offsetMs", 0.0);
        updateScorerInput();
        engine.scorer.setEvents (std::move (events), (float) (double) arg (args, "cents", 0.0));
        auto* o = new juce::DynamicObject();
        o->setProperty ("listening", engine.scorerInput.load() >= 0);
        return ok (juce::var (o));
    }
    if (cmd == "gameTone")      return gameTone (args);
    if (cmd == "gameDetectOffset") { gameDetectOffsetMs = (double) arg (args, "offsetMs", 0.0); updateScorerInput(); return ok(); }
    if (cmd == "gameGuitar")
    {
        // the studio track your guitar comes in through (its effects are your sound)
        Track* t = engine.song.findTrack ((int) arg (args, "trackId", gameGuitarTrackId));
        if (t == nullptr || t->kind != TrackKind::Audio)
        {
            for (auto& tr : engine.song.tracks)
                if (tr->kind == TrackKind::Audio && tr->monitor && tr->id != gameToneTrackId) { t = tr.get(); break; }
        }
        if (t == nullptr && (bool) arg (args, "create", false))
        {
            engine.pushUndo();
            t = engine.addTrack (TrackKind::Audio, juce::String (u8"Guitarra (juego)"));
            for (auto fx : { "gate", "amp", "cab", "reverb" })
                engine.addInsert (t->id, "builtin", fx, -1);
        }
        if (t == nullptr) return ok (juce::var());
        if (! (gameToneOn && t->id == gameMonitorBackup.first))     // with the song's sound on, you're heard through that track
        {
            const juce::ScopedLock sl (engine.lock);
            t->monitor = true;
        }
        gameGuitarTrackId = t->id;
        updateScorerInput();
        engine.dirty = true;
        touch();
        auto* o = new juce::DynamicObject();
        o->setProperty ("trackId", t->id);
        o->setProperty ("name", t->name);
        o->setProperty ("input", t->inputChannel);
        juce::StringArray fx;
        for (auto& s : t->inserts) if (! s.bypass) fx.add (s.name);
        o->setProperty ("sound", fx.joinIntoString (" · "));
        o->setProperty ("volumeDb", t->volumeDb);
        o->setProperty ("inputGainDb", t->inputGainDb);
        return ok (juce::var (o));
    }
    return fail ("Comando del juego desconocido: " + cmd);
}

Tone3000& EngineApi::tone3000()
{
    if (t3 == nullptr)
        t3 = std::make_unique<Tone3000> (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Galpon").getChildFile ("tone3000.json"),
                                         Engine::defaultSongsFolder().getChildFile ("Tone3000"));
    return *t3;
}

void EngineApi::updateScorerInput()
{
    // listen to the guitar track's input, with its input gain, and know how late it arrives
    auto* t = engine.song.findTrack (gameGuitarTrackId);
    if (t == nullptr || t->kind != TrackKind::Audio) { engine.scorerInput = -1; return; }
    engine.scorerGain = juce::Decibels::decibelsToGain (t->inputGainDb);
    engine.scorerInput = t->inputChannel;
    const double sr = juce::jmax (1.0, engine.getSampleRate());
    engine.scorerLatency = engine.getLatencyCompensationSamples() / sr + gameDetectOffsetMs / 1000.0;
}


// ------------------------------------------------------------------ automatic sound per part of the song
namespace
{
    /** 0..1 knob from a rig param (which can be a number or {value, unit}). */
    double num (const juce::var& v, double def)
    {
        if (v.isObject()) return v["value"].isVoid() ? def : (double) v["value"];
        return v.isVoid() ? def : (double) v;
    }
    juce::String lower (const juce::var& v) { return v.toString().toLowerCase(); }
}

/** What a tone needs, in Galpon's own effects. A tone comes from the pack's rigs.json when it has one,
    or else from its name ("Clean", "Lead Drive"…). */
juce::Array<juce::var> EngineApi::chainForTone (const juce::String& rigId, const juce::String& toneName, bool bass, const juce::var& assign)
{
    juce::Array<juce::var> chain;
    auto add = [&chain] (const juce::String& type, std::initializer_list<std::pair<const char*, double>> params, const juce::String& file = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", type);
        auto* p = new juce::DynamicObject();
        for (auto& kv : params) p->setProperty (kv.first, kv.second);
        o->setProperty ("params", juce::var (p));
        if (file.isNotEmpty()) o->setProperty ("file", file);
        chain.add (juce::var (o));
        return o;
    };
    auto fromPack = [this] (const juce::String& ref) -> juce::String
    {
        // a capture or IR shipped inside the pack: copy it out once, so the effect can open it
        if (gamePack == nullptr || ref.isEmpty()) return {};
        juce::MemoryBlock mb;
        if (gamePack->readBytes (ref, mb).failed() || mb.getSize() < 16) return {};
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Galpon").getChildFile ("JuegoSonidos")
                       .getChildFile (juce::String::toHexString ((juce::int64) gamePack->source().getFullPathName().hashCode64()));
        dir.createDirectory();
        auto f = dir.getChildFile (ref.replaceCharacter ('/', '_'));
        if (! f.existsAsFile() || f.getSize() != (juce::int64) mb.getSize()) f.replaceWithData (mb.getData(), mb.getSize());
        return f.getFullPathName();
    };

    // the rig, if the pack describes it
    juce::var rig;
    if (gamePack != nullptr && rigId.isNotEmpty())
    {
        juce::String err;
        auto rigs = gamePack->readJson (gamePack->manifest()["rigs"].toString(), err);
        if (auto* list = rigs["rigs"].getArray())
            for (auto& r : *list) if (r["id"].toString() == rigId) rig = r;
    }
    const auto name = toneName.toLowerCase();
    const juce::String assignedFile = assign["file"].toString();
    const bool assignedFullRig = assign["gear"].toString().contains ("full") || assign["gear"].toString().contains ("cab");

    bool hasAmp = false, ampIsCapture = false, hasCab = false, hasReverb = false;
    if (auto* blocks = rig["blocks"].getArray())
    {
        for (auto& b : *blocks)
        {
            if ((bool) b["bypassed"]) continue;
            const auto role = lower (b["role"]);
            const auto& prm = b["params"];
            const auto family = lower (b["intent"]["family"]) + " " + lower (b["name"]);
            if (role == "drive" || role == "boost" || role == "fuzz" || role == "distortion")
            {
                const double drv = num (prm["drive"], num (prm["gain"], 0.5));
                add ("drive", { { "drive", drv }, { "tone", 800.0 * std::pow (15.0, num (prm["tone"], 0.5)) },
                                { "level", -12.0 + 18.0 * num (prm["level"], 0.6) } });
            }
            else if (role == "amp" && ! hasAmp)
            {
                hasAmp = true;
                juce::String capture = assignedFile;
                if (capture.isEmpty())
                    if (auto* rs = b["realizations"].getArray())
                        for (auto& rz : *rs) if (rz["engine"].toString() == "nam" && capture.isEmpty()) capture = fromPack (rz["ref"].toString());
                if (capture.isNotEmpty() && juce::File (capture).existsAsFile())
                {
                    ampIsCapture = true;
                    add ("nam", {}, capture);
                    if (assignedFullRig) hasCab = true;
                }
                else
                {
                    double gain = num (prm["gain"], -1);
                    if (gain < 0) gain = family.contains ("clean") ? 0.2 : family.contains ("hi-gain") || family.contains ("high") ? 0.8 : 0.5;
                    add ("amp", { { "gain", gain }, { "bass", (num (prm["bass"], 0.5) - 0.5) * 24 }, { "mid", (num (prm["mid"], 0.5) - 0.5) * 24 },
                                  { "treble", (num (prm["treble"], 0.5) - 0.5) * 24 }, { "cab", 1.0 }, { "master", -8.0 } });
                }
            }
            else if (role == "cab")
            {
                if (! ampIsCapture || hasCab) continue;     // Galpon's amp already has its speaker
                hasCab = true;
                juce::String ir;
                if (auto* rs = b["realizations"].getArray())
                    for (auto& rz : *rs) if (rz["engine"].toString() == "ir" && ir.isEmpty()) ir = fromPack (rz["ref"].toString());
                add ("cab", {}, ir);
            }
            else if (role == "delay")
            {
                const auto& tm = prm["time"].isVoid() ? b["realizations"][0]["params"]["time"] : prm["time"];
                double ms = num (tm, 350);
                if (tm["unit"].toString() == "s") ms *= 1000;
                add ("delay", { { "division", 0.0 }, { "ms", juce::jlimit (10.0, 2000.0, ms) }, { "feedback", juce::jlimit (0.0, 0.9, num (prm["feedback"], 0.3)) },
                                { "mix", juce::jlimit (0.0, 1.0, num (b["mix"], num (prm["mix"], 0.25))) } });
            }
            else if (role == "reverb")
            {
                hasReverb = true;
                add ("reverb", { { "size", num (prm["size"], num (prm["decay"], 0.5)) }, { "mix", juce::jlimit (0.0, 1.0, num (b["mix"], num (prm["mix"], 0.25))) } });
            }
            else if (role == "chorus" || role == "modulation" || role == "mod" || role == "flanger" || role == "phaser")
                add ("chorus", { { "rate", 0.2 + 3.0 * num (prm["rate"], 0.3) }, { "depth", num (prm["depth"], 0.4) }, { "mix", num (b["mix"], 0.5) } });
            else if (role == "compressor" || role == "comp")
                add ("comp", {});
        }
    }
    if (! hasAmp)
    {
        // no rig: guess from the tone's name
        if (assignedFile.isNotEmpty() && juce::File (assignedFile).existsAsFile())
        {
            add ("nam", {}, assignedFile);
            if (! assignedFullRig) add ("cab", {});
        }
        else
        {
            double gain = bass ? 0.25 : 0.45;
            if (name.contains ("clean") || name.contains ("limpi") || name.contains ("acoustic") || name.contains ("acústic")) gain = bass ? 0.15 : 0.18;
            else if (name.contains ("crunch") || name.contains ("rhythm") || name.contains ("ritm")) gain = 0.55;
            else if (name.contains ("lead") || name.contains ("solo")) gain = 0.72;
            else if (name.contains ("dist") || name.contains ("drive") || name.contains ("metal") || name.contains ("heavy") || name.contains ("gain") || name.contains ("fuzz")) gain = 0.85;
            if (! bass && (name.contains ("lead") || name.contains ("solo")) && ! name.contains ("clean"))
                add ("drive", { { "drive", 0.3 }, { "tone", 3500.0 }, { "level", 0.0 } });
            add ("amp", { { "gain", gain }, { "bass", bass ? 3.0 : 0.0 }, { "mid", 0.0 }, { "treble", bass ? -2.0 : 0.0 }, { "cab", 1.0 }, { "master", -8.0 } });
            if (! bass && (name.contains ("lead") || name.contains ("solo")))
                add ("delay", { { "division", 0.0 }, { "ms", 380.0 }, { "feedback", 0.25 }, { "mix", 0.18 } });
            if (! bass && (name.contains ("chorus") || name.contains ("clean")))
                add ("chorus", { { "rate", 0.7 }, { "depth", 0.3 }, { "mix", name.contains ("chorus") ? 0.5 : 0.25 } });
        }
    }
    if (! bass && ! hasReverb)
        add ("reverb", { { "size", 0.4 }, { "mix", name.contains ("clean") ? 0.2 : 0.12 } });
    return chain;
}

juce::var EngineApi::gameTone (const juce::var& args)
{
    if (! (bool) arg (args, "on", true))
    {
        restoreGameTone();
        return ok();
    }
    auto* guitar = engine.song.findTrack (gameGuitarTrackId);
    if (guitar == nullptr || guitar->kind != TrackKind::Audio)
        return fail (juce::String (u8"Primero elige por dónde entra tu guitarra («Escuchar mi guitarra»)"));
    const auto arrId = arg (args, "arrangement").toString();
    const auto arr = gameData["arrangements"][juce::Identifier (arrId.isEmpty() ? "x" : arrId)];
    if (! arr.isObject()) return fail ("No encuentro esa parte de la canción");
    const bool bass = arr["type"].toString() == "bass" || arr["name"].toString().containsIgnoreCase ("bass");

    // the tones of this part: the base one, then the changes (spec: tones {base, base_rig, changes[{t,name,rig}]})
    auto tones = arr["tones"];
    if (! tones.isObject())
        if (auto* list = gamePack != nullptr ? gamePack->manifest()["arrangements"].getArray() : nullptr)
            for (auto& a : *list) if (a["id"].toString() == arrId) tones = a["tones"];
    struct Use { double t; juce::String rig, name; };
    std::vector<Use> uses;
    const auto baseName = tones["base"].toString();
    uses.push_back ({ 0.0, tones["base_rig"].toString(), baseName.isNotEmpty() ? baseName : juce::String (bass ? "Bajo" : "Guitarra") });
    if (auto* ch = tones["changes"].getArray())
        for (auto& c : *ch)
            uses.push_back ({ (double) c["t"], c["rig"].toString(), c["name"].toString().isNotEmpty() ? c["name"].toString() : c["rig"].toString() });
    std::sort (uses.begin() + 1, uses.end(), [] (const Use& a, const Use& b) { return a.t < b.t; });

    // the sound comes out of a track of its own, so your own track's effects stay as they were
    restoreGameTone();
    engine.pushUndo();
    auto* t = engine.addTrack (TrackKind::Audio, juce::String (u8"Sonido del juego"));
    if (t == nullptr) return fail ("No se pudo crear la pista");
    guitar = engine.song.findTrack (gameGuitarTrackId);
    {
        const juce::ScopedLock sl (engine.lock);
        t->inputChannel = guitar->inputChannel;
        t->stereoInput = guitar->stereoInput;
        t->inputGainDb = guitar->inputGainDb;
        t->volumeDb = guitar->volumeDb;
        t->monitor = true;
        gameMonitorBackup = { gameGuitarTrackId, guitar->monitor };
        guitar->monitor = false;
    }
    gameToneTrackId = t->id;
    engine.addInsert (t->id, "builtin", "gate", -1);

    gameRigs.clear();
    gameToneSchedule.clear();
    const auto assignAll = arg (args, "assign", {});
    juce::Array<juce::var> report;
    for (auto& u : uses)
    {
        const auto key = u.rig.isNotEmpty() ? u.rig : u.name;
        int rigIndex = -1;
        for (size_t i = 0; i < gameRigs.size(); ++i) if (gameRigs[i].key == key) rigIndex = (int) i;
        if (rigIndex < 0)
        {
            const auto assign = assignAll[juce::Identifier (key.isEmpty() ? "x" : key)];
            auto chain = chainForTone (u.rig, u.name, bass, assign);
            GameRig r;
            r.key = key;
            r.name = u.name;
            r.first = (int) t->inserts.size();
            juce::StringArray what;
            for (auto& c : chain)
            {
                if (engine.addInsert (t->id, "builtin", c["type"].toString(), -1).failed()) continue;
                auto* slot = engine.findSlot (t->id, (int) t->inserts.size() - 1);
                auto* b = slot != nullptr ? dynamic_cast<BuiltinProcessor*> (slot->processor.get()) : nullptr;
                if (b == nullptr) continue;
                if (auto* ps = c["params"].getDynamicObject())
                    for (auto& kv : ps->getProperties()) b->setReal (kv.name.toString(), (float) (double) kv.value);
                if (c["file"].toString().isNotEmpty()) b->setExtra ("file", c["file"]);
                what.add (c["type"].toString() == "nam" ? juce::File (c["file"].toString()).getFileNameWithoutExtension() : slot->name);
            }
            r.count = (int) t->inserts.size() - r.first;
            gameRigs.push_back (r);
            rigIndex = (int) gameRigs.size() - 1;
            auto* o = new juce::DynamicObject();
            o->setProperty ("key", key);
            o->setProperty ("name", u.name);
            o->setProperty ("chain", what.joinIntoString (" · "));
            o->setProperty ("capture", assign["file"].toString().isNotEmpty());
            report.add (juce::var (o));
        }
        gameToneSchedule.push_back ({ u.t, rigIndex });
    }
    gameToneOn = true;
    gameActiveRig = -2;
    applyGameTone();
    engine.dirty = true;
    touch();
    auto* o = new juce::DynamicObject();
    o->setProperty ("trackId", t->id);
    o->setProperty ("tones", report);
    juce::Array<juce::var> times;
    for (auto& sch : gameToneSchedule) times.add (juce::Array<juce::var> { sch.first, gameRigs[(size_t) sch.second].name });
    o->setProperty ("changes", times);
    return ok (juce::var (o));
}

void EngineApi::applyGameTone()
{
    if (! gameToneOn || gameRigs.empty()) return;
    auto* t = engine.song.findTrack (gameToneTrackId);
    if (t == nullptr) { gameToneOn = false; return; }
    const double pos = engine.game.positionSeconds() + 0.06;     // a hair early: the change is ready when the part starts
    int active = gameToneSchedule.empty() ? 0 : gameToneSchedule.front().second;
    for (auto& s : gameToneSchedule) if (s.first <= pos) active = s.second;
    if (active == gameActiveRig) return;
    gameActiveRig = active;
    const juce::ScopedLock sl (engine.lock);
    for (size_t i = 0; i < gameRigs.size(); ++i)
        for (int k = gameRigs[i].first; k < gameRigs[i].first + gameRigs[i].count && k < (int) t->inserts.size(); ++k)
            t->inserts[(size_t) k].bypass = (int) i != active;
}

void EngineApi::restoreGameTone()
{
    if (gameToneTrackId != 0 && engine.song.findTrack (gameToneTrackId) != nullptr)
        engine.deleteTrack (gameToneTrackId);
    if (gameMonitorBackup.first != 0)
        if (auto* g = engine.song.findTrack (gameMonitorBackup.first))
        {
            const juce::ScopedLock sl (engine.lock);
            g->monitor = gameMonitorBackup.second;
        }
    gameMonitorBackup = { 0, false };
    gameToneTrackId = 0;
    gameToneOn = false;
    gameRigs.clear();
    gameToneSchedule.clear();
    gameActiveRig = -1;
    touch();
}

} // namespace galpon
