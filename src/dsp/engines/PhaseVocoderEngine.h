#pragma once

#include "../PitchShiftEngine.h"

#include <complex>
#include <memory>
#include <string>
#include <vector>

namespace lowchug::dsp
{

/**
    A streaming phase-vocoder pitch shifter, built as the comparison point for
    Signalsmith Stretch.

    Algorithm (Laroche & Dolson, "New phase-vocoder techniques for
    pitch-shifting", 1999):
      1. STFT with a Hann window, zero-phase (frame rotated by N/2) so all
         bins of a sinusoid share roughly one phase.
      2. Find spectral peaks. Each peak owns a "region" of bins out to the
         spectral minimum between it and its neighbours.
      3. Move every region, as a block, from its peak's bin to the shifted
         peak's bin, and rotate the whole region by one phase (identity phase
         locking). This keeps each partial's shape and vertical phase
         coherence, which is what stops chords "phasing".
      4. The rotation accumulates frame to frame so each shifted partial's
         phase advances at the new frequency.
      5. On a detected transient, the accumulated rotations of the regions the
         onset brought in are reset to zero in the frame where the onset sits
         at the window peak, so that frame's original (vertically coherent)
         phases go out unchanged: the pick attack keeps its shape.

    All channels share one analysis (of their sum), so stereo images stay put.
*/
class PhaseVocoderEngine final : public PitchShiftEngine
{
public:
    struct Config
    {
        int fftSize = 4096;            // analysis window, power of two (specified at 48 kHz)
        int overlap = 4;               // frames per synthesis window (>= 4)
        bool transientReset = true;
        float transientThresholdDb = 6.0f; // high-band energy jump that counts as an onset
        // 0 = symmetric Hann windows (latency = fftSize). A smaller power of
        // two selects an asymmetric low-delay window pair: frequency
        // resolution close to fftSize, latency = synthesisSize.
        int synthesisSize = 0;
    };

    explicit PhaseVocoderEngine (Config config);
    ~PhaseVocoderEngine() override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setPitchRatio (float newRatio) override { pendingRatio = newRatio; }
    void setFormantPreservation (bool shouldPreserve) override { preserveFormants = shouldPreserve; }
    bool supportsFormantPreservation() const override { return true; }
    // A sample can only leave once every frame overlapping it has been
    // synthesised, i.e. one synthesis window after it arrived.
    int getLatencySamples() const override { return synthesisSize; }
    void process (const float* const* input, float* const* output,
                  int numChannels, int numSamples) override;
    std::string_view getName() const override { return name; }

private:
    using Complex = std::complex<float>;

    void makeWindows();
    void processFrame();
    void analyseSharedSpectrum();
    bool detectTransient();
    void findPeaksAndRegions();
    void computeRegionShifts (bool resetPhases);
    void synthesiseChannel (int channel);

    Config config;
    std::string name;

    int fftSize = 0, synthesisSize = 0, hop = 0, numBins = 0, numChannels = 0;
    int windowPeak = 0;      // analysis window maximum; FFT phases are relative to it
    float outputScale = 1.0f;
    int rover = 0;
    float sampleRate = 48000.0f;
    float ratio = 1.0f, pendingRatio = 1.0f;
    bool preserveFormants = false;
    int transientHoldoff = 0;
    int framesUntilReset = -1; // -1 = no onset pending

    int highBandStartBin = 0;
    float previousHighBandEnergy = 0.0f;

    std::vector<float> analysisWindow, synthesisWindow;

    struct ChannelState
    {
        std::vector<float> inFifo, outFifo, outAccumulator;
        std::vector<Complex> spectrum, shifted;
    };
    std::vector<ChannelState> channels;

    // Analysis shared by all channels.
    std::vector<Complex> sumSpectrum;
    std::vector<float> magnitude, previousPhase, trueBin;
    std::vector<float> previousMagnitude, preOnsetMagnitude;
    std::vector<float> rotation, previousRotation; // per input bin
    std::vector<float> logEnvelope;                // for formant preservation

    struct Region
    {
        int peak, start, end; // [start, end)
        int shift;      // whole bins
        float fraction; // plus this fraction of a bin, in [0, 1)
        float rotation;
        float gain;
    };
    std::vector<Region> regions;

    std::vector<float> timeBuffer;

    struct Fft;
    std::unique_ptr<Fft> fft;
};

} // namespace lowchug::dsp
