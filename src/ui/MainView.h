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
        |  FINE   TIGHT   MIX   OUTPUT    | QUALITY                   |
        |   (o)    (o)    (o)    (o)      | [   LIVE   |   STUDIO   ] |
        |                                 | [        FORMANT        ] |
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
    juce::TextButton& getModeButton (int index) noexcept { return modeButtons[(size_t) index]; }
    juce::TextButton& getFormantButton() noexcept { return formantButton; }
    LabeledKnob& getTightKnob() noexcept { return tightKnob; }
    void showAboutPanel (bool show) { showAbout (show); }

private:
    juce::Rectangle<int> getKnobsCard() const;
    juce::Rectangle<int> getQualityCard() const;
    void showAbout (bool);

    LowChugProcessor& processor;

    // The controller first: the panels below hold references to it, so it
    // must be constructed before them and destroyed after them.
    TuningController controller;

    HeaderBar header;
    TuningPanel tuning;
    ReadoutPanel readout;

    // The knobs you adjust by ear. (The shift itself is set on the tuning
    // dial; its parameter has no knob of its own.)
    LabeledKnob fineKnob, tightKnob, mixKnob, outputKnob;

    // Quality: Live / Studio (two segments driving one choice parameter)
    // and Formant (a toggle): how the shift is done, set once per session.
    std::array<juce::TextButton, 2> modeButtons;
    juce::ParameterAttachment modeAttachment;
    juce::TextButton formantButton { "Formant" };
    juce::AudioProcessorValueTreeState::ButtonAttachment formantAttachment;

    // The "?" in the two lower cards (the upper cards have their own).
    HelpButton controlsHelp, qualityHelp;

    AboutPanel about; // last, so it sits on top of everything

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

} // namespace lowchug
