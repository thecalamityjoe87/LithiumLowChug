#pragma once

#include "plugin/PluginProcessor.h"
#include "plugin/Tunings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lowchug
{

/**
    The editor's tuning logic, kept apart from any drawing: Playing in, Hear
    and the Shift knob, and how they stay in step.

    The rules:
      - Picking a Hear target also moves the Shift knob to that target's
        shift, so the knob always shows what you hear.
      - Moving the Shift knob BY HAND switches Hear to Custom (the knob is
        then in charge).
      - Changing Playing in keeps the sound: D Standard (-2) from Standard
        becomes Drop C (-2) from Drop D. Playing in Custom forces Hear to
        Custom.

    Realtime safety (why this lives in the editor, not the processor):
      Parameters are plain atomics that the audio thread only ever READS.
      Every linked write here happens on the message thread, in direct
      response to a click or drag, through the normal host-visible path
      (begin gesture, set value, end gesture), exactly as if the user had
      moved both controls. Nothing is written from the audio thread, and
      nothing is written in response to automation or host changes, so
      there are no feedback loops and the host's automation lanes record
      just what the user did.

      The audio thread may see the two writes a block apart (say, the new
      target before the new shift). Each order still describes the same
      sound, and the detuner glides between shifts anyway, so it is inaudible.

    ParameterAttachment is JUCE's generic "one parameter <-> one piece of
    UI" link (SliderAttachment and ButtonAttachment are built on it). Its
    callback always runs on the message thread, even when the change came
    from host automation on the audio thread (it is forwarded
    asynchronously), so onChange handlers may touch components freely.
*/
class TuningController
{
public:
    explicit TuningController (LowChugProcessor&);

    tunings::Source getSource() const;
    int getTarget() const;            // the parameter's index
    int getDisplayedTarget() const;   // the target to highlight (see .cpp)
    int getShiftSemitones() const;    // what you hear, in whole semitones
    float getFineCents() const;

    /** The user clicked a "Playing in" button. */
    void pickSource (tunings::Source);

    /** The user clicked a Hear target. */
    void pickTarget (int targetIndex);

    /** The user moved the Shift knob by hand (call after the knob's own
        attachment has written the new value). */
    void shiftMovedByHand();

    /** Called on the message thread whenever source, target, shift or fine
        change, from anywhere (UI, automation, a preset, the host). */
    std::function<void()> onChange;

private:
    void changed() { if (onChange) onChange(); }

    LowChugProcessor& processor;
    juce::RangedAudioParameter& sourceParam;
    juce::RangedAudioParameter& targetParam;
    juce::RangedAudioParameter& shiftParam;
    juce::RangedAudioParameter& fineParam;

    juce::ParameterAttachment source, target, shift, fine;
};

} // namespace lowchug
