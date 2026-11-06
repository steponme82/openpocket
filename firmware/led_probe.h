/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the LED hunt (issue #8): why do the BlackBox's button lights stay dark?
 *
 * Built only with LED_PROBE 1 (openpocket.c). That build plays no sound. The LED map (git history) drove the
 * 74HC595 as isod89 read it in the stock firmware (data PC8, clock PC5, latch PC6: px_leds.h) and nothing lit, not
 * even all 8 bits blinking. This build tests the ways that reading could be incomplete. The knob steps through:
 *   - PIN CHECK: PC5, PC6 and PC8 driven to 1 and 0 and read back, then left as inputs with a pull-up and with a
 *     pull-down. A pin wired only to a 595 input reads DRIVES OK, FREE; one held by something else reads HELD.
 *   - ROLES 1 to 6: the six ways to give data, clock and latch to PC5, PC6 and PC8 (1 is isod89's), with all 8
 *     bits blinking (FF and 00, 2 Hz).
 *   - LED LINE 1 to 47: one more pin at a time blinks (1 and 0, 2 Hz) while the 595 (isod89's roles) blinks FF and
 *     00 at 1 Hz, so every mix of the two comes once a second. On the FM-1 the 595 only selects a column, and an LED
 *     lights when its LED line is driven too (bsp/boards/fm1/fm1_input.h); an enable (OE, a supply) shows the same
 *     way. Never driven: PA4..PA10 (I2S, amp enable, MASTER, the analog keys), PB1/5/6/7 and PC7 (the screen),
 *     PC0/1 (the knob), port D (the flash). The first light finder (git history) drove these same pins alone,
 *     without trouble.
 * CAB, DLY or REV (the analog keys, never driven here) marks the test on screen, again unmarks it; the list stays at
 * the bottom for a photo. USB stays up, as in the normal build, so the web installer is the way back.
 *
 * Included by openpocket.c (LED_PROBE 1) and by tests/lpsim.c on the PC. The including file provides: PC_IN,
 * ENC_MASK, keys_decode, adc_read2, ADC_CH_KEYS, KEY_NONE, QDEC, dbn_t / dbn_press, ms_now, fm1_delay_us,
 * fm1_wdt_feed, px_usb_service, lcd_rect, ft_box / ft_width and the fonts (ui_text.h), b_str / b_num, guard,
 * op_diag / ST_OK and the UI colours. */
#pragma once
#include <stdint.h>
#include "px_leds.h"

#ifndef LP_REG              /* a GPIO port register: 0x50000 + port * 0x40 + r (bsp/chip/fm1_gpio.h) */
#define LP_REG(port, r) (*(volatile uint32_t *)(0x50000u + (uint32_t)(port) * 0x40u + (uint32_t)(r)))
#endif
#ifndef LP_SETTLE           /* before reading a pin back: an internal pull charges the pad in about a microsecond */
#define LP_SETTLE() fm1_delay_us(50u)
#endif
#ifndef LP_TEXT_HOOK        /* tests/lpsim.c checks that every line fits its box */
#define LP_TEXT_HOOK(s, f, w) ((void)0)
#endif
enum { LP_OUT = 0x00, LP_DIR = 0x08, LP_DIE = 0x0C, LP_PU = 0x10, LP_PD = 0x14 };   /* DIR: 1 input, 0 output */

static const uint8_t LP_PORT[4] = { 0u, 1u, 2u, 7u };                /* PA, PB, PC, PH */
static const char LP_PN[4] = { 'A', 'B', 'C', 'H' };
static const uint32_t LP_NEVER[4] = { 0x07F0u, 0x00E2u, 0x01E3u, 0x0000u };   /* PA4..10 | PB1,5,6,7 | PC0,1,5..8 */
static const uint8_t LP_ROLE[6][3] = {                               /* the PC bit of data, clock, latch */
    { 8u, 5u, 6u }, { 8u, 6u, 5u }, { 5u, 8u, 6u }, { 5u, 6u, 8u }, { 6u, 5u, 8u }, { 6u, 8u, 5u } };
static const uint8_t LP_CHK[3] = { 5u, 6u, 8u };                     /* the pins the pin check reads back */
#define LP_STEP_MS 250u
#define LP_LINES 7u                                                  /* knob: 0 pin check, 1..6 roles, 7.. lines */
enum { CK_OK, CK_HI, CK_LO, CK_ODD };                                /* a pin check result */

static struct {
    uint8_t ids[64];            /* the LED line candidates: id = port index * 16 + bit + 1 (1..64) */
    uint32_t n, pos, id;        /* how many; the knob's position; the line driven now (0 = none) */
    uint32_t sv_out, sv_dir, sv_pu, sv_pd;   /* that line's bits as they were */
    uint32_t t, phase, t_keys;
    int role;                   /* the roles the 595 is driven with (LP_ROLE), -1 during the pin check */
    int dirty, key, kind;       /* kind: the page on screen (0 pin check, 1 roles, 2 line; -1 none) */
    int32_t acc;
    uint8_t enc;
    uint8_t chk[3][2];          /* per pin: driven 1 / 0 read back, pull-up / pull-down read back */
    uint32_t mark_role, mark[4];
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

static uint32_t lp_read(uint32_t m) { LEDS_EDGE(); LP_SETTLE(); return (PC_IN & m) ? 1u : 0u; }
static uint8_t lp_class(uint32_t a, uint32_t b) { return (uint8_t)(a ? (b ? CK_HI : CK_OK) : (b ? CK_ODD : CK_LO)); }
static void lp_check(void)                                           /* PC5, PC6, PC8: driven, then pulled */
{
    lp.role = -1;
    for (int i = 0; i < 3; i++) {
        uint32_t m = 1u << LP_CHK[i], die = LP_REG(2u, LP_DIE) & m, h, l;
        LP_REG(2u, LP_DIE) |= m;                                     /* read the pad */
        LP_REG(2u, LP_PU) &= ~m; LP_REG(2u, LP_PD) &= ~m;
        LP_REG(2u, LP_OUT) |= m; LP_REG(2u, LP_DIR) &= ~m; h = lp_read(m);
        LP_REG(2u, LP_OUT) &= ~m; l = lp_read(m);
        lp.chk[i][0] = lp_class(h, l);
        LP_REG(2u, LP_DIR) |= m; LP_REG(2u, LP_PU) |= m; h = lp_read(m);
        LP_REG(2u, LP_PU) &= ~m; LP_REG(2u, LP_PD) |= m; l = lp_read(m);
        lp.chk[i][1] = lp_class(h, l);
        LP_REG(2u, LP_PD) &= ~m;
        LP_REG(2u, LP_DIE) = (LP_REG(2u, LP_DIE) & ~m) | die;
    }
    lp_roles(0);
}

static void lp_text(uint32_t y, const char *s, const op_font *f, uint16_t col)
{
    LP_TEXT_HOOK(s, f, 232u);                                        /* 4 px clear of each edge */
    ft_box(0, y, 240, s, f, col, UI_BG, 1);
}
static int lp_lit(void)                                              /* the square: what blinks is at 1 */
{
    if (lp.pos == 0u) return 0;
    if (lp.pos < LP_LINES) return !(lp.phase & 1u);                  /* the 595 at FF */
    return (int)(lp.phase & 1u);                                     /* the line at 1 */
}
static void lp_dot(void) { lcd_rect(222, 10, 14, 14, lp_lit() ? UI_ACC : UI_PANEL); }
static void lp_draw(void)
{
    static const char *const CK_DRIVE[4] = { "DRIVES OK", "STUCK AT 1", "STUCK AT 0", "READS INVERTED" };
    static const char *const CK_PULL[4] = { "FREE", "HELD HIGH", "HELD LOW", "ODD" };
    static const char *const ROLE_NAME[3] = { "DATA", "CLOCK", "LATCH" };
    char l[48];
    uint32_t pos = lp.pos;
    int kind = pos == 0u ? 0 : pos < LP_LINES ? 1 : 2;
    if (kind != lp.kind) { lcd_rect(0, 0, 240, 240, UI_BG); lp.kind = kind; }   /* no rows left from another page */
    l[0] = 0;
    if (pos == 0u) b_str(l, "PIN CHECK");
    else if (pos < LP_LINES) { b_str(l, "ROLES "); b_num(l, pos); b_str(l, " OF 6"); }
    else { b_str(l, "LED LINE "); b_num(l, pos - LP_LINES + 1u); b_str(l, " OF "); b_num(l, lp.n); }
    LP_TEXT_HOOK(l, &FT_MD, 200u);
    ft_box(20, 6, 200, l, &FT_MD, UI_ACC, UI_BG, 1);
    lp_dot();
    if (pos == 0u) {
        for (int i = 0; i < 3; i++) {
            int good = lp.chk[i][0] == CK_OK && lp.chk[i][1] == CK_OK;
            l[0] = 0;
            b_str(l, "PC"); b_num(l, LP_CHK[i]); b_str(l, ": ");
            b_str(l, CK_DRIVE[lp.chk[i][0]]); b_str(l, ", "); b_str(l, CK_PULL[lp.chk[i][1]]);
            lp_text(40u + (uint32_t)i * 20u, l, &FT_SM, good ? UI_OK : RED);
        }
        lp_text(104, "ON A 595 INPUT: DRIVES OK, FREE", &FT_SM, UI_DIM);
        lp_text(124, "", &FT_SM, UI_TXT);
        lp_text(144, "KNOB: NEXT TEST", &FT_SM, UI_DIM);
    } else if (pos < LP_LINES) {
        const uint8_t *r = LP_ROLE[pos - 1u];
        for (int i = 0; i < 3; i++) {
            l[0] = 0;
            b_str(l, ROLE_NAME[i]); b_str(l, " PC"); b_num(l, r[i]);
            lp_text(36u + (uint32_t)i * 20u, l, &FT_SM, UI_TXT);
        }
        lp_text(104, pos == 1u ? "ISOD'S READING. ALL 8 BITS: FF / 00" : "SWAPPED. ALL 8 BITS: FF / 00", &FT_SM, UI_ACC);
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
        for (uint32_t j = 0; j < 6u + 64u; j++) {
            char nm[6];
            nm[0] = 0;
            if (j < 6u) { if (!(lp.mark_role >> j & 1u)) continue; b_str(nm, "R"); b_num(nm, j + 1u); }
            else { if (!lp_has(lp.mark, j - 5u)) continue; lp_name(nm, j - 5u); }
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
    if (lp.pos == 0u) return;
    if (lp.pos < LP_LINES) { lp_595((lp.phase & 1u) ? 0x00u : 0xFFu); return; }
    lp_level(lp.id, lp.phase & 1u);
    lp_595((lp.phase & 2u) ? 0xFFu : 0x00u);
}
static void lp_enter(uint32_t now)                                   /* leave the old test, start the new one */
{
    if (lp.id) { lp_give(lp.id); lp.id = 0; }
    lp.phase = 0;
    lp.t = now;
    if (lp.pos == 0u) lp_check();
    else if (lp.pos < LP_LINES) lp_roles((int)lp.pos - 1);
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
    lp.key = KEY_NONE;
    lp.enc = (uint8_t)(PC_IN & ENC_MASK);
    lp.mark_role = 0;
    for (int k = 0; k < 4; k++) lp.mark[k] = 0;
    lp.k.stab = lp.k.last = lp.k.cnt = 0;
    guard.pending = 0;                                               /* a reset here is not a failed boot */
    guard.failed = 0;                                                /* (no UBOOT, no safe mode screen) */
    op_diag.stage = ST_OK;
    lp.kind = -1;
    lp.t_keys = ms_now();
    lp_enter(lp.t_keys);                                             /* the pin check */
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
        lp.t_keys = now;
        lp.key = keys_decode(adc_read2(ADC_CH_KEYS));
        if (dbn_press(&lp.k, lp.key != KEY_NONE) && lp.pos) {
            if (lp.pos < LP_LINES) lp.mark_role ^= 1u << (lp.pos - 1u);
            else lp.mark[(lp.id - 1u) >> 4] ^= 1u << ((lp.id - 1u) & 15u);
            lp.dirty = 1;
        }
    }
    if (lp.pos && now - lp.t >= LP_STEP_MS) {
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
