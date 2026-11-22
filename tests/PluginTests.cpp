// Tests that drive the real plugin processor through JUCE.
//
// juce::UnitTest is JUCE's built-in test framework: each test class
// registers itself via a static instance, and UnitTestRunner runs them all.

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Tunings.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

namespace lowchug
{

class TuningsTests final : public juce::UnitTest
{
public:
    TuningsTests() : UnitTest ("Tunings", "LowChug") {}

    static int indexOf (std::string_view name)
    {
        for (int i = 0; i < (int) tunings::targets.size(); ++i)
            if (tunings::targets[(size_t) i].name == name)
                return i;
        return -1;
    }

    void runTest() override
    {
        using tunings::Source;

        beginTest ("Standard maps to standard tunings");
        expectEquals (*tunings::shiftFor (Source::standard, indexOf ("Eb Standard")), -1);
        expectEquals (*tunings::shiftFor (Source::standard, indexOf ("D Standard")), -2);
        expectEquals (*tunings::shiftFor (Source::standard, indexOf ("B Standard")), -5);

        beginTest ("Drop D maps to drop tunings");
        expectEquals (*tunings::shiftFor (Source::dropD, indexOf ("Drop C")), -2);
        expectEquals (*tunings::shiftFor (Source::dropD, indexOf ("Drop B")), -3);
        expectEquals (*tunings::shiftFor (Source::dropD, indexOf ("Drop A")), -5);

        beginTest ("Octave Down works from either family");
        expectEquals (*tunings::shiftFor (Source::standard, tunings::octaveDownIndex), -12);
        expectEquals (*tunings::shiftFor (Source::dropD, tunings::octaveDownIndex), -12);
        expect (tunings::isReachable (Source::dropD, tunings::octaveDownIndex));

        beginTest ("Per-string changes are not reachable");
        expect (! tunings::isReachable (Source::standard, indexOf ("Drop C")));
        expect (! tunings::isReachable (Source::dropD, indexOf ("D Standard")));

        beginTest ("Custom means manual shift");
        expect (! tunings::shiftFor (Source::standard, tunings::customTargetIndex).has_value());
        expect (! tunings::shiftFor (Source::custom, indexOf ("D Standard")).has_value());

        beginTest ("Readout names what you actually hear");
        expectEquals (juce::String (tunings::describe (Source::dropD, -2)), juce::String ("Drop C"));
        expectEquals (juce::String (tunings::describe (Source::standard, -5)), juce::String ("B Standard"));
        expectEquals (juce::String (tunings::describe (Source::standard, 2)), juce::String ("F# Standard"));
        expectEquals (juce::String (tunings::describe (Source::custom, -3)), juce::String ("-3 st"));

        beginTest ("Manual shift finds the matching target");
        expectEquals (tunings::targetForShift (Source::dropD, -3), indexOf ("Drop B"));
        expectEquals (tunings::targetForShift (Source::standard, -12), tunings::octaveDownIndex);
        expectEquals (tunings::targetForShift (Source::standard, -9), tunings::customTargetIndex);
    }
};

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : UnitTest ("Processor", "LowChug") {}

    static void setParam (LowChugProcessor& p, const char* id, float plainValue)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    static juce::AudioBuffer<float> makeSignal (int channels, int samples)
    {
        juce::AudioBuffer<float> buffer (channels, samples);
        juce::Random random (42);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < samples; ++i)
                buffer.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f
                                             + 0.3f * std::sin ((float) i * 0.0108f * (float) (ch + 1)));
        return buffer;
    }

    /** Feeds the buffer through in odd-sized blocks, like an unpredictable host. */
    static void processInBlocks (LowChugProcessor& p, juce::AudioBuffer<float>& buffer, bool hostBypass)
    {
        juce::MidiBuffer midi;
        const int sizes[] = { 1, 17, 128, 333, 512, 64, 700 }; // 700 > the prepared maximum
        for (int pos = 0, k = 0; pos < buffer.getNumSamples(); ++k)
        {
            const int n = std::min (sizes[k % 7], buffer.getNumSamples() - pos);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
            if (hostBypass)
                p.processBlockBypassed (block, midi);
            else
                p.processBlock (block, midi);
            pos += n;
        }
    }

    /** Checks out[i] == in[i - latency] bit for bit. When the host bypasses
        an active instance, the detuner first crossfades (20 ms) from the
        shifted signal, so exactness is checked once that has finished: it
        completes within the latency, while the dry output is still silence. */
    void expectSampleExactDelay (LowChugProcessor& p, bool hostBypass)
    {
        p.prepareToPlay (48000.0, 512);
        const auto input = makeSignal (2, 24000);
        auto buffer = input;
        processInBlocks (p, buffer, hostBypass);
        const int latency = p.getLatencySamples();
        const int checkFrom = hostBypass ? latency : 0;
        int mismatches = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float expected = i >= latency ? input.getSample (ch, i - latency) : 0.0f;
                if (i >= checkFrom && ! juce::exactlyEqual (buffer.getSample (ch, i), expected))
                    ++mismatches;
            }
        expectEquals (mismatches, 0);
    }

    void runTest() override
    {
        beginTest ("Identity");
        {
            LowChugProcessor p;
            expectEquals (p.getName(), juce::String ("Lithium LowChug"));
            expectEquals (juce::String (JucePlugin_Desc), juce::String ("Guitar Detuner"));
            expectEquals (juce::String (JucePlugin_Manufacturer), juce::String ("Calamity Audio"));
            expect (p.getBypassParameter() == p.getState().getParameter (params::bypass));
        }

        beginTest ("Every parameter exists");
        {
            LowChugProcessor p;
            for (auto id : { params::shift, params::fine, params::sourceTuning, params::targetTuning, params::mode,
                             params::formant, params::tight, params::mix, params::output, params::bypass })
                expect (p.getState().getParameter (id) != nullptr, id);
        }

        beginTest ("Reports the engine latency");
        {
            LowChugProcessor p;
            p.prepareToPlay (48000.0, 512);
            expectEquals (p.getLatencySamples(), 1024);
            p.prepareToPlay (96000.0, 256); // hosts re-prepare on rate changes
            expectEquals (p.getLatencySamples(), 2048);
        }

        beginTest ("Effective shift follows Playing in / Hear, or the shift knob when Custom");
        {
            LowChugProcessor p;
            expectWithinAbsoluteError (p.getEffectiveShift(), -2.0f, 1.0e-4f); // default: Standard -> D Standard
            setParam (p, params::sourceTuning, (float) tunings::Source::dropD);
            setParam (p, params::targetTuning, (float) TuningsTests::indexOf ("Drop A"));
            expectWithinAbsoluteError (p.getEffectiveShift(), -5.0f, 1.0e-4f);
            setParam (p, params::fine, 25.0f);
            expectWithinAbsoluteError (p.getEffectiveShift(), -4.75f, 1.0e-4f);
            setParam (p, params::targetTuning, (float) tunings::customTargetIndex);
            setParam (p, params::shift, -7.0f);
            expectWithinAbsoluteError (p.getEffectiveShift(), -6.75f, 1.0e-4f);
        }

        beginTest ("No shift is bit-identical to the latency-delayed input (odd block sizes)");
        {
            LowChugProcessor p;
            setParam (p, params::sourceTuning, (float) tunings::Source::custom);
            setParam (p, params::shift, 0.0f);
            expectSampleExactDelay (p, false);
        }

        beginTest ("Bypass parameter gives the latency-delayed input exactly");
        {
            LowChugProcessor p;
            setParam (p, params::bypass, 1.0f);
            expectSampleExactDelay (p, false);
        }

        beginTest ("Host bypass (processBlockBypassed) gives the latency-delayed input exactly");
        {
            LowChugProcessor p;
            expectSampleExactDelay (p, true);
        }

        beginTest ("The default tuning actually shifts");
        {
            LowChugProcessor p;
            p.prepareToPlay (48000.0, 512);
            const auto input = makeSignal (2, 48000);
            auto buffer = input;
            processInBlocks (p, buffer, false);
            const int latency = p.getLatencySamples();
            double difference = 0.0;
            for (int i = latency; i < buffer.getNumSamples(); ++i)
                difference = std::max (difference, (double) std::abs (buffer.getSample (0, i) - input.getSample (0, i - latency)));
            expect (difference > 0.01, "output should differ from the delayed input at -2 st");
        }

        beginTest ("Mono in, stereo out copies the shifted signal to both sides");
        {
            LowChugProcessor p;
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            expect (p.setBusesLayout (layout));
            p.prepareToPlay (48000.0, 64);
            auto buffer = makeSignal (2, 9600);
            buffer.clear (1, 0, buffer.getNumSamples());
            processInBlocks (p, buffer, false);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expect (juce::exactlyEqual (buffer.getSample (1, i), buffer.getSample (0, i)));
        }

        beginTest ("State save/restore round trip");
        {
            LowChugProcessor a;
            setParam (a, params::shift, -5.0f);
            setParam (a, params::fine, -12.5f);
            setParam (a, params::mode, 1.0f);
            setParam (a, params::sourceTuning, 1.0f);
            setParam (a, params::mix, 40.0f);
            juce::MemoryBlock saved;
            a.getStateInformation (saved);

            LowChugProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());
            auto plain = [&b] (const char* id) {
                auto* param = b.getState().getParameter (id);
                return param->convertFrom0to1 (param->getValue());
            };
            expectWithinAbsoluteError (plain (params::shift), -5.0f, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::fine), -12.5f, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::mode), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::sourceTuning), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::mix), 40.0f, 1.0e-3f);
        }

        beginTest ("Garbage state is ignored");
        {
            LowChugProcessor p;
            const char junk[] = "not a LowChug state";
            p.setStateInformation (junk, (int) sizeof (junk));
            auto* shift = p.getState().getParameter (params::shift);
            expectWithinAbsoluteError (shift->convertFrom0to1 (shift->getValue()), -2.0f, 1.0e-4f);
        }
    }
};

// Static instances register the tests with JUCE's test runner.
static TuningsTests tuningsTests;
static ProcessorTests processorTests;

} // namespace lowchug

int main()
{
    // Sets up JUCE's message manager etc. for the lifetime of main().
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("LowChug");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    return failures == 0 ? 0 : 1;
}
