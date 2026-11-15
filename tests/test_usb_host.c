/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — test build 41: the USB footswitch by cable, on a PC (no registers, no pedal).
 *   cc -O1 -Wall -Wno-unused-function -o build/test_usb_host tests/test_usb_host.c && build/test_usb_host
 * 1. felucca-bsp's descriptor parser (firmware/bsp/chip/usb_host_desc.h uh_parse_midi) on the M-VAVE Chocolate's layout as macOS
 *    reads it (2026-11-15: "SINCO", Jieli Technology, VID 0x4353 PID 0x4B4D, full speed, configuration 1, interface 0 HID with
 *    one endpoint, 1 Audio Control "USB-Midi", 2 MIDI Streaming with two endpoints, alternate setting 0 each). macOS does not
 *    show the endpoints' addresses and sizes: the class-compliant layout (USB-MIDI 1.0, 9-byte endpoints) with bulk and with
 *    interrupt endpoints, two numberings. The HID endpoint must never be taken for MIDI.
 * 2. Which of the footswitch's USB-MIDI event packets reach the pedal (uh_midi_ok): channel messages and clock, not SysEx.
 * 3. What the screen says (firmware/px_usbhost.h): the host's messages, an unusable device unplugged, PLAY's notes.
 * 4. The host's clock (pxu_ms_add): whole milliseconds of TIMER4's 24 MHz, across TIMER4's wrap (ms_now's would stop the
 *    host's search for good). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/bsp/chip/usb_host_desc.h"
#include "../firmware/px_usbhost.h"

static int failed;
static void ok(int c, const char *what) { printf("%-84s %s\n", what, c ? "ok" : "FAIL"); if (!c) failed++; }

/* the Chocolate's configuration: in_ep / out_ep the MIDI endpoints' addresses, type 2 bulk or 3 interrupt */
static uint32_t chocolate(uint8_t *d, uint8_t in_ep, uint8_t out_ep, uint8_t type, uint8_t mps)
{
    static const uint8_t hid[] = {
        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,          /* interface 0: HID, one endpoint */
        0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0xD5, 0x00,          /* HID descriptor */
        0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x01,                      /* EP 1 IN interrupt, 16 bytes, 1 ms: not MIDI */
        0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x02,          /* interface 1: Audio Control ("USB-Midi") */
        0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x02,          /* its header: one streaming interface, 2 */
        0x09, 0x04, 0x02, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,          /* interface 2: MIDI Streaming, two endpoints */
        0x07, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,                      /* MS header */
        0x06, 0x24, 0x02, 0x01, 0x01, 0x00, 0x06, 0x24, 0x02, 0x02, 0x02, 0x00,   /* MIDI IN jacks, embedded and external */
        0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,          /* MIDI OUT jacks */
        0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,
    };
    uint32_t n = 9;
    memcpy(d + n, hid, sizeof hid); n += sizeof hid;
    const uint8_t out[] = {0x09, 0x05, out_ep, type, mps, 0x00, type == 3u ? 1u : 0u, 0x00, 0x00, 0x05, 0x25, 0x01, 0x01, 0x01};
    const uint8_t in[] = {0x09, 0x05, in_ep, type, mps, 0x00, type == 3u ? 1u : 0u, 0x00, 0x00, 0x05, 0x25, 0x01, 0x01, 0x03};
    memcpy(d + n, out, sizeof out); n += sizeof out;
    memcpy(d + n, in, sizeof in); n += sizeof in;
    const uint8_t cfg[] = {0x09, 0x02, (uint8_t)n, (uint8_t)(n >> 8), 0x03, 0x01, 0x00, 0x80, 0x32};
    memcpy(d, cfg, sizeof cfg);
    return n;
}

static uint32_t pkt(uint32_t cin, uint32_t b0, uint32_t b1, uint32_t b2) { return cin | b0 << 8 | b1 << 16 | b2 << 24; }

int main(void)
{
    uint8_t d[320];
    uh_midi_t m;
    uint32_t n;

    /* ---- 1. the descriptor parser ---- */
    n = chocolate(d, 0x82, 0x02, 2, 64);
    ok(n == 126u && uh_parse_midi(d, n, &m), "the Chocolate (bulk, EP 2 in and out): a MIDI interface is found");
    ok(m.cfg == 1u && m.iface == 2u && m.alt == 0u, "  configuration 1, interface 2 (behind HID at 0 and Audio Control at 1), alt 0");
    ok(m.in_ep == 2u && m.in_type == 2u && m.in_mps == 64u, "  its IN endpoint, not HID's EP 1 IN: EP 2, bulk, 64 bytes");
    ok(m.out_ep == 2u && m.out_type == 2u && m.out_mps == 64u, "  its OUT endpoint: EP 2, bulk, 64 bytes");
    n = chocolate(d, 0x83, 0x03, 3, 16);
    ok(uh_parse_midi(d, n, &m) && m.iface == 2u && m.in_ep == 3u && m.in_type == 3u && m.in_interval == 1u && m.in_mps == 16u &&
       m.out_ep == 3u, "interrupt endpoints (EP 3, 16 bytes, 1 ms): found, the interval kept");
    n = chocolate(d, 0x81, 0x01, 2, 64);
    ok(uh_parse_midi(d, n, &m) && m.iface == 2u && m.in_ep == 1u && m.out_ep == 1u,
       "MIDI on EP 1 too (HID's number, another interface): interface 2's endpoints");
    n = chocolate(d, 0x82, 0x02, 2, 64);
    ok(uh_parse_midi(d, n - 5u, &m) && m.in_ep == 2u, "the last class descriptor cut off (a short read): still found");
    ok(!uh_parse_midi(d, 9u + 25u + 18u, &m), "only HID and Audio Control (a footswitch with no MIDI streaming): NO MIDI");
    {
        uint8_t big[320];
        n = chocolate(big, 0x82, 0x02, 2, 64);
        big[n - 28u + 4u] = 0x00; big[n - 14u + 4u] = 0x00;       /* wMaxPacketSize 0 on both: unusable */
        ok(!uh_parse_midi(big, n, &m), "MIDI endpoints of 0 bytes: not usable, NO MIDI");
    }

    /* ---- 2. the footswitch's packets ---- */
    ok(uh_midi_ok(pkt(0xC, 0xC0, 4, 0)) == pkt(0xC, 0xC0, 4, 0), "Program Change (preset 5, channel 1): passed on");
    ok(uh_midi_ok(pkt(0xB, 0xB0, 118, 127)) == pkt(0xB, 0xB0, 118, 127), "Control Change 118 = 127 (next preset): passed on");
    ok(uh_midi_ok(pkt(0xB, 0xB0, 116, 0)) == pkt(0xB, 0xB0, 116, 0), "Control Change 116 = 0 (a tap's release): passed on");
    ok(uh_midi_ok(pkt(0x1B, 0xB3, 24, 127)) == pkt(0xB, 0xB3, 24, 127), "cable 1, channel 4: passed on, the cable dropped");
    ok(uh_midi_ok(pkt(0xF, 0xF8, 0, 0)) != 0u && uh_midi_ok(pkt(0xF, 0xFE, 0, 0)) == 0u, "clock passed on, active sensing not");
    ok(uh_midi_ok(pkt(0x4, 0xF0, 0x7D, 0x01)) == 0u && uh_midi_ok(pkt(0x7, 0x01, 0x02, 0xF7)) == 0u,
       "SysEx: not passed on (the installer's and the editor's commands are a computer's)");
    ok(uh_midi_ok(pkt(0xB, 0x90, 1, 1)) == 0u && uh_midi_ok(pkt(0xB, 0xB0, 0x80, 1)) == 0u,
       "a CIN that does not match its status, a data byte with bit 7: dropped");

    /* ---- 3. the screen's state ---- */
    {
        pxu_t u = {0};
        ok(u.st == PXU_OFF && pxu_note(&u, 0) == 0u, "power-on: COMPUTER, nothing to say");
        pxu_msg(&u, 0, 10);
        ok(u.st == PXU_OFF, "no message: no change");
        pxu_msg(&u, PXU_MSG_ON, 100);
        ok(u.st == PXU_WAIT && pxu_note(&u, 100) == 0u, "FOOTSW: NOTHING ATTACHED, no note (the page is open)");
        pxu_msg(&u, PXU_MSG_MIDI, 1000);
        ok(u.st == PXU_OK && pxu_note(&u, 1000) == PXU_OK && pxu_note(&u, 3999) == PXU_OK && pxu_note(&u, 4000) == 0u,
           "CONNECTED: PLAY's note for 3 s");
        pxu_msg(&u, PXU_MSG_LOST, 20000);
        ok(u.st == PXU_LOST && pxu_note(&u, 20000) == PXU_LOST && pxu_note(&u, 20000u + 600000u) == PXU_LOST,
           "LOST: the note stays (ten minutes later too)");
        pxu_line(&u, 0, 20100); pxu_line(&u, 0, 21000);
        ok(u.st == PXU_LOST, "  and the line idle does not clear it: the footswitch is still missing");
        pxu_msg(&u, PXU_MSG_MIDI, 30000);
        ok(u.st == PXU_OK && pxu_note(&u, 30001) == PXU_OK, "back: CONNECTED, LOST's note gone");
        pxu_msg(&u, PXU_MSG_NOMIDI, 40000);
        pxu_line(&u, 1, 40010);
        ok(u.st == PXU_NOMIDI && pxu_note(&u, 40010) == PXU_NOMIDI, "NO MIDI INTERFACE while the device is there, the note");
        pxu_line(&u, 0, 40020); pxu_line(&u, 1, 40100); pxu_line(&u, 0, 40150); pxu_line(&u, 0, 40349);
        ok(u.st == PXU_NOMIDI, "  a gap on the line under 200 ms: still there");
        pxu_line(&u, 0, 40350);
        ok(u.st == PXU_WAIT, "  unplugged for 200 ms: NOTHING ATTACHED again");
        pxu_msg(&u, PXU_MSG_LOW, 50000);
        pxu_line(&u, 0, 50000); pxu_line(&u, 0, 50300);
        ok(u.st == PXU_WAIT && pxu_note(&u, 50300) == PXU_LOW, "LOW SPEED DEVICE: the same; its note runs its 3 s");
        pxu_msg(&u, PXU_MSG_LOST, 60000);
        pxu_msg(&u, PXU_MSG_OFF, 61000);
        ok(u.st == PXU_OFF && pxu_note(&u, 61000) == 0u, "COMPUTER again: OFF, LOST's note gone");
        pxu_msg(&u, 99, 62000);
        ok(u.st == PXU_OFF, "an unknown message: ignored");
    }

    /* ---- 4. the host's clock ---- */
    {
        pxu_ms_t c = {0};
        uint32_t ms = 0, t4 = 0, i, mono = 1;
        const uint32_t TPM = 24000u;                    /* TIMER4: 24 ticks a microsecond */
        ms += pxu_ms_add(&c, t4 += 12000u, TPM);
        ok(ms == 0u, "half a millisecond: 0");
        ms += pxu_ms_add(&c, t4 += 12000u, TPM);
        ok(ms == 1u, "the other half: 1");
        for (i = 0; i < 2000u * 200u; i++) {            /* 200 s of polls at 2 kHz, through TIMER4's wrap at 178.9 s */
            uint32_t was = ms;
            ms += pxu_ms_add(&c, t4 += 12000u, TPM);
            if (ms < was) mono = 0;
        }
        ok(mono && ms == 1u + 200000u, "200 s of polls, across TIMER4's wrap: 200,001 ms, never back");
        ok((uint32_t)(t4 / TPM) < 179000u && ms > 179000u, "  (where ms_now, TIMER4 / 24000, started again from 0)");
        ms += pxu_ms_add(&c, t4 += 50u * TPM + 5u, TPM);
        ok(ms == 1u + 200000u + 50u, "a poll 50 ms late (interrupts held off): the 50 ms all counted");
    }

    printf("%s\n", failed ? "USB footswitch: FAIL" : "USB footswitch: all ok");
    return failed != 0;
}
