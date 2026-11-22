#pragma once

#include "LithiumLookAndFeel.h"
#include "MainView.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lowchug
{

/**
    The plugin window.

    Lifetime: the host creates it (through LowChugProcessor::createEditor)
    when you open the plugin's window and deletes it when you close it, while
    the processor and its parameters live on. So the editor holds no state of
    its own: everything it shows comes from the parameters.

    Threading: everything here runs on the message thread (JUCE's UI thread).
    The editor never touches the audio thread directly; it talks to it only
    through parameters (atomics) and the lock-free AnalysisTap.

    Sizing: the UI is laid out once at 800 x 500 and scaled as one with an
    AffineTransform, so it looks identical at every window size. The window
    opens at 840 x 525 and resizes with a fixed 1.6:1 aspect ratio, from
    600 x 375 to 1600 x 1000.
    HiDPI: JUCE already renders at the display's real pixel density (the
    host or OS tells it the scale), and everything here is drawn as vector
    paths and text (no bitmaps), so it is sharp at any scale.
*/
class LowChugEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LowChugEditor (LowChugProcessor&);
    ~LowChugEditor() override;

    void resized() override;

    MainView& getMainView() noexcept { return view; }

private:
    // The LookAndFeel must outlive every component using it, so it is
    // declared first (constructed first, destroyed last).
    LithiumLookAndFeel lookAndFeel;
    MainView view;
    juce::TooltipWindow tooltips { this, 600 }; // shows setTooltip() texts after 600 ms

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowChugEditor)
};

} // namespace lowchug
