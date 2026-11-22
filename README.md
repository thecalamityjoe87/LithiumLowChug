# Lithium LowChug – Guitar Detuner

Play in standard (or Drop D) and hear your guitar tuned lower: E♭ standard, D standard, Drop C, B standard, an octave down. Part of the Lithium family alongside Lithium Drums and Lithium Studio. LowChug is a polyphonic pitch shifter that shifts the whole guitar, chords included, built to track through live.

Formats: VST3, LV2 and Standalone on Linux/Windows/macOS, plus AU on macOS. Made with JUCE.

## Status

| Milestone | State |
|---|---|
| 1. Skeleton and engine evaluation | done: see [docs/engine-evaluation.md](docs/engine-evaluation.md) |
| 2. Core DSP | next |
| 3. Quality features (modes, formants, transients, tight) | |
| 4. UI and presets | |
| 5. Hardening (pluginval, hosts, profiling) | |

Right now the plugin is a clean passthrough with all its parameters in place. It uses JUCE's generic editor.

## Building

You need CMake 3.25 or newer, a C++20 compiler and Ninja (recommended). On Linux you also need JUCE's usual dependencies (ALSA, FreeType, X11, …). JUCE and the Signalsmith libraries are fetched automatically at configure time.

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Plugins end up in `build/release/src/plugin/LithiumLowChug_artefacts/Release/`. Add `-DLOWCHUG_COPY_PLUGINS=ON` to also copy them into your user plugin folders.

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
  PitchShiftEngine.h    the swappable engine interface
  engines/              PhaseVocoderEngine (chosen), SignalsmithEngine, GranularEngine (reference)
src/plugin/         JUCE wrapper: processor, parameters, tuning model
src/ui/             (milestone 4)
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

## Licences

- JUCE 8+ is dual-licensed: AGPLv3, or a commercial JUCE licence. Distributing LowChug binaries requires one or the other.
- Signalsmith Stretch and Signalsmith Linear are MIT licensed.
