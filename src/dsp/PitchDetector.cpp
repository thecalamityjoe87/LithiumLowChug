#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace lowchug::dsp
{

namespace
{
// A delay counts as "the period" once its normalized difference drops below
// this (YIN's paper uses 0.1 to 0.15; guitar strings are clean enough).
constexpr float threshold = 0.12f;
} // namespace

void PitchDetector::prepare (double newSampleRate, float minFrequency, float maxFrequency)
{
    sampleRate = newSampleRate;
    minLag = std::max (2, (int) std::floor (sampleRate / maxFrequency));
    maxLag = (int) std::ceil (sampleRate / minFrequency);

    // Compare over a stretch at least as long as the longest period: a
    // 60 Hz low string at 48 kHz needs 800 samples each way.
    integration = maxLag;
    windowSize = integration + maxLag + 2;
    difference.assign ((size_t) maxLag + 2, 0.0f);
}

PitchDetector::Result PitchDetector::detect (const float* x)
{
    if (windowSize == 0)
        return {};

    // Quiet input (string not played, or decayed): no pitch.
    double energy = 0.0;
    for (int i = 0; i < windowSize; ++i)
        energy += (double) x[i] * x[i];
    if (energy / windowSize < 1.0e-6) // about -60 dBFS RMS
        return {};

    // d(tau): how different the signal is from itself `tau` samples later.
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double sum = 0.0;
        for (int j = 0; j < integration; ++j)
        {
            const double delta = (double) x[j] - x[j + tau];
            sum += delta * delta;
        }
        difference[(size_t) tau] = (float) sum;
    }

    // Normalize by the running mean, so small delays (which always match
    // well) don't win: d'(tau) = d(tau) * tau / sum(d(1..tau)).
    difference[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        running += difference[(size_t) tau];
        difference[(size_t) tau] = running > 0.0 ? (float) (difference[(size_t) tau] * tau / running) : 1.0f;
    }

    // The first dip below the threshold, followed down to its lowest point.
    // (The FIRST, not the deepest: a dip at twice the period would be an
    // octave too low.) Without one, fall back to the deepest dip overall.
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
        if (difference[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxLag && difference[(size_t) tau + 1] < difference[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }

    if (best < 0)
        best = (int) std::distance (difference.begin(),
                                    std::min_element (difference.begin() + minLag, difference.begin() + maxLag + 1));

    const auto clarity = std::clamp (1.0f - difference[(size_t) best], 0.0f, 1.0f);
    if (clarity < 0.5f)
        return { 0.0f, clarity };

    // A parabola through the dip and its neighbors: the period to a
    // fraction of a sample.
    const auto a = difference[(size_t) best - 1], b = difference[(size_t) best], c = difference[(size_t) best + 1];
    const auto curvature = a - 2.0f * b + c;
    const auto offset = curvature > 0.0f ? std::clamp (0.5f * (a - c) / curvature, -0.5f, 0.5f) : 0.0f;

    return { (float) (sampleRate / ((double) best + offset)), clarity };
}

} // namespace lowchug::dsp
