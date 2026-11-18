/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the user's presets in flash.
 *
 * 52 slots of 112 bytes (px_preset_t, 110 B, + a "saved" mark), kept in RAM (st_slot) and written as one whole copy
 * on each save, alternately to copy A [0xF0000, 0xF2000) and copy B [0xF2000, 0xF4000), each two 4 KiB sectors:
 *   sector 0: header (16 B) + slots 0..33        sector 1: slots 34..51
 * Format 2 (v29: a 12th block and the block order). A format-1 copy (v25..v28: 42 slots of 96 B, 11 blocks) is read
 * and converted at boot: the saved sounds come through, the new block off, the order the default one; the next save
 * writes format 2 to the other copy.
 * The header goes last, with a sequence number and a CRC16 of every slot: at boot the valid copy with the highest
 * sequence wins, so a power cut during a save leaves the previous copy. Bytes 0xF00..0xF7F of each sector stay
 * erased: the update loader treats a valid record there as an update record (loader/ldr_core.c).
 * The update loader writes only the app area [0x4000, 0xA3000): presets survive firmware updates.
 * During a write the audio is muted (the output buffers are zeros), the audio IRQ held and the second core idle
 * (its loop runs from RAM): the flash cannot be read through XIP while it is being written.
 */
#pragma once

#define ST_A       0xF0000u
#define ST_B       0xF2000u
#define ST_NSLOT   52u
#define ST_SLOT    112u
#define ST_S0N     34u                          /* slots in sector 0: 16 + 34 * 112 = 0xEF0 < 0xF00 */
#define ST_VER     5u          /* 5: v36 (preset LEVEL); 4: v35 (delay TIME up to 2 s); 3: v34 (amp / drive / cab types renumbered); 2: v29; 1: v25 */
#define ST1_NSLOT  42u                          /* format 1 (v25..v28) */
#define ST1_SLOT   96u
#define ST1_S0N    38u
#define ST1_PSIZE  91u                          /* its preset: name 14, on 11, type 11, p 11 x 5 */
typedef char st_size_check[(sizeof(px_preset_t) < ST_SLOT && ST_NSLOT == 52u) ? 1 : -1];
#define ST_MAGIC   0x5350504Fu                  /* "OPPS" */
#define ST_MARK    0x5Au                        /* slot holds a saved preset (else: the factory one) */
#ifndef ST_IRQ_OFF
#define ST_IRQ_OFF() __asm__ volatile("cli" ::: "memory")
#endif

/* pad[0]: the MIDI settings (px_midi.h, px_midi_cfg_pack); 0 = the defaults, which is what every earlier save wrote.
 * pad[1] (v42): bit 0 = USB AUDIO on (px_midi.h, px_ua_on); the other bits 0. 0 = off, as every earlier save wrote. The
 * Bluetooth audio image (test build 42): bits 2-3 = OUTPUT, 0 MONO, 1 STEREO, 2 GTR+BASS (px_midi.h px_out_mode) */
typedef struct { uint32_t magic, seq; uint16_t crc; uint8_t ver, nslot, last, pad[3]; } st_hdr_t;   /* 16 B */
static uint8_t st_slot[ST_NSLOT][ST_SLOT] __attribute__((aligned(4)));
static uint32_t st_seq, st_cur;                 /* sequence of the copy in use, its base (0: none yet) */
static uint8_t st_ok;                           /* the store can be written (the flash is the expected part) */
static uint8_t st_last;                         /* the slot to start on (the last one saved) */
static uint8_t st_midi;                         /* the MIDI settings byte (header pad[0]) */
static uint8_t st_usb;                          /* the USB settings byte (header pad[1]) */

#if PX_OWN_FX
/* test build 39: the controls learned on the pedal (px_midi.h mid_map) ride in each copy, in sector 1 after its slots, with
 * their own mark and CRC16: "OPMP", PXM_MAP_N entries of 3 bytes, the CRC, 2 bytes 0. The header's CRC covers the slots
 * only, as before, so other firmware reads this copy as it always did and never looks here; a copy it writes has none
 * (the sector is erased), and then nothing is learned. Written before the header like the slots, so a power cut keeps the
 * previous copy whole, its learned controls with it.
 * Test build 43: the last 2 bytes carry USB audio's routing (px_midi.h px_ua_route) and the same ^ 0x5A, which builds 39 to
 * 42 wrote as 0, 0 and do not read: a pair that does not match is the default, 0 */
#define ST_MAP_OFF (0x1000u + (ST_NSLOT - ST_S0N) * ST_SLOT)   /* 0x17E0: under 0xF00 of sector 1 */
#define ST_MAP_LEN (4u + PXM_MAP_N * 3u + 4u)
#define ST_MAP_MAGIC 0x504D504Fu                        /* "OPMP" */
typedef char st_map_check[(ST_MAP_OFF + ST_MAP_LEN <= 0x1F00u && sizeof(px_map_t) == 3u) ? 1 : -1];
static void st_map_pack(uint8_t *b)
{
    uint32_t c;
    b[0] = ST_MAP_MAGIC & 0xFFu; b[1] = (ST_MAP_MAGIC >> 8) & 0xFFu; b[2] = (ST_MAP_MAGIC >> 16) & 0xFFu; b[3] = ST_MAP_MAGIC >> 24;
    memcpy(b + 4, mid_map, PXM_MAP_N * 3u);
    c = ota_crc16(b + 4, PXM_MAP_N * 3u, 0);
    b[4u + PXM_MAP_N * 3u] = (uint8_t)c; b[5u + PXM_MAP_N * 3u] = (uint8_t)(c >> 8);
    b[6u + PXM_MAP_N * 3u] = px_ua_route; b[7u + PXM_MAP_N * 3u] = (uint8_t)(px_ua_route ^ 0x5Au);   /* (test build 43) */
}
static void st_map_load(uint32_t base)                 /* the copy's learned controls, or none */
{
    uint8_t b[ST_MAP_LEN];
    uint32_t i, c;
    px_midi_map_clear();
    px_ua_route = 0;
    if (!base || ota_fread(base + ST_MAP_OFF, b, ST_MAP_LEN)) return;
    if ((b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24) != ST_MAP_MAGIC) return;
    c = ota_crc16(b + 4, PXM_MAP_N * 3u, 0);
    if ((b[4u + PXM_MAP_N * 3u] | b[5u + PXM_MAP_N * 3u] << 8) != (c & 0xFFFFu)) return;
    {   /* test build 43: USB audio's routing (a pair that does not match, as builds 39 to 42 wrote it: the default) */
        const uint8_t r = b[6u + PXM_MAP_N * 3u];
        if ((uint8_t)(r ^ 0x5Au) == b[7u + PXM_MAP_N * 3u] && px_ua_route_valid(r)) px_ua_route = r;
    }
    for (i = 0; i < PXM_MAP_N; i++) if (!px_midi_map_valid((const px_map_t *)(b + 4u + i * 3u))) return;
    memcpy(mid_map, b + 4, PXM_MAP_N * 3u);
}
/* test build 46: each preset's own learned controls (px_midi.h mid_pl), right after the learned map. Test build 48's record:
 * "OPP3", PXM_PL_N entries of 8 bytes (slot, controller's number, the pedal's number, mode, a pedal's setting at the heel and
 * at the toe, the values the pedal sends there), the CRC, then SETTINGS' GUITAR TONE and PICKUP (px_inz_pack) and the same
 * ^ 0x5A. Written and checked as the map is. Read as well: 47's "OPP2" (entries of 6 bytes: the pedal's whole travel); 46's
 * "OPPL" no longer (47 wrote its own over it at its first save). A copy without either (other firmware) has none, and the input
 * as it is. An entry that does not read right is dropped alone; a settings pair that does not match: the input as it is */
#define ST_PL_OFF (ST_MAP_OFF + ST_MAP_LEN)            /* 0x1800 */
#define ST_PL_LEN (4u + PXM_PL_N * 8u + 4u)
#define ST_PL_MAGIC 0x3350504Fu                        /* "OPP3" */
#define ST_PL2_MAGIC 0x3250504Fu                       /* "OPP2" (47's) */
typedef char st_pl_check[(ST_PL_OFF + ST_PL_LEN <= 0x1F00u && sizeof(px_pl_t) == 8u && PXM_PL_SLOTS == ST_NSLOT) ? 1 : -1];
/* test build 49: the presets' scenes (px_midi.h px_sc_tab), right after: "OPSC", ST_NSLOT entries of 10 bytes (the four
 * scenes' blocks on, the scene playing, the scenes stored), the CRC, 2 spare. A copy without it: no scenes */
#define ST_SC_OFF (ST_PL_OFF + ST_PL_LEN)              /* 0x1A08 */
#define ST_SC_LEN (4u + ST_NSLOT * 10u + 4u)
#define ST_SC_MAGIC 0x4353504Fu                        /* "OPSC" */
typedef char st_sc_check[(ST_SC_OFF + ST_SC_LEN <= 0x1F00u && sizeof(px_sc_t) == 10u && sizeof px_sc_tab == ST_NSLOT * 10u) ? 1 : -1];
static uint8_t st_pl_buf[ST_SC_LEN > ST_PL_LEN ? ST_SC_LEN : ST_PL_LEN];   /* (one buffer: boot's reads, a save's writes) */
static void st_sc_pack(uint8_t *b)
{
    const uint32_t T = 4u + ST_NSLOT * 10u, c = ota_crc16((const uint8_t *)px_sc_tab, ST_NSLOT * 10u, 0);
    b[0] = ST_SC_MAGIC & 0xFFu; b[1] = (ST_SC_MAGIC >> 8) & 0xFFu; b[2] = (ST_SC_MAGIC >> 16) & 0xFFu; b[3] = ST_SC_MAGIC >> 24;
    memcpy(b + 4, px_sc_tab, ST_NSLOT * 10u);
    b[T] = (uint8_t)c; b[T + 1u] = (uint8_t)(c >> 8); b[T + 2u] = b[T + 3u] = 0xFFu;
}
/* test build 50: the scenes' names (px_midi.h px_scn), after the scenes: two records of 26 presets each, "OPN1" (presets 1 to
 * 26) and "OPN2" (27 to 52): the magic, 26 x 4 names of 3 bytes (0: none), the CRC, 2 spare; each fits the shared buffer. A
 * save (st_save: the playing preset's) puts its names, a slot's FACTORY reset drops its own; the rest goes over from the copy in
 * use. Only the playing preset's are read (px_scn_load). A copy without them: none */
#define ST_SN_OFF (ST_SC_OFF + ST_SC_LEN)              /* 0x1C18 */
#define ST_SN_HALF 26u
#define ST_SN_LEN (4u + ST_SN_HALF * 12u + 4u)         /* 320 */
typedef char st_sn_check[(ST_SN_OFF + 2u * ST_SN_LEN <= 0x1F00u && ST_SN_LEN <= ST_PL_LEN && 2u * ST_SN_HALF == ST_NSLOT &&
                          sizeof px_scn == 12u) ? 1 : -1];
static uint8_t st_sn_ok;                               /* (the copy in use: its two records read right, bits 0 and 1) */
static uint8_t st_sn_put = 0xFFu, st_sn_drop = 0xFFu;  /* (the next commit: a save's slot, a reset's) */
static int st_sn_valid(uint32_t base, uint32_t h)      /* record h of a copy reads right (read into the shared buffer) */
{
    uint8_t *const b = st_pl_buf;
    const uint32_t T = 4u + ST_SN_HALF * 12u;
    if (!base || ota_fread(base + ST_SN_OFF + h * ST_SN_LEN, b, ST_SN_LEN)) return 0;
    if (b[0] != 'O' || b[1] != 'P' || b[2] != 'N' || b[3] != (uint8_t)('1' + h)) return 0;
    return (b[T] | b[T + 1u] << 8) == (ota_crc16(b + 4, ST_SN_HALF * 12u, 0) & 0xFFFFu);
}
static void st_sn_pack(uint8_t *b, uint32_t h)         /* record h for the copy being written */
{
    const uint32_t T = 4u + ST_SN_HALF * 12u;
    if (!st_sn_valid(st_cur, h)) memset(b, 0, ST_SN_LEN);   /* (none in the copy in use: none) */
    b[0] = 'O'; b[1] = 'P'; b[2] = 'N'; b[3] = (uint8_t)('1' + h);
    if (st_sn_put / ST_SN_HALF == h) memcpy(b + 4u + (st_sn_put % ST_SN_HALF) * 12u, px_scn, 12u);
    if (st_sn_drop / ST_SN_HALF == h) memset(b + 4u + (st_sn_drop % ST_SN_HALF) * 12u, 0, 12u);
    const uint32_t c = ota_crc16(b + 4, ST_SN_HALF * 12u, 0);
    b[T] = (uint8_t)c; b[T + 1u] = (uint8_t)(c >> 8); b[T + 2u] = b[T + 3u] = 0xFFu;
}
static void st_sn_pack0(uint8_t *b) { st_sn_pack(b, 0u); }
static void st_sn_pack1(uint8_t *b) { st_sn_pack(b, 1u); }
static void px_scn_load(uint32_t slot)                 /* the playing preset's names (load_preset, PX_SC_LOAD) */
{
    const uint32_t h = slot / ST_SN_HALF;
    memset(px_scn, 0, sizeof px_scn);
    if (slot < ST_NSLOT && (st_sn_ok >> h & 1u))
        (void)ota_fread(st_cur + ST_SN_OFF + h * ST_SN_LEN + 4u + (slot % ST_SN_HALF) * 12u, px_scn, sizeof px_scn);
    for (uint32_t i = 0; i < sizeof px_scn; i++) {     /* (what is not a letter the screen draws: none) */
        char *const c = &px_scn[0][0] + i;
        if ((uint8_t)*c < 0x20u || (uint8_t)*c > 0x7Eu) *c = 0;
    }
    memcpy(px_scn_saved, px_scn, sizeof px_scn);
}
static void st_sc_load(uint32_t base)                  /* the copy's scenes, or none */
{
    uint8_t *const b = st_pl_buf;
    const uint32_t T = 4u + ST_NSLOT * 10u;
    memset(px_sc_tab, 0, sizeof px_sc_tab);
    if (!base || ota_fread(base + ST_SC_OFF, b, ST_SC_LEN)) return;
    if ((b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24) != ST_SC_MAGIC) return;
    if ((b[T] | b[T + 1u] << 8) != (ota_crc16(b + 4, ST_NSLOT * 10u, 0) & 0xFFFFu)) return;
    memcpy(px_sc_tab, b + 4, sizeof px_sc_tab);
    for (uint32_t i = 0; i < ST_NSLOT; i++)            /* (an entry that does not read right: none) */
        if (px_sc_tab[i].cur >= PX_SC_N || (px_sc_tab[i].set & ~0x0Fu)) memset(&px_sc_tab[i], 0, sizeof px_sc_tab[i]);
}
static void st_pl_pack(uint8_t *b)
{
    const uint32_t T = 4u + PXM_PL_N * 8u, c = ota_crc16((const uint8_t *)mid_pl, PXM_PL_N * 8u, 0);
    b[0] = ST_PL_MAGIC & 0xFFu; b[1] = (ST_PL_MAGIC >> 8) & 0xFFu; b[2] = (ST_PL_MAGIC >> 16) & 0xFFu; b[3] = ST_PL_MAGIC >> 24;
    memcpy(b + 4, mid_pl, PXM_PL_N * 8u);
    b[T] = (uint8_t)c; b[T + 1u] = (uint8_t)(c >> 8);
    b[T + 2u] = px_inz_pack(); b[T + 3u] = (uint8_t)(px_inz_pack() ^ 0x5Au);
}
static void st_pl_load(uint32_t base)                  /* the copy's preset-own learned controls, or none */
{
    uint8_t *const b = st_pl_buf;
    uint32_t i, m, w, T;
    px_midi_pl_clear();
    px_inz_unpack(0);
    if (!base || ota_fread(base + ST_PL_OFF, b, ST_PL_LEN)) return;
    m = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
    w = m == ST_PL_MAGIC ? 8u : m == ST_PL2_MAGIC ? 6u : 0u;   /* (an entry's bytes) */
    if (!w) return;
    T = 4u + PXM_PL_N * w;
    if ((uint8_t)(b[T + 2u] ^ 0x5Au) == b[T + 3u]) px_inz_unpack(b[T + 2u]);
    if ((b[T] | b[T + 1u] << 8) != (ota_crc16(b + 4, PXM_PL_N * w, 0) & 0xFFFFu)) return;
    for (i = 0; i < PXM_PL_N; i++) {
        px_pl_t e = { 0, 0, 0, 0, 0, 0, 0u, 127u };    /* (47's: the pedal's whole travel) */
        memcpy(&e, b + 4u + i * w, w);
        if (e.slot != 0xFFu && px_midi_pl_valid(&e)) mid_pl[i] = e;
    }
}
#endif

static uint32_t st_slot_off(uint32_t base, uint32_t k)
{
    return k < ST_S0N ? base + 16u + k * ST_SLOT : base + 0x1000u + (k - ST_S0N) * ST_SLOT;
}
static uint32_t st1_slot_off(uint32_t base, uint32_t k)
{
    return k < ST1_S0N ? base + 16u + k * ST1_SLOT : base + 0x1000u + (k - ST1_S0N) * ST1_SLOT;
}
/* a format-1 slot -> format 2: the 11 old blocks copied over factory slot k (new block, order: defaults) */
static void st_from_v1(uint32_t k, const uint8_t *o)
{
    memset(st_slot[k], 0xFF, ST_SLOT);
    if (o[ST1_PSIZE] != ST_MARK) return;                       /* never saved: stays the factory preset */
    px_preset_t q;
    px_preset_factory(&q, (int)k);
    memcpy(q.name, o, 14);
    memcpy(q.on, o + 14, 11); q.on[PX_B_SYN] = 0;
    memcpy(q.type, o + 25, 11);
    memcpy(q.p, o + 36, 55);
    px_order_default(q.order);
    memcpy(st_slot[k], &q, sizeof q);
    st_slot[k][sizeof q] = ST_MARK;
}
/* format 1 / 2 -> 3: the saved presets' amp, drive and cab types moved to the v34 set (px_type_v34: the nearest one).
 * In RAM only; the flash gets it at the next save (until then it is redone at each boot). */
static void st_types_v34(void)
{
    static const uint8_t B[3] = {PX_B_DRIVE, PX_B_AMP, PX_B_CAB};
    for (uint32_t k = 0; k < ST_NSLOT; k++) {
        if (st_slot[k][sizeof(px_preset_t)] != ST_MARK) continue;
        px_preset_t *q = (px_preset_t *)(void *)st_slot[k];
        for (uint32_t i = 0; i < 3u; i++) q->type[B[i]] = (uint8_t)px_type_v34(B[i], q->type[B[i]]);
    }
}
static void st_level_v36(void)               /* saved before v36: no preset LEVEL yet (that byte was unused) = 0 dB */
{
    for (uint32_t k = 0; k < ST_NSLOT; k++) {
        if (st_slot[k][sizeof(px_preset_t)] != ST_MARK) continue;
        ((px_preset_t *)(void *)st_slot[k])->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    }
}
static void st_time_v35(void)
{
    for (uint32_t k = 0; k < ST_NSLOT; k++) {
        if (st_slot[k][sizeof(px_preset_t)] != ST_MARK) continue;
        px_preset_t *q = (px_preset_t *)(void *)st_slot[k];
        q->p[PX_B_DELAY][0] = (uint8_t)px_dly_time_v35(q->type[PX_B_DELAY], q->p[PX_B_DELAY][0]);
    }
}
static uint32_t st_crc(void)
{
    uint32_t k, c = 0;
    for (k = 0; k < ST_NSLOT; k++)
        c = ota_crc16(st_slot[k], ST_SLOT, c);
    return c;
}
/* reads copy `base` (format h->ver) into st_slot; returns its CRC as stored on flash */
static uint32_t st_read_copy(uint32_t base, const st_hdr_t *h)
{
    uint32_t k, c = 0;
    if (h->ver == 1u) {
        static uint8_t o[ST1_SLOT];
        memset(st_slot, 0xFF, sizeof st_slot);
        for (k = 0; k < ST1_NSLOT; k++) {
            if (ota_fread(st1_slot_off(base, k), o, ST1_SLOT)) return ~0u;
            c = ota_crc16(o, ST1_SLOT, c);
            st_from_v1(k, o);
        }
        return c;
    }
    for (k = 0; k < ST_NSLOT; k++)
        if (ota_fread(st_slot_off(base, k), st_slot[k], ST_SLOT)) return ~0u;
    return st_crc();
}

/* boot: the newest valid copy into st_slot (call with the flash readable; it may turn interrupts on) */
static void st_load(void)
{
    static const uint32_t BASE[2] = {ST_A, ST_B};
    uint32_t i, k, best = 0, bseq = 0;
    st_hdr_t h;
    memset(st_slot, 0xFF, sizeof st_slot);
    st_hdr_t bh;
    memset(&bh, 0, sizeof bh);
    for (i = 0; i < 2u; i++) {
        if (ota_fread(BASE[i], &h, sizeof h) || h.magic != ST_MAGIC) continue;
        if (!(((h.ver >= 2u && h.ver <= ST_VER) && h.nslot == ST_NSLOT) || (h.ver == 1u && h.nslot == ST1_NSLOT))) continue;
        if (st_read_copy(BASE[i], &h) == h.crc && (!best || (int32_t)(h.seq - bseq) > 0)) {
            best = BASE[i];
            bseq = h.seq;
            st_last = h.last;
            st_midi = h.pad[0];
            st_usb = h.pad[1];
#if PX_OWN_FX
            px_set_unpack(h.pad[2]);              /* test build 40: INPUT, BT POWER, TAILS (0 in every earlier save) */
#endif
            bh = h;
        }
    }
    (void)k;
    if (best) {                                   /* read the winner again (the loop left the other in st_slot) */
        st_read_copy(best, &bh);
        st_cur = best;
        st_seq = bseq;
    } else {
        memset(st_slot, 0xFF, sizeof st_slot);
        st_last = 0;
        st_midi = 0;
        st_usb = 0;
#if PX_OWN_FX
        px_set_unpack(0);
#endif
    }
    if (st_last >= ST_NSLOT) st_last = 0;
#if PX_OWN_FX
    st_map_load(best);                              /* test build 39: the controls learned on the pedal */
    st_pl_load(best);                               /* test build 46: and each preset's own */
    st_sc_load(best);                               /* test build 49: and their scenes */
    st_sn_ok = (uint8_t)(st_sn_valid(best, 0u) | st_sn_valid(best, 1u) << 1);   /* test build 50: their names' records */
#endif
    if (best && bh.ver < 3u) st_types_v34();        /* saved before v34: the old amp / drive / cab numbers */
    if (best && bh.ver < 4u) st_time_v35();         /* saved before v35: delay TIME on the 1 s scale */
    if (best && bh.ver < 5u) st_level_v36();        /* saved before v36: preset LEVEL = 0 dB */
}

static int st_saved(uint32_t k) { return k < ST_NSLOT && st_slot[k][sizeof(px_preset_t)] == ST_MARK; }

/* the preset in slot k: the saved one, else the factory one */
static void st_get(uint32_t k, px_preset_t *p)
{
    if (st_saved(k)) { memcpy(p, st_slot[k], sizeof *p); if (!px_order_valid(p->order)) px_order_default(p->order); }
    else px_preset_factory(p, (int)k);
}

/* audio off the flash: muted output, IRQ held, the second core between jobs */
static void st_audio_hold(int on)
{
    if (on) {
        a_mute = 1;
        for (int i = 0; i < 12; i++) { fm1_wdt_feed(); fm1_delay_ms(1); }   /* > 2 blocks: the DMA plays zeros */
        fm1_irq_mask(FM1_IRQ_ALNK0);
        a_collect();
    } else {
        a_irq_gap_skip();                       /* (the hold is not a late interrupt: px_audio.h) */
        fm1_irq_unmask(FM1_IRQ_ALNK0);
        a_mute = 0;
    }
}

#if PX_OWN_FX
/* a record in a copy being written: packed into the shared buffer, written, read back before the buffer packs the next (test
 * build 49: the scenes' record shares it with the learned controls') */
static int st_rec_write(uint32_t at, void (*pack)(uint8_t *), uint32_t len)
{
    static uint8_t chk[64];
    pack(st_pl_buf);
    if (fl_write(at, st_pl_buf, len)) return -3;
    for (uint32_t k = 0; k < len; k += sizeof chk) {
        const uint32_t n = len - k < sizeof chk ? len - k : (uint32_t)sizeof chk;
        if (ota_fread(at + k, chk, n) || memcmp(chk, st_pl_buf + k, n)) return -5;
    }
    return 0;
}
#endif
/* write the whole store as a new copy (the one not in use); 0 = ok */
static int st_commit(void)
{
    uint32_t base = st_cur == ST_A ? ST_B : ST_A, k;
    st_hdr_t h;
    int rc = 0;
    if (!st_ok) return -1;
    memset(&h, 0, sizeof h);
#if PX_OWN_FX
    st_usb = (uint8_t)((st_usb & 0x0Du) | (px_vol_at ? PX_ST_VOLAT : 0u) | (uint32_t)px_live << 4 | (px_sc_rule ? PX_ST_SCRULE : 0u) |
                       (px_sc_feet ? PX_ST_SCFEET : 0u));   /* (test build 49: PLAY VIEW as CAB left it, with whatever is
                                                  * written; 51: VOL PEDAL) */
#endif
    h.magic = ST_MAGIC; h.seq = st_seq + 1u; h.crc = (uint16_t)st_crc(); h.ver = ST_VER; h.nslot = ST_NSLOT; h.last = st_last; h.pad[0] = st_midi; h.pad[1] = st_usb;
#if PX_OWN_FX
    h.pad[2] = px_set_pack();                     /* test build 40: INPUT, BT POWER, TAILS */
#endif
    st_audio_hold(1);
    if (fl_erase4k(base, &k) || fl_erase4k(base + 0x1000u, &k)) rc = -2;
    for (k = ST_S0N; !rc && k < ST_NSLOT; k++)
        if (fl_write(st_slot_off(base, k), st_slot[k], ST_SLOT)) rc = -3;
#if PX_OWN_FX
    static uint8_t mb[ST_MAP_LEN];                /* test build 39: the learned controls, before the header like the slots */
    st_map_pack(mb);
    if (!rc && fl_write(base + ST_MAP_OFF, mb, ST_MAP_LEN)) rc = -3;
    if (!rc) rc = st_rec_write(base + ST_PL_OFF, st_pl_pack, ST_PL_LEN);   /* test build 46: each preset's own */
    if (!rc) rc = st_rec_write(base + ST_SC_OFF, st_sc_pack, ST_SC_LEN);   /* test build 49: their scenes */
    if (!rc) rc = st_rec_write(base + ST_SN_OFF, st_sn_pack0, ST_SN_LEN);   /* test build 50: the scenes' names */
    if (!rc) rc = st_rec_write(base + ST_SN_OFF + ST_SN_LEN, st_sn_pack1, ST_SN_LEN);
    st_sn_put = st_sn_drop = 0xFFu;
#endif
    if (!rc && fl_write(base + 16u, st_slot[0], ST_S0N * ST_SLOT)) rc = -3;   /* sector 0's slots: st_slot's rows are contiguous */
    if (!rc && fl_write(base, (const uint8_t *)&h, sizeof h)) rc = -4;      /* header last */
    if (!rc) {                                    /* read back */
        st_hdr_t r;
        static uint8_t chk[ST_SLOT];
        if (ota_fread(base, &r, sizeof r) || memcmp(&r, &h, sizeof h)) rc = -5;
        for (k = 0; !rc && k < ST_NSLOT; k++)
            if (ota_fread(st_slot_off(base, k), chk, ST_SLOT) || memcmp(chk, st_slot[k], ST_SLOT)) rc = -5;
#if PX_OWN_FX
        if (!rc && (ota_fread(base + ST_MAP_OFF, chk, ST_MAP_LEN) || memcmp(chk, mb, ST_MAP_LEN))) rc = -5;
#endif
    }
    st_audio_hold(0);
    if (!rc) { st_cur = base; st_seq = h.seq; }
#if PX_OWN_FX
    if (!rc) st_sn_ok = 3u;                       /* (test build 50: both name records just written and read back) */
#endif
    return rc;
}

/* save p to slot k (becomes the start slot) */
static int st_save(uint32_t k, const px_preset_t *p)
{
    if (k >= ST_NSLOT) return -1;
    memset(st_slot[k], 0xFF, ST_SLOT);
    memcpy(st_slot[k], p, sizeof *p);
    st_slot[k][sizeof *p] = ST_MARK;
    st_last = (uint8_t)k;
#if PX_OWN_FX
    st_sn_put = (uint8_t)k;                       /* (test build 50: the playing preset's scene names go with it) */
#endif
    return st_commit();
}

/* slot k back to its factory preset */
static int st_reset(uint32_t k)
{
    if (k >= ST_NSLOT) return -1;
    memset(st_slot[k], 0xFF, ST_SLOT);
#if PX_OWN_FX
    px_midi_pl_drop_slot(k);                      /* test build 46: its own learned controls go with its sound */
    memset(&px_sc_tab[k], 0, sizeof px_sc_tab[k]);   /* test build 49: and its scenes */
    st_sn_drop = (uint8_t)k;                      /* test build 50: and their names */
#endif
    return st_commit();
}

/* boot, after px_flash_boot and before px_usb_start, which presents the USB device as the settings say (interrupts off
 * until audio_start) */
static void px_store_boot(void)
{
    st_ok = px_flash_ok;
    if (st_ok) st_load();
#if PX_OWN_FX
    else { px_midi_map_clear(); px_midi_pl_clear(); px_inz_unpack(0); }   /* (test build 39: nothing learned without a store) */
#endif
    ST_IRQ_OFF();                                 /* (ota_fread turned them on) */
    cur_preset = st_last;
    load_preset(cur_preset);
    px_midi_cfg_unpack(&mid_cfg, st_midi);
    px_ua_on = st_usb & 1u;
#if PX_OWN_FX
    px_out_mode = (uint8_t)((st_usb & PX_ST_OUT_MASK) >> PX_ST_OUT_SH);   /* test build 42: OUTPUT */
    px_live = (uint8_t)((st_usb & PX_ST_VIEW) >> 4); /* test build 47: PLAY VIEW; 49: of three, and the scenes' two rows */
    if (px_live > 2u) px_live = 0;
    px_sc_rule = (uint8_t)!!(st_usb & PX_ST_SCRULE); px_sc_feet = (uint8_t)!!(st_usb & PX_ST_SCFEET);
    px_vol_at = (uint8_t)!!(st_usb & PX_ST_VOLAT);   /* test build 51: VOL PEDAL (0, as every store before it: the input) */
    PX_EXP_PLACE(px_vol_at);
    if (px_out_mode > 2u) px_out_mode = 0;
    a_in_gain_apply();                            /* test build 39: INPUT as stored; test build 40: TAILS to the engine */
    a_tails_apply();
    a_inz_apply();                                /* test build 46: IN-Z and PICKUP */
#endif
}
