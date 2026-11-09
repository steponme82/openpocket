// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the pedal skin (tools/sim/pedal.html) in a real browser (headless Chromium, Playwright) against the hosted
// simulator: its layout, every control by mouse, touch and keyboard, the knobs, the backlights, the live LCD. A screenshot
// of the skin goes to OUTDIR.
//   node tests/test_pedal_page.mjs OUTDIR
import { mkdirSync, mkdtempSync, rmSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { startSim } from "../tools/sim.mjs";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const OUT = process.argv[2] || mkdtempSync(join(tmpdir(), "oppedal-"));
mkdirSync(OUT, { recursive: true });
const TMP = mkdtempSync(join(tmpdir(), "oppedalsim-"));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(76)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

const sim = await startSim({ port: 0, flash: join(TMP, "flash.bin") });
const state = async () => (await fetch(sim.url + "/api/state")).json();
const post = (path) => fetch(sim.url + path, { method: "POST" });
const until = async (test, ms = 1500) => {
  const end = Date.now() + ms;
  for (;;) { const s = await state(); if (test(s)) return s; if (Date.now() > end) return s; await new Promise((r) => setTimeout(r, 15)); }
};

const browser = await chromium.launch();
const open = async (options) => {
  const context = await browser.newContext({ viewport: { width: 1280, height: 960 }, ...options });
  const page = await context.newPage();
  page.on("pageerror", (e) => ok(false, `page error: ${e.message}`));
  await page.goto(sim.url + "/");
  await page.waitForSelector("body[data-frame]");
  return page;
};
const SCALE = 2, NOTCH = 100 * SCALE;                   // Playwright divides a wheel's delta by the device scale
const page = await open({ deviceScaleFactor: SCALE });
const box = (selector) => page.locator(selector).boundingBox();
const centre = (b) => ({ x: b.x + b.width / 2, y: b.y + b.height / 2 });

// ---- 1. the layout, against the photo's proportions ----
const pedal = await box("#pedal");
const rel = async (selector) => { const c = centre(await box(selector)); return { x: (c.x - pedal.x) / pedal.width, y: (c.y - pedal.y) / pedal.height }; };
const near = (a, b) => Math.abs(a - b) < 0.015;
const expected = { "#master": [0.152, 0.228], "#para": [0.843, 0.228], "#lcd": [0.5, 0.301], "[data-control=home]": [0.138, 0.579], "[data-control=cab]": [0.863, 0.579], "[data-control=fs_dn]": [0.167, 0.796], "[data-control=fs_up]": [0.839, 0.796] };
for (const [selector, [x, y]] of Object.entries(expected)) { const p = await rel(selector); ok(near(p.x, x) && near(p.y, y), `layout: ${selector} at (${p.x.toFixed(3)}, ${p.y.toFixed(3)})`); }
const keys = await page.locator(".key").all();
const tops = await Promise.all(keys.map(async (k) => (await k.boundingBox()).y));
ok(keys.length === 7 && new Set(tops).size === 1, "layout: seven buttons in one row");
const svgText = await page.locator(".body").evaluate((e) => e.querySelectorAll("path").length);
ok(svgText > 5 && await page.locator("img").count() === 0, "layout: the panel is drawn in code, no images");

// ---- 2. the live screen ----
const painted = () => page.evaluate(() => [...document.getElementById("lcd").getContext("2d").getImageData(0, 0, 240, 240).data]);
const same = async () => {
  await page.waitForTimeout(250);
  const raw = Buffer.from(await (await fetch(sim.url + "/api/screen.rgb565")).arrayBuffer()), px = await painted();
  for (let i = 0; i < 240 * 240; i++) {
    const c = raw.readUInt16BE(i * 2);
    if (Math.abs(px[i * 4] - ((c >> 11) & 31) * 255 / 31) > 1) return false;
  }
  return true;
};
ok(await same(), "LCD: the canvas holds the simulator's frame buffer");
await page.screenshot({ path: join(OUT, "pedal-skin.png") });

// ---- 3. every button, by mouse ----
const press = async (control) => {
  const before = (await state()).presses;
  await page.locator(`[data-control=${control}]`).click();
  return until((s) => s.presses > before);
};
for (const control of ["fs_up", "fs_dn", "fx", "mod", "amp", "cab", "dly", "rev", "home"]) {
  const s = await press(control);
  ok(s.last_press === control, `mouse: ${control}`);
}
let s = await state();
ok(s.mode === "chain", "HOME: back on the chain page");

// ---- 4. holds, as the firmware times them ----
const home = await box("[data-control=home]");
await page.mouse.move(...Object.values(centre(home)));
await page.mouse.down();
ok(await page.waitForSelector("[data-control=home].held", { timeout: 500 }).then(() => true, () => false), "HOME looks held while it is down");
s = await until((x) => x.mode === "tuner");
ok(s.mode === "tuner" && s.held.includes("home"), "HOME held by the mouse: the tuner");
await page.mouse.up();
s = await until((x) => !x.held.length);
ok(s.mode === "tuner", "released, the tuner stays");
await page.locator("[data-control=fs_up]").click();
s = await until((x) => x.mode === "chain");
ok(s.mode === "chain", "a footswitch leaves it");
await post("/api/press/fs_dn");

const para = centre(await box("#para"));
await page.mouse.move(para.x, para.y);
await page.mouse.down();
s = await until((x) => x.mode === "save");
ok(s.mode === "save", "PARA held: the save page");
await page.mouse.up();
await post("/api/press/home");
await page.mouse.click(para.x, para.y);
s = await until((x) => x.mode === "block");
ok(s.mode === "block", "PARA clicked: PUSH, the block page after the double-click wait");
await post("/api/press/home");
await until((x) => x.mode === "chain");
await page.mouse.click(para.x, para.y);
await page.mouse.click(para.x, para.y);
s = await until((x) => x.mode === "move");
ok(s.mode === "move", "PARA clicked twice: MOVE");
await post("/api/press/home");

// ---- 5. both footswitches: two presses, as the firmware does ----
s = await state();
await page.evaluate(async () => { await Promise.all([pedal.down("fs_dn"), pedal.down("fs_up")]); });
await page.waitForTimeout(800);
const both = await state();
await page.evaluate(async () => { await Promise.all([pedal.up("fs_dn"), pedal.up("fs_up")]); });
ok(both.mode === "chain" && both.slot === s.slot && both.held.length === 2, "both footswitches held: no tuner in OpenPocket");

// ---- 6. the knobs ----
await post("/api/volume/50");
await page.waitForSelector('#master[aria-valuenow="50"]');
const master = await box("#master"), mc = centre(master);
await page.mouse.move(mc.x, mc.y);
await page.mouse.down();
await page.mouse.move(mc.x, mc.y - master.width / 4, { steps: 8 });
await page.mouse.up();
s = await until((x) => x.volume === 60);
ok(s.volume === 60, `MASTER dragged up a quarter of its width: 50 -> ${s.volume}`);
await page.waitForSelector('#master[aria-valuenow="60"]');
const angle = await page.locator("#master").evaluate((e) => e.style.getPropertyValue("--angle"));
ok(Math.abs(parseFloat(angle) - 27) < 0.01, `MASTER points at ${angle}`);
await page.mouse.move(mc.x, mc.y);
await page.mouse.wheel(0, -NOTCH);
s = await until((x) => x.volume === 62);
ok(s.volume === 62, "MASTER wheel: one notch up, the hysteresis' 2");
await page.locator("#master").focus();
await page.keyboard.press("ArrowDown");
s = await until((x) => x.volume === 60);
ok(s.volume === 60, "MASTER in focus: arrow down");
await page.keyboard.press("End");
s = await until((x) => x.volume === 100);
ok(s.volume === 100, "MASTER in focus: End is 100");
await page.locator("#master").blur();

await post("/api/press/amp");
await post("/api/knob/1");
await post("/api/press/push");
s = await until((x) => x.mode === "edit");
const gain = s.preset.p[5][0], k0 = s.knob, pb = await box("#para");
await page.mouse.move(para.x, para.y);
await page.mouse.down();
for (let i = 1; i <= 3; i++) { await page.mouse.move(para.x, para.y - (i + 0.2) * pb.width / 10); await page.waitForTimeout(70); }
await page.mouse.up();
s = await until((x) => x.knob === k0 + 3);
ok(s.preset.p[5][0] === gain + 3 && s.mode === "edit", `PARA dragged three detents, slowly: GAIN ${gain} -> ${s.preset.p[5][0]}, and no PUSH`);
await page.mouse.wheel(0, NOTCH);
s = await until((x) => x.knob === k0 + 2);
ok(s.preset.p[5][0] === gain + 2, "PARA wheel: one detent back");
await page.waitForTimeout(60);                          // closer than 45 ms it would be a fast turn
await page.keyboard.press("ArrowRight");
s = await until((x) => x.knob === k0 + 3);
ok(s.preset.p[5][0] === gain + 3, "the right arrow: one detent");
const turned = await page.locator("#para").evaluate((e) => e.style.getPropertyValue("--angle"));
await post("/api/knob/1");
await until((x) => x.knob === k0 + 4);
await page.waitForFunction((a) => document.getElementById("para").style.getPropertyValue("--angle") !== a, turned);
ok(true, "PARA follows a turn made over HTTP");
await post("/api/press/home");

// ---- 7. the keyboard ----
const key = async (name, control) => {
  const before = (await state()).presses;
  await page.keyboard.press(name);
  const s = await until((x) => x.presses > before);
  ok(s.last_press === control, `key ${name}: ${control}`);
};
await key("ArrowUp", "fs_up");
await key("ArrowDown", "fs_dn");
for (const [k, c] of [["f", "fx"], ["a", "amp"], ["m", "mod"], ["d", "dly"], ["r", "rev"], ["c", "cab"], ["h", "home"]]) await key(k, c);
await key("Escape", "home");
await page.keyboard.down("Escape");
s = await until((x) => x.mode === "tuner");
ok(s.mode === "tuner", "Escape held: the tuner");
await page.keyboard.up("Escape");
await key("Enter", "push");
s = await state();
await page.keyboard.press("Enter");
s = await until((x) => x.mode === "block");
ok(s.mode === "block", "Enter on the chain: PUSH");
await post("/api/press/home");
await page.keyboard.down("Shift");
await page.keyboard.down("H");
await until((x) => x.held.includes("home"));
await page.keyboard.up("Shift");
await page.keyboard.up("h");
await new Promise((r) => setTimeout(r, 900));
s = await state();
ok(s.mode !== "tuner", "Shift+H released Shift first: HOME is let go, no stray long press");
await post("/api/press/home");
await page.locator("[data-control=dly]").focus();
await key("Enter", "dly");
await key(" ", "dly");
await page.locator("[data-control=dly]").blur();
await post("/api/press/home");

// ---- 8. state lights the buttons ----
const litBecomes = (name, level) => page.waitForSelector(`[data-control=${name}][data-lit=${level}]`, { timeout: 2000 }).then(() => true, () => false);
ok(await litBecomes("home", "focus"), "HOME is lit brightest on the chain");
await post("/api/press/dly");
ok(await litBecomes("dly", "focus") && await litBecomes("home", "on"), "the DLY page: DLY brightest, HOME back to lit");
const sysex = async (bytes) => fetch(sim.url + "/api/sysex", { method: "POST", body: JSON.stringify([0xF0, 0x7D, 0x4F, 0x50, ...bytes, 0xF7]) });
await post("/api/press/home");
await sysex([6, 9, 1, 0, 1]); ok(await litBecomes("dly", "on"), "a block switched on over MIDI: its button lights");
await sysex([6, 9, 1, 0, 0]); ok(await litBecomes("dly", "off"), "and off: it goes dark");

// ---- 9. scripts, and a small touch screen ----
await page.evaluate(async () => { await pedal.tap("fs_up"); });
ok((await state()).last_press === "fs_up", "window.pedal.tap presses a button");
await page.evaluate(() => pedal.master(70));
ok((await until((x) => x.volume === 70)).volume === 70, "window.pedal.master sets the volume");
ok(await page.waitForFunction(() => pedal.state().volume === 70, null, { timeout: 1000 }).then(() => true, () => false), "window.pedal.state reads the state");
ok(await same(), "LCD: still the simulator's frame buffer after all that");

const phone = await open({ viewport: { width: 390, height: 780 }, hasTouch: true, isMobile: true });
const before = await state();
await phone.tap("[data-control=amp]");
s = await until((x) => x.presses > before.presses);
ok(s.last_press === "amp", "touch: AMP");
const wide = await phone.locator("#pedal").boundingBox();
ok(wide.width <= 390 && wide.width > 300 && Math.abs(wide.height / wide.width - 1.046) < 0.01, `a phone: the pedal fits, ${Math.round(wide.width)} px wide, in proportion`);
await phone.screenshot({ path: join(OUT, "pedal-skin-phone.png") });
const cdp = await phone.context().newCDPSession(phone);
const touch = (type, points) => cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points });
await post("/api/press/home");
const homeKey = centre(await phone.locator("[data-control=home]").boundingBox());
await touch("touchStart", [homeKey]);
s = await until((x) => x.mode === "tuner");
ok(s.mode === "tuner", "touch: HOME held, the tuner");
await touch("touchEnd", []);
await post("/api/press/home");
await post("/api/volume/50");
await phone.waitForSelector('#master[aria-valuenow="50"]');
const knobBox = await phone.locator("#master").boundingBox(), knobAt = centre(knobBox);
await touch("touchStart", [knobAt]);
await touch("touchMove", [{ x: knobAt.x, y: knobAt.y - knobBox.width / 2 }]);
await touch("touchEnd", []);
s = await until((x) => x.volume === 70);
ok(s.volume === 70, "touch: MASTER dragged up half its width, 50 -> 70");
await touch("touchStart", [centre(await phone.locator("[data-control=fs_dn]").boundingBox()), centre(await phone.locator("[data-control=fs_up]").boundingBox())]);
s = await until((x) => x.held.length === 2);
ok(s.held.length === 2, "touch: both footswitches down at once, two fingers");
await touch("touchEnd", []);
await until((x) => !x.held.length);

const plain = await browser.newPage();
await plain.goto(sim.url + "/plain");
ok(await plain.locator("#blocks").count() === 1, "/plain: the plain control page");

await browser.close();
await sim.close();
rmSync(TMP, { recursive: true, force: true });
console.log(failed ? `\n${failed} FAILED` : `\npedal page: ok (screenshots in ${OUT})`);
process.exit(failed ? 1 : 0);
