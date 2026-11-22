#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace lowchug
{

/**
    Hands copies of the audio (what went in, what came out) from the audio
    thread to the editor's visualizer, without ever blocking the audio thread.

    How: a juce::AbstractFifo, JUCE's lock-free single-producer /
    single-consumer ring buffer. AbstractFifo only does the index
    bookkeeping (with atomics); the samples live in two plain arrays we
    allocate up front. The audio thread is the only writer, the editor's
    timer (message thread) the only reader, so no locks are needed and
    neither side can ever wait for the other.

    Input and output are written at the SAME positions, so sample i of `in`
    and sample i of `out` happened at the same moment. (The output is late by
    the plugin's latency; the analyzer allows for that.)

    The tap only runs while an editor is open (setEnabled), so a closed
    plugin pays nothing. If the editor stops reading (window hidden, UI
    busy), the FIFO fills up and new audio is simply dropped: the audio
    thread never waits.
*/
class AnalysisTap
{
public:
    // About 1.4 s at 48 kHz: plenty of slack for a 30 Hz UI timer.
    static constexpr int capacity = 1 << 16;

    AnalysisTap() : in ((size_t) capacity), out ((size_t) capacity) {}

    /** Message thread: turn the tap on (editor opened) or off (closed). */
    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled, std::memory_order_release); }
    bool isEnabled() const noexcept                  { return enabled.load (std::memory_order_acquire); }

    /** One block's worth of writing, used on the audio thread like this:

            auto block = tap.beginBlock (numSamples);
            block.addInput (channels, numChannels);    // before processing in place
            ...process...
            block.addOutput (channels, numChannels);   // after
            // published to the reader when `block` goes out of scope

        Does nothing at all while the tap is disabled. */
    class Block
    {
    public:
        void addInput (const float* const* channels, int numChannels) noexcept  { write (owner != nullptr ? &owner->in : nullptr, channels, numChannels); }
        void addOutput (const float* const* channels, int numChannels) noexcept { write (owner != nullptr ? &owner->out : nullptr, channels, numChannels); }

    private:
        friend class AnalysisTap;
        Block() = default;
        Block (AnalysisTap& t, int numSamples) noexcept : owner (&t), scope (t.fifo.write (numSamples)) {}

        void write (std::vector<float>* dest, const float* const* channels, int numChannels) noexcept
        {
            if (dest == nullptr || numChannels <= 0)
                return;

            // Mono sum (the average of the channels), into the one or two
            // regions the FIFO handed us (two when the ring wraps around).
            const auto gain = 1.0f / (float) numChannels;
            int src = 0;
            scope.forEach ([&] (int index)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sum += channels[ch][src];
                (*dest)[(size_t) index] = sum * gain;
                ++src;
            });
        }

        AnalysisTap* owner = nullptr;
        juce::AbstractFifo::ScopedWrite scope; // commits the write when destroyed
    };

    /** Audio thread. */
    Block beginBlock (int numSamples) noexcept
    {
        if (! enabled.load (std::memory_order_acquire))
            return {};
        return { *this, numSamples };
    }

    /** Message thread: copies up to `maxSamples` of the oldest unread audio
        into `inDest` / `outDest` and returns how many it copied. */
    int read (float* inDest, float* outDest, int maxSamples) noexcept
    {
        const auto scope = fifo.read (std::min (maxSamples, fifo.getNumReady()));
        int i = 0;
        scope.forEach ([&] (int index)
        {
            inDest[i] = in[(size_t) index];
            outDest[i] = out[(size_t) index];
            ++i;
        });
        return i;
    }

    /** Message thread: drops everything unread (e.g. stale audio from before
        the editor opened). */
    void discardPending() noexcept
    {
        const auto scope = fifo.read (fifo.getNumReady());
    }

private:
    juce::AbstractFifo fifo { capacity };
    std::vector<float> in, out;
    std::atomic<bool> enabled { false };
};

} // namespace lowchug
