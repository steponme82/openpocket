/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — test build 42: LOW OCTAVE's bass out of the chain and the stereo output (engine/px_dsp.c px_lo_*, px_sd_*), the Bluetooth audio image's
 * (-DPX_OWN_FX=1). The octave skips every block after PIT and joins at the chain's end, or goes to the right channel alone:
 *  1. through an amp, a cabinet and a long reverb: the guitar's sound is the same with the octave on or off (the chain
 *     never gets the octave), the reverb's tail included; the octave's 41 Hz comes out whole (the cabinet no longer cuts it);
 *  2. px_engine_bass_out: the left channel is the guitar alone, bit for bit; the right one the octave alone; with no LOW
 *     OCTAVE on, the right channel is silent;
 *  3. a NAM amp on two cores (PRE, NAM and POST a block apart, as firmware/px_audio.h runs them): the same output bit for bit
 *     as the plain engine, one block later, both channels;
 *  4. PIT switched off while the octave plays: it fades out (no step), then nothing; the volume pedal's heel silences it;
 *  5. its level with the amp on: PX_LO_OUTG (+12 dB), within the amps' gain on the five DI loops of assets/loops (RMS,
 *     through the 1x12 US OPEN at noon: the eight fitted amps and the first two NAM factory models; `bassout measure`
 *     lists them);
 *  6. OUTPUT STEREO (px_engine_out PX_OUT_STEREO), inline and through the wet lanes: left and right summed are the mono sound;
 *     the delay's echoes and the reverb's tail reach the sides; with no DELAY or REVERB both channels are the mono sound;
 *     a reverb switched off with TAILS rings out wide; every MOD model sums to the mono sound, seven of them spread (CE
 *     CHORUS, DIMENSION, FLANGER, PHASE 90, SMALL STONE, UNI-VIBE, ROTARY), the tremolos and VIBRATO stay in the middle;
 *     a NAM amp on two cores (with a chorus and a delay) gives both channels bit for bit.
 *   cc -O2 -DPX_OWN_FX=1 -Iengine tests/engine/bassout.c engine/px_dsp.c firmware/px_math.c -lm -o build/bassout && build/bassout */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100.0f
#define FR 128
#define NB 1200                                        /* 3.48 s */
#define N (FR * NB)
static px_engine e;
static float in[N], o_on[N], o_off[N], o_l[N], o_r[N], o_x[N];
static int failed;
static void ok(int c, const char *what) { printf("%-104s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

/* the chain: LOW OCTAVE (D#3 crossover, OCT -1 0 DB or OFF, DRY 0 DB), TWIN, 1x12 US OPEN, a long HALL; or a NAM amp */
static void preset(px_preset_t *p, int oct, int nam)
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    p->on[PX_B_PITCH] = 1; p->type[PX_B_PITCH] = PX_PIT_LOWOCT;
    const uint8_t lo[5] = {40, (uint8_t)oct, 50, 0, 0};
    memcpy(p->p[PX_B_PITCH], lo, 5);
    p->on[PX_B_AMP] = 1; p->type[PX_B_AMP] = nam ? PX_AMP_NAM : PX_AMP_TWIN; memset(p->p[PX_B_AMP], 50, 5);
    if (nam) p->p[PX_B_AMP][0] = 0;                    /* (the first factory model) */
    p->on[PX_B_CAB] = 1; p->type[PX_B_CAB] = 0; memset(p->p[PX_B_CAB], 50, 5);
    p->on[PX_B_REVERB] = 1; p->type[PX_B_REVERB] = PX_REV_HALL;
    p->p[PX_B_REVERB][0] = 100; p->p[PX_B_REVERB][1] = 50; p->p[PX_B_REVERB][2] = 80;
}
static int lanes;                                      /* 1: the wet lanes on, as the firmware with two cores (tests/engine/tails.c) */
/* the plain engine in blocks of 128 (two calls of 64; with the lanes: swap, the chain, the lanes' wet); r: the right channel
 * (px_engine_out: GTR+BASS when r and mode 0, else the mode given) */
static void run_m(const px_preset_t *p, float *y, float *r, int mode)
{
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    if (lanes) px_wet_mode(&e, 1, 2 * FR);
    for (int k = 0; k < N; k += FR) {
        if (lanes) px_wet_swap(&e);
        for (int o = 0; o < FR; o += 64) {
            px_engine_out(&e, mode, r ? r + k + o : 0);
            px_engine_process(&e, in + k + o, y + k + o, 64);
        }
        if (lanes) px_wet_run(&e, 0, 0);
    }
    px_engine_out(&e, PX_OUT_MONO, 0);
}
static void run(const px_preset_t *p, float *y, float *r) { run_m(p, y, r, r ? PX_OUT_BASS : PX_OUT_MONO); }
/* the two-core pipeline, as firmware/px_audio.h dsp_pipe_run: POST on the previous block's network output, PRE and NAM on
 * this block; the output comes one block later */
static void run_pipe_m(const px_preset_t *p, float *y, float *r, int mode)
{
    static float nb[2][FR];
    int cur = 0;
    px_engine_init(&e, SR);
    px_engine_set(&e, p);
    memset(y, 0, sizeof(float) * N);
    if (r) memset(r, 0, sizeof(float) * N);
    for (int b = 0; b < NB; b++) {
        if (b) for (int o = 0; o < FR; o += 64) {
            px_engine_out(&e, mode, r ? r + FR * (b - 1) + o : 0);
            px_engine_part(&e, nb[cur ^ 1] + o, y + FR * (b - 1) + o, 64, PX_PART_POST);
        }
        for (int o = 0; o < FR; o += 64) px_engine_part(&e, in + FR * b + o, nb[cur] + o, 64, PX_PART_PRE);
        for (int o = 0; o < FR; o += 64) px_engine_part(&e, nb[cur] + o, nb[cur] + o, 64, PX_PART_NAM);
        cur ^= 1;
    }
    px_engine_out(&e, PX_OUT_MONO, 0);
}
static void run_pipe(const px_preset_t *p, float *y, float *r) { run_pipe_m(p, y, r, r ? PX_OUT_BASS : PX_OUT_MONO); }
static float amp_at(const float *y, float f, int a, int b)   /* the f component's amplitude over [a, b) */
{
    double c = 0, s = 0;
    for (int k = a; k < b; k++) { double w = 6.283185307 * f * k / SR; c += y[k] * cos(w); s += y[k] * sin(w); }
    return (float)(2.0 * sqrt(c * c + s * s) / (b - a));
}
static float peak(const float *y, int a, int b) { float m = 0; for (int k = a; k < b; k++) if (fabsf(y[k]) > m) m = fabsf(y[k]); return m; }
static float maxdiff(const float *x, const float *y, int a, int b)
{
    float m = 0; for (int k = a; k < b; k++) if (fabsf(x[k] - y[k]) > m) m = fabsf(x[k] - y[k]); return m;
}
static float db(float a) { return 20.0f * log10f(a > 1e-12f ? a : 1e-12f); }
/* the input: a note of f0 for 1 s at the pedal's level, then silence. bright 0: a low E's 2nd and 3rd harmonics, as a
 * guitar's low string through the DI; 1: 12 harmonics falling as 1 / n, as a brighter, higher note */
static void note_in(float f0, int bright)
{
    const int note = (int)(1.0f * SR);
    for (int k = 0; k < N; k++) {
        const float t = (float)k / SR, a = k < note ? 0.15f * expf(-t * 1.2f) : 0.0f;
        float v = 0.0f;
        if (bright) for (int h = 1; h <= 12; h++) v += sinf(6.2831853f * f0 * (float)h * t) / (float)h;
        else v = sinf(6.2831853f * f0 * t) + 0.5f * sinf(6.2831853f * 2.0f * f0 * t) + 0.3f * sinf(6.2831853f * 3.0f * f0 * t);
        in[k] = a * v;
    }
}

static float *di[5]; static int din[5], ndi;
static int load(const char *path, int k)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) { fclose(f); return 0; }
    for (;;) {
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
int main(int argc, char **argv)
{
    const int measure = argc > 1 && !strcmp(argv[1], "measure");
    char what[200];
    px_preset_t p, q;
    /* a low E (82.4 Hz, with its 2nd and 3rd harmonics), 1 s at the pedal's level, then silence */
    const int note = (int)(1.0f * SR);
    note_in(82.41f, 0);
    const int pa = (int)(0.3f * SR), pb = (int)(0.9f * SR), ta = note + (int)(0.3f * SR);

    /* 1. TWIN, 1x12 US OPEN, a long HALL: the chain never gets the octave */
    preset(&p, 50, 0); run(&p, o_on, 0);
    preset(&q, 0, 0); run(&q, o_off, 0);
    const float tail = maxdiff(o_on, o_off, ta, N), ring = peak(o_off, ta, N);
    snprintf(what, sizeof what, "the reverb's tail is the same with the octave on and off (difference %.1f dB, the tail %.1f dB)", db(tail), db(ring));
    ok(tail < 1e-5f && ring > 1e-3f, what);
    for (int k = 0; k < N; k++) o_x[k] = o_on[k] - o_off[k];   /* what the octave adds */
    const float x41 = amp_at(o_x, 41.2f, pa, pb), x82 = amp_at(o_x, 82.41f, pa, pb);
    snprintf(what, sizeof what, "while the note plays the octave adds its 41.2 Hz (%.1f dB) and nothing at the note (82.4 Hz: %.1f dB)", db(x41), db(x82));
    ok(x41 > 0.02f && x82 < 0.05f * x41, what);

    /* 2. the right channel */
    run(&p, o_l, o_r);
    ok(!memcmp(o_l, o_off, sizeof o_l), "px_engine_bass_out: the left channel is the guitar alone, bit for bit (the octave off)");
    const float r41 = amp_at(o_r, 41.2f, pa, pb);
    snprintf(what, sizeof what, "the right channel is the octave alone: 41.2 Hz %.1f dB (added to the mix: %.1f dB), 82.4 Hz %.1f dB", db(r41), db(x41), db(amp_at(o_r, 82.41f, pa, pb)));
    ok(fabsf(db(r41) - db(x41)) < 0.1f && amp_at(o_r, 82.41f, pa, pb) < 0.05f * r41 && peak(o_r, ta, N) < 1e-4f, what);
    { px_preset_t z = q; z.on[PX_B_PITCH] = 0; run(&z, o_l, o_r); }
    ok(peak(o_r, 0, N) == 0.0f, "no LOW OCTAVE on: the right channel is silent");

    /* 3. a NAM amp on two cores, both routings */
    preset(&p, 50, 1);
    run(&p, o_on, 0); run_pipe(&p, o_x, 0);
    ok(!memcmp(o_on, o_x, sizeof(float) * FR * (NB - 1)), "a NAM amp on two cores (PRE / NAM / POST): the plain engine's output bit for bit, a block later");
    run(&p, o_on, o_r); run_pipe(&p, o_x, o_l);
    ok(!memcmp(o_on, o_x, sizeof(float) * FR * (NB - 1)) && !memcmp(o_r, o_l, sizeof(float) * FR * (NB - 1)) && amp_at(o_r, 41.2f, pa, pb) > 0.02f,
       "and with the octave on the right channel: both channels bit for bit, the octave there");

    /* 4. PIT off while the octave plays: a fade, no step; the volume pedal */
    preset(&p, 50, 0);
    px_engine_init(&e, SR); px_engine_set(&e, &p);
    q = p; q.on[PX_B_PITCH] = 0;
    const int sw = (int)(0.5f * SR) / 64 * 64;
    float step = 0.0f, prev = 0.0f;
    for (int k = 0; k < N; k += 64) {
        if (k == sw) px_engine_knobs(&e, &q);
        px_engine_out(&e, PX_OUT_BASS, o_r + k);
        px_engine_process(&e, in + k, o_l + k, 64);
        for (int i = 0; i < 64; i++) { if (k + i > sw - 64 && k + i < sw + 4096 && fabsf(o_r[k + i] - prev) > step) step = fabsf(o_r[k + i] - prev); prev = o_r[k + i]; }
    }
    px_engine_out(&e, PX_OUT_MONO, 0);
    const float before = peak(o_r, sw - 4096, sw);
    snprintf(what, sizeof what, "PIT off while the octave plays: it fades (largest step %.4f against its peak %.3f), then silence (%.1f dB)", step, before, db(peak(o_r, sw + 4096, N)));
    ok(before > 0.01f && step < 0.1f * before && peak(o_r, sw + 4096, N) < 1e-5f, what);
    px_engine_init(&e, SR); px_engine_set(&e, &p);
    px_exp_set(0);                                     /* the volume pedal at the heel (no WHAMMY, no AUTO WAH: the volume) */
    for (int k = 0; k < N; k += 64) { px_engine_out(&e, PX_OUT_BASS, o_r + k); px_engine_process(&e, in + k, o_l + k, 64); }
    px_engine_out(&e, PX_OUT_MONO, 0);
    px_exp_set(-1);
    snprintf(what, sizeof what, "the volume pedal at the heel: the octave silent too (%.1f dB)", db(peak(o_r, (int)(0.2f * SR), N)));
    ok(peak(o_r, (int)(0.2f * SR), N) < 1e-4f, what);

    /* 6. STEREO: the mono sound in the middle, DELAY's and REVERB's wet (and their TAILS) to the sides; inline and in the lanes */
    for (lanes = 0; lanes < 2; lanes++) {
        const char *how = lanes ? "in the lanes" : "inline";
        px_preset_t s;
        preset(&s, 50, 0);
        s.on[PX_B_AMP] = 0; s.on[PX_B_CAB] = 0;          /* (no amp: below the knee, so the sum is exact) */
        s.on[PX_B_DELAY] = 1; s.type[PX_B_DELAY] = PX_DLY_DIGITAL;
        s.p[PX_B_DELAY][0] = 20; s.p[PX_B_DELAY][1] = 50; s.p[PX_B_DELAY][2] = 60; s.p[PX_B_DELAY][3] = 60;
        run_m(&s, o_on, 0, PX_OUT_MONO);
        run_m(&s, o_l, o_r, PX_OUT_STEREO);
        float sum = 0.0f;
        double d2 = 0, m2 = 0;
        for (int k = 0; k < N; k++) {
            const float m = 0.5f * (o_l[k] + o_r[k]), dd = 0.5f * (o_l[k] - o_r[k]);
            if (fabsf(m - o_on[k]) > sum) sum = fabsf(m - o_on[k]);
            if (k >= note) { d2 += (double)dd * dd; m2 += (double)m * m; }
        }
        snprintf(what, sizeof what, "STEREO %s: left and right summed are the mono sound (largest difference %.1f dB of %.1f dB)", how, db(sum), db(peak(o_on, 0, N)));
        ok(sum < 1e-6f && peak(o_on, 0, N) > 0.05f, what);
        snprintf(what, sizeof what, "STEREO %s: the delay's echoes and the reverb's tail reach the sides (side %.1f dB under the middle)", how, 10.0 * log10(m2 / (d2 + 1e-30)));
        ok(d2 > 0.04 * m2, what);
        s.on[PX_B_DELAY] = 0; s.on[PX_B_REVERB] = 0;
        run_m(&s, o_l, o_r, PX_OUT_STEREO);
        run_m(&s, o_on, 0, PX_OUT_MONO);
        snprintf(what, sizeof what, "STEREO %s, no DELAY or REVERB: both channels the mono sound, the octave in the middle", how);
        ok(maxdiff(o_l, o_r, 0, N) == 0.0f && maxdiff(o_l, o_on, 0, N) == 0.0f && amp_at(o_r, 41.2f, pa, pb) > 0.02f, what);
        /* TAILS: the reverb switched off while it rings: its tail stays wide */
        preset(&s, 0, 0);
        s.on[PX_B_AMP] = 0; s.on[PX_B_CAB] = 0;
        px_engine_init(&e, SR); px_engine_tails(&e, 1); px_engine_set(&e, &s);
        if (lanes) px_wet_mode(&e, 1, 2 * FR);
        px_preset_t off = s; off.on[PX_B_REVERB] = 0;
        const int cut = (int)(0.8f * SR) / FR * FR;
        for (int k = 0; k < N; k += FR) {
            if (k == cut) px_engine_knobs(&e, &off);
            if (lanes) px_wet_swap(&e);
            for (int o = 0; o < FR; o += 64) { px_engine_out(&e, PX_OUT_STEREO, o_r + k + o); px_engine_process(&e, in + k + o, o_l + k + o, 64); }
            if (lanes) px_wet_run(&e, 0, 0);
        }
        px_engine_out(&e, PX_OUT_MONO, 0); px_engine_tails(&e, 0);
        d2 = m2 = 0;
        for (int k = note + (int)(0.2f * SR); k < N; k++) { const float m = 0.5f * (o_l[k] + o_r[k]), dd = 0.5f * (o_l[k] - o_r[k]); d2 += (double)dd * dd; m2 += (double)m * m; }
        snprintf(what, sizeof what, "STEREO %s, TAILS: the reverb switched off rings out wide (side %.1f dB under the middle)", how, 10.0 * log10(m2 / (d2 + 1e-30)));
        ok(m2 > 1e-6 && d2 > 0.04 * m2, what);
    }
    lanes = 0;
    {   /* every MOD model: left and right summed are the mono sound; CE CHORUS, DIMENSION, FLANGER, PHASE 90, SMALL STONE,
         * UNI-VIBE and ROTARY spread, the tremolos and VIBRATO stay in the middle */
        int sums = 1, wide = 1, mid = 1;
        char bad[160] = "";
        note_in(196.0f, 1);                              /* (a G, brighter: the rotary's horn and the phasers work on the highs) */
        for (int t = 0; t < PX_MOD_COUNT; t++) {
            px_preset_t s;
            preset(&s, 0, 0);
            s.on[PX_B_PITCH] = 0; s.on[PX_B_AMP] = 0; s.on[PX_B_CAB] = 0; s.on[PX_B_REVERB] = 0;
            s.on[PX_B_MOD] = 1; s.type[PX_B_MOD] = (uint8_t)t; memset(s.p[PX_B_MOD], 60, 5);
            run_m(&s, o_on, 0, PX_OUT_MONO);
            run_m(&s, o_l, o_r, PX_OUT_STEREO);
            float dm = 0.0f; double d2 = 0, m2 = 0;
            for (int k = 0; k < note; k++) {
                const float m = 0.5f * (o_l[k] + o_r[k]), dd = 0.5f * (o_l[k] - o_r[k]);
                if (fabsf(m - o_on[k]) > dm) dm = fabsf(m - o_on[k]);
                d2 += (double)dd * dd; m2 += (double)m * m;
            }
            const int st = t == PX_MOD_CHORUS || t == PX_MOD_DIMENSION || t == PX_MOD_FLANGER || t == PX_MOD_PHASE90 ||
                           t == PX_MOD_STONE || t == PX_MOD_UNIVIBE || t == PX_MOD_ROTARY;
            if (dm > 1e-6f) { sums = 0; snprintf(bad + strlen(bad), sizeof bad - strlen(bad), " %s sum %.1f dB", px_type_name(PX_B_MOD, t), db(dm)); }
            if (st && !(d2 > 0.01 * m2)) { wide = 0; snprintf(bad + strlen(bad), sizeof bad - strlen(bad), " %s narrow", px_type_name(PX_B_MOD, t)); }
            if (!st && maxdiff(o_l, o_r, 0, N) != 0.0f) { mid = 0; snprintf(bad + strlen(bad), sizeof bad - strlen(bad), " %s not in the middle", px_type_name(PX_B_MOD, t)); }
            if (st) printf("   STEREO %-13s side %5.1f dB under the middle\n", px_type_name(PX_B_MOD, t), 10.0 * log10(m2 / (d2 + 1e-30)));
        }
        snprintf(what, sizeof what, "STEREO, every MOD model: left and right summed are the mono sound%s", sums ? "" : bad);
        ok(sums, what);
        snprintf(what, sizeof what, "STEREO: CE CHORUS, DIMENSION, FLANGER, PHASE 90, SMALL STONE, UNI-VIBE and ROTARY reach the sides%s", wide ? "" : bad);
        ok(wide, what);
        snprintf(what, sizeof what, "STEREO: the tremolos and VIBRATO stay in the middle%s", mid ? "" : bad);
        ok(mid, what);
        note_in(82.41f, 0);
    }
    {   /* a NAM amp on two cores, STEREO: both channels bit for bit, a block later */
        px_preset_t s;
        preset(&s, 50, 1);
        s.on[PX_B_DELAY] = 1; s.type[PX_B_DELAY] = PX_DLY_DIGITAL; s.p[PX_B_DELAY][0] = 20; s.p[PX_B_DELAY][2] = 60;
        s.on[PX_B_MOD] = 1; s.type[PX_B_MOD] = PX_MOD_CHORUS; memset(s.p[PX_B_MOD], 60, 5);
        run_m(&s, o_on, o_r, PX_OUT_STEREO); run_pipe_m(&s, o_x, o_l, PX_OUT_STEREO);
        ok(!memcmp(o_on, o_x, sizeof(float) * FR * (NB - 1)) && !memcmp(o_r, o_l, sizeof(float) * FR * (NB - 1)) && maxdiff(o_on, o_r, 0, N) > 1e-3f,
           "STEREO, a NAM amp on two cores: both channels bit for bit, a block later, and they differ");
    }

    /* 5. the octave's level with the amp on: the amps' gain (RMS, on the DI loops, through the 1x12 US OPEN at noon) */
    static const char *const LOOP[5] = {"riff", "lead", "chords", "chug", "arpeggio"};
    for (int k = 0; k < 5; k++) { char path[96]; snprintf(path, sizeof path, "assets/loops/%s.wav", LOOP[k]); if (load(path, ndi)) ndi++; }
    if (ndi < 5) { printf("assets/loops: %d of the 5 DI loops (run from the repository)\n", ndi); failed++; }
    else {
        double sum = 0, lo = 99, hi = -99;
        int na = 0;
        for (int a = 0; a < PX_AMP_COUNT + 2; a++) {     /* the eight fitted amps, then the first two NAM factory models */
            px_preset_t c;
            px_preset_factory(&c, 0);
            for (int b = 0; b < PX_NBLK; b++) c.on[b] = 0;
            c.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
            c.on[PX_B_AMP] = 1; c.type[PX_B_AMP] = (uint8_t)(a < PX_AMP_COUNT ? a : PX_AMP_NAM); memset(c.p[PX_B_AMP], 50, 5);
            if (a >= PX_AMP_COUNT) for (int v = 0; v <= 100; v++) if (px_nam_model(v) == a - PX_AMP_COUNT) { c.p[PX_B_AMP][0] = (uint8_t)v; break; }
            c.on[PX_B_CAB] = 1; c.type[PX_B_CAB] = 0; memset(c.p[PX_B_CAB], 50, 5);
            double g = 0;
            for (int k = 0; k < ndi; k++) {
                float *y = calloc((size_t)din[k], sizeof(float));
                px_engine_init(&e, SR); px_engine_set(&e, &c);
                for (int i = 0; i < din[k]; i += 64) px_engine_process(&e, di[k] + i, y + i, 64);
                double si = 0, so = 0;
                for (int i = 4410; i < din[k]; i++) { si += (double)di[k][i] * di[k][i]; so += (double)y[i] * y[i]; }
                g += 10.0 * log10(so / si) / ndi;
                free(y);
            }
            if (measure) printf("   %-12s %+.1f dB\n", px_type_name(PX_B_AMP, c.type[PX_B_AMP]), g);
            sum += g; na++; if (g < lo) lo = g; if (g > hi) hi = g;
        }
        /* PX_LO_OUTG, read back: the octave on the right channel with the amp on, against with it off */
        preset(&p, 50, 0); p.on[PX_B_CAB] = 0; p.on[PX_B_REVERB] = 0;
        run(&p, o_l, o_r); const float on = amp_at(o_r, 41.2f, pa, pb);
        p.on[PX_B_AMP] = 0; run(&p, o_l, o_x); const float off = amp_at(o_x, 41.2f, pa, pb);
        const double set = db(on) - db(off), mean = sum / na;
        snprintf(what, sizeof what, "with the amp on the octave gets %+.2f dB: the amps' gain on the DI loops is %+.1f to %+.1f dB, %+.1f on average", set, lo, hi, mean);
        ok(set >= lo && set <= hi && fabs(set - mean) < 2.5, what);
    }
    printf("\n%s\n", failed ? "bass out test FAILED" : "bass out test passed");
    return failed ? 1 : 0;
}
