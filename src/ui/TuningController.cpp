#include "TuningController.h"

#include "plugin/Parameters.h"

namespace lowchug
{

namespace
{
juce::RangedAudioParameter& param (LowChugProcessor& p, const char* id)
{
    auto* result = p.getState().getParameter (id);
    jassert (result != nullptr);
    return *result;
}

float plainValue (const juce::RangedAudioParameter& p)
{
    return p.convertFrom0to1 (p.getValue());
}
} // namespace

TuningController::TuningController (LowChugProcessor& p)
    : processor (p),
      sourceParam (param (p, params::sourceTuning)),
      targetParam (param (p, params::targetTuning)),
      shiftParam (param (p, params::shift)),
      fineParam (param (p, params::fine)),
      // Each attachment just reports "something changed"; the views read the
      // current values back through the getters.
      source (sourceParam, [this] (float) { changed(); }),
      target (targetParam, [this] (float) { changed(); }),
      shift (shiftParam, [this] (float) { changed(); }),
      fine (fineParam, [this] (float) { changed(); })
{
}

tunings::Source TuningController::getSource() const
{
    return static_cast<tunings::Source> (juce::roundToInt (plainValue (sourceParam)));
}

int TuningController::getTarget() const
{
    return juce::roundToInt (plainValue (targetParam));
}

int TuningController::getDisplayedTarget() const
{
    // Normally the target itself. But automation can set a combination the
    // UI would never offer (Playing in Drop D, Hear "D Standard"); the
    // processor then plays the whole-guitar equivalent (Drop C), so light up
    // the button for what is actually heard.
    const auto s = getSource();
    const auto t = getTarget();
    if (tunings::isReachable (s, t) && (s != tunings::Source::custom || t == tunings::customTargetIndex))
        return t;
    return tunings::targetForShift (s, getShiftSemitones());
}

int TuningController::getShiftSemitones() const
{
    return processor.getShiftSemitones();
}

float TuningController::getFineCents() const
{
    return plainValue (fineParam);
}

void TuningController::pickSource (tunings::Source newSource)
{
    const int heard = getShiftSemitones();
    const bool wasCustom = getTarget() == tunings::customTargetIndex;

    // 1. Park the current shift on the knob, so whatever Hear becomes, the
    //    sound doesn't jump.
    shift.setValueAsCompleteGesture ((float) heard);

    // 2. The new source.
    source.setValueAsCompleteGesture ((float) static_cast<int> (newSource));

    // 3. The matching target: the same shift in the new family (D Standard
    //    -> Drop C), or Custom if there is none. A Custom target stays Custom.
    if (! wasCustom)
        target.setValueAsCompleteGesture ((float) tunings::targetForShift (newSource, heard));
}

void TuningController::pickTarget (int targetIndex)
{
    // Custom keeps whatever the knob says; a named target also moves the
    // knob to its shift, so the knob shows what you hear.
    const auto heard = getShiftSemitones();
    target.setValueAsCompleteGesture ((float) targetIndex);
    shift.setValueAsCompleteGesture ((float) tunings::shiftFor (getSource(), targetIndex).value_or (heard));
}

void TuningController::shiftMovedByHand()
{
    if (getTarget() != tunings::customTargetIndex)
        target.setValueAsCompleteGesture ((float) tunings::customTargetIndex);
}

} // namespace lowchug
