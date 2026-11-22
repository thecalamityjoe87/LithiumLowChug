#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace lowchug
{

/**
    Measures the pitch shift you actually hear, for the "You hear" card.

    It compares the spectrum of the input with the spectrum of the output on
    a log-frequency axis. On that axis a pitch shift is a sideways slide: every
    harmonic of every string moves by the same distance. So the slide that
    lines the two spectra up best IS the shift, and it works for chords as
    well as single notes. (A tuner-style pitch detector would only work on
    single notes.)

    It also keeps smoothed spectra for drawing.

    Runs on the message thread only (the editor's timer); never touches the
    audio thread. Samples arrive via the processor's AnalysisTap.
*/
class ShiftAnalyzer
{
public:
    // The drawn spectrum: log frequency, 40 Hz to 2 kHz (an octave below a
    // low E up to well past the high strings' third harmonics).
    static constexpr int displayPoints = 160;
    static constexpr float displayMinHz = 40.0f;
    static constexpr float displayMaxHz = 2000.0f;

    /** Where `hz` sits on the drawn axis, 0 (left) to 1 (right). */
    static float displayPosition (float hz);

    /** Allocates everything for this sample rate. Message thread. */
    void prepare (double sampleRate);

    /** Appends audio (input and output, same moment, one mono channel each). */
    void push (const float* in, const float* out, int numSamples);

    struct Result
    {
        enum class State { silent, searching, measured };
        State state = State::silent;
        float measuredSemitones = 0.0f;   // valid when state == measured
        float confidence = 0.0f;          // 0..1, how well the spectra line up
    };

    /** Analyzes the newest audio. `latencySamples` lines the input up with
        the output; `expectedSemitones` is what the controls ask for (the
        search looks 1.5 semitones either side of it). */
    Result update (int latencySamples, float expectedSemitones);

    /** Smoothed spectra for drawing, 0..1 (a -90..-6 dBFS scale). */
    const std::array<float, displayPoints>& getInputSpectrum() const noexcept  { return displayIn; }
    const std::array<float, displayPoints>& getOutputSpectrum() const noexcept { return displayOut; }

    double getSampleRate() const noexcept { return sampleRate; }

private:
    void computeMagnitudes (const std::vector<float>& history, int endOffset, std::vector<float>& magnitudes, float& rms);
    void updateDisplay (const std::vector<float>& magnitudes, std::array<float, displayPoints>& display) const;
    void toPeakProfile (const std::vector<float>& magnitudes, std::vector<float>& profile);

    double sampleRate = 0.0;
    int fftOrder = 13, fftSize = 1 << 13;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer;

    // The newest audio, as a ring buffer.
    std::vector<float> historyIn, historyOut;
    int historyMask = 0, writePos = 0;
    juce::int64 totalPushed = 0;

    std::vector<float> magIn, magOut;           // FFT magnitudes
    std::vector<float> profileIn, profileOut;   // peaks on the log-frequency grid
    std::vector<float> prefix;                  // scratch for the running mean
    std::vector<float> smoothedScores;          // match score per candidate shift, smoothed over time
    float lastExpected = 1.0e9f;

    std::array<float, displayPoints> displayIn {}, displayOut {};
};

} // namespace lowchug
