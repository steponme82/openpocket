#!/bin/sh
# OpenPocket host tests (Linux / macOS, or in the container: tools/op.mjs test; no pedal, no JieLi toolchain needed):
#   tests/run_tests.sh [PocketX_016.fwsc]
#  - engine: every block x model x extreme knob, signal / silence / overload, with the pedal's own libm (px_math.c):
#    no NaN; long runs (3 s pluck + 8 s silence): nothing runs away (only delays at FDBK 100 hold on, by design)
#  - the two-core pipeline (PRE / NAM / POST stages) gives the plain engine's output one block later
#  - NAM hand routines: their start-up check and dispatch with C stand-ins under the sanitizers; post5's assembly text
#    against the C post through a model of its instructions (tests/test_post5.py)
#  - (not run here, it needs the git history: tests/equiv.sh REF, this tree's engine against commit REF's, bit for bit)
#  - the Bluetooth audio image's engine (engine/px_dsp.h PX_OWN_FX): every existing model bit for bit as without it
#    (tests/ownfx_equiv.sh, 5,460 runs); the engine tests built with that image's flags; its own effects (docs/OWN-FX.md):
#    LOW OCTAVE (PIT), low notes get their octave, high ones and the dry signal stay as they are (tests/engine/lowoct.c);
#    SYN PAD, a caught chord held steady, swell, crossing to a new chord, fade, stomp-then-strum, no clicks (tests/engine/pad.c);
#    the expression pedal (test build 25): volume before the delay with its hold, wah sweep, whammy to SHIFT (tests/engine/exp.c);
#    TAILS (test build 40): a delay or reverb switched off, or off in the next preset, rings out, inline and in its lane
#    (tests/engine/tails.c); TREBLE BOOST (test build 40): its knobs, RANGE's shape, lopsided clipping, BOOST, its level at
#    noon matching the dry guitar's loudness over the DI loops (tests/engine/treble.c)
#  - changes without clicks (v38): knob changes glide to exactly where a full set goes, blocks on / off fade, fewer clicks
#  - USER IR (v38): the partitioned FFT convolution against the direct FIR, slots, names
#  - wet lanes (v39): the delay through its lane as inline bit for bit, the reverb's tail lag frames later, the pipeline
#  - NAM: engine/px_nam.c against tools/nam_a2.py with every factory model (needs numpy)
#  - MIDI: the musical-MIDI code (firmware/px_midi.h) on a PC: decoder, CC map, messages sent, settings byte, clock; BLE MIDI
#    (px_ble_midi.h): the Chocolate's writes, and (test build 27) the editor's SysEx kept across writes and its replies cut
#    into notifications, 3,000 of them back whole through a reference reader
#  - the Bluetooth audio image's controls (PX_OWN_FX): SYN PAD's left footswitch and the tuner on both (tests/uisim.c), CC 64
#    (tests/midiio.c), the two effects' pages drawn whole
#  - preset store: save / reload / power cut at every step of a save (firmware/px_store.h on a simulated flash)
#  - the audio callbacks' order (v42, firmware/px_aorder.h): the I2S driver calls the output's callback before the
#    input's when its interrupt enters late; every output block must still be the sound of that interrupt's guitar block
#  - USB audio (v42): the USB device code and the audio glue (bsp/chip/usb.c, firmware/px_uaudio.h) against a model of
#    the controller and a computer: with the setting off the descriptors are the earlier device's byte for byte; with
#    it on, the descriptors, the requests, 10 minutes of each stream with the clocks apart (no gap, no repeat)
#  - LED map + lights driver (led_probe.h, px_leds.h): PC7 enable after the first byte, the 595 timing, the table
#  - button lights (px_lights.h): each block lights its button, FX for its group, HOME blinks in the tuner
#  - packages: tools/pxpkg.py rebuilds the official PocketX_016 app area byte for byte (needs the official file)
#  - installer: docs/pxota.js + docs/pxpkg.js against a simulated pedal running the real update code (ota.c, ldr_core.c)
#  - flasher: tools/flash.mjs (node tools/op.mjs flash) against the same simulated pedal: update, dry run, wrong model, damaged package, rescue of a pedal that restarts in a loop
#  - rescue: tools/uboot_rescue.py (protocol, guards, resume) against a simulated UBOOT pedal, tests/uboot_sim.py
#  - hosted simulator: tools/sim.mjs driven over HTTP (controls, held buttons, screen, flash file, SysEx, the Web MIDI shim)
#  - pedal skin: its input mapping (tools/sim/pedal-input.js)
#  - sound: the WebAssembly engine on known signals, against the native build; the bundled loops against their manifest
#  - Chocolate Plus files: tools/chocolate/fcp.py against a made-up export
#  - Bluetooth audio (docs/BT-AUDIO-TEST.md): the ring and its rate (tests/test_bt_ring.c), the radio pools' allocator
#    (test_bt_lbuf.c), the compiler runtime it adds (test_bt_rt.c), SBC through Android's encoder and decoder (test_bt_sbc.c), the third-party sources (test_bt_sources.mjs),
#    the pairing store in the flash with power cuts at every step (test_bt_ps.c, test build 14)
#    and the Bluetooth images' task message queues (test_ble_taskq.c: messages of the library's sizes, whole, in order)
#  - editor: docs/pxedit.js against the simulated pedal (tests/edsim.c), also over BLE MIDI as the Bluetooth audio image
#    talks to it (test build 27), PING's figures, and the editor's own logic
#    (docs/pxmidi.js: the Control Change map against docs/MIDI.md, learned controls; docs/pxlib.js: the model library)
#  - pages: docs/index.html, docs/editor.html and the pedal skin in headless Chromium (needs Playwright: npm i playwright
#    && npx playwright install chromium)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CC="${CC:-cc}"
echo "== engine"
$CC -O2 -Iengine tests/engine/sweep2.c engine/px_dsp.c firmware/px_math.c -lm -o build/sweep && build/sweep | tail -1
$CC -O2 -Iengine tests/engine/long.c engine/px_dsp.c firmware/px_math.c -lm -o build/long && build/long | tail -1
$CC -O2 -Iengine tests/engine/pipe_test.c engine/px_dsp.c firmware/px_math.c -lm -o build/pipe_test && build/pipe_test | tail -1
# v40.2: the network split between the cores by layers, against one core and through the pipeline
$CC -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPXN_EMUVEC -Iengine tests/engine/lp_test.c firmware/px_math.c -lm -o build/lp_test && build/lp_test | tail -1
# v40.5: CPU0's waits for a second core that never moves end after the limit (a stuck CPU1: LATE, not the watchdog)
$CC -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iengine tests/engine/lp_stuck.c firmware/px_math.c -lm -o build/lp_stuck && build/lp_stuck | tail -1
# v40.2: the NAM hand routines' start-up check, timing and dispatch, with C stand-ins under the sanitizers (a fault there
# would be a boot loop on the pedal)
$CC -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPXN_HAND=1 -Iengine tests/engine/hand_test.c firmware/px_math.c -lm -o build/hand_test && build/hand_test | tail -1
$CC -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPXN_HAND=1 -DPXN_LEAN=1 -Iengine tests/engine/hand_test.c firmware/px_math.c -lm -o build/hand_test_lean && build/hand_test_lean | tail -1   # (the Bluetooth image's)
# v40.5: post5's assembly text itself, run by a model of its instructions against the C post (tests/test_post5.py), and
# the same with one offset changed, which must fail
python3 tests/test_post5.py
if python3 tests/test_post5.py "r6 = [sp+40]" "r6 = [sp+44]" > /dev/null 2>&1; then echo "test_post5 accepts a wrong routine"; exit 1; fi
$CC -O2 -Iengine -Ifirmware tests/engine/change_test.c firmware/px_math.c -lm -o build/change_test && build/change_test | tail -1
$CC -O2 -Iengine -Ifirmware tests/engine/ir_test.c firmware/px_math.c -lm -o build/ir_test && build/ir_test | tail -1
$CC -O2 -Iengine tests/engine/wet_test.c engine/px_dsp.c firmware/px_math.c -lm -o build/wet_test && build/wet_test | tail -1
echo "== the Bluetooth audio image's engine (PX_OWN_FX: the same sound in less code)"
sh tests/ownfx_equiv.sh
FX="-DPX_OWN_FX=1 -DPXN_LEAN=1 -DPXN_HOT_RAM=0 -DPX_RUNS=0 -DPX_LOOP_GUARD=1 -DPX_DLY_SHORT=1"   # (firmware/build.sh's DSPDEF under OP_BT)
$CC -O2 $FX -Iengine tests/engine/sweep2.c engine/px_dsp.c firmware/px_math.c -lm -o build/sweep_fx && build/sweep_fx | tail -1
$CC -O2 $FX -Iengine tests/engine/pipe_test.c engine/px_dsp.c firmware/px_math.c -lm -o build/pipe_fx && build/pipe_fx | tail -1
$CC -O2 $FX -Iengine tests/engine/long.c engine/px_dsp.c firmware/px_math.c -lm -o build/long_fx && build/long_fx "LOW OCTAVE" PAD "TREBLE BOOST" | tail -1
echo "== the Bluetooth audio image's own effects: LOW OCTAVE (PIT), SYN PAD, TREBLE BOOST (DRV) (docs/OWN-FX.md)"
for D in "$FX" "-DPX_OWN_FX=1"; do                     # as the image builds the engine, and with the host's defaults (PX_RUNS 1)
  for t in lowoct pad exp nam_keep tails treble; do     # (exp: test build 25's expression pedal; nam_keep: test build 28's
                                                        # factory models left out, PXN_KEEP below; tails, treble: test build
                                                        # 40's TAILS and TREBLE BOOST)
    $CC -O2 $D -Iengine tests/engine/$t.c engine/px_dsp.c firmware/px_math.c -lm -o build/$t
    build/$t > build/$t.log || { cat build/$t.log; exit 1; }; tail -1 build/$t.log
  done
done
# test build 28: the Bluetooth audio image's 4 factory NAM models (firmware/build.sh PXN_KEEP), the others playing a kept one
$CC -O2 $FX -DPXN_KEEP=0x93u -Iengine tests/engine/nam_keep.c engine/px_dsp.c firmware/px_math.c -lm -o build/nam_keep_bt
build/nam_keep_bt > build/nam_keep_bt.log || { cat build/nam_keep_bt.log; exit 1; }; tail -1 build/nam_keep_bt.log
echo "== NAM (fixed point against the float reference, every factory model)"
$CC -O2 -DPXN_EMUVEC -Iengine tests/engine/nam_test.c engine/px_nam.c -o build/nam_test
if python3 -c "import numpy" 2>/dev/null; then mkdir -p build/nam; python3 tests/test_nam.py build/nam_test > build/nam/log.txt || { cat build/nam/log.txt; exit 1; }; tail -1 build/nam/log.txt; else echo "(numpy not installed: NAM comparison skipped)"; fi
echo "== preset store"
$CC -O1 -Iengine tests/test_store.c engine/px_dsp.c firmware/px_math.c -lm -o build/test_store && build/test_store | tail -2
# (the Bluetooth audio image's, test build 39: the controls learned on the pedal ride in each copy, through power cuts)
$CC -O1 -DPX_OWN_FX=1 -Iengine tests/test_store.c engine/px_dsp.c firmware/px_math.c -lm -o build/test_store_fx && { build/test_store_fx > build/test_store_fx.log; r=$?; tail -1 build/test_store_fx.log; [ $r -eq 0 ] || { grep FAIL build/test_store_fx.log; exit 1; }; }
echo "== audio callbacks' order (the output block is its interrupt's, whichever callback the driver calls first)"
$CC -O1 -Wall -Ifirmware tests/test_aorder.c -o build/test_aorder && build/test_aorder | tail -1
echo "== USB audio (the device code and the audio glue against a model of the controller and a computer)"
UAF="-O1 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Iengine -Ifirmware -Ifirmware/bsp/chip"
$CC $UAF tests/test_usb_audio.c -lm -o build/test_usb_audio
$CC $UAF -DT_REF tests/test_usb_audio.c -lm -o build/test_usb_audio_ref      # the USB device as it was before v42
build/test_usb_audio desc > build/usb_desc_off.txt
build/test_usb_audio_ref > build/usb_desc_ref.txt
if cmp -s build/usb_desc_off.txt build/usb_desc_ref.txt; then echo "USB AUDIO off: the descriptors are the earlier device's, byte for byte"
else echo "USB AUDIO off: the descriptors differ from the earlier device's"; diff build/usb_desc_ref.txt build/usb_desc_off.txt; exit 1; fi
build/test_usb_audio > build/usb_audio.log || { cat build/usb_audio.log; exit 1; }
tail -1 build/usb_audio.log
echo "== screen (every page fully drawn)"
mkdir -p build/ui && $CC -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/uisim tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
build/uisim build/ui > build/ui/log.txt
if grep -q "never drawn" build/ui/log.txt; then grep "never drawn" build/ui/log.txt; echo "screen: FAIL"; exit 1; else echo "screen: $(wc -l < build/ui/log.txt) pages ok (build/ui/*.ppm)"; fi
# the Bluetooth audio image's screen (PX_OWN_FX): the same, then SYN PAD's left footswitch against v42's both-footswitch
# tuner, and the two effects' pages
mkdir -p build/ui_fx && $CC -O1 -Wall -Wno-unused-function -DPX_OWN_FX=1 -Iengine -Ifirmware -o build/uisim_fx tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
build/uisim_fx build/ui_fx > build/ui_fx/log.txt || { tail -3 build/ui_fx/log.txt; echo "screen (PX_OWN_FX): FAIL"; exit 1; }
if grep -q "never drawn" build/ui_fx/log.txt; then grep "never drawn" build/ui_fx/log.txt; echo "screen (PX_OWN_FX): FAIL"; exit 1; else echo "screen (PX_OWN_FX): $(wc -l < build/ui_fx/log.txt) pages ok, SYN PAD's footswitch ok (build/ui_fx/*.ppm)"; fi
echo "== MIDI (firmware/px_midi.h: decoder, Control Change map, what the pedal sends, settings, clock)"
for t in midisim midiio; do
  $CC -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/$t tests/$t.c engine/px_dsp.c firmware/px_math.c -lm
  build/$t > build/$t.log || { grep FAIL build/$t.log; exit 1; }
  tail -1 build/$t.log
done
# (the Bluetooth audio image's: CC 64 catches a SYN PAD's chord, CC 116 taps the delay's tempo, TIME stops at 780 ms; test
# build 27, a write's SysEx also goes to the web editor)
$CC -O1 -Wall -Wno-unused-function -DPX_OWN_FX=1 -DPX_DLY_SHORT=1 -DED_BT=1 -Iengine -Ifirmware -o build/midiio_fx tests/midiio.c engine/px_dsp.c firmware/px_math.c -lm
build/midiio_fx > build/midiio_fx.log || { grep FAIL build/midiio_fx.log; exit 1; }
tail -1 build/midiio_fx.log
echo "== LED map and the lights driver (LED_PROBE build, issue #8)"
mkdir -p build/lp && $CC -O1 -Wall -Wno-unused-function -Ifirmware -o build/lpsim tests/lpsim.c && build/lpsim build/lp
echo "== button lights in the normal firmware (px_lights.h, issue #8)"
$CC -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/lightsim tests/lightsim.c
build/lightsim > build/lightsim.log || { cat build/lightsim.log; exit 1; }; tail -1 build/lightsim.log
echo "== simulated pedal"
$CC -O1 -Wall -Wno-unused-function -o build/devsim tests/devsim.c
if [ -n "$1" ]; then echo "== packages"; python3 tests/test_pxpkg.py "$1"; fi
echo "== installer"
node tests/test_installer.mjs "$1"
echo "== flasher (tools/flash.mjs)"
node tests/test_flash.mjs
echo "== build entry point (tools/op.mjs)"
node tests/test_op.mjs
echo "== rescue tool (tools/uboot_rescue.py on a simulated UBOOT pedal)"
python3 tests/test_uboot_rescue.py $1
echo "== hosted simulator (tools/sim.mjs)"
node tests/test_sim.mjs
echo "== Bluetooth test image sources (third_party/jieli-ble)"
node tests/test_ble_sources.mjs
echo "== Bluetooth audio image: its sources (third_party/jieli-bt, third_party/sbc-android), the ring, the radio pools, SBC, the pairings"
node tests/test_bt_sources.mjs
$CC -O1 -Wall -Wno-unused-function -o build/test_bt_ring tests/test_bt_ring.c -lm && build/test_bt_ring
$CC -O1 -Wall -Wno-unused-function -o build/test_bt_lbuf tests/test_bt_lbuf.c && build/test_bt_lbuf
$CC -O1 -Wall -Wno-unused-function -o build/test_bt_ps tests/test_bt_ps.c && build/test_bt_ps
$CC -O2 -Wall -Wno-unused-function -o build/test_bt_rt tests/test_bt_rt.c -lm && build/test_bt_rt
SBC=third_party/sbc-android
$CC -O1 -w -I$SBC/decoder/include -I$SBC/encoder/include -Ifirmware/bt -o build/test_bt_sbc tests/test_bt_sbc.c firmware/bt/bt_sbc.c \
    $SBC/decoder/srce/*.c $SBC/encoder/srce/*.c -lm && build/test_bt_sbc
echo "== Bluetooth test image status codes"
$CC -O1 -Wall -Wno-unused-function -o build/ble_status tests/test_ble_status.c && build/ble_status
$CC -O1 -Wall -Wno-unused-function -o build/ble_vm tests/test_ble_vm.c && build/ble_vm
$CC -O1 -Wall -Wno-unused-function -I tests/ble_shim -I firmware/ble -o build/test_ble_taskq tests/test_ble_taskq.c && build/test_ble_taskq
$CC -O1 -Wall -Wno-unused-function -Wno-unused-variable -DOP_BT_AUDIO=1 -I tests/ble_shim -I firmware/ble -I firmware/bt -o build/test_bt_taskq tests/test_ble_taskq.c && build/test_bt_taskq
# (test build 37: the usr timeouts run from ble_poll instead of the 10 kHz tick, ble_os.c BT_USR_IN_TICK 0)
$CC -O1 -Wall -Wno-unused-function -Wno-unused-variable -DOP_BT_AUDIO=1 -DBT_USR_IN_TICK=0 -I tests/ble_shim -I firmware/ble -I firmware/bt -o build/test_bt_taskq0 tests/test_ble_taskq.c && build/test_bt_taskq0
# (test build 27: the two LE links, which handle is which link and when to advertise)
$CC -O1 -Wall -Wno-unused-function -I firmware/bt -o build/test_bt_le_links tests/test_bt_le_links.c
build/test_bt_le_links > build/le_links.log || { grep FAIL build/le_links.log; exit 1; }; tail -1 build/le_links.log
# (test build 28: the pedal as a footswitch's central, the advertisement filter and the ATT client against a simulated GATT server)
$CC -O1 -Wall -Wno-unused-function -I firmware/bt -o build/test_bt_le_central tests/test_bt_le_central.c
build/test_bt_le_central > build/le_central.log || { grep FAIL build/le_central.log; exit 1; }; tail -1 build/le_central.log
echo "== Chocolate Plus files (tools/chocolate/fcp.py)"
python3 tests/test_fcp.py
echo "== pedal skin input"
node tests/test_pedal_input.mjs
echo "== simulated sound"
node tests/test_audio.mjs
echo "== editor"
$CC -O1 -Wall -Wno-unused-function -o build/edsim tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
node tests/test_editor.mjs
# the Bluetooth audio image's editor over BLE MIDI (test build 27, px_editor.h ED_BT): the same tests, the SysEx in BLE MIDI
# writes and notifications (tests/test_editor.mjs EDSIM_BLE), on two LE links as the image (the Chocolate's writes between the
# editor's, a second editor on the other link, one link lost)
$CC -O1 -Wall -Wno-unused-function -DED_BT=1 -DED_BT_LINKS=2u -o build/edsim_bt tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
EDSIM_BLE=1 node tests/test_editor.mjs > build/editor_ble.log || { grep "FAIL" build/editor_ble.log; exit 1; }
grep "^BLE:" build/editor_ble.log; tail -1 build/editor_ble.log
# the Bluetooth audio image's own (test build 39, PX_OWN_FX): protocol 5, the controls learned on the pedal, MIDI_IN, over BLE MIDI
$CC -O1 -Wall -Wno-unused-function -DPX_OWN_FX=1 -DED_BT=1 -DED_BT_LINKS=2u -o build/edsim_fx_bt tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
EDSIM=build/edsim_fx_bt EDSIM_FX=1 EDSIM_BLE=1 node tests/test_editor.mjs > build/editor_fx.log || { grep "FAIL" build/editor_fx.log; exit 1; }
grep -c "ok$" build/editor_fx.log | sed 's/^/editor (FX, BLE): checks ok: /'; tail -1 build/editor_fx.log
node tests/test_ping.mjs
node tests/test_editor_logic.mjs
echo "== log channel (tools/op.mjs log)"
$CC -O1 -Wall -Wno-unused-function -DOP_BLE_TEST=1 -o build/edsim_diag tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
node tests/test_log.mjs
if node -e "require('playwright')" 2>/dev/null; then echo "== page"; node tests/test_page.mjs build/page; echo "== editor page"; node tests/test_editor_page.mjs build/editor; echo "== pedal skin"; node tests/test_pedal_page.mjs build/pedal; echo "== pedal sound"; node tests/test_audio_page.mjs build/audio; else echo "(Playwright not installed: page tests skipped)"; fi
