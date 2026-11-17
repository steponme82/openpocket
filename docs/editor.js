// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket web editor: the start. Finding the pedal and every way that can fail, the sections and the way between
// them, the DSP load, the keyboard. The sections themselves: edsound.js, edpresets.js, edmodels.js, edmidi.js.

import { app, $, h, icon, on, emit, say, busy } from "./edcore.js";
import { Editor } from "./pxedit.js";
import { initPresets, loadBank, refreshSlots, refreshBank, stepPreset, save, comparing, compareSlotChanged, compareBeforeUnload } from "./edpresets.js";
import { initSound } from "./edsound.js";
import { initModels, loadAssets } from "./edmodels.js";
import { initMidi } from "./edmidi.js";
import { initScenes } from "./edscenes.js";

let pingTimer = null, connecting = false, progress = 0;

// ---- the connection ----
function setConn(c) {
  app.conn = c;
  document.body.dataset.conn = c;
  const up = c === "ready";
  $("conn").hidden = !(up || c === "lost");
  $("load").hidden = !up;
  $("lostbar").hidden = c !== "lost";
  $("shell").inert = c === "lost";
  if (c === "lost") $("status").textContent = "Disconnected";
  renderGate();
  emit("conn");
}
async function connect() {
  if (connecting || app.conn === "ready") return;
  if (!navigator.requestMIDIAccess) { setConn("unsupported"); return; }
  connecting = true;
  const was = app.conn;
  try {
    if (was !== "lost") setConn("searching");
    try { app.access = app.access || await navigator.requestMIDIAccess({ sysex: true }); }
    catch (_) { setConn("denied"); return; }
    app.access.onstatechange = onPorts;
    const e = new Editor(app.access);
    const info = await e.connect();
    if (!info) { setConn(was === "lost" ? "lost" : "notfound"); return; }
    app.ed = e; app.info = info;
    e.onLost = lost;
    if (was !== "lost") { progress = 0; setConn("loading"); }
    app.cat = await e.catalog((f) => { progress = f; const b = $("gatebar"); if (b) b.style.width = `${Math.round(f * 100)}%`; });
    app.st = await e.state();
    app.focus = app.st.focus;
    app.slots = await e.list();
    await loadAssets();
    e.onState = onPush;
    await e.watch(true);
    $("status").textContent = `${info.identity.replace(/_9?\d+$/, "")} · OpenPocket ${info.version}`;
    setConn("ready");
    emit("slots"); emit("state", {});
    clearInterval(pingTimer);
    pingTimer = setInterval(ping, 1000);
    pinged = Date.now();
    ping();
    loadBank();
    if (was === "lost") say("The pedal is back.", "ok");
  } catch (e) {
    if (app.ed) app.ed.close();
    app.ed = null;
    setConn(was === "lost" ? "lost" : "notfound");
    if (was !== "lost") say(`The pedal stopped answering while it was read (${e.message}). Check the cable and try again.`, "err");
  } finally { connecting = false; }
}
function lost() {
  if (app.conn !== "ready") return;
  clearInterval(pingTimer);
  if (app.ed) app.ed.close();
  app.ed = null;
  setConn("lost");
}
// v42: the pedal leaves USB on purpose (USB AUDIO changed, edmidi.js): let go of it, and look again once it is back
// (it comes back as another USB device about 3 s later; a port change finds it sooner)
on("reconnect", () => { lost(); setTimeout(() => { if (app.conn === "lost") connect(); }, 3500); });
// a port came or went: a pedal plugged back in is found again; the MIDI section lists the other devices
function onPorts() {
  emit("ports");
  if (app.conn === "lost" || app.conn === "notfound") connect();
}
let pinged = 0;
async function ping() {
  if (!app.ed) return;
  try {
    // a tab left in the background pings too seldom, and the pedal stops pushing 5 s after the last request: watch again
    const gap = Date.now() - pinged;
    pinged = Date.now();
    if (gap > 4000 && app.conn === "ready") { await app.ed.watch(true); if (!comparing()) { app.st = await app.ed.state(); app.focus = app.st.focus; emit("state", { push: true }); } }
    app.ping = await app.ed.ping(); renderLoad(); emit("ping");
  } catch (_) { }
}

// a change made on the pedal
function onPush(s) {
  const slotChanged = !app.st || s.slot !== app.st.slot, saved = app.st && app.st.dirty && !s.dirty && !slotChanged;
  if (comparing()) { if (!slotChanged) return; compareSlotChanged(); }
  app.st = s;
  app.focus = s.focus;
  if (slotChanged || saved) { refreshSlots().catch(() => { }); refreshBank([s.slot]).catch(() => { }); }   // (SAVE on the pedal itself)
  emit("state", { push: true });
}

// ---- before the pedal answers ----
const GATE = {
  idle: () => [
    h("h1", { text: "Connect your pedal" }),
    h("ol", { class: "steps" },
      h("li", {}, h("b", { text: "Plug it in. " }), "Pocket X or BlackBox, with a USB-C cable that carries data."),
      h("li", {}, h("b", { text: "Press Connect. " }), "The browser asks to use MIDI devices: allow it."),
      h("li", {}, h("b", { text: "Play. " }), "Every change here sounds at once, and what you change on the pedal shows here.")),
    connectBtn("Connect"),
    h("p", { class: "dim" }, "The pedal needs OpenPocket v32 or newer. ", h("a", { href: "./", text: "Install or update it" }), " first if it runs something older."),
    h("ul", { class: "what" },
      h("li", {}, h("b", { text: "Presets" }), "Browse, rename, reorder, copy and back up the 52 presets."),
      h("li", {}, h("b", { text: "Sound" }), "The signal chain and every block's settings, live."),
      h("li", {}, h("b", { text: "Models" }), "Amp captures from TONE3000 or your files, and cabinet IRs."),
      h("li", {}, h("b", { text: "MIDI" }), "The pedal's control numbers, and your own controller.")),
  ],
  unsupported: () => [
    h("h1", { text: "This browser cannot reach the pedal" }),
    h("p", {}, "The editor talks to the pedal over Web MIDI, which this browser does not have. Open this page in ", h("b", { text: "Chrome or Edge" }), " on a computer or an Android phone."),
    h("p", { class: "dim" }, "Safari and browsers on iPhone and iPad have no Web MIDI. ", h("a", { href: "./", text: "Back to the installer" })),
  ],
  denied: () => [
    h("h1", { text: "MIDI access was refused" }),
    h("p", {}, "The browser did not let this page use MIDI devices, so it cannot see the pedal."),
    h("ol", { class: "steps" },
      h("li", {}, "Click the icon left of the address, then ", h("b", { text: "Site settings" }), "."),
      h("li", {}, "Set ", h("b", { text: "MIDI devices" }), " (\"control and reprogram\") to Allow."),
      h("li", {}, "Come back and try again.")),
    connectBtn("Try again"),
  ],
  searching: () => [h("h1", { text: "Looking for the pedal…" }), h("p", { class: "dim", text: "If the browser asks to use MIDI devices, allow it." })],
  notfound: () => {
    const seen = app.access ? [...app.access.inputs.values()].filter((i) => i.state !== "disconnected").map((i) => i.name || "unnamed device") : [];
    return [
      h("h1", { text: "No pedal answered" }),
      h("p", { id: "seen", text: seen.length ? `The browser sees ${seen.length === 1 ? "this MIDI device" : "these MIDI devices"}: ${seen.join(", ")}. None answered as a pedal running OpenPocket.` : "The browser sees no MIDI device at all." }),
      h("ul", { class: "check" },
        h("li", { text: "Is the pedal switched on and past its start screen?" }),
        h("li", { text: "Does the cable carry data? Many USB-C cables only charge." }),
        h("li", {}, "Does it run OpenPocket v32 or newer? ", h("a", { href: "./", text: "Install or update it" }), "."),
        h("li", { text: "Is another tab or program using it (the installer, a DAW)? Close that first." })),
      connectBtn("Try again"),
      h("p", { class: "dim", text: "The editor also tries again by itself as soon as a device is plugged in." }),
    ];
  },
  loading: () => [
    h("h1", { text: "Reading the pedal…" }),
    h("p", { class: "dim", text: "Its blocks, types and settings are read once per firmware version, then remembered." }),
    h("div", { class: "gate-bar" }, h("i", { id: "gatebar", style: `width:${Math.round(progress * 100)}%` })),
  ],
};
function connectBtn(text) { return h("button", { type: "button", class: "btn primary big", id: "connect", onclick: connect }, icon("usb"), ` ${text}`); }
function renderGate() {
  const g = $("gate"), build = GATE[app.conn];
  g.hidden = !build;
  g.textContent = "";
  if (build) g.append(h("div", { class: "gate-in" }, build()));
}

// ---- the sections ----
const VIEWS = ["presets", "sound", "models", "midi"];
function setView(v, push = true) {
  if (!VIEWS.includes(v)) v = "sound";
  app.view = v;
  document.body.dataset.view = v;
  for (const b of $("tabs").children) { if (b.dataset.go === v) b.setAttribute("aria-current", "page"); else b.removeAttribute("aria-current"); }
  if (push && location.hash.slice(1) !== v) history.replaceState(null, "", `#${v}`);
  emit("view");
}

// ---- the DSP load ----
function loadLevel(p) { return p.bypass ? "over" : p.load >= 85 ? "high" : p.load >= 70 ? "warm" : "ok"; }
function renderLoad() {
  const p = app.ping;
  if (!p) return;
  const lv = loadLevel(p), el = $("load");
  el.dataset.level = lv;
  $("loadv").textContent = p.bypass ? "BYPASS" : `${p.load}%`;
  $("loadbar").style.width = `${Math.min(100, p.load)}%`;
  $("loadpeak").style.left = `${Math.min(100, p.peak ?? p.load)}%`;
  $("loadpeak").hidden = p.peak === undefined;
  el.setAttribute("aria-label", p.bypass ? "DSP overload: the pedal bypasses the effects" : `DSP load ${p.load} percent`);
  if ($("loadpop").matches(":popover-open")) renderLoadPop();
}
function renderLoadPop() {
  const p = app.ping, pop = $("loadpop"), lv = loadLevel(p);
  const words = { ok: "Room to spare.", warm: "Getting full: another heavy block may not fit.", high: "Near the limit. One more block can push the pedal into its overload guard.",
    over: "Overload guard: the pedal passes the guitar through without effects. Switch a block off, or choose a lighter type." };
  pop.textContent = "";
  pop.append(h("h2", { text: "DSP load" }), h("p", { class: lv === "ok" ? "dim" : "warn", text: words[lv] }),
    h("dl", { class: "facts" },
      h("dt", { text: "Now" }), h("dd", { text: `${p.load} %${p.c1 ? "" : " (on the main core)"}` }),
      p.peak !== undefined && [h("dt", { text: "Peak, this preset" }), h("dd", { text: `${p.peak} %` }), h("dt", { text: "Peak since power-on" }), h("dd", { text: `${p.peakAll} %` }),
        h("dt", { text: "Late audio blocks" }), h("dd", { text: p.xrun ? `${p.xrun} since power-on (each one is a click)` : "none" }),
        h("dt", { text: "Blocks at the clipper" }), h("dd", { text: p.clip ? `${p.clip} since power-on: lower the preset LEVEL` : "none" })]));
  if (p.prof && app.st) {
    const rows = app.st.preset.order.filter((b) => p.prof[b]).map((b) => ({ name: app.cat.blocks[b].label, v: p.prof[b], c: app.cat.blocks[b].color }));
    if (p.profNam) rows.push({ name: "NAM network", v: p.profNam, c: "var(--acc)" });
    if (rows.length) pop.append(h("h3", { text: "Where it goes" }), h("div", { class: "prof" }, rows.map((r) =>
      h("div", { class: "prof-r" }, h("span", { text: r.name }), h("span", { class: "prof-b" }, h("i", { style: `width:${Math.min(100, r.v)}%;background:${r.c}` })), h("span", { text: `${r.v} %` })))));
  }
}
function renderConnPop() {
  const pop = $("connpop"), i = app.info;
  pop.textContent = "";
  if (!i) return;
  pop.append(h("h2", { text: i.identity.replace(/_9?\d+$/, "") }),
    h("dl", { class: "facts" }, h("dt", { text: "Firmware" }), h("dd", { text: `OpenPocket ${i.version}` }), h("dt", { text: "Port" }), h("dd", { text: i.port }),
      h("dt", { text: "Presets" }), h("dd", { text: `${i.nslot}, ${app.slots.filter((s) => s.saved).length} of yours` })),
    h("h3", { text: "Keys" }),
    h("dl", { class: "keys" }, h("dt", { text: "/" }), h("dd", { text: "search the presets" }), h("dt", { text: "[  ]" }), h("dd", { text: "previous, next preset" }),
      h("dt", { text: "Ctrl or ⌘ + S" }), h("dd", { text: "save" }), h("dt", { text: "F2" }), h("dd", { text: "rename the preset chosen in the list" })),
    h("a", { class: "btn small", href: "./", text: "Installer and updates" }));
}

// ---- start ----
initPresets(); initSound(); initMidi(); initScenes();
await initModels();
on("view-go", (v) => setView(v));
$("reconnect").onclick = connect;
$("tabs").onclick = (e) => { const b = e.target.closest("button"); if (b) setView(b.dataset.go); };
$("loadpop").addEventListener("beforetoggle", (e) => { if (e.newState === "open") renderLoadPop(); });
$("connpop").addEventListener("beforetoggle", (e) => { if (e.newState === "open") renderConnPop(); });
addEventListener("hashchange", () => setView(location.hash.slice(1), false));
addEventListener("pagehide", compareBeforeUnload);
addEventListener("beforeunload", (e) => { if (busy()) e.preventDefault(); });
addEventListener("keydown", (e) => {
  if (app.conn !== "ready" || $("dlg").open) return;
  const typing = e.target.matches("input:not([type=range]), select, textarea");
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") { e.preventDefault(); if (app.st.dirty) save(); return; }
  if (typing || e.ctrlKey || e.metaKey || e.altKey) return;
  if (e.key === "/") { e.preventDefault(); if (matchMedia("(max-width: 899px)").matches) setView("presets"); $("psearch").focus(); }
  else if (e.key === "[") stepPreset(-1);
  else if (e.key === "]") stepPreset(1);
});
setView(location.hash.slice(1) || "sound", false);
setConn(navigator.requestMIDIAccess ? "idle" : "unsupported");
// a pedal this page was already allowed to use: no need to press Connect again
if (navigator.permissions && navigator.requestMIDIAccess) {
  navigator.permissions.query({ name: "midi", sysex: true }).then((s) => { if (s.state === "granted" && app.conn === "idle") connect(); }, () => { });
}
