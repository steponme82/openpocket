// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — end-to-end test of the web editor's protocol (docs/pxedit.js) against a simulated pedal (tests/edsim.c)
// that runs the pedal's real editor code (firmware/px_editor.h), preset store (px_store.h) and engine catalogue.
//   cc -O1 -o build/edsim tests/edsim.c engine/px_dsp.c -lm && node tests/test_editor.mjs
// Over Bluetooth MIDI (the Bluetooth audio image's editor, test build 27):
//   cc -O1 -DED_BT=1 -o build/edsim_bt tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm && EDSIM_BLE=1 node tests/test_editor.mjs
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { Editor, presetToJSON, presetFromJSON, filePresets, parsePresetFile, clonePreset } from "../docs/pxedit.js";
import { parseNam, levelTrim, packModel, readDi, a2Run, KERNELS, DILATIONS, NW } from "../docs/pxnam.js";
import { parseWav, prepareIr, packIr, irName, resample, IR_LEN } from "../docs/pxir.js";
import { pedalLearn, switchMode, MAP_EACH, MAP_VALUE, PEDAL_MAP_N, CC_NEXT, MAP_PRESS, ccParam, presetEntry, PRESET_MAP_N, CC_SCENE0 } from "../docs/pxmidi.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
// EDSIM_BLE=1: the same tests over BLE MIDI, as the Bluetooth audio image (test build 27) talks to the editor, against
// build/edsim_bt (tests/edsim.c -DED_BT=1). The editor's SysEx is cut into writes as a central cuts them (CoreMIDI, Android:
// the header, a timestamp before each status byte, the data without), at the write sizes centrals use and some smaller ones;
// the pedal's notifications are read as Android's BluetoothPacketDecoder reads them, each checked: a header first, 20 bytes
// at most.
const BLE = !!process.env.EDSIM_BLE;
const FX = !!process.env.EDSIM_FX;           // the Bluetooth audio image's (tests/edsim.c -DPX_OWN_FX=1): protocol 5, test build 39
const SIM = process.env.EDSIM || join(ROOT, "build", BLE ? "edsim_bt" : "edsim");
const TMP = mkdtempSync(join(tmpdir(), "opedit-"));
globalThis.localStorage = { getItem: () => null, setItem: () => {} };
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(72)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// BLE MIDI: a message -> writes of at most `max` bytes, as a central makes them
function bleWrites(msg, max) {
  const t = Date.now() & 0x1FFF, hdr = 0x80 | t >> 7, ts = 0x80 | (t & 0x7F), out = [];
  let p = [hdr];
  for (const b of msg) {
    if (p.length + (b & 0x80 ? 2 : 1) > max) { out.push(p); p = [hdr]; }
    if (b & 0x80) p.push(ts);
    p.push(b);
  }
  if (p.length > 1) out.push(p);
  return out;
}
// and a notification -> its bytes, timestamps out (after the header, a byte with bit 7 set is a timestamp unless the byte
// before was one); the SysEx kept across notifications and handed on whole
function bleRead(st, p, deliver) {
  if (!p.length || p.length > 20 || (p[0] & 0xC0) !== 0x80) st.bad++;
  let ts = false;
  for (let i = 1; i < p.length; i++) {
    const b = p[i];
    if ((b & 0x80) && !ts) { ts = true; continue; }
    ts = false;
    if (b === 0xF0) st.sx = [b];
    else if (st.sx) { st.sx.push(b); if (b === 0xF7) { deliver(Uint8Array.from(st.sx)); st.sx = null; } }
    else st.bad++;                                  // (the pedal sends nothing but the editor's SysEx over Bluetooth)
  }
}

function sim(args) {
  const child = spawn(SIM, args, { stdio: ["pipe", "pipe", "inherit"] });
  const ble = BLE ? { sizes: [20, 182, 3, 514, 7, 244], k: 0, sx: null, bad: 0, notes: 0, writes: 0, after: null, between: null,
    c: { sx: null, bad: 0, notes: 0, got: [] } } : null;   // (c: the second LE link's notifications, read apart)
  if (ble) ble.raw = (l) => child.stdin.write(`${l}\n`);   // (a line as it is: another link's writes)
  const usb = [];                                    // (BLE: what came back the USB way)
  const access = { inputs: new Map(), outputs: new Map() };
  const lines = [];
  let port = null, buf = "", readyFn;
  const ready = new Promise((r) => { readyFn = r; });
  child.stdout.on("data", (d) => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const ln = buf.slice(0, i); buf = buf.slice(i + 1);
      if (ln.startsWith("#port ")) {
        const name = ln.slice(6);
        const input = { name, state: "connected", onmidimessage: null, open: async () => {} };
        const output = { name, state: "connected", open: async () => {}, send: (b) => {
          if (!ble) { child.stdin.write(Buffer.from(b).toString("hex") + "\n"); return; }
          for (const w of bleWrites(b, ble.sizes[ble.k++ % ble.sizes.length])) {
            ble.writes++; child.stdin.write(`b${Buffer.from(w).toString("hex")}\n`);
            if (ble.between) child.stdin.write(`${ble.between}\n`);   // (a line after each write: another link's, in turns)
          }
          if (ble.after) { child.stdin.write(`${ble.after}\n`); ble.after = null; }   // (a line right after this request's writes)
        } };
        port = { input, output };
        access.inputs.set(name, input); access.outputs.set(name, output);
        readyFn();
      } else if (ln.startsWith("#")) lines.push(ln);
      else if (ble && ln.startsWith("b")) {
        ble.notes++;
        bleRead(ble, Buffer.from(ln.slice(1), "hex"), (data) => { if (port && port.input.onmidimessage) port.input.onmidimessage({ data }); });
      } else if (ble && ln.startsWith("c")) {                     // the second LE link's notifications
        ble.c.notes++;
        bleRead(ble.c, Buffer.from(ln.slice(1), "hex"), (data) => ble.c.got.push(data));
      } else if (ble && ln) usb.push(ln);
      else if (port && port.input.onmidimessage && ln) port.input.onmidimessage({ data: Uint8Array.from(Buffer.from(ln, "hex")) });
    }
  });
  const done = new Promise((r) => child.on("exit", r));
  return {
    access, ready, lines,
    pedal: (l) => child.stdin.write(`#pedal ${l}\n`),
    cc: (n, v) => child.stdin.write(`#cc ${n} ${v}\n`),       // (FX) a Control Change from a controller on the pedal
    dump: async () => { lines.length = 0; child.stdin.write("#dump\n"); for (let i = 0; i < 50 && !lines.length; i++) await sleep(10); return lines[0] || ""; },
    ble, usb,
    usbSend: (b) => child.stdin.write(Buffer.from(b).toString("hex") + "\n"),
    bt: async (l = "") => {                     // "#bt": [frames answered, notifications, replies lost, SysEx dropped]
      lines.length = 0; child.stdin.write(`#bt${l ? ` ${l}` : ""}\n`);
      if (l) return null;
      for (let i = 0; i < 50 && !lines.length; i++) await sleep(10);
      return (lines[0] || "").split(" ").slice(1).map(Number);
    },
    end: () => { child.stdin.end(); return done; },
  };
}

// over BLE MIDI: every frame was answered, nothing dropped either way, every notification well formed
async function bleCheck(s) {
  if (!BLE) return;
  const [frames, notes, lost, drop] = await s.bt();
  ok(frames > 0 && notes === s.ble.notes + s.ble.c.notes && !lost && !drop && !s.ble.bad && !s.ble.c.bad,
     `BLE: ${frames} frames in ${s.ble.writes} writes, ${notes} notifications back, none lost or malformed`);
}

// ---- 1. connect, catalogue, current sound ----
const flash1 = join(TMP, "flash1.bin");
let s = sim(["--dump", flash1]);
await s.ready;
let ed = new Editor(s.access);
const info = await ed.connect();
ok(info && info.proto === (FX ? 6 : 4) && info.version === (FX ? "v39 FX6" : "v39") && info.identity === "PocketX_939", `INFO: ${info && `${info.version} ${info.identity}`}`);
ok(info && info.nblk === 12 && info.npar === 5 && info.nslot === 52 && info.nameMax === 13, "INFO: 12 blocks, 5 parameters, 52 slots, names of 13");
const t0 = Date.now();
const cat = await ed.catalog();
const ntypes = cat.blocks.reduce((n, b) => n + b.types.length, 0);
ok(cat.blocks.length === 12 && cat.blocks[5].label === "AMP" && cat.blocks[5].types.length === 9 && cat.blocks[5].types[8].name === "NAM", `catalogue: 12 blocks, ${ntypes} types (${Date.now() - t0} ms)`);
ok(cat.blocks[11].label === "SYN" && cat.blocks[11].color === "#ff1494", `catalogue: the SYN block and its tile colour (${cat.blocks[11].color})`);
const withTexts = cat.blocks.flatMap((b) => b.types.flatMap((t) => t.params.filter((p) => p.texts))).length;
ok(withTexts > 0 && cat.blocks[9].types[0].params.some((p) => p.texts && p.texts.some((x) => /ms|\d/.test(x))), `catalogue: value texts for ${withTexts} parameters (delay time...)`);
let st = await ed.state();
ok(st.slot === 0 && !st.dirty && st.preset.name.length > 0, `STATE: slot 1 "${st.preset.name}", not changed`);

// ---- 2. live editing ----
const amp = 5;
const v1 = await ed.setParam(amp, 0, 77);
st = await ed.state();
ok(v1 === 77 && st.preset.p[amp][0] === 77 && st.dirty, "SET: AMP's 1st knob = 77, the sound is now changed (*)");
ok(await ed.setParam(amp, 1, 120) === 100, "SET: a value above 100 is clamped to 100");
ok(await ed.setType(amp, 99) === 8, "SET: a type out of range is clamped to the last one (NAM)");
await ed.setOn(9, 1);
ok((await ed.state()).preset.on[9] === 1, "SET: the delay switched on");
ok(await ed.name("my toneé longer than thirteen") === "my tone? long", "NAME: 13 characters at most, other characters become ?");
const ord = [...st.preset.order]; [ord[0], ord[11]] = [ord[11], ord[0]];
ok(await ed.order(ord) === 0 && (await ed.state()).preset.order.join() === ord.join(), "ORDER: the block order changed");
ok(await ed.order([0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]) === 1, "ORDER: not a permutation -> refused");
const bad = clonePreset(st.preset); bad.p[0][0] = 100; bad.on[3] = 1; bad.type[4] = 60;
ok(await ed.put(bad) === 1, "PUT: a type that does not exist -> refused");
await ed.focus(9);
ok((await ed.state()).focus === 9, "FOCUS: the pedal shows the delay page");

// ---- 3. pushes: a change on the pedal reaches the editor, the editor's own does not come back ----
const pushes = [];
ed.onState = (x) => pushes.push(x);
await ed.watch(true);
s.pedal(`set ${amp} 2 33`);
for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
ok(pushes.length === 1 && pushes[0].preset.p[amp][2] === 33, "WATCH: a knob turned on the pedal is pushed to the editor");
pushes.length = 0;
await ed.setParam(amp, 3, 12);
await sleep(150);
ok(pushes.length === 0, "WATCH: the editor's own change is not echoed back");
s.pedal("preset 7");
for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
ok(pushes.length >= 1 && pushes.at(-1).slot === 7, "WATCH: a footswitch (slot 8) is pushed");
if (BLE) {                                     // the LE link goes: the pushes stop; a new one, and the editor goes on
  await s.bt("drop");
  pushes.length = 0;
  s.pedal(`set ${amp} 2 44`);
  await sleep(150);
  const quiet = pushes.length === 0;
  await s.bt("link");
  await ed.watch(true);
  s.pedal(`set ${amp} 2 45`);
  for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
  ok(quiet && pushes.length >= 1 && pushes.at(-1).preset.p[amp][2] === 45, "BLE: the link gone, nothing is pushed; a new link, and WATCH again");
  s.usb.length = 0;                            // USB and Bluetooth at once: each frame is answered the way it came
  s.usbSend([0xF0, 0x7D, 0x4F, 0x50, 1, 0xF7]);
  const st2 = await ed.state();
  for (let i = 0; i < 40 && !s.usb.length; i++) await sleep(10);
  const u = s.usb.length ? Buffer.from(s.usb[0], "hex") : null;
  ok(u && u[4] === 1 && s.usb.length === 1 && st2.slot === 7, "BLE: a USB INFO meanwhile is answered over USB, the editor's STATE over Bluetooth");
  // a push and a reply cross: a knob turned on the pedal right after a request (its line comes after the request's writes)
  // while the long reply goes out; then a request right after a knob, while the push goes out. Each waits for the other's
  // notifications (two a pass), nothing is lost. (60 ms first: the pedal pushes at most every 40 ms)
  pushes.length = 0;
  await sleep(60);
  s.ble.k = 0; s.ble.after = `#pedal set ${amp} 2 46`;          // (k 0: each request in one write of 20 bytes)
  const l1 = await ed.list();
  for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
  await sleep(60);
  s.ble.k = 0;
  s.pedal(`set ${amp} 2 47`);
  const pg = await ed.ping();
  for (let i = 0; i < 40 && !pushes.some((x) => x.preset.p[amp][2] === 47); i++) await sleep(10);
  const [, , lost] = await s.bt();
  ok(l1.length === 52 && pg && pushes.some((x) => x.preset.p[amp][2] === 46) && pushes.some((x) => x.preset.p[amp][2] === 47) && lost === 0,
     "BLE: a push and a reply cross: each waits for the other, nothing lost");
  // two LE links (test build 27): the Chocolate on link 1 presses between every write of the editor's requests on link 0
  // (a Control Change, then a Program Change); the editor's frames still come whole, and nothing goes to link 1
  s.ble.between = "c808eb0187f";
  const n0 = s.ble.c.notes, two = await ed.state(), l2 = await ed.list();
  s.ble.between = "c808ec003";
  const nm2 = await ed.namList();
  s.ble.between = null;
  ok(two.slot === 7 && l2.length === 52 && nm2.models.length === 28 && s.ble.c.notes === n0,
     "BLE, two links: the Chocolate's writes between the editor's on the other link spoil nothing, nothing goes to it");
  // a second editor on link 1: INFO from there is answered there only, while the editor on link 0 goes on
  s.ble.c.got.length = 0;
  for (const w of bleWrites([0xF0, 0x7D, 0x4F, 0x50, 1, 0xF7], 7)) s.ble.raw(`c${Buffer.from(w).toString("hex")}`);
  const st3 = await ed.state();
  for (let i = 0; i < 40 && !s.ble.c.got.length; i++) await sleep(10);
  const ci = s.ble.c.got[0];
  ok(ci && ci[4] === 1 && s.ble.c.got.length === 1 && st3.slot === 7, "BLE, two links: INFO asked on link 1 is answered on link 1; link 0's STATE on link 0");
  // link 1 goes: the editor on link 0, the last to ask, is still pushed, without asking again
  await ed.state();
  await s.bt("drop 1");
  pushes.length = 0;
  await sleep(60);
  s.pedal(`set ${amp} 2 48`);
  for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
  ok(pushes.length >= 1 && pushes.at(-1).preset.p[amp][2] === 48, "BLE, two links: link 1 gone, the editor on link 0 is still pushed");
  await s.bt("link 1");
}

// ---- 4. saving, slots ----
await ed.select(2);
await ed.setParam(amp, 0, 11);
await ed.name("SAVED THREE");
ok(await ed.save(4) === 0, "SAVE: the current sound into slot 5");
st = await ed.state();
let list = await ed.list();
ok(st.slot === 4 && !st.dirty && list[4].saved && list[4].name === "SAVED THREE", "SAVE: slot 5 is current, saved, named");
ok(list.length === 52 && list.filter((x) => x.saved).length === 1, "LIST: 52 slots, 1 saved");
const g = await ed.slotGet(10);
ok(!g.saved && g.preset.name === list[10].name, `SLOT_GET: slot 11, factory "${g.preset.name}"`);
const a = clonePreset(g.preset); a.name = "STAGED A";
const b = clonePreset(g.preset); b.name = "STAGED B"; b.p[amp][0] = 99;
ok(await ed.slotPut(20, a, false) === 0 && await ed.slotPut(21, b, false) === 0, "SLOT_PUT: two slots staged in memory");
ok(await ed.commit() === 0, "COMMIT: one flash write");
ok(await ed.slotPut(4, null, true) === 0 && !(await ed.list())[4].saved, "SLOT_PUT: slot 5 back to its factory preset, written at once");
const dumpLine = await s.dump();
ok(dumpLine.split("|")[2] === "1", "the flash was written only inside the preset store");

// ---- 5. files ----
const q = (await ed.slotGet(21)).preset;
const file = JSON.stringify(filePresets([presetToJSON(q, cat, 21)]));
const back = parsePresetFile(file);
const r = presetFromJSON(back[0], cat, (await ed.slotGet(0)).preset);
ok(r.warn.length === 0 && JSON.stringify(r.preset) === JSON.stringify(q), "file: a preset survives export -> import exactly");
const odd = presetFromJSON({ name: "X", blocks: [{ block: "AMP", on: true, type: "NO SUCH AMP", params: [1, 2, 3] }, { block: "FOO" }] }, cat, q);
ok(odd.warn.length === 2 && odd.preset.p[amp][0] === 1, "file: unknown type and block are reported, the rest comes through");
const v33 = presetFromJSON({ name: "OLD", blocks: [{ block: "AMP", on: true, type: "RECTIFIED" }, { block: "DRV", on: true, type: "MUFF PI" }, { block: "CAB", on: true, type: "4x12 H30" }] }, cat, q);
const tn = (b, t) => cat.blocks[b].types[t].name;
ok(v33.warn.length === 3 && tn(amp, v33.preset.type[amp]) === "5150" && tn(4, v33.preset.type[4]) === "M77 OD" && tn(6, v33.preset.type[6]) === "4x12 V30",
   "file: a v33 preset file -> the nearest v34 amp / drive / cab, each one reported");
const dl = cat.blocks.findIndex((b) => b.label === "DLY");
const oldDly = presetFromJSON({ name: "ECHO", blocks: [{ block: "DLY", on: true, type: "TAPE EP-3", params: [50, 30, 20, 50, 50] }] }, cat, q);
const newDly = presetFromJSON(presetToJSON(oldDly.preset, cat), cat, q);
ok(oldDly.preset.p[dl][0] === 40 && oldDly.warn.length === 1 && newDly.preset.p[dl][0] === 40 && newDly.warn.length === 0,
   "file: a delay TIME from before v35 keeps its time (50 -> 40 = 510 ms); a v35 file is taken as is");
// ---- v42: USB AUDIO (protocol 4): off until asked for; kept in the store ----
{
  const u0 = await ed.usbAudio();
  const u1 = await ed.usbAudio(true);
  ok(ed.hasUsbAudio && !u0.on && u0.rc === 0 && u1.on && u1.rc === 0, "USB audio: off by default; switched on");
}
// ---- test build 39 (protocol 5, the Bluetooth audio image): controls learned on the pedal, MIDI_IN, SWITCH ----
if (FX) {
  const eq = cat.blocks.findIndex((b) => b.label === "EQ"), dly = cat.blocks.findIndex((b) => b.label === "DLY");
  let m = await ed.pedalMap();
  ok(ed.hasPedalLearn && m.rc === 0 && !m.each && m.inputDb === 0 && !m.tails && m.btPower === 0 && m.entries.length === PEDAL_MAP_N && m.entries.every((e) => !e),
     "MAP: nothing learned, SWITCH MOMENT, INPUT 0 dB, TAILS off, BT POWER HIGH, 8 places");
  const got = [];
  ed.onMidiIn = (x) => got.push(x);
  ok(await ed.watch(true) === 3, "WATCH 3: the pedal also pushes what a controller sends it");
  s.cc(80, 127); await sleep(150); s.cc(80, 0); await sleep(150);
  ok(got.length === 2 && got[0].type === "cc" && got[0].n1 === 80 && got[0].n2 === 127 && got[1].n2 === 0,
     `MIDI_IN: a controller's CC 80 = 127, then 0, pushed (${got.map((x) => `${x.n1}=${x.n2}`).join(" ")})`);
  m = await ed.setPedalMap(false, pedalLearn(m.entries, 80, { kind: "on", b: eq }, switchMode([127, 0])));
  ok(m.rc === 0 && m.entries[0] && m.entries[0].src === 80 && m.entries[0].dst === 8 + eq && m.entries[0].mode === 0, "MAP: CC 80 learned as the EQ's flip, on its press");
  let was = (await ed.state()).preset.on[eq];
  s.cc(80, 127); await sleep(100);
  const pressed = (await ed.state()).preset.on[eq];
  s.cc(80, 0); await sleep(100);
  const released = (await ed.state()).preset.on[eq];
  ok(pressed === 1 - was && released === pressed, "a learned switch acts: CC 80's press flips the EQ, its release does nothing");
  m = await ed.setPedalMap(false, pedalLearn(m.entries, 81, { kind: "on", b: dly }, switchMode([127])));
  m = await ed.setPedalMap(false, pedalLearn(m.entries, 82, { kind: "preset", step: 1 }, switchMode([127, 0])));
  m = await ed.setPedalMap(false, pedalLearn(m.entries, 4, { kind: "param", b: eq, k: 0 }, switchMode([30])));
  ok(m.rc === 0 && m.entries.filter(Boolean).length === 4 && m.entries[1].mode === MAP_EACH && m.entries[2].dst === CC_NEXT && m.entries[3].mode === MAP_VALUE,
     "MAP: four learned (a switch that alternates, the next preset, a knob as a setting)");
  was = (await ed.state()).preset.on[dly];
  s.cc(81, 127); await sleep(80); s.cc(81, 0); await sleep(80); s.cc(81, 127); await sleep(100);
  ok((await ed.state()).preset.on[dly] === 1 - was, "EACH: three messages of CC 81 flip the delay three times");
  const slot0 = (await ed.state()).slot;
  s.cc(82, 127); await sleep(100); s.cc(82, 0); await sleep(100);
  ok((await ed.state()).slot === (slot0 + 1) % 52, "CC 82 (learned as the next preset) steps one preset on");
  s.cc(4, 127); await sleep(100);
  ok((await ed.state()).preset.p[eq][0] === 100, "CC 4 (learned as the EQ's first setting) sets it by its value");
  const bad = await ed.setPedalMap(false, [{ src: 90, dst: 3, mode: 0 }, null, null, null, null, null, null, null]);
  const dup = await ed.setPedalMap(false, [{ src: 90, dst: 15, mode: 0 }, { src: 90, dst: 16, mode: 0 }, null, null, null, null, null, null]);
  ok(bad.rc === 1 && dup.rc === 1 && bad.entries.filter(Boolean).length === 4, "MAP: a number the pedal does not read, or one controller number twice, is refused (the table kept)");
  m = await ed.setPedalMap(true, m.entries, 4, true, 2, 1);
  ok(m.rc === 0 && m.each && m.inputDb === 4 && m.tails && m.btPower === 2 && m.outMode === 1, "MAP: SWITCH LATCH, INPUT +4 dB, TAILS on, BT POWER LOW, OUTPUT STEREO");
  // test build 43: USB audio's routing, the byte after the entries
  ok(m.usb && m.usb.send === 0 && m.usb.ret === 0 && !m.usb.cut, "MAP: USB audio's routing read, the default (USB SEND OUTPUT, USB RETURN OUTPUT, GUITAR MIX)");
  m = await ed.setPedalMap(true, m.entries, 4, true, 2, 1, { send: 2, ret: 2 + dly, cut: true });
  ok(m.rc === 0 && m.usb.send === 2 && m.usb.ret === 2 + dly && m.usb.cut && m.outMode === 1 && m.entries.filter(Boolean).length === 4,
     "MAP: USB SEND DI + OUT, USB RETURN before DELAY, GUITAR CUT (the rest kept)");
  m = await ed.setPedalMap(true, m.entries, 4, true, 2, 1);
  ok(m.rc === 0 && m.usb.send === 2 && m.usb.ret === 2 + dly && m.usb.cut, "MAP: an editor that sends no routing leaves it as it is");
  {
    const raw = [1, 1, 14, 1 | 2 << 1 | 1 << 3, ...m.entries.flatMap((e) => (e ? [1, e.src, e.dst, e.mode] : [0, 0, 0, 0]))];
    const r1 = ed._map(await ed.request(30, [...raw, 3], 4000)), r2 = ed._map(await ed.request(30, [...raw, 15 << 3], 4000));
    ok(r1.rc === 1 && r2.rc === 1 && r2.usb.ret === 2 + dly && r2.usb.send === 2, "MAP: a routing no build writes (SEND 3, RETURN 15) is refused, the one set kept");
  }
  ed.onMidiIn = null;
}
await bleCheck(s);
await s.end();

// ---- 6. after a power cycle: the store is read back from the flash ----
s = sim([flash1]);
await s.ready;
ed = new Editor(s.access);
await ed.connect();
list = await ed.list();
{
  let u = await ed.usbAudio();
  ok(u.on && u.rc === 0, "USB audio: still on after a power cycle");
  ok(!u.recording && !u.playing && Object.keys(u.counts).length === 20, "USB audio: the streams' state and 20 counts");
  u = await ed.usbAudio(false);
  ok(!u.on && u.rc === 0 && !(await ed.usbAudio()).on, "USB audio: switched off again");
}
if (FX) {
  const m = await ed.pedalMap();
  ok(m.rc === 0 && m.each && m.inputDb === 4 && m.tails && m.btPower === 2 && m.outMode === 1 && m.entries.filter(Boolean).length === 4 && m.entries[0].src === 80,
     "power cycle: the controls learned on the pedal, LATCH, INPUT, TAILS, BT POWER and OUTPUT are still there");
  ok(m.usb && m.usb.send === 2 && m.usb.cut && m.usb.ret === 2 + cat.blocks.findIndex((b) => b.label === "DLY"),
     "power cycle: USB SEND, USB RETURN and GUITAR are still there (test build 43)");
}

// ---- 7. v37: NAM models (an A2 file -> a user slot -> the AMP block's NAM type) ----
{
  let seed = 7;
  const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 - 0.5; };
  const w = Array.from({ length: NW }, () => rnd() * 0.6);
  w[NW - 1] = 0.02;                                                    // head_scale
  const layer = { input_size: 1, condition_size: 1, head: { out_channels: 1, kernel_size: 16, bias: true }, channels: 3,
    kernel_sizes: KERNELS, dilations: DILATIONS, activation: KERNELS.map(() => ({ type: "LeakyReLU", negative_slope: 0.01 })),
    bottleneck: 3, head1x1: { active: false }, layer1x1: { active: true }, gating_mode: KERNELS.map(() => "none") };
  const a2 = { version: "0.7.0", architecture: "WaveNet", config: { layers: [layer], head: null, head_scale: 0.02 }, weights: w, sample_rate: 48000 };
  const file = JSON.stringify({ architecture: "SlimmableContainer", config: { submodels: [{ max_value: 0.5, model: a2 }] } });
  let m = null, why = "";
  try { m = parseNam(file, "My Test Amp.nam"); } catch (e) { why = e.message; }
  ok(m && m.name === "MY TEST AMP" && m.weights.length === NW && /semitones/.test(m.note), `NAM: an A2 file's A2-Lite part is read (${m ? m.name : why})`);
  let a1why = "";
  try { parseNam(JSON.stringify({ architecture: "WaveNet", config: { layers: [{ channels: 16 }] }, weights: [1] }), "a1.nam"); } catch (e) { a1why = e.message; }
  ok(/not an A2 model/.test(a1why), "NAM: an A1 capture is refused, with the reason");
  const di = readDi(readFileSync(join(ROOT, "docs", "nam_di.wav")).buffer);
  const lv = levelTrim(m.weights, di);
  ok(Number.isFinite(lv) && Math.abs(lv) <= 24, `NAM: level measured on the DI clip (${lv.toFixed(2)} dB)`);
  let nl = await ed.namList();
  ok(nl.nfact === 12 && nl.nuser === 16 && nl.models.filter((x) => !x.user && x.used).length === 12 && nl.models.every((x) => !x.user || !x.used),
     "NAM_LIST: 12 factory models, 16 empty user slots");
  const rc = await ed.namUpload(4, packModel(m.name, m.weights, lv));
  nl = await ed.namList();
  ok(rc === 0 && nl.models[12 + 4].used && nl.models[12 + 4].name === "MY TEST AMP", "NAM: the model is written into user slot 5");
  await ed.catalog();
  const nt = cat.blocks[amp].types.findIndex((t) => t.name === "NAM");
  await ed.namTexts();
  const texts = ed.cat.blocks[amp].types[nt].params[0].texts;
  const kv = texts.indexOf("MY TEST ");
  ok(nt === 8 && kv >= 0, `NAM: the AMP block's NAM type offers it as a MODEL (knob ${kv})`);
  await ed.setType(amp, nt);
  ok(await ed.setParam(amp, 0, kv) === kv && (await ed.state()).preset.type[amp] === nt, "NAM: a sound uses it");
  const bad = packModel(m.name, m.weights, lv); bad[64 + 100] ^= 0x55;           // a weight changed: the CRC no longer matches
  ok(await ed.namUpload(5, bad) === 1 && !(await ed.namList()).models[12 + 5].used, "NAM: a damaged model is refused, the slot stays empty");
  if (FX) {                                       // test build 40 (protocol 5): NAM_RENAME, on the pedal itself
    ok(ed.hasNamRename && await ed.namRename(4, "VOX AC30 VX") === 0 && (await ed.namList()).models[12 + 4].name === "VOX AC30 VX",
       "NAM_RENAME: user slot 5 renamed on the pedal (no re-upload)");
    const st4 = await ed.state();
    ok(st4.preset.type[amp] === nt && st4.preset.p[amp][0] === kv, "NAM_RENAME: the sound using it is unchanged");
    ok(await ed.namRename(6, "NOTHING") === 1, "NAM_RENAME: an empty slot is refused");
    ok(await ed.namRename(4, "") === 1 && (await ed.namList()).models[12 + 4].name === "VOX AC30 VX", "NAM_RENAME: an empty name is refused, the name kept");
  }
  ok(await ed.namErase(4) === 0 && !(await ed.namList()).models[12 + 4].used, "NAM: the slot is cleared again");
}
// ---- 8. v38: cabinet IRs (a WAV file -> an IR slot -> the CAB block's USER IR type) ----
{
  const wav = (sr, bits, ch, frames) => {        // a WAV file of `frames` (arrays of channel samples, -1..1)
    const bps = bits >> 3, n = frames.length, b = new ArrayBuffer(44 + n * ch * bps), v = new DataView(b);
    const str = (o, t) => { for (let i = 0; i < 4; i++) v.setUint8(o + i, t.charCodeAt(i)); };
    str(0, "RIFF"); v.setUint32(4, 36 + n * ch * bps, true); str(8, "WAVE"); str(12, "fmt "); v.setUint32(16, 16, true);
    v.setUint16(20, bits === 32 ? 3 : 1, true); v.setUint16(22, ch, true); v.setUint32(24, sr, true);
    v.setUint32(28, sr * ch * bps, true); v.setUint16(32, ch * bps, true); v.setUint16(34, bits, true);
    str(36, "data"); v.setUint32(40, n * ch * bps, true);
    frames.forEach((f, i) => f.forEach((x, c) => {
      const o = 44 + (i * ch + c) * bps;
      if (bits === 16) v.setInt16(o, Math.round(x * 32767), true);
      else if (bits === 24) { const k = Math.round(x * 8388607); v.setUint8(o, k & 255); v.setUint8(o + 1, k >> 8 & 255); v.setInt8(o + 2, k >> 16); }
      else v.setFloat32(o, x, true);
    }));
    return b;
  };
  let seed = 11;
  const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 - 0.5; };
  const resp = (sr, ms) => Array.from({ length: Math.round(sr * ms / 1000) }, (_, i) => [i < 20 ? 0 : rnd() * Math.exp(-(i - 20) / (sr * 0.004)), 0]);
  const w48 = parseWav(wav(48000, 24, 2, resp(48000, 200)));
  ok(w48.sampleRate === 48000 && w48.channels === 2 && w48.bits === 24 && w48.x.length === 9600, "IR: a 24-bit stereo WAV at 48 kHz is read");
  const ir = prepareIr(w48);
  ok(ir.h.length === IR_LEN && Math.abs(ir.h[0]) > 0 && /48 kHz/.test(ir.note) && /23 ms/.test(ir.note) && Math.abs(ir.h[IR_LEN - 1]) < 1e-3,
     `IR: from its onset, at 44.1 kHz, 1024 samples, faded (${ir.note})`);
  const sine = Array.from({ length: 4800 }, (_, i) => Math.sin(2 * Math.PI * 1000 * i / 48000));
  const rs = resample(sine, 48000, 1000), errs = rs.slice(100, 900).map((v, i) => v - Math.sin(2 * Math.PI * 1000 * (i + 100) / 44100));
  ok(Math.max(...errs.map(Math.abs)) < 1e-3, "IR: the resampling keeps a 1 kHz tone (error below -60 dB)");
  let bad = ""; try { parseWav(new ArrayBuffer(40)); } catch (e) { bad = e.message; }
  ok(/not a WAV/.test(bad), "IR: a file that is not a WAV is refused, with the reason");
  const f32 = prepareIr(parseWav(wav(44100, 32, 1, resp(44100, 10).map((f) => [f[0]]))));
  ok(f32.note === "" && f32.h[1000] === 0, "IR: a short float WAV at 44.1 kHz is kept as it is (zeros after its end)");
  let il = await ed.irList();
  ok(il.length === 8 && il.every((x) => !x.used), "IR_LIST: 8 empty slots");
  const name = irName("Mesa_4x12 V30.wav");
  const rc = await ed.irUpload(2, packIr(name, ir.h));
  il = await ed.irList();
  ok(rc === 0 && name === "MESA 4X12 V" && il[2].used && il[2].name === name, `IR: written into slot 3 (${il[2].name})`);
  await ed.catalog();
  const cabB = cat.blocks.findIndex((b) => b.label === "CAB"), it = cat.blocks[cabB].types.findIndex((t) => t.name === "USER IR");
  await ed.irTexts();
  const texts = ed.cat.blocks[cabB].types[it].params[0].texts, kv = texts.indexOf("MESA 4X1");
  ok(it === 7 && kv >= 0 && ed.cat.blocks[cabB].types[it].params.map((p) => p.name).join() === "IR,LOW CUT,HIGH CUT",
     `IR: the CAB block's USER IR type offers it (knob ${kv})`);
  await ed.setType(cabB, it);
  ok(await ed.setParam(cabB, 0, kv) === kv && (await ed.state()).preset.type[cabB] === it, "IR: a sound uses it");
  const dmg = packIr(name, ir.h); dmg[64 + 300] ^= 0x21;
  ok(await ed.irUpload(3, dmg) === 1 && !(await ed.irList())[3].used, "IR: a damaged IR is refused, the slot stays empty");
  ok(await ed.irErase(2) === 0 && !(await ed.irList())[2].used, "IR: the slot is cleared again");
}
ok(list[20].name === "STAGED A" && list[21].name === "STAGED B" && list[20].saved && !list[4].saved, "power cycle: the saved slots are still there");
st = await ed.state();
ok(st.slot === 4, "power cycle: the pedal starts on the last slot SAVEd (imports do not move it)");
await bleCheck(s);
await s.end();

// ---- 8. protocol 6 (the Bluetooth audio image's editor for test builds 46 to 49): MAP's settings bytes, a preset's own
// controls learned on the pedal (PL), scenes (SCENE); then a power cycle. A store of its own (section 6's stays as it is) ----
if (FX) {
  const flash2 = join(TMP, "flash2.bin");
  s = sim(["--dump", flash2]);
  await s.ready;
  ed = new Editor(s.access);
  const inf = await ed.connect();
  const eq = cat.blocks.findIndex((b) => b.label === "EQ"), dly = cat.blocks.findIndex((b) => b.label === "DLY");
  ok(inf.proto === 6 && ed.hasPresetLearn && ed.hasScenes, "protocol 6: a preset's own controls, scenes, MAP's settings bytes");
  // MAP: GUITAR TONE and PICKUP, PLAY VIEW, SCENE+SWITCH, SCENE FOOTSW
  let m = await ed.pedalMap();
  ok(m.rc === 0 && m.more && m.more.tone === 0 && m.more.pickup === 0 && m.more.view === 0 && !m.more.sceneWins && !m.more.feetPresets,
     "MAP: GUITAR TONE NATURAL, PICKUP SINGLE, PLAY VIEW CHAIN, SWITCH WINS, the footswitches on the scenes (the defaults)");
  m = await ed.setPedalMap(m.each, m.entries, m.inputDb, m.tails, m.btPower, m.outMode, { send: 1, ret: 0, cut: false },
    { tone: 8, pickup: 1, view: 2, sceneWins: true, feetPresets: true });
  ok(m.rc === 0 && m.more.tone === 8 && m.more.pickup === 1 && m.more.view === 2 && m.more.sceneWins && m.more.feetPresets && m.usb.send === 1,
     "MAP: GUITAR TONE BRIGHT, PICKUP HUMBUCK, PLAY VIEW SCENES, SCENE WINS, the footswitches on the presets (and the routing)");
  m = await ed.setPedalMap(m.each, m.entries, m.inputDb, m.tails, m.btPower, m.outMode, m.usb);
  ok(m.rc === 0 && m.more.tone === 8 && m.more.pickup === 1 && m.more.view === 2 && m.more.feetPresets, "MAP: an editor that sends no settings bytes leaves them as they are");
  {
    const raw = [1, m.each ? 1 : 0, m.inputDb + 10, (m.tails ? 1 : 0) | m.btPower << 1 | m.outMode << 3,
      ...m.entries.flatMap((e) => (e ? [1, e.src, e.dst, e.mode] : [0, 0, 0, 0])), m.usb.send | (m.usb.cut ? 4 : 0) | m.usb.ret << 3];
    const r1 = ed._map(await ed.request(30, [...raw, 9, 2], 4000)), r2 = ed._map(await ed.request(30, [...raw, 3 << 4, 2], 4000));
    const r3 = ed._map(await ed.request(30, [...raw, 8, 3], 4000)), r4 = ed._map(await ed.request(30, [...raw, 8, 16], 4000));
    ok(r1.rc === 1 && r2.rc === 1 && r3.rc === 1 && r4.rc === 1 && r4.more.tone === 8 && r4.more.view === 2,
       "MAP: a GUITAR TONE, PICKUP or PLAY VIEW no build writes is refused, the ones set kept");
  }
  // PL: a preset's own controls
  const slot = (await ed.state()).slot, other = (slot + 7) % 52;
  let pl = await ed.presetMap(slot);
  ok(pl.rc === 0 && pl.slot === slot && pl.used === 0 && pl.places === PRESET_MAP_N && !pl.entries.length, "PL: none in this preset, 64 places for all the presets");
  pl = await ed.presetPut(slot, { ...presetEntry(30, { kind: "param", b: eq, k: 0 }, MAP_PRESS), lo: 20, hi: 80 });
  ok(pl.rc === 0 && pl.used === 1 && pl.entries.length === 1 && pl.entries[0].src === 30 && pl.entries[0].dst === ccParam(eq, 0) &&
     pl.entries[0].mode === MAP_VALUE && pl.entries[0].lo === 20 && pl.entries[0].hi === 80 && pl.entries[0].clo === 0 && pl.entries[0].chi === 127,
     "PL: CC 30 learned as a pedal on the EQ's first setting, in this preset, 20 at the heel and 80 at the toe");
  const at = async (v) => { s.cc(30, v); await sleep(100); return (await ed.state()).preset.p[eq][0]; };
  const heel = await at(0), toe = await at(127), mid = await at(64);
  ok(heel === 20 && toe === 80 && mid >= 49 && mid <= 51, `CC 30 drives it between the heel's and the toe's values (${heel}, ${mid}, ${toe})`);
  pl = await ed.presetPut(slot, presetEntry(31, { kind: "on", b: dly }, MAP_PRESS));
  pl = await ed.presetPut(slot, presetEntry(32, { kind: "scene", n: 2 }, MAP_PRESS));
  ok(pl.rc === 0 && pl.entries.length === 3 && pl.used === 3 && pl.entries.some((e) => e.src === 32 && e.dst === CC_SCENE0 + 2),
     "PL: CC 31 on the delay's ON, CC 32 on scene 3");
  const was = (await ed.state()).preset.on[dly];
  s.cc(31, 127); await sleep(80); s.cc(31, 0); await sleep(80);
  ok((await ed.state()).preset.on[dly] === 1 - was, "CC 31's press flips the delay, its release does nothing");
  pl = await ed.presetPut(slot, presetEntry(33, { kind: "on", b: dly }, MAP_EACH));
  ok(pl.rc === 0 && pl.entries.length === 3 && !pl.entries.some((e) => e.src === 31) && pl.entries.some((e) => e.src === 33 && e.mode === MAP_EACH),
     "PL: another control on the same row takes its place (one control a row)");
  pl = await ed.presetPut(slot, { ...presetEntry(33, { kind: "param", b: eq, k: 0 }, MAP_PRESS), lo: 100, hi: 0 });
  ok(pl.rc === 0 && pl.entries.length === 2 && pl.entries.find((e) => e.src === 33).dst === ccParam(eq, 0) && !pl.entries.some((e) => e.src === 30),
     "PL: a control moved to another row leaves its old one, and that row's control goes (one row a control)");
  const bad1 = await ed.presetPut(slot, { src: 40, dst: 3, mode: 0, lo: 0, hi: 100, clo: 0, chi: 127 });
  const bad2 = await ed.presetPut(slot, { src: 40, dst: ccParam(eq, 1), mode: MAP_VALUE, lo: 0, hi: 101, clo: 0, chi: 127 });
  ok(bad1.rc === 1 && bad2.rc === 1 && bad2.entries.length === 2, "PL: a number the pedal does not read, or a value out of range, is refused (the preset's kept)");
  pl = await ed.presetForget(slot, 33);
  ok(pl.rc === 0 && pl.entries.length === 1 && pl.used === 1 && pl.entries[0].src === 32, "PL: CC 33 forgotten");
  {   // the 64 places, all the presets together
    const room = (slot + 20) % 52, rows = [];
    for (let b = 0; b < 12; b++) for (let k = 0; k < 5; k++) rows.push(ccParam(b, k));
    for (let b = 0; b < 12; b++) rows.push(8 + b);
    let r = null;
    for (let i = 0; i < PRESET_MAP_N - 1; i++) r = await ed.presetPut(room, { src: 60 + i, dst: rows[i], mode: rows[i] < 20 ? MAP_PRESS : MAP_VALUE, lo: 0, hi: 100, clo: 0, chi: 127 });
    const full = await ed.presetPut(room, { src: 124, dst: rows[PRESET_MAP_N], mode: MAP_PRESS, lo: 0, hi: 100, clo: 0, chi: 127 });
    ok(r.rc === 0 && r.used === PRESET_MAP_N && full.rc === 3 && full.used === PRESET_MAP_N, "PL: 64 places for all the presets, the 65th refused (rc 3)");
    for (let i = 0; i < PRESET_MAP_N - 1; i++) r = await ed.presetForget(room, 60 + i);
    ok(r.rc === 0 && r.used === 1 && !r.entries.length, "PL: forgotten again, one by one");
  }
  // SCENE
  const onNow = async () => (await ed.state()).preset.on;
  const maskNow = async () => (await onNow()).reduce((a, v, b) => a | (v ? 1 << b : 0), 0);
  let sc = await ed.scenes();
  ok(sc.rc === 0 && sc.cur === 0 && sc.set === 0 && sc.saved.set === 0, "SCENE: none stored, scene 1 playing");
  const m0 = await maskNow();
  sc = await ed.sceneSet(2, 1 << dly);
  ok(sc.rc === 0 && sc.set === 4 && sc.masks[2] === 1 << dly && sc.cur === 0 && await maskNow() === m0, "SCENE: scene 3 set to the delay alone and stored; scene 1 plays on");
  sc = await ed.sceneRecall(2);
  ok(sc.rc === 0 && sc.cur === 2 && (sc.set & 5) === 5 && sc.masks[0] === m0 && await maskNow() === 1 << dly,
     "SCENE: scene 3 recalled, the delay alone on (SCENE WINS); scene 1 kept as it played");
  ok((await ed.state()).dirty, "SCENE: scenes not saved yet: the preset differs from its saved one");
  let pushes = 0;
  ok(await ed.watch(true) > 0, "WATCH: on");
  ed.onState = () => pushes++;
  s.cc(CC_SCENE0, 127); await sleep(150);
  sc = await ed.scenes();
  ok(sc.cur === 0 && await maskNow() === m0 && pushes > 0, "CC 65 from a controller recalls scene 1, the blocks as they played; the change pushed");
  s.cc(32, 127); await sleep(80); s.cc(32, 0); await sleep(80);
  sc = await ed.scenes();
  ok(sc.cur === 2 && await maskNow() === 1 << dly, "the preset's own CC 32 recalls scene 3");
  ed.onState = null;
  ok(await ed.save(slot) === 0, "SAVE: the preset with its scenes");
  sc = await ed.sceneRecall(0);
  ok(sc.saved.set === sc.set && sc.saved.masks[2] === 1 << dly && sc.cur === 0 && !(await ed.state()).dirty, "SCENE: saved with the sound; recalling one is no change");
  sc = await ed.sceneSet(1, 0);
  ok(sc.rc === 0 && (sc.set & 2) && sc.masks[1] === 0 && (await ed.state()).dirty, "SCENE: scene 2 set to no block: a change until SAVE");
  sc = await ed.sceneForget(1);
  ok(sc.rc === 0 && !(sc.set & 2) && !(await ed.state()).dirty, "SCENE: scene 2 forgotten: as saved again");
  const badSc = await ed.request(33, [2, 4, 0, 0]);
  ok(badSc[0] === 1, "SCENE: a fifth scene is refused");
  // test build 50: the scenes' names, typed in the editor, 3 characters, every preset its own
  ok(sc.names && sc.names.every((t) => t === "") && sc.saved.names && sc.saved.names.every((t) => t === ""), "SCENE: no scene named yet (test build 50)");
  sc = await ed.sceneName(2, "cho");
  ok(sc.rc === 0 && sc.names[2] === "CHO" && sc.names[0] === "" && sc.saved.names[2] === "" && (await ed.state()).dirty,
     "SCENE: scene 3 named CHO (typed cho): a change until SAVE");
  sc = await ed.sceneName(0, "A");
  ok(sc.rc === 0 && sc.names[0] === "A" && sc.names[2] === "CHO", "SCENE: a name of one character");
  const bn1 = await ed.request(33, [4, 1, 65, 0, 66]), bn2 = await ed.request(33, [4, 1, 127, 0, 0]), bn3 = await ed.request(33, [4, 4, 65, 0, 0]);
  ok(bn1[0] === 1 && bn2[0] === 1 && bn3[0] === 1, "SCENE: a character after a name's end, one past ASCII 126, a fifth scene: refused");
  ok(await ed.save(other) === 0 && (await ed.state()).slot === other, "SAVE into another slot");
  const pl2 = await ed.presetMap(other), sc2 = await ed.scenes();
  ok(pl2.entries.length === 1 && pl2.entries[0].src === 32 && pl2.used === 2 && sc2.saved.set === sc.set && sc2.saved.masks[2] === 1 << dly,
     "SAVE into another slot takes the preset's own controls and its scenes along");
  ok(sc2.saved.names[2] === "CHO" && sc2.saved.names[0] === "A" && !(await ed.state()).dirty, "SAVE keeps the scenes' names (test build 50)");
  await bleCheck(s);
  await s.end();
  // a power cycle: all of it read back from the flash
  s = sim([flash2]);
  await s.ready;
  ed = new Editor(s.access);
  await ed.connect();
  m = await ed.pedalMap();
  ok(m.more && m.more.tone === 8 && m.more.pickup === 1 && m.more.view === 2 && m.more.sceneWins && m.more.feetPresets,
     "power cycle: GUITAR TONE, PICKUP, PLAY VIEW and the two scene rows are still there");
  ok((await ed.state()).slot === other, "power cycle: the pedal starts on the slot saved last");
  pl = await ed.presetMap(other);
  sc = await ed.scenes();
  ok(pl.entries.length === 1 && pl.entries[0].dst === CC_SCENE0 + 2 && pl.used === 2, "power cycle: the preset's own controls are still there");
  ok(sc.set === sc2.saved.set && sc.masks[2] === 1 << dly && sc.cur === sc2.saved.cur, "power cycle: the preset opens with its scenes, in the scene it was saved in");
  ok(sc.names && sc.names[2] === "CHO" && sc.names[0] === "A" && sc.names[1] === "", "power cycle: the scenes' names are still there (test build 50)");
  await ed.select(slot);
  const scOther = await ed.scenes();
  await ed.select(other);
  const scBack = await ed.scenes();
  ok(scOther.names.every((t) => t === "") && scBack.names[2] === "CHO" && !(await ed.state()).dirty, "every preset has its own names: none in the first slot, CHO again in the other");
  await bleCheck(s);
  await s.end();
}

console.log(failed ? `\n${failed} FAILED` : "\nall editor tests passed");
process.exit(failed ? 1 : 0);
