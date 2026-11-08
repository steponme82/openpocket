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
 *  | PUSH EDIT   FOOT: PRESETS    |       | PUSH EDIT   HOME BACK C1 42% |
 *  +------------------------------+       +------------------------------+
 *
 * Controls: KNOB = select / adjust; PUSH = open / edit / confirm; HOME = back to PLAY (long press = tuner);
 * FOOTSWITCHES = next / previous preset (with a SYN PAD on: the left one catches the chord, held 0.7 s fades it); AMP CAB DLY REV MOD = that block's page (press again = on/off);
 * FX = cycles DRV, SFX, SYN, PIT, CMP, GT, EQ. Hold PUSH = SAVE (into one of the 52 slots, in flash).
 * Double-click PUSH on PLAY = MOVE the selected block: KNOB moves it along the chain, PUSH drops it, HOME puts it back.
 * The preset LEVEL (v36) is the GT page's 3rd setting ("PRESET LVL").
 */
#pragma once

enum { M_CHAIN, M_BLOCK, M_EDIT, M_TUNER, M_SAVE, M_MOVE, M_MIDI };
static uint32_t st_gen;                 /* bumped on each save: the "EDITED" reference reloads */
static void st_get(uint32_t k, px_preset_t *p);
static int st_save(uint32_t k, const px_preset_t *p);
static int st_reset(uint32_t k);
static int st_saved(uint32_t k);
static void save_draw(void);
static void midi_draw(void);
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

/* ---- cache of what is on screen ---- */
static struct {
    int full, screen;                  /* screen: 0 PLAY, 1 EDIT */
    uint32_t preset, vol; char name[14];
    int dirty, amp_t, cab_t, amp_on, cab_on, amp_m, cab_m;
    uint8_t tile_on[PX_NBLK], tile_sel[PX_NBLK], tile_blk[PX_NBLK], tile_mv[PX_NBLK];
    int info_b, info_t, info_on;
    int blk, type, np, item, mode, on;
    uint8_t val[PX_NPAR];
    int hint;
    char status[12]; int status_red;
} uc;
static void ui_invalidate(void) { uc.full = 1; }
static int ui_np(void) { return px_nparams((int)ui_blk, PRESET.type[ui_blk]); }
static int ui_nitems(void) { return 2 + ui_np(); }     /* 0 = type, 1 = on/off, 2.. = parameters */
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
static int ui_dirty(void)              /* the preset differs from what its slot holds (hold PUSH to save) */
{
    static px_preset_t ref;
    static uint32_t ref_slot = ~0u, ref_gen = ~0u;
    if (ref_slot != cur_preset || ref_gen != st_gen) { st_get(cur_preset, &ref); ref_slot = cur_preset; ref_gen = st_gen; }
    return memcmp(&ref, &PRESET, sizeof ref) != 0;
}
static void draw_status(int force)
{
    if (uc.screen != 0) return;        /* (EDIT: its band takes the top) */
    if (force || uc.vol != master_vol) {
        char b[10] = "VOL "; num3(b + 4, master_vol);
        ft_box(0, 3, 66, b, &FT_SM, UI_DIM, UI_PANEL, 1);     /* (v36: 66 px; v35's 48 px cut "VOL 100" to "VOL 10") */
        uc.vol = master_vol;
    }
    lcd_rect(66, 0, 18, S_H, UI_PANEL); lcd_rect(158, 0, 12, S_H, UI_PANEL);
    draw_battery(force);
}
/* the DSP load, from the main loop: "C1 42%" (2nd core), "C0", "BYP" (red) */
static void ui_status(const char *s, int red)
{
    if (ui_mode == M_TUNER || ui_mode == M_SAVE || ui_mode == M_MIDI) return;
    int same = uc.status_red == red;
    for (int i = 0; i < 12; i++) { if (uc.status[i] != s[i]) same = 0; if (!s[i]) break; }
    if (same && !uc.full) return;
    if (uc.screen == 0) ft_box(170, 3, 66, s, &FT_SM, red ? RED : UI_DIM, UI_PANEL, 2);
    else ft_box(178, F_Y + 3u, 58, s, &FT_SM, red ? RED : UI_DIM, UI_PANEL, 2);
    for (int i = 0; i < 12; i++) { uc.status[i] = s[i]; if (!s[i]) break; }
    uc.status_red = red;
}
static void draw_hint(int force)
{
    int h = (int)ui_mode;
    if (!force && uc.hint == h) return;
    const char *t = h == M_MOVE ? "KNOB: MOVE    PUSH: DROP"
                  : h == M_EDIT ? "KNOB: VALUE   PUSH: OK"
                  : h == M_BLOCK ? "KNOB: SELECT  PUSH: EDIT"
                  : "KNOB: BLOCK    PUSH: EDIT";
    ft_box(0, F_Y + 3u, uc.screen ? 174u : 240u, t, &FT_SM, UI_DIM, UI_PANEL, uc.screen ? 0 : 1);
    uc.hint = h;
}

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
    if (force || at != uc.amp_t || ao != uc.amp_on || am != uc.amp_m) {
        char nm[12];
        const char *an = px_type_name(PX_B_AMP, at);
        if (am >= 0) { px_nam_name(am, nm); an = nm; }
        ft_box(94, P_NUM_Y + 4u, 142, ao ? an : "NO AMP", &FT_MD, ao ? BLK_COL[PX_B_AMP] : UI_DIM, UI_BG, 0);
        uc.amp_t = at; uc.amp_on = ao; uc.amp_m = am;
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
    if (force || d != uc.dirty) { ft_box(182, P_NUM_Y + 29u, 54, d ? "EDITED" : "", &FT_SM, UI_ACC, UI_BG, 2); uc.dirty = d; }
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
    uint16_t bg = mv ? UI_ACC : (on ? BLK_COL[b] : C_OFF), fg = (on || mv) ? BLK : (uint16_t)((BLK_COL[b] >> 1) & 0x7BEF);
    ui_rbox(x - 2u, y - 2u, T_W + 4u, T_H + 4u, 5, sel ? WHT : UI_BG, UI_BG);   /* selection ring (or nothing) */
    ui_rbox(x, y, T_W, T_H, 4, bg, sel ? WHT : UI_BG);
    ft_box(x + 2u, y + 5u, T_W - 4u, px_block_label(b), &FT_SM, fg, bg, 1);
    uc.tile_on[i] = (uint8_t)on; uc.tile_sel[i] = (uint8_t)sel; uc.tile_blk[i] = (uint8_t)b; uc.tile_mv[i] = (uint8_t)mv;
}
static void draw_tiles(int force)
{
    ui_order_fix();
    for (int i = 0; i < PX_NBLK; i++) {
        int b = PRESET.order[i], on = PRESET.on[b] != 0, sel = (b == (int)ui_blk), mv = sel && ui_mode == M_MOVE;
        if (force || uc.tile_blk[i] != b || uc.tile_on[i] != on || uc.tile_sel[i] != sel || uc.tile_mv[i] != mv) draw_tile(i);
    }
}
static void draw_info(int force)       /* the selected block: label, model, on/off */
{
    int b = (int)ui_blk, t = PRESET.type[b], on = PRESET.on[b] != 0;
    if (!force && uc.info_b == b && uc.info_t == t && uc.info_on == on) return;
    ft_box(0, P_INFO_Y, 8, "", &FT_MD, UI_TXT, UI_BG, 0);
    ft_box(8, P_INFO_Y, 54, px_block_label(b), &FT_MD, BLK_COL[b], UI_BG, 0);
    ft_box(62, P_INFO_Y, 132, px_type_name(b, t), &FT_MD, on ? UI_TXT : UI_DIM, UI_BG, 0);
    lcd_rect(194, P_INFO_Y, 40, 4, UI_BG); lcd_rect(194, P_INFO_Y + 19u, 40, 4, UI_BG);
    ft_box(194, P_INFO_Y + 4u, 40, on ? "ON" : "OFF", &FT_SM, on ? BLK_COL[b] : UI_DIM, UI_BG, 2);
    lcd_rect(234, P_INFO_Y, 6, 23, UI_BG);
    uc.info_b = b; uc.info_t = t; uc.info_on = on;
}

/* ===================================================== EDIT ======== */
static void draw_band(int force)       /* the block's name on its colour, ON / OFF pill (item 1) */
{
    int on = PRESET.on[ui_blk] != 0, sel = (UI_PAGE && ui_item == 1u);
    if (!(force || uc.blk != (int)ui_blk || uc.on != on || (uc.item == 1) != sel)) return;
    uint16_t c = BLK_COL[ui_blk];
    if (force || uc.blk != (int)ui_blk) {
        lcd_rect(0, 0, 240, 4, c); lcd_rect(0, 27, 240, E_BAND - 27u, c); lcd_rect(0, 4, 8, 23, c);
        ft_box(8, 4, 160, px_block_name((int)ui_blk), &FT_MD, BLK, c, 0);
        lcd_rect(168, 4, 8, 23, c); lcd_rect(232, 4, 8, 23, c);
    }
    ui_rbox(176, 4, 56, 23, 6, sel ? WHT : c, c);
    ui_rbox(178, 6, 52, 19, 5, on ? BLK : ui_dark(c), sel ? WHT : c);
    ft_box(182, 8, 44, on ? "ON" : "OFF", &FT_SM, on ? c : UI_TXT, on ? BLK : ui_dark(c), 1);
}
static void draw_type(void)
{
    int sel = (UI_PAGE && ui_item == 0u), ed = sel && ui_mode == M_EDIT;
    uint16_t bg = sel ? C_CARD : UI_BG, fg = ed ? UI_ACC : UI_TXT;
    ui_rbox(4, E_TYPE_Y - 2u, 232, 28, 5, bg, UI_BG);
    ft_box(8, E_TYPE_Y, 20, sel ? "<" : "", &FT_MD, UI_ACC, bg, 1);
    ft_box(28, E_TYPE_Y, 184, px_type_name((int)ui_blk, PRESET.type[ui_blk]), &FT_MD, fg, bg, 1);
    ft_box(212, E_TYPE_Y, 20, sel ? ">" : "", &FT_MD, UI_ACC, bg, 1);
}
static void draw_bar(int k)            /* value + bar: the only thing that moves while adjusting */
{
    uint32_t y = E_P0 + (uint32_t)k * E_ROW;
    int sel = (UI_PAGE && (int)ui_item == k + 2), ed = sel && ui_mode == M_EDIT;
    uint16_t bg = sel ? C_CARD : UI_BG;
    uint32_t v = PRESET.p[ui_blk][k]; if (v > 100u) v = 100u;
    char b[10];
    if (!px_param_text((int)ui_blk, PRESET.type[ui_blk], k, (int)v, b)) num3(b, v);   /* "+5TH", "440 MS"... */
    ft_box(140, y + 1u, 90, b, &FT_MD, ed ? UI_ACC : UI_TXT, bg, 2);
    uint32_t w = v * BAR_W / 100u;
    uint16_t fc = ed ? UI_ACC : BLK_COL[ui_blk];
    if (w) lcd_rect(BAR_X, y + 24u, w, 4, fc);
    if (w < BAR_W) lcd_rect(BAR_X + w, y + 24u, BAR_W - w, 4, C_BAR);
    uc.val[k] = (uint8_t)v;
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
    ft_box(BAR_X, y + 5u, 130, px_param_name((int)ui_blk, PRESET.type[ui_blk], k), &FT_SM, sel ? WHT : UI_DIM, bg, 0);
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
    if (uc.on != on) { draw_band(1); uc.on = on; }
    for (int k = 0; k < np; k++) if (uc.val[k] != PRESET.p[ui_blk][k]) draw_bar(k);
}

/* ---- render: sends only what changed to the screen ---- */
static void ui_render(void)
{
    if (ui_mode == M_TUNER) return;
    if (ui_mode == M_SAVE) { save_draw(); return; }
    if (ui_mode == M_MIDI) { midi_draw(); return; }
    int scr = UI_PAGE ? 1 : 0;
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
        if (scr) { lcd_rect(174, F_Y + 3u, 4, 15, UI_PANEL); lcd_rect(236, F_Y + 3u, 4, 15, UI_PANEL); }
        uc.full = 0;
    }
    if (scr == 0) {
        draw_status(f);
        draw_play_top(f);
        draw_tiles(f);
        draw_info(f);
    } else draw_edit(f);
    draw_hint(f);
    if (f) { char s[12]; for (int i = 0; i < 12; i++) s[i] = uc.status[i]; uc.full = 1; ui_status(s, uc.status_red); uc.full = 0; }
}

/* ---- actions ---- */
static void ui_enter_block(uint32_t b)
{
    if (ui_blk == b && ui_mode != M_CHAIN) PRESET.on[b] ^= 1u;   /* 2nd press on the same button: on/off */
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
    } else if (ui_mode == M_BLOCK) {
        int n = ui_nitems();
        ui_item = (uint32_t)(((int)ui_item + n + dd) % n);
    } else if (ui_mode == M_EDIT) {
        if (ui_item == 0u) {
            int nt = px_ntypes((int)ui_blk);
            PRESET.type[ui_blk] = (uint8_t)((PRESET.type[ui_blk] + nt + dd) % nt);
        } else if (ui_item >= 2u) {
            int k = (int)ui_item - 2, v = PRESET.p[ui_blk][k] + dd * (fast ? 5 : 1);
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
    ft_box(0, F_Y + 3u, 240, "KNOB: SLOT  PUSH: SAVE  HOME: NO", &FT_SM, UI_DIM, UI_PANEL, 1);
}
static void save_draw(void)
{
    if (sv_shown == sv_dest) return;
    sv_shown = sv_dest;
    char b[20];
    if (sv_dest == SV_FACTORY) {
        ft_box(0, 82, 240, "", &FT_XL, UI_ACC, UI_BG, 1);
        ft_box(0, 132, 240, "RESTORE FACTORY", &FT_MD, UI_TXT, UI_BG, 1);
        b[0] = 'S'; b[1] = 'L'; b[2] = 'O'; b[3] = 'T'; b[4] = ' ';
        b[5] = (char)('0' + (cur_preset + 1u) / 10u); b[6] = (char)('0' + (cur_preset + 1u) % 10u); b[7] = 0;
        ft_box(0, 160, 240, st_saved(cur_preset) ? b : "ALREADY THE FACTORY SOUND", &FT_SM, UI_DIM, UI_BG, 1);
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
    ft_box(0, 186, 240, "SAVING...", &FT_MD, UI_ACC, UI_BG, 1);
    if (sv_dest == SV_FACTORY) { rc = st_reset(cur_preset); }
    else { rc = st_save(sv_dest, &PRESET); if (!rc) cur_preset = sv_dest; }
    st_gen++;
    if (!rc) load_preset(cur_preset);
    {
        char b[16] = "ERROR -";
        if (rc) { b[7] = (char)('0' + (uint32_t)(-rc) % 10u); b[8] = 0; }
        ft_box(0, 186, 240, rc ? b : "SAVED", &FT_MD, rc ? RED : GRN, UI_BG, 1);
    }
    for (int i = 0; i < 7; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
    save_close();
}
/* ===================================================== MIDI page ======== */
/* hold FX: the MIDI settings (docs/MIDI.md). KNOB changes the selected row, PUSH moves to the next row and, after the
 * last one, leaves; HOME leaves. The settings are saved (a flash write, as SAVE) only if something changed. */
enum { MI_CHAN, MI_SEND, MI_CLOCK, MI_ROWS };
static uint32_t mi_row, mi_shown_hash = ~0u;
static uint8_t mi_orig;
static void midi_persist(void);                 /* openpocket.c: writes the settings to the store */
static void midi_open(void)
{
    ui_mode = M_MIDI; mi_row = 0; mi_shown_hash = ~0u; mi_orig = px_midi_cfg_pack(&mid_cfg);
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 4, UI_ACC); lcd_rect(0, 27, 240, 3, UI_ACC); lcd_rect(0, 4, 8, 23, UI_ACC);
    ft_box(8, 4, 232, "MIDI", &FT_MD, BLK, UI_ACC, 0);
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    ft_box(0, F_Y + 3u, 240, "KNOB: SET   PUSH: NEXT   HOME: OK", &FT_SM, UI_DIM, UI_PANEL, 1);
}
static void midi_draw(void)
{
    uint32_t h = mi_row | (uint32_t)px_midi_cfg_pack(&mid_cfg) << 4 | (uint32_t)mid_usb_up << 12 |
                 (uint32_t)mid_last[0] << 13 | (uint32_t)mid_last[1] << 16 | (uint32_t)mid_last[2] << 23;
    if (h == mi_shown_hash) return;
    mi_shown_hash = h;
    static const char *const NAME[MI_ROWS] = {"CHANNEL", "SEND", "CLOCK > DELAY"};
    char v[MI_ROWS][8];
    if (mid_cfg.chan) { v[0][0] = (char)(mid_cfg.chan >= 10 ? '0' + mid_cfg.chan / 10 : ' '); v[0][1] = (char)('0' + mid_cfg.chan % 10); v[0][2] = 0; }
    else { v[0][0] = 'O'; v[0][1] = 'M'; v[0][2] = 'N'; v[0][3] = 'I'; v[0][4] = 0; }
    v[1][0] = v[2][0] = 0;
    b_str(v[1], mid_cfg.send ? "ON" : "OFF");
    b_str(v[2], mid_cfg.clock ? "ON" : "OFF");
    for (uint32_t r = 0; r < MI_ROWS; r++) {
        uint32_t y = 44u + r * 36u;
        uint16_t bg = r == mi_row ? C_CARD : UI_BG;
        lcd_rect(0, y, 240, 32, bg);
        ft_box(8, y + 4u, 130, NAME[r], &FT_SM, r == mi_row ? UI_TXT : UI_DIM, bg, 0);
        ft_box(140, y + 2u, 92, v[r], &FT_MD, r == mi_row ? UI_ACC : UI_TXT, bg, 2);
    }
    ft_box(0, 160, 240, mid_usb_up ? "USB: HOST CONNECTED" : "USB: NO HOST", &FT_SM, mid_usb_up ? GRN : UI_DIM, UI_BG, 1);
    char l[24]; l[0] = 0;
    if (mid_last[0] == PXM_PC) { b_str(l, "LAST: PC "); b_num(l, mid_last[1] + 1u); }
    else if (mid_last[0] == PXM_CC) { b_str(l, "LAST: CC "); b_num(l, mid_last[1]); b_str(l, " = "); b_num(l, mid_last[2]); }
    else b_str(l, "LAST: NONE");
    ft_box(0, 186, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
}
static void midi_close(void)
{
    ui_mode = M_CHAIN; ui_item = 0;
    if (px_midi_cfg_pack(&mid_cfg) != mi_orig) midi_persist();
    lcd_rect(0, 0, 240, 240, UI_BG);
    uc.screen = -1;
    ui_invalidate();
}
static void midi_knob(int dd)
{
    if (mi_row == MI_CHAN) mid_cfg.chan = (uint8_t)((mid_cfg.chan + 17u + (uint32_t)(dd > 0 ? 1 : 16)) % 17u);
    else if (mi_row == MI_SEND) mid_cfg.send ^= 1u;
    else mid_cfg.clock ^= 1u;
}
static void midi_push(void) { if (++mi_row >= MI_ROWS) midi_close(); }

static void ui_preset_step(int dd)
{
    int n = px_preset_count();
    cur_preset = (uint32_t)(((int)cur_preset + n + dd) % n);
    load_preset(cur_preset);
    if (ui_mode == M_EDIT) ui_mode = M_BLOCK;
    if ((int)ui_item >= ui_nitems()) ui_item = 0;
}
/* the FX button's blocks, in its order (the FX light follows them: px_lights.h) */
static const uint8_t UI_FXB[7] = {PX_B_DRIVE, PX_B_SFX, PX_B_SYN, PX_B_PITCH, PX_B_COMP, PX_B_GATE, PX_B_EQ};
static void ui_fx_cycle(void)          /* FX: DRV -> SFX -> SYN -> PIT -> CMP -> GT -> EQ */
{
    uint32_t nx = PX_B_DRIVE;
    for (int i = 0; i < 7; i++) if (UI_FXB[i] == ui_blk) nx = UI_FXB[(i + 1) % 7];
    ui_blk = nx; ui_mode = M_BLOCK; ui_item = 0;
}
/* SYN PAD: with a PAD on in the preset the left footswitch is the pad's (a press catches the chord being played,
 * held 0.7 s fades it out: openpocket.c) instead of the previous preset */
static int ui_pad_on(void) { return PRESET.on[PX_B_SYN] && PRESET.type[PX_B_SYN] % PX_SYN_COUNT == PX_SYN_PAD; }

/* ===================================================== tuner ======== */
static const char *NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int tn_last_note = -2, tn_last_x = -1, tn_last_c = 1000, tn_last_lv = 0x7FFF;
/* "IN L -82  R -84 dB": input level of each I2S channel, dB below full scale (24-bit, v26 diagnostic) */
static int lv_str(char *b, int v)
{
    int i = 0;
    if (v <= -199) { b[0] = '-'; b[1] = '-'; b[2] = '-'; return 3; }
    if (v < 0) { b[i++] = '-'; v = -v; }
    char t[4]; num3(t, (uint32_t)v);
    for (int k = 0; t[k]; k++) b[i++] = t[k];
    return i;
}
static void tuner_level(void)
{
    int l = a_level_db(0), r = a_level_db(1);
    if ((((l & 0xFF) | ((r & 0xFF) << 8)) ^ (bat_mv << 16)) == tn_last_lv) return;
    tn_last_lv = ((l & 0xFF) | ((r & 0xFF) << 8)) ^ (bat_mv << 16);
    char b[40]; int i = 0;
    b[i++] = 'I'; b[i++] = 'N'; b[i++] = ' '; b[i++] = ' '; b[i++] = 'L'; b[i++] = ' '; i += lv_str(b + i, l);
    b[i++] = ' '; b[i++] = ' '; b[i++] = ' '; b[i++] = 'R'; b[i++] = ' '; i += lv_str(b + i, r);
    b[i++] = ' '; b[i++] = 'd'; b[i++] = 'B';
    if (bat_mv) {                                             /* + the battery in volts (v36: to check the gauge) */
        b[i++] = ' '; b[i++] = ' '; b[i++] = 'B'; b[i++] = 'A'; b[i++] = 'T'; b[i++] = ' ';
        b[i++] = (char)('0' + bat_mv / 1000); b[i++] = '.'; b[i++] = (char)('0' + bat_mv / 100 % 10); b[i++] = (char)('0' + bat_mv / 10 % 10);
        b[i++] = 'V';
    }
    b[i] = 0;
    ft_box(0, 36, 240, b, &FT_SM, UI_DIM, UI_BG, 1);
}
/* v37: for a photo: the battery's raw reading (PC9, the cell's divider; -1 = no conversion), a status bit the stock firmware
 * reads beside it, the audio blocks that ran late and that reached the output's soft clipper since the start, and the
 * highest block load seen (%) */
static uint32_t tn_last_raw = ~0u;
static int r_str(char *b, int32_t v) { if (v < 0) { b[0] = '-'; b[1] = '-'; return 2; } char t[8]; t[0] = 0; b_num(t, (uint32_t)v); int i = 0; while (t[i]) { b[i] = t[i]; i++; } return i; }
static void tuner_bat_raw(void)
{
    uint32_t h = 0;
    for (int k = 0; k < 5; k++) h = h * 0x9E3779B1u + (uint32_t)bat_raw[k];
    if (h == tn_last_raw) return;
    tn_last_raw = h;
    static const char *const N[5] = {"C9 ", " CH ", " LATE ", " CLIP ", " PK "};
    char b[48]; int i = 0;
    for (int k = 0; k < 5; k++) { for (const char *p = N[k]; *p; p++) b[i++] = *p; i += r_str(b + i, bat_raw[k]); }
    b[i] = 0;
    ft_box(0, 206, 240, b, &FT_SM, UI_DIM, UI_BG, 1);
}
/* v38: where the CPU goes (px_audio.h a_pf): the 4 costliest blocks, % of an audio block's time, in the footer; v39: then
 * "CUT n" once the delay or the reverb has had to cut a wet block short (out of time) */
static uint32_t tn_last_pf = ~0u;
static void tuner_prof(void)
{
    uint32_t h = 0;
    for (int k = 0; k <= PX_NBLK; k++) h = h * 0x9E3779B1u + a_pf[k];
    h = h * 0x9E3779B1u + a_wcut;
    if (h == tn_last_pf) return;
    tn_last_pf = h;
    uint8_t used[PX_NBLK + 1] = {0};
    char b[48]; int i = 0;
    for (int n = 0; n < 4; n++) {
        int best = -1;
        for (int k = 0; k <= PX_NBLK; k++) if (!used[k] && a_pf[k] && (best < 0 || a_pf[k] > a_pf[best])) best = k;
        if (best < 0) break;
        used[best] = 1;
        const char *l = best == PX_NBLK ? "NAM" : px_block_label(best);
        if (i) b[i++] = ' ';
        while (*l && i < 40) b[i++] = *l++;
        b[i++] = ' ';
        char t[8]; t[0] = 0; b_num(t, a_pf[best]);
        for (int j = 0; t[j] && i < 44; j++) b[i++] = t[j];
    }
    if (i) { b[i++] = ' '; b[i++] = '%'; } else { const char *z = "CPU: --"; while (*z) b[i++] = *z++; }
    if (a_wcut) {                                      /* v39: wet blocks cut short (the delay / reverb out of time) */
        const char *z = " CUT ";
        while (*z && i < 44) b[i++] = *z++;
        char t[12]; t[0] = 0; b_num(t, a_wcut > 9999u ? 9999u : a_wcut);
        for (int j = 0; t[j] && i < 47; j++) b[i++] = t[j];
    }
    b[i] = 0;
    ft_box(0, F_Y + 3u, 240, b, &FT_SM, UI_DIM, UI_PANEL, 1);
}
#define TN_BAR_Y 172u
static void tuner_open(void)
{
    ui_mode = M_TUNER;
    a_tmute = 1;                                              /* v36: silent while tuning */
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 0, 240, 4, GRN); lcd_rect(0, 27, 240, 3, GRN); lcd_rect(0, 4, 8, 23, GRN);
    ft_box(8, 4, 232, "TUNER", &FT_MD, BLK, GRN, 0);
    ui_rbox(18, TN_BAR_Y - 2u, 204, 14, 5, C_BAR, UI_BG);
    lcd_rect(110, TN_BAR_Y - 2u, 20, 14, ui_dark(GRN));
    lcd_rect(119, TN_BAR_Y - 10u, 2, 30, UI_TXT);
    ft_box(10, TN_BAR_Y + 18u, 40, "-50", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(100, TN_BAR_Y + 18u, 40, "0", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(190, TN_BAR_Y + 18u, 40, "+50", &FT_SM, UI_DIM, UI_BG, 1);
    tn_last_raw = ~0u;
    lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
    ft_box(120, 9, 116, "ANY KEY: EXIT", &FT_SM, BLK, GRN, 2);   /* v38: the footer shows the CPU per block */
    tn_last_pf = ~0u;
    tn_last_note = -2; tn_last_x = -1; tn_last_c = 1000; tn_last_lv = 0x7FFF;
}
static void tuner_close(void) { a_tmute = 0; ui_mode = M_CHAIN; lcd_rect(0, 0, 240, 240, UI_BG); uc.screen = -1; ui_invalidate(); }
static void tuner_draw(float hz)       /* hz <= 0: no note */
{
    int note = -1, c = 0; float cents = 0.0f;
    tuner_level();
    tuner_bat_raw();
    tuner_prof();
    if (hz > 20.0f) {
        float st = 12.0f * logf(hz / 440.0f) * 1.44269504f;   /* semitones from A4 = 440 Hz */
        int r = (int)(st + (st >= 0 ? 0.5f : -0.5f));
        cents = (st - (float)r) * 100.0f;
        note = ((r % 12) + 12 + 9) % 12;                      /* A = 9 */
        c = (int)(cents + (cents >= 0 ? 0.5f : -0.5f));
    }
    int ok = (note >= 0 && c > -5 && c < 5);
    if (note != tn_last_note || (note >= 0 && (ok != (tn_last_c > -5 && tn_last_c < 5)))) {
        ft_box(0, 60, 240, note < 0 ? "-" : NOTE_N[note], &FT_LG, note < 0 ? UI_DIM : (ok ? GRN : UI_TXT), UI_BG, 1);
        tn_last_note = note;
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
