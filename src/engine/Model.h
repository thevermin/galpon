#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <vector>

namespace galpon
{

enum class TrackKind { Audio, Instrument, Drummer, Bus };   // Bus = auxiliary track fed by sends

juce::String toString (TrackKind);
TrackKind trackKindFromString (const juce::String&);

//==============================================================================
/** One MIDI note inside a clip. Times are in beats, relative to the clip start. */
struct Note
{
    double beat   = 0.0;
    double length = 0.25;
    int    pitch  = 60;
    float  velocity = 0.8f;
};

//==============================================================================
/** A non-note MIDI event inside a clip: sustain pedal, mod wheel, any knob, pitch bend. */
struct MidiEvent
{
    enum Kind { Controller = 0, PitchBend = 1 };
    double beat   = 0.0;
    int    kind   = Controller;
    int    number = 64;      // controller number (64 = sustain pedal, 1 = mod wheel)
    int    value  = 0;       // 0..127 for controllers, 0..16383 for pitch bend (8192 = centre)
};

/** One recorded pass of an audio region (cycle recording keeps every pass). */
struct Take
{
    juce::String audioFile;
    juce::int64  offsetSamples = 0;
};

//==============================================================================
/** Flex (like Logic's Flex Time / Flex Pitch): the region plays a version rendered from the
    original recording, so every change can be redone or undone without losing quality. */
struct FlexSettings
{
    bool         on = false;
    juce::String sourceFile;              // the original recording
    juce::int64  sourceOffset = 0;
    double       sourceBeats = 0;         // length of the original, in beats at sourceBpm
    double       sourceBpm = 0;
    bool         followTempo = false;     // stretch with the song's tempo
    double       quantizeGrid = 0;        // beats (0 = off)
    float        quantizeStrength = 1.0f;
    float        tuneStrength = 0.0f;     // 0..1 automatic tuning
    bool         tuneChromatic = false;   // false: notes of the song's key
    std::vector<std::pair<double, float>> noteShifts;   // note start (s, in the original) -> semitones
};

//==============================================================================
/** A region on the timeline: either MIDI notes or a piece of recorded/imported audio. */
struct Clip
{
    int          id = 0;
    juce::String name;
    double       start  = 0.0;   // beats from song start
    double       length = 4.0;   // beats
    bool         midi   = true;

    // MIDI clips: notes repeat every `loopLength` beats when the clip is stretched longer.
    std::vector<Note> notes;
    std::vector<MidiEvent> events;   // pedal, pitch bend, knobs
    double       loopLength = 4.0;

    // Drummer clips are generated from the track's drummer settings unless `locked`.
    int          seed   = 1;
    bool         locked = false;
    bool         fill   = true;

    // Audio clips
    juce::String audioFile;                                  // file name inside the song's Audio folder
    std::shared_ptr<const juce::AudioBuffer<float>> audio;   // already at the engine sample rate
    juce::int64  offsetSamples = 0;                          // where in the file the clip starts
    float        gainDb = 0.0f;
    std::vector<Take> takes;                                 // all passes; audioFile/offset mirror the active one
    int          activeTake = 0;
    double       sourceBpm = 0;                              // tempo when it was recorded (0 = unknown)
    FlexSettings flex;

    double end() const noexcept { return start + length; }
};

//==============================================================================
/** Deletes processors, but first tells whoever cares (e.g. so a plugin's window can close
    before the plugin disappears). */
struct ProcessorDeleter
{
    static std::function<void (juce::AudioProcessor&)>& hook()
    {
        static std::function<void (juce::AudioProcessor&)> h;
        return h;
    }

    void operator() (juce::AudioProcessor* p) const
    {
        if (p == nullptr) return;
        if (auto& h = hook()) h (*p);
        delete p;
    }
};

using ProcessorPtr = std::unique_ptr<juce::AudioProcessor, ProcessorDeleter>;

template <typename T>
ProcessorPtr adoptProcessor (std::unique_ptr<T> p) { return ProcessorPtr (p.release()); }

//==============================================================================
/** A processor in a track: the instrument, or one of the effects. Built-in or plugin. */
struct Slot
{
    juce::String kind;      // "builtin" or "plugin"
    juce::String type;      // built-in type id, or the plugin's identifier string
    juce::String name;      // what the user sees
    juce::String preset;    // built-in preset name (informational)
    bool         bypass = false;
    juce::PluginDescription description;   // plugins only
    ProcessorPtr processor;
    juce::String savedState; // base64, used when the processor could not be created (e.g. plugin missing)

    bool isEmpty() const noexcept { return kind.isEmpty(); }
};

//==============================================================================
/** Automation: how a control changes over the song (volume rides, a filter opening…).
    target: "volume" (dB), "pan" (-1..1), "send:<busId>" (dB), "fx:<slot>:<param>" (0..1) */
struct AutoPoint { double beat = 0; float value = 0; };
struct AutomationLane
{
    juce::String target;
    std::vector<AutoPoint> points;    // sorted by beat
    bool on = true;

    // the target, parsed once (the audio thread never touches strings)
    enum Kind { Unknown, Volume, Pan, SendLevel, FxParam } kind = Unknown;
    int a = 0, b = 0;      // send: bus id; fx: slot, parameter index
    float lastApplied = -1.0e9f;

    void parseTarget()
    {
        kind = Unknown;
        if (target == "volume") kind = Volume;
        else if (target == "pan") kind = Pan;
        else if (target.startsWith ("send:")) { kind = SendLevel; a = target.substring (5).getIntValue(); }
        else if (target.startsWith ("fx:"))
        {
            auto parts = juce::StringArray::fromTokens (target.substring (3), ":", "");
            if (parts.size() == 2) { kind = FxParam; a = parts[0].getIntValue(); b = parts[1].getIntValue(); }
        }
    }

    /** Value at a beat: straight lines between points, flat before the first and after the last. */
    float valueAt (double beat) const noexcept
    {
        if (points.empty()) return 0.0f;
        if (beat <= points.front().beat) return points.front().value;
        if (beat >= points.back().beat) return points.back().value;
        // binary search: lanes can have thousands of points when movements are recorded
        auto it = std::upper_bound (points.begin(), points.end(), beat, [] (double b, const AutoPoint& p) { return b < p.beat; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const double span = b.beat - a.beat;
        return span <= 0 ? b.value : (float) (a.value + (b.value - a.value) * (beat - a.beat) / span);
    }
    bool active() const noexcept { return on && ! points.empty(); }
};

//==============================================================================
/** A send: a copy of a track's sound going to a bus (e.g. a shared reverb). */
struct Send
{
    int   busId   = 0;
    float levelDb = -12.0f;
    bool  pre     = false;   // before the track's volume fader (Logic's "pre-fader")
};

//==============================================================================
/** A chord on the song's chord track (found by "Identificar acordes", or typed in). */
struct ChordSpan
{
    double beat = 0.0, length = 4.0;
    int    root = 0;        // 0 = C/Do … 11 = B/Si
    int    quality = 0;     // 0 major, 1 minor, 2 dominant 7, 3 minor 7
};

/** A part of the song marked on the "Partes" strip (intro, verse, chorus…). */
struct SongSection
{
    double beat = 0.0, length = 16.0;
    juce::String kind = "verse";   // intro, verse, prechorus, chorus, bridge, solo, outro
};

//==============================================================================
struct DrummerSettings
{
    juce::String style = "rock";
    float complexity = 0.4f;   // 0 = simple, 1 = complex
    float loudness   = 0.6f;   // 0 = soft,   1 = loud
    float fills      = 0.35f;
    float swing      = 0.0f;
    float humanize   = 0.5f;
    bool  kickSnare  = true;
    bool  cymbals    = true;
    bool  toms       = true;
    bool  percussion = false;
    int   kickSnareLevel = 5;  // 1..8
    int   cymbalLevel    = 3;
    int   percussionLevel = 3;
    int   followTrack    = -1; // track the drummer listens to: its id, 0 = nobody, -1 = automatic (the recorded guitar or bass)
};

//==============================================================================
struct Track
{
    int          id = 0;
    TrackKind    kind = TrackKind::Instrument;
    juce::String name;
    juce::String colour = "#4ADE80";
    float        volumeDb = 0.0f;
    float        pan      = 0.0f;   // -1..1
    bool         mute = false, solo = false;
    bool         armed = false, monitor = false;
    int          inputChannel = 0;  // first input channel (0-based)
    bool         stereoInput  = false;
    bool         collapsed    = false;
    float        inputGainDb  = 0.0f;   // audio tracks: boost or cut what comes in (monitor, recording, tuner)

    Slot              instrument;
    std::vector<Slot> inserts;
    std::vector<Send> sends;
    int               output = 0;   // 0 = master, otherwise the id of a bus
    std::vector<AutomationLane> automation;
    bool              autoWrite = false;   // moving a control while playing records it

    AutomationLane* findLane (const juce::String& target)
    {
        for (auto& l : automation) if (l.target == target) return &l;
        return nullptr;
    }
    const AutomationLane* activeLane (const juce::String& target) const
    {
        for (auto& l : automation) if (l.target == target && l.active()) return &l;
        return nullptr;
    }
    DrummerSettings   drummer;
    std::vector<Clip> clips;

    // ---- runtime only ----
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer         midi;
    std::atomic<float>       peakL { 0.0f }, peakR { 0.0f };
    std::atomic<bool>        needsAllNotesOff { false };
    std::atomic<float>       inputPeak { 0.0f };       // level arriving at the input, after the gain
    juce::AudioBuffer<float> delayLine;                // plugin delay compensation
    int                      delayWrite = 0;

    Clip* findClip (int clipId);
};

//==============================================================================
struct Song
{
    juce::String name = juce::String (u8"Canción nueva");
    double bpm = 120.0;
    int    sigNum = 4, sigDen = 4;
    juce::String key = "C";
    bool   minor = false;

    bool   cycle = false;
    double cycleStart = 0.0, cycleEnd = 16.0;   // beats
    bool   metronome = true;
    bool   countIn   = true;
    float  masterDb  = 0.0f;
    bool   limiter   = true;    // keeps the mix from clipping

    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<Slot> masterInserts;
    std::vector<ChordSpan> chords;
    std::vector<SongSection> sections;
    int nextId = 1;

    int newId() noexcept { return nextId++; }
    Track* findTrack (int trackId) const;
    int indexOfTrack (int trackId) const;
    double beatsPerBar() const noexcept { return sigNum * 4.0 / sigDen; }
    double lengthInBeats() const;   // end of the last clip
};

//==============================================================================
// Serialisation helpers (the processors themselves are saved via their own state)
juce::var midiEventToVar (const MidiEvent&);
MidiEvent midiEventFromVar (const juce::var&);
juce::var noteToVar (const Note&);
Note      noteFromVar (const juce::var&);
juce::var clipToVar (const Clip&, bool includeNotes = true);
void      clipFromVar (Clip&, const juce::var&);
juce::var flexToVar (const FlexSettings&);
void      flexFromVar (FlexSettings&, const juce::var&);
juce::var drummerToVar (const DrummerSettings&);
void      drummerFromVar (DrummerSettings&, const juce::var&);

juce::var chordsToVar (const std::vector<ChordSpan>&);
std::vector<ChordSpan> chordsFromVar (const juce::var&);
juce::var sectionsToVar (const std::vector<SongSection>&);
std::vector<SongSection> sectionsFromVar (const juce::var&);
juce::var automationToVar (const std::vector<AutomationLane>&);
std::vector<AutomationLane> automationFromVar (const juce::var&);
juce::var sendsToVar (const std::vector<Send>&);
std::vector<Send> sendsFromVar (const juce::var&);

juce::var getOrDefault (const juce::var& object, const juce::Identifier& key, const juce::var& fallback);

juce::String colourForKind (TrackKind, int index);

} // namespace galpon
