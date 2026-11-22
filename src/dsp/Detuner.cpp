#include "Detuner.h"

#include "EngineConfigs.h"

#include <algorithm>
#include <cmath>

namespace lowchug::dsp
{

namespace
{
// The pitch ratio is updated every this many samples. The engine only picks
// up a new ratio once per analysis hop anyway (256 samples in Live), so
// finer updates would buy nothing.
constexpr int controlInterval = 32;

constexpr double glideSeconds = 0.040; // spec: shift changes glide over 30-50 ms
constexpr double fadeSeconds = 0.020;  // mix, gain, bypass and unity crossfades
} // namespace

Detuner::Detuner (std::unique_ptr<PitchShiftEngine> e)
    : engine (e != nullptr ? std::move (e) : std::make_unique<PhaseVocoderEngine> (configs::live()))
{
    mix.snapTo (1.0f);
    outputGain.snapTo (1.0f);
}

void Detuner::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    preparedChannels = std::max (1, numChannels);
    maxChunk = std::max (1, maxBlockSize);

    engine->prepare (sampleRate, maxChunk, preparedChannels);
    dryDelay.prepare (preparedChannels, engine->getLatencySamples());

    dryBuffers.assign ((size_t) preparedChannels, std::vector<float> ((size_t) maxChunk, 0.0f));
    wetBuffers.assign ((size_t) preparedChannels, std::vector<float> ((size_t) maxChunk, 0.0f));
    dryPointers.resize ((size_t) preparedChannels);
    wetPointers.resize ((size_t) preparedChannels);
    wetSlicePointers.resize ((size_t) preparedChannels);
    offsetPointers.resize ((size_t) preparedChannels);
    inputPointers.resize ((size_t) preparedChannels);
    for (int c = 0; c < preparedChannels; ++c)
    {
        dryPointers[(size_t) c] = dryBuffers[(size_t) c].data();
        wetPointers[(size_t) c] = wetBuffers[(size_t) c].data();
    }

    glide.setRampLength ((int) std::lround (glideSeconds * sampleRate));
    for (auto* s : { &unity, &mix, &outputGain, &bypass })
        s->setRampLength ((int) std::lround (fadeSeconds * sampleRate));

    reset();
}

void Detuner::reset()
{
    engine->reset();
    dryDelay.reset();

    // After a reset, start at the target values: no ramps from stale state.
    glide.snapTo (glide.getTarget());
    unity.snapTo (glide.getTarget() == 0.0f ? 1.0f : 0.0f);
    for (auto* s : { &mix, &outputGain, &bypass })
        s->snapTo (s->getTarget());

    linked = false;
    identicalRun = 0;
}

void Detuner::setShiftSemitones (float semitones) { glide.setTarget (semitones); }
void Detuner::setMix (float wetAmount) { mix.setTarget (std::clamp (wetAmount, 0.0f, 1.0f)); }
void Detuner::setOutputGain (float linearGain) { outputGain.setTarget (std::max (0.0f, linearGain)); }
void Detuner::setBypassed (bool shouldBypass) { bypass.setTarget (shouldBypass ? 1.0f : 0.0f); }

void Detuner::process (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::min (numChannels, preparedChannels);

    // Hosts may send more than they promised in prepare(); work in chunks
    // that fit the preallocated buffers.
    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = std::min (maxChunk, numSamples - start);
        for (int c = 0; c < numChannels; ++c)
            offsetPointers[(size_t) c] = channels[c] + start;
        processChunk (offsetPointers.data(), numChannels, n);
    }
}

void Detuner::updateChannelLinking (const float* const* channels, int numChannels, int numSamples)
{
    const int memory = engine->getMemorySamples();
    if (! linkingEnabled || numChannels < 2 || memory <= 0)
    {
        if (linked)
        {
            for (int c = 1; c < numChannels; ++c)
                engine->copyChannelState (0, c);
            linked = false;
        }
        identicalRun = 0;
        return;
    }

    bool identical = true;
    for (int c = 1; c < numChannels && identical; ++c)
        identical = std::equal (channels[0], channels[0] + numSamples, channels[c]);

    identicalRun = identical ? std::min (identicalRun + numSamples, (long) memory * 4) : 0;

    if (linked && ! identical)
    {
        // Until now the other channels were identical, so their engine
        // state is channel 0's: hand it over and carry on in stereo.
        for (int c = 1; c < numChannels; ++c)
            engine->copyChannelState (0, c);
        linked = false;
    }
    else if (! linked && identicalRun >= memory)
    {
        // Identical for longer than the engine remembers, so every channel's
        // output is now the same: shift channel 0 only and copy it.
        linked = true;
    }
}

void Detuner::processChunk (float* const* channels, int numChannels, int n)
{
    updateChannelLinking (channels, numChannels, n);
    const int engineChannels = linked ? 1 : numChannels;

    dryDelay.process (channels, dryPointers.data(), numChannels, n);

    // Pitch shift in control-rate slices, stepping the gliding ratio.
    for (int offset = 0; offset < n; offset += controlInterval)
    {
        const int length = std::min (controlInterval, n - offset);
        const float semitones = glide.skip (length);
        engine->setPitchRatio (std::exp2 (semitones / 12.0f));

        for (int c = 0; c < engineChannels; ++c)
        {
            inputPointers[(size_t) c] = channels[c] + offset;
            wetSlicePointers[(size_t) c] = wetPointers[(size_t) c] + offset;
        }
        engine->process (inputPointers.data(), wetSlicePointers.data(), engineChannels, length);
    }

    if (linked)
        for (int c = 1; c < numChannels; ++c)
            std::copy (wetBuffers[0].begin(), wetBuffers[0].begin() + n, wetBuffers[(size_t) c].begin());

    // At exactly 0 st (and once any glide has finished), fade to the delayed
    // dry signal so "no shift" is truly transparent.
    unity.setTarget (glide.isSettled() && glide.getTarget() == 0.0f ? 1.0f : 0.0f);

    for (int i = 0; i < n; ++i)
    {
        // The unity crossfade uses a smoothstep curve: a linear ramp's sharp
        // corners would turn the (phase-drifted) difference between the
        // shifted and dry signals into a faint tick. Still exactly 0 and 1 at the ends.
        const float uLinear = unity.next();
        const float u = uLinear * uLinear * (3.0f - 2.0f * uLinear);
        const float m = mix.next(), g = outputGain.next(), b = bypass.next();

        for (int c = 0; c < numChannels; ++c)
        {
            const float dry = dryBuffers[(size_t) c][(size_t) i];
            const float wet = wetBuffers[(size_t) c][(size_t) i];

            // Each stage takes an exact shortcut at 0 and 1, so the fully-dry
            // states (unity, mix 0, bypass) reproduce the delayed input bit
            // for bit instead of picking up rounding from a * (1 - x) + b * x.
            float y = u >= 1.0f ? dry : (u <= 0.0f ? wet : wet + (dry - wet) * u);
            if (m < 1.0f)
                y = m <= 0.0f ? dry : dry + (y - dry) * m;
            if (g != 1.0f)
                y *= g;
            if (b > 0.0f)
                y = b >= 1.0f ? dry : y + (dry - y) * b;

            channels[c][i] = y;
        }
    }
}

} // namespace lowchug::dsp
