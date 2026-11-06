/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the LED hunt (firmware/led_probe.h and the 595 driver px_leds.h, issue #8) on the PC, against fake
 * GPIO registers (pads with pulls), a fake always-on power domain (P33) and a model of the 74HC595 wired as isod89
 * read it:
 *   cc -O1 -Wall -Wno-unused-function -Ifirmware -o build/lpsim tests/lpsim.c && build/lpsim build/lp
 * Checks, on every pass of its loop: the pins it must never drive (screen, knob, I2S, amp enable, MASTER, analog keys)
 * keep every register; besides PC5, PC6 and PC8 at most one pin differs from the start, the LED line on screen.
 * POWER AS FOUND reads CON5 and writes nothing; STOCK POWER writes CON5 = 0x74, once, and no other power register;
 * both blink all 8 bits. With isod89's roles every shift clock edge and latch has a wait before it, the latch is low
 * while shifting, and the bytes arrive MSB first: all 8 bits blink FF / 00 at 2 Hz; on an LED line the line (1 / 0)
 * and the 595 (FF / 00) go through all four mixes each second. All 47 lines in turn, each alone and given back
 * exactly; the knob wraps both ways; the analog keys mark and unmark, also at a level the supply moved; USB is
 * serviced; the boot is not counted as failed; every line of text fits. The driver alone too: leds_init from pins
 * with pulls leaves outputs without them and 0 shifted out, leds_write puts each byte on the outputs (MSB first).
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

/* ---- the pedal: gpio[port p (0 A, 1 B, 2 C, 7 H)][register r / 4]; a pad is its OUT bit when an output, else its
 * pull (no pull reads 0) */
enum { R_OUT = 0, R_IN = 1, R_DIR = 2, R_DIE = 3, R_PU = 4, R_PD = 5 };
static uint32_t gpio[8][16];
static uint32_t pc_in;
static uint32_t pc_pad(void)
{
    uint32_t d = gpio[2][R_DIR];
    return (gpio[2][R_OUT] & ~d) | (gpio[2][R_PU] & d);
}
static uint32_t sim_pc_in(void) { return pc_in & 3u; }   /* the knob */
#define LP_REG(port, r) (gpio[(port)][(r) / 4u])
#define PC_IN sim_pc_in()
#define ENC_MASK 0x3u
enum { KEY_NONE = 0, KEY_CAB, KEY_DLY, KEY_REV };
#define ADC_CH_KEYS 9u
static int32_t sim_adc = 1023;                  /* the keypad's reading: 1023 idle, CAB 617, DLY 706, REV 788 */
static unsigned long adc_calls;
static int32_t adc_read2(uint32_t ch) { if (ch == ADC_CH_KEYS) adc_calls++; return sim_adc; }
static int keys_decode(int32_t v)               /* as openpocket.c */
{
    static const int32_t REF[3] = { 617, 706, 788 };
    int best = -1; int32_t bd = 100000;
    if (v < 0) return KEY_NONE;
    for (int i = 0; i < 3; i++) { int32_t e = v - REF[i]; if (e < 0) e = -e; if (e < bd) { bd = e; best = i; } }
    return (bd <= 42) ? best + 1 : KEY_NONE;
}
/* the always-on power domain: CON5 as the boot leaves it; every write logged */
static uint8_t p33[256];
static int p33_writes, p33_last_a, p33_last_v;
static uint8_t fm1_p33_read(uint32_t a) { return p33[a & 255u]; }
static void fm1_p33_write(uint32_t a, uint8_t v) { p33[a & 255u] = v; p33_writes++; p33_last_a = (int)a; p33_last_v = v; }
static struct { uint8_t rst; } op_pwr;
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

/* ---- the 74HC595 as isod89 read the board (DS PC8, SHCP PC5, STCP PC6), from its data sheet: DS is shifted in on
 * SHCP's rising edge (the first bit in ends at Q7), the outputs take the shift register on STCP's rising edge. The
 * timing rules are counted only while the hunt drives it with those roles (lp.role 0). */
#define M_DS (1u << 8)
#define M_SH (1u << 5)
#define M_ST (1u << 6)
#define M_595 (M_DS | M_SH | M_ST)
static uint32_t lv_prev;
static uint8_t sr, q;
static unsigned long waits, w_sh, latches;
static int e_wait, e_latch, e_dir;
static void sim_wait(void) { waits++; }
static void sim_edge(void);
#define LEDS_PC(r) (gpio[2][(r) / 4u])
#define LEDS_WAIT() sim_wait()
#define LEDS_EDGE() sim_edge()

static int failed, clipped;
#define LP_TEXT_HOOK(s, f, w) do { \
        if (ft_width((f), (s)) > (w) || strlen(s) > 40u) { printf("text too wide: \"%s\"\n", (s)); clipped++; } \
    } while (0)
#include "ui_text.h"
#include "led_probe.h"

static void sim_edge(void)
{
    uint32_t lv = pc_pad(), rise = lv & ~lv_prev, fall = ~lv & lv_prev;
    int strict = lp.role == 0;
    if (rise & M_SH) {
        if (strict && waits == w_sh) e_wait++;  /* no wait while SHCP was low (or since the start) */
        if (strict && (lv & M_ST)) e_latch++;   /* shifting with the latch high: the outputs would ripple */
        if (strict && (gpio[2][R_DIR] & M_DS)) e_dir++;
        sr = (uint8_t)((sr << 1) | ((lv & M_DS) ? 1u : 0u));
        w_sh = waits;
    }
    if (fall & M_SH) { if (strict && waits == w_sh) e_wait++; w_sh = waits; }
    if (rise & M_ST) { if (strict && waits == w_sh) e_wait++; q = sr; latches++; }
    lv_prev = lv;
}

/* ---- the checks */
static const int PORTS[4] = { 0, 1, 2, 7 };
static uint32_t snap[8][16];
static void ok(int c, const char *what) { if (!c) { printf("FAIL: %s\n", what); failed++; } }
static void reset_gpio(void)                    /* a distinctive start; PC5, PC6, PC8 inputs with pull-ups, as booted */
{
    memset(gpio, 0, sizeof gpio);
    for (int k = 0; k < 4; k++) {
        int p = PORTS[k];
        gpio[p][R_OUT] = 0x5A5Au ^ (uint32_t)(k * 0x1111);
        gpio[p][R_DIR] = 0xFFFFu & ~(0x0101u << k);
        gpio[p][R_DIE] = 0xF0F0u >> k;
        gpio[p][R_PD] = 0x3030u >> k;           /* every pin has a pull, so a driven one always differs */
        gpio[p][R_PU] = 0xFFFFu & ~gpio[p][R_PD];
    }
    gpio[1][R_DIR] &= ~0x00E2u;                 /* the screen pins PB1/5/6/7 and PC7: outputs, PC7 high */
    gpio[2][R_DIR] &= ~(1u << 7); gpio[2][R_OUT] |= 1u << 7;
    gpio[2][R_DIR] |= M_595; gpio[2][R_PU] |= M_595; gpio[2][R_PD] &= ~M_595; gpio[2][R_DIE] &= ~M_595;
    memcpy(snap, gpio, sizeof snap);
    pc_in = 0; sim_adc = 1023;
    lv_prev = pc_pad();
    sr = 0x5Au; q = 0xA5u;                      /* a 595 holds garbage at power-on */
}
/* written out again here, not taken from led_probe.h: PA4..PA10, PB1/5/6/7, PC0/1/7 */
static const uint32_t FORBID[4] = { 0x7Fu << 4, (1u << 1) | (1u << 5) | (1u << 6) | (1u << 7), 3u | (1u << 7), 0u };
static int others_changed(uint32_t *who)        /* pins other than PC5/6/8 that differ from the start */
{
    int n = 0;
    for (int k = 0; k < 4; k++)
        for (int b = 0; b < 16; b++) {
            uint32_t m = 1u << b; int p = PORTS[k], d = 0;
            if (p == 2 && (m & M_595)) continue;
            for (int r = 0; r < 16; r++) if ((gpio[p][r] ^ snap[p][r]) & m) d = 1;
            if (d) { n++; if (who) *who = (uint32_t)(k * 16 + b + 1); }
        }
    return n;
}
static int forbidden_touched(void)
{
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < 16; r++)
            if ((gpio[PORTS[k]][r] ^ snap[PORTS[k]][r]) & FORBID[k]) return 1;
    return 0;
}
static int mix_seen[2][2];                      /* on an LED line: [line level][595 at FF] */
static void run(uint32_t ms)                    /* the loop for ms milliseconds, checking every pass */
{
    for (uint32_t t = 0; t < ms; t++) {
        uint32_t who = 0; int n;
        sim_ms++;
        lp_step(ms_now());
        n = others_changed(&who);
        if (forbidden_touched()) { ok(0, "a pin it must never drive was changed"); return; }
        if (n > 1 || (n == 1 && who != lp.id)) { ok(0, "a pin other than the LED line on screen changed"); return; }
        if (lp.id && (q == 0x00u || q == 0xFFu)) {
            uint32_t P = LP_PORT[(lp.id - 1u) >> 4], m = 1u << ((lp.id - 1u) & 15u);
            mix_seen[(gpio[P][R_OUT] & m) ? 1 : 0][q == 0xFFu]++;
        }
    }
}
static void turn(int dir)                       /* one detent: 0 1 3 2 0 is one step forward (the main loop's sign) */
{
    static const uint8_t FWD[4] = { 1, 3, 2, 0 }, BACK[4] = { 2, 3, 1, 0 };
    uint32_t n = lp_npos(), want = dir > 0 ? (lp.pos + 1u) % n : (lp.pos + n - 1u) % n;
    for (int k = 0; k < 4; k++) { pc_in = (pc_in & ~3u) | (dir > 0 ? FWD[k] : BACK[k]); run(2); }
    ok(lp.pos == want, "the knob moves one test per detent");
}
static void go(uint32_t pos) { for (uint32_t k = 0; k <= lp_npos() && lp.pos != pos; k++) turn(+1); ok(lp.pos == pos, "the knob reaches it"); }
static void press_key(int32_t v) { sim_adc = v; run(60); sim_adc = 1023; run(60); }
enum { CAB = 617, DLY = 706, REV = 788 };
static uint32_t pin(int k, int b) { return (uint32_t)(k * 16 + b + 1); }
static uint32_t pos_of(uint32_t id) { for (uint32_t i = 0; i < lp.n; i++) if (lp.ids[i] == id) return LP_LINES + i; return 0; }
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
static void boot(uint8_t con5, uint8_t rst)
{
    reset_gpio();
    memset(p33, 0, sizeof p33); p33[LP_CON5] = con5; p33_writes = 0;
    op_pwr.rst = rst;
    guard.pending = 1; guard.failed = 1; op_diag.stage = 3;
    lp_init();
    run(5);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/lp";

    /* the driver alone (px_leds.h): from inputs with pulls (PC8's a pull-down), then bytes MSB first */
    reset_gpio();
    gpio[2][R_PU] &= ~M_DS; gpio[2][R_PD] |= M_DS;
    memcpy(snap, gpio, sizeof snap);
    lv_prev = pc_pad();
    leds_init();
    ok(q == 0u && leds_byte == 0u && latches == 1u, "leds_init: 0 shifted out");
    ok(!(gpio[2][R_DIR] & M_595) && !(gpio[2][R_PU] & M_595) && !(gpio[2][R_PD] & M_595), "leds_init: outputs, no pulls");
    {
        static const uint8_t V[5] = { 0xA5u, 0x01u, 0x80u, 0x3Cu, 0x00u };
        int good = 1;
        for (int i = 0; i < 5; i++) { leds_write(V[i]); if (q != V[i] || leds_byte != V[i]) good = 0; }
        ok(good, "leds_write: each byte on the outputs, MSB first");
    }
    ok(others_changed(0) == 0, "the driver touches only PC5, PC6, PC8");

    /* after a power-on, the I/O supply at 3.0 V (VDDIOM 2) */
    boot(0x62u, 0x01u);
    ok(guard.pending == 0u && guard.failed == 0u && op_diag.stage == ST_OK, "not counted as a failed boot");
    ok(lp.n == 47u && lp_npos() == 55u, "47 LED lines, 55 tests");
    ok(lp.pos == 0u && lp.con5_found == 0x62u && p33_writes == 0, "POWER AS FOUND: CON5 read, nothing written");
    ok(!(gpio[2][R_DIR] & M_595) && !(gpio[2][R_PU] & M_595) && !(gpio[2][R_PD] & M_595), "the 595 pins outputs, no pulls");
    ok(lp.role == 0 && q == 0xFFu, "isod's roles, all 8 bits lit at once");
    {
        int flips = 0; uint8_t last = q;
        for (int t = 0; t < 2000; t++) { run(1); if (q != last) { flips++; last = q; } }
        ok(flips >= 7 && flips <= 9 && (q == 0u || q == 0xFFu), "FF / 00 at 2 Hz");
    }
    ok(usb_calls > 0u, "USB serviced");
    shot(dir, "power_found");

    /* STOCK POWER: CON5 0x74, once */
    turn(+1);
    ok(lp.pos == 1u && p33_writes == 1 && p33_last_a == LP_CON5 && p33_last_v == 0x74 && p33[LP_CON5] == 0x74u,
       "STOCK POWER: CON5 = 0x74, one write");
    ok(q == 0xFFu, "all 8 bits lit at once");
    run(1000);
    shot(dir, "power_stock");
    turn(-1); turn(+1);
    ok(p33_writes == 1, "set once only");

    /* ROLES 1: all 8 bits at 2 Hz */
    turn(+1);
    ok(lp.pos == 2u && lp.role == 0 && q == 0xFFu, "ROLES 1: isod's roles, lit at once");
    {
        int flips = 0; uint8_t last = q;
        for (int t = 0; t < 2000; t++) { run(1); if (q != last) { flips++; last = q; } if (q && q != 0xFFu) break; }
        ok(flips >= 7 && flips <= 9 && (q == 0u || q == 0xFFu), "FF / 00 at 2 Hz");
    }
    shot(dir, "roles1");
    for (int r = 2; r <= 6; r++) { turn(+1); ok(lp.role == r - 1, "the next roles"); run(600); }

    /* the LED lines: each alone, all four mixes, given back */
    for (uint32_t i = 0; i < lp.n; i++) {
        uint32_t who = 0;
        turn(+1);
        memset(mix_seen, 0, sizeof mix_seen);
        run(1100);
        ok(lp.role == 0 && lp.id == lp.ids[i] && others_changed(&who) == 1 && who == lp.id, "each line in turn, alone");
        ok(mix_seen[0][0] && mix_seen[0][1] && mix_seen[1][0] && mix_seen[1][1], "all four mixes within a second");
        if (lp.id == pin(3, 6)) shot(dir, "line_ph6");
    }
    turn(+1);
    ok(lp.pos == 0u && lp.id == 0u && others_changed(0) == 0, "past the last line: power, all given back");
    turn(-1);
    ok(lp.id == pin(3, 15), "back from power: the last line (PH15)");
    turn(+1);
    ok(p33_writes == 1, "still one power write after a whole round");

    /* marks: the analog keys, also at a level the supply moved */
    press_key(REV);
    ok(lp.mark_pos == 1u, "REV marks POWER AS FOUND");
    press_key(REV);
    ok(lp.mark_pos == 0u, "and unmarks it");
    turn(+1);
    press_key(500);
    ok(lp.mark_pos == 2u, "a key read at a moved level marks STOCK POWER");
    go(LP_ROLES + 2u);
    press_key(DLY);
    ok(lp.mark_pos == (2u | (1u << (LP_ROLES + 2u))), "DLY marks ROLES 3");
    go(pos_of(pin(3, 6)));
    press_key(CAB);
    ok(lp_has(lp.mark, pin(3, 6)), "CAB marks PH6");
    go(pos_of(pin(0, 1)));
    press_key(REV);
    ok(lp_has(lp.mark, pin(0, 1)), "REV marks PA1, the others kept");
    shot(dir, "marks");
    ok(adc_calls >= sim_ms / 10u - 2u && adc_calls <= sim_ms / 10u + 2u, "the keys every 10 ms");

    /* after a restart, with the stock level kept by the always-on domain */
    boot(0x74u, 0x40u);
    ok(lp.con5_found == 0x74u && p33_writes == 0, "a restart: the stock level found, nothing written");
    shot(dir, "power_kept");

    ok(e_wait == 0, "isod's roles: a wait before every shift clock edge and latch");
    ok(e_latch == 0, "isod's roles: the latch low while shifting");
    ok(e_dir == 0, "isod's roles: DS driven while shifting");
    ok(clipped == 0, "every line fits its box");
    printf("LED hunt: %s (%d failures, %lu latches, %lu USB passes)\n", failed ? "FAIL" : "ok", failed, latches, usb_calls);
    return failed ? 1 : 0;
}
