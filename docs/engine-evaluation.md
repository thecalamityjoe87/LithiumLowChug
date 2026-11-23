# Pitch-shift engine evaluation (milestone 1)

**Recommendation: build LowChug on its own phase-vocoder engine (`PhaseVocoderEngine`), not Signalsmith Stretch.**

- Live mode: low-delay window pair, 4096-sample analysis : 1024-sample synthesis, 4x overlap. That is **21.3 ms latency at 48 kHz** and 1.6 % CPU.
- Studio mode: 8192 : 2048, 8x overlap. That is **42.7 ms latency**, with low-end clarity close to the ideal.

The deciding issue is **tuning accuracy**. Signalsmith Stretch puts the low partials of a shifted guitar note **10 to 170 cents off pitch**, depending on its window length. Even its default 120 ms preset leaves the fundamental of a shifted low E about 16 to 19 cents sharp. Our engine puts every partial within 3 cents of the target, and sines within 0.01 cents. Signalsmith also needs about 85 to 120 ms of latency before its low end is clean, so it can't offer a usable Live mode.

Our engine has one weakness that Signalsmith doesn't. A pick attack is followed by some smear in the treble, and its high-frequency attack peak measures 6 to 16 dB hotter than the ideal. Fixing that belongs to milestone 3 (transient handling); see "Open issues" below.

## What was compared

| Engine | What it is |
|---|---|
| **Granular** (reference only) | Two crossfading read heads on a delay line, as in cheap pitch pedals. Shows the latency and quality floor. |
| **Signalsmith Stretch** 1.4.0 | MIT licensed. Phase vocoder with multi-channel phase locking and cross-frequency phase prediction. Tested with blocks from 20 to 120 ms (120 ms is its default preset), plus its split-computation and tonality-limit options. |
| **PhaseVocoder** (symmetric) | Our engine (`src/dsp/engines/PhaseVocoderEngine.cpp`). Laroche–Dolson peak-region shifting with identity phase locking, fractional-bin placement, phase-derived frequencies, and transient phase reset. Symmetric Hann windows, so latency equals the window length. |
| **PV-lowdelay** | The same engine with an asymmetric window pair. A long analysis window gives the frequency resolution; a short synthesis window sets the latency. Written `analysis:synthesis`. |

Rubber Band was excluded (GPL/commercial license), as the brief asked.

## Method

- **Test material.** A synthetic DI guitar take, 21.9 s at 48 kHz, −6 dBFS peak, built from additive plucked strings. Each note has inharmonic partials, a pluck-position comb, frequency-dependent decay and a 1.5 ms pick attack. It contains:
  - a ringing low E
  - a single-note riff
  - palm-muted 16th-note chugs on low E
  - an E5 power chord
  - a six-string open E chord
  - palm-muted power-chord chugs
  - a ringing Drop D low D
  - Drop D chugs
- **Ground truth.** The note model is explicit, so the ideal shifted performance can be rendered exactly: the same notes and envelopes with every partial multiplied by the ratio. Every engine is scored against that.
- **Shifts tested:** −1, −2, −5, −7, −12 and +2 semitones. Quality averages use the core −2, −5 and −7.
- **Host simulation:** 128-sample blocks, plus a run with random 1–512-sample blocks.
- **Metrics:**
  - Log-spectral distance to the ideal, 40 Hz–8 kHz.
  - Sine pitch error at 82.4, 110 and 440 Hz.
  - Sine envelope ripple (warble).
  - Harmonic-to-noise ratio below 1 kHz on the low sustains: the "mud" or "underwater" smear between harmonics.
  - Pick-attack spread, pre-echo and attack level, measured above 2 kHz.
  - Exact latency from an impulse.
  - CPU: the best of three runs, on one core of this laptop.

Your real DI isn't in these numbers, because there was no clean DI recording to use. The recordings in `~/Documents/Lithium Studio/Recordings` are clipped and noisy. Run the harness with `--input your_di.wav` to render your own playing through every engine for listening (see "Reproducing" below).

## Headline results

48 kHz, 128-sample blocks, mono, best of three timing runs. LSD is log-spectral distance; HNR is harmonic-to-noise ratio. Full tables are in `build/eval-full/report.md` after a run.

| Engine | Latency | CPU mono / stereo | Worst sine pitch error | LSD (dB, lower = better) | Low-end HNR: low E −7 / low D −5 | Pre-echo before pick | Attack peak vs ideal |
|---|---|---|---|---|---|---|---|
| *Ideal* | – | – | 0 | 0 | 56.9 / 56.9 dB | −54 dB | 0 dB |
| Granular 30 ms (reference) | 15.1 ms | 0.09 / 0.13 % | unusable (warbles) | 2.36 | −7 / 4 dB | −26 dB | −2.5 dB |
| Signalsmith 40/10 ms | 40.0 ms | 0.49 / 0.73 % | 88.6 ct | 3.03 | 5 / −6 dB | −14 dB | −1.6 dB |
| Signalsmith 85/21 ms | 85.0 ms | 0.52 / 0.77 % | 65.3 ct | 1.99 | 35 / 18 dB | −14 dB | −1.9 dB |
| Signalsmith 120/30 ms (its default) | 120.0 ms | 0.57 / 0.81 % | 43.8 ct | 1.90 | 52 / 49 dB | −14 dB | −2.2 dB |
| PhaseVocoder 4096, 8x | 85.3 ms | 0.81 / 1.27 % | 0.00 ct | 1.79 | 57 / 57 dB | −3 dB | +9.6 dB |
| PV-lowdelay 4096:512 | 10.7 ms | 3.19 / 4.88 % | 0.00 ct | 2.03 | 19 / 16 dB | −20 dB | +19.2 dB |
| **PV-lowdelay 4096:1024 (Live)** | **21.3 ms** | **1.58 / 2.48 %** | **0.00 ct** | **1.73** | **30 / 27 dB** | **−16 dB** | **+15.8 dB** |
| **PV-lowdelay 8192:2048, 8x (Studio)** | **42.7 ms** | **3.46 / 5.43 %** | **0.00 ct** | **1.70** | **53 / 51 dB** | **−2 dB** | **+6.2 dB** |

Every engine's reported latency matched its measured impulse latency exactly.

### Signalsmith's pitch error on a real note

These are per-partial errors in cents for a ringing low E, measured from long FFTs of the steady part:

| Engine, shift | Fund. | 2nd | 3rd | 4th | 5th | 6th | 7th | 8th |
|---|---|---|---|---|---|---|---|---|
| Signalsmith 40/10, −2 st | −169 | +9 | −2 | +8 | +1 | −3 | +4 | 0 |
| Signalsmith 85/21, −7 st | +38 | −15 | +1 | +16 | −9 | +3 | +8 | −9 |
| Signalsmith 120/30, −2 st | +19 | +15 | +8 | +3 | −1 | −1 | +2 | +3 |
| PV-lowdelay 4096:1024, −7 st | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | +2.0 |

The errors differ from partial to partial, so a shifted note isn't just out of tune. It becomes slightly inharmonic, which is exactly where LowChug spends its time: low notes, big shifts.

The output pitch is steady over time (I checked with zero-crossing counts as well as FFT peaks), so this is a fixed bias, not wobble. It shrinks with longer windows: about 1 cent at 200 ms. It also shrinks with more overlap: 40/2.5 ms still gives about 24 cents.

I tried a patch that replaced Stretch's energy-centroid peak frequency with a phase-derived one. It changed nothing, which points to the cause being in how Stretch interpolates phase between bins. I didn't chase it further.

### Other observations

- **Latency vs low end is physics, not tuning.** Separating harmonics 49 to 82 Hz apart needs roughly 40 ms or more of analysis. A symmetric short-window FFT shifter (PV 1024, Signalsmith 20 to 40 ms) turns the low end to mush: the HNR falls to about 0 dB and the fundamental drops by up to 50 dB. The low-delay window pair keeps a long analysis window but adds only the synthesis window's length in latency. That is how the Live configuration gets most of the resolution at 21 ms.
- **Signalsmith's output depends on the host's buffer size.** Its frame grid aligns to the first non-silent block. The audio is equivalent, but renders aren't bit-identical across buffer sizes; our engine's are. Not a blocker, but it makes null tests and regression tests harder.
- **Chords:** no engine warbled on the sine-ripple test (all about 1.9 dB, the same as each other). The log-spectral distance on chords is close between our PV and Signalsmith 85 to 120 ms (2.7 to 2.8 dB).
- **Average CPU is not a constraint.** Every candidate fits the 3 % budget except PV-lowdelay 4096:512. The PV does its FFT in bursts: one frame every 256 samples in Live, where the worst single 128-sample block took about 4–5 % of its deadline. Studio's 8192-point frames are heavier, and their worst block varied from 9 % to 64 % between runs. Before Studio mode ships (milestone 3), each frame's work should be spread across the blocks of its hop, as Stretch's "split computation" does, so a small host buffer never gets a spike.

## Milestone 3 update

Measured with `lowchug-engine-eval --engines shipping` (the shipping configs, and each without the milestone 3 changes) and the DSP tests. 48 kHz, 128-sample blocks, mono, best of three.

| | Live before | **Live now** | Studio before | **Studio now** |
|---|---|---|---|---|
| Latency | 21.3 ms | **21.3 ms** | 42.7 ms | **48.0 ms** |
| CPU mono / stereo | 1.6 / 2.5 % | **2.5 / 3.5 %** | 3.4 / 5.4 % | **4.1 / 6.3 %** |
| Worst block at 128 / 64 / 32-sample buffers | 4 / 8 / 15 % | 7 / 14 / 28 % | 9 / 17 / 33 % | **7 / 11 / 22 %** |
| Low-end HNR, low E -7 / low D -5 (ideal 56.9) | 30 / 27 dB | **55.6 / 52.8 dB** | 52.7 / 51.4 dB | **56.8 / 56.6 dB** |
| Treble residue, low chord, -1 / -5 / -12 st | -69 / -59 / -48 dB | **-70 / -62 / -65 dB** | -83 / -85 / -64 dB | **-92 / -96 / -115 dB** |
| Treble residue, single low E, -1 / -5 / -12 st | -76 / -56 / -43 dB | **-108 / -109 / -109 dB** | -95 / -96 / -104 dB | **-130 / -130 / -130 dB** |
| Pre-echo before a pick after silence | -30 dB | **none** | -33 dB | **none** |
| Max sine pitch error | 0.00 ct | 0.00 ct | 0.00 ct | 0.00 ct |

What changed, and why:

1. **The treble residue came from how low partials were moved, not from the window design.** A real partial at bin k0 is, in the FFT, a lobe `a W(k - k0)` plus the mirror of its negative frequency, `conj(a) W(k + k0)`. The low-delay analysis window has a wide skirt (-36 dB at 7 bins, -49 dB at 14), so for an 82 Hz partial (bin 7 in Live) the mirror reaches it at about -50 dB. Moving a region moved the mirror the wrong way, with a phase following the wrong frequency; bins pushed below DC were lost; and each partial's lobe was split across neighboring regions (sidelobe "peaks", the next note's region), each moving its piece differently. Overlap-add turned all of that into a steady treble residue and the "mud" between low harmonics. Smoother (Hann-edged) windows were tried and measured worse in Live.
   **Fix: the partial model.** The strongest low partials (up to 48, below 128 bins, within 70 dB of the frame's peak) are measured and moved as exact window lobes: amplitudes solved at their peak bins (Gauss-Seidel over a small table of the lobes' overlaps), frequencies measured again with the neighbors subtracted (a neighbor's lobe biases the phase vocoder's reading by up to a third of a bin), lobes and mirrors subtracted from the spectrum, the rest moved region by region as before, and the lobes added back at their shifted places. This is also what brought Live's low end from 30 to 56 dB HNR.
2. **Pick attack.** Two parts:
   - *Pre-echo.* A frame that has only just caught a pick spreads it (and the new note) over its whole synthesis span: up to 21 ms before the pick in Live, 42 ms in Studio. Onsets are now found in the time domain (energy above 2 kHz in 32-sample blocks against the loudest of the last 25 ms, which spans a low note's once-per-cycle treble burst), and while an onset passes through the window the regions it brought in are synthesized separately and faded in from 4 ms before it. Pre-echo before a pick after silence went from -30 / -33 dB to nothing; while another note rings, the treble before the pick is within 1.5 dB of the ideal.
   - *Attack level.* On normal picks the treble of the attack is within about 2 dB of the ideal in both modes (Studio's first 5 ms after silence come out about 8 dB soft). The "+6 to +16 dB hotter attack" in the milestone 1 table comes from the corpus's palm-muted notes, whose ideal treble is tiny: what the engines add there is decay and release smear from fast amplitude changes inside long windows (fast palm-mute decays, the 15 ms note release), which is window physics and remains. Re-deriving onset phases on every frame from the phase gradient across frequency was tried and measured slightly worse than the phase reset, so the reset stays.
3. **Studio's split computation.** Each 8192-point frame's work (FFTs, analysis, synthesis) is spread over the hop that follows it, so no audio block pays for a whole frame. It costs one hop (256 samples, 5.3 ms): Studio is 2048 + 256 = 2304 samples, 48.0 ms. A 8192:1792 window would keep 42.0 ms at about 3 dB less low-end clarity and more CPU; the user chose 48 ms.
4. **Formant preservation.** The spectral envelope is drawn through the real partials only (sidelobe peaks and anything 70 dB down skipped) and smoothed over about +/-150 Hz; each partial takes on the envelope's level where it lands (up to +/-18 dB). On a tone shaped by 600 Hz and 2.4 kHz resonances, shifted -7 st: with formant off the harmonics follow the moved resonances within 0.2 dB; with it on they follow the original resonances within 0.7 dB. Cost: about 0.1 % CPU in Live.
5. **Tight** (in `Detuner`, not the engine): on the shifted signal, a 12 dB/octave high-pass at up to 0.9 x where the low E lands, plus a low shelf of up to -6 dB at 2.5 x that note, scaling with the amount (state-variable filters, settings interpolated per sample, 150 ms glide, so automating it never clicks). At 100 % an octave below the shifted low E is cut by about 17 dB; the upper range is untouched.

## Milestone 5 update

Hardening; details in [hardening.md](hardening.md). Two engine changes, both measured with `lowchug-engine-eval --engines shipping`:

- **Peak frequencies are measured at peaks only.** The phase advance that gives each partial's true frequency used to be computed with an `atan2` for every bin of every frame, but everything downstream reads it only at peak bins. It is now measured once per peak, as arg(X_now x conj(X_before)) (the same quantity). The quality tables of the report are identical to the last digit; CPU fell from 2.46 to 1.95 % (Live, mono) and 4.03 to 3.03 % (Studio, mono), and the worst block at 128 / 64 / 32-sample buffers is now 6 / 12 / 25 % (Live) and 9 / 18 / 41 % (Studio, engine only; the profiler's numbers for the whole effect are in hardening.md).
- **NaN and infinity in the input count as silence.** One NaN used to stay in the onset detector's recursive filter forever, which silently switched the onset mask (pre-echo protection) off for the rest of the session.

The harness's "Envelope (-5 st)" latency column (a cross-correlation of onset envelopes, which the onset mask's reshaped attacks threw off: it read 37 ms for Live) is replaced by "Impulse (-5 st)", the peak of a shifted impulse: 21.1 ms for Live and 47.8 ms for Studio, against 21.3 and 48.0 ms reported (the shifted partials no longer peak at quite the same instant).

## Open issues for later milestones

1. **Pick attack (fixed in milestone 3, see above).** Our PV shows some treble smear after each pick, and an attack peak above 2 kHz that is 6 to 16 dB hotter than ideal. The cause: each spectral region's phase advances independently from frame to frame. That is correct for sustained partials, but it scatters the energy of an attack that is still inside the long analysis window. The current transient phase reset helps the attack frame only. Signalsmith avoids this by also predicting phase across frequency. Planned fix: propagate phase across frequency at onsets (in the style of phase-gradient heap integration), and consider splitting Studio into a long window for the lows and a short one for the highs.
2. **Treble residue in Live (fixed in milestone 3, see above).** Found in milestone 2. Live's short synthesis window leaves a faint steady residue above 2 kHz. It grows with the shift size, from about −61 dB relative to the signal at −1 st to about −42 dB at −12 st. Studio and the symmetric windows are about 25 dB cleaner. 8× overlap helps only at small shifts, so the fix has to be in the low-delay window design itself.
3. **Ultra-low-latency option.** 4096:512 gives 10.7 ms. Its low end is muddier (HNR about 19 dB) and its CPU is about 3 %. It could be offered as a third mode if 21 ms feels too slow when monitoring.
4. **Listening check (done in milestone 5).** These numbers are objective, so the plugin was also checked by ear: played through the user's DAW into the TONE3000 amp sim, it sounds fine. A distorted amp exaggerates exactly the residues measured above, so that is the demanding case.

## Reproducing

```bash
cmake -S . -B build/eval -G Ninja -DLOWCHUG_BUILD_PLUGIN=OFF
cmake --build build/eval
./build/eval/tools/engine-eval/lowchug-engine-eval --out build/eval-full            # about 1 minute
./build/eval/tools/engine-eval/lowchug-engine-eval --input my_di.wav --out build/eval-di
```

The harness writes `report.md` plus WAV files to `audio/`:

- `input_DI.wav`: the synthetic DI corpus.
- `reference_<shift>st.wav`: the ideal shift.
- `<shift>st_<engine>.wav`: each engine's output, latency already removed, so you can A/B them in a DAW.
- `user_<shift>st_<engine>.wav`: your DI through each engine, when you pass `--input`.
