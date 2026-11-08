/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the glue between usb.c's MIDI rings and the pedal (firmware/px_midi_io.h) on a PC, with the rings, the
 * screen mode and the store stubbed: what a host's Program Change, Control Change and clock do to the sound, when the
 * pedal must ignore them, that the input ring is always emptied (usb.c stops reading the host's packets when it is
 * nearly full, and the editor's SysEx arrives in the same packets), and what the pedal sends back.
 *   cc -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/midiio tests/midiio.c engine/px_dsp.c firmware/px_math.c -lm && build/midiio */
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
static uint8_t st_midi, st_ok = 1;
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
static int ui_pad_on(void) { return PRESET.on[PX_B_SYN] && PRESET.type[PX_B_SYN] == PX_SYN_PAD; }   /* as ui.h */
static int pad_presses;
#define PX_PAD_PRESS() (pad_presses++)
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
    printf("\n%s\n", failed ? "MIDI glue tests FAILED" : "all MIDI glue tests passed");
    return failed ? 1 : 0;
}
