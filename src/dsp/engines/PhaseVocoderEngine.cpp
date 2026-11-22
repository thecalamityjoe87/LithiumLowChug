#include "PhaseVocoderEngine.h"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace lowchug::dsp
{

namespace
{
constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;

/** Wraps a phase into [-pi, pi). */
float wrapPhase (float phase)
{
    return phase - twoPi * std::floor ((phase + std::numbers::pi_v<float>) / twoPi);
}
} // namespace

struct PhaseVocoderEngine::Fft
{
    signalsmith::linear::RealFFT<float> fft;
};

PhaseVocoderEngine::PhaseVocoderEngine (Config c)
    : config (c), fft (std::make_unique<Fft>())
{
    assert (config.fftSize >= 256 && (config.fftSize & (config.fftSize - 1)) == 0);
    assert (config.overlap >= 4);
    assert (config.synthesisSize == 0 || (config.synthesisSize & (config.synthesisSize - 1)) == 0);

    char buffer[96];
    if (config.synthesisSize > 0 && config.synthesisSize < config.fftSize)
        std::snprintf (buffer, sizeof (buffer), "PV-lowdelay %d:%d/%dx%s", config.fftSize, config.synthesisSize,
                       config.overlap, config.transientReset ? "" : " noTR");
    else
        std::snprintf (buffer, sizeof (buffer), "PhaseVocoder %d/%dx%s",
                       config.fftSize, config.overlap, config.transientReset ? "" : " noTR");
    name = buffer;
}

PhaseVocoderEngine::~PhaseVocoderEngine() = default;

void PhaseVocoderEngine::prepare (double newSampleRate, int /*maxBlockSize*/, int newNumChannels)
{
    sampleRate = (float) newSampleRate;
    numChannels = newNumChannels;
    activeChannels = numChannels;

    // The config is specified at 48 kHz. At 88.2/96 kHz and above, scale the
    // FFT by the nearest power of two so the window (and latency) stays about
    // the same length in milliseconds. 44.1 kHz keeps the 48 kHz sizes (~9 %
    // longer in time), since FFT sizes must stay powers of two.
    fftSize = config.fftSize;
    if (sampleRate > 48000.0f * 1.5f)
        fftSize *= (int) std::lround (std::exp2 (std::round (std::log2 (sampleRate / 48000.0f))));

    synthesisSize = config.synthesisSize > 0 ? std::min (config.synthesisSize * fftSize / config.fftSize, fftSize) : fftSize;
    hop = synthesisSize / config.overlap;
    numBins = fftSize / 2;
    highBandStartBin = std::max (1, (int) std::lround (1500.0f * (float) fftSize / sampleRate));

    fft->fft.resize ((size_t) fftSize);
    makeWindows();

    channels.assign ((size_t) numChannels, {});
    for (auto& ch : channels)
    {
        ch.inFifo.resize ((size_t) fftSize);
        ch.outFifo.resize ((size_t) hop);
        ch.outAccumulator.resize ((size_t) synthesisSize);
        ch.spectrum.resize ((size_t) numBins);
        ch.shifted.resize ((size_t) numBins);
    }

    sumSpectrum.resize ((size_t) numBins);
    magnitude.resize ((size_t) numBins);
    previousMagnitude.resize ((size_t) numBins);
    preOnsetMagnitude.resize ((size_t) numBins);
    previousPhase.resize ((size_t) numBins);
    trueBin.resize ((size_t) numBins);
    rotation.resize ((size_t) numBins);
    previousRotation.resize ((size_t) numBins);
    logEnvelope.resize ((size_t) numBins);
    regions.reserve ((size_t) numBins); // a region per bin is the absolute maximum
    timeBuffer.resize ((size_t) fftSize);

    reset();
}

void PhaseVocoderEngine::makeWindows()
{
    const int n = fftSize, m = synthesisSize;
    analysisWindow.assign ((size_t) n, 0.0f);
    synthesisWindow.assign ((size_t) n, 0.0f);

    auto hann = [] (float x) { return 0.5f - 0.5f * std::cos (twoPi * x); }; // x in [0, 1)

    if (m == n)
    {
        // Symmetric: Hann analysis and synthesis windows.
        for (int i = 0; i < n; ++i)
            analysisWindow[(size_t) i] = synthesisWindow[(size_t) i] = hann ((float) i / (float) n);
        windowPeak = n / 2;
    }
    else
    {
        // Asymmetric low-delay pair. The analysis window rises slowly (a
        // half Hann) over most of the frame and falls quickly (a square-root
        // half Hann) over the newest m/2 samples, so it still sees a long
        // stretch of signal (good frequency resolution) but its peak sits near
        // "now". The synthesis window only covers the last m samples, chosen
        // so analysis x synthesis = an m-point Hann, which overlap-adds to a
        // constant for hops of m/4 or less. Output latency is then m, not n.
        //
        // A smoother (Hann-squared) product was tried: it measured worse on
        // low-end clarity (-5 dB harmonic-to-noise) and overall accuracy.
        windowPeak = n - m / 2;
        const float pi = std::numbers::pi_v<float>;
        for (int i = 0; i < n; ++i)
        {
            analysisWindow[(size_t) i] = i < windowPeak
                                             ? 0.5f - 0.5f * std::cos (pi * (float) i / (float) windowPeak)
                                             : std::sqrt (0.5f + 0.5f * std::cos (pi * float (i - windowPeak) / float (m / 2)));
        }
        for (int i = n - m; i < n; ++i)
        {
            const float h = hann (float (i - (n - m)) / (float) m);
            synthesisWindow[(size_t) i] = analysisWindow[(size_t) i] > 1.0e-6f ? h / analysisWindow[(size_t) i] : 0.0f;
        }
    }

    // Overlap-add gain: the sum of analysis x synthesis over all frames
    // touching one sample (constant by design). The FFT round trip adds N.
    float sumAll = 0.0f;
    for (int offset = 0; offset < hop; ++offset)
    {
        float s = 0.0f;
        for (int i = n - m + offset; i < n; i += hop)
            s += analysisWindow[(size_t) i] * synthesisWindow[(size_t) i];
        sumAll += s;
    }
    outputScale = 1.0f / ((float) fftSize * sumAll / (float) hop);
}

void PhaseVocoderEngine::reset()
{
    for (auto& ch : channels)
    {
        std::fill (ch.inFifo.begin(), ch.inFifo.end(), 0.0f);
        std::fill (ch.outFifo.begin(), ch.outFifo.end(), 0.0f);
        std::fill (ch.outAccumulator.begin(), ch.outAccumulator.end(), 0.0f);
    }
    std::fill (previousPhase.begin(), previousPhase.end(), 0.0f);
    std::fill (previousRotation.begin(), previousRotation.end(), 0.0f);
    std::fill (previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
    previousHighBandEnergy = 0.0f;
    transientHoldoff = 0;
    framesUntilReset = -1;
    ratio = pendingRatio;

    // Streaming layout: new input is written at [N - hop, N), and a frame is
    // processed each time that span fills up.
    rover = fftSize - hop;
}

void PhaseVocoderEngine::process (const float* const* input, float* const* output,
                                  int numChannelsToProcess, int numSamples)
{
    const int writeStart = fftSize - hop;
    const int channelCount = std::min (numChannelsToProcess, numChannels);
    activeChannels = channelCount;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < channelCount; ++c)
        {
            auto& ch = channels[(size_t) c];
            ch.inFifo[(size_t) rover] = input[c][i];
            output[c][i] = ch.outFifo[(size_t) (rover - writeStart)];
        }

        if (++rover >= fftSize)
        {
            rover = writeStart;
            processFrame();
        }
    }
}

bool PhaseVocoderEngine::copyChannelState (int from, int to)
{
    if (from < 0 || to < 0 || from >= numChannels || to >= numChannels)
        return false;

    // Copying into buffers of the same size: no allocation.
    auto& src = channels[(size_t) from];
    auto& dst = channels[(size_t) to];
    std::copy (src.inFifo.begin(), src.inFifo.end(), dst.inFifo.begin());
    std::copy (src.outFifo.begin(), src.outFifo.end(), dst.outFifo.begin());
    std::copy (src.outAccumulator.begin(), src.outAccumulator.end(), dst.outAccumulator.begin());
    return true;
}

void PhaseVocoderEngine::processFrame()
{
    ratio = pendingRatio;

    // Analysis: window, rotate to zero phase, FFT each channel.
    for (int c = 0; c < activeChannels; ++c)
    {
        auto& ch = channels[(size_t) c];
        // Rotating the frame so the window's peak lands at index 0 makes the
        // FFT phases "zero-phase" relative to that moment.
        for (int i = 0; i < fftSize; ++i)
            timeBuffer[(size_t) ((i - windowPeak) & (fftSize - 1))] = ch.inFifo[(size_t) i] * analysisWindow[(size_t) i];

        fft->fft.fft (timeBuffer.data(), ch.spectrum.data());
        ch.spectrum[0] = { ch.spectrum[0].real(), 0.0f }; // drop the packed Nyquist bin
    }

    analyzeSharedSpectrum();

    // An onset first shows up faintly at the window's edge. Its phases are
    // reset a few frames later, when it reaches the window peak, so that the
    // frame with the most weight carries the attack with its original shape.
    bool resetNow = false;
    if (config.transientReset)
    {
        if (detectTransient())
        {
            framesUntilReset = config.overlap / 2;
            std::copy (previousMagnitude.begin(), previousMagnitude.end(), preOnsetMagnitude.begin());
        }
        if (framesUntilReset >= 0)
            resetNow = (framesUntilReset-- == 0);
    }
    std::copy (magnitude.begin(), magnitude.end(), previousMagnitude.begin());

    findPeaksAndRegions();
    computeRegionShifts (resetNow);

    for (int c = 0; c < activeChannels; ++c)
        synthesiseChannel (c);

    // Slide the input history along by one hop.
    for (int c = 0; c < activeChannels; ++c)
    {
        auto& fifo = channels[(size_t) c].inFifo;
        std::copy (fifo.begin() + hop, fifo.end(), fifo.begin());
    }
}

void PhaseVocoderEngine::analyzeSharedSpectrum()
{
    std::fill (sumSpectrum.begin(), sumSpectrum.end(), Complex {});
    for (int c = 0; c < activeChannels; ++c)
        for (int k = 0; k < numBins; ++k)
            sumSpectrum[(size_t) k] += channels[(size_t) c].spectrum[(size_t) k];

    // The phase advance between frames gives each bin's true frequency, far
    // more precisely than the bin spacing.
    const float expectedAdvancePerBin = twoPi * (float) hop / (float) fftSize;
    const float binsPerRadian = (float) fftSize / (twoPi * (float) hop);

    for (int k = 0; k < numBins; ++k)
    {
        const auto value = sumSpectrum[(size_t) k];
        const float phase = std::arg (value);
        magnitude[(size_t) k] = std::abs (value);

        const float deviation = wrapPhase (phase - previousPhase[(size_t) k] - expectedAdvancePerBin * (float) k);
        trueBin[(size_t) k] = (float) k + deviation * binsPerRadian;
        previousPhase[(size_t) k] = phase;
    }
}

bool PhaseVocoderEngine::detectTransient()
{
    // A pick attack is broadband: compare energy above ~1.5 kHz with the
    // previous frame. Sustained notes have falling high-band energy, so a jump
    // is a reliable onset cue for guitar.
    float energy = 0.0f, total = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        const float e = magnitude[(size_t) k] * magnitude[(size_t) k];
        total += e;
        if (k >= highBandStartBin)
            energy += e;
    }

    // Floors relative to the whole frame, so detection works at any input
    // level: ignore high bands more than 70 dB below the frame's energy.
    const float floor = 1.0e-7f * total + 1.0e-20f;
    const float ratioDb = 10.0f * std::log10 ((energy + floor) / (previousHighBandEnergy + floor));
    previousHighBandEnergy = energy;

    if (transientHoldoff > 0)
    {
        --transientHoldoff;
        return false;
    }

    if (ratioDb > config.transientThresholdDb && energy > 10.0f * floor)
    {
        transientHoldoff = config.overlap; // at most one reset per window length
        return true;
    }
    return false;
}

void PhaseVocoderEngine::findPeaksAndRegions()
{
    regions.clear();

    const float maxMagnitude = *std::max_element (magnitude.begin(), magnitude.end());
    const float threshold = std::max (maxMagnitude * 1.0e-4f, 1.0e-6f); // -80 dB below the frame peak

    // Local maxima over +/- 2 bins.
    for (int k = 2; k < numBins - 2; ++k)
    {
        const float m = magnitude[(size_t) k];
        if (m > threshold && m > magnitude[(size_t) k - 1] && m >= magnitude[(size_t) k + 1]
            && m > magnitude[(size_t) k - 2] && m >= magnitude[(size_t) k + 2])
        {
            regions.push_back ({ k, 0, 0, 0, 0.0f, 0.0f, 1.0f });
        }
    }

    if (regions.empty())
        return;

    // Region boundaries sit at the quietest bin between neighboring peaks.
    regions.front().start = 0;
    for (size_t r = 1; r < regions.size(); ++r)
    {
        const int a = regions[r - 1].peak, b = regions[r].peak;
        int quietest = a + 1;
        for (int k = a + 1; k < b; ++k)
            if (magnitude[(size_t) k] < magnitude[(size_t) quietest])
                quietest = k;

        regions[r - 1].end = quietest;
        regions[r].start = quietest;
    }
    regions.back().end = numBins;
}

void PhaseVocoderEngine::computeRegionShifts (bool resetPhases)
{
    if (regions.empty())
    {
        // Silence: nothing to carry over to the next frame.
        std::fill (previousRotation.begin(), previousRotation.end(), 0.0f);
        return;
    }

    if (preserveFormants)
    {
        // Spectral envelope: straight lines (in dB) between the peaks.
        auto logMag = [this] (int k) { return std::log (magnitude[(size_t) k] + 1.0e-9f); };
        for (int k = 0; k <= regions.front().peak; ++k)
            logEnvelope[(size_t) k] = logMag (regions.front().peak);
        for (size_t r = 1; r < regions.size(); ++r)
        {
            const int a = regions[r - 1].peak, b = regions[r].peak;
            const float la = logMag (a), lb = logMag (b);
            for (int k = a; k <= b; ++k)
                logEnvelope[(size_t) k] = la + (lb - la) * float (k - a) / float (b - a);
        }
        for (int k = regions.back().peak; k < numBins; ++k)
            logEnvelope[(size_t) k] = logMag (regions.back().peak);
    }

    const float rotationPerBin = (ratio - 1.0f) * twoPi * (float) hop / (float) fftSize;

    for (auto& region : regions)
    {
        const float peakTrueBin = trueBin[(size_t) region.peak];
        // Shift by the frequency DIFFERENCE, so ratio 1 gives exactly 0 even
        // where the frequency estimate is off by more than half a bin. The
        // shift is fractional: rounding it to whole bins would leave every
        // synthesised grain up to half a bin off-frequency, which overlap-adds
        // into broadband splatter on fast-decaying notes (palm mutes).
        const float exactShift = peakTrueBin * (ratio - 1.0f);
        const float wholeBins = std::floor (exactShift);
        region.shift = (int) wholeBins;
        region.fraction = exactShift - wholeBins;

        // The output partial must advance by (ratio * frequency) per hop; the
        // input bins already advance by (frequency), so add the difference.
        // Reading the previous rotation at this peak's bin carries the phase
        // of whichever region held this partial last frame (implicit peak tracking).
        region.rotation = wrapPhase (previousRotation[(size_t) region.peak] + rotationPerBin * peakTrueBin);

        if (resetPhases)
        {
            // Only regions the onset actually brought in (energy up >= 6 dB on
            // the pre-onset frame) restart from the input phases; notes that
            // are still ringing keep their phase continuity.
            float now = 0.0f, before = 0.0f;
            for (int k = region.start; k < region.end; ++k)
            {
                now += magnitude[(size_t) k] * magnitude[(size_t) k];
                before += preOnsetMagnitude[(size_t) k] * preOnsetMagnitude[(size_t) k];
            }
            if (now > 4.0f * before)
                region.rotation = 0.0f;
        }

        region.gain = 1.0f;
        if (preserveFormants)
        {
            // Re-apply the envelope at the destination: gain = env(dest) / env(source).
            const int dest = std::clamp (region.peak + region.shift + (region.fraction >= 0.5f ? 1 : 0), 0, numBins - 1);
            const float logGain = std::clamp (logEnvelope[(size_t) dest] - logEnvelope[(size_t) region.peak], -2.8f, 2.8f);
            region.gain = std::exp (logGain);
        }

        for (int k = region.start; k < region.end; ++k)
            rotation[(size_t) k] = region.rotation;
    }

    std::swap (rotation, previousRotation);
}

void PhaseVocoderEngine::synthesiseChannel (int channelIndex)
{
    auto& ch = channels[(size_t) channelIndex];
    std::fill (ch.shifted.begin(), ch.shifted.end(), Complex {});

    for (const auto& region : regions)
    {
        const auto twist = std::polar (region.gain, region.rotation);
        const auto lowerTwist = twist * (1.0f - region.fraction);
        const auto upperTwist = twist * region.fraction;
        const int first = std::max (region.start, -region.shift);
        const int last = std::min (region.end, numBins - region.shift - 1);

        // Fractional placement: each source bin is split between the two
        // destination bins either side of its exact new position (linear
        // interpolation, accurate near the window center where the weight is).
        // Regions can overlap after shifting (downward shifts squeeze them
        // together), so accumulate rather than overwrite.
        for (int k = first; k < last; ++k)
        {
            const auto value = ch.spectrum[(size_t) k];
            ch.shifted[(size_t) (k + region.shift)] += value * lowerTwist;
            ch.shifted[(size_t) (k + region.shift + 1)] += value * upperTwist;
        }
    }

    fft->fft.ifft (ch.shifted.data(), timeBuffer.data());

    // Only the last synthesisSize samples of the frame are non-zero after the
    // synthesis window; overlap-add those.
    const int first = fftSize - synthesisSize;
    for (int i = first; i < fftSize; ++i)
        ch.outAccumulator[(size_t) (i - first)] += timeBuffer[(size_t) ((i - windowPeak) & (fftSize - 1))]
                                                   * synthesisWindow[(size_t) i] * outputScale;

    std::copy (ch.outAccumulator.begin(), ch.outAccumulator.begin() + hop, ch.outFifo.begin());
    std::copy (ch.outAccumulator.begin() + hop, ch.outAccumulator.end(), ch.outAccumulator.begin());
    std::fill (ch.outAccumulator.end() - hop, ch.outAccumulator.end(), 0.0f);
}

} // namespace lowchug::dsp
