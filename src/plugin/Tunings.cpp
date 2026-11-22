#include "Tunings.h"

namespace lowchug::tunings
{

namespace
{
std::optional<Family> familyOf (Source source)
{
    switch (source)
    {
        case Source::standard: return Family::standard;
        case Source::dropD:    return Family::drop;
        case Source::custom:   break;
    }
    return std::nullopt;
}

bool isValidIndex (int targetIndex) { return targetIndex >= 0 && targetIndex < (int) targets.size(); }
} // namespace

bool isReachable (Source source, int targetIndex)
{
    if (! isValidIndex (targetIndex))
        return false;
    const auto family = familyOf (source);
    if (! family || targetIndex == customTargetIndex || targetIndex == octaveDownIndex)
        return true;
    return targets[(size_t) targetIndex].family == *family;
}

std::optional<int> shiftFor (Source source, int targetIndex)
{
    if (! isValidIndex (targetIndex) || targetIndex == customTargetIndex || source == Source::custom)
        return std::nullopt;

    // Unreachable combinations (e.g. Standard -> Drop C, which would need
    // only the low string lowered) still get the matching whole-guitar shift;
    // describe() then names what is actually heard (D Standard), so the
    // readout never lies even if automation asks for the impossible.
    return targets[(size_t) targetIndex].offset;
}

std::string describe (Source source, int shiftSemitones)
{
    if (const auto family = familyOf (source))
    {
        if (shiftSemitones == -12)
            return *family == Family::standard ? "Octave Down" : "Drop D (Octave Down)";

        for (const auto& t : targets)
            if (t.family == *family && t.offset == shiftSemitones && t.name != "Custom" && t.name != "Octave Down")
                return std::string (t.name);

        // Above the list (capo-style +1/+2): name it from the root.
        if (shiftSemitones == 1)  return *family == Family::standard ? "F Standard" : "Drop D#";
        if (shiftSemitones == 2)  return *family == Family::standard ? "F# Standard" : "Drop E";
    }

    if (shiftSemitones == 0)
        return "No shift";
    return (shiftSemitones > 0 ? "+" : "") + std::to_string (shiftSemitones) + " st";
}

int targetForShift (Source source, int shiftSemitones)
{
    const auto family = familyOf (source);
    if (! family)
        return customTargetIndex;
    if (shiftSemitones == -12)
        return octaveDownIndex;
    for (int i = 0; i < customTargetIndex; ++i)
        if (i != octaveDownIndex && targets[(size_t) i].family == *family && targets[(size_t) i].offset == shiftSemitones)
            return i;
    return customTargetIndex;
}

} // namespace lowchug::tunings
