/* SPDX-License-Identifier: GPL-3.0-only */
/* The Bluetooth audio ring (firmware/px_bt_ring.h) on a PC: a phone at 44,100 Hz sends packets of 7 SBC frames (896 samples)
 * every 20.3 ms with up to 15 ms of jitter; the main loop decodes them into the ring every millisecond and steers the rate;
 * the pedal reads 128-sample blocks at its real 44,117.6 Hz (docs/STOCK-FIRMWARE.md section 3). Checks:
 *  - after the start, no underrun in 60 s, the backlog stays near its target, the rate settles at the clocks' ratio;
 *  - the music comes out clean: a 1 kHz sine, against the best-fitting sine at the pedal's rate, > 45 dB;
 *  - a 300 ms pause in the stream stops the reader once, fades out without a jump, and it starts again by itself;
 *  - nothing is added before the start or after the fade (silence stays silence);
 *  - (test build 42) the stereo: one channel alone stays in its channel (1 kHz, 5 kHz), the mono mix unchanged by the side. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/px_bt_ring.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define PHONE_FS 44100.0
#define PEDAL_FS 44117.6
#define FRAME 128u
#define PKT_FRAMES 7u
#define QMAX 4096u                                       /* the encoded queue, in frames */
static int16_t ring_mem[BT_RING_N];
static int16_t enc_q[QMAX][FRAME];                       /* "encoded" frames waiting: here already PCM, decoding is not tested */
static uint32_t q_w, q_r;
static double phase;

static void phone_packet(void)
{
    uint32_t f, i;
    for (f = 0; f < PKT_FRAMES; f++, q_w++)
        for (i = 0; i < FRAME; i++) {
            enc_q[q_w % QMAX][i] = (int16_t)(16000.0 * sin(phase));
            phase += 2.0 * M_PI * 1000.0 / PHONE_FS;
        }
}

int main(void)
{
    bt_ring_t q;
    const uint32_t target = 6615u, prebuf = 1024u;        /* 150 ms in all, 23 ms decoded before it starts */
    const double t_end = 60.0, pkt_dt = PKT_FRAMES * FRAME / PHONE_FS, blk_dt = FRAME / PEDAL_FS;
    double t = 0.0, t_pkt = 0.0, t_sched = 0.0, t_blk = 0.0, t_loop = 0.0;
    uint32_t under_after_start = 0, stops_seen = 0, started_at_blk = 0, blk = 0;
    float out[FRAME];
    static float rec[441176 + FRAME];   /* 10 s at the pedal's rate */
    uint32_t nrec = 0, i;
    double backlog_max = 0, backlog_min = 1e9, step_max = 0, step_min = 9;
    int pause = 0;
    srand(1);
    bt_ring_init(&q, ring_mem);
    while (t < t_end) {
        /* the next event: a packet, a pedal block, a main-loop pass */
        double tn = t_pkt;
        if (t_blk < tn) tn = t_blk;
        if (t_loop < tn) tn = t_loop;
        t = tn;
        if (t == t_pkt) {
            pause = t > 40.0 && t < 40.3;                    /* the phone stops for 300 ms at 40 s */
            if (!pause) phone_packet();
            else phase += 2.0 * M_PI * 1000.0 / PHONE_FS * PKT_FRAMES * FRAME;
            t_sched += pkt_dt;                                  /* the phone's own clock, exact; each packet 0..15 ms late */
            t_pkt = t_sched + (t_sched < 1.0 ? 0.0 : (double)(rand() % 1001) * 15e-6);
            if (t_pkt < t) t_pkt = t;
        } else if (t == t_loop) {
            while (q_r != q_w && bt_ring_room(&q) >= FRAME) { bt_ring_put(&q, enc_q[q_r % QMAX], FRAME); q_r++; }
            bt_ring_steer(&q, (q_w - q_r) * FRAME, target, prebuf);
            t_loop += 0.001;
        } else {
            uint32_t s0 = q.stops;
            for (i = 0; i < FRAME; i++) out[i] = 0.0f;
            bt_ring_mix(&q, out, FRAME, 1.0f);
            if (q.stops != s0) { stops_seen++; if (t > 2.0 && !(t > 40.0 && t < 41.0)) under_after_start++; }
            if (!started_at_blk && bt_ring_playing(&q)) started_at_blk = blk;
            if (t > 5.0 && t < 15.0) { memcpy(rec + nrec, out, sizeof out); nrec += FRAME; }
            if (t > 5.0 && !(t > 39.0 && t < 43.0)) {
                double b = (double)bt_ring_fill(&q) + (double)(q_w - q_r) * FRAME;
                if (b > backlog_max) backlog_max = b;
                if (b < backlog_min) backlog_min = b;
                if ((double)q.step / BT_STEP_ONE > step_max) step_max = (double)q.step / BT_STEP_ONE;
                if ((double)q.step / BT_STEP_ONE < step_min) step_min = (double)q.step / BT_STEP_ONE;
            }
            if (t > 39.9 && t < 41.0) {               /* around the pause: no step bigger than the music's own slope (0.07) */
                static float prev;
                static int seeded, jumps;
                for (i = 0; i < FRAME; i++) {
                    float dv = fabsf(out[i] - prev);
                    if (seeded && dv > 0.1f && jumps++ < 3) { printf("jump %.3f at %.4f s\n", dv, t); under_after_start += 1000; }
                    prev = out[i]; seeded = 1;
                }
            }
            blk++;
            t_blk += blk_dt;
        }
    }
    CHECK(started_at_blk > 0 && started_at_blk < 100, "the reader starts within 100 blocks (block %u)", started_at_blk);
    CHECK(under_after_start == 0, "no underrun outside the pause in 60 s (%u)", under_after_start);
    CHECK(stops_seen == 1, "the 300 ms pause stops the reader exactly once, and it starts again (%u stops)", stops_seen);
    CHECK(backlog_min > 1000.0 && backlog_max < 14000.0, "the backlog stays in [%.0f, %.0f] samples around %u", backlog_min, backlog_max, target);
    {
        double want = PHONE_FS / PEDAL_FS, got = (double)q.step / BT_STEP_ONE;
        CHECK(fabs(got - want) < 0.0006, "the rate settles at the clocks' ratio (%.6f, want %.6f)", got, want);
    }
    {   /* distortion: each 100 ms segment against its best-fitting sine (least squares on cos, sin over a +-0.5 % frequency
         * search); the rate's slow wander is pitch, measured below, not distortion */
        const double w0 = 2.0 * M_PI * 1000.0 / PEDAL_FS;
        const uint32_t seg = 4410u;
        double worst = 1e9;
        for (uint32_t o = 0; o + seg <= nrec; o += seg) {
            double best_db = -1e9, best_k = 0.0;
            for (int pass = 0; pass < 2; pass++) {          /* a coarse search (1e-4), then a fine one (2e-6) around the best */
                const double c0 = pass ? best_k : 0.0, st = pass ? 2e-6 : 1e-4;
                for (int k = -50; k <= 50; k++) {
                    double rel = c0 + k * st, wk = w0 * (1.0 + rel), sc = 0, ss = 0, cc = 0, xs = 0, xc = 0, a, b, e = 0, p = 0, den, db;
                    for (i = 0; i < seg; i++) { double c = cos(wk * i), sn = sin(wk * i), x = rec[o + i]; sc += sn * c; ss += sn * sn; cc += c * c; xs += x * sn; xc += x * c; }
                    den = ss * cc - sc * sc;
                    b = (xs * cc - xc * sc) / den; a = (xc * ss - xs * sc) / den;
                    for (i = 0; i < seg; i++) { double y = a * cos(wk * i) + b * sin(wk * i), d = rec[o + i] - y; e += d * d; p += y * y; }
                    db = 10.0 * log10(p / (e + 1e-30));
                    if (db > best_db) { best_db = db; best_k = rel; }
                }
            }
            if (best_db < worst) worst = best_db;
        }
        CHECK(worst > 45.0, "a 1 kHz sine comes out clean: the worst 100 ms segment is %.1f dB above its error", worst);
        CHECK(step_max - step_min < 0.0012, "the pitch holds: the rate wanders %.3f %% (%.1f cents) after settling", 100.0 * (step_max - step_min), 1200.0 * log2(step_max / step_min));
    }
    {   /* silence before the start and after the fade */
        bt_ring_t z;
        float o[FRAME];
        bt_ring_init(&z, ring_mem);
        for (i = 0; i < FRAME; i++) o[i] = 0.25f;
        bt_ring_mix(&z, o, FRAME, 1.0f);
        int same = 1;
        for (i = 0; i < FRAME; i++) same &= o[i] == 0.25f;
        CHECK(same, "a ring never started adds nothing");
    }
    {   /* test build 42: the stereo. The phone's left channel alone, then its right alone: the mono mix and the side each half of
         * it; read back at the pedal's rate, left = mix + side and right = mix - side. The mono mix comes out bit for bit the same
         * with the side read beside it or not */
        static int16_t mm[BT_RING_N], ms[BT_RING_SN], mm2[BT_RING_N];
        static float lo[400 * FRAME], ro[400 * FRAME];
        static const double FQ[2] = {1000.0, 5000.0};
        int same = 1;
        for (int f = 0; f < 2; f++) for (int side = 0; side < 2; side++) {
            bt_ring_t a, b;
            bt_ring_init2(&a, mm, ms);
            bt_ring_init(&b, mm2);
            a.step = b.step = (uint32_t)(65536.0 * PHONE_FS / PEDAL_FS + 0.5);
            double ph2 = 0.0;
            int started = 0;
            for (int blk = 0; blk < 400; blk++) {
                while (bt_ring_room(&a) >= FRAME) {
                    int16_t m[FRAME], s[FRAME];
                    for (i = 0; i < FRAME; i++) {
                        const int32_t v = (int32_t)(16000.0 * sin(ph2)), l = side ? 0 : v, r = side ? v : 0;
                        ph2 += 2.0 * M_PI * FQ[f] / PHONE_FS;
                        m[i] = (int16_t)((l + r) >> 1); s[i] = (int16_t)((l - r) >> 1);
                    }
                    bt_ring_put2(&a, m, s, FRAME);
                    bt_ring_put(&b, m, FRAME);
                }
                if (!started) { a.starts++; b.starts++; started = 1; }
                float o[FRAME], so[FRAME], o2[FRAME];
                for (i = 0; i < FRAME; i++) o[i] = so[i] = o2[i] = 0.0f;
                bt_ring_mix2(&a, o, so, FRAME, 1.0f);
                bt_ring_mix(&b, o2, FRAME, 1.0f);
                for (i = 0; i < FRAME; i++) { lo[blk * FRAME + i] = o[i] + so[i]; ro[blk * FRAME + i] = o[i] - so[i]; same &= o[i] == o2[i]; }
            }
            double el = 0, er = 0;
            for (i = 100 * FRAME; i < 400 * FRAME; i++) { el += (double)lo[i] * lo[i]; er += (double)ro[i] * ro[i]; }
            const double sep = side ? 10.0 * log10(er / (el + 1e-20)) : 10.0 * log10(el / (er + 1e-20));
            if (f == 0) CHECK(sep > 35.0, "stereo, the %s channel alone at 1 kHz: the other one %.1f dB under it", side ? "right" : "left", sep);
            else CHECK(sep > 12.0, "stereo, the %s channel alone at 5 kHz: the other one %.1f dB under it (the side at half the rate)", side ? "right" : "left", sep);
        }
        CHECK(same, "the mono mix is the same, bit for bit, with the side read beside it");
    }
    printf(fails ? "bt ring: %d FAILED\n" : "bt ring: all passed\n", fails);
    return fails != 0;
}
