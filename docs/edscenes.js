// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket web editor: the preset's scenes (editor protocol 6, the Bluetooth audio image's test build 49; docs/MIDI.md,
// "Scenes"). Four sets of blocks on and off a preset; recalling one switches the blocks to it, as CC 65 to 68 and the
// footswitches do on the pedal. The scene playing is the blocks as they are now: switching a block changes it, as on the
// pedal. Another scene's blocks are set here directly. SAVE keeps them with the sound; recalling one is no change.

import { app, $, h, on, blk, say } from "./edcore.js";
import { setOn } from "./edsound.js";
import { refreshState } from "./edpresets.js";

const N = 4;
let sc = null;          // the pedal's scenes (pxedit.js scenes()), or null: none in this pedal (or not read yet)
let slot = -1;          // the preset they were read for
let open = false;       // the grid shown
let busy = false;

async function load() {
  sc = null;
  slot = app.st ? app.st.slot : -1;
  if (app.conn === "ready" && app.ed && app.ed.hasScenes && app.st) {
    try { sc = await app.ed.scenes(); } catch (_) { sc = null; }
  }
  render();
}
async function change(fn) {
  if (busy) return;
  busy = true;
  try {
    const r = await fn();
    if (r.rc === 0) sc = r; else say("The pedal refused it.", "err");
    await refreshState();                          // (the blocks, and whether the preset differs from its saved one)
  } catch (e) { say(`Not changed: ${e.message}`, "err"); }
  finally { busy = false; render(); }
}
const stored = (n) => !!((sc.set >> n) & 1);
const nowMask = () => app.st.preset.on.reduce((m, v, b) => m | (v ? 1 << b : 0), 0);
const maskOf = (n) => (n === sc.cur ? nowMask() : sc.masks[n]);   // (the scene playing: the blocks as they are)
function toggle(n, b) {
  if (n === sc.cur) { setOn(b, !app.st.preset.on[b]); return; }   // (the scene playing keeps what is switched)
  change(() => app.ed.sceneSet(n, maskOf(n) ^ (1 << b)));
}

function render() {
  const el = $("scenes");
  if (!el) return;
  el.textContent = "";
  el.hidden = !sc || app.conn !== "ready" || !app.st;
  if (el.hidden) return;
  const order = app.st.preset.order;
  const btn = (n) => {
    const playing = n === sc.cur, known = playing || stored(n), m = maskOf(n);
    const names = order.filter((b) => (m >> b) & 1).map((b) => blk(b).label);
    return h("button", { type: "button", class: `sc-b${playing ? " on" : ""}${known ? "" : " empty"}`, "data-scene": n, "aria-pressed": playing ? "true" : "false",
      title: !known ? "Not stored yet: recalled, it takes the blocks as they are" : names.length ? `On: ${names.join(", ")}` : "Every block off",
      onclick: () => { if (!playing) change(() => app.ed.sceneRecall(n)); } },
      h("span", { class: "sc-n", text: `Scene ${n + 1}` }),
      h("span", { class: "sc-dots", "aria-hidden": "true" }, order.map((b) => h("i", { class: (m >> b) & 1 && known ? "on" : "", style: `--c:${blk(b).color}` }))));
  };
  el.append(
    h("div", { class: "sc-bar" },
      h("span", { class: "sc-l", text: "Scenes" }),
      Array.from({ length: N }, (_, n) => btn(n)),
      h("button", { type: "button", class: "btn small", id: "scedit", "aria-expanded": open ? "true" : "false", "aria-controls": "scgrid",
        text: open ? "Close" : "Edit scenes", onclick: () => { open = !open; render(); } })),
    open && grid(order));
}
function grid(order) {
  return h("div", { class: "sc-grid", id: "scgrid" },
    h("div", { class: "sc-wrap" }, h("table", {},
      h("thead", {}, h("tr", {}, h("th", { scope: "col", text: "Block" }),
        Array.from({ length: N }, (_, n) => h("th", { scope: "col" }, `Scene ${n + 1}`, n === sc.cur ? h("small", { text: "playing" }) : !stored(n) ? h("small", { text: "not stored" }) : null)))),
      h("tbody", {}, order.map((b) => h("tr", { style: `--c:${blk(b).color}` },
        h("th", { scope: "row", text: blk(b).label }),
        Array.from({ length: N }, (_, n) => h("td", {}, h("input", { type: "checkbox", checked: !!((maskOf(n) >> b) & 1),
          "aria-label": `${blk(b).label} in scene ${n + 1}`, onchange: () => toggle(n, b) })))))))),
    h("p", { class: "dim small", text: "The scene playing is the blocks as they are: switch one and the scene keeps it. Another scene's blocks are set here, and setting one stores it. Save the preset to keep its scenes; recalling a scene is not a change." }),
    h("div", { class: "sc-forget" }, Array.from({ length: N }, (_, n) => n).filter((n) => stored(n) && n !== sc.cur).map((n) =>
      h("button", { type: "button", class: "btn small", text: `Forget scene ${n + 1}`, title: "Not stored any more: recalled, it takes the blocks as they are",
        onclick: () => change(() => app.ed.sceneForget(n)) }))));
}

export function initScenes() {
  on("conn", () => { open = false; load(); });
  // another preset, or a change on the pedal (a scene recalled by a footswitch or a controller): read again; else drawn again
  on("state", (o) => {
    if (!app.st || app.conn !== "ready") return;
    if (slot !== app.st.slot || (o && o.push)) load(); else render();
  });
}
