# USB-MIDI

## Sub-features
Today: the pedal enumerates as a class-compliant USB-MIDI device; its SysEx carries the installer's update protocol and the editor protocol. Musical messages (Program Change, Control Change) are added by the MIDI feature: see `docs/MIDI.md` when it exists.

## How to get to it (user POV)
Plug the pedal's USB-C into a computer; a DAW or the browser sees a MIDI port.

## Driving it without hardware
`node tools/op.mjs test` (devsim and edsim run the SysEx paths); once the MIDI feature is in, its simulator test (`tests/`) covers the parser and CC map. The pages scripts cover the SysEx clients.

Any SysEx by hand: `curl -s -X POST $U/api/sysex -d 'f0 7d 4f 50 01 f7'` against `node tools/op.mjs sim` (the editor protocol, answered by the real `px_editor.h`); `POST /api/midi` plus `GET /api/events` for unanswered or pushed messages (WATCH).

## Only a person with a pedal can verify
Enumeration on a real host, a DAW sending PC/CC, the pedal answering, audio while MIDI flows, the update still working. Use `docs/MIDI-TEST.md` when it exists; do not claim these from simulators.

## The pedal as USB MIDI host (Bluetooth audio image, test build 41)
SETTINGS' last row, USB PORT: FOOTSW turns the USB-C port into the host of a footswitch by cable (the M-VAVE Chocolate),
through felucca-bsp's `firmware/bsp/chip/usb_host.c`; never at power-on, never saved (`docs/BT-AUDIO-TEST.md`, "Test build
41"). Without hardware: `tests/test_usb_host.c` (the descriptor parser on the Chocolate's layout, the packets passed, the
screen's state, the host's clock), `tests/uisim.c -DPX_OWN_FX=1` (the row and the notes), `tests/midiio.c -DPX_OWN_FX=1`
(its messages read as a computer's, nothing sent to it, Bluetooth not read twice); all in `node tools/op.mjs test`.
Only the pedal can tell: the host's register sequences (unvalidated upstream too), the Chocolate's enumeration, the 5 V from
an OTG adapter with a charging input, the music during enumeration.

## Gotchas
- Never change `usb.c` descriptors or `ota.c` without the captain: they guard the update entry.
- The USB host is built into the Bluetooth audio image only (`OP_BT_AUDIO`): the release image must stay byte for byte.
