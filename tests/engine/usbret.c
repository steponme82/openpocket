/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — test build 43: USB RETURN, the computer's sound into the chain (engine/px_dsp.c px_engine_ret, px_rt_mix), the
 * Bluetooth audio image's (-DPX_OWN_FX=1):
 *  1. nowhere, or somewhere with nothing in it and the guitar kept: the chain bit for bit as without (build 42's sound);
 *  2. before DELAY, the guitar cut: the blocks before DELAY never touch the computer's sound (an amp on or off, the same
 *     output), the delay's echoes and the reverb's tail play it; inline and through the wet lanes;
 *  3. MIX: the guitar's chain and the computer's sound add (within 50 dB: the delay's and the reverb's own rounding);
 *  4. before a block that is off: where the next block on is; the preset's order moves it (a reverb before the amp: the
 *     computer's sound then goes through the amp);
 *  5. GUITAR CUT at the output (PX_RT_END, no sound given): the guitar fades out in 256 samples (5.8 ms) and back in, with
 *     no step; LOW OCTAVE's octave before the return is cut with it;
 *  6. a NAM amp on two cores (PRE, NAM and POST a block apart, as firmware/px_audio.h runs them): the same output bit for
 *     bit as the plain engine, one block later, with the return before the amp (PRE's end), after it (POST), at the start
 *     and at the end;
 *  7. OUTPUT STEREO: the return's echoes reach the sides, the two channels summed are the mono sound.
 *   cc -O2 -DPX_OWN_FX=1 -Iengine tests/engine/usbret.c engine/px_dsp.c firmware/px_math.c -lm -o build/usbret && build/usbret */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100.0f
#define FR 128
#define NB 900                                         /* 2.6 s */
#define N (FR * NB)
static px_engine e;
static float in[N], xr[N], o_a[N], o_b[N], o_c[N], o_l[N], o_r[N];
static int failed;
static void ok(int c, const char *what) { printf("%-112s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

static int lanes;                                      /* 1: the wet lanes on, as the firmware with two cores */
static int rt_at = PX_RT_NONE, rt_x = 1, rt_cut, rt_cut_a = 0, rt_cut_b = N;   /* the return; the cut over [a, b) */
static int cut_at(int k) { return rt_cut && k >= rt_cut_a && k < rt_cut_b; }
/* the plain engine in blocks of 128 (two calls of 64; with the lanes: swap, the chain, the lanes' wet); each call told of the
 * return (xr from the same sample on) */
static void run_m(const px_preset_t *p, float *y, float *r, int mode)
{
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    if (lanes) px_wet_mode(&e, 1, 2 * FR);
    for (int k = 0; k < N; k += FR) {
        if (lanes) px_wet_swap(&e);
        for (int o = 0; o < FR; o += 64) {
            px_engine_out(&e, mode, r ? r + k + o : 0);
            px_engine_ret(&e, PX_PART_ALL, rt_x ? xr + k + o : 0, rt_at, cut_at(k + o));
            px_engine_process(&e, in + k + o, y + k + o, 64);
        }
        if (lanes) px_wet_run(&e, 0, 0);
    }
    px_engine_out(&e, PX_OUT_MONO, 0);
    px_engine_ret(&e, PX_PART_ALL, 0, PX_RT_NONE, 0);
}
static void run(const px_preset_t *p, float *y) { run_m(p, y, 0, PX_OUT_MONO); }
/* the two-core pipeline, as firmware/px_audio.h dsp_pipe_run: POST on the previous block's network output, PRE and NAM on
 * this block; the output one block later. Each part told of the return of the block it works on */
static void run_pipe(const px_preset_t *p, float *y)
{
    static float nb[2][FR];
    int cur = 0;
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    memset(y, 0, sizeof(float) * N);
    for (int b = 0; b < NB; b++) {
        if (b) for (int o = 0; o < FR; o += 64) {
            const int k = FR * (b - 1) + o;
            px_engine_out(&e, PX_OUT_MONO, 0);
            px_engine_ret(&e, PX_PART_POST, rt_x ? xr + k : 0, rt_at, cut_at(k));
            px_engine_part(&e, nb[cur ^ 1] + o, y + k, 64, PX_PART_POST);
        }
        for (int o = 0; o < FR; o += 64) {
            const int k = FR * b + o;
            px_engine_ret(&e, PX_PART_PRE, rt_x ? xr + k : 0, rt_at, cut_at(k));
            px_engine_part(&e, in + k, nb[cur] + o, 64, PX_PART_PRE);
        }
        for (int o = 0; o < FR; o += 64) px_engine_part(&e, nb[cur] + o, nb[cur] + o, 64, PX_PART_NAM);
        cur ^= 1;
    }
    px_engine_ret(&e, PX_PART_PRE, 0, PX_RT_NONE, 0);
    px_engine_ret(&e, PX_PART_POST, 0, PX_RT_NONE, 0);
}
static float peak(const float *y, int a, int b) { float m = 0; for (int k = a; k < b; k++) if (fabsf(y[k]) > m) m = fabsf(y[k]); return m; }
static float maxdiff(const float *x, const float *y, int a, int b)
{
    float m = 0; for (int k = a; k < b; k++) if (fabsf(x[k] - y[k]) > m) m = fabsf(x[k] - y[k]); return m;
}
static float maxstep(const float *y, int a, int b)
{
    float m = 0; for (int k = a + 1; k < b; k++) if (fabsf(y[k] - y[k - 1]) > m) m = fabsf(y[k] - y[k - 1]); return m;
}
static float amp_at(const float *y, float f, int a, int b)   /* the f component's amplitude over [a, b) */
{
    double c = 0, s = 0;
    for (int k = a; k < b; k++) { double w = 6.283185307 * f * k / SR; c += y[k] * cos(w); s += y[k] * sin(w); }
    return (float)(2.0 * sqrt(c * c + s * s) / (b - a));
}
static float db(float a) { return 20.0f * log10f(a > 1e-12f ? a : 1e-12f); }

/* the guitar: a low E (82.4 Hz with its 2nd and 3rd harmonics) from 0.1 s for 1 s, at the pedal's level, then silence */
static void guitar(void)
{
    const int a = (int)(0.1f * SR), b = a + (int)(1.0f * SR);
    for (int k = 0; k < N; k++) {
        const float t = (float)(k - a) / SR, g = k >= a && k < b ? 0.15f * expf(-t * 1.2f) : 0.0f;
        in[k] = g * (sinf(6.2831853f * 82.41f * t) + 0.5f * sinf(6.2831853f * 164.82f * t) + 0.3f * sinf(6.2831853f * 247.23f * t));
    }
}
/* the computer's sound: a flute-like 880 Hz with vibrato from 0.2 s to 0.9 s, at 0.1 (the input's scale), then silence */
static void flute(void)
{
    const int a = (int)(0.2f * SR), b = (int)(0.9f * SR);
    for (int k = 0; k < N; k++) {
        const float t = (float)k / SR, env = k >= a && k < b ? 0.1f * (k < a + 441 ? (float)(k - a) / 441.0f : k > b - 441 ? (float)(b - k) / 441.0f : 1.0f) : 0.0f;
        xr[k] = env * sinf(6.2831853f * 880.0f * t + 0.3f * sinf(6.2831853f * 5.0f * t));
    }
}
/* the chain: DRIVE (TS9), AMP (TWIN, or a NAM amp), CAB, a DIGITAL DELAY, a HALL; or with LOW OCTAVE first */
static void preset(px_preset_t *p, int nam)
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    p->on[PX_B_DRIVE] = 1; p->type[PX_B_DRIVE] = PX_DRV_TS9; memset(p->p[PX_B_DRIVE], 50, 5);
    p->on[PX_B_AMP] = 1; p->type[PX_B_AMP] = nam ? PX_AMP_NAM : PX_AMP_TWIN; memset(p->p[PX_B_AMP], 50, 5);
    if (nam) p->p[PX_B_AMP][0] = 0;                    /* (the first factory model) */
    p->on[PX_B_CAB] = 1; p->type[PX_B_CAB] = 0; memset(p->p[PX_B_CAB], 50, 5);
    p->on[PX_B_DELAY] = 1; p->type[PX_B_DELAY] = PX_DLY_DIGITAL; memset(p->p[PX_B_DELAY], 50, 5);
    p->on[PX_B_REVERB] = 1; p->type[PX_B_REVERB] = PX_REV_HALL; memset(p->p[PX_B_REVERB], 50, 5);
}

int main(void)
{
    char what[240];
    px_preset_t p, q;
    guitar(); flute();
    const int fe = (int)(0.9f * SR);                   /* the flute's end */

    /* 1. nothing to add, the guitar kept: as without */
    preset(&p, 0);
    rt_at = PX_RT_NONE; run(&p, o_a);
    {
        int same = 1;
        memcpy(o_c, xr, sizeof xr); memset(xr, 0, sizeof xr);
        for (int at = PX_RT_NONE; at <= PX_RT_END; at++) {
            rt_at = at; rt_x = 1; run(&p, o_b); if (memcmp(o_a, o_b, sizeof o_a)) same = 0;
            rt_x = 0; run(&p, o_b); if (memcmp(o_a, o_b, sizeof o_a)) same = 0;
        }
        memcpy(xr, o_c, sizeof xr); rt_x = 1;
        ok(same, "every place, with silence given or none, the guitar kept: the chain bit for bit as build 42's");
    }

    for (lanes = 0; lanes < 2; lanes++) {
        const char *lw = lanes ? " (the wet lanes)" : "";
        /* 2. before DELAY, the guitar cut: the drive and the amp never touch it */
        memset(in, 0, sizeof in);                      /* (the guitar silent while it fades out: nothing of it reaches the delay) */
        for (int k = (int)(0.1f * SR); k < N; k++) { const float t = (float)k / SR; in[k] = 0.15f * sinf(6.2831853f * 82.41f * t); }
        preset(&p, 0); q = p; q.on[PX_B_DRIVE] = q.on[PX_B_AMP] = q.on[PX_B_CAB] = 0;
        rt_at = PX_B_DELAY; rt_cut = 1; rt_x = 1;
        run(&p, o_a); run(&q, o_b);
        const float d = maxdiff(o_a, o_b, 0, N), pk = peak(o_a, 0, N), echo = peak(o_a, fe + 2205, N);
        snprintf(what, sizeof what, "before DELAY, GUITAR CUT%s: drive, amp and cabinet on or off, the same (%.1f dB); the echoes and tail play on (%.1f dB)", lw, db(d), db(echo));
        ok(d < 1e-6f && pk > 0.02f && echo > 1e-3f, what);
        rt_x = 0; run(&p, o_c); rt_x = 1;
        snprintf(what, sizeof what, "and with no sound from the computer: silence (%.1f dB)", db(peak(o_c, 512, N)));
        ok(peak(o_c, 512, N) < 1e-6f, what);

        /* 3. MIX: the guitar's chain and the computer's sound add (DIGITAL DELAY and HALL are linear at this level) */
        guitar();
        rt_cut = 0;
        rt_at = PX_B_DELAY; run(&p, o_a);              /* both */
        rt_at = PX_RT_NONE; run(&p, o_b);              /* the guitar alone */
        memcpy(o_l, in, sizeof in); memset(in, 0, sizeof in);
        rt_at = PX_B_DELAY; run(&p, o_c);              /* the computer's sound alone */
        memcpy(in, o_l, sizeof in);
        float m = 0; for (int k = 0; k < N; k++) { const float v = fabsf(o_a[k] - o_b[k] - o_c[k]); if (v > m) m = v; }
        snprintf(what, sizeof what, "MIX%s: the guitar's sound and the computer's add (%.1f dB off, the computer's %.1f dB, the guitar's %.1f dB)", lw, db(m), db(peak(o_c, 0, N)), db(peak(o_b, 0, N)));
        ok(m < 0.003f * peak(o_c, 0, N) && peak(o_c, 0, N) > 0.02f && peak(o_b, 0, N) > 0.02f, what);   /* (within 50 dB: the delay's
                                                                                                         * and the reverb's own rounding) */
    }
    lanes = 0;

    /* 4. before a block that is off: where the next one on is; the order moves it */
    preset(&p, 0); p.on[PX_B_DELAY] = 0;
    rt_cut = 0;
    rt_at = PX_B_DELAY; run(&p, o_a);
    rt_at = PX_B_REVERB; run(&p, o_b);
    ok(!memcmp(o_a, o_b, sizeof o_a), "before DELAY with DELAY off: where REVERB, the next block on, is (bit for bit)");
    {   /* REVERB before the amp: the computer's sound, entering before REVERB, goes through the amp */
        static const uint8_t ORD[PX_NBLK] = {PX_B_GATE, PX_B_COMP, PX_B_PITCH, PX_B_SFX, PX_B_SYN, PX_B_REVERB, PX_B_DRIVE, PX_B_AMP,
                                             PX_B_CAB, PX_B_EQ, PX_B_MOD, PX_B_DELAY};
        preset(&p, 0); memcpy(p.order, ORD, sizeof ORD); p.on[PX_B_DRIVE] = 0;
        q = p; q.on[PX_B_AMP] = 0;
        memset(in, 0, sizeof in); rt_at = PX_B_REVERB; rt_cut = 1;
        run(&p, o_a); run(&q, o_b);
        snprintf(what, sizeof what, "REVERB moved before the amp: the computer's sound, before REVERB, now goes through the amp (on and off differ by %.1f dB)", db(maxdiff(o_a, o_b, 0, N)));
        ok(maxdiff(o_a, o_b, 0, N) > 0.01f, what);
        guitar();
    }

    /* 5. GUITAR CUT at the output: out in 256 samples and back, no step; LOW OCTAVE's octave with it */
    {
        preset(&p, 0);
        for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
        for (int k = 0; k < N; k++) in[k] = 0.2f * sinf(6.2831853f * 441.0f * (float)k / SR);
        rt_at = PX_RT_END; rt_x = 0; rt_cut = 1; rt_cut_a = 64 * 200; rt_cut_b = 64 * 600;
        run(&p, o_a);
        rt_cut = 0; run(&p, o_b);
        const int a = rt_cut_a, b = rt_cut_b;
        const float before = peak(o_a, a - 2000, a), half = fabsf(o_a[a + 128]) / (fabsf(o_b[a + 128]) + 1e-9f);
        const float gone = peak(o_a, a + 256, b), back = maxdiff(o_a, o_b, b + 256 + 2000, N);
        const float st = maxstep(o_a, a - 100, a + 400), st2 = maxstep(o_a, b - 100, b + 400), st0 = maxstep(o_b, a - 100, a + 400);
        snprintf(what, sizeof what, "GUITAR CUT at the output: whole before (%.2f), half at 128 samples (%.2f), gone after 256 (%.1f dB), back after (%.1f dB off)",
                 before, half, db(gone), db(back));
        ok(before > 0.19f && fabsf(half - 0.5f) < 0.02f && gone < 1e-4f && back < 1e-4f, what);   /* (gone: the output's DC
                                                                                                    * blocker's last, decaying) */
        snprintf(what, sizeof what, "  no step: the largest change between samples %.4f going, %.4f coming back (the sine's own %.4f)", st, st2, st0);
        ok(st <= st0 * 1.01f && st2 <= st0 * 1.01f, what);
        rt_cut_a = 0; rt_cut_b = N; rt_x = 1;
        /* LOW OCTAVE first, the return before the amp, the guitar cut: no octave at all */
        preset(&p, 0);
        p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_LOWOCT;
        { const uint8_t lo[5] = {40, 50, 50, 0, 0}; memcpy(p.p[PX_B_PITCH], lo, 5); }
        guitar();
        for (int k = 0; k < (int)(0.1f * SR); k++) in[k] = 0.0f;
        rt_at = PX_B_DRIVE; rt_cut = 1; rt_x = 0;
        run(&p, o_a);
        rt_cut = 0; run(&p, o_b);
        const int pa = (int)(0.3f * SR), pb = (int)(0.9f * SR);
        snprintf(what, sizeof what, "LOW OCTAVE before the return, the guitar cut: its octave cut too (41.2 Hz %.1f dB, kept: %.1f dB)",
                 db(amp_at(o_a, 41.2f, pa, pb)), db(amp_at(o_b, 41.2f, pa, pb)));
        ok(peak(o_a, 512, N) < 1e-6f && amp_at(o_b, 41.2f, pa, pb) > 0.01f, what);
        rt_x = 1; rt_cut = 0;
    }

    /* 6. a NAM amp on two cores: bit for bit, a block later, wherever the return is */
    {
        static const int AT[5] = {PX_B_AMP, PX_B_CAB, PX_RT_IN, PX_RT_END, PX_B_DRIVE};
        static const char *const AW[5] = {"before the amp (PRE's end)", "after it (POST)", "at the start", "at the end", "before DRIVE (PRE)"};
        guitar();
        preset(&p, 1);
        for (int c = 0; c < 2; c++) for (int i = 0; i < 5; i++) {
            rt_at = AT[i]; rt_cut = c;
            run(&p, o_a); run_pipe(&p, o_b);
            snprintf(what, sizeof what, "a NAM amp on two cores, the return %s%s: the plain engine's output bit for bit, a block later",
                     AW[i], c ? ", the guitar cut" : "");
            ok(!memcmp(o_a, o_b, sizeof(float) * FR * (NB - 1)) && peak(o_a, 0, N) > 0.01f, what);
        }
        rt_cut = 0;
    }

    /* 7. OUTPUT STEREO: the return's echoes to the sides, the two channels summed the mono sound */
    {
        preset(&p, 0);
        memset(in, 0, sizeof in);
        rt_at = PX_B_DELAY; rt_cut = 1;
        run_m(&p, o_l, o_r, PX_OUT_STEREO);
        run(&p, o_a);
        float m = 0, d2 = 0;
        for (int k = 0; k < N; k++) { const float v = fabsf(0.5f * (o_l[k] + o_r[k]) - o_a[k]); if (v > m) m = v; d2 += (o_l[k] - o_r[k]) * (o_l[k] - o_r[k]); }
        snprintf(what, sizeof what, "OUTPUT STEREO: the computer's echoes reach the sides (side %.1f dB), left and right summed are the mono sound (%.1f dB off)",
                 db(sqrtf(d2 / N)), db(m));
        ok(m < 1e-5f && d2 > 1e-6f, what);
        rt_cut = 0; rt_at = PX_RT_NONE;
        guitar();
    }

    printf(failed ? "usb return test FAILED\n" : "usb return test passed\n");
    return failed ? 1 : 0;
}
