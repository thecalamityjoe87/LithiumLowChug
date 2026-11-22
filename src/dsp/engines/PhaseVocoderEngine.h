#pragma once

#include "../PitchShiftEngine.h"

#include <complex>
#include <memory>
#include <string>
#include <vector>

namespace lowchug::dsp
{

/**
    A streaming phase-vocoder pitch shifter: LowChug's engine (chosen over
    Signalsmith Stretch in milestone 1; see docs/engine-evaluation.md).

    Algorithm (Laroche & Dolson, "New phase-vocoder techniques for
    pitch-shifting", 1999):
      1. STFT with a Hann window, zero-phase (frame rotated by N/2) so all
         bins of a sinusoid share roughly one phase.
      2. Find spectral peaks. Each peak owns a "region" of bins out to the
         spectral minimum between it and its neighbors.
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
      6. The partial model (milestone 3): the strongest low partials are
         measured and moved as exact window lobes (with the mirror of their
         negative frequency), and only what they don't explain goes through
         step 3. That keeps the low end clean and removes the treble residue
         the low-delay window's wide skirt used to cause.
      7. The onset mask (milestone 3): onsets are also found in the time
         domain, and while one passes through the window, what it brought in
         is kept from sounding before it was played (no pre-echo).
      8. Split computation (optional, Studio): each frame's work is spread
         over the following hop, for one hop of extra latency.

    All channels share one analysis (of their average), so stereo images stay
    put.

    Threading: as PitchShiftEngine. prepare() allocates; everything else,
    including the per-frame work, is allocation-free.
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
        // Spread each frame's work (FFTs, analysis, synthesis) over the hop
        // that follows it instead of doing it all in the block where the
        // frame completes. Costs one hop of extra latency; in exchange no
        // single audio block pays for a whole frame, which matters for big
        // FFTs and small host buffers.
        bool splitComputation = false;
        // Move the strongest low partials as exact window lobes (see "The
        // partial model" in the .cpp): clean low end, no treble residue.
        bool partialModel = true;
        // While an onset passes through the window, keep what it brought in
        // from sounding before it was played (see synthesizeChannel).
        bool onsetMask = true;
    };

    explicit PhaseVocoderEngine (Config config);
    ~PhaseVocoderEngine() override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setPitchRatio (float newRatio) override { pendingRatio = newRatio; }
    void setFormantPreservation (bool shouldPreserve) override { preserveFormants = shouldPreserve; }
    bool supportsFormantPreservation() const override { return true; }
    // A sample can only leave once every frame overlapping it has been
    // synthesized, i.e. one synthesis window after it arrived (plus the hop a
    // split frame takes to compute).
    int getLatencySamples() const override { return synthesisSize + splitDelay(); }
    int getMemorySamples() const override { return fftSize + synthesisSize + splitDelay(); }
    bool copyChannelState (int from, int to) override;
    void process (const float* const* input, float* const* output,
                  int numChannels, int numSamples) override;
    std::string_view getName() const override { return name; }

private:
    using Complex = std::complex<float>;

    int splitDelay() const { return config.splitComputation ? hop : 0; }

    void makeWindows();

    // A frame's life: startFrame() captures the windowed input at a hop
    // boundary; its work is a list of steps (one FFT per channel, the shared
    // analysis, one synthesis per channel) that runSteps() works through,
    // all at once or spread across the next hop; emitHop() then hands the
    // next hop of finished output to the output FIFO.
    void startFrame();
    void runSteps (int upTo);
    void runStep (int step);
    void finishPendingFrame();
    void emitHop();
    int totalSteps() const { return 2 * frameChannels + 1; }

    void analyzeSharedSpectrum();
    void analyzeFrame();
    bool detectTransient();
    void findPeaksAndRegions();
    void measurePeakFrequencies();
    void computeEnvelope();
    void computeRegionShifts (bool resetPhases, bool markOnsets);
    void detectOnsets (const float* const* input, int numChannels, int numSamples);
    void synthesizeChannel (int channel);
    void makeWindowSpectrum();
    Complex windowSpectrumAt (float bins) const;
    int lobeReach (float level) const;
    void selectModeledPartials();
    void computeCoupling (bool withNeighbors);
    void solveAmplitudes (const std::vector<Complex>& spectrum, bool wantCleaned);
    void refinePartialFrequencies();
    void subtractPartials (const std::vector<Complex>& spectrum);
    void addPartials (std::vector<Complex>& shifted, std::vector<Complex>* onsetShifted) const;
    void addLobe (std::vector<Complex>& target, Complex amplitude, float center, int reach, float sign) const;

    Config config;
    std::string name;

    int fftSize = 0, synthesisSize = 0, hop = 0, numBins = 0, numChannels = 0;
    int activeChannels = 0;  // channels fed by the latest process() call
    int windowPeak = 0;      // analysis window maximum; FFT phases are relative to it
    float outputScale = 1.0f;
    int rover = 0;
    float sampleRate = 48000.0f;
    float ratio = 1.0f, pendingRatio = 1.0f;
    bool preserveFormants = false;
    int transientHoldoff = 0;
    int framesUntilReset = -1; // -1 = no onset pending

    // Onsets found in the time domain (detectOnsets), for the onset mask:
    // samples consumed so far, the stream position of the newest frame's
    // last sample, and of the latest onset.
    long long streamPosition = 0, frameEndPosition = 0, lastOnset = -1, maskedOnset = -1;
    bool masking = false;
    float onsetTime = 0.0f; // the masked onset, relative to the window peak (samples)
    std::vector<float> maskBaseMagnitude; // the spectrum just before the masked onset

    struct OnsetDetector
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // high-pass biquad at 2 kHz
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        int blockSize = 32, filled = 0;
        float highEnergy = 0, allEnergy = 0;
        int recentIndex = 0, holdoff = 0;
    } detector;
    std::vector<float> onsetHistory; // the detector's last 25 ms of block energies

    // The frame being worked on (see startFrame).
    bool framePending = false;
    int frameChannels = 0, stepsDone = 0;

    int highBandStartBin = 0;
    float previousHighBandEnergy = 0.0f;

    std::vector<float> analysisWindow, synthesisWindow;

    struct ChannelState
    {
        std::vector<float> inFifo, outFifo, outAccumulator;
        std::vector<float> frame; // the windowed input of the pending frame
        std::vector<Complex> spectrum, shifted, onsetShifted;
    };
    std::vector<ChannelState> channels;

    // Analysis shared by all channels.
    std::vector<Complex> sumSpectrum, previousSumSpectrum; // this frame's and the last
    std::vector<float> magnitude;
    std::vector<float> trueBin; // measured frequency, in bins; valid at this frame's peak bins only
    std::vector<float> previousMagnitude, preOnsetMagnitude;
    std::vector<float> rotation, previousRotation; // per input bin
    std::vector<float> logEnvelope, envelopeScratch; // for formant preservation

    struct Region
    {
        int peak, start, end; // [start, end)
        int shift;      // whole bins
        float fraction; // plus this fraction of a bin, in [0, 1)
        float rotation;
        float gain;
        Complex twist;  // gain and rotation as one complex factor
        bool onset;     // brought in by an onset still passing through the window
    };
    std::vector<Region> regions;
    std::vector<int> regionOfBin; // which region each input bin belongs to

    // The analysis window's spectrum W(d), d = 0 .. mirrorBins bins, sampled
    // every 1/windowSpectrumOversampling bin (W(-d) = conj W(d)).
    static constexpr int windowSpectrumOversampling = 32;
    static constexpr int mirrorBins = 128;
    std::vector<Complex> windowSpectrum;
    std::vector<float> windowSkirt; // windowSkirt[d] = max |W| at d bins or further out

    // The partial model. Shared by all channels: which regions are modeled
    // (strongest first, at most maxModeled), how far each lobe reaches, and
    // the lobes' overlaps at the peak bins. Per channel (scratch, reused):
    // the amplitudes, and the residual spectrum once the lobes are removed.
    static constexpr int maxModeled = 48;
    std::vector<int> modeled, modelReach;
    std::vector<Complex> couplingPositive, couplingMirror;
    std::vector<Complex> modelAmplitude, residual;
    std::vector<Complex> cleaned;                            // per partial: bins peak - 1 .. peak + 1, others removed
    std::vector<Complex> cleanedAtBin, previousCleanedAtBin; // the same by bin, this frame and the last
    float lobeFloor = 0.0f; // lobes are drawn out to where they fall below this

    std::vector<float> timeBuffer, onsetTimeBuffer;

    struct Fft;
    std::unique_ptr<Fft> fft;
};

} // namespace lowchug::dsp
