#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace galpon
{

/** A ready-made guitar or bass sound: a chain of Galpon's own effects with their settings.
    The same list is offered in the studio (on audio tracks) and in the game (for each part of a
    song), and the game picks the closest one by itself. Built from the amp settings players use
    most (clean Fender-style, British crunch, JCM800 hard rock, boosted high gain…). */
struct TonePreset
{
    struct Step { juce::String type; std::vector<std::pair<juce::String, float>> params; };

    juce::String id, name, group, description;
    float drive = 0.0f;      // how distorted it sounds, 0 (clean) … 1 (high gain): used to pick one by ear
    bool bass = false;       // a bass sound
    bool lead = false;       // made for solos (boost, delay)
    std::vector<Step> chain;
};

const std::vector<TonePreset>& tonePresets();
const TonePreset* findTonePreset (const juce::String& id);

/** The closest preset to what a song asks for: `heardDrive` is how distorted its guitar sounds
    (0..1, or < 0 if unknown) and `toneName` the name the song gives the sound ("…_clean", "solo"…). */
const TonePreset& closestTonePreset (float heardDrive, const juce::String& toneName, bool bass);

} // namespace galpon
