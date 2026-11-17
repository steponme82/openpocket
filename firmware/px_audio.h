/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — the REAL sound: guitar -> DSP engine (engine/px_dsp.c) -> output, set by the UI.
 *
 * Pocket X audio chain (found with test builds v10..v14, see docs/AUDIO-SDK.md):
 *   I2S ALNK0, group IIS_PORTA, JieLi master, MCLK=PA4 SCLK=PA5 LRCK=PA6, 44.1 kHz, 24-bit stereo (v26)
 *   24-bit like the official firmware (PocketX_016: "24-bit" bit of each channel in ALNK CON1): in 24-bit mode
 *   the DMA stores one sample per 32-bit word, right-aligned (bit 23 = sign); LEN counts words, so
 *   sr_points = 2 x A_FR to keep 128 stereo frames per block. In 16-bit, the input quantisation noise,
 *   multiplied by the amp gain, was heard as a constant hiss.
 *   guitar INPUT = DAT1 (PA8)    OUTPUT = DAT0 (PA7) -> DAC "344C" -> SGM8905 amp -> OUT + headphones
 *   amp ENABLE = PA9 at 3.3 V
 * I2S driver: the one from the official JieLi SDK (sdk/iis.bc, Apache-2.0), compiled from its bitcode.
 *
 * Processing runs in the I2S interrupt (block of 128 frames = 2.9 ms), in float on the hardware
 * FPU (-mcpu=r3 -mfprev1). Safeguard: if the DSP takes more than 85 % of the time for 8 blocks
 * in a row, we switch to BYPASS (direct guitar) instead of locking up the pedal. The UI shows the load.
 */
#pragma once
#include <stdint.h>
#ifndef BENCH_AT_BOOT
#define BENCH_AT_BOOT 0                                 /* 1 = DSP benchmark screen at boot (diagnostics) */
#endif
#include <math.h>
#include "px_dsp.h"

/* ===================== JieLi I2S driver API (SDK iis.h) ===================== */
struct iis_platform_data {
    uint8_t channel_in, channel_out, port_sel, data_width, mclk_output, slave_mode,
            update_edge, f32e, keep_alive, sel, width_16_to_24;
    uint16_t dump_points_num, sr_points;
};
int  iis_open(struct iis_platform_data *pd, uint8_t index);
int  iis_set_sample_rate(int sample_rate, uint8_t index);
void iis_channel_on(uint8_t channel, uint8_t index);
void iis_set_enc_data_handler(void *priv, void (*cb)(void *, uint8_t *, int, uint8_t), uint8_t index);
void iis_set_dec_data_handler(void *priv, void (*cb)(void *, uint8_t *, int, uint8_t), uint8_t index);
void iis_irq_handler(uint8_t index);
#define IIS_PORTA 0

/* ===================== functions provided to the driver (and the engine) ===================== */
const uint8_t log_tag_const_d_AUDIO_IIS = 0, log_tag_const_e_AUDIO_IIS = 0, log_tag_const_i_AUDIO_IIS = 0;
const uint32_t config_asser = 0;
void log_print(int lvl, const char *tag, const char *fmt, ...) { (void)lvl; (void)tag; (void)fmt; }
int  printf(const char *fmt, ...) { (void)fmt; return 0; }
void *os_current_task_rom(void) { return 0; }
void cpu_assert_debug(void) { }
void P33_SYSTEM_RESET(void) { for (;;) ; }

#if !OP_BLE_TEST   /* the ble build has a real heap (ble/ble_os.c); iis_open draws its block from it */
static uint8_t jl_heap[4096] __attribute__((aligned(8)));   /* iis_open: 2 channels x 8 x sr_points (256) */
void *malloc(unsigned n) { return (n <= sizeof jl_heap) ? jl_heap : 0; }   /* single block: iis_open */
void free(void *p) { (void)p; }
#endif
/* memset / memcpy: felucca-bsp libc.c (included by openpocket.c) */

static volatile uint32_t *jl_preg(uint32_t gpio, uint32_t off)
{
    uint32_t port = gpio >> 4;
    if (port == 3u || port > 7u) return 0;             /* no port D (flash) */
    if (port == 2u && (gpio & 15u) == 7u) return 0;    /* PC7 = display */
    return (volatile uint32_t *)(0x50000u + port * 0x40u + off);
}
static void jl_rmw(uint32_t g, uint32_t off, int on)
{
    volatile uint32_t *r = jl_preg(g, off);
    if (!r) return;
    if (on) *r |= 1u << (g & 15u); else *r &= ~(1u << (g & 15u));
}
int gpio_direction_input(uint32_t g)         { jl_rmw(g, 0x08, 1); return 0; }
int gpio_direction_output(uint32_t g, int v) { jl_rmw(g, 0x00, v); jl_rmw(g, 0x08, 0); return 0; }
int gpio_set_die(uint32_t g, int on)         { jl_rmw(g, 0x0C, on); return 0; }
int gpio_set_pull_up(uint32_t g, int on)     { jl_rmw(g, 0x10, on); return 0; }
int gpio_set_pull_down(uint32_t g, int on)   { jl_rmw(g, 0x14, on); return 0; }

/* ===================== engine + interrupt-time processing ===================== */
#define A_FR     128u                                  /* frames per I2S block */
#define A_BUDGET (A_FR * 1000000u / 44100u * FM1_TICKS_PER_US)   /* ticks available per block (~2.9 ms) */
#define G_AMP_EN 9u                                    /* PA9: SGM8905 amp enable */

static px_engine g_eng __attribute__((section(".pool"), aligned(8)));   /* ~250 KB: "POOL" RAM region */
static float a_in[A_FR], a_out[A_FR];
#if PX_OWN_FX
/* test build 42: OUTPUT (px_midi.h px_out_mode): STEREO and GTR+BASS make a_outr the right channel (engine/px_dsp.c
 * px_engine_out), through the same mutes, fades, volume and knee as the left (dsp_out); a_res_sp: a_resr holds the block
 * a_res holds, for a_send. Latched once a block (a_mode), so a block is all one way or the other. The phone's music: both
 * channels in STEREO, the left in GTR+BASS (the right is the bass's alone) */
static float a_outr[A_FR];
static int32_t a_resr[A_FR];
static uint8_t a_mode, a_split;
static volatile uint8_t a_res_sp;
#define A_BASS(o) px_engine_out(&g_eng, a_mode, a_outr + (o))
#endif
#if PX_OWN_FX
/* test build 39: INPUT (px_midi.h px_in_db) as the input's scale: the interrupt reads one float, set from the main loop */
static volatile float a_in_k = 0.5f / 8388608.0f;
static void a_in_gain_apply(void) { a_in_k = (0.5f / 8388608.0f) * powf(10.0f, (float)px_in_db * 0.05f); }
static void a_tails_apply(void) { px_engine_tails(&g_eng, px_tails); }   /* test build 40: TAILS (px_midi.h) */
/* test build 46: IN-Z (px_midi.h px_inz, px_pickup, px_inz_coefs: the pickup loaded as an input of that impedance would load
 * it), one biquad over the input after INPUT. Off (1M): nothing done to the input, bit for bit */
static volatile uint8_t a_inz_on;
static float a_inz_c[5], a_inz_z[2];                   /* b0 b1 b2 a1 a2; the state (the interrupt's own) */
static int a_running;                                  /* (defined below: the audio interrupt is on) */
static void a_inz_apply(void)                          /* (the main loop: SETTINGS, the store at boot) */
{
    float c[5];
    if (!px_inz_coefs(g_eng.sr, c)) { a_inz_on = 0; return; }
    if (a_running) fm1_irq_mask(FM1_IRQ_ALNK0);         /* (the interrupt reads the five as one set; at boot it is not on yet) */
    for (int i = 0; i < 5; i++) a_inz_c[i] = c[i];
    if (!a_inz_on) a_inz_z[0] = a_inz_z[1] = 0.0f;
    a_inz_on = 1;
    if (a_running) fm1_irq_unmask(FM1_IRQ_ALNK0);
}
#endif
#if OP_BT_AUDIO
/* output, 24-bit right-aligned, in the RAM code window's free end (app-bt.ld .bss_hi), where test builds 12 and 13 kept two
 * slots against the callbacks' order; v42's px_aorder.h sends each block in its own interrupt whichever callback comes first */
static int32_t a_res[A_FR] __attribute__((section(".bss.hi.a_res"), aligned(8)));
static volatile uint32_t a_en_s[5];                    /* the entries by the flags they find (the log's isr line, EN) */
#else
static int32_t a_res[A_FR];                            /* output, 24-bit right-aligned */
#endif
static volatile float a_lvl[2];                        /* input L / R: mean square, in 24-bit steps */
/* input level in dB below full scale (dBFS), rounded; -199 = digital silence */
static int a_level_db(int ch)
{
    float m = a_lvl[ch & 1] * (1.0f / (8388608.0f * 8388608.0f));
    if (m < 1e-20f) return -199;
    return (int)(4.3429448f * logf(m) - 0.5f);         /* 10 log10 */
}
static volatile float a_gain = 0.64f;                  /* MASTER volume (knob), applied at the output */
static volatile uint32_t a_load, a_peak, a_blocks;     /* DSP load in % */
/* v40.4: the smoothed loads' sums (the load x 8). Smoothed in whole per cent, (load * 7 + pct) / 8 rounded down at every
 * block: a load reached from below stopped up to 7 short (79 % read 73), one reached from above read right */
static uint32_t a_lds, a_cls[2];
/* v37: what can be heard as a crackle. a_xrun: blocks that took longer than their 2.9 ms (the output then repeats or
 * misses part of a block), a_xrun_at the block of the last one; a_clip: blocks whose output reached the soft clipper's
 * knee (0.8 of full scale; it used to be a hard clip at full scale) */
static volatile uint32_t a_xrun, a_xrun_at, a_clip, a_peak_all;
static volatile uint8_t a_bypass, a_over, a_hold;   /* a_hold: DSP held off (boot, safe mode) */
static volatile uint8_t a_mute;                      /* output zeros (a flash write is coming) */
static volatile uint8_t a_tmute;                     /* v36: tuner open: the output fades to silence (5 ms) */
static float a_mg = 1.0f;
/* v38: changes without clicks. A change of type, order or NAM model fades the output out (a_xft 0), is applied, and fades
 * it back in (~6 ms each way). Knob changes and blocks on / off are handed to the audio side (a_kp, a_kpend) and applied
 * at the start of its next block, where they glide and fade (px_engine_knobs): the interrupt is never held off for them. */
static volatile float a_xf = 1.0f;                     /* the change fade's gain now */
/* v40.3: the last full change, for PING (ticks): the last change seen (any), the full one's, the engine's set (how
 * long), the sound back (the end of the fade-in, written by the audio side) */
static volatile uint32_t a_sw0, a_swc, a_swset, a_sw1;
static volatile uint8_t a_swin;                        /* 1 from a full change until its fade-in ends */
static float a_gv = 0.64f;                             /* the volume as it glides */
static volatile uint8_t a_xft = 1;                     /* where the change fade goes: 1 sound, 0 silence */
static px_preset_t a_kp;
static volatile uint8_t a_kpend;
static uint8_t a_full;                                 /* the next apply must be a full one (a user model rewritten) */
static void a_commit(void) { if (a_kpend) { px_engine_knobs(&g_eng, &a_kp); a_kpend = 0; } }
/* v38: where the time goes, per block, over the last second: % of an audio block's time (index PX_NBLK: the NAM network;
 * with a NAM amp the blocks after it run on the second core, beside the ones before it) */
static uint8_t a_pf[PX_NBLK + 1];
static uint32_t a_wcut;                                /* v39: wet blocks cut short so far (the footer, when not 0) */
#if OP_BT_AUDIO
static uint32_t a_wcut_base;                           /* (test build 12) the cut blocks counted before a retirement or a reset
                                                        * cleared the engine's count (px_wet_mode, px_engine_init) */
#define A_WCUT_KEEP() (a_wcut_base += g_eng.wl_late)
#else
#define a_wcut_base 0u
#define A_WCUT_KEEP() ((void)0)
#endif
static volatile uint8_t a_nan;                         /* engine output a NaN/Inf: needs a reset */
/* v42: USB audio (px_usb.h, docs/USB-AUDIO.md). While the USB AUDIO setting is on, each output block goes through
 * px_ua_block in a_send (CPU0, the I2S interrupt): the block to the computer, the computer's playback into the output.
 * a_uat: the ticks it took, part of CPU0's work in a block (0 while the setting is off: the loads are then as before) */
static volatile uint8_t a_ua;
static uint32_t a_uat;
static void px_ua_block(int32_t *d, uint32_t n);
#if PX_OWN_FX && OP_UAUDIO
/* test build 43: USB RETURN (px_midi.h px_ua_route; px_uaudio.h px_ua_chain; engine/px_dsp.c px_engine_ret). CPU0 reads the
 * block's computer sound in a_block, before the block is handed over, as it reads the music: a_rx (mono, the input's scale),
 * a_rt_x 1 if it holds some; and latches where it joins the chain (a_rt_at, a PX_RT_* place) and whether the guitar is cut
 * there (a_rt_cut). a_rt_out: the return is at the output, mixed in a_send as v42's (px_ua_block). Every engine call that
 * makes the chain is told (A_RET), PRE and POST each their own: they run at once on the two cores */
static float a_rx[A_FR];
static int8_t a_rt_at = PX_RT_NONE;
static uint8_t a_rt_x, a_rt_cut, a_rt_out = 1u;
static void px_ua_chain(void);
#define A_RET(part, o) px_engine_ret(&g_eng, (part), a_rt_x ? a_rx + (o) : 0, a_rt_at, a_rt_cut)
#elif PX_OWN_FX
#define A_RET(part, o) ((void)0)
#endif
static volatile uint32_t a_nan_count;
static int a_running;

#if OP_BT_AUDIO
/* the Bluetooth audio test image: the phone's music (firmware/bt/bt_a2dp.c decodes it into this ring), added after the volume and
 * before the soft knee, at its own level; never while the output is muted for a flash write */
#include "px_bt_ring.h"
static bt_ring_t bt_ring;
static const float bt_gain = 0.5f;                     /* -6 dB: the phone's own volume does the rest */
/* this block's music, filled by CPU0 (a_bt_fill), read by whichever core runs dsp_out. Test build 17: in .bss_hi, far above the
 * radio's 64 KB window; up to test build 16 it lay beside the ring, inside the window, and CPU1 cleared and read it in every
 * block (app-bt.ld .data_hi says why that matters) */
static float bt_mix[A_FR] __attribute__((section(".bss.hi.bt_mix")));
static float bt_side[A_FR];                            /* test build 42: the music's side, (L - R) / 2: + left, - right in STEREO */
#endif

/* DSP work for one block: a_in -> a_res (gain, clipping) + load measurement. Runs on the 2nd core (CPU1)
 * when available, otherwise on CPU0 in the interrupt. */
static void dsp_out(void);
static void dsp_load(uint32_t t0, uint32_t lim);
static void dsp_block(void)
{
    uint32_t t0 = fm1_ticks();
    a_commit();
#if PX_OWN_FX
    a_mode = px_out_mode; a_split = a_mode != 0u;
#endif
    if (!a_bypass && !a_hold) {
        for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK) {
#if PX_OWN_FX
            A_BASS(o);
            A_RET(PX_PART_ALL, o);                     /* (test build 43: USB RETURN) */
#endif
            px_engine_process(&g_eng, a_in + o, a_out + o, PX_MAX_BLOCK);
        }
    } else {
        for (uint32_t i = 0; i < A_FR; i++) a_out[i] = a_in[i];
#if PX_OWN_FX
        for (uint32_t i = 0; i < A_FR; i++) a_outr[i] = 0.0f;
#endif
    }
    dsp_out();
    dsp_load(t0, 85u);
}
static void dsp_out(void)                              /* a_out -> a_res: safety net, mutes, volume */
{
    /* safety net: a NaN/Inf would poison the filters forever (silence). Output silence and
     * the main loop resets the engine (audio_poll). */
    for (uint32_t i = 0; i < A_FR; i++) {
        float v = a_out[i];
        if (v != v || v > 1.0e6f || v < -1.0e6f) { a_nan = 1; break; }
    }
#if PX_OWN_FX
    const int sp = a_split;                            /* test build 42: OUTPUT STEREO or GTR+BASS, a_outr the right channel */
    if (sp) for (uint32_t i = 0; i < A_FR; i++) {
        float v = a_outr[i];
        if (v != v || v > 1.0e6f || v < -1.0e6f) { a_nan = 1; break; }
    }
    if (a_nan || a_mute) for (uint32_t i = 0; i < A_FR; i++) a_outr[i] = 0.0f;
#endif
    if (a_nan) for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;
    if (a_mute) for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;
    {   /* tuner mute, with a short ramp both ways (no click) */
        float tg = a_tmute ? 0.0f : 1.0f;
        if (a_mg != tg) {
#if PX_OWN_FX
            for (uint32_t i = 0; i < A_FR; i++) { a_mg += (tg - a_mg) * 0.005f; a_out[i] *= a_mg; a_outr[i] *= a_mg; }
#else
            for (uint32_t i = 0; i < A_FR; i++) { a_mg += (tg - a_mg) * 0.005f; a_out[i] *= a_mg; }
#endif
            if (tg > 0.5f ? a_mg > 0.999f : a_mg < 0.0005f) a_mg = tg;
        } else if (tg == 0.0f) {
            for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;
#if PX_OWN_FX
            for (uint32_t i = 0; i < A_FR; i++) a_outr[i] = 0.0f;
#endif
        }
    }
#if OP_BT_AUDIO
    /* the phone's music (bt_mix: read from the ring by CPU0 before the block was handed over, a_bt_fill), through the change
     * fade below with the guitar (up to test build 10 it was added after it, so a full preset change cut the guitar but left
     * the music at full level in the blocks the DMA then repeats while the interrupt is held, audio_apply). A NaN found in
     * this block drops this block's music with the guitar (2.9 ms); from the next block on, a_bt_fill leaves it waiting */
    if (a_nan) for (uint32_t i = 0; i < A_FR; i++) bt_mix[i] = bt_side[i] = 0.0f;
#endif
    /* the output: a soft knee from 0.8 of full scale (the same slope there, then bending towards full scale, which it
     * never reaches) instead of a hard clip, whose corners crackle on the loudest notes */
    {   /* v38: the change fade, linear, 256 samples (5.8 ms) each way */
        const float tg = a_xft ? 1.0f : 0.0f;
        float xf = a_xf;
        if (xf != tg) {
            const float st = tg > xf ? 1.0f / 256.0f : -1.0f / 256.0f;
            for (uint32_t i = 0; i < A_FR; i++) {
                xf += st;
                if (st > 0.0f ? xf > 1.0f : xf < 0.0f) xf = tg;
                a_out[i] *= xf;
#if PX_OWN_FX
                a_outr[i] *= xf;
#endif
#if OP_BT_AUDIO
                bt_mix[i] *= xf;
                bt_side[i] *= xf;
#endif
            }
            a_xf = xf;
            if (xf == 1.0f && a_swin) { a_sw1 = fm1_ticks(); a_swin = 0; }
        } else if (tg == 0.0f) {
            for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;
#if PX_OWN_FX
            for (uint32_t i = 0; i < A_FR; i++) a_outr[i] = 0.0f;
#endif
#if OP_BT_AUDIO
            for (uint32_t i = 0; i < A_FR; i++) bt_mix[i] = bt_side[i] = 0.0f;   /* (already 0, unless the gate above read a_xft / a_xf
                                                                     * otherwise: a change between the reads, a CPU1 misread) */
#endif
        }
    }
    const float g = a_gain;                            /* v38: the volume glides there (~3 ms), no zipper */
    float gv = a_gv;
    int clip = 0;
#if PX_OWN_FX && OP_BT_AUDIO
    const float mr = a_mode == 1u ? 1.0f : 0.0f;       /* test build 42: STEREO, the music in stereo (else mono, as before) */
#endif
    for (uint32_t i = 0; i < A_FR; i++) {
        gv += (g - gv) * 0.008f;
#if OP_BT_AUDIO && PX_OWN_FX
        float v = a_out[i] * gv + bt_mix[i] + bt_side[i] * mr, m = v < 0.0f ? -v : v;   /* (STEREO: the music's side, + left) */
#elif OP_BT_AUDIO
        float v = a_out[i] * gv + bt_mix[i], m = v < 0.0f ? -v : v;
#else
        float v = a_out[i] * gv, m = v < 0.0f ? -v : v;
#endif
        if (m > 0.8f) {
            float t = (m - 0.8f) * 5.0f;
            m = 0.8f + 0.2f * t / (1.0f + t);
            v = v < 0.0f ? -m : m;
            clip = 1;
        }
        a_res[i] = (int32_t)(v * 8388607.0f);
#if PX_OWN_FX
        if (sp) {                                      /* the right channel: the same volume and knee */
#if OP_BT_AUDIO
            float w = a_outr[i] * gv + (bt_mix[i] - bt_side[i]) * mr, mw = w < 0.0f ? -w : w;   /* (- right) */
#else
            float w = a_outr[i] * gv, mw = w < 0.0f ? -w : w;
#endif
            if (mw > 0.8f) { float t = (mw - 0.8f) * 5.0f; mw = 0.8f + 0.2f * t / (1.0f + t); w = w < 0.0f ? -mw : mw; clip = 1; }
            a_resr[i] = (int32_t)(w * 8388607.0f);
        }
#endif
    }
    a_gv = gv;
#if PX_OWN_FX
    a_res_sp = (uint8_t)(sp ? a_mode : 0u);            /* (test build 43: which split, for USB SEND, px_uaudio.h) */
#endif
    if (clip) a_clip++;
}
static void dsp_load(uint32_t t0, uint32_t lim)        /* the block's load, the safety bypass (over lim % 8 blocks running) */
{
    uint32_t pct = (fm1_ticks() - t0) * 100u / A_BUDGET;
    a_lds += pct - a_lds / 8u;
    a_load = a_lds / 8u;
    if (pct > a_peak) a_peak = pct;
    if (pct > a_peak_all) a_peak_all = pct;
    if (pct >= 100u && !a_hold) { a_xrun++; a_xrun_at = a_blocks; }
    if (!a_bypass && !a_hold) {
        if (pct > lim) { if (++a_over >= 8u) a_bypass = 1; } else a_over = 0;
    }
    a_blocks++;
}

/* --- second core: block N goes to CPU1 at the end of IRQ N; IRQ N+1 collects the result before
 * sending it out. Same latency as before (processing already had to finish before the next block), but CPU0
 * is free between IRQs: display and UI never touch the audio again. Automatic fallback to CPU0
 * if the 2nd core rejects a job or misbehaves (FM1_CPU1_FALLBACK 1, Felucca's fm1_cpu1.h). */
static volatile uint8_t a_c1;                          /* 1 = DSP runs on the 2nd core */
static uint8_t a_job;                                  /* a block is in progress on CPU1 */
/* v39: the wet lanes (px_dsp.h px_wet_*, docs/STOCK-FIRMWARE.md 7.3): with the 2nd core on, the delay and the reverb make
 * their sound beside the chain, two blocks late (their taps make up for it):
 *  - one core's chain (no NAM amp): CPU1 runs the chain, CPU0 the lanes at the same time, in the interrupt (cut short,
 *    and counted, if they ever reach 85 % of the block);
 *  - a NAM amp on two cores: CPU1 runs the lanes first in its share (post_job), where the delay and the reverb ran in
 *    v38; the network's sharing between the cores takes their time into account as before.
 * v39.1: the lanes of a NAM preset no longer run after the block, in the time left before the next interrupt: there was
 * too little of it (the network shares its work between the cores, so the lanes' time only half came back), they were
 * cut short every block, and the echoes and the reverb came out chopped at the block rate (a whistle). */
#ifndef OP_WET_LANES
#define OP_WET_LANES 1                                 /* 0: the delay and the reverb in the chain, as in v38 */
#endif
#if DSP_CPU1
#define A_WLAG (2 * (int)A_FR)
#ifndef OP_NAM_LP
#define OP_NAM_LP 1                                    /* v40.2: 0 = the network split between the cores by frames, as v40 */
#endif
static void a_c1_off(void)                             /* one core from now on: no lanes, no layer split */
{
    a_c1 = 0;
    A_WCUT_KEEP();
    px_wet_mode(&g_eng, 0, 0);
    if (g_eng.nam_lp) { px_nam_lp(&g_eng, 0); a_nan = 1; }   /* (a block CPU1 left half done: the engine is reset) */
}
static void dsp_job(uint32_t arg) { (void)arg; dsp_block(); }
#endif
#if DSP_CPU1
/* the NAM network's helper core (CPU1), for its two-core modes: the job functions (px_nam_helper) */
static int a_hrun(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
static int a_hwait(void) { return fm1_cpu1_wait(); }
/* v37: a NAM amp on two cores, pipelined (one block more latency: 2.9 ms). In each block's interrupt CPU1 first runs
 * the blocks after the amp on the previous block's network output (and makes its output), while CPU0 runs the blocks
 * before the amp on this block; then both run the network on this block (CPU1 as its helper). PRE and POST never touch
 * the same block's state (tests/engine/pipe_test.c: the same output as one core, one block later). */
static float a_nb[2][A_FR];                            /* the network's output: being made / the previous block's */
static uint8_t a_nbi, a_nbv, a_prun;                  /* a_nb[a_nbi] is this block's; a_nbv: the other one holds one */
static void post_run(uint32_t prev, int run, int nbv)   /* run: the engine on; nbv: a_nb[prev] holds a block */
{
    if (g_eng.wl_on) px_wet_run(&g_eng, 0u, 0);       /* v39.1: the lanes (the previous block's sends), first */
#if PX_OWN_FX
    a_mode = px_out_mode; a_split = a_mode != 0u;
#endif
    if (run && nbv) {
        for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK) {
#if PX_OWN_FX
            A_BASS(o);
            A_RET(PX_PART_POST, o);
#endif
            px_engine_part(&g_eng, a_nb[prev] + o, a_out + o, PX_MAX_BLOCK, PX_PART_POST);
        }
    } else if (!run) {
        for (uint32_t i = 0; i < A_FR; i++) a_out[i] = a_in[i];
    } else for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;   /* the pipeline's first block */
#if PX_OWN_FX
    if (!(run && nbv)) for (uint32_t i = 0; i < A_FR; i++) a_outr[i] = 0.0f;
#endif
    dsp_out();
}
static void post_job(uint32_t prev) { post_run(prev, a_prun, a_nbv); }
/* diagnostic 22: CPU1 then goes straight on to the network (pxn_serve), taking the last frames of each 32-frame block
 * while CPU0 takes the first ones and never waits; CPU0 takes more frames in the first blocks by how late CPU1 came
 * to the network in the blocks before (a_late, ticks: POST's end against PRE's) */
static volatile uint32_t a_tpost;                      /* (CPU1) when POST was done */
static int32_t a_late;
static void pipe_job(uint32_t prev)
{
    post_job(prev);
    a_tpost = fm1_ticks();
    pxn_serve();
}
static void dsp_pipe_run(int run)                      /* run: the engine on (else bypassed) */
{
    const uint32_t prev = a_nbi ^ 1u;
    a_commit();                                        /* (before CPU1 starts: the engine is all ours here) */
    a_prun = (uint8_t)run;
    if (run) pxn_srv_begin(a_late > 0 ? a_late : 0);
    const int job = fm1_cpu1_run(run ? pipe_job : post_job, prev);
    if (run && !job) pxn_srv_end();
    if (run) for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK) {
#if PX_OWN_FX
        A_RET(PX_PART_PRE, o);
#endif
        px_engine_part(&g_eng, a_in + o, a_nb[a_nbi] + o, PX_MAX_BLOCK, PX_PART_PRE);
    }
    const uint32_t tpre = fm1_ticks();
    if (!job) post_job(prev);                          /* (CPU1 off: here) */
    if (run) for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK)
        px_engine_part(&g_eng, a_nb[a_nbi] + o, a_nb[a_nbi] + o, PX_MAX_BLOCK, PX_PART_NAM);
    if (job) {
        if (run) pxn_srv_end();
        if (fm1_cpu1_wait() < 0) { a_c1_off(); post_job(prev); }   /* (dropped: here, and one core from now on) */
        else if (run) a_late += ((int32_t)(a_tpost - tpre) - a_late) / 4;
    }
    a_nbv = (uint8_t)run;
    a_nbi = (uint8_t)prev;
}
/* v40.2: the network split between the cores by layers (engine/px_nam.c pxn_lp_*). CPU0 runs the blocks before the amp
 * and the network's first layers in each block's interrupt; CPU1 runs the blocks after the amp on the block before's
 * network output, the network's other layers and its head. Every layer runs on whole blocks: no per-layer waits; the
 * output is the same (tests/engine/lp_test.c).
 * v40.4: CPU1 works on through the interrupts. Until now its job ended with the block's last network block, which CPU0
 * hands over at the end of its own share: CPU1 idled at the start of each block, the next interrupt waited for it, and
 * the two cores' work in a block had to fit one after the other (the blocks before the amp + CPU0's share of the
 * network + one network block on CPU1 within 2.9 ms): a chain could run only up to about 88 % a core, and after a full
 * change, when the interrupt runs block after block to catch up, the wait counted as load and engaged the bypass on
 * chains that fit. Now CPU1 runs one worker (lp_job) for as long as the layer split is on: each interrupt asks it for
 * the blocks after the amp (a_lpreq); it first finishes the network blocks handed over before the request (their
 * output is what those blocks play), runs them, and goes back to the network. The interrupt waits only for the
 * output it sends (a_lp_post, in a_dec) and never for the network: the blocks handed over late in a block are finished
 * during the next one. Each core's work may now fill its block. The load shown is the busier core's: CPU0's interrupt
 * (its waits included), or what CPU1 has to do in a block (the blocks after the amp, as timed, and its share of the
 * network). How many layers CPU0 takes follows from the two cores' other work, as timed, and the network's parts
 * (px_nam_lp_share): set in one step, not layer by layer. */
static volatile uint8_t a_lpquit;                      /* (CPU0) the worker must return */
static volatile uint32_t a_lpreq, a_lpdone;            /* requests for the blocks after the amp: asked (CPU0), served (CPU1) */
static volatile uint32_t a_lparg, a_lpupto;            /* the request: prev | nbv << 1 | run << 2; the network blocks before it */
static volatile uint32_t a_lptp;                       /* (CPU1) its last blocks after the amp, lanes and output included (ticks) */
static uint8_t a_lpw;                                  /* the worker runs */
static uint32_t a_lpc0, a_decw;                        /* CPU0's own share last block; its wait for the output (ticks) */
static uint32_t a_lpc1;                                /* CPU1's work for a block: a_lptp + its share of the network (ticks) */
static uint32_t a_lpta;                                /* CPU0's share of the network last block (ticks) */
static uint32_t a_lpe0, a_lpe1;                        /* the cores' other work, smoothed (ticks x 4; 0: not timed yet) */
static uint32_t a_encw;                                /* a_enc's wait for the output (ticks: not CPU0's work) */
static uint8_t a_lpreal;                               /* the last request ran the blocks after the amp (a_lptp is theirs) */
static uint8_t a_lpstuck;                              /* blocks in a row whose share stayed below the one asked */
static uint8_t a_lpnew;                                /* blocks timed since the preset was set (up to 6) */
/* the network's work in layers 0..s, in 1/1024 (px_nam_lp_share), in the RAM code window's free end (app.ld .bss_hi) */
static uint16_t a_lpsh[PXN_NL] __attribute__((section(".bss.hi.a_lpsh")));
static uint32_t a_cl[2], a_cpk[2];                     /* v40.2: each core's load (%, smoothed) and peak since the preset was set */
static void lp_job(uint32_t arg)
{
    uint32_t done = a_lpdone;
    (void)arg;
    for (;;) {
        fm1_cpu1_sync();
        if (a_lpreq != done) {                         /* an interrupt asked for the blocks after the amp */
            const uint32_t q = a_lparg, upto = a_lpupto;
            while (px_nam_lp_before(upto)) px_nam_lp_b(&g_eng);   /* (the network blocks they play, first) */
            const uint32_t t0 = fm1_ticks();
            post_run(q & 1u, (int)(q >> 2 & 1u), (int)(q >> 1 & 1u));
            a_lptp = fm1_ticks() - t0;
            fm1_cpu1_sync();
            a_lpdone = ++done;                         /* (the output is there: a_dec may send it) */
            fm1_cpu1_sync();
            continue;
        }
        if (a_lpquit) break;                           /* (asked to return: the network blocks left stay queued) */
        px_nam_lp_b(&g_eng);                           /* (the oldest network block handed over, if any) */
    }
}
/* the blocks after the amp asked by the interrupt before are done: a_res is whole, and the lanes and those blocks'
 * state are no longer in use (CPU1 is in the network, or idle) */
static void a_collect(void);
static uint32_t a_lp_post(void)                        /* returns the ticks it waited */
{
    if (!a_lpw) return 0u;
    const uint32_t t = fm1_ticks();
    while (a_lpdone != a_lpreq && fm1_cpu1_busy()) {
        fm1_cpu1_sync();
        /* v40.5: not for ever. The worker is one job for as long as the split runs, so fm1_cpu1_busy stays 1 and
         * fm1_cpu1_wait's limit never applies here: a CPU1 that stopped without a fault would hold the interrupt until
         * the watchdog. As fm1_cpu1_wait: past FM1_CPU1_TIMEOUT_US it is retired (LATE), a_collect then goes to one core */
        if (fm1_ticks() - t >= FM1_CPU1_TIMEOUT_US * FM1_TICKS_PER_US) { fm1_cpu1_retire(FM1_CPU1_LATE); break; }
    }
    if (a_lpdone != a_lpreq) a_collect();              /* (its worker ended without it: dropped) */
    return fm1_ticks() - t;
}
static void lp_split(uint32_t e0, uint32_t e1)         /* the split for the cores' other work e0, e1 (ticks) */
{
    /* CPU0: e0 + n sh(s), CPU1: e1 + n (1 - sh(s)), n the network's ticks for a block: the s that brings them
     * closest; the split in use stays unless another is better by half a layer's work (a layer: n / 25) */
    const int32_t n = (int32_t)(pxn_frame_ticks() * A_FR), d = (int32_t)e0 - (int32_t)e1 - n;
    const int now = px_nam_lp_split_used();
    int best = now;
    int32_t fb = d + ((n >> 5) * (int32_t)a_lpsh[now] >> 4);          /* (n sh / 512 = 2 n sh / 1024) */
    if (fb < 0) fb = -fb;
    fb -= n / 50;
    for (int k = 0; k < PXN_NL; k++) {
        int32_t f = d + ((n >> 5) * (int32_t)a_lpsh[k] >> 4);
        if (f < 0) f = -f;
        if (f < fb) { fb = f; best = k; }
    }
    px_nam_lp_split(best);
    /* CPU0 takes more layers only as CPU1 has finished them for the blocks before, without waiting for it. With CPU1
     * behind (it is the busier one) that never comes: for 2 layers or more, or after 4 blocks, CPU0 waits for it once */
    a_lpstuck = (uint8_t)(best > now ? a_lpstuck + 1u : 0u);
    if (best >= now + 2 || a_lpstuck >= 4u) { px_nam_lp_force(); a_lpstuck = 0; }
}
static void dsp_lp_run(int run, uint32_t t_irq)
{
    const uint32_t prev = a_nbi ^ 1u;
    a_commit();                                        /* (CPU1 is in the network, or idle: the blocks are all ours here) */
    if (run && a_lpc0) {                               /* the cores' other work last block; the split from it */
        /* (as timed for the first 6 blocks, the lanes coming in two blocks late; then smoothed) */
        const uint32_t o0 = a_lpc0 > a_lpta ? a_lpc0 - a_lpta : 0u, o1 = a_lptp;
        const int fresh = a_lpnew < 6u;
        if (fresh) a_lpnew++;
        a_lpe0 = a_lpe0 && !fresh ? a_lpe0 - a_lpe0 / 4u + o0 : o0 * 4u;
        if (a_lpreal) {                                /* (not the pipeline's first block: silence) */
            if (!a_lpe1) { a_peak = 0; a_cpk[0] = a_cpk[1] = 0; }   /* (the peaks: from the first split set for this preset) */
            a_lpe1 = a_lpe1 && !fresh ? a_lpe1 - a_lpe1 / 4u + o1 : o1 * 4u;
        }
        if (a_lpe1) lp_split(a_lpe0 / 4u, a_lpe1 / 4u);
    }
    a_lpreal = (uint8_t)(run && a_nbv);
    a_prun = (uint8_t)run;
    a_lparg = prev | (uint32_t)a_nbv << 1 | (uint32_t)(run != 0) << 2;
    a_lpupto = px_nam_lp_handed();
    fm1_cpu1_sync();
    a_lpreq++;
    fm1_cpu1_sync();
    if (!a_lpw) {                                      /* the worker, started (CPU1 off: its work here) */
        a_lpquit = 0;
        fm1_cpu1_sync();
        if (fm1_cpu1_run(lp_job, 0)) { a_lpw = 1; a_job = 1; }
        else {
            while (px_nam_lp_b(&g_eng)) { }
            post_run(prev, run, a_nbv);
            a_lpdone = a_lpreq; a_lptp = 0;
        }
    }
    uint32_t ta = 0;
    if (run) {
        for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK) {
#if PX_OWN_FX
            A_RET(PX_PART_PRE, o);
#endif
            px_engine_part(&g_eng, a_in + o, a_nb[a_nbi] + o, PX_MAX_BLOCK, PX_PART_PRE);
        }
        ta = fm1_ticks();
        if (!a_lpe1 && a_lpw) {
            /* the first two blocks after a change: CPU1's other work is not timed yet, and it has little (the pipeline
             * and the lanes fill): CPU0 takes as many layers as fit in 80 % of its block, so that the split then only
             * has to come down (CPU1 takes layers over at once; CPU0 only as CPU1 has finished them) */
            const uint32_t n = pxn_frame_ticks() * A_FR, used = ta - t_irq;
            int s = PXN_NL - 1;
            while (s > 0 && used + (n >> 5) * a_lpsh[s] / 32u > A_BUDGET * 80u / 100u) s--;
            px_nam_lp_split(s);
        }
        for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK)
            px_engine_part(&g_eng, a_nb[a_nbi] + o, a_nb[a_nbi] + o, PX_MAX_BLOCK, PX_PART_NAM);
        /* v40.5: a wait for CPU1 in the network gave up (px_nam.c pxn_lp_late): CPU1 stopped without a fault. Retired
         * (LATE): a_dec's a_lp_post then finds it gone and a_collect goes to one core, the engine reset (a_c1_off) */
        if (px_nam_lp_gave_up()) fm1_cpu1_retire(FM1_CPU1_LATE);
        ta = fm1_ticks() - ta;
        if (!a_lpw) while (px_nam_lp_b(&g_eng)) { }
    }
    a_lpta = ta;
    a_lpc0 = run && a_lpw ? fm1_ticks() - t_irq - a_encw + a_uat : 0u;   /* (from the interrupt: the input, the tuner's samples; USB audio) */
    /* CPU1's work for a block: the blocks after the amp as last timed, and its share of the network at this split */
    {
        const uint32_t n = pxn_frame_ticks() * A_FR;
        a_lpc1 = run && a_lpw ? a_lptp + (n >> 5) * (1024u - a_lpsh[px_nam_lp_split_now()]) / 32u : 0u;
    }
    a_nbv = (uint8_t)run;
    a_nbi = (uint8_t)prev;
}
static void dsp_pipe(uint32_t t_irq)
{
    uint32_t t0 = fm1_ticks();
    if (g_eng.nam_lp) {
        dsp_lp_run(!a_bypass && !a_hold, t_irq);
        const uint32_t c1 = a_lpc1, c0 = fm1_ticks() - t_irq + a_decw + a_uat;
        t0 = fm1_ticks() - (c1 > c0 ? c1 : c0);         /* the busier core */
        for (int k = 0; k < 2; k++) {                   /* each core's own (PING's profile) */
            const uint32_t pc = (k ? c1 : c0) * 100u / A_BUDGET;
            a_cls[k] += pc - a_cls[k] / 8u;
            a_cl[k] = a_cls[k] / 8u;
            if (pc > a_cpk[k]) a_cpk[k] = pc;
        }
    } else { dsp_pipe_run(!a_bypass && !a_hold); t0 -= a_uat; }   /* (USB audio's time in a_dec: CPU0's too) */
    dsp_load(t0, 93u);                                 /* (CPU0's whole block here, the UI gets the rest) */
}
#endif
static void a_collect(void)                            /* collects the block in progress on CPU1 */
{
#if DSP_CPU1
    if (a_job) {
        a_job = 0;
        if (a_lpw) {                                   /* v40.4: the layer split's worker: asked to return (it serves */
            fm1_cpu1_sync();                           /* a request it has first) */
            a_lpquit = 1;
            fm1_cpu1_sync();
        }
        if (fm1_cpu1_wait() < 0) a_c1_off();           /* dropped: CPU0 takes over (this block is lost) */
        if (a_lpw) {
            a_lpw = 0;
            a_lpdone = a_lpreq;                        /* (served before it returned, or dropped with it) */
            while (px_nam_lp_b(&g_eng)) { }            /* (the network blocks it left, finished here) */
        }
    }
#endif
}

#if DSP_CPU1 && OP_BLE_TEST
/* the Bluetooth images turn the flash path off with only CPU0's interrupts off (px_usb.h ota_fread; the radio's start-up burst,
 * fm1_ble_rf.h): nothing of CPU1's may run from the flash then. A block's job is waited for (fm1_cpu1_wait). v41's layer-split
 * worker never ends while the split runs, so fm1_cpu1_wait would retire it (LATE) after 8 ms: it is waited for until it idles in
 * its loop (RAM code) with no request and no network block left, within the same 8 ms. A core that faulted or misread is held
 * (fm1_cpu1_wait retires it): its fault handler parks in the flash. A core retired, here or before, is collected here as v42's
 * own retirements are (a_collect: one core, the layer split and the lanes off, the queue emptied, the engine reset): the next
 * interrupt would otherwise hand network blocks to the stopped core and wait 8 ms for them (pxn_lp_a), then retire it again
 * as LATE over a fault's G 4. Both callers have the flash on and CPU0's interrupts off */
static void a_cpu1_quiet(void)
{
    if (a_lpw) {
        const uint32_t t = fm1_ticks();
        while ((a_lpdone != a_lpreq || px_nam_lp_queued()) && fm1_cpu1_busy()) {
            fm1_cpu1_sync();
            if (fm1_ticks() - t >= FM1_CPU1_TIMEOUT_US * FM1_TICKS_PER_US) { fm1_cpu1_retire(FM1_CPU1_LATE); break; }
        }
        if (fm1_cpu1_on && !(fm1_cpu1_fault.count || fm1_cpu1_mb.rejected)) return;   /* (idle in its loop) */
    }
    if (fm1_cpu1_wait() < 0) a_collect();             /* (retired, or off: nothing of the worker's left to the interrupt) */
}
#define FM1_CPU1_QUIET() a_cpu1_quiet()                /* (fm1_ble_rf.h's burst) */
#endif

/* ---- tuner: the interrupt stores the input decimated by 4 (11025 Hz); the main loop estimates the note ---- */
#define TN_N 1024u
#if OP_BLE_TEST   /* the Bluetooth test image: the buffers in the cache RAM (app.ld CRAM); without it (pxc_init failed) no tuner */
#define TN_MEM __attribute__((section(".cram.tuner")))
#define TN_READY (pxc_status == PXC_READY)
#else
#define TN_MEM
#define TN_READY 1
#endif
static float tn_buf[TN_N] TN_MEM;
static volatile uint32_t tn_w;
static void tn_capture(const float *x, uint32_t n)
{
    if (!TN_READY) return;
    uint32_t w = tn_w;
    for (uint32_t i = 0; i + 3u < n; i += 4u) {
        tn_buf[w] = (x[i] + x[i + 1u] + x[i + 2u] + x[i + 3u]) * 0.25f;
        w = (w + 1u) & (TN_N - 1u);
    }
    tn_w = w;
}
#define TN_W 600u
#define TN_TMAX 180u
static float tn_lin[TN_W + TN_TMAX] TN_MEM;   /* the newest W + TMAX samples in order: all the difference function reads */
static float tn_d[200] TN_MEM;
static float tuner_estimate(void)      /* frequency in Hz, or 0 (silence / unclear). YIN algorithm. */
{
    const uint32_t W = TN_W, TMIN = 10u, TMAX = TN_TMAX;  /* 61 Hz .. 1100 Hz at 11025 Hz */
    uint32_t w = tn_w;
    float e = 0.0f;
    if (!TN_READY) return 0.0f;
    for (uint32_t i = 0; i < TN_N; i++) {              /* the energy over all TN_N samples, the copy of the newest */
        const float v = tn_buf[(w + i) & (TN_N - 1u)];
        e += v * v;
        if (i >= TN_N - W - TMAX) tn_lin[i - (TN_N - W - TMAX)] = v;
    }
    if (e < (float)TN_N * 1.0e-6f) return 0.0f;        /* too weak: nothing to tune */
    const float *x = tn_lin;
    float run = 0.0f;
    tn_d[0] = 1.0f;
    for (uint32_t t = 1; t <= TMAX; t++) {
        float d = 0.0f;
        for (uint32_t i = 0; i < W; i++) { float v = x[i] - x[i + t]; d += v * v; }
        run += d;
        tn_d[t] = (run > 0.0f) ? d * (float)t / run : 1.0f;
    }
    uint32_t best = 0;
    for (uint32_t t = TMIN; t < TMAX; t++)
        if (tn_d[t] < 0.15f) { while (t + 1u < TMAX && tn_d[t + 1u] < tn_d[t]) t++; best = t; break; }
    if (!best) {
        float m = 0.35f;
        for (uint32_t t = TMIN; t < TMAX; t++) if (tn_d[t] < m) { m = tn_d[t]; best = t; }
        if (!best) return 0.0f;
    }
    float a = tn_d[best - 1u], b = tn_d[best], c = tn_d[best + 1u], den = a - 2.0f * b + c;
    float tf = (float)best + ((den != 0.0f) ? 0.5f * (a - c) / den : 0.0f);
    return 11025.0f / tf;
}

/* v40.2: OP_TESTSIG 1 (measurements only, never a release): a synthetic guitar replaces the input, so the effects that
 * work only while something sounds (envelopes, pitch tracking, the synth's keys, tails) run as with a player: a note and
 * its fifth, sawtooth, plucked every 0.4 s at about -12 dBFS and decaying, the notes walking two octaves up and down */
#ifndef OP_TESTSIG
#define OP_TESTSIG 0
#endif
/* OP_UADEV 1 (measurements only, never a release): USB audio's figures for the pedal's timing (px_uaudio.h): when the
 * interrupt's two callbacks run, the USB poll's gaps, the rings' fills; a counter or the computer's playback in the
 * record stream in place of the sound, so a frame lost or repeated on the way shows exactly; a steady tone as the input;
 * the output blocks sent in the order the driver asks, as before v42 */
#ifndef OP_UADEV
#define OP_UADEV 0
#endif
#if OP_UADEV
static uint8_t ua_dev;                                 /* the modes (px_uaudio.h) */
static void uad_enc(uint32_t t);
static void uad_send(void);
static void uad_tone(float *x);
#define A_ORDER_OLD (ua_dev & 8u)
#else
#define A_ORDER_OLD 0u
#endif
#if OP_TESTSIG
static void a_testsig(void)
{
    static const float HZ[8] = {82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f, 196.0f, 110.0f};
    static float ph[2], env;
    static uint32_t n, k;
    for (uint32_t i = 0; i < A_FR; i++, n++) {
        if (n >= 17640u) { n = 0; k = (k + 1u) & 7u; env = 0.25f; }   /* a pluck every 0.4 s */
        const float f = HZ[k] / 44100.0f;
        ph[0] += f; if (ph[0] >= 1.0f) ph[0] -= 1.0f;
        ph[1] += f * 1.4983f; if (ph[1] >= 1.0f) ph[1] -= 1.0f;
        a_in[i] = env * ((2.0f * ph[0] - 1.0f) + 0.6f * (2.0f * ph[1] - 1.0f));
        env *= 0.99995f;
    }
}
#endif
#if OP_BT_AUDIO
/* test build 17: this block's music, read from the ring here, by CPU0, before the block is handed over (to CPU1, the pipeline or
 * dsp_block). The ring (bt_a2dp.c bt_ring_mem) lies in the radio's 64 KB window, which the second core must not touch: up to
 * test build 16 dsp_out read it there, on CPU1. The block before has left bt_mix: a_collect / a_lp_post waited for its
 * dsp_out. Not read while the fade sits at silence, or after a NaN, or muted: the music waits, it does not skip */
static void a_bt_fill(void)
{
    for (uint32_t i = 0; i < A_FR; i++) bt_mix[i] = bt_side[i] = 0.0f;
    if (!a_mute && !a_nan && !(a_xft == 0 && a_xf == 0.0f)) bt_ring_mix2(&bt_ring, bt_mix, bt_side, A_FR, bt_gain);
}
#endif
static void a_block(const uint8_t *data, int len)      /* a guitar block: its DSP started, or run */
{
#if DSP_CPU1
    const uint32_t t_irq = fm1_ticks();
#endif
#if DSP_CPU1
    /* a_in must no longer be read by CPU1. v40.4: the layer split's worker stays (it may be in the network): only its
     * blocks after the amp must be done */
    a_encw = 0;
    if (a_lpw && a_c1 && g_eng.nam_lp && px_nam_dual(&g_eng)) a_encw = a_lp_post(); else
#endif
    a_collect();
#if DSP_CPU1
    if (g_eng.wl_on) px_wet_swap(&g_eng);              /* v39: nothing runs: the lanes' buffers change roles */
#endif
    const uint32_t *s = (const uint32_t *)data;
    uint32_t n = (uint32_t)len / 8u;                   /* 2 32-bit words per frame */
    if (n > A_FR) n = A_FR;
    float sl = 0.0f, sr = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float l = (float)((int32_t)(s[2u * i] << 8) >> 8), r = (float)((int32_t)(s[2u * i + 1u] << 8) >> 8);
        sl += l * l; sr += r * r;
#if PX_OWN_FX
        a_in[i] = l * a_in_k;                          /* (test build 39: x0.5 and INPUT, -10..+10 dB) */
#else
        a_in[i] = l * (0.5f / 8388608.0f);             /* guitar = L channel only (like the official fw); R carries
                                                          only noise (~ -77 dBFS, measured v26). x0.5: same level
                                                          as the old (L + R) / 2, settings are unchanged */
#endif
    }
#if PX_OWN_FX
    if (a_inz_on) {                                    /* (test build 46: IN-Z, the pickup loaded) */
        float z1 = a_inz_z[0], z2 = a_inz_z[1];
        const float b0 = a_inz_c[0], b1 = a_inz_c[1], b2 = a_inz_c[2], a1 = a_inz_c[3], a2 = a_inz_c[4];
        for (uint32_t i = 0; i < n; i++) {
            const float x = a_in[i], y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y;
            a_in[i] = y;
        }
        a_inz_z[0] = z1; a_inz_z[1] = z2;
    }
#endif
    for (uint32_t i = n; i < A_FR; i++) a_in[i] = 0.0f;
    if (n) {                                           /* input levels L / R (tuner screen) */
        a_lvl[0] += (sl / (float)n - a_lvl[0]) * 0.0625f;
        a_lvl[1] += (sr / (float)n - a_lvl[1]) * 0.0625f;
    }
#if OP_TESTSIG
    a_testsig();
#endif
#if OP_UADEV
    uad_tone(a_in);
#endif
    tn_capture(a_in, A_FR);
#if OP_BT_AUDIO
    a_bt_fill();                                       /* (the music, on CPU0: the ring is in the radio's window) */
#endif
#if PX_OWN_FX && OP_UAUDIO
    px_ua_chain();                                     /* (test build 43: USB RETURN, on CPU0 as the USB rings are) */
#endif
#if DSP_CPU1
    /* a NAM amp on two cores (px_nam_dual): pipelined here (dsp_pipe), CPU1 its helper */
    if (a_c1 && !px_nam_dual(&g_eng)) {
        a_nbv = 0;
        a_commit();                                    /* v39: here, before the lanes run beside the chain */
        if (fm1_cpu1_run(dsp_job, 0)) {
            a_job = 1;
            if (g_eng.wl_on) px_wet_run(&g_eng, t_irq + A_BUDGET * 85u / 100u, 0);   /* v39: the lanes, on CPU0 */
            return;
        }
        a_c1_off();
    }
    if (a_c1) { dsp_pipe(t_irq); return; }            /* (the lanes: in post_job) */
    a_nbv = 0;
#endif
    dsp_block();
}
static void a_send(int32_t *d, uint32_t n)             /* an output block filled: the block a_block just started */
{
#if OP_UADEV
    uad_send();
#endif
#if DSP_CPU1
    a_decw = 0;
    if (a_lpw) a_decw = a_lp_post();                   /* v40.2: the layer split: only its output (the worker goes on) */
    else
#endif
    a_collect();                                       /* the previous block must be finished */
    if (n > A_FR) n = A_FR;
#if PX_OWN_FX
    if (a_res_sp) for (uint32_t i = 0; i < n; i++) { d[2u * i] = a_res[i]; d[2u * i + 1u] = a_resr[i]; }   /* (L, R) */
    else
#endif
    for (uint32_t i = 0; i < n; i++) d[2u * i] = d[2u * i + 1u] = a_res[i];
#if OP_UAUDIO
    if (a_ua) {                                        /* v42: USB audio */
        const uint32_t t = fm1_ticks();
        px_ua_block(d, n);
        a_uat = fm1_ticks() - t;
    }
#endif
}
#if OP_BT_AUDIO
/* test build 14: the radio's interrupts run above the audio's, so between a_dec and a_enc of one entry they can stay longer than
 * the 10 kHz timer's 40 us v42 measured. A noted block plays one block after its output callback was due: an a_enc within half a
 * block of its note is still the same entry's, and fills it in time while the entry's lateness, that gap and the DSP's wait stay
 * under a block. A note dropped leaves that half's old block to play again (a click). The isr line counts them (bt/bt_a2dp.c):
 * a_ord_drop the notes dropped, a_ord_age the longest from a note to its a_enc (ticks; this second, since boot) */
#define A_ORDER_TICKS (A_BUDGET / 2u)
static volatile uint32_t a_ord_drop, a_ord_age_s, a_ord_age;
#define A_ORDER_SEEN(filled) do { const uint32_t g_ = fm1_ticks() - a_dqt; if (!(filled)) a_ord_drop++; \
        if (g_ > a_ord_age_s) a_ord_age_s = g_; if (g_ > a_ord_age) a_ord_age = g_; } while (0)
#else
#define A_ORDER_TICKS (A_BUDGET / 4u)                  /* an a_enc this soon after an a_dec is the same interrupt's (it comes within microseconds) */
#endif
#include "px_aorder.h"                                /* v42: a_enc and a_dec, in whichever order the driver calls them */
#if OP_BLE_TEST
/* the Bluetooth test images time the audio interrupt (the log's isr line, bt/bt_a2dp.c): the longest gap between two entries
 * (2.9 ms when on time: more is an interrupt that started late, which a_xrun, timing the DSP alone, does not see) and the
 * longest stay, this second (the main loop turns it over) and since boot; in TIMER4 ticks. A gap across a hold of the interrupt
 * (a preset change, a flash write) is not counted: a_irq_gap_skip() before each unmask */
static volatile uint32_t a_irq_t0, a_irq_gap_s, a_irq_gap, a_irq_in_s, a_irq_in;
static inline void a_irq_gap_skip(void) { a_irq_t0 = 0; }
void fm1_alnk0_irq(void)
{
    const uint32_t t0 = fm1_ticks();
    uint32_t d;
#if OP_BT_AUDIO
    {   /* test build 12: the half-done flags this entry finds (ALNK0 CON2 bits 4-7, 0x12E00 + 8: iis.bc struct iis_reg's field 4;
         * a read, the handler clears them): none, the output's only, the input's only, both, another channel's */
        const uint32_t f = (uint32_t)*(volatile uint8_t *)0x12E08u >> 4;
        a_en_s[f & 3u]++;
        if (f & 0xCu) a_en_s[4]++;
    }
#endif
    if (a_irq_t0) {
        d = t0 - a_irq_t0;
        if (d > a_irq_gap_s) a_irq_gap_s = d;
        if (d > a_irq_gap) a_irq_gap = d;
    }
    a_irq_t0 = t0 ? t0 : 1u;
    iis_irq_handler(0);
    d = fm1_ticks() - t0;
    if (d > a_irq_in_s) a_irq_in_s = d;
    if (d > a_irq_in) a_irq_in = d;
}
#else
static inline void a_irq_gap_skip(void) { }
void fm1_alnk0_irq(void) { iis_irq_handler(0); }
#endif
extern void isr_alnk0(void);

/* ===================== UI -> engine: same structure (px_preset_t), plain copy ===================== */
static void ui_to_px(px_preset_t *pp) { *pp = PRESET; }
static uint32_t ui_hash(void)                          /* hash of the whole preset (volume handled separately) */
{
    const uint8_t *b = (const uint8_t *)&PRESET;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof(PRESET); i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
static int a_wait(volatile uint8_t *v, uint8_t until, uint32_t ms)   /* 1 = *v reached until within ms */
{
    for (uint32_t t = 0; *v != until; t++) { if (t >= ms * 10u) return 0; fm1_delay_us(100); }
    return 1;
}
#if OP_BT_AUDIO
/* Before the audio interrupt is held for a recompute: wait until silent blocks fill both halves of the output DMA, which keeps
 * playing its two halves over and over while the interrupt is held. A block reaches the DMA in the interrupt it is computed in
 * (px_aorder.h), one later through the NAM pipeline, and the fade can read 0 before its block is counted: 5 counted blocks,
 * 14.5 ms, for margin, bounded at 30. Up to test build 10 the hold began as soon as the fade read 0, while the halves still held
 * the last full-level block and a half-faded one: a buzz at the DMA's 172 Hz for the length of the recompute, on every full
 * preset change (every image) */
static void a_quiet_dma(void)
{
    const uint32_t b0 = a_blocks;
    for (uint32_t t = 0; a_blocks - b0 < 5u && t < 300u; t++) fm1_delay_us(100);
}
#else
#define a_quiet_dma() ((void)0)
#endif
static void audio_apply(void)                          /* the preset to the engine (main loop) */
{
    px_preset_t pp;
    ui_to_px(&pp);
    /* v38: knob changes and blocks on / off go to the audio side, which applies them at the start of a block */
    const int live = a_running && !a_mute && a_wait(&a_kpend, 0, 50u);
    if (live && !a_full) {
        const int ch = px_engine_change(&g_eng, &pp);
        if (ch == PX_CH_NONE) return;
        if (ch == PX_CH_KNOBS) {
            px_engine_prepare(&g_eng, &pp);            /* the cabinet's heavy part, here, while the audio plays */
            a_kp = pp;
            __asm__ volatile("" ::: "memory");          /* (a_kp and the cabinet store written before the flag) */
            a_kpend = 1;
            return;
        }
    }
    a_full = 0;
    a_swc = a_sw0;
    /* anything else: the output fades out, the engine is set (the interrupt held off meanwhile: a late block now only
     * repeats silence), and the output fades back in */
    if (live) {
        a_xft = 0;
        for (uint32_t t = 0; a_xf != 0.0f && t < 400u; t++) fm1_delay_us(100);   /* (40 ms at most) */
        a_quiet_dma();
    }
    const uint32_t t0 = fm1_ticks();
    if (a_running) fm1_irq_mask(FM1_IRQ_ALNK0);
    a_collect();                                       /* CPU1 must not process during the recompute */
#if DSP_CPU1
    while (px_nam_lp_b(&g_eng)) { }                    /* v40.2: the layer split's blocks in flight, finished */
#endif
    px_engine_set(&g_eng, &pp);
#if DSP_CPU1
    px_nam_lp(&g_eng, a_c1 && OP_NAM_LP && px_nam_dual(&g_eng));
#endif
    a_swset = fm1_ticks() - t0;
    a_kpend = 0;
#if DSP_CPU1
    a_nbv = 0;                                         /* (the pipeline restarts from silence) */
#endif
    a_bypass = 0; a_over = 0; a_peak = 0; a_cpk[0] = a_cpk[1] = 0;
#if DSP_CPU1
    a_lpc0 = 0; a_lpe0 = a_lpe1 = 0; a_lptp = 0; a_lpreal = 0; a_lpstuck = 0; a_lpnew = 0;   /* v40.4: the cores' other work is timed anew */
#endif
    if (a_xf != 1.0f) a_swin = 1; else a_sw1 = fm1_ticks();   /* (not faded out: the audio was not running) */
    a_irq_gap_skip();
    if (a_running) fm1_irq_unmask(FM1_IRQ_ALNK0);
    a_xft = 1;
}
static void audio_reset(void)                          /* after a NaN: engine reset, preset re-applied */
{
    px_preset_t pp;
    ui_to_px(&pp);
    if (a_running) a_quiet_dma();                      /* (a_nan: the blocks since are silent, the music too) */
    if (a_running) fm1_irq_mask(FM1_IRQ_ALNK0);
    a_collect();
    A_WCUT_KEEP();
    px_engine_init(&g_eng, 44100.0f);
#if DSP_CPU1
    px_wet_mode(&g_eng, a_c1 && OP_WET_LANES, A_WLAG);   /* (the init cleared it) */
#endif
    px_engine_set(&g_eng, &pp);
#if DSP_CPU1
    px_nam_lp(&g_eng, a_c1 && OP_NAM_LP && px_nam_dual(&g_eng));   /* (the init cleared it; the network was reset) */
#endif
    a_kpend = 0; a_xf = 0.0f; a_xft = 1;               /* (back from silence) */
    a_nan = 0; a_nan_count++;
    a_irq_gap_skip();
    if (a_running) fm1_irq_unmask(FM1_IRQ_ALNK0);
}
static void audio_volume(uint32_t vol)                 /* 0..100 -> gain (quadratic curve, ~logarithmic) */
{
    float v = (float)vol * 0.01f;
    a_gain = v * v;
}


/* ===================== boot benchmark screen (6 s): CPU/FPU speed and engine cost ===================== */
static void b_num(char *d, uint32_t v)
{
    char t[10]; int n = 0;
    while (*d) d++;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 9);
    while (n) *d++ = t[--n];
    *d = 0;
}
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static uint32_t b_engine(const px_preset_t *pp)        /* % of the real-time budget for 128 frames */
{
    px_engine_set(&g_eng, pp);
#if PX_OWN_FX
    px_engine_out(&g_eng, PX_OUT_MONO, 0);
#endif
    for (uint32_t i = 0; i < A_FR; i++) a_in[i] = 0.3f * sinf((float)i * 0.0157f) + 0.05f * sinf((float)i * 0.11f);
    for (int w = 0; w < 4; w++) { px_engine_process(&g_eng, a_in, a_out, 64); px_engine_process(&g_eng, a_in + 64, a_out + 64, 64); }
    fm1_wdt_feed();
    uint32_t t0 = fm1_ticks();
    for (int r = 0; r < 8; r++) { px_engine_process(&g_eng, a_in, a_out, 64); px_engine_process(&g_eng, a_in + 64, a_out + 64, 64); }
    uint32_t dt = (fm1_ticks() - t0) / 8u;
    fm1_wdt_feed();
    return dt * 100u / A_BUDGET;
}
static void b_fix2(char *d, uint32_t v100)           /* 123 -> "1.23" */
{
    b_num(d, v100 / 100u);
    b_str(d, ".");
    if (v100 % 100u < 10u) b_str(d, "0");
    b_num(d, v100 % 100u);
}
static uint32_t b_ticks(void) { return fm1_ticks(); }
/* v37: what the NAM network costs on the pedal, piece by piece (cycles at 360 MHz: ticks x 15). Off by default:
 * BENCH_AT_BOOT 1 (at the top of this file) shows it for 12 s at boot. */
static void audio_bench(void)
{
#if BENCH_AT_BOOT
    char l[48];
    uint32_t y = 26;
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_text(4, 4, "DSP BENCH " OP_VERSION, UI_ACC, UI_BG, 2);
    /* integer loop and float loop: time per iteration (ns) */
    volatile uint32_t ia = 0; volatile float fa = 0.0f, fb = 1.0001f, fc = 0.5f;
    uint32_t t0 = fm1_ticks();
    for (uint32_t i = 0; i < 100000u; i++) ia += i;
    uint32_t ti = fm1_ticks() - t0;
    t0 = fm1_ticks();
    for (uint32_t i = 0; i < 100000u; i++) fa = fa * fb + fc;
    uint32_t tf = fm1_ticks() - t0;
    fm1_wdt_feed();
    (void)ti; (void)tf;
    l[0] = 0; b_str(l, "NAM ROUTINE: "); b_str(l, pxn_asm_in_use() ? "HAND (OK)" : "C (CHECK FAILED)");
    lcd_text(4, y, l, pxn_asm_in_use() ? GRN : RED, UI_BG, 1); y += 14;
    /* v37 diagnostic 22: the ways of running the network as timed at start-up (% of real time, NAM alone): the CPU, the
     * unit in turn, both at once with the CPU taking 0, 4 or 8 frames of 16; two cores (CPU1 taking 16 or 12 frames of
     * 32), with the unit beside CPU0 or not. RDOT: the layer read straight from its ring (no gather), against the gathered
     * X rows (the CPU alone); FAST RAM: the vector unit's inputs in the data-cache ways (px_cacheram.h) */
    {
        l[0] = 0; b_str(l, "RDL ");                /* diagnostic 20: a layer's groups in one call */
        if (pxn_rdl_state()) b_num(l, pxn_rdot_ticks(3) * 100u / (A_BUDGET * 2u)); else b_str(l, "FAIL");
        b_str(l, "% RDQ "); b_str(l, pxn_rdq_checked() ? "" : "(FAILED) ");
        b_num(l, pxn_rdot_ticks(2) * 100u / (A_BUDGET * 2u)); b_str(l, "% RDOT ");
        b_num(l, pxn_rdot_ticks(1) * 100u / (A_BUDGET * 2u));
        b_str(l, pxn_rdl_state() == 2 ? "% >L" : pxn_rdq_in_use() ? "% >Q" : pxn_rdot_chosen() ? "% >R" : "% >G");
        lcd_text(4, y, l, UI_TXT, UI_BG, 1); y += 12;
        fm1_wdt_feed();
        static const char *M[PXN_MODES] = {"C", " V", " B0 ", " B4 ", " B8 ", "2C ", " V12 ", " V16 ", " V12C4 ",
                                           " 2C20 ", " 2C24 "};
        for (int m = 0; m < PXN_MODES; m++) {
            if (m == 0 || m == 5 || m == 9) l[0] = 0;
            uint32_t t = px_nam_tuned(m);
            b_str(l, M[m]);
            if (t) b_num(l, t * 100u / (A_BUDGET * 2u)); else b_str(l, "-");
            if (m == 4 || m == 8) { lcd_text(4, y, l, GRN, UI_BG, 1); y += 12; }
        }
        b_str(l, " >"); b_num(l, (uint32_t)pxn_mode());
        lcd_text(4, y, l, GRN, UI_BG, 1); y += 12;
        /* diagnostic 18: where the CPU alone (mode 0) spends it: input, tap tables, kernel (groups of 4 frames), the
         * gathered 1..3-frame rests, post, the head (groups, rest, rings + output), the whole */
#define B_P(k) (pxn_prof_ticks(k) * 100u / (A_BUDGET * 2u))
        const uint32_t g1 = pxn_prof_ticks(1), g8 = pxn_prof_ticks(8);
        l[0] = 0; b_str(l, "IN "); b_num(l, B_P(0)); b_str(l, " TBL "); b_num(l, B_P(8));
        b_str(l, " KER "); b_num(l, (g1 > g8 ? g1 - g8 : 0u) * 100u / (A_BUDGET * 2u));
        b_str(l, " REST "); b_num(l, B_P(2));
        lcd_text(4, y, l, YEL, UI_BG, 1); y += 12;
        l[0] = 0; b_str(l, "POST "); b_num(l, B_P(3)); b_str(l, " HEAD "); b_num(l, B_P(6)); b_str(l, "+");
        b_num(l, B_P(7)); b_str(l, "+"); b_num(l, B_P(4)); b_str(l, " ALL "); b_num(l, B_P(5));
        lcd_text(4, y, l, YEL, UI_BG, 1); y += 12;
        {   /* the new post (post3) against post2, mode 0; >3 when it is the one used */
            const int p3 = pxn_post3_state();
            l[0] = 0; b_str(l, "POST2 "); b_num(l, pxn_post_ticks(0) * 100u / (A_BUDGET * 2u));
            b_str(l, " POST3 ");
            if (p3) b_num(l, pxn_post_ticks(1) * 100u / (A_BUDGET * 2u)); else b_str(l, "FAILED");
            b_str(l, p3 == 2 ? " >3" : " >2");
            lcd_text(4, y, l, p3 ? YEL : RED, UI_BG, 1); y += 14;
        }
#undef B_P
    }
    fm1_wdt_feed();
    /* diagnostic 21: the network alone, then every factory preset as the audio interrupt runs it (with a NAM amp:
     * pipelined on the two cores; otherwise the whole chain on one core), % of real time: green up to 80, yellow up
     * to 85, red above (the safety bypass: 93 pipelined, 85 otherwise) */
    px_preset_t pp;
    px_preset_factory(&pp, 17);
    for (int z = 0; z < PX_NBLK; z++) pp.on[z] = 0;
    pp.on[PX_B_AMP] = 1;
    {
        uint32_t pct = b_engine(&pp);
        l[0] = 0; b_str(l, "NAM AMP ONLY "); b_num(l, pct); b_str(l, " %  PRESETS:");
        lcd_text(4, y, l, pct <= 85u ? GRN : RED, UI_BG, 1); y += 12;
    }
    for (int f = 0; f < 24; f++) {
        px_preset_factory(&pp, f);
        uint32_t pct = b_engine(&pp);
#if DSP_CPU1
        if (px_nam_dual(&g_eng)) {
            a_nbv = 0;
            uint32_t dt = 0;
            a_late = 0;
            for (int r = 0; r < 16; r++) {
                uint32_t t0 = fm1_ticks();
                dsp_pipe_run(1);
                if (r >= 8) dt += fm1_ticks() - t0;
            }
            a_nbv = 0;
            fm1_wdt_feed();
            pct = dt / 8u * 100u / A_BUDGET;
        }
#endif
        l[0] = 0;
        if (f < 9) b_str(l, "0");
        b_num(l, (uint32_t)f + 1u); b_str(l, " "); b_num(l, pct);
        lcd_text(4 + (uint32_t)(f % 5) * 48u, y, l, pct <= 80u ? GRN : pct <= 85u ? YEL : RED, UI_BG, 1);
        if (f % 5 == 4 || f == 23) y += 12;
    }
    lcd_text(4, y + 4, "100% = REAL TIME. PLEASE TAKE A PHOTO", UI_ACC, UI_BG, 1);
    for (int i = 0; i < 120; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
#endif
}

static struct iis_platform_data a_pd;
static void audio_start(void)
{
#if BENCH_AT_BOOT
    px_engine_init(&g_eng, 44100.0f);
    audio_bench();                                     /* benchmark screen (diagnostic): off by default */
    px_nam_check(b_ticks);                             /* (the bench used the NAM memory) */
#endif
    op_stage(ST_ENG_INIT, "ENGINE INIT...");
    px_engine_init(&g_eng, 44100.0f);
    px_clock = b_ticks;                                /* v38: each block's time (the tuner page shows it) */
    op_stage(ST_ENG_SET, "ENGINE SET...");
#if DSP_CPU1
    a_c1 = fm1_cpu1_on;                                /* started by app_main at the very start (fm1_cpu1_start) */
    for (int i = 0; i < PXN_NL; i++) a_lpsh[i] = (uint16_t)px_nam_lp_share(i);
    px_wet_mode(&g_eng, a_c1 && OP_WET_LANES, A_WLAG);   /* v39: the wet lanes, with the 2nd core */
#endif
    audio_volume(master_vol);
    audio_apply();
    op_stage(ST_I2S, "AUDIO START...");
    a_pd.channel_in = 1u << 1;                         /* DAT1 = PA8: guitar */
    a_pd.channel_out = 1u << 0;                        /* DAT0 = PA7: output */
    a_pd.port_sel = IIS_PORTA;
    a_pd.data_width = (1u << 1) | (1u << 0);          /* 24-bit on both channels, like the official fw */
    a_pd.mclk_output = 1; a_pd.slave_mode = 0;
    a_pd.dump_points_num = 0; a_pd.sr_points = 2u * A_FR;   /* LEN in words: 2 per frame in 24-bit */
    iis_set_enc_data_handler(0, a_enc, 0);
    iis_set_dec_data_handler(0, a_dec, 0);
    iis_open(&a_pd, 0);
    iis_set_sample_rate(44100, 0);
    iis_channel_on((uint8_t)(a_pd.channel_in | a_pd.channel_out), 0);
    fm1_irq_attach(FM1_IRQ_ALNK0, isr_alnk0, 3);
    a_running = 1;
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    gpio_direction_output(G_AMP_EN, 1);                /* amp ON (after I2S: no "pop") */
}

static void audio_prof(uint32_t now)                    /* (main loop) once a second: px_engine.pf -> a_pf */
{
    static uint32_t t_last, b_last;
    if (now - t_last < 1000u) return;
    const uint32_t nb = a_blocks - b_last;
    t_last = now; b_last = a_blocks;
    for (int i = 0; i <= PX_NBLK; i++) {
        uint32_t t = g_eng.pf[i];
        g_eng.pf[i] = 0;
        if (i == PX_NBLK) { t += g_eng.nam_pfb; g_eng.nam_pfb = 0; }   /* v40.2: the layer split's other core */
        if (i == PX_B_DELAY || i == PX_B_REVERB) {     /* v39: with the lanes, their time beside the chain */
            const int L = i == PX_B_REVERB;
            t += g_eng.wl_pf[L]; g_eng.wl_pf[L] = 0;
        }
        const uint32_t den = A_BUDGET * nb / 100u;      /* (32-bit: the pedal has no 64-bit division) */
        const uint32_t pc = den ? t / den : 0u;
        a_pf[i] = (uint8_t)(pc > 255u ? 255u : pc);
    }
    a_wcut = a_wcut_base + g_eng.wl_late;
}
/* v40.3: the status corner shows LOADING (a_ld, from the main loop) while the sound waits for a NAM model, the MODEL
 * knob's 40 ms: from 20 ms after the change until the new model plays, 300 ms at least (no flash); a preset is set at
 * once, its model with it, so it shows nothing. For PING: how long the last one was shown (ms), how many since boot */
static uint8_t a_ld;
static uint32_t a_ldms, a_ldn;
static int audio_nam_load(void)                        /* 1 = the preset needs another NAM model */
{
    return PRESET.type[PX_B_AMP] == PX_AMP_NAM && px_nam_model(PRESET.p[PX_B_AMP][0]) != g_eng.nam_idx;
}
/* call from the main loop, after the controls and before the screen: applies preset changes (40 ms debounce; v40.3: a
 * preset loaded, at once) */
#if OP_BT_AUDIO && DSP_CPU1
/* test build 19: the second core back after a fault or a misread, for the stage. Up to test build 18 one fault left the pedal on one
 * core until power-off: test build 17's boot read an address that is not there (DBG bit 4 axi_rd_inv, AXI_RD_INV_ID 0x140 = CPU1,
 * the SDK's cpu/wl82 debug.c and debug.h) at the start of the chain (px_engine_part+0x206, 2.5 s), test build 14's once at 6.5 s.
 * Now, 300 ms after it was retired, CPU1 starts again (fm1_cpu1_start: its stack, mailbox and fault count afresh) and the engine
 * is set up for two cores as after a NaN (audio_reset: the lanes and the layer split back, a short fade in from silence): the
 * cost is that fade. At most 8 a boot, then one core as before. Not after CAB's one core (nothing retired) or a LATE (a core too
 * slow for its block would come back too slow). The c1 regs line (bt/bt_a2dp.c) keeps the last fault's record and the count */
static uint8_t a_c1_revives;
static uint32_t a_c1_revive_ms;
static void a_c1_revive(void)
{
    const uint32_t t = fm1_uptime_ms();
    if (a_c1 || fm1_cpu1_on || a_c1_revives >= 8u) return;
    if (fm1_cpu1_gave_up != FM1_CPU1_FAULT && fm1_cpu1_gave_up != FM1_CPU1_MISREAD) return;
    if (!fm1_cpu1_gave_ms || t - fm1_cpu1_gave_ms < 300u) return;
    fm1_cpu1_bar.barred = 0;                           /* (the retirement barred it for the boots after: lifted) */
    if (fm1_cpu1_start() < 0) { a_c1_revives = 8u; return; }   /* (it did not come up: one core, as before) */
    a_c1_revives++;
    a_c1_revive_ms = t | 1u;
    a_c1 = 1;
    {   /* (not a NaN: not counted as one) */
        const uint32_t n = a_nan_count;
        audio_reset();
        a_nan_count = n;
    }
}
#endif
static void audio_poll(uint32_t now, int at_once)
{
    audio_prof(now);
    static uint32_t last_h, t_change, t_apply, t_ld, vol_shown = 1000u;
    static int pending, nam;
    if (a_nan) audio_reset();
#if OP_BT_AUDIO && DSP_CPU1
    a_c1_revive();
#endif
    uint32_t h = ui_hash();
    if (h != last_h) { last_h = h; t_change = now; pending = 1; nam = audio_nam_load(); a_sw0 = fm1_ticks(); }
    if (pending && nam && !a_ld && now - t_change >= 20u) { a_ld = 1; t_ld = now; a_ldn++; }
    /* 40 ms after the last change; v38: while a knob turns, every 40 ms too (it glides: no need to wait for the end) */
    if (pending && (at_once || now - t_change >= 40u ||
                    (now - t_apply >= 40u && !a_kpend && px_engine_change(&g_eng, &PRESET) == PX_CH_KNOBS))) {
        pending = 0; t_apply = now; audio_apply();
    }
    if (a_ld && !pending && now - t_ld >= 300u && (!a_swin || now - t_ld >= 1000u)) { a_ld = 0; a_ldms = now - t_ld; }
    if (master_vol != vol_shown) { vol_shown = master_vol; audio_volume(master_vol); }
}
