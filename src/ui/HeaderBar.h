#pragma once

#include "Controls.h"
#include "PresetBar.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/** The top bar: the Lithium wordmark, the presets, the latency readout,
    Bypass and About. */
class HeaderBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit HeaderBar (LowChugProcessor&);

    /** The "i" button was clicked. */
    std::function<void()> onShowAbout;

    void paint (juce::Graphics&) override;
    void resized() override;

    PresetBar& getPresetBar() noexcept { return presetBar; }

private:
    // juce::Timer calls this on the message thread. The latency only changes
    // when the host re-prepares the plugin (new sample rate), so a slow poll
    // is plenty and simpler than any notification.
    void timerCallback() override;
    juce::String makeLatencyText() const;
    float getWordmarkRight() const;

    LowChugProcessor& processor;
    juce::String latencyText;
    juce::Rectangle<float> latencyArea;

    PresetBar presetBar;

    InfoButton infoButton;
    juce::TextButton bypassButton { "Bypass" };
    // A ButtonAttachment links a toggle button to a bool parameter, both
    // ways (see LabeledKnob for what attachments do). Declared after the
    // button so it is destroyed first.
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace lowchug
