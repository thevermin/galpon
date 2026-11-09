#pragma once

#include "Engine.h"
#include "SoundLibrary.h"
#include "Stems.h"
#include "Feedpak.h"
#include "Tone3000.h"

namespace galpon
{

/** Turns commands from the interface ("addTrack", "setParam", ...) into engine calls,
    and describes the song back to the interface as JSON. Message thread only. */
class EngineApi
{
public:
    explicit EngineApi (Engine&);

    /** Runs a command. Always returns an object with "ok" and, on failure, "error". */
    /** Runs a command from the interface. Any failure inside comes back as an error message:
        a command never takes the app down. */
    juce::var call (const juce::String& command, const juce::var& args);
    /** The last commands run (newest last), for the crash report. Safe to read from a crash handler. */
    static juce::String recentCommands();

    juce::var getState();
    juce::var getTick();
    juce::var getAudioSetup();

    /** Goes up whenever the song changes, so the UI knows to fetch the state again. */
    int getStateVersion() const noexcept { return stateVersion; }
    void touch() noexcept { ++stateVersion; }

    // Things the app shell provides (windows, dialogs)
    std::function<juce::Result (int trackId, int slotIndex)> openPluginEditor;

private:
    /** Puts the recorded acoustic kit on a track (when it has been downloaded). */
    juce::Result useRealDrums (Track&);
    juce::var callUnchecked (const juce::String& command, const juce::var& args);
    juce::var slotInfo (const Slot&) const;
    juce::var paramsFor (int trackId, int slot);
    juce::var waveform (const juce::String& file, int points);
    juce::var library();
    juce::var recentSongs();
    juce::Result setAudioSetup (const juce::var&);

    Engine& engine;
    SoundLibrary sounds;
    StemSeparator stems;
    void applyStems();

    // game mode
    juce::var gameCall (const juce::String& cmd, const juce::var& args);
    juce::Array<juce::File> gameFolders() const;
    std::unique_ptr<Feedpak> gamePack;
    struct CachedSummary { juce::int64 size = 0; juce::Time modified; juce::var summary; };
    std::map<juce::String, CachedSummary> gameSummaryCache;
    int gameGuitarTrackId = 0;
    std::unique_ptr<Tone3000> t3;
    Tone3000& tone3000();
    juce::var gameData;
    double gameDetectOffsetMs = 0;
    void updateScorerInput();
    // automatic sound per part
    juce::var gameTone (const juce::var& args);
    juce::Array<juce::var> chainForTone (const juce::String& rigId, const juce::String& toneName, bool bass, const juce::var& assign);
    void applyGameTone();
    void restoreGameTone();
    struct GameRig { juce::String key, name; int first = 0, count = 0; };
    std::vector<GameRig> gameRigs;
    std::vector<std::pair<double, int>> gameToneSchedule;
    int gameToneTrackId = 0, gameActiveRig = -1;
    bool gameToneOn = false;
    std::pair<int, bool> gameMonitorBackup { 0, false };
    int stateVersion = 1;
    std::map<juce::String, juce::var> waveformCache;
};

} // namespace galpon
