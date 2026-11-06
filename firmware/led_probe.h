/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the LED map (issue #8): which bit of the 74HC595 (px_leds.h) lights which button?
 *
 * Built only with LED_PROBE 1 (openpocket.c). That build plays no sound. The knob steps through ALL 8 BITS, then
 * BIT 0 .. BIT 7; the bits on screen blink at 2 Hz (the byte alternates between them and 0, so the one light that
 * changes is theirs, whether it is lit by a 1 or by a 0: the others may stay lit) and the square at the top right is
 * lit while they are 1.
 *   - Pressing the button whose light blinks writes it into the table at the bottom (FX, MOD, AMP, CAB, DLY, REV,
 *     HOME, the knob's PUSH, either footswitch); the same button again clears it. A photo of the table is the map.
 *   - Driven: PC5, PC6 and PC8 (the 595's shift clock, latch and data) and PC7, the lights' enable, low (px_leds.h
 *     leds_init, as the stock firmware). The earlier test builds of issue #8 lit nothing because PC7 stayed high:
 *     OpenPocket held it so as a screen line.
 *   - USB stays up, as in the normal build, so the web installer takes the pedal back to a normal OpenPocket or to
 *     the official firmware.
 *
 * Included by openpocket.c (LED_PROBE 1) and by tests/lpsim.c on the PC. The including file provides: PA_IN, PH_IN,
 * PC_IN, ENC_MASK, PA_FX / PA_HOME / PA_FS_UP / PA_FS_DN, PH_PUSH / PH_MOD / PH_AMP, base_pa, base_ph, keys_decode,
 * adc_read2, ADC_CH_KEYS, KEY_CAB / KEY_DLY / KEY_REV, QDEC, dbn_t / dbn_press, ms_now, fm1_delay_us, fm1_wdt_feed,
 * px_usb_service, lcd_rect, ft_box / ft_width and the fonts (ui_text.h), b_str / b_num, guard, op_diag / ST_OK and
 * the UI colours. */
#pragma once
#include <stdint.h>
#include "px_leds.h"

#ifndef LP_TEXT_HOOK        /* tests/lpsim.c checks that every line fits its box */
#define LP_TEXT_HOOK(s, f, w) ((void)0)
#endif
#define LP_BLINK_MS 250u    /* half a period: 2 Hz */
#define LP_NPOS 9u          /* ALL 8 BITS, BIT 0 .. BIT 7 */

enum { LB_FX, LB_MOD, LB_AMP, LB_CAB, LB_DLY, LB_REV, LB_HOME, LB_PUSH, LB_FSR, LB_FSL, LB_N };
static const char *const LP_BTN[LB_N] = { "FX", "MOD", "AMP", "CAB", "DLY", "REV", "HOME", "PUSH", "R FOOT", "L FOOT" };

static struct {
    uint32_t pos;               /* 0 = all 8 bits, 1..8 = bit pos - 1 */
    uint32_t t_blink, t_keys;
    int hi, dirty, key;
    int32_t acc;
    uint8_t enc;
    uint8_t map[8];             /* per bit: the button that lit, + 1 (0 = none yet) */
    dbn_t b[LB_N];
} lp;

static uint8_t lp_mask(void) { return lp.pos ? (uint8_t)(1u << (lp.pos - 1u)) : 0xFFu; }
static void lp_show(void) { leds_write(lp.hi ? lp_mask() : 0u); }

static void lp_hex2(char *d, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    while (*d) d++;
    d[0] = H[(v >> 4) & 15u]; d[1] = H[v & 15u]; d[2] = 0;
}
static void lp_text(uint32_t x, uint32_t y, uint32_t w, const char *s, const op_font *f, uint16_t col, int align)
{
    LP_TEXT_HOOK(s, f, w);
    ft_box(x, y, w, s, f, col, UI_BG, align);
}
static void lp_dot(void) { lcd_rect(222, 10, 14, 14, lp.hi ? UI_ACC : UI_PANEL); }
static void lp_draw(void)
{
    char l[44];
    uint32_t bit = lp.pos ? lp.pos - 1u : 0u;
    int mapped = lp.pos && lp.map[bit];
    lp_text(20, 6, 200, "LED MAP", &FT_MD, UI_ACC, 1);
    lp_dot();
    l[0] = 0;
    if (lp.pos) { b_str(l, "BIT "); b_num(l, bit); } else b_str(l, "ALL 8 BITS");
    lp_text(0, 36, 240, l, &FT_NM, UI_TXT, 1);
    l[0] = 0;
    b_str(l, "BYTE "); lp_hex2(l, lp_mask()); b_str(l, " / 00, TWICE A SECOND");
    lp_text(0, 74, 240, l, &FT_SM, UI_ACC, 1);
    l[0] = 0;
    if (!lp.pos) b_str(l, "EVERY BUTTON LIGHT SHOULD BLINK");
    else if (mapped) { b_str(l, "BIT "); b_num(l, bit); b_str(l, " LIGHTS: "); b_str(l, LP_BTN[lp.map[bit] - 1u]); }
    else b_str(l, "PRESS THE BUTTON THAT BLINKS");
    lp_text(0, 96, 240, l, &FT_SM, mapped ? UI_OK : UI_TXT, 1);
    lp_text(0, 114, 240, !lp.pos ? "KNOB: ONE BIT AT A TIME" : mapped ? "SAME BUTTON AGAIN: CLEAR" : "KNOB: NEXT BIT",
            &FT_SM, UI_DIM, 1);
    for (uint32_t i = 0; i < 8u; i++) {                              /* the map: bits 0-3 left, 4-7 right */
        uint16_t col = (lp.pos && i == bit) ? UI_ACC : lp.map[i] ? UI_TXT : UI_DIM;
        l[0] = 0;
        b_str(l, "BIT "); b_num(l, i); b_str(l, ": "); b_str(l, lp.map[i] ? LP_BTN[lp.map[i] - 1u] : "--");
        lp_text(i < 4u ? 24u : 136u, 140u + (i & 3u) * 18u, 100u, l, &FT_SM, col, 0);
    }
    lp_text(0, 218, 240, "SQUARE LIT = BITS AT 1", &FT_SM, UI_DIM, 1);
}

static void lp_init(void)
{
    lp.pos = 0;
    lp.hi = 1;
    lp.acc = 0;
    lp.key = KEY_NONE;
    lp.enc = (uint8_t)(PC_IN & ENC_MASK);
    for (int i = 0; i < 8; i++) lp.map[i] = 0;
    for (int i = 0; i < LB_N; i++) lp.b[i].stab = lp.b[i].last = lp.b[i].cnt = 0;
    guard.pending = 0;                                               /* a reset here is not a failed boot */
    guard.failed = 0;                                                /* (no UBOOT, no safe mode screen) */
    op_diag.stage = ST_OK;
    leds_init();
    lp.t_blink = lp.t_keys = ms_now();
    lp_show();
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
                lp.pos = dd > 0 ? (lp.pos + 1u) % LP_NPOS : (lp.pos + LP_NPOS - 1u) % LP_NPOS;
                lp.hi = 1;
                lp.t_blink = now;
                lp_show();
                lp.dirty = 1;
            }
        }
    }
    {   /* the buttons (pressed = differs from the idle state); CAB / DLY / REV are the analog keypad, every 10 ms */
        uint32_t pa = (PA_IN & 0xFFFFu) ^ base_pa, ph = (PH_IN & 0xFFFFu) ^ base_ph;
        int hit = -1, keys = now - lp.t_keys >= 10u;
        int raw[LB_N] = {
            (pa & PA_FX) != 0, (ph & PH_MOD) != 0, (ph & PH_AMP) != 0, 0, 0, 0,
            (pa & PA_HOME) != 0, (ph & PH_PUSH) != 0, (pa & PA_FS_UP) != 0, (pa & PA_FS_DN) != 0 };
        if (keys) { lp.t_keys = now; lp.key = keys_decode(adc_read2(ADC_CH_KEYS)); }
        raw[LB_CAB] = lp.key == KEY_CAB; raw[LB_DLY] = lp.key == KEY_DLY; raw[LB_REV] = lp.key == KEY_REV;
        for (int i = 0; i < LB_N; i++) {
            if ((i == LB_CAB || i == LB_DLY || i == LB_REV) && !keys) continue;
            if (dbn_press(&lp.b[i], raw[i]) && hit < 0) hit = i;
        }
        if (hit >= 0 && lp.pos) {
            uint8_t *m = &lp.map[lp.pos - 1u];
            *m = (*m == (uint8_t)(hit + 1)) ? 0u : (uint8_t)(hit + 1);
            lp.dirty = 1;
        }
    }
    if (now - lp.t_blink >= LP_BLINK_MS) {
        lp.t_blink = now;
        lp.hi = !lp.hi;
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
