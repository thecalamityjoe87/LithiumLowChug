// Stress tests for the DSP core, built with the C++ standard library's own
// checks switched on (see tests/CMakeLists.txt): _GLIBCXX_ASSERTIONS for
// libstdc++ (GCC, most Linux builds), _LIBCPP_HARDENING_MODE for libc++
// (Clang on macOS). Every std::vector operator[] is then bounds-checked, and
// an out-of-range read stops the program even in an optimized build.
//
// Why: milestone 3 had a bug where a frame of sound followed by a frame of
// silence reused the sound frame's list of modeled partials, indexing a
// region list that was now empty. The stale memory it read was harmless, so
// the audio was fine and only AddressSanitizer saw it. With bounds checks, the
// same read aborts this test in a plain release build, so the regression is
// caught by ctest, not only by the separate sanitizer builds.
//
// Nothing is compared against expected audio here (DspTests does that). The
// test passes if every scenario runs to the end without a failed check and
// every output sample is finite.

#include "dsp/Detuner.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

using lowchug::dsp::Detuner;

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++failures;
}

using Channels = std::vector<std::vector<float>>;

/** A plucked low note with harmonics, starting at `start` seconds, lasting
    `length` seconds, with exact silence before and after. */
void addPluck (std::vector<float>& signal, double sampleRate, double hz, double start, double length, float level)
{
    const auto first = (size_t) (start * sampleRate), last = std::min (signal.size(), (size_t) ((start + length) * sampleRate));
    for (size_t i = first; i < last; ++i)
    {
        const double t = (double) (i - first) / sampleRate;
        double v = 0.0;
        for (int h = 1; h * hz < 12000.0; ++h)
            v += std::exp (-t * (1.0 + 0.4 * h)) / h * std::sin (2.0 * std::numbers::pi * h * hz * t + h);
        signal[i] += (float) (level * std::min (1.0, t / 0.0015) * v);
    }
}

/** Plays `input` through `detuner` in random-sized blocks (1 to 700, more
    than the prepared maximum), letting `beforeBlock` change settings. */
bool runRandomBlocks (Detuner& detuner, Channels& buffer, unsigned seed,
                      const std::function<void (Detuner&, size_t)>& beforeBlock = {})
{
    std::mt19937 random (seed);
    std::uniform_int_distribution<int> blockSize (1, 700);
    std::vector<float*> pointers (buffer.size());
    for (size_t pos = 0; pos < buffer[0].size();)
    {
        const int n = (int) std::min<size_t> ((size_t) blockSize (random), buffer[0].size() - pos);
        if (beforeBlock)
            beforeBlock (detuner, pos);
        for (size_t c = 0; c < buffer.size(); ++c)
            pointers[c] = buffer[c].data() + pos;
        detuner.process (pointers.data(), (int) buffer.size(), n);
        pos += (size_t) n;
    }

    for (const auto& channel : buffer)
        for (float v : channel)
            if (! std::isfinite (v))
                return false;
    return true;
}

const char* modeName (Detuner::Mode m) { return m == Detuner::Mode::studio ? "Studio" : "Live"; }

/** Sound, then silence longer than any analysis window (8192 samples at
    48 kHz is 0.17 s), then sound again: the frames where sound turns into
    silence are where stale per-frame data used to be read. Short bursts,
    single clicks and very quiet notes vary how many partials the last
    sounding frame had. */
void testSoundAndSilence()
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (auto mode : { Detuner::Mode::live, Detuner::Mode::studio })
            for (bool formant : { false, true })
                for (int channels : { 1, 2 })
                {
                    std::vector<float> take ((size_t) (4.0 * sampleRate), 0.0f);
                    addPluck (take, sampleRate, 82.41, 0.10, 0.60, 0.5f);  // a low E, cut off: sound then silence
                    addPluck (take, sampleRate, 110.0, 1.00, 0.30, 0.5f);  // an A
                    addPluck (take, sampleRate, 123.47, 1.00, 0.30, 0.3f); // with a B: a power chord
                    addPluck (take, sampleRate, 73.42, 1.80, 0.02, 0.5f);  // a 20 ms burst
                    take[(size_t) (2.40 * sampleRate)] = 0.9f;             // a lone click
                    addPluck (take, sampleRate, 82.41, 3.00, 0.40, 1.0e-5f); // very quiet (-100 dB)

                    Channels buffer ((size_t) channels, take);
                    if (channels == 2)
                        for (auto& v : buffer[1])
                            v *= 0.7f; // not identical: no channel linking

                    Detuner detuner;
                    detuner.setMode (mode);
                    detuner.setFormantPreservation (formant);
                    detuner.setTight (0.5f);
                    detuner.setShiftSemitones (-5.0f);
                    detuner.prepare (sampleRate, 512, channels);
                    const bool ok = runRandomBlocks (detuner, buffer, 1);

                    char what[200];
                    std::snprintf (what, sizeof (what), "sound / silence / sound: %s, formant %s, %d ch, %.1f kHz",
                                   modeName (mode), formant ? "on" : "off", channels, sampleRate / 1000.0);
                    check (ok, what);
                }
}

/** Everything changing at once while sound starts and stops: mode switches
    (both engines running), the shift across its whole range, channels
    linking and unlinking, bypass and mix. */
void testEverythingMoving()
{
    const double sampleRate = 48000.0;
    std::vector<float> take ((size_t) (6.0 * sampleRate), 0.0f);
    for (int k = 0; k < 12; ++k)
        addPluck (take, sampleRate, 82.41 * std::exp2 (k % 5 / 12.0), 0.5 * k, k % 3 == 0 ? 0.45 : 0.15, 0.4f);

    // Channels identical for the first half (linked), different after.
    Channels buffer { take, take };
    for (size_t i = buffer[1].size() / 2; i < buffer[1].size(); ++i)
        buffer[1][i] *= -0.5f;

    Detuner detuner;
    detuner.setFormantPreservation (true);
    detuner.prepare (sampleRate, 512, 2);
    std::mt19937 random (9);
    std::uniform_real_distribution<float> shift (-12.0f, 2.0f), unit (0.0f, 1.0f);
    const bool ok = runRandomBlocks (detuner, buffer, 2, [&] (Detuner& d, size_t)
    {
        const float dice = unit (random);
        if (dice < 0.02f)
            d.setMode (d.getMode() == Detuner::Mode::live ? Detuner::Mode::studio : Detuner::Mode::live);
        else if (dice < 0.10f)
            d.setShiftSemitones (dice < 0.04f ? 0.0f : shift (random)); // including exactly 0 (the dry crossfade)
        else if (dice < 0.12f)
            d.setTight (unit (random));
        else if (dice < 0.13f)
            d.setBypassed (unit (random) < 0.5f);
        else if (dice < 0.14f)
            d.setMix (unit (random));
        else if (dice < 0.15f)
            d.setFormantPreservation (unit (random) < 0.5f);
    });
    check (ok, "mode switches, shift, Tight, bypass, mix and channel linking changing during sound and silence");
}

/** Inputs no guitar makes: full-scale noise, DC, a square wave, values in
    the denormal range, and NaN and infinity. */
void testHostileInput()
{
    const double sampleRate = 48000.0;
    const auto length = (size_t) (3.0 * sampleRate);
    std::mt19937 random (5);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    struct Case { const char* name; std::function<float (size_t)> sample; };
    const Case cases[] = {
        { "full-scale noise", [&] (size_t) { return noise (random); } },
        { "DC at full scale", [] (size_t) { return 1.0f; } },
        { "full-scale 55 Hz square", [=] (size_t i) { return std::fmod ((double) i * 55.0 / sampleRate, 1.0) < 0.5 ? 1.0f : -1.0f; } },
        { "denormal-range noise", [&] (size_t) { return noise (random) * 1.0e-39f; } },
        { "NaN and infinity bursts in a note", [=] (size_t i) {
              if (i % 20000 < 3)
                  return i % 40000 < 3 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
              return (float) (0.3 * std::sin (2.0 * std::numbers::pi * 82.41 * (double) i / sampleRate));
          } },
    };

    for (const auto& c : cases)
        for (auto mode : { Detuner::Mode::live, Detuner::Mode::studio })
        {
            std::vector<float> take (length);
            for (size_t i = 0; i < length; ++i)
                take[i] = c.sample (i);
            // The last second is silence: nothing may be left ringing or broken.
            std::fill (take.end() - (long) sampleRate, take.end(), 0.0f);

            Channels buffer { take };
            Detuner detuner;
            detuner.setMode (mode);
            detuner.setFormantPreservation (true);
            detuner.setTight (1.0f);
            detuner.setShiftSemitones (-7.0f);
            detuner.prepare (sampleRate, 512, 1);
            bool ok = runRandomBlocks (detuner, buffer, 3);

            // NaN and infinity count as silence (the engine), so once the
            // dry path's delay has passed them, the output is clean again.
            // The final half second must be silent, or nearly.
            float tail = 0.0f;
            for (size_t i = length - (size_t) (0.5 * sampleRate); i < length; ++i)
                tail = std::max (tail, std::abs (buffer[0][i]));
            ok = ok && tail < 1.0e-3f;

            char what[200];
            std::snprintf (what, sizeof (what), "%s, %s: finite output, silent after the input stops", c.name, modeName (mode));
            check (ok, what);
        }
}

} // namespace

int main()
{
    testSoundAndSilence();
    testEverythingMoving();
    testHostileInput();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
