#include "PluginProcessor.h"

#include "Parameters.h"
#include "Tunings.h"

namespace lowchug
{

// BusesProperties is a protected type of AudioProcessor, so this helper has
// to be a (static) member to be allowed to use it.
juce::AudioProcessor::BusesProperties LowChugProcessor::makeBuses()
{
    // Default to stereo; isBusesLayoutSupported() also allows mono.
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

LowChugProcessor::LowChugProcessor()
    : AudioProcessor (makeBuses()),
      // "PARAMETERS" is the root tag of the saved state. nullptr = no undo manager.
      state (*this, nullptr, "PARAMETERS", params::createLayout())
{
    shiftValue = state.getRawParameterValue (params::shift);
    fineValue = state.getRawParameterValue (params::fine);
    sourceValue = state.getRawParameterValue (params::sourceTuning);
    targetValue = state.getRawParameterValue (params::targetTuning);
    mixValue = state.getRawParameterValue (params::mix);
    outputValue = state.getRawParameterValue (params::output);
    bypassValue = state.getRawParameterValue (params::bypass);
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
    detuner.prepare (sampleRate, maximumExpectedSamplesPerBlock, std::max (1, getTotalNumInputChannels()));

    // Start from the current settings rather than gliding in from defaults.
    pushParametersToDsp (false);
    detuner.reset();

    // Hosts delay every other track by this much so ours stays in time. The
    // dry path is delayed by the same amount, so it is constant in every
    // state: shifted, unshifted, any mix, bypassed.
    setLatencySamples (detuner.getLatencySamples());
}

void LowChugProcessor::reset()
{
    detuner.reset();
}

float LowChugProcessor::getEffectiveShift() const
{
    const auto source = static_cast<tunings::Source> (juce::roundToInt (sourceValue->load()));
    const int target = juce::roundToInt (targetValue->load());

    // A named target ("Hear: Drop C") decides the shift; "Custom" (on either
    // side) hands control to the manual shift parameter.
    const int semitones = tunings::shiftFor (source, target).value_or (juce::roundToInt (shiftValue->load()));
    return (float) semitones + fineValue->load() / 100.0f;
}

void LowChugProcessor::pushParametersToDsp (bool forceBypass)
{
    // The detuner smooths every one of these, so it's fine to set them on
    // every block even while automation is moving them.
    detuner.setShiftSemitones (getEffectiveShift());
    detuner.setMix (mixValue->load() / 100.0f);
    detuner.setOutputGain (juce::Decibels::decibelsToGain (outputValue->load()));
    detuner.setBypassed (forceBypass || bypassValue->load() >= 0.5f);
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

    // Process the input channels in place. (Identical stereo channels are
    // detected inside the detuner and shifted only once.)
    detuner.process (buffer.getArrayOfWritePointers(), std::min (numIn, buffer.getNumChannels()), numSamples);

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

juce::AudioProcessorEditor* LowChugProcessor::createEditor()
{
    // A temporary auto-generated editor; the real LowChug UI is milestone 4.
    return new juce::GenericAudioProcessorEditor (*this);
}

void LowChugProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The APVTS keeps every parameter in a ValueTree; serialise it as XML.
    // copyState() is safe to call while the audio thread is running.
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LowChugProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Only accept our own state (a host may hand us anything).
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace lowchug

// JUCE calls this to create the plugin instance; it must be a free function
// with exactly this name and signature.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lowchug::LowChugProcessor();
}
