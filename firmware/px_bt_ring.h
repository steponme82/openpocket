/* SPDX-License-Identifier: GPL-3.0-only */
/* OpenPocket — the phone's music into the pedal's output (the Bluetooth audio test image, docs/BT-AUDIO-TEST.md).
 *
 * The writer (the main loop, firmware/bt/bt_a2dp.c) decodes SBC frames into a ring of mono 16-bit samples; the reader (the audio
 * path, px_audio.h dsp_out, on either core) reads it at a rate the writer sets, by linear interpolation, and adds it to the
 * output. The pedal's clock and the phone's differ (the pedal's runs about 0.04 % fast), so the writer steers the rate from
 * everything buffered, decoded or not (bt_ring_steer): the phone's jitter is absorbed by the encoded packets waiting in the
 * radio's receive pool, compact, and this ring only covers the main loop's pauses.
 *
 * One writer and one reader, no lock: the writer owns w, starts and step; the reader owns r, frac, stops and its fade. The
 * reader plays while starts != stops: the writer starts it when enough is decoded, the reader stops it when it runs dry (an
 * underrun, counted), with a short fade both ways so it never clicks. Pure C, no hardware: tests/test_bt_ring.c.
 *
 * Test build 42: the stereo. Beside the mono mix, (L + R) / 2, a second ring may hold the side, (L - R) / 2, at half the rate
 * (BT_RING_SN samples: the same 58 ms; the stereo up to 11 kHz, where the ear places sounds), filled by bt_ring_put2 and read
 * at the same position by bt_ring_mix2, which then gives both: left = mono + side, right = mono - side. Without it (bt_ring_init,
 * bt_ring_put, bt_ring_mix), the mono mix alone, exactly as before. */
#pragma once
#include <stdint.h>

#ifndef BT_RING_N
#define BT_RING_N 2560u                         /* samples (58 ms at 44.1 kHz) */
#endif
#define BT_RING_SN (BT_RING_N / 2u)              /* the side's samples, at half the rate */
#define BT_STEP_ONE 65536u                      /* the reader's step, Q16: 1.0 = the phone's rate */
#define BT_STEP_MAX (BT_STEP_ONE / 200u)        /* +-0.5 %: the furthest the rate is pulled (about 9 cents) */
#define BT_FADE 0.004f                          /* the fade's step a sample: 250 samples (6 ms) */

typedef struct {
    int16_t *buf;                               /* BT_RING_N samples */
    int16_t *sbuf;                              /* (test build 42) BT_RING_SN samples of the side at half the rate, or 0 */
    int16_t sacc;                               /* (writer) the side's first sample of a pair */
    float slast;                                /* (reader) the side's last sample played */
    volatile uint32_t w;                        /* (writer) samples written, ever */
    volatile uint32_t starts;                   /* (writer) times the reader was started */
    volatile uint32_t step;                     /* (writer) the reader's step, Q16 */
    uint32_t base;                              /* (writer) the step at the stream's nominal rate: rate / 44,100, Q16 */
    volatile uint32_t r;                        /* (reader) samples consumed, ever: the integer part of the position */
    uint32_t frac;                              /* (reader) the fraction, Q16 */
    volatile uint32_t stops;                    /* (reader) times it ran dry */
    float g;                                    /* (reader) the fade's gain now, 0..1 */
    float last;                                 /* (reader) the last sample played (the fade-out after an underrun) */
    float est;                                  /* (writer) the backlog, smoothed (samples) */
} bt_ring_t;

static inline void bt_ring_init2(bt_ring_t *q, int16_t *buf, int16_t *sbuf)
{
    uint32_t i;
    q->buf = buf;
    q->sbuf = sbuf;
    q->sacc = 0;
    q->slast = 0.0f;
    if (sbuf) for (i = 0; i < BT_RING_SN; i++) sbuf[i] = 0;
    for (i = 0; i < BT_RING_N; i++) buf[i] = 0;
    q->w = q->r = q->frac = 0;
    q->starts = q->stops = 0;
    q->step = q->base = BT_STEP_ONE;
    q->g = q->last = 0.0f;
    q->est = 0.0f;
}
static inline void bt_ring_init(bt_ring_t *q, int16_t *buf) { bt_ring_init2(q, buf, 0); }

/* ---- writer */
static inline uint32_t bt_ring_fill(const bt_ring_t *q) { return q->w - q->r; }
static inline uint32_t bt_ring_room(const bt_ring_t *q) { uint32_t f = bt_ring_fill(q); return f + 2u >= BT_RING_N ? 0u : BT_RING_N - 2u - f; }
static inline int bt_ring_playing(const bt_ring_t *q) { return q->starts != q->stops; }
/* n mono samples in (n <= bt_ring_room), and with a side ring their side s (0: none), each pair of it averaged into one */
static inline void bt_ring_put2(bt_ring_t *q, const int16_t *x, const int16_t *s, uint32_t n)
{
    uint32_t w = q->w, i;
    for (i = 0; i < n; i++) q->buf[(w + i) % BT_RING_N] = x[i];
    if (q->sbuf && s)
        for (i = 0; i < n; i++) {
            const uint32_t a = w + i;
            if (!(a & 1u)) q->sacc = s[i];
            else q->sbuf[(a >> 1) % BT_RING_SN] = (int16_t)(((int32_t)q->sacc + s[i]) >> 1);
        }
    __asm__ volatile("" ::: "memory");          /* (the samples before the index: the reader may be on the other core) */
    q->w = w + n;
}
static inline void bt_ring_put(bt_ring_t *q, const int16_t *x, uint32_t n) { bt_ring_put2(q, x, 0, n); }
/* the stream's sampling rate (a phone may choose 48 kHz): the reader resamples it to the pedal's 44.1 kHz */
static inline void bt_ring_rate(bt_ring_t *q, uint32_t hz)   /* (32-bit: 48,000 << 16 fits; the firmware has no 64-bit division) */
{ q->base = hz && hz <= 48000u ? (hz << 16) / 44100u : BT_STEP_ONE; }

/* once per main-loop pass (about 1 kHz): start the reader when the whole backlog (decoded here + still encoded, in samples) has
 * reached `target` and at least `prebuf` samples are decoded, so it starts (and starts again after running dry) with its full
 * margin; then steer its rate towards keeping that backlog. The backlog jumps by whole packets, so it is smoothed first (about
 * 0.2 s); the pull is proportional, at most BT_STEP_MAX at a backlog of 0 or 2 x target. */
static inline void bt_ring_steer(bt_ring_t *q, uint32_t encoded, uint32_t target, uint32_t prebuf)
{
    const float backlog = (float)(bt_ring_fill(q) + encoded);
    float d;
    int32_t s;
    if (!bt_ring_playing(q)) {
        q->est = backlog;
        q->step = q->base;
        if (bt_ring_fill(q) >= prebuf && backlog >= (float)target) q->starts = q->starts + 1u;
        return;
    }
    q->est += (backlog - q->est) * 0.005f;
    d = (q->est - (float)target) / (float)target;   /* -1 .. +1 around the target */
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    s = (int32_t)q->base + (int32_t)(d * (float)BT_STEP_MAX * ((float)q->base * (1.0f / (float)BT_STEP_ONE)));
    q->step = (uint32_t)s;
}

/* ---- reader: adds n samples of the music, times `gain`, to out (the audio path; inline, so it runs from RAM with dsp_out).
 * Running dry stops it (stops = starts: the writer starts it again when enough is decoded); the last sample then fades out over
 * about 60 samples instead of stopping dead. sout (test build 42): with a side ring, its side is added there, read at the same
 * position (half of it in its own samples), faded with the mono mix; 0: the mono mix alone, as before. */
static inline __attribute__((always_inline)) void bt_ring_mix2(bt_ring_t *q, float *out, float *sout, uint32_t n, float gain)
{
    uint32_t i, r = q->r, frac = q->frac;
    const uint32_t w = q->w, step = q->step;
    float g = q->g, last = q->last, slast = q->slast;
    const int sd = sout && q->sbuf;
    int on = q->starts != q->stops;
    if (!on && g <= 0.0f) return;
    for (i = 0; i < n; i++) {
        float s, ss = 0.0f;
        if (on && r + 1u < w) {
            const float a = (float)q->buf[r % BT_RING_N], b = (float)q->buf[(r + 1u) % BT_RING_N];
            s = a + (b - a) * (float)frac * (1.0f / 65536.0f);
            if (sd) {   /* a side sample is a pair's average, half a sample later: its position at half the rate (Q16), taken
                         * within the ring and a ring's worth ahead, so that it is never below 0 and never wraps */
                const uint32_t rr = r % BT_RING_N, pq = (rr << 15) + (frac >> 1) + (BT_RING_SN << 16) - 16384u;
                const uint32_t jr = pq >> 16, f2 = pq & 0xFFFFu;   /* (jr: the side sample, + BT_RING_SN) */
                if (((r - rr) >> 1) + jr + 1u < (w >> 1) + BT_RING_SN) {
                    const float c = (float)q->sbuf[jr % BT_RING_SN], d = (float)q->sbuf[(jr + 1u) % BT_RING_SN];
                    ss = c + (d - c) * (float)f2 * (1.0f / 65536.0f);
                } else ss = slast;
            }
            frac += step;
            r += frac >> 16;
            frac &= 0xFFFFu;
            if (g < 1.0f) { g += BT_FADE; if (g > 1.0f) g = 1.0f; }
        } else {
            if (on) { q->stops = q->starts; on = 0; }   /* ran dry: an underrun */
            s = last;
            ss = slast;
            g -= BT_FADE * 4.0f;
            if (g <= 0.0f) { g = 0.0f; last = slast = 0.0f; break; }
        }
        last = s;
        out[i] += s * (1.0f / 32768.0f) * g * gain;
        if (sd) { slast = ss; sout[i] += ss * (1.0f / 32768.0f) * g * gain; }
    }
    q->r = r;
    q->frac = frac;
    q->g = g;
    q->last = last;
    q->slast = slast;
}
static inline __attribute__((always_inline)) void bt_ring_mix(bt_ring_t *q, float *out, uint32_t n, float gain) { bt_ring_mix2(q, out, 0, n, gain); }
