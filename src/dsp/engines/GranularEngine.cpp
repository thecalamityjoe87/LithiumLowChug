#include "GranularEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace lowchug::dsp
{

GranularEngine::GranularEngine (Config c) : config (c)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "Granular %.0fms", (double) config.grainMs);
    name = buffer;
}

void GranularEngine::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    grainSamples = float (sampleRate * config.grainMs / 1000.0);
    lineSize = 1;
    while (lineSize < (int) (grainSamples + minDelay) + 8)
        lineSize *= 2;

    delayLines.assign ((size_t) numChannels, std::vector<float> ((size_t) lineSize, 0.0f));
    latencySamples = (int) std::lround (minDelay + grainSamples * 0.5f); // average read delay
    reset();
}

void GranularEngine::reset()
{
    for (auto& line : delayLines)
        std::fill (line.begin(), line.end(), 0.0f);
    writeIndex = 0;
    phase = 0.0f;
}

float GranularEngine::readInterpolated (const std::vector<float>& line, float delay) const
{
    const float readPos = (float) writeIndex - delay;
    const float floorPos = std::floor (readPos);
    const float frac = readPos - floorPos;
    const int mask = lineSize - 1;
    const int i1 = (int) floorPos & mask;
    const int i0 = (i1 - 1) & mask, i2 = (i1 + 1) & mask, i3 = (i1 + 2) & mask;

    // 4-point cubic (Catmull-Rom) interpolation.
    const float y0 = line[(size_t) i0], y1 = line[(size_t) i1], y2 = line[(size_t) i2], y3 = line[(size_t) i3];
    const float a = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c = 0.5f * (y2 - y0);
    return ((a * frac + b) * frac + c) * frac + y1;
}

void GranularEngine::process (const float* const* input, float* const* output,
                              int numChannels, int numSamples)
{
    // Each head's delay sweeps across one grain; the read speed relative to
    // the write head is `ratio`, so the delay changes by (1 - ratio) per sample.
    const float phaseStep = std::abs (1.0f - ratio) / grainSamples;
    const bool delayGrows = ratio < 1.0f;
    const int channels = std::min (numChannels, (int) delayLines.size());

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < channels; ++c)
            delayLines[(size_t) c][(size_t) writeIndex] = input[c][i];

        const float phaseB = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
        const float delayA = minDelay + grainSamples * (delayGrows ? phase : 1.0f - phase);
        const float delayB = minDelay + grainSamples * (delayGrows ? phaseB : 1.0f - phaseB);

        // sin^2 + cos^2 = 1: the two heads crossfade to constant gain, and each
        // head is silent at the moment it jumps.
        const float gainA = std::sin (std::numbers::pi_v<float> * phase);
        const float weightA = gainA * gainA;

        for (int c = 0; c < channels; ++c)
        {
            const auto& line = delayLines[(size_t) c];
            output[c][i] = weightA * readInterpolated (line, delayA)
                           + (1.0f - weightA) * readInterpolated (line, delayB);
        }

        phase += phaseStep;
        if (phase >= 1.0f)
            phase -= 1.0f;
        writeIndex = (writeIndex + 1) & (lineSize - 1);
    }
}

} // namespace lowchug::dsp
