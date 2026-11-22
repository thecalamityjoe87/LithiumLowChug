#pragma once

#include "AboutPanel.h"
#include "Controls.h"
#include "HeaderBar.h"
#include "ReadoutPanel.h"
#include "TuningController.h"
#include "TuningPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The whole UI, laid out once at 800 x 500 logical pixels. The editor
    scales it to the window (see LowChugEditor), so nothing here deals with
    window size or screen DPI.

        +-------------------------------------------------------------+
        | LITHIUM / LOWCHUG  GUITAR DETUNER      LATENCY  BYPASS  (i)  |
        +---------------------------------+---------------------------+
        | PLAYING IN  [Standard|Drop D|C] | YOU HEAR                  |
        | HEAR  [E] [Eb] [D] [C#] [C]     | Drop C            [-2 st] |
        |       [B] [Bb] [A] [Oct] [Cust] | spectrum + string markers |
        |                                 | * On target  -2.00 st 0 ct|
        +---------------------------------+---------------------------+
        |  SHIFT   FINE   MIX   OUTPUT    | QUALITY      COMING SOON  |
        |   (o)    (o)    (o)    (o)      | [LIVE|STUDIO] [FORMANT] (o)|
        +---------------------------------+---------------------------+
*/
class MainView final : public juce::Component
{
public:
    explicit MainView (LowChugProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // For tests.
    TuningController& getTuningController() noexcept { return controller; }
    TuningPanel& getTuningPanel() noexcept { return tuning; }
    HeaderBar& getHeader() noexcept { return header; }
    ReadoutPanel& getReadoutPanel() noexcept { return readout; }
    UserSlider& getShiftSlider() noexcept { return shiftKnob.getSlider(); }

private:
    juce::Rectangle<int> getKnobsCard() const;
    juce::Rectangle<int> getComingSoonCard() const;
    void showAbout (bool);

    LowChugProcessor& processor;

    // The controller first: the panels below hold references to it, so it
    // must be constructed before them and destroyed after them.
    TuningController controller;

    HeaderBar header;
    TuningPanel tuning;
    ReadoutPanel readout;

    LabeledKnob shiftKnob, fineKnob, mixKnob, outputKnob;

    // Coming soon (milestone 3): attached to their parameters already, so
    // they show the saved values, but disabled.
    std::array<juce::TextButton, 2> modeButtons;
    juce::ParameterAttachment modeAttachment;
    juce::TextButton formantButton { "Formant" };
    juce::AudioProcessorValueTreeState::ButtonAttachment formantAttachment;
    LabeledKnob tightKnob;

    // The "?" in the two lower cards (the upper cards have their own).
    HelpButton controlsHelp, qualityHelp;

    AboutPanel about; // last, so it sits on top of everything

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

} // namespace lowchug
