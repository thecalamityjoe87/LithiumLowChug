#pragma once

#include <algorithm>
#include <vector>

namespace lowchug::dsp
{

/**
    A fixed, whole-sample delay for several channels. Used to delay the dry
    signal by exactly the engine's latency, so dry and wet line up (mixing
    misaligned copies would comb-filter) and bypass doesn't jump in time.

    Copies samples exactly: a delayed signal nulls bit-for-bit against the input.
*/
class DelayLine
{
public:
    /** Allocates. Not realtime-safe. */
    void prepare (int numChannels, int delaySamples)
    {
        delay = std::max (0, delaySamples);
        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) delay + 1, 0.0f));
        writeIndex = 0;
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writeIndex = 0;
    }

    int getDelay() const { return delay; }

    /** Writes `input` and reads the signal from `delay` samples ago into
        `output`. input and output may be the same memory. */
    void process (const float* const* input, float* const* output, int numChannels, int numSamples)
    {
        const int size = delay + 1;
        int index = writeIndex;
        for (int c = 0; c < numChannels && c < (int) buffers.size(); ++c)
        {
            auto& buffer = buffers[(size_t) c];
            index = writeIndex;
            for (int i = 0; i < numSamples; ++i)
            {
                // The buffer holds delay+1 samples, so the slot about to be
                // overwritten holds the sample from exactly `delay` ago.
                buffer[(size_t) index] = input[c][i];
                const int readIndex = index + 1 == size ? 0 : index + 1;
                output[c][i] = buffer[(size_t) readIndex];
                index = readIndex;
            }
        }
        writeIndex = index;
    }

private:
    std::vector<std::vector<float>> buffers;
    int delay = 0, writeIndex = 0;
};

} // namespace lowchug::dsp
