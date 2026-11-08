/* OpenPocket — guitar sound engine v3 (see px_dsp.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Engine rules (learned on the pedal):
 *  - single precision everywhere (AC791N float FPU), never double;
 *  - coefficients are computed in px_engine_set (outside real time); processing is only + and x;
 *  - no modulo in hot loops; anti-denormal offset at the input;
 *  - ALWAYS verify with firmware/px_math.c (the pedal's libm), never the PC libm.
 *
 * Voicing sources (circuit analyses, datasheets, manuals): ElectroSmash (TS808, RAT, DS-1, Big Muff, Fuzz Face,
 * Klon, Distortion+, Bluesbreaker), Electric Druid, R.G. Keen / GEOFEX, Rob Robinette & Duncan's TSC (amp tone
 * stacks), Celestion / Jensen / EV / JBL speaker curves, Roland/Boss/Ampeg/Electro-Harmonix manuals.
 */
#include "px_dsp.h"
#include "px_nam.c"                                     /* v37: NAM A2-Lite (one translation unit: every build gets it) */
/* v40: on the pedal the audio path runs from RAM, as the stock firmware's does: firmware/build.sh compiles this file with
 * -ffunction-sections and tools/ramcode.py moves the per-block functions (px_engine_part, the delay and reverb loops,
 * the NAM network, the oversampling, the IR...) and the hand-written routines into the RAM code window, which sits
 * within call range of the flash (firmware/app.ld). Nothing to mark here: the code is the compiler's, unchanged. */
#define PX_HOT
#define PX_INL
#define PX_AINL __attribute__((always_inline))
#include <math.h>
#include <stddef.h>
#include <string.h>

#define PI_F 3.14159265f

/* ================================================================ tools ===== */
static inline float px_tanh(float x)                   /* rational, err < 1e-3 on [-3,3] */
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float px_abs(float x) { return x < 0.0f ? -x : x; }
static inline float triode(float u)                    /* grid current (u > 0) limits earlier and harder than cutoff */
{                                                      /* = u >= 0 ? 0.7 px_tanh(u / 0.7) : px_tanh(u), folded */
    float u2 = u * u;
    if (u >= 0.0f) return u > 2.1f ? 0.7f : u * (13.23f + u2) / (13.23f + 9.0f * u2);
    return u < -3.0f ? -1.0f : u * (27.0f + u2) / (27.0f + 9.0f * u2);
}
static inline float lfo_sin(float ph)                  /* parabolic sine, ph in [0,1) -> -1..1 */
{
    if (ph < 0.5f) return 16.0f * ph * (0.5f - ph);
    ph -= 0.5f;
    return -16.0f * ph * (0.5f - ph);
}
static inline float sin_hq(float ph)                   /* refined parabola (0.1 % error): audio-rate carrier */
{
    float y = lfo_sin(ph);
    return 0.225f * (y * px_abs(y) - y) + y;
}
static inline float lfo_tri(float ph) { return 4.0f * px_abs(ph - 0.5f) - 1.0f; }
static inline float f01(uint8_t v) { return (float)(v > 100 ? 100 : v) * 0.01f; }
static inline float db2lin(float db) { return powf(10.0f, db / 20.0f); }
static inline float wrap1(float ph) { if (ph >= 1.0f) ph -= 1.0f; else if (ph < 0.0f) ph += 1.0f; return ph; }
static float onepole_k(float sr, float sec) { return 1.0f - expf(-1.0f / (sec * sr)); }

static void bq_reset(px_bq *b) { b->z1 = b->z2 = 0.0f; }
static inline float bq_run(px_bq *b, float x)
{
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}
/* the same over a buffer, in place: the coefficients and state in registers through the loop (same operations, same
 * result as bq_run sample by sample) */
static void bq_block(px_bq *q, float *b, int n)
{
    const float b0 = q->b0, b1 = q->b1, b2 = q->b2, a1 = q->a1, a2 = q->a2;
    float z1 = q->z1, z2 = q->z2;
    for (int k = 0; k < n; k++) {
        const float x = b[k], y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        b[k] = y;
    }
    q->z1 = z1;
    q->z2 = z2;
}
/* two filters in series over a buffer, interleaved: the first on sample k while the second does sample k-1, so that
 * their two feedback loops overlap in the FPU instead of waiting for each other (same operations per filter, same
 * result as bq_block twice) */
static void bq_block2(px_bq *p, px_bq *q, float *b, int n)
{
    if (n <= 0) return;
    const float pb0 = p->b0, pb1 = p->b1, pb2 = p->b2, pa1 = p->a1, pa2 = p->a2;
    const float qb0 = q->b0, qb1 = q->b1, qb2 = q->b2, qa1 = q->a1, qa2 = q->a2;
    float pz1 = p->z1, pz2 = p->z2, qz1 = q->z1, qz2 = q->z2;
    float x = b[0], pend = pb0 * x + pz1;
    pz1 = pb1 * x - pa1 * pend + pz2;
    pz2 = pb2 * x - pa2 * pend;
    for (int k = 1; k < n; k++) {
        const float xp = b[k], yp = pb0 * xp + pz1;
        pz1 = pb1 * xp - pa1 * yp + pz2;
        pz2 = pb2 * xp - pa2 * yp;
        const float u = pend, yq = qb0 * u + qz1;
        qz1 = qb1 * u - qa1 * yq + qz2;
        qz2 = qb2 * u - qa2 * yq;
        b[k - 1] = yq;
        pend = yp;
    }
    {
        const float u = pend, yq = qb0 * u + qz1;
        qz1 = qb1 * u - qa1 * yq + qz2;
        qz2 = qb2 * u - qa2 * yq;
        b[n - 1] = yq;
    }
    p->z1 = pz1; p->z2 = pz2; q->z1 = qz1; q->z2 = qz2;
}
/* v37: the same filter in integer arithmetic (the pedal's FPU is slow; its integer multiply-adds are not): the signal in
 * Q26 (+-32), the coefficients in Q29 (+-4), direct form I with a 64-bit sum. The float filter's coefficients
 * are converted each call (they may change between calls); 0 = a coefficient out of range: not run (use bq_block). */
typedef struct { int32_t x1, x2, y1, y2; } px_fq;      /* (laid out as int32_t[4]) */
int px_fx_int = 0;
uint32_t (*px_clock)(void);                            /* v38: a clock for the per-block times (px_engine.pf), or none */
int px_fx_hand;                                         /* the integer filter loop by hand (px_fx_check) */
#define FQ_CB 29                                       /* coefficient fraction bits */
#define FQ_SB 26                                       /* signal fraction bits (+-32) */
static int32_t fq_c(float c) { return (int32_t)(c * (float)(1u << FQ_CB) + (c >= 0.0f ? 0.5f : -0.5f)); }
static int fq_ok(const px_bq *f)
{
    const float m = (float)(1u << (31 - FQ_CB)) - 0.01f;
    return f->b0 < m && f->b0 > -m && f->b1 < m && f->b1 > -m && f->b2 < m && f->b2 > -m &&
           f->a1 < m && f->a1 > -m && f->a2 < m && f->a2 > -m;
}
/* the loop, on (b0, b1, b2, a1, a2) in Q29 and the state (x1, x2, y1, y2): the sum's bits 29..60 kept (rounded down,
 * no saturation: the signal's range leaves room). By hand on the pedal (checked against this one at start-up) */
static void fq_loop_c(int32_t *b, int n, const int32_t *c, int32_t *st)
{
    int32_t x1 = st[0], x2 = st[1], y1 = st[2], y2 = st[3];
    for (int k = 0; k < n; k++) {
        const int32_t x = b[k];
        int64_t acc = (int64_t)c[0] * x;
        acc += (int64_t)c[1] * x1;
        acc += (int64_t)c[2] * x2;
        acc -= (int64_t)c[3] * y1;
        acc -= (int64_t)c[4] * y2;
        const int32_t y = (int32_t)(uint32_t)(uint64_t)(acc >> FQ_CB);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        b[k] = y;
    }
    st[0] = x1; st[1] = x2; st[2] = y1; st[3] = y2;
}
#if defined(__PI32V2__)
void px_fq_asm(int32_t *b, int n, const int32_t *c, int32_t *st);
__asm__("\t.text\n"
        "\t.global px_fq_asm\n"
        "px_fq_asm:\n"
        "\t[--sp] = {rets, r15-r4}\n"
        "\tsp += -4\n"
        "\t[sp+0] = r3\n"
        "\tr4 = [r2+0]\n\tr5 = [r2+4]\n\tr6 = [r2+8]\n\tr7 = [r2+12]\n\tr12 = [r2+16]\n"
        "\tr13 = [r3+0]\n\tr14 = [r3+4]\n\tr15 = [r3+8]\n\tr11 = [r3+12]\n"
        "\tr1 = r1 >> 1\n"
        ".Lpx_fq:\n"
        "\tr10 = [r0+0]\n"
        "\tr9_r8 = r10 * r4 (s)\n"
        "\tr9_r8 += r13 * r5 (s)\n"
        "\tr9_r8 += r14 * r6 (s)\n"
        "\tr9_r8 -= r15 * r7 (s)\n"
        "\tr9_r8 -= r11 * r12 (s)\n"
        "\tr9_r8 >>>= 29\n"
        "\t[r0++=4] = r8\n"
        "\tr14 = [r0+0]\n"
        "\tr3_r2 = r14 * r4 (s)\n"
        "\tr3_r2 += r10 * r5 (s)\n"
        "\tr3_r2 += r13 * r6 (s)\n"
        "\tr3_r2 -= r8 * r7 (s)\n"
        "\tr3_r2 -= r15 * r12 (s)\n"
        "\tr3_r2 >>>= 29\n"
        "\t[r0++=4] = r2\n"
        "\tr13 = r14\n\tr14 = r10\n\tr15 = r2\n\tr11 = r8\n"
        "\tr1 += -1\n"
        "\tif (r1 != 0) goto .Lpx_fq\n"
        "\tr3 = [sp+0]\n"
        "\t[r3+0] = r13\n\t[r3+4] = r14\n\t[r3+8] = r15\n\t[r3+12] = r11\n"
        "\tsp += 4\n"
        "\t{pc, r15-r4} = [sp++]\n");
#endif
static void (*fq_loop)(int32_t *, int, const int32_t *, int32_t *) = fq_loop_c;
/* start-up: the hand loop against the C one (1 = in use) */
int px_fx_check(void)
{
#if defined(__PI32V2__)
    static int32_t a[66], b[66];
    int32_t sa[4] = {1000, -2000, 3000, -4000}, sb[4] = {1000, -2000, 3000, -4000};
    const int32_t c[5] = {fq_c(0.25f), fq_c(-0.4f), fq_c(0.2f), fq_c(-1.6f), fq_c(0.7f)};
    uint32_t r = 777u;
    for (int i = 0; i < 66; i++) { r = r * 1103515245u + 12345u; a[i] = b[i] = (int32_t)r >> 5; }
    fq_loop_c(a, 66, c, sa);
    px_fq_asm(b, 66, c, sb);
    for (int i = 0; i < 66; i++) if (a[i] != b[i]) return 0;
    for (int i = 0; i < 4; i++) if (sa[i] != sb[i]) return 0;
    fq_loop = px_fq_asm;
    return 1;
#else
    return 0;
#endif
}
static void fq_block(const px_bq *f, px_fq *s, int32_t *b, int n)
{
    if (n <= 0) return;
    const int32_t c[5] = {fq_c(f->b0), fq_c(f->b1), fq_c(f->b2), fq_c(f->a1), fq_c(f->a2)};
    if (n & 1) fq_loop_c(b, n, c, &s->x1);              /* (the hand loop does 2 samples a turn) */
    else fq_loop(b, n, c, &s->x1);
}
static int32_t px_fqbuf[PX_OVERSAMPLE * PX_MAX_BLOCK];
static void fq_to(int32_t *q, const float *x, int n)
{
    for (int k = 0; k < n; k++) {
        float v = x[k] * (float)(1u << FQ_SB);
        q[k] = v >= 2147483520.0f ? 2147483520 : v <= -2147483520.0f ? -2147483520 : (int32_t)v;
    }
}
static void fq_from(float *x, const int32_t *q, int n)
{
    for (int k = 0; k < n; k++) x[k] = (float)q[k] * (1.0f / (float)(1u << FQ_SB));
}
/* a chain of filters over a float buffer: in integer arithmetic if all can be, else as floats */
static void fq_chain(px_bq *f, px_fq *s, int nf, float *b, int n)
{
    int ok = 1;
    for (int c = 0; c < nf; c++) ok &= fq_ok(&f[c]);
    if (!ok) { for (int c = 0; c < nf; c++) bq_block(&f[c], b, n); return; }
    fq_to(px_fqbuf, b, n);
    for (int c = 0; c < nf; c++) fq_block(&f[c], &s[c], px_fqbuf, n);
    fq_from(b, px_fqbuf, n);
}
/* a chain of filters over a buffer, two at a time */
static void bq_chain(px_bq *f, int nf, float *b, int n)
{
    int c = 0;
    for (; c + 1 < nf; c += 2) bq_block2(&f[c], &f[c + 1], b, n);
    if (c < nf) bq_block(&f[c], b, n);
}
static void bq_set(px_bq *b, float b0, float b1, float b2, float a0, float a1, float a2)
{
    float z1 = b->z1, z2 = b->z2;                      /* keep state: no click on change */
    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0; b->a1 = a1 / a0; b->a2 = a2 / a0;
    b->z1 = z1; b->z2 = z2;
}
static void bq_ident(px_bq *b) { bq_set(b, 1, 0, 0, 1, 0, 0); }
static float clampf_hz(float fs, float f) { if (f > fs * 0.45f) f = fs * 0.45f; if (f < 5.0f) f = 5.0f; return f; }
static void bq_lowpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, (1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
}
static void bq_highpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, (1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
}
static void bq_bandpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, al, 0, -al, 1 + al, -2 * c, 1 - al);
}
static void bq_peak(px_bq *b, float fs, float f, float q, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}
static void bq_lowshelf(px_bq *b, float fs, float f, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w);
    float al = s / 2.0f * 1.4142135f, ap = A + 1, am = A - 1, tsa = 2.0f * sqrtf(A) * al;
    bq_set(b, A * (ap - am * c + tsa), 2 * A * (am - ap * c), A * (ap - am * c - tsa),
           ap + am * c + tsa, -2 * (am + ap * c), ap + am * c - tsa);
}
static void bq_highshelf(px_bq *b, float fs, float f, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w);
    float al = s / 2.0f * 1.4142135f, ap = A + 1, am = A - 1, tsa = 2.0f * sqrtf(A) * al;
    bq_set(b, A * (ap + am * c + tsa), -2 * A * (am + ap * c), A * (ap + am * c - tsa),
           ap - am * c + tsa, 2 * (am - ap * c), ap - am * c - tsa);
}
static float tan_pi(float fs, float f) { float w = PI_F * clampf_hz(fs, f) / fs; return sinf(w) / cosf(w); }
static inline float op_run(px_op *o, float x)
{
    float y = o->b0 * x + o->z;
    o->z = o->b1 * x - o->a1 * y;
    return y;
}
static void op_hp(px_op *o, float fs, float f)
{
    float K = tan_pi(fs, f), n = 1.0f / (1.0f + K);
    o->b0 = n; o->b1 = -n; o->a1 = (K - 1.0f) * n;
}
static void op_lp(px_op *o, float fs, float f)
{
    float K = tan_pi(fs, f), n = 1.0f / (1.0f + K);
    o->b0 = K * n; o->b1 = K * n; o->a1 = (K - 1.0f) * n;
}
static void bq_hp1(px_bq *b, float fs, float f)          /* first order (RC coupling, like the real circuits) */
{
    float K = tan_pi(fs, f);
    bq_set(b, 1.0f, -1.0f, 0.0f, 1.0f + K, K - 1.0f, 0.0f);
}

/* ============================================================ catalog ===== */
static const char *const BLK_LABEL[PX_NBLK] = {"GT", "CMP", "PIT", "SFX", "DRV", "AMP", "CAB", "EQ", "MOD", "DLY", "REV",
                                               "SYN"};
static const char *const BLK_NAME[PX_NBLK] = {"NOISE GATE", "COMPRESSOR", "PITCH", "SPECIAL FX", "DRIVE",
                                              "AMP", "CABINET", "EQUALIZER", "MODULATION", "DELAY", "REVERB",
                                              "INSTRUMENT"};
/* default processing order (v29: each preset carries its own) */
static const uint8_t ORDER_DEF[PX_NBLK] = {PX_B_GATE, PX_B_COMP, PX_B_PITCH, PX_B_SFX, PX_B_SYN, PX_B_DRIVE, PX_B_AMP,
                                           PX_B_CAB, PX_B_EQ, PX_B_MOD, PX_B_DELAY, PX_B_REVERB};
void px_order_default(uint8_t *o) { for (int i = 0; i < PX_NBLK; i++) o[i] = ORDER_DEF[i]; }
int px_order_valid(const uint8_t *o)
{
    unsigned seen = 0;
    for (int i = 0; i < PX_NBLK; i++) { if (o[i] >= PX_NBLK || (seen >> o[i]) & 1u) return 0; seen |= 1u << o[i]; }
    return 1;
}
typedef struct { const char *name; uint8_t np; const char *const *pn; } tdef;

static const char *const PN_GATE[] = {"THRESH", "RELEASE", "PRESET LVL"};   /* (3rd: the preset's level, v36) */
static const tdef T_GATE[] = {{"NOISE GATE", 3, PN_GATE}};

static const char *const PN_CMP[] = {"SUSTAIN", "ATTACK", "LEVEL", "MIX"};
static const char *const PN_OPTO[] = {"COMPRESS", "LEVEL", "MIX"};
static const char *const PN_SWELL[] = {"RISE", "SENS", "LEVEL"};
static const tdef T_COMP[PX_CMP_COUNT] = {
    {"SQUASH", 3, PN_CMP}, {"STUDIO", 4, PN_CMP}, {"OPTO LA-2", 3, PN_OPTO}, {"SLOW SWELL", 3, PN_SWELL}};

static const char *const PN_OCT[] = {"DRY", "SUB -1", "SUB -2", "UP +1", "TONE"};
static const char *const PN_POLY[] = {"DRY", "OCT -1", "OCT +1"};
static const char *const PN_WHAM[] = {"SHIFT", "MIX", "GLIDE"};
static const char *const PN_HARM[] = {"INTERVAL", "MIX"};
static const char *const PN_DET[] = {"CENTS", "MIX"};
static const char *const PN_LOWOCT[] = {"RANGE", "OCT -1", "DRY", "GROWL", "COMP"};
static const tdef T_PITCH[PX_PIT_COUNT] = {
    {"OCTAVER", 5, PN_OCT}, {"POLY OCTAVE", 3, PN_POLY}, {"WHAMMY", 3, PN_WHAM}, {"HARMONIZER", 2, PN_HARM},
    {"DETUNE", 2, PN_DET}, {"LOW OCTAVE", 5, PN_LOWOCT}};

static const char *const PN_AWAH[] = {"SENS", "RESO", "RANGE", "DECAY"};
static const char *const PN_LWAH[] = {"RATE", "DEPTH", "RESO", "CENTER"};
static const char *const PN_RING[] = {"FREQ", "MIX", "WOBBLE"};
static const char *const PN_CRUSH[] = {"BITS", "SR DIV", "MIX"};
static const char *const PN_SLICE[] = {"RATE", "DEPTH", "PATTERN"};
static const char *const PN_LOFI[] = {"TONE", "DRIVE", "MIX"};
static const char *const PN_SYN[] = {"SWEEP", "RESO", "SUB", "MIX"};
static const tdef T_SFX[PX_SFX_COUNT] = {
    {"ENVELOPE WAH", 4, PN_AWAH}, {"AUTO WAH", 4, PN_LWAH}, {"RING MOD", 3, PN_RING}, {"BITCRUSHER", 3, PN_CRUSH},
    {"SLICER", 3, PN_SLICE}, {"LO-FI RADIO", 3, PN_LOFI}, {"SQUARE SYNTH", 4, PN_SYN}};

/* synth / instrument (v29): keyboard machines (polyphonic: octave shifters + key envelope), synths that follow
 * the note (mono: tracker + oscillator + ladder filter) or the waveform (PWM, bass: comparator / dividers), and
 * other guitars (12-string, acoustic, sitar, baritone) */
static const char *const PN_ORG[] = {"16'", "8'", "4'", "PERC", "DRY"};
static const char *const PN_CHU[] = {"16'", "8'", "4'", "2'", "DRY"};
static const char *const PN_STR[] = {"ATTACK", "RELEASE", "OCTAVE", "TONE", "DRY"};
static const char *const PN_CHO[] = {"ATTACK", "RELEASE", "VOWEL", "OCTAVE", "DRY"};
static const char *const PN_MONO[] = {"WAVE", "OCTAVE", "CUTOFF", "RESO", "ENV"};
static const char *const PN_PWM[] = {"WIDTH", "RATE", "CUTOFF", "RESO", "MIX"};
static const char *const PN_BSY[] = {"OCT -1", "OCT -2", "CUTOFF", "ENV", "DRY"};
static const char *const PN_12S[] = {"OCTAVE", "DETUNE", "TONE"};
static const char *const PN_ACO[] = {"BODY", "BASS", "TREBLE", "ROOM"};
static const char *const PN_SIT[] = {"KEY", "BUZZ", "SYMPATH", "DECAY"};
static const char *const PN_BAR[] = {"TUNE", "TONE", "MIX"};
static const char *const PN_PAD[] = {"ATTACK", "RELEASE", "TONE", "MOTION", "MIX"};
static const tdef T_SYN[PX_SYN_COUNT] = {
    {"ORGAN", 5, PN_ORG}, {"CHURCH ORGAN", 5, PN_CHU}, {"STRINGS", 5, PN_STR}, {"CHOIR", 5, PN_CHO},
    {"MONO SYNTH", 5, PN_MONO}, {"PWM SYNTH", 5, PN_PWM}, {"BASS SYNTH", 5, PN_BSY}, {"12-STRING", 3, PN_12S},
    {"ACOUSTIC", 4, PN_ACO}, {"SITAR", 4, PN_SIT}, {"BARITONE", 3, PN_BAR}, {"PAD", 5, PN_PAD}};

/* drives: each knob has a role; the name shown comes from the role */
enum { R_DRIVE, R_DIST, R_FUZZ, R_SUST, R_BOOST, R_TONE, R_TREBLE, R_LEVEL, R_BASS, R_VOICE, R_GATE, R_BIAS,
       R_LOW, R_HIGH, R_TIGHT, R_BRIGHT, R_BLEND, R_NROLE };
static const char *const ROLE_NAME[R_NROLE] = {"DRIVE", "DIST", "FUZZ", "SUSTAIN", "BOOST", "TONE", "TREBLE", "LEVEL",
                                               "BASS", "VOICE", "GATE", "BIAS", "LOW", "HIGH", "TIGHT", "BRIGHT", "BLEND"};

/* ============================================================== drives ===== */
enum { CL_SOFT, CL_ASYM, CL_HARD, CL_GE, CL_LED, CL_MOS, CL_MUFF, CL_FUZZ, CL_OCTA, CL_BOOST };
enum { TM_LP, TM_MUFF, TM_HM2, TM_MT2 };
typedef struct {
    const char *name;
    uint8_t role[5], np;
    float hp, shelf, lp, mid, mid_db, mid_q10;                       /* pre-clip (base rate) */
    float g0, g1;                                                    /* gain range, dB */
    uint8_t clip, tm; float t0, t1;                                  /* clipper, post tone (oversampled) */
    float p2, p2db;                                                  /* fixed post filter (0 dB = low-pass) */
    float blend, bias, gate;                                         /* /100 */
} drv_v;          /* (floats: tools/fit_drive.py fits them to captures) */
#define L3(a, b, c) {a, b, c, 0, 0}, 3
#define L4(a, b, c, d) {a, b, c, d, 0}, 4
#ifdef PX_FIT
drv_v px_fit_drv; int px_fit_drv_on; float px_fit_drv_trim = 1.0f;
#endif
/* v34: fitted to NAM captures of the pedals (tools/fit_drive.py); knobs at noon = the captured setting */
static const drv_v DV[PX_DRV_COUNT] = {
    {"TS9", L3(R_DRIVE, R_TONE, R_LEVEL), 69.84f, 100.0f, 0.0f, 0.0f, 0.0f, 7.0f, 29.46f, 49.46f, CL_SOFT, TM_LP, 2102.0f, 1.132e+04f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},  /* Tim R TS9 */
    {"KLON", L3(R_DRIVE, R_TREBLE, R_LEVEL), 11.65f, 239.6f, 0.0f, 449.9f, 2.0f, 11.21f, 18.99f, 52.99f, CL_GE, TM_LP, 842.3f, 4717.0f, 0.0f, 0.0f, 2.468f, 0.0f, 0.0f},  /* Keith B klone_g6_t6_o5 */
    {"M77 OD", L3(R_DRIVE, R_TONE, R_LEVEL), 10.0f, 1111.0f, 0.0f, 0.0f, 0.0f, 7.0f, 39.88f, 67.88f, CL_ASYM, TM_LP, 1249.0f, 4995.0f, 0.0f, 0.0f, 0.09043f, 0.06363f, 0.0f},  /* Tudor N MXR M77 Custom Badass Modified OD Tone@6.5_Output@10 */
    {"SWEDE HM-2", L4(R_DIST, R_LOW, R_HIGH, R_LEVEL), 49.27f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 36.83f, 61.83f, CL_HARD, TM_HM2, 100.1f, 1265.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},  /* Peter N HM-2_SWEDE_Std_ESR-0.0034 */
};

/* =============================================================== amps ====== */
typedef struct {
    const char *name;
    uint8_t n, hard;
    float g[4], bias[4];
    float lp[4], hp[4];                                   /* inter-stage low-pass / coupling high-pass (Hz) */
    float tight, bright, bright_db;                       /* input high-pass, bright cap (fades with gain) */
    float bass, mid, midq10, treb;                        /* tone stack */
    float rb, rm, rt, scoop;                              /* knob ranges (dB), fixed mid scoop at noon */
    float pres, res, res_db;                              /* presence, resonance (power amp + speaker) */
    float vf, vdb;                                        /* fixed voice peak */
    float sag100, pw10;                                   /* power-supply sag, power-amp drive */
} amp_v;          /* (floats: tools/fit_amp.py fits them to captures; see docs/FITTED-MODELS.md) */
#ifdef PX_FIT
amp_v px_fit_amp; int px_fit_on; float px_fit_trim = 1.0f;
#endif
/* v34: each row fitted to a NAM capture of the real amp (tools/fit_amp.py, docs/FITTED-MODELS.md); knobs at noon
 * = the captured setting. Captures: the NAM community collection (GPL v3), credited in the comments. */
static const amp_v AV[PX_AMP_COUNT] = {
  {"TWIN", 1,0, {0.3f,0,0,0}, {-2.302e-07f,0,0,0}, {1.995e+04f,0,0,0}, {220.9f,0,0,0}, 29.88f,3532.0f,9.754f, 154.8f,439.0f,3.031f,1005.0f, 10.0f,8.0f,10.0f,-11.81f, 2825.0f,93.49f,1.404f, 599.8f,-7.461f, 4.05f,35.8f},  /* Tim R Fender TwinVerb Norm Bright */
  {"AC15 TB", 3,0, {9.42f,9.975f,1.816f,0}, {0.06444f,-0.4316f,3.31e-07f,0}, {2544.0f,4256.0f,5172.0f,0}, {67.75f,71.05f,114.9f,0}, 92.86f,1343.0f,19.65f, 77.92f,878.4f,7.153f,703.2f, 9.0f,8.0f,11.0f,-10.72f, 1731.0f,40.08f,0.8217f, 4552.0f,12.52f, 196.4f,6.342f},  /* Phillipe P VOXAC15-TopBoost */
  {"JCM900", 3,0, {2.21f,3.039f,2.231f,0}, {0.04791f,0.3043f,-0.1265f,0}, {2.094e+04f,2.089e+04f,2.085e+04f,0}, {145.2f,170.2f,173.3f,0}, 53.37f,4123.0f,12.42f, 50.09f,567.8f,2.264f,833.8f, 11.0f,10.0f,10.0f,-16.09f, 9891.0f,107.4f,3.579f, 2480.0f,2.732f, 16.73f,50.37f},  /* Tim R JCM90050WDualVerbCHAG6 */
  {"JCM2000", 4,0, {8.543f,6.047f,7.104f,3.937f}, {-0.0004265f,-0.04477f,0.258f,0.3669f}, {1.051e+04f,1.011e+04f,1.077e+04f,1.818e+04f}, {38.3f,48.77f,90.03f,27.44f}, 111.2f,1016.0f,12.16f, 51.54f,719.6f,8.087f,6428.0f, 12.0f,10.0f,10.0f,-2.586f, 2911.0f,105.3f,3.054f, 304.5f,0.09714f, 18.58f,6.411f},  /* Tim R JCM2000 L2 G6 */
  {"MARK IV", 4,0, {7.683f,7.948f,8.82f,6.955f}, {0.01532f,-0.01585f,0.0974f,-0.1513f}, {1.105e+04f,1.025e+04f,9167.0f,7818.0f}, {37.8f,61.58f,79.03f,53.29f}, 121.7f,1317.0f,7.075f, 410.8f,588.3f,4.153f,708.6f, 12.0f,12.0f,10.0f,-14.18f, 1864.0f,107.2f,0.7193f, 300.3f,-0.1191f, 30.97f,15.42f},  /* Roman A LT_MESA_MARKIV_1 */
  {"SHIRLEY", 3,0, {14.57f,12.87f,0.9559f,0}, {8.291e-05f,1.581e-06f,0.4817f,0}, {1.609e+04f,9064.0f,1.022e+04f,0}, {75.04f,90.07f,140.2f,0}, 104.2f,735.8f,19.94f, 212.9f,536.8f,3.517f,3829.0f, 11.0f,10.0f,10.0f,-10.71f, 2764.0f,159.2f,6.964f, 1368.0f,0.01628f, 105.2f,16.02f},  /* Sascha S DirtyShirleyMini_crunch_G6 */
  {"PRO MOD", 4,0, {6.752f,4.731f,19.9f,0.963f}, {-0.0004333f,-0.0001043f,0.5f,4.907e-08f}, {1.162e+04f,1.497e+04f,6022.0f,8590.0f}, {25.21f,34.53f,172.3f,8.071f}, 83.83f,581.3f,17.36f, 483.1f,631.1f,15.95f,1633.0f, 12.0f,10.0f,10.0f,-6.407f, 1859.0f,92.49f,12.0f, 6408.0f,5.301f, 39.48f,13.34f},  /* Tim R Splawn Pro Mod 2nd Gear - G12 */
  {"5150", 4,0, {7.122f,7.229f,11.68f,10.45f}, {0.001611f,-0.02376f,0.1732f,-0.0003214f}, {8118.0f,8207.0f,8905.0f,1.582e+04f}, {35.27f,46.39f,56.44f,38.31f}, 95.85f,1282.0f,5.8f, 494.3f,603.4f,3.356f,4957.0f, 12.0f,11.0f,11.0f,-8.117f, 7287.0f,136.9f,6.174f, 7286.0f,-1.026f, 16.01f,9.421f},  /* Helga B 5150 BlockLetter - NoBoost */
};
#include "px_trim.h"                                   /* per-amp / per-drive level matching (calibrated on PC) */

/* ============================================================ cabinets ===== */
typedef struct {
    const char *name;
    float hp, hpq10, bump, bump_db;
    float d1, d1db, d2, d2db;
    float p1, p1db, p2, p2db, lp, lpq;                    /* lpq: the top end's resonance (0.9 = as before v34) */
} cab_v;          /* (floats: tools/fit_cab.py fits them to measured cabinets) */
#ifdef PX_FIT
cab_v px_fit_cab; int px_fit_cab_on;
#endif
static const cab_v CV[PX_CAB_COUNT] = {
    /* name hp q bump dB dip1 dB dip2 dB pres1 dB pres2 dB lp [lpq] */
    {"1x12 US OPEN", 95, 9, 105, 3,   650,-3,    0, 0,  2700, 5, 5000, 3,  6500},
    {"2x12 BLUE", 80, 8,  90, 3,  2000,-3,    0, 0,  1350, 3, 3500, 6,  6000},
    {"4x12 GREEN", 70, 7, 100, 5,  2800,-3,    0, 0,  2000, 4, 3500, 3,  4800},
    {"4x12 T75", 75, 7, 105, 5,   650,-3,    0, 0,  2500, 5, 5000, 5,  6000},
    {"4x12 V30", 165.7f, 14.37f, 101.6f, 11.95f, 1558.0f, -5.565f, 5034.0f, 1.745f, 2868.0f, 9.288f, 5017.0f, 12.0f, 7097.0f, 0.5f},  /* fitted: 8 amp / amp+cab capture pairs (Phillipe P, GPL) */
    {"1x12 CREAM", 80, 8,  90, 4,  1500,-2,    0, 0,  2500, 4, 4500, 3,  6000},
    {"DI (NO CAB)", 0, 0,   0, 0,     0, 0,    0, 0,     0, 0,    0, 0,     0},
    {"USER IR", 0, 0,   0, 0,     0, 0,    0, 0,     0, 0,    0, 0,     0},    /* v38: an impulse response (px_ir_*) */
};

/* ================================================================== EQ ===== */
static const char *const PN_EQ5[] = {"100 HZ", "400 HZ", "1 KHZ", "2.5 KHZ", "6 KHZ"};
static const char *const PN_GEQ[] = {"80 HZ", "240 HZ", "750 HZ", "2.2 KHZ", "6.6 KHZ"};
static const char *const PN_PEQ[] = {"LOW", "MID FREQ", "MID GAIN", "MID Q", "HIGH"};
static const tdef T_EQ[PX_EQ_COUNT] = {{"5 BAND", 5, PN_EQ5}, {"GRAPHIC 5", 5, PN_GEQ}, {"PARAMETRIC", 5, PN_PEQ}};

/* ========================================================== modulation ===== */
static const char *const PN_RDM[] = {"RATE", "DEPTH", "MIX"};
static const char *const PN_DIM[] = {"MODE", "MIX"};
static const char *const PN_FLG[] = {"RATE", "DEPTH", "MANUAL", "FDBK", "MIX"};
static const char *const PN_P90[] = {"SPEED", "DEPTH"};
static const char *const PN_STN[] = {"RATE", "COLOR", "DEPTH"};
static const char *const PN_UVB[] = {"SPEED", "INTENS", "MIX"};
static const char *const PN_RD[] = {"RATE", "DEPTH"};
static const char *const PN_BTR[] = {"RATE", "DEPTH", "SHAPE"};
static const char *const PN_HTR[] = {"RATE", "DEPTH", "TONE"};
static const char *const PN_ROT[] = {"SPEED", "DEPTH", "BALANCE"};
static const tdef T_MOD[PX_MOD_COUNT] = {
    {"CE CHORUS", 3, PN_RDM}, {"DIMENSION", 2, PN_DIM}, {"FLANGER", 5, PN_FLG}, {"PHASE 90", 2, PN_P90},
    {"SMALL STONE", 3, PN_STN}, {"UNI-VIBE", 3, PN_UVB}, {"OPTO TREMOLO", 2, PN_RD}, {"BIAS TREMOLO", 3, PN_BTR},
    {"HARMONIC TREM", 3, PN_HTR}, {"ROTARY", 3, PN_ROT}, {"VIBRATO", 2, PN_RD}};

/* =============================================================== delay ===== */
static const char *const PN_DT[] = {"TIME", "FDBK", "MIX", "TONE"};
static const char *const PN_DMM[] = {"TIME", "FDBK", "MIX", "RATE", "DEPTH"};
static const char *const PN_DTP[] = {"TIME", "FDBK", "MIX", "TONE", "WOW"};
static const char *const PN_DSP[] = {"TIME", "FDBK", "MIX", "HEADS", "WOW"};
static const char *const PN_DRV2[] = {"TIME", "FDBK", "MIX"};
static const char *const PN_DLF[] = {"TIME", "FDBK", "MIX", "GRIT"};
static const tdef T_DLY[PX_DLY_COUNT] = {
    {"DIGITAL", 4, PN_DT}, {"ANALOG BBD", 4, PN_DT}, {"MEMORY MAN", 5, PN_DMM}, {"TAPE EP-3", 5, PN_DTP},
    {"SPACE ECHO", 5, PN_DSP}, {"REVERSE", 3, PN_DRV2}, {"LO-FI", 4, PN_DLF}, {"DUAL TAP", 4, PN_DT}};

/* ============================================================== reverb ===== */
static const char *const PN_RV[] = {"DECAY", "TONE", "MIX"};
static const char *const PN_RSP[] = {"DECAY", "TONE", "MIX", "DRIP"};
static const char *const PN_RSH[] = {"DECAY", "TONE", "MIX", "SHIMMER"};
static const char *const PN_RGT[] = {"HOLD", "TONE", "MIX"};
static const tdef T_REV[PX_REV_COUNT] = {
    {"ROOM", 3, PN_RV}, {"HALL", 3, PN_RV}, {"CATHEDRAL", 3, PN_RV}, {"PLATE", 3, PN_RV}, {"SPRING", 4, PN_RSP},
    {"SHIMMER", 4, PN_RSH}, {"GATED", 3, PN_RGT}, {"AMBIENT", 3, PN_RV}};

/* ------------------------------------------------------------ catalog API --- */
static const char *const PN_AMP[] = {"GAIN", "BASS", "MID", "TREBLE", "PRESENCE"};
static const char *const PN_NAM[] = {"MODEL", "GAIN", "BASS", "MID", "TREBLE"};
static const char *const PN_CAB[] = {"BODY", "BRIGHT", "MIC"};
static const char *const PN_CABIR[] = {"IR", "LOW CUT", "HIGH CUT"};   /* v38 */

const char *px_block_label(int b) { return (b >= 0 && b < PX_NBLK) ? BLK_LABEL[b] : "?"; }
const char *px_block_name(int b)  { return (b >= 0 && b < PX_NBLK) ? BLK_NAME[b] : "?"; }
int px_ntypes(int b)
{
    static const uint8_t N[PX_NBLK] = {1, PX_CMP_COUNT, PX_PIT_COUNT, PX_SFX_COUNT, PX_DRV_COUNT, PX_AMP_NTYPES,
                                       PX_CAB_COUNT, PX_EQ_COUNT, PX_MOD_COUNT, PX_DLY_COUNT, PX_REV_COUNT,
                                       PX_SYN_COUNT};
    return (b >= 0 && b < PX_NBLK) ? N[b] : 1;
}
static const tdef *tdef_of(int b, int t)
{
    switch (b) {
    case PX_B_GATE: return &T_GATE[0];
    case PX_B_COMP: return &T_COMP[t];
    case PX_B_PITCH: return &T_PITCH[t];
    case PX_B_SFX: return &T_SFX[t];
    case PX_B_EQ: return &T_EQ[t];
    case PX_B_MOD: return &T_MOD[t];
    case PX_B_DELAY: return &T_DLY[t];
    case PX_B_REVERB: return &T_REV[t];
    case PX_B_SYN: return &T_SYN[t];
    }
    return 0;
}
const char *px_type_name(int b, int t)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (b == PX_B_DRIVE) return DV[t].name;
    if (b == PX_B_AMP) return t == PX_AMP_NAM ? "NAM" : AV[t].name;
    if (b == PX_B_CAB) return CV[t].name;
    const tdef *d = tdef_of(b, t);
    return d ? d->name : "?";
}
int px_nparams(int b, int t)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (b == PX_B_DRIVE) return DV[t].np;
    if (b == PX_B_AMP) return 5;
    if (b == PX_B_CAB) return t == PX_CAB_DI ? 0 : 3;
    const tdef *d = tdef_of(b, t);
    return d ? d->np : 0;
}
const char *px_param_name(int b, int t, int k)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (k < 0 || k >= px_nparams(b, t)) return "";
    if (b == PX_B_DRIVE) return ROLE_NAME[DV[t].role[k]];
    if (b == PX_B_AMP) return t == PX_AMP_NAM ? PN_NAM[k] : PN_AMP[k];
    if (b == PX_B_CAB) return t == PX_CAB_IR ? PN_CABIR[k] : PN_CAB[k];
    return tdef_of(b, t)->pn[k];
}

/* value text for knobs where a number 0..100 says little (intervals, ms, bits...) */
static const int8_t HARM_ST[10] = {-12, -7, -5, -4, -3, 3, 4, 5, 7, 12};
static const char *const HARM_TXT[10] = {"-OCT", "-5TH", "-4TH", "-3RD", "-m3", "+m3", "+3RD", "+4TH", "+5TH", "+OCT"};
static const char *const HEAD_TXT[7] = {"1", "2", "3", "1+2", "2+3", "1+3", "ALL"};
static int harm_idx(int v) { int i = v * 10 / 101; return i < 0 ? 0 : (i > 9 ? 9 : i); }
/* TIME (v35): 20..1000 ms in steps of 12.25 ms up to 80, then to 2 s in steps of 50 ms; REVERSE: 250..1000 ms */
static float dly_ms(int t, int v)
{
    if (t == PX_DLY_REVERSE) return 250.0f + (float)v * 7.5f;
    return v <= 80 ? 20.0f + (float)v * 12.25f : 1000.0f + (float)(v - 80) * 50.0f;
}
static void put_int(char *b, int v, int plus)
{
    char t[8]; int n = 0, neg = v < 0;
    if (neg) v = -v;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
    int i = 0;
    if (neg) b[i++] = '-'; else if (plus && n && !(n == 1 && t[0] == '0')) b[i++] = '+';
    while (n) b[i++] = t[--n];
    b[i] = 0;
}
static void put_str(char *b, const char *s) { int i = 0; while (s[i] && i < 7) { b[i] = s[i]; i++; } b[i] = 0; }
/* LOW OCTAVE's RANGE: 0 = LOWEST (the OC-5's: the lowest note of any chord), 1..100 = the crossover, 80 Hz (only
 * the lowest notes of the E string) to 400 Hz (up to G4) */
static float lo_fc(int v) { return 80.0f * powf(5.0f, (float)(v < 1 ? 0 : v - 1) * (1.0f / 99.0f)); }
static void lo_note(char *b, int v)                   /* the note at the crossover: "D#3", or LOWEST */
{
    if (v < 1) { put_str(b, "LOWEST"); return; }
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int m = (int)(69.5f + 17.31234f * logf(lo_fc(v) / 440.0f));   /* 12 log2(f / 440) + 69, rounded */
    put_str(b, NM[m % 12]);
    int i = 0; while (b[i]) i++;
    b[i++] = (char)('0' + m / 12 - 1); b[i] = 0;
}
static void lo_db(char *b, float db)                   /* "+6 DB", "0 DB", "-12 DB" */
{
    put_int(b, (int)(db + (db >= 0.0f ? 0.5f : -0.5f)), 1);
    int i = 0; while (b[i]) i++;
    b[i++] = ' '; b[i++] = 'D'; b[i++] = 'B'; b[i] = 0;
}
/* LOW OCTAVE's other knobs in plain values: OCT -1 and DRY in dB against the note (OCT -1 50 = 0 dB, DRY 100 =
 * 0 dB), GROWL the drive into its clipper in dB, COMP its ratio (the octave's level change for the note's, 1.1:1 to
 * 10:1); OFF at 0 */
static void lo_text(char *b, int k, int v)
{
    const float x = (float)v * 0.01f;
    if (v <= 0) { put_str(b, "OFF"); return; }
    switch (k) {
    case 1: lo_db(b, 8.685889f * logf(2.0f * x)); return;            /* 20 log10 */
    case 2: lo_db(b, 8.685889f * logf(x)); return;
    case 3: lo_db(b, 8.685889f * logf(1.0f + 24.0f * x)); return;
    default: {
        int r10 = (int)(10.0f / (1.0f - 0.9f * x) + 0.5f);          /* the ratio, in tenths */
        int i = 0;
        if (r10 >= 100) { b[i++] = (char)('0' + r10 / 100); b[i++] = (char)('0' + (r10 / 10) % 10); }
        else { b[i++] = (char)('0' + r10 / 10); b[i++] = '.'; b[i++] = (char)('0' + r10 % 10); }
        b[i++] = ':'; b[i++] = '1'; b[i] = 0;
    }
    }
}
/* PAD's knobs: ATTACK 0.1 .. 6 s (the swell, and the crossing into a new chord), RELEASE 0.2 .. 10 s (the fade when
 * stopped), TONE 300 .. 2400 Hz, MOTION the grains' detune 0 .. 15 cents, MIX the pad's level (0 DB at 50) */
static float pad_att_s(float x) { return 0.1f + x * x * 5.9f; }
static float pad_rel_s(float x) { return 0.2f + x * x * 9.8f; }
static float pad_tone_hz(float x) { return 300.0f * powf(8.0f, x); }
static void pad_text(char *b, int k, int v)
{
    const float x = (float)v * 0.01f;
    int i = 0;
    switch (k) {
    case 0: case 1: {                                  /* "0.1 S" .. "9.9 S", "10 S" */
        int d = (int)((k ? pad_rel_s(x) : pad_att_s(x)) * 10.0f + 0.5f);
        if (d >= 100) { b[i++] = (char)('0' + d / 100); b[i++] = (char)('0' + (d / 10) % 10); }
        else { b[i++] = (char)('0' + d / 10); b[i++] = '.'; b[i++] = (char)('0' + d % 10); }
        b[i++] = ' '; b[i++] = 'S'; b[i] = 0;
    } return;
    case 2: put_int(b, (int)(pad_tone_hz(x) / 10.0f + 0.5f) * 10, 0); while (b[i]) i++;
        b[i++] = ' '; b[i++] = 'H'; b[i++] = 'Z'; b[i] = 0; return;
    case 3: if (v <= 0) { put_str(b, "OFF"); return; }
        put_int(b, (int)(x * 15.0f + 0.5f), 0); while (b[i]) i++;
        b[i++] = ' '; b[i++] = 'C'; b[i++] = 'T'; b[i] = 0; return;
    default: lo_text(b, 1, v); return;                /* as LOW OCTAVE's OCT -1: 0 DB at 50 */
    }
}
static const char *const NOTE_TXT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int syn_key_idx(int v) { int i = v * 12 / 101; return i < 0 ? 0 : (i > 11 ? 11 : i); }
static float syn_att_ms(float x) { return 10.0f + x * x * 1490.0f; }     /* STRINGS / CHOIR attack */
static float syn_rel_ms(float x) { return 50.0f + x * x * 2950.0f; }     /* release */
static int syn_bari_st(int v) { return -1 - (int)((float)v * 0.01f * 11.99f); }
static int syn_oct(int v) { return v < 34 ? -1 : (v < 67 ? 0 : 1); }
/* presets saved before v34 (store format 1 / 2): the old amp / drive / cab numbers -> the nearest of the v34 set */
/* the TIME knob value (0..100) whose delay is nearest to `ms` (MIDI clock sync) */
int px_dly_value_for_ms(int type, float ms)
{
    int best = 0;
    float bd = 1e30f;
    for (int v = 0; v <= 100; v++) {
        float d = dly_ms(type, v) - ms;
        if (d < 0.0f) d = -d;
        if (d < bd) { bd = d; best = v; }
    }
    return best;
}
int px_dly_time_v35(int type, int v)
{
    if (type == PX_DLY_REVERSE || v < 0) return v < 0 ? 0 : v;
    int k = (int)((float)v * (9.8f / 12.25f) + 0.5f);   /* old: 20 + 9.8 v ms; new: 20 + 12.25 v ms up to 80 */
    return k > 80 ? 80 : k;
}
int px_type_v34(int blk, int old)
{
    static const uint8_t A[29] = {PX_AMP_TWIN, PX_AMP_TWIN, PX_AMP_TWIN, PX_AMP_SHIRLEY, PX_AMP_AC15, PX_AMP_AC15, PX_AMP_AC15, PX_AMP_SHIRLEY, PX_AMP_SHIRLEY, PX_AMP_JCM900, PX_AMP_JCM2000, PX_AMP_JCM900, PX_AMP_JCM2000, PX_AMP_AC15, PX_AMP_MARK4, PX_AMP_PROMOD, PX_AMP_MARK4, PX_AMP_5150, PX_AMP_5150, PX_AMP_5150, PX_AMP_PROMOD, PX_AMP_PROMOD, PX_AMP_5150, PX_AMP_5150, PX_AMP_5150, PX_AMP_TWIN, PX_AMP_AC15, PX_AMP_TWIN, PX_AMP_TWIN};
    static const uint8_t D[27] = {PX_DRV_TS9, PX_DRV_TS9, PX_DRV_TS9, PX_DRV_KLON, PX_DRV_KLON, PX_DRV_M77, PX_DRV_HM2, PX_DRV_HM2, PX_DRV_M77, PX_DRV_KLON, PX_DRV_KLON, PX_DRV_KLON, PX_DRV_TS9, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_M77, PX_DRV_KLON, PX_DRV_HM2, PX_DRV_HM2, PX_DRV_TS9, PX_DRV_KLON};
    static const uint8_t C[15] = {PX_CAB_112JN, PX_CAB_112JN, PX_CAB_112JN, PX_CAB_212BLUE, PX_CAB_412GB, PX_CAB_112JN, PX_CAB_412GB, PX_CAB_412T75, PX_CAB_412V30, PX_CAB_412V30, PX_CAB_112CREAM, PX_CAB_112JN, PX_CAB_412V30, PX_CAB_112JN, PX_CAB_DI};
    if (old < 0) return 0;
    if (blk == PX_B_AMP) return old < 29 ? A[old] : 0;
    if (blk == PX_B_DRIVE) return old < 27 ? D[old] : 0;
    if (blk == PX_B_CAB) return old < 15 ? C[old] : 0;
    return old;
}

/* v38: USER IR's LOW CUT 20..400 Hz and HIGH CUT 20 kHz (0: off) .. 2 kHz, logarithmic */
static float ir_lo_hz(int v) { return 20.0f * powf(20.0f, (float)v * 0.01f); }
static float ir_hi_hz(int v) { return 20000.0f * powf(0.1f, (float)v * 0.01f); }
int px_param_text(int b, int t, int k, int v, char *buf)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    float x = (float)v * 0.01f;
    switch (b) {
    case PX_B_GATE:
        if (k == PX_PATCH_LEVEL) {                     /* "+1.5 DB": 0.3 dB a step */
            int d = (v - 50) * 3, a = d < 0 ? -d : d, i = 0;
            buf[i++] = d < 0 ? '-' : '+';
            if (a >= 100) buf[i++] = (char)('0' + a / 100);
            buf[i++] = (char)('0' + (a / 10) % 10); buf[i++] = '.'; buf[i++] = (char)('0' + a % 10);
            buf[i++] = ' '; buf[i++] = 'D'; buf[i++] = 'B'; buf[i] = 0;
            return 1;
        }
        break;
    case PX_B_CAB:
        if (t == PX_CAB_IR && k == 0) {                /* v38: the IR's name */
            char nm[12];
            px_ir_name(px_ir_slot(v), nm);
            for (int i = 0; i < 9; i++) buf[i] = nm[i];
            buf[8] = 0;
            return 1;
        }
        if (t == PX_CAB_IR && k == 1) { put_int(buf, (int)(ir_lo_hz(v) + 0.5f), 0); return 1; }
        if (t == PX_CAB_IR && k == 2) { put_int(buf, (int)(ir_hi_hz(v) + 0.5f), 0); return 1; }
        break;
    case PX_B_AMP:
        if (t == PX_AMP_NAM && k == 0) {               /* the model's name */
            char nm[12];
            px_nam_name(px_nam_model(v), nm);
            for (int i = 0; i < 9; i++) buf[i] = nm[i];
            buf[8] = 0;
            return 1;
        }
        if (t == PX_AMP_NAM && k == 1) { float db = (x - 0.5f) * 24.0f; put_int(buf, (int)(db + (db >= 0 ? 0.5f : -0.5f)), 1); return 1; }
        break;
    case PX_B_PITCH:
        if (t == PX_PIT_HARMONY && k == 0) { put_str(buf, HARM_TXT[harm_idx(v)]); return 1; }
        if (t == PX_PIT_WHAMMY && k == 0) { float st = (x - 0.5f) * 48.0f; put_int(buf, (int)(st + (st >= 0 ? 0.5f : -0.5f)), 1); return 1; }
        if (t == PX_PIT_DETUNE && k == 0) { put_int(buf, (int)(3.0f + x * 27.0f + 0.5f), 0); return 1; }
        if (t == PX_PIT_LOWOCT && k == 0) { lo_note(buf, v); return 1; }
        if (t == PX_PIT_LOWOCT && k <= 4) { lo_text(buf, k, v); return 1; }
        break;
    case PX_B_SFX:
        if (t == PX_SFX_CRUSH && k == 0) { put_int(buf, 16 - (int)(x * 14.0f + 0.5f), 0); return 1; }
        if (t == PX_SFX_CRUSH && k == 1) { put_int(buf, 1 + (int)(x * 39.0f + 0.5f), 0); return 1; }
        if (t == PX_SFX_RING && k == 0) { put_int(buf, (int)(20.0f * powf(100.0f, x) + 0.5f), 0); return 1; }
        if (t == PX_SFX_SLICER && k == 2) { buf[0] = 'P'; put_int(buf + 1, 1 + (int)(x * 7.99f), 0); return 1; }
        break;
    case PX_B_EQ:
        if (t == PX_EQ_PARAM && k == 1) { put_int(buf, (int)(150.0f * powf(33.0f, x) + 0.5f), 0); return 1; }
        if (t == PX_EQ_PARAM && k == 3) { int q = (int)((0.4f + x * 5.6f) * 10.0f + 0.5f);
            buf[0] = (char)('0' + q / 10); buf[1] = '.'; buf[2] = (char)('0' + q % 10); buf[3] = 0; return 1; }
        { float db = (x - 0.5f) * 24.0f; put_int(buf, (int)(db + (db >= 0 ? 0.5f : -0.5f)), 1); return 1; }
    case PX_B_MOD:
        if (t == PX_MOD_DIMENSION && k == 0) { put_int(buf, 1 + (int)(x * 3.99f), 0); return 1; }
        if (t == PX_MOD_ROTARY && k == 0) { put_str(buf, v < 50 ? "SLOW" : "FAST"); return 1; }
        break;
    case PX_B_SYN:
        if ((t == PX_SYN_STRINGS || t == PX_SYN_CHOIR) && k == 0) { put_int(buf, (int)(syn_att_ms(x) + 0.5f), 0); return 1; }
        if ((t == PX_SYN_STRINGS || t == PX_SYN_CHOIR) && k == 1) { put_int(buf, (int)(syn_rel_ms(x) + 0.5f), 0); return 1; }
        if (t == PX_SYN_MONO && k == 1) { put_int(buf, syn_oct(v), 1); return 1; }
        if (t == PX_SYN_12STR && k == 1) { put_int(buf, (int)(x * 15.0f + 0.5f), 0); return 1; }
        if (t == PX_SYN_SITAR && k == 0) { put_str(buf, NOTE_TXT[syn_key_idx(v)]); return 1; }
        if (t == PX_SYN_BARI && k == 0) { put_int(buf, syn_bari_st(v), 0); return 1; }
        if (t == PX_SYN_PAD) { pad_text(buf, k, v); return 1; }
        break;
    case PX_B_DELAY:
        if (k == 0) { put_int(buf, (int)(dly_ms(t, v) + 0.5f), 0); int i = 0; while (buf[i]) i++;
                      buf[i] = ' '; buf[i + 1] = 'M'; buf[i + 2] = 'S'; buf[i + 3] = 0; return 1; }   /* "450 MS" */
        if (t == PX_DLY_SPACE && k == 3) { put_str(buf, HEAD_TXT[(int)(x * 6.99f)]); return 1; }
        break;
    }
    return 0;
}

/* ============================================================= presets ===== */
static void blk(px_preset_t *p, int b, int on, int type, int a, int c, int d, int e, int f)
{
    p->on[b] = (uint8_t)on; p->type[b] = (uint8_t)type;
    p->p[b][0] = (uint8_t)a; p->p[b][1] = (uint8_t)c; p->p[b][2] = (uint8_t)d; p->p[b][3] = (uint8_t)e; p->p[b][4] = (uint8_t)f;
}
static void base_preset(px_preset_t *p)
{
    memset(p, 0, sizeof(*p));
    blk(p, PX_B_GATE,   1, 0, 40, 20, 50, 0, 0);   /* 3rd: preset LEVEL (v36), 50 = 0 dB */
    blk(p, PX_B_COMP,   0, PX_CMP_SQUASH, 50, 30, 50, 100, 0);
    blk(p, PX_B_PITCH,  0, PX_PIT_OCTAVER, 70, 50, 0, 0, 40);
    blk(p, PX_B_SFX,    0, PX_SFX_AUTOWAH, 60, 60, 60, 40, 0);
    blk(p, PX_B_DRIVE,  0, PX_DRV_TS9, 30, 50, 60, 50, 50);
    blk(p, PX_B_AMP,    1, PX_AMP_SHIRLEY, 60, 60, 50, 60, 50);
    blk(p, PX_B_CAB,    1, PX_CAB_412V30, 50, 50, 50, 0, 0);
    blk(p, PX_B_EQ,     0, PX_EQ_5BAND, 50, 50, 50, 50, 50);
    blk(p, PX_B_MOD,    0, PX_MOD_CHORUS, 30, 40, 40, 30, 50);
    blk(p, PX_B_DELAY,  0, PX_DLY_DIGITAL, 28, 30, 25, 50, 30);
    blk(p, PX_B_REVERB, 0, PX_REV_HALL, 50, 50, 20, 40, 0);
    blk(p, PX_B_SYN,    0, PX_SYN_ORGAN, 50, 70, 40, 30, 0);
    px_order_default(p->order);
}
static void setname(px_preset_t *p, const char *s)
{
    int i = 0;
    for (; s[i] && i < (int)sizeof(p->name) - 1; i++) p->name[i] = s[i];
    p->name[i] = 0;
}
enum { NPRESETS = 52 };
int px_preset_count(void) { return NPRESETS; }
/* v37: 24 factory presets built round the factory NAM A2 models (NAM(model, gain, bass, mid, treble): knobs at noon
 * = the captured amp), each one levelled (LEVEL, tools/level_presets.py: -23 LUFS on DI guitar, as v36); slots 25..52: USER,
 * a clean starting point */
#define LVL(v) (p->p[PX_B_GATE][PX_PATCH_LEVEL] = (uint8_t)(v))
#define NOGATE (p->on[PX_B_GATE] = 0)
#define NAM(m, g, b, md, t) blk(p, PX_B_AMP, 1, PX_AMP_NAM, px_nam_knob(m), g, b, md, t)
enum { M_TWIN, M_AC15, M_SUPER59, M_SHIRLCL, M_JCM900, M_SHIRLEY, M_JCM2000, M_MARK4, M_JVM, M_PROMOD, M_5150, M_6505 };
void px_preset_factory(px_preset_t *p, int idx)
{
    base_preset(p);
    if (idx < 0 || idx >= NPRESETS) idx = 0;
    p->on[PX_B_AMP] = p->on[PX_B_CAB] = 1;
    switch (idx) {
    /* ---- clean ---- */
    case 0: setname(p, "TWIN CLEAN"); blk(p, PX_B_GATE, 1, 0, 15, 50, 50, 0, 0); LVL(54);
        blk(p, PX_B_COMP, 1, PX_CMP_STUDIO, 30, 50, 50, 50, 0);
        NAM(M_TWIN, 50, 50, 50, 55); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_PLATE, 40, 50, 22, 0, 0); break;
    case 1: setname(p, "CHORUS CLEAN"); blk(p, PX_B_GATE, 1, 0, 15, 50, 50, 0, 0); LVL(55);
        NAM(M_TWIN, 45, 50, 50, 55); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_CHORUS, 30, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 35, 25, 18, 50, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 45, 50, 22, 0, 0); break;
    case 2: setname(p, "AC15 CHIME"); blk(p, PX_B_GATE, 1, 0, 20, 50, 50, 0, 0); LVL(50);
        NAM(M_AC15, 50, 48, 50, 52); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 55, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 40, 50, 20, 40, 0); break;
    case 3: setname(p, "FUNK WAH"); NOGATE; LVL(59);
        blk(p, PX_B_COMP, 1, PX_CMP_SQUASH, 55, 35, 50, 0, 0); blk(p, PX_B_SFX, 1, PX_SFX_AUTOWAH, 55, 50, 55, 40, 0);
        NAM(M_TWIN, 45, 45, 55, 60); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 35, 50, 15, 0, 0); break;
    case 4: setname(p, "SURF SPRING"); blk(p, PX_B_GATE, 1, 0, 15, 50, 50, 0, 0); LVL(66);
        NAM(M_SUPER59, 45, 50, 45, 58); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 55, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_TAPE, 7, 10, 15, 50, 20); blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 55, 55, 40, 60, 0); break;
    case 5: setname(p, "AMBIENT SWELL"); blk(p, PX_B_GATE, 1, 0, 18, 50, 50, 0, 0); LVL(68);
        blk(p, PX_B_COMP, 1, PX_CMP_SWELL, 50, 50, 50, 0, 0);
        NAM(M_TWIN, 45, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 45, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_CHORUS, 20, 30, 30, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DUAL, 35, 45, 30, 40, 0); blk(p, PX_B_REVERB, 1, PX_REV_AMBIENT, 50, 45, 40, 0, 0); break;
    /* ---- edge of break-up, crunch ---- */
    case 6: setname(p, "TS BLUES"); blk(p, PX_B_GATE, 1, 0, 18, 50, 50, 0, 0); LVL(64);
        blk(p, PX_B_DRIVE, 1, PX_DRV_TS9, 35, 50, 50, 0, 0);
        NAM(M_SUPER59, 55, 50, 50, 52); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 40, 50, 18, 40, 0); break;
    case 7: setname(p, "BLUES BREAKUP"); blk(p, PX_B_GATE, 1, 0, 22, 50, 50, 0, 0); LVL(51);
        NAM(M_SHIRLCL, 55, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_ANALOG, 29, 20, 15, 40, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 50, 18, 0, 0); break;
    case 8: setname(p, "KLON CRUNCH"); blk(p, PX_B_GATE, 1, 0, 18, 50, 50, 0, 0); LVL(66);
        blk(p, PX_B_DRIVE, 1, PX_DRV_KLON, 40, 50, 55, 0, 0);
        NAM(M_JCM900, 45, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 50, 18, 0, 0); break;
    case 9: setname(p, "BRIT CRUNCH"); blk(p, PX_B_GATE, 1, 0, 25, 30, 50, 0, 0); LVL(51);
        NAM(M_JCM900, 55, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 35, 50, 15, 0, 0); break;
    case 10: setname(p, "70S ROCK"); blk(p, PX_B_GATE, 1, 0, 25, 30, 50, 0, 0); LVL(48);
        blk(p, PX_B_DRIVE, 1, PX_DRV_M77, 20, 50, 65, 0, 0);
        NAM(M_AC15, 60, 55, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_PLATE, 35, 50, 15, 0, 0); break;
    case 11: setname(p, "BOUTIQUE"); blk(p, PX_B_GATE, 1, 0, 25, 30, 50, 0, 0); LVL(40);
        NAM(M_SHIRLEY, 50, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 29, 15, 12, 45, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 50, 15, 0, 0); break;
    /* ---- lead ---- */
    case 12: setname(p, "BRIT LEAD"); blk(p, PX_B_GATE, 1, 0, 30, 30, 50, 0, 0); LVL(50);
        NAM(M_JCM2000, 55, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_ANALOG, 39, 30, 20, 45, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 45, 45, 20, 0, 0); break;
    case 13: setname(p, "SMOOTH LEAD"); blk(p, PX_B_GATE, 1, 0, 30, 30, 50, 0, 0); LVL(41);
        NAM(M_MARK4, 55, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_TAPE, 35, 30, 20, 45, 20); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 45, 45, 18, 0, 0); break;
    case 14: setname(p, "HAIR METAL"); blk(p, PX_B_GATE, 1, 0, 35, 30, 50, 0, 0); LVL(50);
        blk(p, PX_B_DRIVE, 1, PX_DRV_M77, 25, 55, 60, 0, 0);
        NAM(M_JVM, 50, 55, 45, 55); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 33, 25, 15, 50, 0); blk(p, PX_B_REVERB, 1, PX_REV_PLATE, 40, 50, 18, 0, 0); break;
    case 15: setname(p, "SPACE LEAD"); blk(p, PX_B_GATE, 1, 0, 30, 30, 50, 0, 0); LVL(48);
        NAM(M_PROMOD, 50, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DUAL, 29, 40, 25, 40, 0); blk(p, PX_B_REVERB, 1, PX_REV_SHIMMER, 55, 45, 30, 40, 0); break;
    /* ---- high gain ---- */
    case 16: setname(p, "MODERN ROCK"); blk(p, PX_B_GATE, 1, 0, 40, 30, 50, 0, 0); LVL(44);
        NAM(M_PROMOD, 55, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 30, 50, 12, 0, 0); break;
    case 17: setname(p, "5150 RHYTHM"); blk(p, PX_B_GATE, 1, 0, 45, 25, 50, 0, 0); LVL(44);
        blk(p, PX_B_DRIVE, 1, PX_DRV_TS9, 0, 55, 75, 0, 0);
        NAM(M_5150, 50, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0); break;
    case 18: setname(p, "TIGHT METAL"); blk(p, PX_B_GATE, 1, 0, 50, 20, 50, 0, 0); LVL(42);
        NAM(M_6505, 50, 55, 45, 55); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 55, 50, 0, 0); break;
    case 19: setname(p, "SWEDE DEATH"); blk(p, PX_B_GATE, 1, 0, 55, 25, 50, 0, 0); LVL(62);
        blk(p, PX_B_DRIVE, 1, PX_DRV_HM2, 70, 80, 80, 50, 0);
        NAM(M_JCM900, 40, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0); break;
    case 20: setname(p, "DOOM"); blk(p, PX_B_GATE, 1, 0, 40, 35, 50, 0, 0); LVL(26);
        NAM(M_5150, 60, 62, 45, 45); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 60, 40, 40, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_ANALOG, 45, 20, 10, 40, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 40, 35, 15, 0, 0); break;
    /* ---- effects ---- */
    case 21: setname(p, "VIBE ROCK"); blk(p, PX_B_GATE, 1, 0, 18, 50, 50, 0, 0); LVL(51);
        blk(p, PX_B_MOD, 1, PX_MOD_UNIVIBE, 35, 55, 60, 0, 0);
        NAM(M_JCM900, 55, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 50, 18, 0, 0); break;
    case 22: setname(p, "ORGAN"); NOGATE; LVL(75);
        blk(p, PX_B_SYN, 1, PX_SYN_ORGAN, 50, 70, 40, 30, 0);
        NAM(M_TWIN, 45, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_ROTARY, 30, 60, 50, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 50, 18, 0, 0); break;
    case 23: setname(p, "ACOUSTIC"); NOGATE; LVL(66);
        blk(p, PX_B_SYN, 1, PX_SYN_ACOUSTIC, 55, 50, 55, 40, 0); blk(p, PX_B_COMP, 1, PX_CMP_STUDIO, 40, 40, 85, 60, 0);
        p->on[PX_B_AMP] = 0; p->on[PX_B_CAB] = 0;
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 55, 18, 0, 0); break;
    /* ---- 25..52: USER, a clean NAM amp to start from ---- */
    default: {
        char n[14] = "USER ";
        n[5] = (char)('0' + (idx + 1) / 10); n[6] = (char)('0' + (idx + 1) % 10); n[7] = 0;
        setname(p, n); NOGATE; LVL(50);
        NAM(M_TWIN, 50, 50, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 35, 50, 15, 0, 0);
    } break;
    }
}
#undef NAM
#undef LVL
#undef NOGATE

/* ============================================================ recalc ======= */
/* reverb (v35). FDN: 8 lines (prime lengths at 44.1 kHz, x the type's size), Hadamard feedback, each line with its own
 * low / high frequency decay (Jot); the first 4 lines move slowly (chorus in the tail: no metallic ringing).
 * Plate: J. Dattorro, "Effect Design Part 1" (JAES 1997), lengths at 29761 Hz. Both: Dattorro's input diffusers. */
static const int RV_FDN[8] = {1171, 1373, 1601, 1847, 2111, 2393, 2689, 3011};
static const int RV_DIF[4] = {142, 107, 379, 277};
static const float RV_DIFG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
static const int PL_LEN[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
static const int PL_TAP[14] = {266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121};
static const uint8_t PL_TU[14] = {10, 10, 11, 12, 6, 7, 8, 6, 6, 7, 8, 10, 11, 12};
static const float ER_T[4] = {0.11f, 0.31f, 0.57f, 1.0f};
static const float ER_G[4] = {0.95f, -0.72f, 0.5f, -0.33f};
typedef struct { float size, pre_ms, er, er_ms, mod, rt0, rt1, hf0, hf1, out; } rv_def;
static const rv_def RVD[PX_REV_COUNT] = {
    /* size  pre   ER  ER ms mod (samples at 44.1 kHz)  RT min +range  HF decay ratio  output */
    {0.30f,  4, 0.70f, 25,  4, 0.3f, 0.9f,  0.15f, 0.60f, 0.502f},   /* ROOM */
    {0.85f, 18, 0.35f, 60, 20, 1.2f, 2.8f,  0.15f, 0.60f, 0.475f},   /* HALL */
    {1.35f, 35, 0.25f, 90, 24, 3.0f, 7.0f,  0.12f, 0.55f, 0.393f},   /* CATHEDRAL */
    {1.0f,   0, 0.0f,   0, 16, 0.6f, 4.4f,  0.0f,  0.0f,  1.478f},   /* PLATE (size: Dattorro's) */
    {0.40f,  0, 0.0f,   0,  2, 1.5f, 2.5f,  0.10f, 0.40f, 0.279f},   /* SPRING */
    {1.0f,  25, 0.0f,   0, 20, 2.0f, 6.0f,  0.20f, 0.60f, 0.368f},   /* SHIMMER */
    {0.55f,  3, 0.50f, 30,  8, 2.5f, 0.0f,  0.30f, 0.50f, 0.468f},   /* GATED */
    {1.35f, 30, 0.0f,   0, 28, 6.0f, 14.0f, 0.10f, 0.50f, 0.368f}};  /* AMBIENT */
static const uint16_t SLICE_PAT[8] = {0xAAAA, 0xCCCC, 0xF0F0, 0xB6B6, 0x9249, 0xEEEE, 0xDB6D, 0x8888};

static float role_val(const drv_v *d, const uint8_t *q, int role, float def)
{
    for (int k = 0; k < d->np; k++) if (d->role[k] == role) return f01(q[k]);
    return def;
}

static void rc_dyn(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_GATE];
    e->gate_thr = db2lin(-80.0f + f01(q[0]) * 60.0f);
    e->gate_rel = expf(-1.0f / (0.001f * (5.0f + f01(q[1]) * 495.0f) * sr));

    q = p->p[PX_B_COMP];
    int t = p->type[PX_B_COMP] % PX_CMP_COUNT;
    e->cmp_rel = expf(-1.0f / (0.15f * sr));
    e->cmp_mix = 1.0f;
    switch (t) {
    case PX_CMP_SQUASH:
        e->cmp_k = f01(q[0]) * 10.0f;
        e->cmp_att = expf(-1.0f / (0.001f * (1.0f + f01(q[1]) * 49.0f) * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        break;
    case PX_CMP_STUDIO:
        e->cmp_k = f01(q[0]) * 4.0f;
        e->cmp_att = expf(-1.0f / (0.001f * (1.0f + f01(q[1]) * 49.0f) * sr));
        e->cmp_rel = expf(-1.0f / (0.2f * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        e->cmp_mix = f01(q[3]);
        break;
    case PX_CMP_OPTO:                                  /* optical cell: 10 ms attack, slow program-like release */
        e->cmp_k = f01(q[0]) * 6.0f;
        e->cmp_att = expf(-1.0f / (0.010f * sr));
        e->cmp_rel = expf(-1.0f / (0.6f * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[1]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        e->cmp_mix = f01(q[2]);
        break;
    default:                                           /* slow gear: volume swell after each pick attack */
        e->cmp_rise = onepole_k(sr, 0.05f + f01(q[0]) * f01(q[0]) * 1.95f);
        e->cmp_thr = db2lin(-30.0f - f01(q[1]) * 30.0f);
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f);
        e->cmp_att = expf(-1.0f / (0.001f * sr));
        e->cmp_rel = expf(-1.0f / (0.05f * sr));
        e->cmp_slowk = onepole_k(sr, 0.03f);
        break;
    }
}

static void rc_pitch(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_PITCH];
    int t = p->type[PX_B_PITCH] % PX_PIT_COUNT;
    e->ps_nv = 0; e->ps_glide = 1.0f;
    switch (t) {
    case PX_PIT_OCTAVER:                               /* analog: flip-flop divider (-1, -2) + rectifier (+1) */
        bq_lowpass(&e->oct_lp, sr, 500.0f, 0.7f);
        bq_lowpass(&e->oct_lp2, sr, 500.0f, 0.7f);
        bq_lowpass(&e->oct_sublp, sr, 150.0f + f01(q[4]) * 650.0f, 0.7f);
        bq_lowpass(&e->oct_sublp2, sr, 100.0f + f01(q[4]) * 400.0f, 0.7f);
        bq_bandpass(&e->oct_bp, sr, 900.0f, 0.7f);
        bq_highpass(&e->oct_uphp, sr, 60.0f, 0.7f);
        e->oct_dry = f01(q[0]); e->oct_s1 = f01(q[1]) * 2.5f; e->oct_s2 = f01(q[2]) * 2.5f; e->oct_up = f01(q[3]) * 2.0f;
        break;
    case PX_PIT_POLYOCT:                               /* two delay-line shifters: -12 and +12 */
        e->ps_nv = 2; e->ps_dry = f01(q[0]);
        e->ps_W[0] = 1400.0f; e->ps_inc[0] = 0.5f / 1400.0f; e->ps_lvl[0] = f01(q[1]) * 1.2f;
        e->ps_W[1] = 1000.0f; e->ps_inc[1] = -1.0f / 1000.0f; e->ps_lvl[1] = f01(q[2]);
        break;
    case PX_PIT_WHAMMY: {
        float st = (f01(q[0]) - 0.5f) * 48.0f, m = f01(q[1]);
        e->ps_nv = 1; e->ps_dry = 1.0f - m; e->ps_lvl[0] = m;
        e->ps_W[0] = 1400.0f; e->ps_invW = 1.0f / 1400.0f;
        e->ps_rt = expf(st * 0.05776227f);             /* 2^(st/12) */
        if (e->ps_r <= 0.0f) e->ps_r = e->ps_rt;
        e->ps_glide = onepole_k(sr, 0.003f + f01(q[2]) * f01(q[2]) * 0.6f);
    } break;
    case PX_PIT_HARMONY: {
        float r = expf((float)HARM_ST[harm_idx(q[0])] * 0.05776227f);
        e->ps_nv = 1; e->ps_dry = 1.0f; e->ps_lvl[0] = f01(q[1]) * 1.1f;
        e->ps_W[0] = r < 1.0f ? 1400.0f : 1100.0f; e->ps_inc[0] = (1.0f - r) / e->ps_W[0];
    } break;
    case PX_PIT_LOWOCT: {                              /* the lowest note below RANGE an octave down, the rest dry */
        static const float QB6[3] = {0.5176f, 0.7071f, 1.9319f};   /* 6th-order Butterworth (an 8th: +1.5 ms) */
        const float sd = sr / (float)PX_LO_DEC;
        e->lo_lowest = q[0] < 1;
        const float fc = e->lo_lowest ? 600.0f : lo_fc(q[0]);
        bq_lowpass(&e->lo_aa[0], sr, 700.0f, 0.5412f); /* 4th-order Butterworth: nothing folds back at sr / 8 */
        bq_lowpass(&e->lo_aa[1], sr, 700.0f, 1.3066f);
        for (int i = 0; i < 3; i++) bq_lowpass(&e->lo_sel[i], sd, fc, QB6[i]);   /* steep at RANGE */
        bq_highpass(&e->lo_hp, sd, 50.0f, 0.7071f);   /* rumble and thumps kept out of the octave (-10 dB at 30 Hz) */
        e->lo_lpk = onepole_k(sr, 1.0f / (2.0f * PI_F * 900.0f));   /* after the x8 interpolation: its images */
        /* the tilt: one-pole low-passes at 60 Hz, -6 dB an octave each above it, so that of two notes the lower
         * one leads the phase (4: a fifth above is 10 dB down; LOWEST, 6: 16 dB, a fourth 11 dB, anywhere on the
         * neck). The level comes from the band before the tilt: a pluck's tilted start settles in 20 ms. */
        e->lo_np = e->lo_lowest ? 6 : 4;
        e->lo_tk = onepole_k(sd, 1.0f / (2.0f * PI_F * 60.0f));
        e->lo_aatk = onepole_k(sd, 0.0003f); e->lo_arel = onepole_k(sd, 0.025f);   /* the level: up at once, down in 25 ms */
        e->lo_c = 0.9f * f01(q[4]);                    /* COMP: 0 .. 0.9 of the way to an even level */
        e->lo_lvl = f01(q[1]) * 2.0f; e->lo_dry = f01(q[2]);
        {   /* GROWL (the OC-3's DRIVE, on the octave only): tanh(k o), as loud as the clean octave at 0.25, then
             * smoothed above 2 kHz; mixed in with the knob, so 0 leaves the octave exactly as it is */
            float g = f01(q[3]);
            e->lo_g = g; e->lo_gk = 1.0f + 24.0f * g;
            e->lo_gn = 0.25f / px_tanh(0.25f * e->lo_gk);
            e->lo_glk = onepole_k(sr, 1.0f / (2.0f * PI_F * 2000.0f));
        }
    } break;
    default: {                                         /* doubler: +c and -c cents */
        float c = 3.0f + f01(q[0]) * 27.0f, m = f01(q[1]);
        float r0 = expf(c * 0.000577623f), r1 = expf(-c * 0.000577623f);
        e->ps_nv = 2; e->ps_dry = 1.0f - 0.5f * m;
        e->ps_W[0] = 1200.0f; e->ps_inc[0] = (1.0f - r0) / 1200.0f; e->ps_lvl[0] = 0.6f * m;
        e->ps_W[1] = 1100.0f; e->ps_inc[1] = (1.0f - r1) / 1100.0f; e->ps_lvl[1] = 0.6f * m;
    } break;
    }
    if (e->ps_ph[1] == 0.0f && e->ps_ph[0] == 0.0f) e->ps_ph[1] = 0.37f;
}

static void rc_sfx(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_SFX];
    int t = p->type[PX_B_SFX] % PX_SFX_COUNT;
    switch (t) {
    case PX_SFX_AUTOWAH:                               /* envelope filter (Mu-Tron style) */
        e->sfx_c = 1.0f + f01(q[0]) * 15.0f;           /* sensitivity */
        e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = 800.0f + f01(q[2]) * 2700.0f;       /* range */
        e->sfx_a = onepole_k(sr, 0.008f);
        e->sfx_b = onepole_k(sr, 0.05f + f01(q[3]) * 0.45f);
        break;
    case PX_SFX_LFOWAH:
        e->sfx_inc = (0.1f + f01(q[0]) * f01(q[0]) * 7.9f) / sr;
        e->sfx_c = f01(q[1]);
        e->sfx_qd = 1.0f - f01(q[2]) * 0.85f;
        e->sfx_d = f01(q[3]);
        break;
    case PX_SFX_RING:
        e->sfx_inc = 20.0f * powf(100.0f, f01(q[0])) / sr;
        e->sfx_mix = f01(q[1]);
        e->sfx_c = f01(q[2]) * 0.5f;
        e->sfx_d = 1.3f / sr;                          /* wobble LFO */
        break;
    case PX_SFX_CRUSH: {
        int bits = 16 - (int)(f01(q[0]) * 14.0f + 0.5f);
        e->crush_q = powf(2.0f, (float)(bits - 1));
        e->crush_k = 1 + (int)(f01(q[1]) * 39.0f + 0.5f);
        e->sfx_mix = f01(q[2]);
    } break;
    case PX_SFX_SLICER:
        e->sfx_inc = (2.0f + f01(q[0]) * 14.0f) / (16.0f * sr);
        e->sfx_c = f01(q[1]);
        e->sfx_pat = SLICE_PAT[(int)(f01(q[2]) * 7.99f)];
        e->sfx_a = onepole_k(sr, 0.002f);
        break;
    case PX_SFX_LOFI:
        bq_highpass(&e->sfx_f1, sr, 350.0f + f01(q[0]) * 250.0f, 0.7f);
        bq_lowpass(&e->sfx_f2, sr, 1500.0f + f01(q[0]) * 2500.0f, 1.4f);
        e->sfx_c = 1.0f + f01(q[1]) * 8.0f;
        e->sfx_mix = f01(q[2]);
        break;
    default:                                           /* square synth: square + sub through a swept low-pass */
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[0]);
        e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = f01(q[2]);
        e->sfx_mix = f01(q[3]);
        e->sfx_a = onepole_k(sr, 0.002f);
        e->sfx_b = onepole_k(sr, 0.15f);
        if (e->sfx_ff == 0.0f) e->sfx_ff = 1.0f;
        break;
    }
}

static void rc_drive(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr, osr = sr * (float)(e->drv_os ? e->drv_os : PX_OVERSAMPLE);
    int t = p->type[PX_B_DRIVE] % PX_DRV_COUNT;
    const drv_v *d = &DV[t];
#ifdef PX_FIT
    if (px_fit_drv_on) { d = &px_fit_drv; t = -1; }     /* the fitter's candidate: no per-type special case */
#endif
    const uint8_t *q = p->p[PX_B_DRIVE];
    float dr = role_val(d, q, R_DRIVE, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_DIST, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_FUZZ, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_SUST, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_BOOST, 0.5f);
    float tone = role_val(d, q, R_TONE, -1.0f);
    if (tone < 0.0f) tone = role_val(d, q, R_TREBLE, 0.5f);
    float level = role_val(d, q, R_LEVEL, 0.5f);

    float gdb = (float)d->g0 + (float)(d->g1 - d->g0) * dr;
    e->drv_gain = db2lin(gdb);

    /* pre-clip, base rate */
    float hp = (float)d->hp;
    bq_hp1(&e->drv_hp, sr, hp);
    e->drv_pre = 0;
    if (d->shelf) {                                    /* TS-style: lows bypass the clipping gain */
        bq_hp1(&e->drv_shelf, sr, (float)d->shelf);
        e->drv_shelf_lo = 1.0f / e->drv_gain;
        e->drv_pre |= 1;
    }
    if (d->lp) { bq_lowpass(&e->drv_lp, sr, (float)d->lp, 0.7f); e->drv_pre |= 2; }
    if (d->mid) {
        float f = (float)d->mid, db = (float)d->mid_db;
        bq_peak(&e->drv_mid, sr, f, (float)d->mid_q10 * 0.1f, db);
        e->drv_pre |= 4;
    }

    /* clipper */
    e->drv_clip = d->clip;
    e->drv_bias = (float)d->bias * 0.01f;
    e->drv_gate = 0.0f;
    e->drv_off = px_tanh(e->drv_bias);
    e->drv_dcb = (d->clip == CL_ASYM || d->clip == CL_FUZZ || d->clip == CL_OCTA);
    e->drv_dck = 2.0f * PI_F * 12.0f / osr;

    /* post tone, oversampled */
    e->drv_tm = d->tm; e->drv_np = 1;
    switch (d->tm) {
    case TM_MUFF:                                      /* LP / HP crossfade: the classic mid scoop */
        bq_lowpass(&e->drv_t1, osr, (float)d->t0, 0.6f);
        bq_highpass(&e->drv_t2, osr, (float)d->t1, 0.6f);
        e->drv_mix = tone;
        e->drv_np = 2;
        break;
    case TM_HM2:
        bq_peak(&e->drv_t1, osr, (float)d->t0, 0.9f, role_val(d, q, R_LOW, 0.5f) * 15.0f);
        bq_peak(&e->drv_t2, osr, (float)d->t1, 0.7f, role_val(d, q, R_HIGH, 0.5f) * 15.0f);
        e->drv_np = 2;
        break;
    case TM_MT2:
        bq_peak(&e->drv_t1, osr, (float)d->t0, 0.8f, (role_val(d, q, R_LOW, 0.5f) - 0.5f) * 24.0f);
        bq_highshelf(&e->drv_t2, osr, (float)d->t1, (role_val(d, q, R_HIGH, 0.5f) - 0.5f) * 24.0f);
        e->drv_np = 2;
        break;
    default:
        bq_lowpass(&e->drv_t1, osr, (float)d->t0 * powf((float)d->t1 / (float)d->t0, tone), 0.7f);
        break;
    }
    if (d->p2) {
        if (d->p2db == 0) bq_lowpass(&e->drv_t3, osr, (float)d->p2, 0.7f);
        else bq_peak(&e->drv_t3, osr, (float)d->p2, 0.8f, (float)d->p2db);
        e->drv_np |= 4;
    }

    float bl = (float)d->blend * 0.01f;
    bl *= (1.0f - 0.8f * dr);                     /* Klon: clean path fades as gain rises */
    e->drv_blend = bl;
#ifdef PX_FIT
    if (px_fit_drv_on) { e->drv_level = px_fit_drv_trim * 2.0f * level; return; }
#endif
    e->drv_level = DRV_TRIM[t] * 2.0f * level * powf(10.0f, DRV_DB[t] * 0.05f);   /* (v36 trim: before the amp) */
}

static int px_may_load = 1;                            /* v38: 0 in px_engine_knobs (the audio side): no flash reads there */
/* ---------------------------------------------------------------- NAM (v37) --- */
#include "px_nam_factory.h"                            /* the factory models (tools/mk_nam_factory.py) */
#if defined(__PI32V2__) && !defined(PX_NAM_POOL)       /* the pedal: the big piece in its "POOL" RAM region */
#define PX_NAM_POOL __attribute__((section(".pool")))
#endif
#ifndef PX_NAM_POOL
#define PX_NAM_POOL
#endif
/* the rings, all in one piece (83.4 KB): on the pedal in the POOL region beside the engine, so that the ordinary RAM
 * stays free (Bluetooth will need it) */
#define PX_NAM_R0 ((PXN_RING_BYTES + 7u) / 8u * 2u)
static pxn_model px_nam_m;
static int32_t px_nam_r0[PX_NAM_R0] PX_NAM_POOL __attribute__((aligned(8)));
/* no user slots unless the firmware (firmware/px_models.h) or a simulator provides them */
__attribute__((weak)) int px_nam_user(int slot, char *name12, float *level_db, float *w)
{
    (void)slot; (void)name12; (void)level_db; (void)w;
    return 0;
}
int px_nam_model(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v * PX_NAM_N / 101;
}
int px_nam_knob(int m)
{
    if (m < 0) m = 0;
    if (m >= PX_NAM_N) m = PX_NAM_N - 1;
    for (int v = 0; v <= 100; v++)                     /* the middle of the model's span of the knob */
        if (px_nam_model(v) == m) { int w = v; while (w < 100 && px_nam_model(w + 1) == m) w++; return (v + w) / 2; }
    return 0;
}
int px_nam_name(int m, char *buf)
{
    int i = 0;
    if (m >= 0 && m < PX_NAM_NFACT) {
        const char *s = PXN_FNAME[m];
        while (s[i] && i < 11) { buf[i] = s[i]; i++; }
        buf[i] = 0;
        return 1;
    }
    float lv;
    int u = m - PX_NAM_NFACT;
    if (u >= 0 && u < PX_NAM_NUSER && px_nam_user(u, buf, &lv, 0)) { buf[11] = 0; return 1; }
    buf[i++] = 'U';
    if (u + 1 >= 10) buf[i++] = (char)('0' + (u + 1) / 10);
    buf[i++] = (char)('0' + (u + 1) % 10);
    const char *e = " EMPTY";
    for (int j = 0; e[j]; j++) buf[i++] = e[j];
    buf[i] = 0;
    return 0;
}
/* the NAM ring memory as scratch (the firmware's bench screen; the model's history is lost) */
int32_t *px_nam_scratch(void)
{
    px_nam_m.ready = 0;
    return px_nam_r0;
}
/* model m into the network: factory weights straight from flash, a user slot through the ring memory (free until
 * pxn_load clears it). 0 = loaded. */
static int nam_load(int m, float *level_db)
{
    const float *w = 0;
    *level_db = 0.0f;
    if (m >= 0 && m < PX_NAM_NFACT) { w = PXN_FW[m]; *level_db = PXN_FLVL[m]; }
    else if (m >= PX_NAM_NFACT && m < PX_NAM_N) {
        char nm[12];
        float *tmp = (float *)px_nam_r0;
        if (px_nam_user(m - PX_NAM_NFACT, nm, level_db, tmp)) w = tmp;
    }
    if (!w) { px_nam_m.ready = 0; return -1; }
    return pxn_load(&px_nam_m, w, px_nam_r0, sizeof px_nam_r0, 0, 0);
}
/* start-up: the hand-written NAM routines checked (in the ring memory), then, given a clock (ticks), the ways of running
 * the network timed on the first factory model and the fastest kept (pxn_tune). The model is loaded again by the next
 * engine set. */
static uint32_t px_nam_tt[PXN_MODES];
int px_nam_check(uint32_t (*ticks)(void))
{
    typedef char room[sizeof px_nam_r0 >= PXN_TEST_WORDS * 4u ? 1 : -1];
    (void)sizeof(room);
    const int ok = pxn_selftest(px_nam_r0);
    px_fx_hand = px_fx_check();
    px_nam_m.ready = 0;
    if (ticks) {
        float lv;
        if (!nam_load(0, &lv)) pxn_tune(&px_nam_m, ticks, px_nam_tt);
        px_nam_m.ready = 0;
    }
    return ok;
}
uint32_t px_nam_tuned(int mode) { return mode >= 0 && mode < PXN_MODES ? px_nam_tt[mode] : 0; }
/* the network will run on two cores in this engine's next blocks: the caller must run px_engine_process on the core
 * that hands jobs to the other one (pxn_set_helper) */
int px_nam_dual(const px_engine *e) { return pxn_dual() && e->amp_nam && e->cur.on[PX_B_AMP] && px_nam_m.ready; }
void px_nam_helper(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void)) { pxn_set_helper(run, wait); }
void px_nam_fast(void *base, unsigned bytes) { pxn_set_fast((int32_t *)base, bytes); }
/* every model is trimmed to -20 LUFS on the DI clip (its level trim); this puts NAM + the 1x12 at GAIN noon at the fitted
 * amps' -23 LUFS (mean over the factory models, measured by tools/level_presets.py's DI) */
#define PX_NAM_REF_DB (-6.0f)
static void rc_nam(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    const uint8_t *q = p->p[PX_B_AMP];
    float sr = e->sr, lv = 0.0f;
    int m = px_nam_model(q[0]);
    e->amp_n = 0;
    e->amp_nam = 1;
    if ((m != e->nam_idx || !px_nam_m.ready) && px_may_load) {   /* (v38: never on the audio side) */
        if (nam_load(m, &lv)) { e->nam_idx = -1; return; }
        e->nam_idx = m;
        e->nam_out_g = powf(10.0f, (lv + PX_NAM_REF_DB) * 0.05f);
    }
    /* the pedal's input (hard playing ~ -17 dBFS) -> the captures' DI level (x 1 / 0.15), then GAIN +-12 dB */
    e->nam_in_g = (1.0f / 0.15f) * powf(10.0f, (f01(q[1]) - 0.5f) * 24.0f * 0.05f);
    bq_lowshelf(&e->ts_low, sr, 120.0f, (f01(q[2]) - 0.5f) * 20.0f);
    bq_peak(&e->ts_mid, sr, 800.0f, 0.7f, (f01(q[3]) - 0.5f) * 16.0f);
    bq_highshelf(&e->ts_high, sr, 2500.0f, (f01(q[4]) - 0.5f) * 20.0f);
}
/* the NAM block on n samples of e->nam_buf, in place */
PX_HOT static void nam_run(px_engine *e, float *b, int n, float trim)
{
    const float gi = e->nam_in_g, go = e->nam_out_g * trim;
    for (int k = 0; k < n; k++) b[k] *= gi;
    pxn_process(&px_nam_m, b, b, n);
    for (int k = 0; k < n; k++) b[k] *= go;
    bq_block2(&e->ts_low, &e->ts_mid, b, n);
    bq_block(&e->ts_high, b, n);
}

static void rc_amp(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr, osr = sr * PX_OVERSAMPLE;
    int t = p->type[PX_B_AMP];
    if (t >= PX_AMP_NTYPES) t = 0;
    e->amp_nam = 0;
    if (t == PX_AMP_NAM) { rc_nam(e); return; }
    const amp_v *v = &AV[t];
#ifdef PX_FIT
    if (px_fit_on) v = &px_fit_amp;                   /* the fitter's candidate (tests/fit/fitlib.c) */
#endif
    const uint8_t *q = p->p[PX_B_AMP];
    float gain = f01(q[0]);
    float userg = 0.3f + gain * gain * 3.2f;
    e->amp_n = v->n; e->amp_hard = v->hard;
    for (int i = 0; i < v->n; i++) {
        e->amp_g[i] = v->g[i] * userg;
        e->amp_bias[i] = v->bias[i];
        e->amp_boff[i] = triode(v->bias[i]);
        op_lp(&e->amp_lp[i], osr, (float)v->lp[i] * 0.85f);      /* tube stage: Miller-cap roll-off */
        op_hp(&e->amp_hp[i], osr, (float)v->hp[i]);      /* coupling cap */
    }
    bq_highpass(&e->amp_tight, sr, (float)v->tight, 0.7f);
    if (v->bright) bq_highshelf(&e->amp_bright, sr, (float)v->bright, (float)v->bright_db * (1.0f - gain));
    else bq_ident(&e->amp_bright);
    bq_lowshelf(&e->ts_low, sr, (float)v->bass, (f01(q[1]) - 0.5f) * 2.0f * (float)v->rb);
    bq_peak(&e->ts_mid, sr, (float)v->mid, (float)v->midq10 * 0.1f, (f01(q[2]) - 0.5f) * 2.0f * (float)v->rm + (float)v->scoop);
    bq_highshelf(&e->ts_high, sr, (float)v->treb, (f01(q[3]) - 0.5f) * 2.0f * (float)v->rt);
    bq_highshelf(&e->amp_pres, sr, (float)v->pres, f01(q[4]) * 7.0f - 1.0f);
    if (v->res_db) bq_lowshelf(&e->amp_res, sr, (float)v->res, (float)v->res_db); else bq_ident(&e->amp_res);
    if (v->vdb) bq_peak(&e->amp_voice, sr, (float)v->vf, 0.8f, (float)v->vdb); else bq_ident(&e->amp_voice);
    e->amp_sagk = (float)v->sag100 * 0.012f;
    e->amp_envk = 1.0f - expf(-1.0f / (0.1f * sr));     /* blocking: ~100 ms (coupling cap x grid leak), once per sample */
    e->amp_bk = (v->n >= 4 ? 0.45f : 0.35f) * e->amp_envk;   /* env is kept pre-scaled by the depth (0.45 / 0.35) */
    e->amp_pw = (float)v->pw10 * 0.1f;
    e->amp_pwn = 1.0f / px_tanh(e->amp_pw);
    {   /* v36: the level at each GAIN setting, measured (px_trim.h): every amp at the same loudness at noon, +-3 dB
         * over the whole knob, so a clean and a high-gain preset sit together */
        float gi = gain * 10.0f; int i0 = (int)gi; if (i0 > 9) i0 = 9;
        float db = AMP_LVL[t][i0] + (AMP_LVL[t][i0 + 1] - AMP_LVL[t][i0]) * (gi - (float)i0);
        e->amp_out = powf(10.0f, db * 0.05f);
    }
#ifdef PX_FIT
    if (px_fit_on) e->amp_out = px_fit_trim;
#endif
}

/* cabinet fine structure, one sample: sparse taps on a 64-sample ring + breakup resonators, h[k] = A dec^k sin(kw)
 * = b z^-1 / (1 - a1 z^-1 - a2 z^-2). The state is passed in so the level normalisation can run its own copy. */
PX_INL static inline float cab_detail(const px_cabd *c, float x, float *rb, int *rw, float (*ms)[2])
{
    int w = (*rw - 1) & 63;
    float y = 0.0f, xp;
    *rw = w;
    rb[w] = x;
    for (int k = 0; k < c->nt; k++) y += c->tg[k] * rb[(w + c->ti[k]) & 63];
    xp = rb[(w + 1) & 63];                              /* x[n-1] */
    for (int m = 0; m < PX_CABM; m++) {
        float yn = c->m[m][0] * ms[m][0] + c->m[m][1] * ms[m][1] + c->m[m][2] * xp;
        ms[m][1] = ms[m][0]; ms[m][0] = yn;
        y += yn;
    }
    return y * c->dg;
}

/* a cabinet's whole set (v38: made away from the engine, so that the main loop can make it while the audio plays: its
 * normalisation runs the impulse through 4608 samples, too long for the audio interrupt) */
typedef struct { px_bq f[8]; int n; px_cabd cd; float g; } cab_set;
static void cab_make(float sr, const px_preset_t *p, cab_set *o)
{
    int t = p->type[PX_B_CAB] % PX_CAB_COUNT;
    const cab_v *c = &CV[t];
#ifdef PX_FIT
    if (px_fit_cab_on) c = &px_fit_cab;               /* the fitter's candidate (the type still sets open back) */
#endif
    const uint8_t *q = p->p[PX_B_CAB];
    px_bq *f = o->f;
    px_cabd *cd = &o->cd;
    int n = 0;
    memset(o, 0, sizeof *o);
    if (t == PX_CAB_DI || t == PX_CAB_IR) return;
    float body = (f01(q[0]) - 0.5f) * 10.0f, bright = (f01(q[1]) - 0.5f) * 10.0f, mic = 0.65f + f01(q[2]) * 0.7f;
    bq_highpass(&f[n++], sr, (float)c->hp, (float)c->hpq10 * 0.1f);
    bq_peak(&f[n++], sr, (float)c->bump, 1.0f, (float)c->bump_db + body);
    if (c->d1) bq_peak(&f[n++], sr, (float)c->d1, 1.2f, (float)c->d1db);
    if (c->d2) bq_peak(&f[n++], sr, (float)c->d2, 1.5f, (float)c->d2db);
    if (c->p1) bq_peak(&f[n++], sr, (float)c->p1, 1.4f, (float)c->p1db + bright);
    if (c->p2) bq_peak(&f[n++], sr, (float)c->p2, 1.6f, (float)c->p2db + bright * 0.6f);
    {
        float lq = c->lpq > 0.0f ? c->lpq : 0.9f;      /* the cone's top: the knee's sharpness */
        bq_lowpass(&f[n++], sr, (float)c->lp * mic, lq);
        bq_lowpass(&f[n++], sr, (float)c->lp * mic * 1.15f, lq * (0.8f / 0.9f));
    }
    o->n = n;
    /* fine structure: what a real speaker adds to the smooth curve above. Sparse taps (direct sound, the mic's
     * off-axis comb, the back wave of open cabinets) + the cone's breakup modes as 2-pole resonators: the same
     * impulse response as v25's 128-tap FIR (A dec^k sin kw per mode), without its cost on the pedal. */
    {
        uint32_t rnd = 0x9E3779B9u * (uint32_t)(t + 1);
#define RND() (rnd = rnd * 1664525u + 1013904223u, (float)(rnd >> 8) * (1.0f / 16777216.0f))
        int nt = 0;
        float dc = 0.0f;
        /* mic: two paths across the cone (centre .. edge): an off-axis comb;
         * MIC 100 = cap centre (notch out of band), MIC 0 = cone edge (notch ~6 kHz) */
        float tau = (0.00002f + (1.0f - f01(q[2])) * 0.00006f) * sr;   /* notch from ~25 kHz (centre) to ~6 kHz (edge) */
        int ti = (int)tau; float tf = tau - (float)ti;
        cd->ti[nt] = 0; cd->tg[nt++] = 0.80f;                        /* depth: at most -4.4 dB */
        if (ti == 0) cd->tg[0] += 0.20f * (1.0f - tf); else { cd->ti[nt] = (uint8_t)ti; cd->tg[nt++] = 0.20f * (1.0f - tf); }
        cd->ti[nt] = (uint8_t)(ti + 1); cd->tg[nt++] = 0.20f * tf;
        /* open back: the rear wave comes round, inverted, ~0.8 ms later */
        if (t == PX_CAB_112JN || t == PX_CAB_212BLUE) {
            static const float BW[5] = {-0.01f, -0.025f, -0.03f, -0.025f, -0.01f};
            int d = (int)(0.0008f * sr);
            if (d > 61) d = 61;
            for (int k = 0; k < 5; k++) { cd->ti[nt] = (uint8_t)(d - 2 + k); cd->tg[nt++] = BW[k]; }
        }
        cd->nt = nt;
        for (int k = 0; k < nt; k++) dc += cd->tg[k];
        /* cone breakup: narrow resonances between 1.2 and 6 kHz, fixed per speaker (seeded by the cab): the same 10
         * draws as v25, the PX_CABM strongest kept (the weakest, < 1 dB, were not worth their cost) */
        float mf[10], mq[10], mr[10];
        for (int m = 0; m < 10; m++) {
            mf[m] = 1200.0f * powf(5.0f, RND());
            mq[m] = 5.0f + 7.0f * RND();
            mr[m] = 0.10f + 0.20f * RND();                      /* peaks of +1..+2.5 dB, a few dips */
            if (RND() < 0.25f) mr[m] = -0.6f * mr[m];
        }
        for (int m = 0; m < PX_CABM; m++) {
            int b = m;
            for (int j = m + 1; j < 10; j++) if (px_abs(mr[j]) > px_abs(mr[b])) b = j;
            float fr = mf[b], Q = mq[b], rel = mr[b];
            mf[b] = mf[m]; mq[b] = mq[m]; mr[b] = mr[m];
            float w = 2.0f * PI_F * fr / sr, dec = expf(-PI_F * fr / (Q * sr)), A = rel * 2.0f * PI_F * fr / (Q * sr);
            float a1 = 2.0f * dec * cosf(w), a2 = -dec * dec, bb = A * dec * sinf(w);   /* h[k] = A dec^k sin(kw) */
            cd->m[m][0] = a1; cd->m[m][1] = a2; cd->m[m][2] = bb;
            dc += bb / (1.0f - a1 - a2);
        }
        cd->dg = 1.0f / dc;                                     /* unity at low frequencies: the bass stays as above */
#undef RND
    }
    px_bq g[8];
    float en = 0.0f, rb[64], ms[PX_CABM][2];
    int rw = 0;
    for (int k = 0; k < n; k++) { g[k] = f[k]; bq_reset(&g[k]); }
    memset(rb, 0, sizeof rb); memset(ms, 0, sizeof ms);
    for (int i = 0; i < 4096 + 512; i++) {              /* energy normalisation (filters, then the detail): every */
        float x = (i == 0) ? 1.0f : 0.0f;               /* cab plays at the same level */
        for (int k = 0; k < n; k++) x = bq_run(&g[k], x);
        float y = cab_detail(cd, x, rb, &rw, ms);
        en += y * y;
    }
    o->g = (en > 1e-12f) ? 1.0f / sqrtf(en * 2.0f) : 1.0f;
}
/* ---- v38: USER IR, uniform partitioned convolution ----
 * The IR's PX_IR_LEN taps in IR_P partitions of IR_B (= the engine's 64-frame block). Each partition's spectrum is kept
 * (real FFT of 128: the partition, then 64 zeros); every block the last two blocks of input are transformed (overlap-
 * save), the spectra of the last IR_P input blocks are multiplied with the partitions' and summed, and the last 64
 * samples of the inverse transform are the output: no latency. A block: 2 FFTs of 128 and 16 x 65 complex multiply-adds
 * (about 420 instructions a sample on the pedal).
 * One IR at a time: its memory is here (like the NAM network's), 17 KB. */
#define IR_B 64
#define IR_F 128
#define IR_P (PX_IR_LEN / IR_B)
static float ir_H[IR_P][IR_F];                         /* the partitions' spectra (packed: [0] DC, [1] Nyquist, re/im) */
static float ir_X[IR_P][IR_F];                         /* the last IR_P input blocks' spectra (a ring, newest at ir_pos) */
static float ir_in[IR_F], ir_Y[IR_F];                  /* the last two input blocks; the sum of the products */
static int ir_pos, ir_ok;
static float fft_w[64];                                /* e^(2 pi i m / 64), m = 0..31: cos, sin */
static float rft_c[33], rft_s[33];                     /* cos, sin of 2 pi k / 128, k = 0..32 */
static uint8_t fft_rev[64];
static int fft_ready;
static void fft_init(void)
{
    for (int m = 0; m < 32; m++) { fft_w[2 * m] = cosf(2.0f * PI_F * (float)m / 64.0f); fft_w[2 * m + 1] = sinf(2.0f * PI_F * (float)m / 64.0f); }
    for (int k = 0; k <= 32; k++) { rft_c[k] = cosf(2.0f * PI_F * (float)k / 128.0f); rft_s[k] = sinf(2.0f * PI_F * (float)k / 128.0f); }
    for (int i = 0; i < 64; i++) { int r = 0; for (int b = 0; b < 6; b++) if (i >> b & 1) r |= 1 << (5 - b); fft_rev[i] = (uint8_t)r; }
    fft_ready = 1;
}
static void fft64(float *z, float sg)                  /* in place, 64 complex (re, im): sg -1 forward, +1 inverse (unscaled) */
{
    for (int i = 0; i < 64; i++) {
        const int r = fft_rev[i];
        if (r > i) { float t = z[2 * i]; z[2 * i] = z[2 * r]; z[2 * r] = t; t = z[2 * i + 1]; z[2 * i + 1] = z[2 * r + 1]; z[2 * r + 1] = t; }
    }
    for (int len = 2, tw = 32; len <= 64; len <<= 1, tw >>= 1) {
        const int h = len >> 1;
        for (int j = 0; j < h; j++) {
            const float wr = fft_w[2 * j * tw], wi = sg * fft_w[2 * j * tw + 1];
            for (int i = j; i < 64; i += len) {
                float *a = z + 2 * i, *b = z + 2 * (i + h);
                const float tr = b[0] * wr - b[1] * wi, ti = b[0] * wi + b[1] * wr;
                b[0] = a[0] - tr; b[1] = a[1] - ti;
                a[0] += tr; a[1] += ti;
            }
        }
    }
}
/* real FFT of 128 in place: x[0..127] -> [0] X0, [1] X64, [2k], [2k+1] Xk (k = 1..63) */
static void rfft128(float *x)
{
    fft64(x, -1.0f);                                   /* Z = FFT of z[n] = x[2n] + i x[2n+1] */
    const float z0r = x[0], z0i = x[1];
    for (int k = 1; k < 32; k++) {
        const int m = 64 - k;
        const float ar = x[2 * k], ai = x[2 * k + 1], br = x[2 * m], bi = x[2 * m + 1];
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai - bi);       /* E = (Z[k] + conj Z[64-k]) / 2 */
        const float orr = 0.5f * (ai + bi), oi = -0.5f * (ar - br);     /* O = (Z[k] - conj Z[64-k]) / 2i */
        const float cr = rft_c[k], ci = -rft_s[k];                     /* W^k = e^(-2 pi i k / 128) */
        const float tr = orr * cr - oi * ci, ti = orr * ci + oi * cr;
        x[2 * k] = er + tr; x[2 * k + 1] = ei + ti;                    /* X[k] = E + W^k O */
        x[2 * m] = er - tr; x[2 * m + 1] = ti - ei;                    /* X[64-k] = conj(E - W^k O) */
    }
    x[65] = -x[65];                                    /* X[32] = conj Z[32] */
    x[0] = z0r + z0i; x[1] = z0r - z0i;
}
/* its inverse, times 128 (the factor is in the partitions' spectra) */
static void irfft128(float *x)
{
    const float x0 = x[0], x64 = x[1];
    for (int k = 1; k < 32; k++) {
        const int m = 64 - k;
        const float ar = x[2 * k], ai = x[2 * k + 1], br = x[2 * m], bi = x[2 * m + 1];
        const float er = ar + br, ei = ai - bi;                        /* 2 E = X[k] + conj X[64-k] */
        const float dr = ar - br, di = ai + bi;                        /* D = X[k] - conj X[64-k] */
        const float cr = rft_c[k], ci = rft_s[k];                      /* conj W^k */
        const float orr = dr * cr - di * ci, oi = dr * ci + di * cr;   /* 2 O = D conj W^k */
        x[2 * k] = er - oi; x[2 * k + 1] = ei + orr;                   /* Z[k] = E + i O */
        x[2 * m] = er + oi; x[2 * m + 1] = orr - ei;                   /* Z[64-k] = conj E + i conj O */
    }
    x[64] = 2.0f * x[64]; x[65] = -2.0f * x[65];       /* Z[32] = conj X[32] (x2, as the others) */
    x[0] = x0 + x64; x[1] = x0 - x64;
    fft64(x, 1.0f);                                    /* z = 64 x IFFT(Z): x[2n] = Re, x[2n+1] = Im */
}
/* the spectra of an IR (PX_IR_LEN taps), its level set as the modelled cabinets' (energy 1/2) */
static void ir_load_h(const float *h)
{
    static float b[IR_F];
    float en = 0.0f;
    if (!fft_ready) fft_init();
    for (int i = 0; i < PX_IR_LEN; i++) en += h[i] * h[i];
    const float g = (en > 1e-12f ? 1.0f / sqrtf(en * 2.0f) : 0.0f) * (1.0f / 128.0f);
    for (int q = 0; q < IR_P; q++) {
        for (int i = 0; i < IR_B; i++) { b[i] = h[q * IR_B + i] * g; b[IR_B + i] = 0.0f; }
        rfft128(b);
        for (int i = 0; i < IR_F; i++) ir_H[q][i] = b[i];
    }
}
static void ir_clear(void) { memset(ir_X, 0, sizeof ir_X); memset(ir_in, 0, sizeof ir_in); ir_pos = 0; }
/* one block of 64, in place */
PX_HOT __attribute__((noinline)) static void ir_run(float *xb)
{
    float *X = ir_X[ir_pos], *Y = ir_Y;
    for (int i = 0; i < IR_B; i++) { ir_in[i] = ir_in[IR_B + i]; ir_in[IR_B + i] = xb[i]; }
    for (int i = 0; i < IR_F; i++) X[i] = ir_in[i];
    rfft128(X);
    /* bin by bin, the sum over the partitions in registers: partition q meets the input block q blocks back, ring
     * slot ir_pos - q (q = 0..ir_pos), then from the ring's end (slot IR_P - 1 down to ir_pos + 1) */
    const int n1 = ir_pos + 1, n2 = IR_P - n1;
    for (int k = 0; k < IR_F; k += 2) {
        const float *A = &ir_X[ir_pos][k], *H = &ir_H[0][k];
        float yr = 0.0f, yi = 0.0f;
        if (k == 0) {                                  /* DC and Nyquist: real */
            for (int q = 0; q < IR_P; q++) {
                const float *a = &ir_X[q <= ir_pos ? ir_pos - q : IR_P - 1 - (q - n1)][0];
                yr += a[0] * H[0]; yi += a[1] * H[1];
                H += IR_F;
            }
            Y[0] = yr; Y[1] = yi;
            continue;
        }
        for (int q = 0; q < n1; q++) {
            const float ar = A[0], ai = A[1], hr = H[0], hi = H[1];
            yr += ar * hr; yr -= ai * hi; yi += ar * hi; yi += ai * hr;
            A -= IR_F; H += IR_F;
        }
        A = &ir_X[IR_P - 1][k];
        for (int q = 0; q < n2; q++) {
            const float ar = A[0], ai = A[1], hr = H[0], hi = H[1];
            yr += ar * hr; yr -= ai * hi; yi += ar * hi; yi += ai * hr;
            A -= IR_F; H += IR_F;
        }
        Y[k] = yr; Y[k + 1] = yi;
    }
    irfft128(Y);
    for (int i = 0; i < IR_B; i++) xb[i] = Y[IR_B + i];
    ir_pos = ir_pos + 1 < IR_P ? ir_pos + 1 : 0;
}
/* the slots: the firmware's (firmware/px_irs.h), or none */
#ifndef PX_IR_USER_GIVEN                               /* (a test that includes this file gives its own) */
__attribute__((weak)) int px_ir_user(int slot, char *name12, float *h) { (void)slot; (void)name12; (void)h; return 0; }
#endif
int px_ir_slot(int v) { if (v < 0) v = 0; if (v > 100) v = 100; return v * PX_IR_N / 101; }
int px_ir_knob(int s)
{
    if (s < 0) s = 0;
    if (s >= PX_IR_N) s = PX_IR_N - 1;
    for (int v = 0; v <= 100; v++)
        if (px_ir_slot(v) == s) { int w = v; while (w < 100 && px_ir_slot(w + 1) == s) w++; return (v + w) / 2; }
    return 0;
}
int px_ir_name(int s, char *buf)
{
    if (s >= 0 && s < PX_IR_N && px_ir_user(s, buf, 0)) { buf[11] = 0; return 1; }
    const char *e = " EMPTY";
    int i = 0;
    buf[i++] = 'I'; buf[i++] = 'R'; buf[i++] = (char)('1' + (s < 0 ? 0 : s % 9));
    for (int k = 0; e[k] && i < 11; k++) buf[i++] = e[k];
    buf[i] = 0;
    return 0;
}
static void rc_cab_ir(px_engine *e)
{
    const uint8_t *q = e->cur.p[PX_B_CAB];
    const float sr = e->sr;
    const int s = px_ir_slot(q[0]);
    e->cab_n = 0; e->cab_firon = 0;
    if ((s != e->ir_slot || !ir_ok) && px_may_load) {  /* (a new slot is a full change: never on the audio side) */
        char nm[12];
        float *h = &ir_X[0][0];                        /* (scratch: cleared after) */
        if (!fft_ready) fft_init();
        ir_ok = px_ir_user(s, nm, h);
        if (ir_ok) ir_load_h(h);
        ir_clear();
        e->ir_slot = s;
    }
    bq_highpass(&e->ir_lo, sr, ir_lo_hz(q[1]), 0.7071f);
    const float hi = ir_hi_hz(q[2]);
    if (hi >= sr * 0.45f) bq_ident(&e->ir_hi); else bq_lowpass(&e->ir_hi, sr, hi, 0.7071f);
}

/* px_engine_prepare's store: the last set made, and what it was made for */
static cab_set px_cab_st;
static struct { int ok; float sr; uint8_t type, q[3]; } px_cab_key;
static int cab_key_is(float sr, const px_preset_t *p)
{
    return px_cab_key.ok && px_cab_key.sr == sr && px_cab_key.type == p->type[PX_B_CAB] &&
           !memcmp(px_cab_key.q, p->p[PX_B_CAB], 3);
}
static void rc_cab(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    if (p->type[PX_B_CAB] % PX_CAB_COUNT == PX_CAB_IR) { rc_cab_ir(e); return; }
    static cab_set o;                                  /* (off the stack: 400 bytes) */
    const cab_set *c = &o;
    if (cab_key_is(e->sr, p)) c = &px_cab_st; else cab_make(e->sr, p, &o);
    const int n = c->n;
    if (!n) { e->cab_n = 0; e->cab_firon = 0; return; }
    if (!e->cab_firon) { memset(e->cab_rb, 0, sizeof e->cab_rb); memset(e->cab_ms, 0, sizeof e->cab_ms); e->cab_rw = 0; }
    e->cd = c->cd;
    e->cab_firon = 1;
    e->cab_g = c->g;
    for (int k = 0; k < n; k++) {                      /* new coefficients, the state kept (reset if the chain changed) */
        px_bq f = c->f[k];
        if (e->cab_n == n) { f.z1 = e->cab[k].z1; f.z2 = e->cab[k].z2; } else bq_reset(&f);
        e->cab[k] = f;
    }
    e->cab_n = n;
}

static void rc_eq(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_EQ];
    int t = p->type[PX_B_EQ] % PX_EQ_COUNT;
#define EQDB(k) ((f01(q[k]) - 0.5f) * 24.0f)
    if (t == PX_EQ_5BAND) {
        bq_lowshelf(&e->eq[0], sr, 100.0f, EQDB(0));
        bq_peak(&e->eq[1], sr, 400.0f, 1.0f, EQDB(1));
        bq_peak(&e->eq[2], sr, 1000.0f, 1.0f, EQDB(2));
        bq_peak(&e->eq[3], sr, 2500.0f, 1.0f, EQDB(3));
        bq_highshelf(&e->eq[4], sr, 6000.0f, EQDB(4));
        e->eq_n = 5;
    } else if (t == PX_EQ_GEQ) {                       /* the 5-band graphic of the Californian lead amps */
        bq_peak(&e->eq[0], sr, 80.0f, 1.4f, EQDB(0));
        bq_peak(&e->eq[1], sr, 240.0f, 1.4f, EQDB(1));
        bq_peak(&e->eq[2], sr, 750.0f, 1.4f, EQDB(2));
        bq_peak(&e->eq[3], sr, 2200.0f, 1.4f, EQDB(3));
        bq_peak(&e->eq[4], sr, 6600.0f, 1.4f, EQDB(4));
        e->eq_n = 5;
    } else {
        bq_lowshelf(&e->eq[0], sr, 100.0f, EQDB(0));
        bq_peak(&e->eq[1], sr, 150.0f * powf(33.0f, f01(q[1])), 0.4f + f01(q[3]) * 5.6f, EQDB(2));
        bq_highshelf(&e->eq[2], sr, 5000.0f, EQDB(4));
        e->eq_n = 3;
    }
#undef EQDB
}

static void rc_mod(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_MOD];
    int t = p->type[PX_B_MOD] % PX_MOD_COUNT;
    float r = f01(q[0]);
    e->mod_fb = 0.0f; e->mod_mix = 1.0f;
    switch (t) {
    case PX_MOD_CHORUS:                                /* bucket-brigade chorus: triangle LFO, dark wet path */
        e->mod_inc = (0.1f + r * r * 6.0f) / sr;
        e->mod_base = 0.006f * sr; e->mod_sweep = (0.0005f + 0.0055f * f01(q[1])) * sr;
        e->mod_mix = f01(q[2]);
        bq_lowpass(&e->mod_lp, sr, 6600.0f, 0.7f);
        break;
    case PX_MOD_DIMENSION: {
        static const float RATE[4] = {0.25f, 0.25f, 0.5f, 0.5f}, DEP[4] = {0.0005f, 0.0009f, 0.0007f, 0.0012f};
        int m = (int)(r * 3.99f);
        e->mod_inc = RATE[m] / sr; e->mod_base = 0.006f * sr; e->mod_sweep = DEP[m] * sr;
        e->mod_mix = f01(q[1]);
        bq_lowpass(&e->mod_lp, sr, 8000.0f, 0.7f);
    } break;
    case PX_MOD_FLANGER:
        e->mod_inc = (0.05f + r * r * 4.95f) / sr;
        e->mod_sweep = f01(q[1]) * 0.008f * sr;
        e->mod_base = (0.0003f + f01(q[2]) * 0.0047f) * sr;
        e->mod_fb = f01(q[3]) * 0.92f;
        e->mod_mix = f01(q[4]);
        break;
    case PX_MOD_PHASE90:                               /* 4 identical JFET stages, no feedback (script) */
        e->mod_inc = (0.1f + r * r * 7.9f) / sr;
        e->ph_lo = 60.0f; e->ph_hi = 300.0f + f01(q[1]) * 1700.0f;
        e->mod_fb = 0.0f;
        break;
    case PX_MOD_STONE:
        e->mod_inc = (0.05f + r * r * 7.0f) / sr;
        e->mod_fb = f01(q[1]) * 0.7f;
        e->ph_lo = 100.0f; e->ph_hi = 400.0f + f01(q[2]) * 3100.0f;
        break;
    case PX_MOD_UNIVIBE:                               /* 4 stages with staggered caps, light-bulb LFO */
        e->mod_inc = (0.5f + r * r * 11.5f) / sr;
        e->mod_depth = f01(q[1]);
        e->mod_mix = f01(q[2]);
        e->uv_k[0] = 1.0f; e->uv_k[1] = 0.068f; e->uv_k[2] = 31.9f; e->uv_k[3] = 3.19f;
        e->uv_up = onepole_k(sr, 0.006f); e->uv_dn = onepole_k(sr, 0.03f);
        break;
    case PX_MOD_OPTOTREM: {
        float hz = 1.0f + r * r * 9.0f;
        e->mod_inc = hz / sr; e->mod_depth = f01(q[1]);
        e->trem_att = onepole_k(sr, 0.005f);
        e->trem_rel = onepole_k(sr, 0.2f - (hz - 1.0f) * 0.0167f);
    } break;
    case PX_MOD_BIASTREM:
        e->mod_inc = (1.0f + r * r * 11.0f) / sr; e->mod_depth = f01(q[1]); e->mod_shape = f01(q[2]);
        break;
    case PX_MOD_HARMTREM:
        e->mod_inc = (1.0f + r * r * 9.0f) / sr; e->mod_depth = f01(q[1]);
        bq_lowpass(&e->mod_lp, sr, 300.0f + f01(q[2]) * 900.0f, 0.6f);
        break;
    case PX_MOD_ROTARY:
        e->rot_ht = (q[0] < 50) ? 0.83f : 6.7f;
        e->rot_dt = (q[0] < 50) ? 0.67f : 5.7f;
        if (e->rot_hs <= 0.0f) { e->rot_hs = e->rot_ht; e->rot_ds = e->rot_dt; }
        e->rot_hk = onepole_k(sr, 1.0f); e->rot_dk = onepole_k(sr, 4.0f);
        e->mod_depth = f01(q[1]); e->rot_bal = f01(q[2]);
        bq_lowpass(&e->mod_lp, sr, 800.0f, 0.7f);
        break;
    default:                                           /* vibrato: wet only */
        e->mod_inc = (2.0f + r * r * 12.0f) / sr;
        e->mod_base = 0.005f * sr; e->mod_sweep = f01(q[1]) * 0.004f * sr;
        break;
    }
}

static void rc_delay(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr * 0.5f;                           /* v40: the line's rate (half rate, as the stock delays): ticks */
    const float lpmax = 0.45f * sr;                    /* (the filters in the loop stay below its Nyquist) */
    const uint8_t *q = p->p[PX_B_DELAY];
    int t = p->type[PX_B_DELAY] % PX_DLY_COUNT;
    float len = dly_ms(t, q[0]) * 0.001f * sr;
    if (len > (float)(PX_DLY_N - 600)) len = (float)(PX_DLY_N - 600);
    if (e->dly_len < 1.0f || e->dly_type != t) { e->dly_len = len; e->rev_cnt = 0; e->rev_start = e->dly_w; }
    e->dly_type = t;
    e->dly_tgt = len;
    e->dly_fb = f01(q[1]) * 0.95f;
    e->dly_satn = 1.0f;
    e->dly_mix = f01(q[2]);
    e->dly_wow = 0.0f; e->dly_flut = 0.0f; e->dly_sat = 0.0f; e->dly_q = 0.0f; e->dly_mod = 0.0f;
    e->dly_inc = 0.7f / sr; e->dly_inc2 = 8.0f / sr;
    float tone = f01(q[3]), lp;
    /* (depths in ticks: half the samples they were at the full rate, the same times) */
    switch (t) {
    case PX_DLY_DIGITAL: case PX_DLY_DUAL:
        lp = 3000.0f + tone * 13000.0f;
        bq_lowpass(&e->dly_lp, sr, lp < lpmax ? lp : lpmax, 0.7f); bq_highpass(&e->dly_hp, sr, 40.0f, 0.7f); break;
    case PX_DLY_ANALOG:                                /* bucket brigade: dark repeats that saturate */
        bq_lowpass(&e->dly_lp, sr, 1500.0f + tone * 2000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 80.0f, 0.7f);
        e->dly_sat = 1.0f; e->dly_mod = 0.75f; break;
    case PX_DLY_MEMORY:                                /* analog with modulated repeats */
        bq_lowpass(&e->dly_lp, sr, 2800.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 80.0f, 0.7f);
        e->dly_sat = 1.0f; e->dly_inc = (0.5f + f01(q[3]) * 4.5f) / sr; e->dly_mod = 1.0f + f01(q[4]) * 0.004f * sr; break;
    case PX_DLY_TAPE:                                  /* tape echo: wow 0.7 Hz + flutter 8 Hz */
        bq_lowpass(&e->dly_lp, sr, 4000.0f + tone * 4000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 120.0f, 0.7f);
        e->dly_sat = 2.0f; e->dly_wow = 0.5f + f01(q[4]) * (len * 0.004f + 3.0f); e->dly_flut = f01(q[4]) * 1.5f; break;
    case PX_DLY_SPACE: {                               /* 3 playback heads 1:2:3 */
        static const uint8_t HM[7] = {1, 2, 4, 3, 6, 5, 7};
        int h = HM[(int)(tone * 6.99f)], nh = (h & 1) + ((h >> 1) & 1) + ((h >> 2) & 1);
        e->dly_heads = h; e->dly_hn = 1.0f / sqrtf((float)nh); e->dly_hf = 1.0f / (float)nh;
        bq_lowpass(&e->dly_lp, sr, 4500.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 150.0f, 0.7f);
        e->dly_sat = 2.0f; e->dly_wow = 0.5f + f01(q[4]) * (len * 0.003f + 2.5f); e->dly_flut = f01(q[4]) * 1.25f;
    } break;
    case PX_DLY_REVERSE:
        bq_lowpass(&e->dly_lp, sr, 9000.0f < lpmax ? 9000.0f : lpmax, 0.7f); bq_highpass(&e->dly_hp, sr, 60.0f, 0.7f);
        e->rev_xf = 1.0f / (0.01f * sr); break;
    default:                                           /* lo-fi: few bits, narrow band, wobbly */
        bq_lowpass(&e->dly_lp, sr, 4000.0f - tone * 2000.0f, 0.9f); bq_highpass(&e->dly_hp, sr, 300.0f, 0.7f);
        e->dly_q = powf(2.0f, 11.0f - tone * 6.0f); e->dly_wow = 2.0f + tone * 4.0f; e->dly_inc = 0.5f / sr; e->dly_sat = 1.5f; break;
    }
    if (e->dly_sat > 0.0f) {                           /* saturating loop: tops out at 1.08 = self-oscillation, like the real units */
        e->dly_sat = 1.0f + e->dly_sat * 0.5f;         /* drive into the loop clipper */
        e->dly_satn = 1.0f / e->dly_sat;
        float f = f01(q[1]);
        e->dly_fb = f < 0.85f ? f * 0.95f : 0.8075f + (f - 0.85f) * 1.817f;
    }
}

/* the reverb's memory, laid out for type t (only when the type changes: the lines are cleared) */
static void rv_layout(px_engine *e, int t, float srs)
{
    const rv_def *v = &RVD[t];
    int n[13];
    float f = 1.0f;
    e->rv_alg = t == PX_REV_PLATE;
    for (int pass = 0; pass < 2; pass++) {
        float k = e->rv_alg ? srs * f * (44100.0f / 29761.0f) : srs * f * v->size;
        float kd = srs * f * (44100.0f / 29761.0f) * (e->rv_alg ? 1.0f : (v->size < 1.0f ? 0.4f + 0.6f * v->size : 1.0f));
        int pre = (int)(v->pre_ms * 0.001f * 44100.0f * srs) + 1, span = (int)(v->er_ms * 0.001f * 44100.0f * srs);
        e->rv_pd = pre;
        e->rv_ner = v->er > 0.0f ? 4 : 0;
        for (int j = 0; j < 4; j++) { e->rv_ert[j] = pre + (int)(ER_T[j] * (float)span); e->rv_erg[j] = ER_G[j] * v->er; }
        n[0] = pre + span + 2;
        for (int i = 0; i < 4; i++) { n[1 + i] = (int)((float)RV_DIF[i] * kd) + 1; e->rv_dg[i] = RV_DIFG[i]; }
        e->rv_mdep = (e->rv_alg ? 16.0f * (44100.0f / 29761.0f) : v->mod) * srs;
        int ext = (int)(2.0f * e->rv_mdep) + 3;          /* room for the modulation and the interpolation */
        for (int i = 0; i < 8; i++) {
            float d = (float)(e->rv_alg ? PL_LEN[i] : RV_FDN[i]) * k;
            int mod = e->rv_alg ? (i == 0 || i == 4) : i < 4;
            e->rv_d[5 + i] = d;
            n[5 + i] = (int)d + (mod ? ext : 0);
        }
        int tot = 0;
        for (int u = 0; u < 13; u++) tot += n[u] + 1;
        if (tot <= PX_RV_M) break;
        f = (float)PX_RV_M / (float)tot * 0.98f;           /* (above 44.1 kHz only) shorter lines */
    }
    int b = 0;
    for (int u = 0; u < 13; u++) { e->rv_b[u] = b; e->rv_n[u] = n[u]; b += n[u] + 1; }   /* a unit lives n + 1 slots */
    for (int j = 0; j < 14; j++) {
        int tp = (int)((float)PL_TAP[j] * srs * (44100.0f / 29761.0f));
        e->rv_tap[j] = tp < n[PL_TU[j]] ? tp : n[PL_TU[j]] - 1;
    }
    memset(e->rv_m, 0, sizeof e->rv_m);
    for (int i = 0; i < 8; i++) e->rv_s[i] = 0.0f;
    e->rv_fl = e->rv_fr = 0.0f;
    e->rv_inc1 = (e->rv_alg ? 1.0f : 0.53f) / (srs * 44100.0f);
    e->rv_inc2 = 0.79f / (srs * 44100.0f);
    e->rv_lay = t + 1;
}
#ifndef SHIM_K
#define SHIM_K 0.8f                                    /* shimmer: octave fed back (v35: about as much as v34) */
#endif
static void rc_reverb(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_REVERB];
    int t = p->type[PX_B_REVERB] % PX_REV_COUNT;
    const rv_def *v = &RVD[t];
    float d = f01(q[0]), tone = f01(q[1]);
    e->rv_shim = 0.0f; e->rv_gate = 0; e->sp_on = 0;
    if (t == PX_REV_SPRING) { e->sp_on = 1; e->sp_a = 0.5f + f01(q[3]) * 0.25f; }
    if (t == PX_REV_SHIMMER) e->rv_shim = f01(q[3]) * SHIM_K;
    if (t == PX_REV_GATED) { e->rv_gate = 1; e->rv_hold = (0.15f + d * 0.45f) * sr; e->rv_gate_rel = onepole_k(sr, 0.02f); }
    e->rv_sr = sr * 0.5f;                              /* the tank runs at half rate (tails need no more than 11 kHz) */
    if (e->rv_lay != t + 1) rv_layout(e, t, e->rv_sr / 44100.0f);
    float rt = v->rt0 + d * v->rt1;
    sr = e->rv_sr;
    if (e->rv_alg) {                                   /* plate: 4 decay gains per trip round the tank */
        float loop = 0.0f;
        for (int i = 0; i < 8; i++) loop += e->rv_d[5 + i];
        loop += e->rv_mdep * 2.0f;
        e->rv_dec = powf(10.0f, -3.0f * loop / (4.0f * rt * sr));
        float dd = e->rv_dec + 0.15f;
        e->rv_dd2 = dd < 0.25f ? 0.25f : (dd > 0.5f ? 0.5f : dd);
        e->rv_damp = 0.05f + (1.0f - tone) * 0.3f;
        e->rv_bw = 0.9995f;
    } else {                                           /* FDN: decay time per line at low frequencies and at 4 kHz */
        float rth = rt * (v->hf0 + v->hf1 * tone), c = cosf(2.0f * PI_F * 4000.0f / sr);
        for (int i = 0; i < 8; i++) {
            float L = e->rv_d[5 + i] + (i < 4 ? e->rv_mdep : 0.0f);
            float g = powf(10.0f, -3.0f * L / (rt * sr)), r = powf(10.0f, -3.0f * L / (rth * sr)) / g;
            /* one pole g (1 - p) / (1 - p z^-1): |H(4 kHz)| / H(0) = r  ->  (1-r^2) p^2 - 2 (1 - r^2 c) p + (1-r^2) = 0 */
            float r2 = r * r, A = 1.0f - r2, B = 1.0f - r2 * c, pl = 0.0f;
            if (A > 1e-6f) pl = (B - sqrtf(B * B - A * A)) / A;
            e->rv_p[i] = pl;
            e->rv_g[i] = g * (1.0f - pl);
        }
    }
    e->rv_in = 0.25f;
    e->rv_out = v->out;
    e->rv_mix = f01(q[2]);
    /* v40: the low cut after the tank runs at its rate (the stock reverbs' post too); TONE stays at the full rate */
    bq_highpass(&e->rv_hp, sr, t == PX_REV_SPRING ? 120.0f : 50.0f, 0.7f);
    sr = e->sr;
    if (t == PX_REV_SPRING) bq_lowpass(&e->rv_tone, sr, 2500.0f + tone * 2500.0f, 0.8f);
    else bq_lowpass(&e->rv_tone, sr, 3000.0f + tone * 9000.0f, 0.7f);
    e->sh_k = onepole_k(sr, 1.0f / (2.0f * PI_F * 5000.0f));
    e->sh_hk = onepole_k(sr, 1.0f / (2.0f * PI_F * 300.0f));
}


/* ---- SYN: synth / instrument ---- */
static void syn_voice(px_engine *e, int v, float st, float W, float lvl)
{   /* delay-line shifter voice: ratio r = 2^(st/12) -> the read tap's delay changes by (1 - r) per sample */
    float r = expf(st * 0.05776227f);
    e->syn_inc[v] = (1.0f - r) / W; e->syn_W[v] = W; e->syn_lvl[v] = lvl;
    if (v >= e->syn_nv) e->syn_nv = v + 1;
}
static float opk(float sr, float hz) { float k = 2.0f * PI_F * hz / sr; return k > 0.9f ? 0.9f : k; }   /* one-pole */
static void rc_syn(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_SYN];
    int t = p->type[PX_B_SYN] % PX_SYN_COUNT;
    float a = f01(q[0]), b = f01(q[1]), c = f01(q[2]), d = f01(q[3]), f = f01(q[4]);
    if (t != e->syn_type) {                            /* new instrument: start from a quiet state */
        e->syn_type = t; e->syn_kg = 0.0f; e->syn_key = 0; e->syn_ff = 1.0f; e->syn_ff2 = 1.0f; e->syn_sq = 0.0f;
        for (int i = 0; i < 4; i++) e->syn_lad[i] = 0.0f;
        for (int i = 0; i < 6; i++) { e->syn_cs[i] = 0.0f; e->syn_cp[i] = 0; }
        for (int i = 0; i < 6; i++) bq_reset(&e->syn_f[i]);
        memset(e->syn_buf, 0, sizeof e->syn_buf); memset(e->syn_cb, 0, sizeof e->syn_cb);
        e->syn_f0 = 0.0f; e->syn_per = 0.0f; e->syn_p1 = e->syn_p2 = 0.0f;
        e->pad_capr = -1; e->pad_act = -1; e->pad_k = 0; e->pad_y0 = e->pad_y1 = 0.0f; e->pad_attp = 0;
        e->pad_env = e->pad_slow = 0.0f;
        for (int r = 0; r < 3; r++) { e->pad_g[r] = e->pad_gt[r] = 0.0f; for (int g = 0; g < PX_PAD_NG; g++) e->pad_gt0[r][g] = (float)g / (float)PX_PAD_NG; }
        e->pad_seen_p = e->pad_press; e->pad_seen_s = e->pad_stop;   /* presses made before: forgotten */
        for (int i = 0; i < 2; i++) { bq_reset(&e->pad_aa[i]); bq_reset(&e->pad_tone[i]); }
    }
    e->syn_nv = 0; e->syn_dry = 0.0f; e->syn_d8 = 0.0f; e->syn_out = 1.0f; e->syn_nf = 0; e->syn_plvl = 0.0f;
    e->syn_clk = 0.0f; e->syn_cdep[0] = e->syn_cdep[1] = 0.0f;
    e->syn_ea = onepole_k(sr, 0.001f); e->syn_er = onepole_k(sr, 0.03f); e->syn_sr2 = onepole_k(sr, 0.15f);
    e->syn_ea2 = onepole_k(sr, 0.02f);
    e->syn_thr = 0.003f;
    e->syn_ka = onepole_k(sr, 0.003f); e->syn_kr = onepole_k(sr, 0.02f);
    e->syn_lpk = opk(sr, 3000.0f); e->syn_hpk = 0.0f;
    e->syn_glk = onepole_k(sr, 0.012f); e->syn_fdk = expf(-1.0f / (0.35f * sr));
    e->syn_pk = expf(-1.0f / (0.3f * sr));
    switch (t) {
    case PX_SYN_ORGAN: {                               /* drawbars: 16' = -12, 8' = the guitar, 4' = +12, perc = +19 */
        float n = 1.0f / (0.35f + a + b + c);
        syn_voice(e, 0, -12.0f, 1400.0f, a * n * 1.3f);
        syn_voice(e, 1, 12.0f, 1000.0f, c * n);
        e->syn_d8 = b * n;
        if (d > 0.0f) { syn_voice(e, 2, 19.0f, 700.0f, 0.0f); e->syn_plvl = d * n * 1.6f; }
        e->syn_clk = 0.08f; e->syn_dry = f; e->syn_out = 1.1f;
    } break;
    case PX_SYN_CHURCH: {                              /* pipe organ: 16' 8' 4' 2', slow speech, long release */
        float n = 1.0f / (0.35f + a + b + c + d);
        syn_voice(e, 0, -12.0f, 1400.0f, a * n * 1.3f);
        syn_voice(e, 1, 12.0f, 1000.0f, c * n);
        syn_voice(e, 2, 24.0f, 800.0f, d * n * 0.8f);
        e->syn_d8 = b * n;
        e->syn_ka = onepole_k(sr, 0.06f); e->syn_kr = onepole_k(sr, 0.45f);
        e->syn_lpk = opk(sr, 2200.0f); e->syn_dry = f; e->syn_out = 1.1f;
        e->syn_cdep[0] = 0.0006f * sr; e->syn_cdep[1] = 0.0f;          /* a little "celeste" movement */
        e->syn_linc[0] = 0.4f / sr; e->syn_linc[1] = 5.0f / sr; e->syn_cbase = 0.008f * sr;
    } break;
    case PX_SYN_STRINGS: case PX_SYN_CHOIR: {          /* tape replay / string machine: swell + octave + ensemble */
        float oct = t == PX_SYN_STRINGS ? c : d;
        e->syn_ka = onepole_k(sr, syn_att_ms(a) * 0.001f); e->syn_kr = onepole_k(sr, syn_rel_ms(b) * 0.001f);
        float n = 1.0f / (1.0f + oct * 0.8f + 0.3f);
        e->syn_d8 = n;
        syn_voice(e, 0, 12.0f, 1000.0f, oct * 0.8f * n);
        syn_voice(e, 1, -12.0f, 1400.0f, 0.3f * n);
        e->syn_cdep[0] = 0.0015f * sr; e->syn_cdep[1] = 0.00025f * sr;   /* ensemble: slow + fast vibrato */
        e->syn_linc[0] = 0.6f / sr; e->syn_linc[1] = 6.0f / sr; e->syn_cbase = 0.007f * sr;
        if (t == PX_SYN_STRINGS) {
            e->syn_lpk = opk(sr, 1200.0f + d * d * 6000.0f);
            e->syn_out = 0.62f;
        } else {                                       /* vowel: "ooh" (0) .. "aah" (100) */
            float f1 = 300.0f + c * 430.0f, f2 = 870.0f + c * 220.0f, f3 = 2240.0f + c * 200.0f;
            bq_bandpass(&e->syn_f[0], sr, f1, 3.0f); bq_bandpass(&e->syn_f[1], sr, f2, 5.0f);
            bq_bandpass(&e->syn_f[2], sr, f3, 7.0f); e->syn_nf = 3;
            e->syn_lpk = opk(sr, 4000.0f);
            e->syn_out = 0.96f;
        }
        e->syn_dry = f;
    } break;
    case PX_SYN_MONO:                                  /* one note: tracker -> oscillator -> 24 dB ladder */
        bq_lowpass(&e->syn_f[0], sr, 110.0f, 0.54f); bq_lowpass(&e->syn_f[1], sr, 110.0f, 1.31f);
        bq_highpass(&e->syn_f[2], sr, 50.0f, 0.7f);   /* no DC / rumble in the tracker */
        e->syn_wave = a; e->syn_mul = syn_oct(q[1]) < 0 ? 0.5f : (syn_oct(q[1]) > 0 ? 2.0f : 1.0f);
        e->syn_cut = 60.0f * powf(150.0f, c); e->syn_res = d * 0.92f; e->syn_fenv = 0.0f; e->syn_ea2 = f;
        e->syn_ka = onepole_k(sr, 0.002f); e->syn_kr = onepole_k(sr, 0.06f);
        e->syn_out = 0.27f;
        break;
    case PX_SYN_PWM:                                   /* comparator square, its width moving: no tracking delay */
        bq_lowpass(&e->syn_f[0], sr, 600.0f, 0.7f); bq_highpass(&e->syn_f[1], sr, 70.0f, 0.7f);
        e->syn_pw0 = (a - 0.5f) * 0.9f; e->syn_pwd = 0.35f;
        e->syn_linc[0] = (0.1f + b * b * 7.9f) / sr;
        e->syn_cut = 80.0f * powf(100.0f, c); e->syn_res = d * 0.9f; e->syn_ea2 = 0.3f;
        e->syn_ka = onepole_k(sr, 0.002f); e->syn_kr = onepole_k(sr, 0.05f);
        e->syn_dry = 1.0f - f; e->syn_out = f * 0.27f;
        break;
    case PX_SYN_BASS:                                  /* dividers: -1 and -2 octaves, filter swept by the attack */
        bq_lowpass(&e->syn_f[0], sr, 400.0f, 0.7f); bq_lowpass(&e->syn_f[1], sr, 400.0f, 0.7f);
        e->syn_cut = 60.0f * powf(40.0f, c); e->syn_res = 0.45f; e->syn_ea2 = d;
        e->syn_wave = a; e->syn_pwd = b;
        e->syn_fdk = expf(-1.0f / (0.25f * sr));
        e->syn_ka = onepole_k(sr, 0.002f); e->syn_kr = onepole_k(sr, 0.06f);
        e->syn_dry = f; e->syn_out = 0.22f;
        break;
    case PX_SYN_12STR: {                               /* octave strings (+12, detuned, thin) + unison doubling */
        float cents = b * 15.0f;
        syn_voice(e, 0, 12.0f + cents * 0.01f, 1000.0f, a * 0.85f);
        e->syn_hpk = opk(sr, 150.0f + c * c * 1500.0f);
        e->syn_cdep[0] = 0.0004f * sr * b; e->syn_linc[0] = 0.35f / sr; e->syn_cbase = 0.006f * sr;
        e->syn_dry = 1.0f; e->syn_out = 0.35f * b;     /* unison copy level */
    } break;
    case PX_SYN_ACOUSTIC: {                            /* magnetic pickup -> flat-top: body, top, sparkle, air */
        float fb = 135.0f - a * 45.0f;                 /* body size: parlour .. jumbo */
        bq_highpass(&e->syn_f[0], sr, 70.0f, 0.7f);
        bq_peak(&e->syn_f[1], sr, fb, 1.3f, 2.0f + b * 8.0f);
        bq_peak(&e->syn_f[2], sr, 230.0f, 1.2f, 2.5f);
        bq_peak(&e->syn_f[3], sr, 480.0f, 1.0f, -5.0f);
        bq_peak(&e->syn_f[4], sr, 3200.0f, 1.0f, -4.0f);   /* the pickup's own resonance, taken back out */
        bq_highshelf(&e->syn_f[5], sr, 5000.0f, -2.0f + c * 12.0f);
        e->syn_nf = 6; e->syn_dry = 0.0f; e->syn_out = 0.67f; e->syn_sym = d * 0.35f;
    } break;
    case PX_SYN_SITAR: {                               /* jawari buzz + 6 sympathetic strings tuned to KEY */
        static const int8_t ST[6] = {0, 7, 12, 16, 14, 9};
        float root = 130.81f * expf((float)syn_key_idx(q[0]) * 0.05776227f);   /* C3 .. B3 */
        int base = 0;
        for (int i = 0; i < 6; i++) {
            float fr = root * expf((float)ST[i] * 0.05776227f);
            int L = (int)(sr / fr + 0.5f); if (L > 340) L = 340; if (L < 8) L = 8;
            e->syn_cl[i] = L; e->syn_c0[i] = base; base += L;
            if (e->syn_cp[i] >= L) e->syn_cp[i] = 0;
        }
        e->syn_buzz = b; e->syn_sym = c * 1.2f;
        e->syn_cfb = 0.985f + d * 0.0135f; e->syn_cdamp = 0.35f;
        bq_bandpass(&e->syn_f[0], sr, 2800.0f, 1.3f); bq_bandpass(&e->syn_f[1], sr, 500.0f, 0.7f);
        e->syn_dry = 1.0f; e->syn_out = 0.7f;
    } break;
    case PX_SYN_PAD: {                                 /* a chord held on a footswitch: grains at sr / 8 */
        const float sd = sr / (float)PX_PAD_DEC;
        bq_lowpass(&e->pad_aa[0], sr, 2200.0f, 0.5412f); bq_lowpass(&e->pad_aa[1], sr, 2200.0f, 1.3066f);
        const float fc = pad_tone_hz(c);
        bq_lowpass(&e->pad_tone[0], sr, fc, 0.5412f); bq_lowpass(&e->pad_tone[1], sr, fc, 1.3066f);
        e->pad_ka = onepole_k(sd, pad_att_s(a) / 3.0f);      /* ~95% of the way in ATTACK */
        e->pad_kr = onepole_k(sd, pad_rel_s(b) / 3.0f);
        e->pad_kf = onepole_k(sd, 0.01f);                    /* a slice dropped to record into: 30 ms */
        e->pad_mot = d * 0.0087f;                            /* 15 cents */
        e->pad_mix = f * 2.0f * PX_PAD_NORM;
        e->syn_dry = 1.0f;
    } break;
    default: {                                         /* baritone: the whole guitar a few semitones down */
        syn_voice(e, 0, (float)syn_bari_st(q[0]), 1400.0f, c);
        bq_lowshelf(&e->syn_f[0], sr, 180.0f, b * 6.0f); bq_highshelf(&e->syn_f[1], sr, 3500.0f, -b * 4.0f);
        e->syn_nf = 2; e->syn_dry = 1.0f - c; e->syn_out = 1.0f;
    } break;
    }
}

static void px_recalc(px_engine *e)
{
    float sr = e->sr, osr = sr * PX_OVERSAMPLE;
    rc_dyn(e);
    rc_pitch(e);
    rc_sfx(e);
    (void)osr;                                         /* (v38: the half-band filters have fixed coefficients) */
    /* v37: before a NAM amp the drive runs at x2 (the network does not alias; x4 cost 18% of the CPU) */
    e->drv_os = (e->cur.on[PX_B_AMP] && e->cur.type[PX_B_AMP] == PX_AMP_NAM) ? 2 : PX_OVERSAMPLE;
    rc_drive(e);
    rc_amp(e);
    rc_cab(e);
    rc_eq(e);
    rc_mod(e);
    rc_delay(e);
    rc_reverb(e);
    rc_syn(e);
    /* v36: each type's level at noon, measured (px_trim.h): switching a block on keeps the loudness */
    const px_preset_t *p = &e->cur;
    static const float *const DB[PX_NBLK] = {0, CMP_DB, PIT_DB, SFX_DB, 0, 0, CAB_DB, 0, MOD_DB, DLY_DB, REV_DB, SYN_DB};
    static const uint8_t NT[PX_NBLK] = {1, PX_CMP_COUNT, PX_PIT_COUNT, PX_SFX_COUNT, PX_DRV_COUNT, 1, PX_CAB_COUNT, 1,
                                        PX_MOD_COUNT, PX_DLY_COUNT, PX_REV_COUNT, PX_SYN_COUNT};
    for (int b = 0; b < PX_NBLK; b++)
        e->btr[b] = DB[b] ? powf(10.0f, DB[b][p->type[b] % NT[b]] * 0.05f) : 1.0f;
    /* preset LEVEL (v36): -15..+15 dB, 0.3 dB a step, kept in the gate block's spare 5th setting */
    e->patch_g = powf(10.0f, ((float)p->p[PX_B_GATE][PX_PATCH_LEVEL] - 50.0f) * 0.3f * 0.05f);
}

void px_engine_init(px_engine *e, float sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->oct_ff = 1.0f; e->oct_ff2 = 1.0f;
    e->dly_type = -1;
    e->syn_type = -1;
    e->nam_idx = -1;
    e->ir_slot = -1;
    px_nam_m.ready = 0;
    px_preset_t p; px_preset_factory(&p, 0);
    px_engine_set(e, &p);
}
static void wl_clear(px_engine *e);
void px_engine_set(px_engine *e, const px_preset_t *p)
{
    e->cur = *p;
    if (px_order_valid(p->order)) for (int i = 0; i < PX_NBLK; i++) e->ord[i] = p->order[i];
    else px_order_default(e->ord);
    px_recalc(e);
    e->run = e->cur;                                   /* v38: nothing gliding, nothing fading */
    wl_clear(e);                                       /* v39: the lanes start empty (the output fades in) */
    e->gl_left = 0;
    for (int b = 0; b < PX_NBLK; b++) { e->bf[b] = e->bfa[b] = p->on[b] ? 1.0f : 0.0f; e->bfd[b] = 0; }
}

/* ---- v38: changes without clicks (px_dsp.h) ---- */
#define PX_GLIDE_TICKS 16                              /* knobs: the new values in 16 blocks of 64 frames (23 ms) */
#define PX_FADE_TICKS 16                               /* a block on / off: faded over the same time */
/* the settings that glide: gains, mixes, depths, delay times (each a plain float used as it is in the processing; the
 * filters' coefficients change at once, as they always did: a small step in a filter does not click) */
#define GLF_(f) (uint32_t)offsetof(px_engine, f)
static const uint32_t GL_F[] = {
    GLF_(cmp_make), GLF_(cmp_mix), GLF_(cmp_k),
    GLF_(oct_dry), GLF_(oct_s1), GLF_(oct_s2), GLF_(oct_up), GLF_(ps_dry), GLF_(ps_lvl[0]), GLF_(ps_lvl[1]),
    GLF_(lo_dry), GLF_(lo_lvl), GLF_(lo_g), GLF_(lo_gk), GLF_(lo_gn), GLF_(lo_c),   /* LOW OCTAVE */
    GLF_(sfx_mix), GLF_(sfx_c), GLF_(sfx_d),
    GLF_(syn_dry), GLF_(syn_out), GLF_(syn_d8), GLF_(syn_plvl), GLF_(syn_lvl[0]), GLF_(syn_lvl[1]), GLF_(syn_lvl[2]),
    GLF_(drv_gain), GLF_(drv_level), GLF_(drv_blend), GLF_(drv_mix), GLF_(drv_shelf_lo),
    GLF_(amp_g[0]), GLF_(amp_g[1]), GLF_(amp_g[2]), GLF_(amp_g[3]), GLF_(amp_out), GLF_(nam_in_g),
    GLF_(cab_g),
    GLF_(mod_mix), GLF_(mod_depth), GLF_(mod_fb), GLF_(mod_base), GLF_(mod_sweep), GLF_(mod_shape), GLF_(ph_lo),
    GLF_(ph_hi), GLF_(rot_bal),
    GLF_(dly_mix), GLF_(dly_fb),
    GLF_(rv_mix), GLF_(rv_out), GLF_(rv_shim),
    GLF_(patch_g),
};
#undef GLF_
#define GL_N ((int)(sizeof GL_F / sizeof GL_F[0]))
typedef char px_gl_room[GL_N <= PX_GL_MAX ? 1 : -1];
#define GLP(e, i) ((float *)((char *)(e) + GL_F[i]))

static int nam_model_of(const px_preset_t *p) { return px_nam_model(p->p[PX_B_AMP][0]); }
int px_engine_change(const px_engine *e, const px_preset_t *p)
{
    const px_preset_t *c = &e->cur;
    uint8_t od[PX_NBLK];
    const uint8_t *o = p->order;
    if (!px_order_valid(o)) { px_order_default(od); o = od; }
    if (memcmp(o, e->ord, PX_NBLK)) return PX_CH_FULL;
    int k = 0;
    const int nam = c->type[PX_B_AMP] == PX_AMP_NAM;
    for (int b = 0; b < PX_NBLK; b++) {
        if (c->type[b] != p->type[b]) return PX_CH_FULL;
        if (c->on[b] != p->on[b]) {
            if (b == PX_B_AMP && nam) return PX_CH_FULL;    /* the NAM amp sets how the blocks share the two cores */
            k = 1;
        }
        if (memcmp(c->p[b], p->p[b], PX_NPAR)) {
            if (b == PX_B_AMP && nam && nam_model_of(c) != nam_model_of(p)) return PX_CH_FULL;   /* another network */
            if (b == PX_B_CAB && c->type[b] == PX_CAB_IR && px_ir_slot(c->p[b][0]) != px_ir_slot(p->p[b][0]))
                return PX_CH_FULL;                     /* another IR (read from the flash) */
            k = 1;
        }
    }
    return k ? PX_CH_KNOBS : PX_CH_NONE;
}
void px_engine_prepare(const px_engine *e, const px_preset_t *p)
{
    if (cab_key_is(e->sr, p)) return;
    px_cab_key.ok = 0;
    cab_make(e->sr, p, &px_cab_st);
    px_cab_key.sr = e->sr; px_cab_key.type = p->type[PX_B_CAB]; memcpy(px_cab_key.q, p->p[PX_B_CAB], 3);
    px_cab_key.ok = 1;
}
void px_engine_knobs(px_engine *e, const px_preset_t *p)
{
    float was[GL_N];
    for (int i = 0; i < GL_N; i++) was[i] = *GLP(e, i);
    const px_preset_t old = e->cur;
    e->cur = *p;
    px_may_load = 0;                                   /* (nothing read from the flash here: see px_engine_change) */
    if (px_order_valid(p->order)) for (int i = 0; i < PX_NBLK; i++) e->ord[i] = p->order[i];
    for (int b = 0; b < PX_NBLK; b++) {
        if (!memcmp(old.p[b], p->p[b], b == PX_B_CAB ? 3 : PX_NPAR)) continue;   /* (CAB: 3 settings) */
        switch (b) {
        case PX_B_GATE: case PX_B_COMP: rc_dyn(e); break;
        case PX_B_PITCH: rc_pitch(e); break;
        case PX_B_SFX: rc_sfx(e); break;
        case PX_B_DRIVE: rc_drive(e); break;
        case PX_B_AMP: rc_amp(e); break;
        case PX_B_CAB: rc_cab(e); break;
        case PX_B_EQ: rc_eq(e); break;
        case PX_B_MOD: rc_mod(e); break;
        case PX_B_DELAY: rc_delay(e); break;
        case PX_B_REVERB: rc_reverb(e); break;
        default: rc_syn(e); break;
        }
    }
    px_may_load = 1;
    e->patch_g = powf(10.0f, ((float)p->p[PX_B_GATE][PX_PATCH_LEVEL] - 50.0f) * 0.3f * 0.05f);
    int any = 0;
    for (int i = 0; i < GL_N; i++) {                   /* from where they were (mid-glide too) to the new values */
        float *v = GLP(e, i);
        e->gl_t[i] = *v;
        if (*v != was[i]) any = 1;
        *v = was[i];
    }
    e->gl_left = any ? PX_GLIDE_TICKS : 0;
    if (!any) for (int i = 0; i < GL_N; i++) *GLP(e, i) = e->gl_t[i];
    for (int b = 0; b < PX_NBLK; b++) {                /* blocks switched on or off: they fade */
        if (p->on[b] && (e->bfd[b] < 0 || !e->run.on[b] || e->bf[b] < 1.0f)) e->bfd[b] = 1;
        else if (!p->on[b] && e->run.on[b]) e->bfd[b] = -1;
    }
    for (int b = 0; b < PX_NBLK; b++) {                /* run: cur, the blocks still fading out kept on */
        uint8_t on = (uint8_t)(p->on[b] || e->bfd[b] < 0);
        if (on && !e->run.on[b]) e->bf[b] = e->bfa[b] = 0.0f;
        e->run.on[b] = on;
    }
    memcpy(e->run.p, p->p, sizeof p->p);
    memcpy(e->run.type, p->type, sizeof p->type);
    memcpy(e->run.order, p->order, sizeof p->order);
}
/* once per block, before it: the glides and the fades move on (called by the stage that runs first) */
static void px_tick(px_engine *e)
{
    if (e->gl_left > 0) {
        const float k = 1.0f / (float)e->gl_left;
        for (int i = 0; i < GL_N; i++) { float *v = GLP(e, i); *v += (e->gl_t[i] - *v) * k; }
        if (--e->gl_left == 0) for (int i = 0; i < GL_N; i++) *GLP(e, i) = e->gl_t[i];
    }
    for (int b = 0; b < PX_NBLK; b++) {
        e->bfa[b] = e->bf[b];
        if (!e->bfd[b]) continue;
        float f = e->bf[b] + (float)e->bfd[b] * (1.0f / (float)PX_FADE_TICKS);
        if (f >= 1.0f) { f = 1.0f; e->bfd[b] = 0; }
        else if (f <= 0.0f) { f = 0.0f; e->bfd[b] = 0; e->run.on[b] = 0; }
        e->bf[b] = f;
    }
}

/* ========================================================== processing ===== */
static inline float clip_run(const px_engine *e, float d)
{
    float a, y;
    switch (e->drv_clip) {
    case CL_SOFT: return px_tanh(d);
    case CL_ASYM: return px_tanh(d + e->drv_bias) - e->drv_off;
    case CL_HARD:
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return d < 0.0f ? -y : y;
    case CL_GE: return d / (1.0f + px_abs(d));
    case CL_LED:
        a = px_abs(d); y = a >= 1.0f ? 1.0f : a * (1.5f - 0.5f * a * a);
        return d < 0.0f ? -y : y;
    case CL_MOS:
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return 0.5f * (px_tanh(d) + (d < 0.0f ? -y : y));
    case CL_MUFF:                                      /* two cascaded clipping stages */
        d = px_tanh(d) * 3.0f;
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return d < 0.0f ? -y : y;
    case CL_FUZZ:                                      /* starved transistor: asymmetric, optional gate */
        if (e->drv_gate > 0.0f) { a = px_abs(d); if (a < e->drv_gate) d *= a / e->drv_gate; }
        d += e->drv_bias;
        return d >= 0.0f ? px_tanh(d) : 0.7f * px_tanh(1.4f * d);
    case CL_OCTA:                                      /* full-wave rectifier: octave up, then fuzz */
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return 0.7f * px_tanh(1.5f * a) + 0.3f * (d < 0.0f ? -y : y);
    default: return px_tanh(d * 0.4f) * 2.5f;          /* clean boost */
    }
}
static inline float drive_pre(px_engine *e, float x)   /* base rate */
{
    x = bq_run(&e->drv_hp, x);
    if (e->drv_pre & 2) x = bq_run(&e->drv_lp, x);
    if (e->drv_pre & 4) x = bq_run(&e->drv_mid, x);
    if (e->drv_pre & 1) x = x * e->drv_shelf_lo + bq_run(&e->drv_shelf, x);
    return x;
}
PX_INL static inline float drive_run(px_engine *e, float x)   /* oversampled */
{
    float d = clip_run(e, x * e->drv_gain) + e->dn;
    if (e->drv_dcb) { e->drv_dcs += (d - e->drv_dcs) * e->drv_dck; d -= e->drv_dcs; }
    if (e->drv_tm == TM_MUFF) {
        float lo = bq_run(&e->drv_t1, d), hi = bq_run(&e->drv_t2, d);
        d = lo + (hi - lo) * e->drv_mix;
    } else {
        d = bq_run(&e->drv_t1, d);
        if (e->drv_np & 2) d = bq_run(&e->drv_t2, d);
    }
    if (e->drv_np & 4) d = bq_run(&e->drv_t3, d);
    return d * e->drv_level + x * e->drv_blend;
}
/* ---- v38: oversampling by polyphase IIR half-band filters (as the stock firmware does: two all-pass branches, each
 * section y = c (x - y1) + x1, one multiply; the branches run at the lower rate). x4 = two x2 stages: stage 1 (44.1 <->
 * 88.2 kHz) passes 0..20 kHz and stops 24.1..44.1 kHz by 78 dB with 6 sections; stage 2 (88.2 <-> 176.4 kHz) has 20..68.2
 * kHz to turn and does it with 2 sections (66 dB). Coefficients: Laurent de Soras' design (HIIR), tools/halfband.py. The
 * 4th-order Butterworth filters of v37 stopped 24..30 kHz by only 6..13 dB: that much of what the clipping made up there
 * came back down as aliasing. */
static const float HB1[6] = {0.062889286f, 0.224015205f, 0.424695820f, 0.616864442f, 0.782113520f, 0.927573355f};
static const float HB2[2] = {0.132097462f, 0.574998436f};
/* One branch at a time (each branch is a chain of its own: 3 sections in stage 1, 1 in stage 2), so that its state and
 * coefficients stay in registers through the loop (on the pedal the FPU works in the 16 general registers) */
static void hb_br3(float *xs, float *ys, float c0, float c1, float c2, const float *in, float *out, int n)
{   /* out[2i] (stride 2) = the branch on in[2i] (stride 2): x2 up and down both take every other sample */
    float x0 = xs[0], x1 = xs[1], x2 = xs[2], y0 = ys[0], y1 = ys[1], y2 = ys[2];
    for (int i = 0; i < n; i++) {
        float a = in[2 * i], t;
        t = (a - y0) * c0 + x0; x0 = a; y0 = t; a = t;
        t = (a - y1) * c1 + x1; x1 = a; y1 = t; a = t;
        t = (a - y2) * c2 + x2; x2 = a; y2 = t;
        out[2 * i] = t;
    }
    xs[0] = x0; xs[1] = x1; xs[2] = x2; ys[0] = y0; ys[1] = y1; ys[2] = y2;
}
static void hb_br1(float *xs, float *ys, float c0, const float *in, float *out, int n)
{
    float x0 = xs[0], y0 = ys[0];
    for (int i = 0; i < n; i++) {
        float a = in[2 * i], t = (a - y0) * c0 + x0;
        x0 = a; y0 = t;
        out[2 * i] = t;
    }
    xs[0] = x0; ys[0] = y0;
}
/* x2 up: in[n] -> out[2n]: the even branch (coefficients 0, 2, 4) makes out[2i], the odd one out[2i + 1], both from in[i]
 * (state: s->x/y[0..2] the even branch, [3..5] the odd one); x2 down: in[2i + 1] through the even branch, in[2i] through
 * the odd one, out[i] = their mean. The stride-2 reads and writes go through px_os_d. */
static float px_os_d[4 * PX_MAX_BLOCK];
static void hb1_up(px_hb *s, const float *in, float *out, int n)
{
    float *d = px_os_d;
    for (int i = 0; i < n; i++) d[2 * i] = in[i];
    hb_br3(s->x, s->y, HB1[0], HB1[2], HB1[4], d, out, n);
    hb_br3(s->x + 3, s->y + 3, HB1[1], HB1[3], HB1[5], d, out + 1, n);
}
static void hb1_down(px_hb *s, const float *in, float *out, int n)
{
    float *d = px_os_d;
    hb_br3(s->x, s->y, HB1[0], HB1[2], HB1[4], in + 1, d, n);
    hb_br3(s->x + 3, s->y + 3, HB1[1], HB1[3], HB1[5], in, d + 1, n);
    for (int i = 0; i < n; i++) out[i] = 0.5f * (d[2 * i] + d[2 * i + 1]);
}
static void hb2_up(px_hb *s, const float *in, float *out, int n)
{
    float *d = px_os_d;
    for (int i = 0; i < n; i++) d[2 * i] = in[i];
    hb_br1(s->x, s->y, HB2[0], d, out, n);
    hb_br1(s->x + 1, s->y + 1, HB2[1], d, out + 1, n);
}
static void hb2_down(px_hb *s, const float *in, float *out, int n)
{
    float *d = px_os_d;
    hb_br1(s->x, s->y, HB2[0], in + 1, d, n);
    hb_br1(s->x + 1, s->y + 1, HB2[1], in, d + 1, n);
    for (int i = 0; i < n; i++) out[i] = 0.5f * (d[2 * i] + d[2 * i + 1]);
}
#undef HB_SEC
static float px_os_m[2 * PX_MAX_BLOCK];                /* (stage 1 <-> 2) */
static void os_up(px_hb *st, const float *in, float *out, int n, int os)   /* os 2 or 4: st[0] stage 1, st[1] stage 2 */
{
    if (os == 2) { hb1_up(&st[0], in, out, n); return; }
    hb1_up(&st[0], in, px_os_m, n);
    hb2_up(&st[1], px_os_m, out, 2 * n);
}
static void os_down(px_hb *st, const float *in, float *out, int n, int os)   /* st[2] stage 2, st[3] stage 1 */
{
    if (os == 2) { hb1_down(&st[3], in, out, n); return; }
    hb2_down(&st[2], in, px_os_m, 2 * n);
    hb1_down(&st[3], px_os_m, out, n);
}

/* drive_run over n oversampled samples in place, stage by stage (the same operations as drive_run sample by sample);
 * the anti-denormal offset follows the base-rate sample (os samples each) */
static float px_os_t[PX_OVERSAMPLE * PX_MAX_BLOCK], px_os_h[PX_OVERSAMPLE * PX_MAX_BLOCK], px_os_u[PX_OVERSAMPLE * PX_MAX_BLOCK];
static void drive_block(px_engine *e, float *u, int n, int os)   /* (static buffers: off the interrupt's stack) */
{
    float *t = px_os_t, *h = px_os_h;
    for (int i = 0; i < n; i++) {
        const float dn = ((i / os) & 1) ? 1e-15f : -1e-15f;
        float d = clip_run(e, u[i] * e->drv_gain) + dn;
        if (e->drv_dcb) { e->drv_dcs += (d - e->drv_dcs) * e->drv_dck; d -= e->drv_dcs; }
        t[i] = d;
    }
    if (e->drv_tm == TM_MUFF) {
        memcpy(h, t, (unsigned)n * sizeof(float));
        bq_block(&e->drv_t1, t, n);
        bq_block(&e->drv_t2, h, n);
        for (int i = 0; i < n; i++) t[i] = t[i] + (h[i] - t[i]) * e->drv_mix;
    } else if (e->drv_np & 2) {
        bq_block2(&e->drv_t1, &e->drv_t2, t, n);
    } else bq_block(&e->drv_t1, t, n);
    if (e->drv_np & 4) bq_block(&e->drv_t3, t, n);
    for (int i = 0; i < n; i++) u[i] = t[i] * e->drv_level + u[i] * e->drv_blend;
}
PX_INL static inline float amp_stage(px_engine *e, int i, float x, int upd)
{
    x = op_run(&e->amp_hp[i], x);
    float u = x * e->amp_g[i] + e->amp_bias[i] - e->amp_env[i];
    if (upd) e->amp_env[i] += (u > 0.0f ? u : 0.0f) * e->amp_bk - e->amp_env[i] * e->amp_envk;   /* grid current charges the */
    x = triode(u) - e->amp_boff[i] + e->dn;        /* coupling cap: the bias shifts with playing, then recovers */
    return op_run(&e->amp_lp[i], x);               /* (the bias swallows tiny signals: re-inject the offset) */
}
static inline float rd_frac(const float *buf, int mask, float pos)   /* pos in [0, mask+1) */
{
    int i = (int)pos; float fr = pos - (float)i;
    return buf[i] + (buf[(i + 1) & mask] - buf[i]) * fr;
}
static inline float hermite(float y0, float y1, float y2, float y3, float f)   /* 4-point cubic between y1 and y2 */
{
    float c1 = 0.5f * (y2 - y0), c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2), c2 = y0 - y1 + c1 - c3;
    return ((c3 * f + c2) * f + c1) * f + y1;
}
static inline float rd_herm(const float *buf, int mask, float pos)
{
    int i = (int)pos; float f = pos - (float)i;
    return hermite(buf[(i - 1) & mask], buf[i & mask], buf[(i + 1) & mask], buf[(i + 2) & mask], f);
}
/* 16-bit memories: values are rounded (exact decay times); once a tail is down near the last bits, they are cut
 * towards zero instead, so the loop dies out instead of holding a tiny pattern for ever (limit cycle) */
#define DL_Q 16384.0f                                  /* delay memory: 16 bits, +-2.0 */
static inline int16_t dl_q(float v, float rq)
{
    v = v * DL_Q + (v >= 0.0f ? rq : -rq);
    if (v > 32767.0f) v = 32767.0f; else if (v < -32767.0f) v = -32767.0f;
    return (int16_t)v;
}
static inline float dly_read(const int16_t *buf, int i, float f)   /* between i and i + 1 (i in 0..N-1) */
{
    int a = i - 1, b = i + 1, c = i + 2;
    if (a < 0) a += PX_DLY_N;
    if (b >= PX_DLY_N) b -= PX_DLY_N;
    if (c >= PX_DLY_N) c -= PX_DLY_N;
    return hermite((float)buf[a], (float)buf[i], (float)buf[b], (float)buf[c], f) * (1.0f / DL_Q);
}
static inline float dly_tap(const px_engine *e, float d)
{
    int di = (int)d;                                   /* whole samples apart from the fraction: no float wrap */
    float f = d - (float)di;
    int i = e->dly_w - di - 1;                         /* d = di + f behind the write: between i and i + 1, 1 - f */
    if (i < 0) i += PX_DLY_N;
    return dly_read(e->dly_buf, i, 1.0f - f);
}
static inline float mod_tap(const px_engine *e, float d)
{
    float pos = (float)e->mod_w - d;
    if (pos < 0.0f) pos += 2048.0f;
    return rd_herm(e->mod_buf, 2047, pos);
}
/* ---- reverb (v35) ----
 * One 16-bit ring for every line: the write pointer turns once per tick, unit u writes at (ptr + base u) and what it
 * wrote k ticks ago is at (ptr + base u + k). The tank works in the ring's units (x RV_Q): no scaling per access. */
#define RV_Q 16384.0f                                  /* reverb memory: 16 bits, +-2.0 */
#define RV_MASK (PX_RV_M - 1)
#define RV_RD(u, k) ((float)m[(p + b[u] + (k)) & RV_MASK])
static inline int16_t rv_q(float v, float rq)          /* rq: 0.5 = rounded, 0 = cut towards zero */
{
    union { float f; uint32_t u; } a, r;
    a.f = v; r.f = rq; r.u |= a.u & 0x80000000u;        /* rq with v's sign */
    v += r.f;
    v = v > 32767.0f ? 32767.0f : v;
    v = v < -32767.0f ? -32767.0f : v;
    return (int16_t)v;
}
#define RV_WR(u, v) (m[(p + b[u]) & RV_MASK] = rv_q((v), rq))
static inline float rv_lin(const int16_t *m, int at, float k)      /* fractional read, k ticks back from `at` */
{
    int ki = (int)k; float f = k - (float)ki;
    float a = (float)m[(at + ki) & RV_MASK];
    return a + ((float)m[(at + ki + 1) & RV_MASK] - a) * f;
}
/* pre-delay, early reflections, Dattorro's 4 input diffusers */
PX_INL static inline float rv_front(px_engine *e, int16_t *m, int p, const int *b, float rq, float in, float *er)
{
    RV_WR(0, in);
    float r = 0.0f;
    for (int j = 0; j < e->rv_ner; j++) r += e->rv_erg[j] * RV_RD(0, e->rv_ert[j]);
    *er = r;
    float d = RV_RD(0, e->rv_pd);
    if (e->rv_alg) { e->rv_s[0] += e->rv_bw * (d - e->rv_s[0]); d = e->rv_s[0]; }
    float t, w;                                        /* gains 0.75, 0.75, 0.625, 0.625 */
    t = RV_RD(1, e->rv_n[1]); w = d - 0.75f * t;  RV_WR(1, w); d = t + 0.75f * w;
    t = RV_RD(2, e->rv_n[2]); w = d - 0.75f * t;  RV_WR(2, w); d = t + 0.75f * w;
    t = RV_RD(3, e->rv_n[3]); w = d - 0.625f * t; RV_WR(3, w); d = t + 0.625f * w;
    t = RV_RD(4, e->rv_n[4]); w = d - 0.625f * t; RV_WR(4, w); d = t + 0.625f * w;
    return d;
}
PX_INL static inline float rv_fdn(px_engine *e, float in)
{
    int16_t *m = e->rv_m; const int p = e->rv_ptr, *b = e->rv_b; const float rq = e->rv_rq;
    float er, d = rv_front(e, m, p, b, rq, in * RV_Q, &er), y[8];
    e->rv_ph1 = wrap1(e->rv_ph1 + e->rv_inc1);
    e->rv_ph2 = wrap1(e->rv_ph2 + e->rv_inc2);
    float md = e->rv_mdep;
    y[0] = rv_lin(m, p + b[5], e->rv_d[5] + md * (1.0f + lfo_sin(e->rv_ph1)));
    y[1] = rv_lin(m, p + b[6], e->rv_d[6] + md * (1.0f + lfo_sin(wrap1(e->rv_ph1 + 0.25f))));
    y[2] = rv_lin(m, p + b[7], e->rv_d[7] + md * (1.0f + lfo_sin(e->rv_ph2)));
    y[3] = rv_lin(m, p + b[8], e->rv_d[8] + md * (1.0f + lfo_sin(wrap1(e->rv_ph2 + 0.25f))));
    y[4] = RV_RD(9, e->rv_n[9]); y[5] = RV_RD(10, e->rv_n[10]); y[6] = RV_RD(11, e->rv_n[11]); y[7] = RV_RD(12, e->rv_n[12]);
    float o = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.35355339f;
    float *s = e->rv_s;
    for (int i = 0; i < 8; i++) s[i] = e->rv_g[i] * y[i] + e->rv_p[i] * s[i];   /* decay + damping per line */
    float a0 = s[0] + s[1], a1 = s[0] - s[1], a2 = s[2] + s[3], a3 = s[2] - s[3];    /* Hadamard 8 */
    float a4 = s[4] + s[5], a5 = s[4] - s[5], a6 = s[6] + s[7], a7 = s[6] - s[7];
    float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
    float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
    const float h = 0.35355339f;
    RV_WR(5, (b0 + b4) * h + d);  RV_WR(6, (b1 + b5) * h - d);
    RV_WR(7, (b2 + b6) * h + d);  RV_WR(8, (b3 + b7) * h - d);
    RV_WR(9, (b0 - b4) * h + d);  RV_WR(10, (b1 - b5) * h - d);
    RV_WR(11, (b2 - b6) * h + d); RV_WR(12, (b3 - b7) * h - d);
    return (o + er) * (1.0f / RV_Q);
}
PX_INL static inline float rv_plate(px_engine *e, float in)
{
    int16_t *m = e->rv_m; const int p = e->rv_ptr, *b = e->rv_b; const float rq = e->rv_rq;
    float er, d = rv_front(e, m, p, b, rq, in * RV_Q, &er);
    e->rv_ph1 = wrap1(e->rv_ph1 + e->rv_inc1);
    float md = e->rv_mdep, dec = e->rv_dec, dm = e->rv_damp, g2 = e->rv_dd2, t, w;
    /* left half of the tank: modulated all-pass (-0.7), delay, damping, decay, all-pass, delay, decay */
    t = rv_lin(m, p + b[5], e->rv_d[5] + md * (1.0f + lfo_sin(e->rv_ph1)));
    w = d + e->rv_fr + 0.7f * t; RV_WR(5, w);
    float l = t - 0.7f * w;
    t = RV_RD(6, e->rv_n[6]); RV_WR(6, l);
    e->rv_s[1] = t + (e->rv_s[1] - t) * dm;
    t = RV_RD(7, e->rv_n[7]); w = e->rv_s[1] * dec - g2 * t; RV_WR(7, w);
    l = t + g2 * w;
    t = RV_RD(8, e->rv_n[8]); RV_WR(8, l);
    float lend = t * dec;
    /* right half */
    t = rv_lin(m, p + b[9], e->rv_d[9] + md * (1.0f + lfo_sin(wrap1(e->rv_ph1 + 0.25f))));
    w = d + e->rv_fl + 0.7f * t; RV_WR(9, w);
    float r = t - 0.7f * w;
    t = RV_RD(10, e->rv_n[10]); RV_WR(10, r);
    e->rv_s[2] = t + (e->rv_s[2] - t) * dm;
    t = RV_RD(11, e->rv_n[11]); w = e->rv_s[2] * dec - g2 * t; RV_WR(11, w);
    r = t + g2 * w;
    t = RV_RD(12, e->rv_n[12]); RV_WR(12, r);
    e->rv_fl = lend; e->rv_fr = t * dec;
    /* output: Dattorro's left and right taps, summed */
    const int *tp = e->rv_tap;
    float o = RV_RD(10, tp[0]) + RV_RD(10, tp[1]) - RV_RD(11, tp[2]) + RV_RD(12, tp[3]) - RV_RD(6, tp[4]) - RV_RD(7, tp[5]) - RV_RD(8, tp[6])
            + RV_RD(6, tp[7]) + RV_RD(6, tp[8]) - RV_RD(7, tp[9]) + RV_RD(8, tp[10]) - RV_RD(10, tp[11]) - RV_RD(11, tp[12]) - RV_RD(12, tp[13]);
    return o * (0.3f / RV_Q);
}
static inline float ps_voice(const float *buf, int w, float *ph, float inc, float W)
{                                                      /* plain 2-tap shifter (shimmer: smeared anyway) */
    float a = *ph + inc;
    a = wrap1(a);
    *ph = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    float pa = (float)w - (3.0f + a * W); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (3.0f + b * W); if (pb < 0.0f) pb += (float)PX_PS_N;
    float s = lfo_sin(a * 0.5f), g = s * s;            /* sin^2 / cos^2 crossfade: sums to 1 */
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}
#define PS_OFFMAX 512                                  /* one period of low E */
PX_HOT static float ps_align(const float *buf, int w, float d_ref, float d_new)
{                                                      /* SOLA: offset that puts the fading-in tap in phase */
    int r0 = w - (int)d_ref, best = 0;
    float bc = -1e30f;
    for (int pass = 0; pass < 2; pass++) {             /* coarse (every 8 offsets, every 4th sample), then fine */
        int lo = pass ? best - 7 : 0, hi = pass ? best + 7 : PS_OFFMAX, st = pass ? 1 : 8, b2 = best, js = pass ? 2 : 4;
        if (lo < 0) lo = 0;
        for (int o = lo; o <= hi; o += st) {
            int c0 = w - (int)(d_new + (float)o);
            float acc = 0.0f, en = 1e-9f;
            for (int j = 0; j < 64; j += js) {
                float x = buf[(r0 - j) & (PX_PS_N - 1)], y = buf[(c0 - j) & (PX_PS_N - 1)];
                acc += x * y; en += y * y;
            }
            acc = acc / sqrtf(en);                     /* normalised: loudness does not bias the choice */
            if (acc > bc) { bc = acc; b2 = o; }
        }
        best = b2;
    }
    return (float)best;
}
PX_INL static inline float ps_voice_al(float *buf, int w, float *ph, float *off, float inc, float W)
{
    float a0 = *ph, a = wrap1(a0 + inc);
    *ph = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    int wa = inc > 0.0f ? (a < a0) : (a > a0);                                  /* tap A restarts (silent) */
    int wb = inc > 0.0f ? (a0 < 0.5f && a >= 0.5f) : (a0 >= 0.5f && a < 0.5f);   /* tap B restarts (silent) */
    float da = 3.0f + a * W, db = 3.0f + b * W;
    if (wa) off[0] = ps_align(buf, w, db + off[1], da);
    if (wb) off[1] = ps_align(buf, w, da + off[0], db);
    float pa = (float)w - (da + off[0]); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (db + off[1]); if (pb < 0.0f) pb += (float)PX_PS_N;
    float s = lfo_sin(a * 0.5f), g = s * s;
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}
/* LOW OCTAVE's fast octave: phase halving. Two allpass chains 90 degrees apart (Olli Niemitalo's coefficients,
 * 90 +- 0.7 degrees from 30 Hz to 1 kHz at sr / 8, 3.6 ms at the low E) make the low band a turning arrow, z; the
 * octave is an arrow turning half as fast, h, kept in step by h <- unit(h + unit(z) conj(h)) every sample (the
 * half-way arrow between h and h turned as far as z turned), and given z's length. No buffer to read from: the
 * octave is as late as the filters in front of it. It follows one note: with two in the band it gives neither. */
static const float LO_HC[8] = {0.47940087f, 0.87621849f, 0.97659759f, 0.99749926f,      /* the squares, chain A */
                               0.16175850f, 0.73302893f, 0.94534970f, 0.99059916f};     /* chain B */
static inline float lo_ap(float x, float c, float *xh, float *yh)   /* y = c (x + y[n-2]) - x[n-2] */
{
    float y = c * (x + yh[1]) - xh[1];
    xh[1] = xh[0]; xh[0] = x; yh[1] = yh[0]; yh[0] = y;
    return y;
}
#define LO_REF 0.1f                                    /* COMP brings the octave towards this level */
#define LO_FLOOR 0.002f                                /* and stops raising it below this one (-54 dB) */
static float lo_fast(px_engine *e, float a)
{
    float ua = a, ub = a;                              /* the band's own level: its 90-degree pair, z's length */
    for (int i = 0; i < 4; i++) ua = lo_ap(ua, LO_HC[i], e->lo_bx[i], e->lo_by[i]);
    for (int i = 4; i < 8; i++) ub = lo_ap(ub, LO_HC[i], e->lo_bx[i], e->lo_by[i]);
    const float A = sqrtf(ub * ub + e->lo_bd * e->lo_bd);
    e->lo_bd = ua;
    e->lo_amp += (A - e->lo_amp) * (A > e->lo_amp ? e->lo_aatk : e->lo_arel);
    float t = a;                                       /* the tilt: the lowest note in the band leads the phase */
    for (int i = 0; i < e->lo_np; i++) { e->lo_t[i] += (t - e->lo_t[i]) * e->lo_tk; t = e->lo_t[i]; }
    float pa = t, pb = t;
    for (int i = 0; i < 4; i++) pa = lo_ap(pa, LO_HC[i], e->lo_ax[i], e->lo_ay[i]);
    for (int i = 4; i < 8; i++) pb = lo_ap(pb, LO_HC[i], e->lo_ax[i], e->lo_ay[i]);
    const float re = pb, im = e->lo_ad;                /* chain A a sample later: 90 degrees behind chain B */
    e->lo_ad = pa;
    const float m2 = re * re + im * im;
    float hr = e->lo_hr, hi = e->lo_hi;
    if (m2 > 1e-20f) {
        const float inv = 1.0f / sqrtf(m2), wr = re * inv, wi = im * inv;
        if (hr * hr + hi * hi < 0.5f) { hr = 1.0f; hi = 0.0f; }
        const float gr = hr + wr * hr + wi * hi, gi = hi + wi * hr - wr * hi;   /* h + unit(z) conj(h) */
        const float gn = gr * gr + gi * gi;
        if (gn > 1e-6f) { const float n = 1.0f / sqrtf(gn); hr = gr * n; hi = gi * n; }
    }
    e->lo_hr = hr; e->lo_hi = hi;
    float amp = e->lo_amp;
    if (e->lo_c > 0.0f) {                              /* COMP: amp^(1-c), towards LO_REF; closes below LO_FLOOR */
        amp *= powf(LO_REF / (amp > LO_FLOOR ? amp : LO_FLOOR), e->lo_c);
        if (e->lo_amp < LO_FLOOR) { const float x = e->lo_amp * (1.0f / LO_FLOOR); amp *= x * x; }
    }
    return amp * hr;
}


/* ---- SYN: synth / instrument, one sample ---- */
static inline float polyblep(float t, float dt)        /* band-limited step correction (saw / pulse edges) */
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float syn_saw(float ph, float dt) { return 2.0f * ph - 1.0f - polyblep(ph, dt); }
static inline float syn_ladder(px_engine *e, float in, float fc)
{   /* 4 one-poles, tanh on the feedback: a 24 dB/oct "transistor ladder" low-pass, resonant */
    if (fc > e->sr * 0.42f) fc = e->sr * 0.42f;
    float wc = PI_F * fc / e->sr, g = wc / (1.0f + wc);
    float u = px_tanh(in - e->syn_res * 4.0f * e->syn_lad[3]);
    e->syn_lad[0] += g * (u - e->syn_lad[0]);
    e->syn_lad[1] += g * (e->syn_lad[0] - e->syn_lad[1]);
    e->syn_lad[2] += g * (e->syn_lad[1] - e->syn_lad[2]);
    e->syn_lad[3] += g * (e->syn_lad[2] - e->syn_lad[3]);
    return e->syn_lad[3] * (1.0f + e->syn_res * 1.5f);
}
static inline float syn_tap(const px_engine *e, float d)   /* ensemble / doubler line, d samples back */
{
    float pos = (float)e->syn_cw - d; if (pos < 0.0f) pos += (float)PX_SYN_CB;
    return rd_frac(e->syn_cb, PX_SYN_CB - 1, pos);
}
/* key gate: a "key" goes down when the string is picked, up when it has died away; the level in between is held
 * (sustain), as on a keyboard */
static inline float syn_keys(px_engine *e, float x)
{
    float a = px_abs(x);
    e->syn_env += (a - e->syn_env) * (a > e->syn_env ? e->syn_ea : e->syn_er);
    e->syn_slow += (e->syn_env - e->syn_slow) * (e->syn_env > e->syn_slow ? e->syn_ea : e->syn_sr2);
    int on = e->syn_key ? (e->syn_env > e->syn_thr * 0.5f) : (e->syn_env > e->syn_thr);
    if (on && !e->syn_key) { e->syn_perc = 1.0f; e->syn_click = 1.0f; }
    e->syn_key = on;
    e->syn_kg += ((on ? 1.0f : 0.0f) - e->syn_kg) * (on ? e->syn_ka : e->syn_kr);
    if (e->syn_kg < 1e-6f) e->syn_kg = 0.0f;
    float g = 0.25f / (e->syn_slow + 0.004f);
    return x * (g > 12.0f ? 12.0f : g);
}
PX_INL static inline float syn_shift(px_engine *e, int v)
{
    return ps_voice_al(e->syn_buf, e->syn_w, &e->syn_ph[v], e->syn_off[v], e->syn_inc[v], e->syn_W[v]);
}
/* pitch tracker: low-passed (cut-off following the note), zero crossings with hysteresis, median of 3 periods */
static inline int syn_track(px_engine *e, float x)
{
    /* a steep fixed low-pass far below most notes: whatever the note, its fundamental comes out on top */
    float b = bq_run(&e->syn_f[1], bq_run(&e->syn_f[0], bq_run(&e->syn_f[2], x))), ab = px_abs(b);
    e->syn_sq = ab > e->syn_sq ? ab : e->syn_sq * 0.998f;
    float h = e->syn_sq * 0.2f;
    int trig = 0;
    e->syn_cnt += 1.0f;
    if (e->syn_ff < 0.0f && b > 0.0f && e->syn_prev <= 0.0f) {   /* armed (went below -h): upward zero crossing */
        e->syn_ff = 1.0f; trig = 1;
        float tc = e->syn_cnt - b / (b - e->syn_prev + 1e-9f);         /* sub-sample crossing time */
        float per = tc - e->syn_per;
        e->syn_per = tc;
        if (per > e->sr / 1500.0f && per < e->sr / 55.0f) {
            float m = per, p1 = e->syn_p1, p2 = e->syn_p2;          /* median of the last three */
            if ((p1 <= m && m <= p2) || (p2 <= m && m <= p1)) { }
            else if ((m <= p1 && p1 <= p2) || (p2 <= p1 && p1 <= m)) m = p1;
            else m = p2;
            e->syn_p2 = p1; e->syn_p1 = per;
            if (m > 0.0f) {
                e->syn_f0t = e->sr / m;
                if (e->syn_snap || e->syn_f0 <= 0.0f) { e->syn_f0 = e->syn_f0t; e->syn_snap = 0; }   /* new note */
            }
        }
        if (e->syn_cnt > 1e6f) { e->syn_cnt -= 1e6f; e->syn_per -= 1e6f; }
    } else if (e->syn_ff > 0.0f && b < -h) e->syn_ff = -1.0f;
    else if (e->syn_ff == 0.0f) e->syn_ff = 1.0f;
    e->syn_prev = b;
    return trig;
}
/* ---- SYN PAD: the chord caught on the footswitch, held as overlapping grains ---- */
#define PX_PAD_L 2048                                  /* a slice: 372 ms at sr / 8, 16 bits */
#define PX_PAD_G 882                                   /* a grain: 160 ms */
#define PX_PAD_SKIP 441                                /* after a pick attack heard while recording: 80 ms */
static inline int16_t *pad_slice(px_engine *e, int r) { return r < 2 ? e->syn_pad + r * PX_PAD_L : e->syn_pad2; }
static inline float pad_rand(px_engine *e)             /* 0 .. 1 */
{
    e->syn_rnd = e->syn_rnd * 1664525u + 1013904223u;
    return (float)(e->syn_rnd >> 9) * (1.0f / 8388608.0f);
}
/* A new grain starts where it is in phase with the grain playing loudest now (the one nearest the middle of its
 * window): 24 starts 8 samples apart from a random one (35 ms: two periods of the low E, at sr / 8), then +-4 by
 * one, compared over 48 samples. Grains out of phase beat (the level wobbled 5 dB); in phase they do not. */
static void pad_grain_new(px_engine *e, int r, int g)
{
    const int16_t *sl = pad_slice(e, r);
    const int hi = PX_PAD_L - (int)(PX_PAD_G * 1.01f) - 3;
    e->pad_gi[r][g] = 1.0f + e->pad_mot * (2.0f * pad_rand(e) - 1.0f);
    int lead = -1; float bw = 0.0f;
    for (int i = 0; i < PX_PAD_NG; i++) {
        if (i == g) continue;
        const float w = lfo_sin(e->pad_gt0[r][i] * 0.5f);
        if (w * w > bw) { bw = w * w; lead = i; }
    }
    int base = (int)(pad_rand(e) * (float)(hi - 24 * 8 - 4));
    if (lead < 0 || bw < 0.25f) { e->pad_gp[r][g] = (float)base; return; }
    float ref[48];
    const float lp = e->pad_gp[r][lead] + e->pad_gt0[r][lead] * (float)PX_PAD_G * e->pad_gi[r][lead];
    for (int j = 0; j < 48; j++) {
        int i0 = (int)(lp + (float)j * e->pad_gi[r][lead]);
        ref[j] = (float)sl[i0 < PX_PAD_L - 1 ? i0 : PX_PAD_L - 1];
    }
    int best = base; float bc = -1e30f;
    for (int pass = 0; pass < 2; pass++) {
        const int lo = pass ? best - 4 : base, n = pass ? 9 : 24, st = pass ? 1 : 8;
        int b2 = best;
        for (int k = 0; k < n; k++) {
            const int c0 = lo + k * st;
            if (c0 < 0 || c0 > hi) continue;
            float acc = 0.0f, en = 1.0f;
            for (int j = 0; j < 48; j += 2) { const float v = (float)sl[c0 + j]; acc += v * ref[j]; en += v * v; }
            acc /= sqrtf(en);
            if (acc > bc) { bc = acc; b2 = c0; }
        }
        best = b2;
    }
    e->pad_gp[r][g] = (float)best;
}
/* A caught chord still decays over its slice (a strum: about 3 dB in 372 ms), so grains from its start would play
 * louder than grains from its end: each eighth of the slice is brought to the slice's mean level, the gain moving
 * linearly between their centres. Once per catch. */
static void pad_flatten(int16_t *sl)
{
    float rm[8], mean = 0.0f;
    for (int b = 0; b < 8; b++) {
        float a = 0.0f;
        for (int i = 0; i < PX_PAD_L / 8; i++) { const float v = (float)sl[b * (PX_PAD_L / 8) + i]; a += v * v; }
        rm[b] = sqrtf(a / (float)(PX_PAD_L / 8)) + 1.0f; mean += rm[b] * 0.125f;
    }
    for (int i = 0; i < PX_PAD_L; i++) {
        float pos = ((float)i + 0.5f) / (float)(PX_PAD_L / 8) - 0.5f;
        int b0 = (int)pos; if (pos < 0.0f) { b0 = 0; pos = 0.0f; } if (b0 > 6) { b0 = 6; pos = 7.0f; }
        const float fr = pos - (float)b0;
        float g = mean / (rm[b0] + (rm[b0 + 1] - rm[b0]) * fr);
        g = g > 2.0f ? 2.0f : g < 0.5f ? 0.5f : g;
        float v = (float)sl[i] * g;
        v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
        sl[i] = (int16_t)v;
    }
}
void px_pad_press(px_engine *e) { e->pad_press++; }   /* the UI and MIDI only: the audio side never writes these */
void px_pad_stop(px_engine *e) { e->pad_stop++; }
static void pad_step(px_engine *e, float a)            /* one sample at sr / 8 */
{
    const uint32_t pp = e->pad_press, ps = e->pad_stop;
    if (ps != e->pad_seen_s) {                         /* held footswitch: everything fades over RELEASE */
        e->pad_seen_s = ps;
        for (int r = 0; r < 3; r++) { e->pad_gt[r] = 0.0f; e->pad_rk[r] = e->pad_kr; }
        e->pad_capr = -1;
    }
    if (pp != e->pad_seen_p) {                         /* a press: record the chord from now into a free slice */
        e->pad_seen_p = pp;
        if (e->pad_capr < 0) {
            int r = -1;
            for (int i = 0; i < 3; i++) if (i != e->pad_act && (r < 0 || e->pad_g[i] < e->pad_g[r])) r = i;
            e->pad_capr = r;
            e->pad_gt[r] = 0.0f; e->pad_rk[r] = e->pad_kf;   /* (if it was still fading out: dropped quickly) */
        }
        e->pad_wait = 1; e->pad_cap = 0; e->pad_skip = 0;
    }
    {   /* a pick attack: the envelope jumps over its slow average */
        const float m = a < 0.0f ? -a : a;
        e->pad_env = m > e->pad_env ? m : e->pad_env + (m - e->pad_env) * 0.006f;
        e->pad_slow += (e->pad_env - e->pad_slow) * 0.0006f;
    }
    const int att = e->pad_env > 1.8f * e->pad_slow + 0.01f, edge = att && !e->pad_attp;
    e->pad_attp = att;
    if (e->pad_capr >= 0) {
        const int r = e->pad_capr;
        if (e->pad_wait) { if (e->pad_g[r] < 1e-4f) { e->pad_g[r] = 0.0f; e->pad_wait = 0; } }
        else if (edge && e->pad_cap < PX_PAD_L / 2) { e->pad_cap = 0; e->pad_skip = PX_PAD_SKIP; }   /* strummed */
        else if (e->pad_skip > 0) e->pad_skip--;
        else {
            float v = a * 16384.0f;
            v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
            pad_slice(e, r)[e->pad_cap++] = (int16_t)v;
            if (e->pad_cap >= PX_PAD_L) {              /* caught: it swells in, the one before fades out */
                pad_flatten(pad_slice(e, r));
                if (e->pad_act >= 0) { e->pad_gt[e->pad_act] = 0.0f; e->pad_rk[e->pad_act] = e->pad_ka; }
                e->pad_act = r; e->pad_gt[r] = 1.0f; e->pad_rk[r] = e->pad_ka; e->pad_capr = -1;
                for (int g = 0; g < PX_PAD_NG; g++) { e->pad_gt0[r][g] = (float)g / (float)PX_PAD_NG; pad_grain_new(e, r, g); }
            }
        }
    }
    float y = 0.0f;
    for (int r = 0; r < 3; r++) {
        e->pad_g[r] += (e->pad_gt[r] - e->pad_g[r]) * e->pad_rk[r];
        if (e->pad_gt[r] == 0.0f && e->pad_g[r] < 1e-5f) e->pad_g[r] = 0.0f;
        if (e->pad_g[r] == 0.0f || r == e->pad_capr) continue;
        const int16_t *sl = pad_slice(e, r);
        float acc = 0.0f;
        for (int g = 0; g < PX_PAD_NG; g++) {          /* NG grains, 1/NG apart: their windows sum to NG / 2 */
            float t = e->pad_gt0[r][g];
            const float pos = e->pad_gp[r][g] + t * (float)PX_PAD_G * e->pad_gi[r][g];
            const int i0 = (int)pos;
            const float fr = pos - (float)i0, s = lfo_sin(t * 0.5f);
            acc += ((float)sl[i0] + (float)(sl[i0 + 1] - sl[i0]) * fr) * s * s;
            t += 1.0f / (float)PX_PAD_G;
            if (t >= 1.0f) { t -= 1.0f; pad_grain_new(e, r, g); }
            e->pad_gt0[r][g] = t;
        }
        y += acc * e->pad_g[r];
    }
    e->pad_y0 = e->pad_y1; e->pad_y1 = y * (2.0f / (float)PX_PAD_NG / 16384.0f);
}
static float pad_run(px_engine *e, float x)
{
    const float a = bq_run(&e->pad_aa[1], bq_run(&e->pad_aa[0], x));
    if (++e->pad_k >= PX_PAD_DEC) { e->pad_k = 0; pad_step(e, a); }
    float o = e->pad_y0 + (e->pad_y1 - e->pad_y0) * ((float)e->pad_k * (1.0f / (float)PX_PAD_DEC));
    o = bq_run(&e->pad_tone[1], bq_run(&e->pad_tone[0], o));
    return x + o * e->pad_mix + e->dn;
}
PX_INL static inline float syn_run(px_engine *e, int t, float x)
{
    float y = 0.0f;
    switch (t) {
    case PX_SYN_ORGAN: case PX_SYN_CHURCH: {
        float xs = syn_keys(e, x);
        e->syn_buf[e->syn_w] = xs;
        e->syn_lp += e->syn_lpk * (xs - e->syn_lp);
        y = e->syn_lp * e->syn_d8 + syn_shift(e, 0) * e->syn_lvl[0] + syn_shift(e, 1) * e->syn_lvl[1];
        if (e->syn_nv > 2) {
            if (t != PX_SYN_ORGAN) y += syn_shift(e, 2) * e->syn_lvl[2];
            else if (e->syn_perc > 0.002f) {           /* percussion: its voice only while it sounds */
                y += syn_shift(e, 2) * e->syn_plvl * e->syn_perc; e->syn_perc *= e->syn_pk;
            }
        }
        if (++e->syn_w >= PX_PS_N) e->syn_w = 0;
        if (e->syn_click > 1e-4f) {                    /* key click: a 2 ms burst */
            e->syn_rnd = e->syn_rnd * 1664525u + 1013904223u;
            y += ((float)(e->syn_rnd >> 8) * (2.0f / 16777216.0f) - 1.0f) * e->syn_click * e->syn_clk;
            e->syn_click *= 0.985f;
        }
        if (e->syn_cdep[0] > 0.0f) {                   /* church: slow celeste */
            e->syn_cb[e->syn_cw] = y;
            e->syn_lfo[0] = wrap1(e->syn_lfo[0] + e->syn_linc[0]);
            float w = syn_tap(e, e->syn_cbase + e->syn_cdep[0] * lfo_sin(e->syn_lfo[0]));
            if (++e->syn_cw >= PX_SYN_CB) e->syn_cw = 0;
            y = 0.6f * y + 0.6f * w;
        }
        y = px_tanh(y * 1.3f) * 0.77f;                 /* tone-wheel / pipe: no harsh peaks */
        return y * e->syn_kg * e->syn_out + x * e->syn_dry + e->dn;
    }
    case PX_SYN_STRINGS: case PX_SYN_CHOIR: {
        float xs = syn_keys(e, x);
        e->syn_buf[e->syn_w] = xs;
        y = xs * e->syn_d8 + syn_shift(e, 0) * e->syn_lvl[0] + syn_shift(e, 1) * e->syn_lvl[1];
        if (++e->syn_w >= PX_PS_N) e->syn_w = 0;
        if (e->syn_nf) y = bq_run(&e->syn_f[0], y) + 0.6f * bq_run(&e->syn_f[1], y) + 0.35f * bq_run(&e->syn_f[2], y)
                           + 0.12f * y;
        e->syn_lp += e->syn_lpk * (y - e->syn_lp);
        e->syn_hp += e->syn_lpk * (e->syn_lp - e->syn_hp);
        e->syn_cb[e->syn_cw] = e->syn_hp;
        e->syn_lfo[0] = wrap1(e->syn_lfo[0] + e->syn_linc[0]);
        e->syn_lfo[1] = wrap1(e->syn_lfo[1] + e->syn_linc[1]);
        float w = 0.0f;
        for (int i = 0; i < 3; i++) {                  /* ensemble: 3 taps, 120 degrees apart */
            float o = (float)i * 0.33333f;
            w += syn_tap(e, e->syn_cbase + e->syn_cdep[0] * lfo_sin(wrap1(e->syn_lfo[0] + o))
                            + e->syn_cdep[1] * lfo_sin(wrap1(e->syn_lfo[1] + o)));
        }
        if (++e->syn_cw >= PX_SYN_CB) e->syn_cw = 0;
        y = px_tanh(w * 0.4f) * 2.5f;
        return y * e->syn_kg * e->syn_out + x * e->syn_dry + e->dn;
    }
    case PX_SYN_MONO: {
        float a = px_abs(x);
        e->syn_env += (a - e->syn_env) * (a > e->syn_env ? e->syn_ea : e->syn_er);
        e->syn_slow += (e->syn_env - e->syn_slow) * e->syn_glk;           /* slower: re-attack detection */
        int on = e->syn_key ? (e->syn_env > e->syn_thr * 0.5f) : (e->syn_env > e->syn_thr * 1.5f);
        if (on && !e->syn_key) { e->syn_snap = 1; e->syn_p1 = e->syn_p2 = 0.0f; }   /* after a silence: no glide */
        syn_track(e, x);
        if ((on && !e->syn_key) || e->syn_env > 1.6f * e->syn_slow + e->syn_thr) e->syn_fenv = 1.0f;
        e->syn_key = on;
        e->syn_kg += ((on && e->syn_f0 > 0.0f ? 1.0f : 0.0f) - e->syn_kg) * (on ? e->syn_ka : e->syn_kr);
        if (e->syn_kg < 1e-6f) e->syn_kg = 0.0f;
        e->syn_f0 += (e->syn_f0t - e->syn_f0) * e->syn_glk;
        float fr = e->syn_f0 * e->syn_mul, dt = fr / e->sr;
        if (dt > 0.45f) dt = 0.45f;
        e->syn_oph += dt; if (e->syn_oph >= 1.0f) e->syn_oph -= 1.0f;
        float saw = syn_saw(e->syn_oph, dt), o;
        if (e->syn_wave < 0.5f) {                      /* saw -> square */
            float ph2 = e->syn_oph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
            float sq = saw - syn_saw(ph2, dt);
            o = saw + (sq - saw) * (e->syn_wave * 2.0f);
        } else {                                       /* square -> narrow pulse */
            float pw = 0.5f - (e->syn_wave - 0.5f) * 0.8f, ph2 = e->syn_oph + pw;
            if (ph2 >= 1.0f) ph2 -= 1.0f;
            o = saw - syn_saw(ph2, dt);
        }
        e->syn_fenv *= e->syn_fdk;
        float lv = 0.6f + 0.4f * (e->syn_env * 6.0f > 1.0f ? 1.0f : e->syn_env * 6.0f);
        y = syn_ladder(e, o * 0.8f, e->syn_cut * (1.0f + e->syn_ea2 * e->syn_fenv * 8.0f));
        return y * e->syn_kg * lv * e->syn_out + e->dn;
    }
    case PX_SYN_PWM: {
        float a = px_abs(x);
        e->syn_env += (a - e->syn_env) * (a > e->syn_env ? e->syn_ea : e->syn_er);
        int on = e->syn_key ? (e->syn_env > e->syn_thr * 0.5f) : (e->syn_env > e->syn_thr);
        if (on && !e->syn_key) e->syn_fenv = 1.0f;
        e->syn_key = on;
        e->syn_kg += ((on ? 1.0f : 0.0f) - e->syn_kg) * (on ? e->syn_ka : e->syn_kr);
        if (e->syn_kg < 1e-6f) e->syn_kg = 0.0f;
        float b = bq_run(&e->syn_f[1], bq_run(&e->syn_f[0], x)), ab = px_abs(b);
        e->syn_sq = ab > e->syn_sq ? ab : e->syn_sq * 0.9995f;
        e->syn_lfo[0] = wrap1(e->syn_lfo[0] + e->syn_linc[0]);
        float th = e->syn_sq * (e->syn_pw0 + e->syn_pwd * lfo_sin(e->syn_lfo[0]));
        float sq = b > th ? 1.0f : -1.0f;
        e->syn_fenv *= e->syn_fdk;
        y = syn_ladder(e, sq * 0.7f, e->syn_cut * (1.0f + e->syn_ea2 * e->syn_fenv * 4.0f));
        return y * e->syn_kg * e->syn_out + x * e->syn_dry + e->dn;
    }
    case PX_SYN_BASS: {
        float a = px_abs(x);
        e->syn_env += (a - e->syn_env) * (a > e->syn_env ? e->syn_ea : e->syn_er);
        e->syn_slow += (e->syn_env - e->syn_slow) * e->syn_glk;
        int on = e->syn_key ? (e->syn_env > e->syn_thr * 0.5f) : (e->syn_env > e->syn_thr);
        if ((on && !e->syn_key) || e->syn_env > 1.6f * e->syn_slow + e->syn_thr) e->syn_fenv = 1.0f;
        e->syn_key = on;
        e->syn_kg += ((on ? 1.0f : 0.0f) - e->syn_kg) * (on ? e->syn_ka : e->syn_kr);
        if (e->syn_kg < 1e-6f) e->syn_kg = 0.0f;
        float b = bq_run(&e->syn_f[1], bq_run(&e->syn_f[0], x)), ab = px_abs(b);
        e->syn_sq = ab > e->syn_sq ? ab : e->syn_sq * 0.9995f;
        float h = e->syn_sq * 0.2f;
        if (e->syn_prev <= 0.0f && b > h) {            /* flip-flops: -1 and -2 octaves */
            e->syn_prev = 1.0f; e->syn_ff = -e->syn_ff;
            if (e->syn_ff > 0.0f) e->syn_ff2 = -e->syn_ff2;
        } else if (e->syn_prev > 0.0f && b < -h) e->syn_prev = -1.0f;
        float s = e->syn_ff * e->syn_wave + e->syn_ff2 * e->syn_pwd;
        e->syn_fenv *= e->syn_fdk;
        y = syn_ladder(e, s * 0.6f, e->syn_cut * (1.0f + e->syn_ea2 * e->syn_fenv * 12.0f));
        return y * e->syn_kg * e->syn_out + x * e->syn_dry + e->dn;
    }
    case PX_SYN_12STR: {
        e->syn_buf[e->syn_w] = x;
        float o = syn_shift(e, 0);
        if (++e->syn_w >= PX_PS_N) e->syn_w = 0;
        e->syn_hp += e->syn_hpk * (o - e->syn_hp);     /* octave strings are thin: high-pass */
        y = x * e->syn_dry + (o - e->syn_hp) * e->syn_lvl[0];
        if (e->syn_out > 0.0f) {                       /* the unison pairs, slightly out of tune */
            e->syn_cb[e->syn_cw] = x;
            e->syn_lfo[0] = wrap1(e->syn_lfo[0] + e->syn_linc[0]);
            y += syn_tap(e, e->syn_cbase + e->syn_cdep[0] * lfo_sin(e->syn_lfo[0])) * e->syn_out;
            if (++e->syn_cw >= PX_SYN_CB) e->syn_cw = 0;
        }
        return y + e->dn;
    }
    case PX_SYN_ACOUSTIC: {
        y = x;
        for (int i = 0; i < e->syn_nf; i++) y = bq_run(&e->syn_f[i], y);
        e->syn_cb[e->syn_cw] = y;                      /* the body / room: a few early reflections */
        float r = syn_tap(e, 0.0031f * e->sr) * 0.6f - syn_tap(e, 0.0053f * e->sr) * 0.45f
                  + syn_tap(e, 0.0079f * e->sr) * 0.35f - syn_tap(e, 0.0112f * e->sr) * 0.25f;
        if (++e->syn_cw >= PX_SYN_CB) e->syn_cw = 0;
        return (y + r * e->syn_sym) * e->syn_out * 1.8f + e->dn;
    }
    case PX_SYN_SITAR: {
        float a = px_abs(x);
        e->syn_env += (a - e->syn_env) * (a > e->syn_env ? e->syn_ea : e->syn_er);
        float c = e->syn_env * (1.0f - 0.85f * e->syn_buzz);      /* the string grazes the curved bridge */
        float ex = x > c ? x - c : 0.0f;
        float bz = bq_run(&e->syn_f[0], ex) * (2.0f + e->syn_buzz * 10.0f);
        y = x + bz * e->syn_buzz;
        float in_s = bq_run(&e->syn_f[1], x) * 0.04f, sum = 0.0f;
        for (int i = 0; i < 6; i++) {                  /* sympathetic strings: damped comb resonators */
            float *bf = e->syn_buf + e->syn_c0[i];
            float r = bf[e->syn_cp[i]];
            e->syn_cs[i] = r + (e->syn_cs[i] - r) * e->syn_cdamp;
            if (e->syn_cs[i] > 4.0f) e->syn_cs[i] = 4.0f; else if (e->syn_cs[i] < -4.0f) e->syn_cs[i] = -4.0f;
            bf[e->syn_cp[i]] = in_s + e->syn_cs[i] * e->syn_cfb;
            if (++e->syn_cp[i] >= e->syn_cl[i]) e->syn_cp[i] = 0;
            sum += r;
        }
        return (y + sum * e->syn_sym) * e->syn_out + e->dn;
    }
    case PX_SYN_PAD:
        return pad_run(e, x);
    default: {                                         /* baritone */
        e->syn_buf[e->syn_w] = x;
        y = x * e->syn_dry + syn_shift(e, 0) * e->syn_lvl[0];
        if (++e->syn_w >= PX_PS_N) e->syn_w = 0;
        for (int i = 0; i < e->syn_nf; i++) y = bq_run(&e->syn_f[i], y);
        return y + e->dn;
    }
    }
}

/* ---- v38: the delay and the reverb over a block, each type's branches decided once for the block (on the pedal a taken
 * branch costs about 8 cycles: the per-sample code had 6 to 10 of them). Each call with constants is its own copy of
 * the loop; the operations and their order are the per-sample code's, so the output is the same, bit for bit. */
PX_AINL static inline void dly_loop(px_engine *e, const float *xb, float *wo, int n, const int T, const int M, const int FL,
                                    const int Q, const int S)
{
    /* v39: in a wet lane its output is heard lg frames late: the taps heard read lg frames less far back, the ones fed
     * back keep the delay time (the repeats keep their spacing). v40: the line runs at half rate, as the stock delays
     * (one tick every two samples), so lg is in ticks there */
    const float lg = e->wl_dlag * 0.5f;
    for (int k = 0; k < n; k++) {
        float x = xb[k];
        if (!e->dly_tick) {                            /* v40: the first sample of a pair: kept, the last output held */
            e->dly_xa = x; e->dly_tick = 1;
            wo[k] = e->dly_y1 * e->dly_mix;
            continue;
        }
        x = 0.5f * (e->dly_xa + x);                    /* the tick's input: the pair averaged (a zero at 22.05 kHz) */
            e->dly_len += (e->dly_tgt - e->dly_len) * 0.0008f;   /* time glides (no click); 0.0004 a sample before */
            float rd = e->dly_len, d, fsrc;
            if (M) {
                e->dly_ph = wrap1(e->dly_ph + e->dly_inc);
                rd += (e->dly_wow + e->dly_mod) * (1.0f + lfo_sin(e->dly_ph));
                if (FL) { e->dly_ph2 = wrap1(e->dly_ph2 + e->dly_inc2); rd += e->dly_flut * (1.0f + lfo_sin(e->dly_ph2)); }
            }
            if (T == PX_DLY_REVERSE) {
                int G = (int)e->dly_len;
                if (e->rev_cnt >= G) { e->rev_cnt = 0; e->rev_start = e->dly_w; }
                int rp = e->rev_start - 1 - e->rev_cnt; if (rp < 0) rp += PX_DLY_N;
                float c = (float)e->rev_cnt, wg = c * e->rev_xf, w2 = (float)(G - e->rev_cnt) * e->rev_xf;
                if (wg > w2) wg = w2;
                if (wg > 1.0f) wg = 1.0f;
                d = (float)e->dly_buf[rp] * (1.0f / DL_Q) * wg;
                e->rev_cnt++;
                fsrc = d;
            } else if (T == PX_DLY_SPACE) {
                float s = 0.0f;
                if (e->dly_heads & 1) s += dly_tap(e, rd * 0.33333f);
                if (e->dly_heads & 2) s += dly_tap(e, rd * 0.66667f);
                if (e->dly_heads & 4) s += dly_tap(e, rd);
                d = s * e->dly_hn; fsrc = s * e->dly_hf;   /* loop gain stays <= FDBK whatever the heads */
                if (lg > 0.0f) {
                    s = 0.0f;
                    if (e->dly_heads & 1) s += dly_tap(e, rd * 0.33333f - lg);
                    if (e->dly_heads & 2) s += dly_tap(e, rd * 0.66667f - lg);
                    if (e->dly_heads & 4) s += dly_tap(e, rd - lg);
                    d = s * e->dly_hn;
                }
            } else if (T == PX_DLY_DUAL) {
                float a = dly_tap(e, rd), b = dly_tap(e, rd * 0.75f);
                fsrc = a;
                if (lg > 0.0f) { a = dly_tap(e, rd - lg); b = dly_tap(e, rd * 0.75f - lg); }
                d = 0.6f * (a + 0.8f * b);
            } else {
                d = dly_tap(e, rd); fsrc = d;
                if (lg > 0.0f) d = dly_tap(e, rd - lg);
            }
            if (Q) {
                float v = fsrc * e->dly_q;
                fsrc = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->dly_q;
                if (lg > 0.0f) { v = d * e->dly_q; d = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->dly_q; }
                else d = fsrc;
            }
            float fbk = bq_run(&e->dly_hp, bq_run(&e->dly_lp, fsrc)) * e->dly_fb;
            if (S) fbk = px_tanh(fbk * e->dly_sat) * e->dly_satn;   /* unity for small signals */
            float wv = x + fbk, av = px_abs(wv);
            e->dly_env = av > e->dly_env ? av : e->dly_env * 0.9999f;   /* (0.99995 a sample before) */
            e->dly_rq = e->dly_env < 2e-4f ? 0.0f : 0.5f;
            e->dly_buf[e->dly_w] = dl_q(wv, e->dly_rq);
            if (++e->dly_w >= PX_DLY_N) e->dly_w = 0;
        {   /* v40: a gentle lift of the top (+1 dB at 5 kHz), for what the pair's average and the interpolation take off */
            const float dl = 1.15f * d - 0.15f * e->dly_dp;
            e->dly_dp = d; d = dl;
        }
        e->dly_y0 = e->dly_y1; e->dly_y1 = d; e->dly_tick = 0;
        wo[k] = 0.5f * (e->dly_y0 + d) * e->dly_mix;   /* v39: the wet part only (wo may be xb: x is read first) */
    }
}
static void dly_block(px_engine *e, const float *xb, float *wo, int n, int t)
{
    const int m = e->dly_wow > 0.0f || e->dly_mod > 0.0f, fl = e->dly_flut > 0.0f, q = e->dly_q > 0.0f, s = e->dly_sat > 0.0f;
    switch (t) {                                       /* the combinations each type sets (rc_delay) */
    case PX_DLY_DIGITAL: if (!m && !fl && !q && !s) { dly_loop(e, xb, wo, n, PX_DLY_DIGITAL, 0, 0, 0, 0); return; } break;
    case PX_DLY_DUAL:    if (!m && !fl && !q && !s) { dly_loop(e, xb, wo, n, PX_DLY_DUAL, 0, 0, 0, 0); return; } break;
    case PX_DLY_REVERSE: if (!m && !fl && !q && !s) { dly_loop(e, xb, wo, n, PX_DLY_REVERSE, 0, 0, 0, 0); return; } break;
    case PX_DLY_ANALOG:  if (m && !fl && !q && s) { dly_loop(e, xb, wo, n, PX_DLY_ANALOG, 1, 0, 0, 1); return; } break;
    case PX_DLY_MEMORY:  if (m && !fl && !q && s) { dly_loop(e, xb, wo, n, PX_DLY_MEMORY, 1, 0, 0, 1); return; } break;
    case PX_DLY_TAPE:
        if (m && !q && s) { if (fl) dly_loop(e, xb, wo, n, PX_DLY_TAPE, 1, 1, 0, 1); else dly_loop(e, xb, wo, n, PX_DLY_TAPE, 1, 0, 0, 1); return; }
        break;
    case PX_DLY_SPACE:
        if (m && !q && s) { if (fl) dly_loop(e, xb, wo, n, PX_DLY_SPACE, 1, 1, 0, 1); else dly_loop(e, xb, wo, n, PX_DLY_SPACE, 1, 0, 0, 1); return; }
        break;
    case PX_DLY_LOFI:    if (m && !fl && q && s) { dly_loop(e, xb, wo, n, PX_DLY_LOFI, 1, 0, 1, 1); return; } break;
    }
    dly_loop(e, xb, wo, n, t, m, fl, q, s);                /* (anything else: decided per sample, as before) */
}
PX_AINL static inline void rv_loop(px_engine *e, const float *xb, float *wo, int n, const int ALG, const int SP, const int SH,
                                   const int GT)
{
    for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;
        float x = xb[k];
            float inr = x * e->rv_in + e->sh_out + e->dn;
            if (SP) {                            /* spring dispersion: chirpy all-pass chain */
                float a = e->sp_a;
                for (int i = 0; i < 6; i++) { float y = -a * inr + e->sp_z[i]; e->sp_z[i] = inr + a * y; inr = y; }
            }
            float acc;
            if (!e->rv_tick) { e->rv_xa = inr; e->rv_tick = 1; acc = e->rv_y1; }
            else {                                     /* one tick of the tank every 2 samples */
                float in2 = 0.5f * (e->rv_xa + inr);
                float y = ALG ? rv_plate(e, in2) : rv_fdn(e, in2), aa = px_abs(y) + px_abs(in2);
                e->rv_ptr = (e->rv_ptr - 1) & RV_MASK;
                e->rv_env = aa > e->rv_env ? aa : e->rv_env * 0.9999f;
                /* the last bits of a tail: fade it out over ~0.5 s, then let the ring empty itself (cut towards zero) */
                if (e->rv_env < 5e-4f) e->rv_fade *= 0.9997f; else e->rv_fade = 1.0f;
                e->rv_rq = e->rv_fade < 0.03f ? 0.0f : 0.5f;
                y = bq_run(&e->rv_hp, y * e->rv_fade * e->rv_out);   /* v40: the low cut at the tank's rate, as the stock */
                e->rv_y0 = e->rv_y1; e->rv_y1 = y; e->rv_tick = 0;
                acc = 0.5f * (e->rv_y0 + y);
            }
            acc = bq_run(&e->rv_tone, acc);            /* (TONE at the full rate: it also takes the half rate's images down) */
            if (SH) {                   /* octave-up shifter in the feedback loop */
                e->sh_buf[e->sh_w] = acc;
                float s = ps_voice(e->sh_buf, e->sh_w, &e->sh_ph, -1.0f / 1024.0f, 1024.0f);
                if (++e->sh_w >= PX_PS_N) e->sh_w = 0;
                e->sh_lp += (s - e->sh_lp) * e->sh_k;
                e->sh_hp += (e->sh_lp - e->sh_hp) * e->sh_hk;   /* v35: only the upper octave goes round again */
                e->sh_out = (e->sh_lp - e->sh_hp) * e->rv_shim;
            } else e->sh_out = 0.0f;
            if (GT) {
                float a = px_abs(x);
                e->rv_genv = a > e->rv_genv ? a : a + (e->rv_genv - a) * 0.999f;
                if (e->rv_genv > 0.01f) e->rv_hold_n = (int)e->rv_hold;
                float tg = 0.0f;
                if (e->rv_hold_n > 0) { e->rv_hold_n--; tg = 1.0f; }
                e->rv_gg += (tg - e->rv_gg) * (tg > e->rv_gg ? 0.05f : e->rv_gate_rel);
                if (e->rv_gg < 1e-6f) e->rv_gg = 0.0f;
                acc *= e->rv_gg;
            }
        wo[k] = acc * e->rv_mix * 1.4f;                /* v39: the wet part only; the dry part: x * (1 - 0.4 mix) */
    }
}
static void rv_block(px_engine *e, const float *xb, float *wo, int n)
{
    const int alg = e->rv_alg != 0, sp = e->sp_on != 0, sh = e->rv_shim > 0.0f, gt = e->rv_gate != 0;
    if (!alg && !sp && !sh && !gt) rv_loop(e, xb, wo, n, 0, 0, 0, 0);          /* room, hall, cathedral, ambient */
    else if (alg && !sp && !sh && !gt) rv_loop(e, xb, wo, n, 1, 0, 0, 0);      /* plate */
    else if (!alg && sp && !sh && !gt) rv_loop(e, xb, wo, n, 0, 1, 0, 0);      /* spring */
    else rv_loop(e, xb, wo, n, alg, sp, sh, gt);                                 /* shimmer, gated (and anything else) */
}

/* ---- v39: the wet lanes (px_dsp.h). [lane][buffer][frame]: 2 KB of RAM ---- */
static float px_wl[2][2][PX_WL_N];
static void wl_clear(px_engine *e)
{
    e->wl_b = 0;
    for (int L = 0; L < 2; L++) {
        e->wl_rd[L] = e->wl_p[L] = 0;
        for (int b = 0; b < 2; b++) { e->wl_n[L][b] = 0; e->wl_w[L][b] = 0; }
    }
}
void px_wet_mode(px_engine *e, int on, int lag)
{
    wl_clear(e);
    e->wl_late = 0;
    e->wl_on = (uint8_t)(on != 0);
    e->wl_dlag = on ? (float)lag : 0.0f;
}
void px_wet_swap(px_engine *e)
{
    const int b = e->wl_b ^ 1;                         /* the buffer made wet meanwhile: the chain's now */
    e->wl_b = (uint8_t)b;
    for (int L = 0; L < 2; L++) {
        e->wl_rd[L] = e->wl_w[L][b] ? e->wl_n[L][b] : 0;
        e->wl_w[L][b] = 0; e->wl_n[L][b] = 0; e->wl_p[L] = 0;
    }
}
int px_wet_run(px_engine *e, uint32_t deadline, volatile const uint8_t *stop)
{
    const int b = e->wl_b ^ 1;                         /* the sends of the block before */
    for (int L = 0; L < 2; L++) {
        const int n = e->wl_n[L][b];
        if (!n || e->wl_w[L][b]) continue;
        const uint32_t t0 = px_clock ? px_clock() : 0u;
        float *bw = px_wl[L][b];
        const int dly_t = e->run.type[PX_B_DELAY] % PX_DLY_COUNT;
        for (int o = 0; o < n; o += 16) {
            if ((stop && *stop) || (deadline && px_clock && (int32_t)(px_clock() - deadline) > 0)) {
                e->wl_n[L][b] = (uint16_t)o;           /* the rest: silence (the lines miss it too) */
                e->wl_w[L][b] = 1;
                e->wl_late++;
                if (px_clock) e->wl_pf[L] += px_clock() - t0;
                return 0;
            }
            const int m = n - o < 16 ? n - o : 16;
            if (L) rv_block(e, bw + o, bw + o, m); else dly_block(e, bw + o, bw + o, m, dly_t);
        }
        e->wl_w[L][b] = 1;
        if (px_clock) e->wl_pf[L] += px_clock() - t0;
    }
    return 1;
}

/* part: PX_PART_ALL, or (with a NAM amp: px_nam_dual) one stage of the two-core pipeline: PRE the blocks before the
 * amp (in -> out, the network's input), NAM the network and its tone stack (out in place), POST the blocks after it
 * (in: the network's output -> out). PRE and POST touch different blocks: they may run at once on the two cores. */
PX_HOT void px_engine_part(px_engine *e, const float *in, float *out, int n, int part)
{
    if (part == PX_PART_ALL || part == PX_PART_PRE) px_tick(e);   /* v38: glides and fades move on, once a block */
    const px_preset_t *p = &e->run;                    /* v38: with the blocks still fading out */
    const float sr = e->sr;
    const int cmp_t = p->type[PX_B_COMP] % PX_CMP_COUNT;
    const int pit_t = p->type[PX_B_PITCH] % PX_PIT_COUNT;
    const int sfx_t = p->type[PX_B_SFX] % PX_SFX_COUNT;
    const int mod_t = p->type[PX_B_MOD] % PX_MOD_COUNT;
    const int dly_t = p->type[PX_B_DELAY] % PX_DLY_COUNT;
    const int drv_on = p->on[PX_B_DRIVE], amp_on = p->on[PX_B_AMP], amp_os = amp_on && e->amp_n > 0;
    const int syn_t = p->type[PX_B_SYN] % PX_SYN_COUNT;
    /* the blocks to run, in this preset's order (GATE always: it carries the anti-denormal offset) */
    uint8_t st[PX_NBLK];
    float tr[PX_NBLK];                                 /* each block's level trim */
    int ns = 0, ip_drv = -100, ip_amp = -200;
    for (int i = 0; i < PX_NBLK; i++) {
        int b = e->ord[i];
        if (b != PX_B_GATE && !p->on[b]) continue;
        if (b == PX_B_DRIVE) ip_drv = ns;
        if (b == PX_B_AMP) ip_amp = ns;
        tr[ns] = e->btr[b];
        st[ns++] = (uint8_t)b;
    }
    const float pg = e->patch_g;
    /* DRIVE right before AMP: one oversampled section for both (set A). Otherwise each its own: the amp keeps
     * set A, a drive elsewhere takes set B (set A when the amp does not oversample). */
    const int pair = drv_on && amp_on && ip_amp == ip_drv + 1 &&
                     e->bf[PX_B_DRIVE] == 1.0f && e->bfa[PX_B_DRIVE] == 1.0f && e->bf[PX_B_AMP] == 1.0f &&
                     e->bfa[PX_B_AMP] == 1.0f;         /* v38: not while one of them fades (each fades on its own) */
    /* supply sag: slow (~11 ms), its two divisions once a block (divisions are slow on the pedal's FPU) */
    const float sag_pa = 1.0f / (1.0f + e->amp_sag * e->amp_sagk), sag_pre = 1.0f / (1.0f + e->amp_sag * e->amp_sagk * 0.3f);

    /* v37: a NAM amp works on whole blocks: the blocks before it run first over the n samples (pass 0), the network
     * on them, then the blocks after it (pass 1). Without NAM: one pass, sample by sample, as before. */
    const int nam = amp_on && e->amp_nam && px_nam_m.ready && ip_amp >= 0 && n <= PX_MAX_BLOCK;
    if (part != PX_PART_ALL && !nam) {                 /* (the caller checks px_nam_dual: not reached) */
        if (out != in) for (int k = 0; k < n; k++) out[k] = part == PX_PART_PRE ? in[k] : 0.0f;
        return;
    }
    if (part == PX_PART_NAM) {
        const uint32_t t0 = px_clock ? px_clock() : 0u;
        nam_run(e, out, n, tr[ip_amp]);
        if (px_clock) e->pf[PX_NBLK] += px_clock() - t0;
        return;
    }
    int s_lo = 0, s_hi = nam ? ip_amp : ns;
    const int p0 = part == PX_PART_POST, p1 = part == PX_PART_PRE ? 1 : nam ? 2 : 1;
    for (int pass = p0; pass < p1; pass++) {
    if (pass == 1) {
        if (part == PX_PART_ALL) {
            const uint32_t t0 = px_clock ? px_clock() : 0u;
            nam_run(e, e->nam_buf, n, tr[ip_amp]);
            if (px_clock) e->pf[PX_NBLK] += px_clock() - t0;
        }
        s_lo = ip_amp + 1;
        s_hi = ns;
    }
    /* v37: block by block over the n samples (each block's state stays in registers through its loop; the order of
     * each sample's operations is unchanged, so the output is the same as sample by sample) */
    float xb[PX_MAX_BLOCK];
    for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;            /* anti-denormal (-300 dB): digital silence never decays */
        float x;
        if (pass == 0) {
            x = in[k] + e->dn;
            { float y = x - e->dc_x1 + 0.9995f * e->dc_y1; e->dc_x1 = x; e->dc_y1 = y; x = y; }
        } else x = part == PX_PART_POST ? in[k] : e->nam_buf[k];
        xb[k] = x;
    }
    for (int s = s_lo; s < s_hi; s++) {
        const int sblk = st[s];
        const float strim = tr[s];
        /* v38: a block fading in or out: its output mixed with its input, the mix ramped over the block */
        const uint32_t pt0 = px_clock ? px_clock() : 0u;   /* v38: each block's time (px_clock: the firmware's) */
        const float fa = e->bfa[sblk], fz = e->bf[sblk];
        const int fading = p->on[sblk] && (fa < 1.0f || fz < 1.0f);   /* (GATE runs off too: not a fade) */
        float dryb[PX_MAX_BLOCK];
        if (fading) for (int k = 0; k < n; k++) dryb[k] = xb[k];
        if (sblk == PX_B_CAB) {                          /* cabinet: its filters one after the other over the block */
            if (p->on[PX_B_CAB] && p->type[PX_B_CAB] % PX_CAB_COUNT == PX_CAB_IR) {   /* v38: an impulse response */
                if (ir_ok && n == IR_B) ir_run(xb);      /* (blocks of 64 as on the pedal; other sizes pass) */
                bq_block2(&e->ir_lo, &e->ir_hi, xb, n);
                for (int k = 0; k < n; k++) xb[k] *= strim;
            } else if (p->on[PX_B_CAB] && e->cab_n) {
                if (px_fx_int) fq_chain(e->cab, (px_fq *)e->fq_cab, e->cab_n, xb, n);
                else bq_chain(e->cab, e->cab_n, xb, n);
                for (int k = 0; k < n; k++) {
                    float x = xb[k];
                    if (e->cab_firon) x = cab_detail(&e->cd, x, e->cab_rb, &e->cab_rw, e->cab_ms);
                    xb[k] = x * e->cab_g * strim;
                }
            } else for (int k = 0; k < n; k++) xb[k] *= strim;
            goto faded;
        }
        if (sblk == PX_B_DRIVE) {                        /* drive (+ the fitted amp's preamp when it comes right after:
                                                            pair), oversampled, stage by stage over the block. Set A, or B
                                                            when the amp oversamples on its own; set C at x2 before NAM */
            if (drv_on) {
                const int both = pair && amp_os;
                const int os = (!both && e->drv_os == 2) ? 2 : PX_OVERSAMPLE, no = os * n;
                px_hb *set = os == 2 ? e->os_c : (pair || !amp_os) ? e->os_a : e->os_b;
                float *ub = px_os_u, pre[PX_MAX_BLOCK];
                for (int k = 0; k < n; k++) {
                    e->dn = (k & 1) ? 1e-15f : -1e-15f;
                    float x = drive_pre(e, xb[k]);
                    if (both) { x = bq_run(&e->amp_tight, x); x = bq_run(&e->amp_bright, x); x *= sag_pre; }
                    pre[k] = x;
                }
                os_up(set, pre, ub, n, os);
                if (both) {                              /* drive and preamp sample by sample (bias: once a base sample) */
                    for (int j = 0; j < no; j++) {
                        e->dn = ((j >> 2) & 1) ? 1e-15f : -1e-15f;
                        float u = drive_run(e, ub[j]);
                        for (int i = 0; i < e->amp_n; i++) u = amp_stage(e, i, u, (j & 3) == 0);
                        ub[j] = u;
                    }
                } else drive_block(e, ub, no, os);
                os_down(set, ub, xb, n, os);
                for (int k = 0; k < n; k++) xb[k] *= strim;
            } else for (int k = 0; k < n; k++) xb[k] *= strim;
            goto faded;
        }
        if (sblk == PX_B_DELAY || sblk == PX_B_REVERB) {   /* v38: over the block, each type's own loop */
            if (p->on[sblk]) {
                const int L = sblk == PX_B_REVERB;
                if (e->wl_on && e->wl_p[L] + n <= PX_WL_N) {   /* v39: the lane: the send in, two blocks old wet out */
                    const float dg = L ? 1.0f - 0.4f * e->rv_mix : 1.0f;
                    float *bw = px_wl[L][e->wl_b] + e->wl_p[L];
                    const int rd = (int)e->wl_rd[L] - (int)e->wl_p[L];
                    for (int k = 0; k < n; k++) {
                        const float x = xb[k], w = k < rd ? bw[k] : 0.0f;
                        bw[k] = x;
                        xb[k] = x * dg + w;
                    }
                    e->wl_p[L] = (uint16_t)(e->wl_p[L] + n);
                    e->wl_n[L][e->wl_b] = e->wl_p[L];
                } else {
                    float wb[PX_MAX_BLOCK];
                    if (L) {
                        rv_block(e, xb, wb, n);
                        const float dg = 1.0f - 0.4f * e->rv_mix;
                        for (int k = 0; k < n; k++) xb[k] = xb[k] * dg + wb[k];
                    } else {
                        dly_block(e, xb, wb, n, dly_t);
                        for (int k = 0; k < n; k++) xb[k] = xb[k] + wb[k];
                    }
                }
            }
            for (int k = 0; k < n; k++) xb[k] *= strim;
            goto faded;
        }
        if (sblk == PX_B_AMP && !pair && amp_os) {       /* the fitted amp's own oversampled preamp (set A), over the block;
                                                            its tone stack and power amp then sample by sample below */
            float *ub = px_os_u, pre[PX_MAX_BLOCK];
            for (int k = 0; k < n; k++) {
                float x = bq_run(&e->amp_tight, xb[k]);
                pre[k] = bq_run(&e->amp_bright, x) * sag_pre;
            }
            os_up(e->os_a, pre, ub, n, PX_OVERSAMPLE);
            for (int j = 0; j < PX_OVERSAMPLE * n; j++) {
                e->dn = ((j >> 2) & 1) ? 1e-15f : -1e-15f;
                float u = ub[j];
                for (int i = 0; i < e->amp_n; i++) u = amp_stage(e, i, u, (j & 3) == 0);
                ub[j] = u;
            }
            os_down(e->os_a, ub, xb, n, PX_OVERSAMPLE);
        }
        for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;
        float x = xb[k];
        switch (sblk) {
        case PX_B_GATE:
            /* ---- gate ---- */
            if (p->on[PX_B_GATE]) {
                float a = px_abs(x);
                e->gate_env = (a > e->gate_env) ? a : a + (e->gate_env - a) * 0.9995f;
                float tg = (e->gate_env > e->gate_thr) ? 1.0f : 0.0f;
                e->gate_g = (tg > e->gate_g) ? tg : tg + (e->gate_g - tg) * e->gate_rel;
                if (e->gate_g < 1e-6f) e->gate_g = 0.0f;   /* closed: exact zero, never a denormal */
                x *= e->gate_g;
            }
            x += e->dn;
        break;
        case PX_B_COMP:
            /* ---- compressor ---- */
            if (p->on[PX_B_COMP]) {
                float a = px_abs(x);
                float c = (a > e->cmp_env) ? e->cmp_att : e->cmp_rel;
                e->cmp_env = a + (e->cmp_env - a) * c;
                if (cmp_t == PX_CMP_SWELL) {
                    e->cmp_slow += (e->cmp_env - e->cmp_slow) * e->cmp_slowk;
                    if (e->cmp_env > 1.5f * e->cmp_slow + e->cmp_thr) e->cmp_sw = 0.0f;   /* new pick attack (v36: 1.5, was 1.8) */
                    else e->cmp_sw += (1.0f - e->cmp_sw) * e->cmp_rise;
                    /* v36: the gain acts 2.9 ms late (look-ahead): the attack is caught before it is heard */
                    e->cmp_la[e->cmp_lw] = x;
                    e->cmp_lw = (e->cmp_lw + 1) & 127;
                    x = e->cmp_la[e->cmp_lw] * e->cmp_sw * e->cmp_sw * e->cmp_make;
                } else {
                    float y = x * e->cmp_make / (1.0f + e->cmp_k * e->cmp_env * 4.0f);
                    x = x + (y - x) * e->cmp_mix;
                }
            }
        break;
        case PX_B_PITCH:
            /* ---- pitch ---- */
            if (p->on[PX_B_PITCH]) {
                if (pit_t == PX_PIT_OCTAVER) {
                    float b = bq_run(&e->oct_lp2, bq_run(&e->oct_lp, x));
                    float a = px_abs(b);
                    e->oct_env = a + (e->oct_env - a) * 0.995f;
                    if (e->oct_prev <= 0.0f && b > 0.0f && e->oct_env > 0.002f) {
                        e->oct_ff = -e->oct_ff;
                        if (e->oct_ff > 0.0f) e->oct_ff2 = -e->oct_ff2;
                    }
                    e->oct_prev = b;
                    float s1 = bq_run(&e->oct_sublp, e->oct_ff * e->oct_env);
                    float s2 = bq_run(&e->oct_sublp2, e->oct_ff2 * e->oct_env);
                    float u = px_abs(bq_run(&e->oct_bp, x));
                    u = bq_run(&e->oct_uphp, u);
                    x = x * e->oct_dry + s1 * e->oct_s1 + s2 * e->oct_s2 + u * e->oct_up;
                } else if (pit_t == PX_PIT_LOWOCT) {
                    float a = bq_run(&e->lo_aa[1], bq_run(&e->lo_aa[0], x));
                    if (++e->lo_k >= PX_LO_DEC) {          /* every 8th sample: the low band at its own rate */
                        e->lo_k = 0;
                        for (int i = 0; i < 3; i++) a = bq_run(&e->lo_sel[i], a);
                        a = bq_run(&e->lo_hp, a);
                        float v = lo_fast(e, a);
                        e->lo_y0 = e->lo_y1; e->lo_y1 = v;
                    }
                    float o = e->lo_y0 + (e->lo_y1 - e->lo_y0) * ((float)e->lo_k * (1.0f / (float)PX_LO_DEC));
                    e->lo_lp += (o - e->lo_lp) * e->lo_lpk;
                    float oc = e->lo_lp;
                    if (e->lo_g > 0.0f) {                  /* GROWL */
                        e->lo_glp += (px_tanh(oc * e->lo_gk) * e->lo_gn - e->lo_glp) * e->lo_glk;
                        oc += (e->lo_glp - oc) * e->lo_g;
                    }
                    x = x * e->lo_dry + oc * e->lo_lvl;
                } else {
                    e->ps_buf[e->ps_w] = x;
                    if (pit_t == PX_PIT_WHAMMY) {
                        e->ps_r += (e->ps_rt - e->ps_r) * e->ps_glide;
                        e->ps_inc[0] = (1.0f - e->ps_r) * e->ps_invW;
                    }
                    float y = x * e->ps_dry;
                    for (int v = 0; v < e->ps_nv; v++)
                        y += ps_voice_al(e->ps_buf, e->ps_w, &e->ps_ph[v], e->ps_off[v], e->ps_inc[v], e->ps_W[v]) * e->ps_lvl[v];
                    if (++e->ps_w >= PX_PS_N) e->ps_w = 0;
                    x = y;
                }
            }
        break;
        case PX_B_SFX:
            /* ---- special fx ---- */
            if (p->on[PX_B_SFX]) {
                switch (sfx_t) {
                case PX_SFX_AUTOWAH: case PX_SFX_LFOWAH: {
                    float t;
                    if (sfx_t == PX_SFX_AUTOWAH) {
                        float a = px_abs(x);
                        e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                        t = e->sfx_env * e->sfx_c; if (t > 1.0f) t = 1.0f;
                        t = 250.0f + t * t * e->sfx_d;
                    } else {
                        e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc);
                        t = e->sfx_d + e->sfx_c * 0.5f * lfo_sin(e->sfx_ph);
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        t = 350.0f + t * t * 1900.0f;
                    }
                    float f = 2.0f * PI_F * t / sr;
                    float hp = x - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = e->sfx_bp * 1.6f + e->sfx_lo * 0.15f;
                } break;
                case PX_SFX_RING: {
                    e->sfx_ph2 = wrap1(e->sfx_ph2 + e->sfx_d);
                    e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc * (1.0f + e->sfx_c * lfo_sin(e->sfx_ph2)));
                    float c = sin_hq(e->sfx_ph);
                    x = x * (1.0f - e->sfx_mix) + x * c * e->sfx_mix * 1.4f;
                } break;
                case PX_SFX_CRUSH:
                    if (++e->crush_n >= e->crush_k) {
                        float v = x * e->crush_q;
                        e->crush_v = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->crush_q;
                        e->crush_n = 0;
                    }
                    x = x + (e->crush_v - x) * e->sfx_mix;
                    break;
                case PX_SFX_SLICER: {
                    e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc);
                    int step = (int)(e->sfx_ph * 16.0f) & 15;
                    float tg = ((e->sfx_pat >> (15 - step)) & 1) ? 1.0f : 1.0f - e->sfx_c;
                    e->sfx_g += (tg - e->sfx_g) * e->sfx_a;
                    if (e->sfx_g < 1e-6f) e->sfx_g = 0.0f;
                    x *= e->sfx_g;
                } break;
                case PX_SFX_LOFI: {
                    float y = bq_run(&e->sfx_f2, bq_run(&e->sfx_f1, x));
                    y = px_tanh(y * e->sfx_c) * (1.5f / (1.0f + e->sfx_c * 0.2f));
                    x = x + (y - x) * e->sfx_mix;
                } break;
                default: {                                 /* square synth */
                    float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    float b = bq_run(&e->sfx_f1, x), h = e->sfx_env * 0.1f;
                    if (e->sfx_prev <= 0.0f && b > h) {
                        e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff;
                    } else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                    float sq = e->sfx_prev * e->sfx_env, sub = e->sfx_ff * e->sfx_env;
                    float s = sq + sub * e->sfx_d;
                    float t = e->sfx_env * (2.0f + e->sfx_c * 30.0f); if (t > 1.0f) t = 1.0f;
                    float f = 2.0f * PI_F * (120.0f + t * t * (300.0f + e->sfx_c * 3500.0f)) / sr;
                    float hp = s - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = x * (1.0f - e->sfx_mix) + e->sfx_lo * 2.0f * e->sfx_mix;
                } break;
                }
            }
        break;
        case PX_B_SYN:
            x = syn_run(e, syn_t, x);
            break;
        case PX_B_AMP:                                 /* (its oversampled preamp: above, over the block) */
            if (amp_on && !e->amp_nam) {                 /* (NAM without its model: passes through) */
                if (!amp_os) x = bq_run(&e->amp_tight, x);
                x = bq_run(&e->ts_low, x);
                x = bq_run(&e->ts_mid, x);
                x = bq_run(&e->ts_high, x);
                x = bq_run(&e->amp_voice, x);
                x = bq_run(&e->amp_res, x);
                x = bq_run(&e->amp_pres, x);
                float a = px_abs(x);                       /* power amp + supply sag */
                e->amp_sag += (a - e->amp_sag) * 0.002f;
                x = px_tanh(x * e->amp_pw) * e->amp_pwn * sag_pa;
                x = x * e->amp_out + e->dn;
            }
        break;
        case PX_B_CAB:
            /* ---- cabinet ---- */
            if (p->on[PX_B_CAB] && e->cab_n) {
                for (int c = 0; c < e->cab_n; c++) x = bq_run(&e->cab[c], x);
                if (e->cab_firon) x = cab_detail(&e->cd, x, e->cab_rb, &e->cab_rw, e->cab_ms);   /* breakup, mic, back wave */
                x *= e->cab_g;
            }
        break;
        case PX_B_EQ:
            /* ---- eq ---- */
            if (p->on[PX_B_EQ]) for (int c = 0; c < e->eq_n; c++) x = bq_run(&e->eq[c], x);
        break;
        case PX_B_MOD:
            /* ---- modulation ---- */
            if (p->on[PX_B_MOD]) {
                e->mod_ph = wrap1(e->mod_ph + e->mod_inc);
                float l = lfo_sin(e->mod_ph);
                switch (mod_t) {
                case PX_MOD_CHORUS: case PX_MOD_DIMENSION: {
                    float tri = lfo_tri(e->mod_ph), wet;
                    e->mod_buf[e->mod_w] = x;
                    if (mod_t == PX_MOD_CHORUS) wet = mod_tap(e, e->mod_base + e->mod_sweep * (0.5f + 0.5f * tri));
                    else wet = 0.5f * (mod_tap(e, e->mod_base + e->mod_sweep * tri) + mod_tap(e, e->mod_base - e->mod_sweep * tri));
                    wet = bq_run(&e->mod_lp, wet);
                    if (++e->mod_w >= 2048) e->mod_w = 0;
                    x = x * (1.0f - 0.5f * e->mod_mix) + wet * 0.5f * e->mod_mix;
                } break;
                case PX_MOD_FLANGER: case PX_MOD_VIBRATO: {
                    float d = e->mod_base + e->mod_sweep * (0.5f + 0.5f * (mod_t == PX_MOD_FLANGER ? lfo_tri(e->mod_ph) : l));
                    float wet = mod_tap(e, d);
                    e->mod_buf[e->mod_w] = x + wet * e->mod_fb;
                    if (++e->mod_w >= 2048) e->mod_w = 0;
                    x = (mod_t == PX_MOD_VIBRATO) ? wet : x * (1.0f - 0.5f * e->mod_mix) + wet * 0.5f * e->mod_mix;
                } break;
                case PX_MOD_PHASE90: case PX_MOD_STONE: {
                    float u = 0.5f + 0.5f * l;
                    float fc = e->ph_lo + (e->ph_hi - e->ph_lo) * u * u;
                    float w = PI_F * fc / sr, a = (1.0f - w) / (1.0f + w);
                    float s = x + e->ph_fb * e->mod_fb;
                    for (int i = 0; i < 4; i++) { float y = -a * s + e->ph_z[i]; e->ph_z[i] = s + a * y; s = y; }
                    e->ph_fb = s;
                    x = 0.5f * (x + s);
                } break;
                case PX_MOD_UNIVIBE: {
                    float tg = 0.5f + 0.5f * l;
                    e->uv_lamp += (tg - e->uv_lamp) * (tg > e->uv_lamp ? e->uv_up : e->uv_dn);
                    float fc0 = 60.0f + e->uv_lamp * e->mod_depth * 1200.0f, s = x;
                    for (int i = 0; i < 4; i++) {
                        float fc = fc0 * e->uv_k[i]; if (fc > sr * 0.45f) fc = sr * 0.45f;
                        float w = PI_F * fc / sr, a = (1.0f - w) / (1.0f + w);
                        float y = -a * s + e->ph_z[i]; e->ph_z[i] = s + a * y; s = y;
                    }
                    x = x * (1.0f - e->mod_mix) + s * e->mod_mix;
                } break;
                case PX_MOD_OPTOTREM: {
                    float tg = 1.0f - e->mod_depth * (0.5f + 0.5f * l);
                    e->trem_g += (tg - e->trem_g) * (tg < e->trem_g ? e->trem_att : e->trem_rel);
                    x *= e->trem_g;
                } break;
                case PX_MOD_BIASTREM: {
                    float sq = l > 0.0f ? 1.0f : -1.0f, sh = l + (sq - l) * e->mod_shape;
                    float g = 1.0f - e->mod_depth * (0.5f + 0.5f * sh);
                    x = px_tanh(x * g * 1.2f) * 0.85f;
                } break;
                case PX_MOD_HARMTREM: {
                    float lo = bq_run(&e->mod_lp, x), hi = x - lo;
                    float m = 0.5f + 0.5f * l;
                    x = lo * (1.0f - e->mod_depth * m) + hi * (1.0f - e->mod_depth * (1.0f - m));
                    x *= 1.0f + 0.5f * e->mod_depth;
                } break;
                case PX_MOD_ROTARY: {
                    e->rot_hs += (e->rot_ht - e->rot_hs) * e->rot_hk;
                    e->rot_ds += (e->rot_dt - e->rot_ds) * e->rot_dk;
                    e->rot_h = wrap1(e->rot_h + e->rot_hs / sr);
                    e->rot_d = wrap1(e->rot_d + e->rot_ds / sr);
                    float lo = bq_run(&e->mod_lp, x), hi = x - lo;
                    e->mod_buf[e->mod_w] = hi;
                    float hs = lfo_sin(e->rot_h);
                    float horn = mod_tap(e, (0.001f + 0.00044f * e->mod_depth * hs) * sr);
                    if (++e->mod_w >= 2048) e->mod_w = 0;
                    float hc = lfo_sin(wrap1(e->rot_h + 0.25f));
                    horn *= 1.0f - 0.35f * e->mod_depth * (0.5f + 0.5f * hc);
                    lo *= 1.0f - 0.25f * e->mod_depth * (0.5f + 0.5f * lfo_sin(e->rot_d));
                    x = lo * (1.5f - e->rot_bal) + horn * (0.5f + e->rot_bal);
                } break;
                default: break;
                }
            }
        break;
        default: break;
        }
        xb[k] = x * strim;
        }
    faded:
        if (fading) {
            const float dg = (fz - fa) / (float)n;
            float g = fa;
            for (int k = 0; k < n; k++) { g += dg; xb[k] = dryb[k] + (xb[k] - dryb[k]) * g; }
        }
        if (px_clock) e->pf[sblk] += px_clock() - pt0;
    }
    if (nam && pass == 0) {
        float *d = part == PX_PART_PRE ? out : e->nam_buf;
        for (int k = 0; k < n; k++) d[k] = xb[k];
        continue;
    }
    for (int k = 0; k < n; k++) {
        float x = xb[k];
        { float y = x - e->odc_x1 + 0.9990f * e->odc_y1; e->odc_x1 = x; e->odc_y1 = y; x = y; }
        x *= pg;
        {   /* v36: soft limiter instead of a hard clip: transparent up to 0.8, then rounds off smoothly to 1.2 */
            float a = px_abs(x);
            if (a > 0.8f) { float y = 0.8f + 0.4f * px_tanh((a - 0.8f) * 2.5f); x = x > 0.0f ? y : -y; }
        }
        out[k] = x;
    }
    }
}
void px_engine_process(px_engine *e, const float *in, float *out, int n) { px_engine_part(e, in, out, n, PX_PART_ALL); }
