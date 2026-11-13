/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha on the M-VAVE FM-1: felucca-bsp's boards/fm1, behind the board calls lorcha.c makes (board.h).
 *   14 buttons, 27 note keys (F3..G5), 7 encoders, the MASTER pot and the battery, 41 LEDs (one per key),
 *   the ST7789 panel, ALNK0 audio out on port C. */
#pragma once

#define LB_NAME "FM-1"
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_960"            /* the identity the installer sees; == the package's (build.sh --product) */
#endif
#define LB_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)   /* the FM-1's flash parts */
/* the update's staging: chip/ota.c's FM-1 default (0xE0000, 5 x 4 KiB); the only flash Lorcha writes */
#define FL_RANGE_OK(off, n) FL_IN(off, n, 0x000E0000u, 0x000E5000u)

#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_jack.h"
#include "fm1_uart.h"

/* ---- the controls ---- */
#define LB_NBTN 14u
static const char *const LB_BTN_NAME[LB_NBTN] = {"OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT",
                                                 "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
enum { LB_B_OCT_DN = 0, LB_B_OCT_UP = 1, LB_B_PAGE = 8, LB_B_NOTE = 9, LB_B_TONE = 12, LB_B_WALK = 13,
       LB_B_VOL_DN = -1, LB_B_VOL_UP = -1, LB_B_WAVE = 3 };
#define LB_NKEY 27u
#define LB_KEY_NOTE0 53u                  /* F3 */
#define LB_NENC 7u
static const char *const LB_ENC_NAME[LB_NENC] = {"SEL", "ALG", "K1", "K2", "K3", "K4", "PRE"};
#define LB_ENC_CUTOFF 0u                 /* the encoder that also sets the filter: SELECT */
#define LB_NAN 2u
static const char *const LB_AN_NAME[LB_NAN] = {"MASTER", "BATT"};
#define LB_AN_MASTER 0
#define LB_NPAD 0u
#define LB_NLED FM1_NKEY                  /* 0..13 the buttons', 14..40 the keys' */
#define LB_LED_KEY0 14u

static int32_t lb__an[LB_NAN];

/* the TRS MIDI IN (UART1 on PH8, its DMA into a ring) */
#define LB_HAS_TRS 1
static volatile uint8_t lb__trs_ring[64] __attribute__((aligned(16)));
static uint32_t lb__trs_r;

static void lb_init(void)                 /* IRQs off, before TIMER5 */
{
    fm1_input_init();
    fm1_adc_init();
    fm1_uart1_midi_init(lb__trs_ring, sizeof lb__trs_ring);
}
/* the bytes that came in since the last call, into out (at most max; an overrun keeps the newest) */
static uint32_t lb_trs_take(uint8_t *out, uint32_t max)
{
    uint32_t n = fm1_uart1_rx_take(), i;
    if (n > sizeof lb__trs_ring) {
        lb__trs_r += n - (uint32_t)sizeof lb__trs_ring;
        n = sizeof lb__trs_ring;
    }
    if (n > max)
        n = max;
    for (i = 0; i < n; i++)
        out[i] = lb__trs_ring[lb__trs_r++ % sizeof lb__trs_ring];
    return n;
}
static void lb_tick(void) { fm1_input_tick(); }     /* TIMER5, 10 kHz */
static uint32_t lb_buttons(void) { return fm1_in.buttons; }
static uint64_t lb_keys(void) { return fm1_in.notes; }
static uint32_t lb_key_vel(uint32_t k) { (void)k; return 100u; }   /* (velocity from the keys is off by default) */
static int32_t lb_enc_take(uint32_t e) { return fm1_enc_take(e); }
static uint32_t lb_pads(void) { return 0; }
static uint32_t lb_pad_vel(uint32_t p) { (void)p; return 0; }
static void lb_analog_poll(void)          /* main loop, every 40 ms: polled conversions, a few hundred us */
{
    lb__an[0] = fm1_adc_read(FM1_ADC_MASTER);
    lb__an[1] = fm1_adc_read(FM1_ADC_BATT);
}
static int32_t lb_analog(uint32_t i) { return i < LB_NAN ? lb__an[i] : -1; }
static uint32_t lb_jack(void) { return fm1_hp_in(); }
#define LB_HAS_JACK 1

/* LEDs: level 0..64; the FM-1's are on or off (at half and over) */
static uint8_t lb__led[LB_NLED];
static void lb_led(uint32_t i, uint32_t level)
{
    if (i < LB_NLED)
        lb__led[i] = (uint8_t)level;
}
static void lb_pad_rgb(uint32_t p, uint32_t rgb) { (void)p; (void)rgb; }
static void lb_leds_flush(void)
{
    uint32_t i;
    for (i = 0; i < LB_NLED; i++)
        fm1_led_key(i, lb__led[i] >= 32u);
}

/* ---- the panel: felucca-bsp's wiring, Lorcha's own init (panel.h) ---- */
#define LB_LCD_SETTLE_MS 50u
static void lb_lcd_hw_init(void) { fm1_lcd_hw_init(); }
static void lb_lcd_cmd_raw(uint8_t c) { fm1_lcd_send_cmd(c); (void)fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_data_raw(const void *p, uint32_t n) { fm1_lcd_send_data(p, n); (void)fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_tune(void (*cmd)(uint8_t, const uint8_t *, uint32_t)) { (void)cmd; }
#define LB_LCD_MADCTL 0x00u
static void lb_lcd_after_init(void) { fm1_lcd_baud(4); fm1_lcd_backlight(1); }
static uint32_t lb_lcd_timeouts(void) { return fm1_lcd_timeouts; }

/* ---- audio: stereo Q23, 44,117.6 Hz ---- */
#define LB_AUDIO_HALF_FRAMES 64u
static int32_t lb__abuf[2u * 2u * LB_AUDIO_HALF_FRAMES] __attribute__((aligned(4)));
static void lb_render(int32_t *out, uint32_t frames);    /* lorcha.c */
static volatile uint32_t lb_audio_irqs;
void fm1_alnk0_irq(void)
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        lb_render(&lb__abuf[fm1_audio_free_half() * 2u * LB_AUDIO_HALF_FRAMES], LB_AUDIO_HALF_FRAMES);
        fm1_audio_ack_half();
        lb_audio_irqs++;
    }
}
extern void isr_alnk0(void);
static int lb_audio_start(void)           /* IRQs off; 0 or felucca-bsp's FM1_AUDIO_ERR_* */
{
    return fm1_audio_init_checked(lb__abuf, sizeof lb__abuf / sizeof lb__abuf[0], 2u * LB_AUDIO_HALF_FRAMES,
                                  FM1_AUDIO_RATE, isr_alnk0, 3);
}
