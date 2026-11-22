// Tests for the plugin-independent DSP core.
//
// A tiny self-contained runner: each check prints a line, and the exit code
// is non-zero if anything failed (which is all ctest needs).

#include "dsp/Detuner.h"
#include "dsp/EngineConfigs.h"
#include "dsp/engines/PhaseVocoderEngine.h"

#include <signalsmith-linear/fft.h>

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

using namespace lowchug::dsp;

namespace
{
int failures = 0;

void check (bool ok, const char* what, double value = 0.0)
{
    std::printf ("%s  %s (%.4g)\n", ok ? "PASS" : "FAIL", what, value);
    if (! ok)
        ++failures;
}

PhaseVocoderEngine::Config liveConfig() { return configs::live(); }
PhaseVocoderEngine::Config studioConfig() { return configs::studio(); }

std::vector<float> runEngine (PhaseVocoderEngine& engine, const std::vector<float>& input, double sampleRate,
                              float ratio, const std::function<int()>& nextBlock)
{
    engine.prepare (sampleRate, 512, 1);
    engine.setPitchRatio (ratio);
    engine.reset();

    std::vector<float> output (input.size());
    std::vector<float> inBlock (512), outBlock (512);
    for (size_t pos = 0; pos < input.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) nextBlock(), input.size() - pos);
        std::copy (input.begin() + (long) pos, input.begin() + (long) pos + n, inBlock.begin());
        const float* in = inBlock.data();
        float* out = outBlock.data();
        engine.process (&in, &out, 1, n);
        std::copy (outBlock.begin(), outBlock.begin() + n, output.begin() + (long) pos);
        pos += (size_t) n;
    }
    return output;
}

/** Strongest frequency in signal[start, start+length), refined by parabolic
    interpolation of the log magnitude. */
double measureFrequency (const std::vector<float>& signal, size_t start, size_t length, double sampleRate)
{
    size_t fftSize = 1;
    while (fftSize < length * 2)
        fftSize *= 2;
    signalsmith::linear::RealFFT<float> fft (fftSize);
    std::vector<float> time (fftSize, 0.0f);
    std::vector<std::complex<float>> freq (fftSize / 2);
    for (size_t i = 0; i < length; ++i)
        time[i] = signal[start + i] * float (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) length));
    fft.fft (time.data(), freq.data());

    size_t peak = 2;
    for (size_t k = 2; k < freq.size() - 1; ++k)
        if (std::norm (freq[k]) > std::norm (freq[peak]))
            peak = k;
    const double a = std::log (std::norm (freq[peak - 1])), b = std::log (std::norm (freq[peak])),
                 c = std::log (std::norm (freq[peak + 1]));
    return ((double) peak + 0.5 * (a - c) / (a - 2.0 * b + c)) * sampleRate / (double) fftSize;
}

std::vector<float> sine (double frequency, double seconds, double sampleRate)
{
    std::vector<float> s ((size_t) (seconds * sampleRate));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = float (0.5 * std::sin (2.0 * std::numbers::pi * frequency * (double) i / sampleRate));
    return s;
}

void testPitchAccuracy (const char* label, PhaseVocoderEngine::Config config, double sampleRate)
{
    PhaseVocoderEngine engine (config);
    double worst = 0.0;
    for (double f : { 82.41, 110.0, 440.0 })
        for (int semitones : { -1, -2, -5, -7, -12, 2 })
        {
            const double ratio = std::exp2 (semitones / 12.0);
            const auto out = runEngine (engine, sine (f, 2.5, sampleRate), sampleRate, (float) ratio, [] { return 128; });
            const double measured = measureFrequency (out, (size_t) (1.0 * sampleRate), (size_t) (1.2 * sampleRate), sampleRate);
            worst = std::max (worst, std::abs (1200.0 * std::log2 (measured / (f * ratio))));
        }
    char what[128];
    std::snprintf (what, sizeof (what), "%s @ %.0f Hz: shifted sines within +/-2 cents (worst cents)", label, sampleRate);
    check (worst <= 2.0, what, worst);
}

void testLatency (const char* label, PhaseVocoderEngine::Config config, double sampleRate)
{
    PhaseVocoderEngine engine (config);
    std::vector<float> impulse ((size_t) sampleRate, 0.0f);
    const size_t at = (size_t) (0.2 * sampleRate);
    impulse[at] = 1.0f;
    const auto out = runEngine (engine, impulse, sampleRate, 1.0f, [] { return 128; });

    size_t peak = 0;
    for (size_t i = 0; i < out.size(); ++i)
        if (std::abs (out[i]) > std::abs (out[peak]))
            peak = i;
    const int measured = (int) peak - (int) at;
    char what[128];
    std::snprintf (what, sizeof (what), "%s @ %.0f Hz: reported latency %d == measured (measured)", label, sampleRate,
                   engine.getLatencySamples());
    check (measured == engine.getLatencySamples(), what, measured);
}

void testBlockSizeIndependence()
{
    // Hosts may send any block size, varying from call to call; the output
    // must not depend on it.
    PhaseVocoderEngine engine (liveConfig());
    const auto input = sine (98.0, 1.5, 48000.0);
    const auto a = runEngine (engine, input, 48000.0, 0.75f, [] { return 128; });
    unsigned state = 1;
    const auto b = runEngine (engine, input, 48000.0, 0.75f, [&state] {
        state = state * 1664525u + 1013904223u;
        return 1 + (int) ((state >> 8) % 512u);
    });
    double maxDiff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (a[i] - b[i]));
    check (maxDiff == 0.0, "random 1-512 sample blocks give bit-identical output (max diff)", maxDiff);
}
// ---- Detuner (the full effect chain) ----------------------------------------

using Stereo = std::vector<std::vector<float>>;

std::function<int()> randomBlocks (unsigned seed)
{
    auto state = std::make_shared<unsigned> (seed);
    return [state] {
        *state = *state * 1664525u + 1013904223u;
        return 1 + (int) ((*state >> 8) % 512u);
    };
}

/** Streams `input` through the detuner in host-like blocks. `beforeBlock`
    can change settings between blocks (automation). */
Stereo runDetuner (Detuner& detuner, Stereo input, const std::function<int()>& nextBlock,
                   const std::function<void (size_t)>& beforeBlock = {})
{
    const int numChannels = (int) input.size();
    std::vector<float*> pointers ((size_t) numChannels);
    for (size_t pos = 0; pos < input[0].size();)
    {
        const int n = (int) std::min<size_t> ((size_t) nextBlock(), input[0].size() - pos);
        if (beforeBlock)
            beforeBlock (pos);
        for (int c = 0; c < numChannels; ++c)
            pointers[(size_t) c] = input[(size_t) c].data() + pos;
        detuner.process (pointers.data(), numChannels, n);
        pos += (size_t) n;
    }
    return input; // processed in place
}

Stereo noise (int channels, double seconds, double sampleRate, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    Stereo s ((size_t) channels, std::vector<float> ((size_t) (seconds * sampleRate)));
    for (auto& ch : s)
        for (auto& v : ch)
            v = dist (rng);
    return s;
}

/** True if out[i] == in[i - delay] exactly (and silence before). */
bool isExactlyDelayed (const Stereo& in, const Stereo& out, int delay)
{
    for (size_t c = 0; c < in.size(); ++c)
        for (size_t i = 0; i < in[c].size(); ++i)
        {
            const float expected = i >= (size_t) delay ? in[c][i - (size_t) delay] : 0.0f;
            if (out[c][i] != expected)
                return false;
        }
    return true;
}

void testDetunerNullAtZero()
{
    for (double sampleRate : { 44100.0, 96000.0, 48000.0 }) // re-prepare at each rate, like a host would
    {
        Detuner detuner;
        detuner.setShiftSemitones (0.0f);
        detuner.prepare (sampleRate, 512, 2);
        const auto in = noise (2, 1.0, sampleRate, 7);
        const auto out = runDetuner (detuner, in, randomBlocks (3));
        char what[128];
        std::snprintf (what, sizeof (what), "Detuner @ %.0f Hz: 0 st nulls bit-exactly against the input delayed by the latency",
                       sampleRate);
        check (isExactlyDelayed (in, out, detuner.getLatencySamples()), what, detuner.getLatencySamples());
    }
}

void testDetunerDryStatesExact()
{
    const auto in = noise (2, 1.0, 48000.0, 11);
    {
        Detuner detuner;
        detuner.setShiftSemitones (-5.0f);
        detuner.setMix (0.0f);
        detuner.prepare (48000.0, 512, 2);
        check (isExactlyDelayed (in, runDetuner (detuner, in, randomBlocks (5)), detuner.getLatencySamples()),
               "Detuner: mix 0 % gives the latency-delayed dry signal exactly");
    }
    {
        Detuner detuner;
        detuner.setShiftSemitones (-5.0f);
        detuner.setBypassed (true);
        detuner.prepare (48000.0, 512, 2);
        check (isExactlyDelayed (in, runDetuner (detuner, in, randomBlocks (6)), detuner.getLatencySamples()),
               "Detuner: bypass gives the latency-delayed dry signal exactly (no time jump)");
    }
}

void testDetunerWetLatency()
{
    // A 1-cent shift runs the engine but leaves an impulse's peak in place:
    // the shifted path must line up with the dry delay.
    Detuner detuner;
    detuner.setShiftSemitones (0.01f);
    detuner.prepare (48000.0, 512, 1);
    Stereo impulse (1, std::vector<float> (48000, 0.0f));
    impulse[0][12000] = 1.0f;
    const auto out = runDetuner (detuner, impulse, [] { return 128; });
    size_t peak = 0;
    for (size_t i = 0; i < out[0].size(); ++i)
        if (std::abs (out[0][i]) > std::abs (out[0][peak]))
            peak = i;
    const int measured = (int) peak - 12000;
    check (measured == detuner.getLatencySamples(), "Detuner: shifted path latency == reported latency == dry delay (measured)",
           measured);
}

/** Loudest 2 ms of content above 2 kHz relative to the signal's RMS, in dB.
    The test signals are low notes, so anything up there is a click. */
double worstClickDb (const std::vector<float>& signal, size_t start, double sampleRate)
{
    // 8th-order high-pass at 2 kHz (four cascaded RBJ biquads).
    const double w = 2.0 * std::numbers::pi * 2000.0 / sampleRate, alpha = std::sin (w) / (2.0 * 0.7071), cw = std::cos (w);
    const double a0 = 1.0 + alpha, b0 = (1.0 + cw) / 2.0 / a0, b1 = -(1.0 + cw) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
    std::vector<double> hp (signal.begin(), signal.end());
    for (int pass = 0; pass < 4; ++pass)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : hp)
        {
            const double y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = y;
            v = y;
        }
    }
    double total = 0;
    for (size_t i = start; i < signal.size(); ++i)
        total += (double) signal[i] * signal[i];
    const double rms = std::sqrt (total / (double) (signal.size() - start));

    const auto window = (size_t) (0.002 * sampleRate);
    double worst = 0;
    for (size_t i = start; i + window < hp.size(); i += window / 2)
    {
        double e = 0;
        for (size_t j = i; j < i + window; ++j)
            e += hp[j] * hp[j];
        worst = std::max (worst, std::sqrt (e / (double) window));
    }
    return 20.0 * std::log10 (worst / rms + 1e-12);
}

std::vector<float> lowChord (double seconds, double sampleRate)
{
    std::vector<float> s ((size_t) (seconds * sampleRate));
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / sampleRate;
        s[i] = float (0.25 * (std::sin (2 * std::numbers::pi * 82.41 * t) + std::sin (2 * std::numbers::pi * 123.47 * t)
                              + std::sin (2 * std::numbers::pi * 164.81 * t)));
    }
    return s;
}

void testDetunerAutomationClicks()
{
    // The engine leaves a faint high-frequency residue whose level depends on
    // the exact shift (a known engine limitation, tracked separately). A
    // click is anything a CHANGE adds on top of that, so each transition is
    // compared with the worst steady residue anywhere along its glide path.
    //
    // The smoothing, glide and crossfades live in Detuner and don't depend on
    // the window size, so this runs on the Studio engine, whose residue is
    // ~25 dB lower than Live's: that makes the test sensitive enough to catch
    // a click of 0.1 % of full scale (checked below).
    const double sampleRate = 48000.0;
    const Stereo chord { lowChord (1.5, sampleRate) };
    const auto jumpAt = (size_t) (0.75 * sampleRate), measureFrom = (size_t) (0.4 * sampleRate);
    auto makeDetuner = [] { return Detuner (std::make_unique<PhaseVocoderEngine> (configs::studio())); };

    auto steadyOutput = [&] (float semitones) {
        auto d = makeDetuner();
        d.setShiftSemitones (semitones);
        d.prepare (sampleRate, 512, 1);
        return runDetuner (d, chord, [] { return 128; })[0];
    };
    auto worstResidueAlong = [&] (float from, float to) {
        double worst = -200.0;
        const int steps = std::max (1, (int) std::ceil (std::abs (to - from) / 0.25f));
        for (int i = 0; i <= steps; ++i)
            worst = std::max (worst, worstClickDb (steadyOutput (from + (to - from) * (float) i / (float) steps), measureFrom, sampleRate));
        return worst;
    };

    {
        // Test the test: a 0.1 % single-sample click must stand out.
        auto withClick = steadyOutput (-7.0f);
        const double clean = worstClickDb (withClick, measureFrom, sampleRate);
        withClick[(size_t) (0.9 * sampleRate)] += 0.001f;
        const double margin = worstClickDb (withClick, measureFrom, sampleRate) - clean;
        check (margin > 3.0, "Click metric detects a 0.1 % click (dB above residue)", margin);
    }

    // Stepped automation between tunings, including into and out of 0 st
    // (which crossfades to and from the dry path) and the full -12 -> +2 range.
    const std::pair<float, float> jumps[] = { { -2, -5 }, { -5, 0 }, { 0, -7 }, { -7, -12 }, { -12, 2 },
                                              { 2, 0 }, { 0, -1 }, { -1, -5 }, { -5, -3 } };
    double worstExcess = -100.0;
    for (const auto& [from, to] : jumps)
    {
        auto d = makeDetuner();
        d.setShiftSemitones (from);
        d.prepare (sampleRate, 512, 1);
        const auto out = runDetuner (d, chord, [] { return 128; }, [&, to = to] (size_t pos) {
            if (pos >= jumpAt)
                d.setShiftSemitones (to);
        });
        worstExcess = std::max (worstExcess, worstClickDb (out[0], measureFrom, sampleRate) - worstResidueAlong (from, to));
    }
    check (worstExcess < 3.0, "Detuner: stepped shift automation adds no clicks (worst dB above steady residue)", worstExcess);

    // Fine tune swept continuously, retargeted on every (random-sized) block.
    auto sweeping = makeDetuner();
    sweeping.prepare (sampleRate, 512, 1);
    const auto swept = runDetuner (sweeping, chord, randomBlocks (9), [&] (size_t pos) {
        sweeping.setShiftSemitones (-2.0f + 0.5f * (float) std::sin ((double) pos / sampleRate * 6.0));
    });
    const double sweepExcess = worstClickDb (swept[0], measureFrom, sampleRate) - worstResidueAlong (-2.5f, -1.5f);
    check (sweepExcess < 3.0, "Detuner: per-block fine-tune automation adds no clicks (worst dB above steady residue)", sweepExcess);
}

void testDetunerChannelLinking()
{
    // A mono source on a stereo track, which then briefly diverges (e.g. a
    // stereo effect before LowChug) and goes back to identical.
    const double sampleRate = 48000.0;
    const auto chord = lowChord (5.0, sampleRate);
    Stereo in { chord, chord };
    for (size_t i = (size_t) (1.5 * sampleRate); i < (size_t) (2.5 * sampleRate); ++i)
        in[1][i] += 0.2f * (float) std::sin (2 * std::numbers::pi * 196.0 * (double) i / sampleRate);

    Detuner linkedDetuner, fullDetuner;
    fullDetuner.setChannelLinkingEnabled (false);
    bool wasLinked = false;
    for (auto* d : { &linkedDetuner, &fullDetuner })
    {
        d->setShiftSemitones (-5.0f);
        d->prepare (sampleRate, 512, 2);
    }
    const auto a = runDetuner (linkedDetuner, in, [] { return 128; }, [&] (size_t) {
        wasLinked = wasLinked || linkedDetuner.isChannelLinkingActive();
    });
    const auto b = runDetuner (fullDetuner, in, [] { return 128; });

    double maxDiff = 0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < a[c].size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (a[c][i] - b[c][i]));
    check (wasLinked && linkedDetuner.isChannelLinkingActive(), "Detuner: identical stereo channels get linked (shifted once)");
    check (maxDiff < 1.0e-4, "Detuner: linked output matches full stereo processing, through link/unlink (max diff)", maxDiff);

    // Cost: linked stereo should cost about the same as mono.
    auto timeIt = [&] (bool linking) {
        Detuner d;
        d.setChannelLinkingEnabled (linking);
        d.setShiftSemitones (-5.0f);
        d.prepare (sampleRate, 512, 2);
        Stereo same { chord, chord };
        const auto t0 = std::chrono::steady_clock::now();
        runDetuner (d, same, [] { return 128; });
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const double unlinkedSeconds = timeIt (false), linkedSeconds = timeIt (true);
    std::printf ("      (identical stereo: %.2f%% CPU linked vs %.2f%% unlinked at 48 kHz / 128)\n",
                 100.0 * linkedSeconds / 5.0, 100.0 * unlinkedSeconds / 5.0);
    check (linkedSeconds < 0.8 * unlinkedSeconds, "Detuner: linked stereo is cheaper than full stereo (time ratio)",
           linkedSeconds / unlinkedSeconds);
}
} // namespace

int main()
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        testPitchAccuracy ("Live", liveConfig(), sampleRate);
        testLatency ("Live", liveConfig(), sampleRate);
    }
    testPitchAccuracy ("Studio", studioConfig(), 48000.0);
    testLatency ("Studio", studioConfig(), 48000.0);
    testBlockSizeIndependence();

    testDetunerNullAtZero();
    testDetunerDryStatesExact();
    testDetunerWetLatency();
    testDetunerAutomationClicks();
    testDetunerChannelLinking();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
