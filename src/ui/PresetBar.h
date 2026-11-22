#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The header's preset box:  [<]  [ D Standard        LIVE v ]  [>]

    Click the box for a two-column menu (Live presets | Studio presets), or
    step through them with the arrows. The name gets a "*" once you change
    any setting the preset covers.

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
    static constexpr int idealWidth = 28 + 4 + 196 + 4 + 28;

    /** Loads the preset `steps` places away, wrapping around. */
    void step (int steps);

    /** Re-reads the current preset and whether it was edited (its timer
        does this ten times a second). */
    void refresh();

    /** The text shown in the box (for tests). */
    juce::String getShownName() const { return shownName; }

    void resized() override;

private:
    class Arrow;
    class Box;

    void timerCallback() override { refresh(); }
    void showMenu();

    LowChugProcessor& processor;
    std::unique_ptr<Arrow> previous, next;
    std::unique_ptr<Box> box;
    juce::String shownName;
    bool shownStudio = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace lowchug
