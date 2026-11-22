#pragma once

#include "engines/PhaseVocoderEngine.h"

namespace lowchug::dsp::configs
{

// The engine settings LowChug ships with, chosen in the milestone 1
// evaluation (docs/engine-evaluation.md). Sizes are at 48 kHz; the engine
// scales them for higher sample rates.

/** Live: low-delay windows, 1024 samples (21.3 ms at 48 kHz) of latency. */
inline PhaseVocoderEngine::Config live()
{
    PhaseVocoderEngine::Config c;
    c.fftSize = 4096;
    c.synthesisSize = 1024;
    c.overlap = 4;
    return c;
}

/** Studio: longer windows for the cleanest low end. 2048 + 256 samples
    (48.0 ms at 48 kHz) of latency: the extra hop (256) lets each 8192-point
    frame's work be spread over the blocks that follow it, so no single
    audio block has to pay for a whole frame. */
inline PhaseVocoderEngine::Config studio()
{
    PhaseVocoderEngine::Config c;
    c.fftSize = 8192;
    c.synthesisSize = 2048;
    c.overlap = 8;
    c.splitComputation = true;
    return c;
}

} // namespace lowchug::dsp::configs
