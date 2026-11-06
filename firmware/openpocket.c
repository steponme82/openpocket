/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — Milestone 1 (boot + screen) — Donner Pocket X.
 * SCREEN (reverse-engineered from the stock firmware): SOFTWARE SPI on PORT B
 *   CS = PB1, D/C = PB5, CLK = PB6, DO = PB7   (+ PC7 driven, reset/backlight?)
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
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#if DSP_CPU1
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
extern uint32_t _rt_start[], _rt_end[], _rt_load[];
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
    PC_OUT |= PC7;                     /* PC7 high (reset released / backlight?) */
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
static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;        /* 0..100, follows the MASTER knob (ADC channel 2) */
static void st_get(uint32_t k, px_preset_t *p);   /* px_store.h: the saved preset of slot k, else the factory one */
static void load_preset(uint32_t i) { st_get(i, &PRESET); }

#if AUDIO_PROBE
void fm1_timer5_irq(void) { }
#endif
#if !AUDIO_PROBE
void op_audio_irq(void) { }           /* internal DAC: not used by the Pocket X */
#endif
/* readable crash screen: vector, PC, return address, bus error register — then reboot */
static void hex8(char *b, uint32_t v){ static const char H[]="0123456789ABCDEF";
    for (int i=0;i<8;i++) b[i]=H[(v>>(28-4*i))&15u]; b[8]=0; }
static void fm1_fault(const fm1_crash_t *c)
{
    char h[9];
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
/* port H (index 7): declared by Felucca and read by the stock firmware. NO port D on this chip. */
#define PH_IN   (*(volatile uint32_t *)0x501C4u)
#define PH_DIE  (*(volatile uint32_t *)0x501CCu)
#define PH_PU   (*(volatile uint32_t *)0x501D0u)
#define PH_DIR  (*(volatile uint32_t *)0x501C8u)
#define ENC_MASK 0x3u      /* PC0=A, PC1=B */
#define OP_VERSION_NUM "36"
#define OP_VERSION "v" OP_VERSION_NUM   /* shown at boot: changes with every released build */
#define PROBE 0            /* 1 = GPIO diagnostic: shows the pins that change at the bottom */
#define ADC_PROBE 0        /* 1 = ADC diagnostic: shows the channels that move at the bottom (C2:512) */
#define LED_PROBE 1        /* 1 = LED hunt (issue #8, led_probe.h): stock I/O power, 595 roles, LED lines; no sound */
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
    PC_DIR &= ~PC7; PC_OUT |= PC7;      /* PC7 drives the screen: always an output at 1 */
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

/* ---- battery (v36): the chip's own VBAT / 4 channel through the PMU test bus, scaled by the 0.8 V bandgap
 * read the same way (JieLi AC79 SDK, cpu/wl82/key/adc_api.c): mV = 4 x 800 x vbat / vbg. P3_ANA_CON4: bit 0
 * PMU_DET_EN, bits 3:1 the PMU channel (0 VBG, 5 VBAT/4, 6 LDO5V/4); WLA_CON0 bits 17:15 = 4: the PMU on the ADC's
 * channel 15, bit 14: the bandgap path. Everything is put back as it was after each reading. */
#define P3_ANA_CON4 0x04u
static int bat_mv, bat_usb_mv;         /* last readings (0: none) */
static int32_t pmu_conv(uint32_t sel)
{
    uint32_t t, v = 0;
    for (int k = 0; k < 3; k++) {                       /* 2 conversions to settle the mux, the 3rd counts */
        FM1_ADC_CON_R = 0;
        if (sel == 0u) FM1_WLA_CON0 |= 1u << 14; else FM1_WLA_CON0 &= ~(1u << 14);
        FM1_ADC_CON_R = (FM1_ADC_CON_SDK & 0xF00Fu) | 0x40u | (15u << 8);
        FM1_ADC_CON_R |= 0x10u;
        FM1_ADC_CON_R |= 0x40u;
        for (t = 0; t < 20000u && !fm1_adc_done(); t++) ;
        if (t == 20000u) { fm1_adc_abort(); return -1; }
        v = fm1_adc_take();
    }
    return (int32_t)v;
}
static void bat_read(void)
{
    uint8_t c4 = fm1_p33_read(P3_ANA_CON4);
    uint32_t w = FM1_WLA_CON0;
    FM1_WLA_CON0 = (w & ~(7u << 15)) | (4u << 15);
    fm1_p33_write(P3_ANA_CON4, (uint8_t)((c4 & 0xF0u) | 1u | (0u << 1)));
    int32_t vbg = pmu_conv(0u);
    fm1_p33_write(P3_ANA_CON4, (uint8_t)((c4 & 0xF0u) | 1u | (5u << 1)));
    int32_t vb = pmu_conv(5u);
    fm1_p33_write(P3_ANA_CON4, (uint8_t)((c4 & 0xF0u) | 1u | (6u << 1)));
    int32_t v5 = pmu_conv(6u);
    fm1_p33_write(P3_ANA_CON4, c4);
    FM1_WLA_CON0 = w;
    if (vbg < 100 || vbg > 900 || vb < 0) { bat_mv = 0; return; }
    int mv = (int)(3200 * vb / vbg), mu = v5 < 0 ? 0 : (int)(3200 * v5 / vbg);
    bat_mv = bat_mv ? (bat_mv * 3 + mv) / 4 : mv;      /* a little smoothing */
    bat_usb_mv = mu;
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
static void keys_capture_idle(void)
{
    fm1_delay_ms(20);
    keys_idle = fm1_adc_read(ADC_CH_KEYS);
    /* if a button was held at boot (value close to a button's), assume idle is 1023 */
    static const int32_t REF[3] = { 617, 706, 788 };
    for (int i = 0; i < 3 && keys_idle >= 0; i++) {
        int32_t e = keys_idle - REF[i]; if (e < 0) e = -e;
        if (e <= 45) { keys_idle = 1023; break; }
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
#include "px_audio.h"
#endif

#if !AUDIO_PROBE
#include "ui.h"
#include "px_usb.h"         /* USB-MIDI + update entry (web installer) */
#include "px_store.h"       /* the user's presets in flash */
#if !AUDIO_PROBE
static uint32_t ed_load(void) { return a_bypass ? a_peak : a_load; }   /* PING: the DSP load, as at the bottom right */
static uint32_t ed_flags(void) { return (a_bypass ? 1u : 0u) | (a_c1 ? 2u : 0u); }
#include "px_editor.h"      /* the web editor (docs/EDITOR-PROTOCOL.md) */
#endif
#endif
#if LED_PROBE
#if AUDIO_PROBE
#error "LED_PROBE needs the normal build: USB (the web installer) is the way back"
#endif
#include "led_probe.h"      /* the LED hunt (issue #8) */
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
    uint32_t adc_tick = 0, home_t = 0, t_knob = 0, t_tuner = 0;
    int home_down = 0, home_long = 0, push_down = 0, push_long = 0;
    uint32_t push_t = 0, clk_t = 0;
    int clk_pend = 0;                    /* chain page: a PUSH waiting to see if a 2nd one makes it a double-click */
    int32_t master_raw = -100;
#if DSP_CPU1
    fm1_cpu1_bar_build(27u);             /* a new build gives the 2nd core another chance */
    fm1_cpu1_start();                    /* at boot, before audio and interrupts (Felucca) */
#endif
    lcd_init();
    /* boot screen with the version: shows which image is running */
    lcd_rect(0,0,240,240,UI_BG);
    ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
    ft_box(0, 126, 240, OP_VERSION, &FT_SM, UI_DIM, UI_BG, 1);
#if !AUDIO_PROBE
    {   /* the supplies as found -> as set (fm1_cstart): "PWR 3>7 11>15 DCDC" */
        char l[32]; l[0] = 0;
        b_str(l, "PWR "); b_num(l, op_pwr.vdc0); b_str(l, " "); b_num(l, op_pwr.sys0); b_str(l, " (BOOT LEVELS)");
        ft_box(0, 150, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
        char h[9]; l[0] = 0;
        b_str(l, "RST "); rst_names(l, op_pwr.rst); b_str(l, " PINR "); hex8(h, op_pwr.pinr); b_str(l, h + 6);
        ft_box(0, 170, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
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
            lcd_rect(0, 0, 240, 240, UI_BG);
            ft_box(0, 6, 240, "LAST BOOT FAILED", &FT_MD, RED, UI_BG, 1);
            l[0] = 0; b_str(l, "STOPPED AT: "); b_str(l, ST_NAME[st]);
            ft_box(0, 40, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            if (!crashed) {
                l[0] = 0; b_str(l, "RESET BY: "); rst_names(l, op_pwr.rst);
                ft_box(0, 62, 240, l, &FT_SM, UI_ACC, UI_BG, 1);
            }
            if (crashed) {
                l[0] = 0; b_str(l, "CRASH  PC "); hex8(h, fm1_crash.pc); b_str(l, h);
                ft_box(0, 62, 240, l, &FT_SM, UI_ACC, UI_BG, 1);
                l[0] = 0; b_str(l, "RETS "); hex8(h, fm1_crash.rets); b_str(l, h); b_str(l, "  VEC "); b_num(l, fm1_crash.vec);
                ft_box(0, 80, 240, l, &FT_SM, UI_ACC, UI_BG, 1);
                l[0] = 0; b_str(l, "DSP CODE AT "); hex8(h, (uint32_t)(uintptr_t)px_block_label); b_str(l, h);
                ft_box(0, 98, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
                l[0] = 0; b_str(l, "UPTIME "); b_num(l, fm1_crash.uptime_ms); b_str(l, " MS  DBG "); hex8(h, fm1_crash.dbg); b_str(l, h);
                ft_box(0, 116, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            }
            l[0] = 0; b_str(l, "BLOCKS "); b_num(l, op_diag.blocks); b_str(l, "  LOAD "); b_num(l, op_diag.load);
            b_str(l, "%  PEAK "); b_num(l, op_diag.peak); b_str(l, "%");
            ft_box(0, 140, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            l[0] = 0; b_str(l, "NAN "); b_num(l, op_diag.nans); b_str(l, "  CPU1 "); b_num(l, op_diag.c1);
            b_str(l, "  LOOPS "); b_num(l, op_diag.loops);
            ft_box(0, 158, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            ft_box(0, 184, 240, "PLEASE TAKE A PHOTO", &FT_MD, UI_ACC, UI_BG, 1);
            ft_box(0, 212, 240, "SAFE MODE: DSP OFF THIS BOOT", &FT_SM, UI_DIM, UI_BG, 1);
            for (int i = 0; i < 120; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }   /* 12 s */
            lcd_rect(0, 0, 240, 240, UI_BG);
            ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
            ft_box(0, 126, 240, OP_VERSION " SAFE MODE", &FT_SM, UI_DIM, UI_BG, 1);
        }
        op_diag.crash_seen = cc; op_diag.safe = (uint32_t)safe;
        op_diag.blocks = op_diag.load = op_diag.peak = op_diag.nans = op_diag.c1 = op_diag.loops = 0;
    }
    op_stage(ST_INPUTS, "INPUTS...");
    input_init();
    adc_pins_analog();
    input_baseline();
    keys_capture_idle();
    op_stage(ST_PRESET, "PRESET...");
    load_preset(cur_preset);
#if LED_PROBE
    px_usb_start();                      /* USB-MIDI + update entry: the web installer is the way back */
    px_store_boot();
    fm1_irq_enable_all();                /* audio_start does it in the normal build; here no audio is started */
    led_probe_run();                     /* never returns */
#endif
#if AUDIO_PROBE
    audio_probe_run();
#else
    a_hold = 1;                          /* DSP bypassed until the UI is up and 1.5 s have passed */
    px_usb_start();                      /* USB-MIDI device + update entry (web installer) */
    px_store_boot();                     /* the saved presets; start on the last one saved */
    audio_start();                       /* guitar -> DSP engine -> output, amp enabled */
    op_stage(ST_UI, "SCREEN...");
    lcd_rect(0, 0, 240, 240, UI_BG);
    ui_invalidate();
    ui_render();
    op_stage(ST_RUN, 0);
#endif
    enc_last = (uint8_t)(PC_IN & ENC_MASK);
    for (;;) {
        uint32_t now = ms_now();
        fm1_wdt_feed();
#if !AUDIO_PROBE
        px_usb_service(now);             /* web installer: handshake, update, UBOOT soft key */
        audio_poll(now);                 /* the sound follows the UI (preset, settings, volume) */
        if (ui_mode != M_TUNER) {        /* DSP load at the bottom right: "C1 42%" (2nd core), "C0" or BYP */
            static uint32_t t_cpu;
            if (now - t_cpu >= 500u) {
                t_cpu = now;
                char c[10];
                uint32_t l = a_bypass ? a_peak : a_load; int i = 0;
                if (l > 999u) l = 999u;
                if (safe) { c[i++]='S'; c[i++]='A'; c[i++]='F'; c[i++]='E'; c[i++]=' '; }
                else if (a_bypass) { c[i++]='B'; c[i++]='Y'; c[i++]='P'; c[i++]=' '; }
                else { c[i++]='C'; c[i++]=(char)(a_c1 ? '1' : '0'); c[i++]=' '; }
                if (l >= 100u) c[i++] = (char)('0' + l / 100u);
                if (l >= 10u) c[i++] = (char)('0' + (l / 10u) % 10u);
                c[i++] = (char)('0' + l % 10u); c[i++] = '%'; c[i] = 0;
                ui_status(c, a_bypass);
            }
        } else if (now - t_tuner >= 100u) { t_tuner = now; tuner_draw(tuner_estimate()); }
#endif
        if (!boots_ok && now > 6000u) {   /* 4.5 s of DSP first: a pedal that resets under load is caught */ boots_ok = 1; guard.pending = 0; guard.failed = 0; op_diag.stage = ST_OK; }
#if !AUDIO_PROBE
        if (a_hold && !safe && now > 1500u) a_hold = 0;   /* DSP on */
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

        int any = 0;                     /* a control was used (also used to exit the tuner) */
        /* --- encoder (direction inverted: subtract); fast rotation = steps of 5 --- */
        uint8_t s = (uint8_t)(PC_IN & ENC_MASK);
        if (s != enc_last) {
            accum -= QDEC[(enc_last << 2) | s];
            enc_last = s;
            int dd = 0;
            if (accum >= 4) { accum -= 4; dd = 1; } else if (accum <= -4) { accum += 4; dd = -1; }
            if (dd) {
                int fast = (now - t_knob) < 45u; t_knob = now;
                if (ui_mode == M_SAVE) save_knob(dd); else if (ui_mode != M_TUNER) ui_knob(dd, fast);
                any = 1;
            }
        }
        /* --- buttons (pressed = differs from the idle state) --- */
        uint32_t pa = (PA_IN & 0xFFFFu) ^ base_pa, ph = (PH_IN & 0xFFFFu) ^ base_ph;
        /* PUSH: short press = usual action (on release), long press (0.8 s) = SAVE */
        dbn_press(&btn[B_PUSH], (ph & PH_PUSH) != 0);
        int push_now = btn[B_PUSH].stab != 0, p_push = 0, p_save = 0;
        if (push_now && !push_down) { push_down = 1; push_long = 0; push_t = now; }
        if (push_now && push_down && !push_long && now - push_t >= 800u) { push_long = 1; p_save = 1; }
        if (!push_now && push_down) { push_down = 0; if (!push_long) p_push = 1; }
        int p_up = dbn_press(&btn[B_FSUP], (pa & PA_FS_UP) != 0);
        int p_dn = dbn_press(&btn[B_FSDN], (pa & PA_FS_DN) != 0);
        int p_fx = dbn_press(&btn[B_FX], (pa & PA_FX) != 0);
        int p_mod = dbn_press(&btn[B_MOD], (ph & PH_MOD) != 0);
        int p_amp = dbn_press(&btn[B_AMP], (ph & PH_AMP) != 0);
        /* HOME: short press = back to the chain, long press (0.7 s) = tuner */
        dbn_press(&bhome, (pa & PA_HOME) != 0);
        int home_now = bhome.stab != 0, p_home = 0, p_tuner = 0;
        if (home_now && !home_down) { home_down = 1; home_long = 0; home_t = now; }
        if (home_now && home_down && !home_long && now - home_t >= 700u) { home_long = 1; p_tuner = 1; }
        if (!home_now && home_down) { home_down = 0; if (!home_long) p_home = 1; }

        int p_cab = 0, p_dly = 0, p_rev = 0;
        int32_t mv = -1;
        if (now - adc_tick >= 10u) {     /* analog keypad CAB/DLY/REV + MASTER knob, every 10 ms */
            adc_tick = now;
            {   /* battery: every 2 s (v36) */
                static uint32_t t_bat;
                if (!t_bat || now - t_bat >= 2000u) { t_bat = now ? now : 1u; bat_read(); }
            }
            int k = keys_decode(adc_read2(ADC_CH_KEYS));
            p_cab = dbn_press(&btn[B_CAB], k == KEY_CAB);
            p_dly = dbn_press(&btn[B_DLY], k == KEY_DLY);
            p_rev = dbn_press(&btn[B_REV], k == KEY_REV);
            mv = adc_read2(ADC_CH_MASTER);
        }
#if !AUDIO_PROBE
        if (ui_mode == M_SAVE) {
            if (p_push) save_confirm();
            else if (p_home) save_close();
        } else if (ui_mode == M_TUNER) {
            if (p_push || p_save || p_up || p_dn || p_fx || p_mod || p_amp || p_cab || p_dly || p_rev || p_home) tuner_close();
        } else if (p_save) {
            save_open();
        } else if (ui_mode == M_MOVE) {  /* moving a block: PUSH drops it, HOME puts it back */
            clk_pend = 0;
            if (p_push) ui_move_drop();
            else if (p_home) ui_move_cancel();
        } else {
            /* chain page: double-click PUSH = move the selected block; a single click opens it (after 350 ms) */
            if (p_push && ui_mode == M_CHAIN) {
                if (clk_pend && now - clk_t < 350u) { clk_pend = 0; ui_move_start(); }
                else { clk_pend = 1; clk_t = now; }
                p_push = 0;
            }
            if (clk_pend && (now - clk_t >= 350u || ui_mode != M_CHAIN)) {
                clk_pend = 0;
                if (ui_mode == M_CHAIN) ui_push();
            }
            if (ui_mode == M_MOVE) { }
            else if (p_tuner) tuner_open();
            else {
                if (p_push) ui_push();
                if (p_up) ui_preset_step(+1);
                if (p_dn) ui_preset_step(-1);
                if (p_home) ui_home();
                if (p_fx) ui_fx_cycle();
                if (p_mod) ui_enter_block(PX_B_MOD);
                if (p_amp) ui_enter_block(PX_B_AMP);
                if (p_cab) ui_enter_block(PX_B_CAB);
                if (p_dly) ui_enter_block(PX_B_DELAY);
                if (p_rev) ui_enter_block(PX_B_REVERB);
            }
        }
        (void)any;
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
        ui_render();                     /* redraws only what changed */
#endif
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    uint32_t vdc0, sys0, dcdc, rst0, pinr0;
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
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    op_pwr.vdc0 = (uint8_t)vdc0; op_pwr.vdc1 = (uint8_t)fm1_vdc14();
    op_pwr.sys0 = (uint8_t)sys0; op_pwr.sys1 = (uint8_t)fm1_sysvdd(); op_pwr.dcdc = (uint8_t)dcdc;
    op_pwr.rst = (uint8_t)rst0; op_pwr.pinr = (uint8_t)pinr0;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;) ;
}
