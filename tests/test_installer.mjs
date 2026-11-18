// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — end-to-end test of the web installer (docs/pxota.js, docs/pxpkg.js) against a simulated pedal
// (tests/devsim.c) that runs the pedal's real update code: firmware/bsp/chip/ota.c (step 1) and loader/ldr_core.c
// (step 2), on a 1 MiB flash. No browser, no hardware.
//   cc -O1 -o build/devsim tests/devsim.c && node tests/test_installer.mjs [PocketX_016.fwsc]
// The optional official package checks the way back's step 1 (the official loader staged by OpenPocket's ota.c).
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { Updater, IS_OPENPOCKET } from "../docs/pxota.js";
import { productOf, logicalImage, checkOfficial } from "../docs/pxpkg.js";
import { simAccess } from "./simpedal.mjs";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const TMP = mkdtempSync(join(tmpdir(), "opinst-"));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(70)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const FAST = { afterUpgrade: 50, afterStep1: 50, loaderWait: 5000, step1Idle: 3000, step2Idle: 5000, afterWrite: 50,
  backWait: 5000, perRequest: 0, poll: 50 };

execFileSync("python3", [join(ROOT, "tests", "fixtures.py"), TMP], { stdio: "inherit" });

const pkg = (name) => readFileSync(join(TMP, name));
const flashOf = (f) => readFileSync(f);
const py = (code, ...a) => execFileSync("python3", ["-c", code, ...a]).toString().trim();
const appOf = `
import sys; sys.path.insert(0, sys.argv[2])
import pocketx_fw as fw
d = fw.flash_decrypt(open(sys.argv[1], 'rb').read())
e = fw.flash_parse(d)
bad = [k for k, v in fw.flash_checks(d, e).items() if not v]
app = d[0x4000 + e[1]['addr']:0x4000 + e[1]['addr'] + e[1]['len']]
i = app.find(b'OP-PRODUCT:')
print(app[i + 11:i + 31].split(b'\\0')[0].decode(), 'CRC-OK' if not bad else 'CRC-BAD')`;

// 1. the package files
const px = pkg("PocketX_924.fwsc"), bb = pkg("BlackBox_924.fwsc");
ok(productOf(px) === "PocketX_924" && productOf(bb) === "BlackBox_924", "packages carry their identity");

// 2. OpenPocket 923 -> 924: handshake, step 1 (ota.c stages the loader), step 2 (ldr_core writes), back as 924
{
  const dump = join(TMP, "after_update.bin");
  const sim = simAccess([join(TMP, "flash_old.bin"), "--dump", dump]);
  await sim.ready;
  [...sim.access.outputs.values()][0].send([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7]);   // a DAW's General MIDI reset: not ours
  const u = new Updater(sim.access, null, FAST);
  const steps = [];
  const who = await u.identify();
  ok(who && who.id.text === "PocketX_923" && IS_OPENPOCKET(who.id), "a foreign SysEx is dropped; the pedal reports the OpenPocket it runs (PocketX_923)");
  let res = null, err = null;
  try { res = await u.install(logicalImage(px), "PocketX_924", (s) => steps.push(s)); } catch (e) { err = e; }
  ok(!err && res === "PocketX_924", `update 923 -> 924 ${err ? "(" + err.code + " " + err.message + ")" : ""}`);
  ok(["start", "verify", "loader", "write", "reboot", "done"].every((s) => steps.includes(s)), "every step reported");
  const code = await sim.end();
  ok(code === 0, "the pedal never wrote outside its allowed ranges");
  ok(sim.log.some((l) => /step1 rc=0/.test(l)) && sim.log.some((l) => /staged head=LOADER.BIN/.test(l)), "step 1: loader staged at 0xE0000 (JLFS LOADER.BIN)");
  ok(sim.log.some((l) => /record=flash loader_at=0xe0000/.test(l)), "step 1: update record written (flash 0xE4F00, RAM)");
  ok(sim.log.some((l) => /step2 rc=0/.test(l)), "step 2: the loader wrote the app area");
  const after = flashOf(dump), before = flashOf(join(TMP, "flash_old.bin"));
  ok(after.subarray(0, 0x4000).equals(before.subarray(0, 0x4000)), "flash head [0, 0x4000) untouched");
  ok(after.subarray(0xC0000, 0xC0010).toString() === "STOCK USER DATA!", "user data above the app area untouched");
  ok(after.subarray(0xE0000, 0xE5000).every((b) => b === 0xFF), "staging area erased afterwards");
  ok(py(appOf, dump, join(ROOT, "tools")) === "PocketX_924 CRC-OK", "flash now holds app PocketX_924, every CRC right");
}

// 3. the wrong package for the pedal: refused before anything happens
{
  const sim = simAccess([join(TMP, "flash_old.bin")]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  let err = null;
  try { await u.install(logicalImage(bb), "BlackBox_924", () => {}); } catch (e) { err = e; }
  ok(err && err.code === "model", "a BlackBox package on a Pocket X is refused (model)");
  await sim.end();
}

// 4. official firmware -> OpenPocket (its step 1 simulated by ota.c, answering as PocketX_016)
{
  const dump = join(TMP, "from_stock.bin");
  const sim = simAccess([join(TMP, "flash_old.bin"), "--stock", "PocketX_016", "--dump", dump]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  let res = null, err = null;
  try { res = await u.install(logicalImage(px), "PocketX_924", () => {}); } catch (e) { err = e; }
  ok(!err && res === "PocketX_924", `official PocketX_016 -> OpenPocket 924 ${err ? "(" + err.code + ")" : ""}`);
  await sim.end();
}

// 4b. M-VAVE's BlackBox V22 and later (their flash head puts the app area at 0x6000, the boot loader running to 0x5CA0):
//     OpenPocket's loader writes from 0x4000 and would overwrite that boot loader (isod89/openpocket#31, #33)
{   // the page refuses a pedal that says it runs BlackBox_022 before sending anything
  const dump = join(TMP, "v22_page.bin");
  const sim = simAccess([join(TMP, "flash_v22.bin"), "--stock", "BlackBox_022", "--dump", dump]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  const steps = [];
  let err = null;
  try { await u.install(logicalImage(bb), "BlackBox_924", (s) => steps.push(s)); } catch (e) { err = e; }
  await sim.end();
  ok(err && err.code === "layout" && err.detail === "BlackBox_022" && !steps.length && !sim.log.some((l) => /step1/.test(l)) &&
     flashOf(dump).equals(flashOf(join(TMP, "flash_v22.bin"))), "official BlackBox_022: refused by the page, nothing sent, nothing written");
}
{   // the loader refuses the V22 head itself, whatever the pedal says (here 021): nothing written, the record gone, the pedal
    // back on its own firmware, the page told at once (not after the write's 3-minute wait)
  const dump = join(TMP, "v22_loader.bin");
  const sim = simAccess([join(TMP, "flash_v22.bin"), "--stock", "BlackBox_021", "--dump", dump]);
  await sim.ready;
  const u = new Updater(sim.access, null, { ...FAST, step2Idle: 30000 });
  const t0 = Date.now();
  let err = null;
  try { await u.install(logicalImage(bb), "BlackBox_924", () => {}); } catch (e) { err = e; }
  const took = Date.now() - t0;
  await new Promise((r) => setTimeout(r, 200));
  await sim.end();
  const before = flashOf(join(TMP, "flash_v22.bin")), after = flashOf(dump);
  ok(err && err.code === "layout" && /past 0x4000|not at 0x4000/.test(err.detail) && took < 10000 && sim.log.some((l) => /step2 rc=-6[1-5]/.test(l)),
     `a V22 head behind an older identity: the loader refuses it (${err ? err.detail : "no error"}), the page told in ${took} ms`);
  ok(after.subarray(0, 0xA3000).equals(before.subarray(0, 0xA3000)), "V22 head refused: the head and [0x4000, 0xA3000) untouched");
  ok(after.subarray(0xE4000, 0xE5000).every((b) => b === 0xFF) && sim.log.some((l) => /app BlackBox_021 refused record=none/.test(l)),
     "V22 head refused: the update record erased, the pedal restarts on its own firmware");
}

// 5. a damaged package: the loader refuses it before erasing anything
{
  const dump = join(TMP, "damaged.bin");
  const sim = simAccess([join(TMP, "flash_old.bin"), "--dump", dump]);
  await sim.ready;
  const u = new Updater(sim.access, null, { ...FAST, step2Idle: 1500 });
  const img = logicalImage(px);
  img[0x60000] ^= 0x55;                                     // one byte inside the app area of flash.bin
  let err = null;
  try { await u.install(img, "PocketX_924", () => {}); } catch (e) { err = e; }
  await sim.end();
  ok(err && sim.log.some((l) => /step2 rc=-12/.test(l)), "damaged flash.bin: the loader stops (CRC, rc -12)");
  ok(flashOf(dump).subarray(0x4000, 0xA3000).equals(flashOf(join(TMP, "flash_old.bin")).subarray(0x4000, 0xA3000)),
     "damaged package: nothing in the app area erased");
}

// 6a. rescue mode (issue #1): the first pedal that answers gets the package of its model at once
{
  const dump = join(TMP, "rescue.bin");
  const sim = simAccess([join(TMP, "flash_old.bin"), "--dump", dump]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  const asked = [];
  const pick = (m) => { asked.push(m); return m === "PocketX" ? { image: logicalImage(px), product: "PocketX_924" } : null; };
  let res = null, err = null;
  try { res = await u.rescue(pick, () => {}); } catch (e) { err = e; }
  await sim.end();
  ok(!err && res === "PocketX_924" && asked.join() === "PocketX", `rescue: the pedal answers, its model's package is written ${err ? "(" + err.code + ")" : ""}`);
  ok(py(appOf, dump, join(ROOT, "tools")) === "PocketX_924 CRC-OK", "rescue: flash holds app PocketX_924, every CRC right");
}
{
  const sim = simAccess([join(TMP, "flash_old.bin")]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  let err = null;
  try { await u.rescue(() => null, () => {}); } catch (e) { err = e; }
  await sim.end();
  ok(err && err.code === "pkg", "rescue: no package for the pedal's model -> refused, nothing sent");
}

// 6b. the presets survive an update: the store [0xF0000, 0xF4000) is never erased, even sectors that end like an
// update record (the loader's record scan must skip it)
{
  const src = join(TMP, "flash_store.bin"), dump = join(TMP, "store_after.bin");
  py(`
import sys
f = bytearray(open(sys.argv[1], "rb").read())
def crc(b, c=0):
    for x in b:
        c ^= x << 8
        for _ in range(8): c = ((c << 1) ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
    return c
for s in range(0xF0000, 0xF4000, 0x1000):
    f[s:s + 0x1000] = bytes((i * 7 + s // 0x1000) & 0xFF for i in range(0x1000))
    r = bytearray(f[s + 0xF00:s + 0xF00 + 80]); r[6:8] = (0x5441).to_bytes(2, "little")
    r[0:2] = crc(r[2:80]).to_bytes(2, "little"); f[s + 0xF00:s + 0xF00 + 80] = r
open(sys.argv[2], "wb").write(f)`, join(TMP, "flash_old.bin"), src);
  const sim = simAccess([src, "--dump", dump]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  let res = null, err = null;
  try { res = await u.install(logicalImage(px), "PocketX_924", () => {}); } catch (e) { err = e; }
  await sim.end();
  const same = py(`
import sys
a = open(sys.argv[1], "rb").read()[0xF0000:0xF4000]; b = open(sys.argv[2], "rb").read()[0xF0000:0xF4000]
print("same" if a == b else "changed")`, src, dump);
  ok(!err && res === "PocketX_924" && same === "same", `presets: an update leaves the preset store untouched (${same})`);
}

// 6. the way back, step 1 with the real official package (when given): checkOfficial, then OpenPocket's ota.c checks
//    and stages the official loader (its boot area must be valid)
const off = process.argv[2];
if (off && existsSync(off)) {
  const bytes = readFileSync(off);
  const chk = await checkOfficial(new Uint8Array(bytes));
  ok(chk.known && chk.product === "PocketX_016", "official PocketX_016 package recognised (SHA-256)");
  const sim = simAccess([join(TMP, "flash_old.bin")]);
  await sim.ready;
  const u = new Updater(sim.access, null, FAST);
  const dev = await u.find((id) => IS_OPENPOCKET(id));
  dev.link.send([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7]);
  await new Promise((r) => setTimeout(r, 50));
  const s1 = await u.serve(dev.link, chk.image, 0xE0000000, 3000, null);
  await new Promise((r) => setTimeout(r, 200));
  await sim.end();
  ok(s1.finished && sim.log.some((l) => /step1 rc=0/.test(l)) && sim.log.some((l) => /foreign-loader/.test(l)),
     "way back: OpenPocket stages the official loader (head checked)");
  let bad = null;
  try { await checkOfficial(new Uint8Array(px)); } catch (e) { bad = e; }
  ok(bad !== null, "an OpenPocket package is not accepted as the official one");
} else {
  console.log("(no official package given: way-back test skipped)");
}

console.log(failed ? `\n${failed} FAILED` : "\nall installer tests passed");
process.exit(failed ? 1 : 0);
