/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — what the button lights show (issue #8). px_leds.h drives them; this decides them, once per main-loop
 * pass, from the preset being played:
 *   FX                          lit while any block of the FX button's group is on (UI_FXB: drive, SFX, synth,
 *                               pitch, comp, gate, EQ)
 *   AMP, MOD, DLY, REV, CAB     lit while their block is on
 *   HOME                        blinks (2 Hz) while the tuner is open, dark otherwise
 * The bits are the LED map's, read on a BlackBox (2026-11-06): 0 HOME, 1 FX, 2 AMP, 3 MOD, 4 DLY, 5 REV, 6 CAB,
 * 7 none: the panel's order (a Pocket X may differ: not checked there yet). On the wire a 0 lights, so the byte
 * goes out inverted (as the stock firmware sends it), and only when it changes. Confirmed on a BlackBox: each
 * light follows its block, HOME blinks in the tuner. Not yet: a setting to switch the lights off (it belongs with
 * the settings store isod89/openpocket#10 adds).
 * Included by openpocket.c (after ui.h: UI_FXB) and by tests/lightsim.c. */
#pragma once
#include <stdint.h>
#include "px_leds.h"

enum { LT_HOME, LT_FX, LT_AMP, LT_MOD, LT_DLY, LT_REV, LT_CAB };      /* the light on each bit */
#define LT_BLINK_MS 250u                                             /* the tuner's HOME blink: half a period */

static uint8_t lights_of(const px_preset_t *p, int tuner, uint32_t now)   /* the lit lights, 1 = lit */
{
    uint8_t v = 0;
    for (int i = 0; i < 7; i++)
        if (p->on[UI_FXB[i]]) v |= 1u << LT_FX;
    if (p->on[PX_B_AMP]) v |= 1u << LT_AMP;
    if (p->on[PX_B_MOD]) v |= 1u << LT_MOD;
    if (p->on[PX_B_DELAY]) v |= 1u << LT_DLY;
    if (p->on[PX_B_REVERB]) v |= 1u << LT_REV;
    if (p->on[PX_B_CAB]) v |= 1u << LT_CAB;
    if (tuner && !((now / LT_BLINK_MS) & 1u)) v |= 1u << LT_HOME;
    return v;
}
static void lights_show(uint8_t lit)                                 /* written only when the byte changes */
{
    uint8_t wire = (uint8_t)~lit;
    if (wire != leds_byte) leds_write(wire);
}
static void lights_update(const px_preset_t *p, int tuner, uint32_t now) { lights_show(lights_of(p, tuner, now)); }
static void lights_off(void) { lights_show(0u); }
