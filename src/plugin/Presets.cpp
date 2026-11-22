#include "Presets.h"

#include "Parameters.h"

namespace lowchug::presets
{

namespace
{
int targetIndex (const char* name)
{
    for (int i = 0; i < (int) tunings::targets.size(); ++i)
        if (tunings::targets[(size_t) i].name == std::string_view (name))
            return i;
    jassertfalse; // a preset names a target that doesn't exist
    return tunings::customTargetIndex;
}

/** Every value a preset sets, as (parameter ID, plain value) pairs. */
std::vector<std::pair<const char*, float>> valuesOf (int preset)
{
    const auto& t = tuningOf (preset);
    const auto target = targetIndex (t.target);
    return {
        { params::sourceTuning, (float) static_cast<int> (t.source) },
        { params::targetTuning, (float) target },
        { params::shift, (float) tunings::shiftFor (t.source, target).value_or (0) },
        { params::fine, 0.0f },
        { params::mode, isStudio (preset) ? 1.0f : 0.0f },
        { params::formant, 0.0f },
        { params::tight, 0.0f },
        { params::mix, 100.0f },
        { params::output, 0.0f },
    };
}
} // namespace

bool isStudio (int preset) { return preset >= (int) factoryTunings.size(); }

const Tuning& tuningOf (int preset)
{
    return factoryTunings[(size_t) (juce::jlimit (0, numPresets - 1, preset) % (int) factoryTunings.size())];
}

juce::String getName (int preset)
{
    if (preset < 0 || preset >= numPresets)
        return {};
    return juce::String (tuningOf (preset).name) + (isStudio (preset) ? " (Studio)" : " (Live)");
}

void apply (juce::AudioProcessorValueTreeState& state, int preset)
{
    if (preset < 0 || preset >= numPresets)
        return;

    for (const auto& [id, value] : valuesOf (preset))
        if (auto* p = state.getParameter (id))
        {
            // begin/end gesture: hosts treat it like the user moving the
            // control, so automation recording and undo work.
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
}

bool matches (const juce::AudioProcessorValueTreeState& state, int preset)
{
    if (preset < 0 || preset >= numPresets)
        return false;

    for (const auto& [id, value] : valuesOf (preset))
        if (auto* raw = state.getRawParameterValue (id); raw == nullptr || std::abs (raw->load() - value) > 1.0e-3f)
            return false;
    return true;
}

} // namespace lowchug::presets
