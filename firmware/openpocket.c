/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — Milestone 1 (boot + screen) — Donner Pocket X.
 * SCREEN (reverse-engineered from the stock firmware): SOFTWARE SPI on PORT B
 *   CS = PB1, D/C = PB5, CLK = PB6, DO = PB7   (PC7 is not the screen's: the button lights' enable, px_leds.h)
 * (the FM-1 used port C: that was the cause of the black screen.)
 * Init sequence + 40-line offset: taken verbatim from the Pocket X firmware.
 * Safety net: 2 failed boots -> UBOOT (re-flashable), + backup fm1-1.bin.
 */
#include <stdint.h>
#define AUDIO_PROBE 0      /* 1 = audio test (find IN/OUT on the I2S) instead of the UI */
#define DSP_CPU1 1         /* 1 = DSP engine on the 2nd core (automatic fallback to CPU0 on trouble) */
#if DSP_CPU1 && !AUDIO_PROBE
#define FM1_CPU1 1         /* fm1_irq.h hands 2nd-core faults to fm1_cpu1.h */
#define FM1_CPU1_FALLBACK 1
#else
#undef DSP_CPU1
#define DSP_CPU1 0
#endif
#define FELUCCA_ID "FM-1_900"
/* the Bluetooth audio test build (docs/BT-AUDIO-TEST.md): its boot text and its id for the second core's bar (app_main) */
#define BT_TEST_BUILD 40
#define BT_STR_(x) #x
#define BT_STR(x) BT_STR_(x)
#define BT_TEST_BUILD_TEXT BT_STR(BT_TEST_BUILD)
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#if DSP_CPU1
#if OP_BT_AUDIO
#define FM1_CPU1_RECORD 1  /* the second core's fault record in full (the Bluetooth audio image's c1 line, bt/bt_a2dp.c) */
#define FM1_CPU1_ECHO 1    /* test build 14: CPU0's exceptions that are CPU1's fault echoed, or the late flag of holding it,
                            * recognised and counted (fm1_cpu1.h fm1_cpu1_echo) */
#define FM1_CPU1_ECHO_MAX 0u /* test build 15: none resumed (CPU0's crash, as up to build 13). Build 14 resumed up to 4: on
                            * the pedal the boots it resumed hung until the watchdog, the others did not */
#define FM1_CPU1_OWN_RAM 1 /* test build 14: its mailbox in a section of its own, out of the radio's window with its stacks
                            * (bt/app-bt.ld) */
#endif
#if OP_BLE_TEST
#define FM1_CPU1_NOBAR 1   /* the stall watch's own crash bars no core (px_usb.h fm1_timer5_irq) */
#endif
#include "fm1_cpu1.h"
#endif
#include "font8x8.h"
#include "libc.c"           /* memset / memcpy / memcmp (felucca-bsp, MPL-2.0): also used by the JieLi I2S driver */

/* ---- GPIO port B/C (base 0x50000 + port*0x40 ; OUT +0x00, DIR +0x08 ; DIR 0=output) ---- */
#define PB_OUT (*(volatile uint32_t *)0x50040u)
#define PB_DIR (*(volatile uint32_t *)0x50048u)
#define PC_OUT (*(volatile uint32_t *)0x50080u)
#define PC_DIR (*(volatile uint32_t *)0x50088u)
#define CS  (1u << 1)   /* PB1 */
#define DC  (1u << 5)   /* PB5 */
#define CLK (1u << 6)   /* PB6 */
#define DO  (1u << 7)   /* PB7 */
#define PC7 (1u << 7)

#define PX_ROW_OFFSET 0u
#define C(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
enum { RED=0xF800, GRN=0x07E0, BLU=0x001F, WHT=0xFFFF, BLK=0x0000, CYN=0x07FF, YEL=0xFFE0, ORG=0xFD20 };

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[], _bss_hi_start[], _bss_hi_end[], _pool_tail_start[], _pool_tail_end[],
                _bss_lo_start[], _bss_lo_end[];
static volatile struct { uint32_t magic, pending, failed; } guard __attribute__((section(".noinit")));

static uint32_t ms_now(void) { return (uint32_t)(fm1_ticks() / (FM1_TICKS_PER_US * 1000u)); }

/* ---- screen link: HARDWARE SPI1 + DMA (LCD_HW 1) or software SPI (LCD_HW 0, the old code, fallback) ----
 * PB6 = SPI1_CLK(A), PB7 = SPI1_DO(A) per the AC791x datasheets in the JieLi SDK; IOMAP_CON1 bit 4 = 0 -> group A
 * (as in the SDK's SPI1_PLATFORM_DATA). PB5 (D/C) stays a GPIO, like the FM-1's D/C on the group B DI pin.
 * SPI1 registers and CON = 0x4021 setting: Felucca's screen driver (bsp/boards/fm1/fm1_lcd_hw.h). */
#ifndef LCD_HW
#define LCD_HW 1
#endif
#define LCD_BAUD 2u                     /* SPI1 clock = LSB / (BAUD + 1): 60 MHz / 3 = 20 MHz */
#if LCD_HW
#define LSPI_CON  (*(volatile uint32_t *)0x11D00u)
#define LSPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define LSPI_BUF  (*(volatile uint32_t *)0x11D08u)
#define LSPI_ADR  (*(volatile uint32_t *)0x11D0Cu)
#define LSPI_CNT  (*(volatile uint32_t *)0x11D10u)
#define IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
static uint32_t lcd_timeouts;
static int lspi_busy;                   /* a DMA transfer is in progress */
static void lspi_wait(void)
{
    uint32_t n;
    if (!lspi_busy) return;
    for (n = 0; n < 2000000u && !(LSPI_CON & 0x8000u); n++) ;
    if (n == 2000000u) lcd_timeouts++;
    LSPI_CON |= 0x4000u;                /* clear the done flag */
    lspi_busy = 0;
}
static void lspi_init(void)
{
    IOMAP_CON1 &= ~0x10u;               /* SPI1 on group A: PB6/PB7 */
    LSPI_CON = 0x4021u;
    LSPI_BAUD = LCD_BAUD;
    lspi_busy = 0;
}
static inline void spi_byte(uint8_t b)  /* one byte, blocking */
{
    lspi_wait();
    LSPI_CON |= 0x4000u;
    LSPI_BUF = b;
    lspi_busy = 1;
    lspi_wait();
}
static void lspi_dma(const void *p, uint32_t n)   /* start n bytes from RAM (does not wait) */
{
    lspi_wait();
    LSPI_CON |= 0x4000u;
    LSPI_ADR = (uint32_t)(uintptr_t)p;
    LSPI_CNT = n;
    lspi_busy = 1;
}
#else
static inline void spi_byte(uint8_t b)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 0x80u) PB_OUT |= DO; else PB_OUT &= ~DO;
        PB_OUT &= ~CLK;
        PB_OUT |= CLK;             /* rising edge */
        b = (uint8_t)(b << 1);
    }
    PB_OUT &= ~CLK;
}
#endif
static void lcd_cmd(uint8_t c, const uint8_t *data, uint32_t n)
{
    PB_OUT &= ~CS;                 /* select */
    PB_OUT &= ~DC;                 /* command */
    spi_byte(c);
    if (n) {
        uint32_t i;
        PB_OUT |= DC;              /* data */
        for (i = 0; i < n; i++) spi_byte(data[i]);
    }
    PB_OUT |= CS;                  /* deselect */
}

static const uint8_t PX_INIT[] = {   /* EXACT sequence from the Pocket X firmware (21 commands) */
    0x01,0, 0x11,0,
    0x2A,4, 0x00,0x00,0x00,0xEF,
    0x2B,4, 0x00,0x28,0x01,0x17,
    0xB2,5, 0x0C,0x0C,0x0C,0x00,0x33,
    0x20,0,
    0xB7,1, 0x56, 0xBB,1, 0x18, 0xC0,1, 0x2C,
    0xC2,1, 0x01, 0xC3,1, 0x1F, 0xC4,1, 0x20, 0xC6,1, 0x0F,
    0xD0,2, 0xA6,0xA1,
    0xE0,14, 0xD0,0x0D,0x14,0x0B,0x0B,0x07,0x3A,0x44,0x50,0x08,0x13,0x13,0x2D,0x32,
    0xE1,14, 0xD0,0x0D,0x14,0x0B,0x0B,0x07,0x3A,0x44,0x50,0x08,0x13,0x13,0x2D,0x32,
    0x36,1, 0x00, 0x3A,1, 0x55, 0xE7,1, 0x00,
    0x51,1, 0xFF,                  /* max brightness */
    0x21,0, 0x29,0,                /* INVON, DISPON */
};
static void lcd_init(void)
{
    const uint8_t *p = PX_INIT, *end = PX_INIT + sizeof PX_INIT;
    PB_DIR &= ~(CS | DC | CLK | DO);   /* outputs */
    PC_DIR &= ~PC7;
    PB_OUT |= CS; PB_OUT &= ~CLK;      /* CS high, CLK low */
    PC_OUT |= PC7;                     /* PC7 high: the button lights off (their enable, low = on: px_leds.h) */
#if LCD_HW
    lspi_init();
#endif
    fm1_delay_ms(50);
    while (p < end) {
        uint8_t cmd = *p++, len = *p++;
        lcd_cmd(cmd, p, len);
        p += len;
        if (cmd == 0x01) fm1_delay_ms(150);
        else if (cmd == 0x11) fm1_delay_ms(120);
    }
}
/* DMA pixel buffers (2: fill one while the other is being sent) */
#define LBUF_PX 1024u
#if OP_BT_AUDIO
static void bt_yield(void);     /* (bt/bt_a2dp.c, test build 24: the music's queue decoded while the screen draws) */
static volatile uint8_t lcd_yield_off;   /* (test build 26: the crash screen, fm1_fault: the decoder may be where it stopped) */
#define LCD_YIELD() do { if (!lcd_yield_off) bt_yield(); } while (0)
#else
#define LCD_YIELD() ((void)0)
#endif
#if LCD_HW
static uint8_t lbuf[2][LBUF_PX * 2u] __attribute__((aligned(4)));
static uint32_t lb_i;
#endif
static uint32_t lb_n;
/* filled rectangle (16-bit coords + 40-line offset) */
static void lcd_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb)
{
    if (w == 0u || h == 0u || x >= 240u || y >= 240u) return;   /* v23: a negative size (huge unsigned) used to */
    if (x + w > 240u) w = 240u - x;                            /* stream 4 billion pixels = black screen forever */
    if (y + h > 240u) h = 240u - y;
    uint8_t a[4]; uint32_t i, yo = y + PX_ROW_OFFSET, x1 = x + w - 1u, y1 = yo + h - 1u, px = w * h;
    uint16_t sw = rgb;   /* sent high byte first, no swap */
    a[0]=(uint8_t)(x>>8);  a[1]=(uint8_t)x;  a[2]=(uint8_t)(x1>>8); a[3]=(uint8_t)x1;  lcd_cmd(0x2A,a,4);
    a[0]=(uint8_t)(yo>>8); a[1]=(uint8_t)yo; a[2]=(uint8_t)(y1>>8); a[3]=(uint8_t)y1;  lcd_cmd(0x2B,a,4);
    PB_OUT &= ~CS; PB_OUT &= ~DC; spi_byte(0x2C); PB_OUT |= DC;   /* RAMWR, then data */
#if LCD_HW
    {   /* one buffer filled with the colour, resent as many times as needed (constant contents) */
        uint8_t *b = lbuf[0];
        uint32_t fill = px < LBUF_PX ? px : LBUF_PX;
        for (i = 0; i < fill; i++) { b[2u * i] = (uint8_t)(sw >> 8); b[2u * i + 1u] = (uint8_t)sw; }
        while (px) {
            uint32_t n = px < LBUF_PX ? px : LBUF_PX;
            lspi_dma(b, n * 2u);
            px -= n;
            fm1_wdt_feed();
            LCD_YIELD();
        }
        lspi_wait();
    }
#else
    for (i = 0; i < px; i++) {
        spi_byte((uint8_t)(sw >> 8));
        spi_byte((uint8_t)sw);
        if ((i & 0x3FFu) == 0) fm1_wdt_feed();   /* feed the watchdog during large fills */
    }
#endif
    PB_OUT |= CS;
}

/* ================= OpenPocket UI (on-device sound editor) ================= */
/* palette (RGB565) matching the simulator: dark background + orange accent */
enum { UI_BG=0x0841, UI_PANEL=0x18E3, UI_SEL=0xFB00, UI_ACC=0xFD20,
       UI_TXT=0xEF7D, UI_DIM=0x8410, UI_OK=0x4E6B, UI_OFF=0x2124 };

/* write window + pixel stream (CS stays low between pixels) */
static void lcd_win(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t a[4]; uint32_t yo=y+PX_ROW_OFFSET, x1=x+w-1u, y1=yo+h-1u;
    a[0]=(uint8_t)(x>>8);a[1]=(uint8_t)x;a[2]=(uint8_t)(x1>>8);a[3]=(uint8_t)x1; lcd_cmd(0x2A,a,4);
    a[0]=(uint8_t)(yo>>8);a[1]=(uint8_t)yo;a[2]=(uint8_t)(y1>>8);a[3]=(uint8_t)y1; lcd_cmd(0x2B,a,4);
    PB_OUT &= ~CS; PB_OUT &= ~DC; spi_byte(0x2C); PB_OUT |= DC;
    lb_n = 0;
}
#if LCD_HW
static void lb_flush(void)
{
    if (!lb_n) return;
    lspi_dma(lbuf[lb_i], lb_n * 2u);    /* waits for the previous transfer (the other buffer), then starts this one */
    lb_i ^= 1u; lb_n = 0;
    LCD_YIELD();                        /* (while this buffer goes out) */
}
static inline void lcd_px(uint16_t c)
{
    uint8_t *p = &lbuf[lb_i][lb_n * 2u];
    p[0] = (uint8_t)(c >> 8); p[1] = (uint8_t)c;
    if (++lb_n == LBUF_PX) lb_flush();
}
static inline void lcd_end(void){ lb_flush(); lspi_wait(); PB_OUT |= CS; }
#else
static inline void lcd_px(uint16_t c){ spi_byte((uint8_t)(c>>8)); spi_byte((uint8_t)c); }
static inline void lcd_end(void){ PB_OUT |= CS; }
#endif

#include "ui_text.h"                    /* anti-aliased text (Inter), rounded boxes */

/* one character (scale sx,sy), fg on bg */
static void lcd_char(uint32_t x, uint32_t y, char ch, uint16_t fg, uint16_t bg, uint32_t s)
{
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    const uint8_t *g = FONT8X8[ch - 0x20];
    lcd_win(x, y, 8u*s, 8u*s);
    for (uint32_t row=0; row<8u; row++)
        for (uint32_t ry=0; ry<s; ry++)
            for (uint32_t col=0; col<8u; col++) {
                uint16_t c = (g[row] & (0x80u>>col)) ? fg : bg;
                for (uint32_t rx=0; rx<s; rx++) lcd_px(c);
            }
    lcd_end();
}
static void lcd_text(uint32_t x, uint32_t y, const char *str, uint16_t fg, uint16_t bg, uint32_t s)
{
    while (*str) { lcd_char(x, y, *str++, fg, bg, s); x += 8u*s; }
}
/* ---- preset: the v2 engine's struct (engine/px_dsp.h), edited directly by the UI ---- */
#include "px_dsp.h"
#include "px_nam.h"           /* v37: NAM A2-Lite (the engine includes px_nam.c) */
#include "px_midi.h"                    /* MIDI map and settings (docs/MIDI.md) */
static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;        /* 0..100, follows the MASTER knob (ADC channel 2) */
static void st_get(uint32_t k, px_preset_t *p);   /* px_store.h: the saved preset of slot k, else the factory one */
static uint8_t preset_new;                             /* v40.3: a preset was loaded: the sound follows at once */
static void load_preset(uint32_t i) { st_get(i, &PRESET); preset_new = 1; }

#if AUDIO_PROBE
void fm1_timer5_irq(void) { }
#endif
#if !AUDIO_PROBE
void op_audio_irq(void) { }           /* internal DAC: not used by the Pocket X */
#endif
/* readable crash screen: vector, PC, return address, bus error register — then reboot */
static void hex8(char *b, uint32_t v){ static const char H[]="0123456789ABCDEF";
    for (int i=0;i<8;i++) b[i]=H[(v>>(28-4*i))&15u]; b[8]=0; }
#if OP_BT_AUDIO && DSP_CPU1
/* test build 14: the second core's fault record as it stood at CPU0's own crash (its count, which core took it, when, the
 * PC), for the next boot's LAST START FAILED and the log's last-boot line: in test build 13 CPU0 crashed at the very ms CPU1
 * faulted, and the record went with the RAM. crash: the fm1_crash.count it belongs to */
struct { uint32_t crash, count, cpu, ms, pc; } op_c1x __attribute__((section(".noinit")));
#endif
static void fm1_fault(const fm1_crash_t *c)
{
    char h[9];
#if OP_BT_AUDIO
    lcd_yield_off = 1;                  /* (the screen below must not run the music's decoder: test build 26) */
#endif
#if OP_BT_AUDIO && DSP_CPU1
    op_c1x.crash = c->count; op_c1x.count = fm1_cpu1_fault.count; op_c1x.cpu = fm1_cpu1_fault.cpu;
    op_c1x.ms = fm1_cpu1_fault.ms; op_c1x.pc = fm1_cpu1_fault.pc;
#endif
    lcd_rect(0,0,240,240,RED);
    lcd_text(8,8,"CRASH",WHT,RED,2);
    lcd_text(8,40,"VEC",WHT,RED,1); hex8(h,c->vec);  lcd_text(64,40,h,WHT,RED,1);
    lcd_text(8,56,"PC", WHT,RED,1); hex8(h,c->pc);   lcd_text(64,56,h,WHT,RED,1);
    lcd_text(8,72,"RETS",WHT,RED,1);hex8(h,c->rets); lcd_text(64,72,h,WHT,RED,1);
    lcd_text(8,88,"DBG",WHT,RED,1); hex8(h,c->dbg);  lcd_text(64,88,h,WHT,RED,1);
    lcd_text(8,104,"EMU",WHT,RED,1);hex8(h,c->emu);  lcd_text(64,104,h,WHT,RED,1);
    lcd_text(8,136,"Take a photo :)",WHT,RED,1);
    for (int i=0;i<6;i++){ fm1_wdt_feed(); fm1_delay_ms(1000); }
    fm1_reboot();
}

/* ---- Inputs: rotary encoder (PC0/PC1) + push button (PA2) ----
 * Reverse-engineered from the stock firmware (timer ISR reading PC_IN & 0x3). */
#define PA_IN   (*(volatile uint32_t *)0x50004u)
#define PA_DIR  (*(volatile uint32_t *)0x50008u)
#define PA_DIE  (*(volatile uint32_t *)0x5000Cu)
#define PA_PU   (*(volatile uint32_t *)0x50010u)
#define PB_IN   (*(volatile uint32_t *)0x50044u)
#define PB_DIE  (*(volatile uint32_t *)0x5004Cu)
#define PB_PU   (*(volatile uint32_t *)0x50050u)
#define PC_IN   (*(volatile uint32_t *)0x50084u)
#define PC_DIR  (*(volatile uint32_t *)0x50088u)
#define PC_DIE  (*(volatile uint32_t *)0x5008Cu)
#define PC_PU   (*(volatile uint32_t *)0x50090u)
#define PC_PD   (*(volatile uint32_t *)0x50094u)
/* port H (index 7): declared by Felucca and read by the stock firmware. NO port D on this chip. */
#define PH_IN   (*(volatile uint32_t *)0x501C4u)
#define PH_DIE  (*(volatile uint32_t *)0x501CCu)
#define PH_PU   (*(volatile uint32_t *)0x501D0u)
#define PH_DIR  (*(volatile uint32_t *)0x501C8u)
#define ENC_MASK 0x3u      /* PC0=A, PC1=B */
#ifndef OP_VERSION_NUM                  /* a test channel build (firmware/build.sh, OP_CHANNEL) sets its own number and tag */
#define OP_VERSION_NUM "42"
#endif
#ifndef OP_TAG
#define OP_TAG ""
#endif
#ifndef OP_GUARD_STACK      /* hardware stack-limit guard (fm1_guard.h). build.sh turns it off for the Bluetooth test image, */
#define OP_GUARD_STACK 1    /* whose task stacks (ble_os.c) lie outside [_ustack_lo, _sstack_top]; app.ld checks this at link time */
#endif
#define OP_VERSION "v" OP_VERSION_NUM OP_TAG   /* shown at boot: changes with every released build */
#define PROBE 0            /* 1 = GPIO diagnostic: shows the pins that change at the bottom */
#define ADC_PROBE 0        /* 1 = ADC diagnostic: shows the channels that move at the bottom (C2:512) */
#define LED_PROBE 0        /* 1 = LED map (issue #8, led_probe.h): the knob picks a bit of the lights' 595, it blinks; no sound */
#define PA_PD   (*(volatile uint32_t *)0x50014u)

/* ---- Controls mapped on the real pedal (diagnostic) ----
 * "pressed" = differs from the idle state captured at boot (works whatever the polarity) */
#define PA_FX     (1u<<0)
#define PA_HOME   (1u<<1)
#define PA_FS_UP  (1u<<2)              /* right footswitch ▲ */
#define PA_FS_DN  (1u<<3)              /* left footswitch ▼ */
#define PH_PUSH   ((1u<<4)|(1u<<6))    /* PARA knob push */
#define PH_MOD    ((1u<<7)|(1u<<8))
#define PH_AMP    (1u<<9)
#define PA_KNOWN  (PA_FX|PA_HOME|PA_FS_UP|PA_FS_DN)
#define PH_KNOWN  (PH_PUSH|PH_MOD|PH_AMP)
#define PA_MASTER (1u<<10)             /* MASTER knob = analog pot on PA10 */

static uint32_t base_pa, base_ph;      /* idle state */

static void input_init(void)
{
    PC_DIR |= ENC_MASK; PC_DIE |= ENC_MASK; PC_PU |= ENC_MASK;   /* encoder */
    PA_DIR |= PA_KNOWN; PA_DIE |= PA_KNOWN; PA_PU |= PA_KNOWN;   /* confirmed buttons */
    PH_DIR |= PH_KNOWN; PH_DIE |= PH_KNOWN; PH_PU |= PH_KNOWN;
#if PROBE
    /* unknown pins: digital input + pull-up only (direction left untouched) */
    PA_DIE |= 0xFFFFu; PA_PU |= 0xFFFFu;
    PC_DIE |= 0xFF7Cu; PC_PU |= 0xFF7Cu;                       /* except PC0/1 (encoder), PC7 (screen) */
    PH_DIE |= 0xFFFFu; PH_PU |= 0xFFFFu;
    PB_DIE |= 0xFF1Du; PB_PU |= 0xFF1Du;                       /* except PB1/5/6/7 (screen) */
#endif
    PC_DIR &= ~PC7; PC_OUT |= PC7;      /* PC7, the button lights' enable: high = off until px_leds.h turns them on */
    PC_DIR |= 1u << 9; PC_DIE &= ~(1u << 9); PC_PU &= ~(1u << 9); PC_PD &= ~(1u << 9);   /* PC9: the battery divider (analog) */
}
static void input_baseline(void)
{
    fm1_delay_ms(30);                   /* let the pull-ups settle */
    base_pa = PA_IN & 0xFFFFu; base_ph = PH_IN & 0xFFFFu;
}

#if PROBE
/* 16-pins/port diagnostic: shows the NAME of the pins that change (e.g. "PA12 PH4"). "--" = none. */
static uint32_t probe_base[4], probe_prev[4], probe_t = 0;
static const uint32_t PROBE_MASK[4] = { 0xFFFFu, 0xFF1Du, 0xFF7Cu, 0xFFFFu };
static void probe_capture(void)
{
    probe_base[0]=PA_IN&0xFFFFu; probe_base[1]=PB_IN&0xFFFFu; probe_base[2]=PC_IN&0xFFFFu; probe_base[3]=PH_IN&0xFFFFu;
    for (int i=0;i<4;i++) probe_prev[i]=0xFFFFFFFFu;
}
static void probe_line(uint32_t now)
{
    uint32_t v[4] = { PA_IN&0xFFFFu, PB_IN&0xFFFFu, PC_IN&0xFFFFu, PH_IN&0xFFFFu };
    int same = 1; for (int i=0;i<4;i++) if (v[i]!=probe_prev[i]) same = 0;
    if (same && (now - probe_t) < 400u) return;
    for (int i=0;i<4;i++) probe_prev[i]=v[i];
    probe_t = now;
    char s[40]; int n = 0; static const char PN[4] = {'A','B','C','H'};
    for (int p = 0; p < 4 && n < 33; p++) {
        uint32_t d = (v[p] ^ probe_base[p]) & PROBE_MASK[p];
        for (int b = 0; b < 16 && n < 33; b++)
            if (d & (1u << b)) {
                s[n++]='P'; s[n++]=PN[p];
                if (b >= 10) { s[n++]='1'; s[n++]=(char)('0'+b-10); } else s[n++]=(char)('0'+b);
                s[n++]=' ';
            }
    }
    if (!n) { s[n++]='-'; s[n++]='-'; }
    s[n] = 0;
    lcd_rect(0,224,240,16,UI_PANEL);
    lcd_text(6,228,s,UI_ACC,UI_PANEL,1);
}
#endif

/* ---- Analog inputs (SARADC) ----
 * Felucca's driver (MPL-2.0, same chip). DO NOT call fm1_adc_init():
 * it switches PB1/PB6 to analog, and on the Pocket X those are screen pins. */
#include "fm1_adc.h"
#define ADC_CH_MASTER 2      /* MASTER knob (PA10): 0..1023 */
#define ADC_CH_KEYS   9      /* analog keypad: CAB ~617, DLY ~706, REV ~788 (measured) */
enum { KEY_NONE = 0, KEY_CAB, KEY_DLY, KEY_REV };
static int32_t keys_idle = -1;
static void adc_pins_analog(void)
{   /* PA7/PA8 (possible analog keypad), PA10 (MASTER): analog inputs, no pull */
    uint32_t m = (1u<<7) | (1u<<8) | PA_MASTER;
    PA_DIE &= ~m; PA_PU &= ~m; PA_PD &= ~m; PA_DIR |= m;
}
/* "clean" ADC read: a 1st conversion is discarded (the sampling capacitor still holds
 * the previous channel's voltage — ghosting), then the real one */
static int32_t adc_read2(uint32_t ch)
{
    (void)fm1_adc_read(ch);
    return fm1_adc_read(ch);
}

/* ---- battery (v37): as the stock firmware reads it. On the Pocket X the chip's own VBAT pin is a regulated 3.3 V (it
 * reads 3.31 V charged or not, on USB or not: v36 read it, so the gauge never moved) and its 5 V input is not the USB's.
 * The cell is on PC9 (ADC channel 8) through a 1:2 divider: PocketX_016 (f_02026f60, adc_get_value(8) = f_02025682;
 * BlackBox the same) maps raw 528 and below to 0 %, 636 and above to 100 %, (raw x 100 - 52700) / 109 between, on an
 * average of 300 readings. PC9 is an analog input (input_init). Only the ADC: nothing analog is switched (the PMU test bus
 * readings of the first v37 builds are gone). */
static int bat_mv, bat_usb_mv;         /* last readings (0: none); bat_usb_mv stays 0 (no USB sense found yet) */
static int32_t bat_raw[5] = {-1, -1, -1, -1, -1};   /* the tuner page's raw line: PC9, 0x16A10 bit 16, overruns, clips, peak */
static int bat_pct = -1;               /* the gauge (-1: no reading) */
static int32_t bat_c9q;                /* PC9 smoothed, x4 */
static void bat_read(void)
{
    int32_t c9 = 0, n = 0;
    (void)fm1_adc_read(8u);                             /* (the sampling capacitor still holds the last channel) */
    for (int k = 0; k < 8; k++) { int32_t v = fm1_adc_read(8u); if (v >= 0) { c9 += v; n++; } }
    c9 = n ? c9 / n : -1;
    bat_raw[0] = c9;
    bat_raw[1] = (int32_t)((*(volatile uint32_t *)0x16A10u >> 16) & 1u);   /* (stock reads it beside the gauge: charging?) */
    if (c9 < 100) { bat_mv = 0; bat_pct = -1; bat_c9q = 0; return; }   /* no divider there: no gauge */
    bat_c9q = bat_c9q ? (bat_c9q * 3 + c9 * 4) / 4 : c9 * 4;   /* a little smoothing */
    int r = (int)(bat_c9q / 4);
    bat_pct = r > 635 ? 100 : r < 528 ? 0 : (r * 100 - 52700) / 109;
    bat_mv = r * 6600 / 1023;                           /* the cell, roughly (3.3 V reference, 1:2): for the tuner page */
}
#define KEY_DEBUG 0          /* 1 = shows the raw value of channel 9 at the bottom right (K####) */
/* identifies the analog keypad button by the nearest value (±42 window),
 * independent of the idle state */
static int keys_decode(int32_t v)
{
    static const int32_t REF[3] = { 617, 706, 788 };   /* CAB, DLY, REV */
    if (v < 0) return KEY_NONE;
    int best = -1; int32_t bd = 100000;
    for (int i = 0; i < 3; i++) { int32_t e = v - REF[i]; if (e < 0) e = -e; if (e < bd) { bd = e; best = i; } }
    return (bd <= 42) ? best + 1 : KEY_NONE;
}
#if OP_BT_AUDIO
static int keys_held_at_boot;           /* the analog button held while the pedal started: 1 CAB, 2 DLY, 3 REV, 0 none */
/* test build 28: the key that turns the central role off for a boot (no scanning for the footswitch, LE as in test build 27),
 * and the one that leaves out the rest of the stock power setup (up to test build 27: DLY; 0: none, the setup always done) */
#define BT_CEN_OFF_KEY 2                /* DLY */
#define BT_PWR_KEY 0
#endif
static void keys_capture_idle(void)
{
    fm1_delay_ms(20);
    keys_idle = fm1_adc_read(ADC_CH_KEYS);
    /* if a button was held at boot (value close to a button's), assume idle is 1023 */
    static const int32_t REF[3] = { 617, 706, 788 };
    for (int i = 0; i < 3 && keys_idle >= 0; i++) {
        int32_t e = keys_idle - REF[i]; if (e < 0) e = -e;
#if OP_BT_AUDIO
        if (e <= 45) { keys_idle = 1023; keys_held_at_boot = i + 1; break; }   /* (the Bluetooth audio image's boot keys) */
#else
        if (e <= 45) { keys_idle = 1023; break; }
#endif
    }
}
#if ADC_PROBE
#define ADC_NCH 16
static int32_t adc_base[ADC_NCH];
static uint32_t adc_t = 0;
static char adc_last[40];
static int adc_skip(uint32_t ch) { return ch == 3u || ch == 4u; }   /* PB1/PB6 = screen */
static void adc_capture(void)
{
    fm1_delay_ms(20);
    for (uint32_t ch = 0; ch < ADC_NCH; ch++) adc_base[ch] = adc_skip(ch) ? -1 : fm1_adc_read(ch);
    adc_last[0] = 0;
}
static void adc_line(uint32_t now)
{
    if (now - adc_t < 150u) return;
    adc_t = now;
    char s[40]; int n = 0;
    for (uint32_t ch = 0; ch < ADC_NCH && n <= 20; ch++) {
        if (adc_skip(ch) || adc_base[ch] < 0) continue;
        int32_t v = fm1_adc_read(ch);
        if (v < 0) continue;
        int32_t d = v - adc_base[ch]; if (d < 0) d = -d;
        if (d > 60) {
            s[n++]='C';
            if (ch >= 10u) { s[n++]='1'; s[n++]=(char)('0'+ch-10u); } else s[n++]=(char)('0'+ch);
            s[n++]=':';
            s[n++]=(char)('0'+(v/1000)%10); s[n++]=(char)('0'+(v/100)%10);
            s[n++]=(char)('0'+(v/10)%10);   s[n++]=(char)('0'+v%10);
            s[n++]=' ';
        }
    }
    if (!n) { s[n++]='-'; s[n++]='-'; }
    s[n] = 0;
    static uint32_t drawn_t = 0;
    int same = 1; for (int i = 0; i <= n; i++) if (s[i] != adc_last[i]) { same = 0; break; }
    if (same && (now - drawn_t) < 1000u) return;   /* redraw on change, or every second */
    drawn_t = now;
    for (int i = 0; i <= n; i++) adc_last[i] = s[i];
    lcd_rect(0,224,240,16,UI_PANEL);
    lcd_text(6,228,s,UI_ACC,UI_PANEL,1);
}
#endif

/* generic debounce: returns 1 on the press edge */
typedef struct { uint8_t stab, last, cnt; } dbn_t;
static int dbn_press(dbn_t *d, int raw)
{
    if (raw == d->last) { if (d->cnt < 5u) d->cnt++; }
    else { d->cnt = 0; d->last = (uint8_t)raw; }
    if (d->cnt >= 3u && raw != d->stab) { d->stab = (uint8_t)raw; return raw; }
    return 0;
}
/* ===== boot diagnostics (v23b) : survive a reset in .noinit, shown at the next boot =====
 * stage = last boot step reached; blocks/load/peak/nans/c1/loops = audio state seen by the main loop. */
#define OPD_MAGIC 0x4F504442u
enum { ST_NONE, ST_INPUTS, ST_PRESET, ST_ENG_INIT, ST_ENG_SET, ST_I2S, ST_UI, ST_RUN, ST_OK, ST_N };
static const char *const ST_NAME[ST_N] = {"?", "INPUTS", "PRESET", "ENGINE INIT", "ENGINE SET", "I2S START",
                                          "UI DRAW", "FIRST 6 S", "OK"};
volatile struct { uint32_t magic, stage, blocks, load, peak, nans, c1, loops, crash_seen, safe; } op_diag
    __attribute__((section(".noinit")));
static struct { uint8_t vdc0, vdc1, sys0, sys1, dcdc, rst, pinr; } op_pwr;   /* supplies, reset source, pin reset (fm1_cstart) */
static void op_stage(uint32_t s, const char *txt)
{
    op_diag.stage = s;
    fm1_wdt_feed();
    (void)txt;
}

/* standard quadrature table: index = (old<<2)|new -> -1/0/+1 */
static const int8_t QDEC[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
#if AUDIO_PROBE
#include "audio_probe.h"
#else
#include "px_cacheram.h"     /* v37: data-cache ways as RAM (melodee's routine): the NAM network's vector-unit buffers */
#include "px_audio.h"
#endif

#if !AUDIO_PROBE
#if PX_OWN_FX   /* SYN PAD (the Bluetooth audio image's own effect): the chord caught and let go on the engine, from ui.h's left
                 * footswitch and px_midi_io.h's CC 64 (the main loop: px_dsp.c only reads the two counters) */
#define PX_PAD_PRESS() px_pad_press(&g_eng)
#define PX_PAD_STOP() px_pad_stop(&g_eng)
#define PX_EXP_SET(v) px_exp_set((int)(v))   /* test build 25: px_midi_io.h's CC 117, the expression pedal */
#endif
#if OP_BT_AUDIO     /* test build 26: ui.h's waits on a message ("SAVED", 0.7 s) serve the radio, as the main loop does */
void ble_test_poll(void);
#define UI_IDLE() ble_test_poll()
#define UI_MS() ms_now()
static void bt_power_apply(void);   /* test build 40: SETTINGS' BT POWER, applied by the Bluetooth task (bt/bt_a2dp.c) */
#define PX_BT_POWER_APPLY() bt_power_apply()
#endif
#include "ui.h"
#include "px_lights.h"      /* the button lights follow the blocks (issue #8; px_leds.h drives them) */
#define PX_LIGHTS_OFF() lights_off()
#include "px_usb.h"         /* USB-MIDI + update entry (web installer) */
#include "px_store.h"       /* the user's presets in flash */
#include "px_models.h"      /* v37: the NAM user models in flash */
#include "px_irs.h"         /* v38: the user cabinet IRs in flash */
#if !AUDIO_PROBE
static uint32_t ed_load(void) { return a_bypass ? a_peak : a_load; }   /* PING: the DSP load, as at the bottom right */
static uint32_t ed_flags(void) { return (a_bypass ? 1u : 0u) | (a_c1 ? 2u : 0u); }
/* v40.1: the stacks' deepest use since boot. fm1_cstart paints them; the bytes at the bottom never written since are free */
#define OP_PAINT 0x5AA5C33Cu
extern char _ustack_top[];
static uint32_t stack_free(const char *lo, const char *hi)
{
    const uint32_t *p = (const uint32_t *)(const void *)lo;
    while ((const char *)p < hi && *p == OP_PAINT) p++;
    return (uint32_t)((const char *)p - lo);
}
#if OP_BLE_TEST
uint32_t ble_heap_min_free(void);
uint32_t ble_stack_min_free(void);
#endif
static uint32_t ed_stat(uint32_t k)                    /* PING, after the flags */
{
    return k == 0u ? a_peak : k == 1u ? a_peak_all : k == 2u ? a_xrun : k == 3u ? a_clip
         : k == 4u ? stack_free(_ustack_lo, _ustack_top) : k == 5u ? stack_free(_sstack_lo, _sstack_top)
#if OP_BLE_TEST
         : k == 6u ? ble_heap_min_free() : ble_stack_min_free();
#else
         : 0u;
#endif
}
/* v40.2: PING's profile (14 bits each): each block's and the NAM network's share of an audio block's time over the last
 * second (%, a_pf), the network's way of running (pxn_mode), then from its start-up timing (0.1 % of real time, the
 * network alone): each way of running (px_nam_tuned), and where one core's time goes (pxn_prof_ticks); then the layer
 * split: 0 off, else 1 + the last layer CPU0 runs; then with it CPU0's and CPU1's load and their peaks since the preset
 * was set (%); v40.3: then the last full change: from the change to the sound back (0.1 ms, 0 while it fades in), the
 * engine's set in it (0.1 ms), how long the last LOADING was shown (ms) and how many were since boot */
#define ED_PROF (PX_NBLK + 2u + PXN_MODES + 9u + 5u + 4u)
static uint32_t ed_prof(uint32_t k)
{
    if (k <= PX_NBLK) return a_pf[k];
    if (k == PX_NBLK + 1u) return (uint32_t)pxn_mode();
    k -= PX_NBLK + 2u;
    if (k == PXN_MODES + 9u) return g_eng.nam_lp ? 1u + (uint32_t)px_nam_lp_split_used() : 0u;   /* v40.2 (v40.4: the last block's) */
    if (k > PXN_MODES + 9u && k <= PXN_MODES + 13u) {  /* each core's load and peak (%), the layer split only */
        const uint32_t j = k - PXN_MODES - 10u;
        return g_eng.nam_lp ? (j < 2u ? a_cl[j] : a_cpk[j - 2u]) : 0u;
    }
    if (k >= PXN_MODES + 14u) {                        /* v40.3: the last full change (0.1 ms) and LOADING (ms, count) */
        const uint32_t d = FM1_TICKS_PER_US * 100u;
        const uint32_t v = k == PXN_MODES + 14u ? (a_swin ? 0u : (a_sw1 - a_swc) / d) : k == PXN_MODES + 15u ? a_swset / d
                         : k == PXN_MODES + 16u ? a_ldms : a_ldn;
        return v > 16383u ? 16383u : v;
    }
    const uint32_t t = k < PXN_MODES ? px_nam_tuned((int)k) : pxn_prof_ticks((int)(k - PXN_MODES));
    return t * 10u / (A_BUDGET * 2u / 100u);           /* (ticks for 256 frames: two audio blocks) */
}
static void nm_changed(uint32_t s)   /* v37: a user model was written or erased: the engine reloads it if it plays it */
{
    if (g_eng.nam_idx == (int)(PX_NAM_NFACT + s)) g_eng.nam_idx = -1;
    a_full = 1;                                        /* v38: the same knobs, another network: a full apply */
    audio_apply();
}
static void ir_changed(uint32_t s)   /* v38: a user IR was written or erased: the engine reloads it if it plays it */
{
    if (g_eng.ir_slot == (int)s) g_eng.ir_slot = -1;
    a_full = 1;
    audio_apply();
}
#if OP_BLE_TEST             /* the log channel (firmware/ble/ble_diag.h): only this image answers DIAG, the normal firmware does not change */
#define ED_STATS 8u         /* PING also gives the Bluetooth heap's and tasks' lowest free bytes */
#if OP_BT_AUDIO
#define ED_DIAG_LINES 12u   /* (the Bluetooth audio image: the HCI conversation, the music's FIFO, the interrupts' times
                             * against the audio's, the second core, test build 19 its registers at a fault and its revivals,
                             * bt/bt_a2dp.c; test build 28 the LE links and the editor, test build 33 the central role on a
                             * line of its own, bt/bt_le_midi.c; tools/log.mjs reads them by position) */
#define ED_OUT_BYTES 2754u  /* (its twelve lines of up to 223 characters: 12 x 225 and the reply's head; px_editor.h's is 1,024;
                             * test build 33: 2,529 for eleven) */
#else
#define ED_DIAG_LINES 5u
#endif
#define ED_DIAG_LEN 224u
static void ed_diag_line(uint32_t i, char *out);
#endif
#if OP_BT_AUDIO
#define ED_BT 1             /* test build 27: the web editor over Bluetooth MIDI too (px_editor.h, bt/bt_le_midi.c) */
#define ED_BT_LINKS 2u      /* (two LE links at once: bt/bt_le_links.h) */
#endif
#include "px_editor.h"      /* the web editor (docs/EDITOR-PROTOCOL.md) */
#include "px_midi_io.h"     /* musical MIDI over USB (docs/MIDI.md) */
#endif
#endif
#if LED_PROBE
#if AUDIO_PROBE
#error "LED_PROBE needs the normal build: USB (the web installer) is the way back"
#endif
#include "led_probe.h"      /* the LED map (issue #8) */
#endif
#if OP_BLE_TEST             /* the Bluetooth MIDI test image (docs/BLE-TEST.md): make ble */
#include "ble/ble_os.c"
#include "ble/ble_libc.c"
#if OP_BT_AUDIO             /* the Bluetooth audio test image (docs/BT-AUDIO-TEST.md): make bt. The same image, the classic stack */
#include "bt/bt_cfg.c"
#include "ble/ble_rf.c"
#include "bt/bt_stubs.c"
#include "bt/bt_glue.c"
#include "bt/bt_a2dp.c"
#include "bt/bt_irq.c"           /* the classic baseband's interrupt handlers (bt/bt_isr.S enters them) */
#include "bt/bt_le_midi.c"       /* test build 23: BLE MIDI beside the music (the LE controller and host linked too) */
#else
#include "ble/ble_cfg.c"
#include "ble/ble_rf.c"
#include "ble/ble_stubs.c"
#include "ble/ble_test.c"
#include "ble/ble_midi.c"
#endif
#include "ble/ble_diag.h"
#if OP_BT_AUDIO
#define BT_OFF_BY_KEY bt_off_by_key      /* (bt/bt_a2dp.c: REV held at power-on) */
#else
#define BT_OFF_BY_KEY 0
#endif
static void ed_diag_line(uint32_t i, char *out)
{
    if (i == 0u) ble_boot_text(out, ms_now(), op_diag.safe, op_diag.stage, op_pwr.rst, guard.failed);
    else if (i == 1u) ble_last_text(out, &ble_last);
    else if (i == 2u) ble_test_line(out);
    else if (i == 3u) ble_midi_line(out);
#if OP_BT_AUDIO
    else if (i == 5u) bt_hci_line(out);
    else if (i == 6u) bt_fifo_line(out);  /* (test build 13; build 12: the phone's packets at the controller; 9 to 11: the first
                                           * HCI messages since boot) */
    else if (i == 7u) bt_isr_line(out);
    else if (i == 8u) bt_c1_line(out);
    else if (i == 9u) bt_c1r_line(out);   /* (test build 19) */
    else if (i == 10u) bt_le_line(out);   /* (test build 28: the LE links, the editor; up to 32 the central role too) */
    else if (i == 11u) bt_le_cen_line(out);   /* (test build 33: the central role for the footswitch) */
#endif
    else ble_trace_line(out);
}
#endif

/* P3_RST_SRC as read at boot: why the chip last reset (fm1_sys.h) */
static void rst_names(char *l, uint32_t r)
{
    static const char *const N[7] = {"POWER-ON", "VDDIO LOW", "WATCHDOG", "VCM", "LONG PRESS", "1.2V", "SOFT"};
    int any = 0;
    for (int i = 0; i < 7; i++)
        if (r & (1u << i)) { if (any) b_str(l, "+"); b_str(l, N[i]); any = 1; }
    if (!any) b_str(l, "NONE");
}

static void app_main(void)
{
    uint32_t boots_ok = 0;
    int32_t accum = 0;
    uint8_t enc_last;
    enum { B_PUSH, B_FSUP, B_FSDN, B_FX, B_MOD, B_AMP, B_CAB, B_DLY, B_REV, NBTN };
    dbn_t btn[NBTN] = {{0,0,0}};
    dbn_t bhome = {0,0,0};
    uint32_t adc_tick = 0, t_knob = 0, t_tuner = 0;
    int32_t master_raw = -100;
#if OP_BT_AUDIO
    /* test build 18: the stock core supplies first, before anything else runs. M-VAVE's V22 (2026-11-12) now sets them in its
     * boot loader (isd_config.ini DVDD=15, DCDC14=7): VDC14 1.40 -> 1.60 V, then SYSVDD 1.26 -> 1.38 V with its strong drive, a
     * level per 100 us (fm1_sys.h; the timer runs since fm1_cstart). Test builds 15 to 17 raised them only after the second core
     * had started; the rest of the stock power setup follows the power-on keys (below) */
    fm1_vdc14_raise(7u);
    fm1_p33_or(FM1_P3_ANA_CON6, 0x08u);
    fm1_p33_or(FM1_P3_ANA_CON9, 0x10u);
    fm1_sysvdd_raise(15u);
#endif
#if !AUDIO_PROBE
    {   /* before the second core starts: the data-cache ways as RAM (through a pointer: it is RAM code) */
        void (*volatile f)(void) = pxc_init;
        f();
    }
#endif
#if DSP_CPU1
#if OP_BT_AUDIO
    /* each Bluetooth test build its own id: test build 4's crash barred the second core with the release's id, 402, and every
     * test build after it passed 402 again, so the bar stayed (NOINIT survives a flash) and builds 5 to 10 most likely ran the
     * whole DSP on one core, until a real power-off */
    fm1_cpu1_bar_build(0x42540000u + BT_TEST_BUILD);
#else
    fm1_cpu1_bar_build(403u);            /* a new build gives the 2nd core another chance (v40.4: its job and the blocks' loops changed) */
#endif
    fm1_cpu1_start();                    /* at boot, before audio and interrupts (Felucca) */
#endif
    lcd_init();
    /* boot screen with the version: shows which image is running */
    lcd_rect(0,0,240,240,UI_BG);
    ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
#if OP_BT_AUDIO
    ft_box(0, 126, 240, OP_VERSION " BT AUDIO TEST " BT_TEST_BUILD_TEXT, &FT_SM, UI_DIM, UI_BG, 1);   /* (2: the stall record; 3: the baseband's window, bt/app-bt.ld;
                                                                                       * 4: its interrupts above the audio's, ble/ble_os.c; 5: their own entry, bt/bt_irq.c;
                                                                                       * 6: the radio pools' header at the pool, bt/bt_lbuf.h;
                                                                                       * 7: the HCI events in the log, bt/bt_a2dp.c;
                                                                                       * 8: whole task messages, ble/ble_os.c;
                                                                                       * 9: the HCI conversation from the queues;
                                                                                       * 10: recursive mutexes, ble/ble_os.c;
                                                                                * 11: the pairing's four walls, the second core's bar,
                                                                                *     the clock, the waits in an interrupt, the isr line;
                                                                                * 12: the phone's packets from the controller's hook into
                                                                                *     a FIFO, two output slots, the second core's record;
                                                                                * 13: the reverb's loop cut at its block's end and counted,
                                                                                *     the registers on the LAST BOOT FAILED report;
                                                                                * 14: on OpenPocket v42: the callbacks' order by its
                                                                                *     px_aorder.h, the layer split's worker, the folded
                                                                                *     controller library, firmware/build.sh) */
#elif OP_BLE_TEST
    ft_box(0, 126, 240, OP_VERSION " BLE TEST", &FT_SM, UI_DIM, UI_BG, 1);
#else
    ft_box(0, 126, 240, OP_VERSION, &FT_SM, UI_DIM, UI_BG, 1);
#endif
#if !AUDIO_PROBE
    ft_box(0, 150, 240, "STARTING", &FT_SM, UI_TXT, UI_BG, 1);   /* v42: a plain word first, the numbers small below */
    {   /* the supplies as found (fm1_cstart) and why the chip last reset */
        char l[32]; l[0] = 0;
        b_str(l, "PWR "); b_num(l, op_pwr.vdc0); b_str(l, " "); b_num(l, op_pwr.sys0); b_str(l, " (BOOT LEVELS)");
        ft_box(0, 196, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
        char h[9]; l[0] = 0;
        b_str(l, "RST "); rst_names(l, op_pwr.rst); b_str(l, " PINR "); hex8(h, op_pwr.pinr); b_str(l, h + 6);
        ft_box(0, 214, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
    }
#endif
    for (int i = 0; i < 6; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
    /* --- report of the previous boot, if it did not finish (or crashed) --- */
    int safe = 0;
    {
        uint32_t cc = (fm1_crash.magic == FM1_CRASH_MAGIC) ? fm1_crash.count : 0u;
        if (op_diag.magic != OPD_MAGIC) {
            op_diag.magic = OPD_MAGIC; op_diag.stage = ST_OK; op_diag.crash_seen = 0; op_diag.safe = 0;   /* a crash left by an older build is shown once */
        }
        int crashed = cc != op_diag.crash_seen;
        uint32_t st = op_diag.stage < ST_N ? op_diag.stage : 0u;
        if ((st != ST_OK || crashed) && !op_diag.safe) {
            char l[48], h[9];
            safe = 1;                    /* this boot: DSP bypassed, so the pedal stays usable */
            lcd_rect(0, 0, 240, 240, UI_BG);   /* v42: what happened and what to do first, the numbers for a bug report below */
            ft_box(0, 6, 240, "LAST START FAILED", &FT_MD, RED, UI_BG, 1);
            ft_box(0, 34, 240, "THE EFFECTS ARE OFF FOR THIS START", &FT_SM, UI_TXT, UI_BG, 1);
            ft_box(0, 52, 240, "SWITCH OFF AND ON TO TRY AGAIN", &FT_SM, UI_TXT, UI_BG, 1);
            l[0] = 0; b_str(l, "STOPPED AT: "); b_str(l, ST_NAME[st]);
            ft_box(0, 78, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            if (!crashed) {
                l[0] = 0; b_str(l, "RESET BY: "); rst_names(l, op_pwr.rst);
                ft_box(0, 96, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            }
            if (crashed) {
                l[0] = 0; b_str(l, "CRASH  PC "); hex8(h, fm1_crash.pc); b_str(l, h);
                ft_box(0, 96, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
                l[0] = 0; b_str(l, "RETS "); hex8(h, fm1_crash.rets); b_str(l, h); b_str(l, "  VEC "); b_num(l, fm1_crash.vec);
                ft_box(0, 114, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
#if OP_BT_AUDIO
                {   /* test build 13: the registers at the crash, in place of the DSP code's address. fm1_crash_ctx's slot is this
                     * crash's when its count is (fm1_irq.h fm1_fault_c, at every crash of CPU0's); else a dash (not expected: CPU1's
                     * faults retire it, FM1_CPU1_FALLBACK). In build 12's reverb loop (rv_block) R1 held its end n as reloaded, R6
                     * k + 1, R12 the address it wrote + 4 (build 13's code may use others: rn, after the CI listing listings/px_dsp.s) */
                    const fm1_crash_ctx_t *x = &fm1_crash_ctx[fm1_crash.count % FM1_CRASH_CTX_N];
                    const int ok = x->count == fm1_crash.count;
                    static const char nm[4][6] = { "R1 ", "  R6 ", "R12 ", "  SP " };   /* (a loop: the smallest code) */
                    static const uint8_t rn[3] = { 1, 6, 12 };
                    char *o = l;
                    for (int i = 0; i < 4; i++) {
                        o = ble__put(o, nm[i]);
                        if (ok) o = bt__hex(o, i < 3 ? x->r[rn[i]] : fm1_crash.sp, 8); else *o++ = '-';
                        if (i & 1) { *o = 0; ft_box(0, i == 1 ? 132 : 150, 240, l, &FT_SM, UI_DIM, UI_BG, 1); o = l; }
                    }
                }
#define LBF_Y(y) ((y) + 18)   /* (the lines below: one line further down) */
#else
                l[0] = 0; b_str(l, "DSP CODE AT "); hex8(h, (uint32_t)(uintptr_t)px_block_label); b_str(l, h);
                ft_box(0, 132, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
#define LBF_Y(y) y          /* (the release and BLE images: v42's lines) */
#endif
                l[0] = 0; b_str(l, "UPTIME "); b_num(l, fm1_crash.uptime_ms); b_str(l, " MS  DBG "); hex8(h, fm1_crash.dbg); b_str(l, h);
                ft_box(0, LBF_Y(150), 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            }
            l[0] = 0; b_str(l, "BLOCKS "); b_num(l, op_diag.blocks); b_str(l, "  LOAD "); b_num(l, op_diag.load);
            b_str(l, "%  PEAK "); b_num(l, op_diag.peak); b_str(l, "%");
            ft_box(0, LBF_Y(168), 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            l[0] = 0; b_str(l, "NAN "); b_num(l, op_diag.nans); b_str(l, "  CPU1 "); b_num(l, op_diag.c1);
            b_str(l, "  LOOPS "); b_num(l, op_diag.loops);
            ft_box(0, LBF_Y(186), 240, l, &FT_SM, UI_DIM, UI_BG, 1);
#if OP_BT_AUDIO             /* (test build 13's two register lines: the photo line one line further down, the screen's last) */
#if DSP_CPU1
            if (crashed && op_c1x.crash == fm1_crash.count) {   /* test build 14: the second core's record at CPU0's crash, in the
                                                                  * photo line's place: "C1 F count/cpu ms PC pc" (fm1_fault) */
                char *o = ble__put(l, "C1 F ");
                o = bt__dec(o, op_c1x.count); *o++ = '/'; o = bt__dec(o, op_c1x.cpu); *o++ = ' ';
                o = bt__dec(o, op_c1x.ms); o = ble__put(o, " MS PC "); o = bt__hex(o, op_c1x.pc, 8); *o = 0;
                ft_box(0, 222, 240, l, &FT_SM, UI_ACC, UI_BG, 1);
            } else
#endif
            ft_box(0, 222, 240, "PHOTO THIS FOR A BUG REPORT", &FT_SM, UI_ACC, UI_BG, 1);
#else
            ft_box(0, 212, 240, "PHOTO THIS FOR A BUG REPORT", &FT_SM, UI_ACC, UI_BG, 1);
#endif
            for (int i = 0; i < 120; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }   /* 12 s */
            lcd_rect(0, 0, 240, 240, UI_BG);
            ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
            ft_box(0, 126, 240, OP_VERSION " SAFE MODE", &FT_SM, UI_DIM, UI_BG, 1);
        }
#if OP_BLE_TEST
        ble_last = (ble_boot_t){ st != ST_OK || crashed, crashed, st, op_diag.blocks, op_diag.load, op_diag.peak, op_diag.nans, op_diag.c1,
                                 op_diag.loops, fm1_crash.vec, fm1_crash.pc, fm1_crash.rets, fm1_crash.dbg, fm1_crash.emu, fm1_crash.uptime_ms };
#if OP_BT_AUDIO && DSP_CPU1
        if (crashed && op_c1x.crash == fm1_crash.count) {   /* (test build 14: the second core's record at that crash) */
            ble_last.c1x = 1; ble_last.c1n = op_c1x.count; ble_last.c1cpu = op_c1x.cpu;
            ble_last.c1ms = op_c1x.ms; ble_last.c1pc = op_c1x.pc;
        }
#endif
#endif
        op_diag.crash_seen = cc; op_diag.safe = (uint32_t)safe;
        op_diag.blocks = op_diag.load = op_diag.peak = op_diag.nans = op_diag.c1 = op_diag.loops = 0;
    }
    op_stage(ST_INPUTS, "INPUTS...");
    input_init();
    adc_pins_analog();
    input_baseline();
    keys_capture_idle();
#if OP_BT_AUDIO
    /* the keys held at power-on (test build 18): CAB = one core (the second held before the audio starts: builds 5-10 ran so,
     * barred, with no start-up crash; the stage's way out if two cores fail); DLY = without the rest of the stock power setup.
     * (Test builds 16-17's DLY, the radio's interrupts below the audio as the stock firmware has them, is gone: test build 3 ran
     * so and could not pair, OpenPocket's audio interrupt being the whole DSP block, not stock's DMA work; ble/ble_os.c) */
#if DSP_CPU1
    if (keys_held_at_boot == 1 && !safe) { fm1_cpu1_stop(); fm1_cpu1_on = 0; }
#endif
    /* test build 18: the rest of M-VAVE's power setup (fm1_sys.h fm1_power_stock, from its firmware's __hw_power_init): the 1.3 V
     * supply moved onto its DC-DC and tuned, VDDIOM 4 (V22's isd_config.ini VDDIO=4), the radio's 1.2 V regulator on automatic
     * (on an FM-1 without them the radio heard far less). The second core idles in its RAM loop here, no audio yet. DLY held:
     * not done, as up to test build 17 (on a Pocket X the release's v30 slowed and crackled with the DC-DC: the A/B by ear) */
    if (!BT_PWR_KEY || keys_held_at_boot != BT_PWR_KEY) bt_pwr_stock = (uint8_t)(1 + fm1_power_stock());   /* (test build 28:
                                                                                           * no key leaves it out, BT_PWR_KEY) */
#endif
    op_stage(ST_PRESET, "PRESET...");
    load_preset(cur_preset);
#if LED_PROBE
    px_flash_boot();
    px_store_boot();
    px_usb_start();                      /* USB-MIDI + update entry: the web installer is the way back */
    fm1_irq_enable_all();                /* audio_start does it in the normal build; here no audio is started */
    led_probe_run();                     /* never returns */
#endif
#if AUDIO_PROBE
    audio_probe_run();
#else
    a_hold = 1;                          /* DSP bypassed until the UI is up and 1.5 s have passed */
    px_flash_boot();                     /* the flash's size; a staging area an update left */
    px_store_boot();                     /* the saved presets and the settings; start on the last one saved */
    px_usb_start();                      /* USB-MIDI device + update entry (web installer); v42: USB audio when set */
    nm_scan();                           /* v37: the NAM user models (names, levels) */
    ir_scan();                           /* v38: the user cabinet IRs (names) */
#if DSP_CPU1
    px_nam_helper(a_hrun, a_hwait);      /* v37: CPU1 can take a part of the NAM network */
#endif
#if OP_BLE_TEST
    {   /* the vector unit's inputs: what the cache RAM has left after the tuner's buffers (app.ld .cram) */
        extern char _cram_hi[];
        uint32_t lo = (uint32_t)(uintptr_t)_cram_hi;
        if (pxc_status == PXC_READY && lo < PXC_END) px_nam_fast((void *)(uintptr_t)lo, PXC_END - lo);
        if (pxc_status == PXC_READY && lo + PXN_FAST_BYTES + PX_NAM_LP_BYTES <= PXC_END)   /* v40.2: then the layer split's */
            px_nam_lp_mem((void *)(uintptr_t)(lo + PXN_FAST_BYTES), PX_NAM_LP_BYTES);
    }
#else
    if (pxc_status == PXC_READY) {
        px_nam_fast((void *)PXC_BEGIN, PXC_END - PXC_BEGIN);   /* the vector unit's inputs */
        px_nam_lp_mem((void *)(PXC_BEGIN + PXN_FAST_BYTES), PX_NAM_LP_BYTES);   /* v40.2: then the layer split's queue */
    }
#endif
    /* v37: the NAM routines by hand checked, its fastest way to run chosen. v40.2: not after a failed boot (safe mode,
     * the DSP off anyway): a hand routine that faults in its check then costs one boot, not every one, and the pedal
     * comes up with USB-MIDI for the installer's rescue (it ran C until the next boot) */
#if OP_BT_AUDIO
    if (!safe && keys_held_at_boot != 3) bt_flash_reads();   /* Bluetooth's flash reads, before the audio: no CPU1 job yet */
#endif
    if (!safe) px_nam_check(b_ticks);
    /* v40.5: from here (after the start-up check), CPU0's waits for CPU1 inside the network end as fm1_cpu1_wait's
     * do (px_audio.h dsp_lp_run: retired, LATE) */
    px_nam_lp_clock(b_ticks, FM1_CPU1_TIMEOUT_US * FM1_TICKS_PER_US);
    audio_start();                       /* guitar -> DSP engine -> output, amp enabled */
    leds_init();                         /* the button lights: all off loaded, then on (PC7 low), as stock */
    op_stage(ST_UI, "SCREEN...");
    lcd_rect(0, 0, 240, 240, UI_BG);
    ui_invalidate();
    ui_render();
    op_stage(ST_RUN, 0);
#endif
#if OP_BLE_TEST
    ble_os_init();                       /* the task table, before the main loop polls it (ble_os.c: it lives in NOINIT) */
#if OP_BT_AUDIO
    /* the Bluetooth audio image's power-on keys (the log's c1 line shows KEY, whichever was held). Test build 14: none held, the
     * DSP waits for Bluetooth's start-up (bt/bt_a2dp.c bt_dsp_may_start); DLY, the DSP at 1.5 s as in test build 13 (the
     * 1.96 s window kept measurable); CAB, the same and the stock core supplies (above, after keys_capture_idle); REV: */
    if (keys_held_at_boot == 3) bt_off_by_key = 1;   /* REV held at power-on: this boot without Bluetooth (an A/B by ear) */
    if (keys_held_at_boot == BT_CEN_OFF_KEY) le_cen_off = 1;   /* test build 28, DLY: no central role (bt/bt_le_midi.c) */
#if BT_LE_ROLES != 31
    le_cen_off = 1;                     /* (test build 29: the LE roles of 27, so no central role at all: bt/bt_cfg.c BT_LE_ROLES) */
#endif
    bt_canary_arm();                     /* (test build 14: the pattern after the Bluetooth heap, checked by ble_test_poll) */
#endif
    if (!safe && !BT_OFF_BY_KEY)         /* after a failed boot Bluetooth stays off: the pedal runs, and can be re-flashed */
        ble_test_start();                /* radio on, controller and host up, advertising (the pedal keeps working) */
#endif
    enc_last = (uint8_t)(PC_IN & ENC_MASK);
    for (;;) {
        uint32_t now = ms_now();
        fm1_wdt_feed();
#if OP_BLE_TEST
        ble_test_poll();
#endif
#if !AUDIO_PROBE
        px_usb_service(now);             /* web installer: handshake, update, UBOOT soft key */
        px_midi_service();               /* USB-MIDI: Program / Control Change in, the pedal's own changes out */
        lights_update(&PRESET, ui_mode == M_TUNER, now);   /* the button lights follow the blocks */
        if (ui_mode != M_TUNER) {        /* DSP load in the status corner: "DSP 42%", "PEAK 150%" (bypassed), "SAFE 42%" */
            static uint32_t t_cpu;
            static uint8_t ld_shown;
            if (now - t_cpu >= 500u || a_ld != ld_shown) {   /* (LOADING on and off at once) */
                t_cpu = now; ld_shown = a_ld;
                mid_c1_off = a_c1 ? 0u : (fm1_cpu1_gave_up ? fm1_cpu1_gave_up : 0xFFu);   /* v41: why the 2nd core stopped (MIDI page) */
                char c[16];
                int late = a_xrun && a_blocks - a_xrun_at < 700u;   /* a block ran late in the last 2 s: "DSP 88%!" in red */
                uint32_t ld = a_bypass ? a_peak : a_load;
                ui_dg.load = ld; ui_dg.peak = a_peak; ui_dg.cl[0] = a_cl[0]; ui_dg.cl[1] = a_cl[1];   /* (v42: the diagnostics page) */
                ui_dg.cores = (uint8_t)(a_c1 ? (px_nam_dual(&g_eng) ? 2u : 1u) : 0u);
                ui_dg.split = (uint8_t)(g_eng.nam_lp != 0); ui_dg.guard = (uint8_t)(a_c1 ? 93u : 85u);   /* (px_audio.h's dsp_load limits) */
                ui_dg.late = (uint8_t)late; ui_dg.bypass = a_bypass;
                ui_load_text(c, ld, safe, a_bypass, late);
                int kind = ui_load_kind(ld, safe, a_bypass, late, a_ld);   /* v40.3: LOADING a NAM model; v42: amber when high */
                bat_raw[2] = (int32_t)a_xrun; bat_raw[3] = (int32_t)a_clip; bat_raw[4] = (int32_t)a_peak_all;   /* diagnostics page */
#if OP_BLE_TEST
                {   /* "STARTING 4 C1 42%": the Bluetooth state first; FAILED and ADVERTISING scroll their long lines instead (ble_status.h) */
                    char b[48], full[224]; int k, j;   /* (ED_DIAG_LEN: the Bluetooth audio image's lines are longer) */
                    static uint32_t scroll;
                    ble_test_state(b);
                    if (a_bypass || late) {  /* (test build 11: the DSP's text, red, for 2 s after a late block, as the release shows it) */
                        for (j = 0; c[j]; j++) b[j] = c[j];
                        b[j] = 0;
                    } else if (ble_state == BLE_FAILED || ble_state == BLE_ADV) {   /* longer than the status area: scroll it (ble_status.h) */
                        if (ble_conn) ble_midi_line(full); else ble_test_line(full);
                        ble_marquee(full, scroll++, 11, b);
                    } else {
                        for (k = 0; b[k]; k++) ;
                        b[k++] = ' ';
                        for (j = 0; c[j]; j++) b[k++] = c[j];
                        b[k] = 0;
                    }
                    ui_status(b, kind);
                }
#else
                ui_status(c, kind);
#endif
            }
        }
        if ((ui_mode == M_TUNER || ui_mode == M_DIAG) && now - t_tuner >= 100u) {
            t_tuner = now;
            if (ui_mode == M_TUNER) tuner_draw(tuner_estimate()); else diag_draw();
        }
#endif
#if OP_BT_AUDIO && !AUDIO_PROBE
        /* test build 14: the DSP may start up to 8 s after power-on (the hold, below). The boot guard (two starts in a row that
         * do not get this far: UBOOT, fm1_cstart) is cleared at 6 s as before, so the longer wait adds no way to UBOOT; the
         * stage OK (else the next start is safe mode) waits for 4.5 s of DSP as before, so a pedal that resets under load
         * still starts in safe mode next, and a crash there does too (its record) */
        if (!boots_ok && now > 6000u) { boots_ok = 1; guard.pending = 0; guard.failed = 0; }
        if (boots_ok == 1u && (safe || (bt_dsp_ms && now - bt_dsp_ms >= 4500u))) { boots_ok = 2; op_diag.stage = ST_OK; }
#else
        if (!boots_ok && now > 6000u) {   /* 4.5 s of DSP first: a pedal that resets under load is caught */ boots_ok = 1; guard.pending = 0; guard.failed = 0; op_diag.stage = ST_OK; }
#endif
#if !AUDIO_PROBE
#if OP_BT_AUDIO
        if (a_hold && !safe && now > 1500u && bt_dsp_may_start(now)) { a_hold = 0; bt_dsp_ms = now; }   /* DSP on (test build 14:
                                                                         * once Bluetooth is up, bt/bt_a2dp.c; D in the c1 line) */
#else
        if (a_hold && !safe && now > 1500u) a_hold = 0;   /* DSP on */
#endif
        {
            static uint32_t t_diag;
            op_diag.loops++;
            if (now - t_diag >= 100u) {
                t_diag = now;
                op_diag.blocks = a_blocks; op_diag.load = a_load; op_diag.peak = a_peak;
                op_diag.nans = a_nan_count; op_diag.c1 = a_c1;
            }
        }
#endif

        /* --- encoder (direction inverted: subtract); fast rotation = steps of 5 --- */
        uint8_t s = (uint8_t)(PC_IN & ENC_MASK);
        if (s != enc_last) {
            accum -= QDEC[(enc_last << 2) | s];
            enc_last = s;
            int dd = 0;
            if (accum >= 4) { accum -= 4; dd = 1; } else if (accum <= -4) { accum += 4; dd = -1; }
            if (dd) {
                int fast = (now - t_knob) < 45u; t_knob = now;
                ui_turn(dd, fast);
            }
        }
        /* --- buttons (pressed = differs from the idle state) --- */
        uint32_t pa = (PA_IN & 0xFFFFu) ^ base_pa, ph = (PH_IN & 0xFFFFu) ^ base_ph;
        /* debounced: what is held. ui_buttons (ui.h) times the gestures: PUSH, HOME and FX act at a short release or after a
         * hold, the others at the press */
        dbn_press(&btn[B_PUSH], (ph & PH_PUSH) != 0);
        dbn_press(&btn[B_FSUP], (pa & PA_FS_UP) != 0);
        dbn_press(&btn[B_FSDN], (pa & PA_FS_DN) != 0);
        dbn_press(&btn[B_FX], (pa & PA_FX) != 0);
        dbn_press(&btn[B_MOD], (ph & PH_MOD) != 0);
        dbn_press(&btn[B_AMP], (ph & PH_AMP) != 0);
        dbn_press(&bhome, (pa & PA_HOME) != 0);

        int32_t mv = -1;
        if (now - adc_tick >= 10u) {     /* analog keypad CAB/DLY/REV + MASTER knob, every 10 ms */
            adc_tick = now;
            {   /* battery: every 2 s (v36) */
                static uint32_t t_bat;
                if (!t_bat || now - t_bat >= 2000u) { t_bat = now ? now : 1u; bat_read(); }
            }
            int k = keys_decode(adc_read2(ADC_CH_KEYS));
            dbn_press(&btn[B_CAB], k == KEY_CAB);
            dbn_press(&btn[B_DLY], k == KEY_DLY);
            dbn_press(&btn[B_REV], k == KEY_REV);
            mv = adc_read2(ADC_CH_MASTER);
        }
#if !AUDIO_PROBE
        ui_buttons(now, (uint32_t)(btn[B_FSUP].stab != 0) << UB_FSUP | (uint32_t)(btn[B_FSDN].stab != 0) << UB_FSDN |
                        (uint32_t)(btn[B_PUSH].stab != 0) << UB_PUSH | (uint32_t)(bhome.stab != 0) << UB_HOME |
                        (uint32_t)(btn[B_FX].stab != 0) << UB_FX | (uint32_t)(btn[B_MOD].stab != 0) << UB_MOD |
                        (uint32_t)(btn[B_AMP].stab != 0) << UB_AMP | (uint32_t)(btn[B_CAB].stab != 0) << UB_CAB |
                        (uint32_t)(btn[B_DLY].stab != 0) << UB_DLY | (uint32_t)(btn[B_REV].stab != 0) << UB_REV);
#endif
        if (mv >= 0) {
            /* smoothing (x16 moving average, ~160 ms) then hysteresis of 2 on the 0..100 volume */
            master_raw = (master_raw < 0) ? mv * 16 : master_raw + mv - master_raw / 16;
            int32_t avg = master_raw / 16;
            uint32_t nv;
            if (avg <= 12) nv = 0u;
            else if (avg >= 1010) nv = 100u;
            else nv = (uint32_t)(avg - 12) * 100u / (1010u - 12u);
            int32_t dv = (int32_t)nv - (int32_t)master_vol; if (dv < 0) dv = -dv;
            if (dv >= 2 || (nv == 0u && master_vol != 0u) || (nv == 100u && master_vol != 100u)) master_vol = nv;
        }
#if !AUDIO_PROBE
        audio_poll(now, preset_new);     /* the sound follows the UI (preset, settings, volume), before the screen redraws */
        preset_new = 0;
        ui_render();                     /* redraws only what changed */
#endif
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    uint32_t vdc0, sys0, dcdc, rst0, pinr0;
    {   /* v40.1: paint the stacks (interrupts are still off), for their deepest use (ed_stat) */
        volatile uint32_t here;
        for (d = (uint32_t *)(void *)_ustack_lo; (char *)d < (char *)&here - 256; d++) *d = OP_PAINT;
        for (d = (uint32_t *)(void *)_sstack_lo; (char *)d < _sstack_top - 256; d++) *d = OP_PAINT;
    }
    fm1_time_init();
    /* v31, issue #1: the stock firmware's first act is P3_PINR_CON = 0, the long-press reset off (BlackBox_020
     * f_0200698a). Left on, a pin held at its active level resets the chip every 4 s (the SDK offers 4 or 8 s):
     * Megal's BlackBox reset every 4.3 s after a power cycle, never after an install (the always-on P33 kept the
     * stock firmware's 0 across the warm reset). Read first, so the boot screen can show it. */
    rst0 = fm1_p33_read(FM1_P3_RST_SRC);
    pinr0 = fm1_p33_read(0x94u);
    fm1_p33_write(0x94u, 0);
    fm1_wdt_arm(0x0D);
    /* v33: the supplies stay as the boot loader leaves them (VDC14 3, SYSVDD 11, LDO), as up to v29. v30 set them as
     * the stock firmware does (VDC14 7, SYSVDD 15, the 1.3 V supply on the DC-DC): it was not what fixed issue #1
     * (the long-press reset was), and with it a Pocket X ran its DSP ~25 % slower (DOOM SLUDGE 61 % -> 77 %) and
     * crackled. Only read, for the boot screen. */
    vdc0 = fm1_vdc14();
    sys0 = fm1_sysvdd();
    dcdc = 0;
    fm1_wdt_feed();
    if (guard.magic != 0x48575431u || guard.pending > 1u || guard.failed > 1u) {
        guard.magic = 0x48575431u; guard.failed = 0; guard.pending = 0;
    }
    if (guard.pending) guard.failed++;
    guard.pending = 1;
    if (guard.failed >= 2u) { guard.failed = 0; guard.pending = 0; fm1_enter_uboot(); }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _bss_hi_start; d < _bss_hi_end; d++) *d = 0;
    for (d = _pool_tail_start; d < _pool_tail_end; d++) *d = 0;
    for (d = _bss_lo_start; d < _bss_lo_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
#if OP_BT_AUDIO   /* test build 17: the firmware's own data, above the radio's window (bt/app-bt.ld .data_hi) */
    {
        extern uint32_t _data_hi_start[], _data_hi_end[], _data_hi_load[];
        for (s = _data_hi_load, d = _data_hi_start; d < _data_hi_end; s++, d++) *d = *s;
    }
#endif
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    op_pwr.vdc0 = (uint8_t)vdc0; op_pwr.vdc1 = (uint8_t)fm1_vdc14();
    op_pwr.sys0 = (uint8_t)sys0; op_pwr.sys1 = (uint8_t)fm1_sysvdd(); op_pwr.dcdc = (uint8_t)dcdc;
    op_pwr.rst = (uint8_t)rst0; op_pwr.pinr = (uint8_t)pinr0;
    fm1_guard_enable((OP_GUARD_STACK ? FM1_GUARD_STACK : 0) | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;) ;
}
