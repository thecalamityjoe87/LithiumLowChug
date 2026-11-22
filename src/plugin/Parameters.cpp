#include "Parameters.h"

#include "Tunings.h"

namespace lowchug::params
{

namespace
{
// The number after the ID is the parameter's "version hint". JUCE needs it
// for AU/VST3 so hosts can tell parameters added in later versions apart.
// Bump it only for parameters ADDED in a later release.
juce::ParameterID id (const char* name) { return { name, 1 }; }

juce::StringArray toStringArray (auto&& names)
{
    juce::StringArray result;
    for (const auto& n : names)
        result.add (juce::String (std::string (n)));
    return result;
}

juce::String formatSigned (float value, int decimals, const char* suffix)
{
    return (value > 0.0f ? "+" : "") + juce::String (value, decimals) + suffix;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterInt> (
        id (shift), "Shift", tunings::minShift, tunings::maxShift, -2,
        AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction (
            [] (int v, int) { return formatSigned ((float) v, 0, " st"); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        id (fine), "Fine", NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ct").withStringFromValueFunction (
            [] (float v, int) { return formatSigned (v, 1, " ct"); })));

    StringArray targetNames;
    for (const auto& t : tunings::targets)
        targetNames.add (String (std::string (t.name)));

    layout.add (std::make_unique<AudioParameterChoice> (
        id (sourceTuning), "Playing In", toStringArray (tunings::sourceNames), 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        id (targetTuning), "Hear", targetNames, tunings::defaultTargetIndex));

    layout.add (std::make_unique<AudioParameterChoice> (
        id (mode), "Mode", StringArray { "Live", "Studio" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (id (formant), "Formant", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        id (tight), "Tight", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return String (v, 0) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        id (mix), "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return String (v, 0) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        id (output), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return formatSigned (v, 1, " dB"); })));

    // Returned from getBypassParameter(), so hosts use it for their own
    // bypass button instead of bypassing the plugin from outside (which would
    // remove its latency and make the track jump in time).
    layout.add (std::make_unique<AudioParameterBool> (id (bypass), "Bypass", false));

    return layout;
}

} // namespace lowchug::params
