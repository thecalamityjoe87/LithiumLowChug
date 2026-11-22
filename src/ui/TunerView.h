#pragma once

#include "TuningController.h"
#include "dsp/PitchDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/**
    The tuner, shown in the right card's TUNER tab. It tunes the guitar
    ITSELF (the input, before any shift) to the tuning you play in.

    Shows the nearest note, a needle on a +/-50 cent scale, the frequency,
    and the strings of your "Playing in" tuning (the string being played
    lights up). Chromatic when playing in Custom.

    Threading: like the visualizer it runs on the message thread. The
    ReadoutPanel's timer hands it the input audio the AnalysisTap collected
    (push), then calls update() to detect the pitch and redraw. The audio
    thread is never involved beyond copying samples into the tap.
*/
class TunerView final : public juce::Component
{
public:
    explicit TunerView (TuningController&);

    /** Sizes the buffers for a sample rate (allocates). */
    void prepare (double sampleRate);

    /** Appends input audio (mono). */
    void push (const float* samples, int numSamples);

    /** Detects the pitch of the newest audio and repaints. */
    void update();

    /** The note shown, as a fractional MIDI note (69 = A4), or a negative
        value when no pitch is shown. For tests. */
    float getShownNote() const noexcept { return hasPitch ? shownNote : -1.0f; }

    void paint (juce::Graphics&) override;

private:
    TuningController& controller;
    dsp::PitchDetector detector;
    double sampleRate = 0.0;

    std::vector<float> ring, window; // the newest input, and a straightened copy for the detector
    int writePos = 0;
    juce::int64 received = 0;

    std::array<float, 5> recent {};  // the latest readings, for a median
    int recentCount = 0;
    float shownNote = 0.0f;          // eased toward the median
    bool hasPitch = false;
    int silentUpdates = 0;           // updates since the last reading

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerView)
};

} // namespace lowchug
