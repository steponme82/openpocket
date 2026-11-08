/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — SYN PAD on the PC (a chord caught on a footswitch, held as grains under the guitar), with the pedal's
 * libm:
 *   cc -O2 -Iengine tests/engine/pad.c engine/px_dsp.c firmware/px_math.c -lm -o build/pad && build/pad
 * Checks, with plucked chords (the engine's own preset path, every other block off; px_pad_press / px_pad_stop
 * between blocks, as the footswitch does):
 *   - MIX 0: the guitar as it is; no press: no pad
 *   - a chord caught, then silence: the pad holds it, steady (within 3 dB), at about the chord's level (MIX 50), on its
 *     notes
 *   - ATTACK: the pad swells in over about that time
 *   - a second press on another chord: the pad crosses over to it (the old chord's root gone)
 *   - the footswitch held (px_pad_stop): the pad fades over RELEASE
 *   - pressed first, strummed 150 ms later: the pad holds the ringing chord, not the silence or the pick
 *   - no clicks: the pad never jumps more than its own level from one sample to the next
 *   - the knobs in plain values (seconds, Hz, cents, dB) */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100.0f
#define N (14 * 44100)
static px_engine e;
static float in[N], out[N], dry[N];
static int failed;
static void ok(int c, const char *what) { printf("%-70s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

static const float AM[5] = {110.0f, 164.81f, 220.0f, 261.63f, 329.63f};   /* A minor */
static const float CM[5] = {130.81f, 196.0f, 261.63f, 329.63f, 392.0f};   /* C major */
static float decay = 0.8f;                                                 /* per second (a strum: 2.5 at first) */
static void strum(const float *f, float t0, float amp)                     /* five strings, picked and ringing */
{
    for (int s = 0; s < 5; s++) {
        int k0 = (int)((t0 + 0.012f * (float)s) * SR);
        for (int k = 0; k0 + k < N && k < (int)(4.0f * SR); k++) {
            float t = (float)k / SR, v = 0.0f;
            for (int h = 1; h <= 3; h++) v += sinf(6.2831853f * f[s] * (float)h * t) / (float)(h * h);
            in[k0 + k] += amp * expf(-t * decay) * v;
        }
    }
}
static void preset(px_preset_t *p, int on, int att, int rel, int mix)
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    p->on[PX_B_SYN] = (uint8_t)on; p->type[PX_B_SYN] = PX_SYN_PAD;
    p->p[PX_B_SYN][0] = (uint8_t)att; p->p[PX_B_SYN][1] = (uint8_t)rel; p->p[PX_B_SYN][2] = 70;
    p->p[PX_B_SYN][3] = 40; p->p[PX_B_SYN][4] = (uint8_t)mix;
}
/* runs the input through; at the times in press[] the footswitch is pressed, at stop_t it is held */
static void run(const px_preset_t *p, float *y, const float *press, int np, float stop_t)
{
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    int pi = 0, stopped = 0;
    for (int k = 0; k < N; k += 64) {
        float t = (float)k / SR;
        while (pi < np && press[pi] <= t) { px_pad_press(&e); pi++; }
        if (!stopped && stop_t > 0.0f && stop_t <= t) { px_pad_stop(&e); stopped = 1; }
        px_engine_process(&e, in + k, y + k, 64);
    }
}
static float rms(const float *y, float t0, float t1)
{
    double a = 0; int k0 = (int)(t0 * SR), k1 = (int)(t1 * SR);
    for (int k = k0; k < k1; k++) a += (double)y[k] * y[k];
    return (float)sqrt(a / (k1 - k0));
}
static float amp_at(const float *y, float f, float t0, float t1)
{
    double c = 0, s = 0; int k0 = (int)(t0 * SR), k1 = (int)(t1 * SR);
    for (int k = k0; k < k1; k++) { double w = 6.283185307 * f * k / SR; c += y[k] * cos(w); s += y[k] * sin(w); }
    return (float)(2.0 * sqrt(c * c + s * s) / (k1 - k0));
}
static float pad[N];
static void pad_only(void) { for (int k = 0; k < N; k++) pad[k] = out[k] - dry[k]; }
static float steadiness(float t0, float t1)            /* 20 ms RMS: 95th / 5th percentile, in dB */
{
    static float r[2000]; int n = 0, w = (int)(0.02f * SR);
    for (int k = (int)(t0 * SR); k + w <= (int)(t1 * SR) && n < 2000; k += w) {
        double a = 0; for (int j = 0; j < w; j++) a += (double)pad[k + j] * pad[k + j];
        r[n++] = (float)sqrt(a / w);
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && r[j] < r[j - 1]; j--) { float t = r[j]; r[j] = r[j - 1]; r[j - 1] = t; }
    return 20.0f * log10f((r[n * 95 / 100] + 1e-9f) / (r[n * 5 / 100] + 1e-9f));
}

int main(void)
{
    px_preset_t p;
    char t[16];
    const float P1[1] = {0.3f};

    /* MIX 0 and no press */
    memset(in, 0, sizeof in); strum(AM, 0.1f, 0.12f);
    preset(&p, 0, 30, 30, 50); run(&p, dry, 0, 0, 0);
    preset(&p, 1, 30, 30, 0); run(&p, out, P1, 1, 0);
    float md = 0; for (int k = 0; k < N; k++) { float d = fabsf(out[k] - dry[k]); if (d > md) md = d; }
    preset(&p, 1, 30, 30, 50); run(&p, out, 0, 0, 0);
    float mn = 0; for (int k = 0; k < N; k++) { float d = fabsf(out[k] - dry[k]); if (d > md) mn = d > mn ? d : mn; }
    ok(md < 1e-9f && mn < 1e-9f, "MIX 0: the guitar as it is; no press: no pad");

    /* a chord caught, then silence after 4 s */
    memset(in, 0, sizeof in); strum(AM, 0.1f, 0.12f);
    for (int k = (int)(4.5f * SR); k < N; k++) in[k] = 0.0f;
    preset(&p, 0, 30, 30, 50); run(&p, dry, 0, 0, 0);
    preset(&p, 1, 30, 30, 50); run(&p, out, P1, 1, 0);
    pad_only();
    float ch = rms(in, 0.4f, 0.8f), l1 = rms(pad, 2.0f, 3.0f), l2 = rms(pad, 8.0f, 12.0f), st = steadiness(5.0f, 12.0f);
    printf("   chord %.3f rms; pad %.3f (2-3 s), %.3f (8-12 s, 4 s of silence before); level within %.1f dB\n", ch, l1, l2, st);
    ok(l2 > 0.5f * l1 && l2 < 2.0f * l1 && st < 3.0f, "a caught chord is held, steady, after the guitar stops");
    ok(l1 > 0.5f * ch && l1 < 2.0f * ch, "MIX 50: about the chord's own level");
    {
        float on = 0, off = 0;
        for (int i = 0; i < 4; i++) on += amp_at(pad, AM[i], 8.0f, 12.0f);
        off = amp_at(pad, 146.83f, 8.0f, 12.0f) + amp_at(pad, 196.0f, 8.0f, 12.0f) + amp_at(pad, 123.47f, 8.0f, 12.0f);
        printf("   on the chord's notes %.3f, on D G B (not in it) %.3f\n", on, off);
        ok(on > 6.0f * off, "the pad is the chord: its notes, not others");
    }

    /* ATTACK: 50 = 1.6 s */
    preset(&p, 1, 50, 30, 50); run(&p, out, P1, 1, 0);
    pad_only();
    {
        float full = rms(pad, 5.0f, 6.0f), t50 = -1, t90 = -1;
        for (float tt = 0.3f; tt < 5.0f; tt += 0.02f) {
            float r = rms(pad, tt, tt + 0.05f);
            if (t50 < 0 && r > 0.5f * full) t50 = tt;
            if (t90 < 0 && r > 0.9f * full) { t90 = tt; break; }
        }
        px_param_text(PX_B_SYN, PX_SYN_PAD, 0, 50, t);
        printf("   ATTACK %s: half the level %.2f s after the press, 90%% at %.2f s\n", t, t50 - 0.3f, t90 - 0.3f);
        ok(t50 - 0.3f > 0.6f && t50 - 0.3f < 1.5f && t90 - 0.3f > 1.0f && t90 - 0.3f < 2.6f,
           "ATTACK: the pad swells in over about that time (after the 372 ms catch)");
    }

    /* a second chord */
    memset(in, 0, sizeof in); strum(AM, 0.1f, 0.12f); strum(CM, 5.0f, 0.12f);
    {
        const float P2[2] = {0.3f, 5.2f};
        preset(&p, 0, 30, 30, 50); run(&p, dry, 0, 0, 0);
        preset(&p, 1, 30, 30, 50); run(&p, out, P2, 2, 0);
        pad_only();
        float a_root = amp_at(pad, 110.0f, 3.0f, 4.5f), a_after = amp_at(pad, 110.0f, 9.0f, 12.0f), c_after = amp_at(pad, 130.81f, 9.0f, 12.0f);
        printf("   A (110 Hz) before %.4f, after the C chord %.4f; C (130.8 Hz) after %.4f\n", a_root, a_after, c_after);
        ok(a_after < 0.1f * a_root && c_after > 10.0f * a_after, "a second press crosses over to the new chord");
    }

    /* stop: RELEASE 50 = 2.65 s */
    memset(in, 0, sizeof in); strum(AM, 0.1f, 0.12f);
    for (int k = (int)(4.5f * SR); k < N; k++) in[k] = 0.0f;
    preset(&p, 0, 30, 50, 50); run(&p, dry, 0, 0, 0);
    preset(&p, 1, 30, 50, 50); run(&p, out, P1, 1, 6.0f);
    pad_only();
    {
        float before = rms(pad, 5.0f, 6.0f), at2 = rms(pad, 7.6f, 8.0f), at6 = rms(pad, 11.5f, 12.0f);
        px_param_text(PX_B_SYN, PX_SYN_PAD, 1, 50, t);
        printf("   RELEASE %s: %.1f dB after 1.8 s, %.1f dB after 5.7 s\n", t, 20 * log10f(at2 / before), 20 * log10f(at6 / before + 1e-9f));
        ok(at2 < 0.7f * before && at2 > 0.05f * before && at6 < 0.03f * before, "the footswitch held: the pad fades over RELEASE");
    }

    /* pressed first, strummed 150 ms later */
    memset(in, 0, sizeof in); strum(AM, 0.45f, 0.12f);
    {
        const float P0[1] = {0.3f};
        preset(&p, 0, 10, 30, 50); run(&p, dry, 0, 0, 0);
        preset(&p, 1, 10, 30, 50); run(&p, out, P0, 1, 0);
        pad_only();
        float l = rms(pad, 3.0f, 6.0f), s2 = steadiness(3.0f, 6.0f), c = rms(in, 0.7f, 1.0f);
        printf("   pressed 150 ms before the strum: pad %.3f (chord %.3f), level within %.1f dB\n", l, c, s2);
        ok(l > 0.4f * c && s2 < 3.0f, "pressed before the strum: the pad holds the ringing chord");
    }

    {   /* clicks: the largest step of the pad against its level, over everything above */
        float mx = 0, r = rms(pad, 2.0f, 6.0f);
        for (int k = 1; k < N; k++) { float d = fabsf(pad[k] - pad[k - 1]); if (d > mx) mx = d; }
        printf("   largest step %.3f against a level of %.3f\n", mx, r);
        ok(mx < r, "no clicks");
    }
    {   /* a strum that dies fast (12 dB in a second): the caught slice is levelled, the pad stays steady */
        decay = 2.5f;
        memset(in, 0, sizeof in); strum(AM, 0.1f, 0.2f);
        for (int k = (int)(4.5f * SR); k < N; k++) in[k] = 0.0f;
        preset(&p, 0, 30, 30, 50); run(&p, dry, 0, 0, 0);
        preset(&p, 1, 30, 30, 50); run(&p, out, P1, 1, 0);
        pad_only();
        decay = 0.8f;
        float s3 = steadiness(5.0f, 12.0f);
        printf("   a fast-dying strum: the pad within %.1f dB\n", s3);
        ok(s3 < 3.0f, "a fast-dying strum: the pad still steady (its slice levelled)");
    }
    {   /* four chords in quick succession (ATTACK 2.2 s): the three slices, each its own; the fourth waits for a
         * slice still fading out to go quiet before recording into it; no clicks */
        static const float GM[5] = {98.0f, 146.83f, 196.0f, 246.94f, 392.0f};
        const float P3[4] = {0.3f, 1.2f, 1.9f, 2.6f};
        memset(in, 0, sizeof in);                          /* each chord muted when the next is strummed */
        strum(AM, 0.1f, 0.12f); for (int k = (int)(1.0f * SR); k < N; k++) in[k] = 0.0f;
        strum(CM, 1.0f, 0.12f); for (int k = (int)(1.7f * SR); k < N; k++) in[k] = 0.0f;
        strum(GM, 1.7f, 0.12f); for (int k = (int)(2.4f * SR); k < N; k++) in[k] = 0.0f;
        strum(CM, 2.4f, 0.12f);
        preset(&p, 0, 60, 30, 50); run(&p, dry, 0, 0, 0);
        preset(&p, 1, 60, 30, 50); run(&p, out, P3, 4, 0);
        pad_only();
        double e0 = 0, e1 = 0, e2 = 0, d01 = 0;
        for (int i = 0; i < 2048; i++) {
            e0 += (double)e.syn_pad[i] * e.syn_pad[i]; e1 += (double)e.syn_pad[2048 + i] * e.syn_pad[2048 + i];
            e2 += (double)e.syn_pad2[i] * e.syn_pad2[i]; d01 += fabs((double)e.syn_pad[i] - e.syn_pad2[i]);
        }
        float mx = 0, r = rms(pad, 1.0f, 8.0f);
        for (int k = (int)(1.0f * SR); k < (int)(8.0f * SR); k++) { float d = fabsf(pad[k] - pad[k - 1]); if (d > mx) mx = d; }
        float g_after = amp_at(pad, 130.81f, 9.0f, 12.0f), a_after = amp_at(pad, 110.0f, 9.0f, 12.0f);   /* C last */
        printf("   four chords: slices %.0f %.0f %.0f (rms), largest step %.3f against %.3f; C %.4f, A %.4f after\n",
               sqrt(e0 / 2048), sqrt(e1 / 2048), sqrt(e2 / 2048), mx, r, g_after, a_after);
        ok(e0 > 1e6 && e1 > 1e6 && e2 > 1e6 && d01 > 1e5, "chords in quick succession: the three slices used");
        ok(mx < 0.3f * r && g_after > 10.0f * a_after, "chords in quick succession: no clicks (30%), the last one held");
    }
    {   /* a press made while the block was another synth: forgotten when PAD is chosen */
        memset(in, 0, sizeof in); strum(AM, 0.1f, 0.12f);
        px_preset_t s2;
        preset(&s2, 1, 30, 30, 50); s2.type[PX_B_SYN] = PX_SYN_STRINGS;
        preset(&p, 1, 30, 30, 50);
        px_engine_init(&e, SR);
        px_engine_set(&e, &s2);
        px_pad_press(&e);
        px_engine_set(&e, &p);
        for (int k = 0; k < N; k += 64) px_engine_process(&e, in + k, out + k, 64);
        preset(&p, 0, 30, 30, 50); run(&p, dry, 0, 0, 0);
        pad_only();
        ok(rms(pad, 2.0f, 6.0f) < 1e-6f, "a press made before choosing PAD is forgotten");
    }

    {
        static const struct { int k, v; const char *want; } T[] = {
            {0, 0, "0.1 S"}, {0, 100, "6.0 S"}, {1, 100, "10 S"}, {2, 0, "300 HZ"}, {2, 100, "2400 HZ"},
            {3, 0, "OFF"}, {3, 100, "15 CT"}, {4, 50, "0 DB"}, {4, 0, "OFF"}};
        int good = 1;
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            px_param_text(PX_B_SYN, PX_SYN_PAD, T[i].k, T[i].v, t);
            if (strcmp(t, T[i].want)) { printf("   knob %d at %d shows %s, wanted %s\n", T[i].k, T[i].v, t, T[i].want); good = 0; }
        }
        ok(good, "the knobs in plain values: seconds, Hz, cents, dB");
    }
    printf("\n%s\n", failed ? "PAD tests FAILED" : "all PAD tests passed");
    return failed ? 1 : 0;
}
