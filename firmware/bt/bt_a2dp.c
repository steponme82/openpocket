/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Bluetooth audio test image (build target "bt", docs/BT-AUDIO-TEST.md): JieLi's classic controller and host (the stock
 * firmware's own, third_party/jieli-ble + third_party/jieli-bt) are started; the pedal shows up on a phone as "OpenPocket", pairs
 * with no PIN (Just Works) and plays the phone's music into its output (A2DP sink, SBC; mono, and stereo with OUTPUT STEREO).
 *
 * The image is the Bluetooth test image's (OP_BLE_TEST: ble/ble_os.c's scheduler, the radio start-up, the stall watch, the boot
 * report, DIAG) with the classic stack in place of the LE one: openpocket.c calls the same ble_test_* functions, defined here.
 *
 *   start   an app task ("btapp") runs btstack_init (it waits for the controller), then sends the stack's commands
 *   events  bt_event_notify (the stack calls it): ready -> visible and connectable; connected; media start / stop
 *   audio   the controller shows every media packet to the A2DP code before it chooses between the app and the host; there
 *           (a2dp_media_packet_user_handler) each one is copied into a FIFO (test build 12: the library's own choice failed
 *           from the second packet of each session). The main loop (ble_test_poll) decodes the FIFO's SBC frames (bt_sbc.c)
 *           into the ring (px_bt_ring.h) and steers its rate; px_audio.h dsp_out adds the ring to the output */
#include <stdint.h>
#include "fm1_irq.h"
#include "ble_os.h"
#include "ble_status.h"
#include "bt_sbc.h"

extern int btstack_init(void);
extern int __crc16_mutex_init(void);
extern int task_create(void (*fn)(void *), void *arg, const char *name);
extern uint32_t ble_task_waiting(const char *name);
extern void bt_max_pwr_set(uint8_t pwr, uint8_t pg_pwr, uint8_t iq_pwr, uint8_t ble_pwr);
extern void __set_simple_pair_flag(uint8_t on);
extern void __set_simple_pair_param(uint8_t io_cap, uint8_t oob_data, uint8_t mitm);
extern void __set_a2dp_auto_play_flag(uint8_t on);
extern void __set_page_timeout_value(uint16_t ms);
extern void __set_super_timeout_value(uint16_t ms);
extern void __change_hci_class_type(uint32_t cod);
extern void lmp_set_sniff_disable(void);
extern int user_send_cmd_prepare(int cmd, uint16_t len, uint8_t *param);
extern int a2dp_media_try_get_packet(uint8_t **frame);
extern void a2dp_media_free_packet(void *packet);
extern int os_sem_create(void *s, int cnt);
extern int os_sem_pend(void *s, int timeout);
extern int os_sem_post(void *s);
extern void hci_user_confirmation_request(uint8_t yes, uint8_t *addr);   /* (hci_vendor.c: the reply to HCI 0x33, to that address) */
extern uint32_t bt_stub_calls;
extern const char *bt_stub_last;
/* btstack's avctp_user.c (the library, Apache-2.0): the newest pairing in its device list (here the pairing store, bt_ps.h), then
 * user_send_cmd_prepare(1, 6, its address), the SDK's "connect to this address"; returns 0 when there is none */
extern uint8_t connect_last_device_from_vm(void);
/* test build 23: BLE MIDI beside the music (bt/bt_le_midi.c, included after this file) */
static void bt_le_advertise(void);
static char *bt_le_text(char *o);
static uint32_t bt_le_up(void);                                 /* (test build 28: LE links up; the LE side's watchdog) */
static int bt_le_cen_settled(void);                             /* (test build 34: the footswitch linked, or no central role) */
static void bt_le_watch(uint32_t now);

/* the SDK's numbers (include_lib/btstack/avctp_user.h, include_lib/utils/event/bt_event.h, Apache-2.0) */
enum { USER_CTRL_WRITE_SCAN_ENABLE = 13, USER_CTRL_WRITE_CONN_ENABLE = 15 };
enum { BT_EVENT_FROM_CTRLER = 1, BT_EVENT_FROM_HCI = 2, BT_EVENT_FROM_CON = 3 };
enum { HCI_EV_CONNECTION_COMPLETE = 0x03, HCI_EV_DISCONNECTION_COMPLETE = 0x05, HCI_EV_USER_CONFIRMATION_REQUEST = 0x33 };
/* what the stack hands bt_event_notify (bt_event.h struct bt_event, user_interface.c bt_event_update_to_user): the event, the
 * phone's address (6 bytes, as HCI sends it) and a value (a status or a reason) */
typedef struct { uint8_t event; uint8_t args[7]; uint32_t value; } bt_event_t;
enum { BT_STATUS_INIT_OK = 3, BT_STATUS_START_CONNECTED = 5, BT_STATUS_FIRST_CONNECTED = 6, BT_STATUS_SECOND_CONNECTED = 7,
       BT_STATUS_ENCRY_COMPLETE = 8, BT_STATUS_FIRST_DISCONNECT = 9, BT_STATUS_SECOND_DISCONNECT = 10, BT_STATUS_CONN_A2DP_CH = 39,
       BT_STATUS_A2DP_MEDIA_START = 48, BT_STATUS_A2DP_MEDIA_STOP = 49 };
#define BT_COD 0x240404u                                        /* the stock firmware's class of device: audio, rendering */

/* what the audio path reads (px_audio.h bt_ring): the ring, its samples in the SRAM below the RAM (app-bt.ld .bss_lo2: CPU only, checked
 * by nothing but the ear) */
#define BT_TARGET 6615u                                         /* the backlog it keeps: 150 ms */
#define BT_PREBUF 1024u                                         /* decoded before it starts: 23 ms */
static int16_t bt_ring_mem[BT_RING_N] __attribute__((aligned(4), section(".bss.lo2.bt_ring")));   /* (bt_ring: px_audio.h) */
/* test build 42: the music's side, (L - R) / 2, at half the rate (px_bt_ring.h): 2.5 KB in the RAM, CPU0's alone like the ring
 * (the SRAM below the RAM has 672 bytes left beside the ring and the decoder) */
static int16_t bt_ring_smem[BT_RING_SN] __attribute__((aligned(4)));

/* the state, for the screen and the log */
enum { BT_OFF, BT_STARTING, BT_VISIBLE, BT_LINKED, BT_PLAYING, BT_FAILED };
static volatile uint32_t bt_st;
static int bt_off_by_key;                                       /* REV held at power-on: Bluetooth off this boot (openpocket.c) */
static uint8_t bt_pwr_stock;                                    /* test build 18 (openpocket.c): 0 the stock power setup not done
                                                                 * (DLY held), 1 done with the DC-DC already on, 2 done, moved */
/* test build 18: the last phone called back once after power-on (M-VAVE's V22 fixes the same: "Bluetooth would not automatically
 * reconnect after being turned on"); an iPhone does not page an audio device by itself. 0 not yet, 1 asked, then the library's
 * return + 2 (2: no pairing stored, 3: paging that phone) */
static volatile uint32_t bt_rc_state, bt_rc_ms;
static uint16_t ble_conn;                                       /* (openpocket.c: a link is up, scroll the link line) */
volatile uint32_t bt_cnt_irq_br, bt_cnt_irq_slot;               /* bt_irq.c: IRQ 40 and 41 */
static volatile uint32_t bt_ev_n, bt_ev_hist[6], bt_ev_i;       /* the events, the last six: from << 12 | code << 4 */
static volatile uint32_t bt_init_ok, bt_links, bt_enc, bt_a2dp_ch, bt_media, bt_disc;
static volatile uint32_t bt_cmd_bits, bt_scans;                 /* for the app task: bit 0 = visible and connectable */
static uint32_t bt_cmd_sem[4];
static uint32_t bt_t_start, bt_t_init;
/* test build 14 (ms since power-on, ms_now, as the main loop's): when Bluetooth first became VISIBLE, when the DSP came on
 * (openpocket.c), the last scan enable (bt_app_task: the first at start-up, the others after a link's end or a KICK) */
static uint32_t bt_t_vis, bt_dsp_ms, bt_scan_ms;
#define BT_QUIET_MS 5000u
#define BT_QUIET_LE_MS 30000u                                   /* (test build 28: with an LE link up) */
static uint32_t bt_q40_n, bt_q40_t, bt_kicks, bt_kick_ms;      /* the baseband interrupt's last count and when it changed; the
                                                                 * kicks, the last one's time (ms_now) */
extern uint8_t acl_tx_pool[], acl_tx_pool_end[], acl_rx_pool[], acl_rx_pool_end[];   /* app-bt.ld */

/* ---- the HCI conversation, recorded where it passes (the log's hci line). The integrated controller and host talk through
 * the tasks' queues: the controller's events reach the host task ("btstack", type 0x400001) with the event's bytes inline
 * (btstack_task.c hci_packet_handler; 0x400002 is an ACL packet, a pointer and a length), the host's commands reach the
 * controller's task ("btctrler", 0x400002 and 0x400003 to a link) the same way (btctrler_task.c btctrler_hci_cmd_to_task).
 * ble_os.c os_taskq_post_type calls bt_taskq_spy with every message: the newest 11 are kept, each as the task's initial, the
 * type's low digit and the message's first 5 bytes; ACL packets and Number of Completed Packets events (0x13, with every
 * packet sent) are only counted. (Test builds 9 to 11 also kept the first 12 since boot, the hci0 line: known since) */
#define BT_MSG_LAST 11u
typedef struct { char task, type; uint8_t b[5]; } bt_msg_t;
static bt_msg_t bt_msg_last[BT_MSG_LAST];
static volatile uint32_t bt_msg_n, bt_msg_kept, bt_msg_acl, bt_msg_ncp;
static volatile uint32_t bt_le_rep_host;                        /* (test build 31: LE meta subevent 2 posted to the host task) */
static void bt_taskq_spy(const char *task, int type, int argc, const int *argv)
{
    const int host = task[0] == 'b' && task[2] == 's';
    bt_msg_t m;
    uint32_t i, k;
    m.task = task[2] == 's' ? 'S' : task[2] == 'c' ? 'C' : task[2] == 'e' ? 'E' : task[2] == 'a' ? 'A' : '?';   /* btStack btCtrler btEncry btApp */
    m.type = (char)('0' + (type & 0xF));
    for (i = 0; i < 5u; i++) m.b[i] = (int)(i / 4u) < argc ? (uint8_t)((uint32_t)argv[i / 4u] >> (8u * (i % 4u))) : 0;
    /* (in one piece, interrupts off: the tasks post, and so do the baseband's interrupts, through the same call) */
    local_irq_disable();
    bt_msg_n++;
    if (host && type == 0x400002) bt_msg_acl++;                 /* btstack: an ACL packet */
    else if (host && type == 0x400001 && argc > 0 && (argv[0] & 0xFF) == 0x13) bt_msg_ncp++;
    else if (host && type == 0x400001 && argc > 0 && (argv[0] & 0xFF) == 0x3E && ((uint32_t)argv[0] >> 16 & 0xFFu) == 0x02u)
        bt_le_rep_host++;                                       /* (test build 31: an LE advertising report from the controller,
                                                                 * before the host reads it: counted, not kept) */
    else if (!(host && type == 0xFF)) {                         /* (test build 27: the web editor's wake-ups of the host's task,
                                                                 * stack_run_loop_resume, are not kept) */
        k = bt_msg_kept;
        bt_msg_last[k % BT_MSG_LAST] = m;
        bt_msg_kept = k + 1u;
    }
    local_irq_enable();
}
static const uint8_t *bt_pp;                                    /* the frames left of the packet being decoded: bt_pn bytes */
static uint32_t bt_pn, bt_pkts, bt_frames_in, bt_t_steer;
static uint32_t bt_confirms, bt_hci_disc, bt_conn_fail;         /* the pairing confirmations sent, the links that ended (HCI) */
static uint32_t bt_end;                                         /* the last link's end: HCI event << 8 | its reason or status */
static int16_t bt_frame[BT_SBC_MAX_SAMPLES], bt_frame_s[BT_SBC_MAX_SAMPLES];   /* (the mono mix; the side, test build 42) */

static void bt_cmd(uint32_t bit) { bt_cmd_bits |= bit; os_sem_post(bt_cmd_sem); }
/* test build 40: BT POWER (px_midi.h px_bt_pwr), the radio's highest transmit levels: classic, page, inquiry, LE. HIGH is the
 * SDK's (10, 5, 8, 9), as every earlier build; MID and LOW lower them, for a pedal whose guitar sound ticks while the radio
 * works and players a metre or two from it. Set in the Bluetooth task only (after btstack_init, and on bt_cmd 4) */
static const uint8_t BT_PWR[3][4] = {{10, 5, 8, 9}, {6, 4, 6, 6}, {3, 3, 3, 3}};
static volatile uint8_t bt_pwr_set_n;                           /* times applied (the log: how many) */
static void bt_pwr_now(void)
{
    const uint8_t *l = BT_PWR[px_bt_pwr < 3u ? px_bt_pwr : 0u];
    bt_max_pwr_set(l[0], l[1], l[2], l[3]);
    bt_pwr_set_n++;
}
static volatile uint8_t bt_cmd_up;                              /* the semaphore exists (Bluetooth started; not with REV held) */
static void bt_power_apply(void) { if (bt_cmd_up) bt_cmd(4u); }  /* (not started yet: the task applies it at its start) */

/* ---- the phone's packets (test build 12), taken where the controller shows each one to the A2DP code before it chooses
 * between the app and the host: lmp.c lmp_rx_handler (the controller's task) -> a2dp_media_codec.c acl_u_packet_analyse ->
 * a2dp_media_packet_user_handler, the library's own weak and returning 0. data = the L2CAP header (lmp_rx_handler wrote the HCI
 * header just before it, from its connection), size = 4 + the L2CAP payload. In test build 11 the controller's choice failed from
 * the second packet of each session (PK 1: every later packet went to the host, which has nothing to play it with). Every media
 * packet now goes into a FIFO that bt_decode reads, whatever the controller then does with it. Everything here runs in the main
 * loop's context (the tasks and the timers run inside ble_poll, ble_os.c), so the FIFO needs no lock */
#define BT_HOOK_TAKE 0      /* 1: -1 to the controller, which then frees the packet (neither the app's list nor the host). 0: the
                             * library's own path goes on as in test build 11 (the host gets it, unless HEAL below mends it) */
#define BT_A2_HEAL 1        /* the receiving connection's two A2DP fields written back when found wrong (the FIFO line's heals) */
#define BT_FQ_N 32u         /* packets: 464 ms of the iPhone's 5-frame packets (test build 24: was 16, 232 ms, which a long
                             * drawing of the screen under the NAM network's load overflowed: the FIFO line's drops) */
#define BT_FQ_MAX 672u      /* the pedal's L2CAP MTU (l2cap.c l2cap_max_mtu, in this configuration): no packet is longer */
typedef struct { uint16_t n; uint8_t frames, pad; uint8_t b[BT_FQ_MAX]; } bt_fq_t;
/* test build 24: in the pool's free end (21,632 bytes, only CPU0 touches it), no longer 10,816 bytes of the Bluetooth heap,
 * which the BLE MIDI side (test build 23) now shares */
static bt_fq_t bt_fq_mem[BT_FQ_N] __attribute__((aligned(4), section(".pool.tail.bt_fq")));
static bt_fq_t *bt_fq;                                          /* (bt_fq_mem from ble_test_start on) */
static uint32_t bt_fq_w, bt_fq_r, bt_fq_frames, bt_fq_held, bt_cur_left;
static void bt_fq_reset(void)                                   /* (a session's end: what is queued is dropped) */
{
    if (bt_fq_held) bt_pn = 0;                                  /* (bt_pp points into the slot held) */
    bt_fq_r = bt_fq_w; bt_fq_frames = 0; bt_fq_held = 0; bt_cur_left = 0;
}
static uint32_t bt_fq_samples(void)                             /* the encoded backlog: the FIFO's frames and the rest of the one held */
{
    const uint32_t spf = (uint32_t)bt_sbc_info.blocks * bt_sbc_info.subbands;
    return (bt_fq_frames + bt_cur_left) * (spf ? spf : 128u);
}

/* HEAL. The host's `channel` (a2dp_media_codec.c) is static: it is reached from a global beside it, whose distance is fixed
 * (the vendor's objects never change) and checked in each build's symbols (firmware/build.sh). lmp_connection's bytes (32-bit,
 * natural alignment, lmp.c's TBAA): 11 the app's channel open, 30 its L2CAP CID, 192 bit 3 detaching, 236 the link. (Test
 * build 12's probe of these fields at every packet, its 10 kHz watch and the a2dp line are gone in build 13: the music plays
 * from the FIFO, and their room went to the reverb's loop and the crash report) */
extern uint8_t a2dp_mutual_toggle;
extern void *lmp_conn_for_handle(int handle);
#define BT_CH() (*(uint8_t *const volatile *)(const void *)(&a2dp_mutual_toggle + 8))
#define BT_U16(p, o) (*(volatile uint16_t *)(void *)((uint8_t *)(p) + (o)))
#define BT_U32(p, o) (*(volatile uint32_t *)(void *)((uint8_t *)(p) + (o)))
/* the music path's counts (the log's line 6, bt_fifo_line): the FIFO's drops (full | too big << 16), the library's packets
 * given back undecoded, the heals */
static volatile struct { uint32_t drop, dup, heal; } bt_a2;

int a2dp_media_packet_user_handler(uint8_t *data, uint16_t size)
{
    const uint32_t cid = data[2] | (uint32_t)data[3] << 8, n = data[0] | (uint32_t)data[1] << 8;
    const uint32_t h = (data[-4] | (uint32_t)data[-3] << 8) & 0x0FFFu;
    uint8_t *const c = lmp_conn_for_handle((int)h);
    /* the state lmp_rx_handler tests next, this packet's: the channel open (byte 11) and the CID it expects (30) */
    if (c && bt_st == BT_PLAYING && (!c[11] || BT_U16(c, 30) != cid)) {
#if BT_A2_HEAL
        /* (only the connection the host opened: packets kept on the other one would never be given back, its list is not
         * the one the app reads) */
        if (c == BT_CH() && BT_U32(c, 236) && !(BT_U32(c, 192) & 8u)) {   /* (a link, not detaching) */
            local_irq_disable(); BT_U16(c, 30) = (uint16_t)cid; c[11] = 1u; local_irq_enable();
            bt_a2.heal++;
        }
#endif
    }
    if (!bt_fq || (bt_st != BT_LINKED && bt_st != BT_PLAYING)) return 0;
    /* (from the link on: a session's first packet too, which comes before MEDIA_START) */
    if (n < 13u || n > BT_FQ_MAX || n + 4u > size) bt_a2.drop += 0x10000u;
    else if (bt_fq_w - bt_fq_r >= BT_FQ_N) bt_a2.drop++;
    else {
        bt_fq_t *const s = &bt_fq[bt_fq_w % BT_FQ_N];
        const uint32_t m = 12u + 4u * (data[4] & 0x0Fu);        /* the media header: after the RTP header and its contributors */
        ble_memcpy(s->b, data + 4, n);
        s->n = (uint16_t)n;
        s->frames = m < n ? (uint8_t)(s->b[m] & 0x0Fu) : 0u;
        bt_fq_frames += s->frames;
        bt_fq_w++;
    }
    return BT_HOOK_TAKE && bt_st == BT_PLAYING ? -1 : 0;       /* (a session's first packet always goes on: the host's codec init,
                                                                 * the channel's open and MEDIA_START come from it) */
}

static __attribute__((noinline)) void bt_media_end(void)        /* MEDIA_STOP, FIRST/SECOND_DISCONNECT, the HCI disconnection */
{
    bt_fq_reset();
}

/* the stack's events (its tasks call this) */
int bt_event_notify(int from, void *e)
{
    const uint8_t *ev = e;
    const uint8_t code = ev ? ev[0] : 0xFFu;
    bt_ev_hist[bt_ev_i++ % 6u] = (uint32_t)from << 12 | (uint32_t)code << 4;
    bt_ev_n++;
    if (from == BT_EVENT_FROM_HCI && ev) {
        bt_event_t *const b = e;
        switch (code) {
        case HCI_EV_USER_CONFIRMATION_REQUEST:
            /* Secure Simple Pairing asks the host to confirm (Just Works: nothing to compare). The host leaves the answer to the
             * application (hci_vendor.c, case 0x33), as the SDK's demo does (bt_music.c: bt_send_pair(1)); until test build 10
             * nothing answered, and the controller waited until the phone gave up. To the address of the event, through the
             * same call the SDK's USER_CTRL_PAIR ends in. It posts the reply to the controller's task; while that queue is full
             * the library tries again after os_time_dly(2) (btctrler_task.c __cmd_to_lmp_conn), a real sleep of 20 ms or more
             * in this task, the host's, as the host's own IO-capability reply (case 0x31, which sleeps 100 ms first); each
             * refused try counts in the hci line's LOST, though the reply is not lost. (A PIN code request, 0x16, the host
             * answers itself, with "0000": hci_vendor.c bt_get_pin_code) */
            hci_user_confirmation_request(1u, b->args);
            bt_confirms++;
            BT_CRUMB("pairing confirmed");
            break;
        case HCI_EV_DISCONNECTION_COMPLETE:
        case HCI_EV_CONNECTION_COMPLETE:
            if (code == HCI_EV_CONNECTION_COMPLETE && b->value == 0u) break;   /* (a link that came up) */
            /* a link that ended, or one that failed to come up: the controller turned the scans off when it made the link and
             * does not turn them on again (lmp.c lmp_detach_check: only in its test modes), so the pedal must, as the SDK's
             * demo does on these two events. A link that ends before any profile opened gives no FIRST_DISCONNECT below, and
             * the KICK runs only while BT_VISIBLE: without this a failed pairing left the pedal invisible until power-off */
            if (code == HCI_EV_DISCONNECTION_COMPLETE) bt_hci_disc++; else bt_conn_fail++;
            bt_end = (uint32_t)code << 8 | (b->value & 0xFFu);  /* the reason (05) or the status (03): the log's END */
            ble_conn = 0;
            if (bt_st == BT_LINKED || bt_st == BT_PLAYING) bt_st = BT_VISIBLE;
            bt_media_end(); bt_pn = 0;                          /* (the link is gone: what its FIFO held too) */
            if (bt_st == BT_VISIBLE) bt_cmd(1u);                /* visible and connectable again */
            break;
        default: break;
        }
        return 0;
    }
    if (from != BT_EVENT_FROM_CON) return 0;
    switch (code) {
    case BT_STATUS_INIT_OK:
        bt_init_ok = 1; bt_st = BT_VISIBLE; ble_state = BLE_ADV;
        if (!bt_t_vis) bt_t_vis = ms_now();
        bt_cmd(1u);                                             /* visible (inquiry scan) and connectable (page scan) */
        bt_le_advertise();                                      /* test build 23: and BLE MIDI advertising (bt_le_midi.c), once */
        break;
    case BT_STATUS_START_CONNECTED: case BT_STATUS_FIRST_CONNECTED: case BT_STATUS_SECOND_CONNECTED:
        if (bt_st < BT_LINKED) { bt_st = BT_LINKED; bt_links++; }
        ble_conn = 1;
        break;
    case BT_STATUS_ENCRY_COMPLETE: bt_enc++; break;
    case BT_STATUS_CONN_A2DP_CH: bt_a2dp_ch++; break;
    case BT_STATUS_A2DP_MEDIA_START:                            /* just after the channel's open (the FIFO is not reset: the
                                                                 * session's first packet is already in it) */
        bt_st = BT_PLAYING; bt_media++;
        break;
    case BT_STATUS_A2DP_MEDIA_STOP:
        bt_media_end();                                         /* (sbc_codec_stop sends this before it closes the channel) */
        if (bt_st == BT_PLAYING) bt_st = BT_LINKED;
        break;
    case BT_STATUS_FIRST_DISCONNECT: case BT_STATUS_SECOND_DISCONNECT:
        bt_media_end();                                         /* (a stop that did not come first) */
        bt_disc++; ble_conn = 0; bt_st = BT_VISIBLE;
        bt_cmd(1u);                                             /* visible again for the next phone */
        break;
    default: break;
    }
    return 0;
}

/* btstack_init waits on a semaphore until the controller's task has started, and the stack's commands may wait too: both run
 * here, in a task, never in the main loop */
static void bt_app_task(void *arg)
{
    int rc;
    (void)arg;
    ble_stage_set(BLE_STAGE_INIT);                              /* (BLE_STAGE_RF: ble_rf.c, inside the init) */
    BT_CRUMB("btstack_init");
    rc = btstack_init();
    BT_CRUMB("btstack_init returned");
    if (rc) { ble_judge_start(rc, 1); bt_st = BT_FAILED; return; }
    ble_stage_set(BLE_STAGE_READY);
    bt_pwr_now();                                               /* after the controller's own init: SETTINGS' BT POWER */
    bt_t_init = ble_ms();
    for (;;) {
        os_sem_pend(bt_cmd_sem, 0);
        if (bt_cmd_bits & 1u) {
            bt_cmd_bits &= ~1u;
            BT_CRUMB("scan enable");
            user_send_cmd_prepare(USER_CTRL_WRITE_SCAN_ENABLE, 0, 0);
            user_send_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, 0);
            bt_scans++;
            bt_scan_ms = ms_now();
        }
        if (bt_cmd_bits & 4u) {                                 /* test build 40: SETTINGS' BT POWER changed */
            bt_cmd_bits &= ~4u;
            bt_pwr_now();
        }
        if (bt_cmd_bits & 2u) {                                 /* test build 18: the last phone, called back (ble_test_poll) */
            bt_cmd_bits &= ~2u;
            BT_CRUMB("reconnect: the last phone");
            bt_rc_state = 2u + connect_last_device_from_vm();   /* (paging it, up to the 8 s page timeout; failed, the scans
                                                                 * resume, or the KICK below asks for them again) */
        }
    }
}

/* ---- the stall record (bt_stall.h): the 10 kHz tick (px_usb.h) calls this on every tick, with firing = 1 when the stall watch
 * fires. Nothing happens until a ble_poll has run for 3 s. */
#define BT_STALL_WINDOW 10000u                                  /* the last second before the watch fires: sampled */
static uint32_t bt_stall_on;                                    /* 1: sampling, 2: fired */

static void bt_stall_begin(void)
{
    uint32_t i;
    bt_stall.magic = 0;
    bt_stall.samples = bt_stall.other = 0;
    for (i = 0; i < BT_STALL_N; i++) bt_stall.pc[i] = bt_stall.rets[i] = bt_stall.cnt[i] = 0;
    bt_stall.dmin = 0xFFFFFFFFu; bt_stall.dmax = 0;
}

/* one sample: the PC and RETS the outermost interrupt saved, at the top of the interrupt stack. Interrupts run on the system
 * stack (crt0's ssp, fm1_guard.h fm1_sp_in_isr) and every OpenPocket interrupt entry starts with [--sp] = {psr, rets, reti}
 * (bsp/chip/fm1_isr.S; the same order the fault frame shows, bsp/chip/fm1_vec.S): reti, the interrupted code's PC, at the top
 * minus 12, its rets at minus 8. When this tick interrupted another interrupt, the top is still that interrupt's frame, so it
 * names the code under both: the stuck task. (The classic baseband's interrupts enter through bt/bt_isr.S, the same frame.) */
static void bt_stall_sample(void)
{
    const uint32_t *top = (const uint32_t *)(uintptr_t)_sstack_top;
    uint32_t sp, d, pc, rets, i;
    __asm__ volatile("%0 = sp" : "=r"(sp));
    d = (uint32_t)(uintptr_t)top - sp;
    if (d < bt_stall.dmin) bt_stall.dmin = d;
    if (d > bt_stall.dmax) bt_stall.dmax = d;
    pc = top[-3];
    rets = top[-2];
    bt_stall.samples++;
    for (i = 0; i < BT_STALL_N; i++) {
        if (!bt_stall.cnt[i]) { bt_stall.pc[i] = pc; bt_stall.rets[i] = rets; bt_stall.cnt[i] = 1; return; }
        if (bt_stall.pc[i] == pc && bt_stall.rets[i] == rets) { bt_stall.cnt[i]++; return; }
    }
    bt_stall.other++;
}

/* the classic baseband's native clock, latched as the library's READ_SLOT_CLK does (write 1 to 0x2001C, wait for 0, read
 * 0x20020), but with a bound: -> the clock, *polls = the reads the latch took (0xFFFF: it never cleared, the clock is not
 * running) */
static uint32_t bt_stall_clkn(uint32_t *polls)
{
    uint32_t n = 0;
    *(volatile uint32_t *)0x2001Cu = 1u;
    while (*(volatile uint32_t *)0x2001Cu && ++n < 0xFFFFu) ;
    *polls = n;
    return *(volatile uint32_t *)0x20020u;
}

/* the watch fires: everything but the registers first, then the magic, then the registers (a read that never comes back
 * leaves regs_ok at 0, and the watchdog's reset keeps the rest). reg: 0x20000 (the baseband's control), 0x2000C, 0x20014
 * (the slot's pending bits), 0x20018, 0x2001C as found (1: a latch the library asked for is still pending), the native
 * clock latched twice 1 ms apart, the reads each latch took (low and high half), 0x2FC40, 0x2FC6C (the BT domain's
 * power and clock words that the classic code polls), 0x10010 (bits 14-15: the BT domain's power / reset) */
static void bt_stall_fire(void)
{
    static const uint32_t regs[5] = { 0x20000u, 0x2000Cu, 0x20014u, 0x20018u, 0x2001Cu };
    const char *t = os_current_task();
    uint32_t p1, p2;
    uint32_t i;
    for (i = 0; i < sizeof bt_stall.task - 1u && t && t[i]; i++) bt_stall.task[i] = t[i];
    bt_stall.task[i] = 0;
    bt_stall.stage = ble_stage;
    bt_stall.heap_min = heap_min_free; bt_stall.heap_free = heap_free_bytes;
    bt_stall.mfail = bt_heap_fails; bt_stall.mfail_size = bt_heap_fail_size;
    bt_stall.irq40 = bt_cnt_irq_br; bt_stall.irq41 = bt_cnt_irq_slot;
    bt_stall.ev = bt_ev_n; bt_stall.ev_i = bt_ev_i;
    for (i = 0; i < 4u; i++) bt_stall.ev_last[i] = bt_ev_i >= 4u - i ? bt_ev_hist[(bt_ev_i - 4u + i) % 6u] : 0;
    bt_stall.stub = bt_stub_calls; bt_stall.stub_last = bt_stub_last;
    for (i = 0; i < BT_STALL_CRUMBS; i++) bt_stall.crumb[i] = bt_crumb[i];
    bt_stall.crumb_i = bt_crumb_i;
    bt_stall.regs_ok = 0;
    bt_stall.magic = BT_STALL_MAGIC;
    for (i = 0; i < 5u; i++) bt_stall.reg[i] = *(volatile uint32_t *)(uintptr_t)regs[i];
    bt_stall.reg[5] = bt_stall_clkn(&p1);
    fm1_delay_us(1000u);                                        /* (3.2 ticks of the native clock: does it run?) */
    bt_stall.reg[6] = bt_stall_clkn(&p2);
    bt_stall.reg[7] = p1 | p2 << 16;
    bt_stall.reg[8] = *(volatile uint32_t *)0x2FC40u;
    bt_stall.reg[9] = *(volatile uint32_t *)0x2FC6Cu;
    bt_stall.reg[10] = *(volatile uint32_t *)0x10010u;
    bt_stall.regs_ok = 1;
}

static void bt_stall_tick(int firing)
{
    ble_usr_tick();                                             /* (test build 37: the usr timeouts, every ms, ble/ble_os.c) */
    if (ble_busy_ticks <= BLE_STALL_TICKS - BT_STALL_WINDOW) { bt_stall_on = 0; return; }
    if (!bt_stall_on) { bt_stall_begin(); bt_stall_on = 1; }
    if (!firing) bt_stall_sample();
    else if (bt_stall_on == 1) { bt_stall_fire(); bt_stall_on = 2; }
}

static int bt_stall_valid(void) { return bt_stall.magic == BT_STALL_MAGIC && bt_stall.crumb_i < 0x100000u; }

/* the flash reads Bluetooth needs, before the audio starts (openpocket.c app_main): they switch the flash path off (ota_fread,
 * fl_uid_ram) with only CPU0's interrupts off, and once the audio runs the second core may be in a job that reads the flash.
 * Up to test build 11 they ran in ble_test_start, after audio_start (harmless there only because the DSP was still held) */
static void bt_flash_reads(void)
{
    ble_vm_load();                                              /* the stored radio records, read once before the controller runs */
    bt_mac_make();                                              /* this pedal's address, from the flash's unique ID */
    if (px_flash_ok) bt_ps_boot();                              /* test build 14: the pairings kept in the flash (bt_ps.h; it may
                                                                 * erase a sector: no audio and no radio yet) */
}

void ble_test_start(void)
{
    bt_stall.magic = 0;                                         /* (a stall record is shown until Bluetooth starts again) */
    __crc16_mutex_init();                                       /* else thread_create waits for ever in crc_get_32bit */
    bt_sbc_reset();
    bt_ring_init2(&bt_ring, bt_ring_mem, bt_ring_smem);
    bt_fq = bt_fq_mem;                                          /* the phone's packets (test build 24: no longer from the heap) */
    os_sem_create(bt_cmd_sem, 0);
    bt_cmd_up = 1;
    __set_simple_pair_flag(1);                                  /* Secure Simple Pairing ... */
    __set_simple_pair_param(3, 0, 2);                           /* ... no display or keys, no OOB, 2 = HCI "no MITM, dedicated bonding": Just Works */
    __set_a2dp_auto_play_flag(1);
    __set_page_timeout_value(8000);
    __set_super_timeout_value(8000);
    __change_hci_class_type(BT_COD);
    lmp_set_sniff_disable();                                    /* the link never sleeps (the pedal is powered while it plays) */
    ble_state = BLE_STARTING;
    bt_st = BT_STARTING;
    ble_stage_set(BLE_STAGE_TASK);
    bt_t_start = ble_ms();
    if (task_create(bt_app_task, 0, "btapp")) { ble_fail(BLE_FAIL_TASK_CREATE, -1); bt_st = BT_FAILED; }
}

/* the FIFO's packets -> SBC frames -> the ring, as long as it has room for a frame (24 frames a pass at most: 70 ms). What the
 * library hands the app (the packet its channel's open marked, and any it keeps: after a heal, or a library that works) is a
 * copy of a packet the FIFO holds: it is given back undecoded, every pass, whatever the ring's room (it holds the radio's
 * receive pool) */
static void bt_fq_done(void) { if (bt_fq_held) { bt_fq_r++; bt_fq_held = 0; bt_cur_left = 0; } }
/* test build 24: the longest wait between two decodes while the music plays (ms), and how many waits were longer than the
 * decoded ring (58 ms: each one an underrun, unless the FIFO's backlog was decoded meanwhile by bt_yield). The FIFO line's GAP */
static uint32_t bt_dec_t, bt_gap_max, bt_gap_n, bt_yields;
static void bt_decode(void)
{
    uint32_t pass = 0;
    uint8_t *p = 0;
    {
        const uint32_t now = ble_ms();
        if (bt_st == BT_PLAYING && bt_dec_t) {
            const uint32_t g = now - bt_dec_t;
            if (g > bt_gap_max) bt_gap_max = g;
            if (g > 58u) bt_gap_n++;
        }
        bt_dec_t = now ? now : 1u;
    }
    while (a2dp_media_try_get_packet(&p) > 0 && p) {
        a2dp_media_free_packet(p);
        bt_a2.dup++;
        p = 0;
    }
    while (bt_ring_room(&bt_ring) >= BT_SBC_MAX_SAMPLES && pass++ < 24u) {
        int k;
        if (!bt_pn) {
            const bt_fq_t *s;
            uint32_t off;
            bt_fq_done();
            if (!bt_fq || bt_fq_r == bt_fq_w) break;
            s = &bt_fq[bt_fq_r % BT_FQ_N];
            bt_fq_held = 1;
            bt_fq_frames -= s->frames; bt_cur_left = s->frames;
            bt_pkts++;
            off = bt_sbc_payload(s->b, s->n);
            bt_pp = s->b + off;
            bt_pn = off < s->n ? s->n - off : 0u;               /* (a header longer than the packet: nothing to decode) */
        }
        k = bt_sbc_frame2(&bt_pp, &bt_pn, bt_frame, bt_frame_s);
        if (k <= 0) { bt_pn = 0; continue; }                    /* the packet is done (or refused: the rest is dropped) */
        if (bt_cur_left) bt_cur_left--;
        bt_ring_put2(&bt_ring, bt_frame, bt_frame_s, (uint32_t)k);
        bt_frames_in++;
    }
    if (!bt_pn) bt_fq_done();
}

/* test build 24: the screen's long drawings (a page, a preset's change: hundreds of ms while the NAM network holds CPU0 most of
 * the time) left the music undecoded: the 58 ms ring ran dry (UND) and the packets that came meanwhile overflowed the FIFO (its
 * drops). The screen's two DMA loops (openpocket.c lcd_rect, lb_flush) call this between two buffers: 4 ms or more after the
 * last decode, the FIFO's backlog (150 ms) is decoded into the ring and the rate steered, as the main loop does. Only that: the
 * stack's tasks are not run from here, so no callback (a packet, an event, BLE MIDI) comes in the middle of a drawing */
static void bt_yield(void)
{
    static uint8_t in;
    uint32_t now;
    if (in || bt_st < BT_LINKED || bt_st == BT_FAILED) return;
    now = ble_ms();
    if (now - bt_dec_t < 4u) return;
    in = 1u;
    bt_yields++;
    bt_decode();
    if (now - bt_t_steer >= 4u) {
        bt_t_steer = now;
        bt_ring_rate(&bt_ring, bt_sbc_info.rate);
        bt_ring_steer(&bt_ring, bt_fq_samples(), BT_TARGET, BT_PREBUF);
    }
    in = 0u;
}

/* ---- the isr line's figures (bt_isr_line): the interrupts keep the longest and the sum of the current second, the main loop
 * turns the second over here (the interrupts off for a few loads and stores) and keeps the last whole second's */
static uint32_t bt_isr_t_s;
static struct { uint32_t br_max, br_sum, slot_max, slot_sum, t5, gap, in, en[5], sw, sw0, age; } bt_isr_last;
static void bt_isr_second(uint32_t now)
{
    uint32_t i;
    if (now - bt_isr_t_s < 1000u) return;
    bt_isr_t_s = now;
    local_irq_disable();
    bt_isr_last.br_max = bt_isr_t_br.max_s; bt_isr_last.br_sum = bt_isr_t_br.sum_s; bt_isr_t_br.max_s = bt_isr_t_br.sum_s = 0;
    bt_isr_last.slot_max = bt_isr_t_slot.max_s; bt_isr_last.slot_sum = bt_isr_t_slot.sum_s; bt_isr_t_slot.max_s = bt_isr_t_slot.sum_s = 0;
    bt_isr_last.t5 = px_t5_in_s; px_t5_in_s = 0;
    bt_isr_last.gap = a_irq_gap_s; a_irq_gap_s = 0;
    bt_isr_last.in = a_irq_in_s; a_irq_in_s = 0;
    for (i = 0; i < 5u; i++) { bt_isr_last.en[i] = a_en_s[i]; a_en_s[i] = 0; }   /* (test build 12: px_audio.h fm1_alnk0_irq) */
    bt_isr_last.sw = a_swaps - bt_isr_last.sw0; bt_isr_last.sw0 = a_swaps;   /* (v42 px_aorder.h: the output blocks asked first) */
    bt_isr_last.age = a_ord_age_s; a_ord_age_s = 0;   /* (and the longest from such a block's note to its input: px_audio.h) */
    local_irq_enable();
}

/* the second core's fault, as the main loop first saw it (the c1 line's S): the radio's start-up stage << 28 | bt_st << 24 |
 * the DSP held << 23 | 1 << 22 | IRQ 40's count (fm1_cpu1_fault_c runs before this file's names are known) */
static uint32_t bt_c1_snap;
/* test build 13: the reverb's loop's first cut (engine/px_dsp.h px_loop_wild), as the main loop first saw it: the time (ms
 * since power-on, as the crash report's UPTIME) and the same word as S (the c1 line's W) */
static uint32_t bt_w_at[2];
static __attribute__((noinline)) uint32_t bt_snap(void)   /* (one copy for its two uses: the smaller code) */
{
    return (ble_stage & 15u) << 28 | (bt_st & 15u) << 24 | (uint32_t)(a_hold != 0) << 23 | 1u << 22 | (bt_cnt_irq_br & 0x3FFFFFu);
}

/* test build 14: the DSP waits for Bluetooth's start-up. In every Bluetooth boot of test build 13 the second core faulted at
 * 1,964 ms, about 0.46 s after the scans and the DSP started (1.5 s); with the radio off (REV), never. The DSP (openpocket.c
 * a_hold: dry guitar meanwhile) comes on once Bluetooth has been VISIBLE for BT_DSP_HOLD_MS, or BT_DSP_MAX_MS after power-on at
 * the latest. Bluetooth off (REV, safe mode) or failed: at once (openpocket.c's 1.5 s, as before). DLY or CAB held at
 * power-on: at once too, test build 13's timing (the window kept measurable; CAB also raises the core supplies). The scans
 * that start again after a link's end or a KICK run with the DSP on: SE in the c1 line says when the last one was */
#define BT_DSP_HOLD_MS 2000u
#define BT_DSP_MAX_MS 8000u
static int bt_dsp_may_start(uint32_t now)
{
    (void)now;   /* test build 15: no hold. In build 14 the start-up event moved with the DSP's start (the normal boots hung
                  * then too), and with the stock core supplies (build 14's CAB boot) it did not come at all */
    return 1;
}

/* test build 14: a pattern right after the Bluetooth heap's end (bt/app-bt.ld .bss.bt_canary: then the library's own data,
 * the firmware's after it; the second core's stacks and mailbox, which lay there up to test build 13, are now above the
 * radio's 64 KB window), checked on every main-loop pass: CN in the c1 line, the words found changed (each one put back) and
 * the first one's value. Armed by app_main before Bluetooth starts */
#define BT_CANARY_N 8u
#define BT_CANARY(i) (0xCA9A5000u + (i))
static volatile uint32_t bt_canary[BT_CANARY_N] __attribute__((aligned(8), section(".bss.bt_canary")));
static uint32_t bt_cn_hits, bt_cn_bad;
static void bt_canary_arm(void)
{
    uint32_t i;
    for (i = 0; i < BT_CANARY_N; i++) bt_canary[i] = BT_CANARY(i);
}
static void bt_canary_check(void)
{
    uint32_t i, v;
    for (i = 0; i < BT_CANARY_N; i++)
        if ((v = bt_canary[i]) != BT_CANARY(i)) {
            if (!bt_cn_hits) bt_cn_bad = v;
            bt_cn_hits++;
            bt_canary[i] = BT_CANARY(i);
        }
}

void ble_test_poll(void)
{
    uint32_t now;
    ble_busy(1);
    ble_poll();
    ble_busy(0);
    bt_canary_check();
    now = ble_ms();
    bt_isr_second(now);
    if (fm1_cpu1_fault.count && !bt_c1_snap) bt_c1_snap = bt_snap();
    if (px_loop_wild[0] && !bt_w_at[1]) { bt_w_at[0] = fm1_uptime_ms(); bt_w_at[1] = bt_snap(); }
    if (bt_st == BT_STARTING && !bt_t_init && now - bt_t_start > 10000u) {   /* btstack_init not back in 10 s (the radio start-up
                                                                         * replay takes a few; a call that hangs is the stall watch's) */
        ble_wait = ble_task_waiting("btctrler");
        ble_fail(BLE_FAIL_TIMEOUT, (int32_t)ble_stage);
        bt_st = BT_FAILED;
    }
    if (bt_st == BT_STARTING && bt_t_init && !bt_init_ok && now - bt_t_init > 2000u) {
        bt_st = BT_VISIBLE; ble_state = BLE_ADV;                /* no "ready" event within 2 s of the init: make it visible anyway */
        if (!bt_t_vis) bt_t_vis = ms_now();
        bt_cmd(1u);
    }
    {   /* test build 18: the last phone called back, once per power-on: 3 s after Bluetooth became visible and 5 s after power-on
         * (well past the DSP's start at 1.5 s and the start-up window after it), unless a phone has come by itself meanwhile.
         * Test build 34: and once the footswitch is linked (no paging while the pedal connects to it), or 20 s after power-on */
        const uint32_t t = ms_now();
        if (!bt_rc_state && bt_st == BT_VISIBLE && bt_t_vis && t - bt_t_vis >= 3000u && t >= 5000u
            && (bt_le_cen_settled() || t >= 20000u)) {
            bt_rc_state = 1u; bt_rc_ms = t;
            bt_cmd(2u);
        }
    }
    if (bt_cnt_irq_br != bt_q40_n) { bt_q40_n = bt_cnt_irq_br; bt_q40_t = now; }
    else if (bt_st == BT_VISIBLE && now - bt_q40_t > (bt_le_up() ? BT_QUIET_LE_MS : BT_QUIET_MS)) {
        /* visible, no link, and the baseband's interrupt has been silent for 5 s: the scans have stopped (test build 3 after a
         * failed pairing). Ask for them again (the link scheduler arms its alarm anew); KICK counts it. Test build 28: with an LE
         * link up the radio may serve it for seconds on end (the editor's catalogue read, about 20 s), so 30 s of silence: test
         * build 27's editor link ended by supervision timeout in a boot with two kicks, which came first is not known */
        bt_q40_t = now; bt_kicks++; bt_kick_ms = ms_now();
        BT_CRUMB("kick: scan again");
        bt_cmd(1u);
    }
    bt_le_watch(now);                                           /* (test build 28: the LE side's watchdog, bt_le_midi.c) */
    if (bt_st >= BT_LINKED && bt_st != BT_FAILED) {
        bt_decode();
        if (now - bt_t_steer >= 4u) {                           /* (the encoded backlog: the FIFO's, test build 12; up to test
                                                                 * build 11 the library's remaining time) */
            bt_t_steer = now;
            bt_ring_rate(&bt_ring, bt_sbc_info.rate);
            bt_ring_steer(&bt_ring, bt_fq_samples(), BT_TARGET, BT_PREBUF);
        }
    }
    bt_ps_service(now);                                         /* test build 14: a changed pairing into the flash, 1 s after the
                                                                 * library's last write (bt_ps.h; the audio held about 25 ms) */
}

/* ---- the screen and the log */
static char *bt__hex(char *o, uint32_t v, int digits) { while (digits--) *o++ = "0123456789ABCDEF"[(v >> (4 * digits)) & 15u]; return o; }
static char *bt__dec(char *o, uint32_t v) { return ble__num(o, (int32_t)(v > 0x7FFFFFFFu ? 0x7FFFFFFF : v)); }

/* the short text before the load in the status area: "BT START 4", "BT VISIBLE", "BT LINK", "BT PLAY", "FAILED s c" */
void ble_test_state(char *out)
{
    char *o = out;
    switch (bt_st) {
    case BT_STARTING: o = ble__put(o, "BT START "); o = bt__dec(o, ble_stage); break;
    case BT_VISIBLE: o = ble__put(o, "BT VISIBLE"); break;
    case BT_LINKED: o = ble__put(o, "BT LINK"); break;
    case BT_PLAYING: o = ble__put(o, "BT PLAY"); break;
    case BT_FAILED: ble_status_text(out); return;
    default: o = ble__put(o, "BT OFF"); break;
    }
    *o = 0;
}

/* ---- the stall record's three lines, shown instead of the usual ones while Bluetooth is off (the safe-mode boot after a stall):
 *   STALL btctrler S8 BB 00000107 0000FE02 00000000 0000FFFF 00000001 CLKN 00001234 00001240 L 3/3 PW 00000400 00000000 0000C000
 *   STALL HEAP 1234 MIN 567 MF 1 2560 IRQ 0/0 EV 3 3:03 3:06 STUB 0 CR btctrler, request_irq 40, request_irq 41, low_power_get
 *   STALL PC 02012345/02011111*9876 ... N 10000+0 D 28-160 SP 01C7BF80
 * (out: ED_DIAG_LEN, 224 bytes: each line stops at 220) */
static char *bt__lim(char *o, const char *s, const char *end) { while (*s && o < end) *o++ = *s++; return o; }

/* a radio pool (bt_lbuf.h): " TX used/peak/size Ffails" */
static char *bt_pool_text(char *o, const char *tag, const uint8_t *lo, const uint8_t *hi)
{
    uint32_t i;
    for (i = 0; i < BT_LBUF_POOLS; i++) {
        const bt_lbuf_head_t *h = bt_lbuf_stat[i].h;
        if (!h || (const uint8_t *)h < lo || (const uint8_t *)h >= hi) continue;
        o = ble__put(o, tag); o = bt__dec(o, h->used); *o++ = '/'; o = bt__dec(o, bt_lbuf_stat[i].peak);
        *o++ = '/'; o = bt__dec(o, h->size); o = ble__put(o, " F"); o = bt__dec(o, bt_lbuf_stat[i].fails);
        return o;
    }
    return o;
}

static void bt_stall_line_regs(char *out)
{
    char *o = ble__put(out, "STALL ");
    uint32_t i;
    o = bt__lim(o, bt_stall.task[0] ? bt_stall.task : "main", out + 40);
    o = ble__put(o, " S"); o = bt__dec(o, bt_stall.stage);
    if (!bt_stall.regs_ok) { o = ble__put(o, " REGS NOT READ (the watchdog reset first)"); *o = 0; return; }
    o = ble__put(o, " BB");
    for (i = 0; i < 5u; i++) { *o++ = ' '; o = bt__hex(o, bt_stall.reg[i], 8); }
    o = ble__put(o, " CLKN "); o = bt__hex(o, bt_stall.reg[5], 8); *o++ = ' '; o = bt__hex(o, bt_stall.reg[6], 8);
    o = ble__put(o, " L "); o = bt__dec(o, bt_stall.reg[7] & 0xFFFFu); *o++ = '/'; o = bt__dec(o, bt_stall.reg[7] >> 16);
    o = ble__put(o, " PW");
    for (i = 8; i < 11u; i++) { *o++ = ' '; o = bt__hex(o, bt_stall.reg[i], 8); }
    *o = 0;
}

static void bt_stall_line_state(char *out)
{
    char *o = ble__put(out, "STALL HEAP "), *const end = out + 220;
    uint32_t i, n;
    o = bt__dec(o, bt_stall.heap_free); o = ble__put(o, " MIN "); o = bt__dec(o, bt_stall.heap_min);
    o = ble__put(o, " MF "); o = bt__dec(o, bt_stall.mfail); *o++ = ' '; o = bt__dec(o, bt_stall.mfail_size);
    o = ble__put(o, " IRQ "); o = bt__dec(o, bt_stall.irq40); *o++ = '/'; o = bt__dec(o, bt_stall.irq41);
    o = ble__put(o, " EV "); o = bt__dec(o, bt_stall.ev);
    n = bt_stall.ev_i < 4u ? bt_stall.ev_i : 4u;
    for (i = 4u - n; i < 4u; i++) { *o++ = ' '; o = bt__dec(o, bt_stall.ev_last[i] >> 12); *o++ = ':'; o = bt__hex(o, (bt_stall.ev_last[i] >> 4) & 0xFFu, 2); }
    o = ble__put(o, " STUB "); o = bt__dec(o, bt_stall.stub);
    if (bt_stall.stub && bt_stall.stub_last) { *o++ = ' '; o = bt__lim(o, bt_stall.stub_last, o + 32); }
    o = ble__put(o, " CR");                                     /* the crumbs, oldest first */
    n = bt_stall.crumb_i < BT_STALL_CRUMBS ? bt_stall.crumb_i : BT_STALL_CRUMBS;
    for (i = bt_stall.crumb_i - n; i < bt_stall.crumb_i && o < end - 2; i++) {
        const char *c = bt_stall.crumb[i % BT_STALL_CRUMBS];
        *o++ = i == bt_stall.crumb_i - n ? ' ' : ',';
        if (i != bt_stall.crumb_i - n) *o++ = ' ';
        o = bt__lim(o, c ? c : "?", end);
    }
    *o = 0;
}

static void bt_stall_line_pcs(char *out)
{
    char *o = ble__put(out, "STALL PC");
    uint32_t i;
    for (i = 0; i < BT_STALL_N && bt_stall.cnt[i]; i++) {
        *o++ = ' '; o = bt__hex(o, bt_stall.pc[i], 8); *o++ = '/'; o = bt__hex(o, bt_stall.rets[i], 8);
        *o++ = '*'; o = bt__dec(o, bt_stall.cnt[i]);
    }
    o = ble__put(o, " N "); o = bt__dec(o, bt_stall.samples); *o++ = '+'; o = bt__dec(o, bt_stall.other);
    o = ble__put(o, " D "); o = bt__dec(o, bt_stall.dmin); *o++ = '-'; o = bt__dec(o, bt_stall.dmax);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) { o = ble__put(o, " SP "); o = bt__hex(o, fm1_crash.sp, 8); }
    *o = 0;
}

/* visible, no link: "BT VIS IRQ 40:123 41:4567 EV 9 SCAN 1 V F BD 0B0B ADDR 024F1A2B3C4D UID 1 HEAP 9120 STUB 0 Q40 0 KICK 0
 * TX 0/592/2032 F0 RX 0/0/12272 F0" (out: 224 bytes) */
/* the long lines are built here, then cut to the 223 characters out holds (ED_DIAG_LEN 224, the marquee's full[224]). One
 * buffer for them all: they are built in the main loop only, one at a time, each copied out before it returns */
static char bt_line_t[400];
static void bt_line_out(char *out, const char *t)
{
    uint32_t i;
    for (i = 0; i < 223u && t[i]; i++) out[i] = t[i];
    out[i] = 0;
}

void ble_test_line(char *out)
{
    char *const t = bt_line_t;
    char *o = t;
    uint32_t i;
    if (bt_st == BT_OFF && bt_stall_valid()) { bt_stall_line_regs(out); return; }
    if (bt_st == BT_OFF) { o = ble__put(out, bt_off_by_key ? "BT OFF: REV HELD AT POWER-ON (THIS BOOT WITHOUT BLUETOOTH)" : "BT OFF"); *o = 0; return; }
    if (bt_st == BT_FAILED || bt_st == BT_STARTING) { ble_status_full(out); return; }
    o = ble__put(o, "BT VIS IRQ 40:"); o = bt__dec(o, bt_cnt_irq_br);
    o = ble__put(o, " 41:"); o = bt__dec(o, bt_cnt_irq_slot);
    o = ble__put(o, " EV "); o = bt__dec(o, bt_ev_n);
    o = ble__put(o, bt_init_ok ? " RDY" : " NORDY");
    o = ble__put(o, " SCAN "); o = bt__dec(o, bt_scans);
    /* test build 18: RC the last phone's call-back (0 not yet, 1 asked, 2 no pairing stored, 3+ paged) @ when; PS the stock power
     * setup (0 not done: DLY held, 1 done, 2 done with the 1.3 V supply moved onto its DC-DC) and P3_ANA_CON0 / CON5 read back */
    o = ble__put(o, " RC "); o = bt__dec(o, bt_rc_state); *o++ = '@'; o = bt__dec(o, bt_rc_ms);
    o = ble__put(o, " PS "); o = bt__dec(o, bt_pwr_stock); *o++ = ' ';
    o = bt__hex(o, fm1_p33_read(FM1_P3_ANA_CON0), 2); *o++ = '/'; o = bt__hex(o, fm1_p33_read(FM1_P3_ANA_CON5), 2);
    o = ble__put(o, " V "); o = ble__hex(o, ble_vm_flags()); *o++ = ' '; o = bt__hex(o, ble_vm_area >> 12, 2);   /* (as the BLE image) */
    *o++ = ' '; o = bt__hex(o, ble_vm_trim(), 4);
    o = ble__put(o, " ADDR ");
    for (i = 6; i--;) o = bt__hex(o, bt_mac[i], 2);
    o = ble__put(o, " UID "); o = bt__dec(o, (uint32_t)bt_uid_ok);
    o = ble__put(o, " HEAP "); o = bt__dec(o, ble_heap_min_free());
    o = ble__put(o, " STUB "); o = bt__dec(o, bt_stub_calls);
    if (bt_stub_last) { *o++ = ' '; o = bt__lim(o, bt_stub_last, o + 40); }
    o = ble__put(o, " Q40 "); o = bt__dec(o, (ble_ms() - bt_q40_t) / 1000u);
    o = ble__put(o, " KICK "); o = bt__dec(o, bt_kicks);
    if (bt_kicks) { *o++ = '@'; o = bt__dec(o, bt_kick_ms); }  /* (test build 28: the last one's time, ms since power-on) */
    o = bt_pool_text(o, " TX ", acl_tx_pool, acl_tx_pool_end);
    o = bt_pool_text(o, " RX ", acl_rx_pool, acl_rx_pool_end);
    *o = 0;
    bt_line_out(out, t);
}

/* a link: "BT LINK n1 ENC 1 A2DP 1 PLAY 1 SBC 44100 2CH BP53 FR 1234 ER 0 PK 200 BUF 98+52 MS UND 0 RATE -0.04% DISC 0 FQ 6
 * F A3012/C2344/S1876" (BUF: the FIFO's encoded backlog + the decoded ring, FQ: the packets in the FIFO, test build 12; F: each
 * task's stack never used, in bytes, after its initial: btApp, btCtrler, btStack) */
/* no phone linked: the tasks instead, "TASKS btapp S 01C0A1B0=0 q0 F3012 P0, btctrler Q q0 F2344 P3, btstack R q2 F1876 P7"
 * (state: R ready, Q waiting for a message, S waiting on the semaphore or mutex at that address (= its count), D sleeping,
 * X ended; q the messages queued, F the bytes of its stack never used, P the most messages it had queued, OVF its stack's
 * red zone was written: it nearly overflowed). A task that stays S with its queue growing is the one that is stuck */
static void bt_tasks_line(char *out)
{
    static const char st_ch[] = "FRQSDX";
    char *o = ble__put(out, "TASKS");
    ble_task_info_t t;
    uint32_t i;
    for (i = 0; ble_task_info(i, &t); i++) {
        if (!t.state) continue;
        if (o > out + 160) break;
        o = ble__put(o, i ? ", " : " "); o = bt__lim(o, t.name, o + 12);
        *o++ = ' '; *o++ = t.state < 6u ? st_ch[t.state] : '?';
        if (t.state == 3u && t.wait_on) { *o++ = ' '; o = bt__hex(o, (uint32_t)(uintptr_t)t.wait_on, 8); *o++ = '='; o = ble__num(o, *(const volatile int32_t *)t.wait_on); }
        o = ble__put(o, " q"); o = bt__dec(o, t.queued);
        o = ble__put(o, " F"); o = bt__dec(o, t.free);
        o = ble__put(o, " P"); o = bt__dec(o, t.qpeak);
        if (t.ovf) o = ble__put(o, " OVF");
    }
    *o = 0;
}
/* test build 37: the timers (ble/ble_os.c), on the HCI line: " TS 6/9 TU 0/3 TF 0 TI 412 TR 405 TB 0", the sys table's slots in
 * use / the most ever, the usr table's the same, the adds refused (a full table: that callback never came), the adds from an
 * interrupt (both tables), the usr timeouts run, the times a due one waited a millisecond because the baseband's lock was
 * taken (0 expected: ble_os.c usr_run) */
static char *bt_timers_text(char *o)
{
    o = ble__put(o, " TS "); o = bt__dec(o, tm_used); *o++ = '/'; o = bt__dec(o, tm_peak);
    o = ble__put(o, " TU "); o = bt__dec(o, usr_used); *o++ = '/'; o = bt__dec(o, usr_peak);
    o = ble__put(o, " TF "); o = bt__dec(o, tm_fail + usr_fail);
    o = ble__put(o, " TI "); o = bt__dec(o, tm_irq + usr_irq);
    o = ble__put(o, " TR "); o = bt__dec(o, usr_fired);
    o = ble__put(o, " TB "); return bt__dec(o, usr_held);
}
static char *bt_stacks_text(char *o)                            /* " F A3012/C2344/S1876": each task's stack never used */
{
    ble_task_info_t t;
    uint32_t i, n = 0;
    o = ble__put(o, " F");
    for (i = 0; ble_task_info(i, &t); i++) {
        if (!t.state) continue;
        *o++ = n++ ? '/' : ' ';
        *o++ = t.name[2] == 'a' ? 'A' : t.name[2] == 'c' ? 'C' : t.name[2] == 's' ? 'S' : t.name[2] == 'e' ? 'E' : '?';   /* btApp btCtrler btStack btEncry */
        o = bt__dec(o, t.free);
        if (t.ovf) *o++ = '!';
    }
    return o;
}

void ble_midi_line(char *out)
{
    char *o = bt_line_t;
    if (bt_st == BT_OFF && bt_stall_valid()) { bt_stall_line_state(out); return; }
    if (bt_st < BT_LINKED) {                                    /* (the link's figures are all 0 until a phone is linked) */
        char *e = bt_line_t;                                    /* (test build 27: built in bt_line_t and cut at 223 characters
                                                                 * as the other lines; up to test build 26 in out itself, 224
                                                                 * bytes, which the LE words could pass) */
        bt_tasks_line(bt_line_t);
        while (*e) e++;
        if (e - bt_line_t < 170) e = bt_le_text(e);             /* (test build 23: the BLE MIDI side; test build 28: in full on
                                                                 * the LE line, bt_le_midi.c bt_le_line) */
        *e = 0;
        bt_line_out(out, bt_line_t);
        return;
    }
    const uint32_t ms = bt_fq_samples() * 10u / 441u;
    const int32_t base = (int32_t)(bt_ring.base ? bt_ring.base : 1u);   /* (32-bit: the firmware has no 64-bit division) */
    int32_t pm = ((int32_t)bt_ring.step - base) * 10000 / base;   /* 0.01 %; |step - base| <= 0.5 % of base */
    o = ble__put(o, bt_st == BT_PLAYING ? "BT PLAY n" : "BT LINK n"); o = bt__dec(o, bt_links);
    o = ble__put(o, " ENC "); o = bt__dec(o, bt_enc);
    o = ble__put(o, " A2DP "); o = bt__dec(o, bt_a2dp_ch);
    o = ble__put(o, " PLAY "); o = bt__dec(o, bt_media);
    o = ble__put(o, " SBC "); o = bt__dec(o, bt_sbc_info.rate);
    *o++ = ' '; o = bt__dec(o, bt_sbc_info.channels); o = ble__put(o, "CH BP"); o = bt__dec(o, bt_sbc_info.bitpool);
    o = ble__put(o, " FR "); o = bt__dec(o, bt_sbc_info.frames);
    o = ble__put(o, " ER "); o = bt__dec(o, bt_sbc_info.errors);
    o = ble__put(o, " PK "); o = bt__dec(o, bt_pkts);
    o = ble__put(o, " BUF "); o = bt__dec(o, ms);
    o = ble__put(o, "+"); o = bt__dec(o, bt_ring_fill(&bt_ring) * 10u / 441u); o = ble__put(o, " MS");
    o = ble__put(o, " UND "); o = bt__dec(o, bt_ring.stops);
    o = ble__put(o, " GAP "); o = bt__dec(o, bt_gap_max); *o++ = '/'; o = bt__dec(o, bt_gap_n);   /* (test build 24) */
    *o++ = '/'; o = bt__dec(o, bt_yields);
    o = ble__put(o, " RATE "); if (pm < 0) { *o++ = '-'; pm = -pm; } o = bt__dec(o, (uint32_t)pm / 100u); *o++ = '.';
    *o++ = (char)('0' + (pm / 10) % 10); *o++ = (char)('0' + pm % 10); *o++ = '%';
    o = ble__put(o, " DISC "); o = bt__dec(o, bt_disc);
    o = ble__put(o, " FQ "); o = bt__dec(o, bt_fq_w - bt_fq_r);
    o = bt_le_text(o);                                          /* (test build 23: the BLE MIDI side) */
    o = bt_stacks_text(o);
    *o = 0;
    bt_line_out(out, bt_line_t);
}

/* the last events and the stand-ins called: "EV 3:03 3:06 3:27 ... STUB 2 name" */
void ble_trace_line(char *out)
{
    char *o;
    uint32_t i, n = bt_ev_i < 6u ? bt_ev_i : 6u;
    if (bt_st == BT_OFF && bt_stall_valid()) { bt_stall_line_pcs(out); return; }
    o = ble__put(out, "EV");
    for (i = 0; i < n; i++) {
        uint32_t v = bt_ev_hist[(bt_ev_i - n + i) % 6u];
        *o++ = ' '; o = bt__dec(o, v >> 12); *o++ = ':'; o = bt__dec(o, (v >> 4) & 0xFFu);
    }
    o = ble__put(o, " IRQ 40:"); o = bt__dec(o, bt_cnt_irq_br);
    o = ble__put(o, " 41:"); o = bt__dec(o, bt_cnt_irq_slot);
    o = ble__put(o, " STUB "); o = bt__dec(o, bt_stub_calls);
    o = ble__put(o, " CR");                                     /* the last calls into the firmware, oldest first */
    {
        char *const end = out + 220;
        const uint32_t k = bt_crumb_i, m = k < BT_STALL_CRUMBS ? k : BT_STALL_CRUMBS;
        for (i = k - m; i < k && o < end - 2; i++) {
            const char *c = bt_crumb[i % BT_STALL_CRUMBS];
            *o++ = i == k - m ? ' ' : ',';
            if (i != k - m) *o++ = ' ';
            o = bt__lim(o, c ? c : "?", end);
        }
    }
    *o = 0;
}

/* the HCI conversation (bt_taskq_spy): "HCI n 120 ACL 4 NCP 2 Q 3 LOST 0 S1:0E04010C03 C2:030C000000 ..." the newest 11; S1 = an
 * event to the host (code, length, parameters), C2 / C3 = a command to the controller (as the controller's task receives it) */
static char *bt_msg_text(char *o, const bt_msg_t *m)
{
    uint32_t j;
    *o++ = ' '; *o++ = m->task; *o++ = m->type; *o++ = ':';
    for (j = 0; j < 5u; j++) o = bt__hex(o, m->b[j], 2);
    return o;
}
/* (test build 11: then WI, the waits refused inside an interrupt, WM, the waits the main loop asked for (a timer's callback:
 * it cannot wait), CNF, the pairing confirmations sent, END, the last link's end: 05:rr a Disconnection Complete and its reason
 * (13 the phone ended it, 08 supervision timeout, 22 LMP response timeout, 05 authentication failure), 03:ss a link that failed
 * to come up and its status; test build 37: the timers, TS TU TF TI TR TB (bt_timers_text); and as many of the newest messages
 * as fit) */
void bt_hci_line(char *out)
{
    char *o = ble__put(out, "HCI n ");
    const uint32_t k = bt_msg_kept;
    uint32_t i, m, fit;
    o = bt__dec(o, bt_msg_n); o = ble__put(o, " ACL "); o = bt__dec(o, bt_msg_acl); o = ble__put(o, " NCP "); o = bt__dec(o, bt_msg_ncp);
    o = ble__put(o, " Q "); o = bt__dec(o, bt_q_peak); o = ble__put(o, " LOST "); o = bt__dec(o, bt_q_lost);
    o = ble__put(o, " WI "); o = bt__dec(o, bt_wait_irq); o = ble__put(o, " WM "); o = bt__dec(o, bt_wait_main);
    o = ble__put(o, " CNF "); o = bt__dec(o, bt_confirms);
    if (bt_end) { o = ble__put(o, " END "); o = bt__hex(o, bt_end >> 8, 2); *o++ = ':'; o = bt__hex(o, bt_end & 0xFFu, 2); }
    o = bt_timers_text(o);                                      /* (test build 37: here, where the messages make room) */
    fit = o < out + 220 ? (uint32_t)(out + 220 - o) / 14u : 0u; /* (14 characters a message; the line holds 223) */
    m = k < BT_MSG_LAST ? k : BT_MSG_LAST;
    if (m > fit) m = fit;
    for (i = k - m; i < k; i++) o = bt_msg_text(o, &bt_msg_last[i % BT_MSG_LAST]);
    *o = 0;
}
static char *bt_words(char *o, const volatile uint32_t *v, uint32_t n)   /* n words in hex, each after a space */
{
    while (n--) { *o++ = ' '; o = bt__hex(o, *v++, 8); }
    return o;
}

/* the music path's health (test build 13, in place of build 12's a2dp line): "FIFO" and 4 words in hex: the packets the FIFO
 * took, the ones it dropped (full | too big << 16), the library's packets given back undecoded, the heals (tools/log.mjs
 * names them) */
void bt_fifo_line(char *out)
{
    char *o = ble__put(out, "FIFO");
    o = bt_words(o, &bt_fq_w, 1u);
    o = bt_words(o, &bt_a2.drop, 3u);
    *o = 0;
}

/* the second core (test build 12): "C1 1 G 0@0 K 2232 KEY 0 P 03/0B V 1452 D 3452 SE 1@1453 E 0/0 CN 0 S 00000000 F 0/0 W 0".
 * G: why it is off and when (ms); K: how deep its stack has gone (bytes of 2,816); KEY: the key held at power-on (1 CAB, 2
 * DLY, 3 REV: openpocket.c). Test build 14:
 *  P, the core supplies read back, P3_ANA_CON6 / CON9 in hex: VDC14 their low 3 bits (1.25 V + 50 mV a level), SYSVDD the
 *    low 4 (0.93 V + 30 mV a level), CON9 bit 4 its strong drive; levels 3 and 11 as the boot loader leaves them, 7 and 15
 *    with CON6 bit 3 and CON9 bit 4 as CAB sets them (the stock firmware's 0F/1F);
 *  V, when Bluetooth first became VISIBLE, and D, when the DSP came on (ms since power-on; 0 = not yet);
 *  SE, the scan enables sent and the last one's time (ms);
 *  E, CPU0's exceptions resumed as not its own (the echo of CPU1's fault / the late flag of a hold, fm1_cpu1.h
 *    fm1_cpu1_echo), and after one the PC CPU0 was at (the latest), in hex;
 *  CN, the canary's words found changed after the Bluetooth heap, and after one the first one's value in hex.
 * S: bt_c1_snap. Test build 13: W, the loops' cuts at their block's end (engine/px_dsp.h px_loop_wild); test build 14: after
 * one "/id" (1 the reverb, 2 the delay, 3 a lane's frame count, 4 the lanes' buffer index: PX_WILD_*), then 4 words in hex: n
 * (as the loop reloaded it, or the value read) and k (or the lane) at the latest, the first one's time (ms) and its S word
 * (bt_w_at). After a fault (F count/cpu), 9 words in hex: DBG, the taking core's EMU_MSG, PC, RETS (CPU0's when CPU0 took
 * it, F's second figure 0), CPU1's EMU_MSG, the time (ms, wraps at 179 s), the job handed and the job finished (different:
 * one was running), its function; then X and the debug unit's ten words from 0x1EEE358 in short hex (the write-limit error
 * counts, the bus masters' ids), the first thing cut when the line passes its 223 characters */
void bt_c1_line(char *out)
{
    char *o = ble__put(bt_line_t, "C1 ");
    uint32_t i;
    o = bt__dec(o, a_c1); o = ble__put(o, " G "); o = bt__dec(o, fm1_cpu1_gave_up); *o++ = '@'; o = bt__dec(o, fm1_cpu1_gave_ms);
    o = ble__put(o, " K "); o = bt__dec(o, fm1_cpu1_stack_used());
    o = ble__put(o, " KEY "); o = bt__dec(o, (uint32_t)keys_held_at_boot);
    o = ble__put(o, " P "); o = bt__hex(o, fm1_p33_read(FM1_P3_ANA_CON6), 2); *o++ = '/'; o = bt__hex(o, fm1_p33_read(FM1_P3_ANA_CON9), 2);
    o = ble__put(o, " V "); o = bt__dec(o, bt_t_vis);
    o = ble__put(o, " D "); o = bt__dec(o, bt_dsp_ms);
    o = ble__put(o, " SE "); o = bt__dec(o, bt_scans); *o++ = '@'; o = bt__dec(o, bt_scan_ms);
    o = ble__put(o, " E "); o = bt__dec(o, fm1_cpu1_fault.e[0]); *o++ = '/'; o = bt__dec(o, fm1_cpu1_fault.e[1]);
    if (fm1_cpu1_fault.e[0] | fm1_cpu1_fault.e[1]) { *o++ = ' '; o = bt__hex(o, fm1_cpu1_fault.epc, 8); }
    o = ble__put(o, " CN "); o = bt__dec(o, bt_cn_hits);
    if (bt_cn_hits) { *o++ = ' '; o = bt__hex(o, bt_cn_bad, 8); }
    o = ble__put(o, " S "); o = bt__hex(o, bt_c1_snap, 8);
    o = ble__put(o, " F "); o = bt__dec(o, fm1_cpu1_fault.count); *o++ = '/'; o = bt__dec(o, fm1_cpu1_fault.cpu);
    {   /* (the count read once: a cut meanwhile cannot leave "W 1" without its words) */
        const uint32_t w = px_loop_wild[0];
        o = ble__put(o, " W "); o = bt__dec(o, w);
        if (w) { *o++ = '/'; o = bt__dec(o, px_loop_wild[3]); o = bt_words(o, px_loop_wild + 1, 2u); o = bt_words(o, bt_w_at, 2u); }
    }
    if (fm1_cpu1_fault.count) {
        o = bt_words(o, &fm1_cpu1_fault.dbg, 9u);              /* (dbg .. fn) */
        o = ble__put(o, " X");
        for (i = 0; i < 10u; i++) { *o++ = ' '; o = ble__hex(o, fm1_cpu1_fault.dbgx[i]); }
    }
    *o = 0;
    bt_line_out(out, bt_line_t);
}

/* test build 19, the log's tenth line: "C1R RV 1@2811 DBG 00000010@2510 PC 01C6B0D6 AX 140/3FF SP 01C1FA40 R 1C20000 ... (r0..r15)":
 * the second core's revivals (px_audio.h a_c1_revive: how many, the last when, ms since power-on) and its last fault's record, kept
 * after a revival (fm1_cpu1_start clears the count the c1 line shows): the debug unit's message and when, the PC, AXI_RD_INV_ID /
 * AXI_WR_INV_ID (0x140 = CPU1, 0x100 = CPU0, 0x3FF none: the SDK's debug.h), the stack pointer and r0..r15 at the fault */
void bt_c1r_line(char *out)
{
    char *o = ble__put(bt_line_t, "C1R RV ");
    uint32_t i;
    o = bt__dec(o, a_c1_revives); *o++ = '@'; o = bt__dec(o, a_c1_revive_ms);
    if (fm1_cpu1_fault.ms) {
        o = ble__put(o, " DBG "); o = bt__hex(o, fm1_cpu1_fault.dbg, 8); *o++ = '@'; o = bt__dec(o, fm1_cpu1_fault.ms);
        o = ble__put(o, " PC "); o = bt__hex(o, fm1_cpu1_fault.pc, 8);
        o = ble__put(o, " AX "); o = ble__hex(o, fm1_cpu1_fault.dbgx[6]); *o++ = '/'; o = ble__hex(o, fm1_cpu1_fault.dbgx[7]);
        o = ble__put(o, " SP "); o = bt__hex(o, fm1_cpu1_fault.sp, 8);
        o = ble__put(o, " R");
        for (i = 0; i < 16u; i++) { *o++ = ' '; o = ble__hex(o, fm1_cpu1_fault.r[i]); }
    }
    *o = 0;
    bt_line_out(out, bt_line_t);
}

/* the interrupts against the audio (test builds 11, 12): "ISR 40:12/310/1450 41:4/40/90 T5 9/31 AG 2905/3120 AI 1710/2620 EN 0/344/
 * 344/0/0 OC 0/3 OD 0 OG 12/240 C1 1 G 0 NAM 1 LD 42 PK 55/97 XR 3 CL 0 WC 0 BYP 0 TM 1234 VM 4/2/1/0/3 OVF 0 NAN 0". Times in microseconds: for the
 * baseband's two interrupts (40, 41) the longest in the last second, the longest since boot and the time they took in the last
 * second; T5 the 10 kHz timer's longest (last second / since boot); AG the longest gap between two audio interrupts (2,902 when
 * one entry serves a period; less: two entries a period, the input's and the output's; more: an entry that came late) and AI its
 * longest stay (last second / since boot). Test build 12: EN the audio interrupt's entries in the last second by the flags they
 * found (none / the output's only / the input's only / both / another channel's), OC the output blocks the driver asked for before
 * their input's (px_aorder.h a_swaps), last second / since boot: each one a repeated and a skipped block up to test build 11,
 * one block of latency more in test builds 12 and 13; since v42 each is filled once its own input's block is made; each hold of
 * the interrupt, a full preset change or a flash write, adds one), OD (test build 14) those whose input's callback came too late
 * since boot (px_audio.h A_ORDER_TICKS: each one a click), OG the longest from such a block's note to its input's callback (last
 * second / since boot), NAN the engine's resets after a NaN since boot. Then the DSP: C1 the second core runs (0: one core), G why it is off (fm1_cpu1.h
 * FM1_CPU1_*: 0 it is not; 1 a misread, 2 a job not back within 8 ms, 4 a fault: these three
 * retired it in this boot and bar it at every start until a power-off; 3 barred at start; 5 no answer at start; the c1 line has
 * the rest), NAM 1 the preset's NAM amp is one that runs on both cores (test build 12: whatever C1 is), LD the load and PK its
 * peak (since the last full preset change / since boot) in %, XR the late blocks, CL the clipped ones, WC the wet lane's cut
 * blocks (since boot: test build 12 keeps them across a retirement or a reset), BYP bypassed (over 85 % for 8 blocks), then the
 * library's timers fired (TM), the pairing store's reads and writes by the library, then (test build 14, bt_ps.h) the copies
 * saved to the flash since power-on, the save rounds that failed and the flash copy's sequence number (VM r/w/s/f/q: q is the
 * copy loaded at power-on, 0 none, and one more for each slot a save tries), the stacks' red zones written (OVF) */
static uint32_t bt_us(uint32_t ticks) { return ticks / FM1_TICKS_PER_US; }
void bt_isr_line(char *out)
{
    char *o = ble__put(bt_line_t, "ISR 40:");
    o = bt__dec(o, bt_us(bt_isr_last.br_max)); *o++ = '/'; o = bt__dec(o, bt_us(bt_isr_t_br.max)); *o++ = '/'; o = bt__dec(o, bt_us(bt_isr_last.br_sum));
    o = ble__put(o, " 41:");
    o = bt__dec(o, bt_us(bt_isr_last.slot_max)); *o++ = '/'; o = bt__dec(o, bt_us(bt_isr_t_slot.max)); *o++ = '/'; o = bt__dec(o, bt_us(bt_isr_last.slot_sum));
    o = ble__put(o, " T5 "); o = bt__dec(o, bt_us(bt_isr_last.t5)); *o++ = '/'; o = bt__dec(o, bt_us(px_t5_in));
    o = ble__put(o, " AG "); o = bt__dec(o, bt_us(bt_isr_last.gap)); *o++ = '/'; o = bt__dec(o, bt_us(a_irq_gap));
    o = ble__put(o, " AI "); o = bt__dec(o, bt_us(bt_isr_last.in)); *o++ = '/'; o = bt__dec(o, bt_us(a_irq_in));
    {   /* test build 12: the entries by the flags seen (px_audio.h); test build 14: the output blocks asked first (px_aorder.h),
         * the notes of them dropped, the longest from a note to its input's callback */
        uint32_t i;
        o = ble__put(o, " EN");
        for (i = 0; i < 5u; i++) { *o++ = i ? '/' : ' '; o = bt__dec(o, bt_isr_last.en[i]); }
        o = ble__put(o, " OC "); o = bt__dec(o, bt_isr_last.sw); *o++ = '/'; o = bt__dec(o, a_swaps);
        o = ble__put(o, " OD "); o = bt__dec(o, a_ord_drop);
        o = ble__put(o, " OG "); o = bt__dec(o, bt_us(bt_isr_last.age)); *o++ = '/'; o = bt__dec(o, bt_us(a_ord_age));
    }
    o = ble__put(o, " C1 "); o = bt__dec(o, a_c1);
    o = ble__put(o, " G "); o = bt__dec(o, fm1_cpu1_gave_up);
    o = ble__put(o, " NAM "); o = bt__dec(o, (uint32_t)px_nam_dual(&g_eng));   /* (the preset's type, whatever C1 is) */
    o = ble__put(o, " LD "); o = bt__dec(o, a_load);
    o = ble__put(o, " PK "); o = bt__dec(o, a_peak); *o++ = '/'; o = bt__dec(o, a_peak_all);
    o = ble__put(o, " XR "); o = bt__dec(o, a_xrun);
    o = ble__put(o, " CL "); o = bt__dec(o, a_clip);
    o = ble__put(o, " WC "); o = bt__dec(o, a_wcut);
    o = ble__put(o, " BYP "); o = bt__dec(o, a_bypass);
    o = ble__put(o, " TM "); o = bt__dec(o, ble_cnt_timer);
    o = ble__put(o, " VM "); o = bt__dec(o, bt_vm_reads); *o++ = '/'; o = bt__dec(o, bt_vm_writes);
    *o++ = '/'; o = bt__dec(o, bt_ps_saves); *o++ = '/'; o = bt__dec(o, bt_ps_bad); *o++ = '/'; o = bt__dec(o, bt_ps.seq);
    o = ble__put(o, " OVF "); o = bt__dec(o, bt_stack_ovf);
    o = ble__put(o, " NAN "); o = bt__dec(o, a_nan_count);
    *o = 0;
    bt_line_out(out, bt_line_t);
}
