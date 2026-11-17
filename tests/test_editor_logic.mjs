// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the web editor's own logic, without a browser or a pedal: the Control Change map and the learned
// controls (docs/pxmidi.js) against docs/MIDI.md, and the model library and slot arithmetic (docs/pxlib.js) against
// the engine's rule (engine/px_dsp.c px_nam_model / px_nam_knob); TONE3000's key by site and its same-tab bookkeeping
// (docs/pxt3k.js), in a stand-in page address and session storage. Run by tests/run_tests.sh.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { webcrypto } from "node:crypto";
import { ccParam, ccOn, ccToggle, ccType, ccTarget, toCc, fromCc, typeToCc, typeFromCc, decode, learn, resolve, action, cleanMappings, NBLK, NPAR,
  pedalDst, pedalTarget, pedalLearn, switchMode, PEDAL_MAP_N, MAP_PRESS, MAP_EACH, MAP_VALUE, CC_NEXT, CC_PREV, CC_SCENE0, SCENES, presetEntry, sameTgt } from "../docs/pxmidi.js";
import { cleanSceneName } from "../docs/pxedit.js";
import { knobIndex, indexKnob, slotRef, presetSlot, retarget, usage, packedName, renamePacked, cleanSlotName, Library, pedalKey, planMove, smartName } from "../docs/pxlib.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const check = async (what, fn) => { await fn(); console.log(`${what.padEnd(76)} ok`); };

// ---- MIDI ----
await check("the Control Change map is the one docs/MIDI.md prints", () => {
  const md = readFileSync(`${ROOT}docs/MIDI.md`, "utf8");
  const rows = [...md.matchAll(/^\| (GATE|COMP|PITCH|SFX|DRIVE|AMP|CAB|EQ|MOD|DELAY|REVERB|SYN) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \|$/gm)];
  assert.equal(rows.length, NBLK);
  rows.forEach((r, b) => {
    assert.equal(ccOn(b), +r[2]); assert.equal(ccType(b), +r[3]);
    for (let k = 0; k < NPAR; k++) assert.equal(ccParam(b, k), +r[4 + k]);
  });
  assert.equal(ccToggle(5), 13);
});
await check("every number of the map is unique, and ccTarget is its inverse", () => {
  const seen = new Set([7]);
  for (let b = 0; b < NBLK; b++) {
    for (const [cc, t] of [[ccOn(b), { kind: "on", b }], [ccToggle(b), { kind: "toggle", b }], [ccType(b), { kind: "type", b }],
      ...Array.from({ length: NPAR }, (_, k) => [ccParam(b, k), { kind: "param", b, k }])]) {
      assert.ok(!seen.has(cc), `CC ${cc} twice`); seen.add(cc);
      assert.deepEqual(ccTarget(cc), t);
    }
  }
  assert.deepEqual(ccTarget(7), { kind: "master" });
  for (const free of [0, 6, 32, 38, 64, 98, 101, 120, 127]) assert.equal(ccTarget(free), null);
});
await check("values scale as the pedal's: 0..100 to 0..127 and back exactly", () => {
  for (let v = 0; v <= 100; v++) assert.equal(fromCc(toCc(v)), v);
  assert.equal(toCc(100), 127); assert.equal(fromCc(64), 50);
  for (const n of [1, 4, 9, 11]) for (let t = 0; t < n; t++) assert.equal(typeFromCc(typeToCc(t, n), n), t);
  assert.equal(typeFromCc(127, 9), 8);
});
await check("messages are decoded; SysEx and data bytes are not musical", () => {
  assert.deepEqual(decode([0xB2, 11, 99]), { type: "cc", ch: 3, cc: 11, value: 99 });
  assert.deepEqual(decode([0xC0, 51]), { type: "pc", ch: 1, program: 51 });
  assert.equal(decode([0xF8]).type, "clock");
  assert.equal(decode([0xF0, 0x7D, 0xF7]), null);
  assert.equal(decode([0x40, 0x10]), null);
  assert.equal(decode([]), null);
});
await check("a learned control: one control per target and one target per control, in its scope", () => {
  let m = learn([], { ch: 1, cc: 11 }, { kind: "param", b: 5, k: 1 }, null, "a");
  m = learn(m, { ch: 1, cc: 12 }, { kind: "param", b: 5, k: 1 }, null, "b");           // the target taught again
  assert.deepEqual(m.map((x) => x.id), ["b"]);
  m = learn(m, { ch: 1, cc: 12 }, { kind: "on", b: 9, mode: "toggle" }, null, "c");      // the control taught again
  assert.deepEqual(m.map((x) => x.id), ["c"]);
  m = learn(m, { ch: 1, cc: 12 }, { kind: "param", b: 10, k: 2 }, 4, "d");               // the same control, for preset 05 only
  assert.deepEqual(m.map((x) => x.id), ["c", "d"]);
  assert.deepEqual(resolve(m, { ch: 1, cc: 12 }, 4).map((x) => x.id), ["d"], "the preset's own wins in that preset");
  assert.deepEqual(resolve(m, { ch: 1, cc: 12 }, 3).map((x) => x.id), ["c"]);
  assert.deepEqual(resolve(m, { ch: 2, cc: 12 }, 3), [], "another channel is another control");
});
await check("what a learned control does: settings, switches (flip or follow), types, preset steps", () => {
  const ctx = { on: (b) => b === 9, ntypes: () => 8 };
  assert.deepEqual(action({ tgt: { kind: "param", b: 5, k: 1 } }, 127, ctx), { do: "param", b: 5, k: 1, v: 100 });
  assert.deepEqual(action({ tgt: { kind: "on", b: 9, mode: "toggle" } }, 127, ctx), { do: "on", b: 9, v: 0 });
  assert.equal(action({ tgt: { kind: "on", b: 9, mode: "toggle" } }, 0, ctx), null, "the release of a footswitch does nothing");
  assert.deepEqual(action({ tgt: { kind: "on", b: 9, mode: "follow" } }, 0, ctx), { do: "on", b: 9, v: 0 });
  assert.deepEqual(action({ tgt: { kind: "on", b: 2, mode: "follow" } }, 64, ctx), { do: "on", b: 2, v: 1 });
  assert.deepEqual(action({ tgt: { kind: "type", b: 9 } }, 127, ctx), { do: "type", b: 9, t: 7 });
  assert.deepEqual(action({ tgt: { kind: "preset", step: -1 } }, 100, ctx), { do: "preset", step: -1 });
  assert.equal(action({ tgt: { kind: "preset", step: 1 } }, 10, ctx), null);
});
await check("stored controls that are not well formed are dropped", () => {
  const good = { id: "a", src: { ch: 1, cc: 11 }, tgt: { kind: "param", b: 5, k: 1 }, slot: null };
  const list = [good, { src: { ch: 0, cc: 11 }, tgt: good.tgt, slot: null }, { src: good.src, tgt: { kind: "param", b: 12, k: 0 }, slot: null },
    { src: good.src, tgt: { kind: "on", b: 1 }, slot: null }, { src: good.src, tgt: { kind: "preset", step: 2 }, slot: null }, null, "x"];
  assert.deepEqual(cleanMappings(list), [good]);
  assert.deepEqual(cleanMappings("nonsense"), []);
});

// ---- slots ----
await check("MODEL and IR knobs: the engine's rule (v * n / 101), the knob at the middle of its span", () => {
  for (const n of [28, 8]) {
    for (let v = 0; v <= 100; v++) assert.equal(knobIndex(v, n), Math.floor(v * n / 101));
    for (let i = 0; i < n; i++) {
      const k = indexKnob(i, n);
      assert.equal(knobIndex(k, n), i);
      const span = Array.from({ length: 101 }, (_, v) => v).filter((v) => knobIndex(v, n) === i);
      assert.equal(k, (span[0] + span[span.length - 1]) >> 1);
    }
  }
  assert.equal(knobIndex(200, 28), 27); assert.equal(knobIndex(-3, 28), 0);
});
const cat = { blocks: [{ label: "GT", types: [{ name: "NOISE GATE" }] }, { label: "AMP", types: [{ name: "TWIN" }, { name: "NAM" }] }, { label: "CAB", types: [{ name: "4x12" }, { name: "USER IR" }] }] };
const preset = (ampType, model, cabType = 0, ir = 0) => ({ name: "P", on: [1, 1, 1], type: [0, ampType, cabType], p: [[0, 0, 0, 0, 0], [model, 50, 50, 50, 50], [ir, 0, 0, 0, 0]], order: [0, 1, 2] });
await check("which slot a preset plays, which presets play a slot, and a preset following its slot", () => {
  const nam = slotRef(cat, "nam", 12, 16), ir = slotRef(cat, "ir", 0, 8);
  assert.deepEqual([nam.b, nam.t, nam.n], [1, 1, 28]);
  assert.equal(slotRef({ blocks: [{ label: "AMP", types: [{ name: "TWIN" }] }] }, "nam", 12, 16), null, "a firmware without NAM");
  const u3 = preset(1, indexKnob(12 + 3, 28));
  assert.equal(presetSlot(u3, nam), 3);
  assert.equal(presetSlot(preset(1, indexKnob(4, 28)), nam), -8, "a factory model is no user slot");
  assert.equal(presetSlot(preset(0, indexKnob(15, 28)), nam), -1, "another amp type plays no model");
  assert.equal(presetSlot(preset(0, 0, 1, indexKnob(5, 8)), ir), 5);
  const bank = [{ saved: true, preset: u3 }, { saved: false, preset: u3 }, undefined, { saved: true, preset: preset(1, indexKnob(12, 28)) }, { saved: true, preset: u3 }];
  assert.deepEqual([...usage(bank, nam)], [[3, [0, 4]], [0, [3]]], "saved presets only; holes are skipped");
  const moved = retarget(u3, nam, new Map([[3, 0]]));
  assert.equal(presetSlot(moved, nam), 0);
  assert.equal(u3.p[1][0], indexKnob(15, 28), "the original is not touched");
  assert.equal(retarget(u3, nam, new Map([[5, 0]])), null);
});
await check("a stored file's name: read, cleaned, changed without touching its data", () => {
  const b = new Uint8Array(96); b.set([..."PLEXI LEAD"].map((c) => c.charCodeAt(0)), 8); b[70] = 9;
  assert.equal(packedName(b), "PLEXI LEAD");
  assert.equal(cleanSlotName("  my plexi é lead long "), "MY PLEXI ?");
  const r = renamePacked(b, "My Twin");
  assert.equal(packedName(r), "MY TWIN"); assert.equal(r[70], 9); assert.equal(r[18], 0);
  assert.equal(packedName(b), "PLEXI LEAD");
});
await check("the library (in memory here): no duplicate of the same data, links to the pedal's slots", async () => {
  const lib = await Library.open({});
  assert.equal(lib.persistent, false);
  const bytes = (seed) => { const b = new Uint8Array(96); b[64] = seed; return b; };
  const a = await lib.add({ kind: "nam", name: "ONE", bytes: bytes(1), source: { from: "file", file: "one.nam" } });
  const twin = await lib.add({ kind: "nam", name: "ONE AGAIN", bytes: bytes(1), source: { from: "file", file: "copy.nam" } });
  assert.equal(twin.id, a.id);
  const b = await lib.add({ kind: "nam", name: "TWO", bytes: bytes(2) });
  await lib.add({ kind: "ir", name: "CAB", bytes: bytes(3) });
  assert.deepEqual((await lib.all("nam")).map((e) => e.name), ["ONE", "TWO"]);
  lib.setLink("PocketX", "nam", 0, a.id); lib.setLink("PocketX", "nam", 2, b.id); lib.setLink("PocketX", "nam", 5, "gone");
  const held = await lib.onPedal("PocketX", "nam", [{ slot: 0, used: true, name: "ONE" }, { slot: 2, used: true, name: "RENAMED" }, { slot: 5, used: true, name: "X" }, { slot: 6, used: false, name: "" }]);
  assert.deepEqual([...held.keys()], [0], "a slot that no longer reads the entry's name is not that entry any more");
  assert.deepEqual(lib.links("PocketX", "nam"), { 0: a.id });
  assert.deepEqual(lib.links("BlackBox", "nam"), {});
  await lib.remove(a.id);
  assert.equal(await lib.get(a.id), null);
  assert.equal(pedalKey("PocketX_940"), "PocketX");
});
await check("moving a slot's file: to an empty slot, two changing places, and what cannot move", () => {
  const A = { id: "a" }, B = { id: "b" }, held = new Map([[0, A], [1, B]]), used = (s) => s === 0 || s === 1 || s === 2;
  let p = planMove(0, 4, held, used);
  assert.deepEqual(p.writes, [[4, A], [0, null]]); assert.deepEqual([...p.map], [[0, 4]]);
  p = planMove(0, 1, held, used);
  assert.deepEqual(p.writes, [[1, A], [0, B]]); assert.deepEqual([...p.map], [[0, 1], [1, 0]]);
  assert.ok(planMove(2, 4, held, used).error, "a file the library does not hold cannot move");
  assert.ok(planMove(0, 2, held, used).error, "nor change places with one it does not hold");
  assert.deepEqual(planMove(0, 0, held, used).writes, []);
});
// ---- TONE3000 (docs/pxt3k.js): the key by site, and what the editor's tab keeps for t3k.html when no second window opens ----
{
  if (!globalThis.crypto) globalThis.crypto = webcrypto;
  const store = new Map();
  globalThis.sessionStorage = { getItem: (k) => (store.has(k) ? store.get(k) : null), setItem: (k, v) => { store.set(k, String(v)); }, removeItem: (k) => { store.delete(k); } };
  const at = (href) => { const u = new URL(href); globalThis.location = { href: u.href, origin: u.origin, pathname: u.pathname, assigned: null, assign(x) { this.assigned = x; } }; };
  const FORK = "t3k_pub_dlDUT4gBuF5xAs0MpjZHnU7EeQ09DESn", EDITOR = "https://steponme82.github.io/openpocket/editor.html#models";
  at(EDITOR);
  const t = await import("../docs/pxt3k.js");
  const ask = (o) => sessionStorage.setItem("openpocket-t3k", JSON.stringify({ state: "S1", verifier: "V1", client: FORK, back: EDITOR, at: Date.now(), ...o }));
  await check("TONE3000: the fork's site uses its own key, every other site the project's", () => {
    assert.equal(t.t3kClientFor("https://steponme82.github.io"), FORK);
    for (const o of ["https://isod89.github.io", "http://127.0.0.1:8765", "http://localhost:8765", "https://steponme82.github.io.example.com"]) assert.equal(t.t3kClientFor(o), t.T3K_CLIENT_ID);
    assert.equal(t.t3kRedirect(), "https://steponme82.github.io/openpocket/t3k.html", "the redirect registered for the fork's key");
  });
  await check("TONE3000 in this tab: the Select page with this site's key, the S256 of the verifier kept", async () => {
    await t.t3kSelectHere(EDITOR);
    const u = new URL(location.assigned), p = JSON.parse(sessionStorage.getItem("openpocket-t3k"));
    assert.equal(u.origin + u.pathname, "https://www.tone3000.com/api/v1/oauth/authorize");
    assert.equal(u.searchParams.get("client_id"), FORK);
    assert.equal(u.searchParams.get("redirect_uri"), "https://steponme82.github.io/openpocket/t3k.html");
    assert.equal(u.searchParams.get("prompt"), "select_tone");
    assert.equal(u.searchParams.get("state"), p.state);
    assert.equal(u.searchParams.get("code_challenge_method"), "S256");
    const s256 = Buffer.from(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(p.verifier))).toString("base64url");
    assert.equal(u.searchParams.get("code_challenge"), s256);
    assert.equal(p.client, FORK);
    assert.equal(p.back, EDITOR);
  });
  await check("t3k.html in that tab: the answer checked against the request, which is used once", () => {
    const p = JSON.parse(sessionStorage.getItem("openpocket-t3k"));
    at("https://steponme82.github.io/openpocket/t3k.html");
    assert.deepEqual(t.t3kHere({ code: "C", state: p.state, tone_id: "42" }), { code: "C", tone_id: "42", verifier: p.verifier, client: FORK, back: EDITOR });
    assert.equal(sessionStorage.getItem("openpocket-t3k"), null);
    assert.equal(t.t3kHere({ code: "C", state: p.state, tone_id: "42" }), null, "no request: the page was opened some other way");
  });
  await check("t3k.html: a wrong state, an old request, a cancel, no tone: nothing signed in, back to the editor", () => {
    ask(); assert.match(t.t3kHere({ code: "C", state: "S2", tone_id: "42" }).error, /did not send/);
    assert.equal(sessionStorage.getItem("openpocket-t3k"), null, "a wrong state uses the request up too");
    ask({ at: Date.now() - 31 * 60000 }); assert.match(t.t3kHere({ code: "C", state: "S1", tone_id: "42" }).error, /old one/);
    ask(); assert.equal(t.t3kHere({ state: "S1", error: "access_denied" }).error, "cancelled");
    ask(); assert.equal(t.t3kHere({ code: "C", state: "S1", canceled: "true" }).error, "cancelled");
    ask(); assert.equal(t.t3kHere({ state: "S1" }).error, "no tone was chosen");
    ask(); assert.equal(t.t3kHere({ state: "S1", error: "server_error" }).error, "TONE3000: server_error");
    ask({ back: "https://example.com/x" }); assert.equal(t.t3kHere({ code: "C", state: "S1", tone_id: "1" }).back, EDITOR, "back only to this site");
    ask({ verifier: 7 }); assert.equal(t.t3kHere({ code: "C", state: "S1", tone_id: "1" }), null, "a damaged request is no request");
  });
  await check("t3k.html's note to the editor: read once", () => {
    t.t3kNote({ captures: [{ name: "PLEXI", bytes: "AA==", detail: "", source: { from: "tone3000" } }] });
    assert.equal(t.t3kNoteRead().captures[0].name, "PLEXI");
    assert.equal(t.t3kNoteRead(), null);
  });
}
// ---- test build 39: controls learned on the pedal (firmware/px_midi.h mid_map; px_midi_map_in) ----
{
  assert.deepEqual(pedalDst({ kind: "on", b: 7 }), { dst: 15, value: false });                    // a block: its flip number
  assert.deepEqual(pedalDst({ kind: "preset", step: -1 }), { dst: CC_PREV, value: false });
  assert.deepEqual(pedalDst({ kind: "param", b: 9, k: 0 }), { dst: ccParam(9, 0), value: true });
  assert.equal(pedalTarget(118).kind, "next"); assert.equal(pedalTarget(117).kind, "exp"); assert.equal(pedalTarget(15).kind, "toggle");
  assert.equal(switchMode([127, 0]), MAP_PRESS);                     // 127, then 0 when let go
  assert.equal(switchMode([127]), MAP_EACH);                         // one message a press
  assert.equal(switchMode([0]), MAP_EACH);                           // an alternating switch caught going off
  const empty = new Array(PEDAL_MAP_N).fill(null);
  let t = pedalLearn(empty, 80, { kind: "on", b: 7 }, MAP_PRESS);
  assert.deepEqual(t[0], { src: 80, dst: 15, mode: MAP_PRESS });
  t = pedalLearn(t, 81, { kind: "param", b: 7, k: 0 }, MAP_EACH);  // a knob: always by its value
  assert.equal(t[1].mode, MAP_VALUE);
  t = pedalLearn(t, 80, { kind: "on", b: 9 }, MAP_EACH);           // the same controller number again: it moves, one meaning
  assert.equal(t.filter((e) => e && e.src === 80).length, 1);
  assert.equal(t.find((e) => e && e.src === 80).dst, 17);
  const full = new Array(PEDAL_MAP_N).fill(0).map((_, i) => ({ src: i, dst: 8, mode: 0 }));
  assert.equal(pedalLearn(full, 100, { kind: "on", b: 0 }, MAP_PRESS), null);   // full: nothing learned
  assert.equal(pedalLearn(empty, 128, { kind: "on", b: 0 }, MAP_PRESS), null);  // not a controller number
  assert.equal(CC_NEXT, 118);
}
// ---- editor protocol 6 (test builds 46 to 49): scenes as a target, a preset's own controls (px_midi.h mid_pl) ----
await check("scenes: CC 65 to 68 recall SCENE 1 to 4, learnable on the pedal and on this page", () => {
  assert.equal(CC_SCENE0, 65); assert.equal(SCENES, 4);
  assert.deepEqual(pedalDst({ kind: "scene", n: 3 }), { dst: 68, value: false });
  assert.deepEqual(pedalTarget(66), { kind: "scene", n: 1 });
  assert.equal(pedalTarget(69), null);                                // (not a scene, nor anything else of the pedal's)
  assert.deepEqual(action({ tgt: { kind: "scene", n: 2 } }, 127, {}), { do: "scene", n: 2 });
  assert.equal(action({ tgt: { kind: "scene", n: 2 } }, 0, {}), null);   // a switch's release: nothing
  assert.ok(sameTgt({ kind: "scene", n: 1 }, { kind: "scene", n: 1 }) && !sameTgt({ kind: "scene", n: 1 }, { kind: "scene", n: 2 }));
  const kept = cleanMappings([{ id: "a", src: { ch: 1, cc: 20 }, tgt: { kind: "scene", n: 3 }, slot: null },
    { id: "b", src: { ch: 1, cc: 21 }, tgt: { kind: "scene", n: 4 }, slot: null }]);
  assert.deepEqual(kept.map((m) => m.id), ["a"]);                      // (a fifth scene is dropped)
  const t = pedalLearn(new Array(PEDAL_MAP_N).fill(null), 20, { kind: "scene", n: 0 }, MAP_PRESS);
  assert.deepEqual(t[0], { src: 20, dst: 65, mode: MAP_PRESS });
});
await check("a scene's name: 3 characters of ASCII 32..126, in capitals (docs/pxedit.js cleanSceneName)", () => {
  assert.equal(cleanSceneName("chorus"), "CHO");
  assert.equal(cleanSceneName(" v1 "), "V1");
  assert.equal(cleanSceneName("\u00e92b"), "2B");                       // (what the pedal cannot draw is left out)
  assert.equal(cleanSceneName("a b c"), "A B");
  assert.equal(cleanSceneName(""), ""); assert.equal(cleanSceneName(null), "");
});
await check("a preset's own control: a switch by how it pressed, a pedal on a setting from 0 at the heel to 100 at the toe", () => {
  assert.deepEqual(presetEntry(30, { kind: "param", b: 7, k: 1 }, MAP_PRESS), { src: 30, dst: ccParam(7, 1), mode: MAP_VALUE, lo: 0, hi: 100, clo: 0, chi: 127 });
  assert.deepEqual(presetEntry(31, { kind: "on", b: 9 }, MAP_EACH), { src: 31, dst: 17, mode: MAP_EACH, lo: 0, hi: 100, clo: 0, chi: 127 });
  assert.equal(presetEntry(32, { kind: "scene", n: 2 }, MAP_PRESS).dst, 67);
  assert.equal(presetEntry(128, { kind: "on", b: 0 }, MAP_PRESS), null);
  assert.equal(presetEntry(5, { kind: "nothing" }, MAP_PRESS), null);
});
// ---- test build 40: a name a player reads for a TONE3000 capture (docs/pxlib.js smartName) ----
{
  assert.equal(smartName("Vox AC30 Top Boost", "VX"), "VOX AC30 VX");
  assert.equal(smartName("Fender Deluxe Reverb", "FD"), "FENDER FD");
  assert.equal(smartName("Marshall JCM800 2203 capture pack", "CH1"), "JCM800 CH1");   // a first word too long is passed over
  assert.equal(smartName("Klon Centaur", "Gain 2"), "KLON GAIN 2");
  assert.equal(smartName("", "VX"), "VX");
  assert.equal(smartName("NAM A2 Pack", "G6"), "G6");                               // words that name nothing are left out
  assert.equal(smartName("Friedman BE-100", ""), "FRIEDMAN");
  assert.equal(smartName("", ""), "MODEL");
  for (const [t, m] of [["A very long title of many words", "and a long model name"], ["X", "Y"]]) assert.ok(smartName(t, m).length <= 11);
}
console.log("editor logic test passed");
