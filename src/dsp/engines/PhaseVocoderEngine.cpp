#include "PhaseVocoderEngine.h"

#include "../Profiling.h"

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
    // The synthesis window needn't be a power of two, but its hop
    // (synthesisSize / overlap) and half (synthesisSize / 2) must be whole.
    assert (config.synthesisSize == 0 || config.synthesisSize % (2 * config.overlap) == 0);

    // A name that says how it differs from the shipping defaults.
    const char* model = config.partialModel ? "" : " noModel";
    const char* transients = ! config.transientReset ? " noTR" : (config.onsetMask ? "" : " noMask");
    char buffer[128];
    if (config.synthesisSize > 0 && config.synthesisSize < config.fftSize)
        std::snprintf (buffer, sizeof (buffer), "PV-lowdelay %d:%d/%dx%s%s%s", config.fftSize, config.synthesisSize,
                       config.overlap, transients, model, config.splitComputation ? " split" : "");
    else
        std::snprintf (buffer, sizeof (buffer), "PhaseVocoder %d/%dx%s", config.fftSize, config.overlap, transients);
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
    makeWindowSpectrum();

    channels.assign ((size_t) numChannels, {});
    for (auto& ch : channels)
    {
        ch.inFifo.resize ((size_t) fftSize);
        ch.frame.resize ((size_t) fftSize);
        ch.outFifo.resize ((size_t) hop);
        ch.outAccumulator.resize ((size_t) synthesisSize);
        ch.spectrum.resize ((size_t) numBins);
        ch.shifted.resize ((size_t) numBins);
        ch.onsetShifted.resize ((size_t) numBins);
    }

    sumSpectrum.resize ((size_t) numBins);
    magnitude.resize ((size_t) numBins);
    previousMagnitude.resize ((size_t) numBins);
    preOnsetMagnitude.resize ((size_t) numBins);
    maskBaseMagnitude.assign ((size_t) numBins, 0.0f);
    // 25 ms of detector blocks (32 samples at 48 kHz, scaled like the FFT).
    onsetHistory.assign ((size_t) std::max (1, (int) std::lround (0.025 * sampleRate / std::max (16, 32 * fftSize / config.fftSize))), 0.0f);
    previousSumSpectrum.resize ((size_t) numBins);
    trueBin.resize ((size_t) numBins);
    rotation.resize ((size_t) numBins);
    previousRotation.resize ((size_t) numBins);
    logEnvelope.resize ((size_t) numBins);
    envelopeScratch.resize ((size_t) numBins);
    regions.reserve ((size_t) numBins); // a region per bin is the absolute maximum
    regionOfBin.assign ((size_t) numBins, 0);
    modeled.reserve ((size_t) numBins);
    modelAmplitude.assign ((size_t) maxModeled, Complex {});
    modelReach.assign ((size_t) maxModeled, 0);
    cleaned.assign ((size_t) maxModeled * 3, Complex {});
    couplingPositive.assign ((size_t) (maxModeled * maxModeled * 3), Complex {});
    couplingMirror.assign ((size_t) (maxModeled * maxModeled * 3), Complex {});
    residual.assign ((size_t) numBins, Complex {});
    cleanedAtBin.assign ((size_t) numBins, Complex {});
    previousCleanedAtBin.assign ((size_t) numBins, Complex {});
    timeBuffer.resize ((size_t) fftSize);
    onsetTimeBuffer.resize ((size_t) fftSize);

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
        //
        // (In milestone 3 it was also tried with zero-slope (Hann) edges, to
        // lower the window's spectral skirt: worse again in Live. The treble
        // residue came from how low partials were moved, not from the
        // window; see the partial model.)
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
        std::fill (ch.frame.begin(), ch.frame.end(), 0.0f);
    }
    framePending = false;
    frameChannels = 0;
    stepsDone = 0;
    std::fill (sumSpectrum.begin(), sumSpectrum.end(), Complex {});
    std::fill (previousSumSpectrum.begin(), previousSumSpectrum.end(), Complex {});
    std::fill (previousRotation.begin(), previousRotation.end(), 0.0f);
    std::fill (previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
    std::fill (previousCleanedAtBin.begin(), previousCleanedAtBin.end(), Complex {});
    previousHighBandEnergy = 0.0f;
    transientHoldoff = 0;
    framesUntilReset = -1;
    masking = false;
    streamPosition = frameEndPosition = 0;
    lastOnset = maskedOnset = -1;
    {
        // The detector's high-pass at 2 kHz (RBJ biquad) and block size.
        auto& d = detector;
        const float w = twoPi * 2000.0f / sampleRate, alpha = std::sin (w) / (2.0f * 0.7071f), cw = std::cos (w);
        const float a0 = 1.0f + alpha;
        d = {};
        d.b0 = (1.0f + cw) / 2.0f / a0;
        d.b1 = -(1.0f + cw) / a0;
        d.b2 = d.b0;
        d.a1 = -2.0f * cw / a0;
        d.a2 = (1.0f - alpha) / a0;
        d.blockSize = std::max (16, 32 * fftSize / config.fftSize);
        std::fill (onsetHistory.begin(), onsetHistory.end(), 0.0f);
    }
    ratio = pendingRatio;

    // Streaming layout: new input is written at [N - hop, N), and a frame is
    // processed each time that span fills up.
    rover = fftSize - hop;
}

void PhaseVocoderEngine::process (const float* const* input, float* const* output,
                                  int numChannelsToProcess, int numSamples)
{
    LOWCHUG_PROFILE_SCOPE (buffering); // compiled out unless profiling (see Profiling.h)
    const int writeStart = fftSize - hop;
    const int channelCount = std::min (numChannelsToProcess, numChannels);
    activeChannels = channelCount;

    for (int i = 0; i < numSamples;)
    {
        // Up to the next frame boundary: new input goes in at [N - hop, N),
        // and the output FIFO plays out the hop that was finished last.
        const int n = std::min (numSamples - i, fftSize - rover);
        for (int c = 0; c < channelCount; ++c)
        {
            auto& ch = channels[(size_t) c];
            // A NaN or infinity from the host (or a broken plugin before us)
            // counts as silence: once in the analysis it would poison every
            // frame it touches, and the spectra and phases kept from frame
            // to frame. (Milestone 5.)
            for (int j = 0; j < n; ++j)
            {
                const float x = input[c][i + j];
                ch.inFifo[(size_t) (rover + j)] = std::isfinite (x) ? x : 0.0f;
            }
            std::copy (ch.outFifo.begin() + (rover - writeStart), ch.outFifo.begin() + (rover - writeStart) + n, output[c] + i);
        }
        if (config.onsetMask)
        {
            const float* offsetInput[16];
            for (int c = 0; c < channelCount && c < 16; ++c)
                offsetInput[c] = input[c] + i;
            detectOnsets (offsetInput, std::min (channelCount, 16), n);
        }
        streamPosition += n;
        rover += n;
        i += n;

        if (rover >= fftSize)
        {
            rover = writeStart;
            if (config.splitComputation)
            {
                // The previous frame (computed over the hop that just
                // ended) is finished and played out; the new one starts.
                finishPendingFrame();
                emitHop();
                startFrame();
            }
            else
            {
                // All at once: the new frame overlap-adds straight away, so
                // its first hop plays out from this boundary.
                startFrame();
                emitHop();
            }
        }
        else if (framePending)
        {
            // Split computation: keep pace with the hop, so each block does
            // its share of the frame. Rounding up leaves the boundary block
            // (which also captures the next frame) the least to do.
            const int progress = rover - writeStart;
            runSteps ((totalSteps() * progress + hop - 1) / hop);
        }
    }
}

bool PhaseVocoderEngine::copyChannelState (int from, int to)
{
    if (from < 0 || to < 0 || from >= numChannels || to >= numChannels)
        return false;

    // A split frame captured with fewer channels would never synthesize
    // `to`: finish it now, so the copy below includes its output. (This is
    // a one-off when linked channels start to differ.)
    if (framePending && to >= frameChannels)
        runSteps (totalSteps());

    // Copying into buffers of the same size: no allocation.
    auto& src = channels[(size_t) from];
    auto& dst = channels[(size_t) to];
    std::copy (src.inFifo.begin(), src.inFifo.end(), dst.inFifo.begin());
    std::copy (src.outFifo.begin(), src.outFifo.end(), dst.outFifo.begin());
    std::copy (src.outAccumulator.begin(), src.outAccumulator.end(), dst.outAccumulator.begin());
    std::copy (src.frame.begin(), src.frame.end(), dst.frame.begin());
    std::copy (src.spectrum.begin(), src.spectrum.end(), dst.spectrum.begin());
    return true;
}

void PhaseVocoderEngine::detectOnsets (const float* const* input, int count, int numSamples)
{
    LOWCHUG_PROFILE_SCOPE (onsetDetector);
    // Pick attacks, found in the time domain, where their timing is sharp:
    // the energy above 2 kHz in short blocks (32 samples at 48 kHz),
    // compared with the loudest block of the last 25 ms. A long analysis
    // frame only sees an attack arrive gradually (and, during fast chugs, on
    // top of the last one's treble), so it can't time it, or even always
    // notice it. The 25 ms matters: a low note's treble comes in a burst
    // once per cycle (12 ms for a low E, 18 ms for an A an octave down), and
    // each burst must not look like a new pick.
    auto& d = detector;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = 0.0f; // the channels' average (see analyzeSharedSpectrum)
        for (int c = 0; c < count; ++c)
            x += input[c][i];
        x /= (float) count;
        if (! std::isfinite (x))
            x = 0.0f; // the high-pass below is recursive: a NaN would stay in it forever, and no onset would ever be found again
        const float y = d.b0 * x + d.b1 * d.x1 + d.b2 * d.x2 - d.a1 * d.y1 - d.a2 * d.y2;
        d.x2 = d.x1; d.x1 = x; d.y2 = d.y1; d.y1 = y;
        d.highEnergy += y * y;
        d.allEnergy += x * x;

        if (++d.filled < d.blockSize)
            continue;

        float background = 0.0f;
        for (float e : onsetHistory)
            background = std::max (background, e);
        // An onset: the treble jumps to 4x (6 dB) the loudest of the last
        // 25 ms, and isn't negligible against the block's whole level.
        if (d.holdoff <= 0 && d.highEnergy > 4.0f * background + 1.0e-12f && d.highEnergy > 1.0e-6f * d.allEnergy)
        {
            lastOnset = streamPosition + i - d.blockSize / 2; // the block's middle
            d.holdoff = 20 * d.blockSize;                     // ~13 ms
        }
        d.holdoff -= d.blockSize;
        onsetHistory[(size_t) d.recentIndex] = d.highEnergy;
        d.recentIndex = (d.recentIndex + 1) % (int) onsetHistory.size();
        d.highEnergy = d.allEnergy = 0.0f;
        d.filled = 0;
    }
}

void PhaseVocoderEngine::startFrame()
{
    ratio = pendingRatio;
    frameChannels = activeChannels;
    frameEndPosition = streamPosition;

    for (int c = 0; c < frameChannels; ++c)
    {
        auto& ch = channels[(size_t) c];

        // Window the newest fftSize samples, rotated so the window's peak
        // lands at index 0: the FFT phases are then "zero-phase" relative to
        // that moment.
        for (int i = 0; i < fftSize; ++i)
            ch.frame[(size_t) ((i - windowPeak) & (fftSize - 1))] = ch.inFifo[(size_t) i] * analysisWindow[(size_t) i];

        // Slide the input history along by one hop.
        std::copy (ch.inFifo.begin() + hop, ch.inFifo.end(), ch.inFifo.begin());
    }

    stepsDone = 0;
    framePending = true;

    if (! config.splitComputation)
        runSteps (totalSteps());
}

void PhaseVocoderEngine::finishPendingFrame()
{
    if (framePending)
        runSteps (totalSteps());
}

void PhaseVocoderEngine::runSteps (int upTo)
{
    upTo = std::min (upTo, totalSteps());
    while (stepsDone < upTo)
        runStep (stepsDone++);
    if (stepsDone >= totalSteps())
        framePending = false;
}

void PhaseVocoderEngine::runStep (int step)
{
    if (step < frameChannels)
    {
        LOWCHUG_PROFILE_SCOPE (forwardFft);
        auto& ch = channels[(size_t) step];
        fft->fft.fft (ch.frame.data(), ch.spectrum.data());
        ch.spectrum[0] = { ch.spectrum[0].real(), 0.0f }; // drop the packed Nyquist bin
    }
    else if (step == frameChannels)
    {
        analyzeFrame();
    }
    else
    {
        synthesizeChannel (step - frameChannels - 1);
    }
}

void PhaseVocoderEngine::emitHop()
{
    // The first hop of the overlap-add accumulator is complete (no later
    // frame reaches back into it): move it to the output FIFO. Every
    // channel shifts, so an idle (linked) channel stays aligned too.
    for (auto& ch : channels)
    {
        std::copy (ch.outAccumulator.begin(), ch.outAccumulator.begin() + hop, ch.outFifo.begin());
        std::copy (ch.outAccumulator.begin() + hop, ch.outAccumulator.end(), ch.outAccumulator.begin());
        std::fill (ch.outAccumulator.end() - hop, ch.outAccumulator.end(), 0.0f);
    }
}

void PhaseVocoderEngine::analyzeFrame()
{
    LOWCHUG_PROFILE_SCOPE (analysis);
    analyzeSharedSpectrum();

    // An onset first shows up faintly at the window's edge. Its phases are
    // reset a few frames later, when it reaches the window peak, so that the
    // frame with the most weight carries the attack with its original shape.
    //
    // (Milestone 3 also tried re-deriving onset phases on every frame from
    // the phase gradient across frequency, so the attack would keep its time
    // in each frame. It measured slightly worse overall than this reset.)
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

    if (config.onsetMask)
    {
        // A new onset (found in the time domain as the samples came in):
        // remember the spectrum from just before it, to tell which regions
        // it brought in.
        if (lastOnset > maskedOnset && lastOnset <= frameEndPosition)
        {
            maskedOnset = lastOnset;
            masking = true;
            std::copy (previousMagnitude.begin(), previousMagnitude.end(), maskBaseMagnitude.begin());
        }
        // Its time relative to this frame's window peak. Masking ends once it
        // is older than the whole synthesis span (synthesisSize / 2 before
        // the peak onwards).
        const long long peakPosition = frameEndPosition - (fftSize - windowPeak);
        onsetTime = (float) (maskedOnset - peakPosition);
        if (masking && onsetTime < -(float) (synthesisSize / 2))
            masking = false;
    }
    std::copy (magnitude.begin(), magnitude.end(), previousMagnitude.begin());

    findPeaksAndRegions();
    measurePeakFrequencies();
    computeRegionShifts (resetNow, masking);
}

void PhaseVocoderEngine::analyzeSharedSpectrum()
{
    // The average of the frame's channels (not the sum): identical channels
    // then analyze exactly like one, so linking them (processing just one,
    // see Detuner) or unlinking them never changes a level the analysis
    // compares from frame to frame. The last frame's spectrum is kept for
    // measurePeakFrequencies.
    std::swap (sumSpectrum, previousSumSpectrum);
    std::fill (sumSpectrum.begin(), sumSpectrum.end(), Complex {});
    const float perChannel = 1.0f / (float) std::max (1, frameChannels);
    for (int c = 0; c < frameChannels; ++c)
        for (int k = 0; k < numBins; ++k)
            sumSpectrum[(size_t) k] += channels[(size_t) c].spectrum[(size_t) k] * perChannel;

    for (int k = 0; k < numBins; ++k)
        magnitude[(size_t) k] = std::abs (sumSpectrum[(size_t) k]);
}

void PhaseVocoderEngine::measurePeakFrequencies()
{
    // The phase advance between frames gives each peak's true frequency, far
    // more precisely than the bin spacing. Everything downstream reads it
    // at peak bins only, so it is measured there only: one atan2 per peak
    // instead of one per bin (milestone 5; the per-bin version was most of
    // the analysis time).
    //
    // The advance is arg(now x conj(before)), the same as arg(now) -
    // arg(before) wrapped. A bin that was exactly zero last frame (after a
    // reset, or silence) counts as phase 0, as std::arg(0) gives. Double
    // precision, so the product can't underflow for very quiet peaks.
    const double expectedAdvancePerBin = 2.0 * std::numbers::pi * (double) hop / (double) fftSize;
    const double binsPerRadian = 1.0 / expectedAdvancePerBin;

    for (const auto& region : regions)
    {
        const auto k = (size_t) region.peak;
        const std::complex<double> now = sumSpectrum[k], before = previousSumSpectrum[k];
        const double advance = std::norm (before) > 0.0 ? std::arg (now * std::conj (before)) : std::arg (now);
        const double deviation = std::remainder (advance - expectedAdvancePerBin * (double) k, 2.0 * std::numbers::pi);
        trueBin[k] = (float) ((double) k + deviation * binsPerRadian);
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
            regions.push_back ({ k, 0, 0, 0, 0.0f, 0.0f, 1.0f, {}, false });
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

void PhaseVocoderEngine::computeEnvelope()
{
    LOWCHUG_PROFILE_SCOPE (formant);
    // The spectral envelope, in natural-log magnitude: the shape the body,
    // pickups and cabinet give the spectrum, as opposed to the partials
    // themselves. Drawn through the real partials only: straight lines (in
    // dB) from peak to peak, skipping sidelobe "peaks" (they report a
    // partial elsewhere, far from their own bin) and anything more than 70 dB
    // down (noise). Then smoothed over about +/-150 Hz, so single noisy
    // peaks don't make the gains jump around.
    const float loudest = *std::max_element (magnitude.begin(), magnitude.end());
    int previousPeak = -1;
    float previousLevel = 0.0f;
    for (const auto& region : regions)
    {
        const int peak = region.peak;
        if (magnitude[(size_t) peak] < loudest * 3.2e-4f || std::abs (trueBin[(size_t) peak] - (float) peak) > 1.0f)
            continue;
        const float level = std::log (magnitude[(size_t) peak] + 1.0e-9f);
        if (previousPeak < 0)
            std::fill (logEnvelope.begin(), logEnvelope.begin() + peak + 1, level);
        else
            for (int k = previousPeak; k <= peak; ++k)
                logEnvelope[(size_t) k] = previousLevel + (level - previousLevel) * float (k - previousPeak) / float (peak - previousPeak);
        previousPeak = peak;
        previousLevel = level;
    }
    if (previousPeak < 0)
    {
        std::fill (logEnvelope.begin(), logEnvelope.end(), 0.0f); // no partials: no change
        return;
    }
    std::fill (logEnvelope.begin() + previousPeak, logEnvelope.end(), previousLevel);

    // Two passes of a running-mean box (together a triangle) of +/- half
    // the smoothing width.
    const int half = std::max (1, (int) std::lround (75.0f * (float) fftSize / sampleRate));
    for (int pass = 0; pass < 2; ++pass)
    {
        float sum = 0.0f;
        int count = 0;
        for (int k = 0; k < std::min (half, numBins); ++k, ++count)
            sum += logEnvelope[(size_t) k];
        for (int k = 0; k < numBins; ++k)
        {
            if (k + half < numBins) { sum += logEnvelope[(size_t) (k + half)]; ++count; }
            envelopeScratch[(size_t) k] = sum / (float) count;
            if (k - half >= 0) { sum -= logEnvelope[(size_t) (k - half)]; --count; }
        }
        std::swap (logEnvelope, envelopeScratch);
    }
}

void PhaseVocoderEngine::computeRegionShifts (bool resetPhases, bool markOnsets)
{
    // Nothing from the last frame may survive into this one: synthesis
    // reads `modeled` and the regions, and a silent frame returns early.
    modeled.clear();

    if (regions.empty())
    {
        // Silence: nothing to carry over to the next frame.
        std::fill (previousRotation.begin(), previousRotation.end(), 0.0f);
        return;
    }

    if (preserveFormants)
        computeEnvelope();

    // Low partials: pick the ones to model, and measure their frequencies
    // again with their neighbors removed, before anything below uses them.
    selectModeledPartials();
    if (! modeled.empty())
        refinePartialFrequencies();

    const float rotationPerBin = (ratio - 1.0f) * twoPi * (float) hop / (float) fftSize;

    for (auto& region : regions)
    {
        const float peakTrueBin = trueBin[(size_t) region.peak];
        // Shift by the frequency DIFFERENCE, so ratio 1 gives exactly 0 even
        // where the frequency estimate is off by more than half a bin. The
        // shift is fractional: rounding it to whole bins would leave every
        // synthesized grain up to half a bin off-frequency, which overlap-adds
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

        region.onset = false;
        if (markOnsets)
        {
            // Masked: the regions the onset brought in (energy up >= 3 dB
            // since just before it). Notes still ringing are left alone.
            float now = 0.0f, before = 0.0f;
            for (int k = region.start; k < region.end; ++k)
            {
                now += magnitude[(size_t) k] * magnitude[(size_t) k];
                before += maskBaseMagnitude[(size_t) k] * maskBaseMagnitude[(size_t) k];
            }
            region.onset = now > 2.0f * before;
        }

        region.gain = 1.0f;
        if (preserveFormants)
        {
            // Keep the envelope where it was: a partial moved from the peak
            // to `dest` takes on the envelope's level there, so the body and
            // pickup resonances stay put while the pitch moves (up to 18 dB
            // either way, about 2 nepers).
            const float dest = std::clamp (peakTrueBin * ratio, 0.0f, (float) (numBins - 1));
            const auto lower = (size_t) dest;
            const auto upper = std::min (lower + 1, (size_t) numBins - 1);
            const float t = dest - (float) lower;
            const float atDest = logEnvelope[lower] * (1.0f - t) + logEnvelope[upper] * t;
            region.gain = std::exp (std::clamp (atDest - logEnvelope[(size_t) region.peak], -2.07f, 2.07f));
        }
    }

    for (size_t r = 0; r < regions.size(); ++r)
    {
        auto& region = regions[r];
        region.twist = std::polar (region.gain, region.rotation);
        for (int k = region.start; k < region.end; ++k)
        {
            rotation[(size_t) k] = region.rotation;
            regionOfBin[(size_t) k] = (int) r;
        }
    }

    std::swap (rotation, previousRotation);
}

void PhaseVocoderEngine::synthesizeChannel (int channelIndex)
{
    LOWCHUG_PROFILE_SCOPE (regionShift);
    auto& ch = channels[(size_t) channelIndex];
    std::fill (ch.shifted.begin(), ch.shifted.end(), Complex {});

    // While an onset passes through the window, the regions it brought in
    // are built in a spectrum of their own, so they can be masked in time
    // (see below).
    bool anyOnset = false;
    if (masking)
        for (const auto& region : regions)
            anyOnset = anyOnset || region.onset;
    if (anyOnset)
        std::fill (ch.onsetShifted.begin(), ch.onsetShifted.end(), Complex {});

    // Low partials are moved as exact lobes (see "The partial model"
    // below); whatever they don't explain (noise, attacks, model error) is
    // moved region by region as usual.
    const bool useModel = ! modeled.empty();
    if (useModel)
        subtractPartials (ch.spectrum);
    const auto& source = useModel ? residual : ch.spectrum;

    for (const auto& region : regions)
    {
        auto& target = region.onset ? ch.onsetShifted : ch.shifted;
        const auto lowerTwist = region.twist * (1.0f - region.fraction);
        const auto upperTwist = region.twist * region.fraction;
        const int first = std::max (region.start, -region.shift);
        const int last = std::min (region.end, numBins - region.shift - 1);

        // Fractional placement: each source bin is split between the two
        // destination bins either side of its exact new position (linear
        // interpolation, accurate near the window center where the weight is).
        // Regions can overlap after shifting (downward shifts squeeze them
        // together), so accumulate rather than overwrite.
        for (int k = first; k < last; ++k)
        {
            const auto value = source[(size_t) k];
            target[(size_t) (k + region.shift)] += value * lowerTwist;
            target[(size_t) (k + region.shift + 1)] += value * upperTwist;
        }
    }

    if (useModel)
        addPartials (ch.shifted, anyOnset ? &ch.onsetShifted : nullptr);

    {
        LOWCHUG_PROFILE_SCOPE (inverseFft);
        fft->fft.ifft (ch.shifted.data(), timeBuffer.data());
    }

    // Only the last synthesisSize samples of the frame are non-zero after the
    // synthesis window; overlap-add those.
    const int first = fftSize - synthesisSize;
    {
        LOWCHUG_PROFILE_SCOPE (overlapAdd);
        for (int i = first; i < fftSize; ++i)
            ch.outAccumulator[(size_t) (i - first)] += timeBuffer[(size_t) ((i - windowPeak) & (fftSize - 1))]
                                                       * synthesisWindow[(size_t) i] * outputScale;
    }

    if (anyOnset)
    {
        // The onset mask. Moving regions in frequency loses where in the
        // frame the attack was, so a frame that has only just caught the
        // attack would spread it (and the new note) over its whole
        // synthesis span, up to 42 ms before the note was played (Studio):
        // pre-echo. A note can't sound before it is played, so the onset's
        // part fades in only from 4 ms before the onset (pre-echo that close
        // is masked by the attack itself), reaching full level 1 ms before.
        {
            LOWCHUG_PROFILE_SCOPE (inverseFft);
            fft->fft.ifft (ch.onsetShifted.data(), onsetTimeBuffer.data());
        }
        LOWCHUG_PROFILE_SCOPE (overlapAdd);
        const float fadeStart = onsetTime - 0.004f * sampleRate, fadeEnd = onsetTime - 0.001f * sampleRate;
        for (int i = first; i < fftSize; ++i)
        {
            const float t = (float) (i - windowPeak);
            float gain = 1.0f;
            if (t < fadeEnd)
            {
                const float x = std::clamp ((t - fadeStart) / (fadeEnd - fadeStart), 0.0f, 1.0f);
                gain = x * x * (3.0f - 2.0f * x);
            }
            ch.outAccumulator[(size_t) (i - first)] += onsetTimeBuffer[(size_t) ((i - windowPeak) & (fftSize - 1))]
                                                       * synthesisWindow[(size_t) i] * outputScale * gain;
        }
    }
}

//==============================================================================
// The partial model
//
// Why: a real partial at bin k0 is, in the FFT, a lobe a W(k - k0) (W = the
// analysis window's spectrum) plus the mirror of its negative frequency,
// conj(a) W(k + k0). The low-delay analysis window has a wide skirt, so for
// low partials:
//   - the mirror reaches up from DC (about -50 dB at 82 Hz in Live), and
//     moving a region moves the mirror the wrong way (a negative frequency
//     should move the other way), with a phase that follows the wrong
//     frequency from frame to frame; bins shifted below DC are lost;
//   - a lobe spreads across neighboring regions (sidelobe "peaks", the
//     regions of nearby partials), and each region moves its piece with its
//     own shift and phase.
// Overlap-add turns all of that into a steady treble residue and smears the
// low end between harmonics. So the strongest low partials are measured and
// moved as exact lobes: their amplitudes are solved at their peak bins,
// their lobes subtracted from the spectrum, the rest moved region by region
// as usual, and the lobes added back at their shifted places.
//
// Only peak bins are needed to solve for the amplitudes, so the lobes'
// overlaps at those bins go in a small coupling table, and whole lobes are
// drawn just twice per channel and frame (subtracted, then added back).

void PhaseVocoderEngine::makeWindowSpectrum()
{
    // W(d) = sum_i w[i] e^(-2 pi i d (i - peak) / N): the spectrum of the
    // analysis window as the frames see it (rotated so its peak is time 0),
    // at fractional bins. Zero-padding the window to N x 32 and taking one
    // FFT gives it every 1/32 bin. Allocates: prepare() only.
    const int size = fftSize * windowSpectrumOversampling;
    std::vector<float> padded ((size_t) size, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        padded[(size_t) ((i - windowPeak + size) % size)] = analysisWindow[(size_t) i];

    std::vector<Complex> spectrum ((size_t) size / 2);
    signalsmith::linear::RealFFT<float> bigFft ((size_t) size);
    bigFft.fft (padded.data(), spectrum.data());

    windowSpectrum.assign ((size_t) (mirrorBins + 2) * windowSpectrumOversampling, Complex {});
    for (size_t i = 0; i < windowSpectrum.size(); ++i)
        windowSpectrum[i] = spectrum[i];
    windowSpectrum[0] = { spectrum[0].real(), 0.0f }; // index 0 packs the Nyquist bin in .imag

    // The skirt's envelope: the loudest |W| at each whole-bin distance or beyond.
    windowSkirt.assign ((size_t) mirrorBins + 1, 0.0f);
    float loudest = 0.0f;
    for (int i = (int) windowSpectrum.size() - 1; i >= 0; --i)
    {
        loudest = std::max (loudest, std::abs (windowSpectrum[(size_t) i]));
        if (i % windowSpectrumOversampling == 0 && i / windowSpectrumOversampling <= mirrorBins)
            windowSkirt[(size_t) (i / windowSpectrumOversampling)] = loudest;
    }
}

PhaseVocoderEngine::Complex PhaseVocoderEngine::windowSpectrumAt (float bins) const
{
    if (bins < 0.0f)
        return std::conj (windowSpectrumAt (-bins)); // the window is real
    const float position = bins * (float) windowSpectrumOversampling;
    const auto index = (size_t) position;
    if (index + 1 >= windowSpectrum.size())
        return {}; // far down the skirt: negligible
    const float t = position - (float) index;
    return windowSpectrum[index] * (1.0f - t) + windowSpectrum[index + 1] * t;
}

int PhaseVocoderEngine::lobeReach (float level) const
{
    // How far out a lobe of this level is worth drawing: until the window's
    // skirt has fallen below the floor (loud partials reach further).
    int d = 1;
    while (d < mirrorBins && level * windowSkirt[(size_t) d] > lobeFloor)
        ++d;
    return d;
}

void PhaseVocoderEngine::addLobe (std::vector<Complex>& target, Complex amplitude, float center, int reach, float sign) const
{
    // target[k] += sign * (a W(k - k0) + conj(a) W(k + k0)) for k within
    // `reach` bins: one real partial as the analysis window sees it.
    //
    // This is the model's inner loop, so it walks the table directly: from
    // one bin to the next, the table position moves by exactly 32 entries
    // and the interpolation fraction stays the same.
    // Callers only model partials well above DC; never index outside the table.
    if (! (center >= 0.0f) || reach > mirrorBins)
        return;

    constexpr int step = windowSpectrumOversampling;
    const Complex a = amplitude * sign, mirror = std::conj (amplitude) * sign;
    const auto lerp = [this] (int index, float t)
    { return windowSpectrum[(size_t) index] * (1.0f - t) + windowSpectrum[(size_t) index + 1] * t; };

    int k = std::max (0, (int) std::ceil (center - (float) reach));
    const int last = std::min (numBins - 1, (int) std::floor (center + (float) reach));

    // Left of the center, W(k - k0) = conj W(k0 - k).
    if (k <= last && (float) k <= center)
    {
        const float position = (center - (float) k) * (float) step;
        int index = (int) position;
        const float t = position - (float) index;
        for (; k <= last && (float) k <= center; ++k, index -= step)
            target[(size_t) k] += a * std::conj (lerp (index, t));
    }
    // Right of the center.
    if (k <= last)
    {
        const float position = ((float) k - center) * (float) step;
        int index = (int) position;
        const float t = position - (float) index;
        for (; k <= last; ++k, index += step)
            target[(size_t) k] += a * lerp (index, t);
    }
    // The mirror, from DC up, while it is within reach (k + k0 <= reach).
    {
        const float position = center * (float) step;
        int index = (int) position;
        const float t = position - (float) index;
        for (int j = 0; (float) j + center <= (float) reach && j < numBins; ++j, index += step)
            target[(size_t) j] += mirror * lerp (index, t);
    }
}

void PhaseVocoderEngine::selectModeledPartials()
{
    LOWCHUG_PROFILE_SCOPE (partialModel);
    // The partials worth modeling: real peaks (a sidelobe region reports a
    // partial elsewhere, far from its own peak bin), low enough that their
    // lobes and mirrors cross other regions or reach DC, not so close to DC
    // that the two lobes can't be told apart, and within 70 dB of the
    // frame's loudest bin. The 48 strongest at most, strongest first.
    modeled.clear();
    if (! config.partialModel || regions.empty())
        return;

    const float loudest = *std::max_element (magnitude.begin(), magnitude.end());
    const float quietest = loudest * 3.2e-4f; // -70 dB
    lobeFloor = loudest * 1.0e-5f;            // lobes are drawn down to -100 dB

    for (size_t r = 0; r < regions.size(); ++r)
    {
        const auto& region = regions[r];
        const float k0 = trueBin[(size_t) region.peak];
        if (magnitude[(size_t) region.peak] >= quietest && std::abs ((float) region.peak - k0) <= 1.0f
            && k0 >= 2.0f && k0 * ratio >= 2.0f && k0 < (float) mirrorBins)
            modeled.push_back ((int) r);
    }
    std::sort (modeled.begin(), modeled.end(), [this] (int x, int y)
               { return magnitude[(size_t) regions[(size_t) x].peak] > magnitude[(size_t) regions[(size_t) y].peak]; });
    if (modeled.size() > (size_t) maxModeled)
        modeled.resize ((size_t) maxModeled); // shrinking never allocates

    // How far each lobe reaches, from its peak's level (|W| at the peak is
    // about the window's sum).
    const float atPeak = std::abs (windowSpectrum[0]);
    for (size_t i = 0; i < modeled.size(); ++i)
        modelReach[i] = lobeReach (magnitude[(size_t) regions[(size_t) modeled[i]].peak] / atPeak);
}

void PhaseVocoderEngine::computeCoupling (bool withNeighbors)
{
    // couplingPositive / couplingMirror [(p * P + q) * 3 + j]: W at bin
    // (peak of p) - 1 + j, relative to partial q's lobe and its mirror. With
    // withNeighbors false only j = 1 (the peak bin itself) is filled.
    const int count = (int) modeled.size();
    for (int p = 0; p < count; ++p)
    {
        const int peak = regions[(size_t) modeled[(size_t) p]].peak;
        for (int q = 0; q < count; ++q)
        {
            const float k0 = trueBin[(size_t) regions[(size_t) modeled[(size_t) q]].peak];
            const auto at = (size_t) ((p * count + q) * 3);
            const bool inReach = std::abs ((float) peak - k0) <= (float) modelReach[(size_t) q] + 1.0f;
            for (int j = withNeighbors ? 0 : 1; j < (withNeighbors ? 3 : 2); ++j)
            {
                const float k = (float) (peak - 1 + j);
                couplingPositive[at + (size_t) j] = inReach ? windowSpectrumAt (k - k0) : Complex {};
                couplingMirror[at + (size_t) j] = k + k0 <= (float) mirrorBins ? windowSpectrumAt (k + k0) : Complex {};
            }
        }
    }
}

void PhaseVocoderEngine::solveAmplitudes (const std::vector<Complex>& spectrum, bool wantCleaned)
{
    // At each partial's peak bin: X = sum over partials q of
    // a_q W(peak - k0_q) + conj(a_q) W(peak + k0_q). Solved by Gauss-Seidel
    // (strongest first): each partial's own 2 x 2 system in a and conj(a),
    // with the others' current estimates taken out. It converges in a few
    // passes because each partial dominates its own peak bin, even for a low
    // power chord's notes 3.5 bins apart (Live).
    const int count = (int) modeled.size();
    const auto entry = [count] (int p, int q, int j) { return (size_t) ((p * count + q) * 3 + j); };
    std::fill (modelAmplitude.begin(), modelAmplitude.begin() + count, Complex {});

    for (int pass = 0; pass < 3; ++pass)
        for (int p = 0; p < count; ++p)
        {
            Complex value = spectrum[(size_t) regions[(size_t) modeled[(size_t) p]].peak];
            for (int q = 0; q < count; ++q)
                if (q != p)
                    value -= modelAmplitude[(size_t) q] * couplingPositive[entry (p, q, 1)]
                             + std::conj (modelAmplitude[(size_t) q]) * couplingMirror[entry (p, q, 1)];

            const Complex w1 = couplingPositive[entry (p, p, 1)], w2 = couplingMirror[entry (p, p, 1)];
            const float determinant = std::norm (w1) - std::norm (w2);
            modelAmplitude[(size_t) p] = determinant > 0.0f ? (std::conj (w1) * value - w2 * std::conj (value)) / determinant : Complex {};
        }

    if (! wantCleaned)
        return;

    // For refinePartialFrequencies: the bins around each peak with only that
    // partial's positive-frequency lobe left (its own mirror turns the other
    // way and would bias a frequency reading).
    for (int p = 0; p < count; ++p)
    {
        const int peak = regions[(size_t) modeled[(size_t) p]].peak;
        for (int j = 0; j < 3; ++j)
        {
            Complex value = spectrum[(size_t) std::clamp (peak - 1 + j, 0, numBins - 1)];
            for (int q = 0; q < count; ++q)
            {
                const Complex a = modelAmplitude[(size_t) q];
                if (q != p)
                    value -= a * couplingPositive[entry (p, q, j)];
                value -= std::conj (a) * couplingMirror[entry (p, q, j)];
            }
            cleaned[(size_t) (p * 3 + j)] = value;
        }
    }
}

void PhaseVocoderEngine::refinePartialFrequencies()
{
    LOWCHUG_PROFILE_SCOPE (partialModel);
    // The phase vocoder measures a partial's frequency from how its peak
    // bin's phase advances from frame to frame. Where a neighbor's lobe
    // overlaps that bin, the neighbor pulls the reading off by up to a third
    // of a bin, and a lobe drawn in the wrong place leaves an error behind.
    // So measure the advance again with every other partial subtracted.
    // The previous frame's cleaned values are kept for the bins either side
    // of each peak too, because a peak can move by a bin as partials beat.
    const float expectedAdvancePerBin = twoPi * (float) hop / (float) fftSize;
    const float binsPerRadian = (float) fftSize / (twoPi * (float) hop);
    const int count = (int) modeled.size();

    computeCoupling (true);
    solveAmplitudes (sumSpectrum, true);

    for (int p = 0; p < count; ++p)
    {
        const int peak = regions[(size_t) modeled[(size_t) p]].peak;
        const Complex previous = previousCleanedAtBin[(size_t) peak];
        if (std::norm (previous) <= 0.0f)
            continue; // not a partial here last frame: keep the plain estimate
        const float deviation = wrapPhase (std::arg (cleaned[(size_t) (p * 3 + 1)]) - std::arg (previous)
                                           - expectedAdvancePerBin * (float) peak);
        const float refined = (float) peak + deviation * binsPerRadian;
        if (std::abs (refined - (float) peak) <= 1.0f)
            trueBin[(size_t) peak] = refined;
    }

    // Keep this frame's values for the next: the bins either side of each
    // peak first, then the peaks themselves (so a peak always wins).
    std::fill (cleanedAtBin.begin(), cleanedAtBin.end(), Complex {});
    for (int pass = 0; pass < 2; ++pass)
        for (int p = 0; p < count; ++p)
            for (int j = 0; j < 3; ++j)
                if ((j == 1) == (pass == 1))
                {
                    const int k = regions[(size_t) modeled[(size_t) p]].peak - 1 + j;
                    if (k >= 0 && k < numBins)
                        cleanedAtBin[(size_t) k] = cleaned[(size_t) (p * 3 + j)];
                }
    std::swap (cleanedAtBin, previousCleanedAtBin);

    // The synthesis uses the refined frequencies.
    computeCoupling (false);
}

void PhaseVocoderEngine::subtractPartials (const std::vector<Complex>& spectrum)
{
    LOWCHUG_PROFILE_SCOPE (partialModel);
    solveAmplitudes (spectrum, false);
    std::copy (spectrum.begin(), spectrum.end(), residual.begin());
    for (size_t i = 0; i < modeled.size(); ++i)
        addLobe (residual, modelAmplitude[i], trueBin[(size_t) regions[(size_t) modeled[i]].peak], modelReach[i], -1.0f);
}

void PhaseVocoderEngine::addPartials (std::vector<Complex>& shifted, std::vector<Complex>* onsetShifted) const
{
    LOWCHUG_PROFILE_SCOPE (partialModel);
    for (size_t i = 0; i < modeled.size(); ++i)
    {
        const auto& region = regions[(size_t) modeled[i]];
        const float k0Out = trueBin[(size_t) region.peak] + (float) region.shift + region.fraction;
        auto& target = region.onset && onsetShifted != nullptr ? *onsetShifted : shifted;
        addLobe (target, modelAmplitude[i] * region.twist, k0Out, modelReach[i], 1.0f);
    }
}

} // namespace lowchug::dsp
