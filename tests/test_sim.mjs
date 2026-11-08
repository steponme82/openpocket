// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the hosted simulator (tools/sim.mjs, docs/SIMULATOR.md) driven over its HTTP API: footswitches, knobs,
// the screen, the state, SysEx, the flash file that survives a restart, and the virtual Web MIDI shim carrying the real
// editor (docs/pxedit.js) and installer (docs/pxota.js) code. No browser, no hardware; needs a C compiler.
//   node tests/test_sim.mjs
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
// the version the simulated pedal reports (tests/edsim.c: OP_VERSION)
const VER = /#define OP_VERSION "v(\d+)"/.exec(readFileSync(new URL("edsim.c", import.meta.url), "utf8"))[1];
import { createContext, runInContext } from "node:vm";
import { fileURLToPath } from "node:url";
import { startSim } from "../tools/sim.mjs";
import { Editor } from "../docs/pxedit.js";
import { Updater } from "../docs/pxota.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const TMP = mkdtempSync(join(tmpdir(), "opsim-"));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(76)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
globalThis.localStorage = { getItem: () => null, setItem: () => {} };

const flash = join(TMP, "flash.bin");
let sim = await startSim({ port: 0, flash });
const get = async (path) => (await fetch(sim.url + path)).json();
const post = async (path, body) => fetch(sim.url + path, { method: "POST", body });
const act = async (path, body) => { const r = await post(path, body); assert.equal(r.status, 200, `${path}: ${await r.clone().text()}`); return r.json(); };
const state = () => get("/api/state");
const bad = async (path, status, what) => ok((await post(path)).status === status, what);
const png = async (path = "/api/screen.png") => Buffer.from(await (await fetch(sim.url + path)).arrayBuffer());

// ---- 1. the pedal: a port, a state, the real catalogue ----
let info = await get("/api/info");
ok(info.kind === "pedal" && info.port === "OpenPocket" && info.controls.includes("fs_up"), `info: ${info.kind} on port "${info.port}"`);
let s = await state();
ok(s.slot === 0 && s.name === "TWIN CLEAN" && s.mode === "chain" && s.saved === 0 && s.dirty === 0, `state: slot 1 "${s.name}", chain page, factory sound`);
ok(s.preset.p.length === 12 && s.preset.order.length === 12 && s.dsp.load === 42 && s.writes_ok === 1, "state: 12 blocks, the DSP load, flash writes allowed");

// ---- 2. footswitches and the screen ----
const shot0 = await png("/api/screen.rgb565"), f0 = s.frame;
ok(shot0.length === 240 * 240 * 2, "screen.rgb565: 240 x 240 x 2 bytes");
await act("/api/press/fs_up");
s = await state();
const shot1 = await png("/api/screen.rgb565");
ok(s.slot === 1 && s.name === "CHORUS CLEAN" && s.last_press === "fs_up" && s.presses === 1, `footswitch up: slot 2 "${s.name}"`);
ok(s.frame !== f0 && !shot0.equals(shot1), "the screen was redrawn (frame counter and pixels changed)");
const p = await png("/api/screen.png?scale=2");
ok(p.subarray(1, 4).toString() === "PNG" && p.readUInt32BE(16) === 480 && p.readUInt32BE(20) === 480, "screen.png?scale=2: a 480 x 480 PNG");
await act("/api/press/fs_dn");
ok((await state()).slot === 0, "footswitch down: back to slot 1");

// ---- 3. block buttons, the knob, the pages ----
await act("/api/press/amp");
s = await state();
ok(s.mode === "block" && s.block === 5, "AMP button: the AMP page");
await act("/api/knob/1");
await act("/api/press/push");
s = await state();
const gain = s.preset.p[5][0];
ok(s.mode === "edit", "knob + PUSH: editing the AMP's first setting");
await act("/api/knob/8");
s = await state();
ok(s.preset.p[5][0] === Math.min(100, gain + 8) && s.dirty === 1, `knob x8: GAIN ${gain} -> ${s.preset.p[5][0]}, the sound is edited`);
await act("/api/knob/-3?fast");
ok((await state()).preset.p[5][0] === Math.max(0, s.preset.p[5][0] - 15), "knob counterclockwise, fast: steps of 5");
await act("/api/press/home");
ok((await state()).mode === "chain", "HOME: back to the chain");
await act("/api/press/dpush");
ok((await state()).mode === "move", "double click PUSH: MOVE");
await act("/api/press/home");
await act("/api/press/home_long");
await act("/api/tuner/110");
await sleep(50);
s = await state();
ok(s.mode === "tuner" && s.tuner_hz === 110, "hold HOME: the tuner, hearing 110 Hz");
await act("/api/press/home");
await act("/api/volume/55");
await act("/api/battery/3640/0");
s = await state();
ok(s.mode === "chain" && s.volume === 55 && s.battery_mv === 3640, "any control leaves the tuner; volume and battery are set");

// ---- 3b. buttons held down, timed as the firmware times them ----
const held = async () => (await state()).held;
await act("/api/down/fs_up");
s = await state();
ok(s.slot === 1 && s.held.join() === "fs_up", "a footswitch acts when it goes down, and is reported held");
await act("/api/up/fs_up");
ok((await state()).slot === 1 && (await held()).length === 0, "its release does nothing more");
await act("/api/down/fs_dn"); await act("/api/down/fs_up");
s = await state();
ok(s.slot === 1 && s.mode === "chain" && s.held.length === 2, "both footswitches: two presses, no tuner (as openpocket.c)");
await act("/api/up/fs_dn"); await act("/api/up/fs_up");
await act("/api/press/amp");
await act("/api/down/home");
ok((await state()).mode === "block", "HOME does nothing yet while it is down");
await act("/api/up/home");
ok((await state()).mode === "chain", "HOME acts at a short release");
await act("/api/down/home");
await sleep(760);
ok((await state()).mode === "tuner", "HOME held 0.7 s: the tuner, before the release");
await act("/api/up/home");
s = await state();
ok(s.mode === "tuner" && s.held.length === 0, "the release of that hold leaves the tuner open");
await act("/api/down/home");
await sleep(760);
await act("/api/up/home");
ok((await state()).mode === "tuner", "HOME held again does nothing on the tuner (as openpocket.c)");
await act("/api/down/fx");
s = await state();
ok(s.mode === "chain", "any button leaves the tuner");
await act("/api/up/fx");
await act("/api/down/push");
await sleep(860);
ok((await state()).mode === "save", "PUSH held 0.8 s: the save page");
await act("/api/up/push");
ok((await state()).mode === "save", "the release of that hold stays on the save page");
await act("/api/press/home");
const click = async () => { await act("/api/down/push"); await act("/api/up/push"); };
await click();
ok((await state()).mode === "chain", "one click of PUSH on the chain waits for a second one");
await sleep(400);
ok((await state()).mode === "block", "after 350 ms it was a click: the block page");
await act("/api/press/home");
await click(); await click();
await sleep(400);
ok((await state()).mode === "move", "two clicks within 350 ms: MOVE");
await act("/api/press/home");
const k0 = (await state()).knob;
await act("/api/knob/3");
ok((await state()).knob === k0 + 3, "the state counts the knob's detents");
await bad("/api/down/push_long", 400, "only a button goes down: a gesture is refused");
await bad("/api/up/bogus", 400, "an unknown button is refused");
await act("/api/up/fs_up");
ok(true, "the release of a button that is not down is harmless");
await act("/api/press/fs_dn");

// ---- 4. save: the flash file, then a new session on it ----
await act("/api/press/push_long");
ok((await state()).mode === "save", "hold PUSH: the save page");
await act("/api/knob/4");
await act("/api/press/push");
s = await state();
ok(s.mode === "chain" && s.slot === 4 && s.saved === 1 && s.dirty === 0, "PUSH: saved into slot 5, now the current slot");
await sleep(100);
const dumped = Buffer.from(await (await fetch(sim.url + "/api/flash")).arrayBuffer());
ok(dumped.length === 0x100000 && existsSync(flash) && readFileSync(flash).equals(dumped), "the flash file holds the saved preset (GET /api/flash is the same)");
const saved = s.preset;
await sim.close();
sim = await startSim({ port: 0, flash });
s = await state();
ok(s.slot === 4 && s.saved === 1 && JSON.stringify(s.preset) === JSON.stringify(saved), "a restart on the same flash file: the session is as it was");
await act("/api/reset");
s = await state();
ok(s.slot === 0 && s.saved === 0 && !existsSync(flash), "reset: a blank flash, the factory presets, the file gone");

// ---- 5. bad requests ----
await bad("/api/press/bogus", 400, "an unknown control is refused");
await bad("/api/volume/500", 400, "a volume out of range is refused");
await bad("/api/knob/x", 400, "a knob step that is not a number is refused");
ok((await fetch(sim.url + "/api/press/fs_up")).status === 405, "a control needs POST");
ok((await fetch(sim.url + "/site/../tools/sim.mjs")).status === 404 && (await fetch(sim.url + "/site/%2e%2e/Makefile")).status === 404, "/site/ serves docs/ only");

const served = async (path, type) => { const r = await fetch(sim.url + path); return r.status === 200 && r.headers.get("content-type") === type; };
ok(await served("/", "text/html; charset=utf-8") && await served("/plain", "text/html; charset=utf-8") && await served("/sim/pedal-input.js", "text/javascript; charset=utf-8"), "/ is the pedal, /plain the plain control page, and the input module is served");

// ---- 6. SysEx over HTTP (docs/EDITOR-PROTOCOL.md) ----
let r = await act("/api/sysex", "f0 7d 4f 50 01 f7");
const b = Buffer.from(r.reply, "hex");
ok(b[4] === 1 && b.toString("latin1").includes(`v${VER}`) && b.toString("latin1").includes(`PocketX_9${VER}`), `sysex INFO: the editor protocol answers (v${VER}, PocketX_9${VER})`);
r = await act("/api/sysex?timeout=100", "f0 7d 4f 50 7e f7");
ok(r.reply === null, "sysex: a request the pedal does not understand gets no reply");
r = await act("/api/sysex", JSON.stringify([0xF0, 0x7D, 0x4F, 0x50, 0x06, 5, 0, 0, 91, 0xF7]));   // SET AMP parameter 0 = 91
s = await state();
ok(s.preset.p[5][0] === 91 && s.dirty === 1, "sysex SET (a JSON array body): AMP's first setting = 91, seen in /api/state");

// ---- 7. the virtual Web MIDI: the shim, then the real editor code on it ----
async function openShim(url) {
  const sandbox = {
    document: { currentScript: { src: `${url}/sim/webmidi-shim.js` } }, navigator: {}, fetch, console, Uint8Array, DOMException, URL,
    EventSource: class {                              // the browser's EventSource, over fetch (Node has none before 22)
      constructor(u) {
        this.l = new Map(); this.ctl = new AbortController();
        fetch(u, { signal: this.ctl.signal }).then(async (res) => {
          let buf = "";
          for await (const c of res.body) {
            buf += Buffer.from(c).toString("utf8");
            for (let i; (i = buf.indexOf("\n\n")) >= 0;) {
              const blk = buf.slice(0, i); buf = buf.slice(i + 2);
              const ev = /^event: (.*)$/m.exec(blk), data = /^data: (.*)$/m.exec(blk);
              if (ev) for (const f of this.l.get(ev[1]) || []) f({ data: data ? data[1] : "" });
            }
          }
        }).catch(() => { });
      }
      addEventListener(n, f, o) { const fn = o && o.once ? (e) => { this.l.get(n).delete(fn); f(e); } : f; (this.l.get(n) || this.l.set(n, new Set()).get(n)).add(fn); }
      close() { this.ctl.abort(); }
    },
  };
  createContext(sandbox);
  runInContext(readFileSync(join(ROOT, "tools/sim/webmidi-shim.js"), "utf8"), sandbox);
  return sandbox;
}
let shim = await openShim(sim.url);
let access = await shim.navigator.requestMIDIAccess({ sysex: true });
ok(access.inputs.size === 1 && access.outputs.size === 1 && [...access.inputs.values()][0].name === "OpenPocket", "shim: requestMIDIAccess gives one input and one output, \"OpenPocket\"");
const ed = new Editor(access);
const inf = await ed.connect();
ok(inf && inf.version === `v${VER}` && inf.identity === `PocketX_9${VER}`, "editor (pxedit.js) over the shim: INFO");
const cat = await ed.catalog();
ok(cat.blocks.length === 12 && cat.blocks[5].label === "AMP", "editor: the catalogue");
await ed.setParam(5, 1, 33);
s = await state();
ok(s.preset.p[5][1] === 33, "editor: SET reaches the simulated pedal (seen in /api/state)");
await act("/api/press/fs_up");
const st = await ed.state();
ok(st.slot === s.slot + 1, "editor: STATE follows a footswitch pressed over HTTP");
ed.close();

// ---- 8. the update simulator: the installer's identify over the shim ----
execFileSync("python3", [join(ROOT, "tests", "fixtures.py"), TMP], { stdio: "ignore" });
await sim.close();
sim = await startSim({ port: 0, update: join(TMP, "flash_old.bin") });
info = await get("/api/info");
ok(info.kind === "update" && info.port === "OpenPocket" && info.log.some((l) => /PocketX_923/.test(l)), "--update: devsim runs the update code on the flash image (OpenPocket 923)");
ok((await fetch(sim.url + "/api/state")).status === 409, "--update: no screen or controls");
shim = await openShim(sim.url);
access = await shim.navigator.requestMIDIAccess({ sysex: true });
const up = new Updater(access, () => { });
const dev = await up.identify();
ok(dev && dev.id && dev.id.text === "PocketX_923", `installer (pxota.js) over the shim: found ${dev && dev.id && dev.id.text}`);
await sim.close();

rmSync(TMP, { recursive: true, force: true });
console.log(failed ? `\n${failed} FAILED` : "\nsimulator: ok");
process.exit(failed ? 1 : 0);
