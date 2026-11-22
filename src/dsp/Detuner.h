#pragma once

#include "DelayLine.h"
#include "LinearSmoother.h"
#include "PitchShiftEngine.h"

#include <array>
#include <memory>
#include <vector>

namespace lowchug::dsp
{

/**
    The complete LowChug effect, independent of any plugin framework:

        input --+--> Live engine   --> [unity blend] --> mix --> gain --> [bypass] --+
                +--> Live dry delay -------^-------------^--------------------^------|
                |                                                                    +--> mode crossfade --> out
                +--> Studio engine  --> [unity blend] --> mix --> gain --> [bypass] --+
                +--> Studio dry delay -----^-------------^--------------------^------+

    - Two complete paths, one per mode (Live, Studio), each with its own engine
      and a dry delay matched to that engine's latency. Both are prepared up
      front, so switching modes never allocates.
    - Only the audible path's engine runs. On a mode change the other engine
      is reset and fed for as long as it remembers (getMemorySamples), so its
      output is fully formed; then the output crossfades to it and the old
      engine goes idle. Switching back during the warm-up cancels it;
      switching back during the fade lets it finish, then fades straight
      back (the old engine is still warm).
    - Every control is smoothed. Shift changes glide over 40 ms.
    - At exactly 0 semitones the output crossfades to the delayed dry signal,
      so "no shift" is bit-transparent (it nulls against the delayed input).
    - Dry is delayed by the engine latency, so mix and bypass never
      comb-filter or jump in time.
    - Identical stereo channels (a mono source on a stereo track) are shifted
      once and copied, halving the cost.

    Threading: prepare() allocates (message thread). reset(), the setters
    and process() are realtime-safe (audio thread).
*/
class Detuner
{
public:
    enum class Mode { live, studio };

    /** Uses LowChug's Live and Studio engines (EngineConfigs.h) unless given others. */
    explicit Detuner (std::unique_ptr<PitchShiftEngine> liveEngine = nullptr,
                      std::unique_ptr<PitchShiftEngine> studioEngine = nullptr);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Clears all audio state and jumps straight to the target settings,
        including the target mode (no crossfade). */
    void reset();

    /** The latency of the target mode: what the host should compensate for.
        Identical in every state of that mode (shifted, mixed, bypassed).
        During a mode switch the audible latency changes over the crossfade. */
    int getLatencySamples() const { return getLatencySamples (targetMode); }
    int getLatencySamples (Mode m) const { return path (m).engine->getLatencySamples(); }

    // Targets; all smoothed. ------------------------------------------------
    void setShiftSemitones (float semitones); // shift + fine, e.g. -2.15
    void setMix (float wetAmount);            // 0 = dry, 1 = fully shifted
    void setOutputGain (float linearGain);
    void setBypassed (bool shouldBypass);
    void setFormantPreservation (bool shouldPreserve);
    void setTight (float amount);             // 0 = off, 1 = full (see TightFilter)
    void setMode (Mode newMode) { targetMode = newMode; }

    Mode getMode() const { return targetMode; }

    /** True while a mode change is warming up or crossfading. */
    bool isSwitchingModes() const;

    /** Lets tests compare the identical-channel shortcut against full stereo. */
    void setChannelLinkingEnabled (bool enabled) { linkingEnabled = enabled; }
    bool isChannelLinkingActive() const { return linked; }

    /** Processes in place. Any numSamples is fine (split internally);
        numChannels must not exceed the prepared count. */
    void process (float* const* channels, int numChannels, int numSamples);

    PitchShiftEngine& getEngine (Mode m) { return *path (m).engine; }

private:
    /** Tight: a gentle low cut on the shifted signal that follows the shift.
        Big downward shifts move the guitar's body down into the subs and pile
        up the low mids (mud). Tight answers with a 12 dB/octave high-pass at
        up to 0.9 x the frequency the low E string lands on, plus a low shelf
        of up to -6 dB at 2.5 x that note. Both scale with the amount, from
        exactly nothing at 0 (the filters run anyway, so turning Tight up
        never clicks). Double precision: filters this low need it. */
    struct TightFilter
    {
        // Two state-variable filters in the "topology-preserving" form
        // (Zavalishin; Simper's SVF), which stays clean while its settings
        // move (a plain biquad would tick when Tight or the shift changes).
        struct Coefficients { double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0; };
        struct State { double ic1 = 0, ic2 = 0; };
        Coefficients highPass, shelf;

        void update (double sampleRate, float semitones, float amount);
        static float run (const Coefficients&, State&, float x);
    };

    /** Everything one mode needs: its engine, its dry delay, and buffers. */
    struct Path
    {
        std::unique_ptr<PitchShiftEngine> engine;
        DelayLine dryDelay;
        std::vector<std::vector<float>> dryBuffers, wetBuffers;
        // Pointer arrays are preallocated too: building them per block must not allocate.
        std::vector<float*> dryPointers, wetPointers, wetSlicePointers;
        bool running = false; // its engine is being fed this chunk
        std::vector<std::array<TightFilter::State, 2>> tightState; // per channel: high-pass, shelf
    };

    static int indexOf (Mode m) { return m == Mode::studio ? 1 : 0; }
    Path& path (Mode m) { return paths[(size_t) indexOf (m)]; }
    const Path& path (Mode m) const { return paths[(size_t) indexOf (m)]; }

    void processChunk (float* const* channels, int numChannels, int numSamples);
    void updateModeSwitch (int numSamples);
    void updateChannelLinking (const float* const* channels, int numChannels, int numSamples);

    std::array<Path, 2> paths; // [0] Live, [1] Studio

    int maxChunk = 0, preparedChannels = 0;
    std::vector<float*> offsetPointers;
    std::vector<const float*> inputPointers;

    LinearSmoother glide;  // semitones
    LinearSmoother unity;  // 1 = at exactly 0 st, output the delayed dry signal
    LinearSmoother mix, outputGain, bypass, tight;
    TightFilter tightFilter, previousTight; // this slice's settings and the last one's
    static TightFilter::Coefficients lerp (const TightFilter::Coefficients&, const TightFilter::Coefficients&, double t);
    float previousTightBlend = 0.0f; // the filtered share at the end of the last slice
    double sampleRate = 48000.0;

    // Mode switching. modeFade is how much of the Studio path is heard
    // (0 = all Live, 1 = all Studio).
    Mode targetMode = Mode::live;
    LinearSmoother modeFade;
    int warmupLeft = -1; // samples until the incoming engine is ready; -1 = not warming

    bool linkingEnabled = true, linked = false;
    long identicalRun = 0; // samples for which all channels have been identical
};

} // namespace lowchug::dsp
