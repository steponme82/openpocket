/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the LED hunt (issue #8): why do the BlackBox's button lights stay dark?
 *
 * Built only with LED_PROBE 1 (openpocket.c). That build plays no sound. The LED map (git history) drove the
 * 74HC595 as isod89 read it in the stock firmware (data PC8, clock PC5, latch PC6: px_leds.h) and nothing lit, not
 * even all 8 bits blinking; the first LED hunt (git history) tried the six role swaps and 47 LED lines on ports A, B,
 * C and H, and nothing lit either. Its pin check found the three pins free as inputs (a 595 input reads so; reading
 * a pin back while it drives is not valid on this chip: the AC79 SDK's gpio_read). The six effect buttons light on
 * the stock firmware, as isod89's six light bits say, so the missing piece is likely power. Since v33 OpenPocket
 * leaves the supplies as the boot loader sets them; BlackBox_020's power init sets P3_ANA_CON5 to 0x74: VDDIOM 4,
 * the I/O supply at 3.2 V (docs/AUDIO-SDK.md v30; the WL82 levels are 2.8 V + 0.1 V each, AC79 SDK
 * power_interface.h). The 595 and the lights may need it. The knob steps through:
 *   - POWER AS FOUND: CON5 and the I/O voltage as the boot left them (after a power-on, or kept by the always-on
 *     domain from the firmware before a restart), all 8 bits blinking (FF and 00, 2 Hz, isod89's roles).
 *   - STOCK POWER: CON5 set to 0x74 once, as the stock firmware does (it stays until the pedal is switched off), all
 *     8 bits blinking. Everything after runs at this voltage.
 *   - ROLES 1 to 6: the six ways to give data, clock and latch to PC5, PC6 and PC8 (1 is isod89's), all 8 bits
 *     blinking.
 *   - LED LINE 1 to 47: one more pin at a time blinks (1 and 0, 2 Hz) while the 595 (isod89's roles) blinks FF and
 *     00 at 1 Hz, so every mix of the two comes once a second. Never driven: PA4..PA10 (I2S, amp enable, MASTER,
 *     the analog keys), PB1/5/6/7 and PC7 (the screen), PC0/1 (the knob), PC5/6/8 (the 595), ports D and E.
 * CAB, DLY or REV (the analog keys, never driven here) marks the test on screen, again unmarks it; the list stays at
 * the bottom for a photo. USB stays up, as in the normal build, so the web installer is the way back.
 *
 * Included by openpocket.c (LED_PROBE 1) and by tests/lpsim.c on the PC. The including file provides: PC_IN,
 * ENC_MASK, keys_decode, adc_read2, ADC_CH_KEYS, KEY_NONE, QDEC, dbn_t / dbn_press, ms_now, fm1_delay_us,
 * fm1_wdt_feed, fm1_p33_read / fm1_p33_write, op_pwr, px_usb_service, lcd_rect, ft_box / ft_width and the fonts
 * (ui_text.h), b_str / b_num, guard, op_diag / ST_OK and the UI colours. */
#pragma once
#include <stdint.h>
#include "px_leds.h"

#ifndef LP_REG              /* a GPIO port register: 0x50000 + port * 0x40 + r (bsp/chip/fm1_gpio.h) */
#define LP_REG(port, r) (*(volatile uint32_t *)(0x50000u + (uint32_t)(port) * 0x40u + (uint32_t)(r)))
#endif
#ifndef LP_TEXT_HOOK        /* tests/lpsim.c checks that every line fits its box */
#define LP_TEXT_HOOK(s, f, w) ((void)0)
#endif
enum { LP_OUT = 0x00, LP_DIR = 0x08, LP_PU = 0x10, LP_PD = 0x14 };   /* DIR: 1 input, 0 output */
#define LP_CON5 0x05u       /* P3_ANA_CON5: [3:0] VDDIOM (2.8 V + 0.1 V a level), [5:4] VDDIOW, [7:6] VDDIO drive */
#define LP_CON5_STOCK 0x74u /* BlackBox_020 / PocketX_016's power init: VDDIOM 4 (3.2 V), VDDIOW 3, drive 1 */

static const uint8_t LP_PORT[4] = { 0u, 1u, 2u, 7u };                /* PA, PB, PC, PH */
static const char LP_PN[4] = { 'A', 'B', 'C', 'H' };
static const uint32_t LP_NEVER[4] = { 0x07F0u, 0x00E2u, 0x01E3u, 0x0000u };   /* PA4..10 | PB1,5,6,7 | PC0,1,5..8 */
static const uint8_t LP_ROLE[6][3] = {                               /* the PC bit of data, clock, latch */
    { 8u, 5u, 6u }, { 8u, 6u, 5u }, { 5u, 8u, 6u }, { 5u, 6u, 8u }, { 6u, 5u, 8u }, { 6u, 8u, 5u } };
#define LP_STEP_MS 250u
#define LP_ROLES 2u                                                  /* knob: 0, 1 power, 2..7 roles, 8.. lines */
#define LP_LINES 8u
#define LP_KEY_PRESSED 900  /* an analog key reading below this is a press, even if the supply moved its level */

static struct {
    uint8_t ids[64];            /* the LED line candidates: id = port index * 16 + bit + 1 (1..64) */
    uint32_t n, pos, id;        /* how many; the knob's position; the line driven now (0 = none) */
    uint32_t sv_out, sv_dir, sv_pu, sv_pd;   /* that line's bits as they were */
    uint32_t t, phase, t_keys;
    int role;                   /* the roles the 595 is driven with (LP_ROLE) */
    int dirty, kind;            /* kind: the page on screen (0 power, 1 roles, 2 line; -1 none) */
    int32_t acc;
    uint8_t enc, con5_found, stock;
    uint32_t mark_pos, mark[4]; /* marks: positions 0..7 (bit pos), LED lines (by id) */
    dbn_t k;
} lp;

static uint32_t lp_npos(void) { return LP_LINES + lp.n; }
static void lp_595(uint8_t v)
{
    const uint8_t *r = LP_ROLE[lp.role];
    leds_shift(v, 1u << r[0], 1u << r[1], 1u << r[2]);
}
static void lp_roles(int r)                                          /* the 595 driven with roles r, 0 shifted out */
{
    lp.role = r;
    leds_setup(1u << LP_ROLE[r][0], 1u << LP_ROLE[r][1], 1u << LP_ROLE[r][2]);
    lp_595(0u);
}

static void lp_name(char *d, uint32_t id)                            /* 55 -> "PH6" */
{
    uint32_t p = (id - 1u) >> 4, b = (id - 1u) & 15u;
    while (*d) d++;
    *d++ = 'P';
    *d++ = LP_PN[p];
    if (b >= 10u) { *d++ = '1'; *d++ = (char)('0' + b - 10u); }
    else *d++ = (char)('0' + b);
    *d = 0;
}
static const char *lp_role_of(uint32_t id)                           /* what OpenPocket already reads there */
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
static int lp_has(const uint32_t *set, uint32_t id) { return (int)((set[(id - 1u) >> 4] >> ((id - 1u) & 15u)) & 1u); }
static void lp_hex2(char *d, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    while (*d) d++;
    d[0] = H[(v >> 4) & 15u]; d[1] = H[v & 15u]; d[2] = 0;
}
static void lp_volts(char *d, uint32_t con5)                         /* "3.2 V (CON5 74)" */
{
    uint32_t lv = con5 & 15u;
    if (lv <= 7u) {
        uint32_t dv = 28u + lv;                                      /* tenths of a volt */
        b_num(d, dv / 10u); b_str(d, "."); b_num(d, dv % 10u); b_str(d, " V");
    } else { b_str(d, "LEVEL "); b_num(d, lv); }
    b_str(d, " (CON5 "); lp_hex2(d, con5); b_str(d, ")");
}

static void lp_take(uint32_t id)                                     /* the line becomes an output, low */
{
    uint32_t P = LP_PORT[(id - 1u) >> 4], m = 1u << ((id - 1u) & 15u);
    lp.sv_out = LP_REG(P, LP_OUT) & m; lp.sv_dir = LP_REG(P, LP_DIR) & m;
    lp.sv_pu = LP_REG(P, LP_PU) & m;   lp.sv_pd = LP_REG(P, LP_PD) & m;
    LP_REG(P, LP_PU) &= ~m;
    LP_REG(P, LP_PD) &= ~m;
    LP_REG(P, LP_OUT) &= ~m;
    LP_REG(P, LP_DIR) &= ~m;
}
static void lp_level(uint32_t id, uint32_t hi)
{
    uint32_t P = LP_PORT[(id - 1u) >> 4], m = 1u << ((id - 1u) & 15u);
    if (hi) LP_REG(P, LP_OUT) |= m;
    else LP_REG(P, LP_OUT) &= ~m;
}
static void lp_give(uint32_t id)                                     /* the line back as it was */
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
}

static void lp_text(uint32_t y, const char *s, const op_font *f, uint16_t col)
{
    LP_TEXT_HOOK(s, f, 232u);                                        /* 4 px clear of each edge */
    ft_box(0, y, 240, s, f, col, UI_BG, 1);
}
static int lp_lit(void)                                              /* the square: what blinks is at 1 */
{
    if (lp.pos < LP_LINES) return !(lp.phase & 1u);                  /* the 595 at FF */
    return (int)(lp.phase & 1u);                                     /* the line at 1 */
}
static void lp_dot(void) { lcd_rect(222, 10, 14, 14, lp_lit() ? UI_ACC : UI_PANEL); }
static void lp_draw(void)
{
    static const char *const ROLE_NAME[3] = { "DATA", "CLOCK", "LATCH" };
    char l[48];
    uint32_t pos = lp.pos;
    int kind = pos < LP_ROLES ? 0 : pos < LP_LINES ? 1 : 2;
    if (kind != lp.kind) { lcd_rect(0, 0, 240, 240, UI_BG); lp.kind = kind; }   /* no rows left from another page */
    l[0] = 0;
    if (pos == 0u) b_str(l, "POWER AS FOUND");
    else if (pos == 1u) b_str(l, "STOCK POWER");
    else if (pos < LP_LINES) { b_str(l, "ROLES "); b_num(l, pos - LP_ROLES + 1u); b_str(l, " OF 6"); }
    else { b_str(l, "LED LINE "); b_num(l, pos - LP_LINES + 1u); b_str(l, " OF "); b_num(l, lp.n); }
    LP_TEXT_HOOK(l, &FT_MD, 200u);
    ft_box(20, 6, 200, l, &FT_MD, UI_ACC, UI_BG, 1);
    lp_dot();
    if (pos < LP_ROLES) {
        l[0] = 0; b_str(l, "FOUND "); lp_volts(l, lp.con5_found);
        lp_text(36, l, &FT_SM, lp.con5_found == LP_CON5_STOCK ? UI_OK : UI_TXT);
        lp_text(56, (op_pwr.rst & 1u) ? "(AFTER A POWER-ON)" : "(KEPT FROM BEFORE A RESTART)", &FT_SM, UI_DIM);
        l[0] = 0; b_str(l, "NOW "); lp_volts(l, fm1_p33_read(LP_CON5));
        lp_text(76, l, &FT_SM, lp.stock ? UI_OK : UI_TXT);
        lp_text(104, pos == 0u ? "AS FOUND. ALL 8 BITS: FF / 00" : "SET AS STOCK. ALL 8 BITS: FF / 00",
                &FT_SM, UI_ACC);
        lp_text(124, "ANY LIGHT? CAB, DLY OR REV: MARK", &FT_SM, UI_TXT);
        lp_text(144, pos == 0u ? "KNOB: SET THE STOCK POWER" : "KNOB: NEXT TEST", &FT_SM, UI_DIM);
    } else if (pos < LP_LINES) {
        const uint8_t *r = LP_ROLE[pos - LP_ROLES];
        for (int i = 0; i < 3; i++) {
            l[0] = 0;
            b_str(l, ROLE_NAME[i]); b_str(l, " PC"); b_num(l, r[i]);
            lp_text(36u + (uint32_t)i * 20u, l, &FT_SM, UI_TXT);
        }
        lp_text(104, pos == LP_ROLES ? "ISOD'S READING. ALL 8 BITS: FF / 00" : "SWAPPED. ALL 8 BITS: FF / 00",
                &FT_SM, UI_ACC);
        lp_text(124, "ANY LIGHT? CAB, DLY OR REV: MARK", &FT_SM, UI_TXT);
        lp_text(144, "KNOB: NEXT TEST", &FT_SM, UI_DIM);
    } else {
        uint32_t id = lp.ids[pos - LP_LINES];
        const char *role = lp_role_of(id);
        l[0] = 0;
        lp_name(l, id);
        lp_text(30, l, &FT_NM, UI_TXT);
        l[0] = 0;
        if (role) { b_str(l, role); b_str(l, ": DON'T PRESS IT"); }
        lp_text(66, l, &FT_SM, UI_DIM);
        lp_text(86, "LINE 1 / 0, AND THE 595 FF / 00", &FT_SM, UI_ACC);
        lp_text(104, "ALL FOUR MIXES ONCE A SECOND", &FT_SM, UI_ACC);
        lp_text(124, "ANY LIGHT? CAB, DLY OR REV: MARK", &FT_SM, UI_TXT);
        lp_text(144, "KNOB: NEXT PIN", &FT_SM, UI_DIM);
    }
    {   /* the marks, six a line */
        char m[2][44];
        uint32_t k = 0;
        m[0][0] = m[1][0] = 0;
        for (uint32_t j = 0; j < LP_LINES + 64u; j++) {
            char nm[6];
            nm[0] = 0;
            if (j < LP_LINES) {
                if (!(lp.mark_pos >> j & 1u)) continue;
                if (j < LP_ROLES) { b_str(nm, "P"); b_num(nm, j + 1u); } else { b_str(nm, "R"); b_num(nm, j - LP_ROLES + 1u); }
            } else { if (!lp_has(lp.mark, j - LP_LINES + 1u)) continue; lp_name(nm, j - LP_LINES + 1u); }
            if (k < 12u) { char *t = m[k / 6u]; if (t[0]) b_str(t, " "); b_str(t, nm); }
            k++;
        }
        l[0] = 0;
        b_str(l, "MARKED: ");
        if (k) b_num(l, k); else b_str(l, "NONE YET");
        lp_text(178, l, &FT_SM, UI_DIM);
        lp_text(198, m[0], &FT_SM, UI_TXT);
        lp_text(216, m[1], &FT_SM, UI_TXT);
    }
}

static void lp_show(void)                                            /* the outputs for this position and phase */
{
    if (lp.pos < LP_LINES) { lp_595((lp.phase & 1u) ? 0x00u : 0xFFu); return; }
    lp_level(lp.id, lp.phase & 1u);
    lp_595((lp.phase & 2u) ? 0xFFu : 0x00u);
}
static void lp_enter(uint32_t now)                                   /* leave the old test, start the new one */
{
    if (lp.id) { lp_give(lp.id); lp.id = 0; }
    lp.phase = 0;
    lp.t = now;
    if (lp.pos < LP_ROLES) {
        if (lp.pos == 1u && !lp.stock) { fm1_p33_write(LP_CON5, LP_CON5_STOCK); lp.stock = 1; }   /* once */
        if (lp.role != 0) lp_roles(0);
    } else if (lp.pos < LP_LINES) lp_roles((int)(lp.pos - LP_ROLES));
    else {
        if (lp.role != 0) lp_roles(0);
        lp.id = lp.ids[lp.pos - LP_LINES];
        lp_take(lp.id);
    }
    lp_show();
    lp.dirty = 1;
}

static void lp_init(void)
{
    lp.n = 0;
    for (uint32_t p = 0; p < 4u; p++)
        for (uint32_t b = 0; b < 16u; b++)
            if (!(LP_NEVER[p] & (1u << b))) lp.ids[lp.n++] = (uint8_t)(p * 16u + b + 1u);
    lp.pos = lp.id = 0;
    lp.acc = 0;
    lp.enc = (uint8_t)(PC_IN & ENC_MASK);
    lp.mark_pos = 0;
    for (int k = 0; k < 4; k++) lp.mark[k] = 0;
    lp.k.stab = lp.k.last = lp.k.cnt = 0;
    lp.stock = 0;
    lp.con5_found = fm1_p33_read(LP_CON5);                           /* before anything here changes it */
    guard.pending = 0;                                               /* a reset here is not a failed boot */
    guard.failed = 0;                                                /* (no UBOOT, no safe mode screen) */
    op_diag.stage = ST_OK;
    lp.kind = -1;
    lp_roles(0);
    lp.t_keys = ms_now();
    lp_enter(lp.t_keys);                                             /* power as found */
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
                uint32_t n = lp_npos();
                lp.pos = dd > 0 ? (lp.pos + 1u) % n : (lp.pos + n - 1u) % n;
                lp_enter(now);
            }
        }
    }
    if (now - lp.t_keys >= 10u) {   /* the analog keys (CAB, DLY, REV), every 10 ms: any of them marks */
        int32_t v = adc_read2(ADC_CH_KEYS);
        int down = keys_decode(v) != KEY_NONE || (v >= 0 && v < LP_KEY_PRESSED);
        lp.t_keys = now;
        if (dbn_press(&lp.k, down)) {
            if (lp.pos < LP_LINES) lp.mark_pos ^= 1u << lp.pos;
            else lp.mark[(lp.id - 1u) >> 4] ^= 1u << ((lp.id - 1u) & 15u);
            lp.dirty = 1;
        }
    }
    if (now - lp.t >= LP_STEP_MS) {
        lp.t = now;
        lp.phase = (lp.phase + 1u) & 3u;
        lp_show();
        lp_dot();
    }
    if (lp.dirty) { lp.dirty = 0; lp_draw(); }
}

static void led_probe_run(void)
{
    lp_init();
    for (;;) lp_step(ms_now());
}
