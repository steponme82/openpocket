/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — USB audio: what the pedal asks of felucca-bsp's USB device (bsp/chip/usb.c), set before it is included
 * (px_usb.h; tests/test_usb_audio.c, which runs the same device on a PC). docs/USB-AUDIO.md.
 */
#pragma once

#define FELUCCA_UAC 1                           /* the pedal's sound to the computer (EP4 IN, asynchronous) */
#define FELUCCA_UAC_48K 0                       /* 44.1 kHz only, as the stock firmware: no resampler in the interrupt */
#define FELUCCA_UAC_OUT 1                       /* the computer's playback to the pedal's output (EP4 OUT, adaptive) */
#define FELUCCA_UAC_MIDI_LAST 1                 /* audio control, the streams, then MIDI: the order macOS 13-15 need (usb.c) */
#define FELUCCA_UAC_SWITCH 1                    /* only while the setting is on (usb_uac_on) */
#define HALF_FRAMES A_FR                        /* the frames a block feeds the record ring */
#if OP_BT_AUDIO
/* test build 43, the Bluetooth audio image: both rings in the engine pool's free end (.pool.tail, zeroed at boot; the 96 KB RAM
 * is full), CPU0's only (the USB poll, a_block, a_send); the playback ring of 512 frames (2 KB): the target, a block and a
 * packet are 385, which leaves a block read 3 ms late. (On a PC, tests/test_usb_audio.c: plain arrays) */
#if defined(__PI32V2__)
#define FELUCCA_UAC_RING_ATTR __attribute__((section(".pool.tail.ua_ring"), aligned(4)))
#define FELUCCA_UAC_OUT_RING_ATTR __attribute__((section(".pool.tail.uo_ring"), aligned(4)))
#endif
#define UO_N 512u
#else
#define UO_N 1024u                              /* the playback ring: 4 KB (the target, a block and a packet either way) */
#endif
/* v42: the rings' fills, for the pedal's blocks (px_uaudio.h). A block is written and read where its sound is ready
 * (a_send, px_audio.h): 0.3 to 2.5 ms after the interrupt's start, by the preset, but steady within a preset (measured
 * over the 52 presets with a guitar playing: within 0.06 ms, 0.2 ms on ORGAN) since v42 keeps the callbacks' order
 * (px_aorder.h). felucca-bsp's own figures (a band of 80 to 128, primed at 104, and a target of 256) allow for a render
 * that starts up to 0.7 ms late and held 1.6 ms more than the pedal needs. Measured with these: the record ring's fill at
 * a block's write 38 to 121 frames, the playback ring's at a block's read 167 to 251 (it needs 130).
 * The margin left, with the two clocks up to 300 ppm apart either way (tests/test_usb_audio.c): 0.6 ms (26 frames)
 * and more. The record ring takes a block that comes that much later than the one before (an effect switched on), the
 * playback ring one that comes that much sooner (switched off); beyond it a few frames are held or the ring is primed
 * again, as at a preset change. */
#if !OP_UADEV                                   /* (the measurement option sets them at run time, px_usb.h) */
#define UA_LO 52u                               /* the band of the record ring's fill at a block's write: below, a packet one frame shorter */
#define UA_HI 108u                              /*   above, one frame longer (one correction a block at most) */
#define UA_PRIME 80u                            /* silence ahead of the first block: the band's middle */
#define UO_TGT 208u                             /* the playback ring's fill a block's read aims at */
#endif
