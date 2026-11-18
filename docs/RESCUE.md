# Rescuing a crash-looping BlackBox from a Mac (no extra hardware)

Use this when the pedal restarts in a loop and the installer's rescue mode cannot finish. Between crashes the pedal sits
about 44 seconds in the JieLi mask-ROM USB boot state ("UBOOT": USB 4C4A:8057, shown by macOS as `WL80UBOOT1.00`). The
mask ROM is chip ROM, so it cannot be damaged. `tools/uboot_rescue.py` waits for that state, loads JieLi's RAM loader and
rewrites the app area of the flash from the official package, then restarts the pedal.

## After OpenPocket over M-VAVE's V22 or later

M-VAVE's BlackBox firmware V22 and later put a bigger boot loader at the start of the flash (to `0x5CA0`) and the app at
`0x6000`. OpenPocket's installer up to v42 wrote from `0x4000`, over the end of that boot loader: the pedal stays dark and no
installer sees it ([#31](https://github.com/isod89/openpocket/issues/31), [#33](https://github.com/isod89/openpocket/issues/33)).
Its first 16 KiB are untouched, so this tool can write M-VAVE's own file back over the rest: its head check compares the
pedal's `[0, 0x4000)` with the image's before anything is written. This is the route that brought the pedal of #31 back;
it has not been tried here. Use the M-VAVE file of the version the pedal had (V22 and V23 share those first 16 KiB, so
#31's check passed with V22), downloaded from M-VAVE, after the one-time setup below:

```
python3 -c 'import sys; sys.path.insert(0, "tools"); import pocketx_fw as p; _, L, _, m = p.load(sys.argv[1]); e = next(x for x in m["ents"] if x["name"] == "flash.bin"); open(sys.argv[2], "wb").write(L[e["off"]:e["off"] + e["len"]])' BlackBox-V23.fwsc build/mvave-flash.bin
make rescue ARGS="--image build/mvave-flash.bin"
make rescue-write ARGS="--image build/mvave-flash.bin"
```

The first command takes `flash.bin` out of the package, the second only reads and compares (it stops if the heads differ),
the third writes. The installer no longer does this to a pedal: the page and the update loader both refuse those
firmware versions (README, "Install").

## One-time setup (macOS)

```
brew install libusb
```

That is all. The tool is one Python 3 file that calls libusb through `ctypes` (no `pip install`). It needs two files:

- The official package, `~/Documents/BlackBox (2).fwsc` (BlackBox_016) by default, or `--package FILE.fwsc`, or a raw flash
  image with `--image flash.bin`. Every CRC of a package is checked before it is used.
- JieLi's `wl82loader.bin`, downloaded once into the git-ignored `build/cache/` from kagaimiq/jl-uboot-tool (commit
  `c32b28e`, the same pin as `flash/installer-fm1buddy-pocketx.bat`) and checked against SHA-256
  `d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376` on every run. It is not in this repository. `make clean`
  removes the cache; `--loader FILE` uses a copy you downloaded yourself.

## The commands

`make rescue` and `make rescue-write` (or `node tools/op.mjs rescue [--write]`, the entry point next to `flash`) run the tool with
`sudo` for you and pass options on (`make rescue ARGS="--package FILE.fwsc"`). `make doctor` shows whether libusb, the package and
the loader are in place. The `sudo python3 tools/uboot_rescue.py` lines below are the same thing written out.

macOS keeps its own mass-storage driver on the pedal in this state, and libusb needs root to take the interface from it.
Plug the pedal straight into the Mac (no hub), start the command, and it waits for the pedal and starts by itself the moment
it appears.

1. **Read-only first.** Nothing is written to the pedal's flash; the loader goes to RAM only.

```
make rescue                              # = sudo python3 tools/uboot_rescue.py
```

It checks that the pedal answers INQUIRY as `WL82 UBOOT1.00`, loads the loader, reads the chip key (must be `0x980F`) and
the flash id (must be a 1 MiB flash), saves a backup of the whole flash to `~/OpenPocket-rescue/blackbox-<time>.dump.bin`,
compares the pedal's head `[0, 0x4000)` with the image's, reports how many sectors differ, and sends run-app. Keep this
backup: it is the only copy of what the pedal held before the rescue.

2. **The rescue**, only after the read-only run has passed:

```
make rescue-write                        # = sudo python3 tools/uboot_rescue.py --write
```

Same steps, then for every 4 KiB sector in `[0x4000, 0xA3000)` that differs: erase, write, read back, compare. When it prints
`Done`, power-cycle the pedal. If the pedal drops out of UBOOT in the middle (it reboots into the crashing app and comes
back), the tool waits and starts again by itself; running it again later also resumes. Only the sectors that still differ are
written, so a rerun is short.

Options (after `ARGS=` with make): `--check` (prerequisites only), `--package`, `--image`, `--loader`, `--backup-dir DIR`, `--attempts N` (UBOOT windows to try, default 10).
Exit codes: 0 done, 1 setup problem (libusb, root, files), 2 refused by a safety check, 130 stopped by Ctrl-C.

## BlackBox V21 factory sounds

The official BlackBox V21 download installs the firmware but contains no sound bank.
OpenPocket's NAM, preset and loader storage overlap parts of the stock sound bank.
V21 checks all 20 amp and 20 cabinet records at startup and disables the sound bank
when any of them are invalid. In that state M-EFCS cannot restore it one model at a time.

Use the web installer to install the official [BlackBox V21 package](https://www.m-vave.com/appdownload)
if the pedal is running another version. When it shows `BlackBox_021`, click **Verify factory sounds**.
If the check fails, use the steps below on a Mac with M-EFCS, Python 3 and libusb installed
(`brew install libusb`). Connect only the BlackBox and close other apps using the pedal.
The recovery port uses USB Mass Storage, which [WebUSB protects](https://wicg.github.io/webusb/#has-a-protected-interface-class).
The installer can enter recovery mode over MIDI, and the Terminal tool handles the flash transfer.

1. In Terminal, from the OpenPocket repository, start the read-only check with the same V21 file.
   Replace the example path with the location of your download.

   ```sh
   sudo python3 tools/blackbox_v21_factory.py --package ~/Downloads/BlackBox.fwsc
   ```

2. When Terminal is waiting for UBOOT, click **Enter BlackBox USB recovery** under step 3
   in the installer. The tool checks the complete installed V21 firmware against the download,
   saves a flash backup in `~/OpenPocket-rescue`, and reboots the pedal.
3. Once the read-only check passes, run the same command with `--write` and click the
   recovery button again when Terminal is ready. This replaces the 20 factory amps,
   20 cabs and 80 presets. The tool checks the entire flash after writing. It only
   writes sectors in the factory bank at `0xBF000..0xFCFFF`.
4. After the pedal returns, click **Verify factory sounds** in the installer. This
   compares every record the running firmware returns with the vendor's factory bank.

The tool uses the AM4 files included in the installed M-EFCS app. If they are stored
elsewhere, pass `--assets DIRECTORY` containing `blackbox_am4_ampCab.bin` and
`blackbox_am4_preset.bin`. The files must match the AM4 bank in M-VAVE's official Windows
updater. If USB disconnects during a write, re-enter recovery and rerun the write.
Keep the first flash backup.

## What the first real run showed

The first run on a real, looping BlackBox was done by the captain, who reported that the rescue worked ("the rescue worked!").
Only the files the tool left behind are recorded here; the captain's terminal output was not kept.

| Item | What the files show |
| --- | --- |
| Backups | Two, in `~/OpenPocket-rescue`: `blackbox-20261105-213039.dump.bin` and `blackbox-20261105-213135.dump.bin`, 1,048,576 bytes each (a 1 MiB flash), written 56 s apart. They are byte for byte identical (same SHA-256), so both hold the flash from before any write. |
| What the pedal held | Application `BlackBox_936`, the Bluetooth test image that caused the loop. Its app area passes the package's own CRC checks. |
| Head `[0, 0x4000)` | Identical to the head of the official BlackBox_016 `flash.bin`, so the head check passes. |
| Sectors to write | 159 of the 160 sectors in `[0x4000, 0xA3000)` differed from BlackBox_016 (first `0x4000`, last `0xA2000`). |
| Restored package | BlackBox_016 (`BlackBox (2).fwsc`, the tool's default image). |
| Chip key, flash id | Unknown: they are printed, not saved. The tool's checks (key `0x980F`, 1 MiB flash) passed, because it went on to write. |
| Length of the UBOOT window, sectors written, time taken | Unknown: not saved. A later run should keep its output (`make rescue 2>&1 \| tee rescue.log`). |

The two backups' times are consistent with a read-only run followed by the `--write` run, but no file records which was which.

## Safety rules

- The default is read-only. Writing needs `--write`.
- Writes are limited to whole 4 KiB sectors in `[0x4000, end of the image)`; for the BlackBox package that is
  `[0x4000, 0xA3000)`. The end comes from the package and can never be above `0xFF000`. The head `[0, 0x4000)` (header,
  second-stage boot loader, chip key) and everything above the image (Bluetooth trims, user data) are never touched.
- Only a fixed list of loader commands can be sent. Chip erase (`FB02`) and key burn (`FC12`) are not on it. In a read-only
  run the erase and write commands are not on it either.
- It stops without writing when the device is not `WL82 UBOOT1.00`, the chip key is not `0x980F`, the flash is not 1 MiB, or
  the pedal's head differs from the image's head (a package for another layout).
- The backup is complete on disk before the first erase.
- After a restore, do not reinstall the Bluetooth test image until the cause of the crash loop is understood: the loop came
  from that image.

## If it does not work

| Symptom | Cause and next step |
| --- | --- |
| `libusb not found` | `brew install libusb` (or set `LIBUSB_PATH`). |
| `cannot take the interface from macOS (LIBUSB_ERROR_ACCESS)` | Run the command with `sudo`. |
| Waits forever | The pedal is not in UBOOT right now. It is there about 44 s out of every 70 s while it loops; check `ioreg -p IOUSB` for `WL80UBOOT1.00`, a data cable and a direct port. |
| `REFUSED` | A safety check stopped it; the message names which. Nothing was written. |
| `gave up after N tries` | The pedal kept leaving UBOOT before the run could finish. Rerun: it resumes. |
| Still nothing | The SD-card recovery in `flash/` (fm1buddy on an ESP32-S3) is the other route. |

## Credits

The USB boot protocol is documented by [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (MIT) and
[konsumer/fm1buddy](https://github.com/konsumer/fm1buddy) (GPL-3.0); `tools/uboot_rescue.py` is a new implementation of the
same commands for macOS. Tests: `make rescue-check` runs it against a simulated pedal (`tests/uboot_sim.py`), with no hardware.
