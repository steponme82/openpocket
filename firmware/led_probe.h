/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the light finder (issue #8): which pins drive the BlackBox's button lights?
 *
 * Built only with LED_PROBE 1 (openpocket.c). That build plays no sound. It makes ONE pin at a time blink (an
 * output, 2 Hz, so a light that is on when its pin is high and one that is on when it is low both show) while the
 * pin's name is on the screen, and the knob steps through the pins. A button light that blinks in step with the
 * square at the top right is on that pin, lit when the square is lit (high) or when it is dark (low).
 *   - The right footswitch marks the pin on screen (the list stays at the bottom, for a photo). While PA2 (the
 *     right footswitch itself) blinks, the left one marks instead.
 *   - The left footswitch redraws the screen, in case a pin upset it.
 *   - Never driven: PA4..PA9 (I2S MCLK, SCLK, LRCK, DAT0, DAT1 and the amp enable), PA10 (MASTER), PB1, PB5,
 *     PB6, PB7 and PC7 (the screen), PC0 and PC1 (the knob), port D (the flash). The buttons ARE tried: on the FM-1
 *     PH6 and PH9 are LED lines, and on the Pocket X / BlackBox they read as buttons.
 *   - A pin that resets the chip is remembered across the reset (.noinit) and skipped from then on (the right
 *     footswitch on it tries it again). Switching the pedal off and on starts afresh.
 *   - USB stays up, as in the normal build, so the web installer takes the pedal back to a normal OpenPocket or to
 *     the official firmware.
 *
 * Included by openpocket.c (LED_PROBE 1) and by tests/lpsim.c on the PC. The including file provides: PA_IN, PC_IN,
 * ENC_MASK, PA_FS_UP, PA_FS_DN, base_pa, QDEC, dbn_t / dbn_press, ms_now, fm1_wdt_feed, px_usb_service, lcd_init,
 * lcd_rect, ft_box / ft_width and the fonts (ui_text.h), b_str / b_num, guard, op_diag / ST_OK, op_pwr, the UI
 * colours and RED. */
#pragma once
#include <stdint.h>

#ifndef LP_REG              /* a GPIO port register: 0x50000 + port * 0x40 + r (bsp/chip/fm1_gpio.h) */
#define LP_REG(port, r) (*(volatile uint32_t *)(0x50000u + (uint32_t)(port) * 0x40u + (uint32_t)(r)))
#endif
#ifndef LP_NOINIT
#define LP_NOINIT __attribute__((section(".noinit")))
#endif
#ifndef LP_TEXT_HOOK        /* tests/lpsim.c checks that every line fits */
#define LP_TEXT_HOOK(s, f, w) ((void)0)
#endif
enum { LP_OUT = 0x00, LP_DIR = 0x08, LP_PU = 0x10, LP_PD = 0x14 };   /* DIR: 1 input, 0 output */

static const uint8_t LP_PORT[4] = { 0u, 1u, 2u, 7u };                /* PA, PB, PC, PH */
static const char LP_PN[4] = { 'A', 'B', 'C', 'H' };
static const uint32_t LP_NEVER[4] = { 0x07F0u, 0x00E2u, 0x0083u, 0x0000u };   /* PA4..10 | PB1,5,6,7 | PC0,1,7 */
#define LP_BLINK_MS 250u                                              /* half a period: 2 Hz */
#define LP_MAGIC 0x4C505231u                                          /* "LPR1" */
static volatile struct { uint32_t magic, cur, skip[4], mark[4]; } lp_keep LP_NOINIT;   /* survives a reset */

static struct {
    uint8_t ids[64];            /* the pins tried: id = port index * 16 + bit + 1 (1..64) */
    uint32_t n, i, id;          /* how many; the step on screen (0 = none); the pin driven now (0 = none) */
    uint32_t sv_out, sv_dir, sv_pu, sv_pd;   /* that pin's bits as they were */
    uint32_t t_blink;
    int hi, dirty;
    int32_t acc;
    uint8_t enc;
    dbn_t fs_r, fs_l;
    char note[40];
    uint16_t note_col;
} lp;

static void lp_name(char *d, uint32_t id)                            /* 55 -> "PH6" */
{
    uint32_t p = (id - 1u) >> 4, b = (id - 1u) & 15u;
    int n = 0;
    d[n++] = 'P';
    d[n++] = LP_PN[p];
    if (b >= 10u) { d[n++] = '1'; d[n++] = (char)('0' + b - 10u); }
    else d[n++] = (char)('0' + b);
    d[n] = 0;
}
static const char *lp_role(uint32_t id)                              /* what OpenPocket already reads there */
{
    switch (id - 1u) {
    case 0u: return "FX BUTTON";
    case 1u: return "HOME BUTTON";
    case 2u: return "RIGHT FOOTSWITCH";
    case 3u: return "LEFT FOOTSWITCH";
    case 52u: case 54u: return "KNOB PUSH";                          /* PH4, PH6 (3 * 16 + bit) */
    case 55u: case 56u: return "MOD BUTTON";                         /* PH7, PH8 */
    case 57u: return "AMP BUTTON";                                   /* PH9 */
    }
    return 0;
}
static int lp_has(const volatile uint32_t *set, uint32_t id) { return (int)((set[(id - 1u) >> 4] >> ((id - 1u) & 15u)) & 1u); }
static void lp_flip(volatile uint32_t *set, uint32_t id) { set[(id - 1u) >> 4] ^= 1u << ((id - 1u) & 15u); }
static uint32_t lp_pa_driven(void)                                   /* the PA bit being driven, if any */
{
    return (lp.id && ((lp.id - 1u) >> 4) == 0u) ? 1u << ((lp.id - 1u) & 15u) : 0u;
}

static void lp_take(uint32_t id)                                     /* the pin becomes an output, low */
{
    uint32_t P = LP_PORT[(id - 1u) >> 4], m = 1u << ((id - 1u) & 15u);
    lp.sv_out = LP_REG(P, LP_OUT) & m; lp.sv_dir = LP_REG(P, LP_DIR) & m;
    lp.sv_pu = LP_REG(P, LP_PU) & m;   lp.sv_pd = LP_REG(P, LP_PD) & m;
    lp_keep.cur = id;                                                /* from here on, a reset names this pin */
    LP_REG(P, LP_PU) &= ~m;
    LP_REG(P, LP_PD) &= ~m;
    LP_REG(P, LP_OUT) &= ~m;
    LP_REG(P, LP_DIR) &= ~m;
}
static void lp_level(uint32_t id, int hi)
{
    uint32_t P = LP_PORT[(id - 1u) >> 4], m = 1u << ((id - 1u) & 15u);
    if (hi) LP_REG(P, LP_OUT) |= m;
    else LP_REG(P, LP_OUT) &= ~m;
}
static void lp_give(uint32_t id)                                     /* the pin back as it was */
{
    uint32_t P = LP_PORT[(id - 1u) >> 4], m = 1u << ((id - 1u) & 15u);
    if (lp.sv_dir) {                                                 /* was an input: stop driving it first */
        LP_REG(P, LP_DIR) |= m;
        LP_REG(P, LP_OUT) = (LP_REG(P, LP_OUT) & ~m) | lp.sv_out;
    } else {
        LP_REG(P, LP_OUT) = (LP_REG(P, LP_OUT) & ~m) | lp.sv_out;
        LP_REG(P, LP_DIR) &= ~m;
    }
    LP_REG(P, LP_PU) = (LP_REG(P, LP_PU) & ~m) | lp.sv_pu;
    LP_REG(P, LP_PD) = (LP_REG(P, LP_PD) & ~m) | lp.sv_pd;
    lp_keep.cur = 0;
}

static void lp_set_note(const char *s, uint16_t col)
{
    lp.note[0] = 0;
    b_str(lp.note, s);
    lp.note_col = col;
}
static void lp_text(uint32_t y, const char *s, const op_font *f, uint16_t col)
{
    LP_TEXT_HOOK(s, f, 240u);
    ft_box(0, y, 240, s, f, col, UI_BG, 1);
}
static void lp_dot(void) { lcd_rect(222, 10, 14, 14, (lp.id && lp.hi) ? UI_ACC : UI_PANEL); }
static void lp_draw(void)
{
    char l[48];
    uint32_t id = lp.i ? lp.ids[lp.i - 1u] : 0u, drv = lp_pa_driven(), k = 0;
    int skip = id && lp_has(lp_keep.skip, id), marked = id && lp_has(lp_keep.mark, id);
    const char *fs = (drv & PA_FS_UP) ? "LEFT" : "RIGHT";
    LP_TEXT_HOOK("LIGHT FINDER", &FT_MD, 200u);
    ft_box(20, 6, 200, "LIGHT FINDER", &FT_MD, UI_ACC, UI_BG, 1);
    lp_dot();
    lp_text(36, lp.note, &FT_SM, lp.note_col);
    l[0] = 0;
    if (id) lp_name(l, id); else b_str(l, "READY");
    lp_text(64, l, &FT_NM, skip ? UI_DIM : UI_TXT);
    l[0] = 0;
    if (!id) { b_num(l, lp.n); b_str(l, " PINS TO TRY"); }
    else if (skip) b_str(l, "SKIPPED: IT RESET THE PEDAL");
    else { b_str(l, "BLINKING  "); b_num(l, lp.i); b_str(l, " / "); b_num(l, lp.n); }
    lp_text(104, l, &FT_SM, skip ? RED : UI_ACC);
    l[0] = 0;
    if (id && lp_role(id)) { b_str(l, lp_role(id)); if (!skip) b_str(l, ": DON'T PRESS IT"); }
    lp_text(124, l, &FT_SM, UI_DIM);
    l[0] = 0;
    if (!id) b_str(l, "WATCH THE BUTTON LIGHTS");
    else if (skip) { b_str(l, fs); b_str(l, " FOOTSWITCH: TRY AGAIN"); }
    else if (marked) { b_str(l, "MARKED. "); b_str(l, fs); b_str(l, ": UNMARK"); }
    else { b_str(l, fs); b_str(l, " FOOTSWITCH: MARK IT"); }
    lp_text(144, l, &FT_SM, marked ? UI_OK : UI_TXT);
    lp_text(164, (drv & (PA_FS_UP | PA_FS_DN)) ? "" : "LEFT FOOTSWITCH: REDRAW", &FT_SM, UI_DIM);
    {   /* the marks, six a line */
        char m[2][40];
        m[0][0] = m[1][0] = 0;
        for (uint32_t j = 1u; j <= 64u; j++)
            if (lp_has(lp_keep.mark, j)) {
                if (k < 12u) {
                    char nm[6], *t = m[k / 6u];
                    lp_name(nm, j);
                    if (t[0]) b_str(t, " ");
                    b_str(t, nm);
                }
                k++;
            }
        l[0] = 0;
        b_str(l, "MARKED: ");
        if (k) b_num(l, k); else b_str(l, "NONE YET");
        lp_text(184, l, &FT_SM, UI_DIM);
        lp_text(202, m[0], &FT_SM, UI_TXT);
        lp_text(220, m[1], &FT_SM, UI_TXT);
    }
}

static void lp_select(uint32_t now)                                  /* give the old pin back, take the new one */
{
    uint32_t id = lp.i ? lp.ids[lp.i - 1u] : 0u;
    if (lp.id) { lp_give(lp.id); lp.id = 0; }
    if (id && !lp_has(lp_keep.skip, id)) { lp.id = id; lp_take(id); lp.hi = 0; lp.t_blink = now; }
    lp.dirty = 1;
}

static void lp_init(void)
{
    lp.n = 0;
    for (uint32_t p = 0; p < 4u; p++)
        for (uint32_t b = 0; b < 16u; b++)
            if (!(LP_NEVER[p] & (1u << b))) lp.ids[lp.n++] = (uint8_t)(p * 16u + b + 1u);
    lp.i = lp.id = 0;
    lp.hi = 0;
    lp.acc = 0;
    lp.enc = (uint8_t)(PC_IN & ENC_MASK);
    lp.fs_r.stab = lp.fs_r.last = lp.fs_r.cnt = 0;
    lp.fs_l.stab = lp.fs_l.last = lp.fs_l.cnt = 0;
    lp_set_note("TURN THE KNOB: NEXT PIN", UI_DIM);
    if (lp_keep.magic != LP_MAGIC || (op_pwr.rst & 1u)) {           /* first start, or switched off and on */
        lp_keep.magic = LP_MAGIC;
        lp_keep.cur = 0;
        for (int k = 0; k < 4; k++) { lp_keep.skip[k] = 0; lp_keep.mark[k] = 0; }
    }
    if (lp_keep.cur >= 1u && lp_keep.cur <= 64u) {                  /* the chip reset while that pin blinked */
        char nm[6];
        if (!lp_has(lp_keep.skip, lp_keep.cur)) lp_flip(lp_keep.skip, lp_keep.cur);
        lp_name(nm, lp_keep.cur);
        lp_set_note(nm, RED);
        b_str(lp.note, " RESET THE PEDAL: SKIPPED");
    }
    lp_keep.cur = 0;
    guard.pending = 0;                                               /* poked at on purpose: a reset here is */
    guard.failed = 0;                                                /* not a failed boot (no UBOOT, no safe */
    op_diag.stage = ST_OK;                                           /* mode screen) */
    lcd_rect(0, 0, 240, 240, UI_BG);
    lp.dirty = 1;
}

static void lp_step(uint32_t now)                                    /* one pass of the loop */
{
    fm1_wdt_feed();
    px_usb_service(now);                                             /* the web installer, as in the normal build */
    {   /* the knob: one detent, one step (the main loop's decoding) */
        uint8_t s = (uint8_t)(PC_IN & ENC_MASK);
        if (s != lp.enc) {
            int dd = 0;
            lp.acc -= QDEC[(lp.enc << 2) | s];
            lp.enc = s;
            if (lp.acc >= 4) { lp.acc -= 4; dd = 1; } else if (lp.acc <= -4) { lp.acc += 4; dd = -1; }
            if (dd) {
                uint32_t m = lp.n + 1u;
                lp.i = dd > 0 ? (lp.i + 1u) % m : (lp.i + m - 1u) % m;
                lp_select(now);
                lp_set_note("TURN THE KNOB: NEXT PIN", UI_DIM);
            }
        }
    }
    {   /* the footswitches; a pin being driven is not read */
        uint32_t drv = lp_pa_driven(), pa = (PA_IN & 0xFFFFu) ^ base_pa;
        int r = !(drv & PA_FS_UP) && dbn_press(&lp.fs_r, (pa & PA_FS_UP) != 0);
        int l = !(drv & PA_FS_DN) && dbn_press(&lp.fs_l, (pa & PA_FS_DN) != 0);
        int mark = (drv & PA_FS_UP) ? l : r, redraw = (drv & (PA_FS_UP | PA_FS_DN)) ? 0 : l;
        if (mark && lp.i) {
            uint32_t id = lp.ids[lp.i - 1u];
            if (lp_has(lp_keep.skip, id)) { lp_flip(lp_keep.skip, id); lp_select(now); }   /* try it again */
            else lp_flip(lp_keep.mark, id);
            lp.dirty = 1;
        }
        if (redraw) { lcd_init(); lcd_rect(0, 0, 240, 240, UI_BG); lp.dirty = 1; }
    }
    if (lp.id && now - lp.t_blink >= LP_BLINK_MS) {
        lp.t_blink = now;
        lp.hi = !lp.hi;
        lp_level(lp.id, lp.hi);
        lp_dot();
    }
    if (lp.dirty) { lp.dirty = 0; lp_draw(); }
}

static void led_probe_run(void)
{
    lp_init();
    for (;;) lp_step(ms_now());
}
