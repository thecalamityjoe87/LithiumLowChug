#include "PluginProcessor.h"

#include "Parameters.h"
#include "Presets.h"
#include "StandaloneAudio.h"
#include "Tunings.h"
#include "ui/PluginEditor.h"

namespace lowchug
{

// BusesProperties is a protected type of AudioProcessor, so this helper has
// to be a (static) member to be allowed to use it.
juce::AudioProcessor::BusesProperties LowChugProcessor::makeBuses()
{
    // In the standalone app a guitar arrives on ONE input of the audio
    // interface. With a stereo input the other (empty) input would make the
    // shifted guitar play in one ear only, so the standalone takes a mono
    // input (choose which interface input in Audio/MIDI Settings) and sends
    // the result to both outputs. JUCE records which wrapper is creating us
    // before the constructor runs, so this check is valid here.
    //
    // In a DAW, default to stereo; isBusesLayoutSupported() still accepts
    // mono tracks (mono in, stereo out).
    const bool isStandalone = juce::PluginHostType::getPluginLoadedAs() == wrapperType_Standalone;

    return BusesProperties()
        .withInput ("Input", isStandalone ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

LowChugProcessor::LowChugProcessor()
    : AudioProcessor (makeBuses()),
      // "PARAMETERS" is the root tag of the saved state. nullptr = no undo manager.
      state (*this, nullptr, "PARAMETERS", params::createLayout()),
      currentProgram (presets::defaultPreset) // the parameter defaults are D Standard, Live
{
    shiftValue = state.getRawParameterValue (params::shift);
    fineValue = state.getRawParameterValue (params::fine);
    sourceValue = state.getRawParameterValue (params::sourceTuning);
    targetValue = state.getRawParameterValue (params::targetTuning);
    mixValue = state.getRawParameterValue (params::mix);
    outputValue = state.getRawParameterValue (params::output);
    bypassValue = state.getRawParameterValue (params::bypass);
    modeValue = state.getRawParameterValue (params::mode);
    formantValue = state.getRawParameterValue (params::formant);
    tightValue = state.getRawParameterValue (params::tight);

    // Latency follows the Mode parameter (Live 21 ms, Studio 48 ms). The
    // listener catches changes made on the message thread (the editor, a
    // preset, a restored session) at once; the timer catches the ones that
    // arrive on other threads, such as host automation.
    state.addParameterListener (params::mode, this);
    startTimerHz (10);

    // Standalone app only: audio device fixes and an unmuted guitar input.
    setUpStandaloneAudio();
}

LowChugProcessor::~LowChugProcessor()
{
    state.removeParameterListener (params::mode, this);
}

bool LowChugProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    // mono -> mono, stereo -> stereo, and mono -> stereo (a mono DI feeding a
    // stereo track is the most common guitar setup).
    return (in == mono && (out == mono || out == stereo)) || (in == stereo && out == stereo);
}

void LowChugProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Called on the message thread before playback starts, and again whenever
    // the sample rate, block size or channel layout changes. All allocation
    // happens here, never in processBlock.
    // Both modes' engines are prepared here, so switching modes later never
    // allocates on the audio thread.
    detuner.prepare (sampleRate, maximumExpectedSamplesPerBlock, std::max (1, getTotalNumInputChannels()));

    // Start from the current settings (including the mode) rather than
    // gliding or crossfading in from defaults.
    pushParametersToDsp (false);
    detuner.reset();

    // Hosts delay every other track by this much so ours stays in time. The
    // dry path is delayed by the same amount, so it is constant in every
    // state: shifted, unshifted, any mix, bypassed.
    setLatencySamples (detuner.getLatencySamples());
}

params::Mode LowChugProcessor::getModeParameter() const
{
    return modeValue->load() >= 0.5f ? params::Mode::studio : params::Mode::live;
}

int LowChugProcessor::getLatencySamplesFor (params::Mode mode) const
{
    // The engines know their latency once prepared (it depends on the rate).
    if (getSampleRate() <= 0.0)
        return 0;
    return detuner.getLatencySamples (mode == params::Mode::studio ? dsp::Detuner::Mode::studio : dsp::Detuner::Mode::live);
}

void LowChugProcessor::updateReportedLatency()
{
    // setLatencySamples() notifies the host through JUCE's format wrapper,
    // and that path takes a lock and may call into the host, so it belongs
    // on the message thread, never the audio thread:
    //   - VST3: the wrapper calls IComponentHandler::restartComponent
    //     (kLatencyChanged); the host then asks getLatencySamples() again,
    //     often deactivating and re-preparing the plugin to do it.
    //   - LV2: the wrapper writes the latency to the plugin's "latency"
    //     output port on every run(); hosts read that port, so the change
    //     needs no message at all.
    //   - AU (macOS): the wrapper fires a kAudioUnitProperty_Latency
    //     property change.
    //   - Standalone: no host compensation, nothing to tell.
    // setLatencySamples does nothing when the value is unchanged, so calling
    // this often is cheap.
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    if (const int latency = getLatencySamplesFor (getModeParameter()); latency > 0)
        setLatencySamples (latency);
}

void LowChugProcessor::parameterChanged (const juce::String&, float)
{
    // Called on whichever thread changed the parameter. Only act on the
    // message thread; from the audio thread (automation), the timer picks
    // the change up within 100 ms instead.
    if (juce::MessageManager::existsAndIsCurrentThread())
        updateReportedLatency();
}

void LowChugProcessor::reset()
{
    detuner.reset();
}

int LowChugProcessor::getShiftSemitones() const
{
    const auto source = static_cast<tunings::Source> (juce::roundToInt (sourceValue->load()));
    const int target = juce::roundToInt (targetValue->load());

    // A named target ("Hear: Drop C") decides the shift; "Custom" (on either
    // side) hands control to the manual shift parameter.
    return tunings::shiftFor (source, target).value_or (juce::roundToInt (shiftValue->load()));
}

float LowChugProcessor::getEffectiveShift() const
{
    return (float) getShiftSemitones() + fineValue->load() / 100.0f;
}

void LowChugProcessor::pushParametersToDsp (bool forceBypass)
{
    // The detuner smooths every one of these, so it's fine to set them on
    // every block even while automation is moving them.
    detuner.setShiftSemitones (getEffectiveShift());
    detuner.setMix (mixValue->load() / 100.0f);
    detuner.setOutputGain (juce::Decibels::decibelsToGain (outputValue->load()));
    detuner.setBypassed (forceBypass || bypassValue->load() >= 0.5f);
    detuner.setFormantPreservation (formantValue->load() >= 0.5f);
    detuner.setTight (tightValue->load() / 100.0f);

    // A mode change crossfades inside the detuner (no allocation: both modes
    // were prepared in prepareToPlay). The host hears about the new latency
    // separately, from the message thread (updateReportedLatency).
    detuner.setMode (getModeParameter() == params::Mode::studio ? dsp::Detuner::Mode::studio : dsp::Detuner::Mode::live);
}

void LowChugProcessor::process (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    // Denormals (tiny floats near zero) are very slow on x86; this turns them
    // into zeros for the duration of the block.
    juce::ScopedNoDenormals noDenormals;

    pushParametersToDsp (forceBypass);

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    const int numProcessed = std::min (numIn, buffer.getNumChannels());

    // While the editor is open, copy what comes in and what goes out for its
    // visualizer. Lock-free and allocation-free (see AnalysisTap); a no-op
    // when no editor is open.
    auto tapBlock = analysisTap.beginBlock (numSamples);
    tapBlock.addInput (buffer.getArrayOfReadPointers(), numProcessed);

    // Process the input channels in place. (Identical stereo channels are
    // detected inside the detuner and shifted only once.)
    detuner.process (buffer.getArrayOfWritePointers(), numProcessed, numSamples);

    tapBlock.addOutput (buffer.getArrayOfReadPointers(), numProcessed);

    // Mono in, stereo out: copy the result to the second channel.
    int filledChannels = numIn;
    if (numIn == 1 && numOut > 1)
    {
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        filledChannels = 2;
    }

    // Clear any outputs that have no matching input (they may hold garbage).
    for (int ch = filledChannels; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

void LowChugProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void LowChugProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Same path with bypass forced on: the detuner crossfades to the
    // latency-delayed dry signal (and keeps the engine warm for un-bypass).
    process (buffer, true);
}

juce::AudioProcessorParameter* LowChugProcessor::getBypassParameter() const
{
    return state.getParameter (params::bypass);
}

int LowChugProcessor::getNumPrograms()
{
    return presets::numPresets;
}

const juce::String LowChugProcessor::getProgramName (int index)
{
    return presets::getName (index);
}

void LowChugProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presets::numPresets)
        return;

    // Loading a preset writes nine parameters and the saved state, which
    // belongs on the message thread. Most hosts call this from there, but
    // not all of them (a host may switch programs from its audio or a
    // worker thread). So: apply now if we are on the message thread,
    // otherwise remember it and let AsyncUpdater call handleAsyncUpdate()
    // on the message thread shortly. triggerAsyncUpdate() is safe to call
    // from any thread.
    currentProgram = index;

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyProgram (index);
    }
    else
    {
        pendingProgram = index;
        triggerAsyncUpdate();
    }
}

void LowChugProcessor::handleAsyncUpdate()
{
    if (const auto index = pendingProgram.exchange (-1); index >= 0)
        applyProgram (index);
}

void LowChugProcessor::applyProgram (int index)
{
    presets::apply (state, index);
    // Saved with the session, so the preset box shows it again on reload.
    state.state.setProperty ("program", index, nullptr);
}

juce::AudioProcessorEditor* LowChugProcessor::createEditor()
{
    // Called by the host on the message thread whenever it opens our window.
    // The host owns the editor and deletes it when the window closes; the
    // processor (and all parameter values) lives on regardless.
    return new LowChugEditor (*this);
}

void LowChugProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The APVTS keeps every parameter in a ValueTree; serialize it as XML.
    // copyState() is safe to call while the audio thread is running.
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LowChugProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Only accept our own state (a host may hand us anything).
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));

            // replaceState() only pushes a value into a parameter when it
            // differs from what the parameter currently reads. A JUCE
            // AudioParameterBool keeps whatever raw value a host sent (0.13
            // reads as "off"), so after restoring "off" it would still report
            // 0.13 to the host. Push every value the tree now holds once more,
            // where the parameter's raw value disagrees with it. (Message
            // thread, like the rest of setStateInformation; it notifies the
            // host just as replaceState itself does.)
            for (auto* parameter : getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                {
                    const float restored = ranged->convertTo0to1 (state.getRawParameterValue (ranged->paramID)->load());
                    if (! juce::approximatelyEqual (ranged->getValue(), restored))
                        ranged->setValueNotifyingHost (restored);
                }

            // Which preset was loaded (older sessions don't say: keep the default).
            const int program = state.state.getProperty ("program", presets::defaultPreset);
            currentProgram = juce::jlimit (0, presets::numPresets - 1, program);
        }
}

} // namespace lowchug

// JUCE calls this to create the plugin instance; it must be a free function
// with exactly this name and signature.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lowchug::LowChugProcessor();
}
