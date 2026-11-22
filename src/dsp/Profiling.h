#pragma once

/**
    Opt-in stage timers, for the profiling tool (tools/profile) only.

    Unless LOWCHUG_PROFILE is defined, LOWCHUG_PROFILE_SCOPE expands to
    nothing, so the plugin and the tests contain no timing code at all. The
    profiling tool compiles its own copy of the DSP sources with the macro
    defined.

    The timing is exclusive: entering a scope pauses the one around it, so
    each stage's time excludes the stages nested inside it, and the stages
    add up to the total. Single-threaded (plain globals), which is all the
    tool needs.
*/

#if defined(LOWCHUG_PROFILE)

#include <array>
#include <chrono>
#include <cstddef>

namespace lowchug::dsp::profiling
{

enum class Stage
{
    outside,        // not inside any DSP call (the tool's own loop)
    detuner,        // Detuner: dry delays, glide, Tight, mix, gain, mode crossfade
    buffering,      // engine: FIFOs, windowing each frame, handing out finished hops
    onsetDetector,  // engine: the time-domain onset detector
    forwardFft,     // engine: one FFT per channel per frame
    analysis,       // engine: phase-vocoder analysis, peaks and regions, region shifts
    formant,        // engine: the spectral envelope (Formant on only)
    partialModel,   // engine: selecting, solving, refining, subtracting and adding back lobes
    regionShift,    // engine: moving the regions into the output spectrum
    inverseFft,     // engine: one inverse FFT per channel per frame (two while an onset is masked)
    overlapAdd,     // engine: synthesis window and overlap-add
    count
};

inline constexpr const char* stageNames[] = {
    "outside", "Detuner (delays, Tight, mix)", "buffering, windowing", "onset detector", "forward FFT",
    "analysis (peaks, regions)", "formant envelope", "partial model", "region shifting", "inverse FFT", "overlap-add",
};

using Clock = std::chrono::steady_clock;

inline std::array<double, (std::size_t) Stage::count> seconds {};
inline Stage current = Stage::outside;
inline Clock::time_point last = Clock::now();

inline void charge (Clock::time_point now)
{
    seconds[(std::size_t) current] += std::chrono::duration<double> (now - last).count();
    last = now;
}

inline void clear()
{
    seconds.fill (0.0);
    current = Stage::outside;
    last = Clock::now();
}

/** Charges the time so far to the enclosing stage, then this one until it ends. */
struct Scope
{
    explicit Scope (Stage stage) : previous (current)
    {
        charge (Clock::now());
        current = stage;
    }
    ~Scope()
    {
        charge (Clock::now());
        current = previous;
    }
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

    Stage previous;
};

} // namespace lowchug::dsp::profiling

#define LOWCHUG_PROFILE_CONCAT_(a, b) a##b
#define LOWCHUG_PROFILE_CONCAT(a, b) LOWCHUG_PROFILE_CONCAT_ (a, b)
#define LOWCHUG_PROFILE_SCOPE(stage) \
    const ::lowchug::dsp::profiling::Scope LOWCHUG_PROFILE_CONCAT (lowchugProfileScope, __LINE__) { ::lowchug::dsp::profiling::Stage::stage }

#else

#define LOWCHUG_PROFILE_SCOPE(stage) static_cast<void> (0)

#endif
