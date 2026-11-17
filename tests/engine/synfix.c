/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — test build 44: the Bluetooth audio image's fixes and rebuilds (-DPX_OWN_FX=1; engine/px_dsp.c, docs/OWN-FX.md):
 *  1. SHIMMER: on held notes from 73 Hz to 988 Hz its octave is 2 f within 8 cents (it was up to 51 cents off: the plain
 *     shifter's taps came in out of phase);
 *  2. BARITONE: its sound comes 8 to 13 ms after the pick on average (it was 20 to 25), in tune within 12 cents on low notes;
 *  3. MONO SYNTH: on twenty notes (picked and legato, 82 Hz to 1 kHz, weak fundamentals, a bend) it plays the note within
 *     15 cents after 35 ms at most on average, never an octave off, and a wrong pitch for less than 15 ms on a picked note;
 *  4. SITAR: the jawari brightens the note (the share of its sound above 2 kHz at least twice the dry guitar's) and keeps it
 *     harmonic (the note's pitch unmoved); the tarab rings in KEY after the note stops;
 *  5. STRINGS: the bow swells in (the pick's first 5 ms at least 12 dB under the sound 300 ms on) and holds the level while
 *     the guitar dies (the level 1 s on within 6 dB of the level at 300 ms, where the guitar fell 9 dB); OCTAVE's cellos an
 *     octave down, in tune;
 *  6. TAPE FLUTE: plays the note's pitch within 20 cents (the tape's wow and flutter on it), says nothing to silence, swells in
 *     over ATTACK.
 * Test build 45:
 *  7. TAPE FLUTE on the semitone: a note picked 40 cents sharp that settles 25 sharp plays its semitone exactly from the first
 *     sound, without a glide; a new note two semitones up is taken within 40 ms; a guitar 30 cents flat with a vibrato of 20
 *     cents stays on its semitone, and its next semitone, as flat, is taken (a guitar half a semitone off is between two:
 *     no window can tell);
 *  8. MONO SYNTH through hum (50 Hz and its 3rd and 5th, 30 dB under the notes): the twenty notes as in 3, and once the
 *     string is muted, the hum alone, the synth lets go within 0.2 s (it played the last note on).
 * Test build 47:
 *  9. 12-STRING steady: on held notes, its level against the guitar's (50 ms windows) swings under 1 dB at DETUNE 0, and
 *     at DETUNE 50 it moves under 4 dB a second on average (it was 10, the octave beating against the note's 2nd harmonic at
 *     7.5 cents with the unison copy's sweep); the octave in tune (2 cents) at DETUNE 0, 3 cents sharp at 100; DETUNE's text.
 *   cc -O2 -DPX_OWN_FX=1 -Iengine tests/engine/synfix.c engine/px_dsp.c firmware/px_math.c -lm -o build/synfix && build/synfix */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"

#define SR 44100
#define NS (SR * 6)
static px_engine e;
static float in[NS], out[NS];
static int failed;
static void ok(int c, const char *what) { printf("%-118s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }
static void base(px_preset_t *p, int blk, int ty, const uint8_t k[5])
{
    px_preset_factory(p, 0);
    for (int b = 0; b < PX_NBLK; b++) p->on[b] = 0;
    p->p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    px_order_default(p->order);
    p->on[blk] = 1; p->type[blk] = (uint8_t)ty; memcpy(p->p[blk], k, 5);
}
static void run(const px_preset_t *p, int n)
{
    px_engine_init(&e, (float)SR); px_engine_set(&e, p);
    int k = 0;
    for (; k + 64 <= n; k += 64) px_engine_process(&e, in + k, out + k, 64);
    for (; k < n; k++) out[k] = 0.0f;                  /* (not a whole block: nothing left of the run before) */
}
/* the strongest frequency within [f0, f1] over [a, b): a Hann-windowed scan, 0.02 Hz apart */
static double peak_f(const float *y, int a, int b, double f0, double f1)
{
    double best = -1, bf = f0;
    for (double f = f0; f <= f1; f += 0.02) {
        double c = 0, s = 0;
        for (int k = a; k < b; k += 2) { const double w = 2 * M_PI * f * k / SR, h = 0.5 - 0.5 * cos(2 * M_PI * (k - a) / (b - a)); c += y[k] * h * cos(w); s += y[k] * h * sin(w); }
        if (c * c + s * s > best) { best = c * c + s * s; bf = f; }
    }
    return bf;
}
static double amp_at(const float *y, double f, int a, int b)
{
    double c = 0, s = 0;
    for (int k = a; k < b; k++) { const double w = 2 * M_PI * f * k / SR; c += y[k] * cos(w); s += y[k] * sin(w); }
    return 2 * sqrt(c * c + s * s) / (b - a);
}
static double rms(const float *y, int a, int b) { double s = 0; for (int k = a; k < b; k++) s += (double)y[k] * y[k]; return sqrt(s / (b - a)); }
static double cents(double f, double want) { return 1200.0 * log2(f / want); }
/* y's level against x's in 50 ms windows over [a, b), less its straight trend: the swing (peak to trough, dB) and how fast it
 * moves (dB a second, on average) */
static void wobble(const float *y, const float *x, int a, int b, double *pp, double *speed)
{
    enum { W = SR / 20 };
    static double d[NS / (W / 2) + 2];
    int n = 0;
    for (int s = a; s + W <= b; s += W / 2) {
        double ey = 1e-20, ex = 1e-20;
        for (int k = s; k < s + W; k++) { ey += (double)y[k] * y[k]; ex += (double)x[k] * x[k]; }
        d[n++] = 10 * log10(ey / ex);
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) { sx += i; sy += d[i]; sxx += (double)i * i; sxy += i * d[i]; }
    const double sl = (n * sxy - sx * sy) / (n * sxx - sx * sx), ic = (sy - sl * sx) / n;
    double lo = 1e9, hi = -1e9, mv = 0;
    for (int i = 0; i < n; i++) { const double v = d[i] - (ic + sl * i); if (v < lo) lo = v; if (v > hi) hi = v; if (i) mv += fabs(d[i] - d[i - 1] - sl); }
    *pp = hi - lo; *speed = mv / ((n - 1) * (W / 2.0) / SR);
}
static void saw_note(float *x, int n, double f, double amp, double decay, int harmonics)
{
    for (int k = 0; k < n; k++) {
        const double t = (double)k / SR; double v = 0;
        for (int h = 1; h <= harmonics; h++) v += sin(2 * M_PI * f * h * t) / h;
        x[k] = (float)(amp * exp(-t * decay) * v);
    }
}

int main(void)
{
    char what[256];
    /* 1. SHIMMER on held notes: the octave in tune */
    {
        static const double F[6] = {73.42, 110.0, 196.0, 329.63, 659.26, 987.77};
        const uint8_t k[5] = {55, 45, 100, 100, 0};
        double worst = 0;
        for (int i = 0; i < 6; i++) {
            for (int s = 0; s < NS; s++) in[s] = (float)(0.2 * sin(2 * M_PI * F[i] * s / SR));
            px_preset_t p; base(&p, PX_B_REVERB, PX_REV_SHIMMER, k);
            run(&p, 4 * SR);
            const double c = cents(peak_f(out, (int)(1.5 * SR), 4 * SR - 64, 2 * F[i] * 0.97, 2 * F[i] * 1.03), 2 * F[i]);
            if (fabs(c) > fabs(worst)) worst = c;
        }
        snprintf(what, sizeof what, "SHIMMER: held notes 73 Hz to 988 Hz, the octave within %.1f cents of 2 f (it was up to 51 off)", worst);
        ok(fabs(worst) < 8.0, what);
    }
    /* 2. BARITONE: how late, how in tune */
    {
        static const float HZ[12] = {82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 98.0f, 130.81f, 164.81f, 220.0f, 293.66f, 392.0f};
        for (int tune = 40; tune <= 100; tune += 60) {
            const uint8_t k[5] = {(uint8_t)tune, 50, 100, 0, 0};
            const int st = -1 - (int)((float)tune * 0.01f * 11.99f);
            for (int s = 0; s < NS; s++) {
                const int nn = s / (SR / 2), t0 = nn * (SR / 2); const float f = HZ[nn % 12], t = (float)(s - t0) / SR;
                float ph = f * t; ph -= floorf(ph);
                in[s] = 0.3f * expf(-t * 4.0f) * (2.0f * ph - 1.0f);
            }
            px_preset_t p; base(&p, PX_B_SYN, PX_SYN_BARI, k);
            run(&p, NS);
            double sum = 0; int worst = 0;
            for (int nn = 1; nn < 12; nn++) {
                const int t0 = nn * (SR / 2), t1 = t0 + SR / 2;
                float pk = 0; for (int s = t0; s < t1; s++) if (fabsf(out[s]) > pk) pk = fabsf(out[s]);
                int at = t1; for (int s = t0; s < t1; s++) if (fabsf(out[s]) > 0.5f * pk) { at = s; break; }
                sum += at - t0; if (at - t0 > worst) worst = at - t0;
            }
            const double avg = sum / 11 * 1000.0 / SR;
            /* in tune: a held low note */
            double wc = 0;
            for (int i = 0; i < 3; i++) {
                static const double LF[3] = {82.41, 110.0, 146.83};
                saw_note(in, 4 * SR, LF[i], 0.15, 0.0, 8);
                run(&p, 4 * SR);
                const double want = LF[i] * pow(2.0, st / 12.0), c = cents(peak_f(out, SR, 3 * SR, want * 0.97, want * 1.03), want);
                if (fabs(c) > fabs(wc)) wc = c;
            }
            snprintf(what, sizeof what, "BARITONE %d semitones: the sound %.1f ms after the pick on average (worst %.1f; it was 23), low notes within %.1f cents",
                     st, avg, worst * 1000.0 / SR, wc);
            ok(avg > 3.0 && avg < 13.0 && fabs(wc) < 12.0, what);
        }
    }
    /* 3. MONO SYNTH: twenty notes */
    {
        typedef struct { float f, w2; int legato; float bend; } nt;
        static const nt NT[] = {
            {82.41f, 0.5f, 0, 0}, {110.0f, 0.5f, 1, 0}, {146.83f, 0.5f, 1, 0}, {196.0f, 0.5f, 0, 0}, {246.94f, 0.5f, 1, 0},
            {329.63f, 0.5f, 0, 0}, {440.0f, 0.5f, 1, 0}, {587.33f, 0.5f, 0, 0}, {659.26f, 0.5f, 1, 0}, {880.0f, 0.5f, 0, 0},
            {1046.5f, 0.5f, 1, 0}, {98.0f, 2.0f, 0, 0}, {130.81f, 2.0f, 1, 0}, {164.81f, 1.5f, 0, 0}, {220.0f, 0.5f, 0, 2.0f},
            {392.0f, 0.5f, 1, 0}, {293.66f, 0.5f, 1, 0}, {196.0f, 0.5f, 1, 0}, {82.41f, 1.5f, 0, 0}, {659.26f, 0.5f, 0, 0}};
        const int nn = (int)(sizeof NT / sizeof NT[0]), note = SR * 6 / 10;
        static float x[20 * (SR * 6 / 10)];
        double ph = 0; uint32_t rs = 7;
        for (int n = 0; n < nn; n++) {
            const int gap = NT[n].legato ? 0 : SR / 5;
            for (int k = 0; k < note; k++) {
                const int at = n * note + k;
                if (k < gap) { x[at] = 0.0f; ph = 0; continue; }
                const double tt = (double)(k - gap) / SR, f = NT[n].f * pow(2.0, NT[n].bend / 12.0 * (tt > 0.2 ? fmin(1.0, (tt - 0.2) / 0.1) : 0.0));
                ph += f / SR; ph -= floor(ph);
                const double v = sin(2 * M_PI * ph) + NT[n].w2 * sin(4 * M_PI * ph) + 0.3 * sin(6 * M_PI * ph) + 0.2 * sin(8 * M_PI * ph) + 0.1 * sin(10 * M_PI * ph);
                rs = rs * 1664525u + 1013904223u;
                x[at] = (float)(0.15 * exp(-tt * 3.0) * v + ((double)(rs >> 9) / 8388608.0 - 0.5) * (tt < 0.004 ? 0.6 : 0.002));
            }
        }
        const uint8_t k[5] = {0, 50, 60, 20, 30};
        px_preset_t p; base(&p, PX_B_SYN, PX_SYN_MONO, k);
        px_engine_init(&e, (float)SR); px_engine_set(&e, &p);
        double tl = 0; int nl = 0, octave = 0; double wrong_picked = 0;
        for (int n = 0; n < nn; n++) {
            const int gap = NT[n].legato ? 0 : SR / 5, t0 = n * note + gap;
            int lock = -1, wrong = 0;
            for (int s = n * note; s < (n + 1) * note; s += 64) {
                float o[64];
                px_engine_process(&e, x + s, o, 64);
                if (s < t0) continue;
                const double tt = (double)(s - t0) / SR;
                const double want = NT[n].f * pow(2.0, NT[n].bend / 12.0 * (tt > 0.2 ? fmin(1.0, (tt - 0.2) / 0.1) : 0.0));
                const double c = cents(e.syn_f0 > 0 ? e.syn_f0 : 1e-3, want);
                const int open = e.syn_kg > 0.1f;
                if (open && fabs(c) > 50) wrong += 64;
                if (fabs(c) < 15 && open) { if (lock < 0) lock = s - t0; } else if (tt < 0.15) lock = -1;
                if (tt > 0.35 && tt < 0.55 && fabs(c) > 1100 && fabs(c) < 1300) octave++;
            }
            if (lock >= 0) { tl += lock * 1000.0 / SR; nl++; }
            if (!NT[n].legato && wrong * 1000.0 / SR > wrong_picked) wrong_picked = wrong * 1000.0 / SR;
        }
        snprintf(what, sizeof what, "MONO SYNTH: %d of 20 notes in tune, after %.1f ms on average (it was 84, lost above 600 Hz); a picked note wrong %.1f ms at most; %d octave errors",
                 nl, nl ? tl / nl : 999.0, wrong_picked, octave);
        ok(nl == nn && tl / nl < 35.0 && wrong_picked < 15.0 && octave == 0, what);
    }
    /* 4. SITAR: brighter, still harmonic, the tarab in KEY */
    {
        const uint8_t k[5] = {0, 70, 80, 70, 0};       /* KEY C, BUZZ 70, SYMPATH 80, DECAY 70 */
        saw_note(in, 3 * SR, 196.0, 0.15, 2.5, 6);      /* a G: in C's pentatonic (G4 is a tarab string) */
        for (int s = 3 * SR / 2; s < 3 * SR; s++) in[s] = 0.0f;   /* the note stops at 1.5 s */
        px_preset_t p; base(&p, PX_B_SYN, PX_SYN_SITAR, k);
        run(&p, 3 * SR);
        /* the share of the sound above 2 kHz: a one-pole split at 2 kHz, the note's first second */
        double hi_o = 0, all_o = 0, hi_i = 0, all_i = 0, lo = 0, li = 0;
        const double a2 = 1.0 - exp(-2 * M_PI * 2000.0 / SR);
        for (int s = 0; s < SR; s++) {
            lo += a2 * (out[s] - lo); li += a2 * (in[s] - li);
            hi_o += (out[s] - lo) * (out[s] - lo); all_o += out[s] * out[s];
            hi_i += (in[s] - li) * (in[s] - li); all_i += in[s] * in[s];
        }
        const double sh_o = hi_o / all_o, sh_i = hi_i / all_i;
        const double f = peak_f(out, SR / 10, SR, 196.0 * 0.97, 196.0 * 1.03);
        snprintf(what, sizeof what, "SITAR: the jawari brightens the note (above 2 kHz: %.1f %% of its sound, the guitar's %.1f %%), its pitch unmoved (%+.1f cents)",
                 sh_o * 100, sh_i * 100, cents(f, 196.0));
        ok(sh_o > 2.0 * sh_i && fabs(cents(f, 196.0)) < 5.0, what);
        /* after the note: the tarab's G4 (392 Hz) rings, a note off the scale (F4) does not */
        const double g4 = amp_at(out, 392.0, (int)(1.6 * SR), (int)(2.4 * SR)), f4 = amp_at(out, 349.23, (int)(1.6 * SR), (int)(2.4 * SR));
        snprintf(what, sizeof what, "SITAR: after the note the tarab rings in KEY (G4 %.1f dB over F4, which is not on its scale)", 20 * log10((g4 + 1e-9) / (f4 + 1e-9)));
        ok(g4 > 4.0 * f4 && g4 > 1e-4, what);
    }
    /* 5. STRINGS: the bow swells in and holds; the cellos */
    {
        const uint8_t k[5] = {40, 50, 0, 50, 0};       /* ATTACK about 250 ms, RELEASE, OCTAVE 0, TONE, DRY 0 */
        saw_note(in, 2 * SR, 196.0, 0.15, 1.0, 8);      /* the guitar dies: -9 dB from 0.3 s to 1.3 s */
        px_preset_t p; base(&p, PX_B_SYN, PX_SYN_STRINGS, k);
        run(&p, 2 * SR);
        const double r0 = rms(out, 0, SR / 200), r3 = rms(out, (int)(0.28 * SR), (int)(0.32 * SR)), r13 = rms(out, (int)(1.28 * SR), (int)(1.32 * SR));
        snprintf(what, sizeof what, "STRINGS: the bow swells in (the pick's first 5 ms %.1f dB under 300 ms on) and holds (1.3 s: %+.1f dB against 300 ms, the guitar -9)",
                 20 * log10(r3 / (r0 + 1e-9)), 20 * log10(r13 / r3));
        ok(r3 > 4.0 * r0 && fabs(20 * log10(r13 / r3)) < 6.0, what);
        const uint8_t kc[5] = {40, 50, 50, 50, 0};      /* OCTAVE 50: the cellos */
        saw_note(in, 4 * SR, 196.0, 0.15, 0.0, 8);
        base(&p, PX_B_SYN, PX_SYN_STRINGS, kc);
        run(&p, 4 * SR);
        const double fc = peak_f(out, 2 * SR, 4 * SR - 64, 98.0 * 0.97, 98.0 * 1.03), ac = amp_at(out, 98.0, 2 * SR, 4 * SR - 64), an = amp_at(out, 196.0, 2 * SR, 4 * SR - 64);
        snprintf(what, sizeof what, "STRINGS: OCTAVE 50 adds the cellos an octave down, in tune (%+.1f cents, %.1f dB against the note)", cents(fc, 98.0), 20 * log10(ac / an));
        ok(fabs(cents(fc, 98.0)) < 8.0 && ac > 0.2 * an, what);
    }
    /* 6. TAPE FLUTE */
    {
        const uint8_t k[5] = {30, 50, 50, 50, 0};
        px_preset_t p; base(&p, PX_B_SYN, PX_SYN_FLUTE, k);
        for (int s = 0; s < NS; s++) in[s] = 0.0f;
        run(&p, SR);
        const double quiet = rms(out, SR / 2, SR);
        saw_note(in, 3 * SR, 329.63, 0.15, 0.8, 6);
        run(&p, 3 * SR);
        const double f = peak_f(out, SR / 2, 2 * SR, 329.63 * 0.97, 329.63 * 1.03);
        const double r1 = rms(out, (int)(0.002 * SR), (int)(0.012 * SR)), r2 = rms(out, (int)(0.3 * SR), (int)(0.32 * SR));
        snprintf(what, sizeof what, "TAPE FLUTE: plays the note (%+.1f cents, the tape's wow on it), silent to silence (%.1f dB), swells in (%.1f dB from 10 ms to 300 ms)",
                 cents(f, 329.63), 20 * log10(quiet + 1e-12), 20 * log10(r2 / (r1 + 1e-9)));
        ok(fabs(cents(f, 329.63)) < 20.0 && quiet < 1e-5 && r2 > 4.0 * r1, what);
    }
    /* 7. TAPE FLUTE on the semitone */
    {
        const uint8_t k[5] = {10, 50, 0, 50, 0};        /* (WOW 0: the semitone exactly) */
        px_preset_t p; base(&p, PX_B_SYN, PX_SYN_FLUTE, k);
        px_engine_init(&e, (float)SR); px_engine_set(&e, &p);
        /* A3 picked 40 cents sharp, settling 25 sharp in 80 ms; then B3 legato; 0.3 s of silence; E3 30 cents flat with a
         * 5 Hz vibrato of 20 cents, 1.5 s; then F3, as flat, legato */
        const double A3 = 220.0, B3 = 440.0 * pow(2.0, -10.0 / 12.0), E3 = 440.0 * pow(2.0, -17.0 / 12.0), F3 = 440.0 * pow(2.0, -16.0 / 12.0);
        double ph = 0; int bad = 0, sounding = 0, changes = 0, moved = -1, moved2 = -1; float last = 0;
        for (int s = 0; s < 5 * SR; s += 64) {
            for (int j = 0; j < 64; j++) {
                const double t = (double)(s + j) / SR;
                double f = 0, a = 0;
                if (t < 1.2) { f = A3 * pow(2.0, (25.0 + 15.0 * exp(-t / 0.08)) / 1200.0); a = 0.15 * exp(-t * 1.5); }
                else if (t < 2.0) { f = B3 * pow(2.0, 25.0 / 1200.0); a = 0.15 * exp(-(t - 1.2) * 1.5); }
                else if (t < 2.3) { f = 0; a = 0; }
                else if (t < 3.8) { f = E3 * pow(2.0, (-30.0 + 20.0 * sin(2 * M_PI * 5.0 * t)) / 1200.0); a = 0.15 * exp(-(t - 2.3) * 1.0); }
                else { f = F3 * pow(2.0, -30.0 / 1200.0); a = 0.1 * exp(-(t - 3.8) * 1.0); }
                if (a > 0) { ph += f / SR; ph -= floor(ph); }
                in[j] = (float)(a * (sin(2 * M_PI * ph) + 0.5 * sin(4 * M_PI * ph) + 0.3 * sin(6 * M_PI * ph)));
            }
            px_engine_process(&e, in, out, 64);
            const double t = (double)s / SR;
            if (e.syn_kg < 0.1f) continue;
            sounding++;
            if (t < 1.2 && fabs(cents(e.syn_f0, A3)) > 0.01) bad++;
            if (t >= 1.2 && t < 2.0 && moved < 0 && fabs(cents(e.syn_f0, B3)) < 0.01) moved = (int)((t - 1.2) * 1000.0);
            if (t >= 2.4 && t < 3.8 && fabs(cents(e.syn_f0, E3)) > 0.01) bad++;
            if (t >= 2.4 && t < 3.8 && last > 0 && e.syn_f0 != last) changes++;
            if (t >= 2.4 && t < 3.8) last = e.syn_f0;
            if (t >= 3.8 && moved2 < 0 && fabs(cents(e.syn_f0, F3)) < 0.01) moved2 = (int)((t - 3.8) * 1000.0);
        }
        snprintf(what, sizeof what, "TAPE FLUTE: a sharp note on its semitone from the first sound, B3 taken in %d ms; 30 cents flat: vibrato %d changes, F3 in %d ms (%d blocks off)",
                 moved, changes, moved2, bad);
        ok(bad == 0 && sounding > 500 && moved >= 0 && moved < 40 && changes == 0 && moved2 >= 0 && moved2 < 40, what);
    }
    /* 8. MONO SYNTH through hum */
    {
        const uint8_t k[5] = {0, 50, 60, 20, 30};
        px_preset_t p; base(&p, PX_B_SYN, PX_SYN_MONO, k);
        px_engine_init(&e, (float)SR); px_engine_set(&e, &p);
        static const float F[6] = {110.0f, 196.0f, 329.63f, 440.0f, 659.26f, 146.83f};
        double ph = 0; int intune = 0, lock_n = 0, late = -1;
        for (int s = 0; s < 6 * SR; s += 64) {
            const int n = s / (SR * 3 / 4);            /* six notes of 0.75 s, then the hum alone from 4.5 s */
            for (int j = 0; j < 64; j++) {
                const double t = (double)(s + j) / SR, tn = t - n * 0.75;
                const double hum = 0.004 * (sin(2 * M_PI * 50 * t) + 0.5 * sin(2 * M_PI * 150 * t) + 0.3 * sin(2 * M_PI * 250 * t));
                double v = 0;
                if (n < 6 && tn > 0.05) { ph += F[n] / SR; ph -= floor(ph); v = 0.15 * exp(-(tn - 0.05) * 2.0) * (sin(2 * M_PI * ph) + 0.5 * sin(4 * M_PI * ph) + 0.3 * sin(6 * M_PI * ph)); }
                in[j] = (float)(v + hum);
            }
            px_engine_process(&e, in, out, 64);
            const double t = (double)s / SR, tn = t - n * 0.75;
            if (n < 6 && tn > 0.15 && tn < 0.7) { lock_n++; if (e.syn_kg > 0.5f && fabs(cents(e.syn_f0, F[n])) < 15.0) intune++; }
            if (n >= 6 && late < 0 && e.syn_kg < 0.01f) late = (int)((t - 4.5) * 1000.0);
        }
        snprintf(what, sizeof what, "MONO SYNTH through hum 30 dB down: in tune %d of %d blocks of six notes; the hum alone: silent after %d ms (it played on)",
                 intune, lock_n, late);
        ok(intune > lock_n * 95 / 100 && late >= 0 && late < 200, what);
    }
    /* 9. 12-STRING steady */
    {
        static const double F[4] = {146.83, 196.0, 329.63, 493.88};
        static float oc[NS];
        double pp0 = 0, sp50 = 0, sp0 = 0, c0 = 0, c100 = 0;
        for (int det = 0; det <= 100; det += 50) for (int i = 0; i < 4; i++) {
            const uint8_t k[5] = {70, (uint8_t)det, 50, 0, 0};
            px_preset_t p; base(&p, PX_B_SYN, PX_SYN_12STR, k);
            for (int j = 0; j < 4 * SR; j++) {
                double v = 0;
                for (int h = 1; h <= 10; h++) if (F[i] * h < 8000) v += sin(2 * M_PI * F[i] * h * j / SR + h * 0.7) / h;
                in[j] = (float)(0.15 * v * exp(-0.25 * j / SR));
            }
            run(&p, 4 * SR);
            double pp, sp;
            wobble(out, in, SR, (int)(3.5 * SR), &pp, &sp);
            if (det == 0) { if (pp > pp0) pp0 = pp; sp0 += sp / 4; }
            if (det == 50) sp50 += sp / 4;
            if (det != 50 && i == 2) {                 /* (E4: the octave alone, near 2 f) */
                for (int j = 0; j < 4 * SR; j++) oc[j] = out[j] - in[j];
                const double c = cents(peak_f(oc, SR, 3 * SR, 2 * F[i] * 0.99, 2 * F[i] * 1.01), 2 * F[i]);
                if (det == 0) c0 = c; else c100 = c;
            }
        }
        char t[12];
        px_param_text(PX_B_SYN, PX_SYN_12STR, 1, 50, t);
        snprintf(what, sizeof what, "12-STRING: DETUNE 0 swings %.2f dB at most (%.1f dB/s), DETUNE 50 moves %.1f dB/s; octave %+.1f / %+.1f cents; '%s'",
                 pp0, sp0, sp50, c0, c100, t);
        ok(pp0 < 1.0 && sp50 < 4.0 && fabs(c0) < 2.0 && fabs(c100 - 3.0) < 1.0 && !strcmp(t, "1.5"), what);
    }
    printf(failed ? "syn fixes test FAILED\n" : "syn fixes test passed\n");
    return failed ? 1 : 0;
}
