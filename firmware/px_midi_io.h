/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — musical MIDI over USB: the glue between usb.c's rings and the pedal (docs/MIDI.md).
 *
 * usb.c (felucca-bsp, unchanged) puts the channel voice messages and the clock / transport bytes of the host into
 * midi_in_q (4-byte USB-MIDI event packets, with the millisecond they arrived) and sends what is put in midi_out_q.
 * SysEx never takes this road: it goes to the update code (ota.c) and the editor (px_editor.h) only. Test build 41 (the
 * Bluetooth audio image): with SETTINGS' USB PORT on FOOTSW, felucca-bsp's host (chip/usb_host.c) puts a footswitch's channel
 * messages in the same ring, so a Chocolate by cable is read as a computer is; nothing is sent back to it then (px_usbhost.h).
 * The including file provides px_midi.h, usb.c's rings, PRESET, cur_preset, load_preset, master_vol, ui_mode / ui_item /
 * ui_nitems (ui.h), st_midi, st_usb and st_commit (px_store.h), and ms_now; in the Bluetooth audio image (PX_OWN_FX) also
 * ui_pad_on (ui.h) and PX_PAD_PRESS (openpocket.c), for CC 64; CC 116 is a tap of the delay's tempo; PX_EXP_SET (openpocket.c)
 * for CC 117, the expression pedal.
 *
 * The ring must be emptied: usb.c leaves the host's USB packet unread (NAK) while the ring is nearly full, and the
 * editor's and installer's SysEx come in the same packets. Nothing drained it before this file.
 */
#pragma once

#if PX_OWN_FX && !defined(PXM_STEP)
#define PXM_STEP(dd) ui_preset_step(dd)    /* test build 39: CC 118 / 119, as the footswitches (ui.h) */
#endif

static px_midi_tx_t mid_tx;
static px_midi_clock_t mid_clk;

static void midi_persist(void)                  /* the page's HOME: save the settings (the sound is not touched) */
{
    st_midi = px_midi_cfg_pack(&mid_cfg);
    st_usb = (uint8_t)((st_usb & ~1u) | (px_ua_on & 1u));   /* v42: USB AUDIO (px_usb.h then presents the device anew) */
    ft_box(0, 186, 240, "SAVING...", &FT_MD, UI_ACC, UI_BG, 1);
    if (st_ok) (void)st_commit();
}

#if OP_BLE_TEST
/* what a Bluetooth write leaves here (px_midi_ble_write) goes through the same px_midi_in as USB. The writer is the Bluetooth
 * stack's task, which runs from the main loop (ble_poll), so nothing here races: a full ring only drops what does not fit.
 * With ED_BT (the Bluetooth audio image) the write's SysEx also goes to the web editor (px_editor.h ed_bt_write), which
 * the including file has included before this one, kept for the LE link it came on (px_midi_ble_write_link: two links at once
 * since test build 27). The musical messages of every link share one reader: a Chocolate's message starts with its timestamp
 * and status, so it reads the same whatever another link left half done; only SysEx is kept between writes. */
#include "px_ble_midi.h"
#define BLE_MQ 32u
static uint32_t ble_in_q[BLE_MQ], ble_in_w, ble_in_r;
static px_blem_t ble_parse;
#if ED_BT
static void px_midi_ble_write_link(uint32_t link, const uint8_t *p, uint32_t n)   /* (link: the LE link it came on) */
#else
static void px_midi_ble_write(const uint8_t *p, uint32_t n)
#endif
{
    uint32_t pkt[BLE_MQ], got = px_blem_parse(&ble_parse, p, n, pkt, BLE_MQ), i;
    for (i = 0; i < got && ble_in_w - ble_in_r < BLE_MQ; i++) ble_in_q[ble_in_w++ % BLE_MQ] = pkt[i];
#if ED_BT
    ed_bt_write(link, p, n);   /* the Bluetooth audio image: the same write's SysEx, for the web editor (px_editor.h), kept for
                                * its link: two links' writes may come in turns, the musical messages of both reach the pedal */
#endif
}
#if ED_BT
static void px_midi_ble_write(const uint8_t *p, uint32_t n) { px_midi_ble_write_link(0u, p, n); }   /* (the first link's) */
#endif
#endif

#if PX_OWN_FX
/* test build 24: tap tempo for the delay, from a controller's footswitch (the M-VAVE Chocolate over Bluetooth, or USB). CC 116
 * at 64 and up is a tap (a footswitch's release, value 0, is not). Two taps 200 ms to 2 s apart set the delay's TIME to their
 * interval, the nearest TIME step as MIDI clock does; from the third tap on, to the mean of the last three intervals. A pause
 * of more than 2 s starts over. In this image TIME stops at 780 ms (REVERSE 390): a slower tap gives 780 */
#define PXM_CC_TAP 116u
#define PXM_CC_EXP 117u   /* test build 25: the expression pedal, 0..127 (engine/px_dsp.c px_exp: WHAMMY, AUTO WAH or volume) */
static uint32_t tap_ms, tap_n, tap_iv[3];
static void px_midi_tap(uint32_t ms, int *touched)
{
    const uint32_t d = ms - tap_ms;
    uint32_t k, s = 0u, i;
    int v;
    if (!tap_n || d > 2000u) { tap_n = 1u; tap_ms = ms; return; }
    if (d < 200u) return;                                       /* (a bounce, or a second message of one press) */
    tap_ms = ms;
    tap_iv[(tap_n - 1u) % 3u] = d;
    tap_n++;
    k = tap_n - 1u < 3u ? tap_n - 1u : 3u;
    for (i = 0; i < k; i++) s += tap_iv[i];
    v = px_dly_value_for_ms(PRESET.type[PX_B_DELAY], (float)s / (float)k);
    if (PRESET.p[PX_B_DELAY][0] != v) { PRESET.p[PX_B_DELAY][0] = (uint8_t)v; *touched = 1; }
}
#endif

/* one received event packet, from either road; *touched: the sound changed */
static void px_midi_in(uint32_t pkt, uint32_t ms, int free_ui, int *touched)
{
    px_midi_msg_t m = px_midi_decode(&mid_cfg, pkt);
    if (m.type == PXM_IGNORE) return;
    if (m.type == PXM_CLOCK) {
        uint32_t q = mid_cfg.clock && free_ui ? px_midi_clock_tick(&mid_clk, ms) : 0u;
        if (q) {
            int v = px_dly_value_for_ms(PRESET.type[PX_B_DELAY], (float)q);
            if (PRESET.p[PX_B_DELAY][0] != v) { PRESET.p[PX_B_DELAY][0] = (uint8_t)v; *touched = 1; }
        }
        return;
    }
    if (m.type == PXM_START || m.type == PXM_STOP) { px_midi_clock_reset(&mid_clk); return; }
    mid_last[0] = m.type; mid_last[1] = m.d1; mid_last[2] = m.d2;
#if PX_OWN_FX
    mid_in_n++;                                        /* (the controller's own number: the MIDI page and the editor show it) */
    if (m.type == PXM_CC) {                            /* test build 39: a control learned on the pedal, as the pedal's own */
        uint32_t c = m.d1, v = m.d2;
        if (!px_midi_map_in(&c, &v)) return;           /* (a switch's release) */
        m.d1 = (uint8_t)c; m.d2 = (uint8_t)v;
    }
#endif
    if (m.type == PXM_PC) {
        if (free_ui && m.d1 < px_preset_count()) {
            cur_preset = m.d1;
            load_preset(cur_preset);
            if (ui_mode == M_EDIT) ui_mode = M_BLOCK;
            if ((int)ui_item >= ui_nitems()) ui_item = 0;
            *touched = 1;
        }
#if PX_OWN_FX
    } else if (m.d1 == 64u) {                          /* CC 64 (a sustain pedal) at 64 and up: a SYN PAD catches the chord */
        if (m.d2 >= 64u && ui_pad_on()) PX_PAD_PRESS();
    } else if (m.d1 == PXM_CC_TAP) {                   /* CC 116 at 64 and up: a tap of the delay's tempo (test build 24) */
        if (m.d2 >= 64u && free_ui) px_midi_tap(ms, touched);
    } else if (m.d1 == PXM_CC_EXP) {                   /* CC 117: the expression pedal's position (test build 25). Not a
                                                        * preset change: nothing to save, nothing redrawn, on every page */
        PX_EXP_SET(m.d2);
    } else if (m.d1 == PXM_CC_NEXT || m.d1 == PXM_CC_PREV) {   /* CC 118 / 119 at 64 and up: the next / previous preset, as
                                                        * the footswitches (test build 39: a footswitch in its CC mode) */
        if (m.d2 >= 64u && free_ui) { PXM_STEP(m.d1 == PXM_CC_NEXT ? 1 : -1); *touched = 1; }
#endif
    } else {
        uint32_t mv = master_vol;
        int mset = 0;
        if (free_ui && px_midi_cc_apply(&PRESET, m.d1, m.d2, &mv, &mset)) *touched = 1;
        else if (mset) master_vol = mv;
    }
}

/* every main-loop pass */
static void px_midi_service(void)
{
    /* the sound may be changed from the host only while the pedal is on a normal page */
    int free_ui = ui_mode != M_SAVE && ui_mode != M_TUNER && ui_mode != M_MOVE && ui_mode != M_MIDI;
    int touched = 0;
#if PX_OWN_FX
    mid_usb_up = (uint8_t)(!px_usb_foot && usb_midi_out_ready());   /* (test build 41: a computer, not the footswitch) */
#else
    mid_usb_up = (uint8_t)usb_midi_out_ready();
#endif
    while (mi_r != mi_w) {                      /* usb.c's timer interrupt is the only writer */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], ms = midi_in_ms[at];
        RING_PUBLISH();
        mi_r++;
        px_midi_in(pkt, ms, free_ui, &touched);
    }
#if OP_BLE_TEST
#if PX_OWN_FX
    if (px_uh.st == PXU_OK) ble_in_r = ble_in_w;   /* test build 41: while the footswitch on the cable is connected, Bluetooth's
                                                * notes and controls are not read: a Chocolate linked both ways would act twice
                                                * (a block flipped and flipped back, two presets for one). The editor's SysEx
                                                * is not in this ring. Cable lost: Bluetooth is read again, the spare */
#endif
    while (ble_in_r != ble_in_w) px_midi_in(ble_in_q[ble_in_r++ % BLE_MQ], ms_now(), free_ui, &touched);
#endif
    if (midi_in_overflow && mi_r == mi_w) midi_in_overflow = 0;   /* usb.c stopped queueing on overflow: start over */
    if (touched) px_midi_tx_sync(&mid_tx, &PRESET, cur_preset);   /* what the host sent is not sent back */
#if PX_OWN_FX
    if (px_usb_foot) { px_midi_tx_sync(&mid_tx, &PRESET, cur_preset); return; }   /* test build 41: the port is the footswitch's
                                                * host: nothing is sent to it (a footswitch that never takes its OUT endpoint's
                                                * packets would be dropped as lost, chip/usb_host.c uh_tx_poll) */
#endif
    if (!usb_midi_out_ready()) { px_midi_tx_sync(&mid_tx, &PRESET, cur_preset); return; }
    while (midi_out_room()) {
        uint32_t pkt = px_midi_tx_next(&mid_tx, &mid_cfg, &PRESET, cur_preset);
        if (!pkt) break;
        midi_out_try(pkt);
    }
}
