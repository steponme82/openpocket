/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — UI v4 (v36): a PLAY screen and an EDIT screen, like the big multi-effects (Helix / HX Stomp,
 * Boss GT / GX): the preset readable from a distance, the chain in colour, big clear settings. No flicker: nothing
 * is cleared, every element is a fixed box redrawn in one pixel stream, and only when it changed.
 *
 *  PLAY                                   EDIT (a block)
 *  +------------------------------+       +------------------------------+
 *  | VOL 80     EDITED     C1 42% |       |#AMP ######################ON#|  band in the block's colour
 *  | 07   JCM900                  |       | <         JCM900          >  |  model
 *  |      4x12 GREEN              |       | GAIN                     55  |
 *  |        BRIT CRUNCH           |       | ==========-----------------  |  up to 5 settings, bar + value
 *  | [GT][CMP][PIT][SFX][SYN][DRV]|       | BASS                     50  |
 *  | [AMP][CAB][EQ][MOD][DLY][REV]|       | ...                          |
 *  | AMP   JCM900             ON  |       |                              |
 *  | PUSH EDIT   FOOT: PRESETS    |       | PUSH EDIT   HOME BACK DSP 42%|
 *  +------------------------------+       +------------------------------+
 *
 * Controls (ui_buttons, ui_turn and ui_action below: the main loop and the simulator both use them):
 * KNOB = select / adjust; PUSH = open / edit / confirm; HOME = back to PLAY, on PLAY the preset list (v42), hold 0.7 s =
 * tuner, held on = diagnostics; FOOTSWITCHES = next / previous preset, both together = tuner (v42); in the Bluetooth audio
 * image (PX_OWN_FX), with a SYN PAD on, the left one is the pad's: a press catches the chord, held 0.7 s it fades;
 * AMP CAB DLY REV MOD = that block's page (press again = on/off); FX = the next of DRV, SFX, SYN, PIT, CMP, GT, EQ in the
 * preset's chain order (v42). Hold PUSH = SAVE (into one of the 52 slots, in flash). Hold FX = MIDI; in the Bluetooth audio
 * image, on a block page: LEARN its selected row (a controller's switch or pedal, this preset's own: test build 46).
 * Double-click PUSH on PLAY = MOVE the selected block: KNOB moves it along the chain, PUSH drops it, HOME puts it back.
 * The preset LEVEL (v36) is the GT page's 3rd setting ("PRESET LVL").
 */
#pragma once

enum { M_CHAIN, M_BLOCK, M_EDIT, M_TUNER, M_SAVE, M_MOVE, M_MIDI, M_LIST, M_DIAG, M_LEARN, M_GUIDE };   /* (M_LEARN: test build 46;
                                                                                * M_GUIDE: 48) */
static uint32_t st_gen;                 /* bumped on each save: the "EDITED" reference reloads */
static void st_get(uint32_t k, px_preset_t *p);
static int st_save(uint32_t k, const px_preset_t *p);
static int st_reset(uint32_t k);
static int st_saved(uint32_t k);
static void save_draw(void);
static void midi_draw(void);
static void list_draw(void);
static uint32_t ui_mode = M_CHAIN, ui_blk = PX_B_AMP, ui_item = 0;
#define UI_PAGE (ui_mode == M_BLOCK || ui_mode == M_EDIT)   /* the block page has the focus */

#define C_OFF  0x2124                  /* a block switched off */
#define C_LINE 0x2945                  /* thin lines, connectors */
#define C_CARD 0x18E3                  /* a selected row */
#define C_BAR  0x3186                  /* the empty part of a bar */
#include "ui_colors.h"                 /* BLK_COL: the blocks' colours (also the web editor's, px_editor.h) */

/* geometry (font heights: FT_SM 15, FT_MD 23, FT_NM 32, FT_XL 45 px) */
#define S_H      20u                   /* status bar */
#define P_NUM_Y  26u                   /* PLAY: preset number */
#define P_NAME_Y 76u                   /* PLAY: preset name */
#define T_Y0     122u                  /* PLAY: 1st row of tiles */
#define T_DY     30u
#define T_COLS   6
#define T_PITCH  39u
#define T_W      35u
#define T_H      24u
#define P_INFO_Y 186u                  /* PLAY: the selected block */
#define F_Y      222u                  /* footer (both screens) */
#define E_BAND   30u                   /* EDIT: block band */
#define E_TYPE_Y 36u                   /* EDIT: model row */
#define E_P0     70u                   /* EDIT: 1st setting */
#define E_ROW    30u
#define BAR_X    10u
#define BAR_W    220u
#define ST_X     164u                  /* the load figure's box: top right of PLAY, bottom right of a block page */
#define ST_W     72u                   /* (wide enough for "SAFE 999%", 70 px, and "PEAK 999%", 71 px) */
#define C_ALERT  0xB000                /* the overload banner: white text on it reads (contrast 7.1) */

static void num3(char *b, uint32_t v)  /* number without leading zeros */
{
    int i = 0;
    if (v >= 100u) b[i++] = (char)('0' + v / 100u);
    if (v >= 10u) b[i++] = (char)('0' + (v / 10u) % 10u);
    b[i++] = (char)('0' + v % 10u); b[i] = 0;
}
static uint16_t ui_dark(uint16_t c)    /* a colour at ~1/3: the "off" version of a block colour */
{
    return (uint16_t)((((c >> 11) & 31u) / 3u) << 11 | (((c >> 5) & 63u) / 3u) << 5 | ((c & 31u) / 3u));
}

/* what the load figure stands for: the overload banner (BYPASS), LOADING a NAM model (amber on the MODEL row, the amp name
 * greyed on PLAY), a late block (the figure in red), little headroom left (v42: the figure in amber) */
enum { UI_ST_NORM, UI_ST_LATE, UI_ST_BYPASS, UI_ST_LOADING, UI_ST_HIGH };
#define UI_LOAD_HIGH 80u               /* above this load the figure turns amber */

/* ---- cache of what is on screen ---- */
static struct {
    int full, screen;                  /* screen: 0 PLAY, 1 EDIT, -1 another page was drawn over it */
    uint32_t preset, vol; char name[14];
    int dirty, amp_t, cab_t, amp_on, cab_on, amp_m, cab_m;
    uint8_t tile_on[PX_NBLK], tile_sel[PX_NBLK], tile_blk[PX_NBLK], tile_mv[PX_NBLK];
    int info_b, info_t, info_on, info_m;
    int blk, type, np, item, mode, on;
    uint8_t val[PX_NPAR];
    int hint, amp_ld, row_ld; uint32_t bnum;
    char status[12]; int status_kind;
} uc;
static void ui_invalidate(void) { uc.full = 1; }
static int ui_np(void) { return px_nparams((int)ui_blk, PRESET.type[ui_blk]); }
static int ui_nitems(void) { return 2 + ui_np(); }     /* 0 = type, 1 = on/off, 2.. = parameters */
static const char *ui_block_name(int b) { return b == PX_B_SYN ? "SYNTH" : px_block_name(b); }   /* (the documents' name) */

/* v42: a setting that picks from a list (NAM MODEL, USER IR) steps one item a detent, skips the empty slots and shows its
 * place in the list. The stored value stays 0..100 (px_nam_knob, px_ir_knob): presets do not change. */
static int ui_list_n(int b, int t, int k)              /* the list's length, 0: not a list */
{
    if (k != 0) return 0;
    return b == PX_B_AMP && t == PX_AMP_NAM ? PX_NAM_N : b == PX_B_CAB && t == PX_CAB_IR ? PX_IR_N : 0;
}
static int ui_list_of(int b, int v) { return b == PX_B_AMP ? px_nam_model(v) : px_ir_slot(v); }
static int ui_list_val(int b, int i) { return b == PX_B_AMP ? px_nam_knob(i) : px_ir_knob(i); }
static int ui_list_name(int b, int i, char *nm12) { return b == PX_B_AMP ? px_nam_name(i, nm12) : px_ir_name(i, nm12); }
static int ui_list_place(int b, int n, int cur, int *count)   /* cur's place among the filled items (1..), 0: an empty one */
{
    char nm[12];
    int at = 0;
    *count = 0;
    for (int i = 0; i < n; i++) if (ui_list_name(b, i, nm)) { ++*count; if (i == cur) at = *count; }
    return at;
}
static void ui_order_fix(void) { if (!px_order_valid(PRESET.order)) px_order_default(PRESET.order); }
static int ui_pos_of(uint32_t b) { for (int i = 0; i < PX_NBLK; i++) if (PRESET.order[i] == b) return i; return 0; }

static int bat_percent(int mv)         /* a Li-ion cell at rest: 4.15 V full, 3.30 V empty */
{
    static const int16_t V[12] = {3300, 3500, 3630, 3690, 3720, 3750, 3790, 3840, 3900, 3970, 4050, 4150};
    static const int8_t P[12] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    if (mv <= V[0]) return 0;
    for (int i = 1; i < 12; i++) if (mv < V[i]) return P[i - 1] + (P[i] - P[i - 1]) * (mv - V[i - 1]) / (V[i] - V[i - 1]);
    return 100;
}
/* battery, top centre of PLAY: a cell icon + "78%" (green / amber / red), "USB" when the 5 V supply is there, an empty cell and
 * "--" while there is no reading (v37: shown, not hidden: the raw readings are on the tuner page, bat_raw) */
static void draw_battery(int force)
{
    static int last = -100;
    int usb = bat_usb_mv > 4300, pct = bat_pct >= 0 ? bat_pct : bat_mv ? bat_percent(bat_mv) : -1, key = usb ? 1000 + (pct < 0 ? 0 : pct) : pct;
    if (!force && key == last) return;
    last = key;
    const uint32_t x = 84u, y = 4u;                    /* icon 22 x 12 + tip, then the text */
    uint16_t c = usb ? GRN : pct <= 15 ? RED : pct <= 30 ? YEL : GRN;
    lcd_rect(x, 0, 74, y, UI_PANEL); lcd_rect(x, y + 12u, 74, S_H - y - 12u, UI_PANEL);
    lcd_rect(x, y, 22, 1, UI_TXT); lcd_rect(x, y + 11u, 22, 1, UI_TXT);
    lcd_rect(x, y + 1u, 1, 10, UI_TXT); lcd_rect(x + 21u, y + 1u, 1, 10, UI_TXT);
    lcd_rect(x + 22u, y + 3u, 2, 6, UI_TXT); lcd_rect(x + 22u, y, 2, 3, UI_PANEL); lcd_rect(x + 22u, y + 9u, 2, 3, UI_PANEL);
    uint32_t f = pct < 0 ? (usb ? 18u : 0u) : (uint32_t)pct * 18u / 100u;
    if (f) lcd_rect(x + 2u, y + 2u, f, 8, c);
    if (f < 18u) lcd_rect(x + 2u + f, y + 2u, 18u - f, 8, UI_PANEL);
    lcd_rect(x + 1u, y + 1u, 1, 10, UI_PANEL); lcd_rect(x + 20u, y + 1u, 1, 10, UI_PANEL);
    lcd_rect(x + 2u, y + 1u, 18, 1, UI_PANEL); lcd_rect(x + 2u, y + 10u, 18, 1, UI_PANEL);
    char b[8] = "USB";
    if (!usb && pct < 0) { b[0] = '-'; b[1] = '-'; b[2] = 0; }
    else if (!usb) { num3(b, (uint32_t)pct); int i = 0; while (b[i]) i++; b[i] = '%'; b[i + 1] = 0; }
    lcd_rect(x + 24u, y, 4, 12, UI_PANEL);
    ft_box(x + 28u, 3, 46, b, &FT_SM, usb ? GRN : pct < 0 ? UI_DIM : (pct <= 15 ? RED : UI_TXT), UI_PANEL, 0);
}

/* ---- status bar / footer, shared ---- */
static px_preset_t ui_ref;             /* what the current slot holds */
static int ui_dirty(void)              /* the preset differs from what its slot holds (hold PUSH to save) */
{
    static uint32_t ref_slot = ~0u, ref_gen = ~0u;
    if (ref_slot != cur_preset || ref_gen != st_gen) { st_get(cur_preset, &ui_ref); ref_slot = cur_preset; ref_gen = st_gen; }
#if PX_OWN_FX
    {   /* test build 49: in a scene, the blocks' on and off against that scene as saved (a recall is no change); the other
         * scenes as kept against them as saved */
        const px_sc_t *t = &px_sc_tab[cur_preset < 52u ? cur_preset : 0u];
        uint8_t on[PX_NBLK];
        int d;
        memcpy(on, ui_ref.on, sizeof on);
        if (t->set >> px_sc.cur & 1u) for (int b = 0; b < PX_NBLK; b++) ui_ref.on[b] = (uint8_t)(t->mask[px_sc.cur] >> b & 1u);
        d = memcmp(&ui_ref, &PRESET, sizeof ui_ref) != 0 || t->set != px_sc.set;
        memcpy(ui_ref.on, on, sizeof on);
        for (uint32_t i = 0; !d && i < PX_SC_N; i++) d = i != px_sc.cur && (px_sc.set >> i & 1u) && px_sc.mask[i] != t->mask[i];
        return d;
    }
#endif
    return memcmp(&ui_ref, &PRESET, sizeof ui_ref) != 0;
}
static void draw_status(int force)
{
    if (uc.screen != 0) return;        /* (EDIT: its band takes the top) */
    if (uc.status_kind == UI_ST_BYPASS) {                /* overload: the whole bar is the banner */
        if (force) { lcd_rect(0, 0, 240, 3, C_ALERT); ft_box(0, 3, 240, "OVERLOAD: SOUND BYPASSED", &FT_SM, WHT, C_ALERT, 1); lcd_rect(0, 18, 240, S_H - 18u, C_ALERT); }
        return;
    }
    if (force || uc.vol != master_vol) {
        char b[10] = "VOL "; num3(b + 4, master_vol);
        ft_box(0, 3, 66, b, &FT_SM, UI_DIM, UI_PANEL, 1);     /* (v36: 66 px; v35's 48 px cut "VOL 100" to "VOL 10") */
        uc.vol = master_vol;
    }
    lcd_rect(66, 0, 18, S_H, UI_PANEL); lcd_rect(158, 0, ST_X - 158u, S_H, UI_PANEL);
    draw_battery(force);
}
/* the DSP load, from the main loop: "DSP 42%", "DSP 88%!" (a block ran late), "SAFE 42%", "PEAK 150%" (bypassed). v42: which
 * cores ran is on the diagnostics page (ui_dg). Every text fits ST_W (tests/uisim.c checks the worst ones). */
static struct {                        /* the main loop's last DSP figures, for the diagnostics page */
    uint32_t load, peak, cl[2];        /* the load (the peak while bypassed), the peak since the preset, each core's (layer split) */
    uint8_t cores, split, guard, late, bypass;   /* cores: 0 C0, 1 C1, 2 both ("2C"); guard: the safety bypass's limit, % */
} ui_dg = {.guard = 85};
static void ui_load_text(char *c, uint32_t l, int safe, int bypass, int late)
{
    if (l > 999u) l = 999u;
    c[0] = 0;
    b_str(c, safe ? "SAFE " : bypass ? "PEAK " : "DSP ");
    b_num(c, l); b_str(c, "%");
    if (!safe && !bypass && late) b_str(c, "!");
}
static int ui_load_kind(uint32_t l, int safe, int bypass, int late, int loading)
{
    return loading ? UI_ST_LOADING : bypass ? UI_ST_BYPASS : late ? UI_ST_LATE : !safe && l > UI_LOAD_HIGH ? UI_ST_HIGH : UI_ST_NORM;
}
static void ui_status(const char *s, int kind)
{
    if (ui_mode == M_TUNER || ui_mode == M_SAVE || ui_mode == M_MIDI || ui_mode == M_LIST || ui_mode == M_DIAG) return;
#if PX_OWN_FX
    if (ui_mode == M_LEARN || ui_mode == M_GUIDE) return;   /* (test build 46: the preset's MIDI table, 48: the guided learn,
                                                            * pages of their own) */
#endif
    int same = uc.status_kind == kind;
    for (int i = 0; i < 12; i++) { if (uc.status[i] != s[i]) same = 0; if (!s[i]) break; }
    if (same && !uc.full) return;
    int was = uc.status_kind;
    for (int i = 0; i < 12; i++) { uc.status[i] = s[i]; if (!s[i]) break; }
    uc.status[11] = 0;
    uc.status_kind = kind;
    if ((was == UI_ST_BYPASS) != (kind == UI_ST_BYPASS)) { uc.full = 1; return; }   /* the banner comes or goes: redraw the page */
    if (kind == UI_ST_BYPASS) { uc.hint = -1; return; }                              /* (on PLAY the footer shows the peak; a block page's red footer has the instruction alone) */
    int ld = kind == UI_ST_LOADING && uc.screen == 0;     /* on a block page the MODEL row says it instead */
    ft_box(ST_X, uc.screen ? F_Y + 3u : 3u, ST_W, ld ? "LOADING" : s, &FT_SM, kind == UI_ST_LATE ? RED : ld || kind == UI_ST_HIGH ? UI_ACC : UI_DIM, UI_PANEL, 2);
}
#if PX_OWN_FX
/* test build 41: the USB footswitch's news on the footer, in place of the hint (px_usbhost.h pxu_note: CONNECTED, NO MIDI and
 * LOW SPEED for 3 s, LOST until the footswitch is back); an overload's instruction goes first. The includer gives the note
 * (openpocket.c: px_uh_note) */
#ifndef PX_UI_NOTE
#define PX_UI_NOTE() 0u
#endif
static const char *const UI_NOTE[PXU_LOST + 1] = {"", "", "USB: NO MIDI INTERFACE", "USB: LOW SPEED DEVICE", "USB FOOTSW CONNECTED",
                                                  "USB FOOTSW LOST"};
#endif
#if PX_OWN_FX
/* test build 46: controls learned live, a preset's own (px_midi.h mid_learn, mid_pl). On a block page, hold FX on ON or on a
 * setting: test build 48's guided learn (below, a page of its own); held on a row that has one, it clears it; on MODEL, the
 * preset's MIDI table. The footer says what happened, 2.5 s; a row that has one says its number while selected. (Hold FX on
 * PLAY: the SETTINGS page, as before) */
static void learn_persist(void);                       /* (px_midi_io.h: the store written, as a save) */
static void lt_open(void);                             /* (the preset's MIDI table, below) */
static void lt_draw(uint32_t now);
static void lt_target(char *t, uint32_t dst);
static int ui_live(void);                              /* (test build 47: PLAY's own view, below) */
enum { UL_NONE, UL_DONE, UL_CLEARED, UL_FULL, UL_CANCEL };
static struct { uint8_t note, cc; uint32_t note_t, sig, now; } ul;   /* (now: the last pass's time) */
static int ui_row_nums(uint32_t item, uint32_t *dsw, uint32_t *dval)   /* a row's numbers: 1 a setting, 0 ON, -1 none (MODEL) */
{
    if (item == 1u) { *dsw = PXM_CC_TOGGLE0 + ui_blk; *dval = PXM_CC_ON0 + ui_blk; return 0; }
    if (item >= 2u) { *dsw = *dval = px_cc_param(ui_blk, item - 2u); return 1; }
    return -1;
}
static int ui_learn_nums(uint32_t *dsw, uint32_t *dval) { return ui_row_nums(ui_item, dsw, dval); }   /* the selected row's */
static int ui_row_cc(uint32_t item)                    /* the control a row has in this preset, -1: none (its tag) */
{
    uint32_t dsw, dval;
    if (ui_row_nums(item, &dsw, &dval) < 0) return -1;
    const int i = px_midi_pl_row(cur_preset, dsw, dval);
    return i < 0 ? -1 : (int)mid_pl[i].src;
}
/* test build 48: the guided learn, a page of its own (46 and 47 asked in the footer, and any control but PUSH ended a pedal's
 * heel and toe with the whole range, saying LEARNED all the same). A step at a time, each saying what to do and with which
 * control: 1 PEDAL or SWITCH (the knob; PUSH: next); 2 the controller heard (a pedal at its first message; a switch pressed and
 * let go, or not, within 1.2 s); on a setting 3 the value at the heel (a switch: OFF) and 4 at the toe (ON), set with the
 * knob and heard, the pedal held there: where it is is taken too (its own travel, heel to toe); PUSH: next. HOME at any step:
 * nothing learned, nothing changed. Meanwhile the controller moves nothing; the setting's own value comes back at the end */
enum { LG_TYPE, LG_LISTEN, LG_LO, LG_HI };
#define LG_Y0 62u                                      /* (the steps, a row each) */
#define LG_ROW 22u
#define LG_BIG 154u                                    /* (the step's own: what to do, the value) */
static struct {
    uint8_t step, sw, knob, k, orig, lo, clo, src, pos, scene;   /* (scene, test build 49: 1 + the scene whose switch is learned) */
    uint32_t blk, item, slot, dsw, dval, shown;
} lg;
static void lg_val(char *b, uint32_t v)                /* a setting's value as its page shows it */
{
    b[0] = 0;
    if (!px_param_text((int)lg.blk, PRESET.type[lg.blk], lg.k, (int)v, b)) num3(b, v);
}
static void lg_open(int knob, uint32_t dsw, uint32_t dval)
{
    char t[32] = "";
    memset(&lg, 0, sizeof lg);
    lg.knob = (uint8_t)knob; lg.sw = (uint8_t)!knob;   /* (on a setting a pedal, on ON a switch, to begin with) */
    lg.blk = ui_blk; lg.item = ui_item; lg.slot = cur_preset; lg.dsw = dsw; lg.dval = dval;
    if (knob) { lg.k = (uint8_t)(ui_item - 2u); lg.orig = PRESET.p[ui_blk][lg.k]; }
    lg.shown = ~0u; lg.pos = 0xFFu;
    ui_mode = M_GUIDE;
    lcd_rect(0, 30, 240, 210, UI_BG);
    lcd_rect(0, 0, 240, 30, UI_ACC);
    ft_box(8, 4, 100, "LEARN", &FT_MD, BLK, UI_ACC, 0);
    ft_box(108, 8, 128, "HOME: CANCEL", &FT_SM, BLK, UI_ACC, 2);
    lt_target(t, knob ? dval : dsw);                   /* ("GT PRESET LVL", "DLY ON/OFF") */
    ft_box(0, 34, 240, t, &FT_MD, UI_TXT, UI_BG, 1);
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    uc.screen = -1;
}
static void lg_close(int note)                         /* back to the block page, the setting's own value back */
{
    px_learn_cancel(); mid_learn.hold = 0xFFu; mid_learn.keep = 0; mid_learn.pedal = 0;
    if (lg.knob && cur_preset == lg.slot) PRESET.p[lg.blk][lg.k] = lg.orig;
    ui_mode = lg.scene ? M_CHAIN : M_BLOCK; ui_blk = lg.blk; ui_item = lg.item;   /* (a scene's: back to PLAY) */
    if ((int)ui_item >= ui_nitems()) ui_item = 0;      /* (another preset meanwhile: its own rows) */
    ul.note = (uint8_t)note; ul.note_t = ul.now; ul.cc = lg.src;
    uc.screen = -1; ui_invalidate();
}
static void lg_finish(uint32_t hi, uint32_t chi)
{
    const uint32_t dst = mid_learn.rdst;
    if (px_midi_pl_put(lg.slot, lg.src, dst, mid_learn.rmode, dst == lg.dsw ? lg.dval : lg.dsw, lg.knob ? lg.lo : 0u,
                       lg.knob ? hi : 100u)) { lg_close(UL_FULL); return; }
    if (lg.knob && !lg.sw) {                           /* (a pedal on a setting: its own travel) */
        const int i = px_midi_pl_row(lg.slot, dst, 0xFFu);
        if (i >= 0) { mid_pl[i].clo = lg.clo; mid_pl[i].chi = (uint8_t)chi; }
    }
    learn_persist();
    lg_close(UL_DONE);
}
static void lg_arm(void)
{
    px_learn_arm(lg.slot, lg.dsw, lg.dval, lg.knob, ul.now);
    mid_learn.pedal = (uint8_t)!lg.sw; mid_learn.keep = 1;
}
static void lg_scene(uint32_t n)                       /* test build 49: a scene's switch, from SCENES' view: PRESS THE SWITCH */
{
    lg_open(0, PXM_CC_SCENE0 + n, PXM_CC_SCENE0 + n);
    lg.scene = (uint8_t)(1u + n); lg.step = LG_LISTEN;
    lg_arm();
}
static void lg_knob(int dd, int fast)
{
    if (lg.step == LG_TYPE) lg.sw ^= 1u;
    else if (lg.step >= LG_LO) {
        const int v = PRESET.p[lg.blk][lg.k] + dd * (fast ? 5 : 1);
        PRESET.p[lg.blk][lg.k] = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    }
}
static void lg_tick(uint32_t now)
{
    uint32_t src = 0;
    if (ui_mode != M_GUIDE) return;
    if (cur_preset != lg.slot) { lg_close(UL_CANCEL); return; }   /* (another preset, from a controller or the editor) */
    if (lg.step != LG_LISTEN) return;
    const int r = px_learn_poll(now, &src);
    if (r == PXL_SILENT) { lg_arm(); return; }         /* (here it waits as long as it takes) */
    if (r != PXL_DONE) return;
    lg.src = (uint8_t)src;
    if (!lg.knob) { lg_finish(100u, 127u); return; }  /* (ON: that is all) */
    mid_learn.hold = (uint8_t)src;                     /* (it moves nothing until it is done) */
    mid_learn.last = (uint8_t)(lg.sw ? 0u : mid_learn.last);
    lg.step = LG_LO;
}
static void lg_draw(void)
{
    const uint32_t v = lg.knob ? PRESET.p[lg.blk][lg.k] : 0u, n = lg.knob ? 4u : 2u;
    const uint32_t key = lg.step | (uint32_t)lg.sw << 2 | v << 3 | (uint32_t)lg.src << 10 | (uint32_t)mid_learn.got << 17 |
                         (uint32_t)lg.lo << 18;
    char t[24];
    if (key != lg.shown) {
        static const char *const LG_STEP[2][4] = {{"1  PEDAL OR SWITCH", "2  MOVE THE PEDAL", "3  VALUE AT THE HEEL", "4  VALUE AT THE TOE"},
                                                  {"1  PEDAL OR SWITCH", "2  PRESS THE SWITCH", "3  VALUE AT OFF", "4  VALUE AT ON"}};
        static const char *const LG_SAY[2][4] = {{"WHAT WILL YOU USE?", "MOVE THE PEDAL", "PEDAL AT THE HEEL", "PEDAL AT THE TOE"},
                                                 {"WHAT WILL YOU USE?", "PRESS THE SWITCH", "SWITCH OFF", "SWITCH ON"}};
        lg.shown = key;
        for (uint32_t i = 0; i < 4u; i++) {
            const uint32_t y = LG_Y0 + i * LG_ROW, cur = i == lg.step;
            const uint16_t bg = cur ? C_CARD : UI_BG, fg = i < lg.step ? UI_TXT : cur ? UI_ACC : UI_DIM;
            if (i >= n) { lcd_rect(4, y, 232, LG_ROW - 2u, UI_BG); continue; }
            t[0] = 0;
            if (i == 0u) b_str(t, lg.sw ? "SWITCH" : "PEDAL");
            else if (i == 1u && lg.step > 1u) { b_str(t, "CC "); b_num(t, lg.src); }
            else if (i == 2u && lg.step > 2u) lg_val(t, lg.lo);
            ui_rbox(4, y, 232, LG_ROW - 2u, 5, bg, UI_BG);
            ft_box(10, y + 3u, 150, LG_STEP[lg.sw][i], &FT_SM, fg, bg, 0);
            ft_box(160, y + 3u, 70, t, &FT_SM, fg, bg, 2);
        }
        ft_box(0, LG_BIG, 240, LG_SAY[lg.sw][lg.step], &FT_MD, UI_TXT, UI_BG, 1);
        t[0] = 0;
        if (lg.step == LG_TYPE) b_str(t, lg.sw ? "SWITCH" : "PEDAL");
        else if (lg.step == LG_LISTEN) b_str(t, mid_learn.got ? "HEARD" : "LISTENING");
        else lg_val(t, v);
        ft_box(0, LG_BIG + 26u, 240, t, lg.step == LG_LISTEN ? &FT_MD : &FT_NM, lg.step == LG_LISTEN ? UI_DIM : UI_ACC, UI_BG, 1);
        if (lg.step == LG_LISTEN) lcd_rect(0, LG_BIG + 49u, 240, 9, UI_BG);   /* (FT_MD under FT_NM's height) */
        ft_box(0, F_Y + 3u, 240, lg.step == LG_TYPE ? "KNOB: CHOOSE    PUSH: NEXT" : lg.step == LG_LISTEN ? "HOME: CANCEL"
                                 : lg.step == LG_LO ? "KNOB: VALUE    PUSH: NEXT" : "KNOB: VALUE    PUSH: DONE",
               &FT_SM, UI_ACC, UI_PANEL, 1);
        lg.pos = 0xFFu;
    }
    if (lg.step >= LG_LO && !lg.sw && mid_learn.last != lg.pos) {   /* (where the pedal is: a bar above the footer) */
        const uint32_t w = (uint32_t)mid_learn.last * 224u / 127u;
        lg.pos = mid_learn.last;
        lcd_rect(8, F_Y - 6u, w, 4, UI_ACC); lcd_rect(8u + w, F_Y - 6u, 224u - w, 4, C_BAR);
    } else if (lg.step < LG_LO && lg.pos != 0xFEu) { lg.pos = 0xFEu; lcd_rect(8, F_Y - 6u, 224, 4, UI_BG); }
}
static void ui_learn_hold(uint32_t now)
{
    uint32_t dsw, dval;
    const int knob = ui_learn_nums(&dsw, &dval);
    ul.note_t = now;
    if (knob < 0) { lt_open(); return; }               /* (on MODEL: the preset's MIDI table) */
    const int i = px_midi_pl_row(cur_preset, dsw, dval);
    if (i >= 0) {                                      /* the row has one: cleared */
        ul.cc = mid_pl[i].src; memset(&mid_pl[i], 0xFF, sizeof mid_pl[i]);
        ul.note = UL_CLEARED;
        learn_persist();
        return;
    }
    ul.note = UL_NONE;
    lg_open(knob, dsw, dval);
}
static void ui_learn_tick(uint32_t now)               /* every pass: the guided learn's progress; the page's tags */
{
    uint32_t sig = cur_preset * 2654435761u;
    ul.now = now;
    for (uint32_t i = 0; i < PXM_PL_N; i++)            /* (this preset's own changed, or another preset: the page's tags) */
        if (mid_pl[i].slot == cur_preset) sig = (sig ^ (uint32_t)mid_pl[i].src << 8 ^ mid_pl[i].dst) * 16777619u;
    if (sig != ul.sig) { ul.sig = sig; if (UI_PAGE) ui_invalidate(); }
    lg_tick(now);
    if (ul.note && now - ul.note_t >= 2500u) ul.note = UL_NONE;
}
static int ui_learn_line(char *t, uint16_t *c)        /* a block page's footer for the learn: 0 none, else *t in *c */
{
    uint32_t dsw, dval;
    t[0] = 0;
    if (!UI_PAGE && !(ui_mode == M_CHAIN && px_live == 2u)) return 0;   /* (49: SCENES' view says what happened too) */
    switch (ul.note) {
    case UL_DONE: b_str(t, "LEARNED: CC "); b_num(t, ul.cc); *c = GRN; return 1;
    case UL_CLEARED: b_str(t, "CC "); b_num(t, ul.cc); b_str(t, " CLEARED"); *c = UI_ACC; return 1;
    case UL_FULL: b_str(t, "LEARN: 64 IN USE, FULL"); *c = RED; return 1;
    case UL_CANCEL: b_str(t, "LEARN: CANCELLED"); *c = UI_DIM; return 1;
    }
    if (!UI_PAGE) return 0;
    if (ui_item == 0u && px_midi_pl_count(cur_preset)) {   /* (MODEL, in a preset that has some: its table is there) */
        b_num(t, px_midi_pl_count(cur_preset)); b_str(t, " LEARNED  HOLD FX"); *c = UI_ACC; return 1;
    }
    if (ui_learn_nums(&dsw, &dval) >= 0) {
        const int i = px_midi_pl_row(cur_preset, dsw, dval);
        if (i >= 0) { b_str(t, "CC "); b_num(t, mid_pl[i].src); b_str(t, "  HOLD FX: CLEAR"); *c = UI_ACC; return 1; }
    }
    return 0;
}
#endif
static void draw_hint(int force)
{
    int h = (int)ui_mode, alert = uc.status_kind == UI_ST_BYPASS;
#if PX_OWN_FX
    int save = !alert && h == M_CHAIN && uc.dirty == 1;  /* EDITED shows on PLAY: the footer names SAVE (49: 2+ a scene) */
    const int scv = h == M_CHAIN && px_live == 2u && ui_live();   /* (test build 49: SCENES' view: its own controls) */
#else
    int save = !alert && h == M_CHAIN && uc.dirty;       /* EDITED shows on PLAY: the footer names SAVE */
#endif
#if PX_OWN_FX
    uint32_t note = alert ? 0u : PX_UI_NOTE();
    if (note > PXU_LOST || !UI_NOTE[note][0]) note = 0;
    char lt[32]; uint16_t lc = UI_DIM; uint32_t lh = 0;   /* (test build 46: the learn's line, and its text in the key) */
    const int ln = alert ? 0 : ui_learn_line(lt, &lc);
    for (int i = 0; ln && lt[i]; i++) lh = lh * 31u + (uint8_t)lt[i];
    int key = h | alert << 4 | save << 5 | (int)note << 6 | ln << 9 | (int)(lh & 0x7FFFFu) << 10 | scv << 29;
#else
    int key = h | alert << 4 | save << 5;
#endif
    if (!force && uc.hint == key) return;
    uc.hint = key;
    if (alert) {                                         /* overload: what to do (a block page's whole footer turns red) */
        char t[40]; t[0] = 0;
        b_str(t, uc.screen ? "OVERLOAD: TURN A BLOCK OFF" : "TURN A BLOCK OFF  ");
        if (!uc.screen) b_str(t, uc.status);
        if (uc.screen) lcd_rect(0, F_Y, 240, 3, C_ALERT);
        ft_box(0, F_Y + 3u, 240, t, &FT_SM, WHT, uc.screen ? C_ALERT : UI_PANEL, 1);
        return;
    }
#if PX_OWN_FX
    if (note) {                                          /* (the hint's box: a block page keeps its load figure) */
        ft_box(0, F_Y + 3u, uc.screen ? ST_X - 2u : 240u, UI_NOTE[note], &FT_SM, note == PXU_OK ? GRN : RED, UI_PANEL,
               uc.screen ? 0 : 1);
        return;
    }
    if (ln) { ft_box(0, F_Y + 3u, uc.screen ? ST_X - 2u : 240u, lt, &FT_SM, lc, UI_PANEL, uc.screen ? 0 : 1); return; }
#endif
    if (save) {
        ft_box(0, F_Y + 3u, 116, "PUSH: EDIT", &FT_SM, UI_DIM, UI_PANEL, 2);
        ft_box(116, F_Y + 3u, 124, "  HOLD: SAVE", &FT_SM, UI_ACC, UI_PANEL, 0);
        return;
    }
#if PX_OWN_FX
    const char *t = scv ? "KNOB: SCENE    PUSH: RECALL" : h == M_MOVE ? "KNOB: MOVE    PUSH: DROP"
#else
    const char *t = h == M_MOVE ? "KNOB: MOVE    PUSH: DROP"
#endif
                  : h == M_EDIT ? "KNOB: VALUE   PUSH: OK"
                  : h == M_BLOCK ? "KNOB: SELECT  PUSH: EDIT"
                  : "KNOB: BLOCK  PUSH: EDIT  HOME: LIST";
    ft_box(0, F_Y + 3u, uc.screen ? ST_X - 2u : 240u, t, &FT_SM, UI_DIM, UI_PANEL, uc.screen ? 0 : 1);
}

#if PX_OWN_FX
/* ---- test build 49: scenes (px_midi.h px_sc) ---- */
static void sw_map(uint8_t *to)                        /* what each controller number does in this preset: its own first */
{
    uint32_t i;
    memset(to, 0xFF, 128);
    for (i = 0; i < PXM_MAP_N; i++) if (mid_map[i].src < 128u) to[mid_map[i].src] = mid_map[i].dst;
    for (i = 0; i < PXM_PL_N; i++) if (mid_pl[i].slot == cur_preset && mid_pl[i].src < 128u) to[mid_pl[i].src] = mid_pl[i].dst;
}
static uint32_t sc_now(void) { uint32_t m = 0; for (int b = 0; b < PX_NBLK; b++) m |= (uint32_t)(PRESET.on[b] != 0) << b; return m; }
static void ui_scene_recall(uint32_t n)                /* (CC 65 to 68: px_midi_io.h; the footswitches; PUSH on a tile) */
{
    uint8_t to[128];
    uint32_t own = 0xFFFu, a;
    if (n >= PX_SC_N) return;
    if (!px_sc_rule) {                                 /* SWITCH WINS: a block with its own switch is left to it */
        sw_map(to); px_cc_build();
        for (a = 0; a < 128u; a++)
            if (to[a] < 128u && (px_cc_tab[to[a]].kind == PXM_TOGGLE || px_cc_tab[to[a]].kind == PXM_ON)) own &= ~(1u << px_cc_tab[to[a]].blk);
    }
    px_sc.mask[px_sc.cur] = (uint16_t)sc_now(); px_sc.set |= (uint8_t)(1u << px_sc.cur);   /* (the one playing keeps its state) */
    px_sc.cur = (uint8_t)n;
    if (!(px_sc.set >> n & 1u)) { px_sc.mask[n] = (uint16_t)sc_now(); px_sc.set |= (uint8_t)(1u << n); return; }   /* (new: as
                                                                                                     * things are) */
    for (int b = 0; b < PX_NBLK; b++) if (own >> b & 1u) PRESET.on[b] = (uint8_t)(px_sc.mask[n] >> b & 1u);
}
static void sc_fold(void)                              /* (SAVE: the scene playing as it is now) */
{
    if (px_sc.set) { px_sc.mask[px_sc.cur] = (uint16_t)sc_now(); px_sc.set |= (uint8_t)(1u << px_sc.cur); }
}
static uint8_t sc_sel;                                 /* (SCENES' view: the tile the knob is on) */
#endif
/* ===================================================== PLAY ======== */
static void draw_play_top(int force)
{
    if (force || uc.preset != cur_preset) {
        char b[4]; uint32_t n = cur_preset + 1u;
        b[0] = (char)('0' + n / 10u); b[1] = (char)('0' + n % 10u); b[2] = 0;
        ft_box(6, P_NUM_Y, 84, b, &FT_XL, UI_ACC, UI_BG, 0);
        uc.preset = cur_preset;
    }
    int at = PRESET.type[PX_B_AMP], ct = PRESET.type[PX_B_CAB], ao = PRESET.on[PX_B_AMP], co = PRESET.on[PX_B_CAB];
    int am = at == PX_AMP_NAM ? px_nam_model(PRESET.p[PX_B_AMP][0]) : -1;   /* v37: a NAM amp shows its model */
    int ald = uc.status_kind == UI_ST_LOADING && at == PX_AMP_NAM;   /* a NAM model loads: the name greys until it plays */
    if (force || at != uc.amp_t || ao != uc.amp_on || am != uc.amp_m || ald != uc.amp_ld) {
        char nm[12];
        const char *an = px_type_name(PX_B_AMP, at);
        if (am >= 0) { px_nam_name(am, nm); an = nm; }
        ft_box(94, P_NUM_Y + 4u, 142, ao ? an : "NO AMP", &FT_MD, ao && !ald ? BLK_COL[PX_B_AMP] : UI_DIM, UI_BG, 0);
        uc.amp_t = at; uc.amp_on = ao; uc.amp_m = am; uc.amp_ld = ald;
    }
    int cm = ct == PX_CAB_IR ? px_ir_slot(PRESET.p[PX_B_CAB][0]) : -1;   /* v38: a user IR shows its name */
    if (force || ct != uc.cab_t || co != uc.cab_on || cm != uc.cab_m) {
        char nm[12];
        const char *cn = px_type_name(PX_B_CAB, ct);
        if (cm >= 0) { px_ir_name(cm, nm); cn = nm; }
        ft_box(94, P_NUM_Y + 29u, 88, co ? cn : "NO CAB", &FT_SM, UI_DIM, UI_BG, 0);
        uc.cab_t = ct; uc.cab_on = co; uc.cab_m = cm;
    }
    int d = ui_dirty();                                /* EDITED: the sound differs from its saved slot */
#if PX_OWN_FX
    {   /* test build 49: else the scene playing, in a preset that has some */
        char t[10] = "SCENE ";
        t[6] = (char)('1' + px_sc.cur); t[7] = 0;
        d = d ? 1 : px_sc.set ? 2 + px_sc.cur : 0;
        if (force || d != uc.dirty) { ft_box(182, P_NUM_Y + 29u, 54, d == 1 ? "EDITED" : d ? t : "", &FT_SM, UI_ACC, UI_BG, 2); uc.dirty = d; }
    }
#else
    if (force || d != uc.dirty) { ft_box(182, P_NUM_Y + 29u, 54, d ? "EDITED" : "", &FT_SM, UI_ACC, UI_BG, 2); uc.dirty = d; }
#endif
    int nchg = force;
    for (int i = 0; i < 14; i++) if (uc.name[i] != PRESET.name[i]) nchg = 1;
    if (nchg) {
        ft_box(0, P_NAME_Y, 240, PRESET.name, &FT_NM, UI_TXT, UI_BG, 1);
        for (int i = 0; i < 14; i++) uc.name[i] = PRESET.name[i];
    }
}
/* ---- chain tiles: position i shows block PRESET.order[i] ---- */
static uint32_t tile_x(int i) { return 4u + (uint32_t)(i % T_COLS) * T_PITCH; }
static uint32_t tile_y(int i) { return T_Y0 + (uint32_t)(i / T_COLS) * T_DY; }
static void draw_tile(int i)
{
    int b = PRESET.order[i];
    uint32_t x = tile_x(i), y = tile_y(i);
    int on = PRESET.on[b] != 0, sel = (b == (int)ui_blk), mv = sel && ui_mode == M_MOVE;
    uint16_t bg = mv ? UI_ACC : (on ? BLK_COL[b] : C_OFF), fg = (on || mv) ? BLK : ft_mix(BLK_COL[b], C_OFF, 11);   /* (an off label: the block's colour at 11/15, contrast 2.6 to 7.2) */
    ui_rbox(x - 2u, y - 2u, T_W + 4u, T_H + 4u, 5, sel ? WHT : UI_BG, UI_BG);   /* selection ring (or nothing) */
    ui_rbox(x, y, T_W, T_H, 4, bg, sel ? WHT : UI_BG);
    ft_box(x + 2u, y + 5u, T_W - 4u, px_block_label(b), &FT_SM, fg, bg, 1);
    uc.tile_on[i] = (uint8_t)on; uc.tile_sel[i] = (uint8_t)sel; uc.tile_blk[i] = (uint8_t)b; uc.tile_mv[i] = (uint8_t)mv;
}
#if PX_OWN_FX
/* test build 47: PLAY VIEW SWITCHES (px_midi.h px_live): in place of the chain's tiles and the selected block, the blocks this
 * preset's controller switches turn on and off (its own learned ON rows, then the editor's for every preset), in the order of
 * the controllers' numbers, big, each in its colour when on, with its model: 6 at most. The knob shows the chain for 5 s */
#define LV_MAX 6u
#define LV_PEEK 5000u
static uint32_t lv_n = ~0u, lv_key[LV_MAX], lv_peek_t;   /* (what each tile shows: drawn again when it changes) */
static uint8_t lv_peek;
static uint32_t live_list(uint8_t *blk, uint8_t *cc)   /* (blk PX_NBLK + n: scene n; test build 49) */
{
    uint8_t to[128];
    uint32_t n = 0, a, i;
    sw_map(to);
    px_cc_build();
    for (a = 0; a < 128u && n < LV_MAX; a++) {
        const uint32_t d = to[a], s = d - PXM_CC_SCENE0, k = d < 128u ? px_cc_tab[d].kind : PXM_NONE;
        const uint32_t x = s < PX_SC_N ? PX_NBLK + s : k == PXM_TOGGLE || k == PXM_ON ? px_cc_tab[d].blk : 0xFFu;
        if (x == 0xFFu) continue;
        for (i = 0; i < n && blk[i] != x; i++) { }
        if (i == n) { blk[n] = (uint8_t)x; cc[n] = (uint8_t)a; n++; }   /* (a block, a scene once) */
    }
    return n;
}
static int ui_live(void)
{
    uint8_t b[LV_MAX], c[LV_MAX];
    if (lv_peek && ul.now - lv_peek_t >= LV_PEEK) lv_peek = 0;
    if (!px_live || lv_peek || ui_mode != M_CHAIN) return 0;
    return px_live == 2u || live_list(b, c) > 0u;      /* (49: SCENES' view always; SWITCHES' where the preset has some) */
}
static void lv_tile(uint32_t i, uint32_t x, uint32_t y, uint32_t w, uint32_t k, uint16_t bg, uint16_t fg, const char *l, const char *r,
                    const char *u)                     /* (a tile: its label, a number at the right, a line under) */
{
    const uint32_t wn = r[0] ? ft_width(&FT_SM, r) : 0u;
    if (k == lv_key[i]) return;
    lv_key[i] = k;
    ui_rbox(x, y, w, 46, 6, bg, UI_BG);
    ft_box(x + 5u, y + 2u, w - 12u - wn, l, &FT_MD, fg, bg, 0);
    if (wn) ft_box(x + w - 5u - wn, y + 6u, wn, r, &FT_SM, fg, bg, 2);
    ft_box(x + 3u, y + 27u, w - 6u, u, &FT_SM, fg, bg, 1);
}
static void draw_live(int force)
{
    uint8_t b[LV_MAX], c[LV_MAX];
    const uint32_t sv = px_live == 2u, n = sv ? PX_SC_N : live_list(b, c), cols = n <= 4u ? 2u : 3u, w = cols == 2u ? 113u : 74u;
    if (force || (n | sv << 8) != lv_n) {              /* (another layout: the area cleared, every tile drawn) */
        lcd_rect(0, T_Y0 - 2u, 240, F_Y - (T_Y0 - 2u), UI_BG);
        lv_n = n | sv << 8;
        for (uint32_t i = 0; i < LV_MAX; i++) lv_key[i] = ~0u;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t x = 5u + (i % cols) * (w + 4u), y = T_Y0 + 2u + (i / cols) * 50u;
        char l[10] = "SCENE ", r[8] = "", u[12] = "";
        if (sv || b[i] >= PX_NBLK) {                   /* test build 49: a scene, lit while it plays; its switch's number */
            const uint32_t sn = sv ? i : b[i] - PX_NBLK, on = px_sc.cur == sn, sel = sv && sn == sc_sel;
            const int e = px_midi_pl_row(cur_preset, PXM_CC_SCENE0 + sn, 0xFFu);
            if (cols == 2u) { l[6] = (char)('1' + sn); l[7] = 0; }          /* ("SCENE 2", "SC 2" three across) */
            else { l[0] = 'S'; l[1] = 'C'; l[2] = ' '; l[3] = (char)('1' + sn); l[4] = 0; }
            if (!sv || e >= 0) { b_str(u, "CC "); b_num(u, sv ? mid_pl[e].src : c[i]); }
            lv_tile(i, x, y, w, 0x80000000u | sn << 20 | on << 19 | sel << 18 | (uint32_t)(e >= 0 ? mid_pl[e].src : 255),
                    on ? UI_ACC : C_OFF, on ? BLK : UI_TXT, l, r, u);
            if (sv) {                                  /* (the knob's: a ring) */
                const uint16_t rc = sel ? WHT : UI_BG;
                lcd_rect(x, y - 2u, w, 1, rc); lcd_rect(x, y + 47u, w, 1, rc);
            }
            continue;
        }
        const int bb = b[i], t = PRESET.type[bb], on = PRESET.on[bb] != 0;
        const uint16_t bg = on ? BLK_COL[bb] : C_OFF, fg = on ? BLK : ft_mix(BLK_COL[bb], C_OFF, 11);
        char nm[12];
        const char *tn = px_type_name(bb, t);
        const int m = ui_list_n(bb, t, 0) ? ui_list_of(bb, PRESET.p[bb][0]) : -1;
        if (m >= 0 && ui_list_name(bb, m, nm)) tn = nm;
        if (cols == 2u) b_str(r, "CC ");
        b_num(r, c[i]);
        lv_tile(i, x, y, w, (uint32_t)bb << 25 | (uint32_t)c[i] << 18 | (uint32_t)on << 17 | (uint32_t)(t & 255) << 9 | PRESET.p[bb][0],
                bg, fg, px_block_label(bb), r, tn);
    }
}
#endif
static void draw_tiles(int force)
{
    ui_order_fix();
    for (int i = 0; i < PX_NBLK; i++) {
        int b = PRESET.order[i], on = PRESET.on[b] != 0, sel = (b == (int)ui_blk), mv = sel && ui_mode == M_MOVE;
        if (force || uc.tile_blk[i] != b || uc.tile_on[i] != on || uc.tile_sel[i] != sel || uc.tile_mv[i] != mv) draw_tile(i);
    }
}
static void draw_info(int force)       /* the selected block: label, model (v42: a NAM amp's model, a user IR's name), on/off */
{
    int b = (int)ui_blk, t = PRESET.type[b], on = PRESET.on[b] != 0;
    int m = ui_list_n(b, t, 0) ? ui_list_of(b, PRESET.p[b][0]) : -1;
    if (!force && uc.info_b == b && uc.info_t == t && uc.info_on == on && uc.info_m == m) return;
    char nm[12];
    const char *tn = px_type_name(b, t);
    if (m >= 0) { ui_list_name(b, m, nm); tn = nm; }
    ft_box(0, P_INFO_Y, 8, "", &FT_MD, UI_TXT, UI_BG, 0);
    ft_box(8, P_INFO_Y, 54, px_block_label(b), &FT_MD, BLK_COL[b], UI_BG, 0);
    ft_box(62, P_INFO_Y, 132, tn, &FT_MD, on ? UI_TXT : UI_DIM, UI_BG, 0);
    lcd_rect(194, P_INFO_Y, 40, 4, UI_BG); lcd_rect(194, P_INFO_Y + 19u, 40, 4, UI_BG);
    ft_box(194, P_INFO_Y + 4u, 40, on ? "ON" : "OFF", &FT_SM, on ? BLK_COL[b] : UI_DIM, UI_BG, 2);
    lcd_rect(234, P_INFO_Y, 6, 23, UI_BG);
    uc.info_b = b; uc.info_t = t; uc.info_on = on; uc.info_m = m;
}

/* ===================================================== EDIT ======== */
static void draw_band(int force)       /* the block's name on its colour, ON / OFF pill (item 1) */
{
    int on = PRESET.on[ui_blk] != 0, sel = (UI_PAGE && ui_item == 1u);
    if (!(force || uc.blk != (int)ui_blk || uc.bnum != cur_preset || uc.on != on || (uc.item == 1) != sel)) return;
    uint16_t c = BLK_COL[ui_blk];
    if (force || uc.blk != (int)ui_blk || uc.bnum != cur_preset) {   /* (the preset number: a change over MIDI does not leave the page) */
        char n[4]; num3(n, cur_preset + 1u);
        lcd_rect(0, 0, 240, 4, c); lcd_rect(0, 27, 240, E_BAND - 27u, c); lcd_rect(0, 4, 8, 23, c);
        lcd_rect(8, 4, 24, 23, c); ui_rbox(8, 6, 24, 19, 5, BLK, c);
        ft_box(10, 8, 20, n, &FT_SM, c, BLK, 1);
        lcd_rect(32, 4, 4, 23, c);
#if PX_OWN_FX   /* test build 46: ON has a control in this preset: its number after the name, where it fits */
        const int lcc = ui_row_cc(1u);
        const char *bn = ui_block_name((int)ui_blk);
        char tg[8] = "CC";
        uint32_t w = ft_width(&FT_MD, bn) + 8u;
        if (lcc >= 0) b_num(tg, (uint32_t)lcc);
        if (lcc >= 0 && w + ft_width(&FT_SM, tg) <= 136u) {
            ft_box(36, 4, w, bn, &FT_MD, BLK, c, 0);
            ft_box(36u + w, 9, 136u - w, tg, &FT_SM, BLK, c, 0);
            lcd_rect(36u + w, 4, 136u - w, 5, c); lcd_rect(36u + w, 24, 136u - w, 3, c);
        } else
#endif
        ft_box(36, 4, 136, ui_block_name((int)ui_blk), &FT_MD, BLK, c, 0);
        lcd_rect(172, 4, 4, 23, c); lcd_rect(232, 4, 8, 23, c);
        uc.bnum = cur_preset;
    }
    ui_rbox(176, 4, 56, 23, 6, sel ? WHT : c, c);
    ui_rbox(178, 6, 52, 19, 5, on ? BLK : ui_dark(c), sel ? WHT : c);
    ft_box(182, 8, 44, on ? "ON" : "OFF", &FT_SM, on ? c : UI_TXT, on ? BLK : ui_dark(c), 1);
}
static void draw_type(void)
{
    int sel = (UI_PAGE && ui_item == 0u), ed = sel && ui_mode == M_EDIT, many = px_ntypes((int)ui_blk) > 1;
    uint16_t bg = sel ? C_CARD : UI_BG, fg = ed ? UI_ACC : UI_TXT;
    ui_rbox(4, E_TYPE_Y - 2u, 232, 28, 5, bg, UI_BG);
    ft_box(8, E_TYPE_Y, 20, sel && many ? "<" : "", &FT_MD, UI_ACC, bg, 1);   /* (v42: no arrows on a block of one model) */
    ft_box(28, E_TYPE_Y, 184, px_type_name((int)ui_blk, PRESET.type[ui_blk]), &FT_MD, fg, bg, 1);
    ft_box(212, E_TYPE_Y, 20, sel && many ? ">" : "", &FT_MD, UI_ACC, bg, 1);
}
static int ui_loading_row(int k)       /* the NAM amp's MODEL row while its model loads */
{
    return uc.status_kind == UI_ST_LOADING && ui_blk == PX_B_AMP && PRESET.type[PX_B_AMP] == PX_AMP_NAM && k == 0;
}
static void ui_hz_text(char *b, int off)   /* v42: USER IR's cut frequencies with their unit: "89 HZ", "6.3 KHZ", "OFF" */
{
    uint32_t hz = 0;
    for (int i = 0; b[i]; i++) hz = hz * 10u + (uint32_t)(b[i] - '0');
    b[0] = 0;
    if (off) { b_str(b, "OFF"); return; }
    if (hz < 1000u) { b_num(b, hz); b_str(b, " HZ"); return; }
    hz = (hz + 50u) / 100u;
    b_num(b, hz / 10u); b_str(b, "."); b_num(b, hz % 10u); b_str(b, " KHZ");
}
#define E_LIST_X 96u                   /* a list setting: its place at the left, the item's name from here */
static void draw_bar(int k)            /* value + bar: the only thing that moves while adjusting */
{
    uint32_t y = E_P0 + (uint32_t)k * E_ROW;
    int sel = (UI_PAGE && (int)ui_item == k + 2), ed = sel && ui_mode == M_EDIT, t = PRESET.type[ui_blk];
    int ln = ui_list_n((int)ui_blk, t, k), ld = ui_loading_row(k);
    uint16_t bg = sel ? C_CARD : UI_BG, vc = ld ? UI_DIM : ed ? UI_ACC : UI_TXT;   /* (the new name stays grey until it plays) */
    uint16_t fc = ed ? UI_ACC : BLK_COL[ui_blk];
    uint32_t v = PRESET.p[ui_blk][k]; if (v > 100u) v = 100u;
    char b[16];
    uc.val[k] = (uint8_t)v;
    if (ln) {                          /* "MODEL 6/14", the name, one segment per filled item */
        int cur = ui_list_of((int)ui_blk, (int)v), n, at = ui_list_place((int)ui_blk, ln, cur, &n);
        char nm[12];
        b[0] = 0; b_str(b, px_param_name((int)ui_blk, t, k)); b_str(b, " ");
        if (at) b_num(b, (uint32_t)at); else b_str(b, "-");
        b_str(b, "/"); b_num(b, (uint32_t)n);
        ft_box(BAR_X, y + 5u, E_LIST_X - BAR_X, ld ? "LOADING" : b, &FT_SM, ld ? UI_ACC : sel ? WHT : UI_DIM, bg, 0);
        ui_list_name((int)ui_blk, cur, nm);
        ft_box(E_LIST_X, y + 1u, 230u - E_LIST_X, nm, &FT_MD, vc, bg, 2);
        uint32_t sw = n ? BAR_W / (uint32_t)n : BAR_W;
        for (int i = 0; i < n; i++) {
            lcd_rect(BAR_X + (uint32_t)i * sw, y + 24u, sw - 1u, 4, i + 1 == at ? fc : C_BAR);
            lcd_rect(BAR_X + (uint32_t)i * sw + sw - 1u, y + 24u, 1, 4, bg);
        }
        if (sw * (uint32_t)n < BAR_W) lcd_rect(BAR_X + sw * (uint32_t)n, y + 24u, BAR_W - sw * (uint32_t)n, 4, n ? bg : C_BAR);
        return;
    }
    if (!px_param_text((int)ui_blk, t, k, (int)v, b)) num3(b, v);   /* "+5TH", "440 MS"... */
    else if (ui_blk == PX_B_CAB && t == PX_CAB_IR && k >= 1) ui_hz_text(b, k == 2 && v == 0u);
    ft_box(140, y + 1u, 90, b, &FT_MD, vc, bg, 2);
    uint32_t w = v * BAR_W / 100u;
    if (w) lcd_rect(BAR_X, y + 24u, w, 4, fc);
    if (w < BAR_W) lcd_rect(BAR_X + w, y + 24u, BAR_W - w, 4, C_BAR);
}
static void draw_row(int k)            /* one full setting row */
{
    uint32_t y = E_P0 + (uint32_t)k * E_ROW;
    if (k >= ui_np()) { lcd_rect(0, y, 240, E_ROW, UI_BG); return; }
    int sel = (UI_PAGE && (int)ui_item == k + 2);
    uint16_t bg = sel ? C_CARD : UI_BG;
    ui_rbox(4, y, 232, E_ROW - 1u, 5, bg, UI_BG);
    lcd_rect(0, y + E_ROW - 1u, 240, 1, UI_BG);
    if (sel) lcd_rect(4, y + 6u, 3, 14, UI_ACC);                 /* accent marker */
    int ld = ui_loading_row(k);
#if PX_OWN_FX   /* test build 46: a setting that has a control in this preset says its number after its label */
    const int lcc = ld ? -1 : ui_row_cc((uint32_t)k + 2u);
    if (lcc >= 0 && !ui_list_n((int)ui_blk, PRESET.type[ui_blk], k)) {
        const char *nm = px_param_name((int)ui_blk, PRESET.type[ui_blk], k);
        char tg[8] = "CC";
        uint32_t w = ft_width(&FT_SM, nm) + 8u;
        if (w > 92u) w = 92u;
        b_num(tg, (uint32_t)lcc);
        ft_box(BAR_X, y + 5u, w, nm, &FT_SM, sel ? WHT : UI_DIM, bg, 0);
        ft_box(BAR_X + w, y + 5u, 130u - w, tg, &FT_SM, UI_ACC, bg, 0);
    } else
#endif
    if (!ui_list_n((int)ui_blk, PRESET.type[ui_blk], k))         /* (a list setting draws its label with its place) */
        ft_box(BAR_X, y + 5u, 130, ld ? "LOADING" : px_param_name((int)ui_blk, PRESET.type[ui_blk], k), &FT_SM, ld ? UI_ACC : sel ? WHT : UI_DIM, bg, 0);
    if (k == 0) uc.row_ld = ld;
    draw_bar(k);
}
static void draw_item(int it)
{
    if (it == 0) draw_type();
    else if (it == 1) draw_band(1);
    else draw_row(it - 2);
}
static void draw_edit(int force)
{
    int type = PRESET.type[ui_blk], np = ui_np(), on = PRESET.on[ui_blk] != 0;
    if (force || uc.blk != (int)ui_blk || uc.type != type) {
        draw_band(1); draw_type();
        lcd_rect(0, E_TYPE_Y + 26u, 240, E_P0 - E_TYPE_Y - 26u, UI_BG);
        for (int k = 0; k < PX_NPAR; k++) draw_row(k);
        lcd_rect(0, E_P0 + PX_NPAR * E_ROW, 240, F_Y - (E_P0 + PX_NPAR * E_ROW), UI_BG);
        uc.blk = (int)ui_blk; uc.type = type; uc.np = np; uc.item = (int)ui_item; uc.mode = (int)ui_mode; uc.on = on;
        return;
    }
    if (uc.item != (int)ui_item || uc.mode != (int)ui_mode) {   /* highlight moved: 2 items only */
        int old = uc.item;
        uc.item = (int)ui_item; uc.mode = (int)ui_mode;
        if (old != (int)ui_item) draw_item(old);
        draw_item((int)ui_item);
    }
    if (uc.on != on || uc.bnum != cur_preset) { draw_band(1); uc.on = on; }
    if (np > 0 && ui_loading_row(0) != uc.row_ld) draw_row(0);
    for (int k = 0; k < np; k++) if (uc.val[k] != PRESET.p[ui_blk][k]) draw_bar(k);
}

/* ---- render: sends only what changed to the screen ---- */
static void ui_render(void)
{
    if (ui_mode == M_TUNER || ui_mode == M_DIAG) return;   /* (the main loop draws them every 100 ms: tuner_draw, diag_draw) */
    if (ui_mode == M_SAVE) { save_draw(); return; }
    if (ui_mode == M_MIDI) { midi_draw(); return; }
    if (ui_mode == M_LIST) { list_draw(); return; }
#if PX_OWN_FX
    if (ui_mode == M_LEARN) { lt_draw(ul.now); return; }
    if (ui_mode == M_GUIDE) { lg_draw(); return; }
#endif
    int scr = UI_PAGE ? 1 : 0;
#if PX_OWN_FX
    {   /* (test build 47: PLAY VIEW SWITCHES on or off for this preset: PLAY drawn anew) */
        static int was_live = -1;
        const int lv = scr == 0 && ui_live() ? (int)px_live : 0;
        if (lv != was_live) { was_live = lv; uc.full = 1; }
    }
#endif
    if (scr != uc.screen) { uc.screen = scr; uc.full = 1; }
    int f = uc.full;
    if (f) {                                                     /* page change: background drawn once */
        if (scr == 0) {
            lcd_rect(0, 0, 240, S_H, UI_PANEL);
            lcd_rect(0, S_H, 240, P_NUM_Y - S_H, UI_BG);
            lcd_rect(0, P_NUM_Y, 6, 45, UI_BG); lcd_rect(90, P_NUM_Y, 4, 45, UI_BG);
            lcd_rect(94, P_NUM_Y, 142, 4, UI_BG); lcd_rect(94, P_NUM_Y + 27u, 142, 2, UI_BG); lcd_rect(94, P_NUM_Y + 44u, 142, 1, UI_BG);
            lcd_rect(236, P_NUM_Y, 4, 45, UI_BG);
            lcd_rect(0, P_NUM_Y + 45u, 240, P_NAME_Y - P_NUM_Y - 45u, UI_BG);
            lcd_rect(0, P_NAME_Y + 32u, 240, T_Y0 - 2u - P_NAME_Y - 32u, UI_BG);
            lcd_rect(16, P_NAME_Y + 36u, 208, 1, C_LINE);
            lcd_rect(0, T_Y0 - 2u, 2, 2u * T_DY + 2u, UI_BG); lcd_rect(236, T_Y0 - 2u, 4, 2u * T_DY + 2u, UI_BG);
            lcd_rect(0, T_Y0 + T_H + 2u, 240, T_DY - T_H - 4u, UI_BG);     /* between the two rows of tiles */
            lcd_rect(0, T_Y0 + T_DY + T_H + 2u, 240, P_INFO_Y - (T_Y0 + T_DY + T_H + 2u), UI_BG);
            lcd_rect(0, P_INFO_Y + 23u, 240, F_Y - P_INFO_Y - 23u, UI_BG);
        } else {
            lcd_rect(0, E_BAND, 240, 4, UI_BG);
            lcd_rect(0, E_BAND + 4u, 4, F_Y - E_BAND - 4u, UI_BG); lcd_rect(236, E_BAND + 4u, 4, F_Y - E_BAND - 4u, UI_BG);
        }
        lcd_rect(0, F_Y, 240, 3, UI_PANEL); lcd_rect(0, F_Y + 18u, 240, 240u - F_Y - 18u, UI_PANEL);
        if (scr) { lcd_rect(ST_X - 2u, F_Y + 3u, 2, 15, UI_PANEL); lcd_rect(ST_X + ST_W, F_Y + 3u, 240u - ST_X - ST_W, 15, UI_PANEL); }
        uc.full = 0;
    }
    if (scr == 0) {
        draw_status(f);
        draw_play_top(f);
#if PX_OWN_FX
        if (ui_live()) draw_live(f); else
#endif
        { draw_tiles(f); draw_info(f); }
    } else draw_edit(f);
    draw_hint(f);
    if (f) { char s[12]; for (int i = 0; i < 12; i++) s[i] = uc.status[i]; uc.full = 1; ui_status(s, uc.status_kind); uc.full = 0; }
}

/* ---- actions ---- */
static void ui_enter_block(uint32_t b)
{
    if (ui_blk == b && UI_PAGE) PRESET.on[b] ^= 1u;   /* 2nd press on the same button: on/off */
    ui_blk = b; ui_mode = M_BLOCK; ui_item = 0;
}
static uint8_t mv_orig[PX_NBLK];            /* the order before the move (HOME puts it back) */
static void ui_move_start(void)
{
    ui_order_fix();
    for (int i = 0; i < PX_NBLK; i++) mv_orig[i] = PRESET.order[i];
    ui_mode = M_MOVE;
}
static void ui_move_drop(void) { ui_mode = M_CHAIN; }
static void ui_move_cancel(void) { for (int i = 0; i < PX_NBLK; i++) PRESET.order[i] = mv_orig[i]; ui_mode = M_CHAIN; }
static void ui_knob(int dd, int fast)
{
    if (ui_mode == M_CHAIN) {                          /* along the chain, in the preset's order */
        ui_order_fix();
        ui_blk = PRESET.order[(ui_pos_of(ui_blk) + PX_NBLK + dd) % PX_NBLK];
    } else if (ui_mode == M_MOVE) {                    /* the grabbed block swaps with its neighbour */
        int i = ui_pos_of(ui_blk), j = i + (dd > 0 ? 1 : -1);
        if (j >= 0 && j < PX_NBLK) { uint8_t t = PRESET.order[i]; PRESET.order[i] = PRESET.order[j]; PRESET.order[j] = t; }
    } else if (ui_mode == M_BLOCK) {                   /* v42: in the order drawn: ON (item 1) at the top, MODEL, the settings */
        int n = ui_nitems(), p = ui_item < 2u ? 1 - (int)ui_item : (int)ui_item;
        p = (p + n + dd) % n;
        ui_item = (uint32_t)(p < 2 ? 1 - p : p);
    } else if (ui_mode == M_EDIT) {
        if (ui_item == 0u) {
            int nt = px_ntypes((int)ui_blk);
            PRESET.type[ui_blk] = (uint8_t)((PRESET.type[ui_blk] + nt + dd) % nt);
        } else if (ui_item >= 2u) {
            int k = (int)ui_item - 2, t = PRESET.type[ui_blk], ln = ui_list_n((int)ui_blk, t, k);
            if (ln) {                                  /* one filled item a detent, at any speed; it stops at the ends */
                char nm[12];
                for (int i = ui_list_of((int)ui_blk, PRESET.p[ui_blk][k]) + dd; i >= 0 && i < ln; i += dd)
                    if (ui_list_name((int)ui_blk, i, nm)) { PRESET.p[ui_blk][k] = (uint8_t)ui_list_val((int)ui_blk, i); break; }
                return;
            }
            int v = PRESET.p[ui_blk][k] + dd * (fast ? 5 : 1);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            PRESET.p[ui_blk][k] = (uint8_t)v;
        }
    }
}
static void ui_push(void)
{
    if (ui_mode == M_MOVE) { ui_move_drop(); return; }
    if (ui_mode == M_CHAIN) { ui_mode = M_BLOCK; ui_item = 0; }
    else if (ui_mode == M_BLOCK) {
        if (ui_item == 1u) PRESET.on[ui_blk] ^= 1u;
        else ui_mode = M_EDIT;
    } else ui_mode = M_BLOCK;
}
static void ui_home(void) { if (ui_mode == M_MOVE) ui_move_cancel(); ui_mode = M_CHAIN; ui_item = 0; }

/* ===================================================== save page ======== */
/* hold PUSH: SAVE. KNOB picks the slot (01..52, then "FACTORY": this slot back to its factory preset), PUSH saves,
 * HOME cancels. Saving to another slot copies the sound there and moves to it. */
#define SV_FACTORY 52u
static uint32_t sv_dest, sv_shown = ~0u;
static void save_open(void)
{
    ui_mode = M_SAVE; sv_dest = cur_preset; sv_shown = ~0u;
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 4, UI_ACC); lcd_rect(0, 27, 240, 3, UI_ACC); lcd_rect(0, 4, 8, 23, UI_ACC);
    ft_box(8, 4, 232, "SAVE PRESET", &FT_MD, BLK, UI_ACC, 0);
    ft_box(0, 40, 240, PRESET.name, &FT_MD, UI_TXT, UI_BG, 1);
    lcd_rect(20, 70, 200, 1, C_LINE);
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
}
static void save_draw(void)
{
    if (sv_shown == sv_dest) return;
    sv_shown = sv_dest;
    char b[20];
    int restore = sv_dest == SV_FACTORY, can = !restore || st_saved(cur_preset);   /* (v42: nothing to restore: PUSH does nothing) */
    ft_box(0, F_Y + 3u, 240, !can ? "KNOB: SLOT   HOME: NO" : restore ? "PUSH: RESTORE   HOME: NO" : "KNOB: SLOT  PUSH: SAVE  HOME: NO", &FT_SM, UI_DIM, UI_PANEL, 1);
    /* v42: the pedal cannot rename: a copy keeps the name (the web editor renames) */
    ft_box(0, 186, 240, restore || sv_dest == cur_preset ? "" : "NAME STAYS: RENAME IN THE EDITOR", &FT_SM, UI_DIM, UI_BG, 1);
    if (restore) {
        ft_box(0, 82, 240, "", &FT_XL, UI_ACC, UI_BG, 1);
        ft_box(0, 132, 240, "RESTORE FACTORY", &FT_MD, UI_TXT, UI_BG, 1);
        b[0] = 'S'; b[1] = 'L'; b[2] = 'O'; b[3] = 'T'; b[4] = ' ';
        b[5] = (char)('0' + (cur_preset + 1u) / 10u); b[6] = (char)('0' + (cur_preset + 1u) % 10u); b[7] = 0;
        ft_box(0, 160, 240, can ? b : "ALREADY THE FACTORY SOUND", &FT_SM, UI_DIM, UI_BG, 1);
        return;
    }
    px_preset_t d;
    st_get(sv_dest, &d);
    b[0] = (char)('0' + (sv_dest + 1u) / 10u); b[1] = (char)('0' + (sv_dest + 1u) % 10u); b[2] = 0;
    ft_box(0, 82, 240, b, &FT_XL, UI_ACC, UI_BG, 1);
    ft_box(0, 132, 240, sv_dest == cur_preset ? "THIS SLOT" : "REPLACES", &FT_MD, sv_dest == cur_preset ? UI_TXT : UI_DIM, UI_BG, 1);
    ft_box(0, 160, 240, sv_dest == cur_preset ? "" : d.name, &FT_SM, UI_TXT, UI_BG, 1);
}
static void save_close(void)
{
    ui_mode = M_CHAIN; ui_item = 0;
    lcd_rect(0, 0, 240, 240, UI_BG);
    uc.screen = -1;
    ui_invalidate();
}
static void save_knob(int dd)
{
    sv_dest = (sv_dest + SV_FACTORY + 1u + (uint32_t)(dd > 0 ? 1 : SV_FACTORY)) % (SV_FACTORY + 1u);
}
static void save_confirm(void)
{
    int rc;
    if (sv_dest == SV_FACTORY && !st_saved(cur_preset)) return;
    ft_box(0, 186, 240, "SAVING...", &FT_MD, UI_ACC, UI_BG, 1);
    if (sv_dest == SV_FACTORY) { rc = st_reset(cur_preset); }
    else {
#if PX_OWN_FX
        if (sv_dest != cur_preset) (void)px_midi_pl_copy(cur_preset, sv_dest);   /* (test build 46: its learned controls go with it) */
        sc_fold(); px_sc_tab[sv_dest] = px_sc;          /* (test build 49: its scenes, the one playing as it is) */
#endif
        rc = st_save(sv_dest, &PRESET); if (!rc) cur_preset = sv_dest;
    }
    st_gen++;
    if (!rc) load_preset(cur_preset);
    {
        char b[16] = "ERROR -";
        if (rc) { b[7] = (char)('0' + (uint32_t)(-rc) % 10u); b[8] = 0; }
        ft_box(0, 186, 240, rc ? b : "SAVED", &FT_MD, rc ? RED : GRN, UI_BG, 1);
    }
#ifdef UI_IDLE      /* test build 26, the Bluetooth audio image: the radio served while "SAVED" stays (openpocket.c: the music
                     * stopped for the whole 0.7 s, and its queue overflowed) */
    { const uint32_t t0 = UI_MS(); while (UI_MS() - t0 < 700u) { fm1_wdt_feed(); UI_IDLE(); } }
#else
    for (int i = 0; i < 7; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
#endif
    save_close();
}
/* ===================================================== MIDI page ======== */
/* hold FX: the MIDI settings (docs/MIDI.md). KNOB changes the selected row, PUSH moves to the next row and, after the
 * last one, leaves; HOME leaves. The settings are saved (a flash write, as SAVE) only if something changed.
 * v42: a fourth row, USB AUDIO (docs/USB-AUDIO.md): the row shows mi_ua, and the setting (px_ua_on) follows only on
 * leaving, because a change takes the pedal off USB for a second. */
/* test build 39 (PX_OWN_FX): the page is SETTINGS, with more rows: SWITCH, how CC 8..19 flip a block: MOMENT (a switch that
 * sends 127, then 0 when let go: the flip comes on the 0) or LATCH (a switch that alternates 127 and 0, one message a press:
 * every message flips); INPUT, the guitar's level into the chain, -10..+10 dB; and (test build 40) TAILS, the delay and the
 * reverb ring out when they go off and across a preset change, and BT POWER, the radio's transmit power (HIGH as before,
 * MID, LOW). Each is heard or applied at once and saved on leaving. The controls learned on the pedal are counted on the
 * USB line (the web editor's MIDI section learns them, px_midi.h).
 * Test build 41: the last row, USB PORT: COMPUTER (the USB device, as always) or FOOTSW (the USB-C port is the USB MIDI host of
 * a footswitch by cable, px_usbhost.h). Applied at once, so the USB line under the rows shows what the host sees while the
 * page is open (NOTHING ATTACHED, NO MIDI INTERFACE, LOW SPEED DEVICE, CONNECTED, LOST), and never saved: every power-on is
 * COMPUTER, and this row (or switching off) is the way back. */
/* Test build 43: USB audio in the Bluetooth audio image, its rows last: USB AUDIO (applied on leaving, as v42's), USB SEND,
 * USB RETURN and GUITAR (px_midi.h px_ua_route: heard at once, saved on leaving). 13 rows: the page shows 8 of 15 px and
 * scrolls with the selected one, a bar at the right edge saying where. USB RETURN's knob moves the return along this preset's
 * chain: INPUT, before each block after the first, OUTPUT */
#if PX_OWN_FX
enum { MI_CHAN, MI_SEND, MI_CLOCK, MI_EACH, MI_LIVE, MI_SCRULE, MI_SCFEET, MI_INPUT, MI_INZ, MI_PICKUP, MI_TAILS, MI_BTPWR,
       MI_OUT, MI_USB,
#if OP_UAUDIO
       MI_UAUDIO, MI_USEND, MI_URET, MI_UGTR,
#endif
       MI_ROWS };
#elif OP_UAUDIO
enum { MI_CHAN, MI_SEND, MI_CLOCK, MI_UAUDIO,
       MI_ROWS };
#else
enum { MI_CHAN, MI_SEND, MI_CLOCK,
       MI_ROWS };
#endif
#if PX_OWN_FX && !defined(PX_BT_POWER_APPLY)
#define PX_BT_POWER_APPLY() ((void)0)    /* (no Bluetooth here: the setting is only kept) */
#endif
#if PX_OWN_FX && !defined(PX_USB_MODE_APPLY)
#define PX_USB_MODE_APPLY() ((void)0)    /* (no USB host here: the row is only shown) */
#endif
#if PX_OWN_FX
#define MI_VW 12u                        /* a value's text: "BEFORE DLY" and its end */
#else
#define MI_VW 8u
#endif
static uint32_t mi_row, mi_shown_hash = ~0u;
static uint8_t mi_orig, mi_ua;
#if PX_OWN_FX
static uint8_t mi_set_orig, mi_out_orig, mi_route_orig, mi_inz_orig, mi_live_orig;
#endif
#if PX_OWN_FX && OP_UAUDIO
static uint32_t ed_ua_stat(uint32_t k);         /* px_uaudio.h: the streams' state (the USB line) */
/* USB RETURN's knob: along this preset's chain, INPUT, before each block after the first, OUTPUT (px_midi.h PX_UA_RET). A
 * place before the first block (set where the order differed) moves on as INPUT would */
static uint32_t mi_ret_step(uint32_t ret, int dd)
{
    uint8_t ord[PX_NBLK];
    uint32_t pos[PX_UA_RET_N], np = 0, i, cur = 0;
    if (px_order_valid(PRESET.order)) for (i = 0; i < PX_NBLK; i++) ord[i] = PRESET.order[i];
    else px_order_default(ord);
    pos[np++] = 1u;
    for (i = 1; i < PX_NBLK; i++) pos[np++] = 2u + ord[i];
    pos[np++] = 0u;
    for (i = 0; i < np; i++) if (pos[i] == ret) cur = i;
    return pos[(cur + (dd > 0 ? 1u : np - 1u)) % np];
}
#endif
static void midi_persist(void);                 /* openpocket.c: writes the settings to the store */
static void midi_open(void)
{
    ui_mode = M_MIDI; mi_row = 0; mi_shown_hash = ~0u; mi_orig = px_midi_cfg_pack(&mid_cfg); mi_ua = px_ua_on;
#if PX_OWN_FX
    mi_set_orig = px_set_pack(); mi_out_orig = px_out_mode; mi_route_orig = px_ua_route; mi_inz_orig = px_inz_pack();
    mi_live_orig = (uint8_t)(px_live | px_sc_rule << 2 | px_sc_feet << 3);
#endif
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 4, UI_ACC); lcd_rect(0, 27, 240, 3, UI_ACC); lcd_rect(0, 4, 8, 23, UI_ACC);
#if PX_OWN_FX
    ft_box(8, 4, 232, "SETTINGS", &FT_MD, BLK, UI_ACC, 0);
#else
    ft_box(8, 4, 232, "MIDI", &FT_MD, BLK, UI_ACC, 0);
#endif
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    ft_box(0, F_Y + 3u, 240, "KNOB: SET   PUSH: NEXT   HOME: OK", &FT_SM, UI_DIM, UI_PANEL, 1);
}
static void midi_draw(void)
{
    uint32_t h = mi_row | (uint32_t)px_midi_cfg_pack(&mid_cfg) << 4 | (uint32_t)mid_usb_up << 12 |
                 (uint32_t)mid_last[0] << 13 | (uint32_t)mid_last[1] << 16 | (uint32_t)mid_last[2] << 23 | (uint32_t)mi_ua << 31;
    h ^= (uint32_t)mid_c1_off * 0x27D4EB2Fu;
#if PX_OWN_FX
    for (uint32_t i = 0; i < PXM_MAP_N; i++) h ^= ((uint32_t)mid_map[i].src << 16 | (uint32_t)mid_map[i].dst << 8 | mid_map[i].mode) * (0x9E3779B1u + i);
    h ^= (uint32_t)px_set_pack() * 0x85EBCA6Bu;
    h ^= ((uint32_t)px_usb_foot << 4 | px_uh.st) * 0xC2B2AE35u;   /* (test build 41: the row, and what the host sees) */
    h ^= (uint32_t)px_out_mode * 0x165667B1u;        /* (test build 42: OUTPUT) */
    h ^= (uint32_t)px_ua_route * 0xD3A2646Cu;        /* (test build 43: USB SEND, USB RETURN, GUITAR) */
    h ^= ((uint32_t)px_inz_pack() | (uint32_t)px_live << 8 | (uint32_t)px_sc_rule << 10 | (uint32_t)px_sc_feet << 11) * 0x2545F491u;   /* (test build 47: GUITAR TONE and PICKUP, which 46
                                                                                   * never redrew; PLAY VIEW) */
#if OP_UAUDIO
    const uint32_t ust = a_ua ? ed_ua_stat(0) & 5u : 0u;   /* (the computer records: 1, plays: 4) */
    h ^= (ust + 1u) * 0x9E3779B9u;
#endif
#endif
    if (h == mi_shown_hash) return;
    mi_shown_hash = h;
#if PX_OWN_FX
    static const char *const NAME[MI_ROWS] = {"CHANNEL", "SEND", "CLOCK > DELAY",
                                              "SWITCH", "PLAY VIEW", "SCENE+SWITCH", "SCENE FOOTSW", "INPUT", "GUITAR TONE", "PICKUP",
                                              "TAILS", "BT POWER", "OUTPUT", "USB PORT",
#if OP_UAUDIO
                                              "USB AUDIO", "USB SEND", "USB RETURN", "GUITAR",
#endif
    };
#else
    static const char *const NAME[MI_ROWS] = {"CHANNEL", "SEND", "CLOCK > DELAY",
#if OP_UAUDIO
                                              "USB AUDIO",
#endif
    };
#endif
    char v[MI_ROWS][MI_VW];
    if (mid_cfg.chan) { v[0][0] = (char)(mid_cfg.chan >= 10 ? '0' + mid_cfg.chan / 10 : ' '); v[0][1] = (char)('0' + mid_cfg.chan % 10); v[0][2] = 0; }
    else { v[0][0] = 'O'; v[0][1] = 'M'; v[0][2] = 'N'; v[0][3] = 'I'; v[0][4] = 0; }
    v[1][0] = v[2][0] = 0;
    b_str(v[1], mid_cfg.send ? "ON" : "OFF");
    b_str(v[2], mid_cfg.clock ? "ON" : "OFF");
#if OP_UAUDIO
    v[MI_UAUDIO][0] = 0;
    b_str(v[MI_UAUDIO], mi_ua ? "ON" : "OFF");
#endif
#if PX_OWN_FX
    v[MI_EACH][0] = 0;
    b_str(v[MI_EACH], mid_cfg.each ? "LATCH" : "MOMENT");
    v[MI_INPUT][0] = 0;
    if (px_in_db > 0) b_str(v[MI_INPUT], "+"); else if (px_in_db < 0) b_str(v[MI_INPUT], "-");
    b_num(v[MI_INPUT], (uint32_t)(px_in_db < 0 ? -px_in_db : px_in_db)); b_str(v[MI_INPUT], " DB");
    {   /* test build 46: GUITAR TONE, the pickup's load (px_audio.h a_inz_apply), by its sound (47), and the pickup it assumes */
        static const char *const PU[3] = {"SINGLE", "HUMBUCK", "P90"};
        static const char *const LV[3] = {"CHAIN", "SWITCHES", "SCENES"};
        v[MI_INZ][0] = v[MI_PICKUP][0] = v[MI_LIVE][0] = v[MI_SCRULE][0] = v[MI_SCFEET][0] = 0;
        b_str(v[MI_INZ], PX_INZ_NAME[px_inz < PX_INZ_N ? px_inz : 0]);
        b_str(v[MI_PICKUP], PU[px_pickup <= 2u ? px_pickup : 0]);
        b_str(v[MI_LIVE], LV[px_live < 3u ? px_live : 0]);   /* (test build 47; 49: SCENES) */
        b_str(v[MI_SCRULE], px_sc_rule ? "SCENE WINS" : "SWITCH WINS");   /* (test build 49) */
        b_str(v[MI_SCFEET], px_sc_feet ? "PRESETS" : "SCENES");
    }
    v[MI_TAILS][0] = 0;
    b_str(v[MI_TAILS], px_tails ? "ON" : "OFF");
    v[MI_BTPWR][0] = 0;
    b_str(v[MI_BTPWR], px_bt_pwr == 2u ? "LOW" : px_bt_pwr == 1u ? "MID" : "HIGH");
    v[MI_OUT][0] = 0;
    b_str(v[MI_OUT], px_out_mode == 1u ? "STEREO" : px_out_mode == 2u ? "GTR+BASS" : "MONO");   /* test build 42 */
    v[MI_USB][0] = 0;
    b_str(v[MI_USB], px_usb_foot ? "FOOTSW" : "COMPUTER");
#if OP_UAUDIO
    {   /* test build 43 */
        static const char *const SND[3] = {"OUTPUT", "DI", "DI + OUT"};
        const uint32_t ret = PX_UA_RET(px_ua_route);
        v[MI_USEND][0] = v[MI_URET][0] = v[MI_UGTR][0] = 0;
        b_str(v[MI_USEND], SND[PX_UA_SEND(px_ua_route) <= 2u ? PX_UA_SEND(px_ua_route) : 0u]);
        if (ret >= 2u) { b_str(v[MI_URET], "BEFORE "); b_str(v[MI_URET], px_block_label((int)ret - 2)); }
        else b_str(v[MI_URET], ret ? "INPUT" : "OUTPUT");
        b_str(v[MI_UGTR], PX_UA_CUT(px_ua_route) ? "CUT" : "MIX");
    }
#endif
#endif
#if PX_OWN_FX
    /* (the rows share 126 px, 30 at most each, so the USB line stays at 162; the values in the small font in rows under 25.
     * Test build 41: 8 rows of 15 px, the small font's height, in the image; 9 with USB AUDIO, where it builds, of 14.
     * Test build 43: 8 rows at most on the page (MI_VIS), from mi_top: the selected one always among them) */
#define MI_VIS (MI_ROWS < 8u ? MI_ROWS : 8u)
    const uint32_t rh = MI_VIS > 4u ? (126u / MI_VIS < 25u ? 126u / MI_VIS : 25u) : 30u;
    const uint32_t ry = rh > 15u ? (rh - 15u) / 2u : 0u;   /* (rows no taller than the text: no gap line, the rows touch) */
    const uint32_t top = mi_row >= MI_VIS ? mi_row + 1u - MI_VIS : 0u;
    for (uint32_t q = 0; q < MI_VIS; q++) {
        const uint32_t r = top + q, y = 36u + q * rh;
        uint16_t bg = r == mi_row ? C_CARD : UI_BG;
        lcd_rect(0, y, 240, rh > 15u ? rh - 1u : rh, bg);
        ft_box(8, y + (rh == 30u ? 6u : ry), 130, NAME[r], &FT_SM, r == mi_row ? UI_TXT : UI_DIM, bg, 0);
        ft_box(140, y + (rh == 30u ? 2u : rh >= 25u ? 0u : ry), 92, v[r], rh >= 25u ? &FT_MD : &FT_SM, r == mi_row ? UI_ACC : UI_TXT, bg, 2);
    }
    if (MI_ROWS > MI_VIS) {                          /* where the page is in the list: a bar at the right edge */
        const uint32_t th = MI_VIS * rh, bh = th * MI_VIS / MI_ROWS, by = 36u + th * top / MI_ROWS;
        lcd_rect(236, 36, 4, th, UI_BG);
        lcd_rect(237, by, 2, bh, UI_DIM);
    }
#else
    for (uint32_t r = 0; r < MI_ROWS; r++) {       /* (v42: four rows of 30 px; three of 36 before) */
        uint32_t y = 36u + r * 30u;
        uint16_t bg = r == mi_row ? C_CARD : UI_BG;
        lcd_rect(0, y, 240, 28, bg);
        ft_box(8, y + 6u, 130, NAME[r], &FT_SM, r == mi_row ? UI_TXT : UI_DIM, bg, 0);
        ft_box(140, y + 2u, 92, v[r], &FT_MD, r == mi_row ? UI_ACC : UI_TXT, bg, 2);
    }
#endif
#if PX_OWN_FX
    {
        char u[48]; u[0] = 0;
        const uint32_t nl = px_midi_map_count();
        uint16_t col = mid_usb_up ? GRN : UI_DIM;
        if (px_usb_foot) {               /* test build 41: the port is the footswitch's host: what it sees (STARTING: not yet) */
            static const char *const UST[PXU_LOST + 1] = {"STARTING", "NOTHING ATTACHED", "NO MIDI INTERFACE", "LOW SPEED DEVICE",
                                                          "CONNECTED", "LOST"};
            const uint32_t s = px_uh.st <= PXU_LOST ? px_uh.st : PXU_OFF;
            b_str(u, "FOOTSW: "); b_str(u, UST[s]);
            col = s == PXU_OK ? GRN : s <= PXU_WAIT ? UI_ACC : RED;
            if (nl) {                    /* (when it fits) */
                char k[16]; k[0] = 0;
                b_str(k, "  LEARNED "); b_num(k, nl);
                if (ft_width(&FT_SM, u) + ft_width(&FT_SM, k) <= 236u) b_str(u, k);
            }
        } else {
            b_str(u, mid_usb_up ? "USB: HOST CONNECTED" : "USB: NO HOST");
#if OP_UAUDIO
            if (ust) b_str(u, ust == 5u ? "  REC + PLAY" : ust == 1u ? "  REC" : "  PLAY");   /* (test build 43: the streams open) */
#endif
            if (nl) {
                char k[16]; k[0] = 0;
                b_str(k, "  LEARNED "); b_num(k, nl);
                if (ft_width(&FT_SM, u) + ft_width(&FT_SM, k) <= 236u) b_str(u, k);
            }
        }
        ft_box(0, 162, 240, u, &FT_SM, col, UI_BG, 1);
    }
#else
    ft_box(0, 162, 240, mid_usb_up ? "USB: HOST CONNECTED" : "USB: NO HOST", &FT_SM, mid_usb_up ? GRN : UI_DIM, UI_BG, 1);
#endif
    char l[24]; l[0] = 0;
    if (mid_last[0] == PXM_PC) { b_str(l, "LAST: PC "); b_num(l, mid_last[1] + 1u); }
    else if (mid_last[0] == PXM_CC) { b_str(l, "LAST: CC "); b_num(l, mid_last[1]); b_str(l, " = "); b_num(l, mid_last[2]); }
    else b_str(l, "LAST: NONE");
    ft_box(0, 186, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
    /* v41: the second core retired (one core left until a power cycle: presets that need both go to BYP), and why */
    char c[32]; c[0] = 0;
    if (mid_c1_off) {
        static const char *const WHY[] = {"", "MISREAD", "LATE", "UNTIL POWER-OFF", "FAULT", "SILENT"};
        b_str(c, "CORE 2 OFF: "); b_str(c, mid_c1_off <= 5u ? WHY[mid_c1_off] : "?");
    }
    ft_box(0, 204, 240, c, &FT_SM, RED, UI_BG, 1);
}
static void midi_close(void)
{
    ui_mode = M_CHAIN; ui_item = 0;
#if PX_OWN_FX
    if (px_midi_cfg_pack(&mid_cfg) != mi_orig || mi_ua != px_ua_on || px_set_pack() != mi_set_orig || px_out_mode != mi_out_orig ||
        px_ua_route != mi_route_orig || px_inz_pack() != mi_inz_orig || (px_live | px_sc_rule << 2 | px_sc_feet << 3) != mi_live_orig) {
        px_ua_on = mi_ua; midi_persist();
    }
#else
    if (px_midi_cfg_pack(&mid_cfg) != mi_orig || mi_ua != px_ua_on) { px_ua_on = mi_ua; midi_persist(); }
#endif
    lcd_rect(0, 0, 240, 240, UI_BG);
    uc.screen = -1;
    ui_invalidate();
}
static void midi_knob(int dd)
{
    if (mi_row == MI_CHAN) mid_cfg.chan = (uint8_t)((mid_cfg.chan + 17u + (uint32_t)(dd > 0 ? 1 : 16)) % 17u);
    else if (mi_row == MI_SEND) mid_cfg.send ^= 1u;
    else if (mi_row == MI_CLOCK) mid_cfg.clock ^= 1u;
#if PX_OWN_FX
    else if (mi_row == MI_EACH) mid_cfg.each ^= 1u;
    else if (mi_row == MI_INPUT) { px_in_db = (int8_t)px_in_db_clamp(px_in_db + (dd > 0 ? 1 : -1)); a_in_gain_apply(); }   /* heard at once */
    else if (mi_row == MI_INZ) { px_inz_step(dd); a_inz_apply(); }   /* heard at once */
    else if (mi_row == MI_LIVE) px_live = (uint8_t)((px_live + (dd > 0 ? 1u : 2u)) % 3u);   /* (47: PLAY VIEW, at once, saved on
                                                       * leaving; 49: of three) */
    else if (mi_row == MI_SCRULE) px_sc_rule ^= 1u;   /* (test build 49) */
    else if (mi_row == MI_SCFEET) px_sc_feet ^= 1u;
    else if (mi_row == MI_PICKUP) { px_pickup = (uint8_t)((px_pickup + (dd > 0 ? 1u : 2u)) % 3u); a_inz_apply(); }
    else if (mi_row == MI_TAILS) { px_tails ^= 1u; a_tails_apply(); }
    else if (mi_row == MI_BTPWR) { px_bt_pwr = (uint8_t)((px_bt_pwr + (dd > 0 ? 1u : 2u)) % 3u); PX_BT_POWER_APPLY(); }
    else if (mi_row == MI_OUT) px_out_mode = (uint8_t)((px_out_mode + (dd > 0 ? 1u : 2u)) % 3u);   /* test build 42: at once, saved on leaving */
    else if (mi_row == MI_USB) { px_usb_foot ^= 1u; PX_USB_MODE_APPLY(); }   /* test build 41: at once, never saved */
#if OP_UAUDIO   /* test build 43: heard at once (the audio reads px_ua_route each block), saved on leaving */
    else if (mi_row == MI_USEND) px_ua_route = (uint8_t)((px_ua_route & ~3u) | (PX_UA_SEND(px_ua_route) + (dd > 0 ? 1u : 2u)) % 3u);
    else if (mi_row == MI_URET) px_ua_route = (uint8_t)((px_ua_route & 7u) | mi_ret_step(PX_UA_RET(px_ua_route), dd) << 3);
    else if (mi_row == MI_UGTR) px_ua_route ^= 4u;
#endif
#endif
    else mi_ua ^= 1u;
}
static void midi_push(void) { if (++mi_row >= MI_ROWS) midi_close(); }

/* v42: one unsaved edit survives a preset change by foot or from the list: stepping away keeps it, coming back to its slot
 * gives it back (still EDITED). An edit elsewhere takes its place; it is dropped if its slot was saved over meanwhile. */
static px_preset_t ui_kept;
static uint32_t ui_kept_slot = ~0u, ui_kept_sum;     /* (the sum of what the slot held when the edit was kept) */
static uint32_t ui_sum(const px_preset_t *p)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof *p; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
static void ui_goto(uint32_t n)
{
    if (ui_dirty()) { ui_kept = PRESET; ui_kept_slot = cur_preset; ui_kept_sum = ui_sum(&ui_ref); }
    cur_preset = n;
    load_preset(n);
    if (ui_kept_slot == n) {
        if (ui_sum(&PRESET) == ui_kept_sum) PRESET = ui_kept;
        ui_kept_slot = ~0u;
    }
}
static int ui_stepped;                 /* the last footswitch's step (+1, -1; 0: none): both footswitches take it back */
#if PX_OWN_FX
/* SYN PAD (the Bluetooth audio image's own effects, PX_OWN_FX): with a PAD on in the preset the left footswitch is the
 * pad's, not the previous preset: a press catches the chord being played (it never changes the preset), held 0.7 s it
 * fades the pad out; both footswitches still open the tuner, the pad let go (ui_action, ui_press, ui_buttons). The
 * includer gives the engine's two calls (openpocket.c: px_pad_press / px_pad_stop on g_eng) */
#ifndef PX_PAD_PRESS
#define PX_PAD_PRESS() ((void)0)
#endif
#ifndef PX_PAD_STOP
#define PX_PAD_STOP() ((void)0)
#endif
static int ui_pad_on(void) { return PRESET.on[PX_B_SYN] && PRESET.type[PX_B_SYN] % PX_SYN_COUNT == PX_SYN_PAD; }
#endif
static void ui_preset_step(int dd)
{
    int n = px_preset_count();
    ui_goto((uint32_t)(((int)cur_preset + n + dd) % n));
    ui_stepped = dd;
    if (UI_PAGE || ui_mode == M_LIST) { ui_mode = M_CHAIN; ui_item = 0; }   /* a footswitch on a block page goes back to PLAY, where the preset shows */
}
/* the FX button's blocks (the FX light follows them: px_lights.h) */
static const uint8_t UI_FXB[7] = {PX_B_DRIVE, PX_B_SFX, PX_B_SYN, PX_B_PITCH, PX_B_COMP, PX_B_GATE, PX_B_EQ};
static int ui_is_fx(uint32_t b) { for (int i = 0; i < 7; i++) if (UI_FXB[i] == b) return 1; return 0; }
static void ui_fx_cycle(void)          /* FX: the next of its blocks along the preset's chain, as the tiles read (v42), else the first */
{
    ui_order_fix();
    int from = ui_is_fx(ui_blk) ? ui_pos_of(ui_blk) + 1 : 0;
    for (int i = 0; i < PX_NBLK; i++) {
        uint32_t b = PRESET.order[(from + i) % PX_NBLK];
        if (ui_is_fx(b)) { ui_blk = b; break; }
    }
    ui_mode = M_BLOCK; ui_item = 0;
}

/* ===================================================== preset list ======== */
/* v42: HOME on PLAY: the knob browses the presets by name without loading them, PUSH loads the one in the middle, HOME goes
 * back. The playing preset's number is amber. */
#define LS_Y0  38u
#define LS_ROW 36u
static uint32_t ls_at, ls_shown = ~0u;
static void list_open(void)
{
    ui_mode = M_LIST; ls_at = cur_preset; ls_shown = ~0u;
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 4, UI_ACC); lcd_rect(0, 27, 240, 3, UI_ACC); lcd_rect(0, 4, 8, 23, UI_ACC);
    ft_box(8, 4, 232, "PRESETS", &FT_MD, BLK, UI_ACC, 0);
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    ft_box(0, F_Y + 3u, 240, "KNOB: PRESET   PUSH: LOAD", &FT_SM, UI_DIM, UI_PANEL, 1);
    uc.screen = -1;
}
static void list_draw(void)
{
    uint32_t key = ls_at | cur_preset << 8 | st_gen << 16;
    if (key == ls_shown) return;
    ls_shown = key;
    int n = px_preset_count();
    for (uint32_t r = 0; r < 5u; r++) {
        uint32_t k = (uint32_t)(((int)ls_at + n + (int)r - 2) % n), y = LS_Y0 + r * LS_ROW;
        int sel = r == 2u;
        uint16_t bg = sel ? C_CARD : UI_BG;
        px_preset_t q;
        char b[4];
        st_get(k, &q);
        b[0] = (char)('0' + (k + 1u) / 10u); b[1] = (char)('0' + (k + 1u) % 10u); b[2] = 0;
        ui_rbox(4, y, 232, LS_ROW - 2u, 5, bg, UI_BG);
        if (sel) lcd_rect(4, y + 10u, 3, 14, UI_ACC);
        ft_box(12, y + 5u, 40, b, &FT_MD, k == cur_preset ? UI_ACC : sel ? UI_TXT : UI_DIM, bg, 0);
        ft_box(52, y + 5u, 180, q.name, &FT_MD, sel ? WHT : UI_DIM, bg, 0);
    }
}
static void list_knob(int dd) { int n = px_preset_count(); ls_at = (uint32_t)(((int)ls_at + n + dd) % n); }
static void list_close(void) { ui_mode = M_CHAIN; ui_item = 0; uc.screen = -1; ui_invalidate(); }
static void list_load(void) { if (ls_at != cur_preset) ui_goto(ls_at); list_close(); }
#if PX_OWN_FX
/* ===================================================== the preset's MIDI table ======== */
/* test build 46: hold FX on a block page's MODEL row. This preset's own learned controls (KNOB picks one, hold FX clears it), then
 * the web editor's, which work in every preset (grey: the editor changes them). HOME or PUSH: back to the page */
#define LT_Y0 36u
#define LT_ROW 22u
#define LT_VIS 8u
static uint32_t lt_at, lt_shown = ~0u, lt_mode, lt_blk, lt_item;
static uint8_t lt_note;                                /* 1 cleared, 2 the editor's (not here) */
static uint32_t lt_note_t;
static uint32_t lt_rows(int *idx)                      /* the table's rows: this preset's entries, then the editor's (+ 100) */
{
    uint32_t n = 0, i, a;
    for (a = 0; a < 128u; a++)                         /* (by the controller's number) */
        for (i = 0; i < PXM_PL_N; i++) if (mid_pl[i].slot == cur_preset && mid_pl[i].src == a) idx[n++] = (int)i;
    for (i = 0; i < PXM_MAP_N; i++) if (mid_map[i].src < 128u && px_midi_known(mid_map[i].dst)) idx[n++] = 100 + (int)i;
    return n;
}
static void lt_target(char *t, uint32_t dst)           /* what one of the pedal's numbers drives, in words */
{
    px_cc_t c;
    px_cc_build();
    c = px_cc_tab[dst & 127u];
    switch (c.kind) {
    case PXM_TOGGLE: b_str(t, px_block_label(c.blk)); b_str(t, " ON/OFF"); return;
    case PXM_ON: b_str(t, px_block_label(c.blk)); b_str(t, " ON (PEDAL)"); return;
    case PXM_TYPE: b_str(t, px_block_label(c.blk)); b_str(t, " MODEL"); return;
    case PXM_PARAM: b_str(t, px_block_label(c.blk)); b_str(t, " "); b_str(t, px_param_name(c.blk, PRESET.type[c.blk], c.k)); return;
    case PXM_MASTER: b_str(t, "MASTER"); return;
    }
    if (dst - PXM_CC_SCENE0 < PX_SC_N) { b_str(t, "SCENE "); b_num(t, dst - PXM_CC_SCENE0 + 1u); return; }   /* (test build 49) */
    b_str(t, dst == PXM_CC_PAD ? "PAD CATCH" : dst == 116u ? "TAP TEMPO" : dst == 117u ? "EXP PEDAL" : dst == PXM_CC_NEXT ? "NEXT PRESET"
             : dst == PXM_CC_PREV ? "PREV PRESET" : "?");
}
static void lt_open(void)
{
    lt_mode = ui_mode; lt_blk = ui_blk; lt_item = ui_item;
    ui_mode = M_LEARN; lt_at = 0; lt_shown = ~0u; lt_note = 0;
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 4, UI_ACC); lcd_rect(0, 27, 240, 3, UI_ACC); lcd_rect(0, 4, 8, 23, UI_ACC);
    {
        char b[24] = "MIDI  PRESET ";
        b_num(b, cur_preset + 1u);
        ft_box(8, 4, 232, b, &FT_MD, BLK, UI_ACC, 0);
    }
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    uc.screen = -1;
}
static void lt_close(void) { ui_mode = lt_mode; ui_blk = lt_blk; ui_item = lt_item; uc.screen = -1; ui_invalidate(); }
static void lt_knob(int dd)
{
    int idx[PXM_PL_N + PXM_MAP_N];
    const uint32_t n = lt_rows(idx);
    if (n) lt_at = (uint32_t)(((int)lt_at + (int)n + dd) % (int)n);
}
static void lt_clear(uint32_t now)                     /* hold FX: the selected row's control forgotten (this preset's own) */
{
    int idx[PXM_PL_N + PXM_MAP_N];
    const uint32_t n = lt_rows(idx);
    lt_note_t = now;
    if (lt_at >= n) return;
    if (idx[lt_at] >= 100) { lt_note = 2; return; }
    memset(&mid_pl[idx[lt_at]], 0xFF, sizeof mid_pl[0]);
    learn_persist();
    lt_note = 1;
    if (lt_at && lt_at + 1u >= n) lt_at--;
}
static void lt_draw(uint32_t now)
{
    int idx[PXM_PL_N + PXM_MAP_N];
    const uint32_t n = lt_rows(idx);
    if (lt_note && now - lt_note_t >= 2500u) lt_note = 0;
    if (lt_at >= n) lt_at = n ? n - 1u : 0u;
    uint32_t key = lt_at | n << 8 | (uint32_t)lt_note << 16;
    for (uint32_t r = 0; r < n; r++)
        key = key * 31u + (uint32_t)idx[r] * 7u + (idx[r] < 100 ? mid_pl[idx[r]].src + mid_pl[idx[r]].lo * 3u + mid_pl[idx[r]].hi * 5u : 0u);
    if (key == lt_shown) return;
    lt_shown = key;
    const uint32_t top = lt_at >= LT_VIS ? lt_at + 1u - LT_VIS : 0u;
    char rg[32]; rg[0] = 0;                            /* (test build 47: the selected one's two ends, as its page shows them) */
    for (uint32_t q = 0; q < LT_VIS; q++) {
        const uint32_t r = top + q, y = LT_Y0 + q * LT_ROW;
        if (r >= n) {
            ui_rbox(4, y, 232, LT_ROW - 2u, 5, UI_BG, UI_BG);
            if (!n && q == 2u) ft_box(0, y + 3u, 240, "NOTHING LEARNED", &FT_SM, UI_DIM, UI_BG, 1);
            if (!n && q == 3u) ft_box(0, y + 3u, 240, "ON A BLOCK PAGE: HOLD FX", &FT_SM, UI_DIM, UI_BG, 1);
            continue;
        }
        const int sel = r == lt_at, mine = idx[r] < 100;
        const uint32_t src = mine ? mid_pl[idx[r]].src : mid_map[idx[r] - 100].src, dst = mine ? mid_pl[idx[r]].dst : mid_map[idx[r] - 100].dst;
        const uint16_t bg = sel ? C_CARD : UI_BG;
        char a[12] = "CC ", t[28];
        b_num(a, src);
        t[0] = 0;
        if (!mine) b_str(t, "ALL: ");
        lt_target(t, dst);
        if (sel && mine && mid_pl[idx[r]].mode == PXM_MAP_VALUE && (mid_pl[idx[r]].lo != 0u || mid_pl[idx[r]].hi != 100u) &&
            px_cc_tab[dst & 127u].kind == PXM_PARAM) {
            const px_cc_t c = px_cc_tab[dst & 127u];
            char v[12];
            b_str(rg, "RANGE ");
            for (int e = 0; e < 2; e++) {
                const int x = e ? mid_pl[idx[r]].hi : mid_pl[idx[r]].lo;
                v[0] = 0;
                if (!px_param_text(c.blk, PRESET.type[c.blk], c.k, x, v)) b_num(v, (uint32_t)x);
                b_str(rg, v);
                if (!e) b_str(rg, " TO ");
            }
        }
        ui_rbox(4, y, 232, LT_ROW - 2u, 5, bg, UI_BG);
        if (sel) lcd_rect(4, y + 4u, 3, 12, UI_ACC);
        ft_box(12, y + 3u, 60, a, &FT_SM, mine ? UI_ACC : UI_DIM, bg, 0);
        ft_box(72, y + 3u, 160, t, &FT_SM, mine ? (sel ? WHT : UI_TXT) : UI_DIM, bg, 0);
    }
    ft_box(0, F_Y + 3u, 240, lt_note == 1u ? "CLEARED" : lt_note == 2u ? "ALL PRESETS: IN THE EDITOR" : rg[0] ? rg : "HOLD FX: CLEAR   HOME: BACK",
           &FT_SM, lt_note == 1u || (!lt_note && rg[0]) ? UI_ACC : UI_DIM, UI_PANEL, 1);
}
#endif

/* ===================================================== tuner ======== */
/* v42: the tuner only; the readings for a photo are on the diagnostics page, HOME held on past the tuner */
static const char *NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int tn_last_note = -2, tn_last_x = -1, tn_last_c = 1000, tn_last_dir = 2;
#define TN_BAR_Y 172u
static void tuner_open(void)
{
    ui_mode = M_TUNER;
    a_tmute = 1;                                              /* v36: silent while tuning */
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 30, GRN);
    ft_box(8, 4, 140, "TUNER", &FT_MD, BLK, GRN, 0);
    ft_box(148, 8, 88, "A = 440", &FT_SM, BLK, GRN, 2);
    ui_rbox(18, TN_BAR_Y - 2u, 204, 14, 5, C_BAR, UI_BG);
    lcd_rect(110, TN_BAR_Y - 2u, 20, 14, ui_dark(GRN));
    lcd_rect(119, TN_BAR_Y - 10u, 2, 30, UI_TXT);
    ft_box(10, TN_BAR_Y + 18u, 40, "-50", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(100, TN_BAR_Y + 18u, 40, "0", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(190, TN_BAR_Y + 18u, 40, "+50", &FT_SM, UI_DIM, UI_BG, 1);
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    ft_box(0, F_Y + 3u, 240, "ANY SWITCH: EXIT  HOLD HOME: DIAG", &FT_SM, UI_DIM, UI_PANEL, 1);
    tn_last_note = -2; tn_last_x = -1; tn_last_c = 1000; tn_last_dir = 2;
}
static void tuner_close(void) { a_tmute = 0; ui_mode = M_CHAIN; lcd_rect(0, 0, 240, 240, UI_BG); uc.screen = -1; ui_invalidate(); }
static void tuner_draw(float hz)       /* hz <= 0: no note */
{
    int note = -1, c = 0; float cents = 0.0f;
    if (hz > 20.0f) {
        float st = 12.0f * logf(hz / 440.0f) * 1.44269504f;   /* semitones from A4 = 440 Hz */
        int r = (int)(st + (st >= 0 ? 0.5f : -0.5f));
        cents = (st - (float)r) * 100.0f;
        note = ((r % 12) + 12 + 9) % 12;                      /* A = 9 */
        c = (int)(cents + (cents >= 0 ? 0.5f : -0.5f));
    }
    int ok = (note >= 0 && c > -5 && c < 5);
    if (note != tn_last_note || (note >= 0 && (ok != (tn_last_c > -5 && tn_last_c < 5)))) {
        ft_box(56, 60, 128, note < 0 ? "-" : NOTE_N[note], &FT_LG, note < 0 ? UI_DIM : (ok ? GRN : UI_TXT), UI_BG, 1);
        tn_last_note = note;
    }
    int dir = note < 0 || ok ? 0 : c < 0 ? -1 : 1;           /* which way to turn: an arrow toward in tune, on the side the note is */
    if (dir != tn_last_dir) {
        ft_box(10, 76, 46, dir < 0 ? ">>" : "", &FT_MD, UI_ACC, UI_BG, 1);
        ft_box(184, 76, 46, dir > 0 ? "<<" : "", &FT_MD, UI_ACC, UI_BG, 1);
        tn_last_dir = dir;
    }
    if (c != tn_last_c || note < 0) {
        char b[8] = "";
        if (note >= 0) { int a = c < 0 ? -c : c; b[0] = (char)(c < 0 ? '-' : '+'); num3(b + 1, (uint32_t)a); }
        int i = 0; while (b[i]) i++;
        if (note >= 0) { b[i++] = ' '; b[i++] = 'c'; b[i++] = 't'; b[i] = 0; }
        ft_box(60, 128, 120, b, &FT_MD, ok ? GRN : UI_TXT, UI_BG, 1);
        tn_last_c = note < 0 ? 1000 : c;
    }
    int x = -1;
    if (note >= 0) { x = 120 + c * 2; if (x < 22) x = 22; if (x > 216) x = 216; }
    if (x != tn_last_x) {                                     /* needle: erase the old one, draw the new one */
        if (tn_last_x >= 0) {
            uint16_t under = (tn_last_x >= 108 && tn_last_x <= 132) ? ui_dark(GRN) : C_BAR;
            lcd_rect((uint32_t)tn_last_x - 3u, TN_BAR_Y - 2u, 7, 14, under);
            if (tn_last_x >= 107 && tn_last_x <= 133) { lcd_rect(110, TN_BAR_Y - 2u, 20, 14, ui_dark(GRN)); }
            lcd_rect(119, TN_BAR_Y - 10u, 2, 30, UI_TXT);
        }
        if (x >= 0) lcd_rect((uint32_t)x - 3u, TN_BAR_Y - 2u, 7, 14, (x >= 110 && x <= 130) ? GRN : UI_ACC);
        tn_last_x = x;
    }
}

/* ===================================================== diagnostics ======== */
/* v42 (on the tuner page before): what a bug report needs a photo of, as pictures with every figure written beside them.
 * HOME held on past the tuner; the knob turns the page (1: the DSP, 2: input and power); any control leaves. The sound
 * plays. Colours: green, amber above UI_LOAD_HIGH, red when a block ran late or the effects are bypassed. */
#define DG_X   8u
#define DG_W   228u
#define DG_BLK 4                       /* the blocks shown in "CPU PER BLOCK": the costliest, the first one highlighted */
static uint32_t dg_page, dg_hash[4], dg_clip_seen, dg_clip_hold;
static uint16_t dg_col(uint32_t pct, int bad) { return bad ? RED : pct > UI_LOAD_HIGH ? UI_ACC : GRN; }
static uint32_t dg_w(uint32_t v, uint32_t full, uint32_t w) { return v >= full ? w : v * w / full; }
/* a bar of w px for v of full, and a white tick at tick (0: none) with 2 px above and below it */
static void dg_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t v, uint32_t full, uint16_t col, uint32_t tick)
{
    uint32_t f = dg_w(v, full, w);
    lcd_rect(x, y - 2u, w, 2, UI_BG); lcd_rect(x, y + h, w, 2, UI_BG);
    if (f) lcd_rect(x, y, f, h, col);
    if (f < w) lcd_rect(x + f, y, w - f, h, C_BAR);
    if (tick) { uint32_t t = dg_w(tick, full, w); lcd_rect(x + (t ? t - 1u : 0u), y - 2u, 2, h + 4u, WHT); }
}
static void dg_chip(uint32_t x, uint32_t y, uint32_t w, const char *s, uint16_t col)   /* a counter: its text on a dark chip */
{
    ui_rbox(x, y, w, 18, 4, C_CARD, UI_BG);
    ft_box(x + 3u, y + 2u, w - 6u, s, &FT_SM, col, C_CARD, 1);
}
static int dg_top(uint8_t *idx, int n)  /* the costliest blocks (a_pf; PX_NBLK = the NAM network), most first: how many */
{
    uint8_t used[PX_NBLK + 1] = {0};
    int c = 0;
    for (; c < n; c++) {
        int best = -1;
        for (int k = 0; k <= PX_NBLK; k++) if (!used[k] && a_pf[k] && (best < 0 || a_pf[k] > a_pf[best])) best = k;
        if (best < 0) break;
        used[best] = 1; idx[c] = (uint8_t)best;
    }
    return c;
}
static void dg_pct(char *b, const char *label, uint32_t v) { b[0] = 0; b_str(b, label); b_num(b, v > 999u ? 999u : v); b_str(b, "%"); }
static int dg_changed(int k, uint32_t h) { if (h == dg_hash[k]) return 0; dg_hash[k] = h; return 1; }
static uint32_t dg_mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static void diag_dsp(void)             /* page 1: the load against the guard, each core, the counters, the CPU per block */
{
    static const char *const CORES[3] = {"C0", "C1", "2C"};
    uint32_t late = (uint32_t)bat_raw[2], clip = (uint32_t)bat_raw[3], pk = (uint32_t)bat_raw[4];
    int bad = ui_dg.late || ui_dg.bypass;
    uint16_t c = dg_col(ui_dg.load, bad);
    char b[32];
    uint32_t h = 2166136261u;
    h = dg_mix(h, ui_dg.load); h = dg_mix(h, ui_dg.peak); h = dg_mix(h, ui_dg.cores); h = dg_mix(h, ui_dg.guard); h = dg_mix(h, (uint32_t)bad);
    if (dg_changed(0, h)) {
        dg_pct(b, "DSP ", ui_dg.load);
        ft_box(DG_X, 34, 120, b, &FT_MD, c, UI_BG, 0);
        dg_pct(b, "PEAK ", ui_dg.peak);
        ft_box(128, 38, 104, b, &FT_SM, UI_TXT, UI_BG, 2);
        dg_bar(10, 60, 220, 6, ui_dg.load, 100, c, ui_dg.guard);
        b[0] = 0; b_str(b, "CORES "); b_str(b, CORES[ui_dg.cores < 3u ? ui_dg.cores : 0u]);
        ft_box(DG_X, 70, 80, b, &FT_SM, UI_TXT, UI_BG, 0);
        dg_pct(b, "GUARD ", ui_dg.guard);
        ft_box(152, 70, 80, b, &FT_SM, UI_DIM, UI_BG, 2);
    }
    h = 2166136261u;
    for (int k = 0; k < 2; k++) h = dg_mix(h, ui_dg.cl[k]);
    h = dg_mix(h, ui_dg.split); h = dg_mix(h, ui_dg.guard);
    if (dg_changed(1, h)) {
        for (uint32_t k = 0; k < 2u; k++) {                  /* each core (with the layer split; else "--") */
            uint32_t y = 88u + k * 16u;
            b[0] = 0; b_str(b, k ? "CPU1 " : "CPU0 ");
            if (ui_dg.split) { b_num(b, ui_dg.cl[k]); b_str(b, "%"); } else b_str(b, "--");
            ft_box(DG_X, y, 78, b, &FT_SM, ui_dg.split ? UI_TXT : UI_DIM, UI_BG, 0);
            dg_bar(88, y + 5u, 144, 6, ui_dg.split ? ui_dg.cl[k] : 0u, 100, dg_col(ui_dg.cl[k], 0), ui_dg.guard);
        }
    }
    h = dg_mix(dg_mix(dg_mix(dg_mix(2166136261u, late), clip), pk), ui_dg.bypass);
    if (dg_changed(2, h)) {                                /* the counters since boot, red (or amber) once not zero */
        dg_pct(b, "PK ", pk);                              /* (the highest block load since boot) */
        ft_box(88, 70, 64, b, &FT_SM, dg_col(pk, pk >= 100u), UI_BG, 1);
        b[0] = 0; b_str(b, "LATE "); b_num(b, late > 9999u ? 9999u : late); dg_chip(2, 122, 76, b, late ? RED : GRN);
        b[0] = 0; b_str(b, "CLIP "); b_num(b, clip > 9999u ? 9999u : clip); dg_chip(79, 122, 76, b, clip ? UI_ACC : GRN);
        dg_chip(156, 122, 82, ui_dg.bypass ? "BYPASS ON" : "BYPASS OFF", ui_dg.bypass ? RED : GRN);
    }
    uint8_t top[DG_BLK];
    int n = dg_top(top, DG_BLK);
    h = dg_mix(2166136261u, a_wcut);
    for (int i = 0; i < n; i++) h = dg_mix(dg_mix(h, top[i]), a_pf[top[i]]);
    if (dg_changed(3, dg_mix(h, (uint32_t)n))) {
        ft_box(DG_X, 143, 140, "CPU PER BLOCK %", &FT_SM, UI_DIM, UI_BG, 0);
        b[0] = 0;
        if (a_wcut) { b_str(b, "CUT "); b_num(b, a_wcut > 9999u ? 9999u : a_wcut); }   /* v39: wet blocks cut short */
        ft_box(148, 143, 84, b, &FT_SM, RED, UI_BG, 2);
        for (int i = 0; i < DG_BLK; i++) {                   /* sorted, the heaviest in amber */
            uint32_t y = 160u + (uint32_t)i * 15u;
            if (i >= n) { lcd_rect(0, y, 240, 15, UI_BG); continue; }
            const char *l = top[i] == PX_NBLK ? "NAM" : px_block_label(top[i]);
            ft_box(DG_X, y, 42, l, &FT_SM, i ? UI_TXT : WHT, UI_BG, 0);
            dg_bar(52, y + 5u, 148, 5, a_pf[top[i]], 100, i ? UI_DIM : UI_ACC, 0);
            b[0] = 0; b_num(b, a_pf[top[i]]);
            ft_box(202, y, 34, b, &FT_SM, i ? UI_TXT : UI_ACC, UI_BG, 2);
        }
    }
}
#define DG_DB_MIN 90                    /* the input meters: -90 dB to full scale; the last 3 dB are the clip zone */
static int r_str(char *b, int32_t v) { if (v < 0) { b[0] = '-'; b[1] = '-'; return 2; } char t[8]; t[0] = 0; b_num(t, (uint32_t)v); int i = 0; while (t[i]) { b[i] = t[i]; i++; } return i; }
static void diag_power(void)           /* page 2: the input levels, the output's soft clip, the battery, the raw readings */
{
    char b[40];
    int lv[2] = {a_level_db(0), a_level_db(1)};
    uint32_t clip = (uint32_t)bat_raw[3];
    if (clip != dg_clip_seen) { dg_clip_seen = clip; dg_clip_hold = 20u; }   /* lit for 20 draws (2 s) after a clip */
    else if (dg_clip_hold) dg_clip_hold--;
    uint32_t h = dg_mix(dg_mix(2166136261u, (uint32_t)lv[0]), (uint32_t)lv[1]);
    if (dg_changed(0, h)) {
        for (int ch = 0; ch < 2; ch++) {                     /* "L -62 DB" and a meter: green, amber above -18, red above -6 */
            uint32_t y = 54u + (uint32_t)ch * 18u;
            int v = lv[ch] < -DG_DB_MIN ? -DG_DB_MIN : lv[ch] > 0 ? 0 : lv[ch];
            b[0] = 0; b_str(b, ch ? "R " : "L ");
            if (lv[ch] <= -199) b_str(b, "---"); else { if (lv[ch] < 0) b_str(b, "-"); b_num(b, (uint32_t)(lv[ch] < 0 ? -lv[ch] : lv[ch])); }
            b_str(b, " DB");
            ft_box(DG_X, y, 74, b, &FT_SM, UI_TXT, UI_BG, 0);
            uint32_t w = 150u, zone = w * 3u / DG_DB_MIN, f = dg_w((uint32_t)(v + DG_DB_MIN), DG_DB_MIN, w);
            lcd_rect(82, y + 2u, w, 2, UI_BG); lcd_rect(82, y + 12u, w, 2, UI_BG);
            if (f) lcd_rect(82, y + 4u, f, 8, v > -6 ? RED : v > -18 ? UI_ACC : GRN);
            if (f < w - zone) lcd_rect(82 + f, y + 4u, w - zone - f, 8, C_BAR);
            if (f < w) { uint32_t s = f > w - zone ? f : w - zone; lcd_rect(82 + s, y + 4u, w - s, 8, ui_dark(RED)); }   /* the clip zone */
        }
    }
    h = dg_mix(dg_mix(2166136261u, clip), dg_clip_hold ? 1u : 0u);
    if (dg_changed(1, h)) {                                  /* the output: a marker lit while it reaches the soft clipper */
        lcd_rect(DG_X, 112, 14, 14, dg_clip_hold ? RED : C_BAR);
        b[0] = 0; b_str(b, "SOFT CLIP: "); b_num(b, clip); b_str(b, " BLOCKS");
        ft_box(28, 111, 204, b, &FT_SM, dg_clip_hold ? RED : UI_TXT, UI_BG, 0);
    }
    int usb = bat_usb_mv > 4300, pct = bat_pct >= 0 ? bat_pct : bat_mv ? bat_percent(bat_mv) : -1;
    h = dg_mix(dg_mix(dg_mix(2166136261u, (uint32_t)bat_mv), (uint32_t)usb), (uint32_t)pct);
    if (dg_changed(2, h)) {                                  /* the battery: a cell, its volts, its percent (or USB) */
        uint16_t c = usb ? GRN : pct <= 15 ? RED : pct <= 30 ? UI_ACC : GRN;
        uint32_t f = pct < 0 ? 0u : (uint32_t)pct * 40u / 100u;
        lcd_rect(DG_X, 156, 46, 22, UI_TXT); lcd_rect(DG_X + 2u, 158, 42, 18, UI_BG); lcd_rect(DG_X + 46u, 162, 3, 10, UI_TXT);
        if (f) lcd_rect(DG_X + 3u, 159, f, 16, c);
        if (f < 40u) lcd_rect(DG_X + 3u + f, 159, 40u - f, 16, UI_BG);
        b[0] = 0;
        if (bat_mv) { b_num(b, (uint32_t)bat_mv / 1000u); b_str(b, "."); b_num(b, (uint32_t)bat_mv / 100u % 10u); b_num(b, (uint32_t)bat_mv / 10u % 10u); b_str(b, " V"); }
        else b_str(b, "-- V");
        ft_box(62, 156, 96, b, &FT_MD, UI_TXT, UI_BG, 0);
        b[0] = 0;
        if (usb) b_str(b, "USB"); else if (pct >= 0) { b_num(b, (uint32_t)pct); b_str(b, "%"); } else b_str(b, "--");
        ft_box(158, 160, 74, b, &FT_SM, c, UI_BG, 2);
    }
    h = dg_mix(dg_mix(2166136261u, (uint32_t)bat_raw[0]), (uint32_t)bat_raw[1]);
    if (dg_changed(3, h)) {                                  /* the battery's raw reading (PC9) and the status bit beside it */
        int i = 0; b[0] = 0; b_str(b, "RAW  C9 "); while (b[i]) i++;
        i += r_str(b + i, bat_raw[0]); b[i] = 0; b_str(b, "  CH "); while (b[i]) i++; i += r_str(b + i, bat_raw[1]); b[i] = 0;
        ft_box(DG_X, 190, DG_W, b, &FT_SM, UI_DIM, UI_BG, 0);
    }
}
static void diag_frame(void)           /* the page's fixed parts */
{
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 30, UI_DIM);
    ft_box(8, 4, 150, "DIAGNOSTICS", &FT_MD, BLK, UI_DIM, 0);
    {
        char b[16]; b[0] = 0;
        b_str(b, dg_page ? "2/2" : "1/2");
#ifdef OP_VERSION
        b_str(b, "  " OP_VERSION);
#endif
        ft_box(158, 8, 78, b, &FT_SM, BLK, UI_DIM, 2);
    }
    if (dg_page) {
        ft_box(DG_X, 36, DG_W, "INPUT (DB BELOW FULL SCALE)", &FT_SM, UI_DIM, UI_BG, 0);
        ft_box(DG_X, 94, DG_W, "OUTPUT", &FT_SM, UI_DIM, UI_BG, 0);
        ft_box(DG_X, 138, DG_W, "BATTERY", &FT_SM, UI_DIM, UI_BG, 0);
    }
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    ft_box(0, F_Y + 3u, 240, dg_page ? "KNOB: PAGE 1   PHOTO BOTH PAGES" : "KNOB: PAGE 2   PHOTO BOTH PAGES", &FT_SM, UI_DIM, UI_PANEL, 1);
    for (int k = 0; k < 4; k++) dg_hash[k] = ~0u;
}
static void diag_open(void)
{
    ui_mode = M_DIAG;
    a_tmute = 0;                                              /* (the sound plays: the figures are of it) */
    dg_page = 0;
    dg_clip_seen = (uint32_t)bat_raw[3]; dg_clip_hold = 0;   /* (the marker lights for a clip from now on) */
    diag_frame();
}
static void diag_turn(void) { dg_page ^= 1u; diag_frame(); }
static void diag_close(void) { ui_mode = M_CHAIN; ui_item = 0; lcd_rect(0, 0, 240, 240, UI_BG); uc.screen = -1; ui_invalidate(); }
static void diag_draw(void) { if (dg_page) diag_power(); else diag_dsp(); }

/* ===================================================== controls ======== */
/* What each control does on each page, in one place: the main loop (openpocket.c) and the simulator (tests/edsim.c) give
 * ui_buttons the debounced buttons and ui_turn the knob; the simulator's named gestures call ui_action. */
enum { UB_FSUP, UB_FSDN, UB_PUSH, UB_HOME, UB_FX, UB_MOD, UB_AMP, UB_CAB, UB_DLY, UB_REV, UB_N };   /* a button: bit 1 << UB_x */
enum { UA_SAVE = UB_N, UA_MOVE, UA_TUNER, UA_MIDI, UA_FEET, UA_VIEW };   /* the gestures (a button's plain press is its UB_x;
                                                                        * UA_VIEW, test build 49: CAB held) */
#define UI_HOLD_PUSH 800u              /* hold PUSH: SAVE */
#define UI_HOLD_HOME 700u              /* hold HOME: the tuner; held on as long again: the diagnostics */
#define UI_HOLD_FX   800u              /* hold FX: MIDI */
#define UI_CLICK2    350u              /* PUSH again within this, after a click opened a block from PLAY: MOVE that block */
#define UI_FEET      150u              /* the other footswitch within this: both, the tuner */
#if PX_OWN_FX
#define UI_HOLD_PAD  700u              /* hold the left footswitch, a SYN PAD on: the pad fades */
#endif
static struct {
    uint32_t held, t[UB_N], clk_t, fs_t, now;  /* (now: the last pass's time, for a gesture's learn) */
    uint8_t fired[UB_N], clk, clk_blk;
    int8_t fs, fs_dir;                 /* the last footswitch pressed (-1: none) and the step it made */
#if PX_OWN_FX
    uint8_t fs_pad;                    /* it caught a PAD's chord (ui_action): held on, it fades the pad; the other one: the tuner */
    uint8_t fs_sc;                     /* (test build 49: 1 + the scene it left, in SCENES' view: the other one takes it back) */
#endif
} ug = {.fs = -1};
#if PX_OWN_FX
static void lg_action(int a)                           /* (ui_action on this page: HOME and PUSH, nothing else) */
{
    if (a == UB_HOME) { lg_close(UL_CANCEL); return; }
    if (a != UB_PUSH && a != UA_SAVE) return;          /* (PUSH held a little long: PUSH all the same) */
    if (lg.step == LG_TYPE) { lg_arm(); lg.step = LG_LISTEN; }
    else if (lg.step == LG_LO) {
        lg.lo = PRESET.p[lg.blk][lg.k]; lg.clo = mid_learn.last;
        PRESET.p[lg.blk][lg.k] = lg.orig;              /* (the toe from the preset's own value: PUSH alone keeps it) */
        lg.step = LG_HI;
    } else if (lg.step == LG_HI) lg_finish(PRESET.p[lg.blk][lg.k], mid_learn.last);
}
#endif
static void ui_action(int a)
{
    static const uint8_t BLK_OF[5] = {PX_B_MOD, PX_B_AMP, PX_B_CAB, PX_B_DELAY, PX_B_REVERB};
    switch (ui_mode) {
    case M_SAVE: if (a == UB_PUSH) save_confirm(); else if (a == UB_HOME) save_close(); return;
#if PX_OWN_FX   /* test build 47: the footswitches step through the rows, the right one down, the left one up, round */
    case M_MIDI:
        if (a == UB_PUSH) midi_push(); else if (a == UB_HOME) midi_close();
        else if (a == UB_FSUP || a == UB_FSDN) mi_row = (mi_row + (a == UB_FSUP ? 1u : MI_ROWS - 1u)) % MI_ROWS;
        return;
#else
    case M_MIDI: if (a == UB_PUSH) midi_push(); else if (a == UB_HOME) midi_close(); return;
#endif
    case M_TUNER: if (a == UA_TUNER) diag_open(); else tuner_close(); return;   /* any control leaves; HOME held on: diagnostics */
    case M_DIAG: if (a != UA_TUNER) diag_close(); return;
    case M_MOVE: if (a == UB_PUSH) ui_move_drop(); else if (a == UB_HOME) ui_move_cancel(); return;
#if PX_OWN_FX
    case M_LEARN: if (a == UA_MIDI) lt_clear(ug.now); else if (a == UB_HOME || a == UB_PUSH) lt_close(); return;   /* (test build 46) */
    case M_GUIDE: lg_action(a); return;                /* (test build 48) */
#endif
    }
#if PX_OWN_FX
    if ((a == UB_FSUP || a == UB_FSDN) && px_live == 2u && !px_sc_feet) {   /* test build 49: SCENES' view: the scenes, round */
        ug.fs_sc = (uint8_t)(1u + px_sc.cur);
        ui_scene_recall((px_sc.cur + (a == UB_FSUP ? 1u : PX_SC_N - 1u)) % PX_SC_N);
        sc_sel = px_sc.cur;
        return;
    }
    if (ui_mode == M_CHAIN && px_live == 2u && ui_live()) {   /* (its tiles: the knob picks, PUSH recalls, hold FX learns) */
        if (a == UB_PUSH) { ui_scene_recall(sc_sel); return; }
        if (a == UA_MIDI) {
            const int e = px_midi_pl_row(cur_preset, PXM_CC_SCENE0 + sc_sel, 0xFFu);
            ul.note_t = ug.now;
            if (e < 0) { lg_scene(sc_sel); return; }
            ul.cc = mid_pl[e].src; memset(&mid_pl[e], 0xFF, sizeof mid_pl[e]); ul.note = UL_CLEARED;   /* (it had one: cleared) */
            learn_persist();
            return;
        }
    }
#endif
    switch (a) {
    case UB_FSUP: ui_preset_step(+1); break;
#if PX_OWN_FX
    case UB_FSDN: if (ui_pad_on()) { ug.fs_pad = 1; PX_PAD_PRESS(); } else ui_preset_step(-1); break;   /* SYN PAD: the pad's */
    case UA_VIEW:                                      /* test build 49: CAB held: PLAY's next view, CHAIN, SWITCHES, SCENES */
        px_live = (uint8_t)((px_live + 1u) % 3u); lv_peek = 0; sc_sel = px_sc.cur;
        ui_home();
        break;
#else
    case UB_FSDN: ui_preset_step(-1); break;
#endif
    case UB_PUSH: if (ui_mode == M_LIST) list_load(); else ui_push(); break;
    case UB_HOME: if (ui_mode == M_CHAIN) list_open(); else if (ui_mode == M_LIST) list_close(); else ui_home(); break;
    case UB_FX: ui_fx_cycle(); break;
    case UA_SAVE: save_open(); break;
    case UA_MOVE: if (ui_mode == M_CHAIN) ui_move_start(); break;
    case UA_TUNER: case UA_FEET: tuner_open(); break;
#if PX_OWN_FX
    case UA_MIDI: if (UI_PAGE) ui_learn_hold(ug.now); else midi_open(); break;   /* test build 46: on a block page, LEARN */
#else
    case UA_MIDI: midi_open(); break;
#endif
    default: ui_enter_block(BLK_OF[a - UB_MOD]);
    }
}
static void ui_turn(int dd, int fast)  /* the PARA knob, one detent (fast: turned quickly) */
{
    ug.clk = 0;
#if PX_OWN_FX
    if (ui_mode == M_CHAIN && px_live == 2u && ui_live()) { sc_sel = (uint8_t)((sc_sel + (dd > 0 ? 1u : PX_SC_N - 1u)) % PX_SC_N); return; }
    if (ui_mode == M_CHAIN && px_live == 1u) { lv_peek = 1; lv_peek_t = ul.now; }   /* (test build 47: the chain, a while) */
#endif
    if (ui_mode == M_SAVE) save_knob(dd);
    else if (ui_mode == M_MIDI) midi_knob(dd);
    else if (ui_mode == M_LIST) list_knob(dd);
#if PX_OWN_FX
    else if (ui_mode == M_LEARN) lt_knob(dd);
    else if (ui_mode == M_GUIDE) lg_knob(dd, fast);
#endif
    else if (ui_mode == M_DIAG) diag_turn();
    else if (ui_mode != M_TUNER) ui_knob(dd, fast);
}
static void ui_press(int b, uint32_t now)   /* a button's plain press: at its press, PUSH HOME FX at a short release */
{
    if (b == UB_FSUP || b == UB_FSDN) {        /* v42: both footswitches = the tuner. The first acts at once, as always; the
                                                * other one soon after, while the first is still down, takes its step back
                                                * and opens the tuner */
        ug.clk = 0;
        if (ug.fs >= 0 && ug.fs != b && (ug.held >> ug.fs & 1u) && now - ug.fs_t < UI_FEET) {
            int d = ug.fs_dir;
            ug.fs = -1;
            if (d) { ui_preset_step(-d); ui_action(UA_FEET); }
#if PX_OWN_FX
            else if (ug.fs_pad) { ug.fs_pad = 0; PX_PAD_STOP(); ui_action(UA_FEET); }   /* the first caught a chord: let go */
            else if (ug.fs_sc) { ui_scene_recall(ug.fs_sc - 1u); sc_sel = px_sc.cur; ug.fs_sc = 0; ui_action(UA_FEET); }   /* (49) */
#endif
            return;
        }
        ug.fs = (int8_t)b; ug.fs_t = now; ui_stepped = 0;
#if PX_OWN_FX
        ug.fs_pad = 0; ug.fs_sc = 0;
#endif
        ui_action(b);
        ug.fs_dir = (int8_t)ui_stepped;
        return;
    }
    if (b == UB_PUSH) {                        /* v42: a click on PLAY opens the block at once; a 2nd one soon after: MOVE it */
        if (ug.clk && ui_mode == M_BLOCK && ui_item == 0u && ui_blk == ug.clk_blk) {
            ug.clk = 0;
            ui_mode = M_CHAIN;
            ui_action(UA_MOVE);
            return;
        }
        int chain = ui_mode == M_CHAIN;
        ui_action(UB_PUSH);
        ug.clk = (uint8_t)(chain && ui_mode == M_BLOCK); ug.clk_t = now; ug.clk_blk = (uint8_t)ui_blk;
        return;
    }
    ug.clk = 0;
    ui_action(b);
}
static void ui_buttons(uint32_t now, uint32_t held)   /* every pass: which buttons are down (debounced), bit 1 << UB_x */
{
    uint32_t dn = held & ~ug.held, up = ug.held & ~held;
    ug.held = held; ug.now = now;
    if (ug.clk && now - ug.clk_t >= UI_CLICK2) ug.clk = 0;
    for (int b = 0; b < UB_N; b++) {
        int timed = b == UB_PUSH || b == UB_HOME || b == UB_FX;   /* (these have a hold: they act at the release) */
#if PX_OWN_FX
        timed |= b == UB_CAB;                          /* (test build 49: CAB held, PLAY's next view) */
#endif
        uint32_t hold = b == UB_PUSH ? UI_HOLD_PUSH : b == UB_HOME ? UI_HOLD_HOME : UI_HOLD_FX;
        if (dn >> b & 1u) { ug.t[b] = now; ug.fired[b] = 0; if (!timed) ui_press(b, now); }
        else if (timed && (held >> b & 1u) && (b == UB_HOME || !ug.fired[b]) && now - ug.t[b] >= hold) {
            ug.fired[b] = 1; ug.t[b] = now; ug.clk = 0;              /* (HOME: each further hold as long goes one page on) */
#if PX_OWN_FX
            ui_action(b == UB_PUSH ? UA_SAVE : b == UB_HOME ? UA_TUNER : b == UB_FX ? UA_MIDI : UA_VIEW);
#else
            ui_action(b == UB_PUSH ? UA_SAVE : b == UB_HOME ? UA_TUNER : UA_MIDI);
#endif
        }
        if ((up >> b & 1u) && timed && !ug.fired[b]) ui_press(b, now);
    }
#if PX_OWN_FX
    if (ug.fs_pad && (held >> UB_FSDN & 1u) && now - ug.t[UB_FSDN] >= UI_HOLD_PAD) { ug.fs_pad = 0; PX_PAD_STOP(); }   /* held: fade */
    ui_learn_tick(now);                                /* (test build 46) */
#endif
}
