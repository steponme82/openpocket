/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the glue between usb.c's MIDI rings and the pedal (firmware/px_midi_io.h) on a PC, with the rings, the
 * screen mode and the store stubbed: what a host's Program Change, Control Change and clock do to the sound, when the
 * pedal must ignore them, that the input ring is always emptied (usb.c stops reading the host's packets when it is
 * nearly full, and the editor's SysEx arrives in the same packets), and what the pedal sends back.
 *   cc -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/midiio tests/midiio.c engine/px_dsp.c firmware/px_math.c -lm && build/midiio
 * With -DPX_OWN_FX=1 (the Bluetooth audio image's): CC 64 catches a SYN PAD's chord, and only that. With -DED_BT=1 (that image's
 * too, test build 27): a Bluetooth write's SysEx also goes to the web editor, and its musical messages to the pedal as before. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "px_midi.h"

static int failed;
static void ok(int c, const char *what) { printf("%-76s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

/* ---- usb.c's rings (same names, same meaning) ---- */
#define MQ 64u
#define RING_PUBLISH() ((void)0)
static uint32_t midi_in_q[MQ], midi_out_q[MQ], midi_in_ms[MQ];
static volatile uint32_t mi_w, mi_r, mo_w, mo_r;
static volatile uint8_t midi_in_overflow;
static int host_up = 1;
static int usb_midi_out_ready(void) { return host_up; }
static uint32_t midi_out_room(void) { return host_up ? MQ - (mo_w - mo_r) : 0u; }
static int midi_out_try(uint32_t pkt)
{
    if (!host_up || mo_w - mo_r >= MQ) return 0;
    midi_out_q[mo_w % MQ] = pkt;
    mo_w++;
    return 1;
}
static int midi_enqueue(uint32_t pkt, uint32_t ms)    /* as usb.c's midi_enqueue (+ the overflow rule) */
{
    if (midi_in_overflow) return 0;
    if (mi_w - mi_r >= MQ) { midi_in_overflow = 1; return 0; }
    midi_in_q[mi_w % MQ] = pkt;
    midi_in_ms[mi_w % MQ] = ms;
    mi_w++;
    return 1;
}
static uint32_t pop_out(void) { return mo_r != mo_w ? midi_out_q[mo_r++ % MQ] : 0; }

/* ---- the pedal ---- */
enum { M_CHAIN, M_BLOCK, M_EDIT, M_TUNER, M_SAVE, M_MOVE, M_MIDI };
static px_preset_t PRESET;
static uint32_t cur_preset, master_vol = 80, ui_mode = M_CHAIN, ui_item;
static uint8_t st_midi, st_usb, st_ok = 1;
static int commits;
static int ui_nitems(void) { return 2 + px_nparams(PX_B_AMP, PRESET.type[PX_B_AMP]); }
static void load_preset(uint32_t i) { px_preset_factory(&PRESET, (int)i); }
static int st_commit(void) { commits++; return 0; }
typedef struct { int dummy; } op_font;
static op_font FT_MD;
static uint32_t ft_box(uint32_t x, uint32_t y, uint32_t w, const char *s, const op_font *f, uint16_t fg, uint16_t bg, int a)
{ (void)x; (void)y; (void)w; (void)s; (void)f; (void)fg; (void)bg; (void)a; return 0; }
#define UI_ACC 0
#define UI_BG 0
#define OP_BLE_TEST 1
static uint32_t ms_now(void) { return 0; }
#if PX_OWN_FX                                            /* SYN PAD (the Bluetooth audio image): as ui.h, and a counter */
static int ui_pad_on(void) { return PRESET.on[PX_B_SYN] && PRESET.type[PX_B_SYN] % PX_SYN_COUNT == PX_SYN_PAD; }
static int pad_presses;
#define PX_PAD_PRESS() (pad_presses++)
static int exp_last = -1, exp_n;
#define PX_EXP_SET(v) (exp_last = (int)(v), exp_n++)
static int steps, step_n;                                /* test build 39: CC 118 / 119, the footswitches' preset step */
#define PXM_STEP(dd) (steps += (dd), step_n++)
static int scene_last = -1, scene_n;                     /* test build 49: CC 65 to 68, a scene */
#define PXM_SCENE(n) (scene_last = (int)(n), scene_n++)
#endif
#if ED_BT                                                /* the Bluetooth audio image (-DED_BT=1): the web editor's SysEx, kept
                                                        * across writes as px_editor.h keeps it (its ed_bt_write, one line) */
#include "px_ble_midi.h"
static px_blem_sx_t ed_bt_rx[2];                         /* (two LE links, test build 27) */
static void ed_bt_write(uint32_t link, const uint8_t *p, uint32_t n) { if (link < 2u) px_blem_sysex(&ed_bt_rx[link], p, n); }
#endif
#include "px_midi_io.h"

static uint32_t cc(uint32_t ch, uint32_t n, uint32_t v) { return 0xBu | (0xB0u | ch) << 8 | n << 16 | v << 24; }
static uint32_t pc(uint32_t ch, uint32_t n) { return 0xCu | (0xC0u | ch) << 8 | n << 16; }
static void reset(void)
{
    mi_w = mi_r = mo_w = mo_r = 0; midi_in_overflow = 0; host_up = 1; ui_mode = M_CHAIN; ui_item = 0; master_vol = 80;
    memset(&mid_cfg, 0, sizeof mid_cfg); mid_cfg.send = 1;
    memset(&mid_tx, 0, sizeof mid_tx); px_midi_clock_reset(&mid_clk);
    cur_preset = 5; load_preset(5);
    px_midi_service();                              /* first look: the pedal learns what it is playing */
}

int main(void)
{
    reset();
    midi_enqueue(pc(0, 12), 1);
    px_midi_service();
    ok(cur_preset == 12, "Program Change 12 selects slot 13");
    {
        px_preset_t f; px_preset_factory(&f, 12);
        ok(!memcmp(&f, &PRESET, sizeof f), "the sound is slot 13's factory preset");
    }
    ok(pop_out() == 0, "a Program Change from the host is not sent back");

    reset();
    midi_enqueue(pc(0, 60), 1);
    px_midi_service();
    ok(cur_preset == 5, "Program Change 60 (there are 52 slots) is ignored");

    reset();
    midi_enqueue(cc(0, 20 + PX_B_AMP, 0), 1);
    midi_enqueue(cc(0, 7, 127), 2);
    px_midi_service();
    ok(PRESET.on[PX_B_AMP] == 0 && master_vol == 100, "CC 25 = 0 switches the amp off; CC 7 = 127 sets MASTER to 100");
    ok(pop_out() == 0, "the host's changes are not echoed");

    reset();
    PRESET.p[PX_B_DELAY][0] ^= 1;                    /* a knob turned on the pedal */
    px_midi_service();
    {
        uint32_t o = pop_out();
        ok((o & 15u) == 0xB && ((o >> 16) & 0xFF) == px_cc_param(PX_B_DELAY, 0), "a knob turned on the pedal is sent as a Control Change");
        ok(pop_out() == 0, "...once");
    }
    cur_preset = 9; load_preset(9);                  /* a footswitch */
    px_midi_service();
    ok(pop_out() == pc(0, 9) && pop_out() == 0, "a preset change on the pedal is one Program Change");

    reset();
    host_up = 0;
    PRESET.on[PX_B_DELAY] ^= 1;
    px_midi_service();
    host_up = 1;
    px_midi_service();
    ok(pop_out() == 0, "with no host connected nothing is queued, and nothing old is sent when one connects");

    reset();
    ui_mode = M_SAVE;
    midi_enqueue(pc(0, 20), 1);
    midi_enqueue(cc(0, 20 + PX_B_AMP, 0), 2);
    px_midi_service();
    ok(cur_preset == 5 && PRESET.on[PX_B_AMP] == 1, "the SAVE page is open: Program and Control Change are ignored");
    ok(mi_r == mi_w, "...but the ring is emptied");
    ui_mode = M_TUNER;
    midi_enqueue(pc(0, 20), 1);
    px_midi_service();
    ok(cur_preset == 5, "the tuner is open: ignored");
    ui_mode = M_MIDI;
    midi_enqueue(pc(0, 20), 1);
    px_midi_service();
    ok(cur_preset == 5, "the MIDI page is open: ignored");
    ui_mode = M_EDIT; ui_item = 7;
    midi_enqueue(pc(0, 20), 1);
    px_midi_service();
    ok(cur_preset == 20 && ui_mode == M_BLOCK && (int)ui_item < ui_nitems(), "a Program Change while editing goes back to the block page, the selection kept in range");

    reset();
    mid_cfg.chan = 4;
    midi_enqueue(pc(0, 11), 1);
    midi_enqueue(pc(3, 12), 2);
    px_midi_service();
    ok(cur_preset == 12, "channel 4: channel 1's Program Change is ignored, channel 4's is taken");

    reset();
    {   /* a flood (a DAW sending notes, clock and CCs the map does not use) is drained and never wedges the ring */
        uint32_t i;
        for (i = 0; i < 1000; i++) {
            midi_enqueue(0x9u | 0x90u << 8 | (i & 127u) << 16 | 100u << 24, i);
            midi_enqueue(0xFu | 0xF8u << 8, i);
            if (i % 40 == 0) { px_midi_service(); }
        }
        px_midi_service();
        ok(mi_r == mi_w && !midi_in_overflow, "after a flood the ring is empty and usb.c may queue again");
        ok(midi_enqueue(pc(0, 3), 3000) == 1, "and it accepts the next message");
        px_midi_service();
        ok(cur_preset == 3, "which is then taken");
    }
    reset();
    {   /* the flood fills the ring while the main loop is busy (a flash write): the overflow flag is cleared afterwards */
        uint32_t i;
        for (i = 0; i < 100; i++) midi_enqueue(cc(0, 1, i & 127u), i);
        ok(midi_in_overflow == 1, "(setup) the ring overflowed");
        px_midi_service();
        ok(!midi_in_overflow && mi_r == mi_w, "the overflow flag is cleared once the ring is empty");
    }

    reset();
    mid_cfg.clock = 1;
    {
        uint32_t i, ms = 1000;
        for (i = 0; i < 24 * 5; i++) { midi_enqueue(0xFu | 0xF8u << 8, ms); ms += 21; px_midi_service(); }   /* ~ 504 ms a quarter */
        {
            int want = px_dly_value_for_ms(PRESET.type[PX_B_DELAY], 504.0f);
            ok(PRESET.p[PX_B_DELAY][0] == want, "clock sync on: the delay TIME follows a 504 ms quarter note");
        }
        ok(pop_out() == 0, "the tempo-derived change is not sent to the host");
    }
    reset();
    for (uint32_t i = 0, ms = 1000; i < 24 * 5; i++, ms += 21) { midi_enqueue(0xFu | 0xF8u << 8, ms); px_midi_service(); }
    {
        px_preset_t f; px_preset_factory(&f, 5);
        ok(PRESET.p[PX_B_DELAY][0] == f.p[PX_B_DELAY][0], "clock sync off (the default): the clock does nothing");
    }

    reset();
    {
        uint32_t c0 = (uint32_t)commits;
        mid_cfg.chan = 6; mid_cfg.send = 0;
        midi_persist();
        ok(commits == (int)c0 + 1 && st_midi == px_midi_cfg_pack(&mid_cfg), "leaving the MIDI page writes the settings byte to the store, once");
        ok(st_usb == 0, "USB AUDIO off: the USB settings byte stays 0");
#if PX_OWN_FX
        const uint8_t OB = 0x02;                       /* (test build 49: bits 2 to 7 are the Bluetooth image's settings: bit 1) */
#else
        const uint8_t OB = 0x80;
#endif
        px_ua_on = 1; st_usb = OB;
        midi_persist();
        ok(commits == (int)c0 + 2 && st_usb == (OB | 1u), "USB AUDIO on: bit 0 of the USB settings byte, the other bits kept");
        px_ua_on = 0;
        midi_persist();
        ok(st_usb == OB, "USB AUDIO off again: bit 0 cleared");
        st_usb = 0;
    }
    reset();
    {
        uint8_t on[] = {0x80, 0x80, 0xB0, 24, 127}, off[] = {0x80, 0x80, 0xB0, 24, 0}, pc[] = {0x80, 0x80, 0xC0, 7};
        PRESET.on[PX_B_DRIVE] = 0;
        px_midi_ble_write(on, sizeof on);
        px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == 1 && mid_last[0] == PXM_CC && mid_last[1] == 24 && mid_last[2] == 127, "a Bluetooth write switches a block as USB does, and the page shows it");
        ok(pop_out() == 0, "what came over Bluetooth is not sent back");
        ui_mode = M_SAVE;
        px_midi_ble_write(off, sizeof off);
        px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == 1 && ble_in_r == ble_in_w, "the SAVE page ignores Bluetooth too, and the ring is emptied");
        ui_mode = M_CHAIN;
        px_midi_ble_write(pc, sizeof pc);
        px_midi_service();
        ok(cur_preset == 7, "a Bluetooth Program Change loads the preset");
        uint8_t tap[] = {0x80, 0x8E, 0xB1, 12, 0x7F}, tap0[] = {0x80, 0x8E, 0xB1, 12, 0x00};
        uint32_t was = PRESET.on[PX_B_DRIVE];
        px_midi_ble_write(tap, sizeof tap); px_midi_ble_write(tap0, sizeof tap0);
        px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == !was, "a Chocolate tap on CC 12 over Bluetooth flips DRIVE once");
        load_preset(cur_preset);
        was = PRESET.on[PX_B_DRIVE];
        px_midi_ble_write(tap0, sizeof tap0);
        px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == !was, "after a preset change the toggle flips what the new preset has");
    }
#if ED_BT
    {   /* the web editor over Bluetooth (test build 27): every write also gives its SysEx to the editor, kept across writes,
         * and the musical messages of the same writes reach the pedal as before */
        reset();
        uint8_t w1[] = {0x80, 0x81, 0xF0, 0x7D, 0x4F, 0x50, 0x12}, w2[] = {0x80, 0x82, 0xF7, 0x83, 0xC0, 9};
        uint8_t on[] = {0x80, 0x80, 0xB0, 24, 127};
        px_midi_ble_write(w1, sizeof w1);
        px_midi_service();
        ok(!ed_bt_rx[0].ready && cur_preset == 5, "a write that starts an editor SysEx: kept, nothing for the pedal yet");
        px_midi_ble_write(w2, sizeof w2);
        px_midi_service();
        ok(ed_bt_rx[0].ready && ed_bt_rx[0].n == 4 && ed_bt_rx[0].buf[3] == 0x12 && cur_preset == 9,
           "the next write ends it (the editor's PING, whole) and its Program Change still loads preset 10");
        ed_bt_rx[0].ready = 0;
        PRESET.on[PX_B_DRIVE] = 0;
        px_midi_ble_write(on, sizeof on);
        px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == 1 && !ed_bt_rx[0].ready && !ed_bt_rx[0].in, "the Chocolate's Control Change: as before, nothing for the editor");
        ok(pop_out() == 0, "and nothing is sent back over USB");
        /* two LE links at once (test build 27): the editor on link 1 sends a SysEx over three writes while the Chocolate on
         * link 0 presses in between; both are understood, neither spoils the other */
        uint8_t e1[] = {0x80, 0x81, 0xF0, 0x7D, 0x4F, 0x50}, e2[] = {0x80, 0x12, 0x01}, e3[] = {0x80, 0x82, 0xF7};
        uint8_t off[] = {0x80, 0x8E, 0xB0, 24, 0}, pc3[] = {0x80, 0x8E, 0xC0, 3};
        px_midi_ble_write_link(1, e1, sizeof e1);
        px_midi_ble_write_link(0, off, sizeof off);
        px_midi_service();
        int mid = PRESET.on[PX_B_DRIVE] == 0 && ed_bt_rx[1].in && !ed_bt_rx[1].ready;
        px_midi_ble_write_link(1, e2, sizeof e2);
        px_midi_ble_write_link(0, pc3, sizeof pc3);
        px_midi_ble_write_link(1, e3, sizeof e3);
        px_midi_service();
        ok(mid && cur_preset == 3 && ed_bt_rx[1].ready && ed_bt_rx[1].n == 5 && ed_bt_rx[1].buf[4] == 0x01 && !ed_bt_rx[0].ready,
           "two links in turns: the Chocolate's switch and preset act, the editor's SysEx on the other link comes whole");
        ed_bt_rx[1].ready = 0;
    }
#endif
#if PX_OWN_FX
    {   /* CC 64 (sustain) catches the PAD's chord: at 64 and up, with a PAD on; nothing else changes */
        reset();
        px_preset_t before;
        PRESET.type[PX_B_SYN] = PX_SYN_PAD; PRESET.on[PX_B_SYN] = 1; before = PRESET; pad_presses = 0;
        midi_enqueue(cc(0, 64, 127), 1); midi_enqueue(cc(0, 64, 0), 2); midi_enqueue(cc(0, 64, 64), 3); midi_enqueue(cc(0, 64, 63), 4);
        px_midi_service();
        int on = pad_presses == 2 && !memcmp(&before, &PRESET, sizeof before);
        PRESET.on[PX_B_SYN] = 0; pad_presses = 0;
        midi_enqueue(cc(0, 64, 127), 5); px_midi_service();
        int off = pad_presses == 0;
        PRESET.on[PX_B_SYN] = 1; PRESET.type[PX_B_SYN] = PX_SYN_STRINGS; pad_presses = 0;
        midi_enqueue(cc(0, 64, 127), 6); px_midi_service();
        ok(on && off && pad_presses == 0, "CC 64 at 64 and up catches the PAD's chord (PAD on only), the sound unchanged");
    }
    {   /* CC 116 taps the delay's tempo (test build 24): two taps set TIME to their interval, a release (0) is no tap, a bounce
         * under 200 ms is ignored, the last three intervals are averaged, a pause of more than 2 s starts over */
        reset();
        tap_n = 0;
        PRESET.type[PX_B_DELAY] = PX_DLY_DIGITAL;
        midi_enqueue(cc(0, 116, 127), 1000); midi_enqueue(cc(0, 116, 0), 1100); midi_enqueue(cc(0, 116, 127), 1500);
        px_midi_service();
        int two = PRESET.p[PX_B_DELAY][0] == px_dly_value_for_ms(PX_DLY_DIGITAL, 500.0f);
        midi_enqueue(cc(0, 116, 127), 1550); px_midi_service();
        int bounce = PRESET.p[PX_B_DELAY][0] == px_dly_value_for_ms(PX_DLY_DIGITAL, 500.0f);
        midi_enqueue(cc(0, 116, 127), 1900); midi_enqueue(cc(0, 116, 127), 2350); px_midi_service();
        int avg = PRESET.p[PX_B_DELAY][0] == px_dly_value_for_ms(PX_DLY_DIGITAL, 450.0f);   /* (500 + 400 + 450) / 3 */
        int was = PRESET.p[PX_B_DELAY][0];
        midi_enqueue(cc(0, 116, 127), 5000); px_midi_service();
        int pause = PRESET.p[PX_B_DELAY][0] == was;
        midi_enqueue(cc(0, 116, 127), 5600); px_midi_service();
        int restart = PRESET.p[PX_B_DELAY][0] == px_dly_value_for_ms(PX_DLY_DIGITAL, 600.0f);
        ok(two && bounce && avg && pause && restart,
           "CC 116 taps the delay's TIME (release and bounce ignored, three intervals averaged, a pause starts over)");
#if PX_DLY_SHORT
        midi_enqueue(cc(0, 116, 127), 9000); midi_enqueue(cc(0, 116, 127), 10000); px_midi_service();
        ok(PRESET.p[PX_B_DELAY][0] == px_dly_value_for_ms(PX_DLY_DIGITAL, 780.0f) && PRESET.p[PX_B_DELAY][0] < 64,
           "a tap slower than 780 ms gives the shortened ring's top, 780 ms (TIME 63)");
#endif
    }
    {   /* test build 39: CC 118 / 119 step to the next / previous preset at 64 and up (a release is nothing), not on the MIDI page */
        reset();
        steps = step_n = 0;
        midi_enqueue(cc(0, 118, 127), 1); midi_enqueue(cc(0, 118, 0), 2); midi_enqueue(cc(0, 119, 127), 3); midi_enqueue(cc(0, 119, 127), 4);
        px_midi_service();
        int two = step_n == 3 && steps == -1;
        ui_mode = M_MIDI;
        midi_enqueue(cc(0, 118, 127), 5); px_midi_service();
        ui_mode = M_CHAIN;
        ok(two && step_n == 3, "CC 118 / 119 at 64 and up step to the next / previous preset (releases and the MIDI page do nothing)");
    }
    {   /* test build 39: SWITCH LATCH (mid_cfg.each): CC 8..19 flip on every message, for a switch that alternates 127 and 0 */
        reset();
        int was = PRESET.on[PX_B_EQ];
        midi_enqueue(cc(0, 8 + PX_B_EQ, 127), 1); px_midi_service();
        int moment = PRESET.on[PX_B_EQ] == was;             /* (MOMENT, the default: 127 is a press, the flip comes on 0) */
        mid_cfg.each = 1;
        midi_enqueue(cc(0, 8 + PX_B_EQ, 127), 2); px_midi_service();
        int a = PRESET.on[PX_B_EQ] == !was;
        midi_enqueue(cc(0, 8 + PX_B_EQ, 0), 3); px_midi_service();
        int b = PRESET.on[PX_B_EQ] == was;
        mid_cfg.each = 0;
        ok(moment && a && b, "SWITCH LATCH: CC 15 flips the EQ on 127 and on 0 alike (MOMENT: only on 0)");
        ok(px_midi_cfg_pack(&(px_midi_cfg_t){ .send = 1, .each = 1 }) == PXM_CFG_EACH, "LATCH is bit 7 of the settings byte");
    }
    {   /* test build 39: controls learned on the pedal: a controller's own number read as one of the pedal's */
        reset();
        px_midi_map_clear();
        mid_map[0] = (px_map_t){ 80, 8 + PX_B_EQ, PXM_MAP_PRESS };    /* a momentary switch on CC 80: the EQ flips on its press */
        mid_map[1] = (px_map_t){ 81, 8 + PX_B_DELAY, PXM_MAP_EACH };  /* an alternating switch on CC 81: the delay flips each time */
        mid_map[2] = (px_map_t){ 82, PXM_CC_NEXT, PXM_MAP_PRESS };    /* CC 82: the next preset */
        mid_map[3] = (px_map_t){ 4, 117, PXM_MAP_VALUE };             /* an expression pedal on CC 4: the pedal's own, 117 */
        mid_map[4] = (px_map_t){ 20 + PX_B_AMP, 8 + PX_B_MOD, PXM_MAP_PRESS };   /* a learned number wins over the map's own */
        int eq = PRESET.on[PX_B_EQ], dl = PRESET.on[PX_B_DELAY], amp = PRESET.on[PX_B_AMP], mod = PRESET.on[PX_B_MOD];
        uint32_t n0 = mid_in_n;
        midi_enqueue(cc(0, 80, 127), 1); px_midi_service();
        int press = PRESET.on[PX_B_EQ] == !eq;
        midi_enqueue(cc(0, 80, 0), 2); px_midi_service();
        int release = PRESET.on[PX_B_EQ] == !eq && mid_last[1] == 80 && mid_last[2] == 0;   /* (the MIDI page shows the controller's number) */
        midi_enqueue(cc(0, 81, 127), 3); midi_enqueue(cc(0, 81, 0), 4); midi_enqueue(cc(0, 81, 127), 5); px_midi_service();
        int each = PRESET.on[PX_B_DELAY] == !dl;          /* three presses: flipped three times */
        steps = step_n = 0;
        midi_enqueue(cc(0, 82, 127), 6); midi_enqueue(cc(0, 82, 0), 7); px_midi_service();
        int next = step_n == 1 && steps == 1;
        exp_n = 0;
        midi_enqueue(cc(0, 4, 90), 8); px_midi_service();
        int value = exp_n == 1 && exp_last == 90;
        midi_enqueue(cc(0, 20 + PX_B_AMP, 127), 9); px_midi_service();
        int over = PRESET.on[PX_B_AMP] == amp && PRESET.on[PX_B_MOD] == !mod;
        ok(press && release, "learned PRESS: CC 80's press flips the EQ, its release (0) does nothing");
        ok(each, "learned EACH: every message of CC 81 flips the delay");
        ok(next && value, "learned: CC 82 steps to the next preset, CC 4's value moves the expression pedal");
        ok(over, "a learned number wins over the pedal's own meaning of it (CC 25 flips MOD instead of switching the amp)");
        ok(mid_in_n - n0 == 9, "every message received is counted (the editor's monitor)");
        px_map_t bad = { 83, 3, PXM_MAP_PRESS }, empty = { 0xFF, 0xFF, 0xFF }, mode = { 83, 8, 3 };
        ok(!px_midi_map_valid(&bad) && px_midi_map_valid(&empty) && !px_midi_map_valid(&mode),
           "an entry must name a number the pedal reads and a known mode (empty entries are valid)");
        px_midi_map_clear();
    }
    {   /* test build 46: controls learned live, a preset's own (px_midi.h mid_learn, mid_pl). The screen (ui.h) arms a learn for
         * one row of the preset playing; the next controller Control Change, and that control's messages for 1.2 s, are taken
         * (not played) and tell what it is: a momentary switch (a press and its release), one that alternates (EACH), a pedal
         * (values between 0 and 127) */
        reset();
        px_midi_map_clear(); px_midi_pl_clear();
        cur_preset = 3; load_preset(3);
        const uint32_t TEQ = PXM_CC_TOGGLE0 + PX_B_EQ, OEQ = PXM_CC_ON0 + PX_B_EQ, KDL = px_cc_param(PX_B_DELAY, 1);
        uint32_t src = 0;
        int eq = PRESET.on[PX_B_EQ];
        px_learn_arm(3, TEQ, OEQ, 0, 1000u);
        midi_enqueue(cc(0, 3, 127), 1); midi_enqueue(cc(0, 3, 0), 2); px_midi_service();
        int taken = PRESET.on[PX_B_EQ] == eq;
        int w1 = px_learn_poll(1100u, &src) == PXL_WAIT, w2 = px_learn_poll(2200u, &src) == PXL_WAIT;
        int done = px_learn_poll(2400u, &src) == PXL_DONE && src == 3 && !mid_learn.on;
        int e = px_midi_pl_row(3, TEQ, OEQ);
        int press = e >= 0 && mid_pl[e].src == 3 && mid_pl[e].dst == TEQ && mid_pl[e].mode == PXM_MAP_PRESS;
        ok(taken && w1 && w2 && done && press, "LEARN: a momentary switch on CC 3 (press, release) taken, not played; 1.2 s on: PRESS on the EQ's flip");
        midi_enqueue(cc(0, 3, 127), 3); px_midi_service();
        int flip = PRESET.on[PX_B_EQ] == !eq;
        midi_enqueue(cc(0, 3, 0), 4); px_midi_service();
        int rel = PRESET.on[PX_B_EQ] == !eq;
        cur_preset = 4; load_preset(4);
        int eq4 = PRESET.on[PX_B_EQ];
        midi_enqueue(cc(0, 3, 127), 5); midi_enqueue(cc(0, 3, 0), 6); px_midi_service();
        ok(flip && rel && PRESET.on[PX_B_EQ] == eq4, "learned in preset 4 (slot 3): CC 3 flips its EQ on the press; in preset 5 (slot 4) it does nothing");
        /* a switch that alternates (one message a press): EACH; a pedal on a setting: its value; a pedal on ON: on / off */
        cur_preset = 3; load_preset(3);
        px_learn_arm(3, KDL, KDL, 1, 3000u);
        midi_enqueue(cc(0, 4, 10), 7); midi_enqueue(cc(0, 4, 40), 8); midi_enqueue(cc(0, 4, 90), 9); px_midi_service();
        (void)px_learn_poll(3010u, &src);
        int knob = px_learn_poll(4300u, &src) == PXL_DONE && (e = px_midi_pl_row(3, KDL, KDL)) >= 0 && mid_pl[e].src == 4 && mid_pl[e].mode == PXM_MAP_VALUE;
        midi_enqueue(cc(0, 4, 64), 10); px_midi_service();
        int value = PRESET.p[PX_B_DELAY][1] == pxm_from_cc(64);
        ok(knob && value, "LEARN on a setting: a pedal on CC 4 moves the delay's 2nd setting (64: 50)");
        px_learn_arm(3, TEQ, OEQ, 0, 5000u);
        midi_enqueue(cc(0, 5, 20), 11); midi_enqueue(cc(0, 5, 100), 12); px_midi_service();
        (void)px_learn_poll(5010u, &src);
        int pon = px_learn_poll(6300u, &src) == PXL_DONE && (e = px_midi_pl_row(3, TEQ, OEQ)) >= 0 && mid_pl[e].src == 5 && mid_pl[e].dst == OEQ;
        int one = px_midi_pl_count(3) == 2;              /* (CC 3's switch on the same row went: one control a row) */
        midi_enqueue(cc(0, 5, 100), 13); px_midi_service();
        int on1 = PRESET.on[PX_B_EQ] == 1;
        midi_enqueue(cc(0, 5, 10), 14); px_midi_service();
        int off1 = PRESET.on[PX_B_EQ] == 0;
        midi_enqueue(cc(0, 3, 127), 15); px_midi_service();
        ok(pon && one && on1 && off1 && PRESET.on[PX_B_EQ] == 0, "LEARN on ON with a pedal (CC 5): on at 64 and up, off below; it replaced CC 3 on that row");
        px_learn_arm(3, PXM_CC_TOGGLE0 + PX_B_MOD, PXM_CC_ON0 + PX_B_MOD, 0, 7000u);
        midi_enqueue(cc(0, 6, 127), 16); px_midi_service();
        (void)px_learn_poll(7010u, &src);
        int each = px_learn_poll(8300u, &src) == PXL_DONE && (e = px_midi_pl_row(3, PXM_CC_TOGGLE0 + PX_B_MOD, 0xFFu)) >= 0 &&
                   mid_pl[e].mode == PXM_MAP_EACH;
        int mod = PRESET.on[PX_B_MOD];
        midi_enqueue(cc(0, 6, 0), 17); midi_enqueue(cc(0, 6, 127), 18); px_midi_service();
        ok(each && PRESET.on[PX_B_MOD] == mod, "LEARN: a switch that sends one message a press (CC 6): EACH, every message flips the MOD");
        /* a preset's own wins over the editor's (which work in every preset); nothing heard: given up; the table full */
        mid_map[0] = (px_map_t){ 5, PXM_CC_NEXT, PXM_MAP_PRESS };   /* the editor's: CC 5 steps the preset, in every preset */
        steps = step_n = 0;
        PRESET.on[PX_B_EQ] = 0;
        midi_enqueue(cc(0, 5, 100), 19); px_midi_service();
        int own = step_n == 0 && PRESET.on[PX_B_EQ] == 1;
        cur_preset = 6; load_preset(6);
        midi_enqueue(cc(0, 5, 127), 20); midi_enqueue(cc(0, 5, 0), 21); px_midi_service();
        ok(own && step_n == 1, "a preset's own control wins over the editor's: CC 5 switches slot 3's EQ, steps the preset in slot 6");
        cur_preset = 3; load_preset(3);
        px_learn_arm(3, KDL, KDL, 1, 9000u);
        int silent = px_learn_poll(18000u, &src) == PXL_WAIT && px_learn_poll(19001u, &src) == PXL_SILENT && !mid_learn.on;
        for (uint32_t i = 0; i < PXM_PL_N; i++) if (mid_pl[i].slot == 0xFFu) mid_pl[i] = (px_pl_t){ 20, (uint8_t)(i & 127u), (uint8_t)KDL, PXM_MAP_VALUE, 0, 100 };
        const uint32_t KRV = px_cc_param(PX_B_REVERB, 0);   /* (a row slot 3 has nothing on: no entry of its own to replace) */
        px_learn_arm(3, KRV, KRV, 1, 20000u);
        midi_enqueue(cc(0, 9, 64), 23); px_midi_service();
        (void)px_learn_poll(20010u, &src);
        int full = px_learn_poll(21300u, &src) == PXL_FULL;
        ok(silent && full, "LEARN: nothing in 10 s: given up; with all 48 in use: FULL");
        px_pl_t bad1 = { 52, 3, (uint8_t)OEQ, 0 }, bad2 = { 3, 3, 3, 0 }, bad3 = { 3, 3, (uint8_t)OEQ, 3 }, empty = { 0xFF, 0xFF, 0xFF, 0xFF };
        ok(!px_midi_pl_valid(&bad1) && !px_midi_pl_valid(&bad2) && !px_midi_pl_valid(&bad3) && px_midi_pl_valid(&empty),
           "a preset's entry must name a slot, a number the pedal reads and a known mode (empty entries are valid)");
        {   /* a sound saved into another slot takes its own controls along (px_midi_pl_copy), in place of that slot's */
            px_midi_pl_clear();
            px_midi_pl_put(3, 30, PXM_CC_TOGGLE0 + PX_B_EQ, PXM_MAP_PRESS, PXM_CC_ON0 + PX_B_EQ, 0u, 100u);
            px_midi_pl_put(3, 31, KDL, PXM_MAP_VALUE, KDL, 0u, 100u);
            px_midi_pl_put(7, 40, PXM_CC_TOGGLE0 + PX_B_MOD, PXM_MAP_EACH, PXM_CC_ON0 + PX_B_MOD, 0u, 100u);
            const uint32_t lost = px_midi_pl_copy(3, 7);
            int e1 = px_midi_pl_row(7, PXM_CC_TOGGLE0 + PX_B_EQ, 0xFFu), e2 = px_midi_pl_row(7, KDL, 0xFFu);
            ok(lost == 0 && px_midi_pl_count(7) == 2 && px_midi_pl_count(3) == 2 && e1 >= 0 && e2 >= 0 && mid_pl[e1].src == 30 &&
               mid_pl[e2].mode == PXM_MAP_VALUE && px_midi_pl_row(7, PXM_CC_TOGGLE0 + PX_B_MOD, 0xFFu) < 0,
               "saved into another slot: its two controls copied there, the slot's own one gone, the original kept");
            for (uint32_t i = 0; i < PXM_PL_N; i++) if (mid_pl[i].slot == 0xFFu) mid_pl[i] = (px_pl_t){ 20, (uint8_t)(i & 127u), (uint8_t)KDL, PXM_MAP_VALUE, 0, 100 };
            px_midi_pl_drop_slot(20);
            for (uint32_t i = 0, k = 0; i < PXM_PL_N && k < PXM_PL_N - 5u; i++) if (mid_pl[i].slot == 0xFFu) { mid_pl[i] = (px_pl_t){ 21, (uint8_t)k, (uint8_t)KDL, PXM_MAP_VALUE, 0, 100 }; k++; }
            const uint32_t lost2 = px_midi_pl_copy(3, 9);   /* (one place left after slot 3's, 7's and 21's: one of two copied) */
            ok(lost2 == 1 && px_midi_pl_count(9) == 1 && px_midi_pl_count(PXM_PL_SLOTS) == PXM_PL_N, "no room for all: as many as fit copied, the others counted");
            ok(px_midi_pl_copy(3, 3) == 0 && px_midi_pl_count(3) == 2, "saved onto its own slot: nothing to copy");
        }
        px_midi_pl_drop_slot(3);
        ok(px_midi_pl_count(3) == 0 && px_midi_pl_count(21) > 0, "a slot back to its factory sound drops its own controls, the others stay");
        {   /* test build 47: a pedal's heel and toe on a setting (the screen asks them, ui.h ui_range_end): 0 at the heel, 127 at
             * the toe, in proportion between, either way round; the control set aside while they are being set (mid_learn.hold) */
            px_midi_pl_clear(); cur_preset = 3; load_preset(3);
            px_midi_pl_put(3, 31, KDL, PXM_MAP_VALUE, KDL, 30u, 70u);
            midi_enqueue(cc(0, 31, 0), 30); px_midi_service();
            const int h0 = PRESET.p[PX_B_DELAY][1];
            midi_enqueue(cc(0, 31, 127), 31); px_midi_service();
            const int t0 = PRESET.p[PX_B_DELAY][1];
            midi_enqueue(cc(0, 31, 64), 32); px_midi_service();
            const int m0 = PRESET.p[PX_B_DELAY][1];
            ok(h0 == 30 && t0 == 70 && m0 >= 49 && m0 <= 51, "a pedal's range: 30 at the heel, 70 at the toe, 50 halfway");
            px_midi_pl_put(3, 31, KDL, PXM_MAP_VALUE, KDL, 90u, 10u);
            midi_enqueue(cc(0, 31, 0), 33); px_midi_service();
            const int h1 = PRESET.p[PX_B_DELAY][1];
            midi_enqueue(cc(0, 31, 127), 34); px_midi_service();
            ok(h1 == 90 && PRESET.p[PX_B_DELAY][1] == 10, "a pedal's range the other way round: 90 at the heel, 10 at the toe");
            px_midi_pl_put(3, 31, KDL, PXM_MAP_VALUE, KDL, 0u, 100u);
            for (int v = 0; v <= 127; v++) {   /* (the whole range: every value as 46 had it) */
                midi_enqueue(cc(0, 31, (uint32_t)v), 35); px_midi_service();
                if (PRESET.p[PX_B_DELAY][1] != (v * 100 + 63) / 127 && PRESET.p[PX_B_DELAY][1] != (v * 100) / 127) { ok(0, "the whole range as before"); break; }
            }
            const int before = PRESET.p[PX_B_DELAY][1];
            mid_learn.hold = 31;
            midi_enqueue(cc(0, 31, 3), 36); px_midi_service();
            const int held = PRESET.p[PX_B_DELAY][1] == before;
            mid_learn.hold = 0xFFu;
            midi_enqueue(cc(0, 31, 3), 37); px_midi_service();
            ok(held && PRESET.p[PX_B_DELAY][1] != before, "a control whose range is being set moves nothing; after, it does again");
            px_midi_pl_clear();
        }
        {   /* test build 48: the pedal's own travel, taken at the heel and the toe (a pedal that sends 3 to 120): its heel gives
             * the heel's value, its toe the toe's, past either the same; reversed (120 at the heel) too */
            px_midi_pl_clear(); cur_preset = 3; load_preset(3);
            px_midi_pl_put(3, 31, KDL, PXM_MAP_VALUE, KDL, 30u, 70u);
            const int e = px_midi_pl_row(3, KDL, 0xFFu);
            mid_pl[e].clo = 3; mid_pl[e].chi = 120;
            static const uint8_t IN[5] = {3, 0, 120, 127, 62};
            int got[5];
            for (int i = 0; i < 5; i++) { midi_enqueue(cc(0, 31, IN[i]), 40 + i); px_midi_service(); got[i] = PRESET.p[PX_B_DELAY][1]; }
            ok(got[0] == 30 && got[1] == 30 && got[2] == 70 && got[3] == 70 && got[4] >= 49 && got[4] <= 51,
               "a pedal's own travel: 3 at its heel gives 30, 120 at its toe 70, past them the same, 62 halfway");
            mid_pl[e].clo = 120; mid_pl[e].chi = 3;
            midi_enqueue(cc(0, 31, 120), 46); px_midi_service();
            const int r0 = PRESET.p[PX_B_DELAY][1];
            midi_enqueue(cc(0, 31, 3), 47); px_midi_service();
            ok(r0 == 30 && PRESET.p[PX_B_DELAY][1] == 70, "a pedal's own travel the other way round: 120 at the heel, 3 at the toe");
            mid_pl[e].clo = 64; mid_pl[e].chi = 64;
            midi_enqueue(cc(0, 31, 127), 48); px_midi_service();
            ok(PRESET.p[PX_B_DELAY][1] == 70, "a pedal not moved between the heel and the toe: its whole 0..127");
            px_midi_pl_clear();
        }
        {   /* test build 48: the guided learn: a pedal is taken at its first message, the entry left to the screen; while held,
             * where it is (mid_learn.last) */
            uint32_t s2 = 0;
            px_learn_arm(3, KDL, KDL, 1, 50000u); mid_learn.pedal = 1; mid_learn.keep = 1;
            midi_enqueue(cc(0, 33, 90), 49); px_midi_service();
            const int r = px_learn_poll(50010u, &s2);
            ok(r == PXL_DONE && s2 == 33 && mid_learn.rdst == KDL && mid_learn.rmode == PXM_MAP_VALUE && mid_learn.last == 90 &&
               px_midi_pl_count(3) == 0, "guided: a pedal at its first message, VALUE on the setting, nothing put yet");
            mid_learn.hold = 33;
            const int dl = PRESET.p[PX_B_DELAY][1];
            midi_enqueue(cc(0, 33, 7), 50); px_midi_service();
            ok(mid_learn.last == 7 && PRESET.p[PX_B_DELAY][1] == dl, "guided: held, the pedal's place kept, nothing moved");
            mid_learn.hold = 0xFFu; mid_learn.pedal = 0; mid_learn.keep = 0;
        }
        px_midi_map_clear(); px_midi_pl_clear();
    }
    {   /* test build 49: CC 65 to 68 recall SCENE 1 to 4, at any value (a switch that alternates 127 and 0: each press); a
         * controller's switch learned on a scene in a preset recalls it there; not on SETTINGS' page */
        reset();
        px_midi_map_clear(); px_midi_pl_clear();
        scene_n = 0; scene_last = -1;
        midi_enqueue(cc(0, 66, 127), 1); px_midi_service();
        const int a = scene_n == 1 && scene_last == 1;
        midi_enqueue(cc(0, 68, 0), 2); px_midi_service();
        const int b = scene_n == 2 && scene_last == 3;
        px_midi_pl_put(cur_preset, 20, 65, PXM_MAP_EACH, 65, 0u, 100u);   /* (a Chocolate switch on CC 20 learned on SCENE 1) */
        const uint8_t eq = PRESET.on[PX_B_EQ];
        midi_enqueue(cc(0, 20, 127), 3); midi_enqueue(cc(0, 20, 0), 4); px_midi_service();
        const int c = scene_n == 4 && scene_last == 0 && PRESET.on[PX_B_EQ] == eq;
        ui_mode = M_MIDI;
        midi_enqueue(cc(0, 67, 127), 5); px_midi_service();
        ui_mode = M_BLOCK;
        ok(a && b && c && scene_n == 4 && px_midi_known(65) && px_midi_known(68) && !px_midi_known(69),
           "CC 65 to 68: SCENE 1 to 4 at any value, through a switch learned on one, not on SETTINGS' page");
        px_midi_pl_clear();
    }
    {   /* CC 117 is the expression pedal (test build 25): every value goes to the engine, on any page, and the preset stays */
        reset();
        px_preset_t before = PRESET; exp_n = 0; exp_last = -1;
        midi_enqueue(cc(0, 117, 0), 1); midi_enqueue(cc(0, 117, 64), 2); midi_enqueue(cc(0, 117, 127), 3);
        px_midi_service();
        int all = exp_n == 3 && exp_last == 127 && !memcmp(&before, &PRESET, sizeof before);
        ui_mode = M_TUNER;
        midi_enqueue(cc(0, 117, 30), 4); px_midi_service();
        ui_mode = M_BLOCK;
        ok(all && exp_n == 4 && exp_last == 30, "CC 117 sends every position to the expression pedal (the tuner too), the preset unchanged");
    }
    {   /* test build 41: the Chocolate by cable (SETTINGS' USB PORT on FOOTSW). chip/usb_host.c puts its packets in usb.c's ring
         * (source 1), so they act as a computer's, the controls learned on the pedal with them; nothing is sent to it; while it is
         * connected Bluetooth's musical messages are not read (a Chocolate linked both ways would act twice); lost, Bluetooth is
         * read again; back on COMPUTER the pedal's changes go out as before */
        reset();
        px_usb_foot = 1; px_uh.st = PXU_OK;
        px_midi_map_clear();
        mid_map[0] = (px_map_t){ 80, 8 + PX_B_EQ, PXM_MAP_PRESS };
        midi_enqueue(pc(0, 11), 1); px_midi_service();
        int pcok = cur_preset == 11, eq = PRESET.on[PX_B_EQ];
        midi_enqueue(cc(0, 80, 127), 2); midi_enqueue(cc(0, 80, 0), 3); px_midi_service();
        int learned = PRESET.on[PX_B_EQ] == !eq;
        steps = step_n = 0;
        midi_enqueue(cc(0, PXM_CC_NEXT, 127), 4); midi_enqueue(cc(0, PXM_CC_NEXT, 0), 5); px_midi_service();
        ok(pcok && learned && step_n == 1 && steps == 1, "FOOTSW: the cable's Program Change, a learned switch and CC 118 act as a computer's");
        cur_preset = 9; load_preset(9); PRESET.on[PX_B_DELAY] ^= 1;   /* the pedal's own changes, SEND on */
        px_midi_service();
        ok(pop_out() == 0 && mid_usb_up == 0, "FOOTSW: nothing is sent to the footswitch, and no computer is shown on SETTINGS");
        uint8_t ble_on[] = {0x80, 0x80, 0xB0, (uint8_t)(PXM_CC_ON0 + PX_B_DRIVE), 127};
        PRESET.on[PX_B_DRIVE] = 0;
        px_midi_ble_write(ble_on, sizeof ble_on); px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == 0 && ble_in_r == ble_in_w, "connected by cable: Bluetooth's Control Change is not read, its ring emptied");
        px_uh.st = PXU_LOST;
        px_midi_ble_write(ble_on, sizeof ble_on); px_midi_service();
        ok(PRESET.on[PX_B_DRIVE] == 1, "the cable's footswitch lost: Bluetooth's is read again (the spare)");
        px_usb_foot = 0; px_uh.st = PXU_OFF;
        px_midi_service(); (void)pop_out();
        cur_preset = 12; load_preset(12);
        px_midi_service();
        ok(pop_out() == pc(0, 12) && pop_out() == 0, "COMPUTER again: a preset change on the pedal is sent, as before");
        px_midi_map_clear();
    }
#endif
    printf("\n%s\n", failed ? "MIDI glue tests FAILED" : "all MIDI glue tests passed");
    return failed ? 1 : 0;
}
