#pragma once

#include "Model.h"
#include <map>

namespace galpon
{

/** Describes a built-in instrument or effect. */
struct BuiltinInfo
{
    juce::String type, name, category, description; // category: "instrument" or "effect"
};

/** A ready-made sound for the library: a built-in type plus parameter values. */
struct BuiltinPreset
{
    juce::String type, name, group;
    std::vector<std::pair<juce::String, float>> params;   // parameter id -> real (not normalised) value
};

const std::vector<BuiltinInfo>&   builtinTypes();
const std::vector<BuiltinPreset>& builtinPresets();
const BuiltinPreset*              findPreset (const juce::String& name);

std::unique_ptr<juce::AudioProcessor> createBuiltin (const juce::String& type);
void applyPreset (juce::AudioProcessor&, const BuiltinPreset&);

//==============================================================================
/** Base for Galpon's own instruments and effects. Parameters are plain floats so the UI
    can show them as knobs, exactly like a plugin's parameters. */
class BuiltinProcessor : public juce::AudioProcessor
{
public:
    BuiltinProcessor (juce::String typeId, juce::String displayName, bool isInstrument);

    const juce::String getName() const override             { return displayName; }
    const juce::String& getTypeId() const noexcept          { return typeId; }
    bool acceptsMidi() const override                        { return instrument; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 2.0; }
    bool hasEditor() const override                          { return false; }
    juce::AudioProcessorEditor* createEditor() override      { return nullptr; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Extra things that are not knobs, like a loaded model file. */
    virtual juce::var getExtraInfo() const                    { return {}; }
    virtual juce::Result setExtra (const juce::String& key, const juce::var& value);

    float get (const juce::String& paramId) const;
    void  setReal (const juce::String& paramId, float realValue);

protected:
    juce::AudioParameterFloat* addParam (const juce::String& id, const juce::String& name,
                                         float min, float max, float def,
                                         const juce::String& unit = {}, float skewCentre = 0.0f);
    virtual void writeExtraState (juce::XmlElement&) const   {}
    virtual void readExtraState (const juce::XmlElement&)    {}

    std::map<juce::String, juce::AudioParameterFloat*> params;

private:
    juce::String typeId, displayName;
    bool instrument;
};

// Factories implemented in the instrument / effect / amp files
std::unique_ptr<BuiltinProcessor> createBuiltinInstrument (const juce::String& type);
std::unique_ptr<BuiltinProcessor> createBuiltinEffect (const juce::String& type);
std::unique_ptr<BuiltinProcessor> createNamAmp();
std::unique_ptr<BuiltinProcessor> createSampler();
/** What an SFZ file maps (regions, key range, sample files, how many are missing). No audio is loaded. */
juce::var describeSfz (const juce::File&);

} // namespace galpon
