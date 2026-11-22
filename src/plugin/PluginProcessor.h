#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lowchug
{

/**
    The plugin wrapper: owns the parameters and (from milestone 2) the DSP.

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

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // keep the double-precision overload visible

    // Host bypass goes through our own parameter (see Parameters.cpp).
    juce::AudioProcessorParameter* getBypassParameter() const override;

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

private:
    static BusesProperties makeBuses();

    juce::AudioProcessorValueTreeState state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowChugProcessor)
};

} // namespace lowchug
