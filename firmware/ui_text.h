/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — screen drawing over lcd_win / lcd_px / lcd_end / lcd_rect (firmware/openpocket.c on the pedal,
 * tests/uisim.c on the PC): anti-aliased text and rounded boxes. */
#pragma once
/* ================= ANTI-ALIASED text (Inter font, 4-bit alpha) =================
 * ft_box(x, y, w, ...) draws a box of fixed width w and of the font's height: padding + text + padding,
 * in a single pixel stream (background included). Nothing is cleared first: no flicker. align 0 = left,
 * 1 = centre, 2 = right. Text that is too long is clipped. */
#if PX_OWN_FX
#include "fonts_caps.h"                                /* (test build 49: no lowercase, drawn as capitals: 11 KB of flash) */
#else
#include "fonts.h"
#endif
static int ft_glyph(const op_font *f, char c)
{
#if PX_OWN_FX
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
#endif
    for (int i = 0; i < f->n; i++) if (f->chars[i] == c) return i;
    return 0;                                          /* unknown -> 1st character (space) */
}
static uint32_t ft_width(const op_font *f, const char *s)
{
    uint32_t w = 0;
    while (*s) w += f->adv[ft_glyph(f, *s++)];
    return w;
}
static uint16_t ft_mix(uint16_t fg, uint16_t bg, uint32_t a)   /* a = 0..15 */
{
    uint32_t r = (((fg >> 11) & 31u) * a + ((bg >> 11) & 31u) * (15u - a)) / 15u;
    uint32_t g = (((fg >> 5) & 63u) * a + ((bg >> 5) & 63u) * (15u - a)) / 15u;
    uint32_t b = ((fg & 31u) * a + (bg & 31u) * (15u - a)) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
static uint32_t ft_box(uint32_t x, uint32_t y, uint32_t w, const char *s, const op_font *f,
                       uint16_t fg, uint16_t bg, int align)
{
    uint8_t gi[40]; uint32_t n = 0, tw = 0;
    if (x >= 240u || w == 0u) return 0;
    if (x + w > 240u) w = 240u - x;
    while (s[n] && n < 40u) {                          /* characters that fit in the box */
        int g = ft_glyph(f, s[n]);
        if (tw + f->adv[g] > w) break;
        gi[n] = (uint8_t)g; tw += f->adv[g]; n++;
    }
    uint32_t lpad = align == 1 ? (w - tw) / 2u : (align == 2 ? w - tw : 0u), rpad = w - tw - lpad;
    uint16_t pal[16];
    for (uint32_t a = 0; a < 16u; a++) pal[a] = ft_mix(fg, bg, a);
    lcd_win(x, y, w, f->h);
    for (uint32_t r = 0; r < f->h; r++) {
        for (uint32_t i = 0; i < lpad; i++) lcd_px(bg);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t a = f->adv[gi[k]];
            const uint8_t *b = f->bits + f->off[gi[k]];
            uint32_t p = r * a;
            for (uint32_t c = 0; c < a; c++, p++) lcd_px(pal[(b[p >> 1] >> ((p & 1u) ? 0 : 4)) & 15u]);
        }
        for (uint32_t i = 0; i < rpad; i++) lcd_px(bg);
        if ((r & 7u) == 7u) fm1_wdt_feed();
    }
    lcd_end();
    return tw;
}


/* rounded box: corners of radius r (0..6) in the background colour bg, one pixel stream per corner row */
static void ui_rbox(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint16_t col, uint16_t bg)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 1, 0}, {3, 2, 1, 1, 0, 0}};
    if (r > 6u) r = 6u;
    if (2u * r > h) r = h / 2u;
    if (2u * r > w) r = w / 2u;
    for (uint32_t i = 0; i < r; i++) {
        uint32_t k = IN[r][i];
        for (int side = 0; side < 2; side++) {
            uint32_t yy = side ? y + h - 1u - i : y + i;
            if (k) { lcd_rect(x, yy, k, 1, bg); lcd_rect(x + w - k, yy, k, 1, bg); }
            lcd_rect(x + k, yy, w - 2u * k, 1, col);
        }
    }
    lcd_rect(x, y + r, w, h - 2u * r, col);
}
