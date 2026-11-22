#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lowchug::params
{

// Parameter IDs. These strings are stored in saved sessions and presets, so
// never rename one after release (add a new parameter instead).
inline constexpr const char* shift = "shift";
inline constexpr const char* fine = "fine";
inline constexpr const char* sourceTuning = "sourceTuning";
inline constexpr const char* targetTuning = "targetTuning";
inline constexpr const char* mode = "mode";
inline constexpr const char* formant = "formant";
inline constexpr const char* tight = "tight";
inline constexpr const char* mix = "mix";
inline constexpr const char* output = "output";
inline constexpr const char* bypass = "bypass";

enum class Mode { live, studio };

/** Builds every parameter. The APVTS owns them afterwards. */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace lowchug::params
