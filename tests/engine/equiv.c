/* v40.4: the engine's output hashed bit for bit over a wide set of runs, to compare two versions of the engine
 * (tests/equiv.sh REF builds this file against the engine of commit REF and against this tree's, and compares the lists):
 *  A  every block x type x 8 settings, the block alone (no amp, no cabinet): the block's own arithmetic
 *  B  every block x type x 2 settings with a fitted amp and a cabinet (the oversampled pair paths)
 *  C  60 random chains (types, settings, order, on/off), with knob changes and blocks switched while playing (the
 *     glides and fades: px_engine_knobs), in blocks of 64, 48, 16 and 1 frames
 *  D  12 random chains with a NAM amp through the two-core pipeline's parts (PRE, NAM, POST) and the wet lanes
 * 852 runs. Two engines give the same output if and only if their lists are identical. ARG: the sections (default ABCD).
 * Build it as the pedal builds the engine, -fno-builtin -ffp-contract=off: the pedal has no square-root instruction
 * (px_math.c's sqrtf is used) and does not fuse a multiply with an add; a PC compiler left to itself does both. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
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
#define NS (44100 * 2)
static float sig[NS];
static uint32_t rs = 99u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint64_t H(uint64_t h, const float *o, int n)
{
    for (int i = 0; i < n; i++) { uint32_t u; memcpy(&u, &o[i], 4); h = (h ^ u) * 1099511628211ull; }
    return h;
}
static void mksig(void)
{   /* plucked saw notes and fifths, noise, a silence, a very loud passage */
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
        h = H(h, out, bs);
    }
    return h;
}
/* the Bluetooth audio image's own types (engine/px_dsp.h PX_OWN_FX: LOW OCTAVE, PAD, TREBLE BOOST), appended to their blocks' lists, are
 * left out of every section: an engine built with them plays exactly the runs of one built without (tests/equiv.sh with
 * EQUIV_CFLAGS="-DPX_OWN_FX=1 ...", against the commit before them) */
static int nt_stock(int b)
{
    int n = px_ntypes(b);
    while (n > 1 && (!strcmp(px_type_name(b, n - 1), "LOW OCTAVE") || !strcmp(px_type_name(b, n - 1), "PAD") ||
                     !strcmp(px_type_name(b, n - 1), "TREBLE BOOST"))) n--;
    return n;
}
static void rnd_preset(px_preset_t *p, int nam)
{
    px_preset_factory(p, (int)(rnd() % 24));
    for (int z = 0; z < PX_NBLK; z++) {
        p->on[z] = (rnd() & 3) != 0;
        p->type[z] = (uint8_t)(rnd() % (unsigned)nt_stock(z));
        for (int k = 0; k < 5; k++) { const uint32_t r = rnd() % 8; p->p[z][k] = (uint8_t)(r == 0 ? 0 : r == 1 ? 100 : rnd() % 101); }
    }
    if (nam) { p->on[PX_B_AMP] = 1; p->type[PX_B_AMP] = PX_AMP_NAM; }
    else if (p->type[PX_B_AMP] == PX_AMP_NAM) p->type[PX_B_AMP] = (uint8_t)(rnd() % PX_AMP_COUNT);
    for (int i = PX_NBLK - 1; i > 0; i--) { const int j = (int)(rnd() % (unsigned)(i + 1)); const uint8_t t = p->order[i]; p->order[i] = p->order[j]; p->order[j] = t; }
    if (rnd() & 1) px_order_default(p->order);
}
int main(int argc, char **argv)
{
    const char *sec = argc > 1 ? argv[1] : "ABCD";
    mksig();
    for (int i = 0; i < PX_IR_LEN; i++) IRS[i] = ((float)rnd() / 16777216.0f - 0.5f) * expf(-(float)i / 200.0f) * (i ? 1.0f : 4.0f);
    static const uint8_t V[8][5] = {{0, 0, 0, 0, 0}, {100, 100, 100, 100, 100}, {50, 50, 50, 50, 50}, {0, 50, 100, 0, 50},
                                    {100, 0, 50, 100, 0}, {30, 70, 10, 90, 60}, {80, 20, 95, 5, 40}, {65, 100, 0, 75, 100}};
    int n = 0;
    if (strchr(sec, 'A'))
    for (int blk = 0; blk < PX_NBLK; blk++) for (int t = 0; t < nt_stock(blk); t++) {
        if (blk == PX_B_AMP && t == PX_AMP_NAM) continue;
        for (int a = 0; a < 8; a++) {
            px_preset_t p; px_preset_factory(&p, 7);
            for (int z = 0; z < PX_NBLK; z++) p.on[z] = (z == blk);
            p.type[blk] = (uint8_t)t; memcpy(p.p[blk], V[a], 5);
            printf("A blk %d type %d set %d %016llx\n", blk, t, a, (unsigned long long)run(&p, a & 1, NS, 64)); n++;
        }
    }
    if (strchr(sec, 'B'))
    for (int blk = 0; blk < PX_NBLK; blk++) for (int t = 0; t < nt_stock(blk); t++) for (int a = 0; a < 2; a++) {
        px_preset_t p; px_preset_factory(&p, 9);          /* BRIT CRUNCH: a fitted amp, a drive before it */
        for (int z = 0; z < PX_NBLK; z++) p.on[z] = (z == PX_B_AMP || z == PX_B_CAB || z == PX_B_DRIVE || z == blk);
        if (blk == PX_B_AMP && t == PX_AMP_NAM) continue;
        p.type[blk] = (uint8_t)t; memcpy(p.p[blk], V[5 + a], 5);
        printf("B blk %d type %d set %d %016llx\n", blk, t, a, (unsigned long long)run(&p, a, NS / 2, 64)); n++;
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
            if (b % 97 == 40) {                           /* knobs turned, blocks switched: glides and fades */
                px_preset_t q = e.cur;
                for (int j = 0; j < 4; j++) { const int z = (int)(rnd() % PX_NBLK); q.p[z][rnd() % 5] = (uint8_t)(rnd() % 101); }
                const int z = (int)(rnd() % PX_NBLK); q.on[z] ^= 1;
                if (px_engine_change(&e, &q) == PX_CH_KNOBS) { px_engine_prepare(&e, &q); px_engine_knobs(&e, &q); }
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
    printf("%d runs\n", n);
    return 0;
}
