// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket web editor: the Models section. The pedal's 16 NAM user slots and 8 cabinet IR slots, the library of
// files this browser keeps for them (docs/pxlib.js), and TONE3000 (docs/pxt3k.js). The two kinds of slot share
// everything here but how a file is read and sent.

import { app, $, h, fill, icon, on, emit, slotName, say, task, sleep, confirmDialog, promptDialog, pickDialog, menu, plural, dropTarget, fileLabel } from "./edcore.js";
import { writeSlots, entry, selectPreset } from "./edpresets.js";
import { namEntry, readDi, SLOT_BYTES } from "./pxnam.js";
import { parseWav, prepareIr, packIr, irName } from "./pxir.js";
import { Library, KINDS, slotRef, usage, retarget, renamePacked, cleanSlotName, smartName, pedalKey, planMove, indexKnob, presetSlot } from "./pxlib.js";
import { t3kReady, t3kSelect, t3kSelectHere, t3kNoteRead, t3kSignIn, t3kSignedIn, t3kSignOut, t3kTone, t3kModels, t3kDownload, t3kSource, t3kUser, t3kList, t3kA2Count, t3kLink, t3kPicture, T3K_LISTS } from "./pxt3k.js";

let lib = null, namDi = null;
const queue = { nam: [], ir: [] };                // what the last "add files" did, file by file
const libList = { nam: [], ir: [] };              // the library's entries, read again after each change
const pedal = () => pedalKey(app.info && app.info.identity);

const BANKS = {
  nam: {
    title: "Amp captures", sub: "NAM A2", input: "namfile", accept: (f) => /\.nam$/i.test(f.name), files: ".nam files", one: ".nam file",
    named: (f) => /\.(nam|json)$/i.test(f.name),     // (after a pick: the picker has no filter; the content is checked next)
    lead: "The AMP block's NAM type plays these neural captures. The pedal runs A2 captures (A2-Lite): on TONE3000, pick the A2 version.",
    slots: () => app.nam.models.filter((m) => m.user),
    // a .nam file's text -> what the pedal stores, at the same loudness as every other model
    async prepare(text, name) {
      if (!namDi) namDi = readDi(await (await fetch("nam_di.wav")).arrayBuffer());
      await sleep(20);                            // (the level takes a moment of the processor: let the progress show first)
      return namEntry(text, name, namDi);
    },
    read: (f) => f.text(),
    upload: (slot, bytes, progress) => app.ed.namUpload(slot, bytes, progress),
    erase: (slot) => app.ed.namErase(slot),
    async reload() { app.nam = await app.ed.namList(); await app.ed.namTexts(); },
  },
  ir: {
    title: "Cabinet IRs", sub: "WAV", input: "irfile", accept: (f) => /\.wav$/i.test(f.name), files: ".wav files", one: ".wav file",
    named: (f) => /\.wav$/i.test(f.name),
    lead: "The CAB block's USER IR type plays these impulse responses. The pedal keeps the first 23 ms of a WAV file, at 44.1 kHz, and sets its level like the other cabinets.",
    slots: () => app.irs,
    async prepare(buf, name) {
      const nm = irName(name), ir = prepareIr(parseWav(buf));
      return { name: nm, bytes: packIr(nm, ir.h), detail: ir.note };
    },
    read: (f) => f.arrayBuffer(),
    upload: (slot, bytes, progress) => app.ed.irUpload(slot, bytes, progress),
    erase: (slot) => app.ed.irErase(slot),
    async reload() { app.irs = await app.ed.irList(); await app.ed.irTexts(); },
  },
};
const has = (kind) => !!(app.ed && (kind === "nam" ? app.nam : app.irs));      // (null: a firmware without them)
const tag = (kind, slot) => KINDS[kind].tag(slot);

// the pedal's slots and the library, read again; then every section that shows them
export async function refreshAssets(kind, reload = true) {
  if (!has(kind)) return;
  if (reload) await BANKS[kind].reload();
  app.held[kind] = await lib.onPedal(pedal(), kind, BANKS[kind].slots());
  libList[kind] = await lib.all(kind);
  emit("assets", kind);
}
export async function loadAssets() {
  app.nam = null; app.irs = null; app.refs = { nam: null, ir: null };
  if (app.ed.hasNam) { app.nam = await app.ed.namList(); app.refs.nam = slotRef(app.cat, "nam", app.nam.nfact, app.nam.nuser); }
  if (app.ed.hasIr) { app.irs = await app.ed.irList(); app.refs.ir = slotRef(app.cat, "ir", 0, app.irs.length); }
  for (const k of ["nam", "ir"]) await refreshAssets(k, false);
}

// ---- writing a slot ----
async function put(kind, slot, e, progress = () => { }) {
  const rc = await BANKS[kind].upload(slot, e.bytes, progress);
  if (rc) throw new Error(rc === 2 ? "the pedal could not write its memory" : `the pedal refused the ${KINDS[kind].thing}`);
  lib.setLink(pedal(), kind, slot, e.id);
}
async function clear(kind, slot) {
  if (await BANKS[kind].erase(slot)) throw new Error("the pedal could not empty the slot");
  lib.setLink(pedal(), kind, slot, null);
}
// a file taken off the pedal remembers its slot: sent back, it returns there, and the presets that named the slot play it again
async function leave(kind, slot) {
  const e = app.held[kind].get(slot);
  await clear(kind, slot);
  if (e) await lib.put({ ...e, home: slot });
}
const freeSlot = (kind, taken = new Set()) => { const s = BANKS[kind].slots().find((x) => !x.used && !taken.has(x.slot)); return s ? s.slot : -1; };
const usedBy = (kind) => (app.refs[kind] ? usage(app.bank, app.refs[kind]) : new Map());
// the presets using each slot, once every preset has been read: null (with the reason shown) when one cannot be read
async function knownUse(kind) {
  const n = app.slots.length;
  if (app.bank.filter(Boolean).length < n) {
    const read = await task("Reading the presets", async (progress) => { for (let i = 0; i < n; i++) { progress(i / n); await entry(i); } return ""; });
    if (read === undefined) return null;
  }
  return usedBy(kind);
}
const presetNames = (list) => list.map((s) => `${slotName(s)} ${app.slots[s].name}`).join(", ");

// files into the library, and onto the pedal while it has room (or into `into`, one file, replacing what is there)
export function addFiles(kind, files, into = -1) {
  const B = BANKS[kind], K = KINDS[kind];
  queue[kind] = files.map((f) => ({ name: f.name, state: "wait", text: "Waiting" }));
  render(kind);
  return task(files.length === 1 ? `Adding ${files[0].name}` : `Adding ${files.length} ${K.things}`, async (progress) => {
    let sent = 0, kept = 0, failed = 0;
    for (const [i, f] of files.entries()) {
      const q = queue[kind][i], step = (state, text, frac) => { Object.assign(q, { state, text, frac }); render(kind); progress((i + (frac || 0)) / files.length); };
      try {
        if (!B.named(f)) throw new Error(`not a ${B.one}`);
        step("work", kind === "nam" ? "Measuring its level" : "Reading", 0.05);
        const made = await B.prepare(await B.read(f), f.name);
        const e = await lib.add({ kind, name: made.name, bytes: made.bytes, detail: made.detail, source: { from: "file", file: f.name } });
        const slot = into >= 0 ? into : freeSlot(kind);
        if (slot < 0) { kept++; step("lib", "In the library: the pedal's slots are full", 1); continue; }
        step("work", `Sending to ${tag(kind, slot)}`, 0.1);
        await put(kind, slot, e, (x) => step("work", `Sending to ${tag(kind, slot)}`, 0.1 + 0.9 * x));
        await B.reload();
        sent++; step("done", `In ${tag(kind, slot)} as ${e.name}${made.detail ? ` (${made.detail})` : ""}`, 1);
      } catch (err) { failed++; step("err", err.message.replace(/^./, (c) => c.toUpperCase()), 1); }
    }
    await refreshAssets(kind);
    const parts = [sent && `${plural(sent, K.thing)} on the pedal`, kept && `${kept} in the library only (no free slot)`, failed && `${failed} not added`].filter(Boolean);
    return { text: `${parts.join(", ")}.${failed ? " The list says why." : ""}`, kind: failed && !sent && !kept ? "err" : failed ? "info" : "ok" };
  });
}
function sendToPedal(kind, e) {
  const slots = BANKS[kind].slots(), home = Number.isInteger(e.home) && slots[e.home] && !slots[e.home].used ? e.home : -1;
  const slot = home >= 0 ? home : freeSlot(kind);
  if (slot >= 0) return install(kind, e, slot);
  return replaceWith(kind, e);
}
async function replaceWith(kind, e) {
  const K = KINDS[kind], use = await knownUse(kind);
  if (!use) return;
  const to = await pickDialog({
    title: `Put ${e.name} in which slot?`, intro: `Every slot is taken: ${e.name} takes the place of the ${K.thing} you choose.`, searchable: false,
    items: BANKS[kind].slots().map((s) => ({ value: s.slot, n: tag(kind, s.slot), name: s.used ? s.name : "Empty", tag: use.get(s.slot) ? plural(use.get(s.slot).length, "preset") : "" })),
    consequence: (it) => {
      const u = use.get(it.value) || [];
      return { text: u.length ? `${plural(u.length, "preset")} (${presetNames(u)}) will play ${e.name} instead.` : "No saved preset uses this slot.", confirm: `Replace ${it.n}`, danger: true };
    },
  });
  if (to !== null) await install(kind, e, to);
}
function install(kind, e, slot) {
  return task(`Sending ${e.name} to ${tag(kind, slot)}`, async (progress) => {
    await put(kind, slot, e, progress);
    await refreshAssets(kind);
    return `${e.name} is in ${tag(kind, slot)}. Choose it in the ${KINDS[kind].block} block.`;
  });
}
async function removeFromPedal(kind, s) {
  const K = KINDS[kind], use = await knownUse(kind);
  if (!use) return;
  const u = use.get(s.slot) || [], kept = app.held[kind].has(s.slot);
  const body = h("div", {},
    u.length ? h("p", { class: "warn", text: `${plural(u.length, "preset")} ${u.length === 1 ? "uses" : "use"} it: ${presetNames(u)}. ${u.length === 1 ? "It plays" : "They play"} without ${K.without} until another ${K.thing} is in ${tag(kind, s.slot)}.` }) : h("p", { text: "No saved preset uses it." }),
    h("p", { class: "dim", text: kept ? "It stays in the library on this computer: you can send it back later." : `It is not in this browser's library: to get it back you need its file again.` }));
  if (!(await confirmDialog({ title: `Remove ${s.name} from ${tag(kind, s.slot)}?`, body, confirm: "Remove from the pedal", danger: true }))) return;
  await task(`Emptying ${tag(kind, s.slot)}`, async () => { await leave(kind, s.slot); await refreshAssets(kind); return `${tag(kind, s.slot)} is empty.`; });
}
// test build 40 (editor protocol 5): a NAM capture is renamed on the pedal itself (NAM_RENAME), so any slot can be, from any
// browser; a capture this browser's library holds is renamed there too. Older firmware: through the library only (re-sent).
const canRenameOnPedal = (kind) => kind === "nam" && !!(app.ed && app.ed.hasNamRename);
async function renameSlot(kind, s) {
  const e = app.held[kind].get(s.slot), onPedal = canRenameOnPedal(kind);
  if (!e && !onPedal) return;
  const name = await promptDialog({ title: `Rename ${s.name}`, label: "Name on the pedal (11 characters at most)", value: s.name, max: 11, hint: "The pedal shows this name in the block's settings and on the PLAY page. Presets keep using the slot." });
  const nm = name === null ? "" : cleanSlotName(name);
  if (!nm || nm === s.name) return;
  await task(`Renaming ${s.name}`, async (progress) => {
    if (onPedal) {
      const rc = await app.ed.namRename(s.slot, nm);
      if (rc) throw new Error(rc === 2 ? "the pedal could not write it to its flash" : "the pedal found no capture to rename in that slot");
      if (e) await lib.put({ ...e, name: nm, bytes: renamePacked(e.bytes, nm) });
    } else {
      const next = await lib.put({ ...e, name: nm, bytes: renamePacked(e.bytes, nm) });
      await put(kind, s.slot, next, progress);
    }
    await refreshAssets(kind);
    return `${tag(kind, s.slot)} is ${nm} now.`;
  });
}
// a slot's file to another slot (or the two change places); the presets that play them follow
async function moveSlot(kind, s) {
  const held = app.held[kind], slots = BANKS[kind].slots(), use = await knownUse(kind), ref = app.refs[kind];
  if (!use) return;
  const to = await pickDialog({
    title: `Move ${s.name} to…`, intro: "Presets that use it follow it to its new slot.", searchable: false,
    items: slots.map((x) => ({ value: x.slot, n: tag(kind, x.slot), name: x.used ? x.name : "Empty", tag: x.used ? "changes places" : "",
      disabled: x.slot === s.slot || (x.used && !held.has(x.slot)), why: x.slot === s.slot ? "It is here now." : "Not in this browser's library: it cannot change places." })),
    consequence: (it) => {
      const n = (use.get(s.slot) || []).length + (slots[it.value].used ? (use.get(it.value) || []).length : 0);
      return { text: `${slots[it.value].used ? `${s.name} and ${slots[it.value].name} change places.` : `${s.name} goes to ${it.n}.`} ${n ? `${plural(n, "preset")} ${n === 1 ? "is" : "are"} updated to follow.` : "No saved preset is affected."}`, confirm: `Move to ${it.n}` };
    },
  });
  if (to === null) return;
  const plan = planMove(s.slot, to, held, (x) => slots[x].used);
  if (plan.error) { say(plan.error, "err"); return; }
  await task(`Moving ${s.name}`, async (progress) => {
    for (const [i, [slot, e]] of plan.writes.entries()) {
      const part = (x) => progress((i + x) / (plan.writes.length + 1));
      if (e) await put(kind, slot, e, part); else await clear(kind, slot);
    }
    const changes = [];
    for (let i = 0; i < app.slots.length; i++) {
      const b = await entry(i), q = b.saved ? retarget(b.preset, ref, plan.map) : null;
      if (q) changes.push({ slot: i, preset: q });
    }
    const live = presetSlot(app.st.preset, ref), dirty = app.st.dirty;
    if (changes.length) await writeSlots(changes);
    if (dirty && plan.map.has(live)) await app.ed.setParam(ref.b, 0, indexKnob(ref.base + plan.map.get(live), ref.n));
    await refreshAssets(kind);
    return `${s.name} is in ${tag(kind, to)} now.${changes.length ? ` ${plural(changes.length, "preset")} updated to follow.` : ""}`;
  });
}
async function forget(kind, e) {
  if (!(await confirmDialog({ title: `Delete ${e.name} from the library?`, body: "It is removed from this computer's library. What is on the pedal is not touched.", confirm: "Delete", danger: true }))) return;
  await lib.remove(e.id);
  await refreshAssets(kind, false);
}
// the kind's file input (editor.html) opens from labels of it (edcore.js fileLabel): each says first which slot it is for
const aim = { nam: -1, ir: -1 };
function aimAt(kind, into) { aim[kind] = into; $(BANKS[kind].input).multiple = into < 0; }
const pickLabel = (kind, into, attrs, ...kids) => fileLabel(BANKS[kind].input, () => aimAt(kind, into), attrs, ...kids);

// ---- a kind's card ----
function sourceLine(e) {
  if (!e) return h("span", { class: "meta dim", text: "Not in this browser's library" });
  const s = e.source || {};
  if (s.from === "tone3000") return h("span", { class: "meta" }, h("span", { class: "t3k-mark", text: "T3K" }), ` ${s.title}${s.creator ? ` by ${s.creator}` : ""}`);
  return h("span", { class: "meta dim", text: [s.file, e.detail].filter(Boolean).join(" · ") });
}
function usageChip(kind, slot, use) {
  if (app.bank.filter(Boolean).length < app.slots.length) return h("span", { class: "chip dim", text: "…" , title: "Reading the presets" });
  const u = use.get(slot) || [];
  if (!u.length) return h("span", { class: "chip dim", text: "Not used" });
  return h("button", { type: "button", class: "chip link", text: plural(u.length, "preset"), "aria-haspopup": "menu", title: "The presets that use it",
    onclick: (ev) => menu(ev.currentTarget, u.map((p) => ({ label: `${slotName(p)} ${app.slots[p].name}`, run: () => { emit("view-go", "sound"); selectPreset(p); } }))) });
}
function render(kind) {
  const el = $(kind);
  if (!has(kind)) {
    el.textContent = "";
    if (app.conn === "ready") el.append(h("div", { class: "card-head" }, h("h2", { text: BANKS[kind].title })),
      h("p", { class: "dim" }, `This pedal's firmware (${app.info.version}) has no ${KINDS[kind].things} yet. `, h("a", { href: "./", text: "Update it from the installer" }), "."));
    return;
  }
  const B = BANKS[kind], K = KINDS[kind], slots = B.slots(), held = app.held[kind], use = usedBy(kind);
  const used = slots.filter((s) => s.used).length;
  const onPedal = new Set([...held.values()].map((e) => e.id));
  const rest = libList[kind].filter((e) => !onPedal.has(e.id));
  fill(el,
    h("div", { class: "card-head" },
      h("h2", {}, B.title, " ", h("small", { text: B.sub })),
      h("span", { class: "grow" }),
      pickLabel(kind, -1, { class: "btn small", id: `${kind}add` }, icon("plus"), ` Add ${B.files}`)),
    h("div", { class: "cap" },
      h("span", { class: "capbar", role: "img", "aria-label": `${used} of ${slots.length} slots used` }, slots.map((s) => h("i", { class: s.used ? "on" : "" }))),
      h("span", { class: "cap-t", id: `${kind}cap`, text: `${used} of ${slots.length} slots used` })),
    h("p", { class: "lead dim", text: B.lead }),
    queue[kind].length ? h("div", { class: "queue", "aria-live": "polite" },
      queue[kind].map((q) => h("div", { class: `q ${q.state}` }, h("span", { class: "q-n", text: q.name }), h("span", { class: "q-t", text: q.text }),
        q.state === "work" && h("span", { class: "q-p" }, h("i", { style: `width:${Math.round((q.frac || 0) * 100)}%` })))),
      queue[kind].every((q) => q.state !== "wait" && q.state !== "work") && h("button", { type: "button", class: "btn small", text: "Clear this list", onclick: () => { queue[kind] = []; render(kind); } })) : null,
    h("div", { class: "slots", id: `${kind}list` },
      slots.filter((s) => s.used).map((s) => {
        const e = held.get(s.slot);
        return h("div", { class: "srow", "data-slot": s.slot },
          h("span", { class: "n", text: tag(kind, s.slot) }),
          h("span", { class: "nm" }, h("b", { text: s.name }), sourceLine(e)),
          usageChip(kind, s.slot, use),
          h("button", { type: "button", class: "btn icon small", "aria-label": `Actions for ${tag(kind, s.slot)} ${s.name}`, "aria-haspopup": "menu", onclick: (ev) => menu(ev.currentTarget, [
            { label: "Rename…", run: () => renameSlot(kind, s), disabled: !e && !canRenameOnPedal(kind), why: "Not in this browser's library: load its file again to rename it." },
            { label: "Move to another slot…", run: () => moveSlot(kind, s), disabled: !e, why: "Not in this browser's library: load its file again to move it." },
            { label: "Replace with a file…", file: B.input, aim: () => aimAt(kind, s.slot) },
            "-",
            { label: "Remove from the pedal", run: () => removeFromPedal(kind, s), danger: true },
          ]) }, icon("more")));
      }),
      // the empty slots, small: one click loads a file into that very slot (a preset may be waiting for it)
      used < slots.length && h("div", { class: "free" },
        h("span", { class: "free-l", text: used ? "Empty" : "All empty" }),
        slots.filter((s) => !s.used).map((s) => {
          const u = use.get(s.slot) || [];
          return pickLabel(kind, s.slot, { class: `chip slot${u.length ? " need" : ""}`, "data-slot": s.slot,
            title: u.length ? `${plural(u.length, "preset")} (${presetNames(u)}) ${u.length === 1 ? "points" : "point"} here and ${u.length === 1 ? "plays" : "play"} without ${K.without}. Load a file into ${tag(kind, s.slot)}.` : `Load a file into ${tag(kind, s.slot)}`,
            "aria-label": `Load a file into ${tag(kind, s.slot)}${u.length ? `, which ${plural(u.length, "preset")} ${u.length === 1 ? "is" : "are"} waiting for` : ""}` }, tag(kind, s.slot));
        })),
      slots.some((s) => !s.used && (use.get(s.slot) || []).length) && h("p", { class: "warn small" }, icon("alert"),
        ` ${slots.filter((s) => !s.used && (use.get(s.slot) || []).length).map((s) => `${tag(kind, s.slot)} (${presetNames(use.get(s.slot))})`).join(", ")}: presets point to an empty slot and play without ${K.without}.`)));
  if (kind === "nam") {
    const fact = app.nam.models.filter((m) => !m.user);
    el.append(h("details", { class: "factory" }, h("summary", { text: `${fact.length} factory models` }),
      h("div", { class: "fact" }, fact.map((m) => h("span", { text: m.name }))), h("p", { class: "dim", text: "Built into the firmware: they need no slot and cannot be changed." })));
  }
  el.append(h("h3", { text: "Library on this computer" }),
    h("p", { class: "dim", text: lib.persistent ? `Every ${K.thing} you add is kept in this browser, so it can move between slots, be renamed, or come back later. The pedal itself cannot send one back.`
      : `This browser does not let the page keep files: the library lasts until the page is closed.` }),
    h("div", { class: "slots lib", id: `${kind}lib` }, rest.length ? rest.map((e) => h("div", { class: "srow" },
      h("span", { class: "nm" }, h("b", { text: e.name }), sourceLine(e)),
      h("button", { type: "button", class: "btn small", text: "Send to the pedal", onclick: () => sendToPedal(kind, e) }),
      h("button", { type: "button", class: "btn icon small", "aria-label": `Delete ${e.name} from the library`, onclick: () => forget(kind, e) }, icon("x"))))
      : h("p", { class: "dim empty-note", text: libList[kind].length ? `All ${plural(libList[kind].length, K.thing)} in the library ${libList[kind].length === 1 ? "is" : "are"} on the pedal.` : `Nothing yet. Add ${B.files}, or drop them on this panel.` })));
}

// ---- TONE3000 ----
const t3k = { busy: false, user: null, tab: "favorited", lists: {}, error: "", tone: null, models: null, into: "free" };
const gearText = (t) => [t.gear, t.format].filter(Boolean).join(" · ").toUpperCase();
const creator = (u) => (u ? u.display_name || u.username || "" : "");
const logo = () => h("span", { class: "t3k-logo", role: "img", "aria-label": "TONE3000", text: "TONE3000" });
const picture = (t, cls) => { const src = t3kPicture((t.images || [])[0]); return src ? h("img", { class: cls, src, alt: "", loading: "lazy" }) : h("span", { class: cls }); };
const avatar = (u) => { const src = t3kPicture(u.avatar_url); return src && h("img", { src, alt: "" }); };

async function browse() {
  if (!t3kReady()) { say("TONE3000 browsing is not set up in this copy of the editor. Download the A2 capture from tone3000.com and add the .nam file here.", "err"); return; }
  if (t3k.busy) return;
  t3k.busy = true; t3k.error = ""; renderT3k();
  try {
    let sel;
    try { sel = await t3kSelect(); }
    catch (e) {
      if (!e.noWindow) throw e;
      // no second window (an in-app browser: MIDIWeb on iOS, for Bluetooth MIDI): TONE3000 in this tab, and back here
      t3k.busy = false; renderT3k();
      if (await confirmDialog({ title: "TONE3000 opens in this page", confirm: "Go to TONE3000", body: h("div", {},
        h("p", { text: "This browser opened no second window (it has none, or it blocks pop-ups), so TONE3000 takes this page for a moment: sign in there and choose a tone." }),
        h("p", { class: "dim", text: "Its A2 captures come into the library here, and the editor opens again: connect to the pedal and send them from the library." })) }))
        await t3kSelectHere(`${location.href.replace(/#.*$/, "")}#models`);
      return;
    }
    await t3kSignIn(sel);
    t3k.lists = {};
    t3kUser().then((u) => { t3k.user = u; renderT3k(); }, () => { });
    if (sel.tone_id) await openTone({ id: sel.tone_id });
    else loadList(t3k.tab);
  } catch (e) {
    if (e.message !== "cancelled") t3k.error = e.message.replace(/^TONE3000: /, "");
  } finally { t3k.busy = false; renderT3k(); }
}
async function loadList(id) {
  t3k.tab = id; t3k.tone = null; t3k.error = "";
  renderT3k();
  if (t3k.lists[id]) return;
  try { t3k.lists[id] = await t3kList(id); }
  catch (e) { t3k.lists[id] = null; t3k.error = e.message; }
  renderT3k();
}
async function openTone(tone) {
  t3k.tone = tone; t3k.models = null; t3k.error = "";
  renderT3k();
  try {
    const [full, models] = await Promise.all([tone.title ? tone : t3kTone(tone.id), t3kModels(tone.id)]);
    t3k.tone = full; t3k.models = models;
  } catch (e) { t3k.error = e.message; t3k.models = []; }
  renderT3k();
}
// a model of the open tone: downloaded, measured, kept in the library, sent to the slot chosen
function getModels(models) {
  const tone = t3k.tone, into = t3k.into;
  return task(models.length === 1 ? `Getting ${models[0].name || "the capture"} from TONE3000` : `Getting ${models.length} captures from TONE3000`, async (progress) => {
    let sent = 0, kept = 0;
    const taken = new Set();
    for (const [i, m] of models.entries()) {
      const part = (x, text) => progress((i + x) / models.length, text);
      part(0.05, `Downloading ${m.name || "the capture"}…`);
      const text = await t3kDownload(m);
      part(0.2, `Measuring the level of ${m.name || "the capture"}…`);
      const made = await BANKS.nam.prepare(text, `${smartName(tone.title, m.name)}.nam`);   /* (test build 40: a name a player reads) */
      const e = await lib.add({ kind: "nam", name: made.name, bytes: made.bytes, detail: made.detail, source: t3kSource(tone, m) });
      const slot = into === "lib" ? -1 : into === "free" || models.length > 1 ? freeSlot("nam", taken) : +into;
      if (slot < 0) { kept++; continue; }
      taken.add(slot);
      await put("nam", slot, e, (x) => part(0.3 + 0.7 * x, `Sending ${e.name} to ${tag("nam", slot)}…`));
      await BANKS.nam.reload();
      sent++;
    }
    await refreshAssets("nam");
    t3k.into = "free";
    renderT3k();
    const parts = [sent && `${plural(sent, "capture")} on the pedal`, kept && `${plural(kept, "capture")} in the library${into === "lib" ? "" : " only: the pedal's slots are full"}`].filter(Boolean);
    return `${parts.join(", ")}. Choose ${sent === 1 ? "it" : "one"} in the AMP block: type NAM, MODEL.`;
  });
}
function toneRow(t) {
  const n = t3kA2Count(t), u = t.user || {};
  return h("button", { type: "button", class: `tone${n === 0 ? " none" : ""}`, onclick: () => openTone(t) },
    picture(t, "tone-img"),
    h("span", { class: "tone-b" },
      h("span", { class: "tone-t", text: t.title || "Tone" }),
      h("span", { class: "tone-s", text: gearText(t) }),
      h("span", { class: "tone-u" }, avatar(u), creator(u))),
    h("span", { class: `chip${n === 0 ? " dim" : ""}`, text: n === null ? "A2?" : n === 0 ? "No A2" : `${n} A2` }));
}
function toneDetail() {
  const t = t3k.tone, u = t.user || {}, models = t3k.models;
  const slots = app.nam ? BANKS.nam.slots() : [], free = freeSlot("nam");
  const into = h("select", { id: "t3kinto", "aria-label": "Where the capture goes", onchange: () => { t3k.into = into.value; } },
    h("option", { value: "free", text: free >= 0 ? `Next free slot (${tag("nam", free)})` : "Library only (every slot is taken)" }),
    slots.filter((s) => s.used).map((s) => h("option", { value: s.slot, text: `${tag("nam", s.slot)} ${s.name}: replace` })),
    h("option", { value: "lib", text: "Library only, not the pedal" }));
  into.value = [...into.options].some((o) => o.value === String(t3k.into)) ? t3k.into : "free";
  return h("div", { class: "tone-d", id: "t3kcard" },
    h("button", { type: "button", class: "btn small back", onclick: () => loadList(t3k.tab) }, icon("left"), ` ${T3K_LISTS.find((l) => l.id === t3k.tab).label}`),
    h("div", { class: "tone-h" },
      picture(t, "tone-img big"),
      h("div", { class: "tone-b" },
        h("h3", { text: t.title || "Tone" }),
        h("span", { class: "tone-s", text: gearText(t) }),
        h("a", { class: "tone-u", href: t3kLink(u.url), target: "_blank", rel: "noopener" }, avatar(u), creator(u)))),
    t.description && h("p", { class: "tone-desc dim", text: t.description }),
    models === null ? h("p", { class: "dim", text: "Reading its captures…" })
      : !models.length ? h("p", { class: "warn" }, icon("alert"), " This tone has no A2 capture, and the pedal plays A2 only. Choose another tone.")
      : h("div", { class: "tone-m" },
        h("label", { class: "into" }, h("span", { text: "Into" }), into),
        models.map((m) => h("div", { class: "mrow" }, h("span", { class: "nm", text: m.name || `Capture ${m.id}` }), m.size && h("span", { class: "chip dim", text: m.size }),
          h("button", { type: "button", class: "btn small primary", text: "Get", "aria-label": `Get ${m.name || "this capture"}`, onclick: () => getModels([m]) }))),
        models.length > 1 && h("button", { type: "button", class: "btn small", text: `Get all ${models.length} into free slots`, onclick: () => { t3k.into = "free"; getModels(models); } })),
    t3kLink(t.url) && h("a", { class: "ext", href: t3kLink(t.url), target: "_blank", rel: "noopener" }, "Open on TONE3000 ", icon("ext")));
}
function renderT3k() {
  const el = $("t3k");
  el.textContent = "";
  if (!has("nam")) { el.hidden = true; return; }
  el.hidden = false;
  if (!t3kSignedIn()) {
    fill(el,
      h("div", { class: "card-head" }, logo()),
      h("p", { class: "t3k-pitch", text: "Amp, pedal and full-rig captures shared by players, free to use. OpenPocket plays their NAM A2 captures." }),
      h("p", { class: "dim", text: "TONE3000 opens in its own window (in this page, in a browser without windows): sign in there, search and listen, and choose a tone. It comes back here with its captures, ready for a slot." }),
      h("button", { type: "button", class: "btn primary", id: "t3kbrowse", disabled: t3k.busy, onclick: browse }, t3k.busy ? "Waiting for the TONE3000 window…" : "Browse TONE3000"),
      t3k.error && h("p", { class: "warn", role: "alert" }, icon("alert"), ` TONE3000: ${t3k.error}`),
      h("p", { class: "dim small", text: "The sign-in lasts while this page is open and is not stored. The captures you get are kept in the library on this computer." }));
    return;
  }
  const u = t3k.user;
  fill(el, h("div", { class: "card-head" }, logo(), h("span", { class: "grow" }),
    u && h("a", { class: "tone-u me", href: t3kLink(u.url), target: "_blank", rel: "noopener", title: "Signed in to TONE3000" }, avatar(u), creator(u)),
    h("button", { type: "button", class: "btn small", text: "Sign out", onclick: () => { t3kSignOut(); Object.assign(t3k, { user: null, lists: {}, tone: null, models: null, error: "" }); renderT3k(); } })),
    h("button", { type: "button", class: "btn primary", id: "t3kbrowse", disabled: t3k.busy, onclick: browse }, h("span", { class: "t3k-mark", text: "T3K" }), t3k.busy ? " Waiting for the TONE3000 window…" : " Search all of TONE3000"));
  if (t3k.error) el.append(h("p", { class: "warn", role: "alert" }, icon("alert"), ` TONE3000: ${t3k.error}`));
  if (t3k.tone) { el.append(toneDetail()); return; }
  const list = t3k.lists[t3k.tab], meta = T3K_LISTS.find((l) => l.id === t3k.tab);
  el.append(
    h("div", { class: "tabs2", role: "tablist", "aria-label": "TONE3000 lists" }, T3K_LISTS.map((l) =>
      h("button", { type: "button", role: "tab", "aria-selected": l.id === t3k.tab ? "true" : "false", text: l.label, onclick: () => loadList(l.id) }))),
    h("div", { class: "tones", id: "t3klist", role: "tabpanel", "aria-label": meta.label },
      list === undefined ? [1, 2, 3].map(() => h("div", { class: "tone-skel" }))
        : list === null ? h("p", { class: "dim empty-note", text: "This list could not be read. Search all of TONE3000 instead." })
        : list.length ? list.map(toneRow) : h("p", { class: "dim empty-note", text: meta.empty })));
}

// TONE3000 in this tab (t3kSelectHere): what t3k.html brought back, into the library before the pedal is connected
async function t3kCameBack() {
  const n = t3kNoteRead();
  if (!n) return;
  if (n.error) { say(n.error === "cancelled" ? "TONE3000: no tone was chosen." : `TONE3000: ${String(n.error).replace(/^TONE3000: /, "")}`, n.error === "cancelled" ? "info" : "err"); return; }
  const got = [];
  for (const c of Array.isArray(n.captures) ? n.captures : []) {
    try {
      const bytes = Uint8Array.from(atob(c.bytes), (ch) => ch.charCodeAt(0));
      if (bytes.length !== SLOT_BYTES || typeof c.name !== "string") continue;
      got.push((await lib.add({ kind: "nam", name: c.name, bytes, detail: String(c.detail || ""), source: c.source && c.source.from === "tone3000" ? c.source : { from: "file", file: c.name } })).name);
    } catch (_) { }
  }
  if (got.length) say(`${got.join(", ")} from TONE3000 ${got.length === 1 ? "is" : "are"} in the library: once the pedal is connected, send ${got.length === 1 ? "it" : "them"} from Models.`, "ok");
}

export async function initModels() {
  lib = await Library.open();
  for (const k of ["nam", "ir"]) {
    const inp = $(BANKS[k].input);
    inp.onchange = () => { const f = [...inp.files]; inp.value = ""; if (f.length && has(k)) addFiles(k, f, aim[k]); };
  }
  await t3kCameBack();
  addEventListener("pageshow", (e) => { if (e.persisted && t3k.busy) { t3k.busy = false; renderT3k(); } });   // (back from TONE3000 by the browser's Back)
  on("assets", (kind) => { render(kind); if (kind === "nam") renderT3k(); });
  on("bank", () => { if (app.conn === "ready") for (const k of ["nam", "ir"]) if (has(k)) render(k); });
  on("conn", () => { if (app.conn !== "ready") { queue.nam = []; queue.ir = []; } for (const k of ["nam", "ir"]) render(k); renderT3k(); });
  for (const k of ["nam", "ir"]) dropTarget($(k), BANKS[k].accept, (files) => { if (has(k)) addFiles(k, files); });
}
