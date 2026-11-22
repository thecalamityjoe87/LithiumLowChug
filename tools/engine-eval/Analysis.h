#pragma once

// Objective measurements for comparing pitch-shift engines against an ideal
// reference rendering. All signals passed in here are already aligned
// (engine latency removed).

#include "TestSignals.h"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <vector>

namespace lowchug::eval
{

using Signal = std::vector<float>;

inline double hannWindow (size_t i, size_t n)
{
    return 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n);
}

/** Power spectrum of signal[start, start+length) with a Hann window,
    zero-padded to fftSize (a power of two). */
inline std::vector<double> powerSpectrum (const Signal& signal, size_t start, size_t length, size_t fftSize)
{
    signalsmith::linear::RealFFT<float> fft (fftSize);
    std::vector<float> time (fftSize, 0.0f);
    std::vector<std::complex<float>> freq (fftSize / 2);
    for (size_t i = 0; i < length && start + i < signal.size(); ++i)
        time[i] = float (signal[start + i] * hannWindow (i, length));
    fft.fft (time.data(), freq.data());
    freq[0] = { freq[0].real(), 0.0f };

    std::vector<double> power (fftSize / 2);
    for (size_t k = 0; k < power.size(); ++k)
        power[k] = std::norm (freq[k]);
    return power;
}

/** Frequency of the strongest peak, refined by parabolic interpolation on
    the log magnitude (accurate to a small fraction of a bin). */
inline double peakFrequency (const Signal& signal, size_t start, size_t length, double sampleRate)
{
    size_t fftSize = 1;
    while (fftSize < length * 2)
        fftSize *= 2;
    const auto power = powerSpectrum (signal, start, length, fftSize);
    const size_t peak = (size_t) std::distance (power.begin(), std::max_element (power.begin() + 2, power.end() - 2));
    const double a = std::log (power[peak - 1] + 1e-30), b = std::log (power[peak] + 1e-30), c = std::log (power[peak + 1] + 1e-30);
    const double offset = 0.5 * (a - c) / (a - 2.0 * b + c);
    return ((double) peak + offset) * sampleRate / (double) fftSize;
}

/** Peak-to-trough level change (dB) of the 10 ms RMS envelope: a steady sine
    should give ~0 dB; warble and flutter push it up. */
inline double envelopeRippleDb (const Signal& signal, size_t start, size_t length, double sampleRate)
{
    const auto block = (size_t) (0.010 * sampleRate);
    double lo = 1e30, hi = 0;
    for (size_t b = start; b + block <= start + length && b + block <= signal.size(); b += block)
    {
        double sum = 0;
        for (size_t i = 0; i < block; ++i)
            sum += (double) signal[b + i] * signal[b + i];
        const double rms = std::sqrt (sum / (double) block);
        lo = std::min (lo, rms);
        hi = std::max (hi, rms);
    }
    return 20.0 * std::log10 ((hi + 1e-12) / (lo + 1e-12));
}

/** Mean log-spectral distance (dB) between output and reference over
    [start, end) seconds, 40 Hz - 8 kHz. Frames where the reference is
    silent are skipped. Lower is better; it rises with smearing, phasiness,
    warble and pitch error alike. */
inline double logSpectralDistance (const Signal& output, const Signal& reference, double startSeconds, double endSeconds,
                                   double sampleRate)
{
    const size_t fftSize = 2048, hop = 512;
    const auto first = (size_t) (startSeconds * sampleRate), last = (size_t) (endSeconds * sampleRate);
    const auto lowBin = (size_t) (40.0 * fftSize / sampleRate), highBin = (size_t) (8000.0 * fftSize / sampleRate);

    double globalMax = 0;
    std::vector<std::vector<double>> refFrames, outFrames;
    for (size_t pos = first; pos + fftSize <= last && pos + fftSize <= reference.size(); pos += hop)
    {
        refFrames.push_back (powerSpectrum (reference, pos, fftSize, fftSize));
        outFrames.push_back (powerSpectrum (output, pos, fftSize, fftSize));
        globalMax = std::max (globalMax, *std::max_element (refFrames.back().begin(), refFrames.back().end()));
    }

    const double floor = globalMax * 1e-6; // -60 dB
    double total = 0;
    int counted = 0;
    for (size_t f = 0; f < refFrames.size(); ++f)
    {
        const double frameEnergy = std::accumulate (refFrames[f].begin(), refFrames[f].end(), 0.0);
        if (frameEnergy < globalMax * 1e-3)
            continue;
        double sum = 0;
        for (size_t k = lowBin; k < highBin; ++k)
        {
            const double d = 10.0 * std::log10 ((outFrames[f][k] + floor) / (refFrames[f][k] + floor));
            sum += d * d;
        }
        total += std::sqrt (sum / double (highBin - lowBin));
        ++counted;
    }
    return counted > 0 ? total / counted : 0.0;
}

struct HarmonicStats
{
    double hnrDb = 0;            // harmonic energy vs everything else below 1 kHz
    double fundamentalDb = 0;    // fundamental level (relative units, compare to reference)
};

/** Harmonic-to-noise ratio of a sustained note below 1 kHz. Energy between
    the expected partials is the "mud" / "underwater" smear that bad shifters
    leave at the low end. */
inline HarmonicStats harmonicStats (const Signal& signal, double startSeconds, double lengthSeconds, double f0,
                                    bool wound, double sampleRate)
{
    const auto start = (size_t) (startSeconds * sampleRate), length = (size_t) (lengthSeconds * sampleRate);
    size_t fftSize = 1;
    while (fftSize < length * 2)
        fftSize *= 2;
    const auto power = powerSpectrum (signal, start, length, fftSize);
    const double binHz = sampleRate / (double) fftSize;
    const double inharmonicity = wound ? 1.2e-4 : 4.0e-5;

    std::vector<bool> isHarmonic (power.size(), false);
    for (int n = 1;; ++n)
    {
        const double fn = n * f0 * std::sqrt (1.0 + inharmonicity * n * n);
        if (fn > 1000.0)
            break;
        const auto lo = (size_t) std::max (0.0, (fn - 4.0) / binHz), hi = (size_t) ((fn + 4.0) / binHz);
        for (size_t k = lo; k <= hi && k < power.size(); ++k)
            isHarmonic[k] = true;
    }

    double harmonic = 0, total = 0, fundamental = 0;
    const auto lowBin = (size_t) (20.0 / binHz), highBin = (size_t) (1000.0 / binHz);
    for (size_t k = lowBin; k < highBin; ++k)
    {
        total += power[k];
        if (isHarmonic[k])
            harmonic += power[k];
        if (std::abs ((double) k * binHz - f0) < 4.0)
            fundamental += power[k];
    }
    return { 10.0 * std::log10 (harmonic / std::max (total - harmonic, 1e-30)), 10.0 * std::log10 (fundamental + 1e-30) };
}

struct TransientStats
{
    double spreadMs = 0;       // time spread of the onset's energy rise (smaller = tighter attack)
    double preEchoDb = 0;      // energy in the 18 ms before the onset relative to the 18 ms after it
    double attackLossDb = 0;   // attack peak level, output minus reference (negative = softened)
};

/** Onset tightness, averaged over the segment's onsets.

    Spread: take the 1 ms RMS envelope around the onset, keep only its rises,
    and measure their energy-weighted standard deviation in time. A crisp
    pick attack concentrates the rise in a millisecond or two; smearing
    spreads it over the analysis window. Unlike a 10-90% rise time this
    isn't fooled by the previous note still ringing. */
/** 8th-order high-pass (four cascaded RBJ biquads, 48 dB/octave): steep
    enough that strong low partials don't leak into high-band measurements. */
inline Signal highPass (const Signal& in, double cutoff, double sampleRate)
{
    const double w = 2.0 * std::numbers::pi * cutoff / sampleRate, q = 0.7071;
    const double alpha = std::sin (w) / (2.0 * q), cosw = std::cos (w), a0 = 1.0 + alpha;
    const double b0 = (1.0 + cosw) / 2.0 / a0, b1 = -(1.0 + cosw) / a0, b2 = b0;
    const double a1 = -2.0 * cosw / a0, a2 = (1.0 - alpha) / a0;
    Signal out = in;
    for (int pass = 0; pass < 4; ++pass)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : out)
        {
            const double x = v, y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            v = (float) y;
        }
    }
    return out;
}

inline TransientStats transientStats (const Signal& fullOutput, const Signal& fullReference, const Segment& segment, double sampleRate)
{
    // The pick attack lives in the highs, and a 1 ms envelope of a 55 Hz note
    // is mostly waveform ripple, so measure above 2 kHz.
    const auto output = highPass (fullOutput, 2000.0, sampleRate);
    const auto reference = highPass (fullReference, 2000.0, sampleRate);

    const auto window = (size_t) (0.001 * sampleRate), hop = std::max<size_t> (1, window / 4);
    auto envelopeAt = [&] (const Signal& s, size_t pos) {
        double sum = 0;
        for (size_t i = 0; i < window && pos + i < s.size(); ++i)
            sum += (double) s[pos + i] * s[pos + i];
        return std::sqrt (sum / (double) window);
    };
    auto energy = [&] (const Signal& s, size_t a, size_t b) {
        double sum = 0;
        for (size_t i = a; i < b && i < s.size(); ++i)
            sum += (double) s[i] * s[i];
        return sum + 1e-20;
    };
    const auto ms = [&] (double m) { return (size_t) (m * 0.001 * sampleRate); };

    TransientStats stats;
    int count = 0;
    for (double onset : segment.onsets)
    {
        const auto t0 = (size_t) (onset * sampleRate);
        if (t0 < ms (40))
            continue;

        double weightSum = 0, timeSum = 0, timeSqSum = 0;
        double previous = envelopeAt (output, t0 - ms (30));
        for (size_t p = t0 - ms (30) + hop; p < t0 + ms (30); p += hop)
        {
            const double e = envelopeAt (output, p);
            const double rise = std::max (0.0, e - previous);
            previous = e;
            const double w = rise * rise;
            const double t = ((double) p - (double) t0) / sampleRate * 1000.0;
            weightSum += w;
            timeSum += w * t;
            timeSqSum += w * t * t;
        }
        if (weightSum > 0)
        {
            const double mean = timeSum / weightSum;
            stats.spreadMs += std::sqrt (std::max (0.0, timeSqSum / weightSum - mean * mean));
        }

        auto peakIn = [&] (const Signal& s) {
            double peak = 0;
            for (size_t p = t0; p < t0 + ms (15); p += hop)
                peak = std::max (peak, envelopeAt (s, p));
            return peak;
        };
        // Relative to the attack itself (not to the ideal's near-silence,
        // which would turn inaudible -100 dB residue into huge numbers).
        stats.preEchoDb += 10.0 * std::log10 (energy (output, t0 - ms (20), t0 - ms (2)) / energy (output, t0, t0 + ms (18)));
        stats.attackLossDb += 20.0 * std::log10 ((peakIn (output) + 1e-12) / (peakIn (reference) + 1e-12));
        ++count;
    }
    if (count > 0)
    {
        stats.spreadMs /= count;
        stats.preEchoDb /= count;
        stats.attackLossDb /= count;
    }
    return stats;
}

} // namespace lowchug::eval
