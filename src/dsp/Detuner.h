#pragma once

#include "DelayLine.h"
#include "LinearSmoother.h"
#include "PitchShiftEngine.h"

#include <memory>
#include <vector>

namespace lowchug::dsp
{

/**
    The complete LowChug effect, independent of any plugin framework:

        input --+--> pitch-shift engine --> [unity blend] --> mix --> output gain --> [bypass] --> out
                |                                ^              ^                        ^
                +--> dry delay (= engine latency) +--------------+------------------------+

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
    /** Uses LowChug's Live engine when no engine is given. */
    explicit Detuner (std::unique_ptr<PitchShiftEngine> engine = nullptr);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Total delay of the effect, identical in every state (incl. bypass). */
    int getLatencySamples() const { return engine->getLatencySamples(); }

    // Targets; all smoothed. ------------------------------------------------
    void setShiftSemitones (float semitones); // shift + fine, e.g. -2.15
    void setMix (float wetAmount);            // 0 = dry, 1 = fully shifted
    void setOutputGain (float linearGain);
    void setBypassed (bool shouldBypass);
    void setFormantPreservation (bool shouldPreserve) { engine->setFormantPreservation (shouldPreserve); }

    /** Lets tests compare the identical-channel shortcut against full stereo. */
    void setChannelLinkingEnabled (bool enabled) { linkingEnabled = enabled; }
    bool isChannelLinkingActive() const { return linked; }

    /** Processes in place. Any numSamples is fine (split internally);
        numChannels must not exceed the prepared count. */
    void process (float* const* channels, int numChannels, int numSamples);

    PitchShiftEngine& getEngine() { return *engine; }

private:
    void processChunk (float* const* channels, int numChannels, int numSamples);
    void updateChannelLinking (const float* const* channels, int numChannels, int numSamples);

    std::unique_ptr<PitchShiftEngine> engine;
    DelayLine dryDelay;

    int maxChunk = 0, preparedChannels = 0;
    std::vector<std::vector<float>> dryBuffers, wetBuffers;
    // Pointer arrays are preallocated too: building them per block must not allocate.
    std::vector<float*> dryPointers, wetPointers, wetSlicePointers, offsetPointers;
    std::vector<const float*> inputPointers;

    LinearSmoother glide;  // semitones
    LinearSmoother unity;  // 1 = at exactly 0 st, output the delayed dry signal
    LinearSmoother mix, outputGain, bypass;

    bool linkingEnabled = true, linked = false;
    long identicalRun = 0; // samples for which all channels have been identical
};

} // namespace lowchug::dsp
