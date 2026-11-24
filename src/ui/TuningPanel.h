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

    Playing in Custom there are no tuning names to offer, so the dial counts
    semitones instead ("Shift": +2, +1, 0, -1 ... -12, the whole range of the
    shift parameter). Its notches set the shift directly and Hear stays
    Custom; the arc then runs from 0 to the shift, either way.

    The knob in the middle is a juce::Slider whose value is the notch number
    (0, 1, 2 ...), so dragging, the mouse wheel, arrow keys and screen
    readers all work as on any knob. It is not attached to a parameter
    directly: notch numbers change meaning with the source, so its changes
    go through TuningController (pickTarget, or pickShift when counting).

    With named tunings, Custom has no notch. Hear can still be Custom there
    (from automation, or an older session); the pointer then dims at the
    nearest tuning and the readout says "Custom".
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

    /** The named targets on the dial, highest first (targetTuning indices).
        Empty when playing in Custom: the dial then counts semitones. */
    const std::vector<int>& getDetents() const noexcept { return detents; }

    /** True when the dial counts semitones down (playing in Custom). */
    bool countsSemitones() const noexcept { return counting; }

    /** How many notches the dial has right now. */
    int getNumNotches() const noexcept;

    /** The notch the knob points at, as last drawn (for tests). */
    int getShownNotch() const;

    /** When counting, notch k is a shift of maxShift - k semitones: the
        top notch is +2, the last -12. */
    static constexpr int numShiftNotches = tunings::maxShift - tunings::minShift + 1;
    static constexpr int shiftOfNotch (int notch) noexcept { return tunings::maxShift - notch; }
    static constexpr int notchOfShift (int semitones) noexcept { return tunings::maxShift - semitones; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Knob;
    class NameButton;

    float angleOf (int notch) const;
    int nearestNotch() const;   // the notch closest to what you hear
    int selectedNotch() const;  // -1 when nothing on the dial matches what you hear
    void pickNotch (int notch);
    NameButton* nameOf (int notch) const;

    TuningController& controller;
    bool counting = false;      // playing in Custom: the notches are shifts (see shiftOfNotch)
    std::vector<int> detents;
    std::unique_ptr<Knob> knob;
    juce::OwnedArray<NameButton> names;      // one per target index; only the detents show
    juce::OwnedArray<NameButton> shiftNames; // +2 ... -12; shown when counting

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningDial)
};

/**
    The left card: "Playing in" (how your guitar is tuned: Standard, Drop D
    or Custom) as three buttons, and the "Tune down to" dial (which counts
    semitones when playing in Custom).

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
