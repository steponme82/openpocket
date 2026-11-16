/* SPDX-License-Identifier: GPL-3.0-only */
/* The SBC decoder of the Bluetooth audio image: Android's (third_party/sbc-android, Apache-2.0) behind three calls (bt_sbc.h).
 * A packet's RTP header is skipped the way JieLi's own A2DP demo does (apps/demo/demo_edr/bt_decode.c, Apache-2.0): 12 bytes,
 * 4 per contributor, the extension if flagged, the byte that counts the frames, then the first sync byte 0x9C. Stereo is
 * summed to mono, (L + R) / 2. A separate unit from openpocket.c (it is built with the decoder's own headers). */
#include <stdint.h>
#include "bt_sbc.h"
#include "oi_codec_sbc.h"
#include "oi_status.h"

bt_sbc_info_t bt_sbc_info;
static OI_CODEC_SBC_DECODER_CONTEXT ctx;
#if BT_SBC_LO2   /* the firmware: in the SRAM below the RAM, beside the ring (app-bt.ld .bss_lo2; 1,888 bytes, set up by bt_sbc_reset) */
static uint32_t ctx_data[CODEC_DATA_WORDS(2, SBC_CODEC_FAST_FILTER_BUFFERS)] __attribute__((section(".bss.lo2.sbc")));
#else
static uint32_t ctx_data[CODEC_DATA_WORDS(2, SBC_CODEC_FAST_FILTER_BUFFERS)];
#endif
static int16_t pcm[2u * BT_SBC_MAX_SAMPLES];
static int ready;

void bt_sbc_reset(void)
{
    ready = OI_CODEC_SBC_DecoderReset(&ctx, ctx_data, sizeof ctx_data, 2, 2, 0) == OI_OK;
    bt_sbc_info.rate = 0;
}

uint32_t bt_sbc_payload(const uint8_t *pkt, uint32_t len)
{
    uint32_t off;
    if (len < 13u) return len;
    off = 12u + 4u * (pkt[0] & 0x0Fu);
    if (pkt[0] & 0x10u) {                                       /* an extension: its length in words after a 4-byte head */
        if (off + 4u > len) return len;
        off += 4u + 4u * (((uint32_t)pkt[off + 2u] << 8) | pkt[off + 3u]);
    }
    off += 1u;                                                  /* the media payload's header: the number of frames */
    while (off < len && pkt[off] != 0x9Cu) off++;               /* (some phones pad: the demo looks for the sync byte) */
    return off < len ? off : len;                               /* (contributors or an extension longer than the packet: nothing left) */
}

int bt_sbc_frame(const uint8_t **p, uint32_t *n, int16_t *mono) { return bt_sbc_frame2(p, n, mono, 0); }
int bt_sbc_frame2(const uint8_t **p, uint32_t *n, int16_t *mono, int16_t *side)
{
    uint32_t bytes = sizeof pcm, i, k;
    const OI_BYTE *d = *p;
    uint32_t left = *n;
    OI_STATUS st;
    if (!ready) bt_sbc_reset();
    if (left < 4u) return 0;
    st = OI_CODEC_SBC_DecodeFrame(&ctx, &d, &left, pcm, &bytes);
    if (st == OI_CODEC_SBC_NOT_ENOUGH_HEADER_DATA || st == OI_CODEC_SBC_NOT_ENOUGH_BODY_DATA) return 0;
    if (st != OI_OK) { bt_sbc_info.errors++; *p += *n; *n = 0; return -1; }
    *p = d;
    *n = left;
    bt_sbc_info.frames++;
    bt_sbc_info.rate = ctx.common.frameInfo.frequency;
    bt_sbc_info.channels = ctx.common.frameInfo.nrof_channels;
    bt_sbc_info.bitpool = ctx.common.frameInfo.bitpool;
    bt_sbc_info.blocks = ctx.common.frameInfo.nrof_blocks;
    bt_sbc_info.subbands = ctx.common.frameInfo.nrof_subbands;
    bt_sbc_info.mode = ctx.common.frameInfo.mode;
    if (bt_sbc_info.channels == 2u) {
        k = bytes / 4u;                                         /* stereo frames, interleaved L, R */
        if (k > BT_SBC_MAX_SAMPLES) k = BT_SBC_MAX_SAMPLES;
        for (i = 0; i < k; i++) mono[i] = (int16_t)(((int32_t)pcm[2u * i] + pcm[2u * i + 1u]) >> 1);
        if (side) for (i = 0; i < k; i++) side[i] = (int16_t)(((int32_t)pcm[2u * i] - pcm[2u * i + 1u]) >> 1);   /* (test build 42) */
    } else {
        k = bytes / 4u;                                         /* one channel, written at the stride of two (pcmStride): the
                                                                 * decoder counts both slots, so half its bytes are samples (up to
                                                                 * test build 10 this read stale ones after each frame) */
        if (k > BT_SBC_MAX_SAMPLES) k = BT_SBC_MAX_SAMPLES;
        for (i = 0; i < k; i++) mono[i] = pcm[2u * i];
        if (side) for (i = 0; i < k; i++) side[i] = 0;
    }
    return (int)k;
}
