/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — musical MIDI over USB: what a message means for the pedal, and what the pedal says back.
 * The map is documented in docs/MIDI.md. No hardware here (tests/midisim.c runs this file on a PC); the rings and
 * the pedal's state are in px_midi_io.h.
 *
 *   px_midi_decode   one 4-byte USB-MIDI event packet (from usb.c's input ring) -> a message, or "ignore"
 *   px_midi_cc_apply a Control Change -> the preset (and the MASTER volume)
 *   px_midi_tx_next  the next message the pedal sends so the host follows the pedal (Program Change, then CCs)
 *   px_midi_clock_*  MIDI clock -> a quarter-note time, for the delay TIME
 *
 * Needs px_dsp.h (px_preset_t, px_ntypes, px_dly_value_for_ms). SysEx never comes here: usb.c routes it to the
 * update / editor code, and only channel voice messages and clock reach the ring this file reads.
 */
#pragma once

/* ---- settings (one byte in the preset store's header, px_store.h): all-zero = the defaults ---- */
typedef struct {
    uint8_t chan;          /* 0 = omni (receive any channel; send on 1), 1..16 = that channel */
    uint8_t send;          /* 1 = the pedal sends Program Change / Control Change when its sound changes */
    uint8_t clock;         /* 1 = MIDI clock sets the delay TIME to a quarter note */
#if PX_OWN_FX
    uint8_t each;          /* test build 39: 1 = CC 8..19 flip on every message (a switch that alternates 127 and 0, one
                            * message a press); 0 = on value 0 only (a switch that sends 127, then 0 when let go) */
#endif
} px_midi_cfg_t;
#define PXM_CFG_NOSEND 0x20u
#define PXM_CFG_CLOCK  0x40u
#define PXM_CFG_EACH   0x80u   /* (the Bluetooth audio image's; other firmware leaves this bit 0 and ignores it) */
static uint8_t px_midi_cfg_pack(const px_midi_cfg_t *c)
{
#if PX_OWN_FX
    return (uint8_t)((c->chan & 31u) | (c->send ? 0u : PXM_CFG_NOSEND) | (c->clock ? PXM_CFG_CLOCK : 0u) | (c->each ? PXM_CFG_EACH : 0u));
#else
    return (uint8_t)((c->chan & 31u) | (c->send ? 0u : PXM_CFG_NOSEND) | (c->clock ? PXM_CFG_CLOCK : 0u));
#endif
}
static void px_midi_cfg_unpack(px_midi_cfg_t *c, uint8_t b)
{
    c->chan = (uint8_t)(b & 31u);
    if (c->chan > 16u) c->chan = 0;                    /* a byte this firmware never wrote: omni */
    c->send = (b & PXM_CFG_NOSEND) ? 0u : 1u;
    c->clock = (b & PXM_CFG_CLOCK) ? 1u : 0u;
#if PX_OWN_FX
    c->each = (b & PXM_CFG_EACH) ? 1u : 0u;
#endif
}
/* v42: USB AUDIO (docs/USB-AUDIO.md): 1 = the pedal is a USB sound card too. The MIDI page's fourth row and the editor
 * (USB_AUDIO) set it, the store keeps it (px_store.h, st_usb), px_usb.h presents the USB device with or without the
 * audio streams. Not in the Bluetooth test image, whose RAM is full: OP_UAUDIO 0 leaves the row and the streams out. */
#if OP_BLE_TEST
#define OP_UAUDIO 0
#else
#define OP_UAUDIO 1
#endif
static uint8_t px_ua_on;
#if PX_OWN_FX
/* test build 40: three settings of the page FX opens (SETTINGS) and of the editor (MAP), kept together in the preset store's
 * header (pad[2]; 0 in every earlier save = 0 dB, TAILS off, BT POWER HIGH):
 *   bits 0-4  INPUT, the guitar's level into the chain, -10..+10 dB (5-bit two's complement), as the official firmware's
 *             Input Gain (a digital multiply there too): px_audio.h applies it to the input
 *   bits 5-6  BT POWER: 0 HIGH (the SDK's levels, as before), 1 MID, 2 LOW: the radio's transmit power (bt/bt_a2dp.c)
 *   bit 7     TAILS: the delay and the reverb ring out when they go off and across a preset change (engine/px_dsp.c) */
#define PX_IN_DB_MIN (-10)
#define PX_IN_DB_MAX 10
static int8_t px_in_db;
static uint8_t px_bt_pwr, px_tails;
static int px_in_db_clamp(int v) { return v < PX_IN_DB_MIN ? PX_IN_DB_MIN : v > PX_IN_DB_MAX ? PX_IN_DB_MAX : v; }
static uint8_t px_set_pack(void)
{
    return (uint8_t)(((unsigned)px_in_db & 0x1Fu) | (unsigned)(px_bt_pwr & 3u) << 5 | (px_tails ? 0x80u : 0u));
}
static void px_set_unpack(uint8_t b)
{
    int v = b & 0x1F;
    if (v & 0x10) v -= 32;
    px_in_db = (int8_t)px_in_db_clamp(v);
    px_bt_pwr = (uint8_t)((b >> 5) & 3u);
    if (px_bt_pwr > 2u) px_bt_pwr = 0;
    px_tails = (uint8_t)(b >> 7);
}
#endif
/* the pedal's MIDI state, shared by the screen (ui.h) and the glue (px_midi_io.h) */
static px_midi_cfg_t mid_cfg;           /* all-zero until the store is read: omni, send on, clock off */
static uint8_t mid_usb_up;              /* a USB host is connected */
static uint8_t mid_last[3];
static uint8_t mid_c1_off;              /* v41: the second core stopped: 1 misread, 2 late, 3 barred, 4 fault, 5 silent, 0xFF other; 0 runs */             /* the last message received: type (PXM_*), data 1, data 2 */
static uint32_t px_midi_tx_chan(const px_midi_cfg_t *c) { return c->chan ? (uint32_t)c->chan - 1u : 0u; }

/* ---- the Control Change map (docs/MIDI.md) ---- */
enum { PXM_NONE, PXM_ON, PXM_TOGGLE, PXM_TYPE, PXM_PARAM, PXM_MASTER };
typedef struct { uint8_t kind, blk, k; } px_cc_t;
#define PXM_CC_MASTER 7u
#define PXM_CC_TOGGLE0 8u                              /* 8..19: block b flips on value 0, in the block order of 20..31 */
#define PXM_CC_ON0    20u                              /* 20..31: block b on/off */
#define PXM_CC_TYPE0  52u                              /* 52..63: block b model */
static uint32_t px_cc_param(uint32_t b, uint32_t k)    /* 60 settings over three runs of free numbers (38, Data Entry LSB, skipped) */
{
    uint32_t i = b * PX_NPAR + k;
    return i < 5u ? 33u + i : i < 18u ? 34u + i : i < 46u ? 70u + (i - 18u) : 102u + (i - 46u);
}
static uint32_t px_cc_of(const px_cc_t *t)
{
    switch (t->kind) {
    case PXM_ON:     return PXM_CC_ON0 + t->blk;
    case PXM_TOGGLE: return PXM_CC_TOGGLE0 + t->blk;
    case PXM_TYPE:   return PXM_CC_TYPE0 + t->blk;
    case PXM_PARAM:  return px_cc_param(t->blk, t->k);
    case PXM_MASTER: return PXM_CC_MASTER;
    }
    return 0;
}
static px_cc_t px_cc_tab[128];                         /* the inverse of px_cc_of, built once */
static uint8_t px_cc_tab_ok;
static void px_cc_build(void)
{
    px_cc_t t;
    uint32_t b, k;
    if (px_cc_tab_ok) return;
    for (b = 0; b < PX_NBLK; b++) {
        t.blk = (uint8_t)b; t.k = 0;
        t.kind = PXM_ON;   px_cc_tab[px_cc_of(&t)] = t;
        t.kind = PXM_TOGGLE; px_cc_tab[px_cc_of(&t)] = t;
        t.kind = PXM_TYPE; px_cc_tab[px_cc_of(&t)] = t;
        for (k = 0; k < PX_NPAR; k++) { t.kind = PXM_PARAM; t.k = (uint8_t)k; px_cc_tab[px_cc_of(&t)] = t; }
    }
    t.kind = PXM_MASTER; t.blk = t.k = 0;
    px_cc_tab[px_cc_of(&t)] = t;
    px_cc_tab_ok = 1;
}

#if PX_OWN_FX
/* ---- test build 39: controls learned on the pedal (docs/MIDI.md, "Learned on the pedal") ----
 * A Control Change number from a controller (the footswitch over Bluetooth, or one over USB) is read as one of the pedal's
 * own numbers before anything else: a switch becomes a block's flip, the next or the previous preset, a tap of the delay's
 * tempo or the pad's catch; a knob or a pedal becomes a setting or the expression pedal. So a footswitch whose numbers
 * cannot be changed still drives the pedal, and it does without the editor open. Up to PXM_MAP_N, kept with the presets
 * (px_store.h). How a switch's messages count:
 *   PXM_MAP_PRESS  a value of 64 and up is a press, a value below it the release (a switch that sends 127, then 0)
 *   PXM_MAP_EACH   every message is a press (a switch that alternates 127 and 0, one message a press)
 *   PXM_MAP_VALUE  the value passes as it is (a knob, an expression pedal, or a number the pedal reads by its value) */
#define PXM_MAP_N 8u
enum { PXM_MAP_PRESS, PXM_MAP_EACH, PXM_MAP_VALUE };
typedef struct { uint8_t src, dst, mode; } px_map_t;     /* src 0xFF: an empty entry */
static px_map_t mid_map[PXM_MAP_N];
static uint32_t mid_in_n;                              /* the musical messages received since boot (the editor's monitor) */
#define PXM_CC_PAD  64u                                /* (px_midi_io.h: 64 catches a SYN PAD's chord, 116 taps the tempo, */
#define PXM_CC_NEXT 118u                               /* 117 is the expression pedal) 118 / 119 at 64 and up: the next / */
#define PXM_CC_PREV 119u                               /* the previous preset, as the footswitches */
static void px_midi_map_clear(void) { memset(mid_map, 0xFF, sizeof mid_map); }
/* a number the pedal reads: its map, or one of the image's own (64, 116, 117, 118, 119) */
static int px_midi_known(uint32_t cc)
{
    px_cc_build();
    return cc < 128u && (px_cc_tab[cc].kind != PXM_NONE || cc == PXM_CC_PAD || cc == 116u || cc == 117u ||
                         cc == PXM_CC_NEXT || cc == PXM_CC_PREV);
}
static uint32_t px_midi_map_count(void)                /* the entries in use */
{
    uint32_t i, n = 0;
    for (i = 0; i < PXM_MAP_N; i++) n += mid_map[i].src < 128u && px_midi_known(mid_map[i].dst);
    return n;
}
static int px_midi_map_valid(const px_map_t *e)        /* an entry as stored: empty, or a usable one */
{
    if (e->src == 0xFFu) return 1;
    return e->src < 128u && e->mode <= PXM_MAP_VALUE && px_midi_known(e->dst);
}
/* a learned control's message, read as the pedal's own: 1 = go on with *cc / *val (rewritten when learned), 0 = nothing to
 * do (a switch's release) */
static int px_midi_map_in(uint32_t *cc, uint32_t *val)
{
    uint32_t i;
    for (i = 0; i < PXM_MAP_N; i++) {
        const px_map_t *e = &mid_map[i];
        if (e->src != *cc || !px_midi_known(e->dst)) continue;
        *cc = e->dst;
        if (e->mode == PXM_MAP_VALUE) return 1;
        if (e->mode == PXM_MAP_PRESS && *val < 64u) return 0;
        /* a press: what the pedal's own number does for one (a flip happens on 0, everything else on 64 and up) */
        *val = e->dst >= PXM_CC_TOGGLE0 && e->dst < PXM_CC_TOGGLE0 + PX_NBLK ? 0u : 127u;
        return 1;
    }
    return 1;
}
#endif

/* 0..100 <-> 0..127; a model number <-> the middle of its slice of 0..127 */
static uint32_t pxm_to_cc(uint32_t v) { return (v * 127u + 50u) / 100u; }
static uint32_t pxm_from_cc(uint32_t c) { return (c * 100u + 63u) / 127u; }
static uint32_t pxm_type_to_cc(uint32_t t, uint32_t n) { return (t * 128u + 64u) / n; }
static uint32_t pxm_type_from_cc(uint32_t c, uint32_t n) { return c * n / 128u; }

/* ---- a received message ---- */
enum { PXM_IGNORE, PXM_PC, PXM_CC, PXM_CLOCK, PXM_START, PXM_STOP };
typedef struct { uint8_t type, d1, d2; } px_midi_msg_t;

/* One USB-MIDI event packet: byte 0 = cable number << 4 | code index, bytes 1..3 = the MIDI message. Only the
 * channel voice messages the map uses and the realtime clock / transport bytes pass; everything else is ignored.
 * A message on another channel than the setting is ignored (omni: any). */
static px_midi_msg_t px_midi_decode(const px_midi_cfg_t *cfg, uint32_t pkt)
{
    px_midi_msg_t m = {PXM_IGNORE, 0, 0};
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0xFFu, d2 = (pkt >> 24) & 0xFFu;
    if (cin == 0xFu) {                                 /* a single byte: realtime */
        if (st == 0xF8u) m.type = PXM_CLOCK;
        else if (st == 0xFAu || st == 0xFBu) m.type = PXM_START;
        else if (st == 0xFCu) m.type = PXM_STOP;
        return m;
    }
    if (cin != 0xBu && cin != 0xCu) return m;          /* not a Control Change or a Program Change */
    if ((st >> 4) != cin || (d1 & 0x80u)) return m;    /* the status does not match the code index, or bad data */
    if (cin == 0xBu && (d2 & 0x80u)) return m;
    if (cfg->chan && (st & 15u) != (uint32_t)cfg->chan - 1u) return m;
    m.type = cin == 0xCu ? PXM_PC : PXM_CC;
    m.d1 = (uint8_t)d1;
    m.d2 = (uint8_t)(cin == 0xBu ? d2 : 0);
    return m;
}

/* A Control Change on the preset. Returns 1 when the sound changed. `master` (0..100) is set for CC 7 (a change
 * is reported through *master_set). Numbers outside the map change nothing (modulation wheel, sustain, "all notes
 * off"...). */
static int px_midi_cc_apply(px_preset_t *p, uint32_t cc, uint32_t val, uint32_t *master, int *master_set)
{
    px_cc_t t;
    uint32_t v;
    px_cc_build();
    if (cc > 127u || val > 127u) return 0;
    t = px_cc_tab[cc];
    switch (t.kind) {
    case PXM_ON:
        v = val >= 64u;
        if (p->on[t.blk] == v) return 0;
        p->on[t.blk] = (uint8_t)v;
        return 1;
    case PXM_TOGGLE:                                   /* the pedal holds the state, so a preset change cannot put a footswitch out of step */
#if PX_OWN_FX
        if (val && !mid_cfg.each) return 0;            /* (test build 39: EACH PRESS, every message flips) */
#else
        if (val) return 0;
#endif
        p->on[t.blk] = !p->on[t.blk];
        return 1;
    case PXM_TYPE:
        v = pxm_type_from_cc(val, (uint32_t)px_ntypes(t.blk));
        if (p->type[t.blk] == v) return 0;
        p->type[t.blk] = (uint8_t)v;
        return 1;
    case PXM_PARAM:
        v = pxm_from_cc(val);
        if (p->p[t.blk][t.k] == v) return 0;
        p->p[t.blk][t.k] = (uint8_t)v;
        return 1;
    case PXM_MASTER:
        *master = pxm_from_cc(val);
        *master_set = 1;
        return 0;
    }
    return 0;
}

/* ---- what the pedal sends: it follows the preset it plays ---- */
typedef struct {
    px_preset_t seen;          /* the sound the host was last told about */
    uint8_t slot;              /* the slot it was last told about (Program Change) */
    uint8_t valid;             /* 0 until the first sync after boot: nothing is sent for the sound we start with */
} px_midi_tx_t;

/* Take the pedal's present state as already known to the host (after a message received from it, at boot). */
static void px_midi_tx_sync(px_midi_tx_t *t, const px_preset_t *cur, uint32_t slot)
{
    t->seen = *cur;
    t->slot = (uint8_t)slot;
    t->valid = 1;
}

/* The next message to send as a USB-MIDI event packet (cable 0), 0 = nothing to say. A slot change is a Program
 * Change and tells the host the whole new sound at once (no CC burst); after that, each changed setting is one
 * Control Change, in the order on / model / settings of the blocks. Call again while the ring has room. */
static uint32_t px_midi_tx_next(px_midi_tx_t *t, const px_midi_cfg_t *cfg, const px_preset_t *cur, uint32_t slot)
{
    uint32_t ch = px_midi_tx_chan(cfg), b, k;
    px_cc_t c;
    if (!t->valid) { px_midi_tx_sync(t, cur, slot); return 0; }
    if (!cfg->send) { px_midi_tx_sync(t, cur, slot); return 0; }
    if (t->slot != slot) {
        px_midi_tx_sync(t, cur, slot);
        return 0xCu | ((0xC0u | ch) << 8) | (slot << 16);
    }
    px_cc_build();
    for (b = 0; b < PX_NBLK; b++) {
        if (cur->on[b] != t->seen.on[b]) {
            t->seen.on[b] = cur->on[b];
            c.kind = PXM_ON; c.blk = (uint8_t)b; c.k = 0;
            return 0xBu | ((0xB0u | ch) << 8) | (px_cc_of(&c) << 16) | ((cur->on[b] ? 127u : 0u) << 24);
        }
        if (cur->type[b] != t->seen.type[b]) {
            t->seen.type[b] = cur->type[b];
            c.kind = PXM_TYPE; c.blk = (uint8_t)b; c.k = 0;
            return 0xBu | ((0xB0u | ch) << 8) | (px_cc_of(&c) << 16) |
                   (pxm_type_to_cc(cur->type[b], (uint32_t)px_ntypes((int)b)) << 24);
        }
        for (k = 0; k < PX_NPAR; k++) {
            if (cur->p[b][k] != t->seen.p[b][k]) {
                t->seen.p[b][k] = cur->p[b][k];
                c.kind = PXM_PARAM; c.blk = (uint8_t)b; c.k = (uint8_t)k;
                return 0xBu | ((0xB0u | ch) << 8) | (px_cc_of(&c) << 16) | (pxm_to_cc(cur->p[b][k]) << 24);
            }
        }
    }
    return 0;
}

/* ---- MIDI clock -> a quarter note ----
 * 24 clocks per quarter note. The time of 24 clocks is measured from the first of a run to the 25th (one run is
 * one quarter note), so a timestamp's jitter of a millisecond or two is spread over the whole note. A run that is
 * broken (a gap of more than 100 ms between two clocks, or a Stop) starts over. A time is reported once two runs in
 * a row agree within 3 ms, so a drifting host does not make the delay time jump. */
typedef struct { uint32_t first_ms, last_ms, n, prev_q; } px_midi_clock_t;
static void px_midi_clock_reset(px_midi_clock_t *c) { c->n = 0; c->prev_q = 0; }
/* feed one clock at time `ms`; returns the quarter note in ms (200..2000), or 0 */
static uint32_t px_midi_clock_tick(px_midi_clock_t *c, uint32_t ms)
{
    uint32_t q;
    if (c->n && ms - c->last_ms > 100u) c->n = 0;
    c->last_ms = ms;
    if (c->n == 0) { c->first_ms = ms; c->n = 1; return 0; }
    if (++c->n < 25u) return 0;
    q = ms - c->first_ms;
    c->first_ms = ms;
    c->n = 1;
    if (q < 200u || q > 2000u) { c->prev_q = 0; return 0; }
    if (c->prev_q && (q > c->prev_q ? q - c->prev_q : c->prev_q - q) <= 3u) { c->prev_q = q; return q; }
    c->prev_q = q;
    return 0;
}
