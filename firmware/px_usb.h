/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — USB-MIDI + the M-UPGRADE update entry, so the web installer can update the pedal over USB.
 *
 * The USB device and the update protocol are felucca-bsp's (chip/usb.c, chip/ota.c, MPL-2.0, same chip as the FM-1):
 *   - usb_poll() runs from the TIMER5 interrupt at 2 kHz (10 kHz tick, every 5th), as in felucca-bsp's hwtest;
 *   - the main loop answers the handshake (ota_service) and, on the host's "upgrade" command, runs ota_session():
 *     it reads the package's update loader from the host, stages it at 0xE0000 (5 x 4 KiB) and resets into it;
 *     the loader (loader/) then writes the new app area. Nothing is committed before the host says "success".
 *   - the SysEx soft key F0 22 24 35 7D F7 enters the chip's UBOOT mode (rescue).
 * The identity the pedal reports ("PocketX_924", "BlackBox_924") is OP_PRODUCT, which tools/pxpkg.py writes into the
 * app of each package; an image not made by pxpkg (the SD-card image) reports the default below.
 *
 * v42: USB audio (docs/USB-AUDIO.md), while the USB AUDIO setting is on (px_midi.h px_ua_on, off by default). The
 * pedal is then a class-compliant USB sound card as well (USB Audio Class 1, felucca-bsp's streams in usb.c, both on
 * endpoint 4, isochronous): the computer records the pedal's sound and plays through its output. 16-bit stereo at
 * 44.1 kHz. With the setting off the USB device is the same as before, byte for byte (tests/test_usb_audio.c).
 *
 * Test build 41 (the Bluetooth audio image only): the port can also be the USB MIDI host of a footswitch by cable, an M-VAVE
 * Chocolate (felucca-bsp's chip/usb_host.c; px_usbhost.h; docs/BT-AUDIO-TEST.md). Only while SETTINGS' USB PORT row says
 * FOOTSW, never at power-on: the installer, the editor over USB and the rescue always find the USB device first.
 */
#pragma once

#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#if OP_BT_AUDIO
/* test build 41: felucca-bsp's USB MIDI host (chip/usb_host.c) built in. The port is the USB device at every power-on, as
 * always; it becomes the host of a footswitch by cable only while SETTINGS' USB PORT row says FOOTSW (px_midi.h px_usb_foot,
 * not saved; px_usbhost.h; docs/BT-AUDIO-TEST.md, "Test build 41"). The host's waits on the main loop serve the radio as
 * ui.h's do (UI_IDLE, openpocket.c): the music goes on while a footswitch is found */
#if !PX_OWN_FX
#error "the USB footswitch (test build 41) is the Bluetooth audio image's: PX_OWN_FX (px_midi.h px_uh, ui.h's row)"
#endif
#define FELUCCA_USB_HOST 1
#ifdef UI_IDLE
#define UH_IDLE() do { fm1_wdt_feed(); UI_IDLE(); } while (0)
#endif
#include "px_usbhost.h"
#endif
#if OP_UAUDIO
#include "px_uaconf.h"                          /* the streams and their rings (also tests/test_usb_audio.c's) */
#if OP_UADEV                                    /* the measurement option (px_audio.h): the band and the target set at run time */
static uint32_t uad_lo = 52u, uad_hi = 108u, uad_prime = 80u, uad_tgt = 208u;
#define UA_BAND_VAR 1
#define UA_LO uad_lo
#define UA_HI uad_hi
#define UA_PRIME uad_prime
#define UO_TGT uad_tgt
#endif
#endif
#define FELUCCA_USB_PRODUCT "OpenPocket"
#define FELUCCA_USB_MANUFACTURER "OpenPocket"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
/* the app may only write the OTA staging area (its loader + update record), the preset store (px_store.h) and,
 * v37, the NAM user models (px_models.h) */
#if OP_BT_AUDIO
/* the Bluetooth audio image (test build 14) also the pairings' two sectors [0xEE000, 0xF0000) (bt/bt_ps.h). Adjacent areas as
 * one range: the NAM models and the staging [0xC0000, 0xE5000), the pairings, the presets and v38's cabinet IRs
 * [0xEE000, 0xFC000). An erase is one aligned sector and a program one page, so no call the RAM driver accepts can straddle two
 * areas: every call gets the answer of one range per area (tests/test_bt_ps.c), in fewer instructions of RAM code */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0xC0000u, FL_OTA_HI) || FL_IN(off, n, 0xEE000u, 0xFC000u))
#else
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, 0xF0000u, 0xF4000u) || \
                             FL_IN(off, n, 0xC0000u, 0xE0000u) || FL_IN(off, n, 0xF4000u, 0xFC000u))
#endif

/* the package identity, patched by pxpkg.py ("OP-PRODUCT:" + identity, 32 bytes, exactly once in the app) */
static const volatile char OP_PRODUCT[32] __attribute__((used, aligned(4))) = "OP-PRODUCT:PocketX_9" OP_VERSION_NUM;
#define FELUCCA_ID_PTR ((const char *)OP_PRODUCT + 11)

#include "fm1_timer.h"
#if OP_BLE_TEST
static int ble_stall_tick(void);                /* ble/ble_status.h, included after this file */
#if OP_BT_AUDIO
static void bt_stall_tick(int firing);          /* bt/bt_a2dp.c: the stall record's samples (bt/bt_stall.h) */
#endif
#endif
#include "fm1_flash.h"
#include "fm1_cpu1_stop.h"
#include "usb.c"
static uint8_t *ota_scratch(void);              /* px_models.h: the upload staging buffer, idle while ota_session blocks */
#define OTA_SCRATCH ota_scratch()
#include "ota.c"

static uint8_t px_flash_ok;                     /* the flash is a 1 MiB SPI NOR: the update entry may stage */
static volatile uint32_t px_t5_sub;
#if FELUCCA_USB_HOST
/* the host's clock (px_usbhost.h pxu_ms_add): usb.c's fm1_ms, which chip/usb_host.c's waits and timeouts read, advanced at each
 * USB poll by the TIMER4 ticks since the last, so it counts milliseconds and never wraps as ms_now does */
typedef char pxu_msg_check[(UH_MSG_ON == PXU_MSG_ON && UH_MSG_OFF == PXU_MSG_OFF && UH_MSG_MIDI == PXU_MSG_MIDI &&
                            UH_MSG_NOMIDI == PXU_MSG_NOMIDI && UH_MSG_LOW == PXU_MSG_LOW && UH_MSG_LOST == PXU_MSG_LOST) ? 1 : -1];
static pxu_ms_t px_uh_clk;
#endif
#if OP_UADEV
static void uad_poll(void);                     /* px_uaudio.h: the gaps between USB polls */
#endif

#if OP_BLE_TEST
static volatile uint32_t px_t5_in_s, px_t5_in;  /* its longest stay, this second and since boot, TIMER4 ticks (the log's isr line) */
#endif
void fm1_timer5_irq(void)                       /* 10 kHz; USB at 2 kHz */
{
#if OP_BLE_TEST
    const uint32_t t0 = fm1_ticks();
#endif
    fm1_timer5_ack();
#if OP_BLE_TEST                                 /* a ble_poll that does not return: the crash screen says where (ble_status.h) */
    {
        const int stalled = ble_stall_tick();
#if OP_BT_AUDIO
        bt_stall_tick(stalled);                 /* (its last second sampled, the record completed when it fires) */
#endif
        if (stalled) {
#if DSP_CPU1
            fm1_cpu1_nobar = 1;                 /* (this crash is the watch's own, not the second core's fault: no bar) */
#endif
            FM1_VEC[FM1_IRQ_TIMER5] = (uint32_t)(uintptr_t)(fm1_fatal_stubs + 6u * FM1_IRQ_TIMER5);
        }
    }
#endif
    if (++px_t5_sub >= 5u) {
        px_t5_sub = 0;
#if FELUCCA_USB_HOST
        fm1_ms += pxu_ms_add(&px_uh_clk, fm1_ticks(), FM1_TICKS_PER_US * 1000u);   /* test build 41: the host's clock (above) */
#endif
#if OP_UADEV
        uad_poll();
#endif
        usb_poll();
    }
#if OP_BLE_TEST
    {
        const uint32_t d = fm1_ticks() - t0;
        if (d > px_t5_in_s) px_t5_in_s = d;
        if (d > px_t5_in) px_t5_in = d;
    }
#endif
}
extern void isr_timer5(void);

#ifndef PX_LIGHTS_OFF                           /* the button lights off while an update runs (openpocket.c: px_lights.h) */
#define PX_LIGHTS_OFF() ((void)0)
#endif

/* ---- ota.c hooks ---- */
static uint32_t ota_now_ms(void) { return ms_now(); }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint8_t *d = p;
    while (n) {                                 /* 256-byte windows, interrupts off (the SFC is switched off) */
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
#if DSP_CPU1 && OP_BT_AUDIO
        a_cpu1_quiet();                         /* (the Bluetooth audio image reads here with the audio running: nothing of the
                                                 * second core's may run from the flash, and no new block starts; v41's worker
                                                 * idle in its RAM loop; a core that faulted is held: px_audio.h) */
#endif
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK", "LOADER", "CONFIRM", "RESTART"};
    PX_LIGHTS_OFF();
    lcd_rect(0, 0, 240, 240, UI_BG);
    ft_box(0, 92, 240, "UPDATE", &FT_MD, UI_ACC, UI_BG, 1);
    if (step < 9u) {
        ft_box(0, 124, 240, STEP[step < 6u ? step : 0], &FT_SM, UI_TXT, UI_BG, 1);
        ft_box(0, 150, 240, "DO NOT UNPLUG", &FT_SM, UI_DIM, UI_BG, 1);
        return;
    }
    {   /* v42: what happened and what to do, the code for a bug report */
        char b[24] = "UPDATE FAILED (CODE ";
        uint32_t k = 20, v = (uint32_t)(code < 0 ? -code : code);
        if (v >= 10u) b[k++] = (char)('0' + v / 10u % 10u);
        b[k++] = (char)('0' + v % 10u);
        b[k++] = ')'; b[k] = 0;
        ft_box(0, 124, 240, b, &FT_SM, RED, UI_BG, 1);
        ft_box(0, 150, 240, "TRY AGAIN FROM THE INSTALLER", &FT_SM, UI_TXT, UI_BG, 1);
    }
    for (int i = 0; i < 15; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
}
static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;                          /* an intentional reset, not a failed boot */
    guard.failed = 0;
    op_diag.stage = ST_OK;
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                     /* record into RAM, core reset: the SPL runs the staged loader */
}

#if OP_UAUDIO
#include "px_uaudio.h"         /* v42: the blocks to and from the computer, the setting, the figures */
#endif

/* ---- boot / main loop ---- */
static void px_flash_boot(void)                 /* interrupts still off (before audio_start) */
{
    uint32_t f = irq_save(), id = FL_FAR(fl_jedec_ram)();
    irq_restore(f);
    __asm__ volatile("cli" ::: "memory");       /* (irq_restore turned them on: back off until audio_start) */
    px_flash_ok = (id & 0xFFu) == 0x14u && id != 0xFFFFFFu && id != 0u;   /* 2^0x14 = 1 MiB */
    if (px_flash_ok)
        ota_boot_cleanup();                     /* a staging area left by an update: the SPL must not re-enter it */
}
/* after px_store_boot (v42): the computer must find the audio streams, or not, from its first question on */
static void px_usb_start(void)                  /* interrupts still off */
{
#if OP_UAUDIO
    usb_uac_on = px_ua_on;
    a_ua = usb_uac_on;
#endif
    usb_start();
    fm1_timer5_start(isr_timer5, 4);            /* above ALNK0 (3), as felucca-bsp */
}
static void px_audio_halt(void)                 /* before any flash write: nothing may run from XIP but us */
{
    fm1_irq_mask(FM1_IRQ_ALNK0);
    a_collect();
    gpio_direction_output(G_AMP_EN, 0);         /* amp off: no noise while the flash is busy */
#if DSP_CPU1
#if FM1_CPU1_ECHO
    fm1_cpu1_held_t = fm1_ticks() | 1u;         /* (test build 14: a hold's late flag, fm1_cpu1.h fm1_cpu1_echo) */
#endif
    fm1_cpu1_stop();
    a_c1 = 0;
#endif
}
static void ed_service(uint32_t now);
static void px_usb_service(uint32_t now)        /* every main-loop pass */
{
#if OP_UAUDIO
    px_ua_service(now);
#endif
    usb_retry(now);
    if (usb.uboot_req) {                        /* SysEx soft key: the chip's UBOOT (rescue) */
        px_audio_halt();
        lcd_rect(0, 0, 240, 240, UI_BG);
        ft_box(0, 110, 240, "UBOOT (USB)", &FT_MD, UI_ACC, UI_BG, 1);
        guard.pending = 0;
        op_diag.stage = ST_OK;
        usb_detach();
        fm1_delay_ms(30);
        fm1_enter_uboot();
    }
    ed_service(now);                            /* the web editor (px_editor.h): its frames first */
    ota_service();                              /* the installer's handshake */
    if (usb.ota_req) {                          /* its upgrade command: commits and resets, or comes back */
        usb.ota_req = 0;
        if (px_flash_ok) {
            px_audio_halt();
            ota_session();                      /* returns only when nothing was committed */
            guard.pending = 0;
            op_diag.stage = ST_OK;
            fm1_reboot();                       /* back to a clean start (audio, second core) */
        }
    }
}

#if FELUCCA_USB_HOST
/* ---- test build 41: the footswitch by cable (px_usbhost.h) ---- */
static void px_uh_apply(void) { usb_host_switch(px_usb_foot); }   /* SETTINGS' USB PORT row (ui.h): usb_host_task makes the change */
static uint32_t px_uh_note(void) { return pxu_note(&px_uh, fm1_ms); }   /* PLAY's footer (ui.h draw_hint) */
/* every main-loop pass, after px_usb_service. In FOOTSW the host finds the footswitch with blocking control transfers on this
 * loop (some 150 ms when one is plugged in; the radio is served meanwhile, UH_IDLE) and puts its Program and Control Changes in
 * the ring USB MIDI fills (source 1): px_midi_service reads them as a computer's, the controls learned on the pedal with them.
 * Nothing is sent to the footswitch (px_midi_io.h). Back in COMPUTER the port is the USB device again within a pass (usb_retry) */
static void px_uh_service(void)
{
    usb_host_task(fm1_ms);                      /* (the host's clock: ms_now's wrap would stop it looking, px_usbhost.h) */
    pxu_msg(&px_uh, usb_host_message(), fm1_ms);
    if (usb_host_active()) pxu_line(&px_uh, fm1_usb_line(), fm1_ms);
}
#endif
