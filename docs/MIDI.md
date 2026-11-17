# OpenPocket MIDI over USB-C

OpenPocket answers MIDI Program Change and Control Change from a computer, a DAW or a controller over the USB-C cable,
and tells the computer when you change the sound on the pedal. This page is the design and the reference. The hardware
checklist for the first test is [MIDI-TEST.md](MIDI-TEST.md).

**Status.** Written and tested on a PC (host simulators, see "How it was checked"). Not yet tried on a real pedal. It ships
as a separate test package (OpenPocket v37 MIDI TEST), not in the normal installer. **Bluetooth MIDI is only in the separate
Bluetooth test image** ([BLE-TEST.md](BLE-TEST.md)).

## What the pedal does

**Receives** (USB, from the host):

| Message | What happens |
| --- | --- |
| Program Change 0 to 51 | Loads preset slot 1 to 52, as the footswitches do (unsaved changes are dropped). 52 and above are ignored |
| Control Change, the map below | Switches or flips a block on or off, picks a block's model, sets one of its 5 settings, or sets the MASTER volume |
| MIDI clock (24 per quarter note), when CLOCK > DELAY is on | Sets the delay TIME to one quarter note |
| Start, Continue, Stop | Restart the tempo measurement |
| Everything else | Ignored: notes, pitch bend, bank select (0 and 32), modulation, sustain, "all notes off", system messages |

**Sends** (when SEND is on and a host is connected):

| What you do on the pedal | What the computer receives |
| --- | --- |
| Change the preset (footswitch, knob, SAVE to another slot) | One Program Change with the slot number (0 to 51) |
| Turn a setting, switch a block, pick a model | One Control Change per change, with the value from the map |

A change made by the host is not sent back. Changes made from the web editor are sent like changes made on the pedal. The
sound the pedal starts on is not sent at power-on. Nothing is sent while no host is connected, and nothing old is sent
when one connects.

**Channel.** CHANNEL is OMNI (receive on any channel; send on channel 1) or 1 to 16 (receive and send on that channel only).
The clock and the transport bytes have no channel and are always taken.

## The Control Change map

Value scaling: a setting is 0 to 100 on the pedal and 0 to 127 in MIDI (`v * 127 / 100`, rounded; the way back is exact).
On or off: 64 and above is on, below 64 is off; the pedal sends 127 or 0. A model number n of N models is the slice
`n * 128 / N` to `(n + 1) * 128 / N - 1` of 0 to 127; the pedal sends the middle of the slice. Send the highest value to get the
last model.

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

**Toggles (footswitches).** CC 8 to 19 flip a block with **value 0** and ignore every other value, so a footswitch that sends
127 then 0 for a tap flips it once. The pedal holds the state, so a preset change cannot put the footswitch out of step, as
it can with the on/off numbers above.

| CC | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Block | GATE | COMP | PITCH | SFX | DRIVE | AMP | CAB | EQ | MOD | DELAY | REVERB | SYN |

The order is the order of the blocks above: CC 8 + n flips what CC 20 + n switches. The pedal never sends these numbers: it reports a
block with its on/off number.

CC **7** (volume) sets the MASTER volume, 0 to 127 to 0 to 100, like the MASTER knob (turning the knob takes over again).

What the "setting" numbers mean depends on the model (the screen shows the names): for the AMP they are GAIN, BASS, MID,
TREBLE, PRESENCE; setting 3 of GATE is the preset LEVEL (50 = 0 dB); setting 1 of DELAY is TIME. Only the settings the model
has do anything; the others are stored and ignored. See [SOUNDS-AND-CONTROLS.md](SOUNDS-AND-CONTROLS.md) and
[EDITOR-PROTOCOL.md](EDITOR-PROTOCOL.md).

The numbers were chosen so that nothing special is touched: 0 and 32 (bank), 6 and 38 (data entry), 64 to 69 (pedals), 98 to 101
(NRPN and RPN) and 120 to 127 (channel mode, "all notes off") are never used, and 7 is the only standard one. A few settings
use 33 to 51, which are the low halves of controllers 1 to 19; a controller that sends 14-bit pairs on those would move them
by accident. Map them to one of the free numbers in your DAW instead.

The map is in one place in the source, `px_cc_param()` and its siblings in `firmware/px_midi.h`; a test checks that every
number is unique, reserved ones are free, and a value sent by one pedal is applied by another to the same sound.

## From the web editor

The editor's **MIDI** section ([`docs/editor.html`](editor.html), `docs/edmidi.js`, `docs/pxmidi.js`) does three things:

- **The map, named for the current preset.** The table above with each setting's name in the preset on the pedal
  (setting 1 of the AMP is GAIN for a fitted amp and MODEL for NAM), and the settings a type does not have marked.
- **Learned controls.** For a controller plugged into the computer, not into the pedal: click a control in the table,
  then move a knob or press a switch on the controller. From then on the page passes that Control Change on to the
  pedal as an editor command (`SET`, `SELECT`), scaled as the pedal scales its own. A switch can flip on each press or
  follow the value; a control can work in every preset or only in one; a switch can step to the next or the previous
  preset; Program Change 0 to 51 loads preset 1 to 52. Learned controls are kept in the browser and work only while the
  page is open and connected: the pedal itself stores no mapping.
- **Monitor.** What the controllers send and what it did, and the pedal's own messages (with SEND on), each named. A
  setting turned on the pedal lights its cell in the table, which is the quick way to find its number.

CHANNEL, SEND and CLOCK > DELAY stay on the pedal's MIDI page: the editor protocol has no command for them.

## Learned on the pedal (the Bluetooth test image, test build 40)

A footswitch linked to the pedal over Bluetooth (the M-VAVE Chocolate) talks to the pedal, not to the computer or phone that
runs the editor, so the editor's learned controls above never hear it, and they would work only while the page is open anyway.
The Bluetooth test image learns such controls itself:

- **In the editor's MIDI section**, click a control in the table, then press the switch: the pedal tells the editor what it
  received (`MIDI_IN`), and the editor writes it into the pedal's own table (`MAP`, docs/EDITOR-PROTOCOL.md). The pedal keeps it
  with the presets (a flash write, like a save) and it works in every preset, with no page open. Up to 8.
- **What a learned control does**: the controller's number is read as one of the pedal's own, before anything else (a learned
  number wins over the map's meaning of it): a block's on/off is its flip number (8 to 19), the next or the previous preset
  is 118 or 119, a setting is its own number and follows the value. A switch learned "press" acts when its value goes to 64
  or more and ignores the value below (its release); one learned "each" acts at every message (a switch that alternates 127
  and 0, one message a press). The editor tells the two apart while learning (a release within 1.2 s of the press: "press"),
  and either can be changed in its list.
- **Where it is kept**: in each copy of the preset store, after sector 1's slots, with its own mark and CRC
  (`firmware/px_store.h`). Other firmware never reads it, and a save made by other firmware leaves none.

Also in this image:

| Number | What it does |
| --- | --- |
| 64 | at 64 and up: a SYN PAD catches the chord (test build 22) |
| 116 | at 64 and up: a tap of the delay's tempo (test build 24) |
| 117 | the expression pedal: WHAMMY (the note to SHIFT's interval), else WAH or AUTO WAH, else the volume (test build 25; WAH and the ends 4 values wide: test build 45) |
| 118, 119 | at 64 and up: the next, the previous preset, as the footswitches (test build 40) |
| 65, 66, 67, 68 | any value: SCENE 1, 2, 3, 4 of the preset playing (test build 49; below) |

and more rows on the MIDI page, which this image calls **SETTINGS**: **GUITAR TONE** and **PICKUP** (test build 46: the pickup's
tone as a lower input impedance would load it, for a single coil, a humbucker or a P90; named by its sound since test build 47,
BRIGHT, NATURAL, SMOOTH, WARM, WARMER, PUNCHY, DARK, DARKER, MUDDY; docs/BT-AUDIO-TEST.md, "Test build 47"), **PLAY VIEW**
(test build 47: CHAIN, or SWITCHES, the blocks the controllers' switches turn on and off, big on PLAY; below), **INPUT** (-10
to +10 dB, the guitar's level into the chain, as the official firmware's Input Gain), **TAILS**, **BT POWER** and **SWITCH**
(MOMENT, LATCH): how the flip numbers 8 to 19 act. MOMENT (the default) flips on
value 0, for a switch that sends 127, then 0 when let go; LATCH flips at every message, for a switch that alternates 127 and 0
(the Chocolate's CC mode does). The page's USB line also counts the controls learned on the pedal ("LEARNED 2").

## Learned live, one preset's own (the Bluetooth test image, test build 46)

The same idea without the editor, and kept per preset: on the pedal itself, a controller's switch or pedal is taught to a row of
a block page, and drives it in that preset only.

- **Learn** (test build 48: a page of its own, a step at a time): open a block's page (PUSH on PLAY, or its button), select
  its **ON** row or one of its settings with the knob, **hold FX** (0.8 s). The **LEARN** page names the row (`GT PRESET LVL`)
  and lists the steps; the one to do now is lit, with what to do in big letters, and the footer says which control does it.
  1. **PEDAL OR SWITCH**: turn the knob to choose (a setting starts on PEDAL, ON on SWITCH), PUSH.
  2. **MOVE THE PEDAL** (or **PRESS THE SWITCH**): a pedal is taken at its first message; a switch is pressed and let go (a
     momentary switch: it acts on its press) or not (one that sends a message a press: each one acts), told apart in 1.2 s.
     The page waits as long as it takes. On ON that is all.
  3. On a setting, **PEDAL AT THE HEEL** (a switch: **SWITCH OFF**): put the pedal at its heel, turn the knob to the value you
     want there (you hear it: down to `-INF` on PRESET LVL), PUSH.
  4. **PEDAL AT THE TOE** (a switch: **SWITCH ON**): put the pedal at its toe, turn the knob to the value there; it starts from
     the preset's own value, so PUSH alone keeps that. PUSH: done, `LEARNED: CC 7` (green) on the block page, written to the
     flash (as a save: an unsaved sound stays unsaved).

  HOME at any step: `LEARN: CANCELLED`, nothing learned or changed. Nothing else does anything on the page (FX, the
  footswitches, the blocks' buttons); a long PUSH is a PUSH. The controller moves nothing while it is being learned, and the
  setting goes back to the preset's own value, which the preset keeps until the pedal moves. Where the pedal was at the heel
  and at the toe is kept too: a pedal that sends 3 to 120 gives the heel's value at 3 and the toe's at 120 (one sending 120 at
  the heel works the other way round); not moved between the two, its whole 0..127. Examples: PRESET LVL from `-INF` at the
  heel to the preset's level at the toe (a volume pedal at the chain's end: the delay's and the reverb's tails go with it), or
  from the preset's level to `+4.2 DB` (a boost); a delay's MIX from 10 to 40. Test builds 46 and 47 asked in the footer, and
  any control but PUSH gave the setting the whole range while still saying `LEARNED`.
- **What it does**: on **ON**, a switch flips the block (a momentary switch: on its press; one that sends a message a press: on
  each one), a pedal switches it (on at 64 and up, off below); on a **setting**, the value between the heel's and the toe's (a
  switch: its OFF and ON values). Only in the preset where it was learned: in another preset the same control does what the
  editor's learned controls or the pedal's own map say. In the preset's MIDI table, the footer shows the selected one's range
  in the setting's own units (`RANGE -INF TO +0.0 DB`).
- **See and clear**: a setting that has one shows its number after its name (`GAIN CC4`), and ON after the block's name in
  the band where it fits; while selected, the footer says it too (`CC 30  HOLD FX: CLEAR`): hold FX on it clears it
  (`CC 30 CLEARED`, written too). A row has one control, a control one row in a preset: learning another one on a row, or the
  same control on another row, replaces it.
- **The preset's MIDI table**: hold FX on the MODEL row (its footer says `2 LEARNED  HOLD FX` when the preset has some): every
  control of this preset, `CC 30  DLY ON/OFF`, `CC 4  AMP GAIN`; KNOB picks one, hold FX forgets it, HOME goes back. The
  editor's learned controls follow in grey (`ALL: ...`): they work in every preset and the editor changes them.
- **MODEL and PLAY**: hold FX on the MODEL row opens the preset's MIDI table (above); on PLAY, hold FX still opens SETTINGS.
- **Room and order**: up to 64 for all the presets together (`LEARN: 64 IN USE, FULL`). A preset's own control comes before
  the editor's learned controls (which work in every preset), which come before the pedal's own map. CC 117 learned on a row is
  that row's, in that preset, instead of the expression pedal's job.
- **Where it is kept**: in each copy of the preset store, right after the editor's learned controls ("OPP3" since test build
  48, 64 entries of slot, controller's number, the pedal's number, how it counts, its heel's and toe's values and where the
  pedal was at each, a CRC: `firmware/px_store.h`; 47's "OPP2" is read with the pedal's whole travel, 46's "OPPL" no longer). Saving a sound into another
  slot (SAVE, or the editor's SAVE) takes its controls along, in place of that slot's; a slot saved back to its FACTORY sound
  loses its own. Other firmware never reads it. The web editor (editor protocol 6, test build 49) lists the preset's own in
  its MIDI section, their heel and toe editable, and learns one for this preset only.

## Scenes (the Bluetooth test image, test build 49)

Helix's snapshots for the blocks' on and off: each preset has four **scenes**, each a set of blocks on and off; recalling one
switches the blocks to it at once (each block fades, as when switched by hand).

- **Recall**: CC 65, 66, 67, 68 (any value) recall SCENE 1 to 4; or a controller's switch learned on a scene in a preset (in
  SCENES' view, below: select the scene with the knob, **hold FX**, press the switch: `LEARNED: CC 20`; held again on it,
  cleared); or the footswitches in SCENES' view; or the knob and PUSH there.
- **What a scene keeps**: the scene playing keeps what is switched while it plays (Helix's way): switch a block, go to another
  scene and back, the block is as you left it. **SAVE** (hold PUSH) writes the preset with its scenes, and the preset opens in
  the scene it was saved in. A scene never stored begins as things are when it is first recalled. Recalling a scene does not
  make the preset `EDITED`; switching a block in it does, until SAVE. On PLAY, `SCENE 2` shows where `EDITED` goes.
- **Scenes and switches**: a block can have its own switch in the preset (a learned ON row) and still be in the scenes.
  SETTINGS' **SCENE+SWITCH** chooses: **SWITCH WINS** (the default) leaves such a block to its switch, the scenes set every
  other one; **SCENE WINS**: the scenes set it too (the switch's change lasts until the next scene). With a Chocolate's four
  switches: two scenes and two blocks, for example.
- **PLAY's views**: **hold CAB** (0.8 s; a short press still opens the CAB page) turns PLAY's view round: **CHAIN** (as
  always), **SWITCHES** (what each controller switch does in this preset: a block, lit when on, or a scene, lit while it plays,
  in the switches' order), **SCENES** (the four, the one playing lit, the knob's in a ring, each with its switch's number). In
  SCENES' view the knob picks a scene, PUSH recalls it, hold FX learns or clears its switch, and the **footswitches** step
  through the scenes, round (SETTINGS' **SCENE FOOTSW**: **SCENES**, the default, or **PRESETS** to keep them on the presets;
  both together still open the tuner, the scene stepped back). To reach SETTINGS from SCENES' view: hold CAB to CHAIN first.
  The view is kept with the settings at the next save.
- **Where it is kept**: in each copy of the preset store, after the learned controls ("OPSC": for each preset the four scenes,
  the one playing, the ones stored; a CRC: `firmware/px_store.h`). Saving a sound into another slot takes its scenes along; a
  slot saved back to its FACTORY sound loses them. Other firmware never reads it. The web editor (editor protocol 6) shows the
  four under the chain in its Sound view: a tap recalls one, a grid sets each one's blocks; its MIDI section learns a switch
  on a scene and holds SCENE+SWITCH, SCENE FOOTSW and PLAY VIEW.

## PLAY VIEW: what the switches turn on and off (the Bluetooth test image, test build 47)

SETTINGS' **PLAY VIEW** on **SWITCHES**: on PLAY, in place of the chain's tiles and the selected block, the blocks that this
preset's controller switches turn on and off, each in a big tile: the block (`DLY`), the controller's number (`CC 21`), the model
(`DIGITAL`), in the block's colour when it is on, grey when off. The preset's own learned switches first, then the editor's
learned controls (every preset) that the preset's own do not hide, in the order of the controllers' numbers (a Chocolate's A,
B, C, D in order), a block once, 6 at most (2 by 2 up to 4, 3 by 2 above). A preset with no switch learned shows the chain as
before. Turning the knob shows the chain for 5 seconds (to pick a block and PUSH to its page); PUSH, HOME and the rest work as
on the chain. CHAIN (the default) is PLAY as always. Saved with the settings (bit 4 of the USB settings byte).

## Tempo for the delay

Turn **CLOCK > DELAY** on (default off). When the host sends MIDI clock, the pedal times 24 clocks, and sets the delay TIME to
the quarter note once two measurements in a row agree within 3 ms (so a drifting host does not make the delay jump). Quarter
notes shorter than 200 ms or longer than 2 s (above 300 or below 30 BPM) are ignored, and the TIME knob only reaches 2 s. The
nearest TIME step is chosen: 12.25 ms steps up to 1 s, 50 ms steps above it. REVERSE has its own range (250 ms to 1 s). To
follow a tempo with another note value, set the delay's own TIME; this is not a division setting.

## Settings and the screen

**Hold the FX button for 0.8 s** (a short press still cycles the effects, now when you release it) to open the **MIDI page**:

| Row | Values | Default |
| --- | --- | --- |
| CHANNEL | OMNI, 1 to 16 | OMNI |
| SEND | ON, OFF | ON |
| CLOCK > DELAY | ON, OFF | OFF |
| USB AUDIO | ON, OFF | OFF |
| SWITCH (the Bluetooth test image, test build 40) | MOMENT, LATCH | MOMENT |
| INPUT (the Bluetooth test image, test build 40) | -10 DB to +10 DB, steps of 1 dB | 0 DB |
| TAILS (the Bluetooth test image, test build 40) | OFF, ON | OFF |
| BT POWER (the Bluetooth test image, test build 40) | HIGH, MID, LOW | HIGH |
| PLAY VIEW (the Bluetooth test image, test build 47; SCENES and CAB held: 49) | CHAIN, SWITCHES, SCENES | CHAIN |
| SCENE+SWITCH (the Bluetooth test image, test build 49) | SWITCH WINS, SCENE WINS | SWITCH WINS |
| SCENE FOOTSW (the Bluetooth test image, test build 49) | SCENES, PRESETS | SCENES |
| GUITAR TONE (the Bluetooth test image, test build 46; named by its sound since 47) | BRIGHT, NATURAL, SMOOTH, WARM, WARMER, PUNCHY, DARK, DARKER, MUDDY | NATURAL |
| PICKUP (the Bluetooth test image, test build 46) | SINGLE, HUMBUCK, P90 | SINGLE |
| OUTPUT (the Bluetooth test image, test build 42) | MONO, STEREO, GTR+BASS | MONO |
| USB SEND (the Bluetooth test image, test build 43) | OUTPUT, DI, DI + OUT | OUTPUT |
| USB RETURN (the Bluetooth test image, test build 43) | OUTPUT, INPUT, BEFORE GT ... BEFORE SYN | OUTPUT |
| GUITAR (the Bluetooth test image, test build 43) | MIX, CUT | MIX |

In the Bluetooth test image the page's title is **SETTINGS**: it also holds INPUT, the guitar's level into the chain, as the
official firmware's Input Gain (a digital gain at the input, heard at once while you turn it; the input meters of the
diagnostics page show the level before it, so the converter's clipping still shows); TAILS, the delay and the reverb ringing
out when they go off or the next preset has them off (as the official firmware's Delay Tail and Reverb Tail, one switch for
both); BT POWER, the radio's transmit power; and OUTPUT: MONO (the same sound on both channels, as before), STEREO (the
delay, the reverb and the modulations spread to the sides, the two channels summed being the mono sound) or GTR+BASS (the guitar on the left,
LOW OCTAVE's octave alone on the right; [OWN-FX.md](OWN-FX.md)). The editor's MIDI section sets them too. Since test build 43
its rows end with USB PORT, then USB AUDIO and its three routing rows, USB SEND, USB RETURN and GUITAR (what the computer
records, where its sound comes back into the chain, the guitar cut there; [BT-AUDIO-TEST.md](BT-AUDIO-TEST.md), "Test build
43"); 18 rows since test build 49, so the page shows 8 at a time and scrolls with the one selected.

The knob changes the row, PUSH goes to the next one (after the last it leaves), HOME leaves. In the Bluetooth test image (test
build 47) the footswitches walk the rows as well: the right one down, the left one up, round. The page also shows whether a
host is connected and the last Program or Control Change received ("LAST: CC 25 = 127"), which is what to look at first when
nothing happens. Leaving saves the settings only if you changed something (one flash write, about 0.3 s of silence, like SAVE).

**USB AUDIO** (v42) is not a MIDI setting; it lives on this page because it changes what the pedal is on USB. ON makes
the pedal a USB sound card as well as a MIDI device: see [USB-AUDIO.md](USB-AUDIO.md). The change is made when you leave
the page, and the pedal then leaves USB for a second. MIDI works the same with it on or off.

The settings are one byte in the header of the preset store (`firmware/px_store.h`). Older copies have 0 there, which means the
defaults. A power cut while saving leaves the old or the new settings, never a broken store (tested).

While the SAVE page, the MIDI page, the tuner or the block mover is open, the host's Program Change and Control Change are read
and ignored, so a DAW cannot change the preset under a save in progress.

## How it is wired, and what stays separate

The USB device is felucca-bsp's `chip/usb.c` as before, **unchanged**: a class-compliant USB-MIDI interface (no driver on macOS,
Windows 10 and later, Linux, iOS or Android; the port is named "OpenPocket"). It is polled at 2 kHz from a timer interrupt. This
work does not touch the descriptors, `ota.c`, the loader or the boot, update-entry and recovery paths.

There are two kinds of traffic on the one cable, and they never meet:

1. **SysEx** (`F0 ... F7`) belongs to the update and the editor only: the installer's update protocol (`ota.c`), the editor's
   frames `F0 7D 4F 50 ...` ([EDITOR-PROTOCOL.md](EDITOR-PROTOCOL.md)) and the rescue key. `usb.c` collects SysEx into one frame
   buffer for them. A SysEx that is not theirs (a General MIDI reset from a DAW, a patch dump) is taken out of that buffer and
   thrown away by `ota_take`; a SysEx longer than the buffer is dropped. A test sends a foreign SysEx to the simulated pedal
   before an update, and the update still works.
2. **Musical messages** (Program Change, Control Change, clock and transport) go into a 64-entry ring that `usb.c` fills and
   `firmware/px_midi_io.h` empties every pass of the main loop. Nothing sends SysEx into that ring and nothing in it is SysEx.

One fix came with this: until now nothing emptied that ring. `usb.c` leaves the host's USB packet unread while the ring is nearly
full, so a DAW sending clock or notes would, after a few dozen messages, also have stopped the editor's and the installer's SysEx,
which arrive in the same packets. The ring is now always drained (messages the pedal does not use are dropped), and the
overflow flag `usb.c` sets is cleared once the ring is empty. A test floods the ring and checks it recovers.

Code map: `engine/px_dsp.c` has `px_dly_value_for_ms` (the TIME value nearest a delay time); `firmware/px_midi.h` is the pure
logic (decoder, CC map, what to send, settings byte, clock), with no hardware; `firmware/px_midi_io.h` is the glue to the rings
and the pedal's state; `firmware/ui.h` has the MIDI page; `firmware/px_store.h` keeps the settings byte.

## Cost and safety

- **CPU and audio.** The MIDI code runs on the main loop (CPU0), not in the audio interrupt and not on the DSP core. Per pass it
  reads the ring (usually empty) and compares the 84 on/model/setting bytes of the preset with the last values sent. The binary
  grows by about 3.3 KB. The DSP load was **not measured on a pedal** (no hardware here): the hardware checklist asks for the
  "DSP xx%" reading with and without MIDI traffic. Nothing was added to the audio path.
- **Flash.** The only new write is the settings save, in the preset store's own area, through the existing commit.
- **Update and recovery paths.** Unchanged. The tests of the installer, the update loader and the editor run as before.
- **Never flash from here.** The test package is built and checked on a PC; the captain installs it.

## Bluetooth MIDI

Bluetooth MIDI is not in this release. The Bluetooth test image (`make ble`, [BLE-TEST.md](BLE-TEST.md)) is a connectable BLE MIDI
peripheral on JieLi's Apache-2.0 controller and host libraries. A write to the characteristic goes through
`firmware/px_ble_midi.h` into the same `px_midi_decode` and `px_midi_in` as USB, so Program Change, Control Change, the clock and the
page guards mean the same on both roads. It only receives: the pedal's own changes are not sent over Bluetooth.

Before this can leave the test image it needs a setting to switch the radio off (saved in the store; default off, so a pedal that
never uses it pays nothing), a measurement of the DSP load and glitches with a link up, and bonds that survive a power cycle. The
transmit power must not exceed what the stock firmware uses.

## How it was checked (and what was not)

Run `node tools/op.mjs test` (see [BUILDING.md](BUILDING.md)):

- `tests/midisim.c`: the packet decoder (channel filter, bad status, bad data bytes, SysEx never decoded, cable number), the CC
  map (unique, reserved numbers free, exact inverse, value scaling round trips), what the pedal sends (Program Change, one
  Control Change per change, no echo, SEND off), the settings byte, MIDI clock (120 BPM, jitter, gaps, a changing tempo), and
  that a sound sent by one pedal is applied unchanged by another.
- `tests/midiio.c`: the real glue code against the ring: Program Change and Control Change change the sound, are ignored while
  SAVE, tuner or the MIDI page is open, channel filtering, a flood is drained and recovers, clock sync on and off.
- `tests/test_store.c`: the settings survive a power cycle, a preset save and a power cut at every step.
- `tests/uisim.c`: the MIDI page is drawn completely, the channel wraps, leaving saves only if changed.
- `tests/test_installer.mjs`: an update still succeeds after a foreign SysEx.
- `tests/test_op.mjs`, `tests/test_page.mjs`: the test package command and the installer's test channel.

**Not checked, because it needs the pedal:** the USB enumeration and the port name on each OS, a real DAW or app sending, the
timing of replies, the DSP load and any dropout with MIDI flowing, the real screen, the update path on the new build, power
cycles. That is what [MIDI-TEST.md](MIDI-TEST.md) is for.

## The test package

`node tools/op.mjs package-test` (or `make package-test`) builds **OpenPocket v37 MIDI TEST** into `docs/firmware/midi-test/`
(`PocketX_937.fwsc`, `BlackBox_937.fwsc`, its own `manifest.json`) and `firmware/openpocket-midi-test.bin`. The boot screen and
the editor say `v37 MIDI TEST`. The normal files (`docs/firmware/manifest.json`, v36) are not touched, and the normal installer
page never offers it. To install it, open the installer with `?channel=midi-test` (a banner says it is a test build). To go back,
open the normal page and press Install (v36), or use the official-firmware way back on the same page.

## Licences and references

The USB code is felucca-bsp's (MPL-2.0, see [NOTICE.md](../NOTICE.md)); the MIDI code here is GPL-3.0-only like the rest of
OpenPocket. The CC map is OpenPocket's own. Facts about the official firmware come from a private analysis of the captain's
own device, used only to learn how it behaves, and no vendor code, bytes or tables are reproduced.
