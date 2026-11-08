/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the pedal's screen on the PC: firmware/ui.h + ui_text.h + fonts.h drawn into a 240x240 RGB565
 * frame buffer, exactly as the pedal streams it (window + pixels). Writes PPM screenshots of a script of actions.
 *   cc -O1 -Iengine -Ifirmware -o build/uisim tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
 *   build/uisim OUTDIR        -> OUTDIR/NN_name.ppm ; also checks that every pixel is drawn on each full page
 * The frame buffer starts (and is re-poisoned before each full page) in magenta: any pixel left magenta after a
 * full redraw is a gap in the page's layout. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "../firmware/px_midi.h"

#include "simlcd.h"
#define POISON 0xF81F

static void fm1_wdt_feed(void) { }
static void fm1_delay_ms(int ms) { (void)ms; }

static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;
static px_preset_t saved[52];
static int saved_ok[52];
static void st_get(uint32_t k, px_preset_t *p) { if (saved_ok[k]) *p = saved[k]; else px_preset_factory(p, (int)k); }
static int st_save(uint32_t k, const px_preset_t *p) { saved[k] = *p; saved_ok[k] = 1; return 0; }
static int st_reset(uint32_t k) { saved_ok[k] = 0; return 0; }
static int st_saved(uint32_t k) { return saved_ok[k]; }
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
static int a_level_db(int ch) { return ch ? -84 : -62; }
static volatile uint8_t a_tmute;
static int bat_mv = 3920, bat_usb_mv = 0;
static int32_t bat_raw[5] = {612, 1, 0, 3, 91};   /* bat_read's raw values (the tuner page shows them) */
static int bat_pct = -1;                /* (-1: the gauge from bat_mv) */

static void b_num(char *d, uint32_t v) { char t[12]; int n = 0; while (*d) d++; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = t[--n]; *d = 0; }
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static int midi_saved;
static void midi_persist(void) { midi_saved++; }
#include "ui_text.h"
#include "ui.h"

static void shot(const char *dir, const char *name)
{
    static int n;
    char p[512];
    snprintf(p, sizeof p, "%s/%02d_%s.ppm", dir, ++n, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    int holes = 0;
    for (int i = 0; i < 240 * 240; i++) holes += fb[i] == POISON;
    printf("%-28s %6lu px sent%s", name, px_sent, holes ? "" : "\n");
    if (holes) printf("   %d pixels never drawn (magenta)\n", holes);
    px_sent = 0;
}
static void poison(void) { for (int i = 0; i < 240 * 240; i++) fb[i] = POISON; }
static void status(void) { ui_status("C1 42%", 0); }
static void full(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); status(); }

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    load_preset(9);
    cur_preset = 9;
    ui_blk = PX_B_AMP;
    full(); shot(d, "play");
    ui_knob(1, 0); ui_render(); shot(d, "play_knob_next_block");
    ui_knob(-1, 0); ui_push(); poison(); ui_render(); status(); shot(d, "edit_amp_model");
    ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_selected");
    ui_push(); for (int i = 0; i < 8; i++) ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_editing");
    ui_push(); ui_home(); poison(); ui_render(); status(); shot(d, "play_edited");
    ui_enter_block(PX_B_DELAY); poison(); ui_render(); status(); shot(d, "edit_delay");
    ui_knob(-1, 0); ui_render(); shot(d, "edit_delay_onoff_selected");
    ui_enter_block(PX_B_DELAY); ui_render(); shot(d, "edit_delay_off");
    {   /* the knob walks the EDIT page top to bottom as drawn: ON/OFF (band), model, settings, and round again */
        uint32_t m = ui_mode, it = ui_item, b = ui_blk;
        ui_blk = PX_B_AMP; ui_mode = M_BLOCK; ui_item = 0;
        int n = ui_nitems();
        ui_knob(1, 0); if (ui_item != 2u) { printf("edit page: one detent down from the model must reach the first setting\n"); return 1; }
        ui_knob(-1, 0); ui_knob(-1, 0); if (ui_item != 1u) { printf("edit page: one detent up from the model must reach ON/OFF\n"); return 1; }
        ui_knob(-1, 0); if ((int)ui_item != n - 1) { printf("edit page: up from ON/OFF must wrap to the last setting\n"); return 1; }
        ui_knob(1, 0); if (ui_item != 1u) { printf("edit page: down from the last setting must wrap to ON/OFF\n"); return 1; }
        ui_knob(n, 0); if (ui_item != 1u) { printf("edit page: a whole turn of the list must come back to the same line\n"); return 1; }
        ui_knob(-(2 * n + 1), 0); if ((int)ui_item != n - 1) { printf("edit page: big turns back must stay in the list\n"); return 1; }
        ui_mode = m; ui_item = it; ui_blk = b;
    }
    ui_home(); ui_blk = PX_B_GATE; ui_mode = M_BLOCK; full(); shot(d, "edit_gate_preset_level");
    ui_home(); cur_preset = 13; load_preset(13); ui_blk = PX_B_REVERB; poison(); ui_render(); status(); shot(d, "play_smooth_lead");
    ui_move_start(); ui_knob(-1, 0); ui_render(); shot(d, "play_move");
    ui_move_cancel(); cur_preset = 23; load_preset(23); master_vol = 100; bat_mv = 3640; full(); ui_status("BYP 97%", 1); shot(d, "play_acoustic_bypass_vol100_bat12");
    bat_usb_mv = 5000; ui_render(); shot(d, "play_usb");
    bat_usb_mv = 0; bat_mv = 3920; master_vol = 80;
    save_open(); save_draw(); shot(d, "save");
    save_knob(1); save_draw(); shot(d, "save_other_slot");
    save_close(); ui_render(); status();
    tuner_open(); tuner_draw(110.5f); shot(d, "tuner_a_flat");
    {   /* v37: no battery reading: the icon shows an empty cell and "--" (it was hidden) */
        int m = bat_mv; bat_mv = 0; tuner_close(); poison(); ui_render(); status(); shot(d, "play_battery_unknown"); bat_mv = m;
        tuner_open(); tuner_draw(110.5f);
    }
    tuner_draw(110.0f); shot(d, "tuner_a_ok");
    tuner_close(); poison(); ui_render(); status(); shot(d, "play_after_tuner");
    px_midi_cfg_unpack(&mid_cfg, 0);
    midi_open(); midi_draw(); shot(d, "midi_defaults");
    midi_knob(1); midi_knob(1); midi_draw(); shot(d, "midi_channel_2");
    midi_push(); midi_knob(1); mid_usb_up = 1; mid_last[0] = PXM_CC; mid_last[1] = 25; mid_last[2] = 127; midi_draw(); shot(d, "midi_send_off_host_cc");
    midi_push(); midi_knob(1); mid_last[0] = PXM_PC; mid_last[1] = 11; midi_draw(); shot(d, "midi_clock_on_pc");
    midi_push(); poison(); uc.screen = -1; ui_invalidate(); ui_render(); status();
    if (midi_saved != 1 || ui_mode != M_CHAIN) { printf("midi: leaving the page after a change must save once and return to the chain\n"); return 1; }
    midi_open(); midi_close();
    if (midi_saved != 1) { printf("midi: leaving without a change must not save\n"); return 1; }
    mid_cfg.chan = 16; midi_open(); midi_knob(1); midi_draw(); shot(d, "midi_channel_wraps_to_omni");
    if (mid_cfg.chan != 0) { printf("midi: channel 16 + 1 must wrap to omni\n"); return 1; }
    midi_knob(-1); if (mid_cfg.chan != 16) { printf("midi: omni - 1 must wrap to 16\n"); return 1; }
    return 0;
}
