/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the button lights (issue #8): one 74HC595 shift register, 8 outputs, on port C, and PC7, the lights'
 * enable.
 *
 * Pins and timing as the stock firmware drives them (BlackBox_016, _020 and _021 have the same routine, read with
 * JieLi's objdump; isod89 read it first, and finds PocketX_016's the same):
 *   DS (data) = PC8, SHCP (shift clock) = PC5, STCP (latch) = PC6.
 *   PC7 = the lights' enable, low = on (the 595's OE, by how the stock code uses it). The 595 init sets PC7 high
 *   with the three pins and loads the first byte; then the lights go on with PC7 low: in 016 / 020 as the first
 *   step of the screen init, which leaves it low, in 021 right after that first byte. 021 also sets PC7 high again
 *   on its way off. OpenPocket used to keep PC7 high as a screen line: it is not one (the stock screen runs with it
 *   low), and with it high the lights stay dark whatever the 595 holds. Every probe before this one kept it high.
 *   A byte, MSB first: STCP low; for each bit SHCP low, DS = the bit, wait, SHCP high, wait; then STCP high, whose
 *   rising edge puts the byte on the outputs. Stock waits about 4 to 8 us at each step: LEDS_WAIT is 5 us.
 *   Stock sends its byte inverted (DS = 1 for a 0 bit): its logical byte has bit 0 a blink signal (a timer), bits
 *   1..6 the six effect lights, bit 7 always 0; at power-off it writes logical 0, FF on the wire. leds_write here
 *   takes the byte as it goes on the wire.
 * Confirmed on a BlackBox (2026-11-06): with PC7 low after the first byte the lights follow the 595; not tried on a
 * Pocket X yet (the same firmware, and the stock routine is the same there).
 *
 * Every edge is a read-modify-write of PORTC OUT, so these are called from the main loop only (no interrupt writes
 * port C). tests/lpsim.c checks the bit order and the waits against a model of the 595. */
#pragma once
#include <stdint.h>

#ifndef LEDS_PC             /* PORTC register r: OUT +0x00, DIR +0x08 (1 input, 0 output), PU +0x10, PD +0x14 */
#define LEDS_PC(r) (*(volatile uint32_t *)(0x50080u + (uint32_t)(r)))
#endif
#ifndef LEDS_WAIT
#define LEDS_WAIT() fm1_delay_us(5u)
#endif
#ifndef LEDS_EDGE           /* tests/lpsim.c: the 595 model sees each edge */
#define LEDS_EDGE() ((void)0)
#endif
#define LEDS_DS (1u << 8)   /* PC8: data */
#define LEDS_SH (1u << 5)   /* PC5: shift clock, DS is taken on its rising edge */
#define LEDS_ST (1u << 6)   /* PC6: latch, the outputs change on its rising edge */
#define LEDS_PINS (LEDS_DS | LEDS_SH | LEDS_ST)
#define LEDS_EN (1u << 7)   /* PC7: the lights' enable, low = on */
#define LEDS_OFF 0xFFu      /* the stock's power-off byte on the wire (logical 0, inverted) */

static uint8_t leds_byte;   /* what the outputs show now */

static void leds_pin(uint32_t m, uint32_t hi)
{
    if (hi) LEDS_PC(0x00) |= m;
    else LEDS_PC(0x00) &= ~m;
    LEDS_EDGE();
}
/* The pins are parameters so that a test build can try other roles (led_probe.h); the driver is leds_init /
 * leds_write with the pins above. */
static void leds_shift(uint8_t v, uint32_t ds, uint32_t sh, uint32_t st)
{
    leds_pin(st, 0u);
    for (int i = 7; i >= 0; i--) {
        leds_pin(sh, 0u);
        leds_pin(ds, ((uint32_t)v >> i) & 1u);
        LEDS_WAIT();
        leds_pin(sh, 1u);
        LEDS_WAIT();
    }
    leds_pin(st, 1u);
}
static void leds_setup(uint32_t ds, uint32_t sh, uint32_t st)
{
    uint32_t m = ds | sh | st;
    LEDS_PC(0x10) &= ~m;                                             /* no pull-ups (the boot leaves them on) */
    LEDS_PC(0x14) &= ~m;                                             /* no pull-downs */
    LEDS_PC(0x00) = (LEDS_PC(0x00) & ~m) | sh;                       /* SHCP 1, STCP 0, DS 0, as stock */
    LEDS_PC(0x08) &= ~m;                                             /* outputs */
    LEDS_EDGE();
    LEDS_WAIT();                                                     /* SHCP high for a while, even if it was low */
}
static void leds_write(uint8_t v)
{
    leds_shift(v, LEDS_DS, LEDS_SH, LEDS_ST);
    leds_byte = v;
}
static void leds_enable(uint32_t on)                                /* PC7 an output: low = lights on */
{
    if (on) LEDS_PC(0x00) &= ~LEDS_EN;
    else LEDS_PC(0x00) |= LEDS_EN;
    LEDS_PC(0x08) &= ~LEDS_EN;
    LEDS_EDGE();
}
static void leds_init(void)                                          /* as stock: load with the lights off, then on */
{
    leds_enable(0u);
    leds_setup(LEDS_DS, LEDS_SH, LEDS_ST);
    leds_write(LEDS_OFF);
    leds_enable(1u);
}
