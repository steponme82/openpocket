/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the pedal's LCD on the PC: the 240x240 RGB565 frame buffer behind the lcd_win / lcd_px / lcd_end /
 * lcd_rect calls that firmware/ui.h and ui_text.h draw through (the pedal streams the same calls to the panel).
 * Shared by tests/uisim.c (screenshots of a script) and tests/edsim.c (the live, hosted pedal: tools/sim.mjs).
 * fb_gen counts the drawing calls, so a host can tell whether the screen changed. */
#pragma once

enum { RED = 0xF800, GRN = 0x07E0, BLU = 0x001F, WHT = 0xFFFF, BLK = 0x0000, CYN = 0x07FF, YEL = 0xFFE0, ORG = 0xFD20 };
enum { UI_BG = 0x0841, UI_PANEL = 0x18E3, UI_SEL = 0xFB00, UI_ACC = 0xFD20, UI_TXT = 0xEF7D, UI_DIM = 0x8410, UI_OK = 0x4E6B, UI_OFF = 0x2124 };

static uint16_t fb[240 * 240];
static uint32_t fb_gen;
#define lcd_gen fb_gen                                 /* (ui.h, test build 51: the pedal's own count of drawings) */
static uint32_t wx, wy, ww, wh, wi;
static unsigned long px_sent;
static void lcd_win(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { wx = x; wy = y; ww = w; wh = h; wi = 0; }
static void lcd_px(uint16_t c)
{
    uint32_t x = wx + wi % ww, y = wy + wi / ww;
    if (x < 240u && y < 240u) fb[y * 240u + x] = c;
    wi++; px_sent++; fb_gen++;
}
static void lcd_end(void) { }
static void lcd_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (w == 0u || h == 0u || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) fb[(y + j) * 240u + x + i] = c;
    px_sent += w * h; fb_gen++;
}
