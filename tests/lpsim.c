/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the LED map (firmware/led_probe.h) and the lights' driver (px_leds.h, issue #8) on the PC, against
 * fake GPIO registers and a model of the 74HC595 with its output enable on PC7:
 *   cc -O1 -Wall -Wno-unused-function -Ifirmware -o build/lpsim tests/lpsim.c && build/lpsim build/lp
 * Checks: the driver alone (leds_init from pins with pulls, PC7 an input: PC5/6/8 outputs without pulls, the all-off byte FF
 * latched while PC7 is high, then PC7 low; leds_write puts each byte on the outputs, MSB first). The LED map: only
 * PC5, PC6, PC8 and PC7 are ever changed (every other register bit of PA, PB, PC and PH as at start); PC7 ends an
 * output, low; every shift clock edge and the latch's rise have a wait before them, DS is an output and the latch is
 * low while shifting; the byte on the outputs is always the one the screen names, blinking at 2 Hz between the bits
 * on screen and 0, lit right after a turn; the knob wraps both ways over the 9 positions; each button writes itself
 * into the table, again clears it, another replaces it, nothing is written on ALL 8 BITS; the analog keys (CAB, DLY,
 * REV) are read every 10 ms; USB is serviced; the boot is not counted as failed; every line of text fits its box.
 * Writes PPM screenshots to OUTDIR. */
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
static void fm1_wdt_feed(void) { }

/* ---- the pedal around the LED map: gpio[port p (0 A, 1 B, 2 C, 7 H)][register r / 4] */
enum { R_OUT = 0, R_DIR = 2, R_PU = 4, R_PD = 5 };              /* register index: OUT +0, DIR +8, PU +0x10, PD +0x14 */
static uint32_t gpio[8][16];
static uint32_t pa_in, ph_in, pc_in;            /* the buttons as pressed, the knob */
static uint32_t sim_pa_in(void)                 /* as the pad reads: an output pin reads back its own level */
{
    uint32_t dir = gpio[0][R_DIR];
    return (pa_in & dir) | (gpio[0][R_OUT] & ~dir);
}
#define PA_IN sim_pa_in()
#define PH_IN ph_in
#define PC_IN pc_in
#define ENC_MASK 0x3u
#define PA_FX     (1u << 0)                     /* as openpocket.c */
#define PA_HOME   (1u << 1)
#define PA_FS_UP  (1u << 2)
#define PA_FS_DN  (1u << 3)
#define PH_PUSH   ((1u << 4) | (1u << 6))
#define PH_MOD    ((1u << 7) | (1u << 8))
#define PH_AMP    (1u << 9)
static uint32_t base_pa, base_ph;
enum { KEY_NONE = 0, KEY_CAB, KEY_DLY, KEY_REV };
#define ADC_CH_KEYS 9u
static int sim_key;                             /* the analog key held down */
static unsigned long adc_calls;
static int32_t adc_read2(uint32_t ch) { if (ch == ADC_CH_KEYS) adc_calls++; return sim_key; }
static int keys_decode(int32_t v) { return (int)v; }
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
static void b_num(char *d, uint32_t v)
{
    char t[10]; int n = 0;
    while (*d) d++;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 9);
    while (n) *d++ = t[--n];
    *d = 0;
}
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

/* ---- the 74HC595, from its data sheet: DS is shifted in on SHCP's rising edge (the first bit in ends at Q7), the
 * outputs take the shift register on STCP's rising edge. A pin's level: its OUT bit when an output, else its pull. */
#define M_DS (1u << 8)
#define M_SH (1u << 5)
#define M_ST (1u << 6)
#define M_EN (1u << 7)
static uint32_t lv_prev;
static uint8_t sr, q;
static unsigned long waits, w_sh, latches;
static int e_wait, e_latch, e_dir;
static uint32_t pc_level(void) { uint32_t d = gpio[2][R_DIR]; return (gpio[2][R_OUT] & ~d) | (gpio[2][R_PU] & d); }
static void sim_wait(void) { waits++; }
static int en_falls, en_rises;                  /* PC7, the output enable (low = on): its edges */
static uint8_t q_at_enable = 0x5Au;             /* the outputs' byte when PC7 went low */
static unsigned long latches_at_enable;
static void sim_edge(void)
{
    uint32_t lv = pc_level(), rise = lv & ~lv_prev, fall = ~lv & lv_prev;
    if (fall & M_EN) { en_falls++; q_at_enable = q; latches_at_enable = latches; }
    if (rise & M_EN) en_rises++;
    if (rise & M_SH) {
        if (waits == w_sh) e_wait++;            /* no wait while SHCP was low (or since the start) */
        if (lv & M_ST) e_latch++;               /* shifting with the latch high: the outputs would ripple */
        if (gpio[2][R_DIR] & M_DS) e_dir++;     /* DS not driven */
        sr = (uint8_t)((sr << 1) | ((lv & M_DS) ? 1u : 0u));
        w_sh = waits;
    }
    if (fall & M_SH) { if (waits == w_sh) e_wait++; w_sh = waits; }   /* no wait while SHCP was high */
    if (rise & M_ST) { if (waits == w_sh) e_wait++; q = sr; latches++; }   /* a wait after the last shift too */
    lv_prev = lv;
}
#define LEDS_PC(r) (gpio[2][(r) / 4u])
#define LEDS_WAIT() sim_wait()
#define LEDS_EDGE() sim_edge()

static int failed, clipped;
#define LP_TEXT_HOOK(s, f, w) do { \
        if (ft_width((f), (s)) > (w) || strlen(s) > 40u) { printf("text too wide: \"%s\"\n", (s)); clipped++; } \
    } while (0)
#include "ui_text.h"
#include "led_probe.h"

/* ---- the checks */
static const int PORTS[4] = { 0, 1, 2, 7 };
static uint32_t snap[8][16];
static int want, want2 = -1;                    /* the position the test expects (0 all, 1..8 bit want - 1); mid-turn either */
static void ok(int c, const char *what) { if (!c) { printf("FAIL: %s\n", what); failed++; } }
static void reset_gpio(void)                    /* a distinctive start; PC5, PC6, PC8 inputs with pull-ups, as booted */
{
    memset(gpio, 0, sizeof gpio);
    for (int k = 0; k < 4; k++) {
        int p = PORTS[k];
        gpio[p][R_OUT] = 0x5A5Au ^ (uint32_t)(k * 0x1111);
        gpio[p][R_DIR] = 0xFFFFu & ~(0x0101u << k);
        gpio[p][R_PD] = 0x3030u >> k;
        gpio[p][R_PU] = 0xFFFFu & ~gpio[p][R_PD];
    }
    gpio[1][R_DIR] &= ~0x00E2u;                 /* the screen pins PB1/5/6/7 and PC7: outputs, PC7 high */
    gpio[2][R_DIR] &= ~(1u << 7); gpio[2][R_OUT] |= 1u << 7;
    gpio[2][R_DIR] |= M_DS | M_SH | M_ST; gpio[2][R_PU] |= M_DS | M_SH | M_ST; gpio[2][R_PD] &= ~(M_DS | M_SH | M_ST);
    gpio[0][R_DIR] |= 0x000Fu;                  /* the buttons are inputs, as input_init leaves them */
    gpio[7][R_DIR] |= PH_PUSH | PH_MOD | PH_AMP;
    memcpy(snap, gpio, sizeof snap);
    pa_in = ph_in = pc_in = 0; sim_key = KEY_NONE;
    base_pa = sim_pa_in() & 0xFFFFu; base_ph = ph_in;   /* as input_baseline */
    lv_prev = pc_level();
    sr = 0x5Au; q = 0xA5u;                      /* a 595 holds garbage at power-on */
}
static int only_595_pins(void)                  /* every register bit as at start, but PC5/6/8's OUT, DIR, PU, PD, PC7's OUT, DIR */
{
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < 16; r++) {
            uint32_t may = (PORTS[k] == 2 && (r == R_OUT || r == R_DIR || r == R_PU || r == R_PD)) ? (M_DS | M_SH | M_ST) : 0u;
            if (PORTS[k] == 2 && (r == R_OUT || r == R_DIR)) may |= M_EN;   /* PC7: the lights' enable */
            if ((gpio[PORTS[k]][r] ^ snap[PORTS[k]][r]) & ~may) return 0;
        }
    return 1;
}
static uint8_t mask_of(int pos) { return pos ? (uint8_t)(1u << (pos - 1)) : 0xFFu; }
static void run(uint32_t ms)                    /* the loop for ms milliseconds, checking every pass */
{
    for (uint32_t t = 0; t < ms; t++) {
        sim_ms++;
        lp_step(ms_now());
        if (!only_595_pins()) { ok(0, "a pin other than PC5 / PC6 / PC8 was changed"); return; }
        if (q != 0u && q != mask_of(want) && !(want2 >= 0 && q == mask_of(want2))) {
            ok(0, "the 595 shows a byte other than the bits on screen"); return;
        }
    }
}
static void turn(int dir)                       /* one detent: 0 1 3 2 0 is one step forward (the main loop's sign) */
{
    static const uint8_t FWD[4] = { 1, 3, 2, 0 }, BACK[4] = { 2, 3, 1, 0 };
    want2 = dir > 0 ? (want + 1) % 9 : (want + 8) % 9;
    for (int k = 0; k < 4; k++) { pc_in = (pc_in & ~3u) | (dir > 0 ? FWD[k] : BACK[k]); run(2); }
    want = want2; want2 = -1;
    ok(lp.pos == (uint32_t)want, "the knob moves one position per detent");
    ok(q == mask_of(want), "lit right after a turn");
}
static void go(int pos) { for (int k = 0; k < 9 && want != pos; k++) turn(+1); }
static void press_pa(uint32_t m) { pa_in ^= m; run(20); pa_in ^= m; run(20); }
static void press_ph(uint32_t m) { ph_in ^= m; run(20); ph_in ^= m; run(20); }
static void press_key(int k) { sim_key = k; run(60); sim_key = KEY_NONE; run(60); }
static int map(int bit) { return lp.map[bit] ? lp.map[bit] - 1 : -1; }
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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/lp";

    /* the driver alone (px_leds.h): from inputs with pulls (PC8's a pull-down), then bytes MSB first */
    reset_gpio();
    gpio[2][R_PU] &= ~M_DS; gpio[2][R_PD] |= M_DS;
    gpio[2][R_DIR] |= M_EN; gpio[2][R_PU] |= M_EN;   /* PC7 untouched here: an input with its pull-up */
    memcpy(snap, gpio, sizeof snap);
    lv_prev = pc_level();
    leds_init();
    ok(q == 0xFFu && leds_byte == 0xFFu && latches == 1u, "leds_init: the all-off byte FF shifted out");
    ok(!(gpio[2][R_DIR] & (M_DS | M_SH | M_ST)) && !(gpio[2][R_PU] & (M_DS | M_SH | M_ST)) &&
       !(gpio[2][R_PD] & (M_DS | M_SH | M_ST)), "leds_init: PC5, PC6, PC8 outputs, no pulls");
    ok(en_falls == 1 && q_at_enable == 0xFFu && latches_at_enable == 1u && !(gpio[2][R_OUT] & M_EN) &&
       !(gpio[2][R_DIR] & M_EN), "leds_init: PC7 an output, low, once the byte is latched");
    {
        static const uint8_t V[5] = { 0xA5u, 0x01u, 0x80u, 0x3Cu, 0x00u };
        int good = 1;
        for (int i = 0; i < 5; i++) { leds_write(V[i]); if (q != V[i] || leds_byte != V[i]) good = 0; }
        ok(good, "leds_write: each byte on the outputs, MSB first");
    }
    ok(only_595_pins(), "the driver touches only PC5, PC6, PC8 and PC7");
    latches = 0; en_falls = en_rises = 0; latches_at_enable = 0; q_at_enable = 0x5Au;

    /* the LED map */
    reset_gpio();
    guard.pending = 1; guard.failed = 1; op_diag.stage = 3;
    lp_init();
    ok(guard.pending == 0u && guard.failed == 0u && op_diag.stage == ST_OK, "not counted as a failed boot");
    ok(!(gpio[2][R_DIR] & (M_DS | M_SH | M_ST)), "PC5, PC6, PC8 are outputs");
    ok(!(gpio[2][R_PU] & (M_DS | M_SH | M_ST)) && !(gpio[2][R_PD] & (M_DS | M_SH | M_ST)), "without pulls");
    ok(latches == 2u && q == 0xFFu, "the all-off byte FF at start (as stock), then ALL 8 BITS lit");
    ok(en_falls == 1 && en_rises == 0 && q_at_enable == 0xFFu && latches_at_enable == 1u,
       "the lights on (PC7 low) once, after the all-off byte is latched");
    ok(!(gpio[2][R_DIR] & M_EN) && !(gpio[2][R_OUT] & M_EN), "PC7 an output, low");
    ok(only_595_pins(), "nothing else touched by the start");
    run(5);
    ok(usb_calls > 0u, "USB serviced");
    shot(dir, "all_bits");

    {   /* 2 Hz: 8 changes in 2 s, always the bits on screen or 0 */
        int flips = 0; uint8_t last = q;
        for (int t = 0; t < 2000; t++) { run(1); if (q != last) { flips++; last = q; } }
        ok(flips >= 7 && flips <= 9, "2 Hz: 8 changes in 2 s");
    }
    {   /* each bit alone, MSB first (an LSB-first driver would light bit 7 - n) */
        for (int b = 0; b < 8; b++) { turn(+1); run(300); }
        ok(want == 8, "at BIT 7");
        turn(+1);
        ok(lp.pos == 0u, "past BIT 7: ALL 8 BITS");
        turn(-1);
        ok(lp.pos == 8u && q == 0x80u, "back from ALL: BIT 7");
        turn(+1);
    }
    {   /* the table: every button, again clears, another replaces, none on ALL */
        unsigned long a0 = adc_calls; uint32_t t0 = sim_ms;
        press_key(KEY_DLY);
        ok(memcmp(lp.map, "\0\0\0\0\0\0\0\0", 8) == 0, "nothing written on ALL 8 BITS");
        unsigned long n = adc_calls - a0, t = (sim_ms - t0) / 10u;
        ok(n + 1u >= t && n <= t + 1u, "the keys every 10 ms");
        go(1);
        shot(dir, "bit0");
        press_pa(PA_FX);
        ok(map(0) == LB_FX, "FX on BIT 0");
        shot(dir, "bit0_fx");
        press_pa(PA_FX);
        ok(map(0) < 0, "FX again clears it");
        press_pa(PA_FX);
        go(2); press_pa(PA_FS_UP);
        ok(map(1) == LB_FSR, "right footswitch on BIT 1");
        go(3); press_pa(PA_FS_DN);
        ok(map(2) == LB_FSL, "left footswitch on BIT 2");
        go(4); press_ph(PH_AMP);
        ok(map(3) == LB_AMP, "AMP on BIT 3");
        go(5); press_key(KEY_CAB);
        ok(map(4) == LB_CAB, "CAB (analog) on BIT 4");
        go(6); press_ph(1u << 6);
        ok(map(5) == LB_PUSH, "PUSH (PH6 alone) on BIT 5");
        go(7); press_key(KEY_REV); press_pa(PA_HOME);
        ok(map(6) == LB_HOME, "HOME replaces REV on BIT 6");
        go(8); press_ph(1u << 8);
        ok(map(7) == LB_MOD, "MOD (PH8 alone) on BIT 7");
        ok(map(0) == LB_FX && map(3) == LB_AMP, "the table keeps the others");
        shot(dir, "map");
        go(0);
        press_pa(PA_FX);
        ok(map(0) == LB_FX, "a press on ALL changes nothing");
    }
    ok(e_wait == 0, "a wait on each side of every shift clock edge");
    ok(e_latch == 0, "the latch is low while shifting");
    ok(e_dir == 0, "DS is driven while shifting");
    ok(clipped == 0, "every line fits its box");
    printf("LED map: %s (%d failures, %lu latches, %lu USB passes)\n", failed ? "FAIL" : "ok", failed, latches, usb_calls);
    return failed ? 1 : 0;
}
