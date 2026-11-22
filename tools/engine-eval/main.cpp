// LowChug engine A/B harness.
//
// Runs every candidate PitchShiftEngine over:
//   - a synthetic DI guitar corpus (with an exactly known ideal shifted
//     version to score against): chords, palm mutes, single notes, low E/D
//     sustains,
//   - sine tones (82.4 / 110 / 440 Hz) for pitch accuracy,
// and reports quality, latency and CPU. It also writes latency-aligned WAVs
// so the engines can be compared by ear.
//
// Usage:
//   lowchug-engine-eval [--out DIR] [--input di.wav] [--audio none|key|all] [--engines full|quick|shipping]
//
// --engines shipping runs just the configurations in src/dsp/EngineConfigs.h
// (and whatever variants are being compared against them).
//
// With --input, your own DI recording is also rendered through every engine
// at the key shifts (no objective scores: there is no ground truth for it).

#include "Analysis.h"
#include "TestSignals.h"
#include "WavFile.h"

#include "dsp/EngineConfigs.h"
#include "dsp/engines/GranularEngine.h"
#include "dsp/engines/PhaseVocoderEngine.h"
#include "dsp/engines/SignalsmithEngine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace lowchug;
using namespace lowchug::eval;
namespace fs = std::filesystem;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int hostBlockSize = 128;
constexpr int maxBlockSize = 512;

struct EngineSpec
{
    std::string role; // "reference", "PV", "Signalsmith"
    std::function<std::unique_ptr<dsp::PitchShiftEngine>()> make;
};

std::vector<EngineSpec> makeEngineList (const std::string& set)
{
    const bool quick = set == "quick";
    using SS = dsp::SignalsmithEngine;
    using PV = dsp::PhaseVocoderEngine;
    std::vector<EngineSpec> list;

    auto ss = [&] (float block, float interval, bool split = false, float tonal = 0) {
        list.push_back ({ "Signalsmith", [=] { return std::make_unique<SS> (SS::Config { block, interval, split, tonal }); } });
    };
    auto pv = [&] (int size, int overlap, bool transientReset = true, int synthesisSize = 0) {
        list.push_back ({ "PV", [=] { return std::make_unique<PV> (PV::Config { size, overlap, transientReset, 6.0f, synthesisSize }); } });
    };

    auto pvConfig = [&] (PV::Config config) {
        list.push_back ({ "PV", [=] { return std::make_unique<PV> (config); } });
    };

    if (set == "shipping")
    {
        // The shipping configs, and each without the partial model (how
        // milestone 1 shipped) for comparison.
        for (auto base : { dsp::configs::live(), dsp::configs::studio() })
            for (auto [model, mask] : { std::pair { false, false }, std::pair { true, true } })
            {
                auto c = base;
                c.partialModel = model;
                c.onsetMask = mask;
                pvConfig (c);
            }
        return list;
    }

    list.push_back ({ "reference", [] { return std::make_unique<dsp::GranularEngine> (dsp::GranularEngine::Config { 30.0f }); } });

    if (quick)
    {
        pv (2048, 4);
        pv (4096, 4, true, 1024);
        ss (40, 10);
        ss (120, 30);
        return list;
    }

    pv (1024, 4);
    pv (2048, 4);
    pv (4096, 4);
    pv (4096, 8);
    pv (4096, 4, true, 512);
    pv (4096, 8, true, 512);
    pv (4096, 4, true, 1024);
    pv (4096, 4, false, 1024);
    pv (4096, 8, true, 1024);
    pv (8192, 8, true, 1024);
    pv (8192, 4, true, 2048);
    pv (8192, 8, true, 2048);

    ss (20, 5);
    ss (30, 7.5f);
    ss (40, 10);
    ss (40, 10, true);
    ss (60, 15);
    ss (85, 21);
    ss (85, 21, false, 8000);
    ss (120, 30);
    return list;
}

double semitonesToRatio (double semitones) { return std::exp2 (semitones / 12.0); }

struct RunResult
{
    Signal aligned;           // output with the reported latency removed, same length as the input
    double cpuPercent = 0;    // processing time / audio time
    double worstBlockPercent = 0; // slowest single block / block duration
};

/** Streams `input` through the engine in host-sized blocks, like a DAW would. */
RunResult runEngine (dsp::PitchShiftEngine& engine, const Signal& input, double ratio, int numChannels,
                     const std::function<int()>& nextBlockSize)
{
    engine.prepare (sampleRate, maxBlockSize, numChannels);
    engine.setPitchRatio ((float) ratio);
    engine.reset();

    const int latency = engine.getLatencySamples();
    const size_t total = input.size() + (size_t) latency;

    std::vector<std::vector<float>> in ((size_t) numChannels, std::vector<float> (maxBlockSize));
    std::vector<std::vector<float>> out ((size_t) numChannels, std::vector<float> (maxBlockSize));
    std::vector<const float*> inPtrs ((size_t) numChannels);
    std::vector<float*> outPtrs ((size_t) numChannels);

    Signal result (total);
    RunResult run;
    double totalSeconds = 0;

    for (size_t pos = 0; pos < total;)
    {
        const int n = (int) std::min<size_t> ((size_t) nextBlockSize(), total - pos);
        for (int c = 0; c < numChannels; ++c)
        {
            for (int i = 0; i < n; ++i)
                in[(size_t) c][(size_t) i] = pos + (size_t) i < input.size() ? input[pos + (size_t) i] : 0.0f;
            inPtrs[(size_t) c] = in[(size_t) c].data();
            outPtrs[(size_t) c] = out[(size_t) c].data();
        }

        const auto t0 = std::chrono::steady_clock::now();
        engine.process (inPtrs.data(), outPtrs.data(), numChannels, n);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

        totalSeconds += seconds;
        run.worstBlockPercent = std::max (run.worstBlockPercent, 100.0 * seconds / (n / sampleRate));
        std::copy (out[0].begin(), out[0].begin() + n, result.begin() + (long) pos);
        pos += (size_t) n;
    }

    run.cpuPercent = 100.0 * totalSeconds / (total / sampleRate);
    run.aligned.assign (result.begin() + latency, result.end());
    return run;
}

std::function<int()> fixedBlocks (int size) { return [size] { return size; }; }

std::function<int()> randomBlocks()
{
    auto state = std::make_shared<uint32_t> (12345u);
    return [state] {
        *state = *state * 1664525u + 1013904223u;
        return 1 + (int) ((*state >> 8) % (uint32_t) maxBlockSize);
    };
}

std::string fmt (const char* format, double v)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), format, v);
    return buffer;
}

std::string safeName (std::string_view name)
{
    std::string s (name);
    for (auto& ch : s)
        if (! std::isalnum ((unsigned char) ch))
            ch = '_';
    return s;
}

struct EngineReport
{
    std::string name, role;
    double reportedLatencyMs = 0, measuredLatencyMs = 0, impulseLatencyMs = 0;
    double cpuMono = 0, cpuStereo = 0, worstBlock = 0, worstBlock64 = 0, worstBlock32 = 0;
    double maxCentsError = 0, maxRippleDb = 0;
    double variableBlockDiff = 0;
    std::map<std::string, double> lsdByGroup;   // averaged over the key shifts
    std::map<int, double> lsdByShift;           // whole corpus
    std::map<int, double> residueByShift;       // steady treble residue on a low chord, dB re signal
    double hnrLowE7 = 0, hnrLowD5 = 0, fundLossLowE7 = 0, fundLossLowD5 = 0;
    TransientStats transients;                  // chug segments at -5
};

} // namespace

int main (int argc, char** argv)
{
    fs::path outDir = "engine-eval-out";
    std::string userInput;
    std::string audioMode = "key";
    std::string engineSet = "full";

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else if (arg == "--input" && i + 1 < argc)
            userInput = argv[++i];
        else if (arg == "--audio" && i + 1 < argc)
            audioMode = argv[++i];
        else if (arg == "--quick")
            engineSet = "quick";
        else if (arg == "--engines" && i + 1 < argc)
            engineSet = argv[++i];
        else
        {
            std::fprintf (stderr, "usage: %s [--out DIR] [--input di.wav] [--audio none|key|all] [--engines full|quick|shipping]\n", argv[0]);
            return 1;
        }
    }

    fs::create_directories (outDir / "audio");

    const std::vector<int> shifts = { -1, -2, -5, -7, -12, 2 };
    const std::vector<int> keyShifts = { -2, -5, -7 };
    auto isKeyShift = [&] (int s) { return std::find (keyShifts.begin(), keyShifts.end(), s) != keyShifts.end(); };
    auto shouldWriteAudio = [&] (int s) { return audioMode == "all" || (audioMode == "key" && isKeyShift (s)); };

    // ---- Test material ---------------------------------------------------
    std::printf ("Rendering corpus and references...\n");
    const auto corpus = makeCorpus();
    Signal dry = renderCorpus (corpus, sampleRate, 1.0, 1.0f);
    float peak = 0;
    for (float s : dry)
        peak = std::max (peak, std::abs (s));
    const float gain = 0.5f / peak; // -6 dBFS peak, like a healthy DI level
    for (auto& s : dry)
        s *= gain;

    std::map<int, Signal> references;
    for (int shift : shifts)
        references[shift] = renderCorpus (corpus, sampleRate, semitonesToRatio (shift), gain);

    writeWav24 ((outDir / "audio" / "input_DI.wav").string(), { dry }, sampleRate);
    for (int shift : shifts)
        if (shouldWriteAudio (shift))
            writeWav24 ((outDir / "audio" / ("reference_" + std::to_string (shift) + "st.wav")).string(),
                        { references[shift] }, sampleRate);

    AudioFile userFile;
    if (! userInput.empty())
    {
        std::string error;
        if (! readWav (userInput, userFile, error))
        {
            std::fprintf (stderr, "Could not read %s: %s\n", userInput.c_str(), error.c_str());
            return 1;
        }
        if (std::abs (userFile.sampleRate - sampleRate) > 1)
            std::fprintf (stderr, "Note: %s is %.0f Hz; it will be processed as if it were 48 kHz.\n",
                          userInput.c_str(), userFile.sampleRate);
    }

    const std::vector<double> sineFrequencies = { 82.41, 110.0, 440.0 };

    // ---- Run every engine ------------------------------------------------
    std::vector<EngineReport> reports;
    // A low power chord of pure sines: nothing above 2 kHz, so whatever an
    // engine puts up there is its own residue.
    Signal lowChord ((size_t) (3.0 * sampleRate));
    for (size_t i = 0; i < lowChord.size(); ++i)
    {
        const double t = (double) i / sampleRate;
        lowChord[i] = float (0.25 * (std::sin (2 * std::numbers::pi * 82.41 * t) + std::sin (2 * std::numbers::pi * 123.47 * t)
                                     + std::sin (2 * std::numbers::pi * 164.81 * t)));
    }
    auto residueDb = [] (const Signal& out) {
        const auto high = highPass (out, 2000.0, sampleRate);
        double h = 0, all = 0;
        for (size_t i = (size_t) (1.0 * sampleRate); i < (size_t) (2.8 * sampleRate); ++i)
        {
            h += (double) high[i] * high[i];
            all += (double) out[i] * out[i];
        }
        return 10.0 * std::log10 (h / all + 1e-30);
    };

    for (const auto& spec : makeEngineList (engineSet))
    {
        auto engine = spec.make();
        EngineReport report;
        report.name = std::string (engine->getName());
        report.role = spec.role;
        std::printf ("  %s\n", report.name.c_str());
        std::fflush (stdout);

        engine->prepare (sampleRate, maxBlockSize, 1);
        report.reportedLatencyMs = 1000.0 * engine->getLatencySamples() / sampleRate;

        {
            // Exact latency: where an impulse comes out at 0 semitones.
            Signal impulse ((size_t) sampleRate, 0.0f);
            impulse[(size_t) (0.25 * sampleRate)] = 1.0f;
            const auto run = runEngine (*engine, impulse, 1.0, 1, fixedBlocks (hostBlockSize));
            Signal raw ((size_t) engine->getLatencySamples(), 0.0f);
            raw.insert (raw.end(), run.aligned.begin(), run.aligned.end());
            size_t peakIndex = 0;
            for (size_t i = 0; i < raw.size(); ++i)
                if (std::abs (raw[i]) > std::abs (raw[peakIndex]))
                    peakIndex = i;
            report.impulseLatencyMs = 1000.0 * ((double) peakIndex - 0.25 * sampleRate) / sampleRate;
        }

        double lsdKeySum = 0;
        std::map<std::string, double> groupSums;
        std::map<std::string, int> groupCounts;

        for (int shift : shifts)
        {
            const double ratio = semitonesToRatio (shift);
            auto run = runEngine (*engine, dry, ratio, 1, fixedBlocks (hostBlockSize));
            const auto& reference = references[shift];

            if (shift == -5)
            {
                // Timing is noisy on a desktop machine: keep the best of three
                // runs (the least disturbed by other processes).
                for (int repeat = 0; repeat < 2; ++repeat)
                {
                    const auto again = runEngine (*engine, dry, ratio, 1, fixedBlocks (hostBlockSize));
                    run.cpuPercent = std::min (run.cpuPercent, again.cpuPercent);
                    run.worstBlockPercent = std::min (run.worstBlockPercent, again.worstBlockPercent);
                }
                // Measure on the raw (un-aligned) output: re-add the reported latency.
                Signal raw ((size_t) engine->getLatencySamples(), 0.0f);
                raw.insert (raw.end(), run.aligned.begin(), run.aligned.end());
                report.measuredLatencyMs = 1000.0 * measureLatencyByEnvelope (raw, reference, sampleRate, 0.4) / sampleRate;
                report.cpuMono = run.cpuPercent;
                report.worstBlock = run.worstBlockPercent;

                for (const auto& seg : corpus.segments)
                    if (seg.kind == Segment::Kind::chugs)
                    {
                        const auto t = transientStats (run.aligned, reference, seg, sampleRate);
                        report.transients.spreadMs += t.spreadMs / 3.0;
                        report.transients.preEchoDb += t.preEchoDb / 3.0;
                        report.transients.attackLossDb += t.attackLossDb / 3.0;
                    }
            }

            report.residueByShift[shift] = residueDb (runEngine (*engine, lowChord, ratio, 1, fixedBlocks (hostBlockSize)).aligned);
            report.lsdByShift[shift] = logSpectralDistance (run.aligned, reference, 0.0, corpus.length, sampleRate);

            if (isKeyShift (shift))
            {
                lsdKeySum += report.lsdByShift[shift];
                for (const auto& seg : corpus.segments)
                {
                    const char* group = seg.kind == Segment::Kind::chord ? "chords"
                                        : seg.kind == Segment::Kind::chugs ? "chugs"
                                        : seg.kind == Segment::Kind::melody ? "riff" : "sustain";
                    groupSums[group] += logSpectralDistance (run.aligned, reference, seg.start, seg.end, sampleRate);
                    groupCounts[group]++;
                }
            }

            for (const auto& seg : corpus.segments)
            {
                if (seg.kind != Segment::Kind::sustain)
                    continue;
                const bool isLowE = seg.fundamental > 80.0;
                if ((isLowE && shift == -7) || (! isLowE && shift == -5))
                {
                    const double f0 = seg.fundamental * ratio;
                    const auto out = harmonicStats (run.aligned, seg.start + 0.4, 1.5, f0, true, sampleRate);
                    const auto ref = harmonicStats (reference, seg.start + 0.4, 1.5, f0, true, sampleRate);
                    (isLowE ? report.hnrLowE7 : report.hnrLowD5) = out.hnrDb;
                    (isLowE ? report.fundLossLowE7 : report.fundLossLowD5) = out.fundamentalDb - ref.fundamentalDb;
                }
            }

            if (shouldWriteAudio (shift))
                writeWav24 ((outDir / "audio" / (std::to_string (shift) + "st_" + safeName (report.name) + ".wav")).string(),
                            { run.aligned }, sampleRate);

            // Sines: pitch accuracy and amplitude stability.
            for (double f : sineFrequencies)
            {
                const auto sine = makeSine (f, 3.0, sampleRate);
                const auto sineRun = runEngine (*engine, sine, ratio, 1, fixedBlocks (hostBlockSize));
                const auto start = (size_t) (1.0 * sampleRate), length = (size_t) (1.5 * sampleRate);
                const double measured = peakFrequency (sineRun.aligned, start, length, sampleRate);
                const double cents = 1200.0 * std::log2 (measured / (f * ratio));
                report.maxCentsError = std::max (report.maxCentsError, std::abs (cents));
                report.maxRippleDb = std::max (report.maxRippleDb, envelopeRippleDb (sineRun.aligned, start, length, sampleRate));
            }
        }

        for (auto& [group, sum] : groupSums)
            report.lsdByGroup[group] = sum / groupCounts[group];
        report.lsdByGroup["all"] = lsdKeySum / (double) keyShifts.size();

        // Small host buffers: the worst block is what causes dropouts.
        report.worstBlock64 = report.worstBlock32 = 1e9;
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            report.worstBlock64 = std::min (report.worstBlock64, runEngine (*engine, dry, semitonesToRatio (-5), 1, fixedBlocks (64)).worstBlockPercent);
            report.worstBlock32 = std::min (report.worstBlock32, runEngine (*engine, dry, semitonesToRatio (-5), 1, fixedBlocks (32)).worstBlockPercent);
        }

        // Stereo cost (both channels carry signal, no mono shortcut).
        report.cpuStereo = 1e9;
        for (int repeat = 0; repeat < 3; ++repeat)
            report.cpuStereo = std::min (report.cpuStereo,
                                         runEngine (*engine, dry, semitonesToRatio (-5), 2, fixedBlocks (hostBlockSize)).cpuPercent);

        // Variable host block sizes must not change the result.
        {
            const auto a = runEngine (*engine, dry, semitonesToRatio (-5), 1, fixedBlocks (hostBlockSize));
            const auto b = runEngine (*engine, dry, semitonesToRatio (-5), 1, randomBlocks());
            for (size_t i = 0; i < a.aligned.size(); ++i)
                report.variableBlockDiff = std::max (report.variableBlockDiff, (double) std::abs (a.aligned[i] - b.aligned[i]));
        }

        if (! userInput.empty())
            for (int shift : keyShifts)
            {
                const auto run = runEngine (*engine, userFile.channels[0], semitonesToRatio (shift), 1, fixedBlocks (hostBlockSize));
                writeWav24 ((outDir / "audio" / ("user_" + std::to_string (shift) + "st_" + safeName (report.name) + ".wav")).string(),
                            { run.aligned }, sampleRate);
            }

        reports.push_back (report);
    }

    // ---- Report ------------------------------------------------------------
    std::ostringstream md;
    md << "# LowChug engine evaluation\n\n"
       << "48 kHz, host block " << hostBlockSize << " samples, mono unless noted. "
       << "Corpus: synthetic DI (" << fmt ("%.1f", corpus.length) << " s), scored against an ideal shifted rendering.\n\n";

    md << "## Latency and CPU\n\n"
       << "| Engine | Reported latency | Impulse (0 st) | Envelope (-5 st) | CPU mono | CPU stereo | Worst block 128 / 64 / 32 | Variable-block diff |\n"
       << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : reports)
        md << "| " << r.name << " | " << fmt ("%.1f ms", r.reportedLatencyMs) << " | " << fmt ("%.1f ms", r.impulseLatencyMs)
           << " | " << fmt ("%.1f ms", r.measuredLatencyMs)
           << " | " << fmt ("%.2f%%", r.cpuMono) << " | " << fmt ("%.2f%%", r.cpuStereo) << " | "
           << fmt ("%.0f%%", r.worstBlock) << " / " << fmt ("%.0f%%", r.worstBlock64) << " / " << fmt ("%.0f%%", r.worstBlock32)
           << " | " << fmt ("%.1e", r.variableBlockDiff) << " |\n";

    md << "\nImpulse latency is exact; the envelope measure (onset cross-correlation on shifted material) is approximate. "
       << "Variable-block diff = peak difference between 128-sample and random 1-512-sample host blocks (0 = buffer-size independent).\n"
       << "CPU = processing time / audio time on one core. Worst block = slowest single "
       << hostBlockSize << "-sample block as a share of its " << fmt ("%.2f", 1000.0 * hostBlockSize / sampleRate)
       << " ms deadline. Both are the best of three runs.\n\n";

    md << "## Quality\n\n"
       << "Log-spectral distance to the ideal shift (dB, lower is better), averaged over -2/-5/-7 st.\n\n"
       << "| Engine | All | Chords | Chugs | Riff | Sustain | Max pitch error | Sine ripple |\n"
       << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : reports)
        md << "| " << r.name << " | " << fmt ("%.2f", r.lsdByGroup.at ("all")) << " | " << fmt ("%.2f", r.lsdByGroup.at ("chords"))
           << " | " << fmt ("%.2f", r.lsdByGroup.at ("chugs")) << " | " << fmt ("%.2f", r.lsdByGroup.at ("riff")) << " | "
           << fmt ("%.2f", r.lsdByGroup.at ("sustain")) << " | " << fmt ("%.2f c", r.maxCentsError) << " | "
           << fmt ("%.2f dB", r.maxRippleDb) << " |\n";

    md << "\nMax pitch error: worst |cents| over 82.4/110/440 Hz sines at every shift. "
       << "Sine ripple: worst peak-to-trough of the 10 ms RMS envelope (0 dB = perfectly steady; warble shows up here).\n\n";

    md << "### Per shift (whole corpus LSD, dB)\n\n| Engine |";
    for (int s : shifts)
        md << " " << s << " st |";
    md << "\n|---|";
    for (size_t i = 0; i < shifts.size(); ++i)
        md << "---|";
    md << "\n";
    for (const auto& r : reports)
    {
        md << "| " << r.name << " |";
        for (int s : shifts)
            md << " " << fmt ("%.2f", r.lsdByShift.at (s)) << " |";
        md << "\n";
    }

    md << "\n### Treble residue per shift (dB above 2 kHz re signal, low sine chord, lower = cleaner)\n\n| Engine |";
    for (int s : shifts)
        md << " " << s << " st |";
    md << "\n|---|";
    for (size_t i = 0; i < shifts.size(); ++i)
        md << "---|";
    md << "\n";
    for (const auto& r : reports)
    {
        md << "| " << r.name << " |";
        for (int s : shifts)
            md << " " << fmt ("%.1f", r.residueByShift.at (s)) << " |";
        md << "\n";
    }

    {
        const auto& lowE = corpus.segments.front();
        const auto refE = harmonicStats (references[-7], lowE.start + 0.4, 1.5, lowE.fundamental * semitonesToRatio (-7), true, sampleRate);
        const Segment* lowD = nullptr;
        for (const auto& s : corpus.segments)
            if (s.kind == Segment::Kind::sustain && s.fundamental < 80.0)
                lowD = &s;
        const auto refD = harmonicStats (references[-5], lowD->start + 0.4, 1.5, lowD->fundamental * semitonesToRatio (-5), true, sampleRate);

        md << "\n## Low end\n\n"
           << "Harmonic-to-noise ratio below 1 kHz on a sustained note (higher is cleaner; energy between harmonics is the "
           << "\"mud/underwater\" smear). Ideal: low E at -7 = " << fmt ("%.1f dB", refE.hnrDb)
           << ", low D at -5 = " << fmt ("%.1f dB", refD.hnrDb) << ".\n\n"
           << "| Engine | Low E -7 st (55 Hz) HNR | Fundamental vs ideal | Low D -5 st (49 Hz) HNR | Fundamental vs ideal |\n"
           << "|---|---|---|---|---|\n";
        for (const auto& r : reports)
            md << "| " << r.name << " | " << fmt ("%.1f dB", r.hnrLowE7) << " | " << fmt ("%+.1f dB", r.fundLossLowE7) << " | "
               << fmt ("%.1f dB", r.hnrLowD5) << " | " << fmt ("%+.1f dB", r.fundLossLowD5) << " |\n";
    }

    md << "\n## Pick attack (chug segments, -5 st, measured above 2 kHz)\n\n"
       << "Onset spread: time spread of the envelope rise around each pick (lower = tighter). "
       << "Pre-echo: energy in the 18 ms before each pick relative to the 18 ms after it (lower = cleaner). "
       << "Attack level: peak of the attack relative to the ideal.\n\n"
       << "| Engine | Onset spread | Pre-echo (re attack) | Attack level vs ideal |\n|---|---|---|---|\n";
    {
        TransientStats ideal;
        for (const auto& seg : corpus.segments)
            if (seg.kind == Segment::Kind::chugs)
            {
                const auto t = transientStats (references[-5], references[-5], seg, sampleRate);
                ideal.spreadMs += t.spreadMs / 3.0;
                ideal.preEchoDb += t.preEchoDb / 3.0;
            }
        md << "| *Ideal* | " << fmt ("%.2f ms", ideal.spreadMs) << " | " << fmt ("%.1f dB", ideal.preEchoDb) << " | +0.0 dB |\n";
    }
    for (const auto& r : reports)
        md << "| " << r.name << " | " << fmt ("%.2f ms", r.transients.spreadMs) << " | " << fmt ("%.1f dB", r.transients.preEchoDb)
           << " | " << fmt ("%+.1f dB", r.transients.attackLossDb) << " |\n";

    const auto reportText = md.str();
    std::printf ("\n%s", reportText.c_str());
    std::ofstream (outDir / "report.md") << reportText;
    std::printf ("\nReport: %s\nAudio:  %s\n", (outDir / "report.md").string().c_str(), (outDir / "audio").string().c_str());
    return 0;
}
