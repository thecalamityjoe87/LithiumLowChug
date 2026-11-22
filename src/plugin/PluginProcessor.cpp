#include "PluginProcessor.h"

#include "Parameters.h"

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

void LowChugProcessor::prepareToPlay (double /*sampleRate*/, int /*maximumExpectedSamplesPerBlock*/)
{
    // Milestone 2: prepare the pitch-shift engine and dry-path delay here
    // (all allocation happens in this function, never in processBlock), then
    // report the engine's latency with setLatencySamples().
    setLatencySamples (0);
}

void LowChugProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Denormals (tiny floats near zero) are very slow on x86; this turns them
    // into zeros for the duration of the block.
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Mono in, stereo out: copy the input to the second channel.
    int filledChannels = numIn;
    if (numIn == 1 && numOut > 1)
    {
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
        filledChannels = 2;
    }

    // Clear any outputs that have no matching input (they may hold garbage).
    for (int ch = filledChannels; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Milestone 1 is a clean passthrough, so bypassed and active sound the
    // same. From milestone 2, bypass crossfades to the latency-delayed dry
    // signal so toggling it never shifts the audio in time.
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
