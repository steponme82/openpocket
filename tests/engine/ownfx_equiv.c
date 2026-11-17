/* The Bluetooth audio image's engine (PX_OWN_FX, engine/px_dsp.h) against the same tree's engine without it: the output
 * hashed bit for bit over the runs below (tests/ownfx_equiv.sh builds this file both ways and compares the lists). PX_OWN_FX
 * leaves out the drive and delay code no model reaches (px_dsp.c: rc_drive, clip_run, drive_run, drive_block, dly_block),
 * shortens the delay ring by 456 ticks and puts its own effects' state there (px_dsp.h): every existing model must sound
 * the same to the bit. Only the models both builds have are run (LOW OCTAVE and PAD come last in their blocks' lists), so
 * the two lists must be identical line for line; where they differ, the line names the block, the model and the settings.
 *  A  tests/engine/equiv.c's A: every block x type x 8 settings, the block alone, 2 s, wet lanes on odd settings
 *  B  equiv.c's B: every block x type x 2 settings with a fitted amp, a drive and a cabinet
 *  C  equiv.c's C: 60 random chains, knob changes and blocks switched while playing, blocks of 64, 48, 16, 1 frames
 *  D  equiv.c's D: 12 random NAM chains through the two-core pipeline's parts and the wet lanes
 *  F  every factory preset (52), 3 s, lanes off and on (lag 128), blocks of 64; and in blocks of 16
 *  G  every delay type x TIME {0,40,80,81,90,96,99,100} x FDBK {0,60,100} x TONE {0,50,100} x WOW {0,50,100}, MIX 70, 1.5 s
 *  H  every delay type: WOW (and TIME, FDBK) turned while playing, 0 -> 60 -> 0 -> 100, lanes off / on, blocks 64 / 16;
 *     and type changes while playing
 *  L  every delay type x TIME {96, 98, 99, 100} x WOW {0, 100}, FDBK 90, MIX 100, 6 s, lanes off / on: at the top
 *  K  every drive type x DRIVE/TONE/LEVEL/4th knob grid (5^4 = 625), 0.25 s, alone; and with each fitted amp (2 settings)
 * Checks along the way (both builds): the delay's flags are always its type's own set (dly_block's generic loop, which
 * PX_OWN_FX leaves out, is never needed), and every DV row uses only the clip, tone and pre-filter modes PX_OWN_FX keeps.
 * Build: as tests/equiv.sh (-O2 -fno-builtin -ffp-contract=off, firmware/px_math.c), plus the image's -D flags.
 * ARG: the sections (default ABCDFGHKL). Exit 1 on a failed check. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#define PX_IR_USER_GIVEN
#include "px_dsp.c"
static float IRS[PX_IR_LEN];
int px_ir_user(int slot, char *name12, float *h)
{
    if (slot > 1) return 0;
    strcpy(name12, "GOLD");
    if (h) { memcpy(h, IRS, sizeof IRS); if (slot) for (int i = 0; i < PX_IR_LEN; i++) h[i] = -h[PX_IR_LEN - 1 - i] * 0.5f; }
    return 1;
}
static px_engine e;
#define NS (44100 * 3)
static float sig[NS];
static uint32_t rs = 99u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static int ntypes_old(int b)                           /* the models both builds have: PX_OWN_FX's own come last */
{
    int n = px_ntypes(b);
    while (n > 0 && (!strcmp(px_type_name(b, n - 1), "LOW OCTAVE") || !strcmp(px_type_name(b, n - 1), "PAD") ||
                     !strcmp(px_type_name(b, n - 1), "TREBLE BOOST") || !strcmp(px_type_name(b, n - 1), "TAPE FLUTE") ||
                     !strcmp(px_type_name(b, n - 1), "WAH"))) n--;
    return n;
}
/* test build 44: the Bluetooth image changes these on purpose (engine/px_dsp.c; tests/engine/synfix.c): SHIMMER's octave voice
 * (here its amount, SHIMMER, is 0: the reverb type itself, its tank and its other knobs, is still compared bit for bit), and
 * SYN's STRINGS and SITAR (rebuilt), MONO SYNTH (its tracker) and BARITONE (its shifter): here an ORGAN in their place.
 * Test build 45: PITCH's WHAMMY (SHIFT's intervals, PEDAL, the note itself at unison: tests/engine/exp.c): a POLY OCTAVE.
 * Test build 47: SYN's 12-STRING (DETUNE 0 to 3 cents, no unison copy: tests/engine/synfix.c 9), an ORGAN too.
 * Test build 48: PRESET LVL under 20 (down to silence: tests/engine/exp.c), 20 here */
static int syn_changed(int t) { return t == PX_SYN_STRINGS || t == PX_SYN_MONO || t == PX_SYN_SITAR || t == PX_SYN_BARI || t == PX_SYN_12STR; }
static int pit_changed(int t) { return t == PX_PIT_WHAMMY; }
static void no_shim(px_preset_t *p)
{
    if (p->p[PX_B_GATE][PX_PATCH_LEVEL] < 20) p->p[PX_B_GATE][PX_PATCH_LEVEL] = 20;   /* (test build 48: PRESET LVL's own below) */
    if (p->type[PX_B_REVERB] % PX_REV_COUNT == PX_REV_SHIMMER) p->p[PX_B_REVERB][3] = 0;
    if (syn_changed(p->type[PX_B_SYN] % PX_SYN_COUNT)) p->type[PX_B_SYN] = PX_SYN_ORGAN;
    if (pit_changed(p->type[PX_B_PITCH] % PX_PIT_COUNT)) p->type[PX_B_PITCH] = PX_PIT_POLYOCT;
}
static uint64_t H(uint64_t h, const float *o, int n)
{
    for (int i = 0; i < n; i++) { uint32_t u; memcpy(&u, &o[i], 4); h = (h ^ u) * 1099511628211ull; }
    return h;
}
static long n_flagchk, n_bad;
static void dly_check(void)                            /* the delay's flags: always its type's own combination */
{
    const int t = e.run.type[PX_B_DELAY] % PX_DLY_COUNT;
    const int m = e.dly_wow > 0.0f || e.dly_mod > 0.0f, fl = e.dly_flut > 0.0f, q = e.dly_q > 0.0f, s = e.dly_sat > 0.0f;
    int ok;
    switch (t) {
    case PX_DLY_DIGITAL: case PX_DLY_DUAL: case PX_DLY_REVERSE: ok = !m && !fl && !q && !s; break;
    case PX_DLY_ANALOG: case PX_DLY_MEMORY: ok = m && !fl && !q && s; break;
    case PX_DLY_TAPE: case PX_DLY_SPACE: ok = m && !q && s; break;
    case PX_DLY_LOFI: ok = m && !fl && q && s; break;
    default: ok = 0;
    }
    n_flagchk++;
    if (!ok && n_bad++ < 5) printf("FLAGS type %d m %d fl %d q %d s %d\n", t, m, fl, q, s);
}
static void mksig(void)
{   /* equiv.c's: plucked saw notes and fifths, noise, a silence, a very loud passage (3 s here) */
    static const float HZ[8] = {82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f, 196.0f, 659.3f};
    float ph0 = 0, ph1 = 0, env = 0;
    for (int i = 0, k = 0; i < NS; i++) {
        if (i % 9000 == 0) { k = (k + 1) & 7; env = (i / 9000) % 5 == 3 ? 1.6f : 0.25f; }
        const float f = HZ[k] / 44100.0f;
        ph0 += f; if (ph0 >= 1.0f) ph0 -= 1.0f;
        ph1 += f * 1.4983f; if (ph1 >= 1.0f) ph1 -= 1.0f;
        float v = env * ((2.0f * ph0 - 1.0f) + 0.6f * (2.0f * ph1 - 1.0f)) + 0.01f * ((float)(rnd() & 65535) / 32768.0f - 1.0f);
        if (i > 50000 && i < 62000) v = 0.0f;
        sig[i] = v;
        env *= 0.99985f;
    }
}
static uint64_t run(const px_preset_t *p, int wet, int frames, int bs)
{
    uint64_t h = 1469598103934665603ull;
    px_engine_init(&e, 44100.0f); px_engine_set(&e, p);
    if (wet) px_wet_mode(&e, 1, 128);
    float out[64];
    for (int at = 0, b = 0; at + bs <= frames; at += bs, b++) {
        if (wet && bs == 64 && !(b & 1)) px_wet_swap(&e);
        px_engine_process(&e, sig + at, out, bs);
        if (wet && bs == 64 && (b & 1)) px_wet_run(&e, 0, 0);
        if (!(b & 15)) dly_check();
        h = H(h, out, bs);
    }
    return h;
}
static void change(const px_preset_t *q)
{
    const int c = px_engine_change(&e, q);
    if (c == PX_CH_KNOBS) { px_engine_prepare(&e, q); px_engine_knobs(&e, q); }
    else if (c == PX_CH_FULL) px_engine_set(&e, q);
    dly_check();
}
static void rnd_preset(px_preset_t *p, int nam)
{
    px_preset_factory(p, (int)(rnd() % 24));
    for (int z = 0; z < PX_NBLK; z++) {
        p->on[z] = (rnd() & 3) != 0;
        p->type[z] = (uint8_t)(rnd() % (unsigned)ntypes_old(z));
        for (int k = 0; k < 5; k++) { const uint32_t r = rnd() % 8; p->p[z][k] = (uint8_t)(r == 0 ? 0 : r == 1 ? 100 : rnd() % 101); }
    }
    if (nam) { p->on[PX_B_AMP] = 1; p->type[PX_B_AMP] = PX_AMP_NAM; }
    else if (p->type[PX_B_AMP] == PX_AMP_NAM) p->type[PX_B_AMP] = (uint8_t)(rnd() % PX_AMP_COUNT);
    for (int i = PX_NBLK - 1; i > 0; i--) { const int j = (int)(rnd() % (unsigned)(i + 1)); const uint8_t t = p->order[i]; p->order[i] = p->order[j]; p->order[j] = t; }
    if (rnd() & 1) px_order_default(p->order);
    no_shim(p);
}
static void alone(px_preset_t *p, int blk)
{
    px_preset_factory(p, 7);
    for (int z = 0; z < PX_NBLK; z++) p->on[z] = (z == blk);
}
int main(int argc, char **argv)
{
    const char *sec = argc > 1 ? argv[1] : "ABCDFGHKL";
    mksig();
    for (int i = 0; i < PX_IR_LEN; i++) IRS[i] = ((float)rnd() / 16777216.0f - 0.5f) * expf(-(float)i / 200.0f) * (i ? 1.0f : 4.0f);
    for (int t = 0; t < PX_DRV_COUNT; t++)             /* DRVDEAD keeps these modes only */
        if (!(DV[t].clip <= CL_GE && (DV[t].tm == TM_LP || DV[t].tm == TM_HM2) && DV[t].lp == 0.0f && DV[t].p2 == 0.0f)) {
            printf("DV row %d uses a mode DRVDEAD drops\n", t); n_bad++;
        }
    static const uint8_t V[8][5] = {{0, 0, 0, 0, 0}, {100, 100, 100, 100, 100}, {50, 50, 50, 50, 50}, {0, 50, 100, 0, 50},
                                    {100, 0, 50, 100, 0}, {30, 70, 10, 90, 60}, {80, 20, 95, 5, 40}, {65, 100, 0, 75, 100}};
    int n = 0;
    if (strchr(sec, 'A'))
    for (int blk = 0; blk < PX_NBLK; blk++) for (int t = 0; t < ntypes_old(blk); t++) {
        if (blk == PX_B_AMP && t == PX_AMP_NAM) continue;
        if (blk == PX_B_SYN && syn_changed(t)) continue;
        if (blk == PX_B_PITCH && pit_changed(t)) continue;
        for (int a = 0; a < 8; a++) {
            px_preset_t p; alone(&p, blk);
            p.type[blk] = (uint8_t)t; memcpy(p.p[blk], V[a], 5); no_shim(&p);
            printf("A blk %d type %d set %d %016llx\n", blk, t, a, (unsigned long long)run(&p, a & 1, 2 * 44100, 64)); n++;
        }
    }
    if (strchr(sec, 'B'))
    for (int blk = 0; blk < PX_NBLK; blk++) for (int t = 0; t < ntypes_old(blk); t++) for (int a = 0; a < 2; a++) {
        px_preset_t p; px_preset_factory(&p, 9);
        for (int z = 0; z < PX_NBLK; z++) p.on[z] = (z == PX_B_AMP || z == PX_B_CAB || z == PX_B_DRIVE || z == blk);
        if (blk == PX_B_AMP && t == PX_AMP_NAM) continue;
        if (blk == PX_B_SYN && syn_changed(t)) continue;
        if (blk == PX_B_PITCH && pit_changed(t)) continue;
        p.type[blk] = (uint8_t)t; memcpy(p.p[blk], V[5 + a], 5); no_shim(&p);
        printf("B blk %d type %d set %d %016llx\n", blk, t, a, (unsigned long long)run(&p, a, 44100, 64)); n++;
    }
    if (strchr(sec, 'C'))
    for (int c = 0; c < 60; c++) {
        px_preset_t p; rnd_preset(&p, 0);
        static const int BS[4] = {64, 48, 16, 1};
        const int bs = BS[c & 3], frames = bs == 1 ? 6000 : 44100;
        uint64_t h = 1469598103934665603ull;
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        float out[64];
        for (int at = 0, b = 0; at + bs <= frames; at += bs, b++) {
            if (b % 97 == 40) {
                px_preset_t q = e.cur;
                for (int j = 0; j < 4; j++) { const int z = (int)(rnd() % PX_NBLK); q.p[z][rnd() % 5] = (uint8_t)(rnd() % 101); }
                const int z = (int)(rnd() % PX_NBLK); q.on[z] ^= 1;
                no_shim(&q);
                if (px_engine_change(&e, &q) == PX_CH_KNOBS) { px_engine_prepare(&e, &q); px_engine_knobs(&e, &q); }
                dly_check();
            }
            px_engine_process(&e, sig + (at % (NS - 64)), out, bs);
            h = H(h, out, bs);
        }
        printf("C chain %d bs %d %016llx\n", c, bs, (unsigned long long)h); n++;
    }
    if (strchr(sec, 'D'))
    for (int c = 0; c < 12; c++) {
        px_preset_t p; rnd_preset(&p, 1);
        uint64_t h = 1469598103934665603ull;
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        px_wet_mode(&e, c & 1, 128);
        static float nb[2][128], out[128];
        int cur = 0;
        for (int b = 0; b < 120; b++) {
            if (b == 50 || b == 90) {
                px_preset_t q = e.cur;
                const int z = (int)(rnd() % PX_NBLK); if (z != PX_B_AMP) q.on[z] ^= 1;
                q.p[rnd() % PX_NBLK][rnd() % 5] = (uint8_t)(rnd() % 101);
                no_shim(&q);
                if (px_engine_change(&e, &q) == PX_CH_KNOBS) { px_engine_prepare(&e, &q); px_engine_knobs(&e, &q); }
            }
            if (e.wl_on) px_wet_swap(&e);
            if (e.wl_on) px_wet_run(&e, 0, 0);
            if (b) for (int o = 0; o < 128; o += 64) px_engine_part(&e, nb[cur ^ 1] + o, out + o, 64, PX_PART_POST);
            for (int o = 0; o < 128; o += 64) px_engine_part(&e, sig + 128 * b + o, nb[cur] + o, 64, PX_PART_PRE);
            for (int o = 0; o < 128; o += 64) px_engine_part(&e, nb[cur] + o, nb[cur] + o, 64, PX_PART_NAM);
            if (b) h = H(h, out, 128);
            cur ^= 1;
        }
        printf("D chain %d %016llx\n", c, (unsigned long long)h); n++;
    }
    if (strchr(sec, 'F'))
    for (int f = 0; f < px_preset_count(); f++) for (int w = 0; w < 3; w++) {
        px_preset_t p; px_preset_factory(&p, f); no_shim(&p);
        printf("F preset %d %s %016llx\n", f + 1, w == 0 ? "lanes-off" : w == 1 ? "lanes-on" : "bs16",
               (unsigned long long)run(&p, w == 1, NS, w == 2 ? 16 : 64)); n++;
    }
    if (strchr(sec, 'G')) {
        static const uint8_t TI[8] = {0, 40, 80, 81, 90, 96, 99, 100}, FB[3] = {0, 60, 100}, TN[3] = {0, 50, 100}, WW[3] = {0, 50, 100};
        for (int t = 0; t < PX_DLY_COUNT; t++) for (int a = 0; a < 8; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++)
        for (int d = 0; d < 3; d++) {
            px_preset_t p; alone(&p, PX_B_DELAY);
            p.type[PX_B_DELAY] = (uint8_t)t;
            const uint8_t k[5] = {TI[a], FB[b], 70, TN[c], WW[d]};
            memcpy(p.p[PX_B_DELAY], k, 5);
            printf("G dly %d time %d fb %d tone %d wow %d %016llx\n", t, TI[a], FB[b], TN[c], WW[d],
                   (unsigned long long)run(&p, 0, 44100 * 3 / 2, 64)); n++;
        }
    }
    if (strchr(sec, 'H'))
    for (int t = 0; t < PX_DLY_COUNT; t++) for (int w = 0; w < 4; w++) {
        const int wet = w & 1, bs = (w & 2) ? 16 : 64;
        px_preset_t p; alone(&p, PX_B_DELAY);
        p.type[PX_B_DELAY] = (uint8_t)t;
        const uint8_t k[5] = {30, 50, 70, 50, 0};
        memcpy(p.p[PX_B_DELAY], k, 5);
        uint64_t h = 1469598103934665603ull;
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        if (wet) px_wet_mode(&e, 1, 128);
        float out[64];
        for (int at = 0, b = 0; at + bs <= NS; at += bs, b++) {
            const int ms = at / 441;                   /* (centiseconds) */
            if (at % 4410 < bs) {                      /* every 0.1 s: the next step of the script */
                px_preset_t q = e.cur;
                switch ((ms / 10) % 12) {
                case 2: q.p[PX_B_DELAY][4] = 60; break;                 /* WOW 0 -> 60 */
                case 4: q.p[PX_B_DELAY][4] = 0; break;                  /* -> 0 */
                case 5: q.p[PX_B_DELAY][0] = 100; break;                /* TIME to the top */
                case 6: q.p[PX_B_DELAY][4] = 100; q.p[PX_B_DELAY][1] = 100; break;
                case 8: q.p[PX_B_DELAY][0] = 10; q.p[PX_B_DELAY][3] = 0; break;
                case 9: q.on[PX_B_DELAY] ^= 1; break;
                case 10: q.on[PX_B_DELAY] = 1; q.type[PX_B_DELAY] = (uint8_t)((t + 3) % PX_DLY_COUNT); break;   /* another type */
                case 11: q.type[PX_B_DELAY] = (uint8_t)t; q.p[PX_B_DELAY][4] = 0; break;
                }
                change(&q);
            }
            if (wet && bs == 64 && !(b & 1)) px_wet_swap(&e);
            px_engine_process(&e, sig + at, out, bs);
            if (wet && bs == 64 && (b & 1)) px_wet_run(&e, 0, 0);
            h = H(h, out, bs);
        }
        printf("H dly %d lanes %d bs %d %016llx\n", t, wet, bs, (unsigned long long)h); n++;
    }
    if (strchr(sec, 'L'))                              /* long runs just under the top: TIME 96..99, 6 s, the repeats heard */
    for (int t = 0; t < PX_DLY_COUNT; t++) for (int a = 0; a < 4; a++) for (int d = 0; d < 2; d++) for (int wet = 0; wet < 2; wet++) {
        static const uint8_t TI[4] = {96, 98, 99, 100};
        px_preset_t p; alone(&p, PX_B_DELAY);
        p.type[PX_B_DELAY] = (uint8_t)t;
        const uint8_t k[5] = {TI[a], 90, 100, 50, d ? 100 : 0};
        memcpy(p.p[PX_B_DELAY], k, 5);
        uint64_t h = 1469598103934665603ull;
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        if (wet) px_wet_mode(&e, 1, 128);
        float out[64];
        for (int at = 0, b = 0; at + 64 <= 6 * 44100; at += 64, b++) {
            if (wet && !(b & 1)) px_wet_swap(&e);
            px_engine_process(&e, sig + (at % (NS - 64)), out, 64);
            if (wet && (b & 1)) px_wet_run(&e, 0, 0);
            h = H(h, out, 64);
        }
        printf("L dly %d time %d wow %d lanes %d %016llx\n", t, TI[a], k[4], wet, (unsigned long long)h); n++;
    }
    if (strchr(sec, 'K')) {
        static const uint8_t KV[5] = {0, 25, 50, 75, 100};
        for (int t = 0; t < ntypes_old(PX_B_DRIVE); t++) for (int i = 0; i < 625; i++) {
            px_preset_t p; alone(&p, PX_B_DRIVE);
            p.type[PX_B_DRIVE] = (uint8_t)t;
            const uint8_t k[5] = {KV[i % 5], KV[(i / 5) % 5], KV[(i / 25) % 5], KV[(i / 125) % 5], 50};
            memcpy(p.p[PX_B_DRIVE], k, 5);
            printf("K drv %d knobs %d %d %d %d %016llx\n", t, k[0], k[1], k[2], k[3], (unsigned long long)run(&p, 0, 11025, 64)); n++;
        }
        for (int t = 0; t < ntypes_old(PX_B_DRIVE); t++) for (int a = 0; a < PX_AMP_COUNT; a++) for (int s = 0; s < 2; s++) {
            px_preset_t p; px_preset_factory(&p, 9);
            for (int z = 0; z < PX_NBLK; z++) p.on[z] = (z == PX_B_AMP || z == PX_B_CAB || z == PX_B_DRIVE);
            p.type[PX_B_DRIVE] = (uint8_t)t; p.type[PX_B_AMP] = (uint8_t)a; memcpy(p.p[PX_B_DRIVE], V[5 + s], 5);
            printf("K drv %d amp %d set %d %016llx\n", t, a, s, (unsigned long long)run(&p, s, 22050, s ? 64 : 16)); n++;
        }
    }
    printf("%d runs, %ld flag checks, %ld bad\n", n, n_flagchk, n_bad);
    return n_bad != 0;
}
