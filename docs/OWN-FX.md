# LOW OCTAVE, SYN PAD and TREBLE BOOST: the Bluetooth audio image's own effects

Three effects that only the **Bluetooth audio image** has ([BT-AUDIO-TEST.md](BT-AUDIO-TEST.md), `make bt`). The release
firmware, the BLE MIDI image and the simulator do not have them: their engine is v42's, unchanged.

| Block | Model | Settings | What it does |
|---|---|---|---|
| PIT | LOW OCTAVE (the 6th) | RANGE, OCT -1, DRY, GROWL, COMP | an octave under the low notes only, the rest of the guitar untouched |
| SYN | PAD (the 12th) | ATTACK, RELEASE, TONE, MOTION, MIX | the chord you catch on the left footswitch, held as a smooth pad until the next one |
| DRV | TREBLE BOOST (the 5th, test build 40) | BOOST, RANGE, LEVEL | a germanium treble booster of the Rangemaster kind, in front of the amp |

They come after their block's other models, so every preset keeps its meaning. A preset saved with one of them plays
something else in the other images: there LOW OCTAVE becomes OCTAVER, PAD becomes ORGAN and TREBLE BOOST becomes TS9, with
the same knob values (and a pedal running them refuses such a preset from the web editor). Over MIDI, the Control Change
that picks PIT's model (54) divides 0-127 among 6 models in this image and 5 in the others, SYN's (63) among 12 and 11 and
DRV's (56) among 5 and 4, so a value stored in a controller may pick a different model in each image ([MIDI.md](MIDI.md)).

The web editor shows them, with their settings in plain values: it reads the models from the pedal. It keeps each
firmware's list under the version the pedal reports, so this image reports a version of its own (the editor's status line
shows it): `v42 FX2` since test build 40 (`v42 FX` before, without TREBLE BOOST). The editor asks for its list instead of
reusing the one of a v42 pedal, or of an earlier build, it met before.

## LOW OCTAVE

An octave under the low notes, the rest of the guitar untouched, as the OC-3's and OC-5's POLY modes. RANGE shows the note
where it stops: `A2` (20) the E string up to the 4th fret, `D3` (40) the E and A strings in open position, up to `G4` (100)
(the table below). Of two notes below RANGE the lower one gets the octave, as a bass player would play the root of a power
chord (clean: 96-99 % of the octave on it). RANGE at 0 is `LOWEST`, as on the OC-5: the octave of the lowest note of any
chord, anywhere on the neck. OCT -1 at 50 plays the octave as loud as the note. DRY 100 leaves your signal exactly as it is
(the block switched off gives the same samples). GROWL drives the octave alone through a soft clipper, as the OC-3's DRIVE
mode, so that it carries on small speakers and a PA without subwoofers; at 0 the octave stays clean. COMP evens the
octave's level, as a compressor in front of an octaver: at 100 a note 20 dB softer gets an octave only 2 dB softer, and the
hum between notes is not raised.

**The bass skips the rest of the chain (test build 42).** A bass player's sound goes to the desk clean, never through the
guitarist's reverb or delay. So the octave no longer goes on down the chain with the guitar: it leaves at the PIT block, the
guitar (DRY) goes on alone through the drive, the amp, the cabinet, EQ, MOD, DELAY and REVERB, and the octave joins at the
end, after the last block (`engine/px_dsp.c` `px_lo_*`). It still follows the preset's LEVEL, MASTER, the tuner's mute, the
volume pedal and PIT's own on/off fade. Two things change in the sound, both on purpose:

- no reverb, delay or modulation on the bass, and no amp distortion either: the guitar's distortion is cleaner without a sub
  in it;
- the octave's fundamental comes out whole: a guitar cabinet cuts below 70 to 165 Hz, and the low E's octave is 41 Hz, so
  through the chain little more than GROWL's harmonics was left of it. On a PA with subwoofers, or headphones, the same OCT -1
  now sounds much bigger than before: start lower (`-6 DB`, 25) and raise it. On small speakers GROWL still helps.

With the AMP on, the octave gets +12 dB, about what the amps add to the guitar (the eight fitted amps add +12 to +17 dB at
noon, the NAM factory models +9 to +13 dB, measured on the DI loops), so that OCT -1 sets it against the guitar as it comes
out of the amp, as it did against the guitar going in. Your own NAM captures each have their level: set OCT -1 by ear.

Where it comes out is SETTINGS' **OUTPUT** (below): in MONO and STEREO with everything else, in the middle; in GTR+BASS
alone on the right channel, the guitar on the left.

## The output: MONO, STEREO, GTR+BASS (test build 42)

The pedal's output is stereo: the I2S carries both channels to the DAC and the amplifier of OUT and the headphones
(`docs/AUDIO-SDK.md`), and M-VAVE's own firmware makes its delay and reverb stereo. Until test build 42 every OpenPocket
image sent the same sound to both. SETTINGS' **OUTPUT** row (and the editor's MIDI section) chooses, at once, saved with the
settings:

| OUTPUT | Left | Right | For |
|---|---|---|---|
| `MONO` (default) | everything | the same | a mono cable, one amp or one desk channel: as every earlier build |
| `STEREO` | the sound, with the delay, the reverb and the modulations spread; the phone's music in stereo | the same, spread the other way | a stereo cable or two channels; headphones |
| `GTR+BASS` | the guitar | LOW OCTAVE's octave alone (silence without it) | two desk channels, the bass as a bass player's |

**STEREO.** The dry guitar, the amp and LOW OCTAVE's bass stay in the middle; the DELAY's echoes and the REVERB's tail (and
their TAILS when they ring out) and seven of the eleven MOD models also reach the sides. Each adds a side signal to the left
channel and takes it from the right one, so the two channels summed are exactly the MONO sound: a desk that sums them, or a
mono speaker at the end of a stereo chain, hears what MONO gives. The side is:

- DELAY and REVERB: their wet sound a few milliseconds late (5.7 ms for the delay, 9.8 ms for the reverb; `px_side`, 2 KB
  apart, no delay memory taken);
- CE CHORUS: a second voice at the LFO's other half; DIMENSION: its two voices, one each side, as the original's two lines;
  FLANGER: the sweep's other half; PHASE 90 and SMALL STONE: a second phaser at the LFO's other half, so that the notches
  move differently on the two sides; UNI-VIBE: a second lamp; ROTARY: the horn and the drum as two microphones on their two
  sides hear them;
- the tremolos (OPTO, BIAS, HARMONIC) and VIBRATO stay in the middle: a second one of them would change what the two
  channels sum to.

A block after the DELAY, the REVERB or the MOD in the preset's order works on the middle only. With a mono cable in a stereo
jack you get the left channel alone: choose MONO then.

**The phone's music in stereo.** The phone sends stereo; every earlier build summed it to mono as it decoded it. In STEREO the
music now comes out in stereo too: beside the mono mix, (L + R) / 2, its side, (L - R) / 2, is kept at half the rate in a
second ring (2.5 KB in the RAM, `firmware/px_bt_ring.h` `bt_ring_mix2`, `firmware/bt/bt_sbc.c` `bt_sbc_frame2`) and read at the
same position: left = mix + side, right = mix - side. One channel alone stays in its channel 47 dB at 1 kHz and 19 dB at
5 kHz (the side at half the rate keeps the stereo where the ear places sounds). In MONO and GTR+BASS the music is the mono mix,
exactly as before.

**GTR+BASS.** The guitar on the left channel, LOW OCTAVE's octave alone on the right: two channels of a desk, the bass as a
bass player's (a stereo cable, or a TRS-to-two-jacks Y cable). On headphones the bass is in the right ear only. The phone's
music stays on the left.

The octave comes about 14 ms after the note (the dry signal has no delay): it is made from the note's phase, turned half as
fast, so nothing waits in a buffer; the time is the filters' that pick the note. The low band runs at 5.5 kHz. It splits by
pitch, not by string: a note below RANGE gets the octave on whatever string you play it.

Where the octave reaches on the neck, in standard tuning (a note right at the value shown gets it about 3 dB softer, a
whole tone above about 12 dB softer: the edge is soft):

| RANGE | Screen | Crossover | Gets the octave |
|---:|---|---:|---|
| 0 | `LOWEST` | 600 Hz | the lowest note of any chord, anywhere on the neck |
| 1 | `D#2` | 80 Hz | the open low E (a little softer) |
| 10 | `F#2` | 93 Hz | E 0-2 |
| 20 | `A2` | 109 Hz | E 0-4 (the 5th fret and the open A softer) |
| 25 | `A#2` | 118 Hz | E 0-6, A 0-1 |
| 33 | `C3` | 135 Hz | E 0-8, A 0-3 |
| 40 | `D3` | 151 Hz | E 0-10, A 0-5, the open D softer |
| 50 | `F3` | 177 Hz | E 0-13, A 0-8, D 0-3 |
| 60 | `G#3` | 209 Hz | E 0-16, A 0-11, D 0-6, G 0-1 |
| 70 | `B3` | 246 Hz | E 0-18, A 0-13, D 0-8, G 0-3 |
| 80 | `D4` | 289 Hz | E 0-21, A 0-16, D 0-11, G 0-6, B 0-2 |
| 100 | `G4` | 400 Hz | E, A, D up to the 17th fret, G 0-12, B 0-8, e 0-3 |

What the other settings show:

| Setting | Screen |
|---|---|
| OCT -1 | the octave's level against the note: `OFF` at 0, `-6 DB` at 25, `0 DB` at 50, `+3 DB` at 70, `+6 DB` at 100 |
| DRY | your signal's level: `OFF` at 0, `-6 DB` at 50, `0 DB` at 100 (untouched) |
| GROWL | the drive into its clipper: `OFF` at 0, `+11 DB` at 10, `+19 DB` at 33, `+22 DB` at 50, `+28 DB` at 100 |
| COMP | the compression ratio: `OFF` at 0, `1.4:1` at 33, `1.8:1` at 50, `3.6:1` at 80, `10:1` at 100 (a note 20 dB softer gets an octave 2 dB softer) |

Settings to start from. They depend on what reproduces the bass and on the pickup, so tune them by ear (they were worked out
on paper and with recordings, not yet on a stage):

| Situation | RANGE | OCT -1 | DRY | GROWL | COMP | Why |
|---|---|---|---|---|---|---|
| PA or amp with a subwoofer | `D3` (40) | `0 DB` (50) | `0 DB` (100) | `OFF` | `1.4:1` (33) | the octave's fundamental (41 Hz under a low E) comes out: keep it a clean sine |
| No subwoofer: small PA, combo amp, stage monitor | `D3` (40) | `+3 DB` (70) | `0 DB` (100) | `+19 DB` (33) to `+22 DB` (50) | `1.8:1` (50) | 41 Hz barely leaves a 10-12" speaker: GROWL's harmonics (3, 5 times the octave) let the ear hear it |
| Headphones, in-ear monitors | `D3` (40) | `0 DB` (50) | `0 DB` (100) | `OFF` | `1.4:1` (33) | they reproduce the sub; GROWL only for the sound |
| Piezo pickup (acoustic) | `C3` (33) | `-2 DB` (40) | `0 DB` (100) | `OFF` | `2.7:1` (70) | a piezo booms in the lows and slaps hard: a lower RANGE keeps the boom out, more COMP evens the slaps |
| Magnetic pickup (electric, or a soundhole pickup) | `D3` (40) to `F3` (50) | `0 DB` (50) | `0 DB` (100) | to taste | `1.6:1` (40) | an even low end: little COMP needed |
| Bass line that moves up the neck under chords | `LOWEST` (0) | `0 DB` (50) | `0 DB` (100) | as above | as above | the octave follows the lowest note wherever it is |

## SYN PAD

A smooth chord held under your playing, for as long as you want. Strum a chord and press the **left footswitch** (or press
it, then strum: it waits for the strum and skips the pick): the pad catches 0.37 s of the ringing chord and swells it in over
ATTACK, then holds it, steady, while you play over it. Press again on the next chord: the pad crosses over to it over the
same ATTACK. **Hold** the footswitch 0.7 s to let the pad fade out over RELEASE. Your guitar itself goes through untouched.
The pad plays the chord as your guitar sounded it (it is not an oscillator): a clean or lightly driven tone in front of it
gives the smoothest pad. It runs at 5.5 kHz in the synth block's own memory.

**The footswitches.** While the preset has a PAD on, the left footswitch is the pad's and no longer steps back a preset
(the right one still steps forward); a catch never changes the preset's settings or the page you are on. Both footswitches
together still open the tuner, as in v42: left then right within 0.15 s opens it and lets the pad go; right then left is
v42's (the step back, then the tuner). On SAVE, MIDI, MOVE, the tuner and the diagnostics the left footswitch does what it
did before. With the PAD off, or another SYN model, the left footswitch is the previous preset, as always.

**A MIDI footswitch** can catch the chord too: Control Change 64 (sustain) at 64 or more, with a PAD on in the preset; 0 to
63 does nothing. It catches on every page, as the footswitch did on v40.

| Setting | Screen | What it sets |
|---|---|---|
| ATTACK | `0.1 S` (0) to `6.0 S` (100); `1.0 S` at 40, `1.6 S` at 50, `3.0 S` at 70 | the swell, and the crossing into a new chord |
| RELEASE | `0.2 S` (0) to `10 S` (100); `1.8 S` at 40, `2.7 S` at 50, `5.0 S` at 70 | the fade when the footswitch is held |
| TONE | `300 HZ` (0) to `2400 HZ` (100); `690 HZ` at 40, `850 HZ` at 50, `1290 HZ` at 70 | dark to open |
| MOTION | `OFF` (0) to `15 CT` (100); `3 CT` at 20, `6 CT` at 40, `8 CT` at 50 | still, to a slow chorus (the grains' detune) |
| MIX | `OFF` (0), `-6 DB` (25), `0 DB` (50), `+3 DB` (70), `+6 DB` (100) | the pad's level; at `0 DB` about as loud as the chord |

Settings to start from (worked out from what the settings do, not yet tried on a stage):

| Situation | ATTACK | RELEASE | TONE | MOTION | MIX |
|---|---|---|---|---|---|
| A chord held under a solo | `1.6 S` (50) | `2.7 S` (50) | `850 HZ` (50) | `6 CT` (40) | `0 DB` (50) |
| Slow swells, ambient | `3.0 S` (70) | `5.0 S` (70) | `690 HZ` (40) | `9 CT` (60) | `-3 DB` (35) |
| Following the song's chords | `0.3 S` (20) | `1.1 S` (30) | `1290 HZ` (70) | `3 CT` (20) | `0 DB` (50) |

## TREBLE BOOST

A one-transistor germanium booster of the Rangemaster kind. In front of an amp on the edge of breakup (a NAM capture of a
Vox or a Marshall) it cuts the lows that make it woolly, lifts the upper mids and the top, and pushes the amp harder: it
breaks up sooner and the notes cut through. It is made by hand from what the circuit does, not fitted to a capture as the
other four drives are (`engine/px_dsp.c`, its row of `DV` and `TB_HZ`, `TB_DB`):

- the input capacitor's high-pass, picked by RANGE: TREBLE at 1.6 kHz (the original's), MID at 450 Hz, FULL at 90 Hz;
- the guitar's resonance with that capacitor: a broad peak, +2.5 dB at 2.6 kHz;
- the transistor's soft, lopsided clipping (one half of the wave goes first: even harmonics), with BOOST setting 12 to
  36 dB into it;
- its rolled-off top, from 6.5 kHz.

| Setting | Screen | What it sets |
|---|---|---|
| BOOST | 0 to 100 | how hard it pushes. At noon as loud as the guitar without it; 0: -12 dB; 100: +8 to +10 dB, the transistor clipping more |
| RANGE | `FULL` (0 to 33), `MID` (34 to 66), `TREBLE` (67 to 100) | the low cut: a full-range boost, a mid boost, the treble booster |
| LEVEL | 0 to 100 | its output: noon as above, 100 is +6 dB more |

Its level was measured as `tools/calib_levels.py` measures the other models: the five DI loops of `assets/loops` at the
pedal's level, in front of TWIN at noon and the 1x12 US OPEN, BS.1770 loudness. With LEVEL at noon:

| BOOST | 0 | 25 | 50 | 75 | 100 |
|---|---|---|---|---|---|
| FULL | -11.4 dB | -5.5 dB | 0 dB | +4.8 dB | +8.4 dB |
| MID | -11.6 dB | -5.7 dB | 0 dB | +5.2 dB | +9.2 dB |
| TREBLE | -11.8 dB | -5.8 dB | 0 dB | +5.4 dB | +9.8 dB |

Settings to start from (worked out from what the settings do, not yet tried on a stage):

| Sound | BOOST | RANGE | LEVEL | In front of |
|---|---|---|---|---|
| The classic treble-boosted lead | 75 to 100 | `TREBLE` | 50 | a Vox or Plexi capture on the edge of breakup |
| A thicker lead, more crunch | 60 to 75 | `MID` | 50 | the same |
| A boost for the solo, about +6 dB | 50 | `FULL` | 100 | any amp; DRV on and off by Control Change 24 |

## What they cost, and where it comes from

The Bluetooth image had almost no memory left (test build 14: 40 bytes free in the engine's own RAM region, the POOL, and
an estimated 1 to 4 KB in the RAM code window). The two effects are paid for inside that image only (`engine/px_dsp.h`
`PX_OWN_FX`, set by `firmware/build.sh` under `OP_BT`):

- **The same sound in less code.** The drive and delay code no model reaches is left out: the delay's generic loop (each
  delay type has its own), TAPE's and SPACE ECHO's loop without flutter (at WOW 0 the flutter is 0 and stays still), and
  the drive modes none of the four drives uses (and MUFF's buffer, 1 KB). Every existing model gives the same output, bit
  for bit (`tests/ownfx_equiv.sh`, 5,460 runs).
- **The delay ring 456 ticks shorter.** The effects' state (912 bytes) sits where the delay ring ended, so the engine keeps
  its size and every other part of it its place. The longest delay was still 2.0 s (TIME 100). Since test build 24 the
  image stops every delay at 780 ms (REVERSE at 390 ms), for another reason: the ring's memory went to BLE MIDI
  ([BT-AUDIO-TEST.md](BT-AUDIO-TEST.md), "Test build 24").
- Their hot code runs from RAM, like the rest of the audio path.
- TREBLE BOOST (test build 40) is one more row of the drive table and a few lines for its RANGE and its level: it runs the
  drive code the other four run, so on the pedal it costs what TS9 costs.

Estimates from a stand-in compiler (CI's build prints the real figures: `hi RAM`, `pool tail`, `RAM`): against test build
14, RAM code 3.3 to 4.1 KB less (the effects about +3.8 KB, the code left out about -7.1 KB) and the RAM code window's data
832 bytes less, so the window has 4 to 5 KB more free; the flash image about the same (the RAM code is stored in it too;
the flash code grows by about 3 KB); the POOL, the main RAM and NOINIT as before (40 bytes free in the POOL).

## On the pedal

Not measured yet: the CPU they take there, and the second core's stack. On a computer LOW OCTAVE costs less than POLY
OCTAVE (its low band runs every 8th sample) and a held PAD about as much as SITAR, the lightest SYN model; a PAD's catch
does its one-off work in a single block.

- `node tools/op.mjs dsp` with LOW OCTAVE (`LOWEST`, GROWL 100, COMP 100) in front of a NAM preset, the music playing: `DSP`
  and late blocks against the same preset without it.
- With a PAD on a non-NAM preset, after a catch: the log's c1 line, `K` (the second core's stack, of 2,816 bytes).
- Around a catch with a heavy NAM preset: `PK` and late blocks.

## Checked on a computer

- `tests/engine/lowoct.c` (18 checks, as the image builds the engine and with the host's defaults, also under ASan and
  UBSan): a low E gets its octave (41.2 Hz, 100 % of the output on it), a high E none; of two notes below RANGE the lower
  one's octave (E5 98 %, A5 99 %, E + A 96 %); `LOWEST`: a G chord high on the neck 98 % on the G's octave; the octave's
  attack at most 15.6 ms after the note's; the x8 upsampling's images 76 dB under the octave; COMP 100: a note 20 dB softer
  gives an octave 2.0 dB softer, hum stays at -103 dB; a 30 Hz rumble 9.6 dB under; GROWL 100: 32 % third harmonic, about
  the same level; the knobs glide; the screen texts.
- `tests/engine/pad.c` (15 checks, the same builds): a caught chord held within 2.2 dB over 10 s, at the chord's level at
  MIX 50, on its own notes; ATTACK 1.6 S: half the level after 0.92 s; a second press crosses over; RELEASE 2.7 S: -18 dB
  after 1.8 s; pressed before the strum: the ringing chord, not the pick; no clicks (largest step 0.017 against 0.138); four
  chords in quick succession; MIX and TONE turned while held; a press made before PAD was chosen is forgotten.
- Both effects give v40's output bit for bit (LOW OCTAVE 48 runs, PAD 24: settings, block sizes 64, 48, 16 and 1, knob
  turns, catches, a stop).
- `tests/engine/treble.c` (7 checks, the same builds): the model and its knobs, RANGE's texts; small signals, 3 kHz over
  200 Hz: TREBLE +19.2 dB, MID +9.9 dB, FULL +2.9 dB; driven hard, the 2nd harmonic 28 dB and the 3rd 10 dB under the
  note; BOOST raises the level and the harmonics at each step; at noon, at every RANGE, the loudness of the guitar without
  it within 0.5 dB.
- `tests/engine/bassout.c` (22 checks, the same builds): LOW OCTAVE's octave out of the chain, and the output's three ways. Through TWIN, the 1x12 US OPEN
  and a long HALL the guitar comes out the same with the octave on and off (the tail differs by -114 dB), while the note plays
  the octave adds its 41.2 Hz and nothing at the note (-63 dB); OUTPUT GTR+BASS: the left channel is the guitar alone, bit for
  bit, the right the octave alone, silent without LOW OCTAVE; a NAM amp on two cores (PRE, NAM, POST a block apart, as the
  firmware runs them): the plain engine's output bit for bit, a block later, both channels; PIT switched off while the octave
  plays fades it (no step); the volume pedal's heel silences it; its +12 dB lies within the amps' gains. STEREO, inline and
  through the wet lanes: left and right summed are the MONO sound (the largest difference -162 dB), the delay's echoes and
  the reverb's tail reach the sides (the side 5 to 7 dB under the middle), with no DELAY or REVERB both channels are the
  MONO sound, a reverb switched off with TAILS rings out wide; every MOD model sums to the MONO sound, seven spread (the
  side 4 to 13 dB under the middle on a bright note), the tremolos and VIBRATO stay in the middle; a NAM amp on two cores,
  with a chorus and a delay, gives both channels bit for bit.
- `tests/ownfx_equiv.sh`: every existing model in this image's engine against the same engine without `PX_OWN_FX`, 5,460
  runs bit for bit (every block x model x 8 settings, random chains, NAM chains through the two cores' pipeline, the 52
  factory presets, every delay over a TIME x FEEDBACK x TONE x WOW grid, turned while playing and at the top for 6 s, every
  drive over 625 knob settings).
- `tests/uisim.c` built for this image: v42's 48 screens byte for byte, the PAD's 16 footswitch gestures above, every value
  text within its box, both pages drawn whole. `tests/midiio.c`: CC 64.
- The engine's tests built as the image builds it: 2,551 configurations with no NaN, 243 long runs (the same 7 delays at
  FDBK 100 hold on as without the effects), the two cores' pipeline.
- Without `PX_OWN_FX`, `engine/px_dsp.c` and `firmware/openpocket.c` preprocess to v42's text for the release, the BLE and
  the Bluetooth flags, and the simulator's WebAssembly sources too: those images are unchanged.
