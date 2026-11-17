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
 *                                push mod amp cab dly rev, push_long (SAVE), dpush (double click: MOVE), fx_long (MIDI),
 *                                feet (both footswitches: tuner), home_long (hold HOME: tuner, on the tuner: diagnostics).
 *                                "#press ok" / "#press err REASON"
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
 *   in   "#cc N V"               (built with -DPX_OWN_FX=1, test build 39) a Control Change N = V from a controller, as the
 *                                footswitch's: through the controls learned on the pedal, then its map. "#cc ok"
 * Built with -DOP_BLE_TEST=1 it also answers DIAG, as the Bluetooth test image does, from the real text code (ble_diag.h):
 *   in   "#ble adv I T"          advertising; the counters i and t
 *   in   "#ble failed S C"       FAILED, step S, code C
 *   in   "#ble safe"             the boot after a watchdog reset in the first 6 s: safe mode, Bluetooth off
 * Built with -DED_BT=1 it is also the Bluetooth audio image's editor over BLE MIDI (test build 27, px_editor.h ED_BT): a line
 * "b" + hex is one BLE MIDI write to the MIDI characteristic, and the pedal's notifications come back one a line, "b" + hex, sent
 * as bt/bt_le_midi.c sends them (px_blem_pack, 20 bytes at most, two a pass of the main loop: a reply holds the send buffer
 * until it is out). With -DED_BT_LINKS=2u, as the image, a second LE link: its writes and notifications start with "c";
 *   in   "#bt"                   "#bt FRAMES SENT LOST DROP": frames answered over Bluetooth, notifications sent, replies
 *                                dropped (no link, too long, the buffer busy), SysEx dropped by the collectors
 *   in   "#bt drop [1]" / "#bt link [1]"   LE link 0 (or 1) goes (nothing more is sent to it) / a new one comes
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
#define OP_VERSION "v39"
static const char *FELUCCA_ID_PTR = "PocketX_939";

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

/* ---- flash (only the preset store, v37 the NAM model slots and v38 the IR slots may be written, as FL_RANGE_OK on the pedal) ---- */
#define ED_FL_OK(off, n) (FL_IN(off, n, 0xF0000u, 0xF4000u) || FL_IN(off, n, 0xC0000u, 0xE0000u) || \
                          FL_IN(off, n, 0xF4000u, 0xFC000u))
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
static void a_irq_gap_skip(void) { }                   /* (px_audio.h: the Bluetooth images time the audio interrupt) */
static void a_collect(void) { }
#define ST_IRQ_OFF()
static uint8_t px_flash_ok = 1;

/* ---- the pedal's UI state (openpocket.c, ui.h) ---- */
static px_preset_t PRESET;
static uint32_t cur_preset;
static void st_get(uint32_t k, px_preset_t *p);
#if PX_OWN_FX
static void load_preset(uint32_t i);                    /* (test build 49: and its scenes, once px_midi.h is in: below) */
#else
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#endif
static uint32_t master_vol = 80;
static int bat_mv = 3920, bat_usb_mv = 0;
static int32_t bat_raw[5] = {612, 1, 0, 3, 91};   /* bat_read's raw values (the tuner page shows them) */
static int bat_pct = -1;                /* (-1: the gauge from bat_mv) */
static int a_level_db(int ch) { return ch ? -84 : -62; }
static uint8_t a_pf[PX_NBLK + 1] = {0};   /* v38: CPU per block (the tuner's footer) */
static uint32_t a_wcut;                    /* v39: wet blocks cut short */
static volatile uint8_t a_tmute;
static float tuner_hz;
#include "../firmware/px_midi.h"          /* (ui.h's MIDI page, #10) */
#if PX_OWN_FX
static void load_preset(uint32_t i) { st_get(i, &PRESET); PX_SC_LOAD(i); }
#endif
#if PX_OWN_FX
static int a_in_gain_n;                                  /* test build 39: INPUT applied (px_audio.h's, counted here) */
static void a_in_gain_apply(void) { a_in_gain_n++; }
static int a_tails_n;                                    /* test build 40: TAILS to the engine (px_audio.h's, counted here) */
static void a_tails_apply(void) { a_tails_n++; }
static int a_inz_n;
static void a_inz_apply(void) { a_inz_n++; }      /* (test build 46: IN-Z to the input, counted) */
#endif
static void midi_persist(void) { }
static void learn_persist(void) { }                  /* (test build 46: ui.h's learn; the store is edsim's own) */
static void b_num(char *d, uint32_t v) { char t[12]; int n = 0; while (*d) d++; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = t[--n]; *d = 0; }
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
#if PX_OWN_FX && OP_UAUDIO
static volatile uint8_t a_ua;                       /* test build 43: USB audio running (px_audio.h's; SETTINGS' USB line) */
#endif
#include "../firmware/ui_text.h"
#include "../firmware/ui.h"
static uint32_t dsp_load = 42u;
static int dsp_bypass;
static uint32_t ed_load(void) { return dsp_load; }
static uint32_t ed_flags(void) { return (dsp_bypass ? 1u : 0u) | 2u; }
static uint32_t ed_stat(uint32_t k) { return k < 2u ? dsp_load : k < 4u ? 0u : 4096u; }
static uint32_t ed_ua_stat(uint32_t k) { (void)k; return 0u; }   /* v42: USB audio's figures: no computer records here */

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
#include "../firmware/px_irs.h"
static void ir_changed(uint32_t s) { (void)s; }
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
#if ED_BT
/* the LE links, as bt/bt_le_midi.c drives them (ED_BT_LINKS of them: 1, or 2 with -DED_BT_LINKS=2u as the image): the reply in
 * a buffer of its own, sent from there as notifications to the link that asked; link 0's lines start with "b", link 1's "c" */
static uint8_t le_tx[1024];
static uint32_t le_tx_at, le_tx_n, le_tx_link, le_tx_pkts, le_tx_lost, le_up[2] = {1, 1};
static int ed_bt_ready(uint32_t now) { (void)now; return le_tx_at >= le_tx_n; }
static void ed_bt_send(uint32_t link, const uint8_t *p, uint32_t n)
{
    if (link >= ED_BT_LINKS || !le_up[link] || n > sizeof le_tx || le_tx_at < le_tx_n) { le_tx_lost++; return; }
    memcpy(le_tx, p, n);
    le_tx_at = 0;
    le_tx_n = n;
    le_tx_link = link;
}
static void le_pump(void)                              /* two notifications a pass, as the pedal has two with the controller */
{
    uint8_t pkt[20];
    for (int i = 0; i < 2 && le_up[le_tx_link] && le_tx_at < le_tx_n; i++) {
        uint32_t k = px_blem_pack(le_tx, le_tx_n, &le_tx_at, ms_now(), pkt, sizeof pkt);
        putchar(le_tx_link ? 'c' : 'b');
        for (uint32_t j = 0; j < k; j++) printf("%02x", pkt[j]);
        putchar('\n');
        le_tx_pkts++;
    }
    fflush(stdout);
}
static void sim_bt(const char *l)                      /* "#bt drop [LINK]", "#bt link [LINK]", "#bt" */
{
    const uint32_t k = l[4] == ' ' && l[5] == '1' ? 1u : 0u;
    if (!strncmp(l, "drop", 4) || !strncmp(l, "link", 4)) {
        le_up[k] = l[0] == 'l';
        if (le_tx_link == k) le_tx_at = le_tx_n = 0;
        ed_bt_lost(k);
        return;
    }
    uint32_t drop = 0;
    for (uint32_t i = 0; i < ED_BT_LINKS; i++) drop += ed_bt_rx[i].drop;
    printf("#bt %u %u %u %u\n", ed_bt_frames, le_tx_pkts, le_tx_lost, drop);
    fflush(stdout);
}
#endif

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

/* ---- the controls: ui.h's own dispatch (ui_action, ui_turn, ui_buttons), as the pedal's main loop (openpocket.c) uses it ---- */
static const char *const BUTTON[] = {"fs_up", "fs_dn", "push", "home", "fx", "mod", "amp", "cab", "dly", "rev"};   /* (UB_ order) */
enum { N_BUTTON = sizeof BUTTON / sizeof *BUTTON };
static const struct { const char *name; int act; } GESTURE[] = {
    {"push_long", UA_SAVE}, {"dpush", UA_MOVE}, {"home_long", UA_TUNER}, {"fx_long", UA_MIDI}, {"feet", UA_FEET}};
static char last_press[32];
static uint32_t n_press;
static void noted(const char *n) { snprintf(last_press, sizeof last_press, "%s", n); n_press++; }
static const char *press(const char *n)
{
    int a = -1;
    uint32_t i;
    for (i = 0; i < N_BUTTON; i++) if (!strcmp(n, BUTTON[i])) a = (int)i;
    for (i = 0; i < sizeof GESTURE / sizeof *GESTURE; i++) if (!strcmp(n, GESTURE[i].name)) a = GESTURE[i].act;
    if (a < 0) return "unknown control";
    if (a == UA_MOVE && ui_mode != M_CHAIN) return "dpush: only on the chain page";
    ui_action(a);
    noted(n);
    return 0;
}
static int knob_pos;                                   /* detents turned since the start: where the knob points */
static void knob(int n, int fast)
{
    knob_pos += n;
    for (int i = 0; i < (n < 0 ? -n : n); i++) ui_turn(n < 0 ? -1 : 1, fast);
}

/* ---- a button held down, timed by ui_buttons as on the pedal: PUSH, HOME and FX act at the release or after a hold, the
 * others at the press; both footswitches within 150 ms open the tuner ---- */
static uint32_t held;
static void buttons_at(uint32_t now)                   /* ui_buttons, and what acted as last_press: a press, a release, a hold */
{
    static const char *const HOLD[UB_N] = {[UB_PUSH] = "push_long", [UB_HOME] = "home_long", [UB_FX] = "fx_long"};
    uint32_t was = ug.held;
    uint8_t fired[UB_N];
    memcpy(fired, ug.fired, sizeof fired);
    ui_buttons(now, held);
    for (int b = 0; b < UB_N; b++) {
        uint32_t m = 1u << b;
        if (HOLD[b] && !fired[b] && ug.fired[b]) noted(HOLD[b]);
        else if (!HOLD[b] && !(was & m) && (held & m)) noted(BUTTON[b]);
        else if (HOLD[b] && (was & m) && !(held & m) && !fired[b]) noted(BUTTON[b]);
    }
}
static int button(const char *n)
{
    for (int i = 0; i < N_BUTTON; i++) if (!strcmp(n, BUTTON[i])) return i;
    return -1;
}
static const char *button_set(const char *n, int down, uint32_t now)
{
    int b = button(n);
    if (b < 0) return "unknown button";
    held = down ? held | 1u << b : held & ~(1u << b);
    buttons_at(now);
    return 0;
}
static void buttons_service(uint32_t now) { buttons_at(now); }

/* ---- what the host asks for ---- */
static void frame_draw(void)                           /* what the main loop does after each pass */
{
    ui_render();
    char c[12];
    ui_dg.load = ui_dg.peak = dsp_load; ui_dg.cores = 2; ui_dg.split = 1; ui_dg.guard = 93; ui_dg.bypass = (uint8_t)dsp_bypass;
    ui_dg.cl[0] = dsp_load; ui_dg.cl[1] = dsp_load * 9u / 10u;
    ui_load_text(c, dsp_load, 0, dsp_bypass, 0);
    ui_status(c, ui_load_kind(dsp_load, 0, dsp_bypass, 0, 0));
    if (ui_mode == M_TUNER) tuner_draw(tuner_hz);
    if (ui_mode == M_DIAG) diag_draw();
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
    static const char *const MODE[] = {"chain", "block", "edit", "tuner", "save", "move", "midi", "list", "diag", "learn", "guide"};
    frame_draw();
    printf("#status {\"slot\":%u,\"name\":", cur_preset);
    json_str(PRESET.name);
    printf(",\"dirty\":%d,\"saved\":%d,\"mode\":\"%s\",\"block\":%u,\"item\":%u,\"volume\":%u,\"battery_mv\":%d,\"usb_mv\":%d,"
           "\"tuner_hz\":%.2f,\"writes_ok\":%d,\"frame\":%u,\"dsp\":{\"load\":%u,\"bypass\":%u,\"core1\":%u},"
           "\"last_press\":", ed_dirty(), st_saved(cur_preset), MODE[ui_mode], ui_blk, ui_item, master_vol, bat_mv, bat_usb_mv,
           (double)tuner_hz, writes_ok, fb_gen, ed_load(), ed_flags() & 1u, ed_flags() >> 1 & 1u);
    json_str(last_press);
    printf(",\"presses\":%u,\"knob\":%d,\"held\":[", n_press, knob_pos);
    for (int i = 0, n = 0; i < N_BUTTON; i++) if (held >> i & 1u) { printf(n++ ? "," : ""); json_str(BUTTON[i]); }
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
#if ED_BT
    else if (!strncmp(l, "#bt", 3)) sim_bt(l[3] == ' ' ? l + 4 : "");
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
        const char *err = button_set(name, 1, ms_now());
        printf(err ? "#down err %s\n" : "#down ok\n", err);
        fflush(stdout);
    } else if (sscanf(l, "#up %31s", name) == 1) {
        const char *err = button_set(name, 0, ms_now());
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
    }
#if PX_OWN_FX
    else if (sscanf(l, "#cc %d %d", &v, &w) == 2 && v >= 0 && v < 128 && w >= 0 && w < 128) {
        /* test build 39: a Control Change from a controller (the footswitch linked over Bluetooth, or one on USB), as
         * px_midi_io.h's px_midi_in takes it: counted (MIDI_IN to a watching editor), read through the controls learned on the
         * pedal, then the pedal's own map (118 / 119 step the preset; test build 49: 65 to 68 recall a scene; 64, 116, 117
         * are not simulated) */
        uint32_t c = (uint32_t)v, x = (uint32_t)w, mv = master_vol;
        int mset = 0;
        mid_last[0] = PXM_CC; mid_last[1] = (uint8_t)v; mid_last[2] = (uint8_t)w;
        mid_in_n++;
        if (px_learn_feed(c, x)) { }                   /* (test build 46: a learn on a block page takes it) */
        else if (px_midi_map_in(&c, &x, cur_preset)) {
            if (c == PXM_CC_NEXT || c == PXM_CC_PREV) { if (x >= 64u) ui_preset_step(c == PXM_CC_NEXT ? 1 : -1); }
            else if (c - PXM_CC_SCENE0 < PX_SC_N) ui_scene_recall(c - PXM_CC_SCENE0);
            else if (px_midi_cc_apply(&PRESET, c, x, &mv, &mset) == 0 && mset) master_vol = mv;
        }
        printf("#cc ok\n");
        fflush(stdout);
    }
#endif
    else if (!strcmp(l, "#status")) status();
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
#if ED_BT
    if (le_tx_at < le_tx_n) tv.tv_usec = 0;            /* (a reply is going out: the next pass at once) */
#endif
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
#if ED_BT
    if (line[0] == 'b' || line[0] == 'c') {           /* a BLE MIDI write, on link 0 ("b") or 1 ("c") */
        const uint32_t k = line[0] == 'c' ? 1u : 0u;
        uint32_t n = 0, i;
        uint8_t w[2048];
        for (i = 1; line[i] && line[i + 1] && n < sizeof w; i += 2) w[n++] = (uint8_t)(hexv(line[i]) << 4 | hexv(line[i + 1]));
        if (le_up[k]) ed_bt_write(k, w, n);
        return;
    }
#endif
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
    px_store_boot();                                    /* the slots, the last one saved, the settings (as the pedal boots) */
    nm_scan();
    ir_scan();
    uc.screen = -1;
    ui_invalidate();
    printf("#port OpenPocket\n");
    fflush(stdout);
    while (!closed) {
        poll_in(5);
        ed_service(ms_now());
#if ED_BT
        le_pump();
#endif
        buttons_service(ms_now());
        frame_draw();
        if (persist && flash_dirty) { flash_dirty = 0; if (write_flash(persist)) fprintf(stderr, "edsim: cannot write %s\n", persist); }
    }
    if (dump) { FILE *f = fopen(dump, "wb"); if (f) { fwrite(nor, 1, sizeof nor, f); fclose(f); } }
    return 0;
}
