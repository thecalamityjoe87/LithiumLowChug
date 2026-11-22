#pragma once

// A synthetic DI guitar performance with exact ground truth.
//
// Every note is an additive plucked string (inharmonic partials, pluck-position
// comb, frequency-dependent decay, 1.5 ms pick attack). Because the model is
// explicit, the IDEAL pitch-shifted performance can be rendered too: same
// notes, same envelopes, every partial frequency multiplied by the ratio. An
// engine's output is scored against that reference.

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace lowchug::eval
{

struct Note
{
    double start, duration, f0;
    float velocity;
    bool palmMute;
    bool wound; // wound strings are stiffer: more inharmonicity
};

struct Segment
{
    enum class Kind { sustain, melody, chugs, chord };
    std::string name;
    Kind kind;
    double start, end;
    std::vector<double> onsets;
    double fundamental = 0; // sustain segments: the note's f0, for harmonic analysis
};

struct Corpus
{
    std::vector<Note> notes;
    std::vector<Segment> segments;
    double length = 0;
};

namespace pitch
{
inline double fromMidi (int midi) { return 440.0 * std::exp2 ((midi - 69) / 12.0); }
constexpr int D2 = 38, E2 = 40, G2 = 43, A2 = 45, As2 = 46, B2 = 47, D3 = 50, E3 = 52, G3 = 55, Gs3 = 56, B3 = 59, E4 = 64;
} // namespace pitch

inline Corpus makeCorpus()
{
    using namespace pitch;
    Corpus c;
    double t = 0.5;

    auto addNote = [&] (double start, double duration, int midi, float velocity, bool mute) {
        c.notes.push_back ({ start, duration, fromMidi (midi), velocity, mute, midi < G3 });
    };
    auto addChord = [&] (Segment& seg, double start, double duration, std::initializer_list<int> midis,
                         float velocity, bool mute, double strumSpacing) {
        double s = start;
        for (int m : midis)
        {
            addNote (s, duration, m, velocity, mute);
            s += strumSpacing;
        }
        seg.onsets.push_back (start);
    };

    { // Open low E ringing out: low-end cleanliness at big shifts.
        Segment seg { "low E sustain", Segment::Kind::sustain, t, t + 3.0, { t }, fromMidi (E2) };
        addNote (t, 3.0, E2, 0.9f, false);
        c.segments.push_back (seg);
        t += 3.5;
    }
    { // Single-note riff, 8th notes.
        Segment seg { "single-note riff", Segment::Kind::melody, t, 0, {} };
        for (int m : { E2, G2, A2, As2, B2, D3, E3, G3 })
        {
            addNote (t, 0.21, m, 0.8f, false);
            seg.onsets.push_back (t);
            t += 0.22;
        }
        seg.end = t;
        c.segments.push_back (seg);
        t += 0.5;
    }
    { // Palm-muted 16ths on the low E: the "chug" the plugin is named for.
        Segment seg { "palm-mute chugs (E)", Segment::Kind::chugs, t, 0, {} };
        const double sixteenth = 60.0 / 160.0 / 4.0;
        for (int i = 0; i < 16; ++i)
        {
            addNote (t, sixteenth * 0.95, E2, i % 4 == 0 ? 1.0f : 0.75f, true);
            seg.onsets.push_back (t);
            t += sixteenth;
        }
        seg.end = t;
        c.segments.push_back (seg);
        t += 0.5;
    }
    {
        Segment seg { "power chord E5", Segment::Kind::chord, t, t + 2.5, {} };
        addChord (seg, t, 2.5, { E2, B2, E3 }, 0.85f, false, 0.006);
        c.segments.push_back (seg);
        t += 3.0;
    }
    { // Six-string open chord: the polyphonic "no warble" test.
        Segment seg { "open E major chord", Segment::Kind::chord, t, t + 3.0, {} };
        addChord (seg, t, 3.0, { E2, B2, E3, Gs3, B3, E4 }, 0.8f, false, 0.010);
        c.segments.push_back (seg);
        t += 3.5;
    }
    {
        Segment seg { "power-chord chugs", Segment::Kind::chugs, t, 0, {} };
        const double sixteenth = 60.0 / 160.0 / 4.0;
        for (int i = 0; i < 12; ++i)
        {
            addChord (seg, t, sixteenth * 0.95, { E2, B2, E3 }, i % 4 == 0 ? 1.0f : 0.75f, true, 0.003);
            t += sixteenth;
        }
        addChord (seg, t, 0.8, { E2, B2, E3 }, 1.0f, false, 0.006);
        t += 0.8;
        seg.end = t;
        c.segments.push_back (seg);
        t += 0.5;
    }
    { // Drop D low string: -5 makes it Drop A, a ~49 Hz fundamental.
        Segment seg { "low D sustain (Drop D)", Segment::Kind::sustain, t, t + 2.5, { t }, fromMidi (D2) };
        addNote (t, 2.5, D2, 0.9f, false);
        c.segments.push_back (seg);
        t += 3.0;
    }
    {
        Segment seg { "Drop D chugs", Segment::Kind::chugs, t, 0, {} };
        const double sixteenth = 60.0 / 160.0 / 4.0;
        for (int i = 0; i < 12; ++i)
        {
            addChord (seg, t, sixteenth * 0.95, { D2, A2, D3 }, i % 3 == 0 ? 1.0f : 0.75f, true, 0.003);
            t += sixteenth;
        }
        seg.end = t;
        c.segments.push_back (seg);
        t += 0.6;
    }

    c.length = t;
    return c;
}

/** Renders the corpus with every partial frequency multiplied by `ratio`
    (ratio = 1 is the DI input; anything else is the ideal shifted output). */
inline std::vector<float> renderCorpus (const Corpus& corpus, double sampleRate, double ratio, float gain)
{
    std::vector<float> out ((size_t) std::ceil (corpus.length * sampleRate), 0.0f);
    const double nyquistLimit = std::min (12000.0, 0.45 * sampleRate);
    const double twoPi = 2.0 * std::numbers::pi;

    for (const auto& note : corpus.notes)
    {
        const double inharmonicity = note.wound ? 1.2e-4 : 4.0e-5;
        const double pluckPosition = 0.13; // fraction of string length from the bridge
        const double tau1 = note.palmMute ? 0.10 : 2.8;
        const auto startSample = (size_t) std::lround (note.start * sampleRate);
        const double attack = 0.0015, release = 0.015;
        const auto length = (size_t) std::lround ((note.duration + release) * sampleRate);

        for (int n = 1;; ++n)
        {
            // Partial frequency and amplitude are defined on the UNSHIFTED
            // string, so the reference keeps the same partial set and envelopes.
            const double fn = n * note.f0 * std::sqrt (1.0 + inharmonicity * n * n);
            if (fn > nyquistLimit)
                break;

            double amp = note.velocity / n * std::abs (std::sin (std::numbers::pi * n * pluckPosition)) + 0.02 / n;
            if (note.palmMute)
                amp /= 1.0 + std::pow (fn / 700.0, 2.0);
            const double tau = tau1 / (1.0 + std::pow (fn / 600.0, 1.6));
            const double omega = twoPi * fn * ratio / sampleRate;

            // Recursive oscillator and decay (a rotating phasor and a constant
            // multiplier) instead of cos()/exp() per sample: far faster, and
            // the drift over a few seconds is negligible in double precision.
            double re = 1.0, im = 0.0;
            const double rotRe = std::cos (omega), rotIm = std::sin (omega);
            double decay = amp;
            const double decayStep = std::exp (-1.0 / (tau * sampleRate));
            const size_t attackSamples = (size_t) (attack * sampleRate);
            const size_t releaseStart = (size_t) (note.duration * sampleRate);

            for (size_t i = 0; i < length && startSample + i < out.size(); ++i)
            {
                double env = decay;
                if (i < attackSamples)
                    env *= 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / (double) attackSamples);
                else if (i > releaseStart)
                    env *= 0.5 + 0.5 * std::cos (std::numbers::pi * std::min (1.0, (double) (i - releaseStart) / (release * sampleRate)));
                out[startSample + i] += float (env * re);

                const double nextRe = re * rotRe - im * rotIm;
                im = re * rotIm + im * rotRe;
                re = nextRe;
                decay *= decayStep;
            }
        }
    }

    for (auto& s : out)
        s *= gain;
    return out;
}

/** A steady sine with a short fade-in, for pitch-accuracy tests. */
inline std::vector<float> makeSine (double frequency, double seconds, double sampleRate)
{
    std::vector<float> out ((size_t) (seconds * sampleRate));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double time = (double) i / sampleRate;
        const double fade = std::min (1.0, time / 0.005);
        out[i] = float (0.5 * fade * std::sin (2.0 * std::numbers::pi * frequency * time));
    }
    return out;
}

} // namespace lowchug::eval
