// Tests for the plugin-independent DSP core.
//
// A tiny self-contained runner: each check prints a line, and the exit code
// is non-zero if anything failed (which is all ctest needs).

#include "dsp/engines/PhaseVocoderEngine.h"

#include <signalsmith-linear/fft.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <numbers>
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

// The engine configurations the plugin will ship with (see docs/engine-evaluation.md).
PhaseVocoderEngine::Config liveConfig() { return { 4096, 4, true, 6.0f, 1024 }; }
PhaseVocoderEngine::Config studioConfig() { return { 8192, 8, true, 6.0f, 2048 }; }

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

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
