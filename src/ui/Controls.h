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
    slider to match. onUserChange fires only for the hand-made changes, for
    controls that must react to the user but not to their own parameter.

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

/** A tab in a small tab row ("YOU HEAR | TUNER"): caption text, an accent
    underline when selected. As Lithium Drums' tabs, at caption size. */
class TabButton final : public juce::Button
{
public:
    explicit TabButton (const juce::String& name);

    /** Wide enough for its text. */
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabButton)
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

/** What a help mark opens: a heading and a few short paragraphs, in a
    call-out pointing at the mark. (As in Lithium Drums.) */
class HelpPopover final : public juce::Component
{
public:
    HelpPopover (const juce::String& heading, const juce::StringArray& paragraphs);

    void paint (juce::Graphics&) override;

    /** Opens one in a call-out pointing at `target` (in `from`'s coordinates).
        The call-out owns the popover and deletes it when it closes (a click
        elsewhere, or Escape). */
    static void show (juce::Component& from, juce::Rectangle<int> target, const juce::String& heading,
                      const juce::StringArray& paragraphs);

private:
    static constexpr int padding = 16;
    juce::TextLayout layout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPopover)
};

/** The "?" in a card's top-right corner that explains the card, for new
    users. Clicking it opens a HelpPopover. Drawn as in Lithium Drums: a "?"
    in a 14 px ring, gray, cyan on hover. */
class HelpButton final : public juce::Button
{
public:
    HelpButton (juce::String heading, juce::StringArray paragraphs);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    static constexpr int size = 20; // the button; the ring in it is 14 px

private:
    juce::String heading;
    juce::StringArray paragraphs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpButton)
};

/** Where call-outs go: the main view (the editor's child, which the editor
    scales as a whole), so they scale with the rest of the UI. */
juce::Component* findOverlayParent (juce::Component&);

/** Fills a card: the panel color with its border. */
void drawCard (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill);

/** The sunken slot a segmented control's buttons sit in. */
void drawSegmentSlot (juce::Graphics&, juce::Rectangle<float>);

} // namespace lowchug
