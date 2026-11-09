#!/bin/sh
# Build OpenPocket -> firmware/openpocket.bin (app), build/loader/ota.bin (update loader),
#                    docs/firmware/*.fwsc (packages for the web installer)   (in the container: tools/op.mjs build)
# OP_BLE=1 sh firmware/build.sh builds the Bluetooth MIDI test image instead (docs/BLE-TEST.md): the same
#   firmware plus the JieLi controller and host (third_party/jieli-ble) -> firmware/openpocket-ble.bin and build/ble/*.fwsc.
#   Nothing of the normal build's outputs is touched.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BSP="${BSP:-$HERE/bsp}"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
echo "Toolchain : $TC"
echo "BSP       : $BSP"
if [ ! -x "$TC/pi32v2/bin/clang" ]; then
  echo "ERROR: no JieLi toolchain in $TC"
  echo "  -> it is the one you already use to build CLIPPER."
  echo "  -> run again with:  JIELI_TOOLCHAIN=/path/to/toolchain sh firmware/build.sh"
  exit 1
fi
if [ ! -x "$TC/pi32v2/bin/ld" ]; then
  echo "ERROR: linker '$TC/pi32v2/bin/ld' missing (incomplete toolchain)."
  exit 1
fi
# the stack guard: on, except in the Bluetooth test image (its task stacks are outside the guard window); app.ld refuses the
# link if the guard is on and those stacks are outside. OP_GUARD_STACK=1 with OP_BLE=1 is how that check is shown to fire.
GUARD_STACK=1; [ -n "$OP_BLE" ] && GUARD_STACK="${OP_GUARD_STACK:-0}"
if [ -n "$OP_BLE" ]; then O="$HERE/build-ble"; APP="$HERE/openpocket-ble.bin"; PKG="$HERE/../build/ble"; BLEDEF="-DOP_BLE_TEST=1 -I$HERE/ble"
else O="$HERE/build"; APP="$HERE/openpocket.bin"; PKG="$HERE/../docs/firmware"; BLEDEF=""; fi
rm -rf "$O"; mkdir -p "$O"
CC="$TC/pi32v2/bin/clang -target pi32v2"
$CC -c "$BSP/chip/crt0.S"    -o "$O/crt0.o"
$CC -c "$BSP/chip/fm1_vec.S" -o "$O/vec.o"
$CC -c "$BSP/chip/fm1_isr.S" -o "$O/isr.o"
# 2nd core (entry + stacks, Felucca): only if openpocket.c enables it (#define DSP_CPU1 1)
CPU1OBJ=""
if grep -q "^#define DSP_CPU1 1" "$HERE/openpocket.c"; then
  $CC -c "$BSP/chip/fm1_cpu1.S" -o "$O/cpu1.o"; CPU1OBJ="$O/cpu1.o"
fi
# A test channel build (OP_CHANNEL=midi-test): its own version number (OP_TEST_VERSION, default 37) and a boot-screen tag, its
# own files (firmware/openpocket-midi-test.bin, docs/firmware/midi-test/). The release files are not touched.
CHAN="${OP_CHANNEL:-}"
: > "$O/channel.h"
if [ -n "$CHAN" ]; then
  case "$CHAN" in *[!a-z0-9-]*|"") echo "ERROR: OP_CHANNEL must be lowercase letters, digits and -"; exit 1;; esac
  TESTVER="${OP_TEST_VERSION:-40}"
  printf '#define OP_VERSION_NUM "%s"\n#define OP_TAG " %s"\n' "$TESTVER" "$(echo "$CHAN" | tr a-z- A-Z' ')" > "$O/channel.h"
  APP="$HERE/openpocket-$CHAN.bin"; PKG="$HERE/../docs/firmware/$CHAN"
  rm -f "$APP"
  echo "Test channel: $CHAN, version $TESTVER -> $APP, $PKG"
fi
# AC791N hardware FPU (same options as the JieLi SDK for the WL82) + DSP engine at 44.1 kHz
FP="-mcpu=r3 -mfprev1"
DSPDEF="-DPX_SR=44100.0f -DPX_OVERSAMPLE=4"
INC="-I$HERE/inc -I$HERE/../engine"
$CC $FP -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function $DSPDEF $BLEDEF -DOP_GUARD_STACK=$GUARD_STACK $INC \
    -I"$BSP/chip" -I"$BSP/boards/fm1" -include "$O/channel.h" -c "$HERE/openpocket.c" -o "$O/openpocket.o"
$CC $FP -O2 -ffunction-sections -fno-builtin $DSPDEF $INC -c "$HERE/../engine/px_dsp.c" -o "$O/px_dsp.o"
$CC $FP -O2 -fno-builtin -c "$HERE/px_math.c" -o "$O/px_math.o"
BLEOBJ=""; MAPOPT=""
if [ -n "$OP_BLE" ]; then
  MAPOPT="-Map $O/openpocket.map"
  # JieLi's LE controller (LLVM bitcode, Apache-2.0, third_party/jieli-ble) -> pi32v2 objects, and our context switch
  mkdir -p "$O/jl"
  for b in "$HERE"/../third_party/jieli-ble/*/*.bc; do
    $CC -Os -c -x ir "$b" -o "$O/jl/$(basename "$(dirname "$b")")_$(basename "$b" .bc).o"
  done
  rm -f "$O/libjl.a"; "$TC/pi32v2/bin/ar" rcs "$O/libjl.a" "$O"/jl/*.o   # an archive: the linker takes only the objects the image calls
  $CC -c "$HERE/ble/ble_ctx.S" -o "$O/ble_ctx.o"; $CC -c "$HERE/ble/ble_isr.S" -o "$O/ble_isr.o"; BLEOBJ="$O/ble_ctx.o $O/ble_isr.o $O/libjl.a"
fi
# official JieLi I2S driver (LLVM bitcode from the AC79 SDK, Apache-2.0) -> pi32v2 object
$CC -Os -c -x ir "$HERE/sdk/iis.bc" -o "$O/iis.o"
# v40: the audio path runs from RAM, as the stock firmware's does (no flash fetches through the cache, which the two cores
# share): its functions' sections are renamed .ram_text.dsp.* (tools/ramcode.py; the code itself is unchanged), and the
# linker puts them in the RAM code window (firmware/app.ld). --plain: px_dsp.o's hand-written routines (NAM, filters).
RAM_DSP="px_engine_process px_engine_part px_wet_run px_wet_swap dly_block rv_block rv_front clip_run os_up os_down
  ir_run fft64 rfft128 cab_detail ps_voice_al ps_align amp_stage bq_block bq_block2 drive_block nam_run fq_loop_c px_nam_dual
  pxn_process pxn_block pxn_issue pxn_layers_pipe pxn_direct pxn_post_part pxn_rows pxn_rows_cpu pxn_gather pxn_range_cpu
  pxn_serve pxn_srv_begin pxn_srv_end pxn_srv_active pxn_srv_block pxn_help_job pxn_vstart pxn_vwait pxn_kick pxn_venq
  pxn_need pxn_hneed pxn_hstart pxn_hend pxn_layer_block pxn_dual pxn_dot3x4_c pxn_dot4_c pxn_dot1_c pxn_rdot_c pxn_rdl_c
  pxn_post_c pxn_gcopy_c lo_fast"
RAM_FW="a_enc a_dec a_collect a_commit dsp_block dsp_out dsp_load dsp_job post_job pipe_job dsp_pipe dsp_pipe_run
  fm1_alnk0_irq tn_capture fm1_cpu1_run fm1_cpu1_wait fm1_cpu1_retire a_hrun a_hwait b_ticks memcpy memset"
python3 "$HERE/../tools/ramcode.py" "$O/px_dsp.o" --plain $RAM_DSP
python3 "$HERE/../tools/ramcode.py" "$O/openpocket.o" $RAM_FW
# a direct call between the flash code and the RAM code is out of the CPU's call range: refuse it with its name
# (v40: the audio path's RAM code, .ram_text.dsp.*, is within range by its place, firmware/app.ld)
python3 "$HERE/../tools/check_calls.py" "$O/crt0.o" "$O/vec.o" "$O/isr.o" $CPU1OBJ "$O/openpocket.o" "$O/px_dsp.o" "$O/px_math.o"
"$TC/pi32v2/bin/ld" --defsym "op_guard_stack=$GUARD_STACK" -T "$HERE/app.ld" "$O/crt0.o" "$O/vec.o" "$O/isr.o" $CPU1OBJ "$O/openpocket.o" "$O/px_dsp.o" "$O/px_math.o" "$O/iis.o" $BLEOBJ $MAPOPT -o "$O/openpocket.elf"
# symbol map: used to translate a crash address (PC) into a function name
"$TC/common/bin/objdump" -t "$O/openpocket.elf" 2>/dev/null | sort > "$O/symbols.txt" || true
"$TC/common/bin/objcopy" -O binary -j .text     "$O/openpocket.elf" "$O/text.bin"
"$TC/common/bin/objcopy" -O binary -j .data     "$O/openpocket.elf" "$O/data.bin"
"$TC/common/bin/objcopy" -O binary -j .ram_text "$O/openpocket.elf" "$O/rt.bin" 2>/dev/null || : > "$O/rt.bin"
python3 - "$TC" "$O" "$APP" <<'PY'
import re,subprocess,sys
tc,out,dest=sys.argv[1:]
syms=subprocess.run([f"{tc}/common/bin/objdump","-t",f"{out}/openpocket.elf"],capture_output=True,text=True,check=True).stdout
def sym(n):
    m=re.search(rf"^([0-9a-f]+) .*\s{n}$",syms,re.M); return int(m.group(1),16) if m else None
load=sym("_data_load"); rt_load=sym("_rt_load"); start=sym("_start")
assert start==0x02000120,hex(start or 0)
img=bytearray(open(f"{out}/text.bin","rb").read())
for at,part in sorted([(rt_load,open(f"{out}/rt.bin","rb").read()),(load,open(f"{out}/data.bin","rb").read())]):
    if part:
        assert at-0x02000120>=len(img),"sections overlap"
        img+=b"\xff"*(at-0x02000120-len(img))+part
img+=b"\xff"*(-len(img)%4)
open(dest,"wb").write(img); print(f"OK -> openpocket.bin: {len(img)} bytes")
PY

# ---- update loader (loader/, felucca-bsp's, Pocket X layout) -> build/loader/ota.bin ----
L="$O/loader"; mkdir -p "$L"
$CC -c "$HERE/../loader/crt0_ldr.S" -o "$L/crt0_ldr.o"
$CC -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function -I"$HERE/inc" -I"$BSP/chip" -I"$BSP/boards/fm1" \
    -c "$HERE/../loader/loader.c" -o "$L/loader.o"
"$TC/pi32v2/bin/ld" --gc-sections -e _start -T "$HERE/../loader/loader.ld" "$L/crt0_ldr.o" "$L/loader.o" -o "$L/loader.elf"
"$TC/common/bin/objcopy" -O binary -j .text "$L/loader.elf" "$L/loader.bin"
# the loader rewrites the flash: nothing of it may live in XIP (0x02xxxxxx)
"$TC/common/bin/objdump" -h "$L/loader.elf" | python3 -c '
import sys
for ln in sys.stdin:
    p = ln.split()
    if len(p) > 4 and p[1].startswith(".") and int(p[2], 16) and int(p[3], 16) >= 0x02000000:
        sys.exit("ERROR: loader section " + p[1] + " outside RAM")'

python3 "$HERE/../tools/mkloader.py" "$L/loader.bin" "$L/ota.bin"

# ---- update packages for the web installer -> docs/firmware/ (PocketX_9NN.fwsc, BlackBox_9NN.fwsc, manifest.json) ----
VER=$(sed -n 's/^#define OP_VERSION_NUM "\([0-9]*\)".*/\1/p' "$HERE/openpocket.c")
[ -n "$CHAN" ] && VER="$TESTVER"
mkdir -p "$PKG"
python3 "$HERE/../tools/pxpkg.py" "$APP" "$L/ota.bin" "$PKG" --version "$VER" \
    --sdk-files "$HERE/sdk/packager"
