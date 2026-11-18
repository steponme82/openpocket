/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — host test of the preset store (firmware/px_store.h) on a simulated NOR flash:
 * save / reload, alternate copies, a power cut at every step of a save, the bytes the update loader reads
 * (0xF00.. of each sector) kept erased, writes only inside the store.
 *   cc -O1 -Iengine tests/test_store.c engine/px_dsp.c -lm -o build/test_store && build/test_store */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "../firmware/px_midi.h"
#if PX_OWN_FX
static int a_in_gain_n;                                  /* test build 39: INPUT applied (px_audio.h's, counted here) */
static void a_in_gain_apply(void) { a_in_gain_n++; }
static int a_tails_n;                                    /* test build 40: TAILS to the engine (px_audio.h's, counted here) */
static void a_tails_apply(void) { a_tails_n++; }
static int a_inz_n;
static void a_inz_apply(void) { a_inz_n++; }      /* (test build 46: IN-Z to the input, counted) */
#endif

static uint8_t nor[0x100000];
static int fails, ops, cut_at = -1, dead;          /* power cut: after cut_at flash operations nothing more happens */
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)
static int in_store(uint32_t off, uint32_t n) { return off >= 0xF0000u && off + n <= 0xF4000u; }

/* ---- the firmware pieces px_store.h uses ---- */
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)
{
    while (n--) { c ^= (uint32_t)*p++ << 8; for (int i = 0; i < 8; i++) c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1; c &= 0xFFFFu; }
    return c;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *t)
{
    (void)t;
    if (!in_store(off, 0x1000) || (off & 0xFFF)) { printf("FAIL erase outside the store %x\n", off); fails++; return -1; }
    if (dead || (cut_at >= 0 && ops++ >= cut_at)) { dead = 1; return 0; }
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    if (!in_store(off, n)) { printf("FAIL write outside the store %x\n", off); fails++; return -1; }
    for (uint32_t i = 0; i < n; i++) {                 /* a cut can land in the middle of a write */
        if (dead || (cut_at >= 0 && ops++ >= cut_at)) { dead = 1; return 0; }
        nor[off + i] &= s[i];
    }
    return 0;
}
static volatile uint8_t a_mute;
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(int ms) { (void)ms; }
#define FM1_IRQ_ALNK0 11
static void fm1_irq_mask(int n) { (void)n; }
static void fm1_irq_unmask(int n) { (void)n; }
static void a_irq_gap_skip(void) { }                   /* (px_audio.h: the Bluetooth images time the audio interrupt) */
static void a_collect(void) {}
static uint8_t px_flash_ok = 1;
static uint32_t cur_preset;
static px_preset_t PRESET;
static void st_get(uint32_t k, px_preset_t *p);
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#define ST_IRQ_OFF() ((void)0)
#include "../firmware/px_store.h"

static void reboot(void) { memset(st_slot, 0, sizeof st_slot); st_cur = st_seq = 0; st_last = 0; st_midi = 0x55; mid_cfg.chan = 9; st_usb = 0xA5; px_ua_on = 7; px_store_boot(); }
static void loader_bytes_clear(void)
{
    for (uint32_t s = 0xF0000u; s < 0xF4000u; s += 0x1000u)
        for (uint32_t i = 0xF00; i < 0xF80; i++)
            if (nor[s + i] != 0xFF) { printf("FAIL byte %x not erased (update loader record zone)\n", s + i); fails++; return; }
}

int main(void)
{
    px_preset_t p, q, f;
    memset(nor, 0xFF, sizeof nor);
    nor[0xEFFFF] = 0x11; nor[0xF4000] = 0x22;          /* neighbours: must not move */
    reboot();
    CHECK(!st_saved(5) && cur_preset == 0, "empty flash: factory presets, start on slot 1");
    px_preset_factory(&f, 5);
    st_get(5, &q);
    CHECK(!memcmp(&q, &f, sizeof q), "empty flash: slot 6 is its factory preset");

    /* save, reboot, the sound is there and the pedal starts on it */
    px_preset_factory(&p, 5);
    p.p[PX_B_AMP][0] = 99; strcpy(p.name, "MY LEAD");
    CHECK(st_save(5, &p) == 0, "save slot 6");
    reboot();
    st_get(5, &q);
    CHECK(st_saved(5) && !memcmp(&q, &p, sizeof q), "after a reboot: slot 6 holds the saved sound");
    CHECK(cur_preset == 5 && !memcmp(&PRESET, &p, sizeof p), "the pedal starts on the slot saved last");
    st_get(4, &q); px_preset_factory(&f, 4);
    CHECK(!memcmp(&q, &f, sizeof q), "other slots stay factory");

    /* many saves: the copies alternate, the newest wins; the last slot (in sector 1) works too */
    for (int i = 0; i < 25; i++) {
        p.p[PX_B_DELAY][0] = (uint8_t)i;
        p.order[0] = (uint8_t)(i % 2 ? PX_B_SYN : PX_B_GATE); p.order[4] = (uint8_t)(i % 2 ? PX_B_GATE : PX_B_SYN);
        CHECK(st_save((uint32_t)(i % 2 ? ST_NSLOT - 1u : 0), &p) == 0, "repeated save");
    }
    reboot();
    st_get(0, &q); CHECK(q.p[PX_B_DELAY][0] == 24 && q.order[0] == PX_B_GATE, "slot 1: the newest of 25 saves");
    st_get(ST_NSLOT - 1u, &q);
    CHECK(q.p[PX_B_DELAY][0] == 23 && q.order[0] == PX_B_SYN && px_order_valid(q.order), "last slot (second sector): its newest save, with its block order");
    loader_bytes_clear();

    /* factory reset of a slot */
    CHECK(st_reset(ST_NSLOT - 1u) == 0, "restore the last slot to factory");
    reboot();
    st_get(ST_NSLOT - 1u, &q); px_preset_factory(&f, (int)ST_NSLOT - 1);
    CHECK(!st_saved(ST_NSLOT - 1u) && !memcmp(&q, &f, sizeof q), "the last slot is factory again after a reboot");

    /* a power cut at every step of a save: after it, the store is the old one or the new one, never broken */
    {
        static uint8_t snap[0x4000];
        px_preset_t before, newp;
        st_get(7, &before);
        newp = before; newp.p[PX_B_REVERB][2] = 77; strcpy(newp.name, "CUT TEST");
        memcpy(snap, nor + 0xF0000, sizeof snap);
        uint32_t seq0 = st_seq, cur0 = st_cur;
        int olds = 0, news = 0, bad = 0;
        for (int cut = 0; cut < 9000; cut += 7) {
            memcpy(nor + 0xF0000, snap, sizeof snap);
            st_seq = seq0; st_cur = cur0;
            reboot();
            ops = 0; cut_at = cut; dead = 0;
            st_save(7, &newp);
            cut_at = -1; dead = 0;
            reboot();
            st_get(7, &q);
            if (!memcmp(&q, &newp, sizeof q)) news++;
            else if (!memcmp(&q, &before, sizeof q)) olds++;
            else bad++;
            st_get(0, &q);
            if (q.p[PX_B_DELAY][0] != 24) bad++;           /* other slots intact */
        }
        printf("power cut at %d points of a save: old kept %d, new %d, broken %d\n", (9000 + 6) / 7, olds, news, bad);
        CHECK(bad == 0 && olds > 0 && news > 0, "a power cut never breaks the store");
    }
    CHECK(nor[0xEFFFF] == 0x11 && nor[0xF4000] == 0x22, "flash outside the store untouched");

    /* format 1 (v25..v28: 42 slots of 96 B, 11 blocks) written by an older OpenPocket: converted at boot */
    {
        memset(nor + 0xF0000, 0xFF, 0x4000);
        static uint8_t old[42][96];
        memset(old, 0xFF, sizeof old);
        const char *nm = "OLD LEAD";
        for (int k = 1; k < 42; k += 13) {                     /* slots 2, 15, 28, 41 saved */
            memset(old[k], 0, 91);
            memcpy(old[k], nm, 9);
            for (int b = 0; b < 11; b++) { old[k][14 + b] = (uint8_t)(b & 1); old[k][25 + b] = (uint8_t)b; }
            for (int i = 0; i < 55; i++) old[k][36 + i] = (uint8_t)(i + k);
            old[k][91] = ST_MARK;
        }
        uint32_t c = 0;
        for (int k = 0; k < 42; k++) c = ota_crc16(old[k], 96, c);
        st_hdr_t h; memset(&h, 0, sizeof h);
        h.magic = ST_MAGIC; h.seq = 7; h.crc = (uint16_t)c; h.ver = 1; h.nslot = 42; h.last = 27;
        for (int k = 0; k < 42; k++)
            memcpy(nor + 0xF2000 + (k < 38 ? 16 + k * 96 : 0x1000 + (k - 38) * 96), old[k], 96);
        memcpy(nor + 0xF2000, &h, sizeof h);
        reboot();
        st_get(27, &q);
        int ok = !strcmp(q.name, "OLD LEAD") && q.on[PX_B_SYN] == 0 && px_order_valid(q.order) && q.order[4] == PX_B_SYN;
        for (int b = 0; b < 11; b++)                           /* amp / drive / cab: moved to the v34 set */
            ok &= q.on[b] == (b & 1) && q.type[b] == ((b == PX_B_AMP || b == PX_B_DRIVE || b == PX_B_CAB) ? px_type_v34(b, b) : b);
        for (int i = 0; i < 55; i++) {                         /* delay TIME: moved to the v35 2 s scale */
            int want = (uint8_t)(i + 27);
            if (i == PX_B_DELAY * 5) want = px_dly_time_v35(q.type[PX_B_DELAY], want);
            if (i == PX_B_GATE * 5 + PX_PATCH_LEVEL) want = 50;          /* v36: preset LEVEL, 0 dB */
            ok &= ((uint8_t *)q.p)[i] == (uint8_t)want;
        }
        CHECK(ok && st_saved(27) && cur_preset == 27, "format 1 store: saved sounds converted, the pedal starts on the last one");
        st_get(2, &q); px_preset_factory(&f, 2);
        CHECK(!st_saved(2) && !memcmp(&q, &f, sizeof q), "format 1 store: unsaved slots stay factory");
        st_get(45, &q); px_preset_factory(&f, 45);
        CHECK(!memcmp(&q, &f, sizeof q), "format 1 store: the new slots 43..52 are factory");
        q.p[PX_B_SYN][0] = 9;                                  /* next save: format 2 in the other copy */
        CHECK(st_save(27, &q) == 0, "save after conversion");
        reboot();
        st_get(27, &f);
        CHECK(!memcmp(&q, &f, sizeof q) && st_saved(40), "after the first save: format 5, everything still there");
        loader_bytes_clear();
    }
    /* format 2 (v29..v33): the same layout, the old amp / drive / cab numbers -> the v34 set at boot */
    {
        memset(nor + 0xF0000, 0xFF, 0x4000);
        reboot();
        px_preset_factory(&q, 5);
        strcpy(q.name, "MY RECTO");
        q.type[PX_B_AMP] = 17;                                 /* v33 RECTIFIED */
        q.type[PX_B_DRIVE] = 13;                               /* v33 MUFF PI */
        q.type[PX_B_CAB] = 9;                                  /* v33 4x12 H30 */
        q.type[PX_B_MOD] = 2;                                  /* other blocks: untouched */
        CHECK(st_save(5, &q) == 0, "format 2 test store written");
        st_hdr_t h;                                            /* turn the copy just written into a format 2 one */
        uint32_t base = st_cur;
        memcpy(&h, nor + base, sizeof h);
        h.ver = 2;
        memcpy(nor + base, &h, sizeof h);
        reboot();
        st_get(5, &f);
        CHECK(st_saved(5) && !strcmp(f.name, "MY RECTO") && f.type[PX_B_AMP] == PX_AMP_5150 && f.type[PX_B_DRIVE] == PX_DRV_M77 &&
              f.type[PX_B_CAB] == PX_CAB_412V30 && f.type[PX_B_MOD] == 2, "format 2 store: RECTIFIED -> 5150, MUFF -> M77, H30 -> V30");
        CHECK(px_type_v34(PX_B_AMP, 99) == 0 && px_type_v34(PX_B_DRIVE, -1) == 0, "an unknown old type -> the first one");
        loader_bytes_clear();
    }
    /* format 3 (v34): types as today, delay TIME on the 1 s scale -> the 2 s scale (REVERSE keeps its own) */
    {
        memset(nor + 0xF0000, 0xFF, 0x4000);
        reboot();
        px_preset_factory(&q, 12);
        strcpy(q.name, "MY ECHO");
        q.type[PX_B_DELAY] = PX_DLY_TAPE; q.p[PX_B_DELAY][0] = 50; q.p[PX_B_DELAY][1] = 33;    /* 510 ms on v34 */
        CHECK(st_save(12, &q) == 0, "format 3 test store written");
        q.type[PX_B_DELAY] = PX_DLY_REVERSE; q.p[PX_B_DELAY][0] = 50;
        CHECK(st_save(13, &q) == 0, "format 3 test store written (reverse)");
        st_hdr_t h;
        uint32_t base = st_cur;
        memcpy(&h, nor + base, sizeof h);
        h.ver = 3;
        memcpy(nor + base, &h, sizeof h);
        reboot();
        st_get(12, &f);
        px_preset_t g;
        st_get(13, &g);
        CHECK(f.p[PX_B_GATE][PX_PATCH_LEVEL] == 50 && g.p[PX_B_GATE][PX_PATCH_LEVEL] == 50, "format 3 store: preset LEVEL set to 0 dB (50)");
        CHECK(f.p[PX_B_DELAY][0] == 40 && f.p[PX_B_DELAY][1] == 33 && f.type[PX_B_DELAY] == PX_DLY_TAPE && g.p[PX_B_DELAY][0] == 50,
              "format 3 store: TAPE 510 ms stays 510 ms (knob 50 -> 40), REVERSE untouched");
        CHECK(px_dly_time_v35(PX_DLY_DIGITAL, 100) == 80 && px_dly_time_v35(PX_DLY_DIGITAL, 0) == 0, "v34's 1000 ms -> 80, 20 ms -> 0");
        loader_bytes_clear();
    }
    loader_bytes_clear();

    /* the MIDI settings ride in the header of the copy: saved with it, back after a power cycle */
    {
        px_preset_t q;
        px_preset_factory(&q, 4);
        mid_cfg.chan = 7; mid_cfg.send = 0; mid_cfg.clock = 1;
        st_midi = px_midi_cfg_pack(&mid_cfg);
        CHECK(st_commit() == 0, "settings: a commit without a new sound");
        reboot();
        CHECK(mid_cfg.chan == 7 && !mid_cfg.send && mid_cfg.clock, "settings: channel 7, send off, clock on survive a power cycle");
        CHECK(st_save(4, &q) == 0, "settings: a preset save");
        reboot();
        CHECK(mid_cfg.chan == 7 && !mid_cfg.send && mid_cfg.clock, "settings: still there after a preset save (the header is rewritten with them)");
        {   /* a store written before the settings existed has zero there: the defaults, not the last boot's values */
            st_hdr_t h;
            uint32_t base = st_cur;
            memcpy(&h, nor + base, sizeof h);
            h.pad[0] = 0;
            memcpy(nor + base, &h, sizeof h);
            reboot();
            CHECK(mid_cfg.chan == 0 && mid_cfg.send && !mid_cfg.clock, "settings: a header with 0 there (every earlier OpenPocket) = omni, send on, clock off");
        }
        {   /* a power cut during the commit of settings: the old settings or the new, the presets intact */
            uint32_t olds = 0, news = 0, bad = 0;
            for (int cut = 0; cut < 9000; cut += 11) {
                uint8_t keep[0x100000];
                memcpy(keep, nor, sizeof nor);
                mid_cfg.chan = 12; mid_cfg.send = 1; mid_cfg.clock = 0;
                st_midi = px_midi_cfg_pack(&mid_cfg);
                ops = 0; dead = 0; cut_at = cut;
                st_commit();
                cut_at = -1; dead = 0;
                reboot();
                if (mid_cfg.chan == 12) news++; else if (mid_cfg.chan == 0) olds++; else bad++;
                st_get(4, &q);
                if (!st_saved(4)) bad++;
                memcpy(nor, keep, sizeof nor);
            }
            CHECK(bad == 0 && olds > 0 && news > 0, "settings: a power cut during a settings save leaves the old or the new, never a broken store");
        }
    }
    /* v42: the USB AUDIO setting rides in the header too (its second spare byte) */
    {
        px_preset_t q;
        px_preset_factory(&q, 5);
        reboot();
        CHECK(!px_ua_on && !st_usb, "USB audio: off in a store that never set it (every earlier OpenPocket wrote 0 there)");
        mid_cfg.chan = 3; st_midi = px_midi_cfg_pack(&mid_cfg);
        st_usb = 1;
        CHECK(st_commit() == 0, "USB audio: a commit of the setting");
        reboot();
        CHECK(px_ua_on == 1 && st_usb == 1, "USB audio: on survives a power cycle");
        CHECK(mid_cfg.chan == 3, "USB audio: the MIDI settings beside it are kept");
        CHECK(st_save(5, &q) == 0, "USB audio: a preset save");
        reboot();
        CHECK(px_ua_on == 1 && st_saved(5), "USB audio: still on after a preset save");
        {   /* only bit 0 is the setting */
            st_hdr_t h;
            memcpy(&h, nor + st_cur, sizeof h);
            h.pad[1] = 0xFE;
            memcpy(nor + st_cur, &h, sizeof h);
            reboot();
            CHECK(px_ua_on == 0, "USB audio: the byte's other bits do not switch it on");
        }
        st_usb = 0;
#if PX_OWN_FX
        px_live = 0; px_sc_rule = 0; px_sc_feet = 0;   /* (test build 49: 0xFE's bits 4 to 7 were the image's own settings; */
        px_vol_at = 0;                                 /* 51: bit 1 too) */
#endif
        CHECK(st_commit() == 0, "USB audio: a commit of off");
        reboot();
        CHECK(!px_ua_on && !st_usb, "USB audio: off survives a power cycle");
    }
#if PX_OWN_FX
    /* test build 39: the controls learned on the pedal ride in each copy (sector 1, after its slots, own mark and CRC) */
    {
        px_preset_t q;
        px_preset_factory(&q, 6);
        reboot();
        CHECK(px_midi_map_count() == 0, "learned: none in a store that never had them (a copy of an earlier firmware)");
        mid_map[0] = (px_map_t){ 80, 8 + PX_B_EQ, PXM_MAP_PRESS };
        mid_map[3] = (px_map_t){ 81, PXM_CC_NEXT, PXM_MAP_EACH };
        mid_cfg.each = 1; st_midi = px_midi_cfg_pack(&mid_cfg);
        CHECK(st_commit() == 0, "learned: a commit");
        reboot();
        CHECK(px_midi_map_count() == 2 && mid_map[0].src == 80 && mid_map[0].dst == 8 + PX_B_EQ && mid_map[3].mode == PXM_MAP_EACH &&
              mid_map[1].src == 0xFF && mid_cfg.each, "learned: two controls and LATCH survive a power cycle");
        CHECK(st_save(6, &q) == 0, "learned: a preset save");
        reboot();
        CHECK(px_midi_map_count() == 2 && st_saved(6), "learned: still there after a preset save (each copy carries them)");
        {   /* a damaged table (a bit flipped): nothing learned, the presets as they were */
            nor[st_cur + ST_MAP_OFF + 5] ^= 0x01;
            reboot();
            CHECK(px_midi_map_count() == 0 && st_saved(6), "learned: a table whose CRC fails is dropped, the presets kept");
            nor[st_cur + ST_MAP_OFF + 5] ^= 0x01;
            reboot();
            CHECK(px_midi_map_count() == 2, "learned: the same table read again once whole");
        }
        {   /* an earlier firmware writes its copy with nothing there: then nothing is learned, and its copy is read whole */
            uint8_t blank[ST_MAP_LEN];
            memset(blank, 0xFF, sizeof blank);
            memcpy(nor + st_cur + ST_MAP_OFF, blank, sizeof blank);
            reboot();
            CHECK(px_midi_map_count() == 0 && st_saved(6), "learned: a copy without the table (written by an earlier firmware) = none");
        }
        {   /* a power cut while saving a new table: the old table or the new one, never a broken store */
            uint32_t olds = 0, news = 0, bad = 0;
            px_midi_map_clear();
            mid_map[0] = (px_map_t){ 70, 8 + PX_B_DELAY, PXM_MAP_PRESS };
            CHECK(st_commit() == 0, "learned: the old table");
            for (int cut = 0; cut < 9000; cut += 11) {
                uint8_t keep[0x100000];
                memcpy(keep, nor, sizeof nor);
                reboot();
                px_midi_map_clear();
                mid_map[0] = (px_map_t){ 71, 8 + PX_B_REVERB, PXM_MAP_EACH };
                mid_map[1] = (px_map_t){ 72, 117, PXM_MAP_VALUE };
                ops = 0; dead = 0; cut_at = cut;
                st_commit();
                cut_at = -1; dead = 0;
                reboot();
                if (px_midi_map_count() == 2 && mid_map[0].src == 71 && mid_map[1].dst == 117) news++;
                else if (px_midi_map_count() == 1 && mid_map[0].src == 70) olds++;
                else bad++;
                if (!st_saved(6)) bad++;
                memcpy(nor, keep, sizeof nor);
            }
            CHECK(bad == 0 && olds > 0 && news > 0, "learned: a power cut during a save leaves the old table or the new, never a broken one");
        }
        {   /* test build 46: each preset's own learned controls ("OPPL", right after the map): with the store, a slot reset to its
             * factory sound drops its own, a copy without the record (builds before 46) has none, a bad entry is dropped alone */
            px_midi_pl_clear();
            CHECK(px_midi_pl_put(3, 80, 8 + PX_B_EQ, PXM_MAP_PRESS, 20 + PX_B_EQ, 0u, 100u) == 0 &&
                  px_midi_pl_put(3, 4, px_cc_param(PX_B_DELAY, 1), PXM_MAP_VALUE, px_cc_param(PX_B_DELAY, 1), 0u, 100u) == 0 &&
                  px_midi_pl_put(7, 80, 20 + PX_B_MOD, PXM_MAP_VALUE, 8 + PX_B_MOD, 0u, 100u) == 0, "a preset's own: three entries, two presets");
            CHECK(st_commit() == 0, "a preset's own: a commit");
            reboot();
            CHECK(px_midi_pl_count(3) == 2 && px_midi_pl_count(7) == 1 && px_midi_pl_row(7, 20 + PX_B_MOD, 0xFF) >= 0 &&
                  px_midi_map_count() == 2, "a preset's own: they survive a power cycle, the editor's learned controls with them");
            CHECK(st_save(6, &q) == 0, "a preset's own: a preset save");
            reboot();
            CHECK(px_midi_pl_count(52) == 3, "a preset's own: still there after a preset save");
            CHECK(st_reset(3) == 0, "a preset's own: slot 3 back to its factory sound");
            reboot();
            CHECK(px_midi_pl_count(3) == 0 && px_midi_pl_count(7) == 1, "a preset's own: its slot reset, its own go; slot 7's stay");
            nor[st_cur + ST_PL_OFF + 4u + 1u] ^= 0x01;     /* a bit flipped in the record: its CRC fails */
            reboot();
            CHECK(px_midi_pl_count(52) == 0 && px_midi_map_count() == 2 && st_saved(6), "a preset's own: a record whose CRC fails is dropped, the rest kept");
            nor[st_cur + ST_PL_OFF + 4u + 1u] ^= 0x01;
            {
                uint8_t blank[ST_PL_LEN];
                memset(blank, 0xFF, sizeof blank);
                memcpy(nor + st_cur + ST_PL_OFF, blank, sizeof blank);
                reboot();
                CHECK(px_midi_pl_count(52) == 0 && px_midi_map_count() == 2, "a preset's own: a copy without the record (builds before 46) = none");
            }
            px_midi_pl_clear();
            CHECK(px_midi_pl_put(5, 81, 8 + PX_B_EQ, PXM_MAP_PRESS, 20 + PX_B_EQ, 0u, 100u) == 0 && st_commit() == 0, "a preset's own: one entry");
            mid_pl[1] = (px_pl_t){ 60, 82, 8 + PX_B_EQ, PXM_MAP_PRESS, 0, 100 };   /* (a slot no store has: written as it is, read as bad) */
            st_pl_pack(st_pl_buf);
            memcpy(nor + st_cur + ST_PL_OFF, st_pl_buf, ST_PL_LEN);
            reboot();
            CHECK(px_midi_pl_count(52) == 1 && px_midi_pl_count(5) == 1, "a preset's own: a bad entry is dropped alone");
            px_midi_pl_clear();
        }
        {   /* test build 46: IN-Z and PICKUP, in the "OPPL" record's last two bytes (the value, the value ^ 0x5A), applied at boot;
             * the filter (px_inz_coefs) against the circuit it stands for: H(RL) / H(1 M), the lows' level kept, within 0.6 dB up to 5 kHz (the pickups'
             * resonances; test build 47's BRIGHT, a peak of its own at 3.7 to 6.1 kHz, 0.53 dB off at most there, the others 0.40),
             * 1.2 dB up to 8 kHz (the bilinear's warping, where the circuit's ratio heads back to 0 dB) */
            double worst = 0, worst5 = 0;
            for (uint32_t pu = 0; pu < 3u; pu++) for (uint32_t z = 1; z < PX_INZ_N; z++) {
                float c[5];
                px_pickup = (uint8_t)pu; px_inz = (uint8_t)z;
                if (!px_inz_coefs(44117.6f, c)) { worst = 99; continue; }
                for (double f = 100; f <= 8000; f *= 1.25) {
                    const double w = 2 * M_PI * f / 44117.6, L = PX_PU_L[pu], R = PX_PU_R[pu], C = PX_PU_C[pu], RL = PX_INZ_RL[z];
                    const double Ct = C * PX_INZ_CK[z];   /* (test build 47: BRIGHT, the cable's capacitance mostly gone) */
                    double nr = c[0] + c[1] * cos(-w) + c[2] * cos(-2 * w), ni = c[1] * sin(-w) + c[2] * sin(-2 * w);
                    double dr = 1 + c[3] * cos(-w) + c[4] * cos(-2 * w), di = c[3] * sin(-w) + c[4] * sin(-2 * w);
                    const double got = 10 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
                    const double s = 2 * M_PI * f;   /* |D(1M)| / |D(RL)| at j s */
                    const double ar = 1 + R / 1e6 - L * C * s * s, ai = (R * C + L / 1e6) * s, br = 1 + R / RL - L * Ct * s * s, bi = (R * Ct + L / RL) * s;
                    const double want = 10 * log10((ar * ar + ai * ai) / (br * br + bi * bi)) + 20 * log10((1 + R / RL) / (1 + R / 1e6));
                    if (fabs(got - want) > worst) worst = fabs(got - want);
                    if (f <= 5000 && fabs(got - want) > worst5) worst5 = fabs(got - want);
                }
            }
            printf("GUITAR TONE: the filter against the loaded pickup: %.2f dB at most up to 5 kHz, %.2f dB up to 8 kHz\n", worst5, worst);
            CHECK(worst5 < 0.6 && worst < 1.2, "GUITAR TONE: the filter is the loaded pickup's response (0.6 dB to 5 kHz, 1.2 dB to 8 kHz), every PICKUP and GUITAR TONE");
            px_inz = 0; px_pickup = 1;
            { float c[5]; CHECK(!px_inz_coefs(44117.6f, c), "IN-Z: 1M is no filter at all (the input as it is)"); }
            px_inz = 3; px_pickup = 1;
            const int n0 = a_inz_n;
            CHECK(st_commit() == 0, "IN-Z: 90K, HUMBUCK, a commit");
            px_inz = 0; px_pickup = 0;
            reboot();
            CHECK(px_inz == 3 && px_pickup == 1 && a_inz_n == n0 + 1, "IN-Z: 90K and HUMBUCK survive a power cycle, applied at boot");
            nor[st_cur + ST_PL_OFF + ST_PL_LEN - 1u] ^= 0x01;   /* the pair no longer matches */
            reboot();
            CHECK(px_inz == 0 && px_pickup == 0, "IN-Z: a pair that does not match = the input as it is");
            nor[st_cur + ST_PL_OFF + ST_PL_LEN - 1u] ^= 0x01;
            reboot();
            CHECK(px_inz == 3 && px_pickup == 1, "IN-Z: read again once whole");
            px_inz_unpack(0x1Fu); CHECK(px_inz == 0 && px_pickup == 0, "IN-Z: PICKUP 3 (no build writes it) = the input as it is");
            px_inz = 0; px_pickup = 0;
            CHECK(st_commit() == 0, "IN-Z: back to 1M");
        }
        {   /* test build 48: 47's record ("OPP2": 6 bytes an entry) read with the pedal's whole travel, its GUITAR TONE and
             * PICKUP kept, and written again as 48's ("OPP3", the travel with each entry) */
            uint8_t r[4u + PXM_PL_N * 6u + 4u];
            const uint32_t T = 4u + PXM_PL_N * 6u, dl = px_cc_param(PX_B_DELAY, 1);
            memset(r, 0xFF, sizeof r);
            r[0] = 'O'; r[1] = 'P'; r[2] = 'P'; r[3] = '2';
            r[4] = 3; r[5] = 80; r[6] = 8 + PX_B_EQ; r[7] = PXM_MAP_PRESS; r[8] = 0; r[9] = 100;
            r[10] = 3; r[11] = 4; r[12] = (uint8_t)dl; r[13] = PXM_MAP_VALUE; r[14] = 20; r[15] = 70;
            const uint32_t c = ota_crc16(r + 4, PXM_PL_N * 6u, 0);
            r[T] = (uint8_t)c; r[T + 1u] = (uint8_t)(c >> 8);
            r[T + 2u] = 5u | 2u << 4; r[T + 3u] = r[T + 2u] ^ 0x5Au;   /* (DARK, P90) */
            memset(nor + st_cur + ST_PL_OFF, 0xFF, ST_PL_LEN);
            memcpy(nor + st_cur + ST_PL_OFF, r, sizeof r);
            reboot();
            int i = px_midi_pl_row(3, dl, 0xFFu);
            CHECK(px_midi_pl_count(3) == 2 && i >= 0 && mid_pl[i].src == 4 && mid_pl[i].lo == 20 && mid_pl[i].hi == 70 && mid_pl[i].clo == 0 &&
                  mid_pl[i].chi == 127 && px_inz == 5 && px_pickup == 2, "47's record: its entries and ranges, the pedal's whole travel, its GUITAR TONE (DARK) and PICKUP (P90)");
            mid_pl[i].clo = 3; mid_pl[i].chi = 120;
            CHECK(st_commit() == 0, "47's record: a commit");
            reboot();
            i = px_midi_pl_row(3, dl, 0xFFu);
            CHECK(!memcmp(nor + st_cur + ST_PL_OFF, "OPP3", 4) && px_midi_pl_count(3) == 2 && i >= 0 && mid_pl[i].lo == 20 && mid_pl[i].hi == 70 &&
                  mid_pl[i].clo == 3 && mid_pl[i].chi == 120 && px_inz == 5 && px_pickup == 2, "47's record: written again as 48's, the pedal's travel kept");
            mid_pl[i].hi = 101;
            CHECK(!px_midi_pl_valid(&mid_pl[i]), "a pedal's range past 100: not an entry");
            mid_pl[i].hi = 70; mid_pl[i].chi = 128;
            CHECK(!px_midi_pl_valid(&mid_pl[i]), "a pedal's travel past 127: not an entry");
            memcpy(nor + st_cur + ST_PL_OFF, "OPPL", 4);
            reboot();
            CHECK(px_midi_pl_count(52) == 0, "46's record (OPPL) is no longer read: none");
            px_midi_pl_clear(); px_inz_unpack(0);
        }
        {   /* test build 51: a two-job pedal (its mode's bit 6, px_midi.h PXM_PL_DUAL) kept in "OPP3" as it is; one on the GATE's
             * PRESET LVL is not one (dropped alone); SETTINGS' VOL PEDAL in the USB settings byte's bit 1 */
            px_midi_pl_clear();
            CHECK(px_midi_pl_put(4, 7, px_cc_param(PX_B_SFX, 0), PXM_MAP_VALUE | PXM_PL_DUAL, 0xFFu, 10u, 90u) == 0 &&
                  px_midi_pl_put(4, 8, px_cc_param(PX_B_DELAY, 1), PXM_MAP_VALUE, 0xFFu, 0u, 100u) == 0, "two jobs: two pedals in a preset");
            mid_pl[px_midi_pl_row(4, px_cc_param(PX_B_SFX, 0), 0xFFu)].clo = 3;
            mid_pl[2] = (px_pl_t){ 6, 9, (uint8_t)px_cc_param(PX_B_GATE, 2), PXM_MAP_VALUE | PXM_PL_DUAL, 0, 100, 0, 127 };   /* (not one) */
            px_vol_at = 1;
            CHECK(st_commit() == 0, "two jobs: a commit");
            px_midi_pl_clear(); px_vol_at = 0;
            reboot();
            const int i = px_midi_pl_row(4, px_cc_param(PX_B_SFX, 0), 0xFFu);
            CHECK(i >= 0 && mid_pl[i].mode == (PXM_MAP_VALUE | PXM_PL_DUAL) && mid_pl[i].lo == 10 && mid_pl[i].hi == 90 && mid_pl[i].clo == 3 &&
                  px_midi_pl_dual(4) == PX_B_SFX && px_midi_pl_count(4) == 2 && px_midi_pl_count(6) == 0,
                  "two jobs: kept through a power cycle, the GATE's dropped alone");
            CHECK(px_vol_at == 1u && (st_usb & PX_ST_VOLAT), "two jobs: VOL PEDAL BEFORE DLY kept (bit 1)");
            px_vol_at = 0;
            CHECK(st_commit() == 0, "two jobs: VOL PEDAL back to AT INPUT");
            reboot();
            CHECK(px_vol_at == 0u && !(st_usb & PX_ST_VOLAT), "two jobs: AT INPUT kept");
            px_midi_pl_clear();
            CHECK(st_commit() == 0, "two jobs: cleared");
        }
        {   /* test build 49: the presets' scenes ("OPSC", after the learned controls): kept through a power cycle, a slot reset
             * to its factory sound loses its own, a record whose CRC fails is none, an entry that does not read right is
             * dropped alone; a copy without the record (builds before 49): none. PLAY VIEW's three and the two scene rows
             * in the USB settings byte's bits 4 to 7 */
            memset(px_sc_tab, 0, sizeof px_sc_tab);
            px_sc_tab[5] = (px_sc_t){ {0x123u, 0x456u, 0x789u, 0xABCu}, 2u, 0x0Fu };
            px_sc_tab[9] = (px_sc_t){ {0x001u, 0x003u, 0u, 0u}, 1u, 0x03u };
            px_live = 2; px_sc_rule = 1; px_sc_feet = 1;
            CHECK(st_commit() == 0, "scenes: a commit");
            memset(px_sc_tab, 0, sizeof px_sc_tab); px_live = 0; px_sc_rule = 0; px_sc_feet = 0;
            reboot();
            CHECK(px_sc_tab[5].mask[3] == 0xABCu && px_sc_tab[5].cur == 2u && px_sc_tab[5].set == 0x0Fu && px_sc_tab[9].mask[1] == 0x003u &&
                  px_sc_tab[9].cur == 1u && px_sc_tab[0].set == 0u, "scenes: they survive a power cycle");
            CHECK(px_live == 2u && px_sc_rule == 1u && px_sc_feet == 1u, "scenes: PLAY VIEW SCENES, SCENE WINS, the footswitches on presets: kept");
            CHECK(st_reset(5) == 0, "scenes: slot 5 back to its factory sound");
            reboot();
            CHECK(px_sc_tab[5].set == 0u && px_sc_tab[9].set == 0x03u, "scenes: its slot reset, its scenes go; slot 9's stay");
            nor[st_cur + ST_SC_OFF + 4u + 9u * 10u] ^= 0x01;   /* (a bit flipped: the CRC fails) */
            reboot();
            CHECK(px_sc_tab[9].set == 0u && px_midi_map_count() == 2, "scenes: a record whose CRC fails: none, the rest kept");
            nor[st_cur + ST_SC_OFF + 4u + 9u * 10u] ^= 0x01;
            px_sc_tab[3].cur = 7u;                     /* (a scene that is not one: dropped alone) */
            px_sc_tab[9] = (px_sc_t){ {0x001u, 0x003u, 0u, 0u}, 1u, 0x03u };
            CHECK(st_commit() == 0, "scenes: a commit with a bad entry");
            reboot();
            CHECK(px_sc_tab[3].set == 0u && px_sc_tab[3].cur == 0u && px_sc_tab[9].set == 0x03u, "scenes: a bad entry is dropped alone");
            memset(nor + st_cur + ST_SC_OFF, 0xFF, ST_SC_LEN);
            reboot();
            CHECK(px_sc_tab[9].set == 0u, "scenes: a copy without the record (builds before 49): none");
            memset(px_sc_tab, 0, sizeof px_sc_tab); px_live = 0; px_sc_rule = 0; px_sc_feet = 0;
            CHECK(st_commit() == 0, "scenes: cleared again");
        }
        {   /* test build 50: the scenes' names ("OPN1" presets 1-26, "OPN2" 27-52): a save writes the playing preset's, the
             * others go over untouched, a commit that is no save changes none, a FACTORY reset drops its own; a record whose
             * CRC fails: none in its half, the other kept; a copy without them (builds before 50): none; what is no letter: 0 */
            memset(px_scn, 0, sizeof px_scn);
            memcpy(px_scn[0], "VRS", 3); memcpy(px_scn[1], "CH", 2); memcpy(px_scn[3], "SOL", 3);
            px_preset_t q; px_preset_factory(&q, 9);
            CHECK(st_save(9, &q) == 0, "names: preset 10 saved with its names");
            memset(px_scn, 0, sizeof px_scn); memcpy(px_scn[2], "OUT", 3);
            px_preset_factory(&q, 40);
            CHECK(st_save(40, &q) == 0, "names: preset 41 saved with its (the other record)");
            memset(px_scn, 0, sizeof px_scn);
            CHECK(st_commit() == 0, "names: a commit that is no save");
            reboot();
            px_scn_load(9);
            const int a = !memcmp(px_scn[0], "VRS", 3) && !memcmp(px_scn[1], "CH\0", 3) && px_scn[2][0] == 0 && !memcmp(px_scn[3], "SOL", 3) &&
                          !memcmp(px_scn_saved, px_scn, sizeof px_scn);
            px_scn_load(40);
            CHECK(a && !memcmp(px_scn[2], "OUT", 3) && px_scn[0][0] == 0, "names: both kept through a power cycle, each its preset's");
            CHECK(st_reset(9) == 0, "names: preset 10 back to its factory sound");
            reboot(); px_scn_load(9);
            const int b = px_scn[0][0] == 0 && px_scn[3][0] == 0;
            px_scn_load(40);
            CHECK(b && !memcmp(px_scn[2], "OUT", 3), "names: its reset drops its own, preset 41's stay");
            nor[st_cur + ST_SN_OFF + ST_SN_LEN + 4u + 14u * 12u + 6u] ^= 0x01;   /* (a bit flipped in OPN2: its CRC fails) */
            reboot(); px_scn_load(40);
            CHECK(px_scn[2][0] == 0 && st_sn_ok == 1u, "names: a record whose CRC fails: none in its half, the other read");
            nor[st_cur + ST_SN_OFF + ST_SN_LEN + 4u + 14u * 12u + 6u] ^= 0x01;
            memset(px_scn, 0, sizeof px_scn); px_scn[1][0] = 'A'; px_scn[1][1] = 0x07; px_scn[1][2] = 'B';
            px_preset_factory(&q, 5);
            CHECK(st_save(5, &q) == 0, "names: a name with a byte that is no letter");
            reboot(); px_scn_load(5);
            CHECK(px_scn[1][0] == 'A' && px_scn[1][1] == 0 && px_scn[1][2] == 'B', "names: what is no letter reads as 0 (the name ends there)");
            memset(nor + st_cur + ST_SN_OFF, 0xFF, 2u * ST_SN_LEN);
            reboot(); px_scn_load(40);
            CHECK(px_scn[2][0] == 0 && st_sn_ok == 0u, "names: a copy without them (builds before 50): none");
            memset(px_scn, 0, sizeof px_scn);
            CHECK(st_commit() == 0, "names: cleared again");
        }
        px_midi_map_clear(); mid_cfg.each = 0; st_midi = px_midi_cfg_pack(&mid_cfg);
        CHECK(st_commit() == 0, "learned: cleared again");
        /* INPUT (test build 39): the header's third spare byte, signed dB; applied at boot */
        reboot();
        CHECK(px_in_db == 0, "INPUT: 0 dB in a store that never set it (every earlier save wrote 0 there)");
        px_in_db = -7;
        CHECK(st_commit() == 0, "INPUT: a commit of -7 dB");
        a_in_gain_n = 0;
        reboot();
        CHECK(px_in_db == -7 && a_in_gain_n >= 1, "INPUT: -7 dB survives a power cycle and is applied at boot");
        CHECK(st_save(6, &q) == 0, "INPUT: a preset save");
        reboot();
        CHECK(px_in_db == -7, "INPUT: still there after a preset save");
        {   /* a value out of range in the byte: INPUT 15 is clamped to +10 dB, BT POWER 3 (none) is HIGH */
            st_hdr_t hh;
            memcpy(&hh, nor + st_cur, sizeof hh);
            hh.pad[2] = 0x0F | 0x60;
            memcpy(nor + st_cur, &hh, sizeof hh);
            reboot();
            CHECK(px_in_db == PX_IN_DB_MAX && px_bt_pwr == 0 && !px_tails, "INPUT: out of range clamped to +10 dB; BT POWER 3 read as HIGH");
        }
        /* test build 40: TAILS and BT POWER beside INPUT in the same byte, applied at boot */
        px_in_db = -3; px_tails = 1; px_bt_pwr = 2;
        CHECK(st_commit() == 0, "TAILS, BT POWER: a commit");
        a_tails_n = 0;
        reboot();
        CHECK(px_in_db == -3 && px_tails == 1 && px_bt_pwr == 2 && a_tails_n >= 1, "TAILS on, BT POWER LOW and INPUT -3 dB survive a power cycle (TAILS applied at boot)");
        px_in_db = 0; px_tails = 0; px_bt_pwr = 0;
        CHECK(st_commit() == 0, "INPUT, TAILS, BT POWER: back to the defaults");
        /* test build 42: OUTPUT, bits 2-3 of the USB settings byte (header pad[1]), USB AUDIO's bit 0 beside it untouched */
        for (uint8_t m = 1; m <= 2; m++) {
            st_usb = (uint8_t)((st_usb & ~PX_ST_OUT_MASK) | (m << PX_ST_OUT_SH) | 1u);
            CHECK(st_commit() == 0, "OUTPUT: a commit");
            px_out_mode = 0; px_ua_on = 0;
            reboot();
            CHECK(px_out_mode == m && px_ua_on == 1, m == 1 ? "OUTPUT STEREO survives a power cycle, USB AUDIO's bit 0 beside it" : "OUTPUT GTR+BASS survives a power cycle");
        }
        st_usb = (uint8_t)(3u << PX_ST_OUT_SH);
        CHECK(st_commit() == 0, "OUTPUT: a value no build writes (3)");
        reboot();
        CHECK(px_out_mode == 0, "OUTPUT: 3 read as MONO");
        st_usb = 0; px_out_mode = 0; px_ua_on = 0;
        CHECK(st_commit() == 0, "OUTPUT: back to MONO");
        /* test build 43: USB audio's routing in the map record's last two bytes (the value, the value ^ 0x5A) */
        {
            const uint32_t at = ST_MAP_OFF + ST_MAP_LEN - 2u;
            mid_map[2] = (px_map_t){ 90, 8 + PX_B_MOD, PXM_MAP_PRESS };
            px_ua_route = 0;
            CHECK(st_commit() == 0, "USB routing: a commit with the default");
            nor[st_cur + at] = 0; nor[st_cur + at + 1u] = 0;        /* (as builds 39 to 42 wrote them) */
            px_ua_route = 0x55;
            reboot();
            CHECK(px_ua_route == 0 && px_midi_map_count() == 1, "USB routing: a copy of builds 39 to 42 (0, 0) reads as the default, the learned controls kept");
            px_ua_route = (uint8_t)(2u | 4u | (2u + PX_B_DELAY) << 3);   /* DI + OUT, GUITAR CUT, before DELAY */
            CHECK(st_commit() == 0, "USB routing: a commit");
            px_ua_route = 0;
            reboot();
            CHECK(px_ua_route == (2u | 4u | (2u + PX_B_DELAY) << 3) && px_midi_map_count() == 1, "USB routing: DI + OUT, CUT, before DELAY survive a power cycle");
            {
                px_preset_t q;
                px_preset_factory(&q, 9);
                CHECK(st_save(9, &q) == 0, "USB routing: a preset save");
                reboot();
                CHECK(PX_UA_RET(px_ua_route) == 2u + PX_B_DELAY && st_saved(9), "USB routing: still there after a preset save");
            }
            nor[st_cur + at + 1u] ^= 0x10;                  /* the pair no longer matches */
            reboot();
            CHECK(px_ua_route == 0 && px_midi_map_count() == 1, "USB routing: a pair that does not match = the default (the learned controls kept)");
            nor[st_cur + at] = (uint8_t)(15u << 3); nor[st_cur + at + 1u] = (uint8_t)((15u << 3) ^ 0x5Au);   /* RETURN 15: no such place */
            reboot();
            CHECK(px_ua_route == 0, "USB routing: a value no build writes (RETURN 15) = the default");
            nor[st_cur + at] = 3u; nor[st_cur + at + 1u] = (uint8_t)(3u ^ 0x5Au);   /* SEND 3: none */
            reboot();
            CHECK(px_ua_route == 0, "USB routing: SEND 3 = the default");
            px_midi_map_clear(); px_ua_route = 0;
            CHECK(st_commit() == 0, "USB routing: back to the default");
        }
    }
#endif
    loader_bytes_clear();
    printf(fails ? "%d FAILED\n" : "store tests passed\n", fails);
    return fails != 0;
}
