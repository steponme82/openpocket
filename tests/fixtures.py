#!/usr/bin/env python3
"""Test fixtures for tests/test_installer.mjs: a fake app (with its OP_PRODUCT field), a fake update loader wrapped
like the real one, OpenPocket packages built by tools/pxpkg.py, and a starting flash with an older OpenPocket.
  fixtures.py OUTDIR"""
import os
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import mkloader  # noqa: E402
import pxpkg  # noqa: E402

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
rnd = random.Random(7)
body = bytes(rnd.getrandbits(8) for _ in range(90000))
app = b"\x00" * 64 + b"OP-PRODUCT:PocketX_900" + b"\0" * 10 + body           # as the firmware links it
loader = b"\x00\x00FELUCCA-LOADER-1" + bytes(rnd.getrandbits(8) & 0x0F for _ in range(9000))
ota = mkloader.wrap(loader)
sdk = ROOT / "firmware" / "sdk" / "packager"
pxpkg.build(app, ota, out, 24, None, sdk)                                         # PocketX_924, BlackBox_924
files = pxpkg.sdk_files(None, sdk)
old = pxpkg.flash_image(pxpkg.with_product(app[:-500] + b"older", "PocketX_923"), files)
flash = bytearray(b"\xFF" * 0x100000)
flash[:len(old)] = old
flash[0xC0000:0xC0010] = b"STOCK USER DATA!"                                     # USR: must survive
(out / "flash_old.bin").write_bytes(bytes(flash))


# M-VAVE's BlackBox V22 (2026-11-12) and later: the boot loader runs to 0x5CA0, its isd_config follows it, the app area
# (app_dir_head) is at 0x6000. The same flash with its head's top entries moved so (JLFS: 32 bytes each, ENC 0xFFFF, CRC16
# of bytes 2..31 first): loader/ldr_core.c's 1b must refuse it and write nothing (isod89/openpocket#31, #33)
def jl_enc(b, k=0xFFFF):
    b = bytearray(b)
    for i in range(len(b)):
        b[i] ^= k & 0xFF
        k = ((k << 1) ^ (0x1021 if k & 0x8000 else 0)) & 0xFFFF
    return bytes(b)


def crc16(d, c=0):
    for x in d:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
    return c


v22 = bytearray(flash)
seen = []
for o in range(32, 0x400, 32):
    e = bytearray(jl_enc(v22[o:o + 32]))
    name = bytes(e[16:32]).split(b"\0")[0].decode("latin1")
    seen.append(name)
    addr, ln = int.from_bytes(e[4:8], "little"), int.from_bytes(e[8:12], "little")
    if name == "uboot.boot":
        ln = 0x5C00                                                               # [0xA0, 0x5CA0)
    elif name == "isd_config.ini":
        v22[0x5CA0:0x5CA0 + ln] = v22[addr:addr + ln]                             # (its blob too)
        addr = 0x5CA0
    elif name == "app_dir_head":
        addr = 0x6000
    e[4:8], e[8:12] = addr.to_bytes(4, "little"), ln.to_bytes(4, "little")
    e[0:2] = crc16(e[2:32]).to_bytes(2, "little")
    v22[o:o + 32] = jl_enc(e)
    if int.from_bytes(e[14:16], "little"):
        break
assert seen[:3] == ["uboot.boot", "isd_config.ini", "app_dir_head"], seen          # (the official heads' order)
(out / "flash_v22.bin").write_bytes(bytes(v22))
(out / "app_new_px.bin").write_bytes(pxpkg.with_product(app, "PocketX_924"))
print("fixtures ok:", ", ".join(sorted(p.name for p in out.iterdir())))
