/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the NAM user models in flash (v37).
 *
 * 16 slots of 8 KiB at 0xC0000..0xE0000 (below the update loader's staging area, above the app area; the update
 * loader leaves this range alone: loader/ldr_core.c LDR_NAM_LO/HI). A slot: a 64-byte header, then the 1871 float
 * weights of an A2-Lite network in the .nam file's order (engine/px_nam.h), 7548 bytes in all:
 *   magic "OPNM", version 1, 1871, name (11 chars + 0), level trim (0.01 dB), CRC16 of the weights.
 * The web editor sends a model in pieces (NAM_WRITE) into nm_stage, then NAM_COMMIT checks it (every weight finite,
 * the CRC) and writes the slot: two sectors erased, programmed, read back, with the audio held as for a preset save.
 * The names and levels are read once at boot into RAM (the screen asks for them often); the weights are read from
 * the flash only when a preset picks the model (px_engine_set, called with the audio held: the flash cannot be read
 * through SPI while the second core runs the engine from XIP).
 */
#pragma once

#define NM_BASE   0xC0000u
#define NM_SLOT   0x2000u
#define NM_MAGIC  0x4D4E504Fu                    /* "OPNM" */
#define NM_HDR    64u
#define NM_BYTES  (NM_HDR + PXN_NW * 4u)         /* 7548 */
#define NM_CHUNK  112u                           /* bytes per NAM_WRITE (7-bit packed: 128) */

typedef struct {
    uint32_t magic;
    uint16_t ver, nw;
    char name[12];
    int16_t level_cdb;                           /* level trim, 0.01 dB */
    uint16_t crc;                                /* CRC16 (ota_crc16) of the weights */
    uint8_t pad[NM_HDR - 24u];
} nm_hdr_t;
typedef char nm_hdr_check[sizeof(nm_hdr_t) == NM_HDR ? 1 : -1];

#if OP_BLE_TEST   /* the Bluetooth test image: in the SRAM below the RAM (app.ld .bss_lo); what it holds is checked before use */
static uint8_t nm_stage[NM_BYTES] __attribute__((aligned(4), section(".bss.lo.nm_stage")));
#else
static uint8_t nm_stage[NM_BYTES] __attribute__((aligned(4)));
#endif
#ifdef OTA_SCRATCH   /* the update's staging (ota.c ota_stage): ota_session blocks the main loop, so no upload runs meanwhile */
typedef char ota_scratch_check[sizeof nm_stage >= 0x400u + 512u ? 1 : -1];
static uint8_t *ota_scratch(void) { return nm_stage; }
#endif
static struct { uint8_t ok; char name[12]; float level; } nm_cache[PX_NAM_NUSER];
static uint32_t nm_gen;                          /* bumps when a slot changes */

static int nm_hdr_ok(const nm_hdr_t *h)
{
    return h->magic == NM_MAGIC && h->ver == 1u && h->nw == PXN_NW;
}
static void nm_name_clean(char *d, const char *s)
{
    uint32_t i;
    for (i = 0; i < 11u && s[i]; i++) d[i] = (s[i] >= 32 && s[i] <= 126) ? s[i] : '?';
    for (; i < 12u; i++) d[i] = 0;
}
/* boot (and after each write): the headers into nm_cache */
static void nm_scan(void)
{
    nm_hdr_t h;
    for (uint32_t s = 0; s < PX_NAM_NUSER; s++) {
        nm_cache[s].ok = 0;
        if (ota_fread(NM_BASE + s * NM_SLOT, &h, sizeof h) || !nm_hdr_ok(&h)) continue;
        nm_cache[s].ok = 1;
        nm_name_clean(nm_cache[s].name, h.name);
        nm_cache[s].level = (float)h.level_cdb * 0.01f;
    }
    nm_gen++;
}

/* the engine's hook (engine/px_dsp.h): name and level from the cache, the weights from the flash (checked) */
int px_nam_user(int slot, char *name12, float *level_db, float *w)
{
    if (slot < 0 || slot >= (int)PX_NAM_NUSER || !nm_cache[slot].ok) return 0;
    memcpy(name12, nm_cache[slot].name, 12);
    *level_db = nm_cache[slot].level;
    if (w) {
        nm_hdr_t h;
        uint32_t off = NM_BASE + (uint32_t)slot * NM_SLOT;
        if (ota_fread(off, &h, sizeof h) || !nm_hdr_ok(&h)) return 0;
        if (ota_fread(off + NM_HDR, w, PXN_NW * 4u)) return 0;
        if ((uint16_t)ota_crc16((const uint8_t *)w, PXN_NW * 4u, 0) != h.crc) return 0;
    }
    return 1;
}

/* 7-bit unpacking: groups of 8 bytes (a byte of high bits, then 7 bytes), as the editor packs them */
static uint32_t nm_unpack7(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t max)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i + 1u < n && o < max; i += 8u) {
        uint8_t hi = s[i];
        for (uint32_t j = 0; j < 7u && i + 1u + j < n && o < max; j++)
            d[o++] = (uint8_t)(s[i + 1u + j] | ((hi >> j & 1u) << 7));
    }
    return o;
}

/* NAM_WRITE: piece `idx` (NM_CHUNK bytes each) of the staging buffer; 0 = ok */
static int nm_write_piece(uint32_t idx, const uint8_t *packed, uint32_t n)
{
    uint32_t off = idx * NM_CHUNK;
    if (off >= NM_BYTES) return 1;
    uint32_t want = NM_BYTES - off < NM_CHUNK ? NM_BYTES - off : NM_CHUNK;
    return nm_unpack7(packed, n, nm_stage + off, want) == want ? 0 : 1;
}

/* NAM_COMMIT: the staged model into slot s. 0 ok, 1 invalid, 2 flash */
static int nm_commit(uint32_t s)
{
    nm_hdr_t *h = (nm_hdr_t *)nm_stage;
    const uint32_t *w = (const uint32_t *)(nm_stage + NM_HDR);
    if (s >= PX_NAM_NUSER || !nm_hdr_ok(h)) return 1;
    for (uint32_t i = 0; i < PXN_NW; i++) if ((w[i] & 0x7F800000u) == 0x7F800000u) return 1;   /* NaN / Inf */
    if ((uint16_t)ota_crc16(nm_stage + NM_HDR, PXN_NW * 4u, 0) != h->crc) return 1;
    nm_name_clean(h->name, h->name);
    uint32_t base = NM_BASE + s * NM_SLOT, k;
    int rc = 0;
    st_audio_hold(1);
    if (fl_erase4k(base, &k) || fl_erase4k(base + 0x1000u, &k)) rc = 2;
    if (!rc && fl_write(base + NM_HDR, nm_stage + NM_HDR, NM_BYTES - NM_HDR)) rc = 2;
    if (!rc && fl_write(base, nm_stage, NM_HDR)) rc = 2;     /* header last: a cut write leaves no valid slot */
    if (!rc) {                                               /* read back */
        static uint8_t chk[256];
        for (uint32_t o = 0; !rc && o < NM_BYTES; o += sizeof chk) {
            uint32_t m = NM_BYTES - o < sizeof chk ? NM_BYTES - o : sizeof chk;
            if (ota_fread(base + o, chk, m) || memcmp(chk, nm_stage + o, m)) rc = 2;
        }
    }
    st_audio_hold(0);
    nm_scan();
    return rc;
}

#if PX_OWN_FX
/* test build 40: NAM_RENAME: slot s's name only, so any capture can get a name a player reads, from any browser (the web
 * editor could rename only a capture its own library held). The slot is read back into the staging buffer (the audio held
 * meanwhile, as for any read of a model), checked as a commit checks it, the name set, and written again by nm_commit.
 * An upload in progress would lose its staged pieces: the editor does one thing at a time. 0 ok, 1 invalid (an empty or
 * damaged slot, an empty name), 2 flash */
static int nm_rename(uint32_t s, const char *name, uint32_t n)
{
    nm_hdr_t *h = (nm_hdr_t *)nm_stage;
    uint32_t i;
    if (s >= PX_NAM_NUSER || !n || !name[0]) return 1;
    st_audio_hold(1);
    const int rd = ota_fread(NM_BASE + s * NM_SLOT, nm_stage, NM_BYTES);
    st_audio_hold(0);
    if (rd || !nm_hdr_ok(h) || (uint16_t)ota_crc16(nm_stage + NM_HDR, PXN_NW * 4u, 0) != h->crc) return 1;
    for (i = 0; i < sizeof h->name; i++) h->name[i] = 0;
    for (i = 0; i < 11u && i < n && name[i]; i++) h->name[i] = name[i];
    return nm_commit(s);
}
#endif

/* NAM_ERASE: slot s emptied. 0 ok, 1 invalid, 2 flash */
static int nm_erase(uint32_t s)
{
    uint32_t k;
    int rc = 0;
    if (s >= PX_NAM_NUSER) return 1;
    st_audio_hold(1);
    if (fl_erase4k(NM_BASE + s * NM_SLOT, &k) || fl_erase4k(NM_BASE + s * NM_SLOT + 0x1000u, &k)) rc = 2;
    st_audio_hold(0);
    nm_scan();
    return rc;
}
