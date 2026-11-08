/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — PIT LOW OCTAVE on the PC (the notes below RANGE an octave down, the rest dry), with the pedal's libm:
 *   cc -O2 -Iengine tests/engine/lowoct.c engine/px_dsp.c firmware/px_math.c -lm -o build/lowoct && build/lowoct
 * Checks, with sine tones (amplitude 0.3) and the engine's own preset path (every other block off):
 *   - DRY 100, OCT -1 at 0: the output is the dry signal, sample for sample (the block off gives the same)
 *   - low E (82.4 Hz) at RANGE's default: its octave (41.2 Hz) comes out clean, at about the note's level
 *   - high E (329.6 Hz) at that RANGE: no octave (the shifter hears almost nothing)
 *   - a chord, low E + G (196 Hz): the E gets its octave, the G does not (no 98 Hz)
 *   - RANGE at the top: the high E gets its octave (164.8 Hz), and the x8 upsampling leaves no images (-66 dB)
 *   - the octave starts within 60 ms of the note
 *   - RANGE's text is the note at the crossover: D#2 at 0, G4 at 100 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100.0f
#define N 66150                         /* 1.5 s per case at 44.1 kHz */
static px_engine e;
static float in[N], out[N], ref[N];
static int failed;
static void ok(int c, const char *what) { printf("%-68s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

static void preset(px_preset_t *p, int pitch_on, int range, int oct, int dry)
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;        /* 0 dB */
    p->on[PX_B_PITCH] = (uint8_t)pitch_on;
    p->type[PX_B_PITCH] = PX_PIT_LOWOCT;
    p->p[PX_B_PITCH][0] = (uint8_t)range; p->p[PX_B_PITCH][1] = (uint8_t)oct; p->p[PX_B_PITCH][2] = (uint8_t)dry;
}
static void run(const px_preset_t *p, float *y)
{
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    for (int k = 0; k < N; k += 64) px_engine_process(&e, in + k, y + k, N - k < 64 ? N - k : 64);
}
static void tones(float f1, float f2)
{
    for (int k = 0; k < N; k++)
        in[k] = 0.3f * sinf(6.2831853f * f1 * (float)k / SR) + (f2 > 0 ? 0.3f * sinf(6.2831853f * f2 * (float)k / SR) : 0);
}
static float amp_at(const float *y, float f, int from)    /* amplitude of the f component over [from, N) */
{
    double c = 0, s = 0; int n = N - from;
    for (int k = from; k < N; k++) { double w = 6.283185307 * f * k / SR; c += y[k] * cos(w); s += y[k] * sin(w); }
    return (float)(2.0 * sqrt(c * c + s * s) / n);
}
static float rms(const float *y, int from) { double a = 0; for (int k = from; k < N; k++) a += (double)y[k] * y[k]; return (float)sqrt(a / (N - from)); }

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
    float a41 = amp_at(out, 41.2f, from), a82 = amp_at(out, 82.41f, from), r = rms(out, from);
    printf("   low E: 41.2 Hz %.3f, 82.4 Hz %.3f, rms %.3f (purity %.0f%%)\n", a41, a82, r, 100.0f * a41 * a41 * 0.5f / (r * r));
    ok(a41 > 0.2f && a41 < 0.9f, "low E at the default RANGE: its octave, at about the note's level");
    ok(a41 * a41 * 0.5f > 0.8f * r * r, "low E: 80% of the output on the octave (clean)");

    tones(329.63f, 0);
    preset(&p, 1, RDEF, 100, 0); run(&p, out);
    printf("   high E: rms %.4f (input 0.212)\n", rms(out, from));
    ok(rms(out, from) < 0.212f * 0.03f, "high E at that RANGE: no octave (below -30 dB)");

    tones(82.41f, 196.0f);
    preset(&p, 1, RDEF, 100, 0); run(&p, out);
    a41 = amp_at(out, 41.2f, from); float a98 = amp_at(out, 98.0f, from);
    printf("   chord: 41.2 Hz %.3f, 98 Hz %.4f\n", a41, a98);
    ok(a41 > 0.2f && a98 < a41 * 0.06f, "low E + G: the E gets its octave, the G does not (-25 dB)");

    tones(329.63f, 0);
    preset(&p, 1, 100, 100, 0); run(&p, out);
    float a165 = amp_at(out, 164.8f, from);
    printf("   high E, RANGE 100: 164.8 Hz %.3f\n", a165);
    ok(a165 > 0.15f, "RANGE at the top: the high E gets its octave");
    {   /* the x8 interpolation's images, around sr / 8 (5.5 kHz) +- the octave */
        float im = fmaxf(amp_at(out, SR / 8 - 164.8f, from), amp_at(out, SR / 8 + 164.8f, from));
        printf("   images at sr/8 +- 164.8 Hz: %.1f dB under the octave\n", -20.0f * log10f(im / a165));
        ok(im < a165 * 0.0005f, "the upsampling leaves no images (below -66 dB)");
    }

    for (int k = 0; k < N; k++) in[k] = k < (int)(0.2f * SR) ? 0.0f : 0.3f * sinf(6.2831853f * 82.41f * (float)k / SR);
    preset(&p, 1, RDEF, 100, 0); run(&p, out);
    int onset = -1;
    for (int k = (int)(0.2f * SR); k < N - 512 && onset < 0; k += 64) {
        double a = 0; for (int j = 0; j < 512; j++) a += (double)out[k + j] * out[k + j];
        if (sqrt(a / 512) > 0.5 * 0.212) onset = k;
    }
    float ms = onset < 0 ? 1e9f : 1000.0f * (float)(onset - (int)(0.2f * SR)) / SR;
    printf("   octave onset: %.0f ms after the note\n", ms);
    ok(ms < 60.0f, "the octave starts within 60 ms of the note");

    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, 0, t);
    int t0 = !strcmp(t, "D#2");
    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, RDEF, t);
    printf("   RANGE texts: 0 -> D#2? %s, %d -> %s\n", t0 ? "yes" : "no", RDEF, t);
    px_param_text(PX_B_PITCH, PX_PIT_LOWOCT, 0, 100, t);
    ok(t0 && !strcmp(t, "G4"), "RANGE shows the note at the crossover (D#2 .. G4)");

    printf("\n%s\n", failed ? "LOW OCTAVE tests FAILED" : "all LOW OCTAVE tests passed");
    return failed ? 1 : 0;
}
