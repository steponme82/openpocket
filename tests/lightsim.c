/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the button lights in the normal firmware (firmware/px_lights.h over px_leds.h, issue #8) on the PC,
 * against fake port C registers and a model of the 74HC595 with its output enable on PC7:
 *   cc -O1 -Wall -Iengine -Ifirmware -o build/lightsim tests/lightsim.c && build/lightsim
 * Checks: after leds_init the lights are enabled (PC7 low) with all of them dark; every one of the twelve blocks,
 * alone, lights exactly its button (the FX group's seven lights FX); all on lights the six effect buttons and not
 * HOME; HOME blinks at 2 Hz in the tuner and only there; bit 7 is never lit; the byte is written only when it
 * changes; lights_off darkens all; only PC5, PC6, PC7 and PC8 are ever touched. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

/* the FX button's group, written out again here (ui.h's UI_FXB is the firmware's): drive, SFX, synth, pitch, comp,
 * gate, EQ */
static const uint8_t UI_FXB[7] = { PX_B_DRIVE, PX_B_SFX, PX_B_SYN, PX_B_PITCH, PX_B_COMP, PX_B_GATE, PX_B_EQ };

/* ---- port C and the 595: DS PC8, SHCP PC5, STCP PC6, OE PC7 (low = outputs on); a 0 on an output lights it */
enum { R_OUT = 0, R_DIR = 2, R_PU = 4, R_PD = 5 };
static uint32_t pc[16], pc_start[16];
static uint32_t lv_prev;
static uint8_t sr, q;
static unsigned long latches, waits;
static uint32_t pad(void) { uint32_t d = pc[R_DIR]; return (pc[R_OUT] & ~d) | (pc[R_PU] & d); }
static void edge(void)
{
    uint32_t lv = pad(), rise = lv & ~lv_prev;
    if (rise & (1u << 5)) sr = (uint8_t)((sr << 1) | ((lv >> 8) & 1u));
    if (rise & (1u << 6)) { q = sr; latches++; }
    lv_prev = lv;
}
static int enabled(void) { return !(pc[R_DIR] & (1u << 7)) && !(pad() & (1u << 7)); }
static uint8_t lit(void) { return enabled() ? (uint8_t)~q : 0u; }   /* what the eye sees: 1 = lit */
#define LEDS_PC(r) (pc[(r) / 4u])
#define LEDS_WAIT() (waits++)
#define LEDS_EDGE() edge()
#include "px_lights.h"

static int failed;
static void ok(int c, const char *what) { printf("%-66s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }
/* the LED map read on a BlackBox (2026-11-06), written out again here: bit 0 HOME, 1 FX, 2 AMP, 3 MOD, 4 DLY, 5 REV,
 * 6 CAB, 7 none */
enum { W_HOME = 0, W_FX = 1, W_AMP = 2, W_MOD = 3, W_DLY = 4, W_REV = 5, W_CAB = 6 };
static uint8_t want_of(int b)                   /* the light a block alone should light */
{
    for (int i = 0; i < 7; i++) if (UI_FXB[i] == b) return 1u << W_FX;
    switch (b) {
    case PX_B_AMP: return 1u << W_AMP;
    case PX_B_MOD: return 1u << W_MOD;
    case PX_B_DELAY: return 1u << W_DLY;
    case PX_B_REVERB: return 1u << W_REV;
    case PX_B_CAB: return 1u << W_CAB;
    }
    return 0u;
}
static int only_our_pins(void)
{
    for (int r = 0; r < 16; r++) {
        uint32_t may = (r == R_OUT || r == R_DIR) ? 0x1E0u : (r == R_PU || r == R_PD) ? 0x160u : 0u;
        if ((pc[r] ^ pc_start[r]) & ~may) return 0;
    }
    return 1;
}

int main(void)
{
    static px_preset_t p;
    memset(pc, 0, sizeof pc);
    pc[R_OUT] = 0x5A5Au | 0x80u; pc[R_DIR] = 0xFFFFu & ~0x80u;         /* PC7 an output, high, as lcd_init left it */
    pc[R_PU] = 0xFFFFu;
    memcpy(pc_start, pc, sizeof pc);
    lv_prev = pad(); sr = 0x5Au; q = 0x00u;                             /* a 595 holds garbage at power-on */

    leds_init();
    ok(enabled() && lit() == 0u, "after leds_init: the lights enabled, all dark");

    memset(&p, 0, sizeof p);
    lights_update(&p, 0, 0);
    ok(lit() == 0u && latches == 1u, "no block on: all dark, nothing written again");
    {
        int good = 1, every = 1;
        for (int b = 0; b < PX_NBLK; b++) {
            memset(&p, 0, sizeof p); p.on[b] = 1;
            lights_update(&p, 0, 0);
            if (lit() != want_of(b)) { printf("   block %d lit %02X, wanted %02X\n", b, lit(), want_of(b)); good = 0; }
            if (!want_of(b)) every = 0;
        }
        ok(good, "each block alone lights its own button");
        ok(every, "every block has a light (the FX group's seven: FX)");
    }
    memset(&p, 0, sizeof p);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 1;
    lights_update(&p, 0, 0);
    ok(lit() == 0x7Eu, "all on: FX AMP MOD DLY REV CAB lit, HOME dark");
    {
        unsigned long l0 = latches;
        for (uint32_t t = 0; t < 2000u; t++) lights_update(&p, 0, t);
        ok(latches == l0, "nothing changes: nothing written");
    }
    {
        int flips = 0, others = 1; uint8_t last = lit();
        for (uint32_t t = 0; t < 2000u; t++) {
            lights_update(&p, 1, t);
            if ((lit() ^ last) & (1u << W_HOME)) flips++;
            if ((lit() & 0x7Eu) != 0x7Eu) others = 0;
            last = lit();
        }
        ok(flips >= 7 && flips <= 9, "tuner: HOME blinks at 2 Hz");
        ok(others, "tuner: the effect lights stay as they are");
        lights_update(&p, 0, 0);
        ok(!(lit() & (1u << W_HOME)), "tuner closed: HOME dark");
    }
    ok(!(lit() & 0x80u) && (q & 0x80u), "bit 7 never lit");
    lights_off();
    ok(lit() == 0u && enabled(), "lights_off: all dark");
    ok(only_our_pins(), "only PC5, PC6, PC7 and PC8 touched");
    ok(waits > 0u, "the 595 timing waits are used");
    printf("\n%s\n", failed ? "lights tests FAILED" : "all lights tests passed");
    return failed ? 1 : 0;
}
