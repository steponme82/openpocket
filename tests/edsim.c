/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — a simulated pedal for the web editor tests (tests/test_editor.mjs) and the hosted simulator
 * (tools/sim.mjs, docs/SIMULATOR.md).
 *
 * It runs, natively on the PC, the pedal's own editor code (firmware/px_editor.h), preset store (firmware/px_store.h),
 * screen (firmware/ui.h, drawn into the frame buffer of tests/simlcd.h) and engine catalogue (engine/px_dsp.c), on a
 * 1 MiB flash array. As tests/devsim.c: MIDI SysEx through stdin/stdout, one message per line in hex; '#' lines are
 * for the harness:
 *   out  "#port NAME"            the USB port appears
 *   in   "#pedal set B K V"      a knob turned on the pedal: parameter K of block B = V
 *   in   "#pedal on B V"         a block switched on/off on the pedal
 *   in   "#pedal preset N"       a footswitch: slot N (0..51)
 *   in   "#pedal focus B"        a block button
 *   in   "#dump"                 prints "#state SLOT|NAME|WRITES_OK|FOCUS|ON|TYPE|AMP P|ORDER" (the current sound)
 * and, for the hosted simulator, each answered by one line that starts with the same word:
 *   in   "#press NAME"           a control, as the pedal's main loop (openpocket.c) dispatches it: fs_up fs_dn fx home
 *                                push mod amp cab dly rev, push_long (SAVE), dpush (double click: MOVE), home_long
 *                                (tuner). "#press ok" / "#press err REASON"
 *   in   "#down NAME"            a button goes down / comes up, timed as the main loop times it: fs_up fs_dn fx home push
 *   in   "#up NAME"              mod amp cab dly rev. "#down ok" / "#up ok" / "... err REASON"
 *   in   "#knob N [fast]"        the PARA knob turned N detents (negative: the other way)
 *   in   "#vol N"                the MASTER knob, 0..100
 *   in   "#bat MV [USB_MV]"      the battery voltage (and the 5 V supply) the screen shows
 *   in   "#hz F"                 the pitch the tuner page hears (0: none)
 *   in   "#dsp load N"           the load the status text shows, measured by the page that runs the sound (tools/sim/)
 *   in   "#dsp bypass 0|1"       the bypass state: the status text shows "BYP" and the sound passes the dry input
 *   in   "#status"               one JSON line: slot, mode, focused block, volume, the whole current sound, ...
 *   in   "#screen"               "#screen " + 240*240 RGB565 pixels, 4 hex digits each, row by row
 *   in   "#flash PATH"           writes the 1 MiB flash to PATH
 * Built with -DOP_BLE_TEST=1 it also answers DIAG, as the Bluetooth test image does, from the real text code (ble_diag.h):
 *   in   "#ble adv I T"          advertising; the counters i and t
 *   in   "#ble failed S C"       FAILED, step S, code C
 *   in   "#ble safe"             the boot after a watchdog reset in the first 6 s: safe mode, Bluetooth off
 *   edsim [FLASH.bin] [--persist FILE] [--dump OUT.bin]
 * --persist FILE: the flash lives in FILE (read at the start when it exists, written again after each change);
 * --dump OUT.bin: the flash is written to OUT.bin when stdin closes.
 * Built with -DOP_BLE_TEST=1 it also answers DIAG, as the Bluetooth test image does, from the real text code (ble_diag.h):
 *   in   "#ble adv I T"          advertising; the counters i and t
 *   in   "#ble failed S C"       FAILED, step S, code C
 *   in   "#ble safe"             the boot after a watchdog reset in the first 6 s: safe mode, Bluetooth off */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include "../engine/px_dsp.h"
#include "../engine/px_nam.h"
#include "simlcd.h"

static uint8_t nor[0x100000];
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define OP_VERSION "v37"
static const char *FELUCCA_ID_PTR = "PocketX_937";

static uint32_t ms_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)   /* as ota.c: poly 0x1021, init 0 */
{
    uint32_t i;
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        for (i = 0; i < 8u; i++) c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return c;
}

/* ---- flash (only the preset store and, v37, the NAM model slots may be written, as FL_RANGE_OK on the pedal) ---- */
#define ED_FL_OK(off, n) (FL_IN(off, n, 0xF0000u, 0xF4000u) || FL_IN(off, n, 0xC0000u, 0xE0000u))
static int writes_ok = 1;
static int flash_dirty;
static int ota_fread(uint32_t off, void *p, uint32_t n) { if (off + n > sizeof nor) return -1; memcpy(p, nor + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    *took = 0;
    if (!ED_FL_OK(off, 0x1000u) || (off & 0xFFFu)) { writes_ok = 0; return -1; }
    memset(nor + off, 0xFF, 0x1000u);
    flash_dirty = 1;
    return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    if (!ED_FL_OK(off, n)) { writes_ok = 0; return -1; }
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= s[i];
    flash_dirty = 1;
    return 0;
}

/* ---- what px_store.h's audio hold touches on the pedal ---- */
static volatile uint8_t a_mute;
static void fm1_wdt_feed(void) { }
static void fm1_delay_ms(int ms) { (void)ms; }
#define FM1_IRQ_ALNK0 0
static void fm1_irq_mask(int i) { (void)i; }
static void fm1_irq_unmask(int i) { (void)i; }
static void a_collect(void) { }
#define ST_IRQ_OFF()
static uint8_t px_flash_ok = 1;

/* ---- the pedal's UI state (openpocket.c, ui.h) ---- */
static px_preset_t PRESET;
static uint32_t cur_preset;
static void st_get(uint32_t k, px_preset_t *p);
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
static uint32_t master_vol = 80;
static int bat_mv = 3920, bat_usb_mv = 0;
static int32_t bat_raw[5] = {612, 1, 0, 3, 91};   /* bat_read's raw values (the tuner page shows them) */
static int bat_pct = -1;                /* (-1: the gauge from bat_mv) */
static int a_level_db(int ch) { return ch ? -84 : -62; }
static volatile uint8_t a_tmute;
static float tuner_hz;
#include "../firmware/px_midi.h"          /* (ui.h's MIDI page, #10) */
static void midi_persist(void) { }
static void b_num(char *d, uint32_t v) { char t[12]; int n = 0; while (*d) d++; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = t[--n]; *d = 0; }
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
#include "../firmware/ui_text.h"
#include "../firmware/ui.h"
static uint32_t dsp_load = 42u;
static int dsp_bypass;
static uint32_t ed_load(void) { return dsp_load; }
static uint32_t ed_flags(void) { return (dsp_bypass ? 1u : 0u) | 2u; }

/* ---- SysEx in/out ---- */
static uint8_t frame[2048];
static uint32_t frame_len, frame_full;
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!frame_full) return 0; *p = frame; *n = frame_len; return 1; }
static void ota_frame_done(void) { frame_full = 0; }
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
    fflush(stdout);
    return 0;
}

#include "../firmware/px_midi.h"
#include "../firmware/px_store.h"
#include "../firmware/px_models.h"
static void nm_changed(uint32_t s) { (void)s; }
#if OP_BLE_TEST
#include "../firmware/ble/ble_diag.h"
#define ED_DIAG_LINES 4u
#define ED_DIAG_LEN 224u
static uint32_t sim_safe, sim_rst = 0x40u;
static void ed_diag_line(uint32_t i, char *out)
{
    if (i == 0u) ble_boot_text(out, 30000u, sim_safe, 8u, sim_rst, 0u);
    else if (i == 1u) ble_last_text(out, &ble_last);
    else if (i == 3u) { static const uint8_t last[3] = {2, 24, 127}, peer[6] = {1, 2, 3, 4, 5, 6}; ble_link_text(out, 1u, 2u, peer, 48u, 0u, 400u, 7u, 1u, 0x13u, last, 3u, "lmp_hci_reset"); }
    else if (ble_state == BLE_ADV) ble_adv_text(out, 7u, 0xA3u, 0x12ABu, 0x2001u, 0x101u, 0u, 0x80u, 3u, 3u, 1u);
    else ble_status_full(out);
}
static void sim_ble(const char *l)
{
    int a, b;
    if (sscanf(l, "adv %d %d", &a, &b) == 2) { ble_state = BLE_ADV; ble_cnt_irq_ev = (uint32_t)a; ble_cnt_timer = (uint32_t)b; }
    else if (sscanf(l, "failed %d %d", &a, &b) == 2) ble_fail((uint32_t)a, b);
    else if (!strcmp(l, "safe")) { ble_state = BLE_OFF; sim_safe = 1; sim_rst = 4u; ble_last = (ble_boot_t){ .failed = 1, .stage = 7, .blocks = 9 }; }
}
#endif
#include "../firmware/px_editor.h"

static int hexv(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static int closed;
static void pedal(const char *l)
{
    int b, k, v;
    if (sscanf(l, "set %d %d %d", &b, &k, &v) == 3 && b >= 0 && b < PX_NBLK && k >= 0 && k < PX_NPAR) PRESET.p[b][k] = (uint8_t)v;
    else if (sscanf(l, "on %d %d", &b, &v) == 2 && b >= 0 && b < PX_NBLK) PRESET.on[b] = (uint8_t)(v != 0);
    else if (sscanf(l, "preset %d", &v) == 1 && v >= 0 && v < (int)ST_NSLOT) { cur_preset = (uint32_t)v; load_preset(cur_preset); }
    else if (sscanf(l, "focus %d", &b) == 1 && b >= 0 && b < PX_NBLK) { ui_blk = (uint32_t)b; ui_mode = M_BLOCK; }
}

/* ---- the controls, as the pedal's main loop (openpocket.c) dispatches a debounced press ---- */
static char last_press[32];
static uint32_t n_press, n_pad;              /* n_pad: left footswitch presses taken by a PAD (no engine here) */
static const char *press(const char *n)
{
    static const struct { const char *name; uint32_t blk; } BLKBTN[] = {
        {"mod", PX_B_MOD}, {"amp", PX_B_AMP}, {"cab", PX_B_CAB}, {"dly", PX_B_DELAY}, {"rev", PX_B_REVERB}};
    int is_push = !strcmp(n, "push"), is_home = !strcmp(n, "home"), known = is_push || is_home;
    uint32_t i;
    for (i = 0; i < sizeof BLKBTN / sizeof *BLKBTN; i++) known |= !strcmp(n, BLKBTN[i].name);
    known |= !strcmp(n, "fs_up") || !strcmp(n, "fs_dn") || !strcmp(n, "fx") || !strcmp(n, "push_long") || !strcmp(n, "home_long") || !strcmp(n, "dpush");
    if (!known) return "unknown control";
    if (ui_mode == M_SAVE) {                           /* the save page: PUSH saves, HOME cancels, the others do nothing */
        if (is_push) save_confirm(); else if (is_home) save_close();
    } else if (ui_mode == M_TUNER) {                   /* any control leaves the tuner, but a hold of HOME does nothing */
        if (strcmp(n, "home_long")) tuner_close();
    } else if (!strcmp(n, "push_long")) {              /* hold PUSH (0.8 s) */
        save_open();
    } else if (ui_mode == M_MOVE) {                    /* moving a block: PUSH drops it, HOME puts it back */
        if (is_push) ui_move_drop(); else if (is_home) ui_move_cancel();
    } else if (!strcmp(n, "dpush")) {                  /* double-click PUSH on the chain */
        if (ui_mode != M_CHAIN) return "dpush: only on the chain page";
        ui_move_start();
    } else if (!strcmp(n, "home_long")) {              /* hold HOME (0.7 s) */
        tuner_open();
    } else if (is_push) ui_push();
    else if (!strcmp(n, "fs_up")) ui_preset_step(+1);
    else if (!strcmp(n, "fs_dn")) { if (ui_pad_on()) n_pad++; else ui_preset_step(-1); }   /* with a PAD on: the pad's */
    else if (is_home) ui_home();
    else if (!strcmp(n, "fx")) ui_fx_cycle();
    else for (i = 0; i < sizeof BLKBTN / sizeof *BLKBTN; i++) if (!strcmp(n, BLKBTN[i].name)) ui_enter_block(BLKBTN[i].blk);
    snprintf(last_press, sizeof last_press, "%s", n);
    n_press++;
    return 0;
}
static int knob_pos;                                   /* detents turned since the start: where the knob points */
static void knob(int n, int fast)
{
    int dd = n < 0 ? -1 : 1;
    knob_pos += n;
    for (int i = 0; i < (n < 0 ? -n : n); i++) {
        if (ui_mode == M_SAVE) save_knob(dd);
        else if (ui_mode != M_TUNER) ui_knob(dd, fast);
    }
}

/* ---- a button held down, timed as the main loop times it: PUSH and HOME act at the release or after a hold, the others
 * at the press. Two footswitches down are two presses: the firmware has no gesture for both. ---- */
static const char *const BUTTON[] = {"fs_up", "fs_dn", "fx", "home", "push", "mod", "amp", "cab", "dly", "rev"};
enum { N_BUTTON = sizeof BUTTON / sizeof *BUTTON, B_HOME = 3, B_PUSH = 4 };
static struct { int down, fired; uint32_t t; } btn[N_BUTTON];
static int clk_pend;                                   /* chain page: a PUSH waiting to see if a 2nd one makes it a double-click */
static uint32_t clk_t;
static int button(const char *n)
{
    for (int i = 0; i < N_BUTTON; i++) if (!strcmp(n, BUTTON[i])) return i;
    return -1;
}
static const char *button_down(const char *n, uint32_t now)
{
    int b = button(n);
    if (b < 0) return "unknown button";
    if (btn[b].down) return 0;
    btn[b].down = 1; btn[b].fired = 0; btn[b].t = now;
    return b == B_HOME || b == B_PUSH ? 0 : press(n);
}
static const char *button_up(const char *n, uint32_t now)
{
    int b = button(n);
    if (b < 0) return "unknown button";
    if (!btn[b].down) return 0;
    btn[b].down = 0;
    if (btn[b].fired) return 0;
    if (b == B_HOME) return press(n);
    if (b != B_PUSH) return 0;
    if (ui_mode != M_CHAIN) return press(n);
    if (clk_pend && now - clk_t < 350u) { clk_pend = 0; return press("dpush"); }
    clk_pend = 1; clk_t = now;
    return 0;
}
static void buttons_service(uint32_t now)
{
    if (btn[B_PUSH].down && !btn[B_PUSH].fired && now - btn[B_PUSH].t >= 800u) { btn[B_PUSH].fired = 1; press("push_long"); }
    if (btn[B_HOME].down && !btn[B_HOME].fired && now - btn[B_HOME].t >= 700u) { btn[B_HOME].fired = 1; press("home_long"); }
    if (ui_mode == M_MOVE) clk_pend = 0;
    else if (clk_pend && ui_mode != M_SAVE && ui_mode != M_TUNER && (now - clk_t >= 350u || ui_mode != M_CHAIN)) {
        clk_pend = 0;
        if (ui_mode == M_CHAIN) press("push");
    }
}

/* ---- what the host asks for ---- */
static void frame_draw(void)                           /* what the main loop does after each pass */
{
    ui_render();
    char c[12];
    snprintf(c, sizeof c, "%s %u%%", dsp_bypass ? "BYP" : "C1", dsp_load);
    ui_status(c, dsp_bypass);
    if (ui_mode == M_TUNER) tuner_draw(tuner_hz);
}
static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}
static void json_list(const uint8_t *a, int n)
{
    putchar('[');
    for (int i = 0; i < n; i++) printf("%s%d", i ? "," : "", a[i]);
    putchar(']');
}
static void status(void)
{
    static const char *const MODE[] = {"chain", "block", "edit", "tuner", "save", "move"};
    frame_draw();
    printf("#status {\"slot\":%u,\"name\":", cur_preset);
    json_str(PRESET.name);
    printf(",\"dirty\":%d,\"saved\":%d,\"mode\":\"%s\",\"block\":%u,\"item\":%u,\"volume\":%u,\"battery_mv\":%d,\"usb_mv\":%d,"
           "\"tuner_hz\":%.2f,\"writes_ok\":%d,\"frame\":%u,\"dsp\":{\"load\":%u,\"bypass\":%u,\"core1\":%u},"
           "\"last_press\":", ed_dirty(), st_saved(cur_preset), MODE[ui_mode], ui_blk, ui_item, master_vol, bat_mv, bat_usb_mv,
           (double)tuner_hz, writes_ok, fb_gen, ed_load(), ed_flags() & 1u, ed_flags() >> 1 & 1u);
    json_str(last_press);
    printf(",\"presses\":%u,\"knob\":%d,\"held\":[", n_press, knob_pos);
    for (int i = 0, n = 0; i < N_BUTTON; i++) if (btn[i].down) { printf(n++ ? "," : ""); json_str(BUTTON[i]); }
    printf("],\"preset\":{\"name\":");
    json_str(PRESET.name);
    printf(",\"on\":"); json_list(PRESET.on, PX_NBLK);
    printf(",\"type\":"); json_list(PRESET.type, PX_NBLK);
    printf(",\"order\":"); json_list(PRESET.order, PX_NBLK);
    printf(",\"p\":[");
    for (int b = 0; b < PX_NBLK; b++) { printf(b ? "," : ""); json_list(PRESET.p[b], PX_NPAR); }
    printf("]}}\n");
    fflush(stdout);
}
static void screen(void)
{
    frame_draw();
    printf("#screen ");
    for (int i = 0; i < 240 * 240; i++) printf("%04x", fb[i]);
    printf("\n");
    fflush(stdout);
}
static int write_flash(const char *path)                /* whole, or not at all: a session never reads half a flash */
{
    char tmp[1024];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fwrite(nor, 1, sizeof nor, f) != sizeof nor) { fclose(f); remove(tmp); return -1; }
    if (fclose(f)) { remove(tmp); return -1; }
    return rename(tmp, path);
}
static const char *persist;

static void command(const char *l)
{
    int v, w;
    char name[32];
    double d;
    if (!strncmp(l, "#pedal ", 7)) pedal(l + 7);
#if OP_BLE_TEST
    else if (!strncmp(l, "#ble ", 5)) sim_ble(l + 5);
#endif
    else if (!strcmp(l, "#dump")) {           /* "#state SLOT|NAME|WRITES_OK|FOCUS|ON..|TYPE..|P(AMP)..|ORDER.." */
        int i;
        printf("#state %u|%s|%d|%u|", cur_preset, PRESET.name, writes_ok, ui_blk);
        for (i = 0; i < PX_NBLK; i++) printf("%d%s", PRESET.on[i], i < PX_NBLK - 1 ? "," : "|");
        for (i = 0; i < PX_NBLK; i++) printf("%d%s", PRESET.type[i], i < PX_NBLK - 1 ? "," : "|");
        for (i = 0; i < PX_NPAR; i++) printf("%d%s", PRESET.p[PX_B_AMP][i], i < PX_NPAR - 1 ? "," : "|");
        for (i = 0; i < PX_NBLK; i++) printf("%d%s", PRESET.order[i], i < PX_NBLK - 1 ? "," : "\n");
        fflush(stdout);
    } else if (sscanf(l, "#press %31s", name) == 1) {
        const char *err = press(name);
        printf(err ? "#press err %s\n" : "#press ok\n", err);
        fflush(stdout);
    } else if (sscanf(l, "#down %31s", name) == 1) {
        const char *err = button_down(name, ms_now());
        printf(err ? "#down err %s\n" : "#down ok\n", err);
        fflush(stdout);
    } else if (sscanf(l, "#up %31s", name) == 1) {
        const char *err = button_up(name, ms_now());
        printf(err ? "#up err %s\n" : "#up ok\n", err);
        fflush(stdout);
    } else if ((w = sscanf(l, "#knob %d %31s", &v, name)) >= 1) {
        knob(v, w == 2 && !strcmp(name, "fast"));
        printf("#knob ok\n");
        fflush(stdout);
    } else if (sscanf(l, "#vol %d", &v) == 1) {
        master_vol = (uint32_t)(v < 0 ? 0 : v > 100 ? 100 : v);
        printf("#vol ok\n");
        fflush(stdout);
    } else if ((w = sscanf(l, "#bat %d %d", &v, &bat_usb_mv)) >= 1) {
        bat_mv = v;
        if (w == 1) bat_usb_mv = 0;
        printf("#bat ok\n");
        fflush(stdout);
    } else if (sscanf(l, "#hz %lf", &d) == 1) {
        tuner_hz = (float)d;
        printf("#hz ok\n");
        fflush(stdout);
    } else if (sscanf(l, "#dsp load %d", &v) == 1 && v >= 0 && v <= 999) {
        dsp_load = (uint32_t)v;
        printf("#dsp ok\n");
        fflush(stdout);
    } else if (sscanf(l, "#dsp bypass %d", &v) == 1) {
        dsp_bypass = v != 0;
        printf("#dsp ok\n");
        fflush(stdout);
    } else if (!strcmp(l, "#status")) status();
    else if (!strcmp(l, "#screen")) screen();
    else if (!strncmp(l, "#flash ", 7)) {
        printf(write_flash(l + 7) ? "#flash err\n" : "#flash ok\n");
        fflush(stdout);
    }
}
static void poll_in(uint32_t ms)
{
    static char line[8192];
    static size_t ll;
    fd_set fs;
    struct timeval tv = {0, (long)ms * 1000};
    if (frame_full || closed) return;
    FD_ZERO(&fs);
    FD_SET(0, &fs);
    if (select(1, &fs, 0, 0, &tv) <= 0) return;
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) { closed = 1; return; }
        if (c == '\n') break;
        if (ll < sizeof line - 1) line[ll++] = c;
    }
    line[ll] = 0;
    ll = 0;
    if (line[0] == '#') { command(line); return; }
    {
        uint32_t n = 0, i;
        uint8_t m[4096];
        for (i = 0; line[i] && line[i + 1] && n < sizeof m; i += 2) m[n++] = (uint8_t)(hexv(line[i]) << 4 | hexv(line[i + 1]));
        if (n >= 2 && m[0] == 0xF0 && m[n - 1] == 0xF7 && n - 2 <= sizeof frame) {   /* usb.c: the bytes between F0 and F7 */
            memcpy(frame, m + 1, n - 2);
            frame_len = n - 2;
            frame_full = 1;
        }
    }
}

int main(int argc, char **argv)
{
    const char *dump = 0;
    memset(nor, 0xFF, sizeof nor);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--persist") && i + 1 < argc) persist = argv[++i];
        else { FILE *f = fopen(argv[i], "rb"); if (f) { if (fread(nor, 1, sizeof nor, f)) { } fclose(f); } }
    }
    if (persist) {                                      /* a session's flash: left as it was, or blank the first time */
        FILE *f = fopen(persist, "rb");
        if (f) {
            if (fread(nor, 1, sizeof nor, f) != sizeof nor) { fprintf(stderr, "edsim: %s: need 1 MiB\n", persist); return 2; }
            fclose(f);
        }
    }
    st_ok = 1;
    st_load();
    nm_scan();
    cur_preset = st_last;
    load_preset(cur_preset);
    uc.screen = -1;
    ui_invalidate();
    printf("#port OpenPocket\n");
    fflush(stdout);
    while (!closed) {
        poll_in(5);
        ed_service(ms_now());
        buttons_service(ms_now());
        frame_draw();
        if (persist && flash_dirty) { flash_dirty = 0; if (write_flash(persist)) fprintf(stderr, "edsim: cannot write %s\n", persist); }
    }
    if (dump) { FILE *f = fopen(dump, "wb"); if (f) { fwrite(nor, 1, sizeof nor, f); fclose(f); } }
    return 0;
}
