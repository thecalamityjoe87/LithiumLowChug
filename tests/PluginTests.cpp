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

        beginTest ("Stereo passthrough is bit-identical with odd block sizes");
        {
            LowChugProcessor p;
            p.prepareToPlay (48000.0, 512);
            juce::Random random (42);
            juce::MidiBuffer midi;
            for (int blockSize : { 1, 17, 128, 333, 512 })
            {
                juce::AudioBuffer<float> buffer (2, blockSize);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
                juce::AudioBuffer<float> original (buffer);
                p.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        expect (juce::exactlyEqual (buffer.getSample (ch, i), original.getSample (ch, i)));
            }
            expectEquals (p.getLatencySamples(), 0);
        }

        beginTest ("Mono in, stereo out copies the input to both sides");
        {
            LowChugProcessor p;
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            expect (p.setBusesLayout (layout));
            p.prepareToPlay (48000.0, 64);
            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            for (int i = 0; i < 64; ++i)
                buffer.setSample (0, i, (float) i / 64.0f);
            juce::MidiBuffer midi;
            p.processBlock (buffer, midi);
            for (int i = 0; i < 64; ++i)
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
