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
| 64 (sustain) | With a SYN PAD on in the preset: 64 to 127 catches the chord being played (as the left footswitch); 0 to 63 nothing |
| Everything else | Ignored: notes, pitch bend, bank select (0 and 32), modulation, "all notes off", system messages |

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

The knob changes the row, PUSH goes to the next one (after the last it leaves), HOME leaves. The page also shows whether a
host is connected and the last Program or Control Change received ("LAST: CC 25 = 127"), which is what to look at first when
nothing happens. Leaving saves the settings only if you changed something (one flash write, about 0.3 s of silence, like SAVE).

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
  "C1 xx%" reading with and without MIDI traffic. Nothing was added to the audio path.
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
