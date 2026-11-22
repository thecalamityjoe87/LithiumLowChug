// LowChug CPU profiler: the complete effect (dsp::Detuner, as the plugin runs
// it) over the synthetic DI corpus, like a host would play it.
//
// Built twice from this one file (see CMakeLists.txt):
//
//   lowchug-profile          the shipping DSP code. Prints average CPU (mono,
//                            linked stereo, true stereo, sample rates) and the
//                            worst block at 32 / 64 / 128-sample buffers,
//                            including while switching modes.
//   lowchug-profile-stages   the DSP code compiled with LOWCHUG_PROFILE (see
//                            src/dsp/Profiling.h). Prints where the time goes:
//                            FFTs, analysis, the partial model, and so on.
//
// Timing depends on the machine and on what else is running: every number is
// the best of several runs (the least disturbed by other processes). Run it
// on an otherwise idle machine: an IDE indexing in the background is enough
// to double the worst-block figures.

#include "../engine-eval/TestSignals.h"

#include "dsp/Detuner.h"
#include "dsp/Profiling.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

using lowchug::dsp::Detuner;

namespace
{

using Channels = std::vector<std::vector<float>>;

constexpr int repeats = 3;
// The worst block is a single maximum, which one OS interruption can ruin:
// it gets more tries.
constexpr int worstBlockRepeats = 5;
constexpr float shiftSemitones = -5.0f;

const char* modeName (Detuner::Mode m) { return m == Detuner::Mode::studio ? "Studio" : "Live"; }

enum class Layout { mono, linkedStereo, trueStereo };
const char* layoutName (Layout l)
{
    return l == Layout::mono ? "mono" : (l == Layout::linkedStereo ? "stereo, identical channels" : "true stereo");
}

/** The corpus as one, two identical, or two different channels. The second
    channel of "true stereo" is a double-tracked take: the same playing
    7 ms later and 2 dB quieter, so the channels never match. */
Channels makeInput (double sampleRate, Layout layout)
{
    const auto corpus = lowchug::eval::makeCorpus();
    const auto take = lowchug::eval::renderCorpus (corpus, sampleRate, 1.0, 1.0f);
    if (layout == Layout::mono)
        return { take };
    if (layout == Layout::linkedStereo)
        return { take, take };

    std::vector<float> second (take.size(), 0.0f);
    const auto delay = (size_t) (0.007 * sampleRate);
    for (size_t i = delay; i < take.size(); ++i)
        second[i] = 0.8f * take[i - delay];
    return { take, second };
}

struct RunStats
{
    double cpuPercent = 0;   // processing time / audio time, on one core
    double worstPercent = 0; // the slowest block, as a share of its deadline (the block's duration)
};

/** Plays `input` through a fresh Detuner in fixed-size blocks.
    `beforeBlock` can change settings between blocks (automation). */
RunStats run (Detuner::Mode mode, bool formant, const Channels& input, double sampleRate, int blockSize,
              const std::function<void (Detuner&, size_t)>& beforeBlock = {})
{
    Detuner detuner;
    detuner.setMode (mode);
    detuner.setShiftSemitones (shiftSemitones);
    detuner.setFormantPreservation (formant);
    detuner.prepare (sampleRate, blockSize, (int) input.size());

    auto buffer = input; // processed in place
    std::vector<float*> pointers (buffer.size());
    const double deadline = blockSize / sampleRate;

    RunStats stats;
    double total = 0;
    for (size_t pos = 0; pos + (size_t) blockSize <= buffer[0].size(); pos += (size_t) blockSize)
    {
        if (beforeBlock)
            beforeBlock (detuner, pos);
        for (size_t c = 0; c < buffer.size(); ++c)
            pointers[c] = buffer[c].data() + pos;

        const auto t0 = std::chrono::steady_clock::now();
        detuner.process (pointers.data(), (int) pointers.size(), blockSize);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

        total += seconds;
        stats.worstPercent = std::max (stats.worstPercent, 100.0 * seconds / deadline);
    }
    stats.cpuPercent = 100.0 * total / ((double) buffer[0].size() / sampleRate);
    return stats;
}

/** The best (least disturbed) of several runs, for each number separately.
    (The stages build keeps its own best run instead.) */
[[maybe_unused]] RunStats bestOf (const std::function<RunStats()>& once, int tries = repeats)
{
    RunStats best { 1e9, 1e9 };
    for (int r = 0; r < tries; ++r)
    {
        const auto s = once();
        best.cpuPercent = std::min (best.cpuPercent, s.cpuPercent);
        best.worstPercent = std::min (best.worstPercent, s.worstPercent);
    }
    return best;
}

#if ! defined(LOWCHUG_PROFILE)

void printCpu()
{
    std::printf ("## Average CPU (%% of one core), 48 kHz, 128-sample blocks, %+.0f st\n\n", (double) shiftSemitones);
    std::printf ("| Mode | Formant | mono | stereo, identical channels | true stereo |\n|---|---|---|---|---|\n");
    const Channels inputs[] = { makeInput (48000.0, Layout::mono), makeInput (48000.0, Layout::linkedStereo),
                                makeInput (48000.0, Layout::trueStereo) };
    for (auto mode : { Detuner::Mode::live, Detuner::Mode::studio })
        for (bool formant : { false, true })
        {
            std::printf ("| %s | %s |", modeName (mode), formant ? "on" : "off");
            for (const auto& input : inputs)
                std::printf (" %.2f %% |", bestOf ([&] { return run (mode, formant, input, 48000.0, 128); }).cpuPercent);
            std::printf ("\n");
        }

    std::printf ("\n## Average CPU at other sample rates (mono, Formant on, 128-sample blocks)\n\n");
    std::printf ("| Mode | 44.1 kHz | 48 kHz | 96 kHz |\n|---|---|---|---|\n");
    for (auto mode : { Detuner::Mode::live, Detuner::Mode::studio })
    {
        std::printf ("| %s |", modeName (mode));
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto input = makeInput (rate, Layout::mono);
            std::printf (" %.2f %% |", bestOf ([&] { return run (mode, true, input, rate, 128); }).cpuPercent);
        }
        std::printf ("\n");
    }
}

void printWorstBlocks()
{
    std::printf ("\n## Worst block (%% of the block's deadline), 48 kHz, Formant on, %+.0f st\n\n", (double) shiftSemitones);
    std::printf ("The slowest single block of the whole corpus. Over 100 %% would be a dropout.\n\n");
    std::printf ("| Mode | Channels | 128 | 64 | 32 |\n|---|---|---|---|---|\n");
    for (auto mode : { Detuner::Mode::live, Detuner::Mode::studio })
        for (auto layout : { Layout::mono, Layout::trueStereo })
        {
            const auto input = makeInput (48000.0, layout);
            std::printf ("| %s | %s |", modeName (mode), layoutName (layout));
            for (int size : { 128, 64, 32 })
                std::printf (" %.0f %% |", bestOf ([&] { return run (mode, true, input, 48000.0, size); }, worstBlockRepeats).worstPercent);
            std::printf ("\n");
        }

    // While switching, the incoming engine warms up (about a quarter of a
    // second) and both engines run: the heaviest moments the plugin has.
    std::printf ("\n## Worst block while switching modes every 0.5 s (true stereo, Formant on)\n\n");
    std::printf ("| 128 | 64 | 32 | average CPU |\n|---|---|---|---|\n");
    const auto input = makeInput (48000.0, Layout::trueStereo);
    const auto halfSecond = (size_t) (0.5 * 48000.0);
    auto toggle = [halfSecond] (Detuner& d, size_t pos)
    { d.setMode ((pos / halfSecond) % 2 == 0 ? Detuner::Mode::live : Detuner::Mode::studio); };
    std::printf ("|");
    double cpu = 0;
    for (int size : { 128, 64, 32 })
    {
        const auto s = bestOf ([&] { return run (Detuner::Mode::live, true, input, 48000.0, size, toggle); }, worstBlockRepeats);
        std::printf (" %.0f %% |", s.worstPercent);
        if (size == 128)
            cpu = s.cpuPercent;
    }
    std::printf (" %.2f %% |\n", cpu);
}

#else

void printStages()
{
    namespace prof = lowchug::dsp::profiling;
    std::printf ("## Where the time goes, 48 kHz, 128-sample blocks, Formant on, %+.0f st\n\n", (double) shiftSemitones);
    std::printf ("Exclusive time per stage (nested stages are not counted twice), as a share of the total and as %% of one core.\n"
                 "The timers themselves add a little; compare the total with lowchug-profile's numbers.\n\n");

    for (auto layout : { Layout::mono, Layout::trueStereo })
    {
        const auto input = makeInput (48000.0, layout);
        const double audioSeconds = (double) input[0].size() / 48000.0;
        for (auto mode : { Detuner::Mode::live, Detuner::Mode::studio })
        {
            // Keep the least disturbed run's breakdown.
            std::array<double, (size_t) prof::Stage::count> best {};
            double bestTotal = 1e9;
            for (int r = 0; r < repeats; ++r)
            {
                prof::clear();
                run (mode, true, input, 48000.0, 128);
                prof::charge (prof::Clock::now());
                double total = 0;
                for (size_t s = 1; s < best.size(); ++s)
                    total += prof::seconds[s];
                if (total < bestTotal)
                {
                    bestTotal = total;
                    best = prof::seconds;
                }
            }

            std::printf ("### %s, %s: %.2f %% of one core in total\n\n", modeName (mode), layoutName (layout),
                         100.0 * bestTotal / audioSeconds);
            std::printf ("| Stage | share | %% of one core |\n|---|---|---|\n");
            for (size_t s = 1; s < best.size(); ++s)
                std::printf ("| %s | %.1f %% | %.2f %% |\n", prof::stageNames[s], 100.0 * best[s] / bestTotal,
                             100.0 * best[s] / audioSeconds);
            std::printf ("\n");
        }
    }
}

#endif

} // namespace

int main()
{
    std::printf ("# LowChug CPU profile\n\n");
#if defined(LOWCHUG_PROFILE)
    printStages();
#else
    printCpu();
    printWorstBlocks();
#endif
    return 0;
}
