#pragma once

#include "../PitchShiftEngine.h"

#include <string>
#include <vector>

namespace lowchug::dsp
{

/**
    The classic time-domain "two read heads on a delay line" shifter that
    cheap pitch pedals use. Very low latency and CPU, but chords warble and
    low notes flutter, because the crossfades ignore the signal's period.

    It is in the evaluation as a REFERENCE (the latency/quality floor), not as
    a candidate for the main path.
*/
class GranularEngine final : public PitchShiftEngine
{
public:
    struct Config
    {
        float grainMs = 30.0f;
    };

    explicit GranularEngine (Config config);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setPitchRatio (float newRatio) override { ratio = newRatio; }
    int getLatencySamples() const override { return latencySamples; }
    void process (const float* const* input, float* const* output,
                  int numChannels, int numSamples) override;
    std::string_view getName() const override { return name; }

private:
    float readInterpolated (const std::vector<float>& line, float delay) const;

    Config config;
    std::string name;
    std::vector<std::vector<float>> delayLines;
    int lineSize = 0, writeIndex = 0;
    float grainSamples = 0.0f, minDelay = 4.0f;
    float phase = 0.0f, ratio = 1.0f;
    int latencySamples = 0;
};

} // namespace lowchug::dsp
