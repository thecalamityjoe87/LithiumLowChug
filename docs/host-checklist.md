# Host checklist: Lithium Studio

A manual check of Lithium LowChug in a real host, to run after each release build. Lithium Studio (`~/.local/bin/lithium-studio`) is built on Tracktion Engine. Tracktion restarts playback when a plugin reports a new latency, so a short restart when you switch modes is expected and correct.

**Last run: 2026-11-22, milestone 5 build. Passed:** mode switches with latency, session save and reload, and automation of Mode, Formant and Tight, plus a listening check through the TONE3000 amp sim.

Automated checks that already pass (milestone 5): pluginval at strictness 10 for the VST3 and the LV2, the Steinberg VST3 validator (537 of 537), and the unit tests. This list covers what only a real host can show.

## Before you start

1. Build and install:
   ```bash
   cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOWCHUG_COPY_PLUGINS=ON
   cmake --build build/release
   ```
   This installs `~/.vst3/Lithium LowChug.vst3` and `~/.lv2/Lithium LowChug.lv2`.
2. Start Lithium Studio and rescan plugins, so it picks up the new build and not a cached old one.
3. Set the audio device to 48 kHz. The latency figures below are for 48 kHz.
4. Have a DI guitar clip ready (anything with clear pick attacks), or play live into an input track.

Tick each box as you go. If something fails, note the step number and what you saw.

## 1. Name, vendor and category

- [ ] The plugin list shows **Lithium LowChug**, not "LithiumLowChug" or "LowChug".
- [ ] The vendor/manufacturer is **Calamity Audio**, and it sits next to Lithium Drums if the list groups by vendor.
- [ ] The category is an effect under **Pitch Shift** (VST3: `Fx|Pitch Shift`). If Lithium Studio lists LV2 plugins, the LV2 shows as a **Pitch** plugin.
- [ ] It loads on an audio track as an effect, and it is not offered as an instrument.
- [ ] The editor opens with the Lithium look: the LITHIUM / LOWCHUG / GUITAR DETUNER wordmark, the preset box in the header, and Live | Studio buttons.
- [ ] About shows the AGPLv3 notice, and its License button works.

## 2. The 18 programs

Open the host's program/preset menu for the plugin (not the plugin's own preset box).

- [ ] It lists exactly 18 programs: the 9 Live versions first, then the 9 Studio versions.

  | # | Program | Playing in | Shift | # | Program |
  |---|---|---|---|---|---|
  | 1 | Eb Standard (Live) | Standard | −1 st | 10 | Eb Standard (Studio) |
  | 2 | D Standard (Live) | Standard | −2 st | 11 | D Standard (Studio) |
  | 3 | C# Standard (Live) | Standard | −3 st | 12 | C# Standard (Studio) |
  | 4 | C Standard (Live) | Standard | −4 st | 13 | C Standard (Studio) |
  | 5 | B Standard (Live) | Standard | −5 st | 14 | B Standard (Studio) |
  | 6 | Drop C (Live) | Drop D | −2 st | 15 | Drop C (Studio) |
  | 7 | Drop B (Live) | Drop D | −3 st | 16 | Drop B (Studio) |
  | 8 | Drop A (Live) | Drop D | −5 st | 17 | Drop A (Studio) |
  | 9 | Octave Down (Live) | Standard | −12 st | 18 | Octave Down (Studio) |

- [ ] A fresh instance starts on **D Standard (Live)**.
- [ ] Picking a program from the host menu updates the editor: the preset box name, Playing in, the "Tune down to" dial, the Shift knob, and Live/Studio.
- [ ] Picking a program from the plugin's own preset box shows the same name in the host's menu.
- [ ] Every program sets Formant off, Tight 0 %, Mix 100 % and Output 0 dB.
- [ ] Turning any covered control (Shift, Fine, Formant, Tight, Mix, Output, mode) adds a `*` to the preset box name.

## 3. Latency on mode switches

At 48 kHz, Live is **1024 samples (21.3 ms)** and Studio is **2304 samples (48.0 ms)**. At 44.1 kHz they are 23.2 ms and 52.2 ms, and at 96 kHz 21.3 ms and 48.0 ms again.

- [ ] If Lithium Studio shows plugin latency anywhere (plugin info, a track's delay compensation readout), it reads 1024 samples in Live and 2304 in Studio.
- [ ] Click **Studio** while stopped, then **Live**: the readout follows each time.
- [ ] Click **Studio** during playback. You may hear one short playback restart (Tracktion re-syncs), then the shifted sound continues with no click or dropout. Do the same back to **Live**.
- [ ] Switch with a program: pick **B Standard (Studio)**, then **B Standard (Live)** from the host menu. The latency follows, as with the buttons.
- [ ] **Compensation is right.** Duplicate the DI track, and on the copy set LowChug to **Hear: Custom**, **Shift 0**, **Fine 0**. At 0 st the plugin passes the signal through, delayed by exactly its latency. Play both tracks:
  - [ ] With a polarity/phase invert on one track (if Lithium Studio has one), the pair cancels to silence in both Live and Studio.
  - [ ] Without one: solo both and listen for flamming or a doubled attack (there should be none), or export both tracks and check that the pick transients line up to the sample.
- [ ] With the copy shifted (e.g. Drop C), its pick attacks still land on the beat with the original, not 21 or 48 ms late.
- [ ] **Host bypass** (Lithium Studio's own bypass button on the plugin): the track stays in time, with no jump forward by 21 or 48 ms.

## 4. Session save and reload

1. [ ] Load **Drop B (Studio)**, then change: Formant **on**, Tight **50 %**, Fine **−12.5 ct**, Mix **80 %**, Output **−3 dB**. The preset box shows `Drop B (Studio)*`.
2. [ ] Save the session, quit Lithium Studio, start it again and open the session.
3. After reloading:
   - [ ] Every control is as you left it: Studio, Formant on, Tight 50 %, Fine −12.5 ct, Mix 80 %, Output −3 dB, Playing in Drop D, Hear Drop B.
   - [ ] The preset box still shows `Drop B (Studio)*`.
   - [ ] The latency is Studio's (2304 samples) from the first playback, and the compensation check from step 3 still lines up.
   - [ ] It sounds the same as before saving.
4. [ ] Repeat with a **Live** program and Formant **off**. Live's latency (1024) is restored, and Formant comes back off, not on.
5. [ ] Two instances on two tracks, one Live and one Studio, both restore their own settings.

## 5. Automation of Mode, Formant and Tight

In Lithium Studio's automation lanes for the plugin:

- [ ] The parameter list includes **Mode**, **Formant** and **Tight** (and Shift, Fine, Playing In, Hear, Mix, Output, Bypass).
- [ ] **Mode:** draw automation that switches Live → Studio → Live every couple of bars over a DI clip.
  - [ ] The Live | Studio buttons follow the automation during playback.
  - [ ] Each switch crossfades with no click. The new mode takes about a quarter of a second to warm up first, and a playback restart from Tracktion at a switch is acceptable.
  - [ ] Delay compensation follows: after each switch settles, the track is back in time (the duplicate-track check from step 3 still lines up).
- [ ] **Formant:** automate off → on → off on a sustained chord. The tone changes (the body stays put when on) with no click.
- [ ] **Tight:** automate a ramp from 0 % to 100 % and back on a low riff. The low end thins smoothly as it rises, with no zipper noise or clicks.
- [ ] Record automation by moving each control in the editor while the lane is armed. The lanes capture it, and playback reproduces it.
- [ ] Automation survives a session save and reload (step 4).

## If something fails

- Note the step, the mode (Live/Studio), the sample rate and buffer size, and what you heard or saw.
- For latency problems, check whether the plugin's latency readout changed (the plugin reported it) or not (the host didn't pick it up).
- `LOWCHUG_SNAPSHOT_DIR=<dir> ./build/release/tests/lowchug-plugin-tests` saves PNG snapshots of the editor, to compare with what the host shows.
