# Bluetooth audio test image (A2DP: the phone's music through the pedal)

An optional build of OpenPocket that makes the pedal a **Bluetooth speaker**, as the stock BlackBox firmware is: it shows up on a
phone as **`OpenPocket`**, pairs with no PIN, and the phone's music plays **mixed into the pedal's output**, with the guitar.

It is a test image, its own build target (`make bt`). The normal firmware and the installer's packages are unchanged.

> **Status: test build 44** (below; build 38 is the one played longest, about 2 hours of music). Test build 27 brought the web
> editor over Bluetooth. Up to test build 14: builds 1 to 10 ran on a pedal: the
> radio starts, a phone sees the pedal and links, but no phone
> could pair, and the guitar crackled. Build 11 paired an iPhone (below, "Test build 11"), but no music came out, and the guitar
> still crackled with Bluetooth on. Build 12 takes the phone's packets where the controller first shows them, sends the output
> in two slots against the crackles, and records the second core's fault (below, "Test build 12"); on some power-ons it
> crashed in the reverb, 2 s after power-on. Build 13 stops the reverb's loop at its block's end and counts it, and shows the
> registers of a crash (below, "Test build 13"). Build 14 is the same image on OpenPocket v42, with v42's own fix of the
> crackles' cause and the Bluetooth library folded at build time to make the room, and it keeps the pairing across power-offs
> (below, "Test build 14"). Every part that can be checked on a computer is checked (see "Checked on a computer").

## What is in it, and what is not

- **In:** classic Bluetooth (BR/EDR) with Secure Simple Pairing ("Just Works", no PIN), the A2DP sink with SBC, as the stock
  firmware. The music is summed to **mono** (the pedal's output is mono), at **-6 dB**, added **after** the MASTER volume and
  **before** the output's soft knee. The phone's own volume sets its level. It keeps playing in bypass and in the tuner.
- **In since test build 14:** the pairing remembered after a power-off (a phone paired once connects again without being
  paired again), kept in two flash sectors of the pedal's own (below, "The pairing kept").
- **Its own effects** ([OWN-FX.md](OWN-FX.md)): LOW OCTAVE, a 6th PITCH model (an octave under the low strings only),
  SYN PAD, a 12th SYN model (a chord caught on the left footswitch and held under you), since test build 40 TREBLE
  BOOST, a 5th DRV model (a germanium treble booster), and since test build 44 TAPE FLUTE, a 13th SYN model. Test build 44
  also puts right SHIMMER, BARITONE and MONO SYNTH and rebuilds SITAR and STRINGS. Only this image has them; the release's
  engine is unchanged.
- **In since test build 24:** Bluetooth MIDI (BLE MIDI) beside the music, for a Chocolate (below, "Test build 24"); since
  test build 27 the web editor over Bluetooth too, on a second LE link beside the Chocolate's (below, "Test build 27: the web
  editor over Bluetooth"); since test build 28 the regular Chocolate too, the pedal as its central (below, "Test build 28").
- **In since test build 43:** USB audio, the pedal as a USB sound card, as the release's (v42), with what the computer records
  (the DI too) and where its sound comes back into the chain chosen on the pedal (below, "Test build 43").
- **Not in:** play/pause from the pedal (AVRCP), a level setting on the pedal. (Stereo: since test build 42, with SETTINGS'
  OUTPUT on STEREO, the phone's music and the effects come out in stereo; MONO, the default, sums it as before.)
- **Left out to make room** (the release image has them): 8 of the 12 factory NAM models, since test build 28 (it keeps TWIN, AC15, JCM900 and MARK IV; a preset with another one
  plays a kept one; the capture slots the editor fills are all there); delay times over 780 ms, since test build 24 (TIME
  stops at 780 ms, REVERSE at 390 ms, against 2 s and 1 s).

## Build

```
make bt           # firmware/openpocket-bt.bin and build/bt/PocketX_942.fwsc, BlackBox_942.fwsc, manifest.json
```

(`node tools/op.mjs bt` is the same; `OP_BT=1 sh firmware/build.sh` inside the container.) CI builds it on every pull request:
the artifact `openpocket-bt-audio` holds the packages (`build/bt/`), the image, its symbol table and its link map.

## Flash it

Your pedal must already run OpenPocket.

1. `make serve-bt` opens the offline installer on `http://localhost:8765/` with `build/bt/` as its package folder (Chrome or Edge,
   USB cable). Or copy the artifact's `build/bt/*.fwsc` and `manifest.json` into `docs/firmware/` and use `python3 tools/serve.py`.
2. **Connect**, **Install**. The start-up screen reads `v42 BT AUDIO TEST 14` (the number is the test build's).
   Before pairing with a phone that saw an earlier build, forget the pedal there first (iPhone: **Settings > Bluetooth**, the
   **i** next to `OpenPocket` or `Accessorio`, **Forget This Device**). Builds up to 13 kept no pairing, so the phone holds a
   key the pedal does not have. From build 14 on the pedal keeps it, across power-offs and across installs of later test builds:
   forget the pedal once, when you first pair with build 14.
3. **Back to normal:** `make serve` (the release packages) and **Install** again. The SD-card recovery (`make appimage`) works
   as before. Installing writes only the application area, as any update does. This image itself also writes its pairing store
   (two 4 KB sectors at `0xEE000`, just below the presets) when a phone pairs; the release firmware never reads or writes them,
   and an update leaves them as they are.

## What you should see

| Step | On the pedal (status area, bottom right) | On the phone |
| --- | --- | --- |
| 1. start-up, a few seconds | `BT START 2`, `3`, `4`: the radio starting (the stages are the BLE image's; the digit can be cut off on the play screen, the log's bluetooth line has it) | |
| 2. ready | `BT VISIBLE`, then the visible line scrolls (below) | **Settings > Bluetooth**: `OpenPocket` appears under other devices |
| 3. tap it | `BT LINK`, the link line scrolls | paired and connected, **no PIN asked** |
| 4. play music | `BT PLAY` | the music plays **from the pedal's output**, with the guitar |
| 5. pause / disconnect | `BT LINK` / `BT VISIBLE` again | |

**A/B key, held while switching the pedal on, until the start-up screen has gone** (for that boot only; the log's `c1` line
shows `KEY`). Switch off for real (USB out too, 10 s) before each boot you compare:

| Key | That boot |
| --- | --- |
| **REV** | Bluetooth off (`BT OFF`): the same image with the radio off, to compare the sound with and without Bluetooth by ear |

(CAB and DLY held at power-on do nothing since test build 13: the c1 line only shows `KEY 1` or `KEY 2`. Test build 12's DLY
key, the output in one slot as build 11 sent it, is gone: its A/B is settled. Since build 14 the output is sent as v42 sends
it, in place of the two slots (below, "Test build 14"). A CAB key for the stock firmware's supply levels did not fit in the
flash.)

`FAILED s c` means the start failed: the line that scrolls says which step and stage (the BLE image's codes,
[BLE-TEST.md](BLE-TEST.md#reading-the-status-text)). The stall watch, the crash screen and the `LAST START FAILED` report (v42's
name; up to test build 13 `LAST BOOT FAILED`) work as in the BLE image, and after a failed boot Bluetooth stays off for that
boot, so the pedal can always be re-flashed. Since test build 13 this image's report shows the registers of a crash (`R1` `R6`,
then `R12` `SP`) where the BLE image shows `DSP CODE AT`; the lines below them move one line down, and v42's `PHOTO THIS FOR A
BUG REPORT` is the screen's last line. Photograph both screens (below, "Test build 13").

### If it stalls (`VEC 3F`, then a safe-mode boot)

The stall watch ends a Bluetooth task that runs for 4 s without giving the processor back: the crash screen, `VEC 3F`, then a
boot in safe mode with Bluetooth off. The `PC` and `RETS` on that crash screen are whatever the watch's 10 kHz tick interrupted,
which with sound running is mostly the audio interrupt (`fm1_cpu1_wait`, `a_collect`): not the stuck code. This image keeps a
**stall record** instead: during the last second before the watch fires, the tick samples the stuck task's own PC and RETS (from
the interrupt stack), and when it fires the record takes the rest. The safe-mode boot shows it, on the log's last three lines,
until Bluetooth starts again:

```
bluetooth  STALL btctrler S8 BB 00000107 0000FE02 00000000 0000FFFF 00000001 CLKN 00001234 00001240 L 3/3 PW 00000400 00000000 0000C000
link       STALL HEAP 9120 MIN 8800 MF 0 0 IRQ 0/0 EV 0 STUB 0 CR rf replay, rf replay done, btctrler, request_irq 40, request_irq 41, low_power_get
trace      STALL PC 02012345/02011111*9876 N 10000+0 D 28-160 SP 01C7BF80
```

| Field | Meaning |
| --- | --- |
| `STALL btctrler S8` | the task that was running and the start-up stage (the BLE image's stages: 3 = the radio replay, 7 = after it, 8 = the baseband asked for its interrupts) |
| `BB` | the classic baseband's registers 0x20000, 0x2000C, 0x20014, 0x20018 and 0x2001C (1 there: a clock latch the library asked for never completed) |
| `CLKN a b` `L x/y` | the classic baseband's native clock latched twice, 1 ms apart, and the reads each latch took (65535: it never completed: the clock does not run) |
| `PW` | 0x2FC40, 0x2FC6C (the Bluetooth domain's power and clock words the classic code waits on) and 0x10010 |
| `HEAP` `MIN` `MF n size` | the Bluetooth heap free now and at its lowest, failed allocations and the last one's size (the library stops in a loop on one) |
| `CR` | the last calls the library made into the firmware, oldest first |
| `PC pc/rets*n` | the stuck code: each PC and RETS sampled, and how often (look them up in `build-bt/symbols.txt` of the same CI run) |
| `N` `D` `SP` | samples taken (+ those that fit no slot), the interrupt stack's depth at the samples, the crash's stack pointer |

### The visible line

`BT VIS IRQ 40:1234 41:56789 EV 4 RDY SCAN 1 V F BD 0B0B ADDR 024F1A2B3C4D UID 1 HEAP 9120 STUB 0 Q40 0 KICK 0 TX 0/592/2032 F0 RX 0/0/12272 F0`

| Field | Meaning |
| --- | --- |
| `IRQ 40` `41` | the classic baseband's interrupts (packets, the alarm) and the slot timers: they rise when the radio runs |
| `EV` | events from the stack; `RDY`: it said it is ready (`NORDY`: it did not, the pedal made itself visible anyway after 2 s) |
| `SCAN` | times "visible and connectable" was sent to the controller |
| `V` | the stored radio calibration: found records (`F` = all four; since test build 14 the BLE image's flags in full: `10` and `20`, VM 106 or 107 missing and v41's defaults given in its place, `400` the calibration measured, `100` and `200` its scan or trim failed), the VM area (flash / 4096), the crystal trim (as the BLE image) |
| `ADDR` | this pedal's Bluetooth address, made from the flash chip's unique ID (`UID 1`; `UID 0`: not read, the fixed BLE address is used) |
| `HEAP` | the Bluetooth heap's lowest free bytes |
| `STUB` | calls of the stand-ins (functions this image leaves out) and the last one's name (the trace line lists them in order) |
| `Q40` | seconds since IRQ 40 last rose. While a phone is searching it stays near 0; a large number means the scans have stopped |
| `KICK` | times the pedal asked for its scans again because IRQ 40 had been silent for 5 s while visible (test build 4); since test build 28 30 s while an LE link is up, and `@` the last one's time (ms since power-on) |
| `TX` `RX` | the radio's send and receive pools: bytes in use / the most ever in use / the pool's size, and `F` failed allocations. While a phone is linked, `RX` must rise: the phone's packets wait there (test builds 1 to 5 never used it, see below) |

(Test build 11 also had `RNG a/b`, the random source the pairing's keys come from: it worked, and build 12 drops it.)

While no phone is linked, the link line shows the Bluetooth tasks instead (test build 8): `TASKS btapp S 01C0A1B0=0 q0 F3012
P0, btctrler Q q0 F2344 P3, btstack R q2 F1876 P7`, each task's state (`R` ready, `Q` waiting for a message, `S` waiting
on the semaphore or mutex at that address, `=` its count, `D` sleeping, `X` ended) and `q` its queued messages. A task that
stays `S` while its `q` grows is the stuck one: look its address up in `build-bt/symbols.txt`. Since test build 11 also `F`,
the bytes of its stack (4 KB) never used, `P`, the most messages it ever had queued, and `OVF` if it came within 64 bytes of
its stack's end.

The `hci` line (test build 9) is the HCI conversation between the controller and the host, which in this integrated stack
travels through the tasks' queues: the newest 11 messages (test builds 9 to 11 also had `hci0`, the first 12 since boot; build
12's a2dp line took its place, and build 13's FIFO line took the a2dp line's). `n` all messages posted to a task, `ACL` data packets to the host and `NCP` Number of Completed Packets events
(only counted), `Q` the deepest a task's queue has been and `LOST` the posts refused because a queue was full (the library
tries some of them again after a short sleep, so not each one is a message lost). Each entry: the task
(`S` the host, btstack; `C` the controller, btctrler; `E` btencry; `A` btapp), the message's type digit, then its first 5
bytes in hex. `S1` is an event to the host: the event code, the parameters' length, the parameters (`S1:0E04010C03`: Command
Complete, 1 command allowed, opcode 0x0C03; `S1:04..` Connection Request; `S1:03..` Connection Complete, status next; `S1:05..`
Disconnection Complete; `S1:31` IO Capability Request; `S1:33` User Confirmation Request; `S1:36` Simple Pairing Complete).
`C2` / `C3` are commands to the controller as its task receives them. Since test build 11 the `hci` line also has `WI`, waits
the library asked for inside the baseband's interrupt and was refused (each one a message or an allocation the library then
dropped), `WM`, waits asked by the main loop (a software timer's callback, which cannot wait), `CNF`, the pairing
confirmations the pedal sent, and `END cc:rr`, how the last link ended: `05:rr` a Disconnection Complete with its reason
(`13` the phone ended it, `08` the link was lost, `22` an LMP response timed out, `05` authentication failed), `03:ss` a link
that failed to come up and its status. A successful pairing walks `S1:31`, `S1:32`, `S1:33`, then a `C3` message (the
confirmation), `S1:36 00` (Simple Pairing Complete), `S1:18` (the link key) and `S1:08 00` (encryption on).

The trace line ends with `CR`, the last calls the library made into the firmware, oldest first (the stand-ins by name,
`request_irq 40`, `low_power_get`, `kick: scan again`, `send pool full`...).

### The link line

`BT PLAY n1 ENC 1 A2DP 1 PLAY 1 SBC 44100 2CH BP53 FR 51234 ER 0 PK 7345 BUF 98+52 MS UND 0 RATE -0.04% DISC 0 FQ 6 F A3012/C2344/S1876`

| Field | Meaning |
| --- | --- |
| `n` `ENC` `A2DP` `PLAY` | links so far, encrypted links, A2DP channels, times the music started |
| `SBC 44100 2CH BP53` | the stream: its rate, channels, bitpool (53: the usual "high quality") |
| `FR` `ER` `PK` | SBC frames decoded, frames refused, packets decoded (test build 11 stayed at one a session: no music) |
| `BUF a+b MS` | the music waiting: still encoded in the FIFO (a) and decoded (b); it keeps about 150 ms in all |
| `UND` | times the decoded music ran out (an underrun: a short gap) |
| `RATE` | how much the pedal is speeding up or slowing the music to follow the phone's clock (about -0.04 % is normal) |
| `DISC` | disconnections |
| `FQ` | the phone's packets waiting in the FIFO (test build 12; 16 at most, about 6 to 10 while it plays). Test build 11 had `PD` here |
| `F` | each Bluetooth task's stack never used, in bytes, after its initial: `A` btapp, `C` btctrler, `S` btstack (`!`: it nearly overflowed) |

### The isr line (since test build 11)

`ISR 40:12/310/1450 41:4/40/90 T5 9/31 AG 2144/2935 AI 846/942 EN 0/344/344/0/0 OC 0/3 OD 0 OG 12/240 C1 1 G 0 NAM 1 LD 42 PK 55/97 XR 3 CL 0 WC 0 BYP 0 TM 1234 VM 4/2/1/0/1 OVF 0 NAN 0`

What the interrupts cost the audio, and the DSP's state, for the crackles. Times are in microseconds.

| Field | Meaning |
| --- | --- |
| `40:a/b/c` `41:a/b/c` | the classic baseband's two interrupts: the longest in the last second, the longest since boot, and the time they took in all in the last second. They run above the audio |
| `T5 a/b` | the 10 kHz timer (USB, the stall watch), also above the audio: the longest in the last second / since boot |
| `AG a/b` | the longest gap between two audio interrupt entries (last second / since boot). 2,902 when one entry serves a period; less (2,144 in build 11's log): two entries a period, the input's and then the output's, so AG is the period less the input's stay; more: an entry came late. A preset change or a flash write is not counted |
| `AI a/b` | the audio interrupt's longest stay (last second / since boot), the DSP and its wait for the second core included |
| `EN a/b/c/d/e` | (test build 12) the audio interrupt's entries in the last second, by the flags they found: none / the output's only / the input's only / both / another channel's. About 344 a second each is two entries a period; `d` counts the periods whose output callback ran first; `e` must be 0 |
| `OC a/b` | (test build 14) the output blocks the driver asked for before their input's, in the last second / since boot (v42's `firmware/px_aorder.h`, `a_swaps`). Up to test build 11 each one repeated a 2.9 ms block and skipped the next: a click on any sounding note; builds 12 and 13 sent the block before (their `OC a/b c/d` counted the changes of order); since build 14 each such block is filled as soon as its input's block is made, in the same entry, with no latency more, unless its input's callback comes too late (`OD`). Each full preset change and each flash write (the audio interrupt held) adds one by itself |
| `OD` | (test build 14) of those, the ones whose input's callback came more than half a block (1,451 µs) after the output's, since boot (`px_audio.h` `A_ORDER_TICKS`): each one a click, that half of the output playing its old block again. It must stay 0 |
| `OG a/b` | (test build 14) the longest from an output callback that came first to its input's callback (last second / since boot): the radio's interrupts (`40`, `41`) and `T5` running between the two. `OD` rises once it reaches 1,451 µs |
| `C1` | 1: the second core runs; 0: the whole DSP is on one core (heavy presets then go to `BYP`) |
| `G` | why the second core is off: 0 it is not; 1 it misread shared memory, 2 a job was not back within 8 ms, 4 it faulted (1, 2 and 4: retired in this boot, and barred at every start until a power-off); 3 barred at start (by such a retirement, or by a crash of the first core while it ran, in an earlier boot of the same build); 5 it did not answer at start. The c1 line has the rest |
| `NAM` | 1: the preset's NAM amp is one that runs on both cores; 0: not (test build 12: whatever `C1` is) |
| `LD` `PK a/b` | the DSP load, and its peak since the last full preset change / since boot, % of a block's time |
| `XR` `CL` `WC` `BYP` | late blocks (each one a likely crackle), clipped blocks, the delay and reverb lane's cut blocks (clicks in the tails; since build 12 kept across a retirement or a reset), bypassed |
| `TM` `OVF` | the library's software timers that fired, task stacks that nearly overflowed |
| `VM r/w/s/f/q` | the pairing store (test build 14; builds 11 to 13 had `VM r/w`): `r` and `w` the library's reads and writes of its records; `s` the copies saved to the flash since power-on; `f` the save rounds that failed (the first two tried again 1 s later; from the third on only a new change tries again); `q` the flash copy's sequence number: at power-on that of the copy loaded (0: none), then one more for each slot a save tries. `q` 0 with `f` 3: the flash read differently the second time at power-on and was left alone, the pairing in RAM only for that boot. `node tools/op.mjs log` says it in words |
| `NAN` | (test build 12) the engine's resets after a NaN, since boot: each one 15 to 50 ms of silence |

### The FIFO line (test build 13)

`FIFO 000004D2 00000000 00000005 00000002`: the music path's counts since boot, 4 words in hex (`node tools/op.mjs log` prints
them a second time with their names). It is the log's seventh line, in place of test build 12's a2dp line (`A2` and 23 words:
the controller's own A2DP state for each packet, and a 10 kHz watch over it). That probe is gone in build 13, to make room for
the reverb's cut and the crash report; the music plays from the FIFO whatever the controller's state says, and the heal stays.

| Word | Meaning |
| --- | --- |
| 1 | packets the FIFO took (about 70 a second while the music plays) |
| 2 | packets it could not take: full in the low half, too big in the high half (`00010003`: 3 full, 1 too big) |
| 3 | packets the library handed the pedal, given back undecoded (the FIFO already has them) |
| 4 | heals: the receiving connection's two A2DP fields written back when found wrong, on the connection the host opened only |

### The c1 line (test builds 12, 13)

`C1 1 G 0@0 K 2232 KEY 0 S 00000000 F 0/0 W 0`; after a cut of the reverb's loop, 4 words in hex after `W`'s count; after a
fault of the second core, 19 more words in hex.

| Field | Meaning |
| --- | --- |
| `C1` `G g@t` | the second core runs; why it is off (as in the isr line) and when it was retired (ms; wraps every 179 s) |
| `K` | how deep the second core's stack has gone, in bytes (of 2,816) |
| `KEY` | the key held at power-on: 0 none, 1 CAB and 2 DLY (nothing in this build), 3 REV |
| `S` | where Bluetooth was when the main loop first saw a fault: stage << 28 \| state << 24 \| DSP held << 23 \| 1 << 22 \| IRQ 40's count |
| `F n/c` | the second core's faults, and which core took the exception (0: the first, the usual case) |
| `W` | (test build 13) the reverb loop's cuts at its block's end in this boot; after one, `n` and `k` at the latest cut, the time the main loop first saw a cut (ms since power-on, wraps every 179 s) and the `S` word then (how to read them: "Test build 13") |
| then | DBG (the debug unit's message: bits 16-18 a bus error, 10 a PC limit), the taking core's EMU_MSG, PC, RETS (the first core's when it took the exception), the second core's own EMU_MSG, the time (ms), the job handed and the job finished (different: one was running), its function, and the debug unit's ten words from `0x1EEE358` (the bus masters' ids of an invalid access, the write-limit error counts) |

The line holds 223 characters: with a cut and a fault of the second core in the same boot, the debug unit's last four or so
words are cut off.

## Please report

- What happened at each step of the table, the phone (model, iOS / Android version), and a photo of the status area at the
  furthest step reached (or of `FAILED ...`).
- `node tools/op.mjs log --follow` while you try it (USB cable): it reads the same lines over USB, so no one has to read the
  scrolling text. Send its output.
- With the music playing: `node tools/op.mjs dsp --presets 1,14,18,19,20,21,24 --seconds 8`, and the same with the music
  paused. The question this image is for: does a streaming radio push heavy presets into `BYP` or late blocks?
- By ear: clicks or gaps in the music (and whether `UND` rises), hum or buzz in the guitar sound while the music plays.
- For crackles: the `isr` line while they happen, and the same preset with Bluetooth off (REV held at power-on). If they are
  there with Bluetooth off too, the radio is not the cause. If they come only with Bluetooth on and nothing is playing (a
  high-gain preset, the guitar's volume at zero), ticks at the scans' rhythm point to the radio's supply or RF reaching the
  analog side, which no firmware change can fix.
- After any boot whose c1 line shows `G 4` or a `W` other than `W 0`: the c1 line as it is (its words, looked up in
  `build-bt/symbols.txt` of the same CI run, say where the second core faulted). A red crash screen: a photo of it (PC, RETS,
  VEC, DBG), before switching off, and one of the `LAST START FAILED` report that follows it (since test build 13: the registers).

## Known limits and risks

0. **The radio's interrupts run above the audio's** (test build 4): the classic baseband's handlers have slot deadlines
   (625 µs), and OpenPocket's audio interrupt runs its DSP work in blocks of 128 frames, up to 2.9 ms (stock runs its dry chain
   in its audio interrupt too, but in blocks of 64 frames, with the radio below it). A heavy preset may run late more often
   while Bluetooth is busy: the `isr` line (test build 11) shows each interrupt's cost and whether the audio started late.
   Since test build 5
   they enter through OpenPocket's own interrupt entry (`firmware/bt/bt_isr.S`, flags saved first, `csync` before `rti`) and
   C bodies that follow the library's two handlers (`firmware/bt/bt_irq.c`): the library's own handlers end without `csync`,
   and in test build 4, above the audio, the DSP they interrupted faulted (the reverb, `VEC 1`, 0.5 s after the DSP started).

1. **A pairing is saved 1 s after the library's last change to it** (test build 14; builds 11 to 13 kept it in RAM until
   power-off). A power-off within that second, or during the save, loses that pairing: the phone then says it cannot connect,
   and must forget `OpenPocket` and pair again. A power-off at any other moment loses nothing (the previous copy stays until the
   new one is complete).
2. **Saving a preset while the music plays** gives a short gap (interrupts are off while the flash is written). Saving a pairing
   does too, about 25 ms, once per pairing (the audio held as for a preset).
3. **The decoded music, the SBC decoder's state and the mix buffer sit in 7.5 KB of SRAM** (`0x01C06000..0x01C07E00`) that
   OpenPocket never used before. If anything else writes there, you would hear clicks or noise in the music, nothing worse.
4. **The flash window is larger in this image** (the package's whole app slot, `0x9D740` bytes, instead of `0x8DFBC`): the classic
   stack is bigger than the LE one. The packages and the update loader already used that slot size.
5. **Pedals that ran BlackBox_021** before OpenPocket may have lost part of their stored radio calibration (021 kept it at
   `0x9F000`, inside the area OpenPocket rewrites); the `V` field shows what was found. Classic Bluetooth is stricter about the
   crystal trim than BLE.
6. **Regulation:** the pedal was certified with the stock firmware. This image uses the library's own power table; it is a hobby
   experiment, not a product.

## How it works

```
openpocket.c (OP_BT_AUDIO)   the BLE test image's hooks: ble_test_start() after the screen, ble_test_poll() in the main loop
firmware/bt/bt_a2dp.c        the app: start (a task runs btstack_init), the stack's events, the music: packets -> SBC -> ring
firmware/bt/bt_sbc.c         Android's SBC decoder behind three calls: the packet's first frame, one frame, mono
firmware/px_bt_ring.h        the decoded music's ring and its rate, read by px_audio.h dsp_out (in RAM, on either core)
firmware/bt/bt_glue.c        what the stack needs from the system: the radio pools' allocator (bt_lbuf.h), low power, the
                             address (the flash's unique ID), the name, the SDP record (the library's own A2DP sink record)
firmware/bt/bt_ps.h          the pairing records: the library's VM file in RAM, and its copy in the flash (test build 14)
firmware/bt/bt_cfg.c         the stack's configuration: classic only, one link, an A2DP sink (the SDK's values, Apache-2.0)
firmware/bt/bt_stubs.c       empty stand-ins for what the image leaves out (LE, TWS, calls, other profiles), counted
firmware/bt/app-bt.ld        the memory map: the classic stack's sections, its pools, the ring
firmware/bt/members.txt      the JieLi objects compiled (third_party/jieli-ble + third_party/jieli-bt)
tools/jlfold.py              since test build 14 they are compiled folded: their configuration constants known (bt_cfg.c)
firmware/ble/                shared with the BLE image: the scheduler and heap, the radio start-up, the stall watch, DIAG
```

- **The Bluetooth stack is the stock firmware's:** JieLi's classic controller and host from the AC79 SDK (Apache-2.0, LLVM
  bitcode, the archives `third_party/jieli-ble` already pins). The SDP record the phone reads is the library's own, byte for byte
  the stock firmware's (`JL_A2DP`).
- **The SBC decoder is Android's** (`third_party/sbc-android`, Apache-2.0), fixed point, about 2 % of one core.
- **The music path:** the radio's receive pool (12 KB) holds the phone's packets as they arrive; where the controller shows each
  media packet to the A2DP code, the pedal copies it into a 16-packet FIFO (test build 12: the controller's own hand-over to the
  application failed from the second packet of each session); the main loop decodes the FIFO into a 58 ms ring of mono samples;
  the audio path reads the ring at a rate the main loop steers, so the pedal follows the phone's clock (the two differ by about
  0.04 %) with a backlog of 150 ms, by linear interpolation. Nothing of the stack runs in the audio interrupt: it only reads the
  ring.
- **Memory** (beyond the BLE image's): three task stacks of 4 KB in NOINIT (test builds 1 to 10: four of 2.75 KB, the fourth
  never used), the baseband's table (4.2 KB) and the receive pool in the SRAM below the RAM, the ring, the decoder's state and
  the mix buffer above it, the pairing store (684 bytes of RAM; since test build 14 its copy in two flash sectors), the FIFO
  (10.8 KB of the Bluetooth heap); in the RAM code window's free end, the output block (512 bytes; builds 12 and 13: two, 1 KB)
  and, since test build 14, the engine's buffers that v42 moved below the RAM (14.6 KB: there they would take the baseband's
  table and the receive pool's place).
- **The baseband's 64 KB window.** The classic baseband reaches memory as 16-bit offsets from its table (`bd_base`, register
  0x2FC44): everything the library hands it must lie less than 64 KB above `bd_base`, and the library asserts it (a failed
  assertion stops in a loop). So the send pool (2 KB, also the page and inquiry scan buffers), the controller's own variables
  and the Bluetooth heap come first in the RAM, as the SDK keeps them next to `bd_base` (its `btctler_lib_bss.ld`), and the link
  checks it (`firmware/bt/app-bt.ld`). Test build 1 had the send pool at the end of the RAM, 118 KB above: its assertion was
  the stall at 5.5 s.
- **The radio pools' allocator** (`firmware/bt/bt_lbuf.h`) keeps each pool's header at the pool's first byte: the library
  hands `lbuf_init` the pool and then uses the pool's own address as the pool, never the value returned. Test builds 1 to 5
  aligned the header to 16 bytes; the receive pool (0x01C02DE4) is only 4-byte aligned, so every allocation from it failed and
  a phone's link died after a few seconds (`RX 0/0`).

## Test build 11

Builds 1 to 10 got the radio up and a phone linked, then each failed further along. A review of the whole path, against the
library's own code, found four walls between "linked" and "paired", one behind the other, so all four are fixed together:

1. **The host ignored the link.** It tells a classic link from an LE one by comparing the handle with `get_ble_link_handle()`,
   and that was a stand-in returning 0: no handle was classic, so every packet of the phone's was dropped and L2CAP, SDP and
   A2DP never heard of the link. It returns 0x50 now, the library's own value (`bt_glue.c`).
2. **Nobody confirmed the pairing.** "Just Works" still asks the host to confirm (HCI 0x33); this host leaves the answer to the
   application, and the pedal never gave it, so the phone's timer ran out. The pedal confirms now (`bt_a2dp.c`), as the SDK's
   demo does.
3. **The phone's SDP question was never answered.** The SDP server waited, for a phone whose maker it does not know, for its own
   SDP client, which this image does not have. It answers at once now (`bt_cfg.c`, `bt_stubs.c`).
4. **After a failed link the pedal stayed invisible.** The controller turns the scans off when it makes a link and leaves it to
   the host to turn them on again; the pedal now does at every link's end (`bt_a2dp.c`).

Also: the scheduler's clock no longer wraps every 179 s (every timer and sleep pending at the wrap used to stop for good); no
wait is made inside the baseband's interrupt any more (the library asks, and was always told "not in an interrupt"); three task
stacks of 4 KB with their high-water marks; the pairing kept in RAM until power-off; the music packet held at a stop given back.

For the crackles: since test build 4's crash, the second core was most likely **barred** on this pedal: that crash set the bar,
and every test build after it had the same id, so the bar stayed until a real power-off, and the whole DSP ran on one core,
with the radio's interrupts on top. Each test build now has its own id, and the stall watch's own crash no longer bars it: the
first boot of build 11 runs on two cores (`C1 1` in the `isr` line; the screen shows the cores only where it shows the load:
in bypass, `BYP C1 ...`, for 2 s after a late block, `C1 42%!`, and with Bluetooth off by REV, `BT OFF C1`; while Bluetooth
is on, the status area scrolls the Bluetooth line instead). Then the
`isr` line measures what the radio costs the audio, and REV at power-on gives the same image without the radio, to compare by
ear. A preset change now also waits for silence to reach the output before it holds the audio interrupt (up to build 10 the
output buzzed at 172 Hz for the length of the recompute, in every image), and the phone's music fades with the guitar.

## Test build 12

Build 11 paired an iPhone and the phone streamed (about 70 packets a second), but the pedal played nothing (`PK 1`), and the
guitar still crackled with Bluetooth on, never with it off (REV). A review of the build-11 log against the library's code found:

1. **No music.** The controller decides, for each media packet, whether it goes to the application or to the host: it keeps it
   for the application only if the packet's channel is the one the application opened. In build 11 only the first packet of
   each session was kept (the one the channel's open marked itself); every later one failed that test and went to the host,
   which has nothing to play it with. No code in the library could have undone the open; why the test fails is not settled. So
   the pedal now copies every media packet where the controller first shows it to the A2DP code, before that test, into a
   FIFO, and plays from there, whatever the test then decides. The new a2dp line measures the test's inputs for each packet,
   writes them back when wrong (`H`), and watches them 10,000 times a second, to settle the cause. (Build 13 keeps the
   writing back and drops the rest: the FIFO line.)
2. **Crackles.** The audio interrupt's driver (the SDK's) calls the output callback and the input-and-DSP callback each when it
   finds its own flag up. Build 11's isr line shows two entries a period, input first (`AG 2144` = the period less the DSP's
   stay), and seconds where the order flipped (`AG` just under the period): a few microseconds of extra latency (the radio's
   interrupts, a cold cache line) flip it. With one output buffer each flip played one 2.9 ms block twice and skipped the
   next: a step on any sounding note, with no late block, more often with Bluetooth on. The output now has two slots: the
   output callback always sends the block before the newest, whatever the order (2.9 ms more latency). `EN` and `OC` in the
   isr line count the entries and the flips; DLY at power-on brought build 11's single buffer back, to confirm by ear (gone in
   build 13: the two slots stay).
3. **The second core's fault.** It faulted (`G 4`) in every Bluetooth-on boot of build 11 and never with Bluetooth off. Its
   record is now kept and printed (the c1 line): the debug unit's message and bus-master ids, CPU1's own exception word, the
   time, the job, the stack's depth, and where Bluetooth was. (A CAB key for the stock firmware's supply levels, the cheap test
   of the "misreads under load" the board's notes describe, did not fit in the flash: the record decides whether it is needed.)

Also: Bluetooth's flash reads (the radio records, the address) run before the audio starts, and the radio's start-up burst and
the flash reads wait for the second core's job in flight (the flash path is off meanwhile); `WC` survives a retirement; the
log's ninth line; the build checks the library's static tables are where the a2dp line reads them (in build 13, where the
heal reads `channel`), and keeps the compiled controller code (`listings/` in the artifact) to compare with the library's IR.

## Test build 13

Build 12 played the phone's music. But on some power-ons with Bluetooth on it crashed about 2 s after power-on, while the radio
started: the red crash screen showed `VEC 1`, `DBG 00002000` (the first core wrote into a guard band) and a `PC` in `rv_block`,
the reverb. The photos and the compiled code tell how, not yet why:

- The reverb's loop runs over a chunk of 16 frames (the wet lane) or a block of 64 (the chain). In build 12's code the compiler
  made its end test "stop when k equals n", so a k that once got past n never stopped: the loop's store marched 13 KB through
  the RAM, until the guard band at `0x01C74000` stopped it.
- Why k got past n is open: something wrote over n or k on the loop's stack (A), or one read of n, or one branch, went wrong
  on the first core during the radio's start-up (B).

What changed:

1. **The reverb's loop stops at 64 frames, whatever its end says, and counts it**: `W` in the log's c1 line, with n as the loop
   read it and k. No caller passes more than 64, so in normal running `W` stays 0. A cut costs a click instead of a crash:
   up to 48 frames are still written past a 16-frame chunk, into the chunks after it, the lane's other buffer or the drive and
   amp's oversampling buffer, so the click can be in the reverb or, for one block, in the amp's sound. (The delay's loop has
   the same form; its cut did not fit in the flash.) In the code CI built (run 43039170246, `listings/px_dsp.s`), the cut also
   brought the end test back to "k below n": the loop now has two exits, and the compiler no longer rewrites it. A k that
   gets past n now just ends the loop. So a cut means that n was read too large, and a k that jumps is no longer counted.
2. **The `LAST BOOT FAILED` report shows the crash's registers**: `R1` and `R6`, then `R12` and `SP`, in place of `DSP CODE AT`;
   the lines below move down, and its `PLEASE TAKE A PHOTO` line is gone (the red screen before it asks already).
3. **The room for them:** build 12's a2dp line (its probe of every packet and its 10 kHz watch) and the DLY key are gone. The
   heal stays, and the log's seventh line is now the FIFO line.

### What to do

Switch off for real between boots (USB out too, 10 s). After each boot, once the pedal has started (about 10 s), read the
log once: `node tools/op.mjs log --count 1`. `W` starts from 0 at every boot, so a single reading at the end would show the
last boot only. Send all the readings.

| Boots | Phone | Key at power-on |
| --- | --- | --- |
| 5 | any | REV held (Bluetooth off) |
| 5 | its Bluetooth off until the pedal shows `BT VISIBLE` | none |
| 5 | its Bluetooth on and near | none |

If a boot crashes: a photo of the red crash screen, then one of the `LAST START FAILED` report that follows (it stays 12 s),
then the log reading. That reading is of the safe-mode boot, whose DSP is off: it cannot show the crashed boot's `W`.

Before the third set, pair the phone with the pedal once in this build (step 3 of "What you should see"), and do not forget
`OpenPocket` on the phone between these boots: it must still list the pedal. The pedal forgets the pairing at power-off, but a
phone that remembers it may try to connect while the pedal starts. That is part of the test. Note with each reading which set
it belongs to: the log tells the REV boots (`KEY 3`) from the others, but not the second set from the third.

### Reading W

`W 0`: no cut of the reverb's loop was counted in this boot. After a cut: `W count n k time S`, the count in decimal, the
rest in hex. `node tools/op.mjs log` names them and says which line of this table applies. The cut is taken when k reaches 64
(40 in hex), and it records that k.

| W shows | It means |
| --- | --- |
| `n` 10 (16) and `k` 40 (64) | n was read too large once, and was 16 again when the cut read it: a one-off wrong read of n on the first core (B) |
| `n` 40 and `k` 40 | the same in the chain's call (a block of 64), not in a lane's chunk |
| `n` anything else, `k` 40 | n's word on the stack was written over (A): its value is the writer's fingerprint |
| `k` above 40 | not expected with this end test: k jumped while n was also read too large. `0020xxxx` is the second core's known misread |
| `k` below 40 | a k below 64 cannot pass the cut's own check: that check misfired once (B), or k's register changed |
| the time and `S` | when the main loop first saw the cut (ms since power-on, wrapping every 179 s like `UPTIME`) and where Bluetooth was then, the same word as the c1 line's `S`. The main loop does not run during the radio's start-up replay (stage 3): a cut there is seen only once the replay has returned, at stage 7 or a step after it (the numbers are not in time order: 4, ready, comes after 7), and later than the cut itself (build 12 crashed at 1,971 ms) |

`W 0` does not rule out everything: a k that got past n ends the loop without a cut (this build's end test), and an n written
over with a value up to 64, or a k thrown back, still ends the loop late (up to 48 frames past the chunk) without a cut.

Many cuts in one boot, or cuts long after the start-up, are worth saying: the crash was a one-off at the radio's start-up.

### Reading the registers (if it still crashes)

The cut covers what a wrong n or k did in build 12 (above). If the crash comes back, something else went wild, and the
report's registers say what. Read `DBG` first (on the `UPTIME` line): `00002000` (bit 13) is the first core writing into a
guard band, as in build 12, and the rows below apply. `00200000` (bit 21) is a bus error flagged on the first core, most
likely the delayed flag of holding the second core (build 4's crash): the `PC` and the registers are then only where the first
core happened to be, and R1, R6 and R12 say nothing about the loop.

| The report shows | It means |
| --- | --- |
| `R1 -  R6 -` and `R12 -  SP -` | the saved registers are not this crash's (not expected: every crash of the first core saves them; a fault of the second core retires it, `G 4` in the log, and makes no report) |
| `PC` in `rv_block` | not expected: with its end test and the cut, build 13's loop cannot reach a guard band. Its registers moved too (CI listing): n is reloaded into r0, k is in r6 (r7 in the other three variants) and the store's pointer in r5, so `R1`, `R6` and `R12` are not the loop's n, k and pointer here. Send the photo: the developer reads them with the listing |
| `SP` | the stack at the crash: about `01C7BE2C`..`01C7BE40` when the audio interrupt came from the main loop |
| another `PC` | the registers are that code's: look the PC up in `build-bt/symbols.txt` of the same CI run |

**Checked before flashing build 13** (CI run 43039170246, `rv_block` in `listings/px_dsp.s`, the compiler's assembly):

- the four loop heads each copy k through the empty asm and branch to one shared cut path with an unsigned
  `if (r0 >= 64)`;
- each loop's end test is `ifs (k < r0)` with n reloaded from `[sp+128]` on every sample (build 12's was an equality), and
  the cut path records n from that same word and the k it compared;
- the frame is build 12's: `sp += -192`, n at `[sp+128]`, k saved at `[sp+88]` around the call to `rv_front`;
- the registers moved: n reloads into r0, k is in r6 (r7 in the other variants) and the store's pointer in r5. `rn` in
  `firmware/openpocket.c` was left at r1, r6 and r12: with this end test the loop cannot reach a guard band, so a crash
  report from build 13 is about some other code, and its registers are read with that code's listing.

**Again for build 14**: v42 puts `rv_front` inside `rv_block`, so its code and frame change. Before flashing, check the same
points in build 14's `listings/px_dsp.s`: the loop heads' unsigned compare and shared cut path, the end test `k < n` reloading
n on every sample, the frame, and which registers hold n, k and the store's pointer.

## Test build 14

The same image on OpenPocket v42 (v41: the NAM network split between the cores by a worker that stays on the second core,
the NAM's hand routines rdm6, rdm15 and post5; v42: the pedal's controls and screens, the audio callbacks' order, USB audio,
which is not in this image). What changed for Bluetooth:

1. **The crackles' cause, fixed as v42 fixes it.** v42 found the same fault from the other side (`firmware/px_aorder.h`): the
   10 kHz timer, above the audio interrupt, makes it enter late, both callbacks are due, and the output's runs first. An output
   block asked for before its input's is now filled as soon as that input's block is made, in the same interrupt, so the
   block goes out with no latency more. Builds 12 and 13's two slots (one block, 2.9 ms, more latency) are gone, and `OC` in
   the isr line counts these blocks. What can still click is the time between the two callbacks of one entry, where the
   radio's interrupts and the 10 kHz timer, above the audio's, can run for longer than v42 measured: an input's callback that
   comes later than the window after its output's (`px_audio.h A_ORDER_TICKS`: half a block, 1,451 µs, in this image; v42's
   725 µs elsewhere) is taken for another entry's, and that output block is left as it was (a click). `OD` counts those,
   `OG` the longest that time got (`AI` is the whole stay, the DSP included, and does not show it). Two costs of filling the
   block in its own entry, which builds 12 and 13 did not have: in every period the audio interrupt waits there for the
   second core's block (its job or the layer split's blocks after the amp), so the main loop, which decodes the music and runs
   the Bluetooth tasks, gets less of the first core's time; and the block must be ready within the period, the entry's
   lateness included, where the slots had a whole period more.
2. **The second core's worker.** While a NAM preset plays, v41's layer split keeps one job running on the second core. The
   radio's start-up burst and the flash reads (`ota_fread`), which switch the flash path off, waited for the second core's job
   to end; this one never ends, and that wait would retire the core after 8 ms (`G 2`, then `BYP` on NAM presets until
   power-off). They now wait until the worker idles in its loop (RAM code) with nothing left to do (`px_audio.h
   a_cpu1_quiet`), within the same 8 ms. A core retired there (it never idled, or it faulted) is collected there at once, as
   v42's own retirements are: one core from then on, the engine reset (15 to 50 ms of silence, `NAN`), and `G` keeps why
   (`4` for a fault). Left to the next audio interrupt, that interrupt would have handed network blocks to the stopped core,
   waited 8 ms for them, and retired it again as `G 2`.
3. **The library folded at build time** (`tools/jlfold.py fold`, the folding probe's variant B): the configuration constants
   the firmware gives it folded in, the print helpers the firmware leaves empty dropped: 94.7 KB less flash. v41's and v42's
   firmware does not fit the app slot without it. The code is JieLi's IR compiled again with its constants known, not the
   published objects: pairing and the music must be checked again. In the container, `OP_BT=1 OP_BT_FOLD=0 sh
   firmware/build.sh` builds the library as before (the image then does not fit). The probe still runs: its `B` must give
   "the real image's bytes".
4. **Memory.** v40.4 moved the engine's buffers (14.6 KB) to the SRAM below the RAM, where this image keeps the baseband's
   table and the receive pool: here they stay in the RAM code window, as in builds 12 and 13 (`firmware/bt/app-bt.ld`). To make
   that room, the NAM's hot routines (rdm6, rdm15, post5) run from the flash, as in the release (the BLE image keeps them in
   RAM, `firmware/build.sh PXN_HOT_RAM`). How they run from the flash with the radio's code also running from it is not
   measured: compare `LD` and `PK` of a NAM preset with build 13's.
5. **v42's screens**: `LAST START FAILED`, and an overload shows a red banner. The status area still shows the Bluetooth state
   (scrolling while visible or linked); v42's load text replaces it only after a late block (`DSP 88%!`) or in bypass, and
   beside `BT OFF` or `BT START` it is cut off: the isr line has `LD` and `C1`. HOME on the play screen opens the preset list;
   HOME held opens the tuner, then the diagnostics; both footswitches together open the tuner. A new id for the second core's
   bar, so a bar left by build 13 does not carry over.
6. **The visible line's `V`** shows the BLE image's flags in full (`F` stays `F`): v41 gives a pedal whose VM 106 or 107 is
   missing the Pocket X backup's values instead of zeros (`10`, `20`), so the crystal trim beside it may be that default.
7. **The pairing kept** (below, "The pairing kept").

### The pairing kept

Up to build 13 the pedal kept the phone's pairing in RAM: after a power-off the phone still had its key, the pedal did not, and
the phone could connect only after **Forget This Device** and a new pairing. Build 14 keeps it in the flash
(`firmware/bt/bt_ps.h`):

- **What is kept:** the Bluetooth library's own pairing records (21 records of 32 bytes: the link keys of the phones that paired
  and what it learnt about them), exactly as the library writes them.
- **Where:** two 4 KB sectors at `0xEE000` and `0xEF000`, just below the presets, which OpenPocket never used (the stock
  firmware kept cabinets there). Each holds 5 copies of the records; a copy carries a sequence number and a CRC, and its mark is
  written last, so a copy cut short by a power-off is never taken for a good one. At power-on the newest good copy is loaded.
- **When:** 1 s after the library's last change, from the main loop, with the audio held as for a preset save (about 25 ms of
  silence). The library changes the records when a phone pairs, when a phone it knows connects after another one, when a
  phone's key fails, and the first time it learns a phone's type; not when the same phone connects again. So a save comes with
  a pairing, not with every connection.
- **Wear:** a sector is erased once every 5 saves, at a power-on (before the audio and the radio start), or during a save in a
  session of more than 5. At the 100,000 erases a NOR flash sector is commonly rated for, about a million pairings.
- **Not kept:** a pairing made less than 1 s before a power-off, or one whose save is cut by it (the copy before it stays).
  The BLE image keeps nothing of this, and the release firmware never touches the two sectors.

How to test it:

1. On the phone, forget `OpenPocket` if it paired with an earlier build. Pair it with the pedal (step 3 of "What you should
   see") and play some music.
2. `node tools/op.mjs log`: the isr line's `VM` shows a copy saved, e.g. `VM 4/2/1/0/1` (`s` 1); the tool says "1 saved to
   the flash since power-on".
3. Switch the pedal off for real (USB out too), wait 10 s, switch it on.
4. Once the pedal shows `BT VISIBLE`, the phone connects by itself, or after a tap on `OpenPocket` in its Bluetooth list, **with
   no Forget This Device and no new pairing**. Play music: it comes through the pedal.
5. `node tools/op.mjs log` again: `VM` shows `q` 1 or more with `s` 0, e.g. `VM 4/0/0/0/1` ("a copy loaded from the flash at
   power-on"). If the phone did not connect: this reading, and whether the phone listed `OpenPocket` as connected, failed, or
   not at all.
6. The same after pairing a second phone (or the same phone again, after forgetting the pedal on it): each pairing raises `s`
   and `q`, `f` stays 0, and after a power-off the phone paired last connects.

A power-on whose `VM` shows `q` 0 and `f` 3 found the copies reading differently on a second look: it left the flash alone and
kept the pairing in RAM only for that boot (as builds 11 to 13); the next power-on reads it again. Report such a reading.

### What to check

1. "What you should see", steps 1 to 5, with the phone that paired with build 13 (forget the pedal there first): the folded
   library pairs and plays.
2. The crackles: with the music playing, the guitar through CHURCH ORGAN and a heavy NAM preset. `OC` rising (beyond one for
   each preset change and flash write) with `OD` at 0 and no clicks is the fix working, and `OG` says how close those entries
   came to the window (1,451 µs); with clicks, the isr line (`OD`, `OG`, `40`, `41`, `T5`, `XR`) while they happen. Clicks with
   `OD` 0 and `XR` 0 point at a block not ready within its period: build 13's two slots are the way back.
3. The load: `node tools/op.mjs dsp --presets 1,14,18,19,20,21,24 --seconds 8` with the music playing and paused, against build
   13's figures (the NAM routines from the flash, item 4).
4. The music's decoding: with the music playing for a few minutes, the link line's `UND` and `BUF` on a heavy preset without NAM
   (`C1 1`, a high `LD`) and on a heavy NAM preset, against build 13. `UND` rising where build 13's stayed at 0 is the first
   core's new wait in each period (item 1) leaving the main loop too little time.
5. The crash at the radio's start-up (about 1.96 s): the same 15 boots as build 13 ("What to do"), with `W` and `G` of each.
   Build 12's record showed the second core trapping at that moment in every Bluetooth boot; v41's worker keeps it busy through
   the whole start-up on a NAM preset. Before flashing, check `rv_block` in the listings as for build 13 ("Reading the
   registers"). With the pairing kept, the third set's phone (its Bluetooth on and near) now connects during the start-up.
6. The pairing kept: "How to test it" above.

## Its own effects: LOW OCTAVE and SYN PAD

On top of test build 14 the image has two effects of its own (their knobs, the PAD's footswitch and CC 64, settings to
start from: [OWN-FX.md](OWN-FX.md)), built only here (`engine/px_dsp.h` `PX_OWN_FX`, which `firmware/build.sh` sets under
`OP_BT`). Their room is paid for in this image: the drive and delay code no model reaches is left out (the same sound, bit
for bit) and the delay ring is 456 ticks shorter (the longest delay is still 2.0 s). Every other model sounds as before.
The web editor sees the image as `v42 FX` (`v42 FX2` since test build 40, with TREBLE BOOST).

What changes on the pedal:

- PIT has a 6th model, LOW OCTAVE; SYN a 12th, PAD.
- With a PAD on in the preset, the left footswitch catches the chord (it no longer steps back a preset there) and, held
  0.7 s, fades it; both footswitches together still open the tuner. MIDI CC 64 at 64 or more catches the chord too.

Before flashing, in CI's artifact: the `hi RAM`, `pool tail` and `RAM` lines of the build log (the RAM code window should
have about 4 to 5 KB more free than test build 14; the pool tail 40 bytes left, as before), and `rv_block` in
`listings/px_dsp.s`, which should read as build 14's apart from its labels' numbers (the effects' state keeps the reverb's
fields where they were; in `linked.txt` its addresses move).

What to check:

1. LOW OCTAVE at `LOWEST`, GROWL and COMP at 100, in front of a NAM preset, the music playing: `node tools/op.mjs dsp`
   against the same preset without it (late blocks, `BYP`).
2. A PAD on a preset without NAM, after a catch: the c1 line's `K` (the second core's stack, of 2,816 bytes).
3. Around a catch with a heavy NAM preset: `PK` and late blocks.
4. By ear: the octave on the low strings only, the pad holding a chord steady, no clicks at a catch or a crossing.

## Test build 24: BLE MIDI, the delay at 780 ms, tap tempo

- BLE MIDI beside the music, as test build 23 meant it (23 did not link: a stand-in for `le_controller_handler_for_ble5`
  was missing).
- The delay's ring is 18,176 ticks (`PX_DLY_SHORT`): TIME stops at 780 ms, REVERSE at 390 ms; the screen shows 780 for
  any TIME of 63 or more. The 53,760 bytes this frees in the pool take the NAM model, its rows and the IR's spectrum
  (`PX_BT_POOL`), about 24 KB out of the main RAM.
- The music's packet queue holds 32 packets (464 ms, was 16) in the pool, no longer in the Bluetooth heap: its 10,816
  bytes go back to the heap, which BLE now shares.
- While the screen draws, the queued music is decoded every 4 ms (`bt_yield`, called from the screen's two DMA loops):
  the long drawings under the NAM network's load used to leave the 58 ms ring dry (`UND`) and overflow the queue. The
  FIFO line's `GAP max/n/y`: the longest wait between two decodes while playing (ms), the waits longer than 58 ms, the
  decodes the screen asked for.
- Tap tempo: MIDI CC 116 at 64 or more is a tap. Two taps 200 ms to 2 s apart set TIME to their interval, from the third
  on to the mean of the last three; a pause of more than 2 s starts over (`firmware/px_midi_io.h`, `tests/midiio.c`).

What to check: the Chocolate connects over Bluetooth and its footswitches act; the music with a heavy NAM preset while
changing pages and presets: `UND`, `GAP` and the drops stay low.

## Test build 25: the expression pedal

One expression pedal, on MIDI CC 117 (a Chocolate Plus with a pedal in its jack: CubeSuite's expression pedal page, CC 117,
0 to 127), moves what the preset has on:

- a **WHAMMY** (PIT): heel the note as played, toe its SHIFT (the interval, up or down), so SHIFT 75 is an octave up at the
  toe; its GLIDE still smooths;
- else an **AUTO WAH** (SFX): heel to toe the sweep CENTER makes, 350 Hz to 2.25 kHz (DEPTH 0 for a plain wah pedal; with
  DEPTH, the LFO moves around the pedal);
- else the **volume**, before the delay and the reverb, so closing it leaves their tails ringing (half way about -18 dB, the
  heel silent).

The position stays out of the preset: nothing to save, nothing redrawn, on every page. When the pedal comes back to the volume
from another job (a wah turned off), the volume stays at unity until the pedal moves, so a wah left at the heel does not
silence the next song. Until a first position comes the engine plays as before, bit for bit (`tests/ownfx_equiv.sh`). The
volume is smoothed over 10 ms, the wah over 15 ms, the whammy over 15 ms and then its own GLIDE (`engine/px_dsp.c` `px_exp`,
`tests/engine/exp.c`, `tests/midiio.c`).

What to check: the pedal as the volume (swells, the delay's tails after it closes), a wah, a WHAMMY with a NAM preset (its
pitch shifter runs as before: late blocks, `BYP`), and that turning a wah off at the heel keeps the sound.

## Test build 26: the music through a save

Test build 25's first log with music showed `GAP 2031/2`: the main loop stopped for 2 s once, when a preset was saved. After
the flash write, the save page kept "SAVED" on the screen for 0.7 s in a plain wait, with the radio unserved: the music's
ring ran dry (`UND`), its queue overflowed (dropped 4) and the radio's receive pool filled (`RX .../12096/12272 F16`). The
wait now serves the radio as the main loop does (`ui.h` `UI_IDLE`, `ble_test_poll`). The flash write itself still holds the
audio, the guitar's too, for a moment: save between songs. The crash screen no longer runs the music's decoder while it draws.

## Test build 27: the web editor over Bluetooth

The web editor ([`editor.html`](https://isod89.github.io/openpocket/editor.html), or `http://localhost:8765/editor.html`
after `make serve-bt`) now also reaches the pedal over Bluetooth MIDI, the link the Chocolate uses: from a Mac, from Chrome on
Android, or from a Web MIDI browser on an iPhone or iPad (MIDIWeb). Nothing changes on the page: the computer or the phone
shows the pedal as a MIDI port named `OpenPocket`, and the editor finds it there as it finds the USB one. USB works as before,
also with both open: each request is answered the way it came.

**Two Bluetooth MIDI links at once.** The pedal takes two Bluetooth LE links (up to test build 26: one): the Chocolate on
one and the editor on the other, beside the phone's music (the classic link), so the sound can be tweaked from the phone while
the Chocolate switches the effects. The pedal goes on advertising while a link is free, so the second central finds it while
the first is connected, and again after either goes. The Chocolate's Control and Program Changes act from either link; the
editor's replies go only to the link that asked, and a change made from the Chocolate shows in the editor as a change made on
the pedal does.

### Connect

- **Mac.** Unplug the pedal's USB cable (a charger is fine), so that the editor finds the Bluetooth port and not the USB one.
  Open **Audio MIDI Setup** (Applications > Utilities), **Window > Show MIDI Studio**, then the Bluetooth button in its
  toolbar (**Configure Bluetooth**): `OpenPocket` is listed, **Connect**. Then open the editor in Chrome and press
  **Connect**. When done, **Disconnect** in the same window (the Chocolate stays connected meanwhile).
- **Android.** Open the editor in Chrome and press **Connect**. Chrome sees a Bluetooth MIDI device only once Android has
  connected it: if the editor does not find the pedal, connect `OpenPocket` with a BLE MIDI app (for example "MIDI BLE
  Connect"), leave it connected, and press **Connect** again.
- **iPhone, iPad.** Safari has no Web MIDI: use a Web MIDI browser such as MIDIWeb. Open the editor's address there, connect
  `OpenPocket` from its Bluetooth MIDI panel, then press **Connect** on the page.

The first time a browser connects to this firmware, the editor reads its catalogue (the blocks, their models and the values'
texts): about 150 requests and 1,800 notifications, about 20 s over Bluetooth (a moment over USB). The browser keeps it, so the
next connections start at once; a browser that already used the editor over USB with this build has it already.

### What works, and how fast

Everything the editor does over USB: presets, live editing, the changes made on the pedal shown on the page, saving, backups,
NAM models and cabinet IRs. Bluetooth is slower than USB, about 2.5 KB/s from the pedal: a knob follows at once, a list or a
model takes longer (a NAM model goes in 67 pieces: some seconds). A save (a preset, a model, an IR) holds the audio during the
flash write, as a save on the pedal does (test build 26: save between songs), and the radio waits too: if the link drops there,
the editor says the pedal is lost; connect again.

Over USB only: installing firmware (the installer), the rescue (UBOOT) key, and the log channel (`node tools/op.mjs log`: its
DIAG reply is too long for Bluetooth and is dropped). Nothing but the editor's frames is taken from Bluetooth.

### How it works

- The pedal reads the editor's SysEx in the MIDI characteristic's writes, across writes (BLE MIDI cuts it, with a header and
  timestamps): `firmware/px_ble_midi.h` `px_blem_sysex`, in a pass of its own beside the Chocolate's reader, whose messages are
  as before. One frame at a time, up to 160 bytes.
- The reply goes into the LE side's own buffer of 1 KB (`firmware/bt/bt_le_midi.c`), so the USB editor's buffer is free at
  once. The host's task sends it as notifications of the MIDI characteristic from its `ble_profile_loop` hook, in packets of 20
  bytes (one link-layer packet each, whatever MTU the central asked for), each acknowledgement of the controller bringing the
  next. Only a few wait unacknowledged in the controller: about 2 for each 15 ms of the link's interval (1 at 7.5 ms, 2 at
  15 ms, 4 from 30 ms), so the editor takes the same small share of the radio whatever interval the central chose, and the
  music keeps the rest. A reply that sends nothing for 3 s is dropped (the editor asks again).
- A push (a change made on the pedal) waits for the last reply to be out, and a request from Bluetooth for the last push.
- A new LE link, or the end of one, drops what was left of the last one's on it (a reply, a frame, the pushes).
- Memory: 1,024 bytes for the replies and 160 for the frame being read, in the main RAM, and a few words.

**Two links** (`firmware/bt/bt_le_links.h`, `firmware/bt/bt_cfg.c`). The controller's radio entities go from one to two
(`config_btctler_le_hw_nums`, and the host's `config_le_hci_connection_num`, which may not pass it): advertising takes a free
entity, and a central's connection turns it into the link and ends the advertising (`ll_adv.c`), so two entities give one link
and advertising, or two links. The library's multi-link scheduling (`multilink_schedule.c`, linked since test build 23, and
`config_btctler_le_slave_multilink` on since then) places the two links' events; no other library member is needed. After a
link is made or ends, the pedal asks for advertising while a link is free, and asks again every 2 s while one is (asked while it
runs it does nothing), so a central that comes later finds it. Each link has its own reader for the editor's SysEx, so the
Chocolate pressing between the writes of an editor's request on the other link spoils nothing; the musical messages of both
links share the reader they had, which reads each Chocolate message from its own timestamp and status.

What it costs: about 2.3 KB more of the Bluetooth heap with both links up (the host's second connection, 1,240 bytes,
`btstack_memory.c`; the bonding table's second entry, 56; the LE baseband's second entity, 664, `RF_ble.c`; the second link's
state, 336, while it is up), against about 13 KB left at its lowest before; about 0.2 KB of main RAM (the second link's SysEx
reader). And radio time: advertising every 500 ms (three short packets, about 0.3 % of the air) while only one link is up, and a
second link's connection events. Whether the music stays clean with the three links up is the thing to check on the pedal.

### In the log

The MIDI line's LE words (`node tools/op.mjs log` over USB: plug the cable back after the test, the counts stay until
power-off) start with `LE 2 L3 ... A4/0 I12/24`: the links up (0 to 2), the links made since power-on, advertising asked n times
/ its last answer, each link's interval (1.25 ms units). Once the editor has used Bluetooth they end with `ED 12/340 N1/0`: the
frames answered over Bluetooth / the notifications sent, and each link's client configuration (`1`: notifications on; `0` on the
editor's link with notifications sent means its central never wrote it, as one bonded before may not, and the replies went
anyway). Then, when there are any, `X` and the replies dropped (no link, too long, or 3 s without progress) and `SX` and the
SysEx dropped (too long, unfinished, or come while one waited).

### What to check

1. The music playing from the phone, the Chocolate on and connected (the log's `LE 1`). From the Mac (USB unplugged): connect
   `OpenPocket` in Audio MIDI Setup, open the editor, **Connect** (`LE 2`). The catalogue loads (about 20 s the first time),
   then the presets.
2. A knob in the editor: the pedal follows at once. A knob on the pedal: the page follows. The music meanwhile: no skips.
3. The Chocolate's footswitches while the editor is open: the effects switch, and the page shows them.
4. A preset saved from the editor (the music skips once, as a save on the pedal).
5. **Disconnect** in Audio MIDI Setup: the Chocolate still acts; connect again: the editor comes back.
6. The Chocolate off and on: it connects again while the editor stays.
7. The USB cable back: the log's MIDI line (`LE`, `I`, `ED`, `X`, `SX`), and the music's (`UND`, `GAP`).
8. The same from an iPhone with MIDIWeb (the music from that iPhone too: three links), and from Chrome on Android.

## Test build 28: the pedal as the Chocolate's central

The regular M-VAVE Chocolate (not the Plus) is only a Bluetooth peripheral: phones and computers connect to it, it connects to
nothing, so up to test build 27 it could not reach the pedal. M-VAVE's own firmware connects to it as its **central**, and so
does this build: while no footswitch is connected, the pedal listens for it (it scans), connects to it, finds its MIDI
characteristic and subscribes to its notifications; its footswitches then act as the Chocolate Plus's do (Control and Program
Changes, tap tempo, the expression pedal), through the same MIDI reader. The editor over Bluetooth (test build 27) works beside
it, and so does the phone's music.

### Use

1. Switch the pedal on, then the Chocolate (or the other way round). Within a few seconds the pedal connects to it and its
   footswitches act. Nothing to pair by hand: if the Chocolate asks for pairing, the pedal asks for Just Works pairing by
   itself (that path is untried: the log's `PR` counts it).
2. The first Chocolate the pedal links after power-on is the only one it takes until the next power-off (another player's
   Chocolate on stage is never taken). Its address is in the log's LE line (`@...`).
3. Keep the Chocolate away from a phone's app while the pedal looks for it: a Chocolate already connected to a phone does not
   advertise, so the pedal cannot find it.
4. **DLY held at power-on: no central role** for that boot: no scanning, the LE side exactly as test build 27 (two phones or a
   Chocolate Plus). It is the stage fallback if the scanning ever disturbs the music. (DLY used to leave out the rest of the
   stock power setup, a diagnostic of test builds 18 to 27; the stock setup is now always done.)

### How it works

- **Which one is the Chocolate.** It is the advertiser that shows the BLE MIDI service UUID and a name containing `FOOTCTRL`
  (the regular Chocolate's Bluetooth name in its quick guide and in players' reports) or `CHOCOLATE` (another of its manuals),
  without case, in its advertising data or its scan response, advertises as connectable and is heard at -80 dBm or more
  (`firmware/bt/bt_le_central.h`, `LE_CEN_NAMES`, `LE_CEN_RSSI`). Once one has been linked and subscribed, only its address.
- **Scanning** (active, for the scan responses' names) runs only while the footswitch's link is down: 10 ms every 200 ms, 5 % of
  the radio's time. With it found, the pedal stops scanning and connects (the library's initiator, which itself listens 5 ms in
  every 15 while the classic link is on), asking a connection interval of 30 ms at most, no latency, a 2 s supervision timeout
  (a flash write's pause does not end the link). A connection not made in 3 s is cancelled and the scanning goes on.
- **The client** is the pedal's own, not the library's GATT client (not linked): the ATT packets with an odd opcode, the
  responses and notifications, reach it through `att_dispatch_register_client`, and its requests go out as the server's
  notifications do. It reads the primary services until the BLE MIDI one, its characteristics until the MIDI one, its
  descriptors after the value until the client configuration, then writes it on (notifications). With the same Chocolate again
  it goes straight to that write, with the handles found before. An answer that asks for security makes it pair (Just Works)
  and ask again once the link is encrypted; an unanswered request (5 s), or pairing that encrypts nothing (8 s), ends the link
  and the scanning starts again.
- **The links.** Two LE links, typed: one for the footswitch (the pedal is its central), one for a phone or computer (the
  editor). The two radio entities give: advertising and scanning (nothing connected), the footswitch's link and advertising, a
  phone's link and scanning, or both links. So with the central role on, only one phone's LE link at a time (a second phone,
  or a Chocolate Plus beside the editor, needs DLY at power-on).
- **The library.** The controller's master link is `ll_master.c`, from the same pinned `btctrler.a` as the rest of
  `third_party/jieli-ble` (its two handlers were empty stand-ins up to test build 27); everything it calls was already in the
  image, apart from a Wi-Fi detection report (stood in) and two log switches. The roles: `LE_MASTER | LE_SLAVE | LE_ADV |
  LE_SCAN | LE_INIT` (`bt_cfg.c`). The scanner, the initiator and the multi-link scheduler were linked already.
- **Memory.** No more links than test build 27, so no more of the Bluetooth heap for them; the scanner's own link state (20
  bytes) while it scans; the client and the filter about 0.2 KB of main RAM; `ll_master.c`'s code in the flash.

### In the log

The DIAG's eleventh line (`node tools/op.mjs log`: the `le` row) holds the whole LE side: `LE 2 L3 W134 P1 A4/0 C24 P12 U2
24/0/200 D08@31500 IRQ 812/97 WD1@36000 ED 12/340 N0/1 CEN LINK R310 M2 T2 K2 S2 F0 MIDI 41 @A1B2C3D4E5F6`. After the links'
counts and advertising, each link up as its role (`C` the footswitch's, the pedal its central; `P` a phone's) and interval
(1.25 ms); when there are any, `U` the connection updates (count, then the last one's interval, latency and supervision timeout
in 10 ms), `PHY` the PHY updates (count, the last transmit/receive PHY), `DL` the data length changes (count, the last transmit
octets), `D` the last LE link's end (its reason in hex: `08` supervision timeout, `13` the other side ended it, `16` the pedal
did; `@` ms since power-on, as `SE` and `RC`); the LE interrupts; `WD` the watchdog's restarts (below) and the last one's time;
the editor's words (test build 27); then the central role: `CEN` and its step (`SCAN`, `INIT` connecting, `LINK`; `KEY` when
DLY was held), `R` advertising reports read, `M` the footswitch's matched, `T` connections asked, `K` links made, `S`
subscriptions, `F` failures (a link dropped by the client), `MIDI` its notifications; `PR` pairings asked, `CL` the client's
step and last ATT error, and `@` the footswitch's address once known.

### What to check

1. The music playing from the phone, the Chocolate on: within a few seconds its footswitches act. The log: `CEN LINK`, `S1`,
   `MIDI` rising with each press. `CEN SCAN` with `R` rising and `M0` means no advertiser passed the filter: the Chocolate's
   name is then neither of the two (a scanner app on a phone, such as nRF Connect, shows its name and services).
2. The Chocolate off and on again: it comes back by itself (`K`, `S` up by one).
3. The editor connected over Bluetooth meanwhile (test build 27): both work.
4. The music with the Chocolate away (scanning): `UND`, `GAP` as in test build 27.
5. DLY held at power-on: `CEN KEY`, and the LE side as test build 27.

### Test build 27's LE link drop

On the pedal, test build 27's editor link from an iPhone (MIDIWeb, no music) ended by supervision timeout (`D8`) about 20 s
in, as the catalogue read ended, and the LE side then stayed silent until power-off: the LE interrupts and the controller's own
thread stood still, and advertising, asked again (`A2/0`), never came back on air. The cause is not known yet. Build 28 adds:

- **A watchdog** (`bt_le_watch`, from the main loop): while something should be on air (advertising, scanning or a link) and
  neither LE interrupt has moved for 4 s (advertising alone moves them every 500 ms), it turns advertising and scanning off and
  on again, which closes their radio entities and opens new ones, scheduled afresh. `WD` counts it. Asking again without
  turning off first (build 27) does nothing: the controller answers "already open".
- **The classic scans' kick waits 30 s** instead of 5 while an LE link is up: the radio can serve an LE link for seconds on end
  (the catalogue read), and the drop came in a boot with two kicks (which came first is not known). `KICK` now has its time.
- **The link layer's procedures in the log** (`U`, `PHY`, `DL`, above) and the drop's time (`D08@...`), to see what the phone
  did before a drop.

What was checked in the library (its bitcode): a lost link's radio entity is freed (`le_hw_close`: its anchors cleared, a
suspended advertiser or scanner resumed); the Wi-Fi detection hooks on that path (`ll_wifi_detect_ext`,
`ll_update_ch_done_wfdet_callback`) return nothing, so their stand-ins change nothing; an LE link's end does not reach the
classic side's disconnection handling (`hci_vendor.c` sends it to the LE handlers only). The LE scheduler (`ble_slot_schedule`)
skips its pass when an interrupt arrives while a pass from the controller's task holds its lock: a lost pass is one way every
LE event could stop at once, not proven.

To check: the editor's catalogue read over Bluetooth from an iPhone, then 2 minutes idle: no `D08`; if it comes, the `U`
words before it and whether `WD` brought the pedal back (it shows in the phone's Bluetooth MIDI list again within a few
seconds, `A` rises, the LE interrupts move).

### Test build 28's hang at Bluetooth's start

On the pedal, test build 28 (c595492) stopped about 1.5 s after power-on: a watchdog reset, then safe mode, with no stall
record (`last boot FAILED: it stopped at FIRST 6 S, reset by WATCHDOG; blocks 126, load 80%`). The safe-mode boot's own c1
line (`V never`, `SE 0`) is that boot's, Bluetooth off, not the failed one's. 126 blocks of 2.9 ms with the DSP's load mean
the DSP had just started (1.5 s), the moment Bluetooth becomes visible in a normal boot (`V 1452`): there test build 28
starts advertising and, new, scanning for the footswitch.

The cause, in the library's bitcode: the LE event interrupt (`RF_ble.c ble_event_irq_handler`) holds the baseband's spinlock
(`bb_hdl`, a `testset` loop, interrupts off) for its whole run and calls `ble_slot_schedule`, which for a scanning or
initiating entity calls `le_hw_suspend`. That asks `cpu_in_irq()`: in an interrupt it uses `ble_hw_disable_isr`, otherwise
`ble_hw_disable`, which takes the same spinlock. This image's `cpu_in_irq()` counted only the classic baseband's interrupts
(`bt/bt_irq.c`); the LE ones jump into the library's own entries (`ble/ble_isr.S`) and could not count themselves. So the LE
interrupt took the lock it held, and spun there for ever with the interrupts off: nothing ran, not even the stall watch's
tick. Up to test build 27 no entity scanned or initiated, so nothing in an LE interrupt reached that question.

The fix (`ble/ble_os.c`): `cpu_in_irq()` is 1 also while the code runs on the system stack, where every interrupt runs and
nothing else does (crt0 points `ssp` there; the main loop runs on `_ustack`, the tasks on their own stacks; the stall record
relies on the same). Inside the LE interrupts the library now takes its interrupt paths, as in the SDK, and the waits of
`ble_os.c` refuse there. `tests/test_ble_taskq.c` checks it. The BLE image is unchanged (its `cpu_in_irq()` stays 0).

To check: a normal boot with the Chocolate away: the pedal starts, `CEN SCAN` and `R` rising in the LE line; then with the
Chocolate on.

### Test build 30's silent scanner, and test build 31's scanner alone

Test build 30 started (the hang fixed) but its scanner heard nothing: `CEN SCAN R0`, and the LE interrupts were advertising's
alone (98 and 90 in 16 s), with a Mac and an iPhone nearby. In the library's bitcode: beside advertising, a scanner starts
suspended (`RF_ble.c le_hw_scanning`: more than one radio entity), and only the slot scheduler, from the LE event interrupt,
resumes it, when a gap of its interval less its window (304 slots) lies ahead and the status register 0x28038's bit 1 is clear
(`ble_slot_schedule`). Alone, a scanner is enabled at once. Also: a failed open (`ll_scan.c ll_scan_open`: no radio entity free,
58) leaves the scanner half-open, and every later enable answers 12, "already open"; only an off clears it. (The scan
parameters are not an HCI command here: the host sets them directly, `ll_scan_set_params`, while the scanner is closed. The
host passes every advertising report on, whatever started the scan.)

Test build 31 (`LE_SCAN_ALONE`, `bt/bt_le_midi.c`): with no phone linked, advertising pauses while the pedal scans, 4 s
scanning, then 2 s advertising (`CEN PAUSE`), in turn, until the footswitch is linked; its connection is made alone too, and
advertising comes back with its link. With a phone linked, its link and the scanner, as in build 30. A scanner silent for 2 s is
turned off and on (no longer only asked again). New in the LE line, after `CEN`: `AH` while advertising is held, `RAW` the
advertising reports that reach the pedal's callback (before any test), `SO` the scanner starts asked, and `HW`: the library's
radio entities, `HW2 0:1/0 1:2/2 IE 03 BB 00` = two entities, slot 0 scanning (state 1, run flag 0: enabled at once), slot 1
advertising (state 2, 2: resumed); states 1 scanning, 2 advertising, 3 connecting, 6 the footswitch's link, 7 a phone's; run
flag 1 or 3 suspended, and `+n-m` after it the resumes and suspensions the main loop saw; `IE` the entities' interrupt
enables, `BB` the status register. `RH` counts the advertising reports the controller posted to the host (before the host
reads them), `RAW` those that reached the pedal's callback.

Also in test build 31, for a phone's link: with DLY held (no central role) test build 30 lost an iPhone's link every few
seconds, the phone ending it (`D13`), where build 27 kept it. `att_dispatch.c` gives every ATT packet with an odd opcode to the
client once one is registered (the footswitch's, since test build 28), and to the server only while none is: the client is now
registered only with the central role on, and it hands a packet of another link to the server (`AF` counts them). The `D` word
now ends with the link's life in ms (`D13@49866/2950`); `EN n/ss` counts a phone link's encryption changes and the last status
(`06`: the key is missing), `LTK n` the phone asking to encrypt with a key from an earlier pairing. This image keeps LE pairing
keys in RAM only: after a restart, a phone that paired before must forget the pedal (iPhone: Settings > Bluetooth, the `i` next
to `OpenPocket`, Forget This Device) if `EN` shows `06`.

Test build 33 adds the GATT server's side of a phone's link (`le_att_server_spy`, put in front of the library's own server
handler): `AT 14/13 E1 08:0007/0A M185/23 10>11 08>09 08>01 0A>0B` = the ATT requests that reached the server / the answers it
sent, the error responses (count, then the last one's request opcode, handle and error code), the phone's MTU / the pedal's
(Exchange MTU), and the last four request > answer opcodes, oldest first (`10>11` read by group type answered, `08>01` read by
type answered with an error, `0A>0B` a read). With the central role off the LE line ends after `CEN KEY` and the `HW` word.

Test build 33 also changes the footswitch's side, after test build 32's log (`RH135 RAW135 ... R134 M0`: the scanner, alone,
heard 134 reports and none passed the filter, the Chocolate in CubeSuite's "Manufacturer" mode, `CU` on its display; and an
iPhone's link died by supervision timeout after 1 s, `D08@49526/1044`, with the scanner resumed beside it):

- **No scanning while a phone's LE link is up** (`bt_le_links.h le_links_want_scan`): the footswitch first, then the phone.
- **The filter without the MIDI UUID**: a name containing `FOOTCTRL` or `CHOCOLATE`, connectable, heard at -85 dBm or more
  (`LE_CEN_RSSI`, -80 up to 32). After the connection the client looks for the BLE MIDI service; an address without it (no
  service, characteristic or client configuration; not a refusal for security) is not taken again this boot (`SK` counts
  them, four kept).
- **Why `M` stays 0**, on the `cen` line after `M`: `NM` reports showing a footswitch's name, `UU` reports with the MIDI UUID,
  `CN` connectable reports, `B` the best RSSI of a name-matched address, `LF flags/rssi` the last name-matched address (flags 1
  the UUID, 2 the name, 4 connectable: 6 or 7 is taken), `SK` the addresses skipped.
- **The central role on a line of its own**, the DIAG's twelfth, `cen` in `make log` (`bt_le_cen_line`): with the GATT
  server's and the filter's words the LE line passed the 223 characters a line holds, and its end, the footswitch's outcome,
  was cut off. In order: the state (`AH` with it), the outcome (`T K S F MIDI PR CL @`), the filter's view (`R M NM UU CN B LF
  SK`), the scanner's counts (`RH RAW SO`), the radio entities last (`HW ... IE BB`):
  `CEN LINK T2 K2 S2 F0 MIDI 41 CL5/00 @A1B2C3D4E5F6 R310 M2 NM40 UU40 CN40 B-60 LF7/-62 RH320 RAW320 SO3 HW2 0:2/2 1:6/0 IE 03 BB 00`.

A Mac's scan of the regular Chocolate in CubeSuite's "Manufacturer" mode, `CU` on its display (17 sightings): the name
`FootCtrl`, connectable, -76 dBm, the 16-bit service `1812` (HID over GATT, as a Bluetooth keyboard), no MIDI UUID, and the
manufacturer data `73696E636FD60508004A4C414953444B` ("sinco", then "JLAISDK": a JieLi chip as well). In that mode it is not a
BLE MIDI device: test build 33 links it, finds no MIDI service and skips it (`SK1`) until the pedal is powered off. For the
pedal the Chocolate goes in its MIDI mode, the one in which a phone lists it as "FootCtrl Bluetooth".

### Test build 34: the footswitch's link left alone while it is made

Test build 33 matched the Chocolate (in CubeSuite's "advanced custom mode": the name FootCtrl, no MIDI UUID) twice and
linked twice, and both links ended before any answer: `3E` ("failed to be established") after 197 ms, six events of 30 ms.
Advertising had come back on at the very connection complete, beside the new master link, and music was playing.

- **Advertising stays off** from the scan through the connection until the footswitch is subscribed, 10 s (a safety net), or
  the link's end (`LE_CEN_ADV_HOLD_MS`).
- **The central role's clock**: its steps (the scan's turns, the pause, the connection's time limit) ran only after a message
  of the host's task, and with no phone and no music nothing posted one: test build 33 stayed in `CEN PAUSE` for good. The main
  loop now wakes the host's task every 200 ms while the role is on and the footswitch is not subscribed (`LE_CEN_WAKE_MS`).
- **The last phone's call-back** (classic paging, 5 s after power-on) waits until the footswitch is linked, or 20 s.
- **The cen line**: `CD3E/197` the footswitch's last link's end and its life in ms (the LE line's `D` is any link's), `X3E n`
  the links that ended `3E`, `IV n` the interval its connection complete gave (1.25 ms), and `LF flags/rssi/t`: `t` the
  footswitch's connectable advertisements, 0 undirected, 1 directed (to another central, which a footswitch keeps to: it then
  ignores the pedal's connection request, and the link ends `3E`).

From the library's bitcode, on the master's establishment: the initiator is enabled at once when no LE link exists
(`le_hw_initiating`); the connection request takes its window, offset and interval from the link layer's parameters
(`__connect_req_pdu`, CSA #1: the features have no CSA #2); the new master link's first anchor is the request's window offset
(`le_hw_ioctrl` 6: offset x 2 + 4 slots, `__master_reset_transmitWindowDelay`), and with `config_btctler_le_master_multilink` 0
`master_setup_first_data_location` changes nothing: the vendor's single-master path, with no stand-in on it.

### Test build 35: the footswitch's link kept at 30 ms, and room for advertising beside it

Test build 34 on the pedal, in the gig's order (pedal, Chocolate in its standard mode, then the phone's music): the Chocolate
linked and changed presets (`CEN LINK ... K10 S7 MIDI 11`), the music played clean (`UND 0`, no drop), but its link kept
ending and coming back (`K10` in about three minutes, `X3E 3`, `CD08/2054`), and MIDIWeb never found the pedal. The cen line
showed why on two counts:

- **The link's interval**: made at 30 ms (`IV24`), it ran at 11.25 ms (`C9` in the LE line): the Chocolate asked for a faster
  link (an L2CAP connection parameter update request) and the library accepted it. Beside the music that is too much radio.
  Now `l2cap_connection_update_request_just` (l2cap.c's weak hook, before the library accepts a request) refuses any request
  below `LE_CEN_INTERVAL`: no update procedure, the link stays at 30 ms. `UR n+m/min-max` in the cen line: the requests
  refused (and accepted), the last one's interval range.
- **The advertiser beside the link never started** (`HW2 0:6/0 1:0/0`: the second radio entity in state 0, not 2).
  `le_hw_advertising` takes 2 x 263 bytes from the LE baseband's receive DMA pool, which `config_btctler_le_rx_nums` sizes
  (294 bytes each: 1,505 with 5), and a link keeps its own receive buffers there; when they are missing it stops before
  advertising ("RX DMA NULL"). Now 8 (2,387 bytes, 882 more from the Bluetooth heap); the setting sizes nothing else
  (`le_hw_init`). In the log, the second entity in state 2 (`1:2/..`) is advertising.

### Test build 36: longer supervision timeouts, and a slower phone link

Test build 35 on the pedal: the Chocolate held one link 101.6 s at 30 ms (it asked for 15 ms three times, refused, and stayed);
MIDIWeb found the pedal and the editor, the Chocolate and the music worked together for about 25 s (`W85`, `ED 84/658`,
`MIDI 5`). Then both LE links ended at the same moment by supervision timeout (`CD08/101619`, `D08@109540/26045`), with no
preset saved; the music's link stayed up (`UND 0`, `DISC 0`). The pedal linked the Chocolate again by itself, with advertising
beside it (`HW2 0:6/0 1:2/0`). The likely cause: the pedal's own link (its clock) and the phone's (the phone's clock, an
iPhone at 7.5 ms, four events in each of the Chocolate's 30 ms) drift into each other, and with the supervision timeouts at
about 1 and 2 s both end.

- **The footswitch's supervision timeout 6 s** (`LE_CEN_TIMEOUT` 600; up to 35: 2 s), **and its interval 28.75 ms**
  (`LE_CEN_INTERVAL` 23; up to 35: 30 ms): not a multiple of a phone's 7.5, 15 or 30 ms, so the two links' events slide past
  each other by 1.25 ms or more per event instead of overlapping for tens of seconds. The refusal follows it.
- **A phone's link asked for 15 to 30 ms and 6 s**, 3 s after it is made, when it is faster or less tolerant
  (`gap_request_connection_parameter_update`, within Apple's limits). `PQ n/rc` in the LE line: the requests and the library's
  answer; the phone's own update then shows as `U n intv/lat/timeout`.

### Test build 37: room in the LE transmit pool

Test build 36 crashed 87 s after power-on, with the Chocolate linked, the music playing and MIDIWeb's editor reading the
pedal: `CRASH PC 0200109E` (`P33_SYSTEM_RESET`, the library's fatal stop: an endless loop here) `RETS 0204D178` (inside
`le_hw_send_packet`) `VEC 63`, `DBG 0`. `le_hw_send_packet` stops there when its transmit buffer cannot be had: `hw_alloc_tx`
takes size + 24 bytes, as an lbuf, from the LE baseband's transmit DMA pool (`le_hw_init`: `config_btctler_le_acl_total_nums`
x 294 + 35 bytes, 1,505 with 5), which also holds the advertiser's 2 x 263 (since test build 35 advertising runs beside the
footswitch's link) and the initiator's. Test build 36 had asked the phone for 15-30 ms, and the editor sends up to
`le_tx_inflight` packets at a time by the interval: 4 from 30 ms, 1 at the iPhone's 7.5 ms before.

- **`config_btctler_le_acl_total_nums` 8** (2,387 bytes; 882 more from the Bluetooth heap).
- **The editor 2 packets out at most** (`le_tx_inflight`): a little slower to read the pedal over Bluetooth, never a full pool.

### Test build 38: the library's timers on time

Test build 37 on the pedal: no crash with the editor used hard, the music clean, but the Chocolate's link still ended by
supervision timeout about every 1.5 minutes, even with no phone linked and a 6 s timeout, and it was heard again only seldom
(`NM5` in about 11 minutes). The LE controller's multi-link scheduler (`config_btctler_le_slave_multilink` 1:
multilink_schedule.c, from the LE event interrupt) arms usr and sys timeouts for its advertising and connection-update
windows; up to 37 they ran from `ble_poll`, a main-loop pass late (tens of ms with the DSP's load), and their adds were not
safe in an interrupt.

- **The Bluetooth session's ff8c60a**: the timers' adds and deletes save and put back the interrupt state; an interrupt never
  calls `ble_ms()`; the usr timeouts have their own table, run every millisecond by the 10 kHz tick (`BT_USR_IN_TICK` 1), and a
  due one waits a millisecond when the baseband's lock is taken (`TB`, 0 expected).
- **The HCI line's words**: `TS used/peak` and `TU used/peak` the two tables, `TF` adds refused, `TI` adds from an interrupt,
  `TR` usr timeouts run, `TB` the waits.

## Test build 40: the footswitch learned on the pedal, TAILS, INPUT, BT POWER, TREBLE BOOST, readable model names

Build 38 is the gig build (tag `bt-test-38`). Build 40 is build 38 with what a player asked for after using it (build 39 was
never released: its work is in 40):

- **Controls learned on the pedal.** The Chocolate talks to the pedal, so the editor's own learning (for controllers plugged
  into the computer) never heard it. Now, in the editor's MIDI section, click a control (a block's on/off, the next or the
  previous preset, a setting), then press the footswitch: the pedal reports what it received (`MIDI_IN`, editor protocol 5)
  and the editor writes it into the pedal's own table (`MAP`). The pedal keeps up to 8 with the presets and they work with no
  page open, in every preset. A switch is learned "press" (it sends 127, then 0: it acts on the press) or "each" (it
  alternates 127 and 0: every message acts), told apart while learning and changeable in the list. docs/MIDI.md, "Learned on
  the pedal"; `firmware/px_midi.h` `px_midi_map_in`, `firmware/px_store.h` (in each copy, with its own mark and CRC).
- **The page FX opens is SETTINGS**, with four more rows (`firmware/ui.h`), all also in the editor's MIDI section:
  - **SWITCH**: MOMENT (as before: CC 8 to 19 flip on value 0) or LATCH (they flip at every message, for the Chocolate's CC
    mode, which alternates 127 and 0);
  - **INPUT**: the guitar's level into the chain, -10 to +10 dB, as the official firmware's Input Gain (heard at once);
  - **TAILS**: OFF (as before) or ON: a DELAY or REVERB that goes off, by a switch or because the next preset has it off,
    stops taking the guitar but rings out with the type and settings it had, until it is quiet (15 s at most, then faded out
    over 0.25 s); a full preset change still dips the sound for its ~12 ms fade, the tail goes on after it
    (`engine/px_dsp.c` `px_tl`, `tests/engine/tails.c`);
  - **BT POWER**: HIGH (the SDK's levels, as every earlier build), MID or LOW, the radio's highest transmit levels
    (`bt/bt_a2dp.c` `BT_PWR`), for a pedal whose guitar sound ticks while the radio works.
- **CC 118 / 119**: the next / the previous preset, for a footswitch in its CC mode, which sends no Program Change.
- **NAM capture names**: a capture can be renamed on the pedal itself from the editor (`NAM_RENAME`, editor protocol 5), from
  any browser; and a capture taken from TONE3000 gets the tone's first words before a short model name ("VOX AC30 VX" instead
  of "VX"; `docs/pxlib.js` `smartName`).
- **The preset level beside the preset's name** in the editor (the GT page's PRESET LVL on the pedal, -15 to +15 dB).
- **TREBLE BOOST**, a fifth DRV model: a germanium treble booster of the Rangemaster kind, to put in front of the amp (a NAM
  capture of a Vox or a Marshall): BOOST, RANGE (FULL, MID, TREBLE: the three-way switch of the later clones), LEVEL. At
  noon it is as loud as the guitar without it; BOOST pushes the amp harder (-12 to +9 dB around noon) and makes it clip
  more. Made by hand, not fitted to a capture ([OWN-FX.md](OWN-FX.md), `tests/engine/treble.c`). The editor reads the new
  model from the pedal, which reports `v42 FX2`, so an editor that met build 38 asks for the list again.
- **The release image is v42's again, byte for byte**: test build 28's fallback for the factory models left out
  (`engine/px_dsp.c` `pxn_fact`) is compiled only when some are left out (`PXN_KEEP`).

### What to check

1. The level: in the editor (Sound), move **Level** under the preset's name; the GT block's PRESET LVL follows, and the sound
   gets louder or softer. Save, then load another preset and come back.
2. INPUT: hold FX (SETTINGS), PUSH to the INPUT row, turn the knob while playing: the guitar gets louder into the amp at
   once. HOME saves it; power off and on: it is kept.
3. TAILS: set it ON (SETTINGS, or the editor's MIDI section). Play with a delay and a reverb on, switch the delay off: its
   echoes ring out. Change to a preset with the delay off: the echoes go on after the short dip of the change.
4. BT POWER: if the guitar ticks with Bluetooth on, try MID, then LOW, with the Chocolate and the phone at their usual
   distance: the ticks should get rarer, and the links must hold. When it ticks, the diagnostics page (hold HOME 1.4 s) says
   whether the audio ran late (`LATE` rising: the timing) or not (the radio itself, heard through the input).
5. The Chocolate in its CC mode (CubeSuite), linked to the pedal: open the pedal's SETTINGS page (hold FX), press each switch
   and read `LAST: CC n = v`: that is the number the switch sends.
6. Learn: in the editor's MIDI section, click EQ's **On / off**, press the Chocolate's switch once and let go. The editor says
   what it learned and lists it under **Learned by the pedal**; the pedal's SETTINGS page counts it (`LEARNED 1`). Close the
   editor: the switch still switches the EQ, in every preset. Power the pedal off and on: it still does.
7. A switch for the next preset: click **Next preset**, press another switch.
8. A capture's name: in the editor's Models section, **Rename…** on a capture the browser did not load: the pedal shows the
   new name at once.
9. TREBLE BOOST: DRV on, model TREBLE BOOST, in front of a NAM capture on the edge of breakup. RANGE TREBLE, BOOST from 0 to
   100: the amp goes from clean to driven, the low strings stay tight. RANGE MID and FULL: fuller, then a plain boost.
   Switching DRV on at noon keeps the loudness.
10. The log, with the pedal on USB, for the record (from the repository's folder):

```bash
make log
```

## Test build 41: the Chocolate by cable (USB MIDI host)

Build 40 with one experiment: the pedal's USB-C port can be the **USB MIDI host** of the M-VAVE Chocolate, so the footswitch
works by cable instead of Bluetooth. Its Program Change and Control Change go the same way as a computer's USB MIDI and the
Chocolate's Bluetooth MIDI (`firmware/px_midi_io.h`): presets, blocks, CC 116 (tap), 117 (expression), 118 / 119 (next /
previous) and the controls learned on the pedal (build 40). The host is felucca-bsp's (`firmware/bsp/chip/usb_host.c`,
opt-in, never used by OpenPocket before); **its authors have not validated it on real hardware, and neither have we**: this
build is the first time it runs on a pedal.

### What to connect: the 5 V

The pedal does not put 5 V on its USB-C port (`firmware/bsp/chip/fm1_usb.h`: "Nothing here drives VBUS: the port needs power
from outside"; the port is built to be powered, as a device's). The Chocolate, like any USB device, shows up only when it sees
5 V on its cable, battery charged or not. So the 5 V must come from something in between:

- **A USB-C OTG adapter with a charging input** (recommended): a USB-C plug, a USB-A socket and a USB-C socket for a charger
  (sold as "USB-C OTG adapter with PD charging"). The plug goes into the pedal, the Chocolate's own cable (USB-A to USB-C) into
  the USB-A socket, a USB charger or a power bank (5 V) into the charging socket. The charger powers the Chocolate and charges
  the pedal (its battery icon reads `USB`).
- **Or a USB-C hub with a charging input** (the kind sold for laptops: USB-C plug, USB-A ports, a USB-C PD input), the charger
  in it, the Chocolate in a USB-A port. The host looks behind one hub. A hub that waits for 5 V from its host (the pedal gives
  none) stays dark: then use the adapter.

A plain USB-C to USB-C cable between the pedal and the Chocolate does nothing (no 5 V, and both are devices); a plain OTG
adapter with no charging input neither. If the charger adds hum to the guitar sound, try another charger or a power bank.

### On the pedal: SETTINGS, USB PORT

The port is the normal USB device at every power-on: the web installer, the editor over USB and the rescue paths find the
pedal as before. It becomes the Chocolate's host only by a choice on the pedal: hold FX (SETTINGS), PUSH to the last row,
**USB PORT**, turn the knob: **FOOTSW**. It is applied at once, and **not saved**: every power-on is COMPUTER again.

- **Why not saved.** The host code has never run on this pedal. Saved, a fault in it (a hang, a crash) would come back at every
  power-on, and with the port a host the installer could not reach the pedal to fix it. Not saved, switching off brings back
  the normal USB device, with nothing to undo. Once the cable has worked through a rehearsal, a later build can keep the
  choice (bit 1 of the store header's USB settings byte, `firmware/px_store.h` pad[1], is free).
- **Back to COMPUTER:** the same row (the port is the USB device again within a second), or switch the pedal off and on.
  While the row says FOOTSW a computer cannot reach the pedal over USB; the web editor over Bluetooth still can.

The line under the rows says what the host sees, while the page is open:

| `FOOTSW: ...` | meaning |
|---|---|
| `STARTING` | the port is turning into a host (a moment) |
| `NOTHING ATTACHED` (amber) | no device on the line: nothing plugged, or the Chocolate has no 5 V (see above) |
| `CONNECTED` (green) | the Chocolate's MIDI interface found and read (interface 2, behind its HID interface 0) |
| `NO MIDI INTERFACE` (red) | a device answered but no USB MIDI interface was found in it, or it could not be read: unplug it, wait a second, plug it again |
| `LOW SPEED DEVICE` (red) | a keyboard, a mouse: not supported |
| `LOST` (red) | the footswitch was connected and is gone (unplugged, or it stopped answering); the host keeps looking |

On PLAY (and a block page), the footer says it too: `USB FOOTSW CONNECTED` (green, 3 s), `USB: NO MIDI INTERFACE` and
`USB: LOW SPEED DEVICE` (red, 3 s), and **`USB FOOTSW LOST` (red, it stays** until the footswitch is back or the row is set
to COMPUTER: a footswitch gone in the middle of a song is not a note to miss).

- **Bluetooth is not read twice.** The pedal is also the Chocolate's Bluetooth central (build 28). While the footswitch on the
  cable is CONNECTED, Bluetooth's notes and controls are not read, or each press would act twice (a block flipped and flipped
  back, two presets for one); the editor over Bluetooth is not affected. When the cable is lost, Bluetooth is read again: the
  spare.
- **Nothing is sent to the footswitch** (the pedal's own changes, which SEND gives a computer, are not sent to it).

### How it works

- `firmware/px_usb.h` builds felucca-bsp's host in (`FELUCCA_USB_HOST`, this image only) and runs it from the main loop
  (`px_uh_service`): `usb_host_switch` from the row, `usb_host_task` each pass, its messages (`usb_host_message`) to the
  screen's state (`firmware/px_usbhost.h`). The host enumerates with blocking control transfers on the main loop (about 150 ms
  when a footswitch is plugged in), finds the first MIDI streaming interface even behind HID, and the 2 kHz USB interrupt reads
  its IN endpoint into the ring USB MIDI fills: `px_midi_service` reads it as a computer's.
- **Its clock.** The host times its waits and timeouts with usb.c's `fm1_ms`, which OpenPocket never advanced, and the main
  loop's `ms_now()` wraps every 178.9 s (TIMER4 at 24 MHz): given that, the host would stop looking for a device at the first
  wrap. The USB interrupt now adds TIMER4's ticks to `fm1_ms` at each poll (`pxu_ms_add`). A side effect, in this image only:
  a computer's USB MIDI now carries its arrival time too. It was always 0, so CLOCK > DELAY and CC 116 taps over USB did
  nothing (they now work; the release image still has that, to fix separately).
- **The music goes on.** The host's waits on the main loop serve the radio as the "SAVED" wait does (`UH_IDLE`, test build 26):
  the music may skip a little at the moment the footswitch is found, not more.
- Not supported: low-speed devices, a hub behind a hub, sending to the footswitch.

### What to check

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**;
the start-up screen reads `v42 BT AUDIO TEST 41`:

```bash
make serve-bt
```

1. With the computer's cable in: SETTINGS (hold FX), PUSH to the last row: `USB PORT` reads `COMPUTER`, and the line under the
   rows `USB: HOST CONNECTED`. HOME.
2. Unplug the computer. Connect the pedal to the adapter, the charger to the adapter (plugged in, on), the Chocolate's cable
   to the adapter's USB-A socket and to the Chocolate, the Chocolate on. The pedal's battery icon reads `USB`.
3. SETTINGS, PUSH to `USB PORT`, turn the knob: `FOOTSW`. The line reads `FOOTSW: NOTHING ATTACHED`, then within a second
   `FOOTSW: CONNECTED`. If it stays at NOTHING ATTACHED, the Chocolate has no 5 V (check the charger, try the hub or another
   adapter); NO MIDI INTERFACE: unplug the Chocolate's cable, wait a second, plug it in again, and report it.
4. Still on SETTINGS, press each Chocolate switch: `LAST: PC n` or `LAST: CC n = v` changes at each press (this page reads
   them and does not act on them). If the line says CONNECTED but `LAST` does not change, the Chocolate may send its MIDI only
   over Bluetooth while it is linked that way too, or be in a keyboard (HID) mode: switch the pedal off, then on with REV held
   (this boot without Bluetooth), choose FOOTSW again and press: report what `LAST` does.
5. HOME. On PLAY the footer says `USB FOOTSW CONNECTED` for 3 s. Play: the switches change presets, switch blocks and do what
   they were learned for, as over Bluetooth. Tap tempo (CC 116) sets the delay's TIME.
6. With the Chocolate also linked by Bluetooth (as usual): each press acts once (a block's switch flips it once; CC 118 steps
   one preset).
7. Pull the Chocolate's cable while playing: the footer turns red, `USB FOOTSW LOST`, and stays. If the Chocolate is linked by
   Bluetooth, its switches work again over Bluetooth. Plug the cable back: `USB FOOTSW CONNECTED`, the cable again.
8. With the phone's music playing: plug and unplug the Chocolate a few times. The music may skip briefly when it is found; the
   pedal must not restart, and the Bluetooth links must hold.
9. Back: SETTINGS, `USB PORT`, `COMPUTER`. Unplug the adapter, plug the computer: the installer and `make log` find the
   pedal. Then switch the pedal off and on with FOOTSW chosen: it starts as COMPUTER.
10. The log, for the record (the pedal on COMPUTER and on the computer's cable):

```bash
make log
```

If the pedal restarts or hangs in FOOTSW: switch it off and on (it is COMPUTER again), and report what the screen said last.

## Test build 42: LOW OCTAVE's bass out of the chain, and the stereo output

Build 41 with two changes asked by a player. First, **a bass never goes through the guitar's reverb or delay**. LOW OCTAVE's octave
now leaves the chain at the PIT block and joins at the end, after the last block, so no amp, cabinet, EQ, MOD, DELAY or REVERB
touches it; it still follows the preset's LEVEL, MASTER, the tuner, the volume pedal and PIT's on/off fade. Its 41 Hz now comes
out whole (the cabinet cut it before): the same OCT -1 sounds bigger on a full-range system, so start lower and raise it.
With the AMP on it gets +12 dB, about what the amps add to the guitar ([OWN-FX.md](OWN-FX.md), "LOW OCTAVE").

Second, **the stereo output**: the pedal's output is stereo (M-VAVE's firmware uses it), and every OpenPocket image so far
sent the same sound to both channels. A new SETTINGS row (hold FX), **OUTPUT**, before USB PORT, also in the editor's MIDI
section: **MONO** (the default, as before), **STEREO** (the delay's echoes and the reverb's tail, TAILS included, and seven
modulations, CE CHORUS, DIMENSION, FLANGER, PHASE 90, SMALL STONE, UNI-VIBE and ROTARY, spread to the sides, the dry guitar and
the bass in the middle; the two channels summed are exactly the MONO sound; and the phone's music in true stereo: its side, at
half the rate, beside the mono mix) or **GTR+BASS** (the
guitar on the left channel, LOW OCTAVE's octave alone on the right, for two channels of a desk). Applied at once, saved on
leaving the page (bits 2-3 of the store header's USB settings byte). With a mono cable in the OUT jack, keep MONO: a mono
plug takes the left channel alone. `engine/px_dsp.c` `px_lo_*`, `px_sd_*`, `px_engine_out`; `firmware/px_audio.h` (a_outr,
a_resr: the right channel through the same mutes, fades, volume and knee as the left); `tests/engine/bassout.c`. The web
editor's reply buffer went back to the main RAM (18 KB free there) to make room for this code in the RAM code window.

### What to check

1. A preset with LOW OCTAVE, an amp, and a long reverb and a delay: play low notes, then stop. The guitar's echoes and tail
   ring out, the bass's do not: it stops with the note. Low notes sound deeper than with build 41.
2. OCT -1: set the bass against the guitar by ear (start around `-6 DB`).
3. The volume pedal and the preset's LEVEL move the bass with the guitar; PIT switched off fades it out.
4. OUTPUT STEREO, headphones or a stereo cable: a preset with a delay and a reverb sounds wide, the dry guitar in the
   middle; a CE CHORUS, a DIMENSION, a PHASE 90 or the ROTARY move between the two sides. Switch the delay off with TAILS on:
   its echoes ring out wide. Turn the stereo off and on while playing: no click. Play music from the phone: a song with
   instruments panned left and right sounds wide, as on the phone's own headphones.
5. OUTPUT GTR+BASS, the OUT jack to two channels of a desk (a stereo cable, or a TRS-to-two-jacks Y cable): the guitar on the
   left, the bass alone on the right. A preset without LOW OCTAVE: the right channel is silent.
6. OUTPUT MONO again: both channels the same. Power off and on: the setting is kept.

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 42`:

```bash
make serve-bt
```

## Test build 43: the pedal as a USB sound card, the computer's sound back into the chain

Build 42 with USB audio, asked by a player who plays guitar-to-MIDI on a computer: the pedal sends the clean guitar to the
computer, a program such as MIDI Guitar turns it into a flute or a synth, and that sound comes back into the pedal before its
delay and reverb, with the guitar itself cut.

USB audio is the release's (v42, [USB-AUDIO.md](USB-AUDIO.md)): a class-compliant sound card, 2 channels, 16 bit, 44.1 kHz both
ways, no driver to install. This image left it out for room; it now fits with both of its rings in the engine's pool (the
playback ring 512 frames, half the release's). SETTINGS (hold FX) has four more rows after USB PORT; the last three are also
in the editor's MIDI section:

| Row | Values | What it does |
| --- | --- | --- |
| USB AUDIO | OFF, ON | the pedal is a USB sound card too. Applied when you leave the page: the pedal leaves USB for a second (as v42) |
| USB SEND | OUTPUT, DI, DI + OUT | what the computer records: the pedal's sound (as v42; both channels of STEREO and GTR+BASS); the guitar as it enters the chain, before any block, on both channels (INPUT applied); or the DI on the left and the pedal's sound on the right |
| USB RETURN | OUTPUT, INPUT, BEFORE GT ... BEFORE SYN | where the computer's sound comes in: at the output, in stereo, after MASTER (as v42); at the chain's start, through every block; or right before a block, wherever the preset puts it, so that the blocks after it play the computer's sound and the ones before it never touch it |
| GUITAR | MIX, CUT | at USB RETURN's place: the guitar and the computer's sound together, or the guitar cut, so that only the computer's sound goes on from there |

- The knob moves USB RETURN along the current preset's chain: INPUT, before each block after the first, OUTPUT. The setting
  names a block, not a position: in a preset with another order the return follows that block. Before a block that is off,
  the computer's sound comes in where the next block that is on is.
- In the chain the computer's sound is mono (the middle of its two channels) at the input's scale: what USB SEND DI recorded
  comes back at the level it left. Reamping works that way: record a DI take, then play it back with USB RETURN INPUT and
  GUITAR CUT and record USB SEND OUTPUT. At OUTPUT it stays stereo, as v42.
- GUITAR CUT acts only while the computer plays into the pedal (its output stream open): when the program stops or the cable
  is pulled, the guitar comes back. It fades out and in over 5.8 ms. LOW OCTAVE's octave and STEREO's side made before the
  return go with the guitar.
- With USB RETURN in the chain and USB SEND on OUTPUT or DI + OUT, a program that passes its input to its output (input
  monitoring on that track) feeds the pedal's sound back into the pedal: keep USB SEND on DI, or that monitoring off.
- The delay through the computer, estimated from the rings' figures: the DI reaches the computer about 1.5 to 8.5 ms after
  the string, and the pedal plays the computer's sound about 5 to 8 ms after it arrives (3 ms more with a NAM amp on two
  cores and the return after the amp). Add the program's own buffers and the guitar-to-MIDI tracking, which waits for a
  period or two of the note. Fine for pads, strings and flutes, late for staccato.
- What it costs: the two rings take the pool's last 4 KB (852 bytes left), USB audio's code in the interrupt adds 0.9 KB to
  the RAM code window (1,752 bytes left), the image is 6 KB larger (622,312 bytes, 22.6 KB of the slot left). With no program using the device nothing runs. The CPU it
  takes beside the radio and a NAM amp is the open question: only the pedal can tell.

`firmware/px_uaudio.h` (`px_ua_chain`, `px_ua_take`: the computer's sound read in `a_block`, on CPU0 like the music; the
record ring per USB SEND), `engine/px_dsp.c` `px_engine_ret` / `px_rt_mix` (the return's place in the chain, the guitar's
fade), `firmware/px_midi.h` `px_ua_route` (one byte, kept in the store's map record), `firmware/ui.h` (the four rows; the page
scrolls, 8 rows at a time).

### What to check

1. SETTINGS, USB AUDIO ON, then leave the page: the computer shows a sound card named OpenPocket, with an input and an output.
   The phone's music, the Chocolate and the editor over Bluetooth go on working.
2. The load: note the DSP figure (PING, or the tuner page's footer) with a NAM preset, first with no program using the
   device, then while a program records and plays through it. Any crackle, an `XRUN`, or the safety bypass: report it with
   the log.
3. USB SEND DI, record a track: the clean guitar, no effect on it, on both channels. DI + OUT: the DI left, the pedal's
   sound right.
4. The flute: MIDI Guitar (or another) on the OpenPocket input, its synth to the OpenPocket output; USB RETURN BEFORE DLY,
   GUITAR CUT: only the flute, through the preset's delay and reverb. Turn USB RETURN: BEFORE AMP puts the flute through the
   amp; INPUT through everything; OUTPUT gives the flute dry, in stereo. GUITAR MIX: both.
5. Stop the program: the guitar comes back. Start it again: it is cut again.
6. A long session with the phone's music on as well: a click, a dropout or a lost Bluetooth link, report it.
7. Power off and on: USB AUDIO, USB SEND, USB RETURN and GUITAR are kept.

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 43`:

```bash
make serve-bt
```

## Test build 44: SHIMMER in tune, BARITONE sooner, MONO SYNTH's tracker, SITAR and STRINGS rebuilt, TAPE FLUTE

Build 43 with the models a player found unusable put right, and one more ([OWN-FX.md](OWN-FX.md), "Test build 44"):

- **SHIMMER**: the octave in tune (it was off by up to half a semitone, by the note: E +41 cents, G -51).
- **BARITONE**: about 9 ms after the pick instead of 23 on average.
- **MONO SYNTH**: a new tracker (YIN): in tune about 16 ms after the pick on every note from 82 Hz to 1 kHz (it lost every
  note above about 600 Hz), and no more wrong note at the start of a phrase.
- **SITAR**: rebuilt as an Indian sitar: the jawari's buzz that narrows as the note dies, and six sympathetic strings an octave
  above KEY on its pentatonic. Set KEY to the song's key.
- **STRINGS**: rebuilt as an orchestral section: the bow swelling in and holding the note, four players with their own vibrato,
  the violins' body; OCTAVE adds the cellos, then the violins an octave up.
- **TAPE FLUTE** (new, the last SYN model): a Mellotron-like flute on MONO SYNTH's tracker, one note at a time: BREATH, the
  tape's WOW, TONE, ATTACK, DRY.

The web editor reads the catalogue again (its version text is now `v42 FX3`). Room: the image is 630,132 bytes (14.8 KB of the
slot left), the RAM code window has 1,160 bytes free (STRINGS' and TAPE FLUTE's samples, MONO's tracker and the shifters'
search run from the flash: inline, the new code was 1,080 bytes over), the RAM 2,944, the pool 852.

### What to check

1. SHIMMER on a held chord and on single notes up the neck: the octave in tune with the note.
2. BARITONE at MIX 100: play fast; it follows the pick.
3. MONO SYNTH: single notes all over the neck, slides and bends: the right note from the first moment.
4. SITAR with KEY on the song's key: the buzz on each note, the sympathetic strings ringing after it. STRINGS with a HALL: chords
   swelling in, held; OCTAVE at noon for the cellos. TAPE FLUTE on a melody.
5. Anything that sounds wrong, say which model and what you played.

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 44`:

```bash
make serve-bt
```

## Test build 45: MONO SYNTH through hum, TAPE FLUTE on the note, WHAMMY one way, a WAH on the pedal

Build 44 with a player's second round ([OWN-FX.md](OWN-FX.md), "Test build 45"):

- **MONO SYNTH**: it tracked the clean loops, not a guitar. Hum lifted every dip of the tracker's difference function over its
  fixed threshold, so it heard no note and the last one went on; and with hum or noise above its gate's level the gate never
  shut, so the synth droned the last note at 60 % through every pause. Now the period is the first dip near the deepest, a
  note that stops being heard lets go after 93 ms, and the gate opens at a third of the level it took (a single coil with its
  volume down opened it for 4 notes in 10). On the lead loop with a hum 15 dB under the playing: 557 notes right of 598 (497),
  2 blips (19), no drone in its pauses (all of them). The riff's high C6s are C6s again: build 44 held them an octave down
  for 46 ms.
- **TAPE FLUTE**: the nearest semitone (A 440), played straight: no more gliding into the note. It holds while the guitar stays
  within 60 cents of it, and moves on a new note heard clearly three times in a row (13 ms).
- **WHAMMY**: SHIFT is four intervals, `-2 OCT`, `-1 OCT`, `+1 OCT`, `+2 OCT`, and the expression pedal (CC 117) goes from the
  note at the heel to the interval at the toe (it was -24 to +24 semitones in steps of 0.48, the screen rounding: its "0"
  could be 48 cents sharp). At the heel the note itself (the shifter's copy, up to 32 ms late, made a comb with MIX under
  100). The pedal's ends are 4 values wide, so a pedal that never sends 0 or 127 still reaches them. A new knob, **PEDAL**, is
  the position without an expression pedal (0 the note, 100 the interval), and the one to learn a pedal onto in the editor.
  A saved SHIFT keeps its meaning: +12 is `+1 OCT`, +24 `+2 OCT`.
- **WAH** (new, the last SFX model): a wah pedal on the expression pedal: the heel 372 Hz, the toe 2.33 kHz, on a wah's
  taper; **PEDAL** (its position without a pedal), **Q** (2 to 10), **RANGE** (the heel, 280 to 490 Hz), **LEVEL** (+-12 dB).
  At noon as loud as the amp alone.

The web editor reads the catalogue again (its version text is now `v42 FX4`). MONO's and TAPE FLUTE's note choice runs from
the flash; the room left is test build 46's, below.

### What to check

1. MONO SYNTH: single notes all over the neck, with the guitar's volume down too; let a note ring out: the synth stops with it.
2. TAPE FLUTE on a melody: each note on its pitch from the start.
3. WHAMMY with the expression pedal: heel the dry note, toe exactly the interval; try the four SHIFTs.
4. WAH with the expression pedal: heel to toe; Q and RANGE to taste.
5. The organs: if ORGAN or CHURCH crackles with other blocks on, run this with the pedal on the computer's USB (it plays every
   saved preset for about 8 seconds, turn the volume down, then goes back to the preset it had), and say which presets
   crackle:

   ```bash
   node tools/op.mjs dsp
   ```

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 45`:

```bash
make serve-bt
```

## Test build 46: controls learned live on the pedal, each a preset's own

Build 45 with a player's request: teach a controller's switch or pedal to the pedal without the editor, and keep it per preset
([MIDI.md](MIDI.md), "Learned live, one preset's own"):

- On a block page, select **ON** or a setting, **hold FX**, then press the controller's switch or move its pedal. The footer
  says `LEARN: SWITCH OR PEDAL`, then `LEARNED: CC 30`; it is written at once.
- On ON a switch flips the block (a momentary switch or one that sends a message a press, told apart by itself), a pedal
  switches it; on a setting the pedal moves the value.
- It works only in the preset where it was learned, and goes along when the sound is saved into another slot.
- A learned setting shows its number after its name (`GAIN CC4`); hold FX on it again to forget it. Hold FX on MODEL: the
  preset's MIDI table, every control it has (KNOB picks one, hold FX forgets it, HOME back), the editor's in grey.
- Hold FX on PLAY still opens SETTINGS (from a block page it is now the learn: HOME first).
- **IN-Z** and **PICKUP** (SETTINGS, after INPUT), for every preset: the guitar's pickup as a lower input impedance would load
  it, as Helix's input impedance does. The pedal's own input stays what it is (about 1 MOhm); a filter does what the load
  would: IN-Z 1M (nothing done), 230K, 136K, 90K, 70K (the pickup's resonant peak down 6 to 14 dB), 32K, 22K, 10K (the top
  going too); PICKUP, the pickup the filter assumes (SINGLE, HUMBUCK, P90: their resonances 3.9, 2.7 and 2.4 kHz). The lows keep
  their level: it changes the tone, not the volume. Within 0.4 dB of the loaded pickup's response up to 5 kHz.

Room: the image is 638,272 bytes (6.5 KB of the slot left), the RAM code window has 504 bytes free (IN-Z's filter is a few
instructions in the audio interrupt; the learn and its table run from the flash with the rest of the screen and MIDI code), the
RAM 2,336, the pool 852.

### What to check

1. On a preset, learn a Chocolate switch on the DELAY's ON, and the expression pedal on the DELAY's MIX (or the AMP's GAIN).
   Play: the switch flips the delay, the pedal moves the value, in that preset only.
2. In another preset the same switch and pedal do what they did before (the expression pedal: volume, WHAMMY or WAH).
3. Switch the pedal off and on: the learned controls are still there. Save the sound into another slot: they go with it.
4. Hold FX on MODEL: the table lists them; forget one there, and learn another one on a row.
5. SETTINGS, IN-Z: from 1M down to 10K with your guitar, PICKUP on what it has.

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 46`:

```bash
make serve-bt
```

## Test build 47: a learned pedal's heel and toe, PLAY VIEW, GUITAR TONE by its sound, 12-STRING steady

Build 46 with a player's round on it ([MIDI.md](MIDI.md), "Learned live, one preset's own" and "PLAY VIEW"):

- **IN-Z and PICKUP showed nothing as the knob turned** (the sound changed; the page drew the value only when another row was
  selected). Fixed: the value is drawn at once.
- **GUITAR TONE** (was IN-Z): named by what it does to the sound, from the brightest: **BRIGHT** (new: as a buffer right at the
  guitar, the cable's capacitance mostly gone, the pickup's peak higher and stronger), **NATURAL** (the input as it is, as IN-Z
  1M), **SMOOTH**, **WARM**, **WARMER** (the pickup's resonant peak tamed: 230K, 136K, 90K), **PUNCHY** (the peak gone, the mids
  forward: 70K), **DARK**, **DARKER** (the top going: 32K, 22K), **MUDDY** (10K). A saved IN-Z keeps its sound under its new
  name.
- **SETTINGS with the footswitches**: the right one goes down a row, the left one up (round), as well as PUSH.
- **A learned pedal's heel and toe**: after a pedal (or a switch) is learned on a setting, the footer asks
  `HEEL: KNOB, PUSH`, then `TOE: KNOB, PUSH`: turn the knob to the value you want there (you hear it), PUSH. The toe starts from
  the preset's own value, so PUSH alone keeps it. The preset keeps its own value until the pedal moves (46 jumped the setting
  to wherever the pedal was, and its range was the whole knob: a preset volume learned there went from -15 to +15 dB). Any
  other button instead of PUSH: the whole range, as 46. A switch on a setting asks `OFF` and `ON`. The preset's MIDI table
  (hold FX on MODEL) shows the selected one's range in the setting's units (`RANGE -1 TO +2`).
- **PLAY VIEW** (SETTINGS, after SWITCH): **SWITCHES** shows on PLAY, in place of the chain, the blocks the preset's controller
  switches turn on and off, big: the block, the controller's number, the model, in the block's colour when on, grey when
  off; the preset's own learned switches, then the editor's for every preset, in the controllers' order, 6 at most. A preset
  with none shows the chain. Turning the knob shows the chain for 5 s. **CHAIN** (the default): PLAY as always.
- **12-STRING steady** ([OWN-FX.md](OWN-FX.md), "Test build 47"): DETUNE is 0 to 3 cents (was 15) and the swept unison copy is
  gone: at noon on held notes the level moves a quarter as fast as in 46; at 0 nothing moves but the guitar. The web editor
  reads the catalogue again (`v42 FX5`).

Room: the image is 640,196 bytes (4.6 KB of the slot left), the RAM code window has 744 bytes free (240 more: 12-STRING's
unison copy is gone from the audio code; the heel and toe, PLAY VIEW and GUITAR TONE's names run from the flash with the rest
of the screen and MIDI code), the RAM 2,048, the pool 852. The release image is v42's, byte for byte.

### What to check

1. SETTINGS (hold FX on PLAY): GUITAR TONE from BRIGHT to MUDDY with your guitar, the name changing as you turn; PICKUP. Walk
   the rows with the footswitches.
2. On a preset, learn the expression pedal on a setting (the GT page's PRESET LVL, or the DELAY's MIX): set the heel and the
   toe with the knob and PUSH. Move the pedal: the setting goes from the one to the other. Change preset and come back: the
   preset starts at its own value until the pedal moves.
3. Learn two or three Chocolate switches on blocks' ON rows, then SETTINGS, PLAY VIEW: SWITCHES. On PLAY, press them: the
   tiles light up and go grey.
4. 12-STRING on its own, DETUNE at noon and at 0, against build 46 (renders: `renders47.zip` beside this build's draft
   release).

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 47`:

```bash
make serve-bt
```

## Test build 48: the guided learn, PRESET LVL down to silence

Build 47 with a player's report on it: learning the expression pedal on the GT page's PRESET LVL, the heel set to -15 and
confirmed, the pedal then went from -15 to +15. On the simulated pedal the same steps with PUSH (pressing the knob) at the
heel and the toe kept the range; the confirm had been another button, and in 47 any control but PUSH ended the heel and toe
with the whole range, saying `LEARNED` all the same.

- **The guided learn** ([MIDI.md](MIDI.md), "Learned live, one preset's own"): hold FX on ON or a setting opens a page of its
  own, a step at a time, the one to do lit, what to do in big letters, the controls in the footer:
  1. **PEDAL OR SWITCH**: the knob chooses, PUSH;
  2. **MOVE THE PEDAL** / **PRESS THE SWITCH**: it waits as long as it takes;
  3. **PEDAL AT THE HEEL** (a switch: **SWITCH OFF**): the pedal at its heel, the knob to the value there (heard), PUSH;
  4. **PEDAL AT THE TOE** (**SWITCH ON**): the pedal at its toe, the knob (from the preset's own value), PUSH: learned.

  HOME at any step: cancelled, nothing changed. Nothing else does anything on the page. Where the pedal is at the heel and at
  the toe is taken too: a pedal that never sends 0 or 127 still gives both values exactly.
- **PRESET LVL down to `-INF`** ([OWN-FX.md](OWN-FX.md), "Test build 48"): 0 is silence, 1 to 19 fall 2 dB a step from -9 dB
  (a pedal fades out smoothly), 20 and up as before. It is the preset's level at the chain's end: at `-INF` the delay's and
  the reverb's tails go too (CC 117's own volume, before the delay, lets them ring).
- The web editor reads the catalogue again (`v42 FX6`).

Room: the image is 642,308 bytes (2.6 KB of the slot left: the flash is now the tight one), the RAM code window has 744 bytes
free (the page runs from the flash with the rest of the screen), the RAM 1,760 (the entries' two more bytes and the record's
buffer: 256), the pool 852. The release image is v42's, byte for byte.

### What to check

1. On a preset, the GT page, PRESET LVL, hold FX. PEDAL, PUSH; move the expression pedal; with the pedal at its heel turn the
   knob down to `-INF`, PUSH; with the pedal at its toe, PUSH (the preset's level) or turn up to `+4.2 DB`, PUSH. Move the
   pedal: from silence to the level. Change preset and come back: the preset starts at its own level until the pedal moves.
2. Learn a Chocolate switch on a block's ON row: SWITCH, PUSH, press it.
3. HOME in the middle of a learn: nothing learned, the setting as it was.

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 48`:

```bash
make serve-bt
```

## Test build 49: scenes, PLAY's three views on CAB held

Build 48 with Helix-style scenes, for the blocks' on and off ([MIDI.md](MIDI.md), "Scenes"):

- **Scenes**: four a preset, each a set of blocks on and off. Recalled by CC 65 to 68 (any value), by a Chocolate switch
  learned on one in the preset, by the footswitches in SCENES' view, or there by the knob and PUSH. The scene playing keeps
  what is switched while it plays; SAVE writes the preset with its scenes, and the preset opens in the scene it was saved in.
  A recall is no edit; `SCENE 2` shows on PLAY where `EDITED` goes.
- **Hold CAB** (a short press still opens CAB) turns PLAY's view round: CHAIN, SWITCHES (each controller switch in this preset:
  a block or a scene, lit when on or playing), SCENES (the four; the knob picks one, PUSH recalls it, hold FX learns or clears
  its switch, the footswitches step through them).
- SETTINGS: **SCENE+SWITCH** (SWITCH WINS: a block with its own switch is left to it; SCENE WINS: the scenes set it too) and
  **SCENE FOOTSW** (SCENES, or PRESETS: the footswitches stay on the presets in SCENES' view). PLAY VIEW has the three.
- The screen draws lowercase letters as capitals (`4X12 GREEN`): the fonts without them leave 11 KB of flash for this and what
  comes next. The release's screen is as it was.
- The web editor, editor protocol 6: its MIDI section has GUITAR TONE, PICKUP, PLAY VIEW, SCENE+SWITCH and SCENE FOOTSW, the
  preset's own learned controls with their heel and toe, a control learned for this preset only (the footswitch's too), and
  SCENE 1 to 4 among what a control can learn; its Sound view has the four scenes under the chain (a tap recalls one, a grid
  sets their blocks). Its own copy of the preset, for EDITED, is gone: the pedal's is used (128 bytes of RAM).

Room: the image is 634,816 bytes (10.1 KB of the slot left, from 2.6: the fonts gave 11 KB, the scenes took about 3, the
editor's protocol 6 0.7), the RAM code window has 744 bytes free, the RAM 1,280 (the presets' scenes: 520), the pool 852. The
release image is v42's, byte for byte.

### What to check

1. On a preset, hold CAB twice: SCENES. Turn the knob to SCENE 2, PUSH: switch some blocks on and off (their buttons, or the
   Chocolate's switches). SCENE 1 with the left footswitch, SCENE 2 with the right: each as you left it. Hold PUSH, SAVE.
2. Hold FX on SCENE 1's tile, press a Chocolate switch; the same on SCENE 2 with another switch. Learn a third switch on a
   block's ON row (DLY). Hold CAB to SWITCHES: the three tiles, in the switches' order. Press them.
3. SETTINGS, SCENE+SWITCH on SCENE WINS: the scenes now set DLY too.
4. Change preset and come back: it opens in the scene it was saved in.

Install it from this branch's folder (Chrome or Edge, the computer's USB cable in the pedal), then **Connect**, **Install**; the
start-up screen reads `v42 BT AUDIO TEST 49`:

```bash
make serve-bt
```

## Checked on a computer

- `tests/test_bt_ring.c`: a phone at 44.1 kHz with 15 ms of jitter, the pedal at its real 44,117.6 Hz, 60 s: no underrun, the rate
  settles at the clocks' ratio, a 1 kHz sine comes out 52 dB clean, the pitch wanders 0.7 cents; a 300 ms pause stops it once,
  without a click, and it starts again.
- `tests/test_bt_sbc.c`: Android's encoder makes the frames a phone sends (joint stereo, 328 kb/s), packed as A2DP packets
  (contributors, an extension, padding): every frame decodes, the output is the mono mix 64 dB above the difference; mono
  streams, also with frames of 8 and 4 blocks (each gives its own count of samples); a frame whose CRC fails is refused and the
  stream goes on; a header whose contributors or extension run past the packet leaves nothing to decode.
- `tests/test_ble_taskq.c`, built twice (the BLE image's scheduler and the Bluetooth audio image's): the task queues with the
  library's message sizes, the recursive mutexes, the clock across TIMER4's wrap (a periodic timer, a one-shot and a task asleep
  across it), and in the audio image's build the waits refused inside the baseband's interrupt, each task's queue peak, stack
  depth and red zone.
- `tests/test_bt_lbuf.c`: the radio pools' allocator through 100,000 random operations (no overlap, no corruption, no leak).
- `tests/test_bt_ps.c` (test build 14): the pairing store, `firmware/bt/bt_ps.h` itself, on a model of the NOR flash, with power
  cuts at random bytes of every save and inside every erase (40,000 rounds: about 7,500 cuts during a save, 960 during a
  power-on's erase, 18,000 power-ons): every power-on loads the records last saved, or those being saved when the power went,
  never anything else; nothing is written outside the two sectors or in their last 256 bytes; every program goes to erased
  bytes; no erase takes the newest good copy; no two copies share a sequence number. Sessions of 1 to 5 saves never erase
  during a save. A sector whose programs stop taking, power cuts with it, and a bit misread at power-on or in a save's
  read-back cost saves, never the copy kept. The Bluetooth image's merged `FL_RANGE_OK` gives every sector erase and page
  program the answer of one range per area. The model catches each of a dozen deliberate faults in the store (no read-back,
  no guard of the newest copy, no second reading before an erase, a save sooner than 1 s, one sequence number a round...).
- `tests/test_bt_sources.mjs`: the vendor objects are as published and listed, the licences are there.
- The web editor over Bluetooth (test build 27):
  - `tests/midisim.c`: the editor's SysEx kept across BLE MIDI writes as CoreMIDI and Android cut it (a timestamp before
    each status, a clock inside, a timestamp byte that reads as F7 or F0, a write that goes on after a timestamp of its own),
    and dropped when another status cuts it, when longer than 160 bytes, or when one comes while one waits (the waiting one
    left as it was); the Chocolate's reader gives the same messages from the same writes. The replies cut into notifications:
    3,000 replies of 2 to 1,100 bytes at packet sizes of 3 to 514, each packet within its size and with its header, back
    byte for byte through a reference reader (Android's BluetoothPacketDecoder's rule) and through `px_blem_sysex`, and never
    a message for the Chocolate's reader.
  - `tests/midiio.c` with the image's flags: a write's SysEx goes to the editor, its Program Change to the pedal as before;
    the Chocolate's Control Change gives the editor nothing.
  - `tests/test_editor.mjs` with `EDSIM_BLE=1` against `tests/edsim.c -DED_BT=1`: the whole editor test (the catalogue,
    editing, pushes, saves, slots, NAM models, IRs, a power cycle) over BLE MIDI writes and notifications, two a pass as the
    pedal sends them; a link lost (nothing more pushed) and a new one; a USB request meanwhile answered over USB; a push and a
    reply crossing (each waits for the other, nothing lost). About 660 frames, 3,900 writes, 5,000 notifications.
  - Two LE links: `tests/test_bt_le_links.c` (`firmware/bt/bt_le_links.h`: each event taken as often as the host delivers it,
    advertising asked while a link is free and never with both up, again after either ends or after a failed ask);
    `tests/midiio.c` (the editor's SysEx on one link with the Chocolate's Control and Program Change between its writes on the
    other: both whole); `tests/test_editor.mjs` with `EDSIM_BLE=1` against `tests/edsim.c -DED_BT_LINKS=2u` (the Chocolate's
    writes between the editor's, INFO from a second central answered on its own link only, one link lost while the editor on
    the other is still pushed).
  - Test build 28, the central role: `tests/test_bt_le_central.c` (the ATT client against a simulated footswitch's GATT
    server answering at the default MTU, in several layouts: the MIDI service last or first, descriptors before its CCC, a
    vendor characteristic before it, no MIDI service, no CCC, encryption asked, a stale cached handle; and the advertisement
    filter: the UUID and the name in separate reports, case, RSSI, not connectable, another BLE MIDI device, the known address,
    malformed data, the table's turnover); `tests/test_bt_le_links.c` (the typed links: advertising and scanning wanted
    as the entities allow, the DLY key's test build 27 behaviour). The member check of `ll_master.c` (nm against the image's
    members and firmware): no symbol left unresolved that build 27 resolved.
  - Each of about twenty deliberate faults (no timestamp before F7, a clock ending the SysEx, a waiting frame overwritten, a
    reply sent over USB or to the other link, a push or a frame not waiting for the buffer, one SysEx reader for both links,
    a link's end stopping the other's editor, an event counted twice, advertising with both links up...) fails at least one
    of these tests.
  - The release image and the BLE image compile to the same code as before (their LLVM IR, compared); the Bluetooth audio
    image calls three more library functions, all in members it already links (`att_server_notify`,
    `hci_le_get_acl_packet_num`, `stack_run_loop_resume`).
- `tests/test_log.mjs`: the FIFO line and `W` named, and the nine lines classified and reported, as the log tool prints them.
- Test build 41, the Chocolate by cable: `tests/test_usb_host.c` (felucca-bsp's descriptor parser on the Chocolate's layout as
  macOS reads it, configuration 1, HID at interface 0 with its interrupt endpoint, Audio Control at 1, MIDI streaming at 2,
  with bulk and interrupt endpoints and two numberings, a cut read, no MIDI interface, unusable sizes; the packets passed on,
  channel messages and clock, not SysEx; the screen's state from the host's messages, an unusable device unplugged, PLAY's
  notes, LOST kept; the host's clock across TIMER4's wrap and after interrupts held off); `tests/uisim.c` with `PX_OWN_FX`
  (the USB PORT row last, to the port at once and back, never saved, every line's text whole and in its colour, the notes on
  PLAY and on a block page beside the load figure, an overload's instruction first); `tests/midiio.c` with the image's flags
  (the cable's Program Change, a learned switch and CC 118 act as a computer's, nothing is sent to the footswitch, Bluetooth's
  messages not read while the cable's footswitch is connected and read again when it is lost, COMPUTER as before). Not
  covered: the host's register sequences (felucca-bsp's, unchanged but for the waits' hook): only the pedal can tell.
- Test build 44: `tests/engine/synfix.c` (the six models, [OWN-FX.md](OWN-FX.md)), `tests/ownfx_equiv.sh` (every other model as the
  release's, bit for bit: 5,420 runs), and the bundled loops rendered through the old and the new models for listening.
- Test build 43, USB audio: `tests/engine/usbret.c` (23 checks, as the image builds the engine and with the host's flags): with
  nothing given or the guitar kept, every place gives build 42's sound bit for bit; before DELAY with the guitar cut, a drive,
  an amp and a cabinet on or off give the same output (the computer's sound skips them), inline and through the wet lanes; MIX
  adds the two; before a block that is off it is where the next one on is; a REVERB moved before the amp takes the computer's
  sound through the amp; the cut fades in 256 samples both ways with no step and takes LOW OCTAVE's octave with it; a NAM amp
  on two cores gives the plain engine's output bit for bit at five places, cut or not; STEREO's two channels sum to the mono
  sound. `tests/test_usb_audio.c` built as this image (its 512-frame playback ring): v42's tests, then USB SEND (DI at the
  input's scale, clipped straight; DI + OUT; the right channel per OUTPUT), the computer's sound read into the chain over 10
  minutes with the clocks 300 ppm apart either way (no gap, no jump, the ring's fill 34 to 127 frames of 512), not mixed at the
  output as well, GUITAR CUT only while the computer plays, a flash write's mute, USB AUDIO off. `tests/uisim.c` (the four
  rows, the page's scroll and bar, every value's text within its box), `tests/test_store.c` (the routing byte through power
  cycles and saves, a copy of builds 39 to 42 read as the default, a byte that does not match or that no build writes read
  as the default, the learned controls untouched), `tests/test_editor.mjs` (MAP's routing byte, an editor that sends none, an
  invalid one refused, kept through a power cycle). Not covered: the load with the radio, a NAM amp and USB audio at once, and
  the streams on a real computer with this image: only the pedal can tell.
- Test build 13's cut, by hand (it is not in `tests/run_tests.sh`): the engine tests built with `-DPX_LOOP_GUARD=1` print the
  same as without it and leave `W` at 0; `rv_block` called with an n above 64 stops at k 64 in each of its four loops, records n
  and k, and writes nothing past frame 63. With the guard off (the release and BLE images) `engine/px_dsp.c` preprocesses to
  the same text as before.
- Test build 14, by hand: the engine is v42's and the cut (`tests/engine/equiv.c`, 852 runs: the same output as v42's engine,
  with and without `-DPX_LOOP_GUARD=1`, `W` 0); the engine tests, plain and with the guard; `engine/px_dsp.c` and
  `firmware/openpocket.c` preprocessed for the release image as v42's (`px_dsp.c` byte for byte; `openpocket.c` apart from
  macros that compile to nothing: its optimised IR is v42's, function for function, now that test build 11's stall-watch
  exception to the second core's bar and power-on key record are built in the Bluetooth images only); `tools/jlfold.py fold`
  on the library's IR (the same 149 constants folded and 86 print helpers emptied as the probe in CI); v42's
  `tests/test_aorder.c` (the callbacks' order). A host model with the real `px_audio.h` and engine and a stand-in second core:
  `a_cpu1_quiet` returns at once for an idle worker, drains a request and its queued network blocks, retires a worker that
  never idles as `G 2` within 8 ms and a faulted core as `G 4`, and collects both (one core, nothing left queued, the next
  interrupt hands nothing over: before, it waited 8 ms and turned `G 4` into `G 2`); this image's callbacks' order fills an
  output-first block whose input comes up to 1,440 µs later and counts one at 1,460 µs in `OD`, with `OG` its time.
  macros that compile to nothing and two leftovers of test build 11, the stall watch's exception to the second core's bar,
  12 bytes, and the power-on key's record); `tools/jlfold.py fold` on the library's IR (the same 149 constants folded and 86
  print helpers emptied as the probe in CI); v42's `tests/test_aorder.c` (the callbacks' order).
- The image's own effects (tests/run_tests.sh, [OWN-FX.md](OWN-FX.md)): `tests/ownfx_equiv.sh`, every existing model with
  `PX_OWN_FX` and without, 5,460 runs bit for bit; LOW OCTAVE (`tests/engine/lowoct.c`, 18 checks) and PAD
  (`tests/engine/pad.c`, 15) as the image builds the engine and with the host's flags, also under ASan and UBSan, both
  giving v40's output bit for bit; the PAD's footswitch against v42's tuner (`tests/uisim.c` built with `PX_OWN_FX`, 16
  gestures, v42's 48 screens unchanged) and CC 64 (`tests/midiio.c`); without `PX_OWN_FX` the engine and `openpocket.c`
  preprocess to v42's text for the release, BLE and Bluetooth flags.
- The member set was chosen from the archives' symbol tables: what it references and does not define is supplied
  (`bt_glue.c`, `bt_cfg.c`) or stood in for (`bt_stubs.c`), with the library's own types.
