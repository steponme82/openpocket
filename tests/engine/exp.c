/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the expression pedal on the PC (test build 25: MIDI CC 117, engine/px_dsp.c px_exp), with the pedal's libm:
 *   cc -O2 -DPX_OWN_FX=1 -Iengine tests/engine/exp.c engine/px_dsp.c firmware/px_math.c -lm -o build/exp && build/exp
 * (the Bluetooth audio image's own engine, PX_OWN_FX: tests/run_tests.sh builds it as that image builds the engine and with
 * the host's defaults, as tests/engine/pad.c)
 * Checks, through the engine's own preset path (every other block off; px_exp_set between blocks, as the MIDI glue does):
 *   - no position yet: the engine plays as before; the pedal at the toe as the volume: the same signal
 *   - the volume: half way about -18 dB, the heel silent, no step wider than the signal's own (smoothed)
 *   - the volume comes before the delay: closed after a phrase, the repeats ring on; closed before it, nothing
 *   - the hold: a wah turned off with the pedal at the heel leaves the volume at unity until the pedal moves
 *   - the wah: heel dark, toe bright (CENTER's sweep, the preset's CENTER ignored)
 *   - the whammy: heel the note, toe the preset's SHIFT (an octave up), half way about half the interval
 * Test build 45:
 *   - WHAMMY's SHIFT is four intervals (-2 OCT, -1 OCT, +1 OCT, +2 OCT), a saved one keeping its meaning (75, +12: +1 OCT)
 *   - at the heel (a pedal's 0 to 3) the note itself, with MIX 50 too: the same gain at 110 Hz, 330 Hz and 1 kHz (the
 *     shifter's copy, up to 32 ms late, made a comb); at the toe (124 to 127) the interval exactly: -1 OCT, half the note
 *   - without a pedal, PEDAL: 100 the interval (+2 OCT: four times the note), 0 the note
 *   - SFX's WAH: the heel's peak about 370 Hz, the toe's about 2.35 kHz, 2.65 octaves apart, the ends 4 values wide
 * Test build 48:
 *   - PRESET LVL (the GT page's, a pedal learned on it a volume pedal): 0 silence, 1 -47 dB, 2 dB a step to -9 dB at 20, then
 *     0.3 dB a step as before (50 = 0 dB, 64 = +4.2 dB); its texts; turned from 50 to 0 it fades, no step 1.5 times wider than the
 *     signal's own */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"
#if !PX_OWN_FX
#error "the expression pedal is the Bluetooth audio image's: build with -DPX_OWN_FX=1"
#endif

#define SR 44100.0f
#define N (4 * 44100)
#define B 64
static px_engine e;
static float in[N], out[N], ref[N];
static int failed;
static void ok(int c, const char *what) { printf("%-100s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

static void preset(px_preset_t *p)
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
}
static void sine(float f, float a, float t0, float t1)
{
    memset(in, 0, sizeof in);
    for (int k = (int)(t0 * SR); k < (int)(t1 * SR) && k < N; k++) in[k] = a * sinf(6.2831853f * f * (float)k / SR);
}
/* runs the input through p; at time at[i] the pedal goes to pos[i] (-1: forgotten), at time sw (if > 0) the preset becomes p2 */
static void run(const px_preset_t *p, float *y, const float *at, const int *pos, int np, float sw, const px_preset_t *p2)
{
    px_exp_set(-1);
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    int pi = 0, swapped = 0;
    for (int k = 0; k < N; k += B) {
        const float t = (float)k / SR;
        while (pi < np && at[pi] <= t) { px_exp_set(pos[pi]); pi++; }
        if (!swapped && sw > 0.0f && sw <= t) { px_engine_set(&e, p2); swapped = 1; }
        px_engine_process(&e, in + k, y + k, N - k < B ? N - k : B);   /* (N is not a multiple of B) */
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
static float db(float r) { return 20.0f * log10f(r > 1e-12f ? r : 1e-12f); }

int main(void)
{
    px_preset_t p, q;

    /* the volume */
    preset(&p);
    sine(440.0f, 0.3f, 0.0f, 4.0f);
    run(&p, ref, 0, 0, 0, 0.0f, 0);
    {
        float at[1] = {0.5f}; int pos[1] = {127};
        run(&p, out, at, pos, 1, 0.0f, 0);
        float d = 0.0f;
        for (int k = 0; k < N; k++) { float a = fabsf(out[k] - ref[k]); if (a > d) d = a; }
        ok(d == 0.0f, "no position yet, then the pedal at the toe as the volume: the signal as it was, sample for sample");
    }
    {
        float at[3] = {0.5f, 1.5f, 2.5f}; int pos[3] = {64, 0, 127};
        run(&p, out, at, pos, 3, 0.0f, 0);
        float half = db(rms(out, 1.0f, 1.4f) / rms(ref, 1.0f, 1.4f)), heel = rms(out, 2.0f, 2.4f), back = rms(out, 3.0f, 3.4f) / rms(ref, 3.0f, 3.4f);
        float jump = 0.0f;
        for (int k = 1; k < N; k++) { float a = fabsf(out[k] - out[k - 1]); if (a > jump) jump = a; }
        printf("  half way %.1f dB, heel %.2g, toe again x%.4f, widest step %.3f (the sine's own: %.3f)\n", half, heel, back, jump,
               0.3f * 6.2831853f * 440.0f / SR);
        ok(half > -19.5f && half < -16.5f && heel < 1e-5f && fabsf(back - 1.0f) < 1e-3f && jump < 0.3f * 6.2831853f * 440.0f / SR * 1.05f,
           "the volume: half way about -18 dB, the heel silent, the toe unity again, no step wider than the sine's own");
    }

    /* before the delay: the repeats ring on */
    preset(&p);
    p.on[PX_B_DELAY] = 1; p.type[PX_B_DELAY] = PX_DLY_DIGITAL;
    p.p[PX_B_DELAY][0] = 20; p.p[PX_B_DELAY][1] = 60; p.p[PX_B_DELAY][2] = 60;   /* 265 ms, feedback, mix */
    sine(330.0f, 0.3f, 0.5f, 0.6f);
    run(&p, ref, 0, 0, 0, 0.0f, 0);
    {
        float at[2] = {0.2f, 0.65f}; int pos[2] = {127, 0};
        run(&p, out, at, pos, 2, 0.0f, 0);
        float after = rms(out, 0.7f, 2.0f) / rms(ref, 0.7f, 2.0f);
        float at2[1] = {0.2f}; int pos2[1] = {0};
        run(&p, out, at2, pos2, 1, 0.0f, 0);
        float before = rms(out, 0.5f, 2.0f) / rms(ref, 0.5f, 2.0f);
        printf("  closed after the phrase: the repeats x%.3f; closed before it: x%.2g\n", after, before);
        ok(after > 0.95f && after < 1.05f && before < 1e-4f, "the volume comes before the delay: closed after a phrase the repeats ring on, before it nothing");
    }

    /* the hold: a wah turned off with the pedal at the heel */
    preset(&p);
    p.on[PX_B_SFX] = 1; p.type[PX_B_SFX] = PX_SFX_LFOWAH;
    p.p[PX_B_SFX][0] = 30; p.p[PX_B_SFX][1] = 0; p.p[PX_B_SFX][2] = 60; p.p[PX_B_SFX][3] = 50;
    preset(&q);
    sine(440.0f, 0.3f, 0.0f, 4.0f);
    run(&q, ref, 0, 0, 0, 0.0f, 0);
    {
        float at[2] = {0.3f, 3.0f}; int pos[2] = {0, 0};
        run(&p, out, at, pos, 2, 1.5f, &q);
        float held = rms(out, 2.2f, 2.9f) / rms(ref, 2.2f, 2.9f), moved = rms(out, 3.3f, 3.9f);
        printf("  the wah off with the pedal at the heel: x%.4f; the pedal moved: %.2g\n", held, moved);
        ok(fabsf(held - 1.0f) < 1e-3f && moved < 1e-5f, "a wah turned off at the heel: the volume stays at unity until the pedal moves, then follows it");
    }

    /* the wah: heel dark, toe bright */
    {
        for (int k = 0; k < N; k++) in[k] = 0.1f * (sinf(6.2831853f * 400.0f * (float)k / SR) + sinf(6.2831853f * 2000.0f * (float)k / SR));
        float at[2] = {0.2f, 2.0f}; int pos[2] = {0, 127};
        run(&p, out, at, pos, 2, 0.0f, 0);
        float heel = amp_at(out, 2000.0f, 1.0f, 1.9f) / amp_at(out, 400.0f, 1.0f, 1.9f);
        float toe = amp_at(out, 2000.0f, 3.0f, 3.9f) / amp_at(out, 400.0f, 3.0f, 3.9f);
        printf("  the wah, 2 kHz against 400 Hz: heel %.1f dB, toe %.1f dB\n", db(heel), db(toe));
        ok(db(toe) - db(heel) > 20.0f, "the wah: heel dark, toe bright (20 dB or more between 400 Hz and 2 kHz)");
    }

    /* the whammy: SHIFT +12 (75), MIX 100, GLIDE 0 */
    preset(&p);
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_WHAMMY;
    p.p[PX_B_PITCH][0] = 75; p.p[PX_B_PITCH][1] = 100; p.p[PX_B_PITCH][2] = 0;
    sine(220.0f, 0.3f, 0.0f, 4.0f);
    {
        float at[3] = {0.2f, 1.4f, 2.6f}; int pos[3] = {0, 127, 64};
        run(&p, out, at, pos, 3, 0.0f, 0);
        float h0 = amp_at(out, 220.0f, 0.6f, 1.3f), h1 = amp_at(out, 440.0f, 0.6f, 1.3f);
        float t0 = amp_at(out, 220.0f, 1.8f, 2.5f), t1 = amp_at(out, 440.0f, 1.8f, 2.5f);
        const float fm = 220.0f * powf(2.0f, 12.0f * (64.0f / 127.0f) / 12.0f);
        float m1 = amp_at(out, fm, 3.0f, 3.9f), m0 = amp_at(out, 220.0f, 3.0f, 3.9f);
        printf("  the whammy: heel 220 Hz %.3f / 440 Hz %.3f; toe %.3f / %.3f; half way %.0f Hz %.3f / 220 Hz %.3f\n", h0, h1, t0, t1, fm, m1, m0);
        ok(h0 > 4.0f * h1 && t1 > 4.0f * t0 && m1 > 4.0f * m0, "the whammy: heel the note, toe its SHIFT (an octave up), half way half the interval");
    }
    /* test build 45: WHAMMY's SHIFT, its heel, its toe, PEDAL */
    {
        char t0[16], t1[16], t2[16], t3[16], t4[16];
        px_param_text(PX_B_PITCH, PX_PIT_WHAMMY, 0, 0, t0); px_param_text(PX_B_PITCH, PX_PIT_WHAMMY, 0, 25, t1);
        px_param_text(PX_B_PITCH, PX_PIT_WHAMMY, 0, 50, t2); px_param_text(PX_B_PITCH, PX_PIT_WHAMMY, 0, 75, t3);
        px_param_text(PX_B_PITCH, PX_PIT_WHAMMY, 0, 100, t4);
        printf("  SHIFT 0, 25, 50, 75, 100: %s, %s, %s, %s, %s\n", t0, t1, t2, t3, t4);
        ok(!strcmp(t0, "-2 OCT") && !strcmp(t1, "-1 OCT") && !strcmp(t2, "+1 OCT") && !strcmp(t3, "+1 OCT") && !strcmp(t4, "+2 OCT"),
           "WHAMMY's SHIFT: four intervals, a saved +12 (75) still +1 OCT, +24 (100) +2 OCT");
    }
    preset(&p);
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_WHAMMY;
    p.p[PX_B_PITCH][0] = 75; p.p[PX_B_PITCH][1] = 50; p.p[PX_B_PITCH][2] = 0; p.p[PX_B_PITCH][3] = 0;
    {
        static const float F3[3] = {110.0f, 330.0f, 1000.0f};
        float g[3];
        for (int i = 0; i < 3; i++) {
            sine(F3[i], 0.2f, 0.0f, 4.0f);
            float at[1] = {0.1f}; int pos[1] = {2};
            run(&p, out, at, pos, 1, 0.0f, 0);
            g[i] = db(amp_at(out, F3[i], 1.0f, 3.0f) / 0.2f);
        }
        printf("  the heel (2), MIX 50: 110 Hz %+.2f dB, 330 Hz %+.2f dB, 1 kHz %+.2f dB\n", g[0], g[1], g[2]);
        ok(fabsf(g[0] - g[1]) < 0.05f && fabsf(g[1] - g[2]) < 0.05f, "WHAMMY at the heel, MIX 50: the note itself, the same gain at 110 Hz, 330 Hz and 1 kHz (no comb)");
    }
    p.p[PX_B_PITCH][0] = 25; p.p[PX_B_PITCH][1] = 100;   /* -1 OCT */
    sine(440.0f, 0.2f, 0.0f, 4.0f);
    {
        float at[1] = {0.1f}; int pos[1] = {125};
        run(&p, out, at, pos, 1, 0.0f, 0);
        const float a220 = amp_at(out, 220.0f, 1.0f, 3.0f), a440 = amp_at(out, 440.0f, 1.0f, 3.0f);
        const float a218 = amp_at(out, 220.0f * powf(2.0f, -0.05f / 12.0f), 1.0f, 3.0f);
        printf("  the toe (125), -1 OCT: 220 Hz %.3f, 5 cents under it %.3f, 440 Hz %.4f\n", a220, a218, a440);
        ok(a220 > 20.0f * a440 && a220 > a218, "WHAMMY at the toe (125), -1 OCT: half the note, exactly");
    }
    p.p[PX_B_PITCH][0] = 100; p.p[PX_B_PITCH][3] = 100;  /* +2 OCT, PEDAL at the toe, no pedal */
    sine(110.0f, 0.2f, 0.0f, 4.0f);
    {
        run(&p, out, 0, 0, 0, 0.0f, 0);
        const float up = amp_at(out, 440.0f, 1.0f, 3.0f), note = amp_at(out, 110.0f, 1.0f, 3.0f);
        p.p[PX_B_PITCH][3] = 0;
        run(&p, ref, 0, 0, 0, 0.0f, 0);
        const float up0 = amp_at(ref, 440.0f, 1.0f, 3.0f), note0 = amp_at(ref, 110.0f, 1.0f, 3.0f);
        printf("  no pedal: PEDAL 100, +2 OCT: 440 Hz %.3f, 110 Hz %.4f; PEDAL 0: 440 Hz %.4f, 110 Hz %.3f\n", up, note, up0, note0);
        ok(up > 20.0f * note && note0 > 20.0f * up0, "WHAMMY without a pedal: PEDAL 100 the interval (+2 OCT), PEDAL 0 the note");
    }
    /* SFX's WAH: the sweep */
    preset(&p);
    p.on[PX_B_SFX] = 1; p.type[PX_B_SFX] = PX_SFX_WAH;
    p.p[PX_B_SFX][0] = 50; p.p[PX_B_SFX][1] = 50; p.p[PX_B_SFX][2] = 50; p.p[PX_B_SFX][3] = 50;
    {
        float pk[4];
        const int POS[4] = {0, 3, 124, 127};
        for (int i = 0; i < 4; i++) {
            float best = 0.0f; pk[i] = 0.0f;
            for (float f = 200.0f; f < 4000.0f; f *= 1.03f) {
                sine(f, 0.05f, 0.0f, 0.6f);
                px_exp_set(-1); px_engine_init(&e, SR); px_engine_set(&e, &p); px_exp_set(POS[i]);
                for (int k = 0; k < (int)(0.6f * SR); k += B) px_engine_process(&e, in + k, out + k, B);
                const float a = amp_at(out, f, 0.3f, 0.6f);
                if (a > best) { best = a; pk[i] = f; }
            }
        }
        printf("  WAH's peak: pedal 0 %.0f Hz, 3 %.0f Hz, 124 %.0f Hz, 127 %.0f Hz\n", pk[0], pk[1], pk[2], pk[3]);
        ok(pk[0] > 330.0f && pk[0] < 420.0f && pk[3] > 2100.0f && pk[3] < 2600.0f && pk[0] == pk[1] && pk[2] == pk[3],
           "WAH: the heel's peak about 370 Hz, the toe's about 2.35 kHz, the ends 4 values wide");
    }
    {   /* test build 48: PRESET LVL down to silence */
        static const int V[] = {0, 1, 10, 19, 20, 50, 64};
        static const float WANT[] = {-999.0f, -47.0f, -29.0f, -11.0f, -9.0f, 0.0f, 4.2f};
        float g[7];
        int good = 1;
        char t[7][12];
        sine(440.0f, 0.1f, 0.0f, 4.0f);
        for (int i = 0; i < 7; i++) {
            px_preset_t p; preset(&p);
            p.p[PX_B_GATE][PX_PATCH_LEVEL] = (uint8_t)V[i];
            run(&p, out, 0, 0, 0, 0.0f, 0);
            g[i] = db(rms(out, 1.0f, 2.0f) / (0.1f / sqrtf(2.0f)));
            px_param_text(PX_B_GATE, 0, PX_PATCH_LEVEL, V[i], t[i]);
            if (i == 0 ? g[i] > -120.0f : fabsf(g[i] - WANT[i]) > 0.15f) good = 0;
        }
        char what[200];
        snprintf(what, sizeof what, "PRESET LVL: %s %.0f dB, %s %.1f, %s %.1f, %s %.1f, %s %.1f, %s %.1f, %s %.1f", t[0], g[0], t[1], g[1],
                 t[2], g[2], t[3], g[3], t[4], g[4], t[5], g[5], t[6], g[6]);
        ok(good && !strcmp(t[0], "-INF") && !strcmp(t[1], "-47 DB") && !strcmp(t[4], "-9.0 DB") && !strcmp(t[6], "+4.2 DB"), what);
        {   /* from 50 to 0 while playing (a knob, or a pedal learned on it: px_engine_knobs): a fade, no step wider than the
             * signal's own */
            px_preset_t p, q; preset(&p); preset(&q);
            q.p[PX_B_GATE][PX_PATCH_LEVEL] = 0;
            px_exp_set(-1);
            px_engine_init(&e, SR); px_engine_set(&e, &p);
            for (int k = 0; k < N; k += B) {
                if (k == 44160) px_engine_knobs(&e, &q);   /* (at 1.0 s, a block's start) */
                px_engine_process(&e, in + k, out + k, N - k < B ? N - k : B);
            }
            float worst = 0.0f;
            for (int k = (int)(0.9f * SR); k < (int)(1.5f * SR); k++) { const float d = fabsf(out[k] - out[k - 1]); if (d > worst) worst = d; }
            printf("  fade: the widest step %.5f (the sine's own %.5f), after %.2g, its first 10 ms %.3f\n", worst,
                   0.1f * 6.2831853f * 440.0f / SR, rms(out, 1.1f, 1.5f), rms(out, 1.0f, 1.01f));
            ok(worst < 0.1f * 6.2831853f * 440.0f / SR * 1.5f && rms(out, 1.1f, 1.5f) < 1e-6f && rms(out, 1.0f, 1.01f) > 0.03f,
               "PRESET LVL: 50 to 0 while playing fades out over 23 ms to silence, no click");
    }
    }
    px_exp_set(-1);
    printf("\n%s\n", failed ? "expression pedal tests FAILED" : "all expression pedal tests passed");
    return failed ? 1 : 0;
}
