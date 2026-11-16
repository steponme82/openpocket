# OpenPocket editor protocol (SysEx over USB-MIDI)

The web editor (`docs/editor.html`, `docs/pxedit.js`) talks to the pedal with this protocol. The firmware side is
`firmware/px_editor.h`; `tests/edsim.c` runs the same code on a PC for the tests. Protocol version 4 (OpenPocket v42: `USB_AUDIO`, command 28; version 3: v38, the cabinet IR commands 24..27; version 2: v37, without them;
version 1: v32 to v36, also without the NAM commands 19..22). A later version only adds commands: an editor written for
an earlier one works unchanged.

## Framing

A request is `F0 7D 4F 50 <cmd> <args...> F7` (`7D` = the non-commercial SysEx ID, `4F 50` = "OP"). Every request
gets exactly one reply with the same header and the same `<cmd>`; a request the pedal does not understand gets no
reply. Every data byte is 7 bit. Send the next request only after the reply to the previous one (the pedal keeps
one incoming frame at a time). While the editor watches (`WATCH`), the pedal also sends `STATE` frames by itself.

The Bluetooth audio test image (test build 27, [BT-AUDIO-TEST.md](BT-AUDIO-TEST.md)) also takes the same frames over
Bluetooth MIDI: as SysEx in the BLE MIDI writes, with the replies and the `STATE` frames as notifications of the MIDI
characteristic (BLE MIDI packets of at most 20 bytes), on either of its two LE links. A reply goes back the way its request
came (USB, or the LE link it came on); the `STATE` frames go the way the last request came. Requests longer than 160 bytes (no command has one) are dropped there, and so are replies
longer than 1,024 bytes (only `DIAG`'s).

| Item | Encoding |
| --- | --- |
| byte | one 7-bit byte |
| string | ASCII 32..126, ended by a 0 byte |
| color | an RGB565 color in 3 bytes: bits 0-6, 7-13, 14-15 |
| preset `P` | 110 bytes, the firmware's `px_preset_t` as is (every byte is below 128): `name` 14 (ASCII, 0-padded, at most 13 characters), `on` 12 (0/1), `type` 12, `p` 12 x 5 (0..100), `order` 12 (a permutation of 0..11) |

The 12 blocks, by index: 0 GATE, 1 COMP, 2 PITCH, 3 SFX, 4 DRIVE, 5 AMP, 6 CAB, 7 EQ, 8 MOD, 9 DELAY, 10 REVERB,
11 SYN (`CAT_BLOCK` gives their labels and names). `order` is the processing and display order.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | protocol (1), firmware string ("v32"), identity string ("PocketX_932"), blocks (12), parameters per block (5), slots (52), name length (13) |
| 2 CAT_BLOCK | block | block, label string ("AMP"), name string ("AMPLIFIER"), types, color |
| 3 CAT_TYPE | block, type | block, type, name string, parameters n, text mask (bit k: parameter k has its own value texts, see `TEXTS`), then n parameter-name strings |
| 4 TEXTS | block, type, k | block, type, k, then 101 strings: the text the pedal shows for values 0..100 ("" = the number itself) |
| 5 STATE | — | slot (0..51), flags (bit 0: the sound differs from the slot's, bit 1: the slot holds a saved preset), focused block, `P` (the current sound) |
| 6 SET | block, what, k, v | block, what, k, v (after clamping). what 0: parameter k = v (0..100); 1: on = v (0/1); 2: type = v |
| 7 ORDER | 12 bytes | rc (0 ok, 1 not a permutation) |
| 8 NAME | string | the name as stored (13 characters at most, other characters become "?") |
| 9 PUT | `P` | rc (0 ok, 1 invalid). Replaces the current sound (not saved) |
| 10 SELECT | slot | slot, rc. Loads the slot as the current sound (unsaved changes are dropped, as on the pedal) |
| 11 FOCUS | block | block. The pedal shows that block's page |
| 12 LIST | start, count (1..26) | start, count, then per slot: saved (0/1), name string |
| 13 SLOT_GET | slot | slot, saved, `P` (the factory preset when not saved) |
| 14 SLOT_PUT | slot, flags, [`P`] | slot, rc (0 ok, 1 invalid, 2 flash). flags bit 0: `P` follows and is stored as a saved preset (else the slot goes back to its factory preset); bit 1: write the flash now (else only in memory until `COMMIT`) |
| 15 COMMIT | — | rc (0 ok, 2 flash). Writes every slot to the flash (one write, about 0.3 s) |
| 16 SAVE | slot | slot, rc. Saves the current sound into the slot, writes the flash, the slot becomes the current one (as the pedal's SAVE) |
| 17 WATCH | on (0/1; protocol 5: bit 1 also asks for `MIDI_IN`) | on (protocol 5: bit 1 too). While on, the pedal pushes `STATE` (cmd 5) when the sound, the slot or the focused block changes on the pedal; it stops by itself 5 s after the last request |
| 18 PING | — | DSP load % (0..127), flags (bit 0: bypass, bit 1: second core); since v40.1 also the peak load % since the preset was set and since boot (0..127 each), the late blocks and the blocks at the soft clipper since boot (2 bytes each: bits 0-6, 7-13, wrapping), the bytes of the main stack and of the interrupt stack never used since boot (2 bytes each, in units of 4 bytes); the Bluetooth test image adds the lowest free bytes of its heap and of its deepest task's stack (the same coding). Since v40.2 then a count n and n figures of 2 bytes each (bits 0-6, 7-13): each block's share of an audio block's time over the last second in %, in the engine's block order, then the NAM network's; the network's way of running (its mode); its start-up timing of each way of running (11, in 0.1 % of real time, 0 = not available); and where one core's time goes in that timing (9 parts, 0.1 %: input, layers, rests, post, rings and output, the whole, the head's groups and rest, the tap tables); then the layer split (0 off, else 1 + the last layer CPU0 runs), then CPU0's and CPU1's load and their peaks since the preset was set (%, with the layer split; else 0; since v40.4 these peaks and the peak load above start again two blocks after a change, when the split is first set for the new sound: CPU0 runs the two blocks before at up to 80 % on purpose). Since v40.3 then the last change of type, order or NAM model: from the change to the sound back after it (0.1 ms; 0 while it still fades in), the engine's set in it (0.1 ms), how long the last LOADING was shown (ms; 0 = never) and how many were shown since boot. `node tools/op.mjs dsp` reads them per preset |
| 19 NAM_LIST | — | factory models (12), user slots (16), then per model (factory first, then the user slots): present (0/1), name string (11 characters at most; an empty user slot reads "U5 EMPTY") |
| 20 NAM_WRITE | piece (2 bytes: bits 0-6, 7-13), 128 packed bytes | the 2 piece bytes, rc (0 ok, 1 invalid). Piece i holds bytes 112 i .. 112 i + 111 of a model (the last piece fewer), packed 7 bit: groups of a byte of high bits (bit j = bit 7 of byte j) then 7 bytes. Stored in RAM only |
| 21 NAM_COMMIT | slot (0..15) | slot, rc (0 ok, 1 invalid: header, a weight that is not a number, CRC; 2 flash). Checks the pieces received and writes them to the user slot |
| 22 NAM_ERASE | slot (0..15) | slot, rc (0 ok, 1 invalid, 2 flash). Empties the user slot |
| 23 DIAG | — | n (5), then n strings: this boot, the last boot, the status line, the link, the trace. Read only. Only the Bluetooth test image answers: other firmware gives no reply. `docs/BLE-TEST.md`, "Reading the pedal from a computer" |
| 24 IR_LIST | — | slots (8), then per slot: present (0/1), name string (11 characters at most; an empty slot reads "IR3 EMPTY") |
| 25 IR_WRITE | piece (2 bytes), 128 packed bytes | the 2 piece bytes, rc (0 ok, 1 invalid). As `NAM_WRITE`: piece i holds bytes 112 i .. 112 i + 111 of an IR (2112 bytes, 19 pieces). Stored in RAM only (the same buffer as `NAM_WRITE`: one upload at a time) |
| 26 IR_COMMIT | slot (0..7) | slot, rc (0 ok, 1 invalid: header, CRC; 2 flash). Checks the pieces received and writes them to the slot |
| 27 IR_ERASE | slot (0..7) | slot, rc (0 ok, 1 invalid, 2 flash). Empties the slot |
| 28 USB_AUDIO | nothing, or on (0/1) | on (0/1), rc (0 ok, 1 this firmware has no USB audio, 2 flash), state, n (20), then n counts of 4 bytes each (bits 0-6, 7-13, 14-20, 21-27). Without an argument it only reads. With one it sets the USB AUDIO setting and, if it changed, writes it to the flash; see "USB audio" below |
| 29 MIDI_IN | (pushed, never asked) | type (1 Program Change, 2 Control Change), number, value, count (bits 0-6 of the messages received since boot). Protocol 5 (the Bluetooth test image, test build 39): while watched with bit 1 (`WATCH` 3), what a controller sent the pedal (the footswitch over Bluetooth, or one on USB), the controller's own number, at most one frame a pass of the main loop and 40 ms after the last push (several between two: the last). See "Learned on the pedal" in docs/MIDI.md |
| 30 MAP | nothing (read), or 1, each, INPUT + 10, flags, then 8 x (present, src, dst, mode) | rc (0 ok, 1 invalid: a number the pedal does not read, a mode above 2, one controller number twice, INPUT out of -10..+10, BT POWER above 2; 2 flash), each, INPUT + 10 (0..20: -10..+10 dB), flags (bit 0 TAILS, bits 1-2 BT POWER: 0 HIGH, 1 MID, 2 LOW, bits 3-4 OUTPUT: 0 MONO, 1 STEREO, 2 GTR+BASS, since test build 42), n (8), then n x (present, src, dst, mode). Protocol 5: the controls learned on the pedal and the SETTINGS page's SWITCH (each: 1 = LATCH), INPUT, TAILS, BT POWER and OUTPUT. Since test build 43 one more byte after the entries, both ways: USB audio's routing, bits 0-1 USB SEND (0 OUTPUT, 1 DI, 2 DI + OUT), bit 2 GUITAR (1 CUT), bits 3-6 USB RETURN (0 OUTPUT, 1 INPUT, 2 + b right before block b); a write without it leaves the routing as it is, one with a value no build writes (SEND 3, RETURN 14 or more) is refused (rc 1). Writing sets all of them and writes the flash (as a save; an unsaved sound stays as it is). mode 0 press, 1 each, 2 value; dst one of the pedal's own numbers (its map, or 64, 116, 117, 118, 119) |
| 31 NAM_RENAME | slot (0..15), name (0-ended, 11 characters at most) | slot, rc (0 ok, 1 invalid: an empty or damaged slot, an empty name; 2 flash). Protocol 5: the user slot's name only. The pedal reads the slot back, checks it as `NAM_COMMIT` does, sets the name and writes it again (the sound holds a moment; the staged pieces of an upload in progress are lost). A sound playing that slot goes on as it was |

### NAM models (protocol 2)

The AMP block's ninth type, `NAM`, plays a neural amp model (NAM A2, its 3-channel "Lite" size, 1871 weights:
`engine/px_nam.h`). Its first parameter, MODEL, picks one of 28 models: 0..11 the factory ones (in the firmware),
12..27 the 16 user slots in the pedal's flash (`firmware/px_models.h`, 0xC0000..0xE0000, kept by firmware updates).
`TEXTS` of AMP type 8, parameter 0 gives each value's model name; the editor asks for it again after a change of
the user slots.

A user model is 7548 bytes, little endian: a 64-byte header, then the 1871 weights as float32 in the `.nam`
file's order.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `0x4D4E504F` ("OPNM") |
| 4 | 2 | version, 1 |
| 6 | 2 | weights, 1871 |
| 8 | 12 | name, ASCII, 0-ended (11 characters at most) |
| 20 | 2 | level trim, int16, 0.01 dB (added to the model's output) |
| 22 | 2 | CRC16 of the weights (polynomial 0x1021, initial value 0) |
| 24 | 40 | 0 |

The editor (`docs/pxnam.js`) reads a `.nam` file, takes its A2-Lite part (an A2 file is a "SlimmableContainer"
with several sizes; a plain A2-Lite WaveNet also works), measures its loudness on `docs/nam_di.wav` and sets the
level trim so that every model plays at -20 LUFS on that clip, then sends 68 `NAM_WRITE` and one `NAM_COMMIT`.
A1 models (the older "standard" / "lite" / "feather" WaveNets) and other architectures are refused: the pedal's CPU
runs A2-Lite only.

### USB audio (protocol 4)

`USB_AUDIO` reads and sets the pedal's USB AUDIO setting ([USB-AUDIO.md](USB-AUDIO.md)), the same one as the fourth
row of the pedal's MIDI page. After a change the pedal sends this reply, and 150 ms later it leaves USB for one second
and comes back as another USB device, with or without the audio streams. The MIDI port goes away with it: the editor
must connect again (after about 3 s).

`state`: bit 0, the computer has the record stream open; bit 1, it is reading it; bit 2, it has the playback stream
open; bit 3, its playback is sounding.

The 20 counts, since the pedal started (they wrap at 2^28), in this order (a client reads `n` of them):

| # | Count |
| --- | --- |
| 1..3 | record stream: times started, packets sent, frames sent |
| 4 | record ring empty: a packet repeated its last frame. It rises when a change of type, order or model or a save holds the audio for a moment (the sound is silent then) |
| 5 | record ring full: a block was dropped because the computer was not reading |
| 6 | the computer asked for a packet and none was ready |
| 7 | the computer stopped reading for more than 20 ms with the stream still open |
| 8, 9 | packets one frame longer and one frame shorter than the pattern (how the pedal's clock is followed) |
| 10, 11 | the record ring's lowest and highest fill at a block's start, in frames, since the stream started |
| 12..14 | playback stream: times started, packets received, frames received |
| 15, 16 | playback ring empty (the playback starts over) and full (a packet dropped) |
| 17 | the time the last audio block spent on USB audio, in timer ticks (a block has 69,648) |
| 18 | record packets the computer took late: it left a USB frame or more without asking for one. Nothing the pedal can mend; macOS does it now and then in the first second of a stream |
| 19 | playback packets that came late: a USB frame or more without one |
| 20 | audio blocks whose output was asked for before their input (`firmware/px_aorder.h`). They are sent in the right order since v42; before, each was a block sent twice and one lost. It rises about twice a second while a stream is open, and with MIDI traffic, with or without USB audio |

The Bluetooth test image has no USB audio: it answers `0, 1, 0, 0`.

### Cabinet IRs (protocol 3)

The CAB block's eighth type, `USER IR`, plays a cabinet impulse response (`engine/px_dsp.c`: uniform partitioned FFT
convolution, 1024 taps at 44.1 kHz). Its settings: IR (the slot, 0..7), LOW CUT (20..400 Hz), HIGH CUT (20 kHz..2 kHz).
The 8 slots are in the pedal's flash (`firmware/px_irs.h`, 0xF4000..0xFC000, kept by firmware updates). `TEXTS` of CAB
type 7, parameter 0 gives each value's IR name.

An IR is 2112 bytes, little endian: a 64-byte header, then 1024 samples as int16 (sample k = s[k] / 32767 x scale).

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `0x5249504F` ("OPIR") |
| 4 | 2 | version, 1 |
| 6 | 2 | samples, 1024 |
| 8 | 12 | name, ASCII, 0-ended (11 characters at most) |
| 20 | 4 | scale, float32 (the samples' full scale; above 0) |
| 24 | 2 | CRC16 of the samples (polynomial 0x1021, initial value 0) |
| 26 | 38 | 0 |

The editor (`docs/pxir.js`) reads a WAV file (PCM 8 / 16 / 24 / 32-bit or float, any rate, mono or stereo: the first
channel), drops the silence before its onset, resamples it to 44.1 kHz (windowed sinc), keeps 1024 samples (a 64-sample
fade at the end when the response is longer), then sends 19 `IR_WRITE` and one `IR_COMMIT`. The pedal sets the level
itself: every IR plays at the energy of the modelled cabinets.

Writing the flash (`SLOT_PUT` with bit 1, `COMMIT`, `SAVE`, `NAM_COMMIT`, `NAM_ERASE`, `IR_COMMIT`, `IR_ERASE`) mutes
the sound for a moment, as
saving on the pedal does. A whole import sends `SLOT_PUT` without
bit 1 for every slot, then one `COMMIT`.

## Preset files

The editor saves presets as JSON: `{"format": "openpocket-presets", "version": 1, "presets": [...]}`, each preset
`{"slot": 1..52, "name", "blocks": [{"block": "AMP", "on", "type": "type name", "params": [5 numbers]}...],
"order": ["GATE", ...]}`. Blocks and types are written by name, so a file stays readable and survives a firmware
that adds types; a single preset is the same object with one preset in `presets`.

Each preset written by OpenPocket v36 carries `"v": 36` (v35: `35`). Older presets are converted on import: without
`"v": 36`, the preset LEVEL (the GT block's 3rd setting, `PRESET LVL`, unused before) is set to 50 = 0 dB; without `"v": 35`,
its delay TIME is moved to v35's scale (the same delay time; REVERSE excepted); amp / drive / cab types from before
v34 (`"RECTIFIED"`...) become the nearest current type. Each change of the delay or the types is listed in the import
notes.
