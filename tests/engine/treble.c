/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — test build 40: TREBLE BOOST (engine/px_dsp.c DV, TB_*), the Bluetooth audio image's (-DPX_OWN_FX=1).
 *  1. the type, its knobs BOOST / RANGE / LEVEL and RANGE's texts FULL / MID / TREBLE;
 *  2. small signals: TREBLE lifts 3 kHz well over 200 Hz, MID less, FULL is nearly flat;
 *  3. driven hard, it clips lopsided (a germanium transistor): the 2nd harmonic is there, as strong as a tenth of the 3rd;
 *  4. BOOST: more drive and more level, step by step;
 *  5. its level with BOOST and LEVEL at noon, at each RANGE: the loudness of the guitar without it (ITU-R BS.1770, the
 *     five DI loops of assets/loops at the pedal's level, in front of TWIN at noon + 1x12 US OPEN, as
 *     tools/calib_levels.py measures the other types), within 0.5 dB. `treble measure` prints TB_DB's correction.
 *   cc -O2 -DPX_OWN_FX=1 -Iengine tests/engine/treble.c engine/px_dsp.c firmware/px_math.c -lm -o build/treble && build/treble */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100.0f
static px_engine e;
static int failed;
static void ok(int c, const char *what) { printf("%-100s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

static px_preset_t chain(int drive, int boost, int range, int level, int amp)
{
    px_preset_t p;
    px_preset_factory(&p, 0);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
    if (amp) {
        p.on[PX_B_AMP] = 1; p.type[PX_B_AMP] = PX_AMP_TWIN; memset(p.p[PX_B_AMP], 50, 5);
        p.on[PX_B_CAB] = 1; p.type[PX_B_CAB] = 0; memset(p.p[PX_B_CAB], 50, 5);
    }
    if (drive) {
        p.on[PX_B_DRIVE] = 1; p.type[PX_B_DRIVE] = PX_DRV_TREBLE;
        p.p[PX_B_DRIVE][0] = (uint8_t)boost; p.p[PX_B_DRIVE][1] = (uint8_t)range; p.p[PX_B_DRIVE][2] = (uint8_t)level;
    }
    return p;
}
static void render(const px_preset_t *p, const float *x, float *y, int n)
{
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    for (int i = 0; i + 64 <= n; i += 64) px_engine_process(&e, x + i, y + i, 64);
}

/* a sine through the booster alone: the output's level at the input's frequency, and at its 2nd and 3rd harmonics */
static void sine(int boost, int range, float f, float amp, float *h1, float *h2, float *h3)
{
    static float x[44100], y[44100];
    const int n = 44100, skip = 22050;
    for (int i = 0; i < n; i++) x[i] = amp * sinf(2.0f * 3.14159265f * f * (float)i / SR);
    px_preset_t p = chain(1, boost, range, 50, 0);
    render(&p, x, y, n);
    double c[3] = {0}, s[3] = {0};
    for (int i = skip; i < n; i++)
        for (int h = 0; h < 3; h++) {
            const double w = 2.0 * 3.141592653589793 * f * (h + 1) * i / SR;
            c[h] += y[i] * cos(w); s[h] += y[i] * sin(w);
        }
    const double m = 2.0 / (n - skip);
    *h1 = (float)(m * sqrt(c[0] * c[0] + s[0] * s[0]));
    *h2 = (float)(m * sqrt(c[1] * c[1] + s[1] * s[1]));
    *h3 = (float)(m * sqrt(c[2] * c[2] + s[2] * s[2]));
}
static float db(float a) { return 20.0f * log10f(a > 1e-12f ? a : 1e-12f); }

/* ITU-R BS.1770: K-weighting, 400 ms blocks every 100 ms, gated at -70 LUFS and 10 dB under the mean (tools/calib_levels.py) */
static double lufs(const float *x, int n)
{
    double b0, b1, b2, a1, a2, c1, c2;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = tan(3.141592653589793 * f0 / SR), Vh = pow(10.0, G / 20.0), Vb = pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        b0 = (Vh + Vb * K / Q + K * K) / a0; b1 = 2.0 * (K * K - Vh) / a0; b2 = (Vh - Vb * K / Q + K * K) / a0;
        a1 = 2.0 * (K * K - 1.0) / a0; a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = tan(3.141592653589793 * f0 / SR);
        const double a0 = 1.0 + K / Q + K * K;
        c1 = 2.0 * (K * K - 1.0) / a0; c2 = (1.0 - K / Q + K * K) / a0;
    }
    double *y = malloc(sizeof(double) * (size_t)n), x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0;
    for (int i = 0; i < n; i++) {
        const double v = b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = v;
        const double w = v - 2.0 * u1 + u2 - c1 * z1 - c2 * z2;
        u2 = u1; u1 = v; z2 = z1; z1 = w;
        y[i] = w;
    }
    const int blk = (int)(0.4f * SR), hop = (int)(0.1f * SR);
    int nz = 0;
    double *z = malloc(sizeof(double) * (size_t)(n / hop + 1));
    for (int i = 0; i < n - blk; i += hop) {
        double s = 0; for (int j = 0; j < blk; j++) s += y[i + j] * y[i + j];
        z[nz++] = s / blk;
    }
    double sum = 0; int m = 0;
    for (int i = 0; i < nz; i++) if (-0.691 + 10.0 * log10(z[i] + 1e-20) > -70.0) { sum += z[i]; m++; }
    double r = -99.0;
    if (m) {
        const double rel = -0.691 + 10.0 * log10(sum / m) - 10.0;
        double s2 = 0; int m2 = 0;
        for (int i = 0; i < nz; i++) if (z[i] > 0 && -0.691 + 10.0 * log10(z[i]) > rel) { s2 += z[i]; m2++; }
        if (m2) r = -0.691 + 10.0 * log10(s2 / m2);
    }
    free(y); free(z);
    return r;
}

/* the five DI loops, at the pedal's level (x 0.15, as tools/calib_levels.py and tests/engine/chain.c) */
static float *di[5]; static int din[5], ndi;
static int load(const char *path, int k)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) { fclose(f); return 0; }
    for (;;) {                                            /* the chunks up to "data" */
        unsigned char c[8]; if (fread(c, 1, 8, f) != 8) { fclose(f); return 0; }
        const uint32_t len = (uint32_t)c[4] | (uint32_t)c[5] << 8 | (uint32_t)c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "data", 4)) {
            const int n = (int)(len / 2);
            int16_t *s = malloc(len); if (fread(s, 2, (size_t)n, f) != (size_t)n) { fclose(f); free(s); return 0; }
            di[k] = malloc(sizeof(float) * (size_t)n); din[k] = n / 64 * 64;
            for (int i = 0; i < n; i++) di[k][i] = (float)s[i] / 32768.0f * 0.15f;
            free(s); fclose(f); return 1;
        }
        fseek(f, (long)len, SEEK_CUR);
    }
}
static double loud(const px_preset_t *p)
{
    double s = 0;
    for (int k = 0; k < ndi; k++) {
        float *y = calloc((size_t)din[k], sizeof(float));
        render(p, di[k], y, din[k]);
        s += lufs(y, din[k]);
        free(y);
    }
    return s / ndi;
}

int main(int argc, char **argv)
{
    const int measure = argc > 1 && !strcmp(argv[1], "measure");
    char what[200], txt[16];
    /* 1. the type and its knobs */
    ok(!strcmp(px_type_name(PX_B_DRIVE, PX_DRV_TREBLE), "TREBLE BOOST") && px_nparams(PX_B_DRIVE, PX_DRV_TREBLE) == 3 &&
       !strcmp(px_param_name(PX_B_DRIVE, PX_DRV_TREBLE, 0), "BOOST") && !strcmp(px_param_name(PX_B_DRIVE, PX_DRV_TREBLE, 1), "RANGE") &&
       !strcmp(px_param_name(PX_B_DRIVE, PX_DRV_TREBLE, 2), "LEVEL"), "TREBLE BOOST: the last drive, knobs BOOST, RANGE, LEVEL");
    static const int RV[6] = {0, 33, 34, 66, 67, 100};
    static const char *const RT[6] = {"FULL", "FULL", "MID", "MID", "TREBLE", "TREBLE"};
    int texts = 1;
    for (int i = 0; i < 6; i++) { txt[0] = 0; texts &= px_param_text(PX_B_DRIVE, PX_DRV_TREBLE, 1, RV[i], txt) && !strcmp(txt, RT[i]); }
    ok(texts, "RANGE reads FULL (0..33), MID (34..66), TREBLE (67..100)");
    txt[0] = 0;
    ok(!px_param_text(PX_B_DRIVE, PX_DRV_TS9, 1, 100, txt), "the other drives' knobs keep their plain numbers");

    /* 2. small signals: the shape */
    float lo[3], hi[3], a, b2;
    for (int r = 0; r < 3; r++) {
        sine(0, r * 50, 200.0f, 0.002f, &lo[r], &a, &b2);
        sine(0, r * 50, 3000.0f, 0.002f, &hi[r], &a, &b2);
    }
    snprintf(what, sizeof what, "small signals, 3 kHz over 200 Hz: FULL %+.1f dB, MID %+.1f dB, TREBLE %+.1f dB",
             db(hi[0] / lo[0]), db(hi[1] / lo[1]), db(hi[2] / lo[2]));
    ok(db(hi[2] / lo[2]) > 14.0f && db(hi[1] / lo[1]) > 5.0f && db(hi[1] / lo[1]) < db(hi[2] / lo[2]) - 5.0f &&
       fabsf(db(hi[0] / lo[0])) < 5.0f, what);

    /* 3. driven: lopsided clipping */
    float h1, h2, h3;
    sine(100, 100, 1000.0f, 0.2f, &h1, &h2, &h3);
    snprintf(what, sizeof what, "driven hard (BOOST 100, 1 kHz at 0.2): 2nd harmonic %.1f dB, 3rd %.1f dB under the 1st",
             db(h1 / h2), db(h1 / h3));
    ok(h2 > 0.1f * h3 && h2 > 0.01f * h1 && h3 > 0.01f * h1, what);

    /* 4. BOOST: more level, more drive */
    float l[5], d3[5];
    int up = 1;
    for (int i = 0; i < 5; i++) {
        sine(i * 25, 100, 1000.0f, 0.03f, &h1, &h2, &h3);
        l[i] = db(h1); d3[i] = db((h2 + h3) / h1);
        if (i && (l[i] < l[i - 1] + 1.0f || d3[i] < d3[i - 1])) up = 0;
    }
    snprintf(what, sizeof what, "BOOST 0..100: level %.1f %.1f %.1f %.1f %.1f dB, harmonics %.0f %.0f %.0f %.0f %.0f dB", l[0], l[1], l[2], l[3], l[4], d3[0], d3[1], d3[2], d3[3], d3[4]);
    ok(up, what);

    /* 5. its level at noon: the guitar's loudness without it */
    static const char *const LOOP[5] = {"riff", "lead", "chords", "chug", "arpeggio"};
    for (int k = 0; k < 5; k++) {
        char path[96];
        snprintf(path, sizeof path, "assets/loops/%s.wav", LOOP[k]);
        if (load(path, ndi)) ndi++;
    }
    if (ndi < 5) { printf("assets/loops: %d of the 5 DI loops (run from the repository)\n", ndi); failed++; }
    else {
        px_preset_t p = chain(0, 0, 0, 0, 1);
        const double ref = loud(&p);
        double worst = 0;
        for (int r = 0; r < 3; r++) {
            p = chain(1, 50, r * 50, 50, 1);
            const double d = loud(&p) - ref;
            if (fabs(d) > worst) worst = fabs(d);
            if (measure) {                              /* the LEVEL (linear: 2 x LEVEL) that matches, through the amp's curve */
                int k0 = 0, k1 = 100;
                while (k1 - k0 > 1) { const int k = (k0 + k1) / 2; p = chain(1, 50, r * 50, k, 1); if (loud(&p) > ref) k1 = k; else k0 = k; }
                p = chain(1, 50, r * 50, k0, 1); const double l0 = loud(&p) - ref;
                p = chain(1, 50, r * 50, k1, 1); const double l1 = loud(&p) - ref;
                const double g0 = k0 ? 20.0 * log10(k0 / 50.0) : -40.0, g1 = 20.0 * log10(k1 / 50.0);
                const double g = l1 > l0 ? g0 + (g1 - g0) * (0.0 - l0) / (l1 - l0) : g1;
                double bb[5];
                for (int i = 0; i < 5; i++) { p = chain(1, i * 25, r * 50, 50, 1); bb[i] = loud(&p) - ref; }
                printf("RANGE %s: %+.2f dB at noon, add %+.2f dB to TB_DB; BOOST 0/25/50/75/100: %+.1f %+.1f %+.1f %+.1f %+.1f dB\n",
                       RT[r * 2], d, g, bb[0], bb[1], bb[2], bb[3], bb[4]);
            }
        }
        snprintf(what, sizeof what, "at noon, every RANGE: the loudness without it (TWIN at noon + 1x12, %.1f LUFS), largest difference %.2f dB", ref, worst);
        ok(worst < 0.5, what);
    }
    printf("\n%s\n", failed ? "treble test FAILED" : "treble test passed");
    return failed ? 1 : 0;
}
