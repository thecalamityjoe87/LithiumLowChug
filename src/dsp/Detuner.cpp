#include "Detuner.h"

#include "EngineConfigs.h"

#include <algorithm>
#include <cmath>

namespace lowchug::dsp
{

namespace
{
// The pitch ratio is updated every this many samples. The engine only picks
// up a new ratio once per analysis hop anyway (256 samples), so finer
// updates would buy nothing.
constexpr int controlInterval = 32;

constexpr double glideSeconds = 0.040;    // spec: shift changes glide over 30-50 ms
constexpr double fadeSeconds = 0.020;     // mix, gain, bypass and unity crossfades
constexpr double modeFadeSeconds = 0.030; // Live <-> Studio crossfade, once the new engine is warm
constexpr double tightSeconds = 0.150;    // Tight changes

// Tight's filters follow where the low E string (82.4 Hz) lands.
constexpr double lowE = 82.41;

/** A smoothstep curve: a linear ramp's sharp corners would turn the
    difference between the two signals into a faint tick. Exactly 0 and 1
    at the ends. */
float smoothstep (float x) { return x * x * (3.0f - 2.0f * x); }
} // namespace

Detuner::TightFilter::Coefficients Detuner::lerp (const TightFilter::Coefficients& a, const TightFilter::Coefficients& b, double t)
{
    return { a.a1 + (b.a1 - a.a1) * t, a.a2 + (b.a2 - a.a2) * t, a.a3 + (b.a3 - a.a3) * t,
             a.m0 + (b.m0 - a.m0) * t, a.m1 + (b.m1 - a.m1) * t, a.m2 + (b.m2 - a.m2) * t };
}

namespace
{
} // namespace

Detuner::Detuner (std::unique_ptr<PitchShiftEngine> liveEngine, std::unique_ptr<PitchShiftEngine> studioEngine)
{
    paths[0].engine = liveEngine != nullptr ? std::move (liveEngine) : std::make_unique<PhaseVocoderEngine> (configs::live());
    paths[1].engine = studioEngine != nullptr ? std::move (studioEngine) : std::make_unique<PhaseVocoderEngine> (configs::studio());
    mix.snapTo (1.0f);
    outputGain.snapTo (1.0f);
}

void Detuner::TightFilter::update (double rate, float semitones, float amount)
{
    // Simper's SVF: g = tan(pi f / fs), k = 1 / Q; the output mixes the
    // input (m0), band-pass (m1) and low-pass (m2) states.
    const double note = lowE * std::exp2 ((double) semitones / 12.0);
    const double pi = 3.14159265358979323846;
    auto make = [] (double g, double k, double m0, double m1, double m2)
    {
        const double a1 = 1.0 / (1.0 + g * (g + k));
        return Coefficients { a1, g * a1, g * g * a1, m0, m1, m2 };
    };
    {
        // High-pass, Q 0.6 (a touch gentler than Butterworth): x - k bp - lp.
        // As the amount goes to 0 the cutoff falls toward 0 Hz.
        const double cutoff = std::max (1.0, 0.9 * note * (double) amount);
        const double k = 1.0 / 0.6;
        highPass = make (std::tan (pi * cutoff / rate), k, 1.0, -k, -1.0);
    }
    {
        // Low shelf, Q 0.707, down by 6 dB x amount: 0 dB is exactly the input.
        const double a = std::pow (10.0, -6.0 * (double) amount / 40.0), k = 1.0 / 0.7071;
        const double g = std::tan (pi * std::min (2.5 * note, 0.45 * rate) / rate) / std::sqrt (a);
        shelf = make (g, k, 1.0, k * (a - 1.0), a * a - 1.0);
    }
}

float Detuner::TightFilter::run (const Coefficients& c, State& s, float input)
{
    const double x = input;
    const double v3 = x - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3; // band-pass
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3; // low-pass
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return (float) (c.m0 * x + c.m1 * v1 + c.m2 * v2);
}

void Detuner::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    preparedChannels = std::max (1, numChannels);
    maxChunk = std::max (1, maxBlockSize);

    // Both modes are prepared now, so a later switch only flips between
    // ready-made paths on the audio thread.
    for (auto& p : paths)
    {
        p.engine->prepare (sampleRate, maxChunk, preparedChannels);
        p.dryDelay.prepare (preparedChannels, p.engine->getLatencySamples());

        p.tightState.assign ((size_t) preparedChannels, {});
        p.dryBuffers.assign ((size_t) preparedChannels, std::vector<float> ((size_t) maxChunk, 0.0f));
        p.wetBuffers.assign ((size_t) preparedChannels, std::vector<float> ((size_t) maxChunk, 0.0f));
        p.dryPointers.resize ((size_t) preparedChannels);
        p.wetPointers.resize ((size_t) preparedChannels);
        p.wetSlicePointers.resize ((size_t) preparedChannels);
        for (int c = 0; c < preparedChannels; ++c)
        {
            p.dryPointers[(size_t) c] = p.dryBuffers[(size_t) c].data();
            p.wetPointers[(size_t) c] = p.wetBuffers[(size_t) c].data();
        }
    }
    offsetPointers.resize ((size_t) preparedChannels);
    inputPointers.resize ((size_t) preparedChannels);

    glide.setRampLength ((int) std::lround (glideSeconds * sampleRate));
    for (auto* s : { &unity, &mix, &outputGain, &bypass })
        s->setRampLength ((int) std::lround (fadeSeconds * sampleRate));
    modeFade.setRampLength ((int) std::lround (modeFadeSeconds * sampleRate));
    tight.setRampLength ((int) std::lround (tightSeconds * sampleRate));

    reset();
}

void Detuner::reset()
{
    for (auto& p : paths)
    {
        p.engine->reset();
        p.dryDelay.reset();
        std::fill (p.tightState.begin(), p.tightState.end(), std::array<TightFilter::State, 2> {});
    }

    // After a reset, start at the target values: no ramps from stale state.
    glide.snapTo (glide.getTarget());
    unity.snapTo (glide.getTarget() == 0.0f ? 1.0f : 0.0f);
    for (auto* s : { &mix, &outputGain, &bypass, &tight })
        s->snapTo (s->getTarget());

    previousTightBlend = smoothstep (std::min (1.0f, tight.getTarget() * 4.0f));
    tightFilter.update (sampleRate, glide.getTarget(), tight.getTarget());
    previousTight = tightFilter;
    modeFade.snapTo (targetMode == Mode::studio ? 1.0f : 0.0f);
    warmupLeft = -1;

    linked = false;
    identicalRun = 0;
}

void Detuner::setShiftSemitones (float semitones) { glide.setTarget (semitones); }
void Detuner::setMix (float wetAmount) { mix.setTarget (std::clamp (wetAmount, 0.0f, 1.0f)); }
void Detuner::setOutputGain (float linearGain) { outputGain.setTarget (std::max (0.0f, linearGain)); }
void Detuner::setBypassed (bool shouldBypass) { bypass.setTarget (shouldBypass ? 1.0f : 0.0f); }
void Detuner::setTight (float amount) { tight.setTarget (std::clamp (amount, 0.0f, 1.0f)); }

void Detuner::setFormantPreservation (bool shouldPreserve)
{
    for (auto& p : paths)
        p.engine->setFormantPreservation (shouldPreserve);
}

bool Detuner::isSwitchingModes() const
{
    return warmupLeft >= 0 || ! modeFade.isSettled();
}

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

void Detuner::updateModeSwitch (int numSamples)
{
    const float wanted = targetMode == Mode::studio ? 1.0f : 0.0f;
    auto& incoming = path (targetMode);

    // Fed during the previous chunk (the flags are recomputed below).
    const bool incomingWasRunning = incoming.running;

    if (std::equal_to<float>() (modeFade.getTarget(), wanted))
    {
        // Already there or fading there. A warm-up for the other mode that
        // is no longer wanted (switched back quickly) is dropped.
        warmupLeft = -1;
    }
    else if (! modeFade.isSettled())
    {
        // Switched back during a crossfade. Turning the fade around halfway
        // would put a corner in its curve (a faint tick), so let it finish
        // (30 ms at most); the next branch then fades straight back.
    }
    else if (warmupLeft < 0 && incomingWasRunning)
    {
        // The incoming path was audible until a moment ago (the fade that
        // just finished), so it is warm: crossfade at once.
        modeFade.setTarget (wanted);
    }
    else if (warmupLeft < 0)
    {
        // Start warming the incoming engine from a clean state. Its dry
        // delay has been fed all along, so it is ready already.
        incoming.engine->reset();
        warmupLeft = incoming.engine->getMemorySamples();
    }

    if (warmupLeft >= 0)
    {
        // Count this chunk: it will be fed (see `running` below). Once fed for
        // as long as the engine remembers, its output no longer depends on
        // the silence it started from, and the crossfade can begin.
        warmupLeft -= numSamples;
        if (warmupLeft <= 0)
        {
            warmupLeft = -1;
            modeFade.setTarget (wanted);
        }
    }

    // A path runs while it is (even partly) audible, about to be, or warming up.
    const float fade = modeFade.getCurrent(), fadeTarget = modeFade.getTarget();
    paths[0].running = fade < 1.0f || fadeTarget < 1.0f;
    paths[1].running = fade > 0.0f || fadeTarget > 0.0f;
    if (warmupLeft >= 0)
        incoming.running = true;
}

void Detuner::updateChannelLinking (const float* const* channels, int numChannels, int numSamples)
{
    int memory = 0;
    for (const auto& p : paths)
        memory = std::max (memory, p.engine->getMemorySamples());
    for (const auto& p : paths)
        if (p.engine->getMemorySamples() <= 0)
            memory = 0; // one engine can't say: never take the shortcut

    auto unlink = [&]
    {
        // Until now the other channels were identical, so their engine
        // state is channel 0's: hand it over and carry on in stereo. (An
        // idle engine is reset before it next runs, so it needs nothing.)
        for (auto& p : paths)
            if (p.running)
                for (int c = 1; c < numChannels; ++c)
                {
                    p.engine->copyChannelState (0, c);
                    p.tightState[(size_t) c] = p.tightState[0];
                }
        linked = false;
    };

    if (! linkingEnabled || numChannels < 2 || memory <= 0)
    {
        if (linked)
            unlink();
        identicalRun = 0;
        return;
    }

    bool identical = true;
    for (int c = 1; c < numChannels && identical; ++c)
        identical = std::equal (channels[0], channels[0] + numSamples, channels[c]);

    identicalRun = identical ? std::min (identicalRun + numSamples, (long) memory * 4) : 0;

    if (linked && ! identical)
    {
        unlink();
    }
    else if (! linked && identicalRun >= memory)
    {
        // Identical for longer than the engines remember, so every channel's
        // output is now the same: shift channel 0 only and copy it.
        linked = true;
    }
}

void Detuner::processChunk (float* const* channels, int numChannels, int n)
{
    updateModeSwitch (n);
    updateChannelLinking (channels, numChannels, n);
    const int engineChannels = linked ? 1 : numChannels;

    // Both dry delays always run (a copy, nearly free), so whichever mode
    // comes next has its dry signal ready.
    for (auto& p : paths)
        p.dryDelay.process (channels, p.dryPointers.data(), numChannels, n);

    // Pitch shift in control-rate slices, stepping the gliding ratio.
    for (int offset = 0; offset < n; offset += controlInterval)
    {
        const int length = std::min (controlInterval, n - offset);
        const float semitones = glide.skip (length);
        const float ratio = std::exp2 (semitones / 12.0f);

        // Tight follows the shift. At exactly 0 its output is skipped (bit
        // for bit); the filters keep running, and over the first quarter of
        // its range their output is crossfaded in, so turning it up from 0
        // never steps (even a filter close to "no change" is not identical).
        const float tightAmount = tight.skip (length);
        tightFilter.update (sampleRate, semitones, tightAmount);
        const bool tightOn = tightAmount > 0.0f || previousTightBlend > 0.0f;
        const float tightBlend = smoothstep (std::min (1.0f, tightAmount * 4.0f));
        const float blendStep = (tightBlend - previousTightBlend) / (float) length;

        for (int c = 0; c < engineChannels; ++c)
            inputPointers[(size_t) c] = channels[c] + offset;

        for (auto& p : paths)
        {
            if (! p.running)
                continue;
            for (int c = 0; c < engineChannels; ++c)
                p.wetSlicePointers[(size_t) c] = p.wetPointers[(size_t) c] + offset;
            p.engine->setPitchRatio (ratio);
            p.engine->process (inputPointers.data(), p.wetSlicePointers.data(), engineChannels, length);

            for (int c = 0; c < engineChannels; ++c)
            {
                auto* wet = p.wetSlicePointers[(size_t) c];
                auto& state = p.tightState[(size_t) c];
                for (int i = 0; i < length; ++i)
                {
                    // Settings glide sample by sample from the last slice's:
                    // the shelf's output weights multiply its states
                    // directly, so even small steps would tick.
                    const double t = (double) (i + 1) / (double) length;
                    const float filtered = TightFilter::run (lerp (previousTight.shelf, tightFilter.shelf, t), state[1],
                                                             TightFilter::run (lerp (previousTight.highPass, tightFilter.highPass, t),
                                                                               state[0], wet[i]));
                    // The blend moves sample by sample (a step per slice
                    // would tick).
                    if (tightOn)
                        wet[i] += (filtered - wet[i]) * (previousTightBlend + blendStep * (float) (i + 1));
                }
            }
        }
        previousTightBlend = tightBlend;
        previousTight = tightFilter;
    }

    if (linked)
        for (auto& p : paths)
            if (p.running)
                for (int c = 1; c < numChannels; ++c)
                    std::copy (p.wetBuffers[0].begin(), p.wetBuffers[0].begin() + n, p.wetBuffers[(size_t) c].begin());

    // At exactly 0 st (and once any glide has finished), fade to the delayed
    // dry signal so "no shift" is truly transparent.
    unity.setTarget (glide.isSettled() && glide.getTarget() == 0.0f ? 1.0f : 0.0f);

    // One mode's output sample: wet -> unity blend -> mix -> gain -> bypass.
    // Each stage takes an exact shortcut at 0 and 1, so the fully-dry
    // states (unity, mix 0, bypass) reproduce the delayed input bit for bit
    // instead of picking up rounding from a * (1 - x) + b * x.
    const auto chain = [] (float dry, float wet, float u, float m, float g, float b)
    {
        float y = u >= 1.0f ? dry : (u <= 0.0f ? wet : wet + (dry - wet) * u);
        if (m < 1.0f)
            y = m <= 0.0f ? dry : dry + (y - dry) * m;
        if (g != 1.0f)
            y *= g;
        if (b > 0.0f)
            y = b >= 1.0f ? dry : y + (dry - y) * b;
        return y;
    };

    const auto& live = paths[0];
    const auto& studio = paths[1];

    for (int i = 0; i < n; ++i)
    {
        const float u = smoothstep (unity.next());
        const float m = mix.next(), g = outputGain.next(), b = bypass.next();
        const float s = smoothstep (modeFade.next()); // how much Studio

        for (int c = 0; c < numChannels; ++c)
        {
            const auto ci = (size_t) c, ii = (size_t) i;
            float y;
            if (s <= 0.0f)
                y = chain (live.dryBuffers[ci][ii], live.wetBuffers[ci][ii], u, m, g, b);
            else if (s >= 1.0f)
                y = chain (studio.dryBuffers[ci][ii], studio.wetBuffers[ci][ii], u, m, g, b);
            else
            {
                const float a = chain (live.dryBuffers[ci][ii], live.wetBuffers[ci][ii], u, m, g, b);
                const float z = chain (studio.dryBuffers[ci][ii], studio.wetBuffers[ci][ii], u, m, g, b);
                y = a + (z - a) * s;
            }
            channels[c][i] = y;
        }
    }
}

} // namespace lowchug::dsp
