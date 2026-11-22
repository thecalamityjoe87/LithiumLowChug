#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The header's preset box, as in Lithium Drums:

        [<]  [ D Standard *            v ]  [>]
             [ Live · play in Standard   ]

    The box shows the preset's name (an accent "*" once you change any
    setting it covers) over its mode and the tuning it is played from.
    Clicking it opens the preset panel in a call-out: Live presets on the
    left, Studio on the right, each column headed with that mode's latency.
    The arrows step through the presets.

    Loading goes through the processor's setCurrentProgram(), the same call
    hosts use for their own program menus, so the two always agree. The box
    re-reads the current program and whether it was edited on a timer, which
    also catches a host switching programs while the editor is open.
*/
class PresetBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit PresetBar (LowChugProcessor&);
    ~PresetBar() override; // defined in the .cpp, where Arrow and Box are complete

    /** The width it wants at 1x. */
    static constexpr int idealWidth = 28 + 4 + 210 + 4 + 28;
    /** And its height. */
    static constexpr int idealHeight = 38;

    /** Loads the preset `steps` places away, wrapping around. */
    void step (int steps);

    /** Re-reads the current preset and whether it was edited (its timer
        does this ten times a second). */
    void refresh();

    /** The preset's name as shown in the box, with " *" when edited (for tests). */
    juce::String getShownName() const { return shownName; }

    /** The box's second line: "Live · play in Standard" (for tests). */
    juce::String getShownDetail() const { return shownDetail; }

    /** A column heading in the panel, with that mode's latency: "LIVE · 21 ms". */
    juce::String getMenuHeader (params::Mode) const;

    /** Builds the preset panel (what the box shows in its call-out). Public
        so tests can use it without opening a window. */
    std::unique_ptr<juce::Component> createPanel();

    void resized() override;

private:
    class Arrow;
    class Box;
    class Panel;

    void timerCallback() override { refresh(); }
    void showPanel();

    LowChugProcessor& processor;
    std::unique_ptr<Arrow> previous, next;
    std::unique_ptr<Box> box;
    juce::String shownName, shownDetail;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace lowchug
