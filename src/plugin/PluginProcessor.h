#pragma once

#include "dsp/Detuner.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lowchug
{

/**
    The plugin wrapper: owns the parameters and feeds them to the DSP
    (lowchug::dsp::Detuner, which knows nothing about JUCE).

    JUCE threading in a nutshell:
      - processBlock() runs on the host's realtime audio thread. It must never
        allocate, lock, log or touch files, or the audio can glitch.
      - Everything else (constructor, prepareToPlay, state save/restore, the
        editor) runs on the message thread or a host worker thread.
      - Parameters are the bridge: the APVTS stores each value in a
        std::atomic<float>, so the audio thread can read it lock-free while
        the UI or host automation writes it.
*/
class LowChugProcessor final : public juce::AudioProcessor
{
public:
    LowChugProcessor();
    ~LowChugProcessor() override = default;

    // Lifecycle -----------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void reset() override; // hosts call this on transport jumps etc.

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // keep the double-precision overload visible

    // Host bypass goes through our own parameter (see Parameters.cpp). If a
    // host bypasses us from outside anyway, it calls processBlockBypassed,
    // which must still delay the audio by our latency or the track would
    // jump forward in time.
    juce::AudioProcessorParameter* getBypassParameter() const override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

    // Editor ----------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Identity ----------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Programs: factory presets arrive in milestone 4 -----------------------
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // State -----------------------------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }

    /** The semitone shift the parameters currently ask for (shift + fine,
        resolved through "Playing in" / "Hear"). */
    float getEffectiveShift() const;

private:
    static BusesProperties makeBuses();
    void pushParametersToDsp (bool forceBypass);
    void process (juce::AudioBuffer<float>& buffer, bool forceBypass);

    juce::AudioProcessorValueTreeState state;
    dsp::Detuner detuner;

    // Pointers into the APVTS's atomics, looked up once here rather than by
    // string on every block (a string lookup is a map search). Reading them
    // with load() is lock-free, so it's safe on the audio thread.
    std::atomic<float>* shiftValue = nullptr;
    std::atomic<float>* fineValue = nullptr;
    std::atomic<float>* sourceValue = nullptr;
    std::atomic<float>* targetValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* outputValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowChugProcessor)
};

} // namespace lowchug
