#include "ShiftAnalyzer.h"

#include <cmath>

namespace lowchug
{

namespace
{
// The measurement grid: log frequency from 70 Hz (below a drop-tuned low
// string) to 4 kHz, in 5-cent steps. Higher harmonics carry most of the
// precision (a cent is a wider slice of Hz up there), so the grid reaches
// well above the fundamentals.
constexpr float gridMinHz = 70.0f;
constexpr float gridMaxHz = 4000.0f;
constexpr int binsPerSemitone = 20;   // 5 cents
const int gridSize = (int) std::floor (12.0f * binsPerSemitone * std::log2 (gridMaxHz / gridMinHz)) + 1;

// Look this far either side of the expected shift: 1.5 semitones.
constexpr int searchRadius = 30;

// The "is this a peak?" baseline: the mean over 2 semitones either side.
constexpr int baselineRadius = 2 * binsPerSemitone;

// Longest latency we allow for (Studio mode at 192 kHz, with headroom).
constexpr int maxLatency = 16384;

float magnitudeAt (const std::vector<float>& magnitudes, float bin)
{
    const auto i = (int) bin;
    if (i < 0 || i + 1 >= (int) magnitudes.size())
        return 0.0f;
    const auto frac = bin - (float) i;
    return magnitudes[(size_t) i] + frac * (magnitudes[(size_t) i + 1] - magnitudes[(size_t) i]);
}
} // namespace

float ShiftAnalyzer::displayPosition (float hz)
{
    return std::log (hz / displayMinHz) / std::log (displayMaxHz / displayMinHz);
}

void ShiftAnalyzer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // About 170 ms of audio per analysis at any sample rate: long enough to
    // resolve the low strings, short enough to follow your playing.
    fftOrder = sampleRate <= 50000.0 ? 13 : (sampleRate <= 100000.0 ? 14 : 15);
    fftSize = 1 << fftOrder;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
                                                              juce::dsp::WindowingFunction<float>::hann, false);
    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);

    const auto historySize = juce::nextPowerOfTwo (fftSize + maxLatency + 1);
    historyIn.assign ((size_t) historySize, 0.0f);
    historyOut.assign ((size_t) historySize, 0.0f);
    historyMask = historySize - 1;
    writePos = 0;
    totalPushed = 0;

    magIn.assign ((size_t) fftSize / 2 + 1, 0.0f);
    magOut.assign ((size_t) fftSize / 2 + 1, 0.0f);
    profileIn.assign ((size_t) gridSize, 0.0f);
    profileOut.assign ((size_t) gridSize, 0.0f);
    prefix.assign ((size_t) gridSize + 1, 0.0f);
    smoothedScores.clear();
    displayIn.fill (0.0f);
    displayOut.fill (0.0f);
}

void ShiftAnalyzer::push (const float* in, const float* out, int numSamples)
{
    if (historyMask == 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        historyIn[(size_t) writePos] = in[i];
        historyOut[(size_t) writePos] = out[i];
        writePos = (writePos + 1) & historyMask;
    }
    totalPushed += numSamples;
}

void ShiftAnalyzer::computeMagnitudes (const std::vector<float>& history, int endOffset,
                                       std::vector<float>& magnitudes, float& rms)
{
    // The fftSize samples that end `endOffset` samples before the newest one.
    const int start = writePos - endOffset - fftSize;
    double sumSquares = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto x = history[(size_t) ((start + i) & historyMask)];
        sumSquares += (double) x * x;
        fftBuffer[(size_t) i] = x * window[(size_t) i];
    }
    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);
    rms = (float) std::sqrt (sumSquares / fftSize);

    fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    // Scaled so a full-scale sine reads about 1 (a Hann window keeps half
    // the amplitude, and the FFT sums fftSize / 2 of it into its bin).
    const auto scale = 4.0f / (float) fftSize;
    for (size_t k = 0; k < magnitudes.size(); ++k)
        magnitudes[k] = fftBuffer[k] * scale;
}

void ShiftAnalyzer::updateDisplay (const std::vector<float>& magnitudes, std::array<float, displayPoints>& display) const
{
    const auto binsPerHz = (float) fftSize / (float) sampleRate;
    for (int p = 0; p < displayPoints; ++p)
    {
        const auto hz = displayMinHz * std::pow (displayMaxHz / displayMinHz, (float) p / (float) (displayPoints - 1));
        const auto db = juce::Decibels::gainToDecibels (magnitudeAt (magnitudes, hz * binsPerHz), -120.0f);
        const auto level = juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 84.0f);

        // Rise at once, fall gently: steadier to look at.
        auto& shown = display[(size_t) p];
        shown = level > shown ? level : shown + 0.25f * (level - shown);
    }
}

void ShiftAnalyzer::toPeakProfile (const std::vector<float>& magnitudes, std::vector<float>& profile)
{
    // Log magnitude on the log-frequency grid...
    const auto binsPerHz = (float) fftSize / (float) sampleRate;
    for (int k = 0; k < gridSize; ++k)
    {
        const auto hz = gridMinHz * std::exp2 ((float) k / (12.0f * binsPerSemitone));
        profile[(size_t) k] = std::log (magnitudeAt (magnitudes, hz * binsPerHz) + 1.0e-6f);
    }

    // ...minus its local average, keeping only what sticks up: the peaks
    // (harmonics). That removes the overall tone (a dark or bright guitar,
    // the mix of dry signal) so only the harmonic positions are compared.
    prefix[0] = 0.0f;
    for (int k = 0; k < gridSize; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + profile[(size_t) k];

    for (int k = 0; k < gridSize; ++k)
    {
        const auto lo = std::max (0, k - baselineRadius), hi = std::min (gridSize, k + baselineRadius + 1);
        const auto mean = (prefix[(size_t) hi] - prefix[(size_t) lo]) / (float) (hi - lo);
        profile[(size_t) k] = std::max (0.0f, profile[(size_t) k] - mean);
    }
}

ShiftAnalyzer::Result ShiftAnalyzer::update (int latencySamples, float expectedSemitones)
{
    latencySamples = juce::jlimit (0, maxLatency, latencySamples);

    if (fft == nullptr || totalPushed < (juce::int64) (fftSize + latencySamples + 1))
        return {};

    // The output, and the input that produced it (latencySamples earlier).
    float rmsIn = 0.0f, rmsOut = 0.0f;
    computeMagnitudes (historyOut, 0, magOut, rmsOut);
    computeMagnitudes (historyIn, latencySamples, magIn, rmsIn);
    updateDisplay (magIn, displayIn);
    updateDisplay (magOut, displayOut);

    // Nothing (or only noise) coming in: nothing to measure.
    if (juce::Decibels::gainToDecibels (rmsIn) < -60.0f || juce::Decibels::gainToDecibels (rmsOut) < -66.0f)
    {
        smoothedScores.clear();
        return {};
    }

    toPeakProfile (magIn, profileIn);
    toPeakProfile (magOut, profileOut);

    // When the controls change, start the averaging afresh.
    const bool fresh = smoothedScores.empty() || std::abs (expectedSemitones - lastExpected) > 0.2f;
    lastExpected = expectedSemitones;
    if (fresh)
        smoothedScores.assign ((size_t) searchRadius * 2 + 1, 0.0f);

    // Slide the input profile along the output profile; score each shift by
    // normalized correlation (1 = identical peaks, 0 = nothing in common).
    // Output peaks sit `lag` grid steps from the input peaks that made them.
    const int centerLag = juce::roundToInt (expectedSemitones * binsPerSemitone);
    for (int j = 0; j <= 2 * searchRadius; ++j)
    {
        const int lag = centerLag - searchRadius + j;
        double dot = 0.0, energyIn = 0.0, energyOut = 0.0;
        for (int k = std::max (0, lag); k < std::min (gridSize, gridSize + lag); ++k)
        {
            const double o = profileOut[(size_t) k];
            const double i = profileIn[(size_t) (k - lag)];
            dot += o * i;
            energyIn += i * i;
            energyOut += o * o;
        }
        const auto score = (float) (dot / std::sqrt (energyIn * energyOut + 1.0e-12));
        auto& smoothed = smoothedScores[(size_t) j];
        smoothed = fresh ? score : smoothed + 0.4f * (score - smoothed);
    }

    const auto best = (int) std::distance (smoothedScores.begin(), std::max_element (smoothedScores.begin(), smoothedScores.end()));

    Result result;
    result.confidence = smoothedScores[(size_t) best];

    // Best at the edge of the search: the real shift is further away (or
    // there is no clear match at all).
    if (best == 0 || best == 2 * searchRadius || result.confidence < 0.3f)
    {
        result.state = Result::State::searching;
        return result;
    }

    // Fit a parabola through the best score and its neighbors for a
    // position finer than the 5-cent grid.
    const auto y0 = smoothedScores[(size_t) best - 1], y1 = smoothedScores[(size_t) best], y2 = smoothedScores[(size_t) best + 1];
    const auto curvature = y0 - 2.0f * y1 + y2;
    const auto offset = curvature < 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / curvature) : 0.0f;

    result.state = Result::State::measured;
    result.measuredSemitones = ((float) (centerLag - searchRadius + best) + offset) / (float) binsPerSemitone;
    return result;
}

} // namespace lowchug
