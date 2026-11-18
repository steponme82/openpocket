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

## USB RETURN: the computer's sound into the chain (test build 43)

With USB audio on ([USB-AUDIO.md](USB-AUDIO.md)), SETTINGS' **USB RETURN** brings the computer's sound into the chain right
before a block (or at its start), and **GUITAR CUT** cuts the guitar there while the computer plays: a synth made from the guitar
on the computer goes through the blocks after that point, and the ones before never touch it.

`px_engine_ret(e, part, x, at, cut)`, before each `px_engine_part`: x the computer's sound for that call (mono, at the input's
scale), at a block (`PX_B_*`), `PX_RT_IN` or `PX_RT_END`, cut the guitar's fade (256 samples, linear, both ways). The place is
found while the preset's order is read: where that block sits, on or off, so a block off still has a place (where the next
block on is). With a NAM amp on two cores, whichever part holds the place mixes it (before the amp: PRE's end; after it: POST),
so a block's return is mixed once; PRE and POST keep their own settings, as they run at once. LOW OCTAVE's octave and STEREO's
side made before the place fade with the guitar. `PX_RT_NONE`, or no sound with the guitar kept, leaves the chain bit for bit
as build 42's (`tests/engine/usbret.c`).

## Test build 44: five models put right, and TAPE FLUTE

A player's verdict on models this image shared with the release: SHIMMER out of tune, BARITONE sluggish, MONO SYNTH tracking
badly, SITAR and STRINGS not sounding like what they are named for. In this image only (the release's are unchanged, bit for
bit: `tests/ownfx_equiv.sh` leaves these out by name):

- **SHIMMER**: its octave is in tune. Its shifter crossfaded two taps with no regard to phase, so the octave landed on a comb
  86 Hz apart instead of on twice the note: on held notes E 330 Hz +41 cents, B 247 Hz +42, A 440 Hz -17, G 196 Hz -51. Each
  tap now comes in where it is in phase with the other (`ox_voice`, `ox_align`: SOLA, as PITCH's, comparing 5.8 ms and reaching
  one period of 69 Hz), within 6 cents from 73 Hz to 988 Hz.
- **BARITONE**: its sound comes 8 to 11 ms after the pick on average, against 20 to 25 (worst 12 to 22 ms, against 31 to 40). A
  tap plays loudest in the middle of its sweep, about (window + one period) / 2 late: the window is now 448 samples instead of
  1400, and the search takes the first offset in phase rather than the best match anywhere in 512 samples. In tune within a few
  cents, its level as steady as before.
- **MONO SYNTH**: a new tracker. The old one counted zero crossings after a fixed 110 Hz low-pass (a note at 660 Hz came out of
  it 60 dB down, under hum and noise: it was lost above about 600 Hz), and after a silence it opened at the last note's pitch
  until it caught up. Now YIN on the note's fundamentals (every 128 samples, at an eighth of the rate, then refined at the full
  rate between samples), a new note taken when heard twice and played at once, the gate shut until the first clear estimate.
  On twenty test notes: in tune after 16.5 ms on average (84 before), a wrong pitch 141 ms in all (2.1 s before), no octave
  error.
- **SITAR**, rebuilt as an Indian sitar. The jawari: the string presses on the curved bridge on one side of its swing, so the
  note is read up to 0.54 ms late in step with its own wave (BUZZ): the buzz is deepest while the note is loud and narrows as it
  dies, and its partials stay harmonic. The tarab: six sympathetic strings an octave above KEY on its pentatonic (Sa Re Ga Pa Dha
  Sa'), bright, tuned between samples, ringing DECAY long (0.8 to 4 s) on what the note excites in them (SYMPATH). The body: the
  low end out, the nasal formant at 1.8 kHz, the bridge's sizzle at 3.6 kHz. Set KEY to the song's key.
- **STRINGS**, rebuilt as an orchestral section. The bow: the note's level held (up to +20 dB), swelled in over ATTACK and again,
  softly, at each new pick (the bow turning), let go over RELEASE; the rosin: a little of the top through a soft clip; the body:
  the guitar's honk at 1.1 kHz out, the violins' wood at 280 Hz and their bridge hill at 2.8 kHz (TONE); the section: four
  players 7 to 16 ms apart, each with its own vibrato and drift. OCTAVE: the cellos an octave down up to noon, then the violins
  an octave up over them, in tune (`ox_voice`). CHOIR keeps its old code.
- **TAPE FLUTE**, new (the 13th SYN type): a Mellotron-like flute played at MONO SYNTH's tracker's pitch, one note at a time:
  the flute's tone, its breath with the chiff at each note (BREATH), the tape's wow and flutter (WOW), its soft saturation and top
  (TONE), a swell over ATTACK (20 to 500 ms), DRY for the guitar under it.

Each one's level at noon is TWIN + 1x12's alone, as every other model's (BS.1770 over the five loops; `engine/px_trim.h`). The
web editor reads the catalogue again (INFO "v42 FX3"). `tests/engine/synfix.c` checks all six.

## Test build 45: MONO SYNTH through hum, TAPE FLUTE on the note, WHAMMY one way, WAH

A player's second round, in this image only (`tests/ownfx_equiv.sh` leaves WHAMMY out by name now, WAH is new):

- **MONO SYNTH** tracked the bundled loops and failed on a guitar. Hum lifted every dip of YIN's normalised difference over the
  fixed threshold (0.2), so the tracker heard nothing and the last note went on; and with hum or noise above the gate's level
  (0.0015) the gate never shut, so the synth played the last note on at 60 % through every pause. Now the period is the first
  dip within 0.15 of the deepest (also safer at a pick whose 2nd harmonic leads: half the period dips less deep); 8 estimates
  in a row that hear no note (93 ms) let the note go, and the gate waits for the next clear one; the gate opens at a third of
  the level (a single coil with its volume down, the loops at a quarter: 4 notes in 10 heard, now all). The octave check and
  the 46 ms before an octave are gone: the riff's high notes are C6s (their C5 50 dB down), and both played them an octave
  low. On the lead loop with a 50 Hz hum and its 3rd and 5th 15 dB under the playing (`build/shim`, not in the tree): 557
  notes right of 598 (build 44: 497), 2 blips back and forth within 40 ms (19), the synth sounding in none of the pauses
  (all 20); clean, 587 (573). A steeper filter against the hum (70 Hz, 24 dB) was tried and dropped: it thinned the low E's
  fundamental into octave errors.
- **TAPE FLUTE** plays the nearest semitone (A 440) straight: it followed the guitar, gliding, and a note picked a little sharp
  slid into place. It holds while the guitar stays within 60 cents of it (vibrato, a guitar a little out of tune) and moves when
  a new note is heard clearly (YIN under 0.15) three times in a row on the same semitone, 13 ms: a string still ringing under
  the new note blurs the period, and two estimates flipped the flute a semitone and back (on the loops: 3 such blips on the lead
  instead of 15, 3 on the chugs instead of 73). A guitar half a semitone off is between two semitones: no window can tell.
- **WHAMMY**: SHIFT is four intervals (`-2 OCT`, `-1 OCT`, `+1 OCT`, `+2 OCT`); the expression pedal (CC 117) goes from the note
  at the heel to the interval at the toe. SHIFT was -24 to +24 semitones in steps of 0.48 with the screen rounding (49, 50 and
  51 all read 0: two of them 48 cents off), and a pedal learned onto it swept all of it. Its zones keep a saved SHIFT's meaning
  (+12 was 75: `+1 OCT`; +24 was 100: `+2 OCT`). At the heel the note itself: the shifter's copy at ratio 1, up to 32 ms late,
  made a comb with MIX under 100; it crossfades in over 10 ms as the pedal leaves the heel. **PEDAL**, a fourth knob: the
  position without an expression pedal, and the knob to learn one onto. The expression pedal's ends are 4 values wide
  (0 to 3, 124 to 127; the volume and the wah's too): a pedal that never sends 0 or 127 still reaches the note and the
  interval exactly.
- **WAH**, new (the 8th SFX type): a wah pedal. A resonant band (Chamberlin's state variable filter) swept on a wah's
  exponential taper from the heel (RANGE: 280 to 490 Hz, 369 at noon) to 2.65 octaves up (2.3 kHz at noon: a Cry Baby's), by
  the expression pedal, else PEDAL; its peak 20 dB over the lows; Q 2 to 10; LEVEL +-12 dB. At noon as loud as TWIN + 1x12
  alone (+10.3 dB: the band takes most of a guitar away). The AUTO WAH's pedal (CENTER's place) stays as it was.

The web editor reads the catalogue again (INFO "v42 FX4"). `tests/engine/synfix.c` (7, 8) and `tests/engine/exp.c` check them.

## Test build 47: 12-STRING steady

12-STRING moved a lot heard alone. Its octave is in tune (within half a cent from 82 Hz to 659 Hz, on held notes); the motion
was DETUNE's: up to 15 cents on the octave (7.5 at noon), which beats against the note's own 2nd harmonic (three times a second
at E4), and a unison copy of the guitar on a delay swept by a slow LFO. In this image DETUNE is 0 to 3 cents (its value reads
`1.5` at noon), and the unison copy is gone (`tests/ownfx_equiv.sh` leaves 12-STRING out now). DETUNE 0 holds the octave dead in
tune: nothing moves but the guitar. On held notes (12, 82 Hz to 659 Hz, OCTAVE 70; the level against the guitar's in 50 ms
windows, `build/shim`, not in the tree), at noon the level moves 2.8 dB a second on average (build 46: 10.4; the worst note
6.7, was 25.8), at DETUNE 0 1.4 dB a second, the same as before. Chords move as they did (about 6.5 dB a second: the octave's
splices against the chord's own notes). The arpeggio loop is 0.9 dB quieter without the unison copy, the chords as loud.
`tests/engine/synfix.c` (9) checks it. The web editor reads the catalogue again (INFO "v42 FX5": DETUNE's text).

## Test build 48: PRESET LVL down to silence

The GT page's PRESET LVL (the preset's level, at the chain's end) went from -15 dB to +15 dB, 0.3 dB a step; a pedal learned on
it could not be a volume pedal. In this image 0 is `-INF` (silence), 1 to 19 fall 2 dB a step from -9 dB (`-47 DB` at 1), so a
pedal learned on it fades the sound out smoothly, and 20 and up are as before (50 = 0 dB, 64 = +4.2 dB, 100 = +15 dB). Its
change glides over 23 ms like any knob (no click, `tests/engine/exp.c`). Being at the chain's end, it takes the delay's and
the reverb's tails down with it: for a volume pedal that lets them ring, CC 117's own volume (before the first DELAY or
REVERB) is the one. `tests/ownfx_equiv.sh` compares PRESET LVL from 20 up. The web editor reads the catalogue again (INFO
"v42 FX6": PRESET LVL's texts).

## Test build 51: the expression pedal's volume, and its two jobs

CC 117's volume (the expression pedal's, test build 25) and a two-job pedal's ([MIDI.md](MIDI.md), "Two jobs for one pedal")
act where SETTINGS' **VOL PEDAL** says: **AT INPUT** (the default) right after the input (after the GATE when it comes first,
so the gate still opens on the guitar's own level): the guitar's own volume knob, a drive cleaning up as it goes down
(`tests/engine/exp.c`: through a saturated drive, half way is 0.3 dB down); or **BEFORE DLY**, before the first DELAY or REVERB
(else at the chain's end), as before: the tone kept, half way 18 dB down. The repeats and the reverb's tail ring on in both;
LOW OCTAVE's octave fades by its own release AT INPUT, and is cut with the chain BEFORE DLY (`tests/engine/bassout.c`). With a
NAM amp right after the GATE, the volume AT INPUT comes before the network (pass 0's end, as USB RETURN's).

A two-job pedal (`px_exp_dual`): the engine is told the block whose on and off pick the job. On: a WAH's or AUTO WAH's sweep
or a WHAMMY's interval from the pedal (job 1 or 2, as CC 117 always was), another setting moved by the firmware with the volume
at unity (job 3); off: the volume (job 0), at unity until the pedal moves. Another block (a preset change) forgets the position.
The web editor reads the catalogue again (INFO "v42 FX7", editor protocol 7).

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
- `tests/engine/usbret.c` (23 checks, the same builds): USB RETURN. Nothing given, or silence with the guitar kept, at every
  place: build 42's output bit for bit; before DELAY with the guitar cut, a drive, an amp and a cabinet on or off give the same
  output (the computer's sound skips them) while the echoes and the tail play it, inline and through the wet lanes; MIX adds the
  two (within 50 dB: the delay's and the reverb's own rounding); before a block that is off it is where the next one on is; a
  REVERB moved before the amp takes the computer's sound through the amp; GUITAR CUT at the output: half at 128 samples, gone
  after 256, back the same way, no step larger than the sine's own; LOW OCTAVE's octave cut with the guitar; a NAM amp on two
  cores gives the plain engine's output bit for bit, a block later, at five places, cut or not; STEREO's two channels sum to
  the mono sound.
- `tests/engine/synfix.c` (9 checks, the same builds): SHIMMER's octave on held notes from 73 Hz to 988 Hz within 8 cents;
  BARITONE at -5 and -12 semitones 8 to 13 ms after the pick on average, low notes within 12 cents; MONO SYNTH on twenty notes
  (picked and legato, 82 Hz to 1 kHz, weak fundamentals, a bend) in tune after 35 ms at most on average, a picked note wrong for
  under 15 ms, no octave error; SITAR's jawari at least doubles the share of the sound above 2 kHz and leaves the pitch, and its
  tarab rings in KEY after the note (G4 at least 12 dB over F4, off the scale); STRINGS swell in (the pick 12 dB under) and hold
  the level within 6 dB while the guitar dies, the cellos in tune; TAPE FLUTE plays the note within 20 cents, nothing to
  silence, and swells in.
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
