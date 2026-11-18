/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — a simulated pedal for the web installer tests (tests/test_installer.mjs).
 *
 * It runs, natively on the PC, the very code the pedal runs for an update:
 *   app mode    firmware/bsp/chip/ota.c (handshake, ota_session: stage the package's loader at 0xE0000, record)
 *   loader mode loader/ldr_core.c (ldr_session: check the package, write the app area [0x4000, 0xA3000))
 * on a 1 MiB flash array. MIDI SysEx goes through stdin/stdout, one message per line in hex. Lines starting with
 * '#' are events for the harness: "#port NAME" (the USB device re-enumerates under that name), "#log ...".
 *   devsim FLASH.bin [--stock IDENTITY] [--dump OUT.bin]
 * FLASH.bin: the initial flash (1 MiB). --stock: answer with this identity in app mode instead of the app's own
 * OP_PRODUCT (the official firmware), its step 1 being simulated by ota.c. On exit (stdin closed) the flash is written
 * to --dump. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static uint8_t nor[0x100000];
static const char *cur_id = "?";
#define FELUCCA_ID_PTR cur_id
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))

/* ---- SysEx in/out ---- */
static uint8_t frame[2048];
static uint32_t frame_len, frame_full, upgrade_req, uboot_req;
static uint32_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}
static int hexv(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static int closed;
static void poll_in(uint32_t ms)                /* one line from the host, if any within ms */
{
    static char line[8192];
    static size_t ll;
    fd_set fs;
    struct timeval tv = {0, (long)ms * 1000};
    if (frame_full || closed)
        return;
    FD_ZERO(&fs);
    FD_SET(0, &fs);
    if (select(1, &fs, 0, 0, &tv) <= 0)
        return;
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) { closed = 1; return; }
        if (c == '\n') break;
        if (ll < sizeof line - 1) line[ll++] = c;
    }
    line[ll] = 0;
    {
        uint8_t m[4096];
        uint32_t n = 0, i;
        for (i = 0; i + 1 < ll; i += 2)
            m[n++] = (uint8_t)(hexv(line[i]) << 4 | hexv(line[i + 1]));
        ll = 0;
        if (n == 6 && !memcmp(m, "\xF0\x22\x24\x35\x7F\xF7", 6)) { upgrade_req = 1; return; }
        if (n == 6 && !memcmp(m, "\xF0\x22\x24\x35\x7D\xF7", 6)) { uboot_req = 1; return; }
        if (n >= 2 && m[0] == 0xF0 && m[n - 1] == 0xF7 && n - 2 <= sizeof frame) {
            memcpy(frame, m + 1, n - 2);
            frame_len = n - 2;
            frame_full = 1;
        }
    }
}
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
    printf("\n");
    fflush(stdout);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!frame_full)
        return 0;
    *p = frame;
    *n = frame_len;
    return 1;
}
static void ota_frame_done(void) { frame_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms(); }
static void ota_idle(void) { poll_in(1); }

/* ---- flash (step 1: the app may write the OTA area only) ---- */
static uint32_t bad_writes;
static int ota_erase(uint32_t off)
{
    if (!FL_IN(off, 0x1000u, 0xE0000u, 0xE5000u) || (off & 0xFFFu)) { bad_writes++; return -8; }
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t i;
    const uint8_t *s = p;
    if (!FL_IN(off, n, 0xE0000u, 0xE5000u)) { bad_writes++; return -8; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];         /* NOR: program clears bits only */
    return 0;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static uint32_t show_step;
static int32_t show_code;
static void ota_show(uint32_t step, int32_t code) { show_step = step; show_code = code; }
static uint8_t committed_parm[112];
static int committed;
static void ota_commit(const uint8_t *parm) { memcpy(committed_parm, parm, 112); committed = 1; }
#include "../firmware/bsp/chip/ota.c"

/* ---- loader (step 2): app area + update records only, never the head ---- */
static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_ok(uint32_t off, uint32_t n) { return FL_IN(off, n, 0x4000u, 0xA3000u) || FL_IN(off, n, 0xA3000u, 0xFF000u); }
static int ldr_erase(uint32_t off)
{
    if (!ldr_ok(off, 0x1000u) || (off & 0xFFFu)) { bad_writes++; return -9; }
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t i;
    const uint8_t *s = p;
    if (!ldr_ok(off, n)) { bad_writes++; return -10; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t record_cleared;
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; poll_in(0); }
#include "../loader/ldr_core.c"

/* ---- the app's identity: OP_PRODUCT inside the decrypted app.bin ---- */
static char app_id[32];
static const char *flash_app_id(void)
{
    static uint8_t area[0x9D740 + 0x200];
    uint32_t i, j;
    memcpy(area, nor + 0x4000, sizeof area);
    for (i = 0; i < sizeof area; i += 32) {            /* SFC: key 0x980F ^ (offset >> 2) per 32-byte block */
        uint32_t k = 0x980Fu ^ (i >> 2);
        for (j = 0; j < 32u && i + j < sizeof area; j++) {
            area[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
    for (i = 0; i + 43 < sizeof area; i++)
        if (!memcmp(area + i, "OP-PRODUCT:", 11)) {
            memcpy(app_id, area + i + 11, 20);
            app_id[20] = 0;
            return app_id;
        }
    return "NO-APP";
}

int main(int argc, char **argv)
{
    const char *stock = 0, *stock0 = 0, *dump = 0;
    FILE *f;
    int i, mode = 0;                                    /* 0 app, 1 loader */
    if (argc < 2) { fprintf(stderr, "devsim FLASH.bin [--stock ID] [--dump OUT]\n"); return 2; }
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--stock") && i + 1 < argc) stock = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
    }
    f = fopen(argv[1], "rb");
    if (!f || fread(nor, 1, sizeof nor, f) != sizeof nor) { fprintf(stderr, "devsim: %s: need 1 MiB\n", argv[1]); return 2; }
    fclose(f);
    stock0 = stock;                                     /* (a refused update: back to it) */
    cur_id = stock ? stock : flash_app_id();
    printf("#port %s\n#log app %s\n", stock ? "USB-Midi" : "OpenPocket", cur_id);
    fflush(stdout);
    while (!closed) {
        poll_in(20);
        if (uboot_req) { printf("#log uboot\n#port none\n"); fflush(stdout); uboot_req = 0; }
        ota_service();
        if (!upgrade_req || mode == 2) { upgrade_req = 0; continue; }
        upgrade_req = 0;
        if (mode == 0) {
            int rc = ota_session();
            printf("#log step1 rc=%d show=%u/%d committed=%d bad=%u\n", rc, show_step, show_code, committed, bad_writes);
            if (committed) {
                /* the SPL would load the staged loader: check what was staged, then run the loader code */
                uint8_t *h = nor + 0xE0000;
                uint32_t len = ota_rd32(h + 8) - 32u;
                printf("#log staged head=%s len=%u record=%s loader_at=%#x\n", (char *)h + 16, len,
                       ota_rd16(nor + 0xE4F00 + 6) == 0x5441u ? "flash" : "none", ota_rd32(committed_parm + 72));
                committed = 0;
                if (!memmem(nor + 0xE0000, 0x200, "FELUCCA-LOADER-1", 16)) {
                    printf("#log foreign-loader staged (the official update loader): not simulated\n#port none\n");
                    fflush(stdout);
                    mode = 2;
                    continue;
                }
                mode = 1;
                ota_deadline = 0;                       /* the loader is a fresh program: step 1's 2-minute limit is not its own */
                stock = 0;
                cur_id = "ota-OpenPocket_900";
                printf("#port OpenPocket Update\n");
            }
        } else {
            int rc = ldr_session();
            printf("#log step2 rc=%d bad=%u records_cleared=%u\n", rc, bad_writes, record_cleared);
            if (rc == 0) {
                memset(nor + 0xE0000, 0xFF, 0x5000);    /* (the new app's ota_boot_cleanup) */
                mode = 0;
                cur_id = flash_app_id();
                printf("#port OpenPocket\n#log app %s\n", cur_id);
            } else if (rc <= LDR_REFUSED) {             /* the device's head refused (1b): the records gone, it restarts */
                const int rec = ota_rd16(nor + 0xE4F00 + 6) == 0x5441u;   /* on its own firmware (ota.c's record) */
                mode = 0;
                stock = stock0;
                cur_id = stock ? stock : flash_app_id();
                printf("#port %s\n#log app %s refused record=%s\n", stock ? "USB-Midi" : "OpenPocket", cur_id, rec ? "flash" : "none");
            }
        }
        fflush(stdout);
    }
    if (dump) {
        f = fopen(dump, "wb");
        fwrite(nor, 1, sizeof nor, f);
        fclose(f);
    }
    return bad_writes ? 1 : 0;
}
