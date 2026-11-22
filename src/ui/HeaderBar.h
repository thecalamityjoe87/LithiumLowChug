#pragma once

#include "Controls.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/** The top bar: the Lithium wordmark, the latency readout and Bypass. */
class HeaderBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit HeaderBar (LowChugProcessor&);

    /** The "i" button was clicked. */
    std::function<void()> onShowAbout;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // juce::Timer calls this on the message thread. The latency only changes
    // when the host re-prepares the plugin (new sample rate), so a slow poll
    // is plenty and simpler than any notification.
    void timerCallback() override;
    juce::String makeLatencyText() const;

    LowChugProcessor& processor;
    juce::String latencyText;

    InfoButton infoButton;
    juce::TextButton bypassButton { "Bypass" };
    // A ButtonAttachment links a toggle button to a bool parameter, both
    // ways (see LabeledKnob for what attachments do). Declared after the
    // button so it is destroyed first.
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace lowchug
