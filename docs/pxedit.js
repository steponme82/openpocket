// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the web editor's protocol over WebMIDI (docs/EDITOR-PROTOCOL.md; the pedal side: firmware/px_editor.h).
// Request / reply, one at a time; STATE frames the pedal pushes while watched go to onState.

import { PORT_RE } from "./pxota.js";
import { pack7, SLOT_BYTES } from "./pxnam.js";
import { IR_BYTES, IR_CHUNK } from "./pxir.js";

const H = [0x7D, 0x4F, 0x50];
export const C = { INFO: 1, CAT_BLOCK: 2, CAT_TYPE: 3, TEXTS: 4, STATE: 5, SET: 6, ORDER: 7, NAME: 8, PUT: 9, SELECT: 10,
  FOCUS: 11, LIST: 12, SLOT_GET: 13, SLOT_PUT: 14, COMMIT: 15, SAVE: 16, WATCH: 17, PING: 18,
  NAM_LIST: 19, NAM_WRITE: 20, NAM_COMMIT: 21, NAM_ERASE: 22, DIAG: 23, IR_LIST: 24, IR_WRITE: 25, IR_COMMIT: 26, IR_ERASE: 27,
  USB_AUDIO: 28, MIDI_IN: 29, MAP: 30, NAM_RENAME: 31, PL: 32, SCENE: 33 };
export const NAM_CHUNK = 112;     // bytes per NAM_WRITE (firmware/px_models.h NM_CHUNK)
// test build 50: a scene's name as the pedal keeps it: 3 characters of ASCII 32..126 at most, in capitals as the pedal draws them
export const cleanSceneName = (s) => String(s ?? "").toUpperCase().replace(/[^\x20-\x7E]/g, "").trim().slice(0, 3).trim();
export const P_SIZE = 110;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
function fail(code, msg) { const e = new Error(msg); e.code = code; return e; }

// a reply's arguments, read in order
class Reader {
  constructor(a) { this.a = a; this.i = 0; }
  b() { if (this.i >= this.a.length) throw fail("short", "reply too short"); return this.a[this.i++]; }
  s() { let t = ""; for (;;) { const c = this.b(); if (!c) return t; t += String.fromCharCode(c); } }
  bytes(n) { const r = this.a.slice(this.i, this.i + n); if (r.length < n) throw fail("short", "reply too short"); this.i += n; return r; }
}

// preset <-> the 110 bytes of px_preset_t
export function decodePreset(b, nblk = 12, npar = 5) {
  let i = 0;
  const name = String.fromCharCode(...b.slice(0, 14)).replace(/\0.*$/s, "");
  i = 14;
  const on = [...b.slice(i, i + nblk)].map((x) => x ? 1 : 0); i += nblk;
  const type = [...b.slice(i, i + nblk)]; i += nblk;
  const p = []; for (let k = 0; k < nblk; k++) { p.push([...b.slice(i, i + npar)]); i += npar; }
  const order = [...b.slice(i, i + nblk)];
  return { name, on, type, p, order };
}
export function encodePreset(q, nblk = 12, npar = 5) {
  const out = [];
  const nm = cleanName(q.name);
  for (let i = 0; i < 14; i++) out.push(i < nm.length ? nm.charCodeAt(i) : 0);
  for (let k = 0; k < nblk; k++) out.push(q.on[k] ? 1 : 0);
  for (let k = 0; k < nblk; k++) out.push(q.type[k] & 0x7F);
  for (let k = 0; k < nblk; k++) for (let j = 0; j < npar; j++) out.push(Math.max(0, Math.min(100, q.p[k][j] | 0)));
  for (let k = 0; k < nblk; k++) out.push(q.order[k] & 0x7F);
  return out;
}
export function cleanName(s, max = 13) {
  return String(s || "").replace(/[^\x20-\x7E]/g, "?").slice(0, max);
}
export const clonePreset = (q) => ({ name: q.name, on: [...q.on], type: [...q.type], p: q.p.map((r) => [...r]), order: [...q.order] });

export class Editor {
  constructor(access, opts = {}) {
    this.access = access;
    this.timeout = opts.timeout || 1500;
    this.onState = null;          // (state) => {}: pushed by the pedal
    this.onLost = null;           // () => {}: the pedal stopped answering
    this.onMidi = null;           // (bytes) => {}: a musical message the pedal sent (docs/MIDI.md), not SysEx
    this.onMidiIn = null;         // ({ type: "cc" | "pc", n1, n2, count }) => {}: protocol 5, what a controller sent the pedal
    this.link = null;
    this.queue = Promise.resolve();
    this.waiting = null;          // { cmd, resolve, reject, timer }
    this.info = null;
    this.cat = null;
  }

  // ---- transport ----
  _onMessage(data) {
    const d = data instanceof Uint8Array ? data : Uint8Array.from(data);
    if (d.length && d[0] !== 0xF0) { if (this.onMidi) { try { this.onMidi(d); } catch (_) { } } return; }
    if (d.length < 6 || d[0] !== 0xF0 || d[1] !== H[0] || d[2] !== H[1] || d[3] !== H[2] || d[d.length - 1] !== 0xF7) return;
    const cmd = d[4], args = d.slice(5, d.length - 1);
    if (this.waiting && this.waiting.cmd === cmd) {
      const w = this.waiting; this.waiting = null; clearTimeout(w.timer); w.resolve(args);
      return;
    }
    if (cmd === C.STATE && this.onState) { try { this.onState(this._state(args)); } catch (_) { } }
    if (cmd === C.MIDI_IN && this.onMidiIn && args.length >= 4) {
      try { this.onMidiIn({ type: args[0] === 1 ? "pc" : "cc", n1: args[1], n2: args[2], count: args[3] }); } catch (_) { }
    }
  }
  _send(cmd, args) {
    if (!this.link) throw fail("closed", "not connected");
    this.link.output.send([0xF0, ...H, cmd, ...args.map((x) => x & 0x7F), 0xF7]);
  }
  _once(cmd, args, timeout) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.waiting = null; reject(fail("timeout", "the pedal did not answer")); }, timeout);
      this.waiting = { cmd, resolve, reject, timer };
      try { this._send(cmd, args); } catch (e) { clearTimeout(timer); this.waiting = null; reject(e); }
    });
  }
  // one request at a time; one retry after a timeout
  request(cmd, args = [], timeout = this.timeout) {
    const run = async () => {
      try { return await this._once(cmd, args, timeout); }
      catch (e) {
        if (e.code !== "timeout") throw e;
        await sleep(50);
        try { return await this._once(cmd, args, timeout); }
        catch (e2) { if (e2.code === "timeout" && this.onLost) this.onLost(); throw e2; }
      }
    };
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }

  // the first OpenPocket port that answers INFO
  async connect() {
    const outs = [...this.access.outputs.values()];
    for (const input of this.access.inputs.values()) {
      if (input.state === "disconnected" || !PORT_RE.test(input.name || "")) continue;
      const output = outs.find((o) => o.name === input.name) || (outs.length === 1 ? outs[0] : null);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      this.link = { input, output };
      input.onmidimessage = (ev) => this._onMessage(ev.data);
      try {
        const r = new Reader(await this.request(C.INFO, [], 600));
        const info = { proto: r.b(), version: r.s(), identity: r.s(), nblk: r.b(), npar: r.b(), nslot: r.b(), nameMax: r.b(), port: input.name };
        if (info.proto >= 1) { this.info = info; return info; }
      } catch (_) { }
      input.onmidimessage = null;
      this.link = null;
    }
    return null;
  }
  // the log channel: its text lines; a timeout when this firmware has none (only the Bluetooth test image answers)
  async diag() {
    const r = new Reader(await this.request(C.DIAG, [], 600));
    return Array.from({ length: r.b() }, () => r.s());
  }
  close() {
    if (this.link) { try { this.link.input.onmidimessage = null; } catch (_) { } }
    this.link = null;
  }

  // ---- the catalogue: blocks, types, parameter names and value texts (cached per firmware) ----
  async catalog(progress = () => {}) {
    const key = `openpocket-cat-${this.info.identity}-${this.info.version}`;
    try { const c = JSON.parse(localStorage.getItem(key) || "null"); if (c && c.blocks && c.blocks.length === this.info.nblk) { this.cat = c; if (this.hasNam) await this.namTexts(); if (this.hasIr) await this.irTexts(); return c; } } catch (_) { }
    const blocks = [];
    for (let b = 0; b < this.info.nblk; b++) {
      const r = new Reader(await this.request(C.CAT_BLOCK, [b]));
      r.b();
      const label = r.s(), name = r.s(), ntypes = r.b();
      const c = r.b() | r.b() << 7 | r.b() << 14;
      const rgb = `#${(((c >> 11) & 31) * 255 / 31 | 0).toString(16).padStart(2, "0")}${(((c >> 5) & 63) * 255 / 63 | 0).toString(16).padStart(2, "0")}${((c & 31) * 255 / 31 | 0).toString(16).padStart(2, "0")}`;
      blocks.push({ label, name, color: rgb, types: new Array(ntypes) });
    }
    const total = blocks.reduce((s, b) => s + b.types.length, 0);
    let done = 0;
    for (let b = 0; b < blocks.length; b++) {
      for (let t = 0; t < blocks[b].types.length; t++) {
        const r = new Reader(await this.request(C.CAT_TYPE, [b, t]));
        r.b(); r.b();
        const name = r.s(), np = r.b(), mask = r.b();
        const params = [];
        for (let k = 0; k < np; k++) params.push({ name: r.s(), texts: null });
        for (let k = 0; k < np; k++) {
          if (!(mask >> k & 1)) continue;
          const q = new Reader(await this.request(C.TEXTS, [b, t, k]));
          q.b(); q.b(); q.b();
          const texts = []; for (let v = 0; v <= 100; v++) texts.push(q.s());
          params[k].texts = texts;
        }
        blocks[b].types[t] = { name, params };
        progress(++done / total);
      }
    }
    this.cat = { blocks };
    try { localStorage.setItem(key, JSON.stringify(this.cat)); } catch (_) { }
    if (this.hasNam) await this.namTexts();
    if (this.hasIr) await this.irTexts();
    return this.cat;
  }
  // v37: the AMP NAM type's MODEL texts name the user models, which change: always asked again (not cached)
  async namTexts() {
    const b = this.cat.blocks.findIndex((x) => x.label === "AMP");
    const t = b < 0 ? -1 : this.cat.blocks[b].types.findIndex((x) => x && x.name === "NAM");
    if (t < 0) return;
    const q = new Reader(await this.request(C.TEXTS, [b, t, 0]));
    q.b(); q.b(); q.b();
    const texts = []; for (let v = 0; v <= 100; v++) texts.push(q.s());
    this.cat.blocks[b].types[t].params[0].texts = texts;
  }

  // ---- the current sound ----
  _state(args) {
    const r = new Reader(args);
    const slot = r.b(), flags = r.b(), focus = r.b();
    return { slot, dirty: !!(flags & 1), saved: !!(flags & 2), focus, preset: decodePreset(r.bytes(P_SIZE), this.info.nblk, this.info.npar) };
  }
  async state() { return this._state(await this.request(C.STATE)); }
  async set(block, what, k, v) { const r = await this.request(C.SET, [block, what, k, v]); return r[3]; }
  setParam(block, k, v) { return this.set(block, 0, k, v); }
  setOn(block, on) { return this.set(block, 1, 0, on ? 1 : 0); }
  setType(block, t) { return this.set(block, 2, 0, t); }
  async order(order) { return (await this.request(C.ORDER, order))[0]; }
  async name(s) { return new Reader(await this.request(C.NAME, [...cleanName(s, this.info.nameMax)].map((c) => c.charCodeAt(0)).concat([0]))).s(); }
  async put(q) { return (await this.request(C.PUT, encodePreset(q, this.info.nblk, this.info.npar)))[0]; }
  async select(slot) { return (await this.request(C.SELECT, [slot]))[1]; }
  async focus(block) { await this.request(C.FOCUS, [block]); }

  // ---- the slots ----
  async list() {
    const out = [];
    for (let s = 0; s < this.info.nslot; s += 26) {
      const r = new Reader(await this.request(C.LIST, [s, Math.min(26, this.info.nslot - s)]));
      r.b(); const n = r.b();
      for (let i = 0; i < n; i++) out.push({ slot: s + i, saved: !!r.b(), name: r.s() });
    }
    return out;
  }
  async slotGet(slot) {
    const r = new Reader(await this.request(C.SLOT_GET, [slot]));
    r.b(); const saved = !!r.b();
    return { slot, saved, preset: decodePreset(r.bytes(P_SIZE), this.info.nblk, this.info.npar) };
  }
  // q null: back to the factory preset. commit: write the flash now
  async slotPut(slot, q, commit) {
    const args = [slot, (q ? 1 : 0) | (commit ? 2 : 0)];
    if (q) args.push(...encodePreset(q, this.info.nblk, this.info.npar));
    return (await this.request(C.SLOT_PUT, args, commit ? 4000 : this.timeout))[1];
  }
  async commit() { return (await this.request(C.COMMIT, [], 4000))[0]; }
  async save(slot) { return (await this.request(C.SAVE, [slot], 4000))[1]; }
  // (protocol 5: bit 1 also asks for MIDI_IN, what a controller sends the pedal, pushed while watched)
  async watch(on) { return (await this.request(C.WATCH, [on ? (this.hasPedalLearn ? 3 : 1) : 0]))[0]; }
  async ping() {
    const r = await this.request(C.PING), p = { load: r[0], bypass: !!(r[1] & 1), c1: !!(r[1] & 2) };
    if (r.length >= 8) Object.assign(p, { peak: r[2], peakAll: r[3], xrun: r[4] | r[5] << 7, clip: r[6] | r[7] << 7 });   // v40.1
    if (r.length >= 12) Object.assign(p, { stackFree: (r[8] | r[9] << 7) * 4, irqStackFree: (r[10] | r[11] << 7) * 4 });
    // v40.2: then a count and that many 2-byte figures (the DSP profile, tools/dsp.mjs): the stack figures end before it
    const base = [12, 16].find((b) => r.length > b && r.length === b + 1 + 2 * r[b]);
    if ((base ?? r.length) >= 16) Object.assign(p, { bleHeapFree: (r[12] | r[13] << 7) * 4, bleTaskStackFree: (r[14] | r[15] << 7) * 4 });
    if (base) {                   // each block's share of an audio block's time (%), in the block order, then the NAM network's
      const f = [], n = this.info ? this.info.nblk : 12;
      for (let i = base + 1; i + 1 < r.length; i += 2) f.push(r[i] | r[i + 1] << 7);
      p.prof = f.slice(0, n); p.profNam = f[n];
    }
    return p;
  }
  // v37 (protocol 2): NAM models. list -> { nfact, nuser, models: [{ index, user, slot, used, name }] }
  get hasNam() { return !!(this.info && this.info.proto >= 2); }
  async namList() {
    const r = new Reader(await this.request(C.NAM_LIST));
    const nfact = r.b(), nuser = r.b(), models = [];
    for (let i = 0; i < nfact + nuser; i++) {
      const used = !!r.b(), name = r.s();
      models.push({ index: i, user: i >= nfact, slot: i - nfact, used, name });
    }
    return { nfact, nuser, models };
  }
  // a packed model (pxnam.js packModel) into user slot `slot`: pieces, then the flash write. rc 0 ok, 1 invalid, 2 flash
  async namUpload(slot, bytes, progress = () => {}) {
    if (bytes.length !== SLOT_BYTES) throw new Error("model: wrong size");
    const n = Math.ceil(bytes.length / NAM_CHUNK);
    for (let i = 0; i < n; i++) {
      const piece = bytes.slice(i * NAM_CHUNK, (i + 1) * NAM_CHUNK);
      const r = await this.request(C.NAM_WRITE, [i & 0x7F, i >> 7, ...pack7(piece)]);
      if (r[2]) throw new Error(`model: piece ${i} refused`);
      progress((i + 1) / (n + 1));
    }
    const rc = (await this.request(C.NAM_COMMIT, [slot], 6000))[1];
    progress(1);
    return rc;
  }
  async namErase(slot) { return (await this.request(C.NAM_ERASE, [slot], 6000))[1]; }
  // test build 40 (protocol 5): a user slot's name only, on the pedal (11 characters at most) -> rc (0 ok, 1 nothing to
  // rename, 2 flash)
  get hasNamRename() { return !!(this.info && this.info.proto >= 5); }
  async namRename(slot, name) {
    const b = [...String(name).slice(0, 11)].map((c) => c.charCodeAt(0) & 0x7F);
    return (await this.request(C.NAM_RENAME, [slot, ...b, 0], 6000))[1];
  }
  // v38 (protocol 3): cabinet IRs. list -> [{ slot, used, name }]
  get hasIr() { return !!(this.info && this.info.proto >= 3); }
  async irList() {
    const r = new Reader(await this.request(C.IR_LIST));
    const n = r.b(), out = [];
    for (let i = 0; i < n; i++) { const used = !!r.b(), name = r.s(); out.push({ slot: i, used, name }); }
    return out;
  }
  // a packed IR (pxir.js packIr) into slot `slot`: pieces, then the flash write. rc 0 ok, 1 invalid, 2 flash
  async irUpload(slot, bytes, progress = () => {}) {
    if (bytes.length !== IR_BYTES) throw new Error("IR: wrong size");
    const n = Math.ceil(bytes.length / IR_CHUNK);
    for (let i = 0; i < n; i++) {
      const piece = bytes.slice(i * IR_CHUNK, (i + 1) * IR_CHUNK);
      const r = await this.request(C.IR_WRITE, [i & 0x7F, i >> 7, ...pack7(piece)]);
      if (r[2]) throw new Error(`IR: piece ${i} refused`);
      progress((i + 1) / (n + 1));
    }
    const rc = (await this.request(C.IR_COMMIT, [slot], 6000))[1];
    progress(1);
    return rc;
  }
  async irErase(slot) { return (await this.request(C.IR_ERASE, [slot], 6000))[1]; }
  // v42 (protocol 4): USB AUDIO, the pedal as a USB sound card (docs/USB-AUDIO.md). usbAudio() reads the setting and
  // the streams; usbAudio(true / false) sets it first: the pedal then leaves USB for a second and comes back as a
  // device with or without the audio streams, so this connection ends (connect again after about 3 s).
  // -> { on, rc (0 ok, 1 not in this firmware, 2 flash), recording, flowing, playing, sounding, counts: {...} }
  get hasUsbAudio() { return !!(this.info && this.info.proto >= 4); }
  async usbAudio(on) {
    const r = await this.request(C.USB_AUDIO, on === undefined ? [] : [on ? 1 : 0], 3000);
    const n = r[3] || 0, v = [];
    for (let i = 0; i < n && 4 + 4 * i + 3 < r.length; i++) { const k = 4 + 4 * i; v.push(r[k] | r[k + 1] << 7 | r[k + 2] << 14 | r[k + 3] << 21); }
    const names = ["recStarts", "recPackets", "recFrames", "recEmpty", "recFull", "recNoPacket", "recStalls", "recLonger", "recShorter",
      "recFillLow", "recFillHigh", "playStarts", "playPackets", "playFrames", "playEmpty", "playFull", "blockTicks", "recSkipped", "playSkipped", "blockSwaps"];
    return { on: !!r[0], rc: r[1], recording: !!(r[2] & 1), flowing: !!(r[2] & 2), playing: !!(r[2] & 4), sounding: !!(r[2] & 8),
      counts: Object.fromEntries(names.map((k, i) => [k, v[i] ?? 0])) };
  }
  // the CAB USER IR type's IR texts name the slots, which change: always asked again (not cached)
  // test build 39 (protocol 5, the Bluetooth audio image): controls learned on the pedal (firmware/px_midi.h mid_map) and
  // SWITCH (CC 8..19 flip on every message: "each"), INPUT (-10..+10 dB), (test build 40) TAILS and BT POWER and (test build
  // 42) OUTPUT, (test build 43) USB audio's routing. pedalMap() reads them; setPedalMap(each, entries, inputDb, tails, btPower,
  // outMode, usb) writes all of them and the flash (the sound holds a moment, as a save). entries: PEDAL_MAP_N of { src, dst,
  // mode } or null; mode 0 press, 1 each, 2 value (docs/pxmidi.js). usb: { send (0 OUTPUT, 1 DI, 2 DI + OUT), ret (0 OUTPUT,
  // 1 INPUT, 2 + b before block b), cut (the guitar cut from there while the computer plays) }, null from a pedal before
  // test build 43 (and left as it is when null is written).
  // -> { rc (0 ok, 1 invalid, 2 flash), each, inputDb, tails, btPower, outMode, entries, usb, more }
  // Protocol 6 (test builds 46 to 49): more, after the routing: { tone: GUITAR TONE (0 NATURAL, 1 SMOOTH, 2 WARM, 3 WARMER,
  // 4 PUNCHY, 5 DARK, 6 DARKER, 7 MUDDY, 8 BRIGHT), pickup: PICKUP (0 SINGLE, 1 HUMBUCK, 2 P90), view: PLAY VIEW (0 CHAIN,
  // 1 SWITCHES, 2 SCENES), sceneWins: SCENE+SWITCH (false SWITCH WINS), feetPresets: SCENE FOOTSW (false SCENES) }, null before
  get hasPedalLearn() { return !!(this.info && this.info.proto >= 5); }
  _map(r) {
    const q = new Reader(r), rc = q.b(), each = !!q.b(), inputDb = q.b() - 10, fl = q.b(), n = q.b(), entries = [];
    for (let i = 0; i < n; i++) { const on = q.b(), src = q.b(), dst = q.b(), mode = q.b(); entries.push(on ? { src, dst, mode } : null); }
    const ur = q.i < q.a.length ? q.b() : -1;   // (test build 43: after the entries)
    const tb = q.i < q.a.length ? q.b() : -1, vb = q.i < q.a.length ? q.b() : -1;   // (protocol 6: then two more)
    return { rc, each, inputDb, tails: !!(fl & 1), btPower: (fl >> 1) & 3, outMode: (fl >> 3) & 3, entries,
      usb: ur < 0 ? null : { send: ur & 3, cut: !!(ur & 4), ret: (ur >> 3) & 15 },
      more: tb < 0 || vb < 0 ? null : { tone: tb & 15, pickup: (tb >> 4) & 3, view: vb & 3, sceneWins: !!(vb & 4), feetPresets: !!(vb & 8) } };
  }
  async pedalMap() { return this._map(await this.request(C.MAP)); }
  // inputDb: SETTINGS' INPUT, -10..+10 dB (the guitar's level into the chain); tails: TAILS; btPower: 0 HIGH, 1 MID, 2 LOW;
  // outMode: OUTPUT, 0 MONO, 1 STEREO (DELAY's and REVERB's wet to the sides), 2 GTR+BASS (the guitar left, LOW OCTAVE's octave right)
  // more (protocol 6): as _map gives it; null leaves those settings as they are (the routing must then be sent too: usb)
  async setPedalMap(each, entries, inputDb = 0, tails = false, btPower = 0, outMode = 0, usb = null, more = null) {
    const a = [1, each ? 1 : 0, Math.max(0, Math.min(20, Math.round(inputDb) + 10)),
      (tails ? 1 : 0) | (Math.max(0, Math.min(2, btPower | 0)) << 1) | (Math.max(0, Math.min(2, outMode | 0)) << 3)];
    for (const e of entries) a.push(...(e ? [1, e.src, e.dst, e.mode] : [0, 0, 0, 0]));
    if (usb) a.push((Math.max(0, Math.min(2, usb.send | 0))) | (usb.cut ? 4 : 0) | (Math.max(0, Math.min(13, usb.ret | 0)) << 3));
    if (usb && more) a.push(Math.max(0, Math.min(8, more.tone | 0)) | Math.max(0, Math.min(2, more.pickup | 0)) << 4,
      Math.max(0, Math.min(2, more.view | 0)) | (more.sceneWins ? 4 : 0) | (more.feetPresets ? 8 : 0));
    return this._map(await this.request(C.MAP, a, 4000));
  }
  // protocol 6 (test builds 46 to 49): a preset's own controls learned on the pedal (firmware/px_midi.h mid_pl), which its rows
  // answer to before the ones above. presetMap(slot) reads them; presetPut(slot, e) learns one, in place of the preset's entry
  // for the same control or the same row; presetForget(slot, src) forgets one. Each writes the flash (as a save; an unsaved
  // sound stays as it is). An entry: { src, dst, mode, lo, hi, clo, chi } (docs/pxmidi.js). One entry a request: a request
  // over Bluetooth MIDI is 160 bytes at most
  // -> { rc (0 ok, 1 invalid, 2 flash, 3 no place left), slot, used (every preset's), places, entries }
  get hasPresetLearn() { return !!(this.info && this.info.proto >= 6); }
  _pl(r) {
    const q = new Reader(r), rc = q.b(), slot = q.b(), used = q.b(), places = q.b(), n = q.b(), entries = [];
    for (let i = 0; i < n; i++) entries.push({ src: q.b(), dst: q.b(), mode: q.b(), lo: q.b(), hi: q.b(), clo: q.b(), chi: q.b() });
    return { rc, slot, used, places, entries };
  }
  async presetMap(slot) { return this._pl(await this.request(C.PL, [slot])); }
  async presetPut(slot, e) { return this._pl(await this.request(C.PL, [slot, 1, e.src, e.dst, e.mode, e.lo, e.hi, e.clo, e.chi], 4000)); }
  async presetForget(slot, src) { return this._pl(await this.request(C.PL, [slot, 2, src], 4000)); }
  // protocol 6 (test build 49): the playing preset's four scenes (firmware/px_midi.h px_sc), each the blocks it turns on (a
  // bit each, in the catalogue's block order). The scene playing is the blocks as they are now. Nothing here writes the
  // flash: SAVE keeps them with the sound, as on the pedal.
  // -> { rc (0 ok, 1 invalid), cur (the scene playing), set (the scenes stored, a bit each), masks [4], names [4] (test build
  //    50: each scene's name, up to 3 characters, "" none; null from a pedal before it), saved: { cur, set, masks, names } (as
  //    saved with the preset) }
  get hasScenes() { return !!(this.info && this.info.proto >= 6); }
  _sc(r) {
    const q = new Reader(r), rc = q.b();
    const one = () => { const cur = q.b(), set = q.b(), masks = []; for (let i = 0; i < 4; i++) masks.push(q.b() | q.b() << 7); return { cur, set, masks }; };
    const now = one(), saved = one();
    const names = () => {                       // (test build 50: after what 49 sent; 3 bytes a scene, a name ends at its first 0)
      if (q.i + 12 > q.a.length) return null;
      return Array.from({ length: 4 }, () => { const b = [q.b(), q.b(), q.b()], z = b.indexOf(0); return String.fromCharCode(...(z < 0 ? b : b.slice(0, z))); });
    };
    const nn = names(), sn = nn && names();
    return { rc, ...now, names: nn, saved: { ...saved, names: sn } };
  }
  async scenes() { return this._sc(await this.request(C.SCENE)); }
  async sceneRecall(n) { return this._sc(await this.request(C.SCENE, [1, n])); }          // (as CC 65 + n)
  async sceneSet(n, mask) { return this._sc(await this.request(C.SCENE, [2, n, mask & 0x7F, (mask >> 7) & 0x1F])); }   // (stored; the
  // scene playing switches its blocks to it)
  async sceneForget(n) { return this._sc(await this.request(C.SCENE, [3, n])); }          // (it takes the blocks as they are when next recalled)
  // test build 50: scene n's name, as cleanSceneName leaves it ("" none: the pedal shows SCENE n); a change until SAVE
  async sceneName(n, text) {
    const t = cleanSceneName(text);
    return this._sc(await this.request(C.SCENE, [4, n, ...[0, 1, 2].map((i) => (i < t.length ? t.charCodeAt(i) : 0))]));
  }
  async irTexts() {
    const b = this.cat.blocks.findIndex((x) => x.label === "CAB");
    const t = b < 0 ? -1 : this.cat.blocks[b].types.findIndex((x) => x && x.name === "USER IR");
    if (t < 0) return;
    const q = new Reader(await this.request(C.TEXTS, [b, t, 0]));
    q.b(); q.b(); q.b();
    const texts = []; for (let v = 0; v <= 100; v++) texts.push(q.s());
    this.cat.blocks[b].types[t].params[0].texts = texts;
  }
}

// ---- preset files (JSON, blocks and types by name) ----
export function presetToJSON(q, cat, slot) {
  return {
    slot: slot === undefined ? undefined : slot + 1,
    v: 36,                                                       // v36 (preset LEVEL); v35 scales (delay TIME up to 2 s)
    name: q.name,
    blocks: cat.blocks.map((b, i) => ({ block: b.label, on: !!q.on[i], type: b.types[q.type[i]] ? b.types[q.type[i]].name : q.type[i], params: [...q.p[i]] })),
    order: q.order.map((i) => cat.blocks[i] ? cat.blocks[i].label : i),
  };
}
export function filePresets(presets) { return { format: "openpocket-presets", version: 1, presets }; }

// v34: amps, drives and cabs were replaced by fitted models (docs/FITTED-MODELS.md); a preset file from v32 / v33
// names the old types: each one -> the nearest new type, the same choice the pedal makes for its saved presets (px_type_v34)
export const V33_TYPES = {"AMP": {"TWIN CLEAN": "TWIN", "DELUXE 65": "TWIN", "PRINCETONE": "TWIN", "TWEED 59": "SHIRLEY", "TWEED CHAMP": "AC15 TB", "AC 30 TB": "AC15 TB", "AC 15": "AC15 TB", "JTM 45": "SHIRLEY", "PLEXI 68": "SHIRLEY", "BRIT 800": "JCM900", "BRIT JVM": "JCM2000", "HIWATT DR": "JCM900", "ORANGE 120": "JCM2000", "CHIME 30": "AC15 TB", "ODS LEAD": "MARK IV", "SLO 100": "PRO MOD", "MARK IIC+": "MARK IV", "RECTIFIED": "5150", "5150 LEAD": "5150", "5150 III": "5150", "ECSTASY": "PRO MOD", "BE LEAD": "PRO MOD", "UBER VH4": "5150", "EURO BALL": "5150", "REVV GEN": "5150", "JC CLEAN": "TWIN", "SUPRO 64": "AC15 TB", "TWO-ROCK": "TWIN", "SVT BASS": "TWIN"}, "DRV": {"TS 808": "TS9", "TS 9": "TS9", "SD-1 OD": "TS9", "KLONE": "KLON", "BD BLUES": "KLON", "OCD DRIVE": "M77 OD", "RAT DIST": "SWEDE HM-2", "DS-1 DIST": "SWEDE HM-2", "DIST PLUS": "M77 OD", "BLUESBREAKER": "KLON", "KING OF TONE": "KLON", "TIMMY": "KLON", "ZEN DRIVE": "TS9", "MUFF PI": "M77 OD", "RAMS HEAD": "M77 OD", "GREEN RUSSIAN": "M77 OD", "FACE GERMAN.": "M77 OD", "FACE SILICON": "M77 OD", "BENDER MK2": "M77 OD", "OCTAFUZZ": "M77 OD", "FUZZ FACTORY": "M77 OD", "TREBLE BOOST": "M77 OD", "EP BOOST": "KLON", "METAL ZONE": "SWEDE HM-2", "PRECISION": "TS9", "BASS DI 7K": "KLON"}, "CAB": {"1x10 OPEN": "1x12 US OPEN", "2x10 VIBRO": "1x12 US OPEN", "2x12 BRIT": "4x12 GREEN", "4x10 TWEED": "1x12 US OPEN", "4x12 H30": "4x12 V30", "1x15 OPEN": "1x12 US OPEN", "8x10 BASS": "4x12 V30", "4x10 HORN": "1x12 US OPEN"}};

// v35: delay TIME 20..1000 ms in 100 steps became 20..1000 ms in 80 steps, then up to 2 s (px_dly_time_v35)
export const dlyTimeV35 = (v) => Math.min(80, Math.floor(v * (9.8 / 12.25) + 0.5));

// a preset from its JSON; problems (unknown block or type) are listed, the rest still comes through
export function presetFromJSON(o, cat, base) {
  const q = clonePreset(base), warn = [];
  q.name = cleanName(o.name || q.name);
  const byLabel = new Map(cat.blocks.map((b, i) => [b.label, i]));
  for (const ob of Array.isArray(o.blocks) ? o.blocks : []) {
    const i = byLabel.get(ob.block);
    if (i === undefined) { warn.push(`unknown block ${ob.block}`); continue; }
    q.on[i] = ob.on ? 1 : 0;
    let t = typeof ob.type === "number" ? ob.type : cat.blocks[i].types.findIndex((x) => x.name === ob.type);
    const moved = typeof ob.type === "string" && t < 0 && V33_TYPES[ob.block] && V33_TYPES[ob.block][ob.type];
    if (moved) {
      t = cat.blocks[i].types.findIndex((x) => x.name === moved);
      if (t >= 0) warn.push(`${ob.block}: ${ob.type} (before v34) -> ${moved}`);
    }
    if (t < 0 || t >= cat.blocks[i].types.length) { warn.push(`${ob.block}: unknown type ${ob.type}`); t = q.type[i]; }
    q.type[i] = t;
    if (Array.isArray(ob.params)) for (let k = 0; k < q.p[i].length; k++) {
      const v = Number(ob.params[k]);
      if (Number.isFinite(v)) q.p[i][k] = Math.max(0, Math.min(100, Math.round(v)));
    }
    if (!(o.v >= 36) && ob.block === "GT") q.p[i][2] = 50;           // before v36: no preset LEVEL (0 dB)
    if (!(o.v >= 35) && ob.block === "DLY" && cat.blocks[i].types[q.type[i]] && cat.blocks[i].types[q.type[i]].name !== "REVERSE") {
      const old = q.p[i][0], now = dlyTimeV35(old);                // a file from before v35: TIME was 20..1000 ms
      if (now !== old) { q.p[i][0] = now; warn.push(`DLY: TIME ${old} (before v35) -> ${now}, same delay`); }
    }
  }
  if (Array.isArray(o.order)) {
    const ord = o.order.map((l) => typeof l === "number" ? l : byLabel.get(l));
    if (ord.length === q.order.length && new Set(ord).size === ord.length && ord.every((x) => x !== undefined && x >= 0 && x < q.order.length)) q.order = ord;
    else warn.push("block order not usable: default order kept");
  }
  return { preset: q, warn };
}
export function parsePresetFile(text) {
  const o = JSON.parse(text);
  if (o && o.format === "openpocket-presets" && Array.isArray(o.presets)) return o.presets;
  if (o && Array.isArray(o.blocks)) return [o];                // a bare preset
  throw fail("format", "not an OpenPocket preset file");
}
