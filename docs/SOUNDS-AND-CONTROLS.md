# OpenPocket — sounds and controls (v37)

![OpenPocket screens](ui-v23.png)

## The chain (12 blocks, 79 models)
Default order: `GT → CMP → PIT → SFX → SYN → DRV → AMP → CAB → EQ → MOD → DLY → REV`

Since v29 every preset has its own block order: **double-click PUSH** on the chain to grab the selected block, turn
the knob to move it, PUSH to drop it (HOME puts it back). Delay before the amp, wah after the drive, octaver after
the fuzz... The order is saved with the preset. DRV and AMP share one oversampled section when AMP comes right after
DRV; elsewhere each runs its own.

Each block has a **type** (the model), an **ON/OFF** state and up to **5 settings (0–100)**. Where a bare number
would mean nothing, the screen shows the real value: interval (`+5TH`), semitones (`-7`), time in ms (`510`),
playback heads (`1+2`), EQ dB (`+6`), bits (`8`), `SLOW`/`FAST` for the rotary.

The names refer to the famous units the sounds are inspired by. Amps, drives and the V30 cabinet are fitted to
captures of the real units (v34); the effects follow published circuit analyses (ElectroSmash, Electric Druid,
R.G. Keen, Boss/Roland/EHX manuals). They are digital models, not copies. The product names are trademarks of their
owners, used only to describe the sound.

### AMP — NAM: neural amp models (v37; MODEL, GAIN, BASS, MID, TREBLE)
The ninth AMP type plays Neural Amp Modeler A2 networks: 12 factory models (TWIN, AC15, SUPER 59, SHIRL CL, JCM900,
SHIRLEY, JCM2000, MARK IV, JVM OD2, PRO MOD, 5150, 6505 RED) and 16 user slots filled from the web editor with A2
captures. MODEL picks the network, GAIN sets the level into it (±12 dB), BASS / MID / TREBLE are an EQ after it. The
models have no cabinet: keep a CAB after the AMP. All the factory presets use NAM. Details: [NAM.md](NAM.md).

### AMP — 8 amps, fitted to captures of the real ones (GAIN, BASS, MID, TREBLE, PRESENCE)
Since v34 there are fewer amps, and each one is fitted by computer to a capture of the real amp (method and
references: [FITTED-MODELS.md](FITTED-MODELS.md)). With the knobs at noon it sounds like the capture.

| Type | Style | Reference |
|---|---|---|
| TWIN | American clean | Fender Twin Reverb |
| AC15 TB | chime | Vox AC15 Top Boost |
| JCM900 | British crunch | Marshall JCM900 |
| JCM2000 | British lead | Marshall JCM2000 DSL |
| MARK IV | American lead | Mesa Boogie Mark IV |
| SHIRLEY | boutique crunch | Friedman Dirty Shirley |
| PRO MOD | modern rock | Splawn Pro Mod |
| 5150 | high gain | Peavey 5150 |

Each amp = 1 to 4 preamp stages (gain, asymmetry, RC filters per stage) oversampled ×4, input filter (tightness),
a "bright" cap that fades as the gain goes up, a tone stack, presence, resonance, power-stage saturation and supply
sag (asymmetric triode curve, bias shift that follows your playing, sag that reaches the preamp too). All 8 amps are
level-matched (±0.1 dB).

### CAB — 7 cabinets (BODY, BRIGHT, MIC) and USER IR (v38: IR, LOW CUT, HIGH CUT)
1x12 US OPEN (Jensen C12N) · 2x12 BLUE (Alnico Blue) · 4x12 GREEN (Greenback) · 4x12 T75 · 4x12 V30 (Vintage 30,
fitted to measurements since v34) · 1x12 CREAM · DI (NO CAB) · USER IR.
**USER IR** (v38) plays a cabinet impulse response loaded from the web editor (8 slots, a `.wav` file each): its first
23 ms (1024 samples at 44.1 kHz), convolved exactly (partitioned FFT), at the same loudness as the other cabinets. IR
picks the slot (its name shows on the PLAY page), LOW CUT 20..400 Hz, HIGH CUT 20 kHz..2 kHz.
Speaker resonance, presence dips and peaks taken from the makers' curves (the V30 from 8 measured amp / amp + cab
pairs). **MIC** moves the mic from the edge (dark) to the centre (bright).
Each cabinet also has a detail layer: the cone's breakup resonances between 1.2 and 6 kHz, the comb filtering of an
off-axis mic (MIC 0 = cone edge: dip around 6 kHz; MIC 100 = centre: out of the audio band) and the rear wave of
the open-back cabinets (1x12 US, 2x12 BLUE), computed with resonators.

### DRV — 4 drives, fitted to captures (DRIVE / DIST, TONE, LEVEL)
| Type | Style | Reference |
|---|---|---|
| TS9 | overdrive (mid hump) | Ibanez TS9 |
| KLON | transparent overdrive | Klon Centaur (clone) |
| M77 OD | boost / hot overdrive | MXR M77 Custom Badass |
| SWEDE HM-2 (LOW, HIGH) | Swedish distortion | Boss HM-2 |

Each drive follows its circuit (input filter, the TS "shelf", clipping type, original tone control), with its
numbers fitted to a capture of the pedal at noon. Level-matched: at noon every drive comes out at the same level as
the guitar.

### SYN — 11 synths, keyboards and other guitars (v29)
Played with a normal guitar: no special pickup, no MIDI.

| Family | Type | Settings | How it works |
|---|---|---|---|
| Keyboards (chords OK) | ORGAN | 16', 8', 4', PERC, DRY | drawbars: −1 octave, the guitar's own pitch, +1 octave; key click; percussion (+19 semitones, decaying) |
| | CHURCH ORGAN | 16', 8', 4', 2', DRY | pipe organ: slow speech, long release, slight celeste |
| | STRINGS | ATTACK, RELEASE, OCTAVE, TONE, DRY | tape-replay / string machine: swell, +1 octave layer, 3-voice ensemble |
| | CHOIR | ATTACK, RELEASE, VOWEL, OCTAVE, DRY | the strings through vocal formants, "ooh" (0) to "aah" (100) |
| Synths that follow your note (one note at a time) | MONO SYNTH | WAVE, OCTAVE, CUTOFF, RESO, ENV | pitch tracker → band-limited saw / square / pulse → 24 dB resonant ladder filter with envelope |
| Synths that follow the waveform (no delay) | PWM SYNTH | WIDTH, RATE, CUTOFF, RESO, MIX | the guitar turned into a pulse wave whose width moves, then the ladder filter |
| | BASS SYNTH | OCT -1, OCT -2, CUTOFF, ENV, DRY | square waves one and two octaves down, filter swept by each attack |
| Other guitars | 12-STRING | OCTAVE, DETUNE, TONE | thin octave strings, slightly out of tune, plus the doubled unison strings |
| | ACOUSTIC | BODY, BASS, TREBLE, ROOM | magnetic pickup → flat-top: body resonance (parlour to jumbo), top, sparkle, body reflections |
| | SITAR | KEY, BUZZ, SYMPATH, DECAY | bridge buzz (jawari) + 6 sympathetic strings tuned to KEY |
| | BARITONE | TUNE, TONE, MIX | the whole guitar 1 to 12 semitones down |

Keyboards hold the note while the string sounds (like a key held down) and release when it dies away or when you
mute it. MONO SYNTH needs single notes: it finds the pitch in 30–80 ms and is accurate to a few cents.

### Effects
| Block | Types |
|---|---|
| GT | NOISE GATE (THRESH, RELEASE) |
| CMP | SQUASH (Dyna Comp), STUDIO (parallel, MIX), OPTO LA-2 (optical, slow release), SLOW SWELL (attack removed: violin effect) |
| PIT | OCTAVER (analog: SUB -1, SUB -2, UP +1), POLY OCTAVE (clean −1 and +1 octave), WHAMMY (±24 semitones, GLIDE), HARMONIZER (fixed interval −OCT…+OCT), DETUNE (doubling ±cents), LOW OCTAVE (−1 octave on the low strings, or on a chord's lowest note: RANGE, OCT -1, DRY, GROWL, COMP) |
| SFX | ENVELOPE WAH (Mu-Tron), AUTO WAH (LFO, or fixed wah with DEPTH 0), RING MOD, BITCRUSHER, SLICER (8 rhythm patterns), LO-FI RADIO, SQUARE SYNTH (square-wave synth + sub, opening filter) |
| EQ | 5 BAND, GRAPHIC 5 (the Mesa "V"), PARAMETRIC |
| MOD | CE CHORUS, DIMENSION (4 modes), FLANGER, PHASE 90, SMALL STONE, UNI-VIBE, OPTO TREMOLO, BIAS TREMOLO, HARMONIC TREM, ROTARY (Leslie, real SLOW↔FAST ramp), VIBRATO |
| DLY | DIGITAL, ANALOG BBD, MEMORY MAN (modulated repeats), TAPE EP-3 (wow & flutter), SPACE ECHO (3 heads, 7 combinations), REVERSE, LO-FI, DUAL TAP (quarter + dotted eighth) — TIME 20–2000 ms (REVERSE 250–1000 ms) |
| REV | ROOM, HALL, CATHEDRAL, PLATE, SPRING (DRIP), SHIMMER (octave in the tail), GATED (80s), AMBIENT (up to 20 s) |

The analog/tape delays (ANALOG, MEMORY MAN, TAPE, SPACE ECHO, LO-FI) **self-oscillate above FDBK ≈ 90**, like the
real ones: lower FDBK to stop it.

**LOW OCTAVE**: an octave under the low notes, the rest of the guitar untouched, as the OC-3's and OC-5's POLY
modes. RANGE shows the note where it stops: `A2` (20) the E string up to the 5th fret and the open A, `D3` (40) the
E and A strings in open position, up to `G4` (100). Of two notes below RANGE the lower one gets the octave, as a
bass player would play the root of a power chord (clean: 96–99 % of the octave on it). RANGE at 0 is `LOWEST`, as on
the OC-5: the octave of the lowest note of any chord, anywhere on the neck. OCT -1 at 50 plays the octave as loud as
the note. DRY 100 leaves your signal exactly as it is (the block switched off gives the same samples). GROWL drives
the octave alone through a soft clipper, as the OC-3's DRIVE mode, so that it carries on small speakers and a PA
without subwoofers; at 0 the octave stays clean. COMP evens the octave's level, as a compressor in front of an
octaver: at 100 a note 20 dB softer gets an octave only 2 dB softer, and the hum between notes is not raised. The
octave comes about 14 ms after the note (the dry signal has no delay): it is made from the note's phase, turned half
as fast, so nothing waits in a buffer; the time is the filters' that pick the note. The low band runs at 5.5 kHz and
costs little CPU. It splits by pitch, not by string: a note below RANGE gets the octave on whatever string you play
it.

**Delay TIME (v35)**: 0–80 = 20–1000 ms in steps of 12.25 ms, 80–100 = 1–2 s in steps of 50 ms; the screen shows
the ms. Presets saved before v35 keep their delay time (the knob value moves). The repeats are read with cubic
interpolation (no dulling when the time glides or the tape wobbles), like the chorus, flanger and vibrato.

**Reverbs (v35)**: the halls, rooms, spring, shimmer, gated and ambient are an 8-line feedback delay network with
early reflections, Dattorro's input diffusers, slowly moving lines (no metallic ringing in the tail) and a decay
time set separately for the lows and for 4 kHz (TONE); PLATE is Dattorro's plate (the classic "Effect Design"
plate). DECAY sets the real decay time (RT60) of the lows: ROOM 0.3–1.2 s, HALL 1.2–4 s, CATHEDRAL 3–10 s,
PLATE 0.6–5 s, SPRING 1.5–4 s, SHIMMER 2–8 s, AMBIENT 6–20 s. They run at 22 kHz in 16 bits (like the classic
studio units), in a quarter of v34's memory.

## Factory presets (v37): 24 sounds, all at the same loudness, then 28 USER slots
Built round the NAM models (v37; v36's were built round the fitted amps) and the fitted drives. Every preset is levelled to the
same loudness (-23 LUFS, EBU R128, measured on real DI guitar by `tools/level_presets.py`): going from a clean to a
high-gain preset no longer jumps by 20 dB.

| | Clean | Crunch | Lead | High gain | Effects |
|---|---|---|---|---|---|
| | 01 TWIN CLEAN | 07 TS BLUES | 13 BRIT LEAD | 17 MODERN ROCK | 22 VIBE ROCK |
| | 02 CHORUS CLEAN | 08 BLUES BREAKUP | 14 SMOOTH LEAD | 18 5150 RHYTHM | 23 ORGAN |
| | 03 AC15 CHIME | 09 KLON CRUNCH | 15 HAIR METAL | 19 TIGHT METAL | 24 ACOUSTIC |
| | 04 FUNK WAH | 10 BRIT CRUNCH | 16 SPACE LEAD | 20 SWEDE DEATH | |
| | 05 SURF SPRING | 11 70S ROCK | | 21 DOOM | |
| | 06 AMBIENT SWELL | 12 BOUTIQUE | | | |

25–52: **USER 25 … USER 52**, a clean NAM TWIN + 1x12 + a little room, to build your own.

**Levels (v36)**: every amp plays at the same loudness at GAIN noon and moves only +-3 dB over the whole knob; every
block type, with its knobs at noon, keeps the loudness when switched on; each preset has its own **PRESET LVL**
(GT page, 3rd line: -15..+15 dB) to balance your sounds. The output ends in a soft limiter (transparent up to
-2 dBFS) instead of a hard clip.

## Controls
| Control | Action |
|---|---|
| KNOB (PARA) | PLAY: pick the block · EDIT page: pick the line · editing: change the value (turn fast = steps of 5) |
| PUSH (knob) | PLAY → open the block's EDIT page · model/setting line → edit · ON/OFF → toggle · editing → confirm |
| PUSH **double-click** (PLAY) | **MOVE** the selected block: knob = move it, PUSH = drop, HOME = put it back |
| PUSH **long press (0.8 s)** | **SAVE**: store the current sound (see below) |
| HOME | back to PLAY · **long press (0.7 s) = tuner** |
| Right / left footswitch | next / previous preset |
| AMP, CAB, DLY, REV, MOD | jump to the block; press again = on/off |
| FX | steps through DRV → SFX → SYN → PIT → CMP → GT → EQ |
| MASTER | output volume |

**Screen (v36)**: PLAY shows the preset number and name large, the amp and cab, the chain in colour (on = filled,
off = dark) and the selected block; EDIT shows the block on its colour, the model, and up to 5 settings with value and
bar. `C1 xx%` (top right on PLAY, bottom right on EDIT) = DSP load on the second core (`C0` = fallback on the main
core, `BYP` = overload guard: turn a block off). `EDITED`: the sound differs from its saved slot.
Top centre of PLAY: the **battery** (v36), from the chip's own VBAT/4 channel (green, amber below 30 %, red below 15 %;
`USB` when the 5 V supply is there); the tuner page shows the same reading in volts.

## MIDI over USB-C (test build v37; [MIDI.md](MIDI.md))
**Hold FX** for 0.8 s: the MIDI page (CHANNEL OMNI or 1 to 16, SEND, CLOCK > DELAY). Program Change 0 to 51 loads slot 1 to 52;
Control Change sets the sound; the pedal sends Program Change and Control Change for what you do on it.

| Block | On / off | Model | Setting 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| GATE | 20 | 52 | 33 | 34 | 35 | 36 | 37 |
| COMP | 21 | 53 | 39 | 40 | 41 | 42 | 43 |
| PITCH | 22 | 54 | 44 | 45 | 46 | 47 | 48 |
| SFX | 23 | 55 | 49 | 50 | 51 | 70 | 71 |
| DRIVE | 24 | 56 | 72 | 73 | 74 | 75 | 76 |
| AMP | 25 | 57 | 77 | 78 | 79 | 80 | 81 |
| CAB | 26 | 58 | 82 | 83 | 84 | 85 | 86 |
| EQ | 27 | 59 | 87 | 88 | 89 | 90 | 91 |
| MOD | 28 | 60 | 92 | 93 | 94 | 95 | 96 |
| DELAY | 29 | 61 | 97 | 102 | 103 | 104 | 105 |
| REVERB | 30 | 62 | 106 | 107 | 108 | 109 | 110 |
| SYN | 31 | 63 | 111 | 112 | 113 | 114 | 115 |

On / off: 64 and up is on. Settings 0 to 127 are 0 to 100. Model: the whole 0 to 127 range is divided between the block's models.
CC 7 is the MASTER volume. Details, the clock sync and the limits: [MIDI.md](MIDI.md).

## Saving your sounds (v25)
- `EDITED` at the top of the PLAY screen means the sound has been edited since the preset was loaded.
- **Long press on PUSH**: SAVE page. The knob picks the slot (1 to 52; the current preset is offered first; the
  screen shows the name of the sound that will be replaced), PUSH saves, HOME cancels.
- At the very end of the list: **RESTORE FACTORY** puts the current slot back to its factory preset.
- The pedal starts on the slot saved last.
- Sounds are kept in flash (0xF0000–0xF4000, two copies): a power cut during a save leaves the previous version,
  never a damaged slot. They **survive OpenPocket updates** (the update loader only writes the app area). The sound
  is muted for about 0.1 s while saving. The block order is saved too. Sounds saved with v25–v28 are converted at
  the first start of v29 (the SYN block off, the default order).

## Tuner
Long press on HOME. The output is muted while the tuner is open (v36). The `IN L … R … dB` line at the top shows the level of each input channel (dB below full
scale): with nothing plugged in, it is the pedal's own noise floor; while playing, it is your guitar's level.

## Audio input (v26–v28)
Like the official firmware: input and output in 24 bits (v26), and the guitar read from the left channel only (v28).
The right input channel only carries constant noise (−77 dB); before v28 it was mixed into the sound, and the
high-gain amps turned it into a constant hiss, even with nothing plugged in.

## Checks run on the PC (with the pedal's own math library, `firmware/px_math.c`)
- 2,497 configurations (every block × type × extreme settings, changed while playing; signal, silence, very loud
  signal): 0 NaN.
- 237 long runs (3 s plucked string + 8 s of silence, every model at full settings): nothing runs away; only the
  delays at FDBK 100 keep repeating (by design).
- Pitch shifters: 100 % of the energy on the target note (440 Hz, 110 Hz, −7 semitones) thanks to phase alignment
  at every crossfade (SOLA).
- Digital silence: no more denormal numbers (silence used to cost 25× more CPU on the PC).
- Memory: engine 247 KB in the 336 KB POOL area (v35: delay and reverb in 16 bits; v34: 332 KB).
- Preset saving (`tests/test_store.c`): power cut simulated at 1,286 points of a save → old version kept 578 times,
  new one 708 times, never a damaged slot; no write outside the preset area.
- CPU: v25 loaded the pedal too much (DOOM SLUDGE in BYP at 89 % on v26). v27 computes the cabinets and the amp
  differently (same sound, 0.5 dB on average): about −16 % on DOOM SLUDGE, −19 % on GROOVE METAL, i.e. +16 to +22 %
  over v24 (BYP guard at 85 %).

## USB update (v24)
Over USB the pedal shows up as a MIDI device named "OpenPocket". The installer page (`docs/index.html`, published
on GitHub Pages) recognises it, installs a new version or takes the pedal back to the official firmware. During an
update the screen shows `UPDATE` and the current step; if it fails, `FAILED -N`, then the pedal restarts on the
previous version (nothing is written before the pedal has checked the package).
