// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket web editor: musical MIDI. The pedal's own Control Change map (docs/MIDI.md, firmware/px_midi.h: the same
// numbers and the same scaling), a decoder for the messages a port delivers, and the editor's learned mappings: a control
// of another MIDI device (a foot controller plugged into the computer) -> something on the pedal. No DOM, no port here.

export const NBLK = 12, NPAR = 5;
export const CC_MASTER = 7, CC_TOGGLE0 = 8, CC_ON0 = 20, CC_TYPE0 = 52;

// 60 settings over three runs of free numbers (px_cc_param)
export function ccParam(b, k) {
  const i = b * NPAR + k;
  return i < 5 ? 33 + i : i < 18 ? 34 + i : i < 46 ? 70 + (i - 18) : 102 + (i - 46);
}
export const ccOn = (b) => CC_ON0 + b;
export const ccToggle = (b) => CC_TOGGLE0 + b;
export const ccType = (b) => CC_TYPE0 + b;

// what a Control Change number means to the pedal: { kind: "master" | "toggle" | "on" | "type" | "param", b, k } or null
export function ccTarget(cc) {
  if (cc === CC_MASTER) return { kind: "master" };
  if (cc >= CC_TOGGLE0 && cc < CC_TOGGLE0 + NBLK) return { kind: "toggle", b: cc - CC_TOGGLE0 };
  if (cc >= CC_ON0 && cc < CC_ON0 + NBLK) return { kind: "on", b: cc - CC_ON0 };
  if (cc >= CC_TYPE0 && cc < CC_TYPE0 + NBLK) return { kind: "type", b: cc - CC_TYPE0 };
  for (let b = 0; b < NBLK; b++) for (let k = 0; k < NPAR; k++) if (ccParam(b, k) === cc) return { kind: "param", b, k };
  return null;
}

// value scaling (pxm_to_cc, pxm_from_cc, pxm_type_to_cc, pxm_type_from_cc)
export const toCc = (v) => Math.floor((v * 127 + 50) / 100);
export const fromCc = (c) => Math.floor((c * 100 + 63) / 127);
export const typeToCc = (t, n) => Math.floor((t * 128 + 64) / n);
export const typeFromCc = (c, n) => Math.floor(c * n / 128);

// one MIDI message as a port delivers it -> { type, ch (1..16), ... } or null for what is not musical (SysEx, running data)
export function decode(d) {
  if (!d || !d.length || d[0] < 0x80) return null;
  const s = d[0];
  if (s === 0xF8) return { type: "clock" };
  if (s === 0xFA) return { type: "start" };
  if (s === 0xFB) return { type: "continue" };
  if (s === 0xFC) return { type: "stop" };
  if (s >= 0xF0) return null;
  const ch = (s & 15) + 1, kind = s & 0xF0;
  if (kind === 0xB0 && d.length >= 3) return { type: "cc", ch, cc: d[1] & 127, value: d[2] & 127 };
  if (kind === 0xC0 && d.length >= 2) return { type: "pc", ch, program: d[1] & 127 };
  if (kind === 0x90 && d.length >= 3) return { type: d[2] ? "note" : "noteoff", ch, note: d[1] & 127, value: d[2] & 127 };
  if (kind === 0x80 && d.length >= 3) return { type: "noteoff", ch, note: d[1] & 127, value: d[2] & 127 };
  return { type: "other", ch };
}

// ---- learned mappings ----
// A mapping: { id, src: { ch, cc }, tgt, slot }. tgt is one of
//   { kind: "param", b, k }          the control's 0..127 sets the setting's 0..100
//   { kind: "on", b, mode }          mode "follow": 64 and above is on; "toggle": each press (a value of 64 or more) flips it
//   { kind: "type", b }              the control's range is cut in as many slices as the block has models
//   { kind: "preset", step }         a press goes step presets on (1 or -1)
//   { kind: "scene", n }             a press recalls scene n (0..3) of the preset playing (editor protocol 6)
// slot: null = in every preset, n = only while preset slot n (0-based) is the current one.
export const sameSrc = (a, b) => a.ch === b.ch && a.cc === b.cc;
export const sameTgt = (a, b) => a.kind === b.kind && a.b === b.b && a.k === b.k && a.step === b.step && a.n === b.n;

// the mappings a Control Change triggers in preset `slot`: one of this preset wins over one for every preset
export function resolve(mappings, msg, slot) {
  const hit = mappings.filter((m) => sameSrc(m.src, msg) && (m.slot === null || m.slot === slot));
  return hit.some((m) => m.slot === slot) ? hit.filter((m) => m.slot === slot) : hit;
}

// what a mapping does with a value: { do: "param", b, k, v } | { do: "on", b, v } | { do: "type", b, t } |
// { do: "preset", step } | { do: "scene", n } | null (nothing: a pedal released, a toggle's 0). ctx: { on(b), ntypes(b) }
export function action(m, value, ctx) {
  const t = m.tgt, press = value >= 64;
  if (t.kind === "param") return { do: "param", b: t.b, k: t.k, v: fromCc(value) };
  if (t.kind === "on") {
    if (t.mode === "toggle") return press ? { do: "on", b: t.b, v: ctx.on(t.b) ? 0 : 1 } : null;
    return { do: "on", b: t.b, v: press ? 1 : 0 };
  }
  if (t.kind === "type") return { do: "type", b: t.b, t: typeFromCc(value, ctx.ntypes(t.b)) };
  if (t.kind === "preset") return press ? { do: "preset", step: t.step } : null;
  if (t.kind === "scene") return press ? { do: "scene", n: t.n } : null;
  return null;
}

// learn: `src` now drives `tgt` in `slot`. A control drives one thing per scope, and a target has one control per scope.
export function learn(mappings, src, tgt, slot, id) {
  const keep = mappings.filter((m) => !(m.slot === slot && (sameSrc(m.src, src) || sameTgt(m.tgt, tgt))));
  return [...keep, { id, src: { ch: src.ch, cc: src.cc }, tgt: { ...tgt }, slot }];
}

// stored mappings (localStorage JSON) -> the ones that are well formed; anything else is dropped
export function cleanMappings(list) {
  if (!Array.isArray(list)) return [];
  const int = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  return list.filter((m) => m && m.src && int(m.src.ch, 1, 16) && int(m.src.cc, 0, 127) && m.tgt &&
    (m.slot === null || int(m.slot, 0, 126)) &&
    ((m.tgt.kind === "param" && int(m.tgt.b, 0, NBLK - 1) && int(m.tgt.k, 0, NPAR - 1)) ||
     (m.tgt.kind === "on" && int(m.tgt.b, 0, NBLK - 1) && (m.tgt.mode === "follow" || m.tgt.mode === "toggle")) ||
     (m.tgt.kind === "type" && int(m.tgt.b, 0, NBLK - 1)) ||
     (m.tgt.kind === "preset" && (m.tgt.step === 1 || m.tgt.step === -1)) ||
     (m.tgt.kind === "scene" && int(m.tgt.n, 0, SCENES - 1))));
}

// ---- test build 39 (editor protocol 5, the Bluetooth audio image): controls learned ON THE PEDAL ----
// The pedal reads a controller's own number as one of its own before anything else (firmware/px_midi.h mid_map) and keeps
// up to PEDAL_MAP_N such entries with the presets, so they work with no page open: a footswitch linked to the pedal over
// Bluetooth, or a controller on its USB port. An entry: { src, dst, mode }, src the controller's number, dst the pedal's own,
// mode 0 "press" (64 and up is a press, below it the switch's release), 1 "each" (every message is a press: a switch that
// alternates 127 and 0) or 2 "value" (the value passes: a knob, a pedal).
export const PEDAL_MAP_N = 8, CC_PAD = 64, CC_TAP = 116, CC_EXP = 117, CC_NEXT = 118, CC_PREV = 119;
export const CC_SCENE0 = 65, SCENES = 4;   // (test build 49: CC 65 to 68, any value, recall SCENE 1 to 4 of the preset playing)
export const MAP_PRESS = 0, MAP_EACH = 1, MAP_VALUE = 2;
// the pedal's own number for one of this page's targets: { dst, value } (value: the number reads a value), or null
export function pedalDst(tgt) {
  if (tgt.kind === "on") return { dst: ccToggle(tgt.b), value: false };
  if (tgt.kind === "preset") return { dst: tgt.step > 0 ? CC_NEXT : CC_PREV, value: false };
  if (tgt.kind === "param") return { dst: ccParam(tgt.b, tgt.k), value: true };
  if (tgt.kind === "type") return { dst: ccType(tgt.b), value: true };
  if (tgt.kind === "scene") return { dst: CC_SCENE0 + tgt.n, value: false };
  return null;
}
// what one of the pedal's own numbers does: ccTarget's, or { kind: "next" | "prev" | "pad" | "tap" | "exp" }, or
// { kind: "scene", n }, or null
export function pedalTarget(cc) {
  if (cc === CC_NEXT) return { kind: "next" };
  if (cc === CC_PREV) return { kind: "prev" };
  if (cc === CC_PAD) return { kind: "pad" };
  if (cc === CC_TAP) return { kind: "tap" };
  if (cc === CC_EXP) return { kind: "exp" };
  if (cc >= CC_SCENE0 && cc < CC_SCENE0 + SCENES) return { kind: "scene", n: cc - CC_SCENE0 };
  return ccTarget(cc);
}
// the table with controller number `src` learned for `tgt` (mode: how the switch behaved while it was learned): a new array
// with the entry in the first free place (an entry with the same src gives its place), or null when the table is full
export function pedalLearn(entries, src, tgt, mode) {
  const d = pedalDst(tgt);
  if (!d || !Number.isInteger(src) || src < 0 || src > 127) return null;
  const out = entries.map((x) => (x && x.src === src ? null : x));
  const i = out.findIndex((x) => !x);
  if (i < 0) return null;
  out[i] = { src, dst: d.dst, mode: d.value ? MAP_VALUE : mode === MAP_EACH ? MAP_EACH : MAP_PRESS };
  return out;
}
// ---- editor protocol 6 (test builds 46 to 49): a preset's OWN controls learned on the pedal (firmware/px_midi.h mid_pl) ----
// The same idea in one preset only: a preset's own control comes before the ones above. PRESET_MAP_N places for all the
// presets together. An entry: { src, dst, mode, lo, hi, clo, chi }: as above, and for a setting driven by a pedal (mode
// "value") the setting at the heel and at the toe (lo, hi: 0..100; the other way round works too) and where the pedal was
// at each (clo, chi: 0..127; the same twice: its whole travel)
export const PRESET_MAP_N = 64;
// the entry for controller number `src` learned for `tgt` in one preset (mode: as pedalLearn's), a pedal on a setting from
// its 0 at the heel to its 100 at the toe; or null. The pedal puts it in place of the preset's entry for the same control or
// the same row (one control a row, one row a control: a block's ON answers to its flip and its on/off numbers)
export function presetEntry(src, tgt, mode) {
  const d = pedalDst(tgt);
  if (!d || !Number.isInteger(src) || src < 0 || src > 127) return null;
  return { src, dst: d.dst, mode: d.value ? MAP_VALUE : mode === MAP_EACH ? MAP_EACH : MAP_PRESS, lo: 0, hi: 100, clo: 0, chi: 127 };
}
// how a switch behaves, from the messages one press gave (values, in order): "press" when a value of 64 or more is
// followed by one below (it sends 127, then 0 when let go), else "each" (one message a press)
export const switchMode = (values) => (values.length >= 2 && values[0] >= 64 && values.slice(1).some((v) => v < 64)) ? MAP_PRESS : MAP_EACH;
