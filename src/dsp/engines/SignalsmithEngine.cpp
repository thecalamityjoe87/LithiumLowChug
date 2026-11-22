#include "SignalsmithEngine.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <cmath>
#include <cstdio>

namespace lowchug::dsp
{

struct SignalsmithEngine::Impl
{
    // A fixed seed makes output reproducible run-to-run (Stretch randomises
    // phases in some situations), which keeps tests and A/B renders stable.
    signalsmith::stretch::SignalsmithStretch<float> stretch { 0x10c4 };
};

SignalsmithEngine::SignalsmithEngine (Config c)
    : impl (std::make_unique<Impl>()), config (c)
{
    char buffer[96];
    std::snprintf (buffer, sizeof (buffer), "Signalsmith %.0f/%.0fms%s%s",
                   (double) config.blockMs, (double) config.intervalMs,
                   config.splitComputation ? " split" : "",
                   config.tonalityLimitHz > 0 ? " tonal" : "");
    name = buffer;
}

SignalsmithEngine::~SignalsmithEngine() = default;

void SignalsmithEngine::prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels)
{
    sampleRate = newSampleRate;

    const auto blockSamples = (int) std::lround (sampleRate * config.blockMs / 1000.0);
    const auto intervalSamples = (int) std::lround (sampleRate * config.intervalMs / 1000.0);

    // configure() allocates; this is why prepare() must not run on the audio thread.
    impl->stretch.configure (numChannels, blockSamples, intervalSamples, config.splitComputation);
    latencySamples = impl->stretch.inputLatency() + impl->stretch.outputLatency();

    applyTransposeSettings();
}

void SignalsmithEngine::reset()
{
    impl->stretch.reset();
}

void SignalsmithEngine::setPitchRatio (float newRatio)
{
    if (newRatio != ratio)
    {
        ratio = newRatio;
        applyTransposeSettings();
    }
}

void SignalsmithEngine::setFormantPreservation (bool shouldPreserve)
{
    if (shouldPreserve != preserveFormants)
    {
        preserveFormants = shouldPreserve;
        applyTransposeSettings();
    }
}

void SignalsmithEngine::applyTransposeSettings()
{
    const auto tonalityLimit = config.tonalityLimitHz > 0.0f
                                   ? float (config.tonalityLimitHz / sampleRate)
                                   : 0.0f;
    impl->stretch.setTransposeFactor (ratio, tonalityLimit);

    // compensatePitch=true moves the spectral envelope back by the inverse of
    // the pitch ratio, i.e. formants stay where they were.
    impl->stretch.setFormantFactor (1.0f, preserveFormants);
}

void SignalsmithEngine::process (const float* const* input, float* const* output,
                                 int /*numChannels*/, int numSamples)
{
    // Equal input and output lengths = pure pitch shift, no time stretch.
    impl->stretch.process (input, numSamples, output, numSamples);
}

} // namespace lowchug::dsp
