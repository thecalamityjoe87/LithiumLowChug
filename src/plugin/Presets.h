#pragma once

#include "Tunings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lowchug::presets
{

/**
    The factory presets: one per common down-tuning, each in a Live and a
    Studio version (18 in all). They are exposed to hosts as JUCE "programs"
    (getNumPrograms / setCurrentProgram ...) and listed in the editor's
    preset box.

    A preset sets the tuning (Playing in, Hear, Shift), the mode, and puts
    the other sound controls back to neutral (Fine 0, Formant off, Tight 0,
    Mix 100 %, Output 0 dB). It never touches Bypass.

    Order: all Live presets, then all Studio presets, in the same tuning
    order, so program N and N + 9 are the same tuning in the two modes.
*/
struct Tuning
{
    const char* name;
    tunings::Source source;
    const char* target; // a tunings::targets name
};

inline constexpr std::array<Tuning, 9> factoryTunings { {
    { "Eb Standard", tunings::Source::standard, "Eb Standard" },
    { "D Standard", tunings::Source::standard, "D Standard" },
    { "C# Standard", tunings::Source::standard, "C# Standard" },
    { "C Standard", tunings::Source::standard, "C Standard" },
    { "B Standard", tunings::Source::standard, "B Standard" },
    { "Drop C", tunings::Source::dropD, "Drop C" },
    { "Drop B", tunings::Source::dropD, "Drop B" },
    { "Drop A", tunings::Source::dropD, "Drop A" },
    { "Octave Down", tunings::Source::standard, "Octave Down" },
} };

inline constexpr int numPresets = (int) factoryTunings.size() * 2;

/** The preset a fresh instance matches: D Standard, Live (the parameter defaults). */
inline constexpr int defaultPreset = 1;

bool isStudio (int preset);
const Tuning& tuningOf (int preset);

/** "D Standard (Live)": the name hosts show in their program lists. */
juce::String getName (int preset);

/** Sets every parameter the preset covers. Call on the message thread: it
    writes parameters as complete gestures, as if the user had set them. */
void apply (juce::AudioProcessorValueTreeState&, int preset);

/** True if the parameters still match the preset (so the UI can mark an
    edited preset with "*"). Safe on the message thread. */
bool matches (const juce::AudioProcessorValueTreeState&, int preset);

} // namespace lowchug::presets
