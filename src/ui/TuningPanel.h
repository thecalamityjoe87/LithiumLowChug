#pragma once

#include "Controls.h"
#include "TuningController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The left card: "Playing in" (Standard / Drop D / Custom) and the "Hear"
    grid, which only offers targets you can reach from what you play in.

    Clicks go to the TuningController, which writes the parameters; the
    buttons light up from the parameters (refresh()), never from the click
    itself. So host automation, presets and the UI always agree.
*/
class TuningPanel final : public juce::Component
{
public:
    explicit TuningPanel (TuningController&);

    /** Re-reads the parameters. Message thread. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> getSlotBounds() const;
    juce::Rectangle<int> getChipArea() const;

    TuningController& controller;
    std::array<juce::TextButton, 3> sourceButtons;
    juce::OwnedArray<TuningChip> chips; // index = targetTuning index

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningPanel)
};

} // namespace lowchug
