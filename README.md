# Lithium LowChug – Guitar Detuner

Play in standard (or Drop D) and hear your guitar tuned lower: E♭ standard, D standard, Drop C, B standard, an octave down. Part of the Lithium family alongside Lithium Drums and Lithium Studio. LowChug is a polyphonic pitch shifter that shifts the whole guitar, chords included, built to track through live.

Formats: VST3, LV2 and Standalone on Linux/Windows/macOS, plus AU on macOS. Made with JUCE.

## Status

| Milestone | State |
|---|---|
| 1. Skeleton and engine evaluation | done: see [docs/engine-evaluation.md](docs/engine-evaluation.md) |
| 2. Core DSP | done |
| 3. Quality features (modes, formants, transients, tight) | done: see the milestone 3 section of the engine evaluation |
| 4. UI and presets | done (ahead of 3, so the plugin could be play-tested): UI, factory presets, tuner |
| 5. Hardening (pluginval, hosts, profiling) | next |

Two modes, switched with the Live | Studio buttons or the presets. Live has 1024 samples (21.3 ms) of latency at 48 kHz, for playing through; Studio has 2304 samples (48.0 ms) and the cleanest low end, for recording and mixing. Hosts are told the new latency when you switch, and the sound crossfades once the other engine has warmed up (about a quarter of a second). Formant keeps the guitar's tone in place as the pitch drops (off is the classic pitch pedal sound); Tight is a gentle low cut that follows the shift.

The editor (src/ui) has the Lithium family look (Lithium Drums' colors, fonts and knob). You pick how your guitar is tuned with the Playing in buttons and the tuning you want with the "Tune down to" dial, whose notches are the tunings reachable from yours; each card has a "?" that explains it. The header's preset box holds the factory presets (Eb, D, C#, C and B Standard, Drop C, B and A, Octave Down, each in a Live and a Studio version; hosts list the same 18 as programs); the name gets a "*" once you change a setting it covers. The right card has two tabs. TUNER tunes the guitar itself (before the shift) to the tuning you play in. YOU HEAR includes a visualizer: input and output spectra with the open strings of the tuning you hear marked, and the shift actually measured between input and output (it compares the two spectra on a log-frequency axis, so it works on chords), with how many cents it is from what the controls ask for.

## Building

You need CMake 3.25 or newer, a C++20 compiler and Ninja (recommended). On Linux you also need JUCE's usual dependencies (ALSA, FreeType, X11, …). JUCE and the Signalsmith libraries are fetched automatically at configure time.

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Plugins end up in `build/release/src/plugin/LithiumLowChug_artefacts/Release/`. Add `-DLOWCHUG_COPY_PLUGINS=ON` to also copy them into your user plugin folders (on Linux `~/.vst3/Lithium LowChug.vst3` and `~/.lv2/Lithium LowChug.lv2`).

The Standalone app is `build/release/src/plugin/LithiumLowChug_artefacts/Release/Standalone/Lithium LowChug`. It takes a mono input (choose your guitar's input under Options > Audio/MIDI Settings) and plays the result on both outputs.

To see the editor without a host, the plugin tests can save PNG snapshots of it:

```bash
LOWCHUG_SNAPSHOT_DIR=/tmp ./build/release/tests/lowchug-plugin-tests
```

[pluginval](https://github.com/Tracktion/pluginval) passes at strictness 5 for the VST3 and LV2:

```bash
pluginval --strictness-level 5 --validate "$HOME/.vst3/Lithium LowChug.vst3"
```

| Option | Default | Builds |
|---|---|---|
| `LOWCHUG_BUILD_PLUGIN` | ON | the JUCE plugin |
| `LOWCHUG_BUILD_TOOLS` | ON | the engine A/B harness |
| `LOWCHUG_BUILD_TESTS` | ON | the unit tests |

For quick DSP-only work, skip JUCE entirely:

```bash
cmake -S . -B build/eval -G Ninja -DLOWCHUG_BUILD_PLUGIN=OFF
```

## Layout

```
cmake/              PluginIdentity.cmake (names, codes, IDs), Dependencies.cmake
src/dsp/            plugin-independent DSP (plain C++, no JUCE)
  Detuner.h             the complete effect: engine, dry delay, glide, mix, gain, bypass
  PitchShiftEngine.h    the swappable engine interface
  EngineConfigs.h       the Live and Studio engine settings (Detuner holds one engine of each)
  engines/              PhaseVocoderEngine (chosen), SignalsmithEngine, GranularEngine (reference)
src/plugin/         JUCE wrapper: processor, parameters, tuning model, analysis tap, standalone audio setup
src/ui/             the editor: Lithium look and feel, panels, tuning sync (TuningController), shift analyzer
assets/fonts/       Barlow, Barlow Condensed, IBM Plex Mono (compiled into the plugin)
tools/engine-eval/  engine A/B harness: synthetic DI corpus, metrics, WAV renders
tests/              DSP tests (no JUCE) and plugin tests (JUCE UnitTest)
docs/               engine evaluation report
```

`src/dsp` depends only on the standard library and the header-only Signalsmith libraries. That way it can be used as a built-in effect in another host without the plugin wrapper.

## Plugin identity

Everything is set in `cmake/PluginIdentity.cmake`. **Don't change the codes after a public release**: hosts would treat the plugin as a different product.

| | |
|---|---|
| Product name | Lithium LowChug |
| CMake target | `LithiumLowChug` (code namespace `lowchug`) |
| Description | Guitar Detuner |
| Company (vendor) | Calamity Audio |
| Manufacturer code | `Clmy` (shared with Lithium Drums) |
| Plugin code | `Lchg` |
| Bundle ID | `io.github.thecalamityjoe87.lithiumlowchug` |
| LV2 URI | `https://github.com/thecalamityjoe87/lithium-lowchug` |
| Categories | VST3 `Fx\|Pitch Shift`, LV2 `PitchPlugin`, AU effect |

## Out of scope (for now)

- **Per-string retuning.** Turning standard into Drop D by lowering only the low E needs polyphonic source separation before the shifter: one engine per separated string. That is a research project of its own; the hook point is noted in `src/dsp/PitchShiftEngine.h`.
- MIDI control beyond host automation.

## Licenses

Lithium LowChug is free software under the GNU Affero General Public License, version 3 ([LICENSE](LICENSE)), like Lithium Drums.

- JUCE 8+ is dual-licensed: AGPLv3, or a commercial JUCE license. Distributing LowChug binaries requires one or the other.
- Signalsmith Stretch and Signalsmith Linear are MIT licensed.
- Barlow, Barlow Condensed and IBM Plex Mono are under the SIL Open Font License (assets/fonts/OFL-*.txt).
