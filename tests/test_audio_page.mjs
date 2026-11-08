// SPDX-License-Identifier: GPL-3.0-only
// The pedal's sound on the skin in a real browser (headless Chromium, Playwright, a fake audio input): the input picker, the
// meters, the engine in its AudioWorklet following the pedal. A screenshot goes to OUTDIR.
//   node tests/test_audio_page.mjs OUTDIR
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { startSim } from "../tools/sim.mjs";
import { wavBytes } from "../tools/sim/render.mjs";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const OUT = process.argv[2] || mkdtempSync(join(tmpdir(), "opaudiopage-"));
mkdirSync(OUT, { recursive: true });
const TMP = mkdtempSync(join(tmpdir(), "opaudiosim-"));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(76)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

const tone = (rate, seconds, level) => wavBytes(Float32Array.from({ length: Math.round(rate * seconds) }, (_, i) => level * Math.sin(2 * Math.PI * 330 * i / rate)), rate);
const LONG = join(TMP, "tone-48k.wav"), SHORT = join(TMP, "short.wav");
writeFileSync(LONG, tone(48000, 2, 0.5));
writeFileSync(SHORT, tone(44100, 0.3, 0.5));

const sim = await startSim({ port: 0, flash: join(TMP, "flash.bin") });
const state = async () => (await fetch(sim.url + "/api/state")).json();
const post = (path) => fetch(sim.url + "/api/" + path, { method: "POST" });
const browser = await chromium.launch({ args: ["--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream", "--autoplay-policy=user-gesture-required"] });
const page = await (await browser.newContext({ viewport: { width: 1280, height: 1000 } })).newPage();
page.on("pageerror", (e) => ok(false, `page error: ${e.message}`));
await page.goto(sim.url + "/");
await page.waitForSelector("body[data-frame]");
await page.waitForSelector("#loop option", { state: "attached" });

const audio = () => page.evaluate(() => window.pedal.audio());
const db = async (id) => { const v = await page.locator(id).getAttribute("data-db"); return v === null || v === "-inf" ? -Infinity : Number(v); };
const settle = (ms = 400) => page.waitForTimeout(ms);
const until = async (test, ms = 3000) => { const end = Date.now() + ms; for (;;) { if (await test()) return true; if (Date.now() > end) return false; await page.waitForTimeout(25); } };
// the loudest reading over a moment: the meters follow the sound
const loudest = async (id, ms = 500) => { let m = -Infinity; const end = Date.now() + ms; while (Date.now() < end) { m = Math.max(m, await db(id)); await page.waitForTimeout(40); } return m; };
const near = (a, b, tolerance) => Math.abs(a - b) <= tolerance;

let a = await audio();
ok(a.running === false && a.sampleRate === null, "nothing runs before a click: browsers forbid it");
const loops = await page.locator("#loop option").count();
ok(loops >= 4 && loops <= 6, `the picker lists the bundled loops (${loops})`);
ok(/CC/.test(await page.locator("#credit").textContent()), "the chosen loop's credit and licence are on the page");
ok(await page.locator("[data-panel=device]").isHidden(), "the device panel waits for its radio");

await page.click("#play");
ok(await until(async () => (await audio()).running && await db("#in-meter") > -50), "Play starts the context and the input meter moves");
a = await audio();
ok(a.sampleRate === 44100, "the context runs at the engine's rate");
ok(await until(async () => await db("#out-meter") > -50), "the output meter moves");
ok(await until(async () => (await audio()).heard === (await state()).name), "what is heard is the preset on the screen");
ok(await until(async () => (await state()).dsp.load !== 42, 2000), "the load on the pedal's status is measured");
ok((await page.locator("#play").textContent()) === "Stop", "the button stops now");
await page.locator("[data-control=fs_up]").click();
ok(await until(async () => (await audio()).heard === (await state()).name && (await state()).slot === 1), "a footswitch changes what is heard");
await page.screenshot({ path: join(OUT, "sound.png") });
await page.click("#play");
ok((await audio()).playing === false, "Stop stops the source");
ok(await until(async () => await db("#in-meter") === -Infinity), "the input meter falls to nothing");

await page.check("[name=source][value=file]");
ok(await page.locator("#drop").isVisible(), "the File radio shows the drop zone");
await page.setInputFiles("#file", LONG);
ok(await until(async () => (await audio()).playing), "a chosen file plays at once");
const level = await loudest("#in-meter");
ok(near(level, 20 * Math.log10(0.5 * 0.15), 1), `a 48 kHz file reaches the engine at the guitar level (${level.toFixed(1)} dB)`);
await page.locator("#level").fill("200");
ok(near(await (settle(), loudest("#in-meter")), level + 6, 1), "the level slider moves the input");
await page.locator("#level").fill("100");
await post("volume/0");
ok(await until(async () => await db("#out-meter") === -Infinity), "MASTER at 0 silences the output");
await post("volume/80");
await settle();
const wet = await loudest("#out-meter");
await page.check("#bypass");
ok(await until(async () => (await state()).dsp.bypass === 1), "the Bypass box reaches the pedal");
await settle();
const dry = await loudest("#out-meter");
ok(near(dry, level + 20 * Math.log10(0.64), 1), `bypass passes the dry input, at MASTER (${dry.toFixed(1)} dB)`);
ok(!near(dry, wet, 1), `and it differs from the chain (${wet.toFixed(1)} dB)`);
await page.uncheck("#bypass");
ok(await until(async () => (await state()).dsp.bypass === 0), "bypass comes off");
await post("press/amp"); await post("knob/1"); await post("press/push"); await post("knob/-100");
await settle(600);
const low = await loudest("#out-meter");
await post("knob/100");
await settle(600);
const high = await loudest("#out-meter");
ok(Math.abs(high - low) > 1, `a setting on the AMP page changes the sound (${low.toFixed(1)} to ${high.toFixed(1)} dB)`);
await post("press/push"); await post("press/home");

await page.uncheck("#repeat");
await page.evaluate(async (bytes) => {
  const transfer = new DataTransfer();
  transfer.items.add(new File([new Uint8Array(bytes)], "dropped.wav", { type: "audio/wav" }));
  document.dispatchEvent(new DragEvent("drop", { dataTransfer: transfer, bubbles: true, cancelable: true }));
}, [...tone(44100, 0.3, 0.5)]);
ok(await until(async () => (await page.locator("#file-name").textContent()) === "dropped.wav"), "a dropped file is taken");
ok(await until(async () => (await page.locator("#play").textContent()) === "Play"), "without Loop, a short file ends by itself");
await page.check("#repeat");

await page.check("[name=source][value=device]");
ok(await page.locator("#device-warning").isVisible(), "the device panel warns about feedback");
ok(!(await page.isChecked("#hear")), "a device starts unheard");
await page.click("#play");
ok(await until(async () => (await audio()).device), "Play opens the device");
ok(await until(async () => (await page.locator("#device option").count()) > 1), "the picker lists the computer's inputs");
ok((await audio()).muted === true, "the device is muted by default");
ok(await until(async () => await db("#in-meter") > -60), "and its meter moves");
await page.check("#hear");
ok((await audio()).muted === false, "the box lets it through");
await page.selectOption("#device", { index: 1 });
ok(await until(async () => (await audio()).device), "another input can be picked");
await page.click("#play");
ok((await audio()).playing === false, "Stop closes it");

await page.check("[name=source][value=loop]");
await page.focus("#level");
const { knob } = await state();
await page.keyboard.press("ArrowRight");
await settle(200);
ok((await state()).knob === knob && (await page.inputValue("#level")) === "105", "arrow keys on a slider stay with the slider");

await browser.close();
await sim.close();
rmSync(TMP, { recursive: true, force: true });
console.log(failed ? `${failed} FAILED` : "all ok");
process.exit(failed ? 1 : 0);
