#pragma once

#include <string_view>

namespace lowchug::dsp
{

/**
    A polyphonic pitch shifter: shifts the WHOLE signal by a frequency ratio.

    This is the seam that lets the shifting algorithm be swapped (Signalsmith
    Stretch, our own phase vocoder, ...) without touching the plugin.

    Threading contract (the usual audio-plugin split):
      - prepare() may allocate, so call it from a non-realtime thread
        (prepareToPlay in JUCE), never while process() can run.
      - reset(), the setters and process() are realtime-safe: no allocation,
        no locks, no I/O. Call them from the audio thread.

    Note on "per-string" retuning (e.g. standard -> Drop D by lowering only the
    low E): that is NOT a pitch shift of the whole signal. It would need a
    polyphonic source-separation stage in front of one engine per string, and
    would live in a different class altogether. See README "Out of scope".
*/
class PitchShiftEngine
{
public:
    virtual ~PitchShiftEngine() = default;

    /** Allocates everything process() will need for blocks of up to
        maxBlockSize samples. Larger blocks must be split by the caller. */
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;

    /** Clears all internal audio state (e.g. on transport jumps). */
    virtual void reset() = 0;

    /** Frequency ratio: 0.5 = one octave down, 2^(-2/12) = two semitones down.
        Engines pick the new value up at their next analysis frame, so callers
        that want a smooth glide should ramp this in small steps. */
    virtual void setPitchRatio (float ratio) = 0;

    /** Keep the spectral envelope (body resonances) in place while the pitch
        moves, so big downward shifts don't sound small and boxy. */
    virtual void setFormantPreservation (bool shouldPreserve) { (void) shouldPreserve; }
    virtual bool supportsFormantPreservation() const { return false; }

    /** Delay in samples between input and output. Valid after prepare(). */
    virtual int getLatencySamples() const = 0;

    /** Processes numSamples (<= maxBlockSize) of each channel.
        input and output must NOT point at the same memory: engines read
        input history while writing output. */
    virtual void process (const float* const* input, float* const* output,
                          int numChannels, int numSamples) = 0;

    virtual std::string_view getName() const = 0;
};

} // namespace lowchug::dsp
