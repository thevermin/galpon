#pragma once

#include <juce_core/juce_core.h>
#include <deque>
#include <functional>
#include <map>

namespace galpon
{

/** Your TONE3000 account, used by the game to bring the captures you like.
    Sign-in is the "device" flow: Galpon shows a code, you approve it on tone3000.com.
    The free API lets an app read your favorites, downloads and creations and download
    their models (searching all of TONE3000 is for commercial apps), so Galpon picks the
    sounds among the tones you marked. Network work happens on a background thread. */
class Tone3000 : private juce::Thread
{
public:
    explicit Tone3000 (const juce::File& settingsFile, const juce::File& downloadFolder);
    ~Tone3000() override;

    /** Everything the interface shows. Message thread. */
    juce::var status() const;

    /** Saves the publishable key (t3k_pub_…) if given, then starts signing in. Returns the page to open. */
    juce::Result connect (const juce::String& publishableKey);
    void disconnect();
    /** Reloads your favorites and downloads. */
    void refresh();
    /** Downloads a tone's model (.nam). status()["files"][toneId] has the file when it's ready. */
    void download (const juce::String& toneId);

    static constexpr const char* base = "https://www.tone3000.com/api/v1";

private:
    void run() override;
    void post (std::function<void()> job);
    juce::var request (const juce::String& method, const juce::String& path, const juce::String& formBody, bool auth, int& status);
    bool ensureToken();
    void loadSettings();
    void saveSettings() const;
    void setError (const juce::String& e);
    void pollDeviceLogin (juce::String deviceCode, int interval, double expiresAt);
    void fetchLists();
    void fetchModel (const juce::String& toneId);

    juce::File settingsFile, folder;
    mutable juce::CriticalSection lock;
    juce::String clientId, accessToken, refreshToken, userName, error, userCode, verifyUrl;
    double expiresAtMs = 0;
    bool busy = false, loginPending = false;
    juce::Array<juce::var> tones;
    std::map<juce::String, juce::String> files;          // tone id -> local .nam
    std::map<juce::String, juce::String> downloading;    // tone id -> "" or error

    juce::CriticalSection jobLock;
    std::deque<std::function<void()>> jobs;
    juce::WaitableEvent wake;
};

} // namespace galpon
