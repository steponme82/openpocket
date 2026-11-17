// SPDX-License-Identifier: GPL-3.0-only
// node tools/op.mjs log: read the pedal's state over USB MIDI instead of from its screen, with what each field means and a
// verdict. The request is DIAG (docs/EDITOR-PROTOCOL.md), read only; docs/pxedit.js and docs/pxota.js do the talking.
//   log [--follow] [--json] [--wait SEC] [--interval SEC] [--count N] [--scan SEC]
import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { C, Editor } from "../docs/pxedit.js";
import { IS_OTA, PORT_RE, Updater } from "../docs/pxota.js";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const USAGE = `usage: node tools/op.mjs log [--follow] [--json] [--wait SEC] [--interval SEC] [--count N] [--scan SEC]
  --wait SEC      keep trying for this long until the pedal has finished starting (use it right after a flash)
  --interval SEC  time between two readings (default 2): the counters must rise between them
  --count N       how many readings (default 2; with --follow, no limit)
  --follow        keep reading until stopped
  --json          one JSON object per reading
  --scan SEC      then listen for the advertisement on this Mac: SEC seconds on its service UUID, SEC seconds on everything
Exit: 0 pass or running, 1 fail, 2 bad arguments, 3 undecided (still starting, the scanner could not run, no log channel).`;
const BLE_MIDI = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";

// firmware/openpocket.c: ST_NAME, rst_names (tests/test_log.mjs compares them)
export const BOOT_STAGES = ["?", "INPUTS", "PRESET", "ENGINE INIT", "ENGINE SET", "I2S START", "UI DRAW", "FIRST 6 S", "OK"];
export const RESET_CAUSES = ["POWER-ON", "VDDIO LOW", "WATCHDOG", "VCM", "LONG PRESS", "1.2V", "SOFT"];
const STAGE_OK = BOOT_STAGES.indexOf("OK");
const FAIL_STEPS = ["none", "task_create failed", "btctrler_task_init failed", "the controller task did not answer (btctrler_task_ready)", "ll_hci_adv_enable failed", "still starting after 5 s"];

// docs/BLE-TEST.md, "The advertising line"
const MEANING = {
  i: "baseband event interrupts (IRQ 45): rising means the radio runs",
  e: "HCI packets the controller returned",
  r: "receive interrupts (IRQ 29): above 0 means a scanner's request was heard",
  t: "library software timers that fired: rising means the controller's tick runs",
  s: "slot-timer stand-in calls: above 0 means the link scheduler wants a timer this image lacks",
  vm: "stored radio records: found mask (7 = VM 106, 107, 108), area / 4096, first two bytes of VM 106",
  CON: "baseband control 0x28000", IEN: "baseband interrupt enables 0x28028", IPND: "baseband pending 0x28030", STAT: "baseband busy flags 0x28038",
  IC45: "IRQ 45 configuration: bit 0 enabled, bits 3:1 priority, 0 = off", IC29: "IRQ 29 configuration: 0 = off", BEN: "BLE block enable 0x2FC80: bit 0 set = on",
};

const number = (line, key, radix = 10) => {
  const m = new RegExp(`\\b${key} ?(-?[0-9A-F]+)\\b`).exec(line);
  return m ? parseInt(m[1], radix) : null;
};

// the status line, exactly as the screen scrolls it
export function parseBle(raw) {
  let m;
  if (raw.startsWith("ADV ")) {
    const f = { state: "advertising", raw };
    for (const k of ["i", "e", "r", "t", "s"]) f[k] = number(raw, k);
    for (const k of ["CON", "IEN", "IPND", "STAT", "IC45", "IC29", "BEN"]) f[k] = number(raw, k, 16);
    if ((m = / V ([0-9A-F]+) ([0-9A-F]+) ([0-9A-F]+)\b/.exec(raw))) f.vm = { found: parseInt(m[1], 16), area: parseInt(m[2], 16), trim: parseInt(m[3], 16) };
    return f;
  }
  if ((m = /^FAILED (\d+) (-?\d+)(?:  step (\S+) stage (\S+))?(?: wait ([0-9A-F]{8}))?/.exec(raw))) {
    return { state: "failed", raw, step: +m[1], code: +m[2], stepName: m[3] || null, stage: m[4] || null, wait: m[5] || null };
  }
  if ((m = /^STARTING (\d+)/.exec(raw))) return { state: "starting", raw, stage: +m[1] };
  if (/^BT (VIS|START|OFF)\b/.test(raw)) return { state: "bt-audio", raw };   // the Bluetooth audio image (docs/BT-AUDIO-TEST.md)
  if (raw.startsWith("STALL ")) return { state: "stall-record", raw };           // its record of an earlier stall, shown while Bluetooth is off
  return { state: raw.startsWith("OFF") ? "off" : "unknown", raw };
}

export function parseBoot(raw) {
  const rst = number(raw, "RST", 16) ?? 0;
  const stage = number(raw, "STAGE") ?? 0;
  return { raw, clockMs: number(raw, "UP"), safe: number(raw, "SAFE") === 1, stage, stageName: BOOT_STAGES[stage] ?? "?", resetBits: rst,
    resetBy: RESET_CAUSES.filter((_, i) => rst >> i & 1), failedBoots: number(raw, "GUARD") };
}

export function parseLast(raw) {
  if (!raw.startsWith("LAST FAILED")) return { raw, failed: false };
  const stage = number(raw, "STAGE") ?? 0;
  const out = { raw, failed: true, stage, stageName: BOOT_STAGES[stage] ?? "?", blocks: number(raw, "BLOCKS"), load: number(raw, "LOAD"), peak: number(raw, "PEAK"),
    nans: number(raw, "NAN"), cpu1: number(raw, "CPU1"), loops: number(raw, "LOOPS"), crash: null };
  const c = raw.indexOf(" CRASH ");
  if (c >= 0) {
    const t = raw.slice(c);
    out.crash = { vec: number(t, "VEC", 16), pc: number(t, "PC", 16), rets: number(t, "RETS", 16), dbg: number(t, "DBG", 16), emu: number(t, "EMU", 16), clockMs: number(t, "UP") };
    // the Bluetooth audio image from test build 14 (firmware/ble/ble_diag.h, openpocket.c op_c1x): the second core's fault record as
    // it stood at that crash: its faults counted, the core that took the last (1 CPU1 itself, 0 CPU0), its ms since power-on, its PC
    const m = / C1F (\d+)\/(\d+) (\d+) ([0-9A-F]+)\b/.exec(t);
    if (m) out.crash.c1 = { count: +m[1], cpu: +m[2], ms: +m[3], pc: parseInt(m[4], 16) };
  }
  return out;
}

const hex = (v) => (v ?? 0).toString(16).toUpperCase().padStart(8, "0");

function lastBootWords(reading) {
  const l = reading.lastBoot;
  // (the Bluetooth audio image's status lines, "BT ..." and its stall record, name its own folder; the BLE image's are "OFF" etc.)
  const dir = ["bt-audio", "stall-record"].includes(reading.ble?.state) ? "build-bt" : "build-ble";
  if (l.crash) {
    const c1 = l.crash.c1;
    const second = !c1 ? "" : c1.count ? `; the second core had faulted ${c1.count} time${c1.count > 1 ? "s" : ""}, the last taken by CPU${c1.cpu} at ${c1.ms} ms (PC ${hex(c1.pc)})${Math.abs(c1.ms - l.crash.clockMs) <= 10 ? ": the same moment as this crash" : ""}`
      : "; the second core had not faulted";
    return `it crashed at PC ${hex(l.crash.pc)} (RETS ${hex(l.crash.rets)}, vector ${l.crash.vec.toString(16).toUpperCase()}, pedal clock ${l.crash.clockMs} ms; look the addresses up in firmware/${dir}/symbols.txt)${second}`;
  }
  return `it stopped at ${l.stageName}, reset by ${reading.boot.resetBy.join(" + ") || "nothing recorded"}`;
}

// What cannot be read at each level, and what is left: the verdict's reason says it in words.
const SCREEN = "Look at the pedal's screen or ask for a photo: nothing else can show";
const NOT_READ = {
  none: `No pedal-like MIDI port. The pedal is unplugged, off, on the red CRASH screen, showing the 12 s LAST START FAILED report (USB starts after it), or in its boot loader. ${SCREEN} the crash's PC, RETS, vector and DBG; they can be read here only after the next boot, in safe mode (the Bluetooth audio image's R1, R6, R12 and SP: only on its LAST START FAILED report).`,
  silent: `A pedal-like MIDI port is there but nothing answers: the main loop is stuck or the firmware failed before USB service. ${SCREEN} what it is doing. If the pedal then resets by itself, read again; a pedal stuck like this needs a power cycle or the UBOOT rescue.`,
};

// { result: pass | running | fail | pending | unknown, reason } for the readings so far and a scan, if one ran
export function judge(readings, scan) {
  const now = readings[readings.length - 1];
  if (now.level === "none" || now.level === "silent") return { result: "unknown", reason: NOT_READ[now.level] };
  if (now.level === "identity") {
    return { result: "unknown", reason: IS_OTA(now.id)
      ? `The pedal is in update mode (${now.id.text}), not running firmware. Finish or restart the update; there is no status to read.`
      : `${now.id.text} answers only the identity query: it is not OpenPocket firmware. There is no status to read.` };
  }
  if (now.level === "info") {
    return { result: "unknown", reason: `${now.info.identity} ${now.info.version} runs and answers MIDI (DSP load ${now.ping.load}%), but it has no log channel: only the Bluetooth test image built with DIAG has one. Bluetooth state, counters and last-boot report cannot be read; use the screen.` };
  }
  const { boot, lastBoot, ble } = now;
  if (boot.safe) return { result: "fail", reason: `The last boot failed: ${lastBootWords(now)}. This boot is safe mode, so Bluetooth stayed off; the boot after this one tries again.` };
  if (ble.state === "failed") {
    const what = FAIL_STEPS[ble.step] ?? `step ${ble.step}`;
    return { result: "fail", reason: `Bluetooth start-up failed: ${what} (code ${ble.code}${ble.stage ? `, stage ${ble.stage}` : ""}${ble.wait ? `, controller waiting on ${ble.wait}` : ""}).` };
  }
  if (ble.state === "starting" || boot.stage !== STAGE_OK) return { result: "pending", reason: `Still starting: boot stage ${boot.stageName}, Bluetooth ${ble.raw}.` };
  if (ble.state === "stall-record") return { result: "running", reason: "Bluetooth is off this boot; the bluetooth, link and trace lines show the record of an earlier stall (docs/BT-AUDIO-TEST.md, \"If it stalls\")." };
  if (ble.state === "bt-audio") {
    if (/^BT OFF/.test(ble.raw)) return { result: "running", reason: /REV HELD/.test(ble.raw) ? "Bluetooth is off this boot (REV held at power-on): the boot without the radio, to compare the sound." : "Bluetooth is off." };
    const linked = /^BT (LINK|PLAY) n[1-9]/.test(now.link || "");
    return { result: "running", reason: `The Bluetooth audio image runs (${linked ? "a phone is linked" : "visible, no phone linked"}). Its lines are read by hand: docs/BT-AUDIO-TEST.md, "What you should see".` };
  }
  if (ble.state !== "advertising") return { result: "fail", reason: ble.state === "off" ? "Bluetooth is off although the boot was normal." : `The status line is not one this tool knows: ${ble.raw}` };
  if (ble.BEN !== null && !(ble.BEN & 1)) return { result: "fail", reason: "The controller says it advertises, but the BLE block is off (BEN bit 0 clear)." };

  const first = readings.find((r) => r.level === "full" && r.ble.state === "advertising");
  if (first === now) return { result: "pending", reason: "One reading only: the counters must be seen rising in a second reading." };
  if (ble.i <= first.ble.i) {
    return { result: "fail", reason: ble.IC45 === 0 ? "No baseband events: interrupt 45 was never enabled (IC45 0)."
      : ble.IEN === 0 ? "No baseband events: interrupt 45 is on, but the baseband never enabled its event (IEN 0)."
        : "No baseband events: the counter i does not rise, so the radio is not running advertising events." };
  }
  const seconds = (Date.parse(now.time) - Date.parse(first.time)) / 1000;   // this computer's clock: the pedal's wraps
  const rate = `Baseband events run at ${((ble.i - first.ble.i) / seconds).toFixed(1)} a second (the advertising interval gives about 10)`;
  const notes = [];
  if (ble.t <= first.ble.t) notes.push("The library's software timers did not fire (t stands still).");
  if (ble.s > 0) notes.push(`The link scheduler asked for a slot timer this image lacks (s ${ble.s}).`);
  if (ble.vm && ble.vm.found !== 7) notes.push(`Not all stored radio records were found (V ${ble.vm.found.toString(16)}, 7 expected): the radio has zeros for the missing trims.`);
  const judged = (result, reason) => ({ result, reason: [reason, ...notes].join(" "), notes });
  if (!scan) return judged("running", `${rate}. That is not proof of packets on the air: add --scan.`);
  if (!scan.ok) return judged("unknown", `${rate}, but the scanner could not run: ${scan.why}.`);
  if (scan.hits) return judged("pass", `This Mac saw the advertisement ${scan.hits} times: name "${scan.name}", RSSI ${scan.rssi} dBm, connectable ${scan.connectable}.`);
  return judged("fail", `${rate} and the controller reports advertising, but this Mac saw no advertisement in ${scan.seconds} s (${scan.others} other devices seen, so the scanner works).`);
}

// the a2dp line (the Bluetooth audio image, test build 12 only: its firmware/bt/bt_a2dp.c bt_a2): "A2" and 23 words in hex, named here. Counts
// in decimal; the states (a connection's A2DP fields in one word, test build 12's bt_lmp_st), the handle:CID, the get's result and the code address
// in hex; W0 is a time << 1 | the connection's slot, DROP the full | too-big counts
const A2_NAMES = ["n", "kept", "hc", "B", "Bn", "Bst", "Bt", "O0", "Ot", "CHM", "G", "Gt", "F", "Ft", "N", "RC", "H", "W", "W0", "W1", "W2", "DROP", "DUP"];
const A2_HEX = new Set(["Bst", "O0", "G", "F", "N", "RC", "W1", "W2"]);
export function a2dpWords(line) {
  const w = line.trim().split(/\s+/).slice(1);
  if (w.length !== A2_NAMES.length || !w.every((x) => /^[0-9A-F]{8}$/i.test(x))) return null;
  return A2_NAMES.map((k, i) => {
    const v = parseInt(w[i], 16) >>> 0;
    const t = A2_HEX.has(k) ? w[i] : k === "hc" ? `${(v >>> 16).toString(16).padStart(4, "0")}:${(v & 0xffff).toString(16).padStart(4, "0")}`
      : k === "W0" ? `${v >>> 1}ms/${v & 1}` : k === "DROP" ? `${v & 0xffff}/${v >>> 16}` : String(v);
    return `${k} ${t}`;
  }).join(", ");
}

// line 6 from test build 13 (firmware/bt/bt_a2dp.c bt_fifo_line): "FIFO" and 4 words in hex, the music path's counts since boot:
// the packets the FIFO took, the ones it dropped (full | too big << 16), the library's packets given back undecoded, the heals
export function fifoWords(line) {
  const w = line.trim().split(/\s+/);
  if (w[0] !== "FIFO" || w.length !== 5 || !w.slice(1).every((x) => /^[0-9A-F]{8}$/i.test(x))) return null;
  const [taken, drop, dup, heal] = w.slice(1).map((x) => parseInt(x, 16) >>> 0);
  return `taken ${taken}, dropped ${drop & 0xffff} full / ${drop >>> 16} too big, given back ${dup}, heals ${heal}`;
}

// the isr line's VM (firmware/bt/bt_a2dp.c bt_isr_line): the pairing records the library read and wrote; from test build 14
// "VM r/w/s/f/q" (firmware/bt/bt_ps.h), also the copies saved to the flash since power-on, the save rounds that failed (the
// first two tried again 1 s later, then only a new change tries) and the flash copy's sequence (at power-on the copy loaded, 0
// none; one more for each slot a save tries). q 0 with f 3: the slots read otherwise at power-on, the flash left alone
export function vmWords(isr) {
  const m = / VM (\d+)\/(\d+)(?:\/(\d+)\/(\d+)\/(\d+))?(?= |$)/.exec(isr ?? "");
  if (!m) return null;
  const [reads, writes] = [+m[1], +m[2]], lib = `pairing records: the library read ${reads} and wrote ${writes}`;
  if (m[3] === undefined) return { reads, writes, text: `${lib}, in RAM only (up to test build 13: gone at power-off)` };
  const [saves, failed, seq] = [+m[3], +m[4], +m[5]];
  const kept = seq === 0 && failed >= 3 ? "the flash copy left alone this session (its slots read otherwise at power-on): the records in RAM only"
    : seq === 0 ? "no copy in the flash yet (none at power-on, none saved since): a pairing is kept once saved"
    : saves === 0 ? `a copy loaded from the flash at power-on (sequence ${seq}), none saved since`
    : `${saves} saved to the flash since power-on (sequence now ${seq})`;
  const fails = seq === 0 || !failed ? "" : failed >= 3 ? `; ${failed} save rounds failed: tried again only at a new change`
    : `; ${failed} save round${failed > 1 ? "s" : ""} failed (each tried again 1 s later)`;
  return { reads, writes, saves, failed, seq, text: `${lib}; ${kept}${fails}` };
}

// the c1 line's W (test build 13: engine/px_dsp.c PX_LOOP_CAP, firmware/bt/bt_a2dp.c bt_c1_line): the reverb loop's cuts at
// its block's end, and after one, n (as the loop reloaded it) and k at the latest cut, the time the main loop first saw a cut
// (ms since power-on, wrapping every 179 s) and its S word then (stage << 28 | state << 24 | DSP held << 23 | 1 << 22 | IRQ 40's
// count). The cut is taken only when k >= 64, and it records that k. In test build 13's compiled code (CI run 43039170246,
// listings/px_dsp.s) the loop's end test is k < n, n reloaded from its stack word every sample (build 12's was k == n): a k that
// gets past n ends the loop, uncounted, so a cut means n was read too large. The reading follows docs/BT-AUDIO-TEST.md, "Test
// build 13" (the lanes' chunks are 16 frames, the chain's calls 64). Test build 14: "W cuts/id", the latest cut's loop
// (engine/px_dsp.h PX_WILD_*: 1 the reverb's, 2 the delay's, both cut as above; 3 a lane's frame count read over 128 and 4 the
// lanes' buffer index read other than 0 or 1, both held to their range by px_wet_run: there n is the value read, k the lane)
export const W_LOOPS = { 1: "the reverb's loop", 2: "the delay's loop", 3: "a wet lane's frame count", 4: "the wet lanes' buffer index" };
export function wWords(c1) {
  const m = / W (\d+)(?:\/(\d+))?((?: [0-9A-F]{8}){4})?(?= |$)/i.exec(c1 || "");
  if (!m) return null;
  const cuts = +m[1], id = m[2] === undefined ? null : +m[2];
  if (!cuts) return { cuts, text: `W 0: no cut of ${/ P [0-9A-F]{2}\//.test(c1) ? "the reverb's or the delay's loop, and no lane count or buffer index out of range," : "the reverb's loop"} counted in this boot (a k that got past n ends the loop uncounted in this build, and an n written over with a value up to 64 still ends it late, uncounted)` };
  if (!m[3]) return null;                              // a cut, but its words are missing: damaged
  const [n, k, t, s] = m[3].trim().split(" ").map((x) => parseInt(x, 16) >>> 0);
  const loop = id === null ? "the reverb's loop" : W_LOOPS[id] ?? `loop ${id} (unknown)`;
  const reading = id === 3 ? `a wet lane's frame count was read as ${n} (128 at most) for lane ${k} (0 the delay, 1 the reverb): held to 128, a click at most; a wrong read (B) or the count's word written over (A)`
    : id === 4 ? `the lanes' buffer index was read as ${n} (0 or 1): held to its range; a wrong read (B) or the index's byte written over (A)`
      : k > 64 ? `k jumped forward while n was read too large (its stack word written or misread, or its register clobbered)${(k >>> 16) === 0x20 ? "; 0x0020xxxx is the misread signature" : ""}`
        : k < 64 ? "a k below 64 cannot pass the cut's check: the check itself misfired once on that core (B), or k's register was clobbered"
          : n === 16 ? "n was read too large once and was 16 again at the cut: a one-off wrong read of n on that core (B)"
            : n === 64 ? "the chain's call (n 64), not a lane's chunk: it read its n too large once (B)"
              : "n's stack word was overwritten (A): n is the writer's fingerprint";
  const at = s >>> 22 & 1 ? `the main loop first saw it at ${t} ms (the pedal clock wraps every 179 s): stage ${s >>> 28}, state ${s >>> 24 & 15}, DSP held ${s >>> 23 & 1}, IRQ 40 ${s & 0x3fffff} (a cut during the radio's start-up replay, stage 3, is seen only once the replay has returned)`
    : "first cut not yet latched by the main loop";
  return { cuts, id, loop, n, k, t, s, reading, text: `W ${cuts} cut${cuts > 1 ? "s" : ""}: n ${n} (0x${n.toString(16).toUpperCase()}), k ${k} (0x${k.toString(16).toUpperCase()}), the latest in ${loop}; ${reading}; ${at}` };
}

// the c1 line's fields from test build 14 (firmware/bt/bt_a2dp.c bt_c1_line), named: KEY, the key held at power-on; P, the core
// supplies read back (P3_ANA_CON6 / CON9); V, when Bluetooth first became VISIBLE and D, when the DSP came on (ms since power-on,
// 0 = not yet); SE, the scan enables and the last one's time; E, CPU0's exceptions resumed as not its own (fm1_cpu1.h
// fm1_cpu1_echo: the echo of CPU1's fault / the late flag of a hold), after one the PC CPU0 was at; CN, the canary after the Bluetooth heap (words found
// changed, the first one's value); after a fault of the second core (F count/cpu), its record's 9 words and X, the debug unit's
// ten words. null for an earlier build's c1 line (no P)
export const KEYS = ["none: two cores, nothing of the second core's in the radio's memory, the stock power setup from power-on as M-VAVE's V22, the last phone called back, the second core started again after a fault, its per-block tables in RAM, LOW OCTAVE and SYN PAD, BLE MIDI beside the music, the music's queue doubled and decoded while the screen draws, the delay up to 780 ms, tap tempo on CC 116, the expression pedal on CC 117, the radio served while a save shows SAVED, the web editor over Bluetooth MIDI on a second LE link beside the footswitch, the pedal as the central of a footswitch that is only a peripheral (the regular Chocolate), an LE watchdog, 4 factory NAM models (test build 49: 48 with scenes (four a preset, the blocks' on and off, CC 65 to 68 or a learned switch), CAB held turning PLAY's view round (CHAIN, SWITCHES, SCENES), the screen's fonts without lowercase; test build 48: 47 with the guided learn (PEDAL or SWITCH, the controller, its heel and toe on a page of their own, the pedal's own travel), PRESET LVL down to -INF; test build 47: 46 with a learned pedal's heel and toe, SETTINGS' PLAY VIEW (the switches' blocks big on PLAY), GUITAR TONE by its sound, the footswitches through SETTINGS, 12-STRING steady; test build 46: 45 with controls learned live on a block page, each a preset's own (hold FX); test build 45: 44 with MONO SYNTH's tracker through hum and noise, TAPE FLUTE on the semitone, WHAMMY from the note to SHIFT's octaves, SFX's WAH on the expression pedal; test build 44: 43 with SHIMMER's octave in tune, BARITONE sooner, MONO SYNTH's YIN tracker, SITAR and STRINGS rebuilt, SYN's TAPE FLUTE; test build 43: 42 with USB audio, the pedal a USB sound card, and SETTINGS' USB SEND, USB RETURN and GUITAR (the computer's sound back into the chain before any block); test build 42: 41 with LOW OCTAVE's octave out of the chain (after the reverb) and the stereo output (SETTINGS' OUTPUT: MONO, STEREO, GTR+BASS); 41: 40 with the Chocolate by USB cable (the pedal its USB MIDI host, SETTINGS' USB PORT); 40: 38 with controls learned on the pedal (editor protocol 5), SWITCH, INPUT, TAILS and BT POWER on the page FX opens (SETTINGS), CC 118 / 119, NAM_RENAME and DRV's TREBLE BOOST (39 never released); 38: 37 with the library's timers safe in interrupts and its usr timeouts run every millisecond by the 10 kHz tick; 37: 36 with the LE transmit pool 8 buffers and the editor 2 packets out at most (36 crashed on a full pool); 36: 35 with 6 s supervision timeouts, the footswitch's and (asked) a phone's, a phone's link asked for 15-30 ms, the footswitch's at 28.75 ms; 35: 34 with the footswitch's requests for a faster link refused (it stays at 30 ms) and the LE receive pool 8 buffers, room for advertising beside its link; 34: 33 with the footswitch's link left alone while it is made (no advertising until it is subscribed, the last phone called back once it is linked or at 20 s), the central role woken without a phone, its own link ends in the log; 33: 32 with the footswitch by its name alone, without the MIDI UUID, no scanning beside a phone's link, the GATT server's words and the filter's view in the log, the central role on a twelfth line; 32: 31 with the ATT client only with the central role on and the other links' packets to the server, encryption and report counters; 31: the footswitch's scan in turn with advertising, alone on the radio; 30: 28 with the LE interrupts known as interrupts, cpu_in_irq; 29: 28 with 27's LE roles and no central role; 28 hung at its first LE scan; 27 without the central role, with 12 models; 26 without the editor over Bluetooth, one LE link; 25 stopped the music 0.7 s at a save; 24 without the pedal; 23: the queue half, the delay 2 s; 22 without BLE MIDI)",
  "CAB: one core, the second held (test builds 16-42; 15: the boot loader's supplies; 14: the stock ones)",
  "DLY: no central role: no scanning for the footswitch, LE as in test build 27 (test build 28; 18-27: without the rest of the stock power setup; 16-17: the radio's interrupts below the audio)", "REV: Bluetooth off"];
const F_NAMES = ["DBG", "EMU", "PC", "RETS", "EMU1", "ms", "job handed", "job finished", "fn"];
export function c1Words(c1) {
  const m = / KEY (\d+) P ([0-9A-F]{2})\/([0-9A-F]{2}) V (\d+) D (\d+) SE (\d+)@(\d+) E (\d+)\/(\d+)(?: ([0-9A-F]{8}))? CN (\d+)(?: ([0-9A-F]{8}))? S ([0-9A-F]{8}) F (\d+)\/(\d+)\b/i.exec(c1 || "");
  if (!m) return null;
  const [key, con6, con9, v, d, se, seMs, echo, late, cn] = [+m[1], parseInt(m[2], 16), parseInt(m[3], 16), +m[4], +m[5], +m[6], +m[7], +m[8], +m[9], +m[11]];
  const epc = m[10] ? parseInt(m[10], 16) >>> 0 : null, cnFirst = m[12] ? parseInt(m[12], 16) >>> 0 : null, faults = +m[14], cpu = +m[15];
  const vdc14 = con6 & 7, sysvdd = con9 & 15, strong = (con9 >> 4) & 1;
  const parts = [`KEY ${key}: ${KEYS[key] ?? "unknown"}`,
    `P: VDC14 ${vdc14} (${(1.25 + 0.05 * vdc14).toFixed(2)} V), SYSVDD ${sysvdd} (${(0.93 + 0.03 * sysvdd).toFixed(2)} V)${strong ? " with its strong drive" : ""}${vdc14 === 7 && sysvdd === 15 ? ", the stock firmware's" : vdc14 === 3 && sysvdd === 11 ? ", the boot loader's" : ""}`,
    `V ${v ? `${v} ms` : "never"}: Bluetooth VISIBLE`, `D ${d ? `${d} ms` : "not yet"}: the DSP on${v && d ? ` (${d - v >= 0 ? `${d - v} ms after VISIBLE` : `${v - d} ms before VISIBLE`})` : ""}`,
    `SE ${se} scan enable${se === 1 ? "" : "s"}${se ? `, the last at ${seMs} ms` : ""}`,
    `E ${echo}/${late}: CPU0's exceptions resumed, ${echo} echo${echo === 1 ? "" : "es"} of the second core's fault, ${late} late flag${late === 1 ? "" : "s"} of holding it${epc !== null ? ` (the latest with CPU0 at PC ${hex(epc)})` : ""}`,
    `CN ${cn}: ${cn ? `the canary after the Bluetooth heap changed (${cn} word${cn > 1 ? "s" : ""}; the first read ${hex(cnFirst)})` : "the canary after the Bluetooth heap intact"}`];
  let record = null, x = null;
  if (faults) {
    let after = c1.slice(m.index + m[0].length);
    const wm = /^ W (\d+)(?:\/\d+)?/.exec(after);    // (W's 4 words only after a cut: then the record's)
    if (wm) after = after.slice(wm[0].length).replace(+wm[1] ? /^(?: [0-9A-F]{8}){4}/i : /^$/, "");
    const words = (after.match(/^((?: [0-9A-F]{8}){1,9})/i)?.[1] ?? "").trim().split(" ").filter(Boolean).map((w) => parseInt(w, 16) >>> 0);
    record = Object.fromEntries(words.map((w, i) => [F_NAMES[i], w]));
    const xm = / X((?: [0-9A-F]{1,8})+)/i.exec(after);
    const cut = c1.length >= 223 && !c1.endsWith(" ");  // (the line's end may be a word cut short: dropped)
    if (xm) x = xm[1].trim().split(" ").slice(0, cut ? -1 : 10).map((w) => parseInt(w, 16) >>> 0);
    parts.push(`F ${faults}/${cpu}: the second core faulted ${faults} time${faults > 1 ? "s" : ""}, the last taken by CPU${cpu}${record.ms !== undefined ? ` at ${record.ms} ms` : ""}${record.PC !== undefined ? `, PC ${hex(record.PC)}` : ""}${record.DBG !== undefined ? `, DBG ${hex(record.DBG)}` : ""}`
      + `${words.length < 9 ? ` (${9 - words.length} of its words cut off: the line holds 223 characters)` : ""}`
      + `${x ? `; debug unit ${x.map((w) => w.toString(16).toUpperCase()).join(" ")}${x.length < 10 ? `${x.length ? " " : ""}(the rest cut off at 223 characters)` : ""}` : ""}`);
  }
  return { key, con6, con9, vdc14, sysvdd, strong, v, d, se, seMs, echo, late, epc, cn, cnFirst, faults, cpu, record, x, text: parts.join("; ") };
}

// the DIAG lines, by position, as a reading's fields. The Bluetooth audio image: hci, its HCI line; line 6 the first HCI messages
// (hci0, test builds 9-11), the phone's packets at the controller (a2dp, test build 12: "A2 ...") or the music's FIFO (fifo, from
// test build 13: "FIFO ..."); isr, from test build 11: the interrupts' times against the audio's, and the DSP; c1, from test build
// 12: the second core and its fault record, from test build 13 also W, the reverb loop's cuts
// (test build 19: c1r, the tenth line, "C1R RV n@ms ...": the second core's revivals and its last fault's registers; test build
// 28: le, the eleventh, "LE ...": the LE links and the editor; test build 33: cen, the twelfth, "CEN ...": the central role for the
// footswitch, until test build 32 the end of the LE line)
export function diagFields([boot, last, ble, link, trace, hci, l6, isr, c1, c1r, le, cen]) {
  const a2dp = l6 && l6.startsWith("A2") ? l6 : undefined, fifo = l6 && l6.startsWith("FIFO") ? l6 : undefined;
  const hci0 = l6 && !a2dp && !fifo ? l6 : undefined;
  return { boot: parseBoot(boot), lastBoot: parseLast(last), ble: parseBle(ble), link, trace, hci, hci0, a2dp, fifo, isr, c1,
           c1r: c1r && c1r.startsWith("C1R") ? c1r : undefined, le: le && le.startsWith("LE ") ? le : undefined,   // (le: test build 28)
           cen: cen && cen.startsWith("CEN ") ? cen : undefined };   // (cen: test build 33)
}
// the c1 regs line in words (test build 19, firmware/bt/bt_a2dp.c bt_c1r_line)
export function c1rWords(c1r) {
  const m = /^C1R RV (\d+)@(\d+)(?: DBG ([0-9A-F]{8})@(\d+) PC ([0-9A-F]{8}) AX ([0-9A-F]+)\/([0-9A-F]+))?/i.exec(c1r || "");
  if (!m) return null;
  const rv = +m[1], parts = [rv ? `the second core was started again ${rv} time${rv > 1 ? "s" : ""} after a fault, the last at ${m[2]} ms` : "the second core was not started again"];
  if (m[3]) {
    const who = { "140": "CPU1", "100": "CPU0", "3FF": "none" };
    parts.push(`its last fault: DBG ${m[3]} at ${m[4]} ms, PC ${m[5]}, the invalid AXI read by ${who[m[6].toUpperCase()] ?? m[6]}, write by ${who[m[7].toUpperCase()] ?? m[7]}`);
  }
  return parts.join("; ");
}

// one reading, at the deepest level the pedal answers
async function read(access, timing) {
  const time = new Date().toISOString();
  const ports = [...access.inputs.values()].filter((p) => p.state !== "disconnected" && PORT_RE.test(p.name || ""));
  if (!ports.length) return { time, level: "none" };
  const ed = new Editor(access);
  const info = await ed.connect();
  if (!info) {
    const dev = await new Updater(access, null, timing).identify();
    return dev ? { time, level: "identity", id: dev.id, port: dev.name } : { time, level: "silent", port: ports[0].name };
  }
  try {
    return { time, level: "full", info, ...diagFields(await ed.diag()) };
  } catch (e) {
    if (e.code !== "timeout") throw e;
    const r = await ed.request(C.PING).catch(() => null);
    if (!r) return { time, level: "silent", port: info.port };      // it answered INFO, then went away: restarting
    return { time, level: "info", info, ping: { load: r[0], bypass: !!(r[1] & 1), secondCore: !!(r[1] & 2) } };
  } finally { ed.close(); }
}

const started = (r) => r.level === "info" || r.level === "identity" || (r.level === "full" && (r.boot.safe || (r.boot.stage === STAGE_OK && r.ble.state !== "starting")));

export function show(r, prev, say) {
  if (r.level !== "full") {
    say(r.level === "info" ? `pedal      ${r.info.identity} ${r.info.version} on "${r.info.port}": answers MIDI, no log channel`
      : r.level === "identity" ? `pedal      ${r.id.text} on "${r.port}": identity only` : r.level === "silent" ? `pedal      "${r.port}" is there, nothing answers` : "pedal      no MIDI port");
    return;
  }
  const { boot, lastBoot: l, ble } = r;
  say(`pedal      ${r.info.identity} ${r.info.version} on "${r.info.port}"`);
  say(`this boot  pedal clock ${(boot.clockMs / 1000).toFixed(1)} s (wraps every 179 s), stage ${boot.stageName}, ${boot.safe ? "SAFE MODE" : "normal"}, reset by ${boot.resetBy.join(" + ") || "?"}, failed boots counted ${boot.failedBoots}`);
  say(`last boot  ${l.failed ? `FAILED: ${lastBootWords(r)}; blocks ${l.blocks}, load ${l.load}%, peak ${l.peak}%, NaN ${l.nans}, CPU1 ${l.cpu1}, loops ${l.loops}` : "finished normally"}`);
  say(`bluetooth  ${ble.raw}`);
  if (r.link) say(`link       ${r.link}`);
  if (r.le) say(`le         ${r.le}`);   // (test build 28: the LE links, the editor; up to 32 the central role for the footswitch)
  if (r.cen) say(`cen        ${r.cen}`);   // (test build 33: the central role for the footswitch)
  if (r.trace) say(`trace      ${r.trace}`);
  if (r.hci) say(`hci        ${r.hci}`);
  if (r.hci0) say(`hci0       ${r.hci0}`);
  if (r.a2dp) {
    say(`a2dp       ${r.a2dp}`);
    const named = a2dpWords(r.a2dp);
    if (named) say(`           ${named}`);
  }
  if (r.fifo) {
    say(`fifo       ${r.fifo}`);
    const named = fifoWords(r.fifo);
    if (named) say(`           ${named}`);
  }
  if (r.isr) {
    say(`isr        ${r.isr}`);
    const vm = vmWords(r.isr);
    if (vm) say(`           ${vm.text}`);
  }
  if (r.c1r) {
    say(`c1 regs    ${r.c1r}`);
    const w = c1rWords(r.c1r);
    if (w) say(`           ${w}`);
  }
  if (r.c1) {
    say(`c1         ${r.c1}`);
    const f = c1Words(r.c1);
    if (f) say(`           ${f.text}`);
    const w = wWords(r.c1);
    // (in safe mode the DSP stays off, and the crashed boot's W went with its RAM: W 0 says nothing about that boot)
    if (w) say(`           ${boot.safe && !w.cuts ? "W not measured: safe mode, the DSP is off this boot (the crashed boot's W was cleared by the restart)" : w.text}`);
  }
  if (ble.state !== "advertising") return;
  const before = prev?.level === "full" && prev.ble.state === "advertising" ? prev.ble : null;
  for (const k of ["i", "e", "r", "t", "s"]) say(`  ${k.padEnd(5)}${String(ble[k]).padEnd(10)}${(before ? `+${ble[k] - before[k]}` : "").padEnd(9)}${MEANING[k]}`);
  if (ble.vm) say(`  ${"V".padEnd(5)}${`${ble.vm.found.toString(16)} ${ble.vm.area.toString(16)} ${ble.vm.trim.toString(16)}`.toUpperCase().padEnd(19)}${MEANING.vm}`);
  for (const k of ["CON", "IEN", "IPND", "STAT", "IC45", "IC29", "BEN"]) say(`  ${k.padEnd(5)}${(ble[k] ?? 0).toString(16).toUpperCase().padEnd(19)}${MEANING[k]}`);
}

function compileScanner() {
  const bin = resolve(ROOT, "build/ble-scan"), src = resolve(ROOT, "tools/ble-scan.swift"), plist = `${bin}.plist`;
  if (existsSync(bin) && statSync(bin).mtimeMs > statSync(src).mtimeMs) return bin;
  mkdirSync(dirname(bin), { recursive: true });
  writeFileSync(plist, `<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.openpocket.ble-scan</string><key>NSBluetoothAlwaysUsageDescription</key><string>Listens for the OpenPocket test advertisement.</string></dict></plist>`);
  const r = spawnSync("swiftc", ["-O", src, "-o", bin, "-Xlinker", "-sectcreate", "-Xlinker", "__TEXT", "-Xlinker", "__info_plist", "-Xlinker", plist], { encoding: "utf8" });
  if (r.error || r.status !== 0) throw new Error(`swiftc failed: ${r.error?.message || r.stderr}`);
  return bin;
}

function scanPass(bin, seconds, uuid) {
  return new Promise((done) => {
    const child = spawn(bin, [String(seconds), ...(uuid ? [uuid] : [])], { stdio: ["ignore", "pipe", "ignore"] });
    const killer = setTimeout(() => child.kill(), (seconds + 15) * 1000);
    let out = "";
    child.stdout.on("data", (d) => { out += d; });
    child.on("close", () => {
      clearTimeout(killer);
      const lines = out.split("\n").filter(Boolean).map((l) => JSON.parse(l));
      done({ hits: lines.filter((l) => !l.done), end: lines.find((l) => l.done) });
    });
  });
}

// the service-UUID pass, then the open pass, or why they could not run
export async function macScan(seconds) {
  if (process.platform !== "darwin") return { ok: false, why: "the scanner is for macOS only" };
  let bin;
  try { bin = compileScanner(); } catch (e) { return { ok: false, why: e.message }; }
  const passes = [await scanPass(bin, seconds, BLE_MIDI), await scanPass(bin, seconds, null)];
  const end = passes.map((p) => p.end).find((e) => e && e.state !== 5) || passes[1].end;
  if (!end) return { ok: false, why: "the helper stopped without a result" };
  if (end.state !== 5) return { ok: false, why: `the Bluetooth adapter is not usable (state ${end.state}, authorization ${end.auth}; 5 and 3 are needed)` };
  const hits = passes.flatMap((p) => p.hits);
  const named = hits.find((h) => h.name) || hits[0];
  return { ok: true, seconds: seconds * 2, hits: hits.length, others: passes[1].end.others, name: named?.name ?? null, id: named?.id ?? null,
    rssi: hits.length ? Math.max(...hits.map((h) => h.rssi)) : null, connectable: named?.connectable ?? null, services: named?.services ?? [] };
}

function parse(argv) {
  const a = { follow: false, json: false, wait: 0, interval: 2, count: 2, scan: 0 };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--follow") a.follow = true;
    else if (v === "--json") a.json = true;
    else if (["--wait", "--interval", "--count", "--scan"].includes(v)) {
      const n = Number(argv[++i]);
      if (!(n >= 0)) throw new Error(`${v} needs a number`);
      a[v.slice(2)] = n;
    } else throw new Error(`unknown argument ${v}`);
  }
  if (a.follow && a.scan) throw new Error("--scan needs an end: leave out --follow");
  return a;
}

const EXIT = { pass: 0, running: 0, fail: 1, pending: 3, unknown: 3 };

// main(argv, { access, say, scan, timing }): the exit code. access defaults to the real USB MIDI ports.
export async function main(argv, { access, say = console.log, scan = macScan, timing } = {}) {
  let args, node = null;
  try { args = parse(argv); } catch (e) { say(`${e.message}\n${USAGE}`); return 2; }
  const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
  try {
    if (!access) {
      const midi = await import("@julusian/midi").then((m) => m.default || m, () => null);
      if (!midi) { say("the MIDI library is not installed: run `npm ci --prefix tools` once (docs/BUILDING.md)."); return 2; }
      node = (await import("./midi-node.mjs")).nodeMidiAccess(midi);
      access = node;
    }
    const readings = [];
    const end = Date.now() + args.wait * 1000;
    let r = await read(access, timing);
    while (!started(r) && Date.now() < end) { await sleep(Math.min(1, args.interval || 1)); r = await read(access, timing); }
    let verdict;
    for (let n = 0; ; n++) {
      readings.push(r);
      verdict = judge(readings);
      if (args.json) { if (!args.scan) say(JSON.stringify({ ...r, verdict })); } else { show(r, readings[readings.length - 2], say); say(`verdict    ${verdict.result.toUpperCase()}: ${verdict.reason}`); }
      if (!args.follow && n + 1 >= args.count) break;
      await sleep(args.interval);
      r = await read(access, timing);
    }
    if (args.scan) {
      if (!args.json) say(`scanning   ${args.scan} s on the BLE-MIDI service UUID, then ${args.scan} s on everything (never connects)`);
      const result = await scan(args.scan);
      readings.push(await read(access, timing));
      verdict = judge(readings, result);
      if (args.json) say(JSON.stringify({ ...readings[readings.length - 1], scan: result, verdict }));
      else { show(readings[readings.length - 1], readings[readings.length - 2], say); say(`verdict    ${verdict.result.toUpperCase()}: ${verdict.reason}`); }
    }
    return EXIT[verdict.result];
  } finally {
    if (node) node.close();
  }
}
