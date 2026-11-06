/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the light finder (firmware/led_probe.h, issue #8) on the PC, against fake GPIO registers:
 *   cc -O1 -Wall -Wno-unused-function -Ifirmware -o build/lpsim tests/lpsim.c && build/lpsim build/lp
 * Checks, on every pass of its loop: the pins it must never touch (screen, knob, I2S, amp enable, MASTER) keep
 * their registers; at most one pin differs from the state at start, the one on screen, and it blinks; every pin
 * goes back exactly as it was; the footswitches mark / unmark, the right footswitch is not read while it is the
 * pin being driven; a reset while a pin blinks skips that pin next time, a power-on starts afresh; the knob wraps
 * both ways; USB is serviced; every line of text fits the screen. Writes PPM screenshots to OUTDIR. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { RED = 0xF800, GRN = 0x07E0, BLU = 0x001F, WHT = 0xFFFF, BLK = 0x0000, CYN = 0x07FF, YEL = 0xFFE0, ORG = 0xFD20 };
enum { UI_BG = 0x0841, UI_PANEL = 0x18E3, UI_SEL = 0xFB00, UI_ACC = 0xFD20, UI_TXT = 0xEF7D, UI_DIM = 0x8410, UI_OK = 0x4E6B, UI_OFF = 0x2124 };

/* ---- screen: as tests/uisim.c */
static uint16_t fb[240 * 240];
static uint32_t wx, wy, ww, wi;
static void lcd_win(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)h; wx = x; wy = y; ww = w; wi = 0; }
static void lcd_px(uint16_t c)
{
    uint32_t x = wx + wi % ww, y = wy + wi / ww;
    if (x < 240u && y < 240u) fb[y * 240u + x] = c;
    wi++;
}
static void lcd_end(void) { }
static void lcd_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (w == 0u || h == 0u || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) fb[(y + j) * 240u + x + i] = c;
}
static int lcd_inits;
static void lcd_init(void) { lcd_inits++; }
static void fm1_wdt_feed(void) { }

/* ---- the pedal around the light finder: gpio[port p (0 A, 1 B, 2 C, 7 H)][register r / 4] */
#define LP_REG(port, r) (gpio[(port)][(r) / 4u])
#define LP_NOINIT
static uint32_t pa_in, pc_in;                   /* the buttons as pressed (pa_in) and the knob */
static uint32_t gpio[8][16];
static uint32_t sim_pa_in(void)                 /* as the pad reads: an output pin reads back its own level */
{
    uint32_t dir = gpio[0][0x08 / 4];
    return (pa_in & dir) | (gpio[0][0x00 / 4] & ~dir);
}
#define PA_IN sim_pa_in()
#define PC_IN pc_in
#define ENC_MASK 0x3u
#define PA_FS_UP (1u << 2)
#define PA_FS_DN (1u << 3)
static uint32_t base_pa;
static const int8_t QDEC[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
typedef struct { uint8_t stab, last, cnt; } dbn_t;
static int dbn_press(dbn_t *d, int raw)          /* as openpocket.c */
{
    if (raw == d->last) { if (d->cnt < 5u) d->cnt++; }
    else { d->cnt = 0; d->last = (uint8_t)raw; }
    if (d->cnt >= 3u && raw != d->stab) { d->stab = (uint8_t)raw; return raw; }
    return 0;
}
static uint32_t sim_ms;
static uint32_t ms_now(void) { return sim_ms; }
static unsigned long usb_calls;
static void px_usb_service(uint32_t now) { (void)now; usb_calls++; }
static struct { uint32_t magic, pending, failed; } guard;
enum { ST_OK = 8 };
static struct { uint32_t stage; } op_diag;
static struct { uint8_t rst; } op_pwr;
static void b_num(char *d, uint32_t v)
{
    char t[10]; int n = 0;
    while (*d) d++;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 9);
    while (n) *d++ = t[--n];
    *d = 0;
}
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

static int failed, clipped;
#define LP_TEXT_HOOK(s, f, w) do { \
        if (ft_width((f), (s)) > (w) || strlen(s) > 40u) { printf("text too wide: \"%s\"\n", (s)); clipped++; } \
    } while (0)
#include "ui_text.h"
#include "led_probe.h"

/* ---- the checks */
static const int PORTS[4] = { 0, 1, 2, 7 };
static uint32_t snap[8][16];
static void ok(int c, const char *what) { if (!c) { printf("FAIL: %s\n", what); failed++; } }
static void reset_gpio(void)                     /* a distinctive start: inputs with some pulls, some levels */
{
    memset(gpio, 0, sizeof gpio);
    for (int k = 0; k < 4; k++) {
        int p = PORTS[k];
        gpio[p][LP_OUT / 4] = 0x5A5Au ^ (uint32_t)(k * 0x1111);
        gpio[p][LP_DIR / 4] = 0xFFFFu & ~(0x0101u << k);   /* a few outputs too */
        gpio[p][LP_PD / 4] = 0x3030u >> k;                 /* every pin has a pull, so a driven one always differs */
        gpio[p][LP_PU / 4] = 0xFFFFu & ~gpio[p][LP_PD / 4];
    }
    gpio[1][LP_DIR / 4] &= ~0x00E2u;            /* the screen pins are outputs, as lcd_init leaves them */
    memcpy(snap, gpio, sizeof snap);
}
static int diff_pins(uint32_t *only)            /* how many pins differ from the start (and the last one's id) */
{
    int n = 0;
    for (int k = 0; k < 4; k++)
        for (int b = 0; b < 16; b++) {
            uint32_t m = 1u << b; int p = PORTS[k], d = 0;
            for (int r = 0; r < 6; r++) if ((gpio[p][r] ^ snap[p][r]) & m) d = 1;
            if (d) { n++; if (only) *only = (uint32_t)(k * 16 + b + 1); }
        }
    return n;
}
/* written out again here, not taken from led_probe.h: PA4..PA9 (I2S, amp enable), PA10 (MASTER), PB1/5/6/7 and
 * PC7 (screen), PC0/1 (knob) */
static const uint32_t FORBID[4] = { (0x7Fu << 4), (1u << 1) | (1u << 5) | (1u << 6) | (1u << 7), 3u | (1u << 7), 0u };
static int never_touched(void)
{
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < 6; r++)
            if ((gpio[PORTS[k]][r] ^ snap[PORTS[k]][r]) & FORBID[k]) return 0;
    return 1;
}
static void run(uint32_t ms)                    /* the loop for ms milliseconds, checking every pass */
{
    for (uint32_t t = 0; t < ms; t++) {
        uint32_t who = 0; int n;
        sim_ms++;
        lp_step(ms_now());
        n = diff_pins(&who);
        if (!never_touched()) { ok(0, "a pin it must never drive was changed"); return; }
        if (n > 1 || (n == 1 && who != lp.id)) { ok(0, "a pin other than the one on screen changed"); return; }
    }
}
static void turn(int dir)                       /* one detent: 0 1 3 2 0 is one step forward (the main loop's sign) */
{
    static const uint8_t FWD[4] = { 1, 3, 2, 0 }, BACK[4] = { 2, 3, 1, 0 };
    for (int k = 0; k < 4; k++) { pc_in = (pc_in & ~3u) | (dir > 0 ? FWD[k] : BACK[k]); run(2); }
}
static void press(uint32_t fs) { pa_in ^= fs; run(20); pa_in ^= fs; run(20); }
static void go_to(uint32_t id)                  /* knob forward until that pin is on screen */
{
    for (uint32_t k = 0; k <= lp.n + 1u && !(lp.i && lp.ids[lp.i - 1u] == id); k++) turn(+1);
    ok(lp.i && lp.ids[lp.i - 1u] == id, "the knob reaches the pin");
}
static uint32_t pin(int k, int b) { return (uint32_t)(k * 16 + b + 1); }
static void shot(const char *dir, const char *name)
{
    static int n;
    char path[512];
    snprintf(path, sizeof path, "%s/%02d_%s.ppm", dir, n++, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); failed++; return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31u) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u),
                           (uint8_t)((c & 31u) * 255u / 31u) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void boot(uint8_t rst)                   /* the pedal starts (registers as the hardware leaves them) */
{
    reset_gpio();
    op_pwr.rst = rst;
    guard.pending = 1; guard.failed = 1; op_diag.stage = 3;
    pc_in = 0; pa_in = 0; base_pa = 0;
    lp_init();
    run(5);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/lp";

    /* power-on, garbage in .noinit */
    lp_keep.magic = 0x12345678u; lp_keep.cur = 7; lp_keep.skip[0] = 0xFFFFu; lp_keep.mark[3] = 0xFFFFu;
    boot(1u);
    ok(lp.n == 50u, "50 pins to try");
    ok(lp_keep.magic == LP_MAGIC && !lp_keep.skip[0] && !lp_keep.mark[3], "a power-on starts afresh");
    ok(guard.pending == 0u && guard.failed == 0u && op_diag.stage == ST_OK, "not counted as a failed boot");
    ok(diff_pins(0) == 0, "nothing driven at start");
    ok(usb_calls > 0u, "USB serviced");
    shot(dir, "ready");

    /* the first pin: PA0 (FX button), an output, blinking at 2 Hz, its pulls off */
    turn(+1);
    ok(lp.i == 1u && lp.id == pin(0, 0), "one detent: the first pin");
    ok(!(gpio[0][LP_DIR / 4] & 1u) && !(gpio[0][LP_PU / 4] & 1u) && !(gpio[0][LP_PD / 4] & 1u), "PA0 driven, no pulls");
    {
        int flips = 0; uint32_t last = gpio[0][LP_OUT / 4] & 1u;
        for (int t = 0; t < 2000; t++) { run(1); if ((gpio[0][LP_OUT / 4] & 1u) != last) { flips++; last ^= 1u; } }
        ok(flips >= 7 && flips <= 9, "2 Hz: 8 changes in 2 s");
    }
    shot(dir, "pa0_fx_button");

    /* every pin forward, then back to READY: all as it was */
    for (uint32_t k = 1; k < lp.n; k++) {
        turn(+1);
        uint32_t id = lp.ids[lp.i - 1u], who = 0;
        run(300);                                        /* past one blink */
        ok(lp.id == id && diff_pins(&who) == 1 && who == id, "each pin in turn, alone");
    }
    turn(+1);
    ok(lp.i == 0u && lp.id == 0u, "past the last pin: READY");
    ok(memcmp(gpio, snap, sizeof snap) == 0, "every register as at start after the whole round");
    turn(-1);
    ok(lp.i == lp.n && lp.id == pin(3, 15), "back from READY: the last pin (PH15)");
    turn(+1);
    ok(lp.i == 0u && memcmp(gpio, snap, sizeof snap) == 0, "and forward again: READY, all as at start");

    /* marks: PH6 with the right footswitch; on PA2 (the right footswitch) only the left one marks */
    go_to(pin(3, 6));
    press(PA_FS_UP);
    ok(lp_has(lp_keep.mark, pin(3, 6)), "right footswitch marks PH6");
    shot(dir, "ph6_marked");
    go_to(pin(3, 9));
    press(PA_FS_UP);
    go_to(pin(0, 2));
    press(PA_FS_UP);
    ok(!lp_has(lp_keep.mark, pin(0, 2)), "the right footswitch is not read while PA2 blinks");
    press(PA_FS_DN);
    ok(lp_has(lp_keep.mark, pin(0, 2)), "the left one marks PA2");
    shot(dir, "pa2_right_footswitch");
    press(PA_FS_DN);
    ok(!lp_has(lp_keep.mark, pin(0, 2)), "and unmarks it");
    {   /* PA2 and PA3 blinking read back as presses: neither may mark or redraw */
        uint32_t m3 = lp_keep.mark[0];
        int i0 = lcd_inits;
        go_to(pin(0, 3));
        run(2000);
        go_to(pin(0, 2));
        run(2000);
        ok(lp_keep.mark[0] == m3 && lcd_inits == i0, "a blinking footswitch pin is not taken for a press");
    }
    go_to(pin(1, 0));
    int inits = lcd_inits;
    press(PA_FS_DN);
    ok(lcd_inits == inits + 1, "left footswitch redraws the screen");
    ok(!lp_has(lp_keep.mark, pin(1, 0)), "and does not mark");

    /* a reset while PB3 blinks: PB3 skipped next boot, the marks kept; the right footswitch tries it again */
    go_to(pin(1, 3));
    ok(lp_keep.cur == pin(1, 3), "the pin being driven is remembered");
    boot(0x04u);                                         /* watchdog reset */
    ok(lp_has(lp_keep.skip, pin(1, 3)), "after the reset PB3 is skipped");
    ok(lp_has(lp_keep.mark, pin(3, 6)) && lp_has(lp_keep.mark, pin(3, 9)), "marks kept across the reset");
    ok(lp.note_col == RED, "the reset is reported");
    shot(dir, "after_reset");
    go_to(pin(1, 3));
    run(600);
    ok(lp.id == 0u && diff_pins(0) == 0, "a skipped pin is not driven");
    shot(dir, "pb3_skipped");
    press(PA_FS_UP);
    ok(!lp_has(lp_keep.skip, pin(1, 3)) && lp.id == pin(1, 3), "right footswitch: try it again");

    /* switched off and on: afresh */
    boot(1u);
    ok(!lp_has(lp_keep.mark, pin(3, 6)) && !lp_has(lp_keep.skip, pin(1, 3)), "power-on clears marks and skips");

    ok(clipped == 0, "every line fits the screen");
    printf("light finder: %s (%d failures, %lu USB passes)\n", failed ? "FAIL" : "ok", failed, usb_calls);
    return failed ? 1 : 0;
}
