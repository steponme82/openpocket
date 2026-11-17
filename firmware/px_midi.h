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
#if PX_OWN_FX
#include <math.h>                                     /* (test build 46: IN-Z's filter, px_inz_coefs) */
#endif

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
 * audio streams. Not in the BLE test image, whose RAM is full: OP_UAUDIO 0 leaves the row and the streams out. The
 * Bluetooth audio image has it since test build 43 (with USB SEND, USB RETURN and GUITAR below) */
#if OP_BLE_TEST && !OP_BT_AUDIO
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
/* test build 46: GUITAR TONE (46's IN-Z) and PICKUP (SETTINGS), the guitar's pickup as the input would load it, for every preset
 * (px_audio.h a_inz_apply: the pedal's own input stays what it is, a filter does what the load would). GUITAR TONE (px_inz): 0
 * NATURAL (the input as it is, about 1 MOhm: nothing done), then lower loads, the pickup's resonant peak and then its top going:
 * 1 SMOOTH (230 kOhm), 2 WARM (136K), 3 WARMER (90K), 4 PUNCHY (70K), 5 DARK (32K), 6 DARKER (22K), 7 MUDDY (10K); test build 47:
 * 8 BRIGHT (a buffer at the guitar: the cable's capacitance taken off, 0.4 of it left), and the names (46 showed the loads in
 * ohms). PICKUP, the pickup the filter assumes: 0 SINGLE, 1 HUMBUCK, 2 P90. Kept in the store's "OPP2" record's last two bytes
 * (px_store.h: px_inz_pack, the same ^ 0x5A; 46's "OPPL" packed 3 bits of IN-Z and PICKUP at bit 3) */
#define PX_INZ_N 9u
static uint8_t px_inz, px_pickup;
static const uint8_t PX_INZ_ORDER[PX_INZ_N] = {8, 0, 1, 2, 3, 4, 5, 6, 7};   /* (the knob's order: BRIGHT, NATURAL, .., MUDDY) */
static const char *const PX_INZ_NAME[PX_INZ_N] = {"NATURAL", "SMOOTH", "WARM", "WARMER", "PUNCHY", "DARK", "DARKER", "MUDDY", "BRIGHT"};
static uint8_t px_inz_pack(void) { return (uint8_t)((px_inz & 15u) | (px_pickup & 3u) << 4); }
static void px_inz_unpack(uint8_t b)
{
    px_inz = (uint8_t)(b & 15u);
    px_pickup = (uint8_t)((b >> 4) & 3u);
    if (px_inz >= PX_INZ_N || px_pickup > 2u || (b & 0xC0u)) px_inz = px_pickup = 0;   /* (a byte no build writes: as it is) */
}
static void px_inz_step(int dd)                        /* GUITAR TONE's knob, along PX_INZ_ORDER, round */
{
    uint32_t i = 0;
    while (i < PX_INZ_N && PX_INZ_ORDER[i] != px_inz) i++;
    px_inz = PX_INZ_ORDER[(i + (dd > 0 ? 1u : PX_INZ_N - 1u)) % PX_INZ_N];
}
/* IN-Z's filter at sample rate sr: c = b0 b1 b2 a1 a2; 0 when it is off (1M). A passive pickup is a coil (L, its resistance R)
 * with the cable's and its own capacitance (C) across it, into the input's resistance RL: H = 1 / (LC s^2 + (RC + L/RL) s +
 * 1 + R/RL). The pedal's input is about 1 MOhm; a lower RL takes the pickup's resonant peak down (6 to 14 dB at 230K to 70K)
 * and, at 32K and below, the top. One biquad gives what RL would have given: H(RL) / H(1 M), bilinear, prewarped at the
 * pickup's resonance, its lows at the level they had (the load's tone, not its level: a real 10K would also take 5.6 dB off
 * a humbucker's lows, which a setting for every preset should not). The pickup assumed: SINGLE 2.6 H, 6 kOhm, 650 pF (3.9 kHz); HUMBUCK 4.5 H, 9 kOhm, 750 pF (2.7 kHz); P90
 * 6.5 H, 8 kOhm, 700 pF (2.4 kHz): one model for all was 11 dB off at another pickup's resonance (px_audio.h runs it) */
static const float PX_PU_L[3] = {2.6f, 4.5f, 6.5f}, PX_PU_R[3] = {6000.0f, 9000.0f, 8000.0f}, PX_PU_C[3] = {650e-12f, 750e-12f, 700e-12f};
static const float PX_INZ_RL[PX_INZ_N] = {1e6f, 230e3f, 136e3f, 90e3f, 70e3f, 32e3f, 22e3f, 10e3f, 1e6f};
static const float PX_INZ_CK[PX_INZ_N] = {1, 1, 1, 1, 1, 1, 1, 1, 0.4f};   /* (the capacitance left: BRIGHT's buffer) */
static int px_inz_coefs(float sr, float *c)
{
    if (!px_inz || px_inz >= PX_INZ_N || px_pickup > 2u) return 0;
    const float L = PX_PU_L[px_pickup], R = PX_PU_R[px_pickup], C = PX_PU_C[px_pickup], RL = PX_INZ_RL[px_inz];
    const float Ct = C * PX_INZ_CK[px_inz];
    const float n2 = L * C, n1 = R * C + L / 1e6f, n0 = 1.0f + R / 1e6f;   /* (the input as it is: 1 MOhm) */
    const float d2 = L * Ct, d1 = R * Ct + L / RL, d0 = 1.0f + R / RL;
    /* (prewarped at the pickup's resonance. Test build 47: BRIGHT's own, higher, for the half it moves: the two halves of the
     * same order, the bilinear's (1 + z^-1)^2 cancels, and the lows stay exact) */
    const float wn = 1.0f / sqrtf(L * C), wd = 1.0f / sqrtf(L * Ct), hn = wn / (2.0f * sr), hd = wd / (2.0f * sr);
    const float Kn = wn * cosf(hn) / sinf(hn), Kd = wd * cosf(hd) / sinf(hd), Kn2 = Kn * Kn, Kd2 = Kd * Kd;
    const float ga = 1.0f / (d2 * Kd2 + d1 * Kd + d0), g = (d0 / n0) * ga;   /* (d0 / n0: the lows as they were) */
    c[0] = (n2 * Kn2 + n1 * Kn + n0) * g; c[1] = 2.0f * (n0 - n2 * Kn2) * g; c[2] = (n2 * Kn2 - n1 * Kn + n0) * g;
    c[3] = 2.0f * (d0 - d2 * Kd2) * ga; c[4] = (d2 * Kd2 - d1 * Kd + d0) * ga;
    return 1;
}
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
/* test build 41: the USB-C port's role, SETTINGS' USB PORT row (ui.h): 0 COMPUTER, the USB device as always (the web installer,
 * the editor over USB, the rescue); 1 FOOTSW, the USB MIDI host of a footswitch by cable (px_usb.h, felucca-bsp's
 * chip/usb_host.c). Not saved, on purpose: every power-on is COMPUTER (docs/BT-AUDIO-TEST.md, "Test build 41"). px_uh: what
 * the host sees (px_usbhost.h), for the screen and for px_midi_io.h */
#include "px_usbhost.h"
static uint8_t px_usb_foot;
/* test build 42: OUTPUT, the two channels (engine/px_dsp.c px_engine_out): 0 MONO, the same sound on both, as every earlier
 * image; 1 STEREO, the mono sound in the middle and DELAY's, REVERB's (with their TAILS) and MOD's wet to the sides, the two summed
 * being the mono sound; 2 GTR+BASS, the guitar on the left channel and LOW OCTAVE's octave alone on the right (silent without
 * it), for two channels of a desk. Applied at once (px_audio.h), saved with the settings: bits 2-3 of the USB settings byte
 * (px_store.h st_usb, the header's pad[1]; bit 0 USB AUDIO, bit 1 left for test build 41's USB PORT) */
static uint8_t px_out_mode;
#define PX_ST_OUT_SH 2u
#define PX_ST_OUT_MASK 0x0Cu
/* test build 47: PLAY VIEW (SETTINGS): 0 CHAIN, PLAY as always; 1 SWITCHES, PLAY shows the blocks this preset's controller
 * switches drive, big, on or off (ui.h draw_live), where it has some. Bit 4 of the USB settings byte (px_store.h st_usb) */
static uint8_t px_live;
#define PX_ST_LIVE 0x10u
/* test build 49: PLAY VIEW has three, held CAB turns them round: 0 CHAIN, 1 SWITCHES (what each controller switch does in this
 * preset: a block or a scene), 2 SCENES (the preset's four, the footswitches on them): bits 4-5. And SETTINGS' two scene rows:
 * bit 6 SCENE+SWITCH (0 SWITCH WINS: a block with its own switch is the switch's; 1 SCENE WINS: scenes set it too), bit 7 SCENE
 * FOOTSW (0 the footswitches step the scenes in SCENES' view; 1 they stay on the presets) */
#define PX_ST_VIEW 0x30u
#define PX_ST_SCRULE 0x40u
#define PX_ST_SCFEET 0x80u
/* test build 49: scenes, as Helix's snapshots, for the blocks' on and off: PX_SC_N a preset, each which blocks are on (a bit
 * each). Recalled by the pedal's own numbers 65 to 68 (any value: a controller's switch learned on a scene, in a preset), by
 * the footswitches in SCENES' view, by PUSH on its tile. The scene playing keeps what is switched while it plays (the next
 * recall takes it along); SAVE writes them with the sound (px_store.h "OPSC"), and the preset opens in the scene it was saved
 * in. A scene never stored begins as things are when it is first recalled */
#define PX_SC_N 4u
#define PXM_CC_SCENE0 65u
typedef struct { uint16_t mask[PX_SC_N]; uint8_t cur, set; } px_sc_t;   /* set: the scenes stored, a bit each */
static px_sc_t px_sc_tab[52];                          /* every preset's, as saved (52: px_store.h ST_NSLOT, PXM_PL_SLOTS) */
static px_sc_t px_sc;                                  /* the playing preset's, as it plays */
static uint8_t px_sc_rule, px_sc_feet;                 /* (SETTINGS: 1 SCENE WINS; 1 the footswitches stay on the presets) */
/* test build 50: the scenes' names, typed in the web editor: 3 letters each, every preset (px_store.h "OPN1" and "OPN2"), shown
 * on the pedal in place of SCENE 2. Only the playing preset's are in RAM: px_scn as it plays (the editor changes them, SAVE
 * writes them), px_scn_saved as its slot has them (EDITED). A name ends at its first 0 */
#define PX_SCN_LEN 3u
static char px_scn[PX_SC_N][PX_SCN_LEN], px_scn_saved[PX_SC_N][PX_SCN_LEN];
static void px_scn_load(uint32_t slot);                /* (px_store.h: the slot's, from the copy in use) */
#define PX_SC_LOAD(i) (px_sc = px_sc_tab[(i) < 52u ? (i) : 0u], px_scn_load(i))
static void px_scene_label(char *t, uint32_t n, int narrow)   /* scene n's name, else "SCENE 2" ("SC 2" where narrow) */
{
    uint32_t i = 0;
    for (; i < PX_SCN_LEN && px_scn[n][i]; i++) t[i] = px_scn[n][i];
    if (i) { t[i] = 0; return; }
    if (narrow) { t[0] = 'S'; t[1] = 'C'; t[2] = ' '; t[3] = (char)('1' + n); t[4] = 0; return; }
    t[0] = 'S'; t[1] = 'C'; t[2] = 'E'; t[3] = 'N'; t[4] = 'E'; t[5] = ' '; t[6] = (char)('1' + n); t[7] = 0;
}
static pxu_t px_uh;
/* test build 43: USB audio's routing (px_uaudio.h, docs/USB-AUDIO.md), SETTINGS' three rows after USB AUDIO, one byte (7 bits,
 * as the editor's MAP sends it):
 *   bits 0-1  USB SEND, what the computer records: 0 OUTPUT (the pedal's sound, as v42; both channels of STEREO and GTR+BASS),
 *             1 DI (the guitar as it enters the chain, on both channels), 2 DI + OUT (the DI left, the pedal's sound right)
 *   bit 2     GUITAR: 0 MIX, 1 CUT: from USB RETURN's place on, the guitar is cut while the computer plays into the pedal
 *             (its playback stream open), and comes back when it stops
 *   bits 3-6  USB RETURN, where the computer's sound comes in: 0 OUTPUT (added at the output, stereo, as v42), 1 INPUT (the
 *             chain's start: the whole chain), 2 + b right before block b (PX_B_*, wherever the preset's order puts it)
 * Kept in the store's map record (px_store.h st_map_pack); 0, the default, is v42's USB audio */
static uint8_t px_ua_route;
#define PX_UA_SEND(r) ((r) & 3u)
#define PX_UA_CUT(r) (((r) >> 2) & 1u)
#define PX_UA_RET(r) (((r) >> 3) & 15u)
#define PX_UA_RET_N (2u + PX_NBLK)                     /* OUTPUT, INPUT, before each block */
static int px_ua_route_valid(uint32_t r) { return r < 128u && PX_UA_SEND(r) <= 2u && PX_UA_RET(r) < PX_UA_RET_N; }
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
                         cc == PXM_CC_NEXT || cc == PXM_CC_PREV || (cc >= PXM_CC_SCENE0 && cc < PXM_CC_SCENE0 + PX_SC_N));
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
/* ---- test build 46: controls learned live, each a preset's own (docs/MIDI.md, "Learned live, one preset's") ----
 * On a block page, hold FX on its ON row or on a setting, then press a controller's switch or move its pedal: from then on
 * that control drives that row in this preset only (the slot's: kept with the presets, px_store.h). A preset's own control
 * wins over the ones learned in the editor (mid_map), which work in every preset. ON: a switch flips the block (PRESS or EACH,
 * told apart as the editor does: a release within the learn's 1.2 s is a momentary switch's), a pedal switches it (on at
 * 64 and up); a setting: the value, 0..127 to 0..100. Up to PXM_PL_N for all the presets together. A preset saved into another
 * slot takes its own with it (px_midi_pl_copy) */
#define PXM_PL_N 64u
#define PXM_PL_SLOTS 52u                               /* (px_store.h ST_NSLOT) */
typedef struct { uint8_t slot, src, dst, mode, lo, hi, clo, chi; } px_pl_t;   /* slot 0xFF: an empty entry. lo / hi (test
                                       * build 47): a pedal's setting at the heel and the toe, 0..100; clo / chi (48): the values
                                       * the pedal sends there, 0..127 (the same twice: 0 and 127) */
static px_pl_t mid_pl[PXM_PL_N];
static void px_midi_pl_clear(void) { memset(mid_pl, 0xFF, sizeof mid_pl); }
static int px_midi_pl_valid(const px_pl_t *e)          /* an entry as stored: empty, or a usable one */
{
    if (e->slot == 0xFFu) return 1;
    return e->slot < PXM_PL_SLOTS && e->src < 128u && e->mode <= PXM_MAP_VALUE && px_midi_known(e->dst) && e->lo <= 100u &&
           e->hi <= 100u && e->clo < 128u && e->chi < 128u;
}
static int px_midi_pl_row(uint32_t slot, uint32_t d1, uint32_t d2)   /* this preset's entry driving a row (either of its two
                                                                      * numbers: ON's flip and its on/off), or -1 */
{
    for (uint32_t i = 0; i < PXM_PL_N; i++)
        if (mid_pl[i].slot == slot && (mid_pl[i].dst == d1 || mid_pl[i].dst == d2)) return (int)i;
    return -1;
}
static uint32_t px_midi_pl_count(uint32_t slot)        /* this preset's entries (PXM_PL_SLOTS: all of them) */
{
    uint32_t i, n = 0;
    for (i = 0; i < PXM_PL_N; i++) n += mid_pl[i].slot != 0xFFu && (slot >= PXM_PL_SLOTS || mid_pl[i].slot == slot);
    return n;
}
static void px_midi_pl_drop_slot(uint32_t slot)        /* a preset's own controls gone (its slot back to the factory sound) */
{
    for (uint32_t i = 0; i < PXM_PL_N; i++) if (mid_pl[i].slot == slot) memset(&mid_pl[i], 0xFF, sizeof mid_pl[i]);
}
/* a preset's sound saved into another slot: its own controls go with it, in place of what that slot had (its old sound's).
 * The ones there is no room for are dropped: their count */
static uint32_t px_midi_pl_copy(uint32_t from, uint32_t to)
{
    uint32_t i, j, lost = 0;
    if (from == to || from >= PXM_PL_SLOTS || to >= PXM_PL_SLOTS) return 0;
    px_midi_pl_drop_slot(to);
    for (i = 0; i < PXM_PL_N; i++) {
        if (mid_pl[i].slot != from) continue;
        for (j = 0; j < PXM_PL_N && mid_pl[j].slot != 0xFFu; j++) { }
        if (j == PXM_PL_N) { lost++; continue; }
        mid_pl[j] = mid_pl[i]; mid_pl[j].slot = (uint8_t)to;
    }
    return lost;
}
/* one control a meaning, one row a control, in a preset: this one replaces what the preset had for either. 0, or -1: full */
static int px_midi_pl_put(uint32_t slot, uint32_t src, uint32_t dst, uint32_t mode, uint32_t d_other, uint32_t lo, uint32_t hi)
{
    int free_i = -1;
    for (uint32_t i = 0; i < PXM_PL_N; i++) {
        px_pl_t *e = &mid_pl[i];
        if (e->slot == slot && (e->src == src || e->dst == dst || e->dst == d_other)) memset(e, 0xFF, sizeof *e);
        if (e->slot == 0xFFu && free_i < 0) free_i = (int)i;
    }
    if (free_i < 0) return -1;
    mid_pl[free_i] = (px_pl_t){ (uint8_t)slot, (uint8_t)src, (uint8_t)dst, (uint8_t)mode, (uint8_t)lo, (uint8_t)hi, 0u, 127u };
    return 0;
}
/* the learn, armed by the screen (ui.h) for one row of one preset: the next controller Control Change is taken, not played,
 * and so is that control's every message for 1.2 s, which tell what it is: values between 0 and 127, a pedal; a press and
 * its release, a momentary switch; else a switch that alternates (EACH). Nothing heard in 10 s: given up */
static struct {
    uint8_t on, got, seen, slot, knob, mid, press, rel, src, dsw, dval;   /* dsw / dval: the row's number for a switch / a pedal */
    uint8_t hold, last;                                             /* (test build 47: a control held off while its range is set;
                                                                     * 48: the value it sent last) */
    uint8_t pedal, keep, rdst, rmode;                               /* (test build 48, the guided learn: a pedal taken at its first
                                                                     * message; keep: the entry left to the screen, rdst / rmode) */
    uint32_t t_arm, t0;                                             /* (the screen's clock) */
} mid_learn = {.hold = 0xFFu};
enum { PXL_NONE, PXL_WAIT, PXL_DONE, PXL_FULL, PXL_SILENT };
static void px_learn_arm(uint32_t slot, uint32_t dsw, uint32_t dval, int knob, uint32_t now)
{
    memset(&mid_learn, 0, sizeof mid_learn);
    mid_learn.hold = 0xFFu;
    mid_learn.on = 1; mid_learn.slot = (uint8_t)slot; mid_learn.dsw = (uint8_t)dsw; mid_learn.dval = (uint8_t)dval;
    mid_learn.knob = (uint8_t)(knob != 0); mid_learn.t_arm = now;
}
static void px_learn_cancel(void) { mid_learn.on = 0; }
static int px_learn_feed(uint32_t cc, uint32_t val)   /* a controller's Control Change: 1 = the learn took it (not played) */
{
    if (mid_learn.hold == cc) { mid_learn.last = (uint8_t)val; return 1; }   /* (its heel and toe being set: the pedal does not
                                                       * move the setting; where it is, for the screen) */
    if (!mid_learn.on) return 0;
    if (!mid_learn.got) { mid_learn.got = 1; mid_learn.src = (uint8_t)cc; }
    else if (cc != mid_learn.src) return 1;           /* (another control meanwhile: not played either) */
    mid_learn.last = (uint8_t)val;
    if (val > 0u && val < 127u) mid_learn.mid = 1;
    if (val >= 64u) mid_learn.press = 1; else if (mid_learn.press) mid_learn.rel = 1;
    return 1;
}
/* every pass of the screen while armed: PXL_WAIT, else how it ended (PXL_DONE: the entry is in, *src the control's number) */
static int px_learn_poll(uint32_t now, uint32_t *src)
{
    uint32_t dst, mode;
    if (!mid_learn.on) return PXL_NONE;
    if (!mid_learn.got) {
        if (now - mid_learn.t_arm < 10000u) return PXL_WAIT;
        mid_learn.on = 0;
        return PXL_SILENT;
    }
    if (!mid_learn.seen) { mid_learn.seen = 1; mid_learn.t0 = now; }   /* (the window opens on the pass that sees the first) */
    if (!mid_learn.pedal && now - mid_learn.t0 < 1200u) return PXL_WAIT;   /* (48: a pedal said to be one: no window) */
    mid_learn.on = 0;
    *src = mid_learn.src;
    if (mid_learn.knob || mid_learn.mid || mid_learn.pedal) { dst = mid_learn.dval; mode = PXM_MAP_VALUE; }
    else { dst = mid_learn.dsw; mode = mid_learn.rel ? PXM_MAP_PRESS : PXM_MAP_EACH; }
    mid_learn.rdst = (uint8_t)dst; mid_learn.rmode = (uint8_t)mode;
    if (mid_learn.keep) return PXL_DONE;               /* (the screen puts it, once its heel and toe are set) */
    return px_midi_pl_put(mid_learn.slot, mid_learn.src, dst, mode, dst == mid_learn.dsw ? mid_learn.dval : mid_learn.dsw, 0u, 100u)
               ? PXL_FULL : PXL_DONE;
}
/* a learned control's message, read as the pedal's own: 1 = go on with *cc / *val (rewritten when learned), 0 = nothing to
 * do (a switch's release). Test build 46: the preset's own controls (slot: the preset playing) before the editor's */
static int px_midi_map_dst(uint32_t dst, uint32_t mode, uint32_t lo, uint32_t hi, uint32_t clo, uint32_t chi, uint32_t *cc,
                           uint32_t *val)
{
    *cc = dst;
    if (mode == PXM_MAP_VALUE) {                       /* test build 47: a setting's heel and toe (0..100), the value between */
        if ((lo != 0u || hi != 100u || (clo != chi && (clo != 0u || chi != 127u))) && px_cc_tab[dst & 127u].kind == PXM_PARAM) {
            int a = (int)clo, b = (int)chi, t;         /* (48: along the pedal's own travel, heel to toe; the same: 0 to 127) */
            if (a == b) { a = 0; b = 127; }
            t = ((int)*val - a) * 1000 / (b - a);
            t = t < 0 ? 0 : t > 1000 ? 1000 : t;
            const int k = (int)lo + (((int)hi - (int)lo) * t + ((int)hi >= (int)lo ? 500 : -500)) / 1000;
            *val = ((uint32_t)k * 127u + 50u) / 100u;  /* (pxm_to_cc: the map reads it back as k) */
        }
        return 1;
    }
    if (mode == PXM_MAP_PRESS && *val < 64u) return 0;
    /* a press: what the pedal's own number does for one (a flip happens on 0, everything else on 64 and up) */
    *val = dst >= PXM_CC_TOGGLE0 && dst < PXM_CC_TOGGLE0 + PX_NBLK ? 0u : 127u;
    return 1;
}
static int px_midi_map_in(uint32_t *cc, uint32_t *val, uint32_t slot)
{
    uint32_t i;
    for (i = 0; i < PXM_PL_N; i++) {
        const px_pl_t *e = &mid_pl[i];
        if (e->slot == slot && e->src == *cc && px_midi_known(e->dst))
            return px_midi_map_dst(e->dst, e->mode, e->lo, e->hi, e->clo, e->chi, cc, val);
    }
    for (i = 0; i < PXM_MAP_N; i++) {
        const px_map_t *e = &mid_map[i];
        if (e->src == *cc && px_midi_known(e->dst)) return px_midi_map_dst(e->dst, e->mode, 0u, 100u, 0u, 127u, cc, val);
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
