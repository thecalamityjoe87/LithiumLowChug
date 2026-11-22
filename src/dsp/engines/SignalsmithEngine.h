#pragma once

#include "../PitchShiftEngine.h"

#include <memory>
#include <string>

namespace lowchug::dsp
{

/**
    PitchShiftEngine backed by Signalsmith Stretch (MIT licensed).

    Stretch is a phase vocoder with multi-channel phase locking and a
    peak-based frequency map, which keeps chords coherent. Its latency is
    roughly one analysis block, so the block length is the main
    quality-vs-latency control.
*/
class SignalsmithEngine final : public PitchShiftEngine
{
public:
    struct Config
    {
        float blockMs = 120.0f;   // analysis window length
        float intervalMs = 30.0f; // hop between analysis frames
        // Spreads each frame's FFT work over the following hop so CPU load per
        // audio block is even (good for small host buffers), at the cost of one
        // extra hop of latency.
        bool splitComputation = false;
        // Above this frequency partials are moved by a constant offset rather
        // than a ratio (0 = shift everything by the ratio).
        float tonalityLimitHz = 0.0f;
    };

    explicit SignalsmithEngine (Config config);
    ~SignalsmithEngine() override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setPitchRatio (float ratio) override;
    void setFormantPreservation (bool shouldPreserve) override;
    bool supportsFormantPreservation() const override { return true; }
    int getLatencySamples() const override { return latencySamples; }
    void process (const float* const* input, float* const* output,
                  int numChannels, int numSamples) override;
    std::string_view getName() const override { return name; }

private:
    void applyTransposeSettings();

    // Pimpl: keeps the (large, template-heavy) Stretch header out of every
    // file that includes this one.
    struct Impl;
    std::unique_ptr<Impl> impl;

    Config config;
    std::string name;
    double sampleRate = 48000.0;
    float ratio = 1.0f;
    bool preserveFormants = false;
    int latencySamples = 0;
};

} // namespace lowchug::dsp
