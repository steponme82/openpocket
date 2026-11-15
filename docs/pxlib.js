// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket web editor: the files a user loads into the pedal's slots (NAM models, cabinet IRs), kept in the browser.
// The pedal stores a model but never sends it back (docs/EDITOR-PROTOCOL.md has no read command), so moving a model to
// another slot, renaming it, or keeping more of them than the pedal has slots needs a copy here: the library
// (IndexedDB; in memory when the browser has none). Plus the arithmetic between a preset and the slot it plays.

// ---- a preset's MODEL / IR setting (0..100) <-> the slot it plays (engine/px_dsp.c: px_nam_model, px_nam_knob, px_ir_slot) ----
export const knobIndex = (v, n) => Math.floor(Math.max(0, Math.min(100, v)) * n / 101);
export function indexKnob(i, n) {                 // the middle of the index's span of the knob, as the pedal's own knob
  let lo = -1, hi = -1;
  for (let v = 0; v <= 100; v++) if (knobIndex(v, n) === i) { if (lo < 0) lo = v; hi = v; }
  return lo < 0 ? 0 : (lo + hi) >> 1;
}

// The two kinds of slot: where a preset names one (block, type, setting 0), how many come before the user's
// (`base`: the factory models), and how the slots read on the pedal.
export const KINDS = {
  nam: { block: "AMP", type: "NAM", tag: (s) => `U${String(s + 1).padStart(2, "0")}`, thing: "capture", things: "captures", without: "an amp" },
  ir: { block: "CAB", type: "USER IR", tag: (s) => `IR${s + 1}`, thing: "IR", things: "IRs", without: "a cabinet" },
};
// where presets name a slot of this kind, in this pedal's catalogue: { b, t, base, n } or null (a firmware without it)
export function slotRef(cat, kind, base, slots) {
  const k = KINDS[kind], b = cat.blocks.findIndex((x) => x.label === k.block);
  const t = b < 0 ? -1 : cat.blocks[b].types.findIndex((x) => x && x.name === k.type);
  return t < 0 ? null : { b, t, base, n: base + slots };
}
// the user slot a preset plays (-1: none, or a factory model)
export function presetSlot(preset, ref) {
  if (!ref || preset.type[ref.b] !== ref.t) return -1;
  return knobIndex(preset.p[ref.b][0], ref.n) - ref.base;
}
// the preset with its slot changed by `map` (slot -> slot), or null when it plays none of them
export function retarget(preset, ref, map) {
  const s = presetSlot(preset, ref);
  if (s < 0 || !map.has(s)) return null;
  const q = { name: preset.name, on: [...preset.on], type: [...preset.type], p: preset.p.map((r) => [...r]), order: [...preset.order] };
  q.p[ref.b][0] = indexKnob(ref.base + map.get(s), ref.n);
  return q;
}
// which saved presets play each slot: Map(slot -> [preset slot, ...]); bank: [{ saved, preset }] (holes allowed)
export function usage(bank, ref) {
  const out = new Map();
  bank.forEach((e, i) => {
    if (!e || !e.saved) return;
    const s = presetSlot(e.preset, ref);
    if (s >= 0) { if (!out.has(s)) out.set(s, []); out.get(s).push(i); }
  });
  return out;
}

// ---- a stored file: the bytes the pedal keeps (64-byte header: the name at 8, 12 bytes, 0-ended; then the data) ----
export function packedName(bytes) {
  let s = "";
  for (let i = 8; i < 20 && bytes[i]; i++) s += String.fromCharCode(bytes[i]);
  return s;
}
export const cleanSlotName = (s) => String(s || "").toUpperCase().replace(/[^ -~]/g, "?").trim().slice(0, 11).trim();
// test build 40: a name a player reads, 11 characters at most, for a capture taken from a TONE3000 tone. A tone's models are
// often named by short codes ("VX", "FD", "CH1"): the tone's first words that fit go before the model's name ("VOX AC30 VX",
// "KLON GAIN 2"). Whole words only; words that name nothing (AMP, CAPTURE, NAM, PACK...) are left out.
const NAME_NOISE = new Set(["THE", "A", "AN", "OF", "BY", "AND", "WITH", "AMP", "AMPS", "AMPLIFIER", "CAPTURE", "CAPTURES", "NAM",
  "A1", "A2", "PACK", "PROFILE", "PROFILES", "TONE", "TONES", "DI", "MODEL", "MODELS", "FULL", "RIG", "COLLECTION", "SET"]);
export function smartName(title, model) {
  const up = (x) => String(x || "").toUpperCase().replace(/[^ -~]/g, " ");
  const mw = up(model).trim().split(/\s+/).filter(Boolean);
  let m = "";
  for (const w of mw) { const t = m ? `${m} ${w}` : w; if (t.length > 11) break; m = t; }   /* whole words, 11 at most */
  if (!m && mw.length) m = mw[0].slice(0, 11);
  const words = up(title).split(/[\s_|/,:;()[\]]+/).filter((w) => w && /[A-Z0-9]/.test(w) && !NAME_NOISE.has(w) && w !== m);
  const room = 11 - (m ? m.length + 1 : 0);
  let pre = "";
  for (const w of words) {                     /* whole words in a row; a first one too long for the room is passed over */
    const t = pre ? `${pre} ${w}` : w;
    if (t.length <= room) pre = t;
    else if (pre) break;
  }
  if (pre) return m ? `${pre} ${m}` : pre;
  return m || cleanSlotName(words[0] || "MODEL");
}
export function renamePacked(bytes, name) {
  const out = Uint8Array.from(bytes), nm = cleanSlotName(name);
  for (let i = 0; i < 12; i++) out[8 + i] = i < nm.length ? nm.charCodeAt(i) : 0;
  return out;
}
const sameData = (a, b) => { if (a.length !== b.length) return false; for (let i = 64; i < a.length; i++) if (a[i] !== b[i]) return false; return true; };

// ---- the library ----
// An entry: { id, kind, name, bytes: Uint8Array, added (ms), detail (a line: its level, what was done to it),
//   home (the slot it was last removed from, where "send to the pedal" puts it back),
//   source: { from: "file", file } | { from: "tone3000", toneId, title, creator, creatorUrl, url, gear, format, image, model } }
// A link says which entry a pedal's slot holds: it stands only while the slot still reads the entry's name.
const DB = "openpocket-library", STORE = "files", LINKS = "openpocket-library-links";
const idb = (req) => new Promise((res, rej) => { req.onsuccess = () => res(req.result); req.onerror = () => rej(req.error); });

export class Library {
  constructor(db, storage) { this.db = db; this.storage = storage; this.mem = new Map(); this.memLinks = {}; }
  static async open(scope = globalThis) {
    let db = null;
    try {
      if (scope.indexedDB) {
        const req = scope.indexedDB.open(DB, 1);
        req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: "id" });
        db = await idb(req);
      }
    } catch (_) { db = null; }                     // (private windows, blocked storage: the session's memory holds it)
    let storage = null;
    try { storage = scope.localStorage || null; } catch (_) { }
    return new Library(db, storage);
  }
  get persistent() { return !!this.db; }
  _store(mode) { return this.db.transaction(STORE, mode).objectStore(STORE); }
  async all(kind) {
    const list = this.db ? await idb(this._store("readonly").getAll()) : [...this.mem.values()];
    return list.filter((e) => e.kind === kind).sort((a, b) => a.name.localeCompare(b.name) || a.added - b.added);
  }
  async get(id) { return (this.db ? await idb(this._store("readonly").get(id)) : this.mem.get(id)) || null; }
  async put(entry) { if (this.db) await idb(this._store("readwrite").put(entry)); else this.mem.set(entry.id, entry); return entry; }
  async remove(id) { if (this.db) await idb(this._store("readwrite").delete(id)); else this.mem.delete(id); }
  // the same data again (a file added twice): the entry already there
  async add(entry) {
    const twin = (await this.all(entry.kind)).find((e) => sameData(e.bytes, entry.bytes));
    if (twin) return twin;
    return this.put({ ...entry, id: entry.id || newId(), added: entry.added || Date.now() });
  }
  // links: { [pedal]: { nam: { [slot]: id }, ir: { ... } } }
  _links() {
    try { return this.storage ? JSON.parse(this.storage.getItem(LINKS) || "{}") : this.memLinks; } catch (_) { return {}; }
  }
  links(pedal, kind) { return { ...((this._links()[pedal] || {})[kind] || {}) }; }
  setLink(pedal, kind, slot, id) {
    const all = this._links();
    const k = ((all[pedal] ||= {})[kind] ||= {});
    if (id) k[slot] = id; else delete k[slot];
    if (this.storage) { try { this.storage.setItem(LINKS, JSON.stringify(all)); } catch (_) { } } else this.memLinks = all;
  }
  // the entry each slot of the pedal holds: slots [{ slot, used, name }] -> Map(slot -> entry); stale links are dropped
  async onPedal(pedal, kind, slots) {
    const links = this.links(pedal, kind), out = new Map();
    for (const s of slots) {
      const id = links[s.slot];
      if (!id) continue;
      const e = s.used ? await this.get(id) : null;
      if (e && e.kind === kind && e.name === s.name) out.set(s.slot, e); else this.setLink(pedal, kind, s.slot, null);
    }
    return out;
  }
}
export const newId = () => (globalThis.crypto?.randomUUID ? globalThis.crypto.randomUUID() : `id-${Date.now()}-${Math.random().toString(36).slice(2)}`);

// a pedal across firmware versions: "PocketX_940" -> "PocketX"
export const pedalKey = (identity) => String(identity || "pedal").replace(/_\d+$/, "");

// where a move of slot `from` to slot `to` leaves the files: [[slot, entry | null], ...] to write, and the map presets
// follow. `held`: Map(slot -> entry) of what the library knows is on the pedal; `used`: (slot) => the pedal has something
// there. A slot whose file is not in the library cannot move (the pedal does not send it back): -> { error }
export function planMove(from, to, held, used) {
  const a = held.get(from);
  if (!a) return { error: "This file is not in this browser's library, and the pedal cannot send it back. Load its file again, then move it." };
  if (from === to) return { writes: [], map: new Map() };
  if (!used(to)) return { writes: [[to, a], [from, null]], map: new Map([[from, to]]) };
  const b = held.get(to);
  if (!b) return { error: "The other slot holds a file that is not in this browser's library, so the two cannot change places." };
  return { writes: [[to, a], [from, b]], map: new Map([[from, to], [to, from]]) };
}
