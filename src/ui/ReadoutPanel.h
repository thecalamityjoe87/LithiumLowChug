#pragma once

#include "Controls.h"
#include "ShiftAnalyzer.h"
#include "TuningController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The right card, "You hear": the tuning you hear in big letters, the
    shift in semitones, and the visualizer that checks it:
      - the input (gray) and output (cyan) spectra on a log-frequency axis,
        with the open strings of the tuning you hear marked, and
      - the shift actually measured between input and output, and how far it
        is from what the controls ask for.

    Threading: while this panel exists it switches the processor's
    AnalysisTap on. A 30 Hz juce::Timer (message thread) drains the tap and
    runs the analysis; the audio thread only ever copies samples into the
    lock-free FIFO.
*/
class ReadoutPanel final : public juce::Component,
                           private juce::Timer
{
public:
    ReadoutPanel (LowChugProcessor&, TuningController&);
    ~ReadoutPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The most recent measurement (for tests). */
    const ShiftAnalyzer::Result& getLastResult() const noexcept { return result; }

    /** Drains the tap and analyzes now (the timer's job; public for tests). */
    void analyzeNow();

private:
    void timerCallback() override { analyzeNow(); }
    bool isBypassed() const;

    void paintSpectrum (juce::Graphics&, juce::Rectangle<float> plot);
    void paintMatchLine (juce::Graphics&, juce::Rectangle<float> line);

    LowChugProcessor& processor;
    TuningController& controller;
    ShiftAnalyzer analyzer;
    ShiftAnalyzer::Result result;
    std::vector<float> scratchIn, scratchOut;
    std::atomic<float>* bypassValue = nullptr;
    HelpButton help;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReadoutPanel)
};

} // namespace lowchug
