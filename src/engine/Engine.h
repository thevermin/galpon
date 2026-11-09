#pragma once

#include "Model.h"
#include "Builtins.h"
#include "PluginHost.h"
#include "Flex.h"
#include "Recorder.h"
#include "GamePlayer.h"
#include "NoteScorer.h"
#include <map>

namespace galpon
{

/** The audio engine: owns the song, talks to the audio interface, plays, records and mixes.

    Threading: everything here is called from the message thread, except the device callbacks.
    Edits take `lock` briefly; the audio thread only ever *tries* the lock, so it never waits
    (if an edit is happening at that instant it outputs one block of silence). */
class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback,
               public juce::AudioPlayHead,
               private juce::Timer
{
public:
    explicit Engine (const juce::File& appDataFolder, bool openAudioDevices = true);
    ~Engine() override;

    //==========================================================================
    // Devices
    juce::AudioDeviceManager deviceManager;
    void saveDeviceSettings();
    juce::File getAppDataFolder() const { return appData; }
    double getSampleRate() const noexcept { return sampleRate; }
    int    getBlockSize() const noexcept  { return blockSize; }
    int    getLatencyCompensationSamples() const;
    float  recordOffsetMs = 0.0f;           // manual extra compensation, if the interface lies about its latency
    float  metronomeLevel = 0.6f;

    /** Prepare without an audio device (tests, offline). */
    void prepareOffline (double sampleRate, int blockSize);

    //==========================================================================
    // Song
    Song song;
    juce::CriticalSection lock;
    juce::File songFolder;
    bool dirty = false;

    void newSong (const juce::String& name = {});
    juce::Result saveSong (const juce::File& folder = {});
    juce::Result loadSong (const juce::File& folder);
    static juce::File defaultSongsFolder();

    //==========================================================================
    // Transport
    void play();
    void stop();
    /** Starts (or stops) recording. Fails, with a reason, if it can't record safely. */
    juce::Result record();

    /** Recording health, for the interface: audio that couldn't be written (should be 0). */
    juce::int64 recordingDroppedSamples() const;
    /** While recording: the tracks being recorded and where the take starts (beats). Message thread. */
    std::vector<int> recordingTrackIds() const;
    double recordStartBeats() const noexcept { return samplesToBeats ((double) recordStartSample); }
    bool recordingDiskFailed() const;
    /** Set by the audio thread when the interface stops in the middle of a recording. */
    std::atomic<bool> deviceLostWhileRecording { false };
    /** Message thread: called often; stops a recording whose interface disappeared. */
    juce::String checkRecordingHealth();

    /** Recordings in the song's Audio folder that no region uses (e.g. after a crash). */
    juce::StringArray findLostRecordings();
    void clearHistory()     { undoStack.clear(); redoStack.clear(); }
    void clearAudioCache()  { audioCache.clear(); }
    /** Puts lost recordings on new tracks (repairing cut-off files first). */
    int recoverLostRecordings (const juce::StringArray& files);
    void setPositionBeats (double beats);
    double getPositionBeats() const noexcept;
    bool isPlaying() const noexcept   { return playing.load(); }
    bool isRecording() const noexcept { return recording.load(); }
    bool isCountingIn() const noexcept;

    //==========================================================================
    // Tracks & processors
    Track* addTrack (TrackKind, const juce::String& preset = {});
    void   deleteTrack (int trackId);
    void   moveTrack (int trackId, int newIndex);
    juce::Result setInstrument (Track&, const juce::String& kind, const juce::String& typeOrPluginId, const juce::String& preset = {});
    juce::Result addInsert (int trackId, const juce::String& kind, const juce::String& typeOrPluginId, int index = -1);   // trackId 0 = master
    void   removeInsert (int trackId, int index);
    void   moveInsert (int trackId, int from, int to);
    std::vector<Slot>* insertsFor (int trackId);
    Slot*  findSlot (int trackId, int slotIndex);    // slotIndex -1 = instrument

    void   regenerateDrummer (Track&, int onlyClipId = 0);
    void   regenerateAllDrummers();
    /** Splits [start, start + length) of a drummer track into the parts of a whole song (intro,
        verse, chorus, verse, chorus, bridge, chorus, ending), one region each, without making it longer. */
    void   arrangeDrummer (Track&, double start, double lengthBeats);
    static std::vector<std::pair<juce::String, int>> songPlan (int bars);
    /** The track the drummer listens to (its "follow" choice, or the automatic one). */
    Track* drummerFollowTrack (const Track& drummer) const;
    Clip&  addClip (Track&, Clip clip);
    juce::Result importAudio (int trackId, const juce::File& file, double atBeat);
    juce::Result importMidiFile (const juce::File& file);
    juce::Result addLoop (const juce::String& loopId, double atBeat, int trackId = 0);

    std::atomic<int> selectedTrackId { 0 };

    //==========================================================================
    // Live input from the UI (on-screen / computer keyboard)
    void liveNote (int pitch, float velocity, bool noteOn);
    void liveMessage (const juce::MidiMessage&);   // pedal, pitch bend, knobs

    //==========================================================================
    // Offline rendering
    /** .wav / .aiff / .flac directly; .mp3 (built-in encoder) and .m4a (AAC, Mac) via a temporary WAV. */
    juce::Result exportMix (const juce::File& file, int bitDepth = 24, double startBeat = 0.0, double endBeat = -1.0,
                            std::function<void (float)> progress = {});
    static juce::Result encodeMp3 (const juce::File& wav, const juce::File& mp3, int kbps = 256);
    static juce::Result encodeM4a (const juce::File& wav, const juce::File& m4a, int kbps = 256);

    /** Renders part of the song to a buffer (only the tracks `include` accepts). */
    juce::AudioBuffer<float> renderRange (double startBeat, double endBeat, const std::function<bool (const Track&)>& include = {});

    /** Chord ID: finds the chords of a region (or of the whole song when clipId is 0)
        and puts them on the chord track. */
    juce::Result detectChords (int trackId, int clipId);
    /** Makes a piano track that plays the chord track. */
    Track* chordsToTrack();

    /** Flex: what's in an audio region (notes and pitch curve, hits) for the editor. */
    juce::var flexAnalyze (int trackId, int clipId);
    /** Flex: renders the region again from its original recording with these settings
        (followTempo, quantizeGrid, quantizeStrength, tuneStrength, tuneChromatic, noteShifts). */
    juce::Result flexApply (int trackId, int clipId, const juce::var& settings);
    /** Flex: back to the original recording. */
    juce::Result flexReset (int trackId, int clipId);

    /** Game mode: the song being played (stems from a .feedpak). */
    GamePlayer game;
    /** Game mode: listens to the guitar's input channel and judges the notes. */
    NoteScorer scorer;
    std::atomic<int> scorerInput { -1 };          // input channel of the guitar track (-1: not listening)
    std::atomic<float> scorerGain { 1.0f };
    std::atomic<double> scorerLatency { 0.0 };    // seconds from hearing the song to your note reaching Galpon
    std::vector<float> scorerScratch;
    /** Seconds between the audio leaving Galpon and reaching the ears (for drawing in sync). */
    double outputLatencySeconds() const;

    /** Records a control movement into automation (while playing, with the track's "write" on). */
    void writeAutomation (Track&, const juce::String& target, float value);
    void endAutomationWrites() { automationWriteState.clear(); }

    /** Switches an audio region to another recorded pass. */
    juce::Result setActiveTake (Track&, Clip&, int takeIndex);
    /** Renders `numSamples` from the current position (no device needed). Used by export and tests. */
    void renderOffline (juce::AudioBuffer<float>& out, int numSamples);

    /** What the audio device calls (also used by tests to feed fake input). */
    void renderBlock (float* const* out, int numOut, const float* const* in, int numIn, int n);

    //==========================================================================
    // Undo (clips, notes, mix settings, drummer, song settings)
    void pushUndo();
    bool undo();
    bool redo();

    //==========================================================================
    // Meters / status for the UI
    struct Meter { float l = 0, r = 0; };
    Meter getMasterMeter() const noexcept  { return { masterPeakL.load(), masterPeakR.load() }; }
    void  resetMasterMeter() noexcept      { masterPeakL = 0.0f; masterPeakR = 0.0f; }
    juce::var getTunerReading();
    std::atomic<bool> tunerEnabled { false };
    juce::String lastError;
    double getCpu() { return deviceManager.getCpuUsage(); }

    PluginHost plugins;
    std::function<void()> onSongChanged;   // e.g. after recording finished
    std::function<void (const juce::String&)> onStatus;

    double beatsToSamples (double beats) const noexcept { return beats * 60.0 / song.bpm * sampleRate; }
    double samplesToBeats (double samples) const noexcept { return samples / sampleRate * song.bpm / 60.0; }

    juce::File getAudioFolder() const { return songFolder.getChildFile ("Audio"); }
    std::shared_ptr<const juce::AudioBuffer<float>> loadAudio (const juce::String& fileName);

    // AudioPlayHead
    juce::Optional<PositionInfo> getPosition() const override { return currentPosition; }

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;

    void prepareAll (double sr, int bs);
    void prepareProcessor (juce::AudioProcessor&);
    void renderSegment (int offset, int len, const float* const* in, int numIn, bool firstSegment);
    void processSlot (juce::AudioProcessor&, juce::AudioBuffer<float>& view, juce::MidiBuffer& midi);
    void collectMidi (Track&, double beat0, double beat1, juce::int64 segStart, int len, juce::MidiBuffer&);
    void mixAudioClips (Track&, juce::int64 segStart, int len, juce::AudioBuffer<float>& view);
    void addMetronome (juce::int64 segStart, int len, int offset);
    void finishRecording();
    void sendAllNotesOff();
    void reloadAudioForSampleRate();
    juce::var snapshot() const;
    void restore (const juce::var&);
    juce::var slotToVar (const Slot&) const;
    void slotFromVar (Slot&, const juce::var&);
    juce::File uniqueSongFolder (const juce::String& name) const;

    // Flex: the original recording, analysed once (cached by file and range)
    struct FlexSource
    {
        juce::AudioBuffer<float> audio;
        std::vector<float> mono;
        std::vector<flex::PitchFrame> frames;
        std::vector<flex::PitchNote> notes;
        std::vector<double> onsets;
        bool pitchDone = false, onsetsDone = false;
    };
    struct FlexPlan
    {
        flex::TimeMap map;
        int srcLen = 0, outLen = 0;
        struct Segment { double start, end; float semitones; };   // in source samples
        std::vector<Segment> pitch;
        std::vector<float> noteCorrection;                        // per source note
    };
    std::map<juce::String, std::shared_ptr<FlexSource>> flexCache;
    std::shared_ptr<FlexSource> flexSourceFor (const FlexSettings&, bool needPitch, bool needOnsets);
    FlexPlan planFlex (const Clip&, const FlexSettings&, FlexSource&);

    void remapFxAutomation (int trackId, const std::function<int (int)>& newSlot);
    struct WriteState { bool active = false; double lastBeat = 0; };
    std::map<int, WriteState> automationWriteState;

    juce::int64 beginOffline (double startBeat);
    void endOffline (juce::int64 savedPos);
    void renderLocked (float* const* out, int numOut, const float* const* in, int numIn, int n);
    void prepareTrack (Track&);
    int  trackLatency (const Track&) const;
    void closeOpenMidiNotes (double atBeat);
    void loadSettings();
    void saveSettings() const;

    juce::File appData;
    juce::AudioFormatManager audioFormats;
    std::atomic<bool> offlineBusy { false };
    double sampleRate = 48000.0;
    int    blockSize  = 512;
    bool   offline    = false;

    std::atomic<bool> playing { false }, recording { false };
    std::atomic<juce::int64> position { 0 };         // in samples, can be negative during count-in
    juce::int64 recordStartSample = 0;
    juce::Optional<PositionInfo> currentPosition;

    juce::AudioBuffer<float> master, scratch;
    /** Keeps the master from clipping. When it has to lower the level it lowers everything, so in
        the game a loud guitar on top of a loud song pulls the song down: `floorGain` caps how far. */
    struct MasterLimiter
    {
        float gain = 1.0f, release = 0.001f;
        std::atomic<float> floorGain { 0.0f };   // 0 = as far as needed; 1 = never lower (it softly clips instead)
        static constexpr float ceiling = 0.891f; // -1 dB: room for MP3s, which overshoot
        void prepare (double sr) { release = 1.0f - std::exp (-1.0f / (0.12f * (float) sr)); gain = 1.0f; }
        void reset() { gain = 1.0f; }
        void process (juce::AudioBuffer<float>& b, int n)
        {
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            const float floor = floorGain.load();
            for (int i = 0; i < n; ++i)
            {
                const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
                float need = peak > ceiling ? ceiling / peak : 1.0f;
                need = std::max (need, floor);
                gain = need < gain ? need : gain + (need - gain) * release;   // instant down, 120 ms back up
                l[i] = soft (l[i] * gain);
                r[i] = soft (r[i] * gain);
            }
        }
        static float soft (float x)
        {
            const float a = std::abs (x);
            if (a <= ceiling) return x;
            const float knee = 1.0f - ceiling;
            return std::copysign (ceiling + knee * std::tanh ((a - ceiling) / knee), x);
        }
    };
    MasterLimiter limiter;
public:
    /** Game mode: how many dB your guitar may pull the song down when you play loud (0 = none). */
    void setDuckLimitDb (float db) { limiter.floorGain = db >= 40.0f ? 0.0f : juce::Decibels::decibelsToGain (-juce::jmax (0.0f, db)); }
private:
    int maxTrackLatency = 0;
    bool cycleRecording = false;
    int autoArmedTrackId = 0;
public:
    juce::StringArray ignoredRecordings;   // lost recordings the user chose not to recover
private:
    std::atomic<float> masterPeakL { 0 }, masterPeakR { 0 };
    double metronomePhase = 0.0, metronomeEnv = 0.0, metronomeFreq = 1000.0;

    juce::MidiMessageCollector liveMidi;
    juce::MidiBuffer liveBlock, emptyMidi;

    // Recording
    juce::TimeSliceThread writerThread { "Galpon recorder" };
    struct AudioTake
    {
        int trackId = 0;
        juce::File file;
        std::unique_ptr<TakeWriter> writer;
        int channel = 0, numChannels = 1;
    };
    std::vector<AudioTake> audioTakes;
    struct MidiTake
    {
        int trackId = 0;
        std::vector<Note> notes;
        std::vector<MidiEvent> events;
        std::array<double, 128> openBeat {};
        std::array<float, 128> openVel {};
        std::array<bool, 128> open {};
    };
    std::vector<MidiTake> midiTakes;

    // Tuner
    std::array<float, 4096> tunerRing {};
    std::atomic<int> tunerWrite { 0 };

    // Audio cache, by file name inside the song's Audio folder
    std::map<juce::String, std::shared_ptr<const juce::AudioBuffer<float>>> audioCache;

    std::vector<juce::var> undoStack, redoStack;
    std::map<int, std::unique_ptr<Track>> graveyard;   // deleted tracks, kept so "undo" can bring them back
    int autosaveCountdown = 0;
};

} // namespace galpon
