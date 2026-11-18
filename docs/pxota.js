// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the M-UPGRADE update protocol over a Web MIDI-shaped access object (the installer page passes Web MIDI; tools/midi-node.mjs passes a Node adapter), for the Donner Pocket X and the M-VAVE BlackBox.
// Adapted from Felucca's web/fm1ota.js (GPL-3.0, Leo Kuroshita / Hügelton Instruments), used by CLIPPER and SLOOP.
//
// Step 1: the running firmware (the official one, or OpenPocket's ota.c) reads parts of the package and stages its
// update loader, then resets into it. Step 2: the loader reads the whole image and writes it. In both steps the
// device sends cmd 0x30 read requests on the package's logical image and the page answers them.

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const UBOOT_KEY = [0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7];
const MAXDATA = 512;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export const MODELS = ["PocketX", "BlackBox"];
export const IS_OTA = (id) => /^ota-/i.test(id.model);
export const OUR_LOADER = (id) => /^ota-OpenPocket_9\d\d$/i.test(id.text);
export const IS_OPENPOCKET = (id) => MODELS.includes(id.model) && id.version >= 900;
export const IS_OFFICIAL = (id) => MODELS.includes(id.model) && id.version < 900;
// M-VAVE's BlackBox firmware V22 (2026-11-12) and later put the app area at 0x6000, behind a boot loader that runs to
// 0x5CA0; OpenPocket's update loader writes from 0x4000, so installing over them would overwrite that boot loader and
// the pedal would no longer start (isod89/openpocket#31, #33). Refused before anything is sent; the loader itself also
// checks the pedal's own flash head (loader/ldr_core.c 1b) and refuses it, whatever the pedal says it runs
export const NEW_LAYOUT = (id) => id.model === "BlackBox" && IS_OFFICIAL(id) && id.version >= 22;
// the MIDI ports the page may probe: OpenPocket and its loader, and the names the official firmware's USB device gets
// (its descriptors say "USB Composite Device" / "USB-Midi" / "Jieli Technology"; the system may add its own words)
export const PORT_RE = /openpocket|pocket|blackbox|black.?box|ota|usb.?midi|composite|jieli|sinco|m-?vave|donner/i;

// errors carry a code the page translates: notfound, model, stopped, noloader, lost, noreturn, mismatch, badreq,
// foreign (detail: the loader's identity), layout (the pedal's flash layout is M-VAVE V22's or later: nothing written;
// detail: the pedal's identity, or the loader's reason)
function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }
// the update loader's refusal of the pedal's flash head (loader/ldr_core.c ldr_head_ok): nothing written, the pedal restarts
// on its own firmware
const HEAD_WHY = ["", "head unreadable", "head unreadable", "app area not at 0x4000", "boot loader past 0x4000", "no app area"];
const refused = (why) => fail("layout", `the update loader refused this pedal's flash layout (${HEAD_WHY[why] || why}): nothing was written`,
  HEAD_WHY[why] || String(why));

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

export function parseIdentity(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0[\s\S]*$/, "");
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : null;
}

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  send(bytes) { try { this.output.send(bytes); return true; } catch (_) { return false; } }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + 1000;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

// timings (ms) as the hardware needs them; the tests shorten them
export const TIMING = { afterUpgrade: 2000, afterStep1: 3000, loaderWait: 30000, step1Idle: 8000, step2Idle: 180000,
  afterWrite: 3000, backWait: 40000, perRequest: 10, poll: 1000, rescuePoll: 100 };

export class Updater {
  constructor(access, log, timing = {}) {
    this.access = access;
    this.log = log || (() => {});
    this.t = { ...TIMING, ...timing };
  }

  // the first in/out pair (same port name) that answers the handshake and passes filter
  async find(filter) {
    const outs = [...this.access.outputs.values()];
    for (const input of this.access.inputs.values()) {
      if (input.state === "disconnected") continue;
      if (!PORT_RE.test(input.name || "")) continue;
      const output = outs.find((o) => o.name === input.name) || (outs.length === 1 ? outs[0] : null);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      const link = new Link(input, output);
      const id = await handshake(link, 2);
      if (id && (!filter || filter(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  // what is connected, for the status line: { id, name } or null (the link closed)
  async identify() {
    const dev = await this.find((id) => MODELS.includes(id.model) || IS_OTA(id));
    if (!dev) return null;
    dev.link.close();
    return { id: dev.id, name: dev.name };
  }

  seen() {
    const names = [...this.access.inputs.values()].filter((p) => p.state !== "disconnected").map((p) => p.name || "?");
    return names.length ? ` (MIDI: ${names.join(", ")})` : " (MIDI: -)";
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await sleep(this.t.poll);
    }
    return null;
  }

  // answer read requests until the device asks for finishAddr ("success"); stops when the port goes (lost)
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.addr >>> 24 === 0xF1) {                    // the loader refused the pedal's flash head (ldr_core.c 1b): nothing written
        link.send(response(r.addr, [...new TextEncoder().encode("refused"), 0]));
        return { served, finished: false, refused: r.addr & 0xFFFFFF };
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      if (this.t.perRequest) await sleep(this.t.perRequest);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl)))
        return { served, finished: false, lost: true };
      served++;
      if (onProgress) onProgress(served, r.addr);
    }
  }

  // full update: running firmware (official or OpenPocket) -> its staged loader -> the new firmware.
  // product: the package identity ("PocketX_924", or the official "PocketX_016" for the way back)
  async install(image, product, onStep) {
    const step = onStep || (() => {});
    const [model] = product.split("_");
    const dev = await this.find((id) => MODELS.includes(id.model));
    if (!dev) throw fail("notfound", "pedal not found", this.seen());
    if (dev.id.model !== model) { dev.link.close(); throw fail("model", `the pedal is a ${dev.id.text}, the package is ${product}`, dev.id.text); }
    return this.installOn(dev, image, product, step);
  }

  // rescue (issue #1): a pedal that restarts every few seconds. The first pedal that answers gets the package of
  // its model at once (pick(model) -> { image, product }, already fetched): the upgrade command stops its DSP.
  async rescue(pick, onStep) {
    const step = onStep || (() => {});
    const dev = await this.find((id) => MODELS.includes(id.model));
    if (!dev) return null;
    const pkg = pick(dev.id.model);
    if (!pkg) { dev.link.close(); throw fail("pkg", dev.id.model, dev.id.model); }
    return this.installOn(dev, pkg.image, pkg.product, step);
  }

  // rescue for a program with no button to press again (the command line): rescue() again and again until a pedal is
  // installed or timeoutMs has passed. The time bounds the waiting only; an install that has started is never cut short.
  // A pedal that vanished before the first transfer finished has had nothing written (stopped, lost): wait for it again.
  // The update loader that a lost attempt left behind is resumed with the package of the pedal that was picked, else with
  // `loader` ({ image, product }): a loader does not report its model, so the caller names the package.
  // onEvent("wait", msLeft) and onEvent("retry", error) report on the way. Returns the version the pedal reports, or null
  // when no pedal answered in time.
  async rescueWithin(pick, onStep, { timeoutMs, onEvent, loader = null } = {}) {
    const end = Date.now() + timeoutMs, tell = onEvent || (() => {});
    let pkg = null;
    for (;;) {
      try {
        const done = await this.rescue((model) => (pkg = pick(model)), onStep);
        if (done) return done;
        const half = pkg || loader;
        if (half && await this.resume(half.image, half.product, onStep)) return half.product;
      } catch (e) {
        if (!["stopped", "lost", "noloader"].includes(e.code)) throw e;
        tell("retry", e);
      }
      const left = end - Date.now();
      if (left <= 0) return null;
      tell("wait", left);
      await sleep(Math.min(this.t.rescuePoll, left));
    }
  }

  async installOn(dev, image, product, step) {
    if (NEW_LAYOUT(dev.id) && parseInt(product.split("_")[1], 10) >= 900) {
      dev.link.close();
      throw fail("layout", `the pedal runs ${dev.id.text}: M-VAVE V22 or later, a flash layout OpenPocket cannot write yet`, dev.id.text);
    }
    step("start", dev.id.text);
    dev.link.send(UPGRADE);
    await sleep(this.t.afterUpgrade);
    const s1 = await this.serve(dev.link, image, 0xE0000000, this.t.step1Idle, (n) => step("verify", n));
    dev.link.close();
    if (!s1.finished)
      throw fail(s1.lost ? "lost" : "stopped", `the pedal ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
    step("loader");
    await sleep(this.t.afterStep1);
    const ota = await this.waitFor(IS_OTA, this.t.loaderWait);
    if (!ota) throw fail("noloader", "the update loader did not appear");
    const s2 = await this.write(ota, image, step);
    if (s2.refused) throw refused(s2.refused);
    if (!s2.finished)
      throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests`);
    return this.comeBack(product, step);
  }

  async write(ota, image, step) {
    ota.link.send(UPGRADE);
    await sleep(this.t.afterUpgrade);
    const s2 = await this.serve(ota.link, image, 0xF0000000, this.t.step2Idle, (n) => step("write", n));
    ota.link.close();
    return s2;
  }

  async comeBack(product, step) {
    step("reboot");
    await sleep(this.t.afterWrite);
    const back = await this.waitFor((id) => MODELS.includes(id.model), this.t.backWait);
    if (!back) throw fail("noreturn", "the pedal did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `written, but the pedal reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // the pedal is already in update mode (a loader) -> finish writing image. Our loader takes any OpenPocket package;
  // another loader (the official one) only the official package (opts.official)
  async resume(image, product, onStep, opts = {}) {
    const ota = await this.find(IS_OTA);
    if (!ota) return false;
    if (!OUR_LOADER(ota.id) && !opts.official) {
      ota.link.close();
      throw fail("foreign", `the pedal is in the update mode of another firmware (${ota.id.text})`, ota.id.text);
    }
    const step = onStep || (() => {});
    const s = await this.write(ota, image, step);
    if (s.refused) throw refused(s.refused);
    if (!s.finished) throw fail(s.lost ? "lost" : "stopped", `the loader stopped after ${s.served} requests`);
    await this.comeBack(product, step);
    return true;
  }

  // Enter the chip's UBOOT mode. The caller chooses which firmware supports the soft key.
  async uboot(filter = IS_OPENPOCKET) {
    const dev = await this.find(filter);
    if (!dev) throw fail("notfound", "pedal not found", this.seen());
    try {
      if (!dev.link.send(UBOOT_KEY)) throw fail("lost", "could not send the recovery key");
    } finally {
      dev.link.close();
    }
  }
}
