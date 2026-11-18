/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the web editor's protocol (docs/EDITOR-PROTOCOL.md): SysEx F0 7D 4F 50 cmd args.. F7.
 *
 * The editor edits PRESET, the very sound the pedal's screen shows and the engine plays (audio_poll applies any change,
 * ui_render redraws what changed), and the 52 slots of the store (px_store.h). Frames come through usb.c's sx_frame
 * (ota_frame_get), replies leave through ota_wire_send, as the FM-1 editor (felucca's editor.c) does; ed_service runs
 * before ota_service, which would take any frame.
 * While watched, a change made on the pedal (knob, buttons, footswitches) is pushed as a STATE frame; a change the
 * editor made itself is not echoed back.
 * The including file provides: PRESET, cur_preset, load_preset, ui_blk / ui_mode / ui_item (ui.h), st_gen, the
 * store (px_store.h), BLK_COL (ui_colors.h), ota_frame_get / ota_frame_done / ota_wire_send, ms_now, OP_VERSION,
 * FELUCCA_ID_PTR, ed_load / ed_flags (the DSP load for PING), and (v37) the NAM user models (px_models.h) with
 * nm_changed(slot), called after a slot was written or erased; (v42) px_ua_on (px_midi.h), st_usb, and with
 * OP_UAUDIO ed_ua_stat(k), USB audio's state (k = 0) and its ED_UA_STATS counts (px_uaudio.h).
 * An image with a log channel also defines ED_DIAG_LINES
 * and ed_diag_line(i, out), which writes line i (at most ED_DIAG_LEN - 1 characters); without them DIAG gets no reply.
 * The Bluetooth audio image sets ED_BT: the editor over Bluetooth MIDI as well (see ed_bt below); it then provides
 * ed_bt_ready and ed_bt_send (bt/bt_le_midi.c) and feeds every BLE MIDI write to ed_bt_write (px_midi_io.h).
 */
#pragma once

#define ED_H0 0x7Du
#define ED_H1 0x4Fu
#define ED_H2 0x50u
#if PX_OWN_FX
#define ED_PROTO 7u                             /* 7: test build 51, a two-job pedal (PL's mode bit 6) and VOL PEDAL (MAP's
                                                 * last byte, bit 4); 6: PL, SCENE and MAP's two settings bytes (test builds
                                                 * 46 to 49); 5: the Bluetooth audio image's test build 39, WATCH's MIDI bit,
                                                 * MIDI_IN, MAP */
#else
#define ED_PROTO 4u                             /* 4: v42, USB_AUDIO; 3: v38, cabinet IRs (IR_LIST .. IR_ERASE); 2: v37, NAM models */
#endif
#define ED_NAME_MAX 13u
#define ED_P_SIZE 110u
#if OP_UADEV
#define ED_UA_STATS 40u                         /* (the measurement option's figures after the 20, px_uaudio.h) */
#else
#define ED_UA_STATS 20u                         /* USB_AUDIO's counts after the state (ed_ua_stat 1..20) */
#endif
#ifndef ED_STATS
#define ED_STATS 6u                             /* PING's figures after the flags (ed_stat): the Bluetooth test image has 8 */
#endif
typedef char ed_size_check[sizeof(px_preset_t) == ED_P_SIZE ? 1 : -1];
enum { ED_INFO = 1, ED_CAT_BLOCK, ED_CAT_TYPE, ED_TEXTS, ED_STATE, ED_SET, ED_ORDER, ED_NAME, ED_PUT, ED_SELECT,
       ED_FOCUS, ED_LIST, ED_SLOT_GET, ED_SLOT_PUT, ED_COMMIT, ED_SAVE, ED_WATCH, ED_PING,
       ED_NAM_LIST, ED_NAM_WRITE, ED_NAM_COMMIT, ED_NAM_ERASE, ED_DIAG,
       ED_IR_LIST, ED_IR_WRITE, ED_IR_COMMIT, ED_IR_ERASE,     /* v38: 24..27 */
       ED_USB_AUDIO,                                           /* v42: 28 */
       ED_MIDI_IN, ED_MAP,                                     /* test build 39 (PX_OWN_FX): 29, pushed while watched; 30 */
       ED_NAM_RENAME,                                          /* test build 40 (PX_OWN_FX): 31 */
       ED_PL, ED_SCENE };                                      /* protocol 6 (PX_OWN_FX): 32, 33 */

#ifndef ED_OUT_BYTES
#define ED_OUT_BYTES 1024u   /* a reply's bytes at most (an image with longer DIAG lines sets more) */
#endif
/* (test builds 23 to 41 kept it in the RAM code window's data, .bss_hi, out of the 96 KB RAM, which BLE MIDI needed then;
 * test build 42 brings it back to the RAM, 18 KB free, for the code of the stereo output; only CPU0 touches it) */
static uint8_t ed_out[ED_OUT_BYTES];
static uint32_t ed_n;
static struct {
    uint8_t on; uint32_t last_ms, hash, push_ms;
#if PX_OWN_FX
    uint8_t midi; uint32_t mid_seen;   /* test build 39: WATCH's bit 1, the editor wants MIDI_IN; the messages it was told of */
#endif
} ed_w;

#ifndef ED_BT
#define ED_BT 0
#endif
#if ED_BT
/* The editor over Bluetooth MIDI (the Bluetooth audio image, docs/BT-AUDIO-TEST.md): the same frames as SysEx in BLE MIDI
 * writes, kept across writes (ed_bt_write, px_ble_midi.h px_blem_sysex), answered the way they came: the reply goes to
 * ed_bt_send, which copies it into the LE link's own buffer and sends it as BLE MIDI notifications (bt/bt_le_midi.c), so ed_out
 * is free again at once and USB's frames are answered as before, even with both editors open. A frame from Bluetooth waits
 * while that buffer still sends (ed_bt_ready), and so do the pushes while the last frame came that way (ed_bt). Nothing but the
 * editor's frames is taken from Bluetooth: no update, no UBOOT key. With no Bluetooth frame ever, every byte goes the USB way
 * as before. */
#include "px_ble_midi.h"
#ifndef ED_BT_LINKS
#define ED_BT_LINKS 1u                          /* the LE links (the Bluetooth audio image: 2, test build 27) */
#endif
static px_blem_sx_t ed_bt_rx[ED_BT_LINKS];      /* each link's SysEx, one whole frame at a time */
static uint8_t ed_bt, ed_bt_link;               /* the last frame came over Bluetooth, on that link: replies and pushes go there */
static uint32_t ed_bt_frames;                   /* frames answered over Bluetooth (bt/bt_le_midi.c's log words) */
static int ed_bt_ready(uint32_t now);           /* the LE side's send buffer is empty (it also keeps it moving) */
static void ed_bt_send(uint32_t link, const uint8_t *p, uint32_t n);   /* a reply, F0 .. F7, into that buffer, for that link */
static void ed_bt_write(uint32_t link, const uint8_t *p, uint32_t n)   /* every BLE MIDI write, with the link it came on */
{
    if (link < ED_BT_LINKS) px_blem_sysex(&ed_bt_rx[link], p, n);
}
static void ed_bt_lost(uint32_t link)           /* that LE link came or went: nothing of the last one's is kept or sent */
{
    if (link >= ED_BT_LINKS) return;
    ed_bt_rx[link].in = ed_bt_rx[link].keep = ed_bt_rx[link].ready = 0;
    if (ed_bt && ed_bt_link == link) { ed_bt = 0; ed_w.on = 0; }
}
#endif

static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0; ed_out[1] = ED_H0; ed_out[2] = ED_H1; ed_out[3] = ED_H2; ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    for (uint32_t i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i]);
    ed_b(0);
}
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
#if ED_BT
    if (ed_bt) { ed_bt_send(ed_bt_link, ed_out, ed_n); return; }
#endif
    ota_wire_send(ed_out, ed_n);
}
static void ed_preset(const px_preset_t *p)
{
    const uint8_t *b = (const uint8_t *)p;
    for (uint32_t i = 0; i < ED_P_SIZE; i++) ed_b(b[i]);
}

/* a received preset: every field in range, the name cleaned; 0 = invalid */
static int ed_take_preset(const uint8_t *a, px_preset_t *q)
{
    uint32_t i, k;
    memcpy(q, a, ED_P_SIZE);
    for (i = 0; i < 14u; i++) {
        uint8_t c = (uint8_t)q->name[i];
        if (i >= ED_NAME_MAX || !c) { for (; i < 14u; i++) q->name[i] = 0; break; }
        if (c < 32u || c > 126u) q->name[i] = '?';
    }
    for (i = 0; i < PX_NBLK; i++) {
        if (q->on[i] > 1u || q->type[i] >= (uint8_t)px_ntypes((int)i)) return 0;
        for (k = 0; k < PX_NPAR; k++) if (q->p[i][k] > 100u) return 0;
    }
    return px_order_valid(q->order);
}

/* the sound differs from what its slot holds (the pedal's "*") */
static int ed_dirty(void)
{
#if PX_OWN_FX   /* protocol 6: as the pedal's EDITED (ui.h ui_dirty), the scenes counted: a recall is no change, a scene set is */
    return ui_dirty() != 0;
#else
    static px_preset_t ref;
    static uint32_t ref_slot = ~0u, ref_gen = ~0u;
    if (ref_slot != cur_preset || ref_gen != st_gen) { st_get(cur_preset, &ref); ref_slot = cur_preset; ref_gen = st_gen; }
    return memcmp(&ref, &PRESET, sizeof ref) != 0;
#endif
}
static uint32_t ed_state_hash(void)
{
    const uint8_t *b = (const uint8_t *)&PRESET;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof PRESET; i++) h = (h ^ b[i]) * 16777619u;
    h = (h ^ cur_preset) * 16777619u;
    h = (h ^ ui_blk) * 16777619u;
#if PX_OWN_FX
    h = (h ^ px_sc.cur ^ (uint32_t)px_sc.set << 8) * 16777619u;   /* (protocol 6: a scene recalled on the pedal is pushed too) */
#endif
    return (h ^ st_gen) * 16777619u;
}
#if PX_OWN_FX
/* protocol 6: MAP's settings bytes as no build writes them (GUITAR TONE past BRIGHT, PICKUP past P90, PLAY VIEW past SCENES, a
 * bit no build sets), and a preset's scenes as SCENE sends them (the scene playing, the ones stored, the four masks in 2 bytes) */
static int ed_more_bad(uint32_t t, uint32_t v) { return (t & 15u) >= PX_INZ_N || ((t >> 4) & 3u) > 2u || t > 0x3Fu || (v & 3u) > 2u || v > 31u; }
static void ed_sc(const px_sc_t *c)
{
    ed_b(c->cur); ed_b(c->set);
    for (uint32_t i = 0; i < PX_SC_N; i++) { ed_b(c->mask[i]); ed_b((uint32_t)c->mask[i] >> 7); }
}
/* test build 50: the scenes' names (px_midi.h px_scn), PX_SCN_LEN characters each, 0 after the end; one as SCENE takes it: ASCII
 * 32..126 up to its first 0, nothing after it */
static void ed_scn(const char (*s)[PX_SCN_LEN])
{
    for (uint32_t i = 0; i < PX_SC_N; i++) for (uint32_t j = 0; j < PX_SCN_LEN; j++) ed_b((uint8_t)s[i][j]);
}
static int ed_scn_ok(const uint8_t *c)
{
    for (uint32_t j = 0, end = 0; j < PX_SCN_LEN; j++) {
        if (!c[j]) end = 1;
        else if (end || c[j] < 32u || c[j] > 126u) return 0;
    }
    return 1;
}
#endif
static void ed_state_frame(void)
{
    ed_begin(ED_STATE);
    ed_b(cur_preset);
    ed_b((ed_dirty() ? 1u : 0u) | (st_saved(cur_preset) ? 2u : 0u));
    ed_b(ui_blk);
    ed_preset(&PRESET);
}
static void ed_ui_fix(void)                       /* the block page after a type change, a new preset... */
{
    if (ui_mode == M_EDIT && ui_item >= 2u + (uint32_t)px_nparams((int)ui_blk, PRESET.type[ui_blk])) ui_mode = M_BLOCK;
    if (ui_item >= 2u + (uint32_t)px_nparams((int)ui_blk, PRESET.type[ui_blk])) ui_item = 0;
}

static void ed_handle(const uint8_t *a, uint32_t n)   /* a: the args, after the command byte */
{
    uint32_t cmd = a[-1], i, k;
    int rc;
    px_preset_t q;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
#if PX_OWN_FX   /* the Bluetooth audio image's own models (LOW OCTAVE, PAD, TREBLE BOOST): a version text of its own, because
                 * the web editor keeps each firmware's catalogue under its identity and version (docs/pxedit.js) and would
                 * otherwise show a v42 pedal's, without them. "v42 FX2" since test build 40 (TREBLE BOOST): an editor that
                 * kept build 38's "v42 FX" catalogue reads it again. "v42 FX3" since test build 44 (TAPE FLUTE); "v42 FX4" since
                 * test build 45 (SFX's WAH, WHAMMY's PEDAL); "v42 FX5" since test build 47 (12-STRING's DETUNE in cents to 3.0);
                 * "v42 FX6" since test build 48 (PRESET LVL down to -INF); "v42 FX7" since test build 51 (a two-job pedal: PL's
                 * mode bit 6, px_midi.h PXM_PL_DUAL; MAP's last byte bit 4, VOL PEDAL) */
        ed_b(ED_PROTO); ed_str(OP_VERSION " FX7", 15); ed_str(FELUCCA_ID_PTR, 27);
#else
        ed_b(ED_PROTO); ed_str(OP_VERSION, 15); ed_str(FELUCCA_ID_PTR, 27);
#endif
        ed_b(PX_NBLK); ed_b(PX_NPAR); ed_b(ST_NSLOT); ed_b(ED_NAME_MAX);
        break;
    case ED_CAT_BLOCK:
        if (n < 1u || a[0] >= PX_NBLK) return;
        ed_b(a[0]); ed_str(px_block_label(a[0]), 15); ed_str(px_block_name(a[0]), 31); ed_b((uint32_t)px_ntypes(a[0]));
        { uint32_t c = BLK_COL[a[0]]; ed_b(c); ed_b(c >> 7); ed_b(c >> 14); }
        break;
    case ED_CAT_TYPE: {
        if (n < 2u || a[0] >= PX_NBLK || a[1] >= (uint32_t)px_ntypes(a[0])) return;
        int np = px_nparams(a[0], a[1]);
        uint32_t mask = 0;
        char t[12];
        for (k = 0; k < (uint32_t)np; k++)
            for (i = 0; i <= 100u && !(mask >> k & 1u); i++)
                if (px_param_text(a[0], a[1], (int)k, (int)i, t)) mask |= 1u << k;
        ed_b(a[0]); ed_b(a[1]); ed_str(px_type_name(a[0], a[1]), 31); ed_b((uint32_t)np); ed_b(mask);
        for (k = 0; k < (uint32_t)np; k++) ed_str(px_param_name(a[0], a[1], (int)k), 15);
        break;
    }
    case ED_TEXTS: {
        if (n < 3u || a[0] >= PX_NBLK || a[1] >= (uint32_t)px_ntypes(a[0]) || a[2] >= PX_NPAR) return;
        char t[12];
        ed_b(a[0]); ed_b(a[1]); ed_b(a[2]);
        for (i = 0; i <= 100u; i++) {
            t[8] = 0;
            ed_str(px_param_text(a[0], a[1], a[2], (int)i, t) ? t : "", 8);
        }
        break;
    }
    case ED_STATE:
        ed_state_frame();
        break;
    case ED_SET: {
        if (n < 4u || a[0] >= PX_NBLK) return;
        uint32_t b = a[0], v = a[3];
        if (a[1] == 0u) {
            if (a[2] >= PX_NPAR) return;
            PRESET.p[b][a[2]] = (uint8_t)(v > 100u ? 100u : v);
            v = PRESET.p[b][a[2]];
        } else if (a[1] == 1u) {
            PRESET.on[b] = (uint8_t)(v ? 1u : 0u);
            v = PRESET.on[b];
        } else if (a[1] == 2u) {
            uint32_t nt = (uint32_t)px_ntypes((int)b);
            PRESET.type[b] = (uint8_t)(v < nt ? v : nt - 1u);
            v = PRESET.type[b];
            ed_ui_fix();
        } else return;
        ed_b(b); ed_b(a[1]); ed_b(a[2]); ed_b(v);
        break;
    }
    case ED_ORDER:
        if (n < PX_NBLK) return;
        rc = px_order_valid(a) ? 0 : 1;
        if (!rc) memcpy(PRESET.order, a, PX_NBLK);
        ed_b((uint32_t)rc);
        break;
    case ED_NAME:
        for (i = 0; i < 14u; i++) q.name[i] = 0;
        for (i = 0; i < n && a[i] && i < ED_NAME_MAX; i++) q.name[i] = (char)(a[i] >= 32u && a[i] <= 126u ? a[i] : '?');
        memcpy(PRESET.name, q.name, 14);
        ed_str(PRESET.name, 14);
        break;
    case ED_PUT:
        if (n < ED_P_SIZE) return;
        rc = ed_take_preset(a, &q) ? 0 : 1;
        if (!rc) { PRESET = q; ed_ui_fix(); }
        ed_b((uint32_t)rc);
        break;
    case ED_SELECT:
        if (n < 1u) return;
        rc = a[0] < ST_NSLOT ? 0 : 1;
        if (!rc) { cur_preset = a[0]; load_preset(cur_preset); ed_ui_fix(); }
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    case ED_FOCUS:
        if (n < 1u || a[0] >= PX_NBLK) return;
        if (ui_mode == M_CHAIN || ui_mode == M_BLOCK || ui_mode == M_EDIT) {
            if (ui_blk != a[0]) { ui_blk = a[0]; ui_item = 0; }
            ui_mode = M_BLOCK;
        }
        ed_b(a[0]);
        break;
    case ED_LIST: {
        if (n < 2u) return;
        uint32_t s = a[0], c = a[1];
        if (s >= ST_NSLOT) s = ST_NSLOT;
        if (c > 26u) c = 26u;
        if (s + c > ST_NSLOT) c = ST_NSLOT - s;
        ed_b(s); ed_b(c);
        for (k = s; k < s + c; k++) { st_get(k, &q); ed_b(st_saved(k) ? 1u : 0u); ed_str(q.name, 14); }
        break;
    }
    case ED_SLOT_GET:
        if (n < 1u || a[0] >= ST_NSLOT) return;
        st_get(a[0], &q);
        ed_b(a[0]); ed_b(st_saved(a[0]) ? 1u : 0u); ed_preset(&q);
        break;
    case ED_SLOT_PUT:
        if (n < 2u || a[0] >= ST_NSLOT) return;
        rc = 0;
        if (a[1] & 1u) {
            if (n < 2u + ED_P_SIZE || !ed_take_preset(a + 2, &q)) rc = 1;
            else { memset(st_slot[a[0]], 0xFF, ST_SLOT); memcpy(st_slot[a[0]], &q, sizeof q); st_slot[a[0]][sizeof q] = ST_MARK; }
        } else memset(st_slot[a[0]], 0xFF, ST_SLOT);
        if (!rc && (a[1] & 2u)) rc = st_commit() ? 2 : 0;
        st_gen++;
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    case ED_COMMIT:
        rc = st_commit() ? 2 : 0;
        st_gen++;
        ed_b((uint32_t)rc);
        break;
    case ED_SAVE:
        if (n < 1u || a[0] >= ST_NSLOT) return;
#if PX_OWN_FX
        if (a[0] != cur_preset) (void)px_midi_pl_copy(cur_preset, a[0]);   /* (test build 46: its learned controls go with it) */
        sc_fold(); px_sc_tab[a[0]] = px_sc;            /* (test build 49: and its scenes) */
#endif
        rc = st_save(a[0], &PRESET) ? 2 : 0;
        if (!rc) cur_preset = a[0];
        st_gen++;
        if (!rc) load_preset(cur_preset);
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    case ED_WATCH:
        ed_w.on = n >= 1u && a[0] ? 1u : 0u;
#if PX_OWN_FX
        if (ed_w.on && !ed_w.midi && n >= 1u && (a[0] & 2u)) ed_w.mid_seen = mid_in_n;   /* (only what comes from now on) */
        ed_w.midi = ed_w.on && n >= 1u && (a[0] & 2u) ? 1u : 0u;
        ed_b(ed_w.on | (uint32_t)ed_w.midi << 1);
#else
        ed_b(ed_w.on);
#endif
        break;
    case ED_PING:
        ed_b(ed_load() > 127u ? 127u : ed_load()); ed_b(ed_flags());
        for (k = 0; k < 2u; k++) ed_b(ed_stat(k) > 127u ? 127u : ed_stat(k));   /* v40.1: the peaks, the late blocks, the clips */
        for (k = 2; k < 4u; k++) { ed_b(ed_stat(k)); ed_b(ed_stat(k) >> 7); }
        for (k = 4; k < ED_STATS; k++) { ed_b(ed_stat(k) >> 2); ed_b(ed_stat(k) >> 9); }   /* free bytes / 4: the stacks (BLE image: its heap, its tasks) */
#ifdef ED_PROF
        ed_b(ED_PROF);                                 /* v40.2: where the DSP time goes (ed_prof), after their count */
        for (k = 0; k < ED_PROF; k++) { ed_b(ed_prof(k)); ed_b(ed_prof(k) >> 7); }
#endif
        break;
    case ED_NAM_LIST: {                                /* v37: every model, factory first, then the user slots */
        char nm[12];
        ed_b(PX_NAM_NFACT); ed_b(PX_NAM_NUSER);
        for (k = 0; k < PX_NAM_N; k++) { rc = px_nam_name((int)k, nm); ed_b(rc ? 1u : 0u); ed_str(nm, 11); }
        break;
    }
    case ED_NAM_WRITE:                                 /* piece (2 bytes, 7 bits each), then 128 packed bytes */
        if (n < 2u) return;
        i = a[0] | (uint32_t)a[1] << 7;
        rc = nm_write_piece(i, a + 2, n - 2u);
        ed_b(a[0]); ed_b(a[1]); ed_b((uint32_t)rc);
        break;
    case ED_NAM_COMMIT:
    case ED_NAM_ERASE:
        if (n < 1u) return;
        rc = cmd == ED_NAM_COMMIT ? nm_commit(a[0]) : nm_erase(a[0]);
        nm_changed(a[0]);                              /* a sound playing that slot reloads it */
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
#if PX_OWN_FX
    case ED_NAM_RENAME: {                              /* test build 40: slot, the name (0-ended, 11 characters at most) */
        char nm[12];
        if (n < 2u) return;
        for (i = 0; i < 11u && 1u + i < n && a[1u + i]; i++) nm[i] = (char)a[1u + i];
        nm[i] = 0;
        rc = nm_rename(a[0], nm, i);                   /* (the weights as they were: nothing to reload) */
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    }
#endif
    case ED_IR_LIST: {                                 /* v38: the cabinet IR slots */
        char nm[12];
        ed_b(PX_IR_N);
        for (k = 0; k < PX_IR_N; k++) { rc = px_ir_name((int)k, nm); ed_b(rc ? 1u : 0u); ed_str(nm, 11); }
        break;
    }
    case ED_IR_WRITE:                                  /* piece (2 bytes, 7 bits each), then 128 packed bytes */
        if (n < 2u) return;
        i = a[0] | (uint32_t)a[1] << 7;
        rc = ir_write_piece(i, a + 2, n - 2u);
        ed_b(a[0]); ed_b(a[1]); ed_b((uint32_t)rc);
        break;
    case ED_IR_COMMIT:
    case ED_IR_ERASE:
        if (n < 1u) return;
        rc = cmd == ED_IR_COMMIT ? ir_commit(a[0]) : ir_erase(a[0]);
        ir_changed(a[0]);                              /* a sound playing that slot reloads it */
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    case ED_USB_AUDIO:                                 /* v42: the USB AUDIO setting (no argument: read), its streams */
#if OP_UAUDIO
        rc = 0;
        i = n >= 1u && a[0] ? 1u : 0u;
#if OP_UADEV
        if (n >= 1u) i = uad_set(a, n);                /* the measurement option's modes (px_uaudio.h) */
#endif
        if (n >= 1u && i != px_ua_on) {
            px_ua_on = (uint8_t)i;                     /* (px_usb.h presents the USB device anew, after this reply) */
            st_usb = (uint8_t)((st_usb & ~1u) | px_ua_on);
            rc = st_commit() ? 2 : 0;
        }
        ed_b(px_ua_on); ed_b((uint32_t)rc); ed_b(ed_ua_stat(0));
        ed_b(ED_UA_STATS);
        for (k = 1; k <= ED_UA_STATS; k++) { i = ed_ua_stat(k); ed_b(i); ed_b(i >> 7); ed_b(i >> 14); ed_b(i >> 21); }
#else
        ed_b(0); ed_b(1); ed_b(0); ed_b(0);            /* (this image has no USB audio) */
#endif
        break;
#if PX_OWN_FX
    case ED_MAP: {                                     /* test build 39: the controls learned on the pedal, SWITCH, INPUT; 40:
                                                        * TAILS, BT POWER */
        rc = 0;
        if (n >= 1u && a[0] == 1u) {                   /* 1, each, INPUT + 10, flags (bit 0 TAILS, bits 1-2 BT POWER), then
                                                        * PXM_MAP_N x (present, src, dst, mode); test build 43: then, if
                                                        * there, USB audio's routing (px_midi.h px_ua_route) */
            static px_map_t mq[PXM_MAP_N];
            uint32_t want = 4u + PXM_MAP_N * 4u;
            if (n < want || a[2] > (uint32_t)(PX_IN_DB_MAX - PX_IN_DB_MIN) || ((a[3] >> 1) & 3u) > 2u || ((a[3] >> 3) & 3u) > 2u) rc = 1;
#if OP_UAUDIO
            if (!rc && n > want && !px_ua_route_valid(a[want])) rc = 1;
#endif
            if (!rc && n > want + 2u && ed_more_bad(a[want + 1u], a[want + 2u])) rc = 1;   /* (protocol 6) */
            for (k = 0; !rc && k < PXM_MAP_N; k++) {
                const uint8_t *e = a + 4u + k * 4u;
                mq[k].src = e[0] ? e[1] : 0xFFu; mq[k].dst = e[2]; mq[k].mode = e[3];
                if (!e[0]) mq[k].dst = mq[k].mode = 0xFFu;
                if (!px_midi_map_valid(&mq[k])) rc = 1;
            }
            for (k = 0; !rc && k < PXM_MAP_N; k++)     /* one controller number, one meaning */
                for (i = k + 1u; !rc && i < PXM_MAP_N; i++)
                    if (mq[k].src != 0xFFu && mq[k].src == mq[i].src) rc = 1;
            if (!rc) {
                memcpy(mid_map, mq, sizeof mid_map);
                mid_cfg.each = a[1] ? 1u : 0u;
                px_in_db = (int8_t)((int)a[2] + PX_IN_DB_MIN);
                a_in_gain_apply();                     /* (heard at once) */
                if ((a[3] & 1u) != px_tails) { px_tails = (uint8_t)(a[3] & 1u); a_tails_apply(); }
                if (((a[3] >> 1) & 3u) != px_bt_pwr) { px_bt_pwr = (uint8_t)((a[3] >> 1) & 3u); PX_BT_POWER_APPLY(); }
                px_out_mode = (uint8_t)((a[3] >> 3) & 3u);   /* test build 42: OUTPUT (the audio reads it each block) */
                st_usb = (uint8_t)((st_usb & ~PX_ST_OUT_MASK) | (px_out_mode << PX_ST_OUT_SH));
#if OP_UAUDIO
                if (n > want) px_ua_route = a[want];   /* test build 43: USB SEND, USB RETURN, GUITAR (heard at once) */
#endif
                if (n > want + 2u) {                   /* protocol 6: GUITAR TONE and PICKUP (heard at once), PLAY VIEW, the
                                                        * scene rows (st_commit keeps them: the store's settings and OPP3) */
                    const uint8_t was = px_inz_pack();
                    px_inz_unpack(a[want + 1u]);
                    if (px_inz_pack() != was) a_inz_apply();
                    px_live = (uint8_t)(a[want + 2u] & 3u);
                    px_sc_rule = (uint8_t)((a[want + 2u] >> 2) & 1u); px_sc_feet = (uint8_t)((a[want + 2u] >> 3) & 1u);
                    px_vol_at = (uint8_t)((a[want + 2u] >> 4) & 1u);   /* (protocol 7, test build 51: VOL PEDAL, heard at once) */
                    PX_EXP_PLACE(px_vol_at);
                }
                st_midi = px_midi_cfg_pack(&mid_cfg);
                if (st_commit()) rc = 2;               /* (it stays in RAM and works: the next save writes it) */
                st_gen++;
            }
        }
        ed_b((uint32_t)rc); ed_b(mid_cfg.each); ed_b((uint32_t)(px_in_db - PX_IN_DB_MIN));
        ed_b(px_tails | (uint32_t)px_bt_pwr << 1 | (uint32_t)px_out_mode << 3);
        ed_b(PXM_MAP_N);
        for (k = 0; k < PXM_MAP_N; k++) {
            const px_map_t *e = &mid_map[k];
            const int on = e->src != 0xFFu;
            ed_b(on ? 1u : 0u); ed_b(on ? e->src : 0u); ed_b(on ? e->dst : 0u); ed_b(on ? e->mode : 0u);
        }
#if OP_UAUDIO
        ed_b(px_ua_route);                             /* (test build 43: after the entries, where builds 39 to 42 ended) */
#else
        ed_b(0u);                                      /* (no routing: its place kept for the two bytes after it) */
#endif
        ed_b(px_inz_pack()); ed_b(px_live | (uint32_t)px_sc_rule << 2 | (uint32_t)px_sc_feet << 3 |
                                  (uint32_t)px_vol_at << 4);   /* (protocol 6; 7, test build 51: bit 4 VOL PEDAL) */
        break;
    }
    case ED_PL: {                                      /* protocol 6: a preset's own controls (px_midi.h mid_pl): slot (read);
                                                        * slot, 1, src, dst, mode, lo, hi, clo, chi (learn one, in place of the
                                                        * preset's for the same control or the same row); slot, 2, src (forget) */
        const uint32_t s = n ? a[0] : PXM_PL_SLOTS;
        if (s >= PXM_PL_SLOTS) return;
        rc = 0;
        if (n >= 9u && a[1] == 1u) {
            const px_pl_t e = { (uint8_t)s, a[2], a[3], a[4], a[5], a[6], a[7], a[8] };
            const uint32_t d = e.dst, o = d >= PXM_CC_TOGGLE0 && d < PXM_CC_ON0 ? d + PX_NBLK      /* (a block's ON: its flip */
                                     : d >= PXM_CC_ON0 && d < PXM_CC_ON0 + PX_NBLK ? d - PX_NBLK : d;   /* and its on/off number) */
            if (!px_midi_pl_valid(&e)) rc = 1;
            else if (px_midi_pl_put(s, e.src, d, e.mode, o, e.lo, e.hi) < 0) rc = 3;
            else for (k = 0; k < PXM_PL_N; k++) if (mid_pl[k].slot == s && mid_pl[k].src == e.src) { mid_pl[k].clo = e.clo; mid_pl[k].chi = e.chi; }
        } else if (n >= 3u && a[1] == 2u) {
            for (k = 0; k < PXM_PL_N; k++) if (mid_pl[k].slot == s && mid_pl[k].src == a[2]) memset(&mid_pl[k], 0xFF, sizeof mid_pl[k]);
        } else if (n >= 2u) rc = 1;
        if (n >= 2u && !rc) { if (st_commit()) rc = 2; st_gen++; }   /* (as a save: an unsaved sound stays as it is) */
        ed_b((uint32_t)rc); ed_b(s); ed_b(px_midi_pl_count(PXM_PL_SLOTS)); ed_b(PXM_PL_N); ed_b(px_midi_pl_count(s));
        for (k = 0; k < PXM_PL_N; k++)
            if (mid_pl[k].slot == s) { const uint8_t *b = &mid_pl[k].src; for (i = 0; i < 7u; i++) ed_b(b[i]); }
        break;
    }
    case ED_SCENE:                                     /* protocol 6: the playing preset's scenes (px_midi.h px_sc): nothing
                                                        * (read); 1, n (recall, as CC 65 + n); 2, n, mask (2 bytes: scene n's
                                                        * blocks; the scene playing switches them); 3, n (forget); test build 50:
                                                        * 4, n, c0, c1, c2 (scene n's name, 0-padded). SAVE keeps them */
        rc = 0;
        if (n) {
            const uint32_t sn = n >= 2u ? a[1] : PX_SC_N;
            if (sn >= PX_SC_N) rc = 1;
            else if (a[0] == 1u) ui_scene_recall(sn);
            else if (a[0] == 2u && n >= 4u && a[3] < 32u) {
                px_sc.mask[sn] = (uint16_t)(a[2] | (uint32_t)a[3] << 7); px_sc.set |= (uint8_t)(1u << sn);
                if (sn == px_sc.cur) for (k = 0; k < PX_NBLK; k++) PRESET.on[k] = (uint8_t)(px_sc.mask[sn] >> k & 1u);
            } else if (a[0] == 3u) px_sc.set &= (uint8_t)~(1u << sn);
            else if (a[0] == 4u && n >= 2u + PX_SCN_LEN && ed_scn_ok(a + 2)) memcpy(px_scn[sn], a + 2, PX_SCN_LEN);   /* (a change
                                                                                                     * until SAVE: ui_dirty) */
            else rc = 1;
        }
        ed_b((uint32_t)rc); ed_sc(&px_sc); ed_sc(&px_sc_tab[cur_preset < 52u ? cur_preset : 0u]);
        ed_scn(px_scn); ed_scn(px_scn_saved);          /* (test build 50: the names as they play and as saved, after what 49 sent,
                                                        * so its editor reads on) */
        break;
#endif
#ifdef ED_DIAG_LINES
    case ED_DIAG: {                                    /* read only: nothing here changes the pedal */
        static char l[ED_DIAG_LEN];
        ed_b(ED_DIAG_LINES);
        for (i = 0; i < ED_DIAG_LINES; i++) { ed_diag_line(i, l); ed_str(l, sizeof l - 1u); }
        break;
    }
#endif
    default:
        return;
    }
    ed_w.hash = ed_state_hash();                       /* the editor's own change: no push back */
    ed_send();
}

/* main loop, before ota_service: editor frames; anything else stays for ota_service */
static void ed_service(uint32_t now)
{
    const uint8_t *p;
    uint32_t n;
    if (ed_w.on && now - ed_w.last_ms > 5000u) ed_w.on = 0;    /* the editor went away */
#if PX_OWN_FX
    if (!ed_w.on) ed_w.midi = 0;                       /* (test build 39: a new WATCH 3 hears only what comes after it) */
#endif
#if ED_BT
    const int bt_free = ed_bt_ready(now);              /* (every pass: it also keeps the LE link's buffer moving) */
    if (ed_w.on && now - ed_w.push_ms >= 40u && (!ed_bt || bt_free)) {   /* (Bluetooth: once the last reply has left) */
#else
    if (ed_w.on && now - ed_w.push_ms >= 40u) {
#endif
        uint32_t h = ed_state_hash();
        if (h != ed_w.hash) { ed_w.hash = h; ed_w.push_ms = now; ed_state_frame(); ed_send(); }
#if PX_OWN_FX
        /* test build 39: what a controller sent the pedal (the footswitch, or one over USB), for the editor's monitor and its
         * learning: the last message received, with the count (several between two passes: the last one) */
        else if (ed_w.midi && ed_w.mid_seen != mid_in_n) {
            ed_w.mid_seen = mid_in_n; ed_w.push_ms = now;
            ed_begin(ED_MIDI_IN);
            ed_b(mid_last[0] == PXM_PC ? 1u : 2u); ed_b(mid_last[1]); ed_b(mid_last[2]); ed_b(mid_in_n);
            ed_send();
        }
#endif
    }
#if ED_BT
    {                                                  /* a frame from Bluetooth, once the last reply there has left; one a
                                                        * pass, the links in turn (its reply holds the buffer) */
        static uint8_t turn;
        uint32_t j;
        for (j = 0; j < ED_BT_LINKS; j++) {
            const uint32_t l = (turn + j) % ED_BT_LINKS;
            px_blem_sx_t *const r = &ed_bt_rx[l];
            const uint8_t *const b = r->buf;
            if (!r->ready || !ed_bt_ready(now)) continue;
            if (r->n >= 4u && b[0] == ED_H0 && b[1] == ED_H1 && b[2] == ED_H2) {
                ed_bt = 1;
                ed_bt_link = (uint8_t)l;
                ed_bt_frames++;
                ed_w.last_ms = now;
                ed_handle(b + 4, r->n - 4u);           /* (read in place: the writes keep nothing while ready is set, and no
                                                        * Bluetooth task runs inside ed_handle) */
            }
            r->ready = 0;                              /* (anything else from Bluetooth is dropped) */
            turn = (uint8_t)((l + 1u) % ED_BT_LINKS);
            break;
        }
    }                                                  /* then USB's frame, in the same pass as before */
#endif
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_H0 || p[1] != ED_H1 || p[2] != ED_H2)
        return;
    {
        static uint8_t f[160];                         /* the longest requests: NAM_WRITE 4 + 1 + 2 + 128, SLOT_PUT */
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++) f[i] = p[i];
        ota_frame_done();                              /* free the frame buffer before replying */
#if ED_BT
        ed_bt = 0;                                     /* (the reply and the pushes go to USB) */
#endif
        ed_w.last_ms = now;
        ed_handle(f + 4, k - 4u);
    }
}
