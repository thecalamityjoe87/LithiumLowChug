#pragma once

#include "Controls.h"
#include "TuningController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The "Tune down to" dial: a stepped selector with one notch per tuning you
    can reach from what you play in (E, Eb, D ... A, then an octave down).
    Turning it clockwise tunes lower; the cyan arc shows how far down you are.
    Click a name around it to jump there.

    The knob in the middle is a juce::Slider whose value is the notch number
    (0, 1, 2 ...), so dragging, the mouse wheel, arrow keys and screen
    readers all work as on any knob. It is not attached to a parameter
    directly: notch numbers change meaning with the source, so its changes
    go through TuningController::pickTarget.

    Custom has no notch (that is what the Shift knob is for). When Hear is
    Custom, the pointer dims at the nearest tuning and the readout says
    "Custom".
*/
class TuningDial final : public juce::Component
{
public:
    explicit TuningDial (TuningController&);
    ~TuningDial() override;

    /** Re-reads source and target. Message thread. */
    void refresh();

    /** Moves `notches` notches (positive = lower), as the user would. */
    void step (int notches);

    /** The targets on the dial, highest first (targetTuning indices). */
    const std::vector<int>& getDetents() const noexcept { return detents; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Knob;
    class NameButton;

    float angleOf (int notch) const;
    int nearestNotch() const;   // the notch closest to what you hear
    int selectedNotch() const;  // -1 when Hear is Custom

    TuningController& controller;
    std::vector<int> detents;
    std::unique_ptr<Knob> knob;
    juce::OwnedArray<NameButton> names; // one per target index; only the detents show

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningDial)
};

/**
    The left card: "Playing in" (how your guitar is tuned: Standard, Drop D
    or Custom) as three buttons, and the "Tune down to" dial.

    Clicks go to the TuningController, which writes the parameters; the
    buttons and the dial light up from the parameters (refresh()), never from
    the click itself. So host automation, presets and the UI always agree.
*/
class TuningPanel final : public juce::Component
{
public:
    explicit TuningPanel (TuningController&);

    /** Re-reads the parameters. Message thread. */
    void refresh();

    TuningDial& getDial() noexcept { return dial; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TuningController& controller;
    std::array<juce::TextButton, 3> sourceButtons;
    TuningDial dial;
    HelpButton help;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningPanel)
};

} // namespace lowchug
