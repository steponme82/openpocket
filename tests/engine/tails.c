/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — test build 40: TAILS (engine/px_dsp.c px_tl), the Bluetooth audio image's (-DPX_OWN_FX=1). A DELAY or REVERB
 * that goes off rings out with what it had, inline and through the wet lanes (the firmware's two-core way):
 *  1. TAILS off: a delay switched off is silent once its fade is over (as before);
 *  2. TAILS on: switched off, its echoes go on, then stop by themselves, with nothing left ringing;
 *  3. TAILS on: a full preset change to a sound with the delay off, of another type and time: the old echoes go on at the
 *     old time, and the next change of that sound (its delay still off) is not a full one;
 *  4. a reverb switched off rings out; switched on again while it rings, it plays as a reverb again;
 *  5. TAILS switched off while a tail rings: it fades out over 0.25 s, then nothing rings.
 *   cc -O2 -DPX_OWN_FX=1 -Iengine tests/engine/tails.c engine/px_dsp.c firmware/px_math.c -lm -o build/tails && build/tails */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

#define FR 128
#define SR 44100.0f
static px_engine e;
static int failed, lanes;
static void ok(int c, const char *what) { printf("%-90s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

/* blocks of 128 frames, the firmware's way: lanes swap, the chain (two calls of 64), the lanes' wet */
static double run(int nblocks, int burst, float *peak)
{
    static float in[FR], out[FR];
    double en = 0.0;
    float pk = 0.0f;
    for (int b = 0; b < nblocks; b++) {
        for (int i = 0; i < FR; i++) in[i] = burst ? 0.3f * sinf((float)(b * FR + i) * 0.0313f) : 0.0f;
        if (lanes) px_wet_swap(&e);
        px_engine_process(&e, in, out, 64);
        px_engine_process(&e, in + 64, out + 64, 64);
        if (lanes) px_wet_run(&e, 0, 0);
        for (int i = 0; i < FR; i++) {
            if (!isfinite(out[i])) { if (peak) *peak = 1e9f; return 1e9; }
            en += (double)out[i] * out[i];
            if (fabsf(out[i]) > pk) pk = fabsf(out[i]);
        }
    }
    if (peak) *peak = pk;
    return en;
}
static int blocks(float ms) { return (int)(ms * 0.001f * SR / FR + 0.5f); }

static px_preset_t base(int delay, int reverb)
{
    px_preset_t p;
    px_preset_factory(&p, 0);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
    p.on[PX_B_GATE] = 1; p.p[PX_B_GATE][0] = 0;          /* (the gate open) */
    p.on[PX_B_DELAY] = (uint8_t)delay; p.type[PX_B_DELAY] = PX_DLY_DIGITAL;
    p.p[PX_B_DELAY][0] = 15; p.p[PX_B_DELAY][1] = 60; p.p[PX_B_DELAY][2] = 70; p.p[PX_B_DELAY][3] = 60;
    p.on[PX_B_REVERB] = (uint8_t)reverb; p.type[PX_B_REVERB] = PX_REV_HALL;
    p.p[PX_B_REVERB][0] = 60; p.p[PX_B_REVERB][1] = 50; p.p[PX_B_REVERB][2] = 60;
    return p;
}
static void start(const px_preset_t *p, int tails)
{
    px_engine_init(&e, SR);
    px_engine_tails(&e, tails);
    px_engine_set(&e, p);
    if (lanes) px_wet_mode(&e, 1, 2 * FR);
    e.dly_len = e.dly_tgt;
}

int main(void)
{
    char what[160];
    for (lanes = 0; lanes < 2; lanes++) {
        const char *mode = lanes ? "lanes" : "inline";
        px_preset_t on = base(1, 0), off = base(0, 0);

        /* 1. TAILS off: switched off, silent after the fade */
        start(&on, 0);
        run(blocks(60), 1, 0);
        px_engine_knobs(&e, &off);
        run(blocks(400), 0, 0);                         /* (the fade, and the dry chain's own short tail) */
        float pk1;
        run(blocks(1000), 0, &pk1);
        snprintf(what, sizeof what, "%s, TAILS off: the delay switched off is silent after its fade (peak %.2g)", mode, pk1);
        ok(pk1 < 1e-6f, what);

        /* 2. TAILS on: it rings out, then stops */
        start(&on, 1);
        run(blocks(60), 1, 0);
        px_engine_knobs(&e, &off);
        run(blocks(400), 0, 0);
        float pk2;
        const double en2 = run(blocks(1000), 0, &pk2);
        snprintf(what, sizeof what, "%s, TAILS on: switched off, the echoes go on (peak %.3f, energy %.3g)", mode, pk2, en2);
        ok(pk2 > 0.02f && en2 > 0.1, what);
        int rang = 0;
        for (int i = 0; i < 400 && px_engine_ringing(&e); i++) { run(blocks(100), 0, 0); rang++; }
        float pk2b;
        run(blocks(200), 0, &pk2b);
        snprintf(what, sizeof what, "%s, TAILS on: the tail stops by itself (%d x 100 ms more), silent after (peak %.2g)", mode, rang, pk2b);
        ok(!px_engine_ringing(&e) && pk2b < 1e-3f, what);

        /* 3. TAILS on: a full change to a sound with the delay off, another type and time: the old echoes go on */
        start(&on, 1);
        run(blocks(60), 1, 0);
        px_preset_t other = base(0, 0);
        other.type[PX_B_AMP] = (uint8_t)((on.type[PX_B_AMP] + 1) % px_ntypes(PX_B_AMP));
        other.type[PX_B_DELAY] = PX_DLY_TAPE; other.p[PX_B_DELAY][0] = 80; other.p[PX_B_DELAY][1] = 0;
        ok(px_engine_change(&e, &other) == PX_CH_FULL, "a change of amp type is a full one");
        px_engine_set(&e, &other);
        if (lanes) px_wet_mode(&e, 1, 2 * FR);
        run(blocks(400), 0, 0);
        float pk3;
        const double en3 = run(blocks(1000), 0, &pk3);
        snprintf(what, sizeof what, "%s, TAILS on: after a full change to a sound with the delay off, the echoes go on (peak %.3f)", mode, pk3);
        ok(pk3 > 0.02f && en3 > 0.1 && (px_engine_ringing(&e) & 1), what);
        other.p[PX_B_EQ][0] = 77;                        /* (a knob of the new sound: its delay, of another type, still off) */
        ok(px_engine_change(&e, &other) == PX_CH_KNOBS, "TAILS on: an off delay of another type does not make the next change a full one");
        px_engine_knobs(&e, &other);
        ok(px_engine_ringing(&e) & 1, "TAILS on: and the tail rings on through it");

        /* 4. a reverb: switched off it rings out, on again it plays */
        px_preset_t ron = base(0, 1), roff = base(0, 0);
        start(&ron, 1);
        run(blocks(60), 1, 0);
        px_engine_knobs(&e, &roff);
        float pk4;
        run(blocks(500), 0, &pk4);
        snprintf(what, sizeof what, "%s, TAILS on: a reverb switched off rings out (peak %.3f)", mode, pk4);
        ok(pk4 > 1e-3f && (px_engine_ringing(&e) & 2), what);
        px_engine_knobs(&e, &ron);
        float pk4b;
        run(blocks(60), 1, &pk4b);
        ok(!(px_engine_ringing(&e) & 2) && e.run.on[PX_B_REVERB] && pk4b > 0.01f, "TAILS on: switched on again while it rings, it plays as a reverb");

        /* 5. TAILS off while a tail rings: silent at once */
        start(&on, 1);
        run(blocks(60), 1, 0);
        px_engine_knobs(&e, &off);
        run(blocks(400), 0, 0);
        px_engine_tails(&e, 0);
        float pk5a, pk5;
        run(blocks(20), 0, &pk5a);
        const int still = px_engine_ringing(&e) & 1;
        run(blocks(300), 0, 0);
        run(blocks(300), 0, &pk5);
        snprintf(what, sizeof what, "%s, TAILS switched off while a tail rings: it fades out (0.25 s), then silent (peak %.2g)", mode, pk5);
        ok(still && !px_engine_ringing(&e) && pk5 < 1e-4f, what);
        px_engine_tails(&e, 0);
    }
    printf("\n%s\n", failed ? "tails test FAILED" : "tails test passed");
    return failed ? 1 : 0;
}
