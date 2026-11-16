/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — USB audio on a PC: the pedal's USB device code (firmware/bsp/chip/usb.c, built as firmware/px_usb.h
 * builds it) and its audio glue (firmware/px_uaudio.h) against a model of the USB controller and of a computer.
 *   cc -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -Ifirmware/bsp/chip tests/test_usb_audio.c -lm -o build/test_usb_audio
 *   build/test_usb_audio            the tests
 *   build/test_usb_audio desc       the device and configuration descriptors with USB AUDIO off, as hex
 * Built with -DT_REF (FELUCCA_UAC 0: the USB device every OpenPocket before v42 was), `desc` prints that device's;
 * tests/run_tests.sh compares the two, byte for byte.
 * Checked here:
 *   - USB AUDIO off: the descriptors a computer reads, and what it gets when it asks for the streams (refused);
 *   - USB AUDIO on: the descriptors' structure (interfaces in the order macOS 13-15 need, both isochronous endpoints),
 *     the streams' requests, the switch off again;
 *   - the record stream over 10 minutes with the pedal's clock 300 ppm fast or slow against the computer's, and with
 *     blocks that start up to 0.7 ms late: every frame the pedal made arrives once, in order (no gap, no repeat), the
 *     ring's fill stays in its band;
 *   - the playback stream over 10 minutes at the same clock differences: no gap, the ring neither empties nor
 *     overflows, the level follows MASTER, the sum with the pedal's sound is clipped, a flash write's mute stops it.
 * Built with -DPX_OWN_FX=1 -DOP_BT_AUDIO=1 (the Bluetooth audio image, test build 43: its 512-frame playback ring) the same,
 * and USB SEND (DI, DI + OUT, the two channels OUTPUT splits), USB RETURN in the chain (the playback read in a_block's place
 * over 10 minutes at the same clock differences: no gap, no jump, the input's scale; not mixed at the output then) and
 * GUITAR CUT only while the computer plays. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1_usb.h"

/* ---- the USB controller: the registers usb.c reads and writes through fm1_usb.h ---- */
enum { R_FADDR = 0, R_INTRUSB = 6, R_INDEX = 14, R_TXMAXP = 16, R_CSR0 = 17, R_TXCSR1 = 17, R_TXCSR2 = 18,
       R_RXCSR1 = 20, R_RXCSR2 = 21, R_COUNT = 22, R_COUNT2 = 23 };
static uint8_t m_reg[16], m_ep[5][8];           /* common; per endpoint: registers 16..23 */
static uint32_t m_rd;                           /* the register being read */
static uint8_t m_e0_in[512];                    /* EP0: what the device sent since the SETUP */
static uint32_t m_e0_n, m_e0_stall;
static uint32_t m_tx4[64], m_tx4_n;             /* EP4 IN: the packet queued (bytes), copied when the device queues it */
static uint32_t m_off;                          /* fm1_usb_off calls: the pedal left the bus */
static void m_wr(uint32_t r, uint32_t v)
{
    uint32_t e = m_reg[R_INDEX] <= 4u ? m_reg[R_INDEX] : 0u;
    if (r < 16u) { m_reg[r] = (uint8_t)v; return; }
    if (r > 23u) return;
    if (e == 0u && r == R_CSR0) {
        if (v & 0x40u) m_ep[0][1] &= (uint8_t)~0x01u;            /* ServicedRxPktRdy */
        if (v & 0x20u) m_e0_stall = 1;                           /* SendStall */
        return;                                                  /* (an IN packet: the computer takes it at once) */
    }
    if (r == R_TXCSR1) {
        if (v & 0x08u) m_ep[e][1] &= (uint8_t)~0x01u;            /* FlushFIFO */
        if (v & 0x01u) m_ep[e][1] = 0x01u;                       /* TxPktRdy (UnderRun cleared) */
        return;
    }
    if (r == R_RXCSR1) {
        if (v & 0x10u) m_ep[e][4] &= (uint8_t)~0x01u;            /* FlushFIFO: the packet is taken */
        return;
    }
    m_ep[e][r - 16u] = (uint8_t)v;
}
static uint32_t m_data(void)
{
    uint32_t e = m_reg[R_INDEX] <= 4u ? m_reg[R_INDEX] : 0u;
    return m_rd < 16u ? m_reg[m_rd] : m_rd <= 23u ? m_ep[e][m_rd - 16u] : 0u;
}
static void m_rd_start(uint32_t r) { m_rd = r; }
static uint32_t m_one(void) { return 1; }
static void m_nop(void) { }
static void m_off_f(void) { m_off++; }
static void m_attach(void *p) { (void)p; }
static void m_buf(uint32_t ep, void *p) { (void)ep; (void)p; }
static void m_ep_enable(uint32_t eps) { (void)eps; }
static void m_send(uint32_t ep, void *p, uint32_t n)
{
    if (ep == 4u && n <= sizeof m_tx4) { memcpy(m_tx4, p, n); m_tx4_n = n; }
}
static void m_ep0_send(void *p, uint32_t n)
{
    if (m_e0_n + n <= sizeof m_e0_in) { memcpy(m_e0_in + m_e0_n, p, n); m_e0_n += n; }
}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }

#define fm1_usb_sie_on m_one
#define fm1_usb_sie_done m_one
#define fm1_usb_sie_wr_start m_wr
#define fm1_usb_sie_rd_start m_rd_start
#define fm1_usb_sie_data m_data
#define fm1_usb_reset m_nop
#define fm1_usb_off m_off_f
#define fm1_usb_attach m_attach
#define fm1_usb_sof_take m_one
#define fm1_usb_rx_sync m_nop
#define fm1_usb_ep_enable m_ep_enable
#define fm1_usb_ep_txbuf m_buf
#define fm1_usb_ep_rxbuf m_buf
#define fm1_usb_ep0_buf(p) ((void)(p))
#define fm1_usb_ep0_send m_ep0_send
#define fm1_usb_ep_send m_send
#define fm1_usb_ep4_txbuf(p) ((void)(p))
#define fm1_usb_ep4_rxbuf(p) ((void)(p))
#define fm1_usb_ep4_send(p, n) m_send(4, p, n)

/* ---- usb.c, as firmware/px_usb.h builds it (T_REF: as it was built before USB audio) ---- */
#define A_FR 128u
#define RING_PUBLISH() ((void)0)
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_USB_PRODUCT "OpenPocket"
#define FELUCCA_USB_MANUFACTURER "OpenPocket"
#ifdef T_REF
#define FELUCCA_UAC 0
#else
#include "px_uaconf.h"                          /* the pedal's streams and rings (px_usb.h) */
#endif
#include "usb.c"
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) { }

/* ---- the computer's side of endpoint 0 ---- */
/* a control request; an OUT data stage if out, else what the device sends goes to m_e0_in. Returns 1 = refused (STALL) */
static int ctrl(uint32_t type, uint32_t req, uint32_t value, uint32_t index, uint32_t len, const uint8_t *out)
{
    const uint8_t s[8] = {(uint8_t)type, (uint8_t)req, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index,
                          (uint8_t)(index >> 8), (uint8_t)len, (uint8_t)(len >> 8)};
    int guard = 64;
    m_e0_n = 0; m_e0_stall = 0;
    memcpy(ep0buf, s, 8);
    m_reg[R_INDEX] = 0; m_ep[0][1] = 0x01; m_ep[0][6] = 8;
    ep0_service();
    if (out && !m_e0_stall) {
        memcpy(ep0buf, out, len);
        m_ep[0][1] = 0x01; m_ep[0][6] = (uint8_t)len;
        ep0_service();
    }
    while (usb.e0_tx && guard--) ep0_service();
    return (int)m_e0_stall;
}
static uint32_t get_descriptor(uint32_t type, uint8_t *d, uint32_t max)
{
    if (ctrl(0x80, 6, type << 8, 0, max, 0)) return 0;
    memcpy(d, m_e0_in, m_e0_n);
    return m_e0_n;
}

#ifdef T_REF
int main(int argc, char **argv)
#else
static int desc_main(void)
#endif
{
    uint8_t d[512];
    uint32_t n, i;
#ifdef T_REF
    (void)argc; (void)argv;
#endif
    usb_start();
    n = get_descriptor(1, d, 18);
    printf("device ");
    for (i = 0; i < n; i++) printf("%02x", d[i]);
    n = get_descriptor(2, d, 9);                 /* as a computer does: the header, then the whole */
    n = get_descriptor(2, d, d[2] | (uint32_t)d[3] << 8);
    printf("\nconfiguration ");
    for (i = 0; i < n; i++) printf("%02x", d[i]);
    printf("\n");
    return 0;
}

#ifndef T_REF
/* ---- the pedal's side: what firmware/px_uaudio.h needs of px_audio.h and px_midi.h ---- */
static float a_out[A_FR], a_gv = 1.0f;
static volatile uint8_t a_mute, a_ua;
static uint32_t a_uat, a_swaps;
#if PX_OWN_FX
#include "px_dsp.h"
#include "px_midi.h"                            /* (px_ua_on, px_ua_route and its fields) */
static float a_in[A_FR], a_outr[A_FR], a_rx[A_FR];
static volatile uint8_t a_res_sp;
static int8_t a_rt_at = PX_RT_NONE;
static uint8_t a_rt_x, a_rt_cut, a_rt_out = 1u;
#else
static uint8_t px_ua_on;
#endif
#include "px_uaudio.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)

static void enumerate(void)                     /* a computer finds the pedal: address, descriptors, configuration 1 */
{
    uint8_t d[512];
    memset(m_ep, 0, sizeof m_ep);
    usb_start();
    usb.config = 0;
    get_descriptor(1, d, 18);
    ctrl(0x00, 5, 7, 0, 0, 0);
    get_descriptor(2, d, 512);
    CHECK(!ctrl(0x00, 9, 1, 0, 0, 0) && usb.config == 1u, "SET_CONFIGURATION 1");
}

/* the configuration descriptor's parts: interfaces (number, alt, class, subclass, endpoints) and endpoints */
typedef struct { uint8_t num, alt, cls, sub, neps; } t_if;
typedef struct { uint8_t addr, attr; uint16_t maxp; uint8_t ifn, alt; } t_ep;
static uint32_t parse_cfg(const uint8_t *d, uint32_t n, t_if *ifs, uint32_t *nif, t_ep *eps, uint32_t *nep, int *ok)
{
    uint32_t i = 0, ac = 0;
    *nif = *nep = 0; *ok = 1;
    while (i + 2u <= n) {
        if (d[i] < 2u || i + d[i] > n) { *ok = 0; break; }
        if (d[i + 1] == 4u && *nif < 16u) ifs[(*nif)++] = (t_if){d[i + 2], d[i + 3], d[i + 5], d[i + 6], d[i + 4]};
        if (d[i + 1] == 5u && *nep < 16u && *nif)
            eps[(*nep)++] = (t_ep){d[i + 2], d[i + 3], (uint16_t)(d[i + 4] | d[i + 5] << 8), ifs[*nif - 1u].num, ifs[*nif - 1u].alt};
        if (d[i + 1] == 0x24u && d[i + 2] == 1u && *nif == 1u) ac = i;     /* the audio control header */
        i += d[i];
    }
    if (i != n) *ok = 0;
    return ac;
}

static void test_off(void)
{
    uint8_t d[512];
    px_ua_on = 0; usb_uac_on = 0;
    enumerate();
    CHECK(get_descriptor(1, d, 18) == 18u && d[12] == 0x00 && d[13] == 0x03, "off: bcdDevice 3.00");
    CHECK(ctrl(0x01, 11, 1, 1, 0, 0), "off: SET_INTERFACE 1 alt 1 is refused (interface 1 is MIDI)");
    CHECK(!ctrl(0x01, 11, 0, 1, 0, 0), "off: SET_INTERFACE 1 alt 0 is accepted");
    CHECK(!uac.alt && !uo.alt, "off: no stream opens");
    CHECK(ctrl(0x22, 1, 0x0100, 0x84, 3, (const uint8_t *)"\x44\xAC\x00"), "off: the sampling frequency request is refused");
    CHECK(ctrl(0x02, 1, 0, 0x84, 0, 0), "off: CLEAR_FEATURE on endpoint 4 is refused");
    for (int i = 0; i < 100; i++) usb_poll();
    CHECK(m_tx4_n == 0u, "off: nothing is ever queued on endpoint 4");
}

static void test_on(void)
{
    uint8_t d[512], d1[18];
    t_if ifs[16]; t_ep eps[16];
    uint32_t n, nif, nep, ac, i;
    int ok;
    px_ua_on = 1; usb_uac_on = 1;
    enumerate();
    CHECK(get_descriptor(1, d1, 18) == 18u && d1[13] == 0x03 && d1[12] != 0x00, "on: another bcdDevice (the computer learns the device anew)");
    CHECK(d1[8] == 0x09 && d1[9] == 0x12 && d1[10] == 0x01 && d1[11] == 0x00, "on: the same vendor and product id");
    n = get_descriptor(2, d, 512);
    ac = parse_cfg(d, n, ifs, &nif, eps, &nep, &ok);
    CHECK(ok && n == (d[2] | (uint32_t)d[3] << 8) && d[4] == 4u, "on: the configuration descriptor is whole, 4 interfaces");
    CHECK(nif == 6u && ifs[0].num == 0u && ifs[0].sub == 1u && ifs[1].num == 1u && ifs[1].sub == 2u && ifs[2].num == 1u &&
          ifs[2].alt == 1u && ifs[3].num == 2u && ifs[3].sub == 2u && ifs[4].num == 2u && ifs[4].alt == 1u &&
          ifs[5].num == 3u && ifs[5].sub == 3u,
          "on: audio control, the record stream, the playback stream, then MIDI");
    for (i = 0; i < nif; i++) CHECK(ifs[i].cls == 1u, "on: every interface is audio class");
    CHECK(ifs[1].neps == 0u && ifs[3].neps == 0u, "on: alt 0 of each stream has no endpoint (no bandwidth while idle)");
    CHECK(ac && d[ac] == 11u && d[ac + 7] == 3u && d[ac + 8] == 1u && d[ac + 9] == 2u && d[ac + 10] == 3u,
          "on: the audio control header lists interfaces 1, 2, 3");
    {
        int in = 0, out = 0, mi = 0, mo = 0;
        for (i = 0; i < nep; i++) {
            if (eps[i].addr == 0x84u) in = (eps[i].attr == 0x05u && eps[i].maxp >= 46u * 4u && eps[i].ifn == 1u && eps[i].alt == 1u);
            if (eps[i].addr == 0x04u) out = (eps[i].attr == 0x09u && eps[i].maxp >= 45u * 4u && eps[i].ifn == 2u && eps[i].alt == 1u);
            if (eps[i].addr == 0x81u) mi = (eps[i].attr == 2u && eps[i].ifn == 3u);
            if (eps[i].addr == 0x01u) mo = (eps[i].attr == 2u && eps[i].ifn == 3u);
        }
        CHECK(nep == 4u && in && out && mi && mo, "on: EP4 IN isochronous asynchronous, EP4 OUT isochronous adaptive, EP1 bulk both ways for MIDI");
    }
    CHECK(!ctrl(0x01, 11, 1, 1, 0, 0) && uac.alt == 1u, "on: SET_INTERFACE 1 alt 1 opens the record stream");
    CHECK(!ctrl(0x81, 10, 0, 1, 1, 0) && m_e0_n == 1u && m_e0_in[0] == 1u, "on: GET_INTERFACE 1 answers 1");
    CHECK(!ctrl(0x01, 11, 1, 2, 0, 0) && uo.alt == 1u, "on: SET_INTERFACE 2 alt 1 opens the playback stream");
    CHECK(!ctrl(0x22, 1, 0x0100, 0x84, 3, (const uint8_t *)"\x44\xAC\x00"), "on: the sampling frequency 44100 is accepted");
    CHECK(!ctrl(0xA2, 0x81, 0x0100, 0x84, 3, 0) && m_e0_n == 3u && !memcmp(m_e0_in, "\x44\xAC\x00", 3), "on: the sampling frequency reads 44100");
    CHECK(!ctrl(0x01, 11, 0, 3, 0, 0) && ctrl(0x01, 11, 1, 3, 0, 0), "on: the MIDI interface has alt 0 only");
    CHECK(!ctrl(0x01, 11, 0, 1, 0, 0) && !uac.alt && !ctrl(0x01, 11, 0, 2, 0, 0) && !uo.alt, "on: alt 0 closes each stream");
}

static void test_switch(void)                   /* the setting changed while a computer records */
{
    uint32_t off0, t;
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1;
    enumerate();
    ctrl(0x01, 11, 1, 1, 0, 0);
    off0 = m_off;
    px_ua_on = 0;
    for (t = 0; t < 149u; t++) px_ua_service(1000u + t);
    CHECK(usb_uac_on == 1u && m_off == off0, "switch: nothing for the first 150 ms (the editor's reply leaves)");
    px_ua_service(1150u);
    CHECK(usb_uac_on == 0u && m_off == off0 + 1u && !usb.up && !usb.config, "switch: then the pedal leaves the bus");
    CHECK(!a_ua && !uac.alt && !uac.go, "switch: the audio interrupt no longer feeds the rings");
    CHECK(usb_retry_ms == 1150u, "switch: it stays off the bus for a second");
    usb_retry(1150u + 999u);
    CHECK(!usb.up, "switch: not back before the second is over");
    usb_retry(1150u + 1000u);
    CHECK(usb.up, "switch: back after it");
    px_ua_on = 1;
    px_ua_service(3000u); px_ua_service(3150u);
    CHECK(usb_uac_on == 1u && a_ua == 1u && m_off == off0 + 2u, "switch: and on again the same way");
}

/* ---- the streams over time. The pedal: a poll every 0.5 ms, a block of 128 frames every 128 / 44117.647 s, both on its
 * own clock (ppm against the computer's); the computer: a frame every 1 ms ---- */
#define T_FS 1.0e12                             /* femtoseconds in a millisecond */
static uint32_t rnd_s = 12345u;
static uint32_t rnd(void) { rnd_s = rnd_s * 1664525u + 1013904223u; return rnd_s >> 8; }
static int32_t ramp(uint32_t k) { return (int32_t)(k % 20000u) - 10000; }   /* the record test's signal, frame k */

static void test_record(double ppm, int late, const char *name)
{
    const double dev = 1.0 + ppm * 1e-6, t_poll = 0.5 * T_FS * dev, t_blk = 128.0 / 44117.647 * 1000.0 * T_FS * dev;
    double tp = t_poll, tb0 = t_blk, tb = t_blk, th = T_FS, end = 600.0e3 * T_FS;
    uint32_t made = 0, got = 0, bad = 0, zeros = 0, short_ = 0, pk[3] = {0, 0, 0}, missed0 = 0;
    uint32_t un0, ov0, st0, up0, dn0;
    int32_t d[2 * A_FR];
    char msg[200];
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1; a_gv = 1.0f; a_mute = 0;
    enumerate();
    ctrl(0x01, 11, 1, 1, 0, 0);
    un0 = uac.underruns; ov0 = uac.overruns; st0 = uac.stalls; up0 = uac.adj_up; dn0 = uac.adj_down;   /* (counted since boot) */
    while (th < end) {
        if (tp <= tb && tp <= th) { usb_poll(); tp += t_poll; continue; }
        if (tb <= th) {                          /* a block: the frames made.. in a_out; they count if the ring took them */
            const uint32_t ov = uac.overruns;
            for (uint32_t i = 0; i < A_FR; i++) {
                const int32_t k = ramp(made + i);
                a_out[i] = ((float)k + (k < 0 ? -0.25f : 0.25f)) / 32767.0f;
            }
            memset(d, 0, sizeof d);
            px_ua_block(d, A_FR);
            if (uac.feed && uac.overruns == ov) made += A_FR;
            tb0 += t_blk;                        /* the next one: on time, or up to 0.7 ms late once in 50 (late) */
            tb = tb0 + (late && rnd() % 50u == 0u ? (double)(rnd() % 700u) / 1000.0 * T_FS : 0.0);
            continue;
        }
        /* the computer's frame: it takes the packet queued, if any */
        if (m_ep[4][1] & 0x01u) {
            const uint32_t nf = m_tx4_n / 4u;
            m_ep[4][1] &= (uint8_t)~0x01u;
            if (nf >= 43u && nf <= 46u) pk[nf == 44u ? 0 : nf == 45u ? 1 : 2]++; else short_++;
            for (uint32_t i = 0; i < nf; i++) {
                const int32_t l = (int16_t)m_tx4[i], r = (int16_t)(m_tx4[i] >> 16);
                if (l != r) bad++;
                if (!got && l == 0 && zeros < 100000u) { zeros++; continue; }       /* the silence before the first block */
                if (l != ramp(got)) { if (bad < 3u) printf("  %s: frame %u is %d, not %d\n", name, got, l, ramp(got)); bad++; }
                got++;
            }
        } else if (uac.flowing) m_ep[4][1] |= 0x04u;                                 /* an IN token found nothing */
        if (th < 1000.0 * T_FS) missed0 = uac.missed;
        th += T_FS;
    }
    snprintf(msg, sizeof msg, "%s: every frame once and in order (%u made, %u received, %u wrong)", name, made, got, bad);
    CHECK(got > 26000000u && bad == 0u && made - got <= 400u, msg);
    snprintf(msg, sizeof msg, "%s: no gap (ring empty %u, ring full %u, no packet ready %u, stalls %u, odd packets %u)", name,
             uac.underruns - un0, uac.overruns - ov0, uac.missed - missed0, uac.stalls - st0, short_);
    CHECK(uac.underruns == un0 && uac.overruns == ov0 && uac.missed == missed0 && uac.stalls == st0 && !short_, msg);
    snprintf(msg, sizeof msg, "%s: the ring's fill at a block's start leaves 0.6 ms in hand and room for a block (%u..%u of %u)", name,
             uac.fill_lo, uac.fill_hi, (unsigned)UA_N);
    CHECK((late || uac.fill_lo >= 26u) && uac.fill_hi + A_FR + 46u < UA_N, msg);   /* (a late block finds less: no gap is the test) */
    printf("  %s: packets of 44 / 45 / other: %u / %u / %u, steered +%u -%u, fill %u..%u, %.3f frames a ms\n", name, pk[0], pk[1],
           pk[2], uac.adj_up - up0, uac.adj_down - dn0, uac.fill_lo, uac.fill_hi, (double)got / 600000.0);
}

static void test_play(double ppm, const char *name)
{
    const double dev = 1.0 + ppm * 1e-6, t_poll = 0.5 * T_FS * dev, t_blk = 128.0 / 44117.647 * 1000.0 * T_FS * dev;
    const double w = 2.0 * 3.14159265358979 * 200.0 / 44100.0;   /* 200 Hz, L; R the same, inverted */
    double tp = t_poll, tb = t_blk, th = T_FS, end = 600.0e3 * T_FS;
    uint32_t sent = 0, acc = 0, n_out = 0, jumps = 0, quiet = 0, lr = 0, fmin = ~0u, fmax = 0, un0, ov0;
    int32_t d[2 * A_FR], last = 0, peak = 0;
    char msg[200];
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1; a_gv = 1.0f; a_mute = 0;
    enumerate();
    ctrl(0x01, 11, 1, 2, 0, 0);
    un0 = uo.underruns; ov0 = uo.overruns;      /* (counted since boot) */
    while (th < end) {
        if (tp <= tb && tp <= th) { usb_poll(); tp += t_poll; continue; }
        if (tb <= th) {
            memset(d, 0, sizeof d);
            memset(a_out, 0, sizeof a_out);
            px_ua_block(d, A_FR);
            if (uo.go && th > 2000.0 * T_FS) {   /* (after the start) */
                const uint32_t f = uo_w - uo_r;
                if (f < fmin) fmin = f;
                if (f > fmax) fmax = f;
                for (uint32_t i = 0; i < A_FR; i++) {
                    const int32_t l = d[2 * i], r = d[2 * i + 1];
                    const int32_t st = l > last ? l - last : last - l;
                    if (n_out && st > 2560000 / 30) jumps++;       /* a 200 Hz sine of 10000 x 256 moves 73,000 a frame at most */
                    if (l + r > 512 || l + r < -512) lr++;
                    if (l > peak) peak = l;
                    last = l; n_out++;
                }
            } else if (th > 2000.0 * T_FS) quiet++;
            tb += t_blk;
            continue;
        }
        if (!(m_ep[4][4] & 0x01u)) {             /* the computer's frame: 44 or 45 frames (44.1 a ms on its clock) */
            uint32_t nf = 44u;
            acc += 100u;
            if (acc >= 1000u) { acc -= 1000u; nf++; }
            for (uint32_t i = 0; i < nf; i++) {
                const int16_t s = (int16_t)lrint(10000.0 * sin(w * (double)(sent + i)));
                ep4rx[i] = (uint32_t)(uint16_t)s | (uint32_t)(uint16_t)(int16_t)-s << 16;
            }
            sent += nf;
            m_ep[4][4] |= 0x01u; m_ep[4][6] = (uint8_t)(nf * 4u); m_ep[4][7] = 0;
        }
        th += T_FS;
    }
    snprintf(msg, sizeof msg, "%s: plays without a gap (%u frames out, %u jumps, %u silent blocks, ring empty %u, ring full %u)", name,
             n_out, jumps, quiet, uo.underruns - un0, uo.overruns - ov0);
    CHECK(n_out > 26000000u && !jumps && !quiet && uo.underruns == un0 && uo.overruns == ov0, msg);
    snprintf(msg, sizeof msg, "%s: left and right kept apart, full level at MASTER 100 (peak %d of 2560000, %u frames off)", name, (int)peak, lr);
    CHECK(!lr && peak > 2540000 && peak <= 2560000, msg);
    snprintf(msg, sizeof msg, "%s: the ring's fill after a block's read leaves 0.6 ms in hand and stays inside it (%u..%u of %u)", name, fmin, fmax, (unsigned)UO_N);
    CHECK(fmin >= 26u && fmax + 45u < UO_N, msg);
    printf("  %s: fill %u..%u frames (target %u)\n", name, fmin, fmax, (unsigned)UO_TGT);
}

static uint32_t play_v = 20000u;
static void play_block(int32_t *d, int32_t pedal)    /* a block with the computer playing play_v (+20000) on both channels */
{
    for (int k = 0; k < 6; k++) {                 /* 3 ms of its packets, the polls taking them */
        if (!(m_ep[4][4] & 0x01u)) {
            for (uint32_t i = 0; i < 45u; i++) ep4rx[i] = play_v | play_v << 16;
            m_ep[4][4] |= 0x01u; m_ep[4][6] = 180u; m_ep[4][7] = 0;
        }
        usb_poll();
    }
    for (uint32_t i = 0; i < 2u * A_FR; i++) d[i] = pedal;
    px_ua_block(d, A_FR);
}
static void test_mix(void)
{
    int32_t d[2 * A_FR];
    int i;
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1; a_gv = 1.0f; a_mute = 0;
    enumerate();
    ctrl(0x01, 11, 1, 2, 0, 0);
    for (i = 0; i < 40; i++) play_block(d, 0);
    CHECK(uo.go && d[0] == 20000 * 256 && d[255] == 20000 * 256, "mix: the computer's playback at MASTER 100: 16 bits to the output's 24");
    a_gv = 0.25f;
    play_block(d, 0);
    CHECK(d[0] == 20000 * 64 && d[255] == 20000 * 64, "mix: it follows MASTER");
    a_gv = 1.0f;
    play_block(d, 6000000);
    CHECK(d[0] == 8388607 && d[255] == 8388607, "mix: the sum with the pedal's sound is clipped to 24 bits");
    play_block(d, -8000000);
    CHECK(d[0] == -8000000 + 20000 * 256, "mix: and added as it is below that");
    a_mute = 1;
    play_block(d, 0);
    CHECK(d[0] == 0 && d[255] == 0 && !uo.go, "mix: a flash write's mute stops the playback (the output must be zeros)");
    a_mute = 0;
    for (i = 0; i < 40; i++) play_block(d, 0);
    CHECK(uo.go && d[0] == 20000 * 256, "mix: it starts again after it");
    ctrl(0x01, 11, 0, 2, 0, 0);
    play_block(d, 1234);
    CHECK(d[0] == 1234 && d[255] == 1234, "mix: with the stream closed the output is the pedal's alone");
    {   /* v42: the stream opened again plays what the computer sends now, not what the ring still held of the last one */
        int old = 0, heard = 0;
        CHECK(uo_w - uo_r >= 100u, "mix: (the ring still holds frames of the stream that closed)");
        ctrl(0x01, 11, 1, 2, 0, 0);
        play_v = 5000u;
        for (i = 0; i < 40; i++) {
            play_block(d, 0);
            for (int k = 0; k < 2 * (int)A_FR; k++) { if (d[k] == 20000 * 256) old++; else if (d[k] == 5000 * 256) heard++; }
        }
        play_v = 20000u;
        CHECK(uo.go && !old && heard > 30 * 2 * (int)A_FR, "mix: a stream opened again does not play the end of the one before");
        ctrl(0x01, 11, 0, 2, 0, 0);
    }
}
static void test_knee(void)                       /* the record stream's level: MASTER at 100, the output's soft knee */
{
    static const float in[6] = {0.0f, 0.5f, -0.5f, 0.8f, 1.0f, -4.0f};
    int32_t d[2 * A_FR];
    uint32_t i, k;
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1;
    enumerate();
    ctrl(0x01, 11, 1, 1, 0, 0);
    uac.flowing = 1;                              /* (the computer reads) */
    px_ua_block(d, A_FR);                         /* primes the ring */
    for (i = 0; i < A_FR; i++) a_out[i] = in[i % 6u];
    k = ua_w;
    px_ua_block(d, A_FR);
    CHECK(ua_w == k + A_FR, "level: a block of 128 frames into the ring");
    {
        const int16_t *s = (const int16_t *)&ua_ring[k & (UA_N - 1u)];
        const float t = (4.0f - 0.8f) * 5.0f, m4 = 0.8f + 0.2f * t / (1.0f + t);
        CHECK(s[0] == 0 && s[2] == 16383 && s[4] == -16383 && s[6] == (int16_t)(0.8f * 32767.0f), "level: below the knee the samples are the chain's output, 16 bits");
        CHECK(s[8] == (int16_t)(0.9f * 32767.0f) && s[10] == (int16_t)(-m4 * 32767.0f) && s[10] > -32767, "level: above it the output's soft knee, never full scale");
        CHECK(s[1] == s[0] && s[3] == s[2] && s[11] == s[10], "level: the same on both channels");
    }
}

#if PX_OWN_FX
/* ---- test build 43: USB SEND, USB RETURN in the chain, GUITAR CUT ---- */
static void test_send(void)
{
    int32_t d[2 * A_FR];
    uint32_t i, k;
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1;
    enumerate();
    ctrl(0x01, 11, 1, 1, 0, 0);
    uac.flowing = 1;
    px_ua_route = 0; a_res_sp = 0;
    px_ua_block(d, A_FR);                         /* primes the ring */
    for (i = 0; i < A_FR; i++) { a_in[i] = 0.1f + 0.001f * (float)i; a_out[i] = -0.3f; a_outr[i] = 0.2f; }
    a_in[5] = 0.7f;                               /* (x 2 above full scale: clipped) */
    px_ua_route = 1;                              /* DI */
    ua_r = ua_w - 8u; px_ua_block(d, A_FR); k = ua_w - A_FR;   /* (the computer took the rest: room, and still primed) */
    {
        const int16_t *s = (const int16_t *)&ua_ring[k & (UA_N - 1u)];
        CHECK(s[0] == (int16_t)(0.2f * 32767.0f) && s[1] == s[0] && s[2] == (int16_t)((0.1f + 0.001f) * 2.0f * 32767.0f),
              "USB SEND DI: the guitar as it enters the chain, back at the input's full scale, on both channels");
        CHECK(s[10] == 32767 && s[11] == 32767, "USB SEND DI: above full scale clipped, straight (no knee)");
    }
    px_ua_route = 2;                              /* DI + OUT */
    ua_r = ua_w - 8u; px_ua_block(d, A_FR); k = ua_w - A_FR;   /* (the computer took the rest: room, and still primed) */
    {
        const int16_t *s = (const int16_t *)&ua_ring[k & (UA_N - 1u)];
        CHECK(s[0] == (int16_t)(0.2f * 32767.0f) && s[1] == (int16_t)(-0.3f * 32767.0f), "USB SEND DI + OUT: the DI left, the pedal's sound right");
    }
    a_res_sp = 1;                                 /* OUTPUT STEREO: the right is the two channels' middle */
    ua_r = ua_w - 8u; px_ua_block(d, A_FR); k = ua_w - A_FR;   /* (the computer took the rest: room, and still primed) */
    CHECK(((const int16_t *)&ua_ring[k & (UA_N - 1u)])[1] == (int16_t)(0.5f * (-0.3f + 0.2f) * 32767.0f), "USB SEND DI + OUT, OUTPUT STEREO: the right is (L + R) / 2, the mono sound");
    a_res_sp = 2;                                 /* GTR+BASS: the right is the two summed */
    ua_r = ua_w - 8u; px_ua_block(d, A_FR); k = ua_w - A_FR;   /* (the computer took the rest: room, and still primed) */
    CHECK(((const int16_t *)&ua_ring[k & (UA_N - 1u)])[1] == (int16_t)((-0.3f + 0.2f) * 32767.0f), "USB SEND DI + OUT, OUTPUT GTR+BASS: the right is the guitar and the bass summed");
    px_ua_route = 0; a_res_sp = 1;                /* OUTPUT, split: the two channels as the output plays them */
    ua_r = ua_w - 8u; px_ua_block(d, A_FR); k = ua_w - A_FR;   /* (the computer took the rest: room, and still primed) */
    {
        const int16_t *s = (const int16_t *)&ua_ring[k & (UA_N - 1u)];
        CHECK(s[0] == (int16_t)(-0.3f * 32767.0f) && s[1] == (int16_t)(0.2f * 32767.0f), "USB SEND OUTPUT, OUTPUT STEREO: left and right as the output plays them");
    }
    a_res_sp = 0; px_ua_route = 0;
    ctrl(0x01, 11, 0, 1, 0, 0);
}
static void play_packets(int k6)                  /* k6 x 0.5 ms of the computer's packets (play_v on both channels), the polls taking them */
{
    for (int k = 0; k < k6; k++) {
        if (!(m_ep[4][4] & 0x01u)) {
            for (uint32_t i = 0; i < 45u; i++) ep4rx[i] = play_v | play_v << 16;
            m_ep[4][4] |= 0x01u; m_ep[4][6] = 180u; m_ep[4][7] = 0;
        }
        usb_poll();
    }
}
static void test_chain(void)
{
    int32_t d[2 * A_FR];
    int i;
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1; a_gv = 1.0f; a_mute = 0;
    enumerate();
    px_ua_route = (uint8_t)((2u + PX_B_DELAY) << 3);   /* USB RETURN before DELAY, GUITAR MIX */
    px_ua_chain();
    CHECK(a_rt_at == PX_B_DELAY && !a_rt_x && !a_rt_cut && !a_rt_out, "USB RETURN before DELAY: the place, nothing yet (no stream)");
    ctrl(0x01, 11, 1, 2, 0, 0);                   /* the computer plays */
    for (i = 0; i < 40; i++) { play_packets(6); px_ua_chain(); memset(d, 0, sizeof d); px_ua_block(d, A_FR); }
    CHECK(a_rt_x && fabsf(a_rx[0] - 20000.0f / 65536.0f) < 1e-6f && fabsf(a_rx[A_FR - 1u] - 20000.0f / 65536.0f) < 1e-6f,
          "USB RETURN in the chain: the computer's sound in a_rx, mono, at the input's scale (16 bits' full scale = 0.5)");
    CHECK(d[0] == 0 && d[2 * A_FR - 1u] == 0, "USB RETURN in the chain: not mixed at the output as well");
    px_ua_route |= 4u;                            /* GUITAR CUT */
    play_packets(6); px_ua_chain();
    CHECK(a_rt_cut, "GUITAR CUT: the guitar cut while the computer plays");
    ctrl(0x01, 11, 0, 2, 0, 0);                   /* it stops */
    play_packets(6); px_ua_chain();
    CHECK(!a_rt_cut && !a_rt_x && !uo.go, "GUITAR CUT: the guitar back as soon as the computer stops playing");
    ctrl(0x01, 11, 1, 2, 0, 0);
    px_ua_route = 4u;                             /* USB RETURN OUTPUT, GUITAR CUT: the chain told only of the cut */
    for (i = 0; i < 40; i++) { play_packets(6); px_ua_chain(); memset(d, 0, sizeof d); px_ua_block(d, A_FR); }
    CHECK(a_rt_out && a_rt_at == PX_RT_END && a_rt_cut && !a_rt_x && d[0] == 20000 * 256, "USB RETURN OUTPUT, GUITAR CUT: the chain's end cut, the computer's sound at the output as v42");
    a_mute = 1; px_ua_route = (uint8_t)((2u + PX_B_DELAY) << 3);
    play_packets(6); px_ua_chain();
    CHECK(!a_rt_x && !uo.go, "USB RETURN in the chain: a flash write's mute stops it");
    a_mute = 0; a_ua = 0;
    play_packets(6); px_ua_chain();
    CHECK(a_rt_at == PX_RT_NONE && !a_rt_x && !a_rt_cut, "USB AUDIO off: the chain as in build 42");
    a_ua = 1; px_ua_route = 0;
    ctrl(0x01, 11, 0, 2, 0, 0);
}
static void test_take(double ppm, const char *name)   /* the playback read into the chain, as a_block reads it, over 10 minutes */
{
    const double dev = 1.0 + ppm * 1e-6, t_poll = 0.5 * T_FS * dev, t_blk = 128.0 / 44117.647 * 1000.0 * T_FS * dev;
    const double w = 2.0 * 3.14159265358979 * 200.0 / 44100.0;
    double tp = t_poll, tb = t_blk, th = T_FS, end = 600.0e3 * T_FS;
    uint32_t sent = 0, acc = 0, n_out = 0, jumps = 0, quiet = 0, fmin = ~0u, fmax = 0, un0, ov0;
    int32_t d[2 * A_FR];
    float last = 0.0f, peak = 0.0f;
    char msg[200];
    px_ua_on = 1; usb_uac_on = 1; a_ua = 1; a_gv = 1.0f; a_mute = 0;
    px_ua_route = (uint8_t)((2u + PX_B_REVERB) << 3);
    enumerate();
    uo_r = uo_w; uo.go = 0;                       /* (a clean start: the tests before left the ring part full; a stream opened
                                                   * again drops what it holds at its first read anyway, usb.c uo.stale) */
    ctrl(0x01, 11, 1, 2, 0, 0);
    un0 = uo.underruns; ov0 = uo.overruns;
    while (th < end) {
        if (tp <= tb && tp <= th) { usb_poll(); tp += t_poll; continue; }
        if (tb <= th) {                          /* a block: a_block reads the return first, a_send then records */
            px_ua_chain();
            if (th > 2000.0 * T_FS) {
                if (a_rt_x) {
                    const uint32_t f = uo_w - uo_r;
                    if (f < fmin) fmin = f;
                    if (f > fmax) fmax = f;
                    for (uint32_t i = 0; i < A_FR; i++) {
                        const float x = a_rx[i], st = fabsf(x - last);
                        if (n_out && st > 0.1526f * 0.03f) jumps++;   /* (a 200 Hz sine of 0.1526 moves 0.0044 a frame at most) */
                        if (fabsf(x) > peak) peak = fabsf(x);
                        last = x; n_out++;
                    }
                } else quiet++;
            }
            memset(d, 0, sizeof d);
            px_ua_block(d, A_FR);
            tb += t_blk;
            continue;
        }
        if (!(m_ep[4][4] & 0x01u)) {             /* the computer's frame: 44 or 45 frames, the same sine on both channels */
            uint32_t nf = 44u;
            acc += 100u;
            if (acc >= 1000u) { acc -= 1000u; nf++; }
            for (uint32_t i = 0; i < nf; i++) {
                const int16_t s16 = (int16_t)lrint(10000.0 * sin(w * (double)(sent + i)));
                ep4rx[i] = (uint32_t)(uint16_t)s16 | (uint32_t)(uint16_t)s16 << 16;
            }
            sent += nf;
            m_ep[4][4] |= 0x01u; m_ep[4][6] = (uint8_t)(nf * 4u); m_ep[4][7] = 0;
        }
        th += T_FS;
    }
    snprintf(msg, sizeof msg, "%s: into the chain without a gap (%u frames, %u jumps, %u silent blocks, ring empty %u, ring full %u)", name,
             n_out, jumps, quiet, uo.underruns - un0, uo.overruns - ov0);
    CHECK(n_out > 26000000u && !jumps && !quiet && uo.underruns == un0 && uo.overruns == ov0, msg);
    snprintf(msg, sizeof msg, "%s: at the input's scale (peak %.4f, 10000 / 65536 = 0.1526)", name, (double)peak);
    CHECK(fabsf(peak - 10000.0f / 65536.0f) < 0.0005f, msg);
    snprintf(msg, sizeof msg, "%s: the ring's fill after a read leaves 0.6 ms in hand and room for a block late by 3 ms (%u..%u of %u)", name, fmin, fmax, (unsigned)UO_N);
    CHECK(fmin >= 26u && fmax + 45u + 128u < UO_N, msg);
    printf("  %s: fill %u..%u frames (target %u)\n", name, fmin, fmax, (unsigned)UO_TGT);
    px_ua_route = 0;
    ctrl(0x01, 11, 0, 2, 0, 0);
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "desc")) { px_ua_on = 0; usb_uac_on = 0; return desc_main(); }
    test_off();
    test_on();
    test_switch();
    test_knee();
    test_mix();
    test_record(0.0, 0, "record, the nominal clocks");
    test_record(+300.0, 0, "record, the pedal 300 ppm slow");
    test_record(-300.0, 0, "record, the pedal 300 ppm fast");
    test_record(+100.0, 1, "record, blocks up to 0.7 ms late");
    test_play(0.0, "playback, the nominal clocks");
    test_play(+300.0, "playback, the pedal 300 ppm slow");
    test_play(-300.0, "playback, the pedal 300 ppm fast");
#if PX_OWN_FX
    test_send();
    test_chain();
    test_take(0.0, "USB RETURN in the chain, the nominal clocks");
    test_take(+300.0, "USB RETURN in the chain, the pedal 300 ppm slow");
    test_take(-300.0, "USB RETURN in the chain, the pedal 300 ppm fast");
#endif
    printf("USB audio: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
#endif
