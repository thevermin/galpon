#include "Model.h"

namespace galpon
{

juce::String toString (TrackKind k)
{
    switch (k)
    {
        case TrackKind::Audio:      return "audio";
        case TrackKind::Instrument: return "instrument";
        case TrackKind::Drummer:    return "drummer";
        case TrackKind::Bus:        return "bus";
    }
    return "instrument";
}

TrackKind trackKindFromString (const juce::String& s)
{
    if (s == "audio")   return TrackKind::Audio;
    if (s == "drummer") return TrackKind::Drummer;
    if (s == "bus")     return TrackKind::Bus;
    return TrackKind::Instrument;
}

juce::var getOrDefault (const juce::var& object, const juce::Identifier& key, const juce::var& fallback)
{
    if (auto* o = object.getDynamicObject())
        if (o->hasProperty (key))
            return o->getProperty (key);
    return fallback;
}

Clip* Track::findClip (int clipId)
{
    for (auto& c : clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

Track* Song::findTrack (int trackId) const
{
    for (auto& t : tracks)
        if (t->id == trackId)
            return t.get();
    return nullptr;
}

int Song::indexOfTrack (int trackId) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i]->id == trackId)
            return (int) i;
    return -1;
}

double Song::lengthInBeats() const
{
    double end = 0.0;
    for (auto& t : tracks)
        for (auto& c : t->clips)
            end = juce::jmax (end, c.end());
    return end;
}

//==============================================================================
juce::var midiEventToVar (const MidiEvent& e)
{
    juce::Array<juce::var> a { e.beat, e.kind, e.number, e.value };
    return a;
}

MidiEvent midiEventFromVar (const juce::var& v)
{
    MidiEvent e;
    if (auto* a = v.getArray(); a != nullptr && a->size() >= 4)
    {
        e.beat   = juce::jmax (0.0, (double) (*a)[0]);
        e.kind   = juce::jlimit (0, 1, (int) (*a)[1]);
        e.number = juce::jlimit (0, 127, (int) (*a)[2]);
        e.value  = juce::jlimit (0, e.kind == MidiEvent::PitchBend ? 16383 : 127, (int) (*a)[3]);
    }
    return e;
}

juce::var noteToVar (const Note& n)
{
    // Compact: [beat, length, pitch, velocity]
    juce::Array<juce::var> a { n.beat, n.length, n.pitch, (double) n.velocity };
    return a;
}

Note noteFromVar (const juce::var& v)
{
    Note n;
    if (auto* a = v.getArray(); a != nullptr && a->size() >= 4)
    {
        n.beat     = (double) (*a)[0];
        n.length   = juce::jmax (0.01, (double) (*a)[1]);
        n.pitch    = juce::jlimit (0, 127, (int) (*a)[2]);
        n.velocity = juce::jlimit (0.0f, 1.0f, (float) (double) (*a)[3]);
    }
    return n;
}

juce::var clipToVar (const Clip& c, bool includeNotes)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.id);
    o->setProperty ("name", c.name);
    o->setProperty ("start", c.start);
    o->setProperty ("length", c.length);
    o->setProperty ("midi", c.midi);
    o->setProperty ("loopLength", c.loopLength);
    o->setProperty ("seed", c.seed);
    o->setProperty ("locked", c.locked);
    o->setProperty ("fill", c.fill);

    if (c.midi)
    {
        if (includeNotes)
        {
            juce::Array<juce::var> notes;
            for (auto& n : c.notes)
                notes.add (noteToVar (n));
            o->setProperty ("notes", notes);
            juce::Array<juce::var> events;
            for (auto& e : c.events)
                events.add (midiEventToVar (e));
            o->setProperty ("events", events);
        }
        o->setProperty ("noteCount", (int) c.notes.size());
        o->setProperty ("eventCount", (int) c.events.size());
    }
    else
    {
        o->setProperty ("audioFile", c.audioFile);
        o->setProperty ("offsetSamples", (double) c.offsetSamples);
        o->setProperty ("gainDb", c.gainDb);
        o->setProperty ("audioMissing", c.audio == nullptr);
        juce::Array<juce::var> takes;
        for (auto& t : c.takes)
        {
            auto* to = new juce::DynamicObject();
            to->setProperty ("audioFile", t.audioFile);
            to->setProperty ("offsetSamples", (double) t.offsetSamples);
            takes.add (juce::var (to));
        }
        o->setProperty ("takes", takes);
        o->setProperty ("activeTake", c.activeTake);
        o->setProperty ("sourceBpm", c.sourceBpm);
        o->setProperty ("flex", flexToVar (c.flex));
    }
    return juce::var (o);
}

void clipFromVar (Clip& c, const juce::var& v)
{
    c.id         = (int) getOrDefault (v, "id", c.id);
    c.name       = getOrDefault (v, "name", c.name).toString();
    c.start      = juce::jmax (0.0, (double) getOrDefault (v, "start", c.start));
    c.length     = juce::jmax (0.0625, (double) getOrDefault (v, "length", c.length));
    c.midi       = (bool) getOrDefault (v, "midi", c.midi);
    c.loopLength = juce::jmax (0.25, (double) getOrDefault (v, "loopLength", c.loopLength));
    c.seed       = (int) getOrDefault (v, "seed", c.seed);
    c.locked     = (bool) getOrDefault (v, "locked", c.locked);
    c.fill       = (bool) getOrDefault (v, "fill", c.fill);

    if (auto notes = getOrDefault (v, "notes", {}); notes.isArray())
    {
        c.notes.clear();
        for (auto& n : *notes.getArray())
            c.notes.push_back (noteFromVar (n));
    }

    if (auto events = getOrDefault (v, "events", {}); events.isArray())
    {
        c.events.clear();
        for (auto& e : *events.getArray())
            c.events.push_back (midiEventFromVar (e));
    }

    if (auto takes = getOrDefault (v, "takes", {}); takes.isArray())
    {
        c.takes.clear();
        for (auto& t : *takes.getArray())
            c.takes.push_back ({ getOrDefault (t, "audioFile", "").toString(),
                                 (juce::int64) (double) getOrDefault (t, "offsetSamples", 0.0) });
    }
    c.activeTake = juce::jlimit (0, juce::jmax (0, (int) c.takes.size() - 1), (int) getOrDefault (v, "activeTake", c.activeTake));

    c.audioFile     = getOrDefault (v, "audioFile", c.audioFile).toString();
    c.offsetSamples = (juce::int64) (double) getOrDefault (v, "offsetSamples", (double) c.offsetSamples);
    c.gainDb        = (float) (double) getOrDefault (v, "gainDb", c.gainDb);
    c.sourceBpm     = (double) getOrDefault (v, "sourceBpm", c.sourceBpm);
    if (auto f = getOrDefault (v, "flex", {}); f.isObject()) flexFromVar (c.flex, f);
}

juce::var flexToVar (const FlexSettings& f)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("on", f.on);
    o->setProperty ("sourceFile", f.sourceFile);
    o->setProperty ("sourceOffset", (double) f.sourceOffset);
    o->setProperty ("sourceBeats", f.sourceBeats);
    o->setProperty ("sourceBpm", f.sourceBpm);
    o->setProperty ("followTempo", f.followTempo);
    o->setProperty ("quantizeGrid", f.quantizeGrid);
    o->setProperty ("quantizeStrength", f.quantizeStrength);
    o->setProperty ("tuneStrength", f.tuneStrength);
    o->setProperty ("tuneChromatic", f.tuneChromatic);
    juce::Array<juce::var> shifts;
    for (auto& [t, st] : f.noteShifts) shifts.add (juce::Array<juce::var> { t, st });
    o->setProperty ("noteShifts", shifts);
    return juce::var (o);
}

void flexFromVar (FlexSettings& f, const juce::var& v)
{
    f.on = (bool) getOrDefault (v, "on", f.on);
    f.sourceFile = getOrDefault (v, "sourceFile", f.sourceFile).toString();
    f.sourceOffset = (juce::int64) (double) getOrDefault (v, "sourceOffset", (double) f.sourceOffset);
    f.sourceBeats = (double) getOrDefault (v, "sourceBeats", f.sourceBeats);
    f.sourceBpm = (double) getOrDefault (v, "sourceBpm", f.sourceBpm);
    f.followTempo = (bool) getOrDefault (v, "followTempo", f.followTempo);
    f.quantizeGrid = juce::jmax (0.0, (double) getOrDefault (v, "quantizeGrid", f.quantizeGrid));
    f.quantizeStrength = juce::jlimit (0.0f, 1.0f, (float) (double) getOrDefault (v, "quantizeStrength", (double) f.quantizeStrength));
    f.tuneStrength = juce::jlimit (0.0f, 1.0f, (float) (double) getOrDefault (v, "tuneStrength", (double) f.tuneStrength));
    f.tuneChromatic = (bool) getOrDefault (v, "tuneChromatic", f.tuneChromatic);
    if (auto s = getOrDefault (v, "noteShifts", {}); s.isArray())
    {
        f.noteShifts.clear();
        for (auto& x : *s.getArray())
            if (auto* a = x.getArray(); a != nullptr && a->size() >= 2)
                f.noteShifts.emplace_back ((double) (*a)[0], juce::jlimit (-24.0f, 24.0f, (float) (double) (*a)[1]));
    }
}

juce::var drummerToVar (const DrummerSettings& d)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("style", d.style);
    o->setProperty ("complexity", d.complexity);
    o->setProperty ("loudness", d.loudness);
    o->setProperty ("fills", d.fills);
    o->setProperty ("swing", d.swing);
    o->setProperty ("humanize", d.humanize);
    o->setProperty ("kickSnare", d.kickSnare);
    o->setProperty ("cymbals", d.cymbals);
    o->setProperty ("toms", d.toms);
    o->setProperty ("percussion", d.percussion);
    o->setProperty ("kickSnareLevel", d.kickSnareLevel);
    o->setProperty ("cymbalLevel", d.cymbalLevel);
    o->setProperty ("percussionLevel", d.percussionLevel);
    o->setProperty ("followTrack", d.followTrack);
    return juce::var (o);
}

void drummerFromVar (DrummerSettings& d, const juce::var& v)
{
    auto f = [&] (const char* k, float cur) { return juce::jlimit (0.0f, 1.0f, (float) (double) getOrDefault (v, k, cur)); };
    auto l = [&] (const char* k, int cur)   { return juce::jlimit (0, 8, (int) getOrDefault (v, k, cur)); };

    d.style      = getOrDefault (v, "style", d.style).toString();
    d.complexity = f ("complexity", d.complexity);
    d.loudness   = f ("loudness", d.loudness);
    d.fills      = f ("fills", d.fills);
    d.swing      = f ("swing", d.swing);
    d.humanize   = f ("humanize", d.humanize);
    d.kickSnare  = (bool) getOrDefault (v, "kickSnare", d.kickSnare);
    d.cymbals    = (bool) getOrDefault (v, "cymbals", d.cymbals);
    d.toms       = (bool) getOrDefault (v, "toms", d.toms);
    d.percussion = (bool) getOrDefault (v, "percussion", d.percussion);
    d.kickSnareLevel  = l ("kickSnareLevel", d.kickSnareLevel);
    d.cymbalLevel     = l ("cymbalLevel", d.cymbalLevel);
    d.percussionLevel = l ("percussionLevel", d.percussionLevel);
    d.followTrack     = (int) getOrDefault (v, "followTrack", d.followTrack);
}

juce::var chordsToVar (const std::vector<ChordSpan>& chords)
{
    juce::Array<juce::var> a;
    for (auto& c : chords)
        a.add (juce::Array<juce::var> { c.beat, c.length, c.root, c.quality });
    return a;
}

std::vector<ChordSpan> chordsFromVar (const juce::var& v)
{
    std::vector<ChordSpan> out;
    if (auto* a = v.getArray())
        for (auto& cv : *a)
            if (auto* x = cv.getArray(); x != nullptr && x->size() >= 4)
            {
                ChordSpan c;
                c.beat = (double) (*x)[0];
                c.length = juce::jmax (0.25, (double) (*x)[1]);
                c.root = ((int) (*x)[2] % 12 + 12) % 12;
                c.quality = juce::jlimit (0, 3, (int) (*x)[3]);
                out.push_back (c);
            }
    return out;
}

juce::var automationToVar (const std::vector<AutomationLane>& lanes)
{
    juce::Array<juce::var> a;
    for (auto& l : lanes)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("target", l.target);
        o->setProperty ("on", l.on);
        juce::Array<juce::var> pts;
        pts.ensureStorageAllocated ((int) l.points.size());
        for (auto& p : l.points) pts.add (juce::Array<juce::var> { p.beat, p.value });
        o->setProperty ("points", pts);
        a.add (juce::var (o));
    }
    return a;
}

std::vector<AutomationLane> automationFromVar (const juce::var& v)
{
    std::vector<AutomationLane> out;
    if (auto* a = v.getArray())
        for (auto& lv : *a)
        {
            AutomationLane l;
            l.target = getOrDefault (lv, "target", "").toString();
            l.on = (bool) getOrDefault (lv, "on", true);
            if (l.target.isEmpty()) continue;
            l.parseTarget();
            if (auto* pts = getOrDefault (lv, "points", {}).getArray())
                for (auto& pv : *pts)
                    if (auto* p = pv.getArray(); p != nullptr && p->size() >= 2)
                    {
                        const float value = (float) (double) (*p)[1];
                        if (std::isfinite (value)) l.points.push_back ({ juce::jmax (0.0, (double) (*p)[0]), value });
                    }
            std::stable_sort (l.points.begin(), l.points.end(), [] (const AutoPoint& x, const AutoPoint& y) { return x.beat < y.beat; });
            out.push_back (std::move (l));
        }
    return out;
}

juce::var sendsToVar (const std::vector<Send>& sends)
{
    juce::Array<juce::var> a;
    for (auto& s : sends)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("busId", s.busId);
        o->setProperty ("levelDb", s.levelDb);
        o->setProperty ("pre", s.pre);
        a.add (juce::var (o));
    }
    return a;
}

std::vector<Send> sendsFromVar (const juce::var& v)
{
    std::vector<Send> out;
    if (auto* a = v.getArray())
        for (auto& sv : *a)
        {
            Send s;
            s.busId = (int) getOrDefault (sv, "busId", 0);
            s.levelDb = juce::jlimit (-80.0f, 6.0f, (float) (double) getOrDefault (sv, "levelDb", -12.0));
            s.pre = (bool) getOrDefault (sv, "pre", false);
            if (s.busId > 0) out.push_back (s);
        }
    return out;
}

juce::String colourForKind (TrackKind k, int index)
{
    if (k == TrackKind::Drummer)
        return "#E6A84A";
    if (k == TrackKind::Bus)
        return "#94A3B8";

    static const char* palette[] = { "#60A5FA", "#4ADE80", "#F472B6", "#C084FC", "#2DD4BF", "#FB923C" };
    return palette[(size_t) index % std::size (palette)];
}

} // namespace galpon
