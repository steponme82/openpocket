/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — PIT LOW OCTAVE on the PC (the lowest note below RANGE, or of the chord with LOWEST, an octave down; the
 * rest dry), with the pedal's libm:
 *   cc -O2 -Iengine tests/engine/lowoct.c engine/px_dsp.c firmware/px_math.c -lm -o build/lowoct && build/lowoct
 * Checks, with sine tones and plucked notes (the engine's own preset path, every other block off):
 *   - DRY 100, OCT -1 at 0: the output is the dry signal, sample for sample (the block off gives the same)
 *   - low E (82.4 Hz) at RANGE D#3: its octave (41.2 Hz) comes out clean, at about the note's level
 *   - high E (329.6 Hz) at that RANGE: no octave; low E + G (196 Hz): the E's octave only
 *   - two notes below RANGE (E5, A5, E + A): the lower note's octave, clean
 *   - RANGE at the top: the high E gets its octave, and the x8 upsampling leaves no images (-66 dB)
 *   - LOWEST: a G chord high on the neck (G3 B3 D4) gets the G's octave; an E5 the E's
 *   - plucked notes: the octave's attack comes less than 20 ms after the note's
 *   - COMP: a note 20 dB softer comes out less than 8 dB softer at COMP 100; hum at -80 dB is not raised above -70 dB
 *   - a 30 Hz rumble mostly stays out of the octave; GROWL 0 changes nothing, GROWL up adds harmonics, not level
 *   - RANGE's text: LOWEST at 0, then the note at the crossover, D#2 at 1, G4 at 100; the other knobs in plain values:
 *     OCT -1 and DRY in dB against the note, GROWL its drive in dB, COMP its ratio, OFF at 0
 *   - v38's knob path: a turn of OCT -1, DRY, GROWL or COMP glides (a smaller step than set at once) and ends where a
 *     full set puts the engine; RANGE is a knob change too */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100.0f
#define N 66150                         /* 1.5 s per case at 44.1 kHz */
static px_engine e;
static float in[N], out[N], ref[N];
static int failed, growl, comp;
static void ok(int c, const char *what) { printf("%-68s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

static void preset(px_preset_t *p, int pitch_on, int range, int oct, int dry)
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;        /* 0 dB */
    p->on[PX_B_PITCH] = (uint8_t)pitch_on;
    p->type[PX_B_PITCH] = PX_PIT_LOWOCT;
    p->p[PX_B_PITCH][0] = (uint8_t)range; p->p[PX_B_PITCH][1] = (uint8_t)oct; p->p[PX_B_PITCH][2] = (uint8_t)dry;
    p->p[PX_B_PITCH][3] = (uint8_t)growl; p->p[PX_B_PITCH][4] = (uint8_t)comp;
}
static void run(const px_preset_t *p, float *y)
{
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    for (int k = 0; k < N; k += 64) px_engine_process(&e, in + k, y + k, N - k < 64 ? N - k : 64);
}
static void tones3(float f1, float f2, float f3, float a)
{
    for (int k = 0; k < N; k++) {
        float t = (float)k / SR;
        in[k] = a * (sinf(6.2831853f * f1 * t) + (f2 > 0 ? sinf(6.2831853f * f2 * t) : 0) + (f3 > 0 ? sinf(6.2831853f * f3 * t) : 0));
    }
}
static void tones(float f1, float f2) { tones3(f1, f2, 0, 0.3f); }
static float amp_at(const float *y, float f, int from)    /* amplitude of the f component over [from, N) */
{
    double c = 0, s = 0; int n = N - from;
    for (int k = from; k < N; k++) { double w = 6.283185307 * f * k / SR; c += y[k] * cos(w); s += y[k] * sin(w); }
    return (float)(2.0 * sqrt(c * c + s * s) / n);
}
static float rms(const float *y, int from) { double a = 0; for (int k = from; k < N; k++) a += (double)y[k] * y[k]; return (float)sqrt(a / (N - from)); }
static float share(const float *y, float f, int from)     /* the share of the output's power on f */
{
    float a = amp_at(y, f, from), r = rms(y, from);
    return r > 0 ? a * a * 0.5f / (r * r) : 0;
}
/* the time a signal's envelope (squared, then two 15 Hz low-passes) first reaches half its peak after t0 */
static float t_half(const float *y, int t0, int len)
{
    static float env[N];
    double k = 1.0 - exp(-6.283185307 * 15.0 / SR), a1 = 0, a2 = 0, mx = 0;
    for (int i = 0; i < len; i++) { a1 += (y[t0 + i] * y[t0 + i] - a1) * k; a2 += (a1 - a2) * k; env[i] = (float)a2; if (a2 > mx) mx = a2; }
    for (int i = 0; i < len; i++) if (env[i] >= 0.5 * mx) return 1000.0f * (float)i / SR;
    return 1e9f;
}

int main(void)
{
    px_preset_t p;
    const int from = N / 3, RDEF = 40;
    char t[16];

    tones(82.41f, 0);
    preset(&p, 1, RDEF, 0, 100); run(&p, out);
    preset(&p, 0, RDEF, 0, 100); run(&p, ref);
    float md = 0; for (int k = 0; k < N; k++) { float d = fabsf(out[k] - ref[k]); if (d > md) md = d; }
    ok(md == 0.0f, "DRY 100, OCT -1 at 0: the dry signal, sample for sample");

    preset(&p, 1, RDEF, 100, 0); run(&p, out);
    float a41 = amp_at(out, 41.2f, from);
    printf("   low E: 41.2 Hz %.3f, %.0f%% of the output on it\n", a41, 100.0f * share(out, 41.2f, from));
    ok(a41 > 0.4f && a41 < 0.8f, "low E at RANGE D#3: its octave, at about the note's level (OCT -1 100)");
    ok(share(out, 41.2f, from) > 0.95f, "low E: 95% of the output on the octave (clean)");

    tones(329.63f, 0);
    preset(&p, 1, RDEF, 100, 0); run(&p, out);
    printf("   high E: rms %.4f (input 0.212)\n", rms(out, from));
    ok(rms(out, from) < 0.212f * 0.03f, "high E at that RANGE: no octave (below -30 dB)");

    tones(82.41f, 196.0f);
    preset(&p, 1, RDEF, 100, 0); run(&p, out);
    printf("   low E + G: 41.2 Hz %.0f%%, 98 Hz %.1f%%\n", 100.0f * share(out, 41.2f, from), 100.0f * share(out, 98.0f, from));
    ok(share(out, 41.2f, from) > 0.95f, "low E + G above RANGE: the E's octave only");

    {   /* two notes below RANGE: the lower one's octave */
        static const float C[3][2] = {{82.41f, 123.47f}, {110.0f, 164.81f}, {82.41f, 110.0f}};
        static const char *const NAME[3] = {"E5", "A5", "E + A"};
        float worst = 1;
        for (int c = 0; c < 3; c++) {
            tones(C[c][0], C[c][1]);
            preset(&p, 1, 60, 100, 0); run(&p, out);
            float sh = share(out, C[c][0] * 0.5f, from);
            printf("   %-6s below RANGE: %.0f%% of the output on the lower note's octave\n", NAME[c], 100.0f * sh);
            if (sh < worst) worst = sh;
        }
        ok(worst > 0.8f, "two notes below RANGE: the lower one's octave, clean (80%)");
    }

    tones(329.63f, 0);
    preset(&p, 1, 100, 100, 0); run(&p, out);
    float a165 = amp_at(out, 164.8f, from);
    printf("   high E, RANGE 100: 164.8 Hz %.3f\n", a165);
    ok(a165 > 0.3f, "RANGE at the top: the high E gets its octave");
    {   /* the x8 interpolation's images, around sr / 8 (5.5 kHz) +- the octave */
        float im = fmaxf(amp_at(out, SR / 8 - 164.8f, from), amp_at(out, SR / 8 + 164.8f, from));
        printf("   images at sr/8 +- 164.8 Hz: %.1f dB under the octave\n", -20.0f * log10f(im / a165));
        ok(im < a165 * 0.0005f, "the upsampling leaves no images (below -66 dB)");
    }

    {   /* LOWEST */
        tones3(196.0f, 246.94f, 293.66f, 0.2f);
        preset(&p, 1, 0, 100, 0); run(&p, out);
        float g = share(out, 98.0f, from), ga = amp_at(out, 98.0f, from);
        tones(82.41f, 123.47f);
        preset(&p, 1, 0, 100, 0); run(&p, out);
        float e5 = share(out, 41.2f, from);
        for (int k = 0; k < N; k++) {                   /* G over a softer D, a fourth below (the hardest case) */
            float tt = (float)k / SR;
            in[k] = 0.12f * sinf(6.2831853f * 146.83f * tt) + 0.2f * sinf(6.2831853f * 196.0f * tt) + 0.2f * sinf(6.2831853f * 246.94f * tt);
        }
        preset(&p, 1, 0, 100, 0); run(&p, out);
        float d = share(out, 73.4f, from);
        printf("   LOWEST: G chord (G3 B3 D4) %.0f%% on 98 Hz (level %.2f), E5 %.0f%% on 41.2 Hz, soft D under G3 B3 %.0f%% on 73.4 Hz\n",
               100.0f * g, ga, 100.0f * e5, 100.0f * d);
        ok(g > 0.7f && ga > 0.3f && e5 > 0.7f, "LOWEST: the octave of the chord's lowest note, anywhere on the neck");
        ok(d > 0.95f, "LOWEST: even a softer root a fourth below (95%)");
    }

    {   /* plucked notes: the octave's attack against the note's */
        static const float F[4] = {82.41f, 110.0f, 98.0f, 123.47f};
        float worst = 0;
        for (int q = 0; q < 4; q++) {
            memset(in, 0, sizeof in);
            const int t0 = (int)(0.2f * SR);
            for (int k = 0; k < (int)(0.9f * SR); k++) {
                float tt = k / SR, env = expf(-tt * 3.0f), s = 0;
                for (int h = 1; h <= 4; h++) s += sinf(6.2831853f * F[q] * h * tt) / (float)(h * h);
                in[t0 + k] = 0.4f * env * s;
            }
            preset(&p, 1, RDEF, 100, 0); run(&p, out);
            float lag = t_half(out, t0, (int)(0.5f * SR)) - t_half(in, t0, (int)(0.5f * SR));
            if (lag > worst) worst = lag;
        }
        printf("   plucked notes: the octave's attack at most %.1f ms after the note's\n", worst);
        ok(worst < 20.0f, "the octave's attack within 20 ms of the note's");
    }

    {   /* COMP */
        float lv[2][2];
        for (int c = 0; c < 2; c++) {
            comp = c ? 100 : 0;
            for (int l = 0; l < 2; l++) {
                tones3(82.41f, 0, 0, l ? 0.3f : 0.03f);
                preset(&p, 1, RDEF, 100, 0); run(&p, out);
                lv[c][l] = rms(out, from);
            }
        }
        comp = 100;                                     /* hum and hiss at -80 dB, nothing played */
        { unsigned sd = 7; for (int k = 0; k < N; k++) { sd = sd * 1103515245u + 12345u;
            in[k] = 1e-4f * (((float)((sd >> 9) & 1023) / 512.0f - 1.0f) + sinf(6.2831853f * 60.0f * (float)k / SR)); } }
        preset(&p, 1, RDEF, 100, 0); run(&p, out);
        float sil = rms(out, from);
        comp = 0;
        float d0 = 20.0f * log10f(lv[0][1] / lv[0][0]), d1 = 20.0f * log10f(lv[1][1] / lv[1][0]);
        printf("   a note 20 dB softer: the octave %.1f dB softer at COMP 0, %.1f dB at COMP 100; -80 dB hum -> %.0f dB\n", d0, d1, 20.0f * log10f(sil + 1e-12f));
        ok(d0 > 17.0f && d1 < 8.0f && sil < 3e-4f, "COMP evens the octave's level; it does not raise the hum");
    }

    {   /* rumble below the low E */
        tones(30.0f, 0);
        preset(&p, 1, RDEF, 100, 0); run(&p, out);
        float rb = rms(out, from);
        tones(82.41f, 0);
        preset(&p, 1, RDEF, 100, 0); run(&p, out);
        float re = rms(out, from);
        printf("   30 Hz rumble: %.1f dB under a low E's octave\n", -20.0f * log10f(rb / re));
        ok(rb < re * 0.4f, "a 30 Hz rumble stays mostly out of the octave (-8 dB)");
    }
    {   /* GROWL */
        tones(82.41f, 0);
        preset(&p, 1, RDEF, 100, 0); run(&p, ref);
        growl = 100;
        preset(&p, 1, RDEF, 100, 0); run(&p, out);
        growl = 0;
        float f1 = amp_at(out, 41.2f, from), f3 = amp_at(out, 123.6f, from), c1 = amp_at(ref, 41.2f, from), c3 = amp_at(ref, 123.6f, from);
        printf("   GROWL 100: octave %.3f (clean %.3f), its 3rd harmonic %.0f%% (clean %.1f%%)\n", f1, c1, 100.0f * f3 / f1, 100.0f * c3 / c1);
        ok(f3 > 0.1f * f1 && c3 < 0.01f * c1 && f1 > 0.5f * c1 && f1 < 2.0f * c1, "GROWL 100: harmonics on the octave, about the same level");
    }

    {   /* v38's knob path: OCT -1, DRY, GROWL and COMP glide (no click), and end where a full set puts them */
        static px_engine e2;
        static float y2[N];
        tones(82.41f, 0);
        const int at = N / 2 / 64 * 64;
        px_preset_t p0, p1;
        preset(&p0, 1, RDEF, 50, 100);
        p1 = p0; p1.p[PX_B_PITCH][1] = 100; p1.p[PX_B_PITCH][2] = 40; p1.p[PX_B_PITCH][3] = 60; p1.p[PX_B_PITCH][4] = 70;
        px_engine_init(&e, SR); px_engine_set(&e, &p0);
        px_engine_init(&e2, SR); px_engine_set(&e2, &p0);
        const int ch = px_engine_change(&e, &p1);
        for (int k = 0; k < N; k += 64) {
            if (k == at) { px_engine_prepare(&e, &p1); px_engine_knobs(&e, &p1); px_engine_set(&e2, &p1); }
            const int m = N - k < 64 ? N - k : 64;          /* (N is not a multiple of 64) */
            px_engine_process(&e, in + k, out + k, m); px_engine_process(&e2, in + k, y2 + k, m);
        }
        float ca = 0, cb = 0, base = 0;                 /* the largest third difference: before, and 30 ms after */
        for (int k = at - 1323; k < at + 1323; k++) {
            float da = fabsf(out[k] - 3 * out[k - 1] + 3 * out[k - 2] - out[k - 3]);
            float db = fabsf(y2[k] - 3 * y2[k - 1] + 3 * y2[k - 2] - y2[k - 3]);
            if (k < at) { if (da > base) base = da; } else { if (da > ca) ca = da; if (db > cb) cb = db; }
        }
        const int same = e.lo_lvl == e2.lo_lvl && e.lo_dry == e2.lo_dry && e.lo_g == e2.lo_g && e.lo_gk == e2.lo_gk &&
                         e.lo_gn == e2.lo_gn && e.lo_c == e2.lo_c && e.gl_left == 0;
        printf("   a knob turn: sorted %s; the step after it %.1fx the one before (%.1fx set at once)\n",
               ch == PX_CH_KNOBS ? "KNOBS" : "FULL", ca / base, cb / base);
        ok(ch == PX_CH_KNOBS && same && ca < 0.5f * cb, "knobs glide (v38): fewer clicks, they end where a full set does");
        p1 = p0; p1.p[PX_B_PITCH][0] = 70;
        ok(px_engine_change(&e2, &p1) == PX_CH_KNOBS, "RANGE is a knob too (no stop, no fade out)");
    }

    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, 0, t);
    int t0 = !strcmp(t, "LOWEST");
    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, 1, t);
    int t1 = !strcmp(t, "D#2");
    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, RDEF, t);
    printf("   RANGE texts: 0 LOWEST? %s, 1 D#2? %s, %d -> %s\n", t0 ? "yes" : "no", t1 ? "yes" : "no", RDEF, t);
    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, 100, t);
    ok(t0 && t1 && !strcmp(t, "G4"), "RANGE shows LOWEST, then the note at the crossover (D#2 .. G4)");
    {
        static const struct { int k, v; const char *want; } T[] = {
            {1, 0, "OFF"}, {1, 25, "-6 DB"}, {1, 50, "0 DB"}, {1, 100, "+6 DB"}, {2, 100, "0 DB"}, {2, 50, "-6 DB"},
            {3, 0, "OFF"}, {3, 100, "+28 DB"}, {4, 0, "OFF"}, {4, 50, "1.8:1"}, {4, 100, "10:1"}};
        int good = 1;
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, T[i].k, T[i].v, t);
            if (strcmp(t, T[i].want)) { printf("   knob %d at %d shows %s, wanted %s\n", T[i].k, T[i].v, t, T[i].want); good = 0; }
        }
        ok(good, "OCT -1, DRY, GROWL and COMP show plain values (dB, ratio, OFF)");
    }

    printf("\n%s\n", failed ? "LOW OCTAVE tests FAILED" : "all LOW OCTAVE tests passed");
    return failed ? 1 : 0;
}
