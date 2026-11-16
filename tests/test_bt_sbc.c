/* SPDX-License-Identifier: GPL-3.0-only */
/* The Bluetooth audio image's SBC path (firmware/bt/bt_sbc.c over third_party/sbc-android) on a PC. Android's own encoder makes the
 * frames a phone sends (44.1 kHz, joint stereo, 16 blocks, 8 subbands, loudness, bitpool 53: the usual "high quality" A2DP
 * setting), packed as A2DP media packets (an RTP header, the frame count, 7 frames). Checks:
 *  - the packet parser finds the first frame, with contributors, an extension and padding before the sync byte;
 *  - every frame decodes; the output is the mono mix (L + R) / 2, delayed by the codec, > 30 dB above the difference;
 *  - the stream's parameters are reported (44.1 kHz, 2 channels, the encoder's bitpool (53 at 328 kb/s), joint stereo);
 *  - a mono stream decodes too; a frame whose CRC fails is refused (counted) and the next packet decodes again;
 *  - mono frames shorter than 128 samples (8 and 4 blocks) give their own count of samples, the right ones (up to test build 10
 *    each gave twice as many, the second half stale);
 *  - a header whose contributors or extension run past the packet leaves nothing to decode (the offset stops at the end). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/bt/bt_sbc.h"
#include "sbc_encoder.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define N_FRAMES 700u                                   /* 2 s */
static int16_t in_l[N_FRAMES * 128u], in_r[N_FRAMES * 128u], out[N_FRAMES * 128u + 128u], side_out[N_FRAMES * 128u + 128u];

static uint32_t encode(SBC_ENC_PARAMS *e, uint32_t f, uint8_t *dst, int stereo)
{
    int16_t blk[2u * 128u];
    uint32_t i;
    for (i = 0; i < 128u; i++) {
        if (stereo) { blk[2u * i] = in_l[f * 128u + i]; blk[2u * i + 1u] = in_r[f * 128u + i]; }
        else blk[i] = in_l[f * 128u + i];
    }
    return SBC_Encode(e, blk, dst);
}

static double snr_best_delay(const int16_t *got, uint32_t n, int stereo, int *delay)
{
    double best = -1e9;
    for (int d = 0; d < 400; d++) {
        double e = 0, p = 0;
        for (uint32_t i = 2000; i + d < n && i < n - 2000; i++) {
            double ref = stereo == 2 ? (in_l[i] - in_r[i]) * 0.5 : stereo ? (in_l[i] + in_r[i]) * 0.5 : in_l[i];   /* (2: the side) */
            double x = got[i + d] - ref;
            e += x * x; p += ref * ref;
        }
        double db = 10.0 * log10(p / (e + 1e-9));
        if (db > best) { best = db; *delay = d; }
    }
    return best;
}

/* mono frames of `blocks` blocks (8 subbands): each frame must give blocks * 8 samples, and they must be the signal */
static int mono_short(int blocks, uint32_t *bad, double *db, int *delay)
{
    SBC_ENC_PARAMS e;
    uint8_t fr[512];
    int16_t blk[128];
    const uint32_t spf = (uint32_t)blocks * 8u;
    uint32_t f, i, nout = 0;
    memset(&e, 0, sizeof e);
    e.s16SamplingFreq = SBC_sf44100; e.s16ChannelMode = SBC_MONO; e.s16NumOfChannels = 1;
    e.s16NumOfSubBands = 8; e.s16NumOfBlocks = (int16_t)blocks; e.s16AllocationMethod = SBC_LOUDNESS; e.u16BitRate = 127;
    SBC_Encoder_Init(&e);
    bt_sbc_reset();
    *bad = 0;
    for (f = 0; (f + 1u) * spf <= N_FRAMES * 128u; f++) {
        const uint8_t *p = fr;
        uint32_t n;
        int got;
        for (i = 0; i < spf; i++) blk[i] = in_l[f * spf + i];
        n = SBC_Encode(&e, blk, fr);
        got = bt_sbc_frame(&p, &n, out + nout);
        if (got != (int)spf) { (*bad)++; if (got < 0) got = 0; }
        nout += (uint32_t)got;
    }
    *db = snr_best_delay(out, nout, 0, delay);
    return (int)nout;
}

static int enc_bitpool;
static uint32_t run(int stereo, int kbps, int damage_at, uint32_t *pkts_out)
{
    SBC_ENC_PARAMS e;
    uint8_t pkt[2048];
    uint32_t f = 0, nout = 0, pkts = 0;
    memset(&e, 0, sizeof e);
    e.s16SamplingFreq = SBC_sf44100;
    e.s16ChannelMode = stereo ? SBC_JOINT_STEREO : SBC_MONO;
    e.s16NumOfChannels = stereo ? 2 : 1;
    e.s16NumOfSubBands = 8;
    e.s16NumOfBlocks = 16;
    e.s16AllocationMethod = SBC_LOUDNESS;
    e.u16BitRate = (uint16_t)kbps;                       /* (the encoder derives its bitpool from the bit rate) */
    SBC_Encoder_Init(&e);
    enc_bitpool = e.s16BitPool;
    bt_sbc_reset();
    while (f < N_FRAMES) {
        uint32_t len = 0, k, off;
        const uint8_t *p;
        uint32_t n;
        /* the RTP header: version 2, one contributor (4 bytes), an extension of one word; the count byte, a padding byte */
        pkt[len++] = 0x80 | 0x10 | 0x01; pkt[len++] = 0x60;
        pkt[len++] = (uint8_t)(pkts >> 8); pkt[len++] = (uint8_t)pkts;
        for (k = 0; k < 8u; k++) pkt[len++] = (uint8_t)k;          /* timestamp, SSRC */
        for (k = 0; k < 4u; k++) pkt[len++] = 0xEE;                /* the contributor */
        pkt[len++] = 0xBE; pkt[len++] = 0xDE; pkt[len++] = 0x00; pkt[len++] = 0x01;   /* extension head: one word */
        for (k = 0; k < 4u; k++) pkt[len++] = 0x11;
        pkt[len++] = 7u;                                           /* 7 frames */
        if (pkts == 3u) pkt[len++] = 0x00;                         /* a phone that pads: the parser looks for the sync byte */
        uint32_t flen = 0;
        for (k = 0; k < 7u && f < N_FRAMES; k++, f++) { flen = encode(&e, f, pkt + len, stereo); len += flen; }
        if ((int)pkts == damage_at) pkt[len - flen + 3u] ^= 0x5A;  /* the last frame's CRC byte: the frame is refused (SBC's CRC
                                                                      covers the header and scale factors, not the samples) */
        off = bt_sbc_payload(pkt, len);
        if (pkts == 0u && off != 12u + 4u + 8u + 1u) printf("payload offset %u\n", off);
        p = pkt + off; n = len - off;
        for (;;) {
            int got = bt_sbc_frame2(&p, &n, out + nout, side_out + nout);
            if (got <= 0) break;
            nout += (uint32_t)got;
        }
        pkts++;
    }
    *pkts_out = pkts;
    return nout;
}

int main(void)
{
    uint32_t i, pkts, nout;
    int d;
    for (i = 0; i < N_FRAMES * 128u; i++) {
        in_l[i] = (int16_t)(16000.0 * sin(2.0 * M_PI * 440.0 * i / 44100.0));
        in_r[i] = (int16_t)(9000.0 * sin(2.0 * M_PI * 660.0 * i / 44100.0 + 1.0));
    }
    {
        uint8_t pk[40] = {0x80, 0x60, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0x9C, 1, 2};
        CHECK(bt_sbc_payload(pk, 16) == 13u, "a plain RTP header: the first frame at byte 13");
        pk[0] = 0x82; pk[20] = 7; pk[21] = 0x9C;
        CHECK(bt_sbc_payload(pk, 30) == 21u, "two contributors: at byte 21");
        CHECK(bt_sbc_payload(pk, 10) == 10u, "a packet too short: none");
        pk[0] = 0x8F;                                            /* 15 contributors (60 bytes) in a 20-byte packet */
        CHECK(bt_sbc_payload(pk, 20) == 20u, "contributors past the packet's end: nothing left (%u)", bt_sbc_payload(pk, 20));
        pk[0] = 0x90; pk[12] = 0xBE; pk[13] = 0xDE; pk[14] = 0x01; pk[15] = 0x00;   /* an extension of 256 words in 24 bytes */
        CHECK(bt_sbc_payload(pk, 24) == 24u, "an extension past the packet's end: nothing left (%u)", bt_sbc_payload(pk, 24));
    }
    nout = run(1, 328, -1, &pkts);
    CHECK(nout == N_FRAMES * 128u, "joint stereo at 328 kb/s: every frame decodes (%u of %u samples, %u packets)", nout, N_FRAMES * 128u, pkts);
    CHECK(bt_sbc_info.rate == 44100u && bt_sbc_info.channels == 2u && bt_sbc_info.bitpool == (uint8_t)enc_bitpool && enc_bitpool == 53 && bt_sbc_info.mode == 3u &&
          bt_sbc_info.blocks == 16u && bt_sbc_info.subbands == 8u,
          "the stream is reported: %u Hz, %u channels, bitpool %u, mode %u, %u blocks, %u subbands", bt_sbc_info.rate,
          bt_sbc_info.channels, bt_sbc_info.bitpool, bt_sbc_info.mode, bt_sbc_info.blocks, bt_sbc_info.subbands);
    {
        double db = snr_best_delay(out, nout, 1, &d);
        CHECK(db > 30.0, "the output is the mono mix: %.1f dB above the difference, the codec's delay %d samples", db, d);
        int d2;
        double ds = snr_best_delay(side_out, nout, 2, &d2);
        CHECK(ds > 30.0 && d2 == d, "test build 42: the side is (L - R) / 2: %.1f dB above the difference, at the same delay (%d)", ds, d2);
    }
    nout = run(0, 127, -1, &pkts);
    {
        double db = snr_best_delay(out, nout, 0, &d);
        CHECK(nout == N_FRAMES * 128u && db > 30.0, "a mono stream (bitpool %d): %u samples, %.1f dB", enc_bitpool, nout, db);
    }
    {
        uint32_t bad;
        double db;
        int n8 = mono_short(8, &bad, &db, &d);
        CHECK(bad == 0u && db > 30.0, "mono frames of 8 blocks: 64 samples each (%u frames wrong), %.1f dB (%d samples)", bad, db, n8);
        int n4 = mono_short(4, &bad, &db, &d);
        CHECK(bad == 0u && db > 25.0, "mono frames of 4 blocks: 32 samples each (%u frames wrong), %.1f dB (%d samples)", bad, db, n4);
    }
    {
        uint32_t e0;
        bt_sbc_info.errors = 0;
        nout = run(1, 328, 20, &pkts);
        e0 = bt_sbc_info.errors;
        CHECK(e0 == 1u && nout >= (N_FRAMES - 7u) * 128u, "a damaged frame is refused once (%u), the rest of the stream decodes (%u samples)", e0, nout);
    }
    printf(fails ? "bt sbc: %d FAILED\n" : "bt sbc: all passed\n", fails);
    return fails != 0;
}
