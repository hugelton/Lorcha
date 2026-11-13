/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha on the M-VAVE Pocket AMP: felucca-bsp's boards/pocketamp, behind the board calls lorcha.c makes (board.h).
 *   6 buttons (the knob's push, HOME, VOL-, VOL+, LEFT, RIGHT), the knob's encoder (PC0 / PC1), no keys, no known LEDs,
 *   the 240 x 240 panel turned a quarter, I2S out and in on port A (the input's level is shown as an analog channel).
 * The flash: the app area [0x4000, 0xA5000), VM [0xA5000, 0xBD000); the update is staged at the top of VM (0xB8000, its record
 * at 0xBCF00), with the stock loader's name in the record. */
#pragma once

#define LB_NAME "Pocket AMP"
#ifndef FELUCCA_ID
#define FELUCCA_ID "Pocket AMP_960"
#endif
#define LB_JEDEC_OK(id) (((id) & 0xFFu) == 0x14u)     /* a 1 MiB part (the units' parts are not listed yet) */
#define OTA_AREA 0x000B8000u
#define OTA_AREA_LEN 0x5000u
#define OTA_REC_NAME "ota-Pocket AMP_105"
#define FL_RANGE_OK(off, n) FL_IN(off, n, 0x000B8000u, 0x000BD000u)

#include "fm1_time.h"
#include "pa_input.h"
#include "pa_lcd_hw.h"
#include "pa_lcd_init.h"
#include "pa_audio.h"

/* ---- the controls ---- */
#define LB_NBTN PA_KEYS
static const char *const LB_BTN_NAME[LB_NBTN] = {"PUSH", "HOME", "VOL-", "VOL+", "LEFT", "RIGHT"};
enum { LB_B_OCT_DN = -1, LB_B_OCT_UP = -1, LB_B_PAGE = 1, LB_B_NOTE = 5, LB_B_TONE = 0, LB_B_WALK = -1,
       LB_B_VOL_DN = 2, LB_B_VOL_UP = 3, LB_B_WAVE = 4 };
#define LB_NKEY 0u
#define LB_KEY_NOTE0 60u
#define LB_NENC 1u
static const char *const LB_ENC_NAME[LB_NENC] = {"KNOB"};
#define LB_ENC_CUTOFF 0u                 /* the encoder that also sets the filter: the knob */
#define LB_NAN 1u
static const char *const LB_AN_NAME[LB_NAN] = {"INPUT"};
#define LB_AN_MASTER -1
#define LB_NPAD 0u
#define LB_NLED 0u
#define LB_HAS_JACK 0

static volatile int32_t lb__enc;
static volatile uint32_t lb__enc_ab, lb__in_peak;
static void lb_init(void)
{
    pa_input_init();
    (void)pa_board_inputs();                     /* PC0, PC1, PA10, PC10 inputs */
    FM1_PR(FM1_PC, FM1_PU) |= 3u;                /* the encoder's pull-ups */
    lb__enc_ab = FM1_PR(FM1_PC, FM1_IN) & 3u;
}
static void lb_tick(void)                        /* TIMER5, 10 kHz */
{
    static uint32_t k;
    if (++k >= 10u) {                            /* 1 kHz: the encoder as a quadrature pair, a detent per 4 transitions */
        static const int8_t STEP[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
        static int32_t sub;
        uint32_t ab = FM1_PR(FM1_PC, FM1_IN) & 3u;
        k = 0;
        sub += STEP[(lb__enc_ab << 2) | ab];
        lb__enc_ab = ab;
        if (ab == 3u && sub) {                   /* back at the detent (both high): whole turns only */
            lb__enc += sub > 0 ? (sub + 2) / 4 : (sub - 2) / 4;
            sub = 0;
        }
    }
    {
        static uint32_t d;
        if (++d >= 100u) {                       /* 100 Hz: the buttons */
            d = 0;
            pa_input_poll();
        }
    }
}
static uint32_t lb_buttons(void) { return pa_in.down; }
static uint64_t lb_keys(void) { return 0; }
static uint32_t lb_key_vel(uint32_t k) { (void)k; return 0; }
static int32_t lb_enc_take(uint32_t e)
{
    int32_t s;
    if (e)
        return 0;
    __asm__ volatile("cli" ::: "memory");
    s = lb__enc;
    lb__enc = 0;
    __asm__ volatile("sti" ::: "memory");
    return s;
}
static uint32_t lb_pads(void) { return 0; }
static uint32_t lb_pad_vel(uint32_t p) { (void)p; return 0; }
static void lb_analog_poll(void) {}
static int32_t lb_analog(uint32_t i)             /* the input's peak since the last look, 0..1023 */
{
    uint32_t p;
    if (i)
        return -1;
    p = lb__in_peak;
    lb__in_peak = 0;
    return (int32_t)(p >> 13);
}
static uint32_t lb_jack(void) { return 0; }
static void lb_led(uint32_t i, uint32_t level) { (void)i; (void)level; }
static void lb_pad_rgb(uint32_t p, uint32_t rgb) { (void)p; (void)rgb; }
static void lb_leds_flush(void) {}

/* ---- the panel: its own init table (pa_lcd_init.h), display on after the first frame ---- */
#define LB_LCD_SETTLE_MS 50u
#define LB_LCD_SEQ 1
#define LB_LCD_MADCTL 0x60u             /* (in the table: a quarter turn from the FM-1) */
static void lb_lcd_hw_init(void) { pa_lcd_hw_init(); }
static void lb_lcd_cmd_raw(uint8_t c) { pa_lcd_send_cmd(c); pa_lcd_wait(); pa_lcd_deselect(); }
static void lb_lcd_data_raw(const void *p, uint32_t n) { pa_lcd_send_data(p, n); pa_lcd_wait(); pa_lcd_deselect(); }
static void lb_lcd_seq(void (*cmd)(uint8_t, const uint8_t *, uint32_t))
{
    uint32_t i;
    for (i = 0; i < sizeof PA_LCD_INIT / sizeof PA_LCD_INIT[0]; i++) {
        if (PA_LCD_INIT[i].cmd == PA_LCD_INIT_DELAY)
            fm1_delay_ms(PA_LCD_INIT[i].len);
        else
            cmd(PA_LCD_INIT[i].cmd, PA_LCD_INIT[i].data, PA_LCD_INIT[i].len);
    }
}
static void lb_lcd_after_init(void) {}
static uint32_t lb_lcd_timeouts(void) { return pa_lcd_timeouts; }

/* ---- audio: out on channel 0, in on channel 1, both stereo 24-bit ---- */
#define LB_AUDIO_HALF_FRAMES 64u
#define LB_HALF_WORDS (2u * LB_AUDIO_HALF_FRAMES)
static int32_t lb__aout[2u * LB_HALF_WORDS] __attribute__((aligned(4)));
static int32_t lb__ain[2u * LB_HALF_WORDS] __attribute__((aligned(4)));
static void lb_render(int32_t *out, uint32_t frames);
static volatile uint32_t lb_audio_irqs;
void fm1_alnk0_irq(void)
{
    uint8_t p = pa_audio_pending();
    if (p & PA_AUDIO_IN) {
        const int32_t *in = &lb__ain[pa_audio_in_half() * LB_HALF_WORDS];
        uint32_t i, pk = lb__in_peak;
        for (i = 0; i < LB_HALF_WORDS; i++) {
            int32_t v = (int32_t)((uint32_t)in[i] << 8) >> 8;   /* 24-bit, not sign-extended */
            uint32_t a = (uint32_t)(v < 0 ? -v : v);
            if (a > pk)
                pk = a;
        }
        lb__in_peak = pk;
        pa_audio_ack_in();
    }
    if (p & PA_AUDIO_OUT) {
        lb_render(&lb__aout[pa_audio_free_half() * LB_HALF_WORDS], LB_AUDIO_HALF_FRAMES);
        pa_audio_ack_out();
        lb_audio_irqs++;
    }
}
extern void isr_alnk0(void);
static int lb_audio_start(void)
{
    uint32_t i;
    for (i = 0; i < 2u * LB_HALF_WORDS; i++)
        lb__aout[i] = lb__ain[i] = 0;
    pa_audio_init(lb__aout, lb__ain, LB_HALF_WORDS, isr_alnk0, 3);
    return 0;
}
