// Tests that drive the real plugin processor through JUCE.
//
// juce::UnitTest is JUCE's built-in test framework: each test class
// registers itself via a static instance, and UnitTestRunner runs them all.

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"
#include "plugin/Tunings.h"
#include "ui/LithiumLookAndFeel.h"
#include "ui/PluginEditor.h"
#include "ui/ShiftAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <thread>

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

        beginTest ("Reports the engine latency in both modes");
        {
            LowChugProcessor p;
            p.setRateAndBufferSizeDetails (48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            expectEquals (p.getLatencySamples(), 1024);
            setParam (p, params::mode, 1.0f); // on the message thread: reported at once
            expectEquals (p.getLatencySamples(), 2304);
            setParam (p, params::mode, 0.0f);
            expectEquals (p.getLatencySamples(), 1024);

            p.setRateAndBufferSizeDetails (96000.0, 256);
            p.prepareToPlay (96000.0, 256); // hosts re-prepare on rate changes
            expectEquals (p.getLatencySamples(), 2048);
            setParam (p, params::mode, 1.0f);
            expectEquals (p.getLatencySamples(), 4608);
        }

        beginTest ("Studio mode: no shift is bit-identical to the input delayed by the Studio latency");
        {
            LowChugProcessor p;
            setParam (p, params::mode, 1.0f);
            setParam (p, params::sourceTuning, (float) tunings::Source::custom);
            setParam (p, params::shift, 0.0f);
            expectSampleExactDelay (p, false);
            expectEquals (p.getLatencySamples(), 2304);
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

        beginTest ("Drop D -> Drop G moves a tone on input 1 down a fifth (input 2 silent, like a guitar interface)");
        {
            LowChugProcessor p;
            setParam (p, params::sourceTuning, (float) tunings::Source::dropD);
            setParam (p, params::targetTuning, (float) TuningsTests::indexOf ("Drop G"));
            p.prepareToPlay (48000.0, 128);

            const int n = 96000;
            juce::AudioBuffer<float> buffer (2, n);
            buffer.clear();
            for (int i = 0; i < n; ++i)
                buffer.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 48000.0f));
            juce::MidiBuffer midi;
            for (int pos = 0; pos < n; pos += 128)
            {
                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, pos, 128);
                p.processBlock (block, midi);
            }

            // Pitch from zero crossings over the second half, and level.
            int crossings = 0;
            double sumSquares = 0.0;
            for (int i = n / 2 + 1; i < n; ++i)
            {
                crossings += (buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f) ? 1 : 0;
                sumSquares += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
            }
            const double measuredHz = crossings / 2.0 / ((n / 2 - 1) / 48000.0);
            const double expectedHz = 220.0 * std::pow (2.0, -7.0 / 12.0);
            const double rms = std::sqrt (sumSquares / (n / 2));
            logMessage ("    measured " + juce::String (measuredHz, 2) + " Hz (expected " + juce::String (expectedHz, 2)
                        + "), rms " + juce::String (rms, 3) + " (input 0.212)");
            expectWithinAbsoluteError (measuredHz, expectedHz, 0.5);
            expectWithinAbsoluteError (rms, 0.212, 0.03);
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

        beginTest ("Factory presets: 18 programs, Live then Studio, each setting its tuning");
        {
            LowChugProcessor p;
            expectEquals (p.getNumPrograms(), 18);
            expectEquals (p.getProgramName (0), juce::String ("Eb Standard (Live)"));
            expectEquals (p.getProgramName (9), juce::String ("Eb Standard (Studio)"));
            expectEquals (p.getProgramName (17), juce::String ("Octave Down (Studio)"));
            expectEquals (p.getCurrentProgram(), presets::defaultPreset);
            expect (presets::matches (p.getState(), presets::defaultPreset), "a fresh instance is D Standard (Live)");

            auto plain = [&p] (const char* id) {
                auto* param = p.getState().getParameter (id);
                return param->convertFrom0to1 (param->getValue());
            };

            setParam (p, params::mix, 40.0f);
            setParam (p, params::fine, 12.0f);
            p.setCurrentProgram (5 + 9); // Drop C (Studio)
            expectEquals (p.getCurrentProgram(), 14);
            expectWithinAbsoluteError (plain (params::sourceTuning), (float) tunings::Source::dropD, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::targetTuning), (float) TuningsTests::indexOf ("Drop C"), 1.0e-4f);
            expectWithinAbsoluteError (plain (params::shift), -2.0f, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::mode), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (plain (params::mix), 100.0f, 1.0e-3f); // presets reset the sound controls
            expectWithinAbsoluteError (plain (params::fine), 0.0f, 1.0e-3f);
            expect (presets::matches (p.getState(), 14));

            setParam (p, params::fine, 5.0f);
            expect (! presets::matches (p.getState(), 14), "changing a setting marks the preset edited");

            // Saved with the session.
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            LowChugProcessor q;
            q.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (q.getCurrentProgram(), 14);
        }

        beginTest ("Studio presets select the Studio engine and report its latency; Live presets the Live one");
        {
            LowChugProcessor p;
            p.setRateAndBufferSizeDetails (48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            for (int program = 0; program < presets::numPresets; ++program)
            {
                p.setCurrentProgram (program);
                expectEquals (p.getLatencySamples(), presets::isStudio (program) ? 2304 : 1024, p.getProgramName (program));
            }

            // And a session saved in Studio reopens in Studio (prepare reads the mode).
            p.setCurrentProgram (9 + 3); // C Standard (Studio)
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            LowChugProcessor q;
            q.setStateInformation (saved.getData(), (int) saved.getSize());
            q.setRateAndBufferSizeDetails (48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            expectEquals (q.getLatencySamples(), 2304);
        }

        beginTest ("Changes from other threads: a program switch is applied on the message thread, "
                   "and mode automation reaches the host latency via the timer");
        {
            // JUCE's message loop can only be run once per process (stopping
            // it is permanent), so both cross-thread checks share one run,
            // chained with callAsync / callAfterDelay.
            LowChugProcessor p;
            p.setRateAndBufferSizeDetails (48000.0, 512);
            p.prepareToPlay (48000.0, 512);

            std::thread host ([&p] { p.setCurrentProgram (7); }); // Drop A (Live)
            host.join();
            expectEquals (p.getCurrentProgram(), 7);

            // Messages are handled in order, so the AsyncUpdater's (posted by
            // the host thread above) is handled before this.
            juce::MessageManager::callAsync ([this, &p]
            {
                auto* target = p.getState().getParameter (params::targetTuning);
                expectEquals (juce::roundToInt (target->convertFrom0to1 (target->getValue())), TuningsTests::indexOf ("Drop A"));

                // Now automation switches to Studio from the audio thread:
                // the latency must not be reported from there...
                std::thread audio ([&p] { setParam (p, params::mode, 1.0f); });
                audio.join();
                expectEquals (p.getLatencySamples(), 1024, "not reported from the audio thread itself");

                // ...but the 10 Hz timer reports it shortly after.
                juce::Timer::callAfterDelay (300, [this, &p]
                {
                    expectEquals (p.getLatencySamples(), 2304);
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                });
            });
            juce::MessageManager::getInstance()->runDispatchLoop();
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


//==============================================================================
/** A guitar-ish chord: each string a sawtooth-like stack of decaying
    harmonics, `shift` semitones away from its open pitch. */
static float chordSample (const int* strings, int numStrings, float shift, int i, double rate)
{
    float x = 0.0f;
    for (int s = 0; s < numStrings; ++s)
    {
        const auto hz = 440.0 * std::pow (2.0, (strings[s] + shift - 69.0) / 12.0);
        for (int h = 1; h <= 8; ++h)
            x += (float) (0.05 / h * std::sin (juce::MathConstants<double>::twoPi * hz * h * i / rate + s * 0.7 + h));
    }
    return x;
}

class ShiftAnalyzerTests final : public juce::UnitTest
{
public:
    ShiftAnalyzerTests() : UnitTest ("Shift analyzer", "LowChug") {}

    void runTest() override
    {
        const int dropD[] = { 38, 45, 50, 55, 59, 64 };

        for (const auto [shift, latency] : { std::pair { -2.0f, 0 }, std::pair { -5.0f, 1024 }, std::pair { -1.25f, 333 },
                                             std::pair { -12.0f, 1024 }, std::pair { 0.0f, 0 } })
        {
            beginTest ("Measures a " + juce::String (shift, 2) + " st shift of a chord (latency " + juce::String (latency) + ")");
            ShiftAnalyzer analyzer;
            analyzer.prepare (48000.0);

            // Output = the input chord, shifted, arriving `latency` samples late.
            const int n = 48000;
            std::vector<float> in ((size_t) n), out ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                in[(size_t) i] = chordSample (dropD, 6, 0.0f, i, 48000.0);
                out[(size_t) i] = i >= latency ? chordSample (dropD, 6, shift, i - latency, 48000.0) : 0.0f;
            }

            ShiftAnalyzer::Result r;
            for (int pos = 0; pos < n; pos += 1600) // 30 updates a second, as the editor does
            {
                analyzer.push (in.data() + pos, out.data() + pos, 1600);
                r = analyzer.update (latency, shift);
            }

            expect (r.state == ShiftAnalyzer::Result::State::measured, "should have measured a shift");
            logMessage ("    measured " + juce::String (r.measuredSemitones, 3) + " st, confidence " + juce::String (r.confidence, 2));
            expectWithinAbsoluteError (r.measuredSemitones, shift, 0.02f); // 2 cents
        }

        // Through the real processor, in both modes: the analyzer lines the
        // input up with the output using the REPORTED latency, so this also
        // checks that the reported latency is what the audio really does.
        const int standard[] = { 40, 45, 50, 55, 59, 64 };
        for (const auto mode : { 0.0f, 1.0f })
            for (const int shift : { -2, -5, -7 })
            {
                beginTest (juce::String (mode > 0.5f ? "Studio" : "Live") + ": a chord through the processor measures "
                           + juce::String (shift) + " st within 2 cents");
                LowChugProcessor p;
                auto set = [&p] (const char* id, float v) {
                    auto* param = p.getState().getParameter (id);
                    param->setValueNotifyingHost (param->convertTo0to1 (v));
                };
                set (params::mode, mode);
                set (params::sourceTuning, (float) tunings::Source::custom);
                set (params::shift, (float) shift);
                p.setRateAndBufferSizeDetails (48000.0, 256);
                p.prepareToPlay (48000.0, 256);

                ShiftAnalyzer analyzer;
                analyzer.prepare (48000.0);
                juce::AudioBuffer<float> block (2, 256);
                std::vector<float> in (256), out (256);
                juce::MidiBuffer midi;
                ShiftAnalyzer::Result r;
                for (int pos = 0; pos < 72000; pos += 256)
                {
                    for (int i = 0; i < 256; ++i)
                        in[(size_t) i] = chordSample (standard, 6, 0.0f, pos + i, 48000.0);
                    block.copyFrom (0, 0, in.data(), 256);
                    block.copyFrom (1, 0, in.data(), 256);
                    p.processBlock (block, midi);
                    std::copy (block.getReadPointer (0), block.getReadPointer (0) + 256, out.begin());
                    analyzer.push (in.data(), out.data(), 256);
                    if (pos % 1536 == 0)
                        r = analyzer.update (p.getLatencySamples(), p.getEffectiveShift());
                }
                expect (r.state == ShiftAnalyzer::Result::State::measured, "should have measured a shift");
                logMessage ("    measured " + juce::String (r.measuredSemitones, 4) + " st, confidence " + juce::String (r.confidence, 2));
                expectWithinAbsoluteError (r.measuredSemitones, (float) shift, 0.02f);
            }

        beginTest ("Silence is reported as silence");
        {
            ShiftAnalyzer analyzer;
            analyzer.prepare (48000.0);
            std::vector<float> zeros (48000, 0.0f);
            analyzer.push (zeros.data(), zeros.data(), (int) zeros.size());
            expect (analyzer.update (1024, -2.0f).state == ShiftAnalyzer::Result::State::silent);
        }
    }
};

//==============================================================================
class EditorTests final : public juce::UnitTest
{
public:
    EditorTests() : UnitTest ("Editor", "LowChug") {}

    static float plain (LowChugProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    }

    static void prepare (LowChugProcessor& p)
    {
        // Hosts set the rate before prepareToPlay; getSampleRate() reads it.
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
    }

    /** Plays `seconds` of a Drop D chord through the processor in 512-sample blocks. */
    static void playChord (LowChugProcessor& p, double seconds)
    {
        const int dropD[] = { 38, 45, 50, 55, 59, 64 };
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < (int) (seconds * 48000.0); pos += 512)
        {
            for (int i = 0; i < 512; ++i)
                block.setSample (0, i, chordSample (dropD, 6, 0.0f, pos + i, 48000.0));
            block.clear (1, 0, 512); // a guitar interface: input 2 silent
            p.processBlock (block, midi);
        }
    }

    static void saveSnapshot (juce::Component& c, const juce::String& name, float scale)
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("LOWCHUG_SNAPSHOT_DIR", {});
        if (dir.isEmpty())
            return;
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        juce::File file (juce::File (dir).getChildFile (name + ".png"));
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    void runTest() override
    {
        beginTest ("Editor opens and closes without leaking");
        {
            LowChugProcessor p;
            prepare (p);
            expectEquals (EmbeddedFonts::getLiveCount(), 0);

            for (int round = 0; round < 3; ++round)
            {
                // Exactly what the VST3/LV2 wrappers do when the host opens
                // and closes the window: createEditorAndMakeActive(), then
                // editorBeingDeleted() and delete.
                std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
                expect (editor != nullptr);
                expect (p.getActiveEditor() == editor.get());
                expect (p.getAnalysisTap().isEnabled(), "the visualizer's tap runs while the editor is open");
                expectEquals (EmbeddedFonts::getLiveCount(), 1);

                editor->setSize (1200, 750); // resizing re-lays out and repaints
                juce::Image image (juce::Image::ARGB, 1200, 750, true);
                juce::Graphics g (image);
                editor->paintEntireComponent (g, true);

                p.editorBeingDeleted (editor.get());
                editor.reset();
                expect (p.getActiveEditor() == nullptr);
                expect (! p.getAnalysisTap().isEnabled(), "closing the editor stops the tap");
                expectEquals (EmbeddedFonts::getLiveCount(), 0); // every font and LookAndFeel released
            }

            // Parameters still work with no editor (no dangling listeners).
            auto* shift = p.getState().getParameter (params::shift);
            shift->setValueNotifyingHost (shift->convertTo0to1 (-7.0f));
            playChord (p, 0.1);
        }

        beginTest ("Picking a Hear target moves the Shift knob to it");
        {
            LowChugProcessor p;
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& controller = editor->getMainView().getTuningController();
            controller.pickTarget (TuningsTests::indexOf ("B Standard"));
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), TuningsTests::indexOf ("B Standard"));
            expectWithinAbsoluteError (plain (p, params::shift), -5.0f, 1.0e-4f);
            expectWithinAbsoluteError (editor->getMainView().getShiftSlider().getValue(), -5.0, 1.0e-4);
        }

        beginTest ("Turning Shift by hand sets Hear to Custom; the knob following its parameter does not");
        {
            LowChugProcessor p;
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& slider = editor->getMainView().getShiftSlider();

            // Automation (or a preset) moves the parameter: Hear stays put.
            auto* shift = p.getState().getParameter (params::shift);
            shift->setValueNotifyingHost (shift->convertTo0to1 (-4.0f));
            expectWithinAbsoluteError (slider.getValue(), -4.0, 1.0e-4);
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), tunings::defaultTargetIndex);

            // The user presses an arrow key on the knob: Hear becomes Custom.
            static_cast<juce::Component&> (slider).keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            expectWithinAbsoluteError (plain (p, params::shift), -5.0f, 1.0e-4f);
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), tunings::customTargetIndex);
            expectEquals (p.getShiftSemitones(), -5);
        }

        beginTest ("Changing Playing in keeps the sound");
        {
            LowChugProcessor p;
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& controller = editor->getMainView().getTuningController();

            controller.pickSource (tunings::Source::dropD); // D Standard (-2) -> Drop C (-2)
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), TuningsTests::indexOf ("Drop C"));
            expectEquals (p.getShiftSemitones(), -2);

            controller.pickSource (tunings::Source::custom); // no names: Custom, still -2
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), tunings::customTargetIndex);
            expectEquals (p.getShiftSemitones(), -2);
        }

        beginTest ("The dial offers only reachable tunings, steps lower, and snaps out of Custom");
        {
            LowChugProcessor p;
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& view = editor->getMainView();
            auto& dial = view.getTuningPanel().getDial();

            // From Standard: E Standard ... A Standard, then Octave Down.
            expectEquals ((int) dial.getDetents().size(), 9);
            expectEquals (dial.getDetents().front(), TuningsTests::indexOf ("E Standard"));
            expectEquals (dial.getDetents().back(), tunings::octaveDownIndex);

            dial.step (1); // D Standard -> C# Standard
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), TuningsTests::indexOf ("C# Standard"));
            expectEquals (p.getShiftSemitones(), -3);

            // From Drop D the dial's notches are the drop tunings.
            view.getTuningController().pickSource (tunings::Source::dropD);
            expectEquals (dial.getDetents().front(), TuningsTests::indexOf ("Drop D"));
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), TuningsTests::indexOf ("Drop B")); // still -3

            // Shift by hand to -6 (Custom); the next step lands on the nearest tuning, Drop Ab.
            static_cast<juce::Component&> (view.getShiftSlider()).keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            static_cast<juce::Component&> (view.getShiftSlider()).keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            static_cast<juce::Component&> (view.getShiftSlider()).keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), tunings::customTargetIndex);
            dial.step (1);
            expectEquals (juce::roundToInt (plain (p, params::targetTuning)), TuningsTests::indexOf ("Drop Ab"));

            // Playing in Custom: no notches at all.
            view.getTuningController().pickSource (tunings::Source::custom);
            expect (dial.getDetents().empty());
        }

        beginTest ("The preset box steps through presets and marks edits");
        {
            LowChugProcessor p;
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& bar = editor->getMainView().getHeader().getPresetBar();
            expectEquals (bar.getShownName(), juce::String ("D Standard"));
            bar.step (1);
            expectEquals (p.getCurrentProgram(), presets::defaultPreset + 1);
            expectEquals (bar.getShownName(), juce::String ("C# Standard"));
            bar.step (-3); // wraps around to the last preset
            expectEquals (p.getCurrentProgram(), 17);
            editor->getMainView().getTuningPanel().getDial().step (-1);
            bar.refresh();
            expect (bar.getShownName().endsWith ("*"), "an edited preset is marked");
            expectEquals (bar.getShownDetail(), juce::String::fromUTF8 ("Studio \xc2\xb7 play in Standard"));
        }

        beginTest ("The preset panel lists Live and Studio presets and loads the one chosen");
        {
            LowChugProcessor p;
            prepare (p);
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& bar = editor->getMainView().getHeader().getPresetBar();
            auto panel = bar.createPanel();
            panel->resized();

            // One row per preset, each a button titled with the preset's full name.
            juce::Button* dropA = nullptr;
            int rows = 0;
            for (auto* child : panel->getChildren())
                if (auto* row = dynamic_cast<juce::Button*> (child))
                {
                    ++rows;
                    if (row->getTitle() == "Drop A (Studio)")
                        dropA = row;
                    if (row->getTitle() == "D Standard (Live)")
                        expect (row->getToggleState(), "the loaded preset is marked");
                }
            expectEquals (rows, presets::numPresets);
            expect (dropA != nullptr);
            if (dropA != nullptr)
                dropA->onClick();
            expectEquals (p.getCurrentProgram(), 9 + 7);
            expectEquals (p.getLatencySamples(), 2304);
            expectEquals (bar.getShownName(), juce::String ("Drop A"));
            expectEquals (bar.getShownDetail(), juce::String::fromUTF8 ("Studio \xc2\xb7 play in Drop D"));
        }

        beginTest ("Quality controls work: Live / Studio, Formant and Tight drive their parameters");
        {
            LowChugProcessor p;
            prepare (p);
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& view = editor->getMainView();

            expect (view.getModeButton (0).isEnabled() && view.getModeButton (1).isEnabled());
            expect (view.getFormantButton().isEnabled() && view.getTightKnob().isEnabled());
            expect (view.getModeButton (0).getToggleState() && ! view.getModeButton (1).getToggleState(), "Live lit");

            view.getModeButton (1).onClick(); // what clicking the Studio segment runs
            expectWithinAbsoluteError (plain (p, params::mode), 1.0f, 1.0e-4f);
            expect (view.getModeButton (1).getToggleState() && ! view.getModeButton (0).getToggleState(), "Studio lit");
            expectEquals (p.getLatencySamples(), 2304);
            view.getHeader().getPresetBar().refresh(); // (its timer does this ten times a second)
            expect (view.getHeader().getPresetBar().getShownName().endsWith ("*"), "mode differs from the Live preset: edited");

            view.getFormantButton().setToggleState (true, juce::sendNotificationSync); // what a click does
            expectWithinAbsoluteError (plain (p, params::formant), 1.0f, 1.0e-4f);

            view.getTightKnob().getSlider().setValue (40.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (plain (p, params::tight), 40.0f, 0.1f);

            // Automation moves them back: the controls follow.
            auto* mode = p.getState().getParameter (params::mode);
            mode->setValueNotifyingHost (mode->convertTo0to1 (0.0f));
            expect (view.getModeButton (0).getToggleState() && ! view.getModeButton (1).getToggleState(), "Live lit again");

            saveSnapshot (*editor, "lowchug-quality-1x", 1.0f);
            view.showAboutPanel (true);
            saveSnapshot (*editor, "lowchug-about-1x", 1.0f);
        }

        beginTest ("The preset menu's column headers show each mode's latency");
        {
            LowChugProcessor p;
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& bar = editor->getMainView().getHeader().getPresetBar();
            expectEquals (bar.getMenuHeader (params::Mode::live), juce::String ("LIVE"), "not prepared yet: no rate, no number");
            prepare (p);
            expectEquals (bar.getMenuHeader (params::Mode::live), juce::String::fromUTF8 ("LIVE \xc2\xb7 21 ms"));
            expectEquals (bar.getMenuHeader (params::Mode::studio), juce::String::fromUTF8 ("STUDIO \xc2\xb7 48 ms"));
        }

        beginTest ("The tuner reads the string being played (before the shift)");
        {
            LowChugProcessor p;
            prepare (p);
            auto editor = std::make_unique<LowChugEditor> (p);
            auto& readout = editor->getMainView().getReadoutPanel();
            readout.showTuner (true);

            // An A string (110 Hz), 6 cents sharp, while LowChug shifts down 2.
            const double hz = 110.0 * std::pow (2.0, 6.0 / 1200.0);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < 48000; pos += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (pos + i) / 48000.0;
                    float v = 0.0f;
                    for (int h = 1; h <= 6; ++h)
                        v += (float) (0.2 / h * std::sin (juce::MathConstants<double>::twoPi * hz * h * t + h));
                    block.setSample (0, i, v);
                }
                block.clear (1, 0, 512);
                p.processBlock (block, midi);
                if (pos % 1536 == 0)
                    readout.analyzeNow();
            }

            const auto note = readout.getTuner().getShownNote();
            logMessage ("    tuner shows MIDI " + juce::String (note, 3) + " (A2 is 45, +6 ct = 45.06)");
            expectWithinAbsoluteError (note, 45.06f, 0.02f); // within 2 cents

            saveSnapshot (*editor, "lowchug-tuner-2x", 2.0f);
        }

        beginTest ("The visualizer measures the real shift (Drop D -> Drop C)");
        {
            LowChugProcessor p;
            auto* source = p.getState().getParameter (params::sourceTuning);
            source->setValueNotifyingHost (source->convertTo0to1 ((float) tunings::Source::dropD));
            auto* target = p.getState().getParameter (params::targetTuning);
            target->setValueNotifyingHost (target->convertTo0to1 ((float) TuningsTests::indexOf ("Drop C")));
            prepare (p);

            auto editor = std::make_unique<LowChugEditor> (p);
            auto& readout = editor->getMainView().getReadoutPanel();
            for (int i = 0; i < 30; ++i) // one second, analyzed 30 times like the timer would
            {
                playChord (p, 1.0 / 30.0);
                readout.analyzeNow();
            }

            const auto& r = readout.getLastResult();
            expect (r.state == ShiftAnalyzer::Result::State::measured, "should have measured the shift");
            logMessage ("    measured " + juce::String (r.measuredSemitones, 3) + " st, confidence " + juce::String (r.confidence, 2));
            expectWithinAbsoluteError (r.measuredSemitones, -2.0f, 0.05f); // within 5 cents: "On target"

            saveSnapshot (*editor, "lowchug-editor-1x", 1.0f);
            saveSnapshot (*editor, "lowchug-editor-2x", 2.0f);
        }
    }
};

// Static instances register the tests with JUCE's test runner.
static TuningsTests tuningsTests;
static ProcessorTests processorTests;
static ShiftAnalyzerTests shiftAnalyzerTests;
static EditorTests editorTests;

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
