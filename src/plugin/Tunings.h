#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace lowchug
{

/**
    The "I'm playing in" -> "I want to hear" model.

    LowChug shifts the WHOLE guitar, so it can only move a tuning to another
    tuning with the same string intervals:
      - standard-family tunings (E, Eb, D ... standard) all have the low
        string a fourth below the A string,
      - drop-family tunings (Drop D, Drop C ...) have it a whole tone lower.
    So "Standard" can become any standard tuning, and "Drop D" any drop
    tuning, and the shift is simply the difference of their offsets.

    Plain C++ (no JUCE) so it can be unit-tested and reused.
*/
namespace tunings
{

enum class Family { standard, drop };

/** What the guitar is actually tuned to. Index = sourceTuning choice index. */
enum class Source { standard, dropD, custom };
inline constexpr std::array<std::string_view, 3> sourceNames { "Standard (E)", "Drop D", "Custom" };

struct Target
{
    std::string_view name;   // also the host-visible choice text (ASCII: hosts render it)
    Family family;
    int offset;              // semitones relative to E Standard / Drop D
};

/** The "I want to hear" list. Index = targetTuning choice index. The last
    entry, "Custom", means "use the shift parameter as set". */
inline constexpr std::array<Target, 18> targets { {
    { "E Standard", Family::standard, 0 },
    { "Eb Standard", Family::standard, -1 },
    { "D Standard", Family::standard, -2 },
    { "C# Standard", Family::standard, -3 },
    { "C Standard", Family::standard, -4 },
    { "B Standard", Family::standard, -5 },
    { "Bb Standard", Family::standard, -6 },
    { "A Standard", Family::standard, -7 },
    { "Drop D", Family::drop, 0 },
    { "Drop C#", Family::drop, -1 },
    { "Drop C", Family::drop, -2 },
    { "Drop B", Family::drop, -3 },
    { "Drop Bb", Family::drop, -4 },
    { "Drop A", Family::drop, -5 },
    { "Drop Ab", Family::drop, -6 },
    { "Drop G", Family::drop, -7 },
    { "Octave Down", Family::standard, -12 }, // works from either family
    { "Custom", Family::standard, 0 },
} };

inline constexpr int customTargetIndex = (int) targets.size() - 1;
inline constexpr int octaveDownIndex = (int) targets.size() - 2;
inline constexpr int defaultTargetIndex = 2; // D Standard

inline constexpr int minShift = -12, maxShift = 2;

/** Can `target` be reached from `source` by shifting the whole guitar?
    (Custom source/target are always "reachable": the shift is manual.) */
bool isReachable (Source source, int targetIndex);

/** The shift (semitones) that turns `source` into `target`, or nothing if
    the target is Custom or the source is Custom (shift is then manual). */
std::optional<int> shiftFor (Source source, int targetIndex);

/** What the listener actually hears: e.g. "Drop C" for Drop D shifted by -2.
    Falls back to "-3 st" style text when there is no common name. */
std::string describe (Source source, int shiftSemitones);

/** The target index whose shift from `source` equals `shiftSemitones`,
    or customTargetIndex if none (used when the user moves `shift` by hand). */
int targetForShift (Source source, int shiftSemitones);

} // namespace tunings
} // namespace lowchug
