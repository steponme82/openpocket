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
 * pad[1] (v42): bit 0 = USB AUDIO on (px_midi.h, px_ua_on); the other bits 0. 0 = off, as every earlier save wrote */
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
 * previous copy whole, its learned controls with it. */
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
    b[6u + PXM_MAP_N * 3u] = b[7u + PXM_MAP_N * 3u] = 0;
}
static void st_map_load(uint32_t base)                 /* the copy's learned controls, or none */
{
    uint8_t b[ST_MAP_LEN];
    uint32_t i, c;
    px_midi_map_clear();
    if (!base || ota_fread(base + ST_MAP_OFF, b, ST_MAP_LEN)) return;
    if ((b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24) != ST_MAP_MAGIC) return;
    c = ota_crc16(b + 4, PXM_MAP_N * 3u, 0);
    if ((b[4u + PXM_MAP_N * 3u] | b[5u + PXM_MAP_N * 3u] << 8) != (c & 0xFFFFu)) return;
    for (i = 0; i < PXM_MAP_N; i++) if (!px_midi_map_valid((const px_map_t *)(b + 4u + i * 3u))) return;
    memcpy(mid_map, b + 4, PXM_MAP_N * 3u);
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

/* write the whole store as a new copy (the one not in use); 0 = ok */
static int st_commit(void)
{
    uint32_t base = st_cur == ST_A ? ST_B : ST_A, k;
    st_hdr_t h;
    int rc = 0;
    if (!st_ok) return -1;
    memset(&h, 0, sizeof h);
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
    return st_commit();
}

/* slot k back to its factory preset */
static int st_reset(uint32_t k)
{
    if (k >= ST_NSLOT) return -1;
    memset(st_slot[k], 0xFF, ST_SLOT);
    return st_commit();
}

/* boot, after px_flash_boot and before px_usb_start, which presents the USB device as the settings say (interrupts off
 * until audio_start) */
static void px_store_boot(void)
{
    st_ok = px_flash_ok;
    if (st_ok) st_load();
#if PX_OWN_FX
    else px_midi_map_clear();                     /* (test build 39: nothing learned without a store) */
#endif
    ST_IRQ_OFF();                                 /* (ota_fread turned them on) */
    cur_preset = st_last;
    load_preset(cur_preset);
    px_midi_cfg_unpack(&mid_cfg, st_midi);
    px_ua_on = st_usb & 1u;
#if PX_OWN_FX
    a_in_gain_apply();                            /* test build 39: INPUT as stored; test build 40: TAILS to the engine */
    a_tails_apply();
#endif
}
