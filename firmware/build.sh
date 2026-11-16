#!/bin/sh
# Build OpenPocket -> firmware/openpocket.bin (app), build/loader/ota.bin (update loader),
#                    docs/firmware/*.fwsc (packages for the web installer)   (in the container: tools/op.mjs build)
# OP_BLE=1 sh firmware/build.sh builds the Bluetooth MIDI test image instead (docs/BLE-TEST.md): the same
#   firmware plus the JieLi controller and host (third_party/jieli-ble) -> firmware/openpocket-ble.bin and build/ble/*.fwsc.
#   Nothing of the normal build's outputs is touched.
# OP_BT=1 sh firmware/build.sh builds the Bluetooth audio test image (docs/BT-AUDIO-TEST.md): the BLE test image with JieLi's
#   classic controller and host (third_party/jieli-ble + third_party/jieli-bt, firmware/bt/members.txt) and Android's SBC decoder
#   (third_party/sbc-android) in place of the LE stack -> firmware/openpocket-bt.bin and build/bt/*.fwsc.
set -e
[ -n "$OP_BT" ] && OP_BLE=1
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
LDS="$HERE/app.ld"
if [ -n "$OP_BT" ]; then O="$HERE/build-bt"; APP="$HERE/openpocket-bt.bin"; PKG="$HERE/../build/bt"; LDS="$HERE/bt/app-bt.ld"
  # three tasks (the controller's, the host's, the app's: the pairing's crypto runs in the controller's, config_bt_function 0) of
  # 4 KB in NOINIT (ble_os.c's sizes; test builds 1 to 10 had four of 2.75 KB, the fourth never used); the flash's unique ID for the address
  BLEDEF="-DOP_BLE_TEST=1 -DOP_BT_AUDIO=1 -DFL_UID -I$HERE/ble -I$HERE/bt"
elif [ -n "$OP_BLE" ]; then O="$HERE/build-ble"; APP="$HERE/openpocket-ble.bin"; PKG="$HERE/../build/ble"; BLEDEF="-DOP_BLE_TEST=1 -I$HERE/ble"
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
  TESTVER="${OP_TEST_VERSION:-42}"
  printf '#define OP_VERSION_NUM "%s"\n#define OP_TAG " %s"\n' "$TESTVER" "$(echo "$CHAN" | tr a-z- A-Z' ')" > "$O/channel.h"
  APP="$HERE/openpocket-$CHAN.bin"; PKG="$HERE/../docs/firmware/$CHAN"
  rm -f "$APP"
  echo "Test channel: $CHAN, version $TESTVER -> $APP, $PKG"
fi
# AC791N hardware FPU (same options as the JieLi SDK for the WL82) + DSP engine at 44.1 kHz
FP="-mcpu=r3 -mfprev1"
DSPDEF="-DPX_SR=44100.0f -DPX_OVERSAMPLE=4"
# v40.2: the test images' flash is full (engine/px_nam.c PXN_LEAN; v40.4: px_dsp.c PX_RUNS). v40.5: the BLE image keeps px_nam.c's
# hot routines (rdm6, rdm15, post5) in the RAM code; the Bluetooth audio image runs them from the flash, as the release does
# (test build 14: its RAM code window holds the engine's buffers, bt/app-bt.ld .bss_hi, and has no room for them)
HOTRAM=1; [ -n "$OP_BT" ] && HOTRAM=0
[ -n "$OP_BLE" ] && DSPDEF="$DSPDEF -DPXN_LEAN=1 -DPXN_HOT_RAM=$HOTRAM -DPX_RUNS=0"
[ -n "$OP_BT" ] && DSPDEF="$DSPDEF -DPX_LOOP_GUARD=1 -DPX_TABLES_RAM=1 -DPX_DLY_SHORT=1"   # test build 13: the reverb's loop ends at PX_MAX_BLOCK, counted; test build 20: the second core's tables in RAM (engine/px_dsp.c)
# the Bluetooth audio image's own engine (engine/px_dsp.h PX_OWN_FX): its own effects, LOW OCTAVE, SYN PAD, TREBLE BOOST (docs/OWN-FX.md),
# paid for by the drive and delay code no model reaches (left out: the same sound) and by the delay ring (456 ticks shorter).
# In DSPDEF, so that openpocket.c (g_eng, px_audio.h) and px_dsp.c see one px_engine
[ -n "$OP_BT" ] && DSPDEF="$DSPDEF -DPX_OWN_FX=1"
# test build 28: the Bluetooth audio image builds in 4 of the 12 factory NAM models (TWIN, AC15, JCM900, MARK IV: bits 0, 1, 4,
# 7, engine/px_nam_factory.h PXN_KEEP), 59,872 bytes of flash for the LE central role; the others play the closest kept one
[ -n "$OP_BT" ] && DSPDEF="$DSPDEF -DPXN_KEEP=0x93u"
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
  if [ -n "$OP_BT" ]; then BCS=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' -e "s|^|$HERE/../|" "$HERE/bt/members.txt")
  else BCS=$(ls "$HERE"/../third_party/jieli-ble/*/*.bc); fi
  if [ -n "$OP_BT" ] && [ "${OP_BT_FOLD:-1}" != 0 ]; then
    # test build 14 (OpenPocket v42): the Bluetooth audio image links the library folded (tools/jlfold.py fold, the probe's
    # variant B: the constants bt/bt_cfg.c and px_audio.h define folded into the members, and the print helpers the
    # firmware defines empty made internal and empty, so the code they guard and their strings go; 94,664 B less in test
    # build 13). Unfolded, v41's and v42's firmware no longer fits the app slot. The same object names: the rest is as below.
    # OP_BT_FOLD=0 (OP_BT=1 OP_BT_FOLD=0 sh firmware/build.sh) compiles the members as the BLE image does: the image then
    # does not fit its flash window.
    python3 "$HERE/../tools/jlfold.py" fold "$TC" "$HERE/bt/bt_cfg.c" "$O/jl" $BCS
  else
    for b in $BCS; do
      $CC -Os -c -x ir "$b" -o "$O/jl/$(basename "$(dirname "$b")")_$(basename "$b" .bc).o"
    done
  fi
  # v40 made memcpy and memset RAM code, out of the call range of the library's code at the end of the flash: the
  # library calls flash copies instead (ble/ble_libc.c)
  for o in "$O"/jl/*.o; do python3 "$HERE/../tools/elfsym.py" "$o" memcpy=ble_memcpy memset=ble_memset; done
  rm -f "$O/libjl.a"; "$TC/pi32v2/bin/ar" rcs "$O/libjl.a" "$O"/jl/*.o   # an archive: the linker takes only the objects the image calls
  $CC -c "$HERE/ble/ble_ctx.S" -o "$O/ble_ctx.o"; $CC -c "$HERE/ble/ble_isr.S" -o "$O/ble_isr.o"; BLEOBJ="$O/ble_ctx.o $O/ble_isr.o $O/libjl.a"
  if [ -n "$OP_BT" ]; then
    # the classic baseband's interrupts, counted; Android's SBC decoder (Apache-2.0) and its wrapper (firmware/bt/bt_sbc.c)
    $CC -c "$HERE/bt/bt_isr.S" -o "$O/bt_isr.o"
    SBC="$HERE/../third_party/sbc-android/decoder"; mkdir -p "$O/sbc"
    for c in "$SBC"/srce/*.c "$HERE/bt/bt_sbc.c"; do
      $CC $FP -Os -ffunction-sections -fno-builtin -w -DBT_SBC_LO2=1 -I"$HERE/bt/sbc_port" -I"$HERE/inc" -I"$SBC/include" -I"$HERE/bt" -c "$c" -o "$O/sbc/$(basename "$c" .c).o"
    done
    # the compiler runtime the classic stack needs and the toolchain lacks (64-bit division, a few double steps)
    $CC -O2 -fno-builtin -Wall -c "$HERE/bt/bt_rt.c" -o "$O/bt_rt.o"
    BLEOBJ="$BLEOBJ $O/bt_isr.o $O/bt_rt.o $O/sbc/*.o"
  fi
fi
# official JieLi I2S driver (LLVM bitcode from the AC79 SDK, Apache-2.0) -> pi32v2 object
$CC -Os -c -x ir "$HERE/sdk/iis.bc" -o "$O/iis.o"
# v40: the audio path runs from RAM, as the stock firmware's does (no flash fetches through the cache, which the two cores
# share): its functions' sections are renamed .ram_text.dsp.* (tools/ramcode.py; the code itself is unchanged), and the
# linker puts them in the RAM code window (firmware/app.ld). --plain: px_dsp.o's hand-written routines (NAM, filters).
RAM_DSP="px_engine_process px_engine_part px_wet_run px_wet_swap dly_block rv_block rv_front clip_run os_up os_down
  ir_run fft64 rfft128 cab_detail ps_voice_al ps_align amp_stage bq_block bq_block2 drive_block nam_run fq_loop_c px_nam_dual
  pxn_process pxn_block pxn_direct pxn_post_part pxn_rows pxn_rows_cpu pxn_gather pxn_range_cpu pxn_dual
  px_nam_lp_b px_nam_lp_queued px_nam_lp_split px_nam_lp_split_now pxn_lp_a pxn_lp_b pxn_lp_queued pxn_lp_split
  pxn_lp_split_now pxn_frame_ticks px_nam_lp_handed px_nam_lp_before pxn_lp_handed pxn_lp_before
  px_nam_lp_split_used px_nam_lp_force pxn_lp_split_used pxn_lp_force px_nam_lp_gave_up pxn_lp_gave_up
  ps_voice_run syn_organ_run pitch_run cab_detail_run gate_run comp_run bq_chain"
# the Bluetooth audio image's own effects (PX_OWN_FX): LOW OCTAVE's octave, SYN PAD's grains (px_pad_press and px_pad_stop,
# which the main loop calls, stay in the flash)
[ -n "$OP_BT" ] && RAM_DSP="$RAM_DSP lo_fast pad_run pad_step pad_grain_new pad_flatten"
# (test build 43: USB RETURN's mix into the chain, px_rt_mix, as every block's)
[ -n "$OP_BT" ] && RAM_DSP="$RAM_DSP px_rt_mix"
# v40.2: the layer split's path (pxn_lp_*, lp_job, dsp_lp_run) in RAM; out of it, to make the room: the frame split's serving
# (now the fallback when the layer split cannot run), the vector unit's paths (never chosen: slower) and the C versions
# of the hand routines (used only if a hand one fails its start-up check: the network is then too slow anyway)
RAM_FW="a_enc a_dec a_block a_send a_collect a_commit dsp_block dsp_out dsp_load dsp_job post_job post_run dsp_pipe lp_job dsp_lp_run a_lp_post lp_split
  fm1_alnk0_irq tn_capture fm1_cpu1_run fm1_cpu1_wait fm1_cpu1_retire a_hrun a_hwait b_ticks memcpy memset"
# test build 21, the Bluetooth audio image: the NAM's frame-split helper job too, which the second core runs (from the flash up to 20)
[ -n "$OP_BT" ] && RAM_DSP="$RAM_DSP pxn_help_job"
python3 "$HERE/../tools/ramcode.py" "$O/px_dsp.o" --plain $RAM_DSP
python3 "$HERE/../tools/ramcode.py" "$O/openpocket.o" $RAM_FW
# a direct call between the flash code and the RAM code is out of the CPU's call range: refuse it with its name
# (v40: the audio path's RAM code, .ram_text.dsp.*, is within range by its place, firmware/app.ld)
python3 "$HERE/../tools/check_calls.py" "$O/crt0.o" "$O/vec.o" "$O/isr.o" $CPU1OBJ "$O/openpocket.o" "$O/px_dsp.o" "$O/px_math.o"
"$TC/pi32v2/bin/ld" --defsym "op_guard_stack=$GUARD_STACK" -T "$LDS" "$O/crt0.o" "$O/vec.o" "$O/isr.o" $CPU1OBJ "$O/openpocket.o" "$O/px_dsp.o" "$O/px_math.o" "$O/iis.o" $BLEOBJ $MAPOPT -o "$O/openpocket.elf"
# symbol map: used to translate a crash address (PC) into a function name
"$TC/common/bin/objdump" -t "$O/openpocket.elf" 2>/dev/null | sort > "$O/symbols.txt" || true
if [ -n "$OP_BT" ]; then
  # bt/bt_a2dp.c reaches one of the library's static tables, a2dp_media_codec.c's channel (BT_CH, for the heal), from the global
  # beside it. Test build 12 also read lmp.c's lmp_acl_link (gone in build 13: its distances stay checked, as a tripwire, with
  # current_conn's). The vendor's objects never change, but this build must prove the distances hold
  python3 - "$O/symbols.txt" <<'PY'
import re,sys
want=("lmp_acl_link","local_private_key","a2dp_mutual_toggle","channel","current_conn")
s={}
for ln in open(sys.argv[1]):
    m=re.match(r"^([0-9a-f]{8}) .*\s([0-9a-f]{8}) (\S+)$",ln.rstrip("\n"))
    if m and m.group(3) in want: s.setdefault(m.group(3),[]).append((int(m.group(1),16),int(m.group(2),16)))
# (test build 23: the LE members linked for BLE MIDI may have statics of the same names, channel above all: each one is taken
# where a2dp_media_codec.c's and lmp.c's own layouts put it, and only a missing one fails)
bad=[n for n in ("lmp_acl_link","local_private_key","a2dp_mutual_toggle") if len(s.get(n,[]))!=1]
if bad: sys.exit("bt layout: not exactly one symbol for "+", ".join(bad))
(la,ls),(pk,_),(mt,_)=(s[n][0] for n in ("lmp_acl_link","local_private_key","a2dp_mutual_toggle"))
near=lambda n,at: next(((a,z) for a,z in s.get(n,[]) if a==at), (s.get(n,[(0,0)])[0]))
(ch,_),(cc,_)=near("channel",mt+8),near("current_conn",mt+4)
if len(s.get("channel",[]))>1 or len(s.get("current_conn",[]))>1: print(f"bt layout: {len(s.get('channel',[]))} symbols named channel, {len(s.get('current_conn',[]))} current_conn: the ones beside a2dp_mutual_toggle taken")
ok=ls==0x250 and pk==la+0x254 and ch==mt+8 and cc==mt+4
print(f"bt layout: lmp_acl_link {la:#010x} ({ls:#x}), local_private_key {pk:#010x}, a2dp_mutual_toggle {mt:#010x}, current_conn {cc:#010x}, channel {ch:#010x}: {'ok' if ok else 'MOVED'}")
if not ok: sys.exit("bt layout: the library's statics moved: firmware/bt/bt_a2dp.c BT_CH must follow if channel moved (lmp_acl_link and current_conn: nothing reads them in test build 13)")
PY
  # the compiled controller and A2DP code, to compare with the IR (the artifact's listings/; neither step can fail the build).
  # (Test build 14: the .s listings are the library as published, unfolded; linked.txt is the code the image links)
  mkdir -p "$O/listings"
  # test build 13: the compiler's own assembly of px_dsp.o (the same flags): rv_block's loop heads, their shared cut path and
  # its frame (engine/px_dsp.c PX_LOOP_CAP; test build 12's crash had n at [sp+128], k at [sp+88], wo at [sp+132])
  $CC $FP -O2 -ffunction-sections -fno-builtin $DSPDEF $INC -S "$HERE/../engine/px_dsp.c" -o "$O/listings/px_dsp.s" 2>/dev/null || true
  for b in btctrler/lmp.c btstack/a2dp_media_codec.c btstack/a2dp_sink.c; do
    $CC -Os -S -x ir "$HERE/../third_party/jieli-bt/$b.bc" -o "$O/listings/$(basename "$b").s" 2>/dev/null || true
  done
  # (test build 14: dly_block too, its 11 loops capped as rv_block's 4, engine/px_dsp.c PX_LOOP_CAP)
  for f in lmp_rx_handler lmp_private_open_sbc_channel lmp_send_acl_u_packet_to_host acl_u_packet_analyse \
           a2dp_media_packet_user_handler a2dp_rx_notice_to_decode rv_block dly_block; do
    r=$(awk -v n="$f" '$NF==n && $3=="F" {print $1" "$(NF-1); exit}' "$O/symbols.txt")
    [ -n "$r" ] || continue
    a=${r% *}; z=${r#* }
    "$TC/common/bin/objdump" -d --start-address=$((0x$a)) --stop-address=$((0x$a + 0x$z)) "$O/openpocket.elf" >> "$O/listings/linked.txt" 2>&1 || true
  done
  # the folding probe (the room Bluetooth MIDI needs): the library built through text IR, as is (R), with its constant
  # configuration folded (A) and with the print helpers emptied too (B: since test build 14 the real link's, so B must give
  # "the real image's bytes"; R shows how far the unfolded library is over), each linked beside the image, which is not
  # touched; the sizes go to the log and listings/fold.txt (tools/jlfold.py; it cannot fail the build)
  python3 "$HERE/../tools/jlfold.py" probe "$TC" "$HERE/bt/bt_cfg.c" "$O/fold" "$O/listings/fold.txt" $BCS -- \
    "$TC/pi32v2/bin/ld" --defsym "op_guard_stack=$GUARD_STACK" -T "$LDS" "$O/crt0.o" "$O/vec.o" "$O/isr.o" $CPU1OBJ \
    "$O/openpocket.o" "$O/px_dsp.o" "$O/px_math.o" "$O/iis.o" $BLEOBJ -o "$O/openpocket.elf" || echo "fold probe: failed"
fi
"$TC/common/bin/objcopy" -O binary -j .text     "$O/openpocket.elf" "$O/text.bin"
"$TC/common/bin/objcopy" -O binary -j .data     "$O/openpocket.elf" "$O/data.bin"
"$TC/common/bin/objcopy" -O binary -j .ram_text "$O/openpocket.elf" "$O/rt.bin" 2>/dev/null || : > "$O/rt.bin"
# (test build 17: the Bluetooth audio image's own data above the radio's window, bt/app-bt.ld .data_hi; none elsewhere)
"$TC/common/bin/objcopy" -O binary -j .data_hi  "$O/openpocket.elf" "$O/datahi.bin" 2>/dev/null || : > "$O/datahi.bin"
python3 - "$TC" "$O" "$APP" <<'PY'
import os,re,subprocess,sys
tc,out,dest=sys.argv[1:]
syms=subprocess.run([f"{tc}/common/bin/objdump","-t",f"{out}/openpocket.elf"],capture_output=True,text=True,check=True).stdout
def sym(n):
    m=re.search(rf"^([0-9a-f]+) .*\s{n}$",syms,re.M); return int(m.group(1),16) if m else None
load=sym("_data_load"); rt_load=sym("_rt_load"); start=sym("_start"); dh_load=sym("_data_hi_load")
be,cl,ch,nb=sym("_bss_end"),sym("_cram_lo"),sym("_cram_hi"),sym("_ble_stack_hi")
re_,hs,he=sym("_rt_end"),sym("_bss_hi_start"),sym("_bss_hi_end")
ls_,le=sym("_bss_lo_start"),sym("_bss_lo_end")
if ls_ is not None and le and le > ls_: print(f"low RAM   : {le-ls_} of 24576 bytes (below the RAM)")
l2s,l2e=sym("_bss_lo2_start"),sym("_bss_lo2_end")
if l2s is not None and l2e and l2e > l2s: print(f"low RAM 2 : {l2e-l2s} of 7680 bytes (the decoded music)")
et,etf=sym("_etext"),sym("_etext_fw")
if et and etf: print(f"flash     : firmware code to {etf:#x}, library code to {et:#x}")
ps_,pe=sym("_pool_tail_start"),sym("_pool_tail_end")
if ps_ and pe: print(f"pool tail : {pe-ps_} bytes, {0x01C5E000-pe} left before the RAM code")
if re_ and he: print(f"hi RAM    : RAM code {re_-0x01C5E000}, data {he-hs}, free {0x01C74000-he} of 90112 bytes (the RAM code window)")
if be: print(f"RAM       : {be-0x01C08000} of 98304 bytes ({98304-(be-0x01C08000)} free)")
bd,bw,b0,c1s,c1e=sym("bd_base"),sym("_bt_window_end"),sym("_bt_c0_end"),sym("_c1_ram_start"),sym("_c1_ram_end")
if bd is not None and bw and b0 and c1s and c1e:   # the Bluetooth audio image (bt/app-bt.ld, test build 14)
    w=bd+0x10000
    print(f"bt window : {bd:#x}..{w:#x}: the heap to {bw:#x}, then the canary and CPU0's data to {b0:#x} ({b0-w:+d}: "
          f"{'the firmware data is above the window' if b0>=w else 'SHORT, the firmware data begins inside the window'}); "
          f"the second core's mailbox and stacks {c1s:#x}..{c1e:#x} ({c1s-w:+d} above it)")
    us,st,mb=sym("fm1_cpu1_ustack"),sym("fm1_cpu1_sstack_top"),sym("fm1_cpu1_mb")
    if us is not None:   # (test build 14: the second core's memory out of the radio's window; app-bt.ld places it)
        print(f"bt window : CPU1 mailbox {mb if mb is None else hex(mb)}, stacks {us:#x}..{st:#x}, region {c1e-c1s} bytes "
              f"(3136 expected), RAM ends {0x01C08000+98304:#x}")
        bad=[t for t,ok in (("the region begins inside the window",c1s>=w),("the stacks are not in the region",c1s<=us and st<=c1e),
                            ("the mailbox is not in the region",mb is None or c1s<=mb<c1e),("the region runs past the RAM",c1e<=0x01C08000+98304)) if not ok]
        if c1e-c1s!=3136: print(f"bt window : NOTE the region is {c1e-c1s} bytes, not 3136: something else is in it, or padding")
        if bad: sys.exit("bt window: the second core's memory: "+"; ".join(bad))
    # test build 17: nothing of the firmware's own in the window but what only CPU0 and the radio touch. Up to build 16 the
    # music's mix buffer and the firmware's initialised data (the output stage's gains, the NAM network's kernel pointers),
    # which the second core reads and writes in every block, lay there. Every input section of a non-library file inside
    # [bd_base, bd_base + 64 KB), from the link map, must be one of these (CPU0 and the radio only):
    cpu0_only={".bd_base",".bss.lo2.bt_ring",".bss.lo2.sbc",".bss.lo.nm_stage",".bss.bt_heap",".bss.bt_canary",".sdp_record_item"}
    cpu0_files=("bt_sbc.o","iis.o","bt_rt.o")   # (the SBC decoder, the I2S driver, the runtime: the radio's task and CPU0's interrupt)
    mp=f"{out}/openpocket.map"
    if os.path.exists(mp):
        ml=open(mp).read().split("\n"); found=[]; ours=[]
        for i,l in enumerate(ml):
            m=re.match(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$",l)
            if not m and re.match(r"^ (\S+)$",l) and i+1<len(ml):
                m2=re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$",ml[i+1])
                if m2: m=(l.strip(),)+m2.groups()
            if m and not isinstance(m,tuple): m=m.groups()
            if not m: continue
            sec,a,sz,f=m[0],int(m[1],16),int(m[2],16),m[3]
            if sz==0 or "libjl.a" in f or "/sbc/" in f or not (a<w and a+sz>bd): continue
            base=os.path.basename(f); ours.append(f"{sec} {base} {a:#x}+{sz}")
            if sec not in cpu0_only and base not in cpu0_files: found.append(f"{sec} {base} {a:#x}+{sz}")
        print(f"bt window : the firmware's own inside it: {'; '.join(ours) if ours else 'none'}")
        dhs,dhe,mix=sym("_data_hi_start"),sym("_data_hi_end"),sym("bt_mix")
        print(f"bt window : the firmware's data {dhs if dhs is None else hex(dhs)}..{dhe if dhe is None else hex(dhe)}, "
              f"the music's mix {mix if mix is None else hex(mix)} ({'above the window' if dhs and mix and dhs>=w and mix>=w else 'NOT above the window'})")
        if dhs is None or mix is None or dhs<w or mix<w: found.append("the firmware's data or the music's mix is not above the window")
        if found: sys.exit("bt window: what the second core may touch is inside the radio's window: "+"; ".join(found))
    else: print("bt window : no link map, the firmware's own data in the window NOT checked")
if nb and nb >= 0x01C7C000 and nb > sym("_ble_stack_lo"): print(f"NOINIT    : {nb-0x01C7C000} of 15696 bytes up to the BLE task stacks' end")
if cl is not None and ch is not None: print(f"cache RAM : {ch-cl} of 28672 bytes (the rest, {28672-(ch-cl)}, to the NAM network's vector unit)")
assert start==0x02000120,hex(start or 0)
img=bytearray(open(f"{out}/text.bin","rb").read())
parts=[(rt_load,open(f"{out}/rt.bin","rb").read()),(load,open(f"{out}/data.bin","rb").read())]
dh=open(f"{out}/datahi.bin","rb").read()
if dh:   # (test build 17: bt/app-bt.ld .data_hi, copied by fm1_cstart)
    assert dh_load is not None,"data_hi without _data_hi_load"
    parts.append((dh_load,dh))
for at,part in sorted(parts):
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
