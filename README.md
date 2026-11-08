# OpenPocket

!!! THIS PROJECT IS A PROOF OF CONCEPT, DONT EXPECT FULL FUNCTION AND SOUND QUALITY !!!

Install here: https://isod89.github.io/openpocket/ (Donner Pocket X; M-VAVE BlackBox: v31 has the fix for issue #1, being tested)
NEW WEB EDITOR: https://isod89.github.io/openpocket/editor.html · **v37: neural amp models (NAM A2), load your own from TONE3000**: [docs/WHATS-NEW-v37.md](docs/WHATS-NEW-v37.md) · since v33: [docs/WHATS-NEW-v34-v36.md](docs/WHATS-NEW-v34-v36.md)

**Open-source guitar multi-effects firmware for the Donner Pocket X and the M-VAVE BlackBox.**
Written from scratch for the pedal's own chip (JieLi AC791N, two cores), installable from the browser.

![OpenPocket screens](docs/ui-v36.png)

## What it does

- **12-block chain, 79 models**, in any order you like (double-click PUSH to move a block; the order is saved with
  the preset): GATE, COMP, PITCH, SPECIAL FX, SYNTH, DRIVE, AMP, CAB, EQ, MOD, DELAY, REVERB
- **Synths, keyboards and other guitars** from a normal guitar (no MIDI pickup): organ, church organ, string machine,
  choir, mono synth with pitch tracking and a resonant ladder filter, PWM synth, bass synth, 12-string, acoustic,
  sitar (bridge buzz + sympathetic strings), baritone
- **Neural amp models (v37)**: the AMP block's NAM type runs Neural Amp Modeler **A2** networks on the pedal: 12
  factory models trained for it (Twin, AC15, Super 59, Dirty Shirley, JCM900, JCM2000, JVM, Mark IV, Pro Mod, 5150,
  6505+) and 16 user slots you fill from the web editor with A2 captures (TONE3000...). All 24 factory presets use
  them. Details: [docs/NAM.md](docs/NAM.md)
- **8 amps fitted to captures of the real ones** (v34): Twin Reverb, AC15 Top Boost, JCM900, JCM2000, Mark IV,
  Dirty Shirley, Pro Mod, 5150. A computer sets the engine's numbers (tube stages oversampled ×4, tone stack, power
  amp, sag) so that it matches a Neural Amp Modeler capture of each amp on real guitar playing; same CPU cost as a
  voiced-by-ear amp. Method and results: [docs/FITTED-MODELS.md](docs/FITTED-MODELS.md)
- **4 drives fitted the same way** (TS9, Klon, MXR M77, HM-2) and **7 cabinets** (the 4x12 V30 fitted to measurements;
  Greenback, T75, Alnico Blue, Jensen, Cream, DI) with a MIC position
- **Pro levels (v36)**: every amp, block and preset at the same loudness (-23 LUFS, measured on real guitar);
  24 factory presets round the fitted amps, 28 USER slots; a PRESET LVL per preset; soft output limiter
- **MIDI over USB-C (v37)**: Program Change selects a preset, Control Change switches blocks and sets their models and
  settings, the pedal tells the computer when you change the sound, MIDI clock sets the delay time. Class-compliant, no driver;
  Bluetooth MIDI is only in the test image ([docs/BLE-TEST.md](docs/BLE-TEST.md)). Reference: [docs/MIDI.md](docs/MIDI.md), hardware test: [docs/MIDI-TEST.md](docs/MIDI-TEST.md)
- What changed: [v37](docs/WHATS-NEW-v37.md), [since v33](docs/WHATS-NEW-v34-v36.md)
- **New screen (v36)**: a PLAY page readable from a distance (preset number and name, amp, chain in colour) and an EDIT
  page with big values and bars
- **8 reverbs rebuilt in v35**: an 8-line feedback delay network with early reflections and moving lines (no
  metallic tail), the decay time you set (low and high frequencies apart), and Dattorro's plate; **delays up to 2 s**
- **11 modulations, 8 delays, 8 reverbs**, octaver / polyphonic octave / Whammy / harmonizer / detune, envelope filter,
  ring modulator, bitcrusher, slicer, square-wave synth, 4 compressors, 3 EQs
- **52 preset slots you can overwrite** (hold PUSH = SAVE; power-cut safe, kept across updates), 52 factory presets
  (one per style), a flicker-free colour UI, a chromatic tuner
- **Detailed cabinets** (cone breakup, off-axis mic comb, open-back reflection) and **tube stages with bias shift**
  (the note compresses on a hard attack and blooms back)
- **24-bit audio** in and out, guitar read from the left input channel only, like the official firmware (the right
  channel only carries noise: mixing it in was the constant hiss on high-gain amps)
- **USB update**: the pedal shows up as a USB-MIDI device and takes updates from the [web installer](docs/index.html), or from the command line (`make flash`, see [docs/BUILDING.md](docs/BUILDING.md))

Full list of models, presets and controls: [docs/SOUNDS-AND-CONTROLS.md](docs/SOUNDS-AND-CONTROLS.md).
Development notes (how the audio path was found, what changed in each version): [docs/AUDIO-SDK.md](docs/AUDIO-SDK.md).

## Install

Open the installer page (GitHub Pages of this repository) in **Chrome or Edge** on a computer, plug the pedal in over
USB, press **Connect**, then **Install**. The page also takes the pedal back to the official firmware: pick the
official `.fwsc` from the maker, it is checked in the browser.

On the **Donner Pocket X** the page works: updating OpenPocket → a newer OpenPocket is tested (2026-11-02, your saved
presets are kept), and other Pocket X owners have installed it from the page. On the **M-VAVE BlackBox**, OpenPocket up to v30 restarted every ~4.3 s after
the pedal was switched off and on ([issue #1](https://github.com/isod89/openpocket/issues/1)). M-VAVE's firmware
turns the chip's **long-press reset** off as its very first step (P33 `PINR_CON = 0`); OpenPocket did not, and on the
BlackBox a pin sits at its active level, so the chip reset itself 4 s after each start. Right after an install the
setting was still the official firmware's (it survives a warm reset), so the pedal worked until the next power cycle.
**v31 turns it off at start** (confirmed on the BlackBox). The boot screen shows `RST <reset source> PINR <value found>`.
(v30 to v32 also set the power supplies the way the official firmware does; it was not needed, and on a Pocket X it made
the DSP slower and crackle, so v33 leaves them as the boot loader does again.) Being tested on the BlackBox. For a pedal that
restarts in a loop, the page has a **rescue mode** (step 2): tick it, press Connect, plug the pedal in; the install
starts the moment the pedal answers and halts its DSP at once. A BlackBox can always go back to its official firmware from the page (step 3). It is the same JieLi update system as the M-VAVE FM-1, where this method is
used by Felucca, CLIPPER and SLOOP. If the pedal still loops, restore it from a Mac with no extra hardware: `make rescue` (read-only check and backup), then
`make rescue-write` (`docs/RESCUE.md`; it needs `brew install libusb`, sudo and the official BlackBox file). The second route is the
SD-card recovery (`flash/`: fm1buddy on an ESP32-S3, `makeimage.bat`).

**Without internet**: double-click `INSTALL-OFFLINE.bat` (Windows; nothing to install: a small PowerShell server,
`tools/serve_offline.ps1`) or run `make serve` (macOS, Linux; `tools/serve.py`, needs Python 3). It serves the installer
and the packages from this folder on this computer only (`http://localhost:8765`) and opens them in Chrome or Edge. Same
page, same steps; keep it running while you install. `INSTALL-OFFLINE.bat editor` opens the web editor the same way. It
builds the packages first when `docs\firmware` has none (`INSTALL-OFFLINE.bat build` rebuilds them).

## Preset editor

From OpenPocket **v32**, the **Preset editor** page (`editor.html`, linked from the installer) edits the pedal live over
USB, in Chrome or Edge: the 52 presets (load, save, save as, move up / down, back to factory), the current sound (every
block, type and setting, on / off, the block order by dragging the tiles, the name), and preset files (export one or
all 52 as JSON, import one to try it or many back into their slots). What you change on the page plays at once, and
what you change on the pedal shows on the page. The protocol: [`docs/EDITOR-PROTOCOL.md`](docs/EDITOR-PROTOCOL.md).

## Build

The same on macOS, Linux and Windows, in one container (Docker and Node 18+): **`make package`** (Windows:
**`build.bat`**), then **`make test`**. On Windows without Docker, `build.bat` builds in WSL instead, with the JieLi
toolchain installed there once (`wsl sh firmware/setup.sh`). Details, pinned versions, the toolchain's licence and troubleshooting:
[`docs/BUILDING.md`](docs/BUILDING.md). The build makes:

- `firmware/openpocket.bin`, the app;
- `docs/firmware/PocketX_9NN.fwsc`, `BlackBox_9NN.fwsc`, `manifest.json`, the installer's packages
  (`tools/pxpkg.py`, with the update loader built from `loader/`);
- `fm1-300.bin` with `make appimage DUMP=fm1-1.bin` (or `makeimage.bat`), from your factory backup (SD-card recovery image).

**`PUBLISH-GITHUB.bat`** does everything in one click: it runs `build.bat`, then publishes the source, a release with
the packages, and the installer on GitHub Pages (with your own `git` / `gh` and GitHub login; never a forced push).
GitHub's `main` becomes exactly this folder's files (v36): the version history from `openpocket.bundle` is merged in,
edits made on the GitHub site stay underneath, and the tag `vNN` points at that commit. **`PUBLISH-SOURCE.bat`**
publishes the source only, without building. Edit the README in this folder rather than on the site: the next publish
replaces the files with this folder's.

## Test (no pedal needed)

`tests/run_tests.sh [PocketX_016.fwsc]` checks, on the PC:

- the preset store on a simulated flash, with a power cut at every step of a save;
- the sound engine with the pedal's own math library: every block × model × extreme setting, no NaN, nothing runs away;
- the package builder: it rebuilds the official PocketX 016 app area **byte for byte** (with the official file);
- the installer against a **simulated pedal** that runs the pedal's real update code (`ota.c`, then the loader's
  `ldr_core.c`) on a simulated flash: update, install over the official firmware, wrong model refused, damaged package
  refused before anything is erased, way back to the official firmware (its loader staged and checked);
- the page itself in headless Chromium (Playwright), clicking Connect and Install.
- the preset editor against a simulated pedal that runs the pedal's real editor code (`px_editor.h`) and preset store
  on a simulated flash: every command, pushes both ways, a power cycle; then the editor page in Chromium (sliders,
  drag and drop, save, move, export / import).

To play with the simulated pedal yourself (screen, footswitches, knob, the installer and editor pages, and its sound,
with bundled guitar loops, a file or an input of your computer), run `make sim` and open `http://127.0.0.1:8766/`; see [`docs/SIMULATOR.md`](docs/SIMULATOR.md).

## Layout

| | |
| --- | --- |
| `engine/` | the sound engine (`px_dsp.c`), portable C, float |
| `firmware/` | the pedal app: screen, controls, audio (I2S), USB, UI (`ui.h`), board support (`bsp/`, felucca-bsp) |
| `loader/` | the update loader (runs from RAM, writes the app area) |
| `docs/` | the web installer (GitHub Pages) and documentation |
| `tools/` | package builder, loader wrapper, font converter, flash image tools |
| `tests/` | host tests and the simulated pedal |
| `flash/` | SD-card recovery with fm1buddy (ESP32-S3) |
| `firmware/ble/`, `third_party/jieli-ble/` | the optional Bluetooth MIDI test image (`make ble`, [docs/BLE-TEST.md](docs/BLE-TEST.md)) and JieLi's Bluetooth controller it links |

## Licence

GPL-3.0-only. Board support and loader: MPL-2.0 (felucca-bsp). JieLi SDK files: Apache-2.0. Inter: OFL 1.1.
The fitted models were computed from the NAM captures of [pelennor2170/NAM_models](https://github.com/pelennor2170/NAM_models)
(GPL-3.0, thanks to their authors) and the DI recordings of
[Alec-Wright/Automated-GuitarAmpModelling](https://github.com/Alec-Wright/Automated-GuitarAmpModelling) (GPL-3.0); neither is shipped.
Product names are trademarks of their owners, used only to describe the sound.
See [NOTICE.md](NOTICE.md). Not affiliated with Donner or M-VAVE. Install at your own risk.

