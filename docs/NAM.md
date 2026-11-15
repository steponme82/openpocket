# Neural amp models (NAM A2) on the pedal (v37)

Since v37 the AMP block has a ninth type, **NAM**, that plays a [Neural Amp Modeler](https://www.neuralampmodeler.com/)
network instead of an amp built from circuit stages. It plays the networks of NAM's **A2** architecture, at its
3-channel size, **A2-Lite**. That is the size the pedal's CPU can run.

## The knobs

| Knob | What it does |
| --- | --- |
| MODEL | which network: the 12 factory models, then the 16 user slots (`U1 EMPTY` ... until you load one) |
| GAIN | the level into the network, ±12 dB around the level of the captures (noon) |
| BASS, MID, TREBLE | an EQ after the network: ±10 dB at 120 Hz, ±8 dB at 800 Hz, ±10 dB above 2.5 kHz |

The models are amp captures without a cabinet, so keep a CAB after the AMP. Every model plays at the same loudness
(-20 LUFS on the reference clip `docs/nam_di.wav`), so switching models keeps the volume.

## Factory models

Twelve A2-Lite networks, trained for the pedal at 44.1 kHz (`tools/train_a2.py`). Each one learnt to sound like a
community capture of a real amp, playing real DI guitar. Accuracy is ESR, the error on 20 s of guitar the training never
heard: 0.01 is very close, and below about 0.05 the two are hard to tell apart.

| MODEL | Amp | Capture (author) | ESR |
| --- | --- | --- | --- |
| TWIN | Fender Twin Reverb (normal, bright) | Tim R Fender TwinVerb Norm Bright (Tim R) | 0.006 |
| AC15 | Vox AC15 Top Boost | Phillipe P VOXAC15-TopBoost (Phillipe P) | 0.045 |
| SUPER 59 | Magnatone Super 59 Mk II | Tim R Magnatone Super 59 Mkii Ch1 Br (Tim R) | 0.009 |
| SHIRL CL | Friedman Dirty Shirley Mini, clean | Sascha S DirtyShirleyMini_Clean_B1_M6_T7_MV10_G4 (Sascha S) | 0.014 |
| JCM900 | Marshall JCM900 50 W Dual Verb | Tim R JCM90050WDualVerbCHAG6 (Tim R) | 0.011 |
| SHIRLEY | Friedman Dirty Shirley Mini, crunch | Sascha S DirtyShirleyMini_crunch_G6 (Sascha S) | 0.033 |
| JCM2000 | Marshall JCM2000 DSL, lead | Tim R JCM2000 L2 G6 (Tim R) | 0.042 |
| MARK IV | Mesa Boogie Mark IV | Roman A LT_MESA_MARKIV_1 (Roman A) | 0.041 |
| JVM OD2 | Marshall JVM, OD2 | Phillipe P JVM-OD2-RD-NoDrive-ESR0,003 (Phillipe P) | 0.056 |
| PRO MOD | Splawn Pro Mod, 2nd gear | Tim R Splawn Pro Mod 2nd Gear - G12 (Tim R) | 0.052 |
| 5150 | Peavey 5150 (block letter), boosted | Helga B 5150 BlockLetter - Boosted (Helga B) | 0.064 |
| 6505 RED | Peavey 6505+ red channel, MXR drive | Helga B 6505+ Red ch - MXR Drive (Helga B) | 0.124 |

High-gain captures are harder for a network this small: their error is higher (the 6505+ most), while the clean
and crunch models are very close. A larger A2 would do better, but would not fit the pedal's CPU.

The factory presets use them; the fitted amps of v34 are still there as the other 8 AMP types.

## Your own models (TONE3000 and others)

1. Get a capture in the **A2** format: on [TONE3000](https://www.tone3000.com/) and in NAM's own trainer, A2 files
   are offered beside the older formats. The pedal reads the A2-Lite part of an A2 file. The older A1 files
   ("standard", "lite", "feather", "nano") do not run on the pedal and are refused with that reason.
2. Open the web editor (`editor.html`), connect the pedal, and in **Models** press **Add .nam files** (several at once,
   or drop them on the panel). The editor checks each file, measures its level and sends it to the next free slot
   (about 2 seconds each; the sound mutes a moment while the flash is written). A file the pedal cannot play is listed
   with the reason. An empty slot's own button loads a file into that slot.
3. In a preset, set the AMP type to **NAM** and choose the capture in **MODEL** (your captures are listed by slot).

Or press **Browse TONE3000** in the same section: TONE3000's own tone browser opens in a pop-up (sign in there; it shows
A2 captures only), and the chosen tone comes back with its creator, its description and its A2 models; **Get** puts one
into the slot chosen beside it. While signed in, the editor also lists your TONE3000 favorites and downloads, and the
trending and latest tones. This uses TONE3000's public API ([tone3000.com/api](https://www.tone3000.com/api)), its
"Select" flow and the lists a free application may read (not its search, which stays in TONE3000's own browser):
OAuth 2.0 with PKCE and OpenPocket's publishable key (`T3K_CLIENT_ID` in `docs/pxt3k.js`), the sign-in coming back
through `docs/t3k.html`. The access token lives in the page only while it is open. It needs that key and
`https://isod89.github.io/openpocket/t3k.html` registered as a redirect URI in TONE3000's settings. A site that publishes
the editor under its own TONE3000 app gets that app's publishable key, chosen by the page's address (`T3K_SITE_KEYS` in
`docs/pxt3k.js`): the fork at `https://steponme82.github.io/openpocket/`, whose key has that site's `t3k.html` as its
redirect URI. Every other address uses `T3K_CLIENT_ID`. Only publishable keys: the sign-in is PKCE, with no secret.

A browser that opens no second window (an in-app browser such as MIDIWeb, the Web MIDI browser an iPhone needs for
Bluetooth MIDI) takes TONE3000 in the editor's own tab instead, after saying so. The code verifier, the state and the
address to come back to wait in that tab's session storage; `t3k.html` checks the state, signs in, gets the tone's A2
captures (the one there is, or the ones chosen there) ready for the pedal, and goes back to the editor, which puts them
in the library. Connect the pedal and send them from there. The token stays in `t3k.html`'s memory and ends with it, and
the redirect URI is the same `t3k.html`, so the same registration serves both ways.

The file buttons (**Add .nam files**, an empty slot's button, **Replace with a file**, **Import** for presets) are
labels of real file inputs with no type filter, so iOS offers `.nam` files instead of greying them out; each file is
checked by its name and its content after it is picked.

**Names (the Bluetooth test image, test build 40).** A capture taken from TONE3000 gets the tone's first words before a
short model name, 11 characters at most ("VOX AC30 VX", "KLON GAIN 2" instead of "VX" or "GAIN 2"; `docs/pxlib.js`
`smartName`). And **Rename…** works on any capture in a pedal with editor protocol 5: the pedal renames the slot itself
(`NAM_RENAME`), so a capture loaded from another browser or from the phone can get a name you read.

**The library.** The pedal stores a capture but cannot send it back. So the editor keeps every capture you add in the
browser (IndexedDB, on that computer only), with where it came from. That is what lets it rename a capture, move it to
another slot or swap two (the presets that use them are rewritten to follow), and keep more captures than the pedal has
slots: **Remove from the pedal** leaves the capture in the library, **Send to the pedal** brings it back. A capture
loaded from another browser shows as "not in this browser's library": load its file again to rename or move it.
Each slot shows how many saved presets use it, and removing a capture names them first.

The 16 user slots are in the pedal's flash at 0xC0000..0xE0000, outside the area a firmware update rewrites: your
models stay through updates.

Most captures are made at 48 kHz. The pedal runs at 44.1 kHz, so such a model plays with its tone about 1.5
semitones lower (the amp's filters sit 8% lower). The editor says so when it loads one. The factory models are
trained at 44.1 kHz and are not affected.

## How it runs

- `engine/px_nam.c`: the network in fixed point (activations with 20 fraction bits, weights with 24, 64-bit sums),
  checked against a float reference (`tools/nam_a2.py`) for every factory model by `tests/test_nam.py`: about 99 dB
  signal to error.
- Blocks of 32 frames, layer by layer. On the CPU, each output channel of a layer is computed for 4 frames at a time
  straight from the layer's history ring (`rdot`): for each kernel tap, the 4 frames' 3 channels sit side by side in
  the ring (12 words), so a weight is read once for 4 frames and no input is copied (where a tap's 4 frames wrap
  round the ring, those 12 words are copied first). Then LeakyReLU, the head sum and the residual (`post`). On the
  pedal these are hand-written (pi32v2 assembly: multiply-adds paired with loads, 3 words moved by one instruction,
  no branch per sample), because this CPU has no data cache in front of its RAM and a taken branch costs about 8
  cycles. At start-up they are run against the C versions on a test pattern, and the C versions are used if they
  differ by a single bit. On this CPU a multiply-add paired with the load of the value it needs next waits for it
  (3.2 cycles, against 2.2 without the wait), so the routines for 6, 15 and 16 taps (`rdq`, written by
  `tools/gen_rdq.py`) are unrolled with each value loaded 2 to 4 steps before its use. The start-up timing compares
  them with the first `rdot` and with gathering each frame's inputs into one row first (then the hardware-loop dot
  routine), and keeps the fastest.
- The dot products can also run on the chip's vector unit (in its FFT block: one command computes a row of weights
  against many X rows, beside the CPU; each frame's inputs to the layer are then gathered into one X row). The block
  is cut in two halves, and half B of a layer never needs the next layer's half A: so while the unit computes one
  half, the CPU gathers or posts the other, and may compute the last 4 or 8 frames of a half itself. The unit costs
  about 4.8 cycles a multiply-add on the pedal, about as much as the CPU. Its inputs (the X rows and a copy of the
  weights, 13 KB) sit in seven of the data-cache ways turned into RAM at boot (`firmware/px_cacheram.h`, melodee's
  routine; the data cache only serves the flash), so that it does not read the RAM the CPUs work in; the start-up
  check runs the unit there too, and the ordinary RAM is used if it differs.
- Two cores: frame t of a layer needs only frames t and earlier of the layer before. So CPU1 takes the first 12 or 16
  frames of each 32-frame block through all 23 layers without ever waiting, and CPU0 the rest, waiting before each
  layer until CPU1 has posted the one before (one counter). With a NAM amp in two-core mode the audio block runs on
  CPU0 (in the audio interrupt) as a pipeline (`dsp_pipe` in `firmware/px_audio.h`, `px_engine_part`): CPU1 first runs
  the effects after the amp on the previous block's network output while CPU0 runs the effects before the amp on this
  block, then both run the network (one block, 2.9 ms, more latency; `tests/engine/pipe_test.c` checks it gives the
  one-core output one block later). In the pipeline the roles are the other way round (`pxn_serve`): CPU1 comes to
  the network after its effects, so it takes the last frames of each 32-frame block and waits per layer for CPU0,
  which takes the first frames and never waits, and up to 12 more of them in the first blocks by how late CPU1 came
  in the blocks before. Without a NAM amp the whole block runs on CPU1 as before.
- v40.2: in the pipeline the two cores split the network by layers instead of by frames (`pxn_lp_*`,
  `dsp_lp_run`). CPU0 runs the input and layers 0..s of each 32-frame block and hands it over; CPU1 runs layers s+1..22,
  the head and the output, after the effects after the amp, and goes on after the interrupt until this block's network
  blocks are done (the next interrupt collects it). Every layer runs on whole blocks, so neither core waits for the
  other at every layer, and the head no longer runs on CPU0 alone. A layer of a block runs only after the same layer of
  the block before, and layer s's output goes to a hand-over buffer that CPU1 copies into layer s+1's ring, so s can move
  from one block to the next: it follows the difference between the two cores' times. The output is the one-core output
  bit for bit (`tests/engine/lp_test.c`: every model, the split moved at random, the second core's work at random
  moments; the pipeline on every NAM preset; on the pedal a development build checks it with both cores at boot). The
  load shown is the busier core's. The hand-over queue (6 KB) sits in the cache RAM after the vector unit's buffers.
- v40.4: CPU1 works on through the interrupts (`lp_job` is one worker for as long as the layer split is on). Before, its
  job ended with the block's last network block, which CPU0 hands over at the end of its own share: CPU1 idled at the
  start of each block, the next interrupt waited for it, and the two cores' work had to fit one after the other in a
  block (the effects before the amp, CPU0's layers, then one network block on CPU1). A chain could use only about 88 %
  of each core, and after a change of type, order or model, when the interrupt runs block after block to catch up
  with the time it was held off, that wait counted as load and engaged the safety bypass on chains that fit. Now each
  interrupt asks the worker for the effects after the amp; it finishes the network blocks handed over before the
  request first (the effects play their output), runs the effects, and returns to the network. The interrupt waits
  only for the output it sends. The network blocks handed over late in a block are finished during the next one, so
  each core's work may fill its block. The split is set in one step from the two cores' other work as timed (the
  effects before the amp on CPU0, the effects after it and the lanes on CPU1) and the network's parts
  (`pxn_lp_share`); in the first two blocks after a change, before CPU1's work is timed, CPU0 takes as many layers
  as fit in 80 % of its block. CPU1 takes layers over at once; CPU0 takes more only as CPU1 has finished them for the
  blocks before (`pxn_lp_a`), or waits for it once when told to (`pxn_lp_force`: a rise of two layers or more). The
  output does not depend on any of it (`tests/engine/lp_test.c`).
- v40.5: CPU0's waits for CPU1 end after a limit. CPU1's worker is one job for as long as the split runs, so the
  limit of `fm1_cpu1_wait` (8 ms, then CPU1 is retired as LATE and CPU0 goes on alone) did not apply to the interrupt's
  waits for it: in `a_lp_post` (its output), in `pxn_lp_a` (the queue full) and in a forced rise (`pxn_lp_wait`). A CPU1
  that stopped without a fault would have held the audio interrupt until the watchdog reset the pedal. These waits now
  end after the same 8 ms (`pxn_lp_clock`, set after the start-up check; none on the PC): `pxn_lp_a` returns without
  handing the block over, CPU1 is retired (LATE), and the next collect goes to one core with the engine reset, as for a
  CPU1 that faults. `tests/engine/lp_stuck.c` checks the network's side with a second core that never moves.
- v40.5: `post5` (`tools/gen_rdq.py --post5`) does `post4`'s work two frames a turn for every layer with an output: the
  residual's 9 weights are read once for the two frames, and every load and store is written with an instruction that
  computes (free with a multiply or multiply-add; with a 1-cycle instruction it costs only its own time), each value
  loaded 2 instructions or more before its use. No word outside the job is read. An odd first frame and the last layer
  go to `post4`. A turn takes 109 cycles against 159 for two of `post4`'s frames. `tests/test_post5.py` runs the
  assembly text itself through a model of its instructions against the C post, on the PC; the pedal checks it at
  start-up like the others and times it.
- v40.5: `rdm6`, `rdm15` and `post5` run from the flash, the other routines from RAM as before. On this chip code in RAM
  is fetched at about 1.5 cycles a word and code in the instruction cache at 1 cycle a word; these three are rows of
  6-byte instructions that work for 2 cycles each, so from RAM they waited for their own instructions. They are 6.6 KB
  and run all the time, so they stay in the cache. The network takes 6 % less time for it. The Bluetooth test image
  keeps them in RAM (`PXN_HOT_RAM`): its radio code runs from the flash all the time, and that was not measured.
- Each layer's history ring has its first 3 frames mirrored after its end, so 4 frames can be read straight from any
  position: the tap pointers just move on by 4 frames per group. On the pedal a layer's whole block of groups runs in one call
  (`pxn_rdl6/15/16_asm`, generated by `tools/gen_rdq.py --rdl`), its tap table on the stack.
- v40.2: on the pedal a multiply-add costs 2 cycles, and so does a load, unless it is paired with the multiply-add (then
  it is free); an ALU instruction costs 1 (measured). `rdl` reads 5 values for 4 multiply-adds (one weight, 4 frames).
  `rdm` (`tools/gen_rdq.py --rdm`) does a 3-row layer's rows 0 and 1 two frames at a time (2 weights, 2 frames: every load
  paired) and row 2 as `rdl`; `post4` reads the layer's input straight from its ring in a loop with no counter in memory,
  the residual's weights on the stack so that their loads pair, and LeakyReLU as `smax(z, (z * 42949673) >> 32)`, the
  same value for every z (checked over all 2^32). Both are checked against C at start-up like the others and timed against
  them; the start-up check, the timing and the dispatch also run on the PC (`tests/engine/hand_test.c`) with C stand-ins
  that read exactly what the hand code reads, under the address sanitizer, so that a check that would fault on the pedal
  (a boot loop) fails there first. `pxn_equiv` runs the whole network with the routines in use against C.
- At start-up the pedal times the ways to run it (CPU alone; the unit one command after the other; the unit and the
  CPU at once, the CPU taking 0, 4 or 8 frames; two cores, with or without the unit) on the first factory model and
  keeps the fastest (`pxn_tune`). The PC test runs every mode with a simulated unit that finishes its commands late,
  at random, and a simulated second core that moves on only while the first waits for it: the output must be exactly
  the CPU's.
- 1,730 multiply-adds per sample in 23 layers; the history buffers take 83 KB of RAM, a model 7.5 KB.
- The load shows at the bottom right of the screen (`C1 nn%`).

## Training (`tools/train_a2.py`)

`train_a2.py --capture CAPTURE.nam --di DI.wav --val DI2.wav --out MODEL.nam --name NAME` trains an A2-Lite network
(JAX) on a capture: the DI goes through the capture (resampled to its rate and back), the A2-Lite learns the result at
44.1 kHz. Loss: pre-emphasised ESR plus a multi-resolution spectral term. `tools/mk_nam_factory.py` writes the
factory models into `engine/px_nam_factory.h`, each with its level trim.

Credits: the captures are from the Neural Amp Modeler community collection
[pelennor2170/NAM_models](https://github.com/pelennor2170/NAM_models) (GNU GPL v3), by the authors named in the
table; the DI guitar is from [Alec-Wright/Automated-GuitarAmpModelling](https://github.com/Alec-Wright/Automated-GuitarAmpModelling)
(GNU GPL v3). The A2 architecture is Steven Atkinson's (Neural Amp Modeler, MIT); the pedal's code is written for
OpenPocket from the published architecture.
