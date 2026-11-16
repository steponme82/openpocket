/* SPDX-License-Identifier: GPL-3.0-only */
/* The SBC decoder of the Bluetooth audio image (bt_sbc.c over third_party/sbc-android): one A2DP media packet at a time, one SBC
 * frame at a time, summed to mono (the pedal's output is mono). Pure C, no hardware: tests/test_bt_sbc.c. */
#pragma once
#include <stdint.h>

#define BT_SBC_MAX_SAMPLES 128u                 /* per channel in one frame: 16 blocks x 8 subbands */

typedef struct {
    uint32_t frames;                            /* frames decoded */
    uint32_t errors;                            /* frames refused (the rest of that packet is dropped) */
    uint32_t rate;                              /* the stream's sampling rate (Hz), 0 before the first frame */
    uint8_t channels, bitpool, blocks, subbands, mode;   /* mode: 0 mono, 1 dual, 2 stereo, 3 joint stereo */
} bt_sbc_info_t;
extern bt_sbc_info_t bt_sbc_info;

void bt_sbc_reset(void);
/* the offset of the first SBC frame in an A2DP media packet (its RTP header, contributors and extension, then the byte that
 * counts the frames), or len if there is none */
uint32_t bt_sbc_payload(const uint8_t *pkt, uint32_t len);
/* decodes the frame at *p (n bytes left there): mono gets its samples (BT_SBC_MAX_SAMPLES at most); *p and *n move past it.
 * Returns the samples written; 0 when what is left is not a whole frame; -1 when the frame is refused (drop the rest) */
int bt_sbc_frame(const uint8_t **p, uint32_t *n, int16_t *mono);
/* test build 42: as bt_sbc_frame, and side (0: none) gets (L - R) / 2, the stereo the mono mix leaves out (0 for a mono stream) */
int bt_sbc_frame2(const uint8_t **p, uint32_t *n, int16_t *mono, int16_t *side);
