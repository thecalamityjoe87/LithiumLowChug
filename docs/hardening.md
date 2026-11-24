# Hardening (milestone 5)

Validation, CPU profiling and test coverage for Lithium LowChug, and the bugs they found. The manual host checks are in [host-checklist.md](host-checklist.md). They were run in Lithium Studio on the milestone 5 build and passed: Live/Studio switches with their latency, session save and reload, and automation of Mode, Formant and Tight. A listening check through the TONE3000 amp sim also sounded fine.

## Validation

| Tool | Format | Result |
|---|---|---|
| pluginval 1.0.4, strictness 10 | VST3 | **pass** (after the fix below) |
| pluginval 1.0.4, strictness 10 | LV2 | **pass** |
| Steinberg VST3 validator (VST3 SDK 3.8.1) | VST3 | **pass**: 537 tests, 0 failed |

```bash
pluginval --strictness-level 10 --validate "$HOME/.vst3/Lithium LowChug.vst3"
pluginval --strictness-level 10 --validate "$HOME/.lv2/Lithium LowChug.lv2"
vst3-validator -e "$HOME/.vst3/Lithium LowChug.vst3"
```

At strictness 10, pluginval runs every test at 44.1, 48 and 96 kHz with 64 to 1024-sample blocks: processing, automation from the audio thread and from the editor, state save and restore, opening and closing the editor, and fuzzed parameter values.

## Bugs found and fixed

1. **Switches weren't fully restored by a saved state (found by pluginval).** pluginval saves the state, moves every parameter to an arbitrary value, restores the state, and checks that every parameter reads its old value again. Formant came back as 0.13 instead of 0. A JUCE `AudioParameterBool` stores the raw value a host sends (0.13 reads as "off"), and the APVTS's `replaceState()` skips parameters whose value already *reads* the same, so the raw 0.13 survived. Bypass had the same bug; pluginval doesn't check the bypass parameter. **Fix:** `setStateInformation()` pushes the restored value again into any parameter whose raw value disagrees with it. Test: "State restore puts every parameter back, even after a host sent in-between values".
2. **One NaN switched pre-echo protection off for good.** The onset detector's 2 kHz high-pass is a recursive filter, so a single NaN in the input (from a host or a broken plugin earlier in the chain) stayed in it forever. The output stayed finite, but no onset was ever found again, and pre-echo came back (−42 dB in Live, −27 dB in Studio, against none). **Fix:** the engine treats non-finite input samples as silence, in the analysis and in the detector. Tests: the "0.7 s after a NaN" pre-echo checks in DspTests, and the hostile-input checks in CheckedDspTests.
3. **Bypass faded with a faint tick.** Its 20 ms fade was a linear ramp, and the ramp's corners left a tick at about −70 dB, 16 dB above Studio's steady residue. **Fix:** the fade follows a smoothstep curve, like the mode and unity fades (still exact at 0 and 1, so bypass stays bit-transparent). Mix keeps its linear ramp: it is a continuous control, and a curve would bend its law. A full 100 → 0 % Mix jump still leaves the −70 dB tick (see "Known limits").

## CPU

Measured with the profiler, which runs the whole effect (`dsp::Detuner`, as the plugin runs it) over the 21.9 s synthetic DI corpus at −5 st: chords, palm-muted chugs, single notes and low sustains. Each average is the best of three runs, and each worst block the best of five, on one core of this laptop (Ryzen 5 PRO 8540U, power-saving governor). Run it on an otherwise idle machine: with an IDE indexing in the background, the worst blocks doubled (one run showed 128 % for Live at 32 samples; repeated on an idle machine it is a steady 27 %).

```bash
./build/release/tools/profile/lowchug-profile          # average CPU, worst blocks
./build/release/tools/profile/lowchug-profile-stages   # where the time goes
```

"True stereo" is a double-tracked take: the second channel is the same playing 7 ms later and 2 dB quieter, so the channels never match. Identical channels (a mono guitar on a stereo track) are shifted once and copied, so they cost the same as mono.

One engine change came out of this. The phase advance that gives each partial's true frequency was computed with an `atan2` for every bin of every frame (2048 in Live, 4096 in Studio), though only peak bins use it. It is now computed once per peak. The quality tables of the engine evaluation are identical to the last digit, and the engines cost 20 to 25 % less (harness, back to back: Live 2.46 → 1.95 %, Studio 4.03 → 3.03 %, mono).

### Average CPU, 48 kHz, 128-sample blocks

**Live stays under the ~3 % budget: 2.10 % mono with Formant on.**

| Mode | Formant | mono | stereo, identical channels | true stereo |
|---|---|---|---|---|
| Live | off | 1.97 % | 1.99 % | 2.96 % |
| Live | on | 2.10 % | 2.12 % | 3.10 % |
| Studio | off | 3.46 % | 3.50 % | 5.98 % |
| Studio | on | 3.72 % | 3.75 % | 6.24 % |

### Other sample rates (mono, Formant on)

| Mode | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Live | 1.93 % | 2.14 % | 4.14 % |
| Studio | 3.37 % | 3.75 % | 7.19 % |

The FFT sizes double at 88.2 kHz and above (to keep the windows the same length in milliseconds), so CPU roughly doubles at 96 kHz.

### Worst block

The slowest single block of the whole corpus, as a share of the time that block may take (its duration). Over 100 % would be a dropout.

| Mode | Channels | 128 | 64 | 32 |
|---|---|---|---|---|
| Live | mono | 7 % | 13 % | 27 % |
| Live | true stereo | 9 % | 19 % | 36 % |
| Studio | mono | 6 % | 12 % | 24 % |
| Studio | true stereo | 12 % | 12 % | 23 % |

Live does each frame's work in the block where the frame completes (one frame every 256 samples), so at small buffers the worst block is a frame's whole cost. Studio spreads each frame over the following hop (split computation), so its worst block grows less as buffers shrink. These figures vary by a few points between runs.

While switching modes, the incoming engine warms up for about a quarter of a second and both engines run. This is the heaviest the plugin gets:

| 128 | 64 | 32 | average CPU |
|---|---|---|---|
| 21 % | 27 % | 43 % | 6.47 % |

### Where the time goes

Exclusive time per stage (nested stages aren't counted twice), Formant on, 48 kHz / 128. The stage timers exist only in the `lowchug-profile-stages` build (`src/dsp/Profiling.h`). They compile to nothing in the plugin, and add 2 to 3 % to the totals.

Each cell: % of one core (share of that configuration's total).

| Stage | Live, mono (2.16 %) | Studio, mono (3.79 %) | Live, true stereo (3.20 %) | Studio, true stereo (6.29 %) |
|---|---|---|---|---|
| Detuner (delays, Tight, mix) | 0.06 % (2.6 %) | 0.06 % (1.6 %) | 0.09 % (2.9 %) | 0.10 % (1.5 %) |
| buffering, windowing | 0.05 % (2.2 %) | 0.08 % (2.2 %) | 0.08 % (2.6 %) | 0.15 % (2.4 %) |
| onset detector | 0.03 % (1.6 %) | 0.03 % (0.9 %) | 0.04 % (1.2 %) | 0.04 % (0.6 %) |
| forward FFT | 0.19 % (9.0 %) | 0.84 % (22.2 %) | 0.37 % (11.7 %) | 1.50 % (23.9 %) |
| analysis (peaks, regions) | 0.26 % (12.0 %) | 0.49 % (12.8 %) | 0.27 % (8.3 %) | 0.50 % (7.9 %) |
| formant envelope | 0.11 % (5.0 %) | 0.22 % (5.7 %) | 0.11 % (3.4 %) | 0.22 % (3.4 %) |
| partial model | 0.81 % (37.7 %) | 0.43 % (11.5 %) | 0.95 % (29.6 %) | 0.57 % (9.1 %) |
| region shifting | 0.44 % (20.6 %) | 0.91 % (23.9 %) | 0.90 % (28.1 %) | 1.82 % (28.9 %) |
| inverse FFT | 0.19 % (8.9 %) | 0.71 % (18.8 %) | 0.37 % (11.6 %) | 1.36 % (21.6 %) |
| overlap-add | 0.01 % (0.4 %) | 0.02 % (0.5 %) | 0.02 % (0.6 %) | 0.04 % (0.6 %) |

- **Live:** the partial model is the largest single cost: up to 48 low partials, with their overlaps solved and their lobes drawn twice per channel per frame. Then region shifting, then the two FFTs.
- **Studio:** the 8192-point FFTs (forward and inverse) take about 40 %, and region shifting most of the rest. Studio has the same hop as Live (256 samples, so the same number of frames per second) but twice the bins per frame: per-bin work like region shifting costs about twice Live's, and the FFTs about four times (bigger transforms, and out of the fastest cache). The partial model costs less than in Live: it covers partials below bin 128, which is 1.5 kHz in Live but 750 Hz in Studio, so Studio models fewer partials, and its coupling work grows with the square of their number.
- **True stereo** adds a forward FFT, region shifting, the partial model's lobes and an inverse FFT per extra channel. The analysis is shared: all channels are analyzed once, from their average.
- The Detuner around the engines (dry delays, Tight's filters, mix, gain, mode crossfade), the onset detector and buffering are 5 to 7 % together.

## Tests

`ctest --test-dir build/release` runs three programs:

| Program | What | Time |
|---|---|---|
| `lowchug-dsp-tests` | the DSP core, no JUCE: 110 checks | ~32 s |
| `lowchug-dsp-checked-tests` | stress tests of the DSP code with the standard library's bounds checks on: 35 checks | ~10 s |
| `lowchug-plugin-tests` | the JUCE processor, presets, state, threading and editor: 38 tests | ~2 s |

The original brief's Testing list isn't in the repository, so this audit maps the tests to the goals of milestones 1 to 5 instead.

| Goal | Covered by |
|---|---|
| Pitch within ±2 cents, both modes, 44.1 / 48 / 96 kHz | DspTests: pitch accuracy |
| Reported latency = real latency, every mode, rate and state | DspTests: latency, shifted path latency; PluginTests: latency in both modes and at 96 kHz, presets report Studio latency, latency reported across threads |
| Output independent of the host's buffer size | DspTests: random 1–512-sample blocks give bit-identical output; PluginTests: odd block sizes including more than the prepared maximum |
| 0 st, Mix 0 % and bypass give the delayed input exactly | DspTests: nulls at 0 st (three rates), dry states; PluginTests: both modes, bypass parameter, host bypass |
| No clicks when anything moves | DspTests: Shift and Fine automation, mode switches (five patterns, three shifts), Tight, **Formant, Bypass and Mix switching** |
| Stereo: linking, mono in / stereo out, a guitar interface's silent second input | DspTests: channel linking; PluginTests: mono in stereo out, Drop D → Drop G on input 1 |
| Quality: treble residue, formant, Tight, pre-echo | DspTests: treble residue, formant, Tight, no pre-echo (**also after a NaN**) |
| Tuner and visualizer | DspTests: pitch detector; PluginTests: ShiftAnalyzer on chords through the processor, tuner, visualizer |
| Tunings, presets, programs | PluginTests: tunings, 18 programs, Studio presets select Studio |
| State | PluginTests: round trip, garbage ignored, **every parameter restored after in-between host values** |
| Threading | PluginTests: program changes and mode latency from other threads |
| Editor | PluginTests: opens and closes without leaking, Quality controls, preset box and panel, dial (named tunings and Custom shifts) |
| Memory safety | CheckedDspTests (**new**, every build); the ASan and ASan/UBSan builds below |
| Hostile input: NaN, infinity, DC, full-scale noise, denormals | CheckedDspTests (**new**) |
| CPU | DspTests prints CPU; the profiler (**new**) |
| Host behavior | pluginval, the VST3 validator, and [host-checklist.md](host-checklist.md) (by hand; passed in Lithium Studio) |

New in milestone 5 (in bold above): the state restore test (bug 1), the NaN checks (bug 2), the Formant, Bypass and Mix click tests (bug 3), and CheckedDspTests.

### The stale-data regression test

Milestone 3 had a bug in which a frame of sound followed by a frame of silence reused the sound frame's list of modeled partials, indexing a region list that was now empty. The stale memory it read happened to be harmless, so the audio was fine, and only AddressSanitizer saw it.

`lowchug-dsp-checked-tests` compiles its own copy of the DSP sources with `_GLIBCXX_ASSERTIONS` (libstdc++) and `_LIBCPP_HARDENING_MODE` (libc++), which bounds-check every `std::vector` access even in an optimized build. It plays sound, silence longer than any window, short bursts, lone clicks and very quiet notes through both modes, at three sample rates, mono and stereo, Formant on and off. With the milestone 3 fix (`modeled.clear()` in `computeRegionShifts`) taken out, it stops at the first out-of-range read:

```
stl_vector.h:1263: ... vector<PhaseVocoderEngine::Region>::operator[] ...: Assertion '__n < this->size()' failed.
```

So that class of bug is now caught by every `ctest` run, not only by the sanitizer builds. (MSVC has no equivalent switch that is safe to mix with release libraries, so on Windows only the sanitizer builds catch it.)

### Sanitizers

Both clean after the milestone 5 changes:

- `build/asan`: Debug + AddressSanitizer: the plugin tests and the checked DSP tests.
- `build/asan-dsp`: RelWithDebInfo + AddressSanitizer and UndefinedBehaviorSanitizer (halting on the first error): the DSP tests and the checked DSP tests.

## Known limits

- **Mix jumps:** a full 100 → 0 % jump in Mix leaves a tick at about −70 dB (relative to the signal), from its linear 20 ms ramp. Continuous Mix automation, Bypass and every other control are click-free.
- The limits documented in milestone 3 remain: treble smear from fast palm-mute decays in long windows, Live's low power-chord residue (about −62 dB at worst), and Studio's first 5 ms after silence coming out about 8 dB soft.
- **Not tested here:** AU (no Mac), Windows, and hosts other than Lithium Studio.
