#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    A Slider that knows whether the USER is changing it.

    A slider's value also changes when its parameter changes elsewhere
    (automation, a preset, another control): the attachment then sets the
    slider to match. The Shift knob must tell the two apart, because only a
    hand-made change should switch Hear to Custom.

    JUCE wraps every user interaction (drag, mouse wheel, double-click
    reset, accessibility) in startedDragging() / stoppedDragging(). Arrow keys
    are the one exception, so keyPressed() is covered here too.
*/
class UserSlider : public juce::Slider
{
public:
    UserSlider();

    /** True only while the user is changing the value. */
    bool isBeingChangedByUser() const noexcept { return userChanging; }

    /** Called after every value change the user made (and after the
        attachment has written it to the parameter). */
    std::function<void()> onUserChange;

protected:
    void startedDragging() override  { userChanging = true; }
    void stoppedDragging() override  { userChanging = false; }
    bool keyPressed (const juce::KeyPress&) override;

private:
    bool userChanging = false;
};

/**
    One of the machined knobs: a caption above, the dial, the value below.

    Wired to its parameter with a SliderAttachment. An attachment is JUCE's
    ready-made two-way link between a control and a parameter:
      - turning the knob sets the parameter, inside a begin/end "gesture"
        so the host can record automation and group undo,
      - changing the parameter (automation, presets, the host's own generic
        UI) moves the knob, always on the message thread even when the change
        came from the audio thread,
      - the knob takes its range, step and value text from the parameter, so
        they can never disagree.
    Without it you would write all of that by hand, with listeners on both
    sides and the threading to get right.
*/
class LabeledKnob final : public juce::Component
{
public:
    /** `origin`: where the value arc starts (0 st, 0 dB), in the
        parameter's units; nothing = the start of the range. */
    LabeledKnob (juce::AudioProcessorValueTreeState&, const char* parameterID, const juce::String& caption,
                  std::optional<float> origin = std::nullopt, int dialSize = theme::size::knob);

    UserSlider& getSlider() noexcept { return slider; }

    /** The size this knob wants at 1x (dial plus caption and value). */
    juce::Rectangle<int> getPreferredSize() const { return { dialSize + 24, dialSize + 40 }; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String caption;
    int dialSize;
    UserSlider slider;
    // Declared AFTER the slider, so it is destroyed BEFORE it: an attachment
    // must never outlive the control it talks to.
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabeledKnob)
};

/** One "Hear" button: a big note name over a small shift ("Eb" / "-1 st"). */
class TuningChip final : public juce::Button
{
public:
    TuningChip (const juce::String& name, const juce::String& big, const juce::String& small);

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    juce::String big, small;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningChip)
};

/** The small round "i" button in the header that opens About, drawn the
    same as Lithium Drums' (a round outline that lights on hover). */
class InfoButton final : public juce::Button
{
public:
    InfoButton();
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoButton)
};

/** Fills a card: the panel color with its border. */
void drawCard (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill);

/** The sunken slot a segmented control's buttons sit in. */
void drawSegmentSlot (juce::Graphics&, juce::Rectangle<float>);

} // namespace lowchug
