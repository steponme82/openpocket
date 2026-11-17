/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the pedal's screen on the PC: firmware/ui.h + ui_text.h + fonts.h drawn into a 240x240 RGB565
 * frame buffer, exactly as the pedal streams it (window + pixels). Writes PPM screenshots of a script of actions.
 *   cc -O1 -Iengine -Ifirmware -o build/uisim tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
 *   build/uisim OUTDIR        -> OUTDIR/NN_name.ppm ; also checks that every pixel is drawn on each full page
 * With -DPX_OWN_FX=1 (the Bluetooth audio image's): then SYN PAD's left footswitch and the two effects' pages too.
 * The frame buffer starts (and is re-poisoned before each full page) in magenta: any pixel left magenta after a
 * full redraw is a gap in the page's layout. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "../firmware/px_midi.h"
#if PX_OWN_FX
static int a_in_gain_n;                                  /* test build 39: INPUT applied (px_audio.h's, counted here) */
static void a_in_gain_apply(void) { a_in_gain_n++; }
static int a_tails_n;                                    /* test build 40: TAILS to the engine (px_audio.h's, counted here) */
static void a_tails_apply(void) { a_tails_n++; }
static int a_inz_n;
static void a_inz_apply(void) { a_inz_n++; }      /* (test build 46: IN-Z to the input, counted) */
#endif

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
#if PX_OWN_FX
static char uis_scn[52][PX_SC_N][PX_SCN_LEN];             /* (test build 50: the slots' scene names, as px_store.h keeps them) */
static void px_scn_load(uint32_t slot) { memcpy(px_scn, uis_scn[slot < 52u ? slot : 0u], sizeof px_scn); memcpy(px_scn_saved, px_scn, sizeof px_scn); }
static int st_save(uint32_t k, const px_preset_t *p) { saved[k] = *p; saved_ok[k] = 1; memcpy(uis_scn[k], px_scn, sizeof px_scn); return 0; }
#else
static int st_save(uint32_t k, const px_preset_t *p) { saved[k] = *p; saved_ok[k] = 1; return 0; }
#endif
static int st_reset(uint32_t k) { saved_ok[k] = 0; return 0; }
static int st_saved(uint32_t k) { return saved_ok[k]; }
#if PX_OWN_FX
static void load_preset(uint32_t i) { st_get(i, &PRESET); PX_SC_LOAD(i); }
#else
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#endif
static int a_level_db(int ch) { return ch ? -84 : -62; }
static uint8_t a_pf[PX_NBLK + 1] = {0, 0, 0, 0, 0, 0, 3, 0, 0, 5, 7, 0, 64};   /* v38: CPU per block (the tuner's footer) */
static uint32_t a_wcut = 2;                            /* v39: wet blocks cut short */
static volatile uint8_t a_tmute;
static int bat_mv = 3920, bat_usb_mv = 0;
static int32_t bat_raw[5] = {612, 1, 0, 3, 91};   /* bat_read's raw values (the tuner page shows them) */
static int bat_pct = -1;                /* (-1: the gauge from bat_mv) */

static void b_num(char *d, uint32_t v) { char t[12]; int n = 0; while (*d) d++; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = t[--n]; *d = 0; }
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static int midi_saved;
static void midi_persist(void) { midi_saved++; }
static int learn_saved;
static void learn_persist(void) { learn_saved++; }   /* (test build 46: a control learned or cleared, written) */
/* user slots for the list settings (F7): NAM slot 3 and IR slots 2 and 5 hold something, the others are empty */
int px_nam_user(int slot, char *name12, float *level_db, float *w) { (void)w; if (slot != 2) return 0; strcpy(name12, "MY AMP"); if (level_db) *level_db = 0; return 1; }
int px_ir_user(int slot, char *name12, float *h) { (void)h; if (slot != 1 && slot != 4) return 0; strcpy(name12, slot == 1 ? "V30 4X12" : "GREENBACK"); return 1; }
#if PX_OWN_FX                                           /* the Bluetooth audio image's SYN PAD: its catches and fades counted */
static int n_catch, n_fade;
#define PX_PAD_PRESS() (n_catch++)
#define PX_PAD_STOP() (n_fade++)
static int usb_apply_n;                                 /* test build 41: SETTINGS' USB PORT to the port (px_usb.h's, counted) */
static uint32_t usb_note;                               /* and the footswitch's note the host gives PLAY (px_usbhost.h pxu_note) */
#define PX_USB_MODE_APPLY() (usb_apply_n++)
#if PX_OWN_FX && OP_UAUDIO
static volatile uint8_t a_ua;                           /* test build 43: USB audio on (px_audio.h's), and the streams the USB */
static uint32_t ua_stat0;                               /* line shows (px_uaudio.h ed_ua_stat 0: 1 records, 4 plays) */
static uint32_t ed_ua_stat(uint32_t k) { return k ? 0u : ua_stat0; }
#endif
#define PX_UI_NOTE() usb_note
#endif
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
static void status(void) { ui_status("DSP 42%", 0); }
static void full(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); status(); }


/* ---- the design review's quick wins (F1, F2, F3, F4, F13): text that fits, and the state each screen shows ---- */
static int count_col(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col)
{
    int n = 0;
    for (uint32_t j = y; j < y + h; j++) for (uint32_t i = x; i < x + w; i++) n += fb[j * 240u + i] == col;
    return n;
}
static void play_again(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); }
static int quick_wins(const char *d)
{
    char c[16];
    /* F1: every load text the main loop can build fits the box it is drawn in (it used to cut "BYP 2C 150%" to "BYP 2C 15") */
    for (uint32_t l = 0; l <= 999u; l++)
        for (int m = 0; m < 8; m++) {
            int safe = m & 1, bypass = (m >> 1) & 1, late = (m >> 2) & 1;
            ui_load_text(c, l, safe, bypass, late);
            if (ft_width(&FT_SM, c) > ST_W) { printf("F1: \"%s\" is %u px, the box %u\n", c, ft_width(&FT_SM, c), ST_W); return 1; }
        }
    ui_load_text(c, 150, 0, 1, 0);
    if (strcmp(c, "PEAK 150%")) { printf("F1: a peak of 150%% reads \"%s\"\n", c); return 1; }
    ui_load_text(c, 88, 0, 0, 1);
    if (strcmp(c, "DSP 88%!")) { printf("F11: a late block reads \"%s\"\n", c); return 1; }
    if (ft_width(&FT_SM, "KNOB: SELECT  PUSH: EDIT") > ST_X - 2u) { printf("F1: the longest hint no longer fits beside the load figure\n"); return 1; }
    /* F1: overload is a red banner with an instruction, on PLAY (top bar) and on a block page (footer) */
    load_preset(9); cur_preset = 9; ui_blk = PX_B_AMP; ui_home(); play_again(); ui_status("DSP 42%", UI_ST_NORM);
    if (count_col(0, 0, 240, 3, C_ALERT)) { printf("F1: PLAY shows the banner without an overload\n"); return 1; }
    ui_status("PEAK 150%", UI_ST_BYPASS); ui_render(); shot(d, "play_overload_banner");
    if (count_col(0, 0, 240, S_H, C_ALERT) < 240 * 5 || count_col(0, F_Y + 3u, 240, 15, WHT) == 0) { printf("F1: PLAY overload: no red bar, or no instruction in the footer\n"); return 1; }
    ui_push(); ui_render(); shot(d, "edit_overload_banner");
    if (count_col(0, F_Y, 240, 18, C_ALERT) < 240 * 3 || count_col(0, F_Y + 3u, 240, 15, WHT) == 0) { printf("F1: block page overload: the footer is not red\n"); return 1; }
    ui_status("DSP 42%", UI_ST_NORM); ui_render();
    if (count_col(0, F_Y, 240, 18, C_ALERT)) { printf("F1: the footer stays red after the overload ends\n"); return 1; }
    ui_home(); play_again(); ui_status("DSP 42%", UI_ST_NORM);
    if (count_col(0, 0, 240, S_H, C_ALERT)) { printf("F1: the top bar stays red after the overload ends\n"); return 1; }
    /* F2: LOADING shows amber on the MODEL row being edited (the name greys), and amber in the corner on PLAY (the amp name greys) */
    {
        play_again(); ui_status("DSP 61%", UI_ST_NORM);
        int amp = count_col(94, P_NUM_Y + 4u, 142, 23, BLK_COL[PX_B_AMP]);
        ui_status("DSP 61%", UI_ST_LOADING); ui_render(); shot(d, "play_loading");
        if (!amp || count_col(94, P_NUM_Y + 4u, 142, 23, BLK_COL[PX_B_AMP]) || !count_col(ST_X, 3, ST_W, 15, UI_ACC)) { printf("F2: PLAY: the amp name must grey and the corner read LOADING in amber\n"); return 1; }
        ui_status("DSP 61%", UI_ST_NORM); ui_render();
        if (count_col(94, P_NUM_Y + 4u, 142, 23, BLK_COL[PX_B_AMP]) != amp) { printf("F2: PLAY: the amp name must come back when the model plays\n"); return 1; }
        ui_push(); poison(); ui_render(); ui_status("DSP 61%", UI_ST_NORM);
        int row = count_col(BAR_X, E_P0 + 5u, 130, 15, UI_ACC);
        ui_status("DSP 61%", UI_ST_LOADING); ui_render(); shot(d, "edit_model_loading");
        if (count_col(BAR_X, E_P0 + 5u, 130, 15, UI_ACC) <= row || count_col(140, E_P0 + 1u, 90, 23, UI_TXT)) { printf("F2: block page: the MODEL row must read LOADING in amber, the name in grey\n"); return 1; }
        if (count_col(ST_X, F_Y + 3u, ST_W, 15, UI_ACC)) { printf("F2: block page: the corner keeps the load figure\n"); return 1; }
        ui_status("DSP 61%", UI_ST_NORM); ui_render();
        if (count_col(BAR_X, E_P0 + 5u, 130, 15, UI_ACC) != row) { printf("F2: block page: the MODEL row must come back\n"); return 1; }
        ui_home(); play_again(); ui_status("DSP 42%", UI_ST_NORM);
    }
    /* F3: a footswitch on a block page goes back to PLAY (on the block it was on); the band carries the preset number */
    ui_blk = PX_B_AMP; ui_mode = M_EDIT; ui_item = 2;
    ui_preset_step(1);
    if (ui_mode != M_CHAIN || ui_item != 0 || ui_blk != PX_B_AMP || cur_preset != 10) { printf("F3: a footswitch on a block page must return to PLAY\n"); return 1; }
    ui_preset_step(-1);
    if (cur_preset != 9) { printf("F3: the footswitch must still change the preset\n"); return 1; }
    ui_enter_block(PX_B_AMP); poison(); uc.screen = -1; ui_invalidate(); ui_render();
    int band = count_col(8, 6, 24, 19, BLK);
    cur_preset = 11; load_preset(11); ui_render(); shot(d, "edit_band_after_midi_change");   /* (a Program Change keeps the page) */
    if (!band || count_col(8, 6, 24, 19, BLK) == band) { printf("F3: the band must show the new preset number after a MIDI change\n"); return 1; }
    ui_home(); cur_preset = 9; load_preset(9);
    /* F4: while EDITED shows, the footer names SAVE */
    play_again(); ui_status("DSP 42%", UI_ST_NORM);
    if (count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: SAVE is named without an edit\n"); return 1; }
    PRESET.p[PX_B_AMP][1] ^= 7u; ui_render(); shot(d, "play_edited_save_hint");
    if (!count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: EDITED shows, but the footer does not name SAVE\n"); return 1; }
    load_preset(9); ui_render();
    if (count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: the SAVE hint stays after the edit is gone\n"); return 1; }
    /* F13: the label of an off tile reads (contrast of at least 2.5 against the tile; the old one was 1.5 to 3.2) */
    for (int b = 0; b < PX_NBLK; b++) {
        uint16_t fg = ft_mix(BLK_COL[b], C_OFF, 11);
        if (fg == C_OFF || fg == BLK_COL[b]) { printf("F13: block %d: an off label must differ from its tile and from an on tile\n", b); return 1; }
    }
    return 0;
}

/* ---- the full overhaul (v42): the design review's F4 to F17, through ui.h's own controls (ui_buttons, ui_turn, ui_action) ---- */
static uint32_t T = 100000u;                            /* the simulated clock (ms); ui_buttons runs every 5 ms as the main loop */
static void hold(uint32_t mask, uint32_t ms) { for (uint32_t t = 0; t < ms; t += 5u) { ui_buttons(T, mask); T += 5u; } }
static void tap(int b) { hold(1u << b, 40); hold(0, 5); }
#define CHECK(c, m) do { if (!(c)) { printf("%s\n", m); return 1; } } while (0)
/* the diagnostics as pictures (v42): each figure beside its graphic, colours by state, two pages turned by the knob */
static int px_at(uint32_t x, uint32_t y, uint16_t c) { return fb[y * 240u + x] == c; }
static int diagnostics(const char *d)
{
    char b[48];
    ui_dg.load = 61; ui_dg.peak = 70; ui_dg.cores = 2; ui_dg.split = 1; ui_dg.guard = 93; ui_dg.cl[0] = 52; ui_dg.cl[1] = 56;
    ui_dg.late = 0; ui_dg.bypass = 0;
    diag_draw(); shot(d, "diagnostics");
    CHECK(dg_page == 0 && count_col(10, 60, 220, 6, GRN) && !count_col(10, 60, 220, 6, UI_ACC), "diag: a load of 61 % is a green bar");
    CHECK(px_at(10 + dg_w(93, 100, 220) - 1, 58, WHT) && px_at(10 + dg_w(93, 100, 220) - 1, 67, WHT), "diag: the guard's tick at 93 %");
    CHECK(count_col(88, 93, 144, 6, GRN) && count_col(88, 109, 144, 6, GRN), "diag: a bar for each core");
    {
        uint8_t top[DG_BLK];
        int n = dg_top(top, DG_BLK);
        CHECK(n == 4 && top[0] == PX_NBLK && top[1] == PX_B_REVERB && top[2] == PX_B_DELAY && top[3] == PX_B_CAB, "diag: CPU per block, costliest first");
        CHECK(count_col(52, 165, 148, 5, UI_ACC) && !count_col(52, 180, 148, 5, UI_ACC), "diag: the heaviest block highlighted, the others not");
    }
    CHECK(count_col(2, 122, 76, 18, GRN) && !count_col(2, 122, 76, 18, RED), "diag: no late block: a green chip");
    ui_dg.load = 86; diag_draw();
    CHECK(count_col(10, 60, 220, 6, UI_ACC) && !count_col(10, 60, 220, 6, GRN), "diag: above 80 % the load turns amber");
    ui_dg.late = 1; bat_raw[2] = 3; diag_draw(); shot(d, "diagnostics_late");
    CHECK(count_col(10, 60, 220, 6, RED) && count_col(2, 122, 76, 18, RED), "diag: a late block: the load and its chip in red");
    ui_dg.late = 0; bat_raw[2] = 0; ui_dg.load = 61; ui_dg.split = 0; diag_draw();
    CHECK(!count_col(88, 93, 144, 6, GRN), "diag: no layer split: no core bars (CPU0 --)");
    ui_dg.split = 1;
    /* every figure fits its box at its widest */
    dg_pct(b, "DSP ", 999); CHECK(ft_width(&FT_MD, b) <= 120u, "diag: DSP 999% fits");
    dg_pct(b, "PEAK ", 999); CHECK(ft_width(&FT_SM, b) <= 104u, "diag: PEAK 999% fits");
    CHECK(ft_width(&FT_SM, "CPU1 100%") <= 78u && ft_width(&FT_SM, "LATE 9999") <= 70u && ft_width(&FT_SM, "CLIP 9999") <= 70u
          && ft_width(&FT_SM, "BYPASS OFF") <= 76u && ft_width(&FT_SM, "PK 999%") <= 64u && ft_width(&FT_SM, "CORES 2C") <= 80u
          && ft_width(&FT_SM, "GUARD 93%") <= 80u, "diag: the chips and core figures fit");
    CHECK(ft_width(&FT_SM, "SOFT CLIP: 99999 BLOCKS") <= 204u && ft_width(&FT_SM, "RAW  C9 4095  CH 1") <= DG_W
          && ft_width(&FT_SM, "R -199 DB") <= 74u && ft_width(&FT_SM, "CUT 9999") <= 84u, "diag: page 2's figures fit");
    /* the knob turns the page; both pages are drawn whole */
    poison(); ui_turn(1, 0); CHECK(dg_page == 1, "diag: the knob turns to page 2");
    diag_draw(); shot(d, "diagnostics_2");
    CHECK(count_col(82, 58, 150, 8, GRN) && count_col(82, 58, 150, 8, ui_dark(RED)), "diag: an input meter, green, with its clip zone");
    CHECK(!px_at(DG_X, 112, RED), "diag: no recent soft clip: the marker is dark");
    bat_raw[3]++; diag_draw(); CHECK(px_at(DG_X + 2, 114, RED), "diag: a new soft clip lights the marker");
    for (int i = 0; i < 25; i++) diag_draw();
    CHECK(!px_at(DG_X + 2, 114, RED), "diag: the marker goes dark again");
    CHECK(count_col(DG_X + 3, 159, 40, 16, GRN), "diag: the battery's cell is filled");
    poison(); ui_turn(-1, 0); diag_draw(); shot(d, "diagnostics_1_again");
    CHECK(dg_page == 0, "diag: and back to page 1");
    return 0;
}
static int overhaul(const char *d)
{
    char c[16];
    load_preset(9); cur_preset = 9; ui_blk = PX_B_AMP; ui_mode = M_CHAIN; ui_item = 0; full();
    /* F6: a click of PUSH on PLAY opens the block at its release, no wait; a 2nd one within 350 ms makes it MOVE */
    hold(1u << UB_PUSH, 60); CHECK(ui_mode == M_CHAIN, "F6: PUSH acts at its release");
    hold(0, 5); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_AMP, "F6: one click opens the block at once");
    hold(1u << UB_PUSH, 60); hold(0, 5); CHECK(ui_mode == M_MOVE && ui_blk == PX_B_AMP, "F6: a 2nd click within 350 ms: MOVE");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "MOVE: HOME puts the block back");
    tap(UB_PUSH); hold(0, 400); tap(UB_PUSH); CHECK(ui_mode == M_EDIT, "F6: a 2nd click after 350 ms edits the model, as on any block page");
    tap(UB_HOME);
    tap(UB_PUSH); ui_turn(1, 0); ui_turn(-1, 0); tap(UB_PUSH); CHECK(ui_mode == M_EDIT, "F6: the knob between the clicks: no MOVE");
    tap(UB_HOME);
    hold(1u << UB_PUSH, 850); CHECK(ui_mode == M_SAVE, "hold PUSH: SAVE");
    hold(0, 5); CHECK(ui_mode == M_SAVE, "the release of that hold stays on SAVE");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "HOME leaves SAVE");
    hold(1u << UB_FX, 850); hold(0, 5); CHECK(ui_mode == M_MIDI, "hold FX: MIDI");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "HOME leaves MIDI");
    /* F5: both footswitches: the first acts at once, the other within 150 ms takes that step back and opens the tuner */
    hold(1u << UB_FSUP, 60); CHECK(cur_preset == 10, "F5: the first footswitch acts at its press");
    poison();
    hold(1u << UB_FSUP | 1u << UB_FSDN, 60);
    CHECK(ui_mode == M_TUNER && cur_preset == 9 && a_tmute, "F5: both footswitches: the step back, the tuner, muted");
    tuner_draw(108.0f); shot(d, "tuner_flat");
    CHECK(count_col(10, 76, 46, 23, UI_ACC) && !count_col(184, 76, 46, 23, UI_ACC), "F12: flat: an arrow on the left, toward in tune");
    CHECK(count_col(0, 30, 240, 30, UI_BG) == 240 * 30 && count_col(0, 206, 240, 16, UI_BG) == 240 * 16, "F12: no diagnostics on the tuner page");
    tuner_draw(112.5f); CHECK(!count_col(10, 76, 46, 23, UI_ACC) && count_col(184, 76, 46, 23, UI_ACC), "F12: sharp: the arrow on the right");
    tuner_draw(110.0f); CHECK(!count_col(10, 76, 46, 23, UI_ACC) && !count_col(184, 76, 46, 23, UI_ACC), "F12: in tune: no arrow");
    hold(0, 300);
    hold(1u << UB_FSDN, 40); hold(1u << UB_FSDN | 1u << UB_FSUP, 40); hold(0, 300);
    CHECK(ui_mode == M_CHAIN && cur_preset == 9 && !a_tmute, "F5: both footswitches leave the tuner, the preset as it was");
    tap(UB_FSUP); hold(0, 200); tap(UB_FSDN); hold(0, 200);
    CHECK(ui_mode == M_CHAIN && cur_preset == 9, "F5: footswitches 200 ms apart are two presses");
    tap(UB_FSUP); tap(UB_FSDN); hold(0, 200);
    CHECK(ui_mode == M_CHAIN && cur_preset == 9, "F5: one footswitch released before the other goes down: two presses");
    /* F12: hold HOME: the tuner; held on: the diagnostics (the photo the release notes ask for), with the sound on */
    hold(1u << UB_HOME, 750); CHECK(ui_mode == M_TUNER, "hold HOME 0.7 s: the tuner");
    poison();
    hold(1u << UB_HOME, 700); CHECK(ui_mode == M_DIAG && !a_tmute, "F12: HOME held on: the diagnostics, the sound on");
    hold(0, 5); CHECK(ui_mode == M_DIAG, "the release leaves the diagnostics open");
    if (diagnostics(d)) return 1;
    tap(UB_FX); CHECK(ui_mode == M_CHAIN, "any control leaves the diagnostics");
    full();
    /* F11: the load figure in plain words, amber when high, red when a block ran late */
    CHECK(ui_load_kind(85, 0, 0, 0, 0) == UI_ST_HIGH && ui_load_kind(80, 0, 0, 0, 0) == UI_ST_NORM && ui_load_kind(85, 1, 0, 0, 0) == UI_ST_NORM
          && ui_load_kind(85, 0, 0, 1, 0) == UI_ST_LATE && ui_load_kind(85, 0, 1, 1, 0) == UI_ST_BYPASS && ui_load_kind(5, 0, 1, 1, 1) == UI_ST_LOADING,
          "F11: the load's kind");
    ui_load_text(c, 42, 0, 0, 0); CHECK(!strcmp(c, "DSP 42%"), "F11: the load reads DSP 42%");
    ui_status("DSP 85%", UI_ST_HIGH); ui_render(); shot(d, "play_dsp_high");
    CHECK(count_col(ST_X, 3, ST_W, 15, UI_ACC) && !count_col(ST_X, 3, ST_W, 15, RED), "F11: a high load in amber");
    ui_status("DSP 91%!", UI_ST_LATE); ui_render(); CHECK(count_col(ST_X, 3, ST_W, 15, RED), "F11: a late block in red");
    ui_status("DSP 42%", UI_ST_NORM); ui_render();
    /* F8: HOME on PLAY: the preset list; the knob browses without loading, PUSH loads, HOME goes back */
    poison(); tap(UB_HOME); CHECK(ui_mode == M_LIST, "F8: HOME on PLAY: the preset list"); ui_render(); shot(d, "preset_list");
    for (int i = 0; i < 3; i++) ui_turn(1, 0);
    ui_render(); shot(d, "preset_list_browsed");
    CHECK(cur_preset == 9 && ls_at == 12, "F8: the knob browses without loading");
    tap(UB_PUSH); CHECK(cur_preset == 12 && ui_mode == M_CHAIN, "F8: PUSH loads the preset shown and returns to PLAY");
    tap(UB_HOME); ui_turn(-1, 0); tap(UB_HOME); CHECK(ui_mode == M_CHAIN && cur_preset == 12, "F8: HOME leaves the list without loading");
    tap(UB_HOME); tap(UB_FSUP); CHECK(ui_mode == M_CHAIN && cur_preset == 13, "F8: a footswitch on the list steps and shows PLAY");
    tap(UB_HOME); tap(UB_AMP); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_AMP && PRESET.on[PX_B_AMP], "F8: a block button on the list opens it, without switching it");
    ui_home(); full();
    /* F4: one unsaved edit survives a preset change and comes back with its slot */
    load_preset(9); cur_preset = 9; full();
    PRESET.p[PX_B_AMP][1] ^= 7u;
    uint8_t ed = PRESET.p[PX_B_AMP][1];
    ui_preset_step(1); CHECK(cur_preset == 10 && !ui_dirty(), "F4: the next preset plays as saved");
    ui_preset_step(-1); ui_render(); shot(d, "play_edit_kept");
    CHECK(ui_dirty() && PRESET.p[PX_B_AMP][1] == ed, "F4: back on the edited preset: the edit is back, EDITED");
    ui_preset_step(1); ui_preset_step(1); ui_preset_step(-1); ui_preset_step(-1);
    CHECK(ui_dirty() && PRESET.p[PX_B_AMP][1] == ed, "F4: the edit waits while other presets play");
    ui_preset_step(1); PRESET.p[PX_B_AMP][2] ^= 3u; ui_preset_step(-1);
    CHECK(!ui_dirty(), "F4: an edit elsewhere takes the place of the first");
    ui_preset_step(1); CHECK(ui_dirty(), "F4: that edit is kept in turn");
    load_preset(10); PRESET.p[PX_B_AMP][1] ^= 5u; { px_preset_t q = PRESET; ui_preset_step(1); st_save(10, &q); st_gen++; } ui_preset_step(-1);
    CHECK(!ui_dirty() && st_saved(10), "F4: a slot saved over meanwhile: the kept edit is dropped");
    st_reset(10); st_gen++; load_preset(9); cur_preset = 9;
    /* F7: a list setting steps one filled item a detent, fast or not, skips empty slots, stops at the last; it shows its place */
    PRESET.type[PX_B_AMP] = PX_AMP_NAM; PRESET.on[PX_B_AMP] = 1; PRESET.p[PX_B_AMP][0] = (uint8_t)px_nam_knob(0);
    ui_blk = PX_B_AMP; ui_mode = M_EDIT; ui_item = 2; full();
    for (int i = 1; i < PX_NAM_NFACT; i++) { ui_turn(1, i & 1); CHECK(px_nam_model(PRESET.p[PX_B_AMP][0]) == i, "F7: one model a detent, fast or not"); }
    ui_turn(1, 0); CHECK(px_nam_model(PRESET.p[PX_B_AMP][0]) == PX_NAM_NFACT + 2, "F7: the empty user slots are skipped");
    ui_turn(1, 0); CHECK(px_nam_model(PRESET.p[PX_B_AMP][0]) == PX_NAM_NFACT + 2, "F7: it stops at the last filled one");
    ui_render(); shot(d, "edit_nam_model_list");
    { int n, at = ui_list_place(PX_B_AMP, PX_NAM_N, PX_NAM_NFACT + 2, &n); CHECK(n == 13 && at == 13, "F7: its place: 13 of 13"); }
    for (int i = 0; i < 40; i++) ui_turn(-1, 1);
    CHECK(px_nam_model(PRESET.p[PX_B_AMP][0]) == 0, "F7: back to the first model, and no further");
    PRESET.type[PX_B_CAB] = PX_CAB_IR; PRESET.on[PX_B_CAB] = 1; PRESET.p[PX_B_CAB][0] = (uint8_t)px_ir_knob(1); PRESET.p[PX_B_CAB][1] = 30; PRESET.p[PX_B_CAB][2] = 0;
    ui_blk = PX_B_CAB; ui_mode = M_EDIT; ui_item = 2; full(); shot(d, "edit_user_ir_list");
    ui_turn(1, 0); CHECK(px_ir_slot(PRESET.p[PX_B_CAB][0]) == 4, "F7: USER IR: the next filled slot");
    ui_turn(1, 0); CHECK(px_ir_slot(PRESET.p[PX_B_CAB][0]) == 4, "F7: USER IR: stops at the last filled slot");
    {   /* F17: the cut frequencies with their unit */
        char b[16];
        strcpy(b, "89"); ui_hz_text(b, 0); CHECK(!strcmp(b, "89 HZ"), "F17: Hz");
        strcpy(b, "6325"); ui_hz_text(b, 0); CHECK(!strcmp(b, "6.3 KHZ"), "F17: kHz");
        strcpy(b, "20000"); ui_hz_text(b, 1); CHECK(!strcmp(b, "OFF"), "F17: HIGH CUT at 0 is off");
        for (int v = 0; v <= 100; v++)
            for (int k = 1; k <= 2; k++) {
                px_param_text(PX_B_CAB, PX_CAB_IR, k, v, b); ui_hz_text(b, k == 2 && v == 0);
                CHECK(ft_width(&FT_MD, b) <= 90u, "F17: a cut frequency does not fit its box");
            }
    }
    /* F14: on PLAY, the line under the tiles names a NAM amp's model and a user IR */
    ui_home(); ui_blk = PX_B_AMP; full();
    int m0 = count_col(62, P_INFO_Y, 132, 23, UI_TXT);
    PRESET.p[PX_B_AMP][0] = (uint8_t)px_nam_knob(5); ui_render(); shot(d, "play_info_nam_model");
    CHECK(count_col(62, P_INFO_Y, 132, 23, UI_TXT) != m0, "F14: the line under the tiles follows the NAM model");
    /* F9: the block page's selection goes in the order drawn: ON at the top, MODEL, the settings */
    ui_blk = PX_B_AMP; ui_mode = M_BLOCK; ui_item = 0;
    ui_turn(-1, 0); CHECK(ui_item == 1u, "F9: back from MODEL: ON");
    ui_turn(1, 0); CHECK(ui_item == 0u, "F9: on from ON: MODEL");
    ui_turn(1, 0); CHECK(ui_item == 2u, "F9: on from MODEL: the first setting");
    ui_item = 1; ui_turn(-1, 0); CHECK((int)ui_item == ui_nitems() - 1, "F9: back from ON: the last setting");
    ui_turn(1, 0); CHECK(ui_item == 1u, "F9: on from the last setting: ON");
    /* F10: FX steps through its blocks in the preset's chain order, as the tiles read */
    load_preset(9); cur_preset = 9; ui_home();
    for (int i = 0, last = -1; i < 7; i++) {
        tap(UB_FX);
        int pos = ui_pos_of(ui_blk);
        CHECK(ui_mode == M_BLOCK && ui_is_fx(ui_blk) && pos > last, "F10: FX goes on along the chain");
        last = pos;
    }
    tap(UB_FX); CHECK(ui_blk == PRESET.order[0] || ui_pos_of(ui_blk) < 3, "F10: after the last, FX starts again from the first");
    /* F17: SYN's page is SYNTH; a block of one model shows no arrows */
    CHECK(!strcmp(ui_block_name(PX_B_SYN), "SYNTH"), "F17: SYN's page is SYNTH");
    ui_blk = PX_B_GATE; ui_mode = M_BLOCK; ui_item = 0; full(); shot(d, "edit_gate_no_arrows");
    CHECK(px_ntypes(PX_B_GATE) == 1 && !count_col(8, E_TYPE_Y, 20, 23, UI_ACC), "F17: NOISE GATE: no arrows");
    /* F15, F16: SAVE says a copy keeps its name; RESTORE FACTORY says RESTORE and does nothing on a factory sound */
    ui_home(); load_preset(9); cur_preset = 9; full();
    poison(); save_open(); save_knob(1); save_draw(); shot(d, "save_copy_keeps_name");
    CHECK(count_col(0, 186, 240, 15, UI_DIM), "F15: SAVE to another slot says the name stays");
    while (sv_dest != SV_FACTORY) save_knob(-1);
    save_draw(); shot(d, "save_restore_nothing");
    CHECK(sv_dest == SV_FACTORY && !st_saved(9), "F16: RESTORE FACTORY");
    tap(UB_PUSH); CHECK(ui_mode == M_SAVE && !st_saved(9), "F16: PUSH does nothing when there is nothing to restore");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "HOME leaves SAVE");
    full();
    return 0;
}

#if PX_OWN_FX
/* ---- the Bluetooth audio image's own effects (PX_OWN_FX): SYN PAD's left footswitch against v42's controls (ui_buttons
 * as the main loop runs it, every 5 ms), and the two effects' pages ---- */
static void pad_start(int on)
{
    cur_preset = 9; load_preset(9); ui_kept_slot = ~0u; ui_mode = M_CHAIN; ui_item = 0; ui_blk = PX_B_AMP; a_tmute = 0;
    PRESET.type[PX_B_SYN] = PX_SYN_PAD; PRESET.on[PX_B_SYN] = (uint8_t)on;
    n_catch = n_fade = 0; hold(0, 400);
}
static int pad_gestures(const char *d)
{
    const uint32_t FSL = UB_FSDN, FSR = UB_FSUP;
    px_preset_t was;
    pad_start(0); tap(FSL); CHECK(cur_preset == 8 && n_catch == 0, "PAD off: the left footswitch steps back, as on v42");
    pad_start(0); hold(1u << FSL, 40); hold(1u << FSL | 1u << FSR, 40); hold(0, 300);
    CHECK(ui_mode == M_TUNER && cur_preset == 9 && a_tmute, "PAD off: left then right within 150 ms: the tuner, the preset as it was");
    tuner_close();
    pad_start(0); hold(1u << FSR, 40); hold(1u << FSR | 1u << FSL, 40); hold(0, 300);
    CHECK(ui_mode == M_TUNER && cur_preset == 9, "PAD off: right then left: the tuner, the preset as it was");
    tuner_close();
    pad_start(1); was = PRESET; tap(FSL);
    CHECK(n_catch == 1 && n_fade == 0 && cur_preset == 9 && !memcmp(&was, &PRESET, sizeof was) && ui_mode == M_CHAIN,
          "PAD on: the left footswitch catches the chord; preset, sound and page unchanged");
    pad_start(1); ui_mode = M_BLOCK; ui_blk = PX_B_SYN; ui_item = 2; tap(FSL);
    CHECK(n_catch == 1 && ui_mode == M_BLOCK && ui_blk == PX_B_SYN && ui_item == 2, "PAD on, on the SYN page: a catch leaves the page as it is");
    pad_start(1); tap(FSR); CHECK(cur_preset == 10 && n_catch == 0, "PAD on: the right footswitch still steps on");
    pad_start(1); hold(1u << FSL, 40); hold(1u << FSL | 1u << FSR, 40); hold(0, 300);
    CHECK(ui_mode == M_TUNER && a_tmute && cur_preset == 9 && n_catch == 1 && n_fade == 1,
          "PAD on: left then right within 150 ms: the tuner, the pad let go, the preset as it was");
    hold(1u << FSL, 40); hold(0, 300);
    CHECK(ui_mode == M_CHAIN && n_catch == 1 && !a_tmute, "PAD on: in the tuner the left footswitch only leaves it (no catch)");
    pad_start(1); hold(1u << FSR, 40); hold(1u << FSR | 1u << FSL, 40); hold(0, 300);
    CHECK(ui_mode == M_TUNER && cur_preset == 9 && n_catch == 0 && n_fade == 0, "PAD on: right then left: the step back and the tuner, no catch");
    tap(FSR); hold(0, 300);
    pad_start(1); hold(1u << FSL, 750); hold(0, 50);
    CHECK(n_catch == 1 && n_fade == 1 && cur_preset == 9 && ui_mode == M_CHAIN, "PAD on: the left held 0.7 s: caught, then faded; once");
    pad_start(1); hold(1u << FSL, 400); hold(0, 600);
    CHECK(n_catch == 1 && n_fade == 0, "PAD on: held 0.4 s: no fade");
    pad_start(1); hold(1u << FSL, 300); hold(1u << FSL | 1u << FSR, 600); hold(0, 300);
    CHECK(n_catch == 1 && n_fade == 0 && cur_preset == 10 && ui_mode == M_CHAIN,
          "PAD on: right pressed 0.3 s into a left hold: a step on, and the hold no longer fades");
    pad_start(1); hold(1u << UB_PUSH, 850); hold(0, 5); tap(FSL);
    CHECK(ui_mode == M_SAVE && n_catch == 0, "PAD on: on SAVE the left footswitch does nothing, as on v42");
    tap(UB_HOME);
    pad_start(1); tap(UB_HOME); tap(FSL);
    CHECK(ui_mode == M_LIST && n_catch == 1 && cur_preset == 9, "PAD on: on the preset list a catch, and the list stays");
    tap(UB_HOME);
    pad_start(1); PRESET.type[PX_B_SYN] = PX_SYN_STRINGS; tap(FSL);
    CHECK(cur_preset == 8 && n_catch == 0, "another SYN model on: the left footswitch steps back");
    pad_start(1); tap(FSL); hold(0, 200); tap(FSL); hold(0, 200);
    CHECK(n_catch == 2 && cur_preset == 9, "PAD on: two presses 0.2 s apart: two catches (the next chord)");
    {   /* every value text of PAD's, LOW OCTAVE's and TREBLE BOOST's settings fits the EDIT page's value box (FT_MD, 90 px) */
        char b[16]; uint32_t wmax = 0; int bad = 0;
        static const int BT[3][2] = {{PX_B_SYN, PX_SYN_PAD}, {PX_B_PITCH, PX_PIT_LOWOCT}, {PX_B_DRIVE, PX_DRV_TREBLE}};
        for (int i = 0; i < 3; i++) for (int k = 0; k < 5; k++) for (int v = 0; v <= 100; v++) {
            if (!px_param_text(BT[i][0], BT[i][1], k, v, b)) continue;
            uint32_t w = ft_width(&FT_MD, b); if (w > wmax) wmax = w; if (w > 90u) { bad++; if (bad < 4) printf("too wide: %s (%u px)\n", b, w); }
        }
        CHECK(!bad && wmax > 0, "PAD's, LOW OCTAVE's and TREBLE BOOST's value texts fit the EDIT page's box");
    }
    /* their pages, drawn whole: the model, the settings in plain values */
    pad_start(1); ui_blk = PX_B_SYN; ui_mode = M_BLOCK; ui_item = 0; full(); shot(d, "edit_syn_pad");
    load_preset(9); PRESET.on[PX_B_PITCH] = 1; PRESET.type[PX_B_PITCH] = PX_PIT_LOWOCT;
    { static const uint8_t K[5] = {40, 70, 100, 33, 50}; memcpy(PRESET.p[PX_B_PITCH], K, 5); }
    ui_blk = PX_B_PITCH; ui_mode = M_BLOCK; ui_item = 0; full(); shot(d, "edit_pitch_low_octave");
    load_preset(9); PRESET.on[PX_B_DRIVE] = 1; PRESET.type[PX_B_DRIVE] = PX_DRV_TREBLE;   /* test build 40 */
    { static const uint8_t K[5] = {75, 100, 50, 0, 0}; memcpy(PRESET.p[PX_B_DRIVE], K, 5); }
    ui_blk = PX_B_DRIVE; ui_mode = M_BLOCK; ui_item = 0; full(); shot(d, "edit_drive_treble_boost");
    ui_home(); load_preset(9); full();
    return 0;
}
/* test build 46: controls learned live, a preset's own (ui.h ui_learn_*, px_midi.h mid_learn / mid_pl): hold FX on a block
 * page's ON row or a setting arms the learn, a controller's control (px_learn_feed, as px_midi_io.h gives it) is taken, 1.2 s
 * later it is in and written (learn_persist); the footer says each step in the hint's box, the load figure kept; held again
 * on that row it clears it; the MODEL row has none; any other control stops a learn; nothing heard in 10 s: given up; on
 * PLAY, hold FX still opens SETTINGS */
static int footer_has(uint16_t col) { return count_col(0, F_Y + 3u, ST_X - 2u, 15, col) > 0; }
static int learn_live(const char *d)
{
    static const char *const T[] = {"LEARNED: CC 127", "CC 127 CLEARED", "LEARN: 64 IN USE, FULL", "LEARN: CANCELLED",
                                    "64 LEARNED  HOLD FX", "CC 127  HOLD FX: CLEAR"};
    for (uint32_t i = 0; i < sizeof T / sizeof T[0]; i++) CHECK(ft_width(&FT_SM, T[i]) <= ST_X - 2u, "a learn's line fits a block page's hint box");
    {   /* test build 48: the guided page's texts fit their boxes */
        static const char *const F[] = {"KNOB: CHOOSE    PUSH: NEXT", "KNOB: VALUE    PUSH: NEXT", "KNOB: VALUE    PUSH: DONE"};
        static const char *const R[] = {"1  PEDAL OR SWITCH", "2  PRESS THE SWITCH", "3  VALUE AT THE HEEL", "4  VALUE AT THE TOE"};
        static const char *const B[] = {"WHAT WILL YOU USE?", "PRESS THE SWITCH", "PEDAL AT THE HEEL", "PEDAL AT THE TOE"};
        int fit = ft_width(&FT_SM, "HOME: CANCEL") <= 128u && ft_width(&FT_NM, "SWITCH") <= 240u && ft_width(&FT_NM, "-14.7 DB") <= 240u;
        for (int i = 0; i < 3; i++) fit &= ft_width(&FT_SM, F[i]) <= 240u;
        for (int i = 0; i < 4; i++) fit &= ft_width(&FT_SM, R[i]) <= 150u && ft_width(&FT_MD, B[i]) <= 240u;
        CHECK(fit, "learn: the guided page's texts fit");
    }
    px_midi_map_clear(); px_midi_pl_clear(); px_learn_cancel(); learn_saved = 0; usb_note = 0;
    ui_home(); load_preset(9); cur_preset = 9; hold(0, 3000);
    tap(UB_AMP); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_AMP && ui_item == 0u, "learn: the AMP page, MODEL selected");
    ui_turn(-1, 0); CHECK(ui_item == 1u, "learn: the knob up to ON");
    full();
    /* a switch on ON: the guided page, SWITCH to begin with */
    hold(1u << UB_FX, 850); hold(0, 10); ui_render(); shot(d, "learn_guide_type");
    CHECK(ui_mode == M_GUIDE && lg.step == LG_TYPE && lg.sw && !mid_learn.on, "learn: hold FX on ON opens the guided learn, SWITCH to begin with");
    tap(UB_FX); tap(UB_DLY); tap(UB_FSUP);
    CHECK(ui_mode == M_GUIDE && lg.step == LG_TYPE && cur_preset == 9 && ui_blk == PX_B_AMP, "learn: FX, a block's button, a footswitch do nothing on the page");
    tap(UB_PUSH); hold(0, 10); ui_render(); shot(d, "learn_guide_press_switch");
    CHECK(lg.step == LG_LISTEN && mid_learn.on && !mid_learn.pedal && mid_learn.keep, "learn: PUSH: PRESS THE SWITCH, listening");
    const int amp = PRESET.on[PX_B_AMP];
    CHECK(px_learn_feed(3, 127) && px_learn_feed(3, 0), "learn: the controller's press and release taken");
    hold(0, 1300); ui_render(); shot(d, "edit_learn_learned");
    int e = px_midi_pl_row(9, PXM_CC_TOGGLE0 + PX_B_AMP, PXM_CC_ON0 + PX_B_AMP);
    CHECK(ui_mode == M_BLOCK && ui_item == 1u && learn_saved == 1 && e >= 0 && mid_pl[e].src == 3 && mid_pl[e].mode == PXM_MAP_PRESS &&
          footer_has(GRN) && PRESET.on[PX_B_AMP] == amp, "learn: in after 1.2 s (a momentary switch: PRESS), written, back on the page, LEARNED in green, nothing played");
    hold(0, 2600); ui_render(); shot(d, "edit_learn_row_has_one");
    CHECK(footer_has(UI_ACC) && !footer_has(GRN), "learn: the note gone, the selected row says its number");
    hold(1u << UB_FX, 850); hold(0, 10); ui_render(); shot(d, "edit_learn_cleared");
    CHECK(ui_mode == M_BLOCK && learn_saved == 2 && px_midi_pl_count(9) == 0 && footer_has(UI_ACC), "learn: held again on that row: cleared and written");
    hold(0, 2600);
    ui_turn(1, 0); ui_turn(1, 0); ui_turn(1, 0); CHECK(ui_item == 3u, "learn: the knob down to GAIN (after the NAM amp's MODEL list)");
    /* a pedal on a setting: PEDAL to begin with; its heel and toe with the knob, heard, the pedal there */
    hold(1u << UB_FX, 850); hold(0, 10);
    CHECK(ui_mode == M_GUIDE && !lg.sw && lg.knob, "learn: on a setting, PEDAL to begin with");
    tap(UB_PUSH);
    CHECK(mid_learn.on && mid_learn.pedal && mid_learn.dval == px_cc_param(PX_B_AMP, 1), "learn: PUSH: MOVE THE PEDAL, listening");
    {
        const int g0 = PRESET.p[PX_B_AMP][1], heel = g0 >= 10 ? g0 - 10 : 0, toe = g0 <= 98 ? g0 + 2 : 100;
        px_learn_feed(4, 70); hold(0, 10);
        CHECK(lg.step == LG_LO && lg.src == 4 && mid_learn.hold == 4 && px_midi_pl_count(9) == 0 && learn_saved == 2,
              "learn: the pedal heard at its first message, nothing in the table yet");
        CHECK(px_learn_feed(4, 3), "learn: the pedal held off meanwhile (here at its heel, 3: it never sends 0)");
        for (int i = 0; i < 10; i++) ui_turn(-1, 0);
        hold(0, 10); ui_render(); shot(d, "learn_guide_heel");
        CHECK(PRESET.p[PX_B_AMP][1] == heel && count_col(8, F_Y - 6u, 224, 4, UI_ACC) <= 6 * 4 && count_col(8, F_Y - 6u, 224, 4, C_BAR) > 200,
              "learn: the knob sets the heel's value, heard; the pedal's place shown (at its heel)");
        tap(UB_PUSH);
        CHECK(lg.step == LG_HI && lg.lo == heel && lg.clo == 3 && PRESET.p[PX_B_AMP][1] == g0,
              "learn: PUSH: the heel taken, the value and where the pedal is; the toe from the preset's own value");
        px_learn_feed(4, 120);                         /* (the pedal at its toe: 120) */
        ui_turn(1, 0); ui_turn(1, 0); hold(0, 10); ui_render(); shot(d, "learn_guide_toe");
        CHECK(count_col(8, F_Y - 6u, 200, 4, UI_ACC) > 0, "learn: the pedal's place shown (at its toe)");
        tap(UB_PUSH); hold(0, 10); ui_render(); shot(d, "edit_learn_pedal_learned");
        e = px_midi_pl_row(9, px_cc_param(PX_B_AMP, 1), 0xFFu);
        CHECK(ui_mode == M_BLOCK && ui_item == 3u && e >= 0 && mid_pl[e].src == 4 && mid_pl[e].mode == PXM_MAP_VALUE && mid_pl[e].lo == heel &&
              mid_pl[e].hi == toe && mid_pl[e].clo == 3 && mid_pl[e].chi == 120 && learn_saved == 3 && PRESET.p[PX_B_AMP][1] == g0 &&
              mid_learn.hold == 0xFFu && !px_learn_feed(4, 64) && footer_has(GRN),
              "learn: PUSH: done, the range and the pedal's travel in, written, the preset's own value back, the pedal free");
    }
    hold(0, 2600); ui_render(); shot(d, "edit_learn_gain_tagged");
    {   /* the tag: GAIN's label then its number, in the accent colour, at the left of the row (the value keeps its place) */
        const uint32_t y = E_P0 + 1u * E_ROW;
        CHECK(count_col(BAR_X, y + 5u, 130, 15, UI_ACC) > 0 && count_col(BAR_X + 120u, y + 5u, 10, 15, UI_ACC) == 0,
              "learn: a row with a control says so: its label, then CC 4 in the accent colour");
    }
    ui_turn(-1, 0); ui_turn(-1, 0); CHECK(ui_item == 0u, "learn: the knob up to MODEL (ON, MODEL, the settings)");
    hold(0, 10); ui_render(); shot(d, "edit_learn_model_row");
    CHECK(footer_has(UI_ACC), "learn: MODEL says the preset's table is there (1 LEARNED  HOLD FX)");
    hold(1u << UB_FX, 850); hold(0, 10); ui_render(); shot(d, "learn_table");
    CHECK(ui_mode == M_LEARN && !mid_learn.on && learn_saved == 3, "learn: hold FX on MODEL opens the preset's MIDI table");
    mid_map[0] = (px_map_t){ 80, PXM_CC_NEXT, PXM_MAP_PRESS };   /* (an editor's, for every preset: grey, not cleared here) */
    hold(0, 10); ui_render(); shot(d, "learn_table_with_editor");
    ui_turn(1, 0); hold(1u << UB_FX, 850); hold(0, 10); ui_render(); shot(d, "learn_table_editor_row");
    CHECK(mid_map[0].src == 80 && learn_saved == 3, "learn: the table leaves the editor's controls to the editor");
    ui_turn(-1, 0); hold(1u << UB_FX, 850); hold(0, 10); ui_render(); shot(d, "learn_table_cleared");
    CHECK(px_midi_pl_count(9) == 0 && learn_saved == 4, "learn: hold FX in the table clears the selected one, written");
    px_midi_map_clear();
    hold(0, 2600); ui_render(); shot(d, "learn_table_empty");
    tap(UB_HOME); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_AMP && ui_item == 0u, "learn: HOME, back to the page it came from");
    /* a save into another slot takes the preset's own controls along */
    CHECK(px_midi_pl_put(9, 6, PXM_CC_TOGGLE0 + PX_B_DELAY, PXM_MAP_EACH, PXM_CC_ON0 + PX_B_DELAY, 0u, 100u) == 0 &&
          px_midi_pl_put(14, 7, PXM_CC_TOGGLE0 + PX_B_MOD, PXM_MAP_EACH, PXM_CC_ON0 + PX_B_MOD, 0u, 100u) == 0, "learn: one in slot 10, one in slot 15");
    ui_action(UA_SAVE); for (int i = 0; i < 5; i++) save_knob(1);
    CHECK(ui_mode == M_SAVE && sv_dest == 14u, "learn: SAVE into slot 15");
    save_confirm();
    CHECK(cur_preset == 14u && px_midi_pl_count(14) == 1 && px_midi_pl_row(14, PXM_CC_TOGGLE0 + PX_B_DELAY, 0xFFu) >= 0 &&
          px_midi_pl_count(9) == 1, "learn: the sound saved into slot 15 took slot 10's control, in place of slot 15's own");
    px_midi_pl_clear(); load_preset(9); cur_preset = 9; tap(UB_AMP); hold(0, 10);
    /* HOME: nothing learned; the page waits for the controller as long as it takes */
    ui_turn(-1, 0); hold(1u << UB_FX, 850); hold(0, 10); ui_turn(1, 0);
    CHECK(ui_mode == M_GUIDE && !lg.sw, "learn: the knob chooses PEDAL or SWITCH");
    tap(UB_HOME); hold(0, 10); ui_render(); shot(d, "edit_learn_cancelled");
    CHECK(ui_mode == M_BLOCK && ui_item == 1u && px_midi_pl_count(9) == 0 && !mid_learn.on && footer_has(UI_DIM) && learn_saved == 4,
          "learn: HOME: LEARN: CANCELLED, nothing learned or written");
    tap(UB_PUSH); CHECK(PRESET.on[PX_B_AMP] == !amp, "  (PUSH works again on the page: it flips ON)");
    tap(UB_PUSH);
    hold(1u << UB_FX, 850); hold(0, 10); tap(UB_PUSH); hold(0, 12000);
    CHECK(ui_mode == M_GUIDE && lg.step == LG_LISTEN && mid_learn.on, "learn: 12 s and nothing heard: still listening (no time limit on the page)");
    {   /* a Program Change from a controller meanwhile: the learn stops, the new preset left as it is */
        load_preset(3); cur_preset = 3; hold(0, 10);
        CHECK(ui_mode == M_BLOCK && !mid_learn.on && px_midi_pl_count(52) == 0, "learn: another preset meanwhile: cancelled");
        load_preset(9); cur_preset = 9; hold(0, 2600);
    }
    {   /* a switch on a setting: its value at OFF and at ON, the switch's own travel the whole 0..127 */
        const int s0 = learn_saved, g0 = PRESET.p[PX_B_AMP][1];
        ui_item = 3u; hold(0, 10);
        hold(1u << UB_FX, 850); hold(0, 10); ui_turn(1, 0);
        CHECK(ui_mode == M_GUIDE && lg.sw, "learn: on a setting, the knob turns PEDAL to SWITCH");
        tap(UB_PUSH); px_learn_feed(5, 127); px_learn_feed(5, 0); hold(0, 1300); ui_render(); shot(d, "learn_guide_switch_off");
        CHECK(lg.step == LG_LO && lg.src == 5 && mid_learn.hold == 5, "learn: a switch on a setting: SWITCH OFF asked");
        ui_turn(-1, 0); ui_turn(-1, 0); tap(UB_PUSH); ui_turn(1, 0); ui_turn(1, 0); ui_turn(1, 0);
        ui_action(UA_SAVE);                            /* (PUSH held a little long: PUSH all the same) */
        e = px_midi_pl_row(9, px_cc_param(PX_B_AMP, 1), 0xFFu);
        CHECK(ui_mode == M_BLOCK && e >= 0 && mid_pl[e].src == 5 && mid_pl[e].mode == PXM_MAP_VALUE && mid_pl[e].lo == g0 - 2 &&
              mid_pl[e].hi == g0 + 3 && mid_pl[e].clo == 0 && mid_pl[e].chi == 127 && learn_saved == s0 + 1 && PRESET.p[PX_B_AMP][1] == g0,
              "learn: OFF and ON taken, written, the whole 0..127 of the switch, the preset's own value back");
        px_midi_pl_clear();
    }
    ui_home(); hold(0, 50); hold(1u << UB_FX, 850); hold(0, 10);
    CHECK(ui_mode == M_MIDI && !mid_learn.on, "learn: on PLAY, hold FX still opens SETTINGS");
    midi_close(); px_midi_pl_clear(); ui_home(); full();
    return 0;
}
/* test build 47: PLAY VIEW SWITCHES: in place of the chain, the blocks the controllers' switches turn on and off */
static int live_view(const char *d)
{
    uint8_t b[LV_MAX], c[LV_MAX];
    px_midi_map_clear(); px_midi_pl_clear();
    ui_home(); load_preset(9); cur_preset = 9; px_live = 1; hold(0, 10); full(); shot(d, "play_view_nothing_learned");
    CHECK(!ui_live(), "play view: no switch learned: the chain as before");
    px_midi_pl_put(9, 21, PXM_CC_TOGGLE0 + PX_B_DELAY, PXM_MAP_EACH, PXM_CC_ON0 + PX_B_DELAY, 0u, 100u);
    px_midi_pl_put(9, 20, PXM_CC_ON0 + PX_B_DRIVE, PXM_MAP_VALUE, PXM_CC_TOGGLE0 + PX_B_DRIVE, 0u, 100u);
    mid_map[0] = (px_map_t){ 22, PXM_CC_ON0 + PX_B_REVERB, PXM_MAP_PRESS };          /* the editor's, for every preset */
    mid_map[1] = (px_map_t){ 21, PXM_CC_TOGGLE0 + PX_B_MOD, PXM_MAP_PRESS };         /* (the preset's own CC 21 first) */
    mid_map[2] = (px_map_t){ 23, (uint8_t)px_cc_param(PX_B_DELAY, 0), PXM_MAP_VALUE };   /* (a setting, not a switch) */
    mid_map[3] = (px_map_t){ 24, PXM_CC_TOGGLE0 + PX_B_DELAY, PXM_MAP_PRESS };       /* (DELAY again: shown once) */
    PRESET.on[PX_B_DELAY] = 1; PRESET.on[PX_B_DRIVE] = 0; PRESET.on[PX_B_REVERB] = 1;
    hold(0, 10); full(); shot(d, "play_view_switches");
    const uint32_t n = live_list(b, c);
    CHECK(ui_live() && n == 3u && c[0] == 20 && b[0] == PX_B_DRIVE && c[1] == 21 && b[1] == PX_B_DELAY && c[2] == 22 && b[2] == PX_B_REVERB,
          "play view: the preset's own switches and the editor's it does not hide, by number, a block once");
    CHECK(count_col(5, T_Y0 + 2u, 113, 46, BLK_COL[PX_B_DELAY]) == 0 && count_col(122, T_Y0 + 2u, 113, 46, BLK_COL[PX_B_DELAY]) > 0,
          "play view: DRIVE off (grey), DELAY on in its colour");
    PRESET.on[PX_B_DRIVE] = 1; hold(0, 10); px_sent = 0; ui_render();
    const unsigned long one = px_sent;
    shot(d, "play_view_drive_on");
    CHECK(one > 0 && one < 113u * 46u * 2u && count_col(5, T_Y0 + 2u, 113, 46, BLK_COL[PX_B_DRIVE]) > 0,
          "play view: a block turned on: its tile alone drawn anew, in its colour");
    px_sent = 0; draw_live(0); CHECK(px_sent == 0, "play view: nothing changed, no tile drawn");
    const uint32_t b0 = ui_blk;
    ui_turn(1, 0); hold(0, 10); ui_render(); shot(d, "play_view_knob_chain");
    CHECK(!ui_live() && ui_blk != b0, "play view: the knob shows the chain (and selects along it)");
    hold(0, 5100); ui_render(); shot(d, "play_view_back");
    CHECK(ui_live(), "play view: 5 s after the knob, the switches again");
    mid_map[4] = (px_map_t){ 25, PXM_CC_TOGGLE0 + PX_B_COMP, PXM_MAP_PRESS };
    mid_map[5] = (px_map_t){ 26, PXM_CC_TOGGLE0 + PX_B_PITCH, PXM_MAP_PRESS };
    mid_map[6] = (px_map_t){ 27, PXM_CC_TOGGLE0 + PX_B_EQ, PXM_MAP_PRESS };
    mid_map[7] = (px_map_t){ 28, PXM_CC_TOGGLE0 + PX_B_GATE, PXM_MAP_PRESS };
    hold(0, 10); full(); shot(d, "play_view_six");
    CHECK(live_list(b, c) == LV_MAX && c[5] == 27, "play view: six at most, three by two");
    px_live = 0; hold(0, 10); full(); shot(d, "play_view_chain_again");
    CHECK(!ui_live(), "play view: CHAIN: the chain");
    px_midi_map_clear(); px_midi_pl_clear();
    return 0;
}
/* test build 49: scenes (px_midi.h px_sc): CAB held turns PLAY's view round; in SCENES' view the knob picks one, PUSH recalls
 * it, the footswitches step them, hold FX learns its switch; the one playing keeps what is switched; SAVE writes them */
static int scenes(const char *d)
{
    px_midi_map_clear(); px_midi_pl_clear(); learn_saved = 0;
    px_live = 0; px_sc_rule = 0; px_sc_feet = 0;
    ui_home(); load_preset(9); cur_preset = 9; hold(0, 10); full();
    CHECK(px_sc.set == 0u && px_sc.cur == 0u, "scenes: a preset without any: none, the first playing");
    hold(1u << UB_CAB, 850); hold(0, 10);
    CHECK(px_live == 1u && ui_mode == M_CHAIN, "scenes: CAB held: PLAY's view SWITCHES");
    tap(UB_AMP); hold(1u << UB_CAB, 850); hold(0, 10);
    CHECK(px_live == 2u && ui_mode == M_CHAIN, "scenes: CAB held on a block page: back to PLAY, SCENES");
    tap(UB_CAB); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_CAB, "scenes: CAB pressed: its page, as before (at the release)");
    ui_home(); hold(0, 10); full(); shot(d, "scenes_view");
    CHECK(ui_live() && count_col(5, T_Y0 + 2u, 113, 46, UI_ACC) > 0 && count_col(122, T_Y0 + 2u, 113, 46, UI_ACC) == 0,
          "scenes: SCENES' view, SCENE 1 lit (playing)");
    const uint8_t dly = PRESET.on[PX_B_DELAY];
    ui_turn(1, 0); tap(UB_PUSH); hold(0, 10); ui_render(); shot(d, "scenes_recall_2");
    CHECK(sc_sel == 1u && px_sc.cur == 1u && px_sc.set == 3u && PRESET.on[PX_B_DELAY] == dly && ui_dirty(),
          "scenes: the knob on SCENE 2, PUSH: a new one, as things are; the first kept; EDITED (not saved)");
    PRESET.on[PX_B_DELAY] = (uint8_t)!dly;            /* (in SCENE 2, DELAY switched) */
    tap(UB_FSDN); hold(0, 10);
    CHECK(px_sc.cur == 0u && PRESET.on[PX_B_DELAY] == dly && cur_preset == 9u, "scenes: the left footswitch: SCENE 1, its DELAY back");
    tap(UB_FSUP); hold(0, 10);
    CHECK(px_sc.cur == 1u && PRESET.on[PX_B_DELAY] == !dly, "scenes: the right footswitch: SCENE 2 as it was left");
    tap(UB_FSDN); tap(UB_FSDN); hold(0, 10);
    CHECK(px_sc.cur == 3u && px_sc.set == 0x0Bu, "scenes: round, SCENE 4 (new)");
    ui_turn(-1, 0); ui_turn(-1, 0); ui_turn(-1, 0); tap(UB_PUSH);
    ui_action(UA_SAVE); save_confirm(); hold(0, 10); ui_render(); shot(d, "scenes_saved");
    CHECK(cur_preset == 9u && px_sc_tab[9].set == 0x0Bu && px_sc_tab[9].cur == 0u && !ui_dirty() && px_sc.cur == 0u,
          "scenes: SAVE: written with the sound, nothing EDITED, the preset in SCENE 1");
    ui_scene_recall(1); hold(0, 10);
    CHECK(px_sc.cur == 1u && PRESET.on[PX_B_DELAY] == !dly && !ui_dirty(), "scenes: a recall is no change (not EDITED)");
    load_preset(9); hold(0, 10);
    CHECK(px_sc.cur == 0u && px_sc.set == 0x0Bu, "scenes: the preset loaded again: its scenes, in the one it was saved in");
    {   /* a block with its own switch: SWITCH WINS leaves it to the switch; SCENE WINS sets it too */
        CHECK(px_midi_pl_put(9, 30, PXM_CC_TOGGLE0 + PX_B_DELAY, PXM_MAP_EACH, PXM_CC_ON0 + PX_B_DELAY, 0u, 100u) == 0, "scenes: DELAY on a switch");
        px_sc.set = 0x0Fu;
        px_sc.mask[2] = px_sc.mask[3] = (uint16_t)(sc_now() & ~(1u << PX_B_DELAY));   /* (SCENES 3 and 4: DELAY off) */
        PRESET.on[PX_B_DELAY] = 1; ui_scene_recall(2);   /* (the switch turned it on; SCENE 3) */
        const int keep = PRESET.on[PX_B_DELAY];
        px_sc_rule = 1; ui_scene_recall(3);
        CHECK(keep == 1 && PRESET.on[PX_B_DELAY] == 0, "scenes: SWITCH WINS: the switch's DELAY kept; SCENE WINS: the scene's");
        px_sc_rule = 0; px_midi_pl_clear();
    }
    {   /* both footswitches: the tuner, the first one's scene step taken back */
        const uint32_t c0 = px_sc.cur;
        hold(1u << UB_FSUP, 40); hold(1u << UB_FSUP | 1u << UB_FSDN, 40); hold(0, 10);
        CHECK(ui_mode == M_TUNER && px_sc.cur == c0, "scenes: both footswitches: the tuner, the scene as it was");
        tap(UB_HOME); hold(0, 10);
    }
    px_sc_feet = 1; ui_home(); hold(0, 10);
    tap(UB_FSUP); CHECK(cur_preset == 10u, "scenes: SCENE FOOTSW PRESETS: the footswitches change the preset");
    tap(UB_FSDN); px_sc_feet = 0; hold(0, 10);
    {   /* a scene's switch, learned in SCENES' view (hold FX on its tile): PRESS THE SWITCH */
        ui_turn(1, 0); ui_turn(1, 0);
        hold(1u << UB_FX, 850); hold(0, 10); ui_render(); shot(d, "scenes_learn");
        CHECK(ui_mode == M_GUIDE && lg.scene == sc_sel + 1u && lg.step == LG_LISTEN && mid_learn.on, "scenes: hold FX on a scene: PRESS THE SWITCH");
        px_learn_feed(40, 127); px_learn_feed(40, 0); hold(0, 1300); ui_render(); shot(d, "scenes_learned");
        const int e = px_midi_pl_row(9, PXM_CC_SCENE0 + sc_sel, 0xFFu);
        CHECK(ui_mode == M_CHAIN && e >= 0 && mid_pl[e].src == 40 && mid_pl[e].mode == PXM_MAP_PRESS && footer_has(GRN) && learn_saved == 1,
              "scenes: learned (a momentary switch), written, back on PLAY, LEARNED in green");
        hold(0, 2600); hold(1u << UB_FX, 850); hold(0, 10);
        CHECK(px_midi_pl_count(9) == 0 && learn_saved == 2 && ui_mode == M_CHAIN, "scenes: held again on it: its switch cleared");
    }
    {   /* SWITCHES' view: a scene's switch among the blocks' */
        CHECK(px_midi_pl_put(9, 41, PXM_CC_SCENE0 + 1u, PXM_MAP_EACH, PXM_CC_SCENE0 + 1u, 0u, 100u) == 0 &&
              px_midi_pl_put(9, 42, PXM_CC_TOGGLE0 + PX_B_REVERB, PXM_MAP_EACH, PXM_CC_ON0 + PX_B_REVERB, 0u, 100u) == 0, "scenes: a scene and REVERB on switches");
        px_live = 1; ui_scene_recall(1); hold(0, 10); full(); shot(d, "play_view_switches_scene");
        uint8_t b[LV_MAX], c[LV_MAX];
        CHECK(live_list(b, c) == 2u && b[0] == PX_NBLK + 1u && c[0] == 41 && b[1] == PX_B_REVERB && count_col(5, T_Y0 + 2u, 113, 46, UI_ACC) > 0,
              "scenes: SWITCHES' view: SCENE 2 (lit, playing) and REVERB, in the switches' order");
        px_live = 0; hold(0, 10); full(); shot(d, "scenes_play_top");
        CHECK(count_col(182, P_NUM_Y + 29u, 54, 15, UI_ACC) > 0 && !ui_dirty(), "scenes: on PLAY, SCENE 2 where EDITED goes");
    }
    {   /* test build 50: the scenes' names (typed in the web editor): on the tiles and PLAY's top, EDITED until SAVE */
        memcpy(px_scn[1], "CHS", 3); memcpy(px_scn[0], "V", 1);
        CHECK(ui_dirty(), "names: a name typed: EDITED");
        char t[10];
        px_scene_label(t, 1, 0);
        const int l1 = !strcmp(t, "CHS");
        px_scene_label(t, 0, 1);
        const int l0 = !strcmp(t, "V");
        px_scene_label(t, 2, 0);
        const int l2 = !strcmp(t, "SCENE 3");
        px_scene_label(t, 3, 1);
        CHECK(l1 && l0 && l2 && !strcmp(t, "SC 4"), "names: a name, else SCENE n (SC n narrow)");
        px_live = 2; hold(0, 10); full(); shot(d, "scenes_named");
        ui_action(UA_SAVE); save_confirm(); hold(0, 10);
        CHECK(!ui_dirty() && !memcmp(uis_scn[9][1], "CHS", 3), "names: SAVE writes them, EDITED gone");
        px_live = 0; ui_scene_recall(1); hold(0, 10); full(); shot(d, "scenes_named_play_top");
        CHECK(count_col(182, P_NUM_Y + 29u, 54, 15, UI_ACC) > 0 && !ui_dirty(), "names: CHS on PLAY's top where EDITED goes");
        load_preset(10); load_preset(9);
        CHECK(!memcmp(px_scn[1], "CHS", 3) && px_scn[2][0] == 0, "names: the preset loaded again has them");
        memset(uis_scn, 0, sizeof uis_scn); memset(px_scn, 0, sizeof px_scn); memset(px_scn_saved, 0, sizeof px_scn_saved);
    }
    px_midi_pl_clear(); px_live = 0; memset(&px_sc, 0, sizeof px_sc); memset(px_sc_tab, 0, sizeof px_sc_tab); ui_home(); full();
    return 0;
}
/* test build 41: the USB footswitch's notes on the footer (px_usbhost.h pxu_note): PLAY across the footer, a block page in the
 * hint's box beside the load figure; an overload's instruction first; the hint back when the note ends */
static int usb_notes(const char *d)
{
    static const uint32_t N[] = {PXU_OK, PXU_NOMIDI, PXU_LOW, PXU_LOST};
    static const char *const SHOT[] = {"play_usb_footsw_connected", "play_usb_no_midi", "play_usb_low_speed", "play_usb_footsw_lost"};
    for (uint32_t i = 0; i < 4u; i++)
        CHECK(ft_width(&FT_SM, UI_NOTE[N[i]]) <= ST_X - 2u, "a footswitch note fits a block page's hint box");
    ui_home(); load_preset(9); cur_preset = 9; usb_note = 0; full();
    const int hint = count_col(0, F_Y + 3u, 240, 15, UI_DIM);
    for (uint32_t i = 0; i < 4u; i++) {
        usb_note = N[i]; ui_render(); shot(d, SHOT[i]);
        CHECK(count_col(0, F_Y + 3u, 240, 15, N[i] == PXU_OK ? GRN : RED) > 0 && count_col(0, F_Y + 3u, 240, 15, UI_DIM) < hint,
              "PLAY: the note in its colour in place of the hint");
    }
    usb_note = 0; ui_render();
    CHECK(count_col(0, F_Y + 3u, 240, 15, RED) == 0 && count_col(0, F_Y + 3u, 240, 15, UI_DIM) == hint, "PLAY: the note ends, the hint is back");
    ui_blk = PX_B_DELAY; ui_mode = M_BLOCK; ui_item = 0; full();
    const int corner = count_col(ST_X, F_Y + 3u, ST_W, 15, UI_DIM);
    usb_note = PXU_LOST; ui_render(); shot(d, "edit_usb_footsw_lost");
    CHECK(count_col(0, F_Y + 3u, ST_X - 2u, 15, RED) > 0 && count_col(ST_X, F_Y + 3u, ST_W, 15, UI_DIM) == corner && corner > 0,
          "a block page: the note in the hint's box, the load figure kept");
    ui_home(); full(); ui_status("PEAK 150%", UI_ST_BYPASS); ui_render(); shot(d, "play_usb_lost_overload");
    CHECK(count_col(0, F_Y + 3u, 240, 15, WHT) > 0 && count_col(0, F_Y + 3u, 240, 15, RED) == 0,
          "an overload's instruction goes before the footswitch's note");
    ui_status("DSP 42%", UI_ST_NORM); ui_render();
    CHECK(count_col(0, F_Y + 3u, 240, 15, RED) > 0, "  and the note comes back after it");
    usb_note = 0; full();
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    load_preset(9);
    cur_preset = 9;
    ui_blk = PX_B_AMP;
    full(); shot(d, "play");
    ui_knob(1, 0); ui_render(); shot(d, "play_knob_next_block");
    ui_knob(-1, 0); ui_push(); poison(); ui_render(); status(); shot(d, "edit_amp_model");
    ui_knob(1, 0); ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_selected");
    ui_push(); for (int i = 0; i < 8; i++) ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_editing");
    ui_push(); ui_home(); poison(); ui_render(); status(); shot(d, "play_edited");
    ui_enter_block(PX_B_DELAY); poison(); ui_render(); status(); shot(d, "edit_delay");
    ui_knob(1, 0); ui_render(); shot(d, "edit_delay_onoff_selected");
    ui_enter_block(PX_B_DELAY); ui_render(); shot(d, "edit_delay_off");
    ui_home(); ui_blk = PX_B_GATE; ui_mode = M_BLOCK; full(); shot(d, "edit_gate_preset_level");
    ui_home(); cur_preset = 13; load_preset(13); ui_blk = PX_B_REVERB; poison(); ui_render(); status(); shot(d, "play_smooth_lead");
    ui_move_start(); ui_knob(-1, 0); ui_render(); shot(d, "play_move");
    ui_move_cancel(); cur_preset = 23; load_preset(23); master_vol = 100; bat_mv = 3640; full(); ui_status("PEAK 97%", UI_ST_BYPASS); ui_render(); shot(d, "play_acoustic_bypass_vol100_bat12");
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
    mid_c1_off = 2; midi_draw(); shot(d, "midi_core2_off_late"); mid_c1_off = 0; midi_draw();   /* v41: the 2nd core retired */
#if !PX_OWN_FX
    /* v42: the fourth row, USB AUDIO: the setting follows the row only when the page is left */
    midi_push(); midi_knob(1); midi_draw(); shot(d, "midi_usb_audio_on");
    if (px_ua_on) { printf("midi: USB AUDIO must not change while the page is open\n"); return 1; }
#endif
#if PX_OWN_FX   /* test build 39: a fifth row, SWITCH (MOMENT / LATCH), and the controls learned on the pedal counted */
    midi_push(); midi_knob(1);
    px_midi_map_clear();
    mid_map[0] = (px_map_t){ 80, 8 + PX_B_EQ, PXM_MAP_PRESS }; mid_map[1] = (px_map_t){ 81, PXM_CC_NEXT, PXM_MAP_EACH };
    mid_last[0] = PXM_CC; mid_last[1] = 80; mid_last[2] = 127; midi_draw(); shot(d, "midi_switch_latch_learned");
    if (!mid_cfg.each) { printf("midi: SWITCH must turn to LATCH\n"); return 1; }
    px_midi_map_clear();
    {   /* test build 47: PLAY VIEW (CHAIN / SWITCHES, 49: / SCENES), after SWITCH, at once; 49: SCENE+SWITCH and SCENE FOOTSW */
        midi_push();
        if (mi_row != MI_LIVE || px_live) { printf("midi: PLAY VIEW must follow SWITCH, CHAIN at first\n"); return 1; }
        midi_knob(1); midi_draw(); shot(d, "midi_play_view_switches");
        if (px_live != 1u) { printf("midi: PLAY VIEW must turn to SWITCHES\n"); return 1; }
        midi_knob(1);
        if (px_live != 2u) { printf("midi: PLAY VIEW must turn to SCENES\n"); return 1; }
        midi_knob(1); midi_knob(-1);
        if (px_live != 2u) { printf("midi: PLAY VIEW must go round SCENES -> CHAIN and back\n"); return 1; }
        midi_knob(-1); midi_knob(-1);
        if (px_live) { printf("midi: PLAY VIEW must turn back to CHAIN\n"); return 1; }
        midi_push();
        if (mi_row != MI_SCRULE || px_sc_rule) { printf("midi: SCENE+SWITCH must follow PLAY VIEW, SWITCH WINS at first\n"); return 1; }
        midi_knob(1); midi_draw(); shot(d, "midi_scene_rule");
        if (!px_sc_rule) { printf("midi: SCENE+SWITCH must turn to SCENE WINS\n"); return 1; }
        midi_knob(1);
        midi_push();
        if (mi_row != MI_SCFEET || px_sc_feet) { printf("midi: SCENE FOOTSW must follow, SCENES at first\n"); return 1; }
        midi_knob(1);
        if (!px_sc_feet) { printf("midi: SCENE FOOTSW must turn to PRESETS\n"); return 1; }
        midi_knob(1);
        if (px_sc_rule || px_sc_feet) { printf("midi: the scene rows must turn back\n"); return 1; }
        if (ft_width(&FT_SM, "SWITCH WINS") > 92u || ft_width(&FT_SM, "SCENE FOOTSW") > 140u) { printf("midi: the scene rows' texts must fit\n"); return 1; }
    }
    {   /* INPUT: -10..+10 dB, applied at once (each knob step), saved on leaving with the rest */
        const int n0 = a_in_gain_n;
        midi_push(); midi_knob(1); midi_knob(1); midi_knob(1); midi_draw(); shot(d, "midi_input_plus3");
        if (px_in_db != 3 || a_in_gain_n - n0 != 3) { printf("midi: INPUT must step by 1 dB and apply each step\n"); return 1; }
        for (int i = 0; i < 30; i++) midi_knob(1);
        if (px_in_db != PX_IN_DB_MAX) { printf("midi: INPUT must stop at +10 dB\n"); return 1; }
        for (int i = 0; i < 40; i++) midi_knob(-1);
        if (px_in_db != PX_IN_DB_MIN) { printf("midi: INPUT must stop at -10 dB\n"); return 1; }
        midi_draw(); shot(d, "midi_input_minus10");
        px_in_db = 3;
    }
    {   /* test build 46: GUITAR TONE (47: named by its sound, BRIGHT, NATURAL .. MUDDY, round both ways; its value redrawn as
         * the knob turns, which 46 did not) and PICKUP (SINGLE, HUMBUCK, P90, round), each to the input at once */
        const int z0 = a_inz_n;
        midi_push(); if (mi_row != MI_INZ) { printf("midi: GUITAR TONE must follow INPUT\n"); return 1; }
        midi_draw(); px_sent = 0;
        midi_knob(1); midi_draw();
        if (px_sent == 0) { printf("midi: GUITAR TONE's value must be drawn anew as the knob turns\n"); return 1; }
        midi_knob(1); midi_draw(); shot(d, "midi_tone_warm");
        if (px_inz != 2u || a_inz_n != z0 + 2) { printf("midi: GUITAR TONE must step NATURAL -> SMOOTH -> WARM, each applied at once\n"); return 1; }
        midi_knob(-1); midi_knob(-1); midi_knob(-1); midi_draw(); shot(d, "midi_tone_bright");
        if (px_inz != 8u) { printf("midi: GUITAR TONE must step back past NATURAL to BRIGHT\n"); return 1; }
        midi_knob(-1);
        if (px_inz != 7u) { printf("midi: GUITAR TONE must go round BRIGHT -> MUDDY\n"); return 1; }
        midi_knob(1); midi_knob(1);
        if (px_inz != 0u) { printf("midi: GUITAR TONE must go round MUDDY -> BRIGHT -> NATURAL\n"); return 1; }
        for (uint32_t i = 0; i < PX_INZ_N; i++)
            if (ft_width(&FT_SM, PX_INZ_NAME[i]) > 92u) { printf("midi: GUITAR TONE's names must fit\n"); return 1; }
        px_sent = 0;
        midi_push(); midi_knob(1); midi_draw(); shot(d, "midi_pickup_humbuck");
        if (px_pickup != 1u || a_inz_n != z0 + 9) { printf("midi: PICKUP must step SINGLE -> HUMBUCK, applied at once\n"); return 1; }
        midi_knob(1); midi_knob(1);
        if (px_pickup != 0u) { printf("midi: PICKUP must go round to SINGLE\n"); return 1; }
        midi_knob(-1);
        if (px_pickup != 2u) { printf("midi: PICKUP must step back SINGLE -> P90\n"); return 1; }
        px_pickup = 0;
    }
    {   /* test build 40: TAILS (ON / OFF, to the engine at once) and BT POWER (HIGH, MID, LOW, round) */
        const int t0 = a_tails_n;
        midi_push(); midi_knob(1);
        if (!px_tails || a_tails_n != t0 + 1) { printf("midi: TAILS must turn on and go to the engine\n"); return 1; }
        midi_push(); midi_knob(1);
        if (px_bt_pwr != 1u) { printf("midi: BT POWER must step HIGH -> MID\n"); return 1; }
        midi_knob(1); midi_knob(1);
        if (px_bt_pwr != 0u) { printf("midi: BT POWER must go round LOW -> HIGH\n"); return 1; }
        midi_knob(-1);
        if (px_bt_pwr != 2u) { printf("midi: BT POWER must step back HIGH -> LOW\n"); return 1; }
        midi_draw(); shot(d, "midi_tails_on_bt_low");
    }
    {   /* test build 42: OUTPUT (MONO, STEREO, GTR+BASS, round both ways), at once (the audio reads it each block), before USB PORT */
        midi_push();
        if (mi_row != MI_OUT || px_out_mode) { printf("midi: OUTPUT must follow BT POWER, MONO at first\n"); return 1; }
        midi_knob(1);
        if (px_out_mode != 1u) { printf("midi: OUTPUT must step MONO -> STEREO\n"); return 1; }
        midi_draw(); shot(d, "midi_output_stereo");
        midi_knob(1);
        if (px_out_mode != 2u) { printf("midi: OUTPUT must step STEREO -> GTR+BASS\n"); return 1; }
        midi_knob(1); midi_knob(-1);
        if (px_out_mode != 2u) { printf("midi: OUTPUT must go round GTR+BASS -> MONO and back\n"); return 1; }
        if (ft_width(&FT_SM, "GTR+BASS") > 92u || ft_width(&FT_SM, "STEREO") > 92u || ft_width(&FT_SM, "OUTPUT") > 140u) { printf("midi: OUTPUT's texts must fit\n"); return 1; }
        midi_draw(); shot(d, "midi_output_gtr_bass");
    }
    {   /* test build 41: USB PORT, the last row: COMPUTER / FOOTSW, to the port at once; in FOOTSW the USB line says what the
         * host sees, each text whole on the line; the controls learned are counted beside it when they fit */
        static const uint8_t ST[] = {PXU_OFF, PXU_WAIT, PXU_NOMIDI, PXU_LOW, PXU_OK, PXU_LOST};
        static const char *const SHOT[] = {"midi_usb_footsw_starting", "midi_usb_footsw_nothing_attached", "midi_usb_footsw_no_midi",
                                           "midi_usb_footsw_low_speed", "midi_usb_footsw_connected", "midi_usb_footsw_lost"};
        const int a0 = usb_apply_n;
        midi_push();
        if (mi_row != MI_USB || MI_USB != MI_OUT + 1u) { printf("midi: USB PORT must follow OUTPUT\n"); return 1; }
        midi_draw(); shot(d, "midi_usb_port_computer");
        if (ft_width(&FT_SM, "COMPUTER") > 92u) { printf("midi: COMPUTER must fit the value's box\n"); return 1; }
        midi_knob(1);
        if (!px_usb_foot || usb_apply_n != a0 + 1) { printf("midi: USB PORT must turn to FOOTSW and go to the port at once\n"); return 1; }
        for (uint32_t i = 0; i < sizeof ST; i++) {
            char t[48] = "FOOTSW: ";
            static const char *const UST[] = {"STARTING", "NOTHING ATTACHED", "NO MIDI INTERFACE", "LOW SPEED DEVICE", "CONNECTED", "LOST"};
            strcat(t, UST[ST[i]]);
            if (ft_width(&FT_SM, t) > 236u) { printf("midi: \"%s\" does not fit the USB line\n", t); return 1; }
            px_uh.st = ST[i]; midi_draw(); shot(d, SHOT[i]);
            const uint16_t want = ST[i] == PXU_OK ? GRN : ST[i] <= PXU_WAIT ? UI_ACC : RED;
            if (!count_col(0, 162, 240, 15, want)) { printf("midi: the USB line must read \"%s\" in its colour\n", t); return 1; }
        }
        px_midi_map_clear();
        mid_map[0] = (px_map_t){ 80, 8 + PX_B_EQ, PXM_MAP_PRESS }; mid_map[1] = (px_map_t){ 81, PXM_CC_NEXT, PXM_MAP_EACH };
        px_uh.st = PXU_OK; mid_last[0] = PXM_CC; mid_last[1] = 118; mid_last[2] = 127; midi_draw(); shot(d, "midi_usb_footsw_connected_learned");
        px_midi_map_clear();
        midi_knob(-1);
        if (px_usb_foot || usb_apply_n != a0 + 2) { printf("midi: USB PORT must come back to COMPUTER at once\n"); return 1; }
        px_uh.st = PXU_OFF;
    }
#if OP_UAUDIO
    {   /* test build 43: USB AUDIO, USB SEND, USB RETURN, GUITAR, the last four rows; the page scrolls to them */
        midi_push();
        if (mi_row != MI_UAUDIO || MI_UAUDIO != MI_USB + 1u || MI_UGTR != MI_ROWS - 1u) { printf("midi: USB AUDIO's rows must follow USB PORT, GUITAR last\n"); return 1; }
        midi_knob(1); midi_draw(); shot(d, "midi_usb_audio_on");
        if (px_ua_on) { printf("midi: USB AUDIO must not change while the page is open\n"); return 1; }
        /* the selected row the last of the 8 shown, the bar at the right edge */
        if (count_col(100, 36 + 7 * 15, 30, 3, C_CARD) < 60 || !count_col(237, 36, 2, 120, UI_DIM)) { printf("midi: the page must scroll to the selected row and show its bar\n"); return 1; }
        midi_push();                                    /* USB SEND: OUTPUT, DI, DI + OUT, round both ways, at once */
        if (PX_UA_SEND(px_ua_route) != 0u) { printf("midi: USB SEND must be OUTPUT at first\n"); return 1; }
        midi_knob(1); if (PX_UA_SEND(px_ua_route) != 1u) { printf("midi: USB SEND must step OUTPUT -> DI\n"); return 1; }
        midi_knob(1); midi_draw(); shot(d, "midi_usb_send_di_out");
        if (PX_UA_SEND(px_ua_route) != 2u) { printf("midi: USB SEND must step DI -> DI + OUT\n"); return 1; }
        midi_knob(1); if (PX_UA_SEND(px_ua_route) != 0u) { printf("midi: USB SEND must go round DI + OUT -> OUTPUT\n"); return 1; }
        midi_knob(-1); if (PX_UA_SEND(px_ua_route) != 2u) { printf("midi: USB SEND must step back OUTPUT -> DI + OUT\n"); return 1; }
        midi_push();                                    /* USB RETURN: along this preset's chain, INPUT .. OUTPUT, round */
        {
            uint8_t ord[PX_NBLK];
            if (px_order_valid(PRESET.order)) memcpy(ord, PRESET.order, PX_NBLK); else px_order_default(ord);
            if (PX_UA_RET(px_ua_route) != 0u) { printf("midi: USB RETURN must be OUTPUT at first\n"); return 1; }
            midi_knob(1); if (PX_UA_RET(px_ua_route) != 1u) { printf("midi: USB RETURN must step OUTPUT -> INPUT\n"); return 1; }
            for (int i = 1; i < PX_NBLK; i++) {
                midi_knob(1);
                if (PX_UA_RET(px_ua_route) != 2u + ord[i]) { printf("midi: USB RETURN must step to before the preset's block %d\n", i); return 1; }
                char t[24] = "BEFORE "; strcat(t, px_block_label(ord[i]));
                if (ft_width(&FT_SM, t) > 92u) { printf("midi: \"%s\" must fit the value's box\n", t); return 1; }
            }
            midi_knob(1); if (PX_UA_RET(px_ua_route) != 0u) { printf("midi: USB RETURN must go round to OUTPUT after the last block\n"); return 1; }
            midi_knob(-1); if (PX_UA_RET(px_ua_route) != 2u + ord[PX_NBLK - 1]) { printf("midi: USB RETURN must step back OUTPUT -> before the last block\n"); return 1; }
            while (PX_UA_RET(px_ua_route) != 2u + PX_B_DELAY) midi_knob(-1);
            if (PX_UA_SEND(px_ua_route) != 2u) { printf("midi: USB RETURN must leave USB SEND as it is\n"); return 1; }
            mid_usb_up = 1; a_ua = 1; ua_stat0 = 5u;
            midi_draw(); shot(d, "midi_usb_return_before_dly_rec_play");
        }
        midi_push();                                    /* GUITAR: MIX / CUT */
        if (PX_UA_CUT(px_ua_route)) { printf("midi: GUITAR must be MIX at first\n"); return 1; }
        midi_knob(1); midi_draw(); shot(d, "midi_usb_guitar_cut");
        if (!PX_UA_CUT(px_ua_route) || PX_UA_RET(px_ua_route) != 2u + PX_B_DELAY || PX_UA_SEND(px_ua_route) != 2u) { printf("midi: GUITAR must turn to CUT, the others as they were\n"); return 1; }
        if (!px_ua_route_valid(px_ua_route)) { printf("midi: the routing must stay a valid byte\n"); return 1; }
        a_ua = 0; ua_stat0 = 0;
    }
#endif
#endif
    midi_push(); poison(); uc.screen = -1; ui_invalidate(); ui_render(); status();
    if (midi_saved != 1 || ui_mode != M_CHAIN) { printf("midi: leaving the page after a change must save once and return to the chain\n"); return 1; }
    if (!px_ua_on) { printf("midi: leaving the page must set USB AUDIO as the row showed\n"); return 1; }
    midi_open(); midi_close();
    if (midi_saved != 1) { printf("midi: leaving without a change must not save\n"); return 1; }
#if PX_OWN_FX && OP_UAUDIO
    if (px_ua_route != (2u | 4u | (2u + PX_B_DELAY) << 3)) { printf("midi: USB SEND, USB RETURN and GUITAR must stay as set after leaving\n"); return 1; }
#endif
    midi_open(); while (mi_row != MI_UAUDIO) midi_push(); midi_knob(-1); midi_close();   /* (USB AUDIO) */
    if (midi_saved != 2 || px_ua_on) { printf("midi: USB AUDIO off again must save once more\n"); return 1; }
#if PX_OWN_FX
    {   /* test build 41: USB PORT alone changed: nothing is saved (every power-on is COMPUTER), and FOOTSW stays after the page */
        midi_open(); while (mi_row != MI_USB) midi_push();
        midi_knob(1); midi_close();
        if (midi_saved != 2 || !px_usb_foot) { printf("midi: USB PORT must not be saved, and must stay FOOTSW after leaving\n"); return 1; }
        px_usb_foot = 0;
    }
    {   /* test build 42: OUTPUT alone changed: saved on leaving */
        const int s0 = midi_saved;
        const uint8_t b0 = px_out_mode;
        midi_open(); while (mi_row != MI_OUT) midi_push();
        midi_knob(1); midi_close();
        if (midi_saved != s0 + 1 || px_out_mode == b0) { printf("midi: OUTPUT changed alone must be saved on leaving\n"); return 1; }
        px_out_mode = 0;
    }
    {   /* test build 47: the footswitches walk the rows (the right one down, the left one up, round); PLAY VIEW alone
         * changed: saved on leaving */
        const int s0 = midi_saved;
        midi_open();
        ui_action(UB_FSUP); ui_action(UB_FSUP);
        if (ui_mode != M_MIDI || mi_row != 2u) { printf("midi: the right footswitch must go down a row\n"); return 1; }
        ui_action(UB_FSDN); ui_action(UB_FSDN); ui_action(UB_FSDN);
        if (mi_row != MI_ROWS - 1u) { printf("midi: the left footswitch must go up a row, round to the last\n"); return 1; }
        while (mi_row != MI_LIVE) ui_action(UB_FSUP);
        midi_knob(1); midi_close();
        if (midi_saved != s0 + 1 || !px_live) { printf("midi: PLAY VIEW changed alone must be saved on leaving\n"); return 1; }
        px_live = 0;
    }
#if OP_UAUDIO
    {   /* test build 43: GUITAR alone changed: saved on leaving */
        const int s0 = midi_saved;
        midi_open(); while (mi_row != MI_UGTR) midi_push();
        midi_knob(1); midi_close();
        if (midi_saved != s0 + 1 || PX_UA_CUT(px_ua_route)) { printf("midi: GUITAR changed alone must be saved on leaving\n"); return 1; }
        px_ua_route = 0;
    }
#endif
#endif
    mid_cfg.chan = 16; midi_open(); midi_knob(1); midi_draw(); shot(d, "midi_channel_wraps_to_omni");
    if (mid_cfg.chan != 0) { printf("midi: channel 16 + 1 must wrap to omni\n"); return 1; }
    midi_knob(-1); if (mid_cfg.chan != 16) { printf("midi: omni - 1 must wrap to 16\n"); return 1; }
#if PX_OWN_FX
    return quick_wins(d) || overhaul(d) || pad_gestures(d) || usb_notes(d) || learn_live(d) || live_view(d) || scenes(d);
#else
    return quick_wins(d) || overhaul(d);
#endif
}
