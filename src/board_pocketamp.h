/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha on the M-VAVE Pocket AMP (felucca-bsp boards/pocketamp): 6 buttons, the knob's encoder (PC0 / PC1, read here), no
 * keys, no LED known to felucca-bsp, the panel turned a quarter, I2S out on port A. */
#pragma once

#define LB_NAME "Pocket AMP"
#include "fm1_time.h"
#include "pa_input.h"
#include "pa_lcd_hw.h"
#include "pa_lcd_init.h"
#include "pa_audio.h"

#define LB_NBTN PA_KEYS
static const char *const LB_BTN_NAME[LB_NBTN] = {"PUSH", "HOME", "VOL-", "VOL+", "LEFT", "RIGHT"};
enum { LB_B_TONE = 0, LB_B_LED = -1, LB_B_MIDI = 5 };      /* the knob's push, (none), RIGHT */
#define LB_NKEY 0u
#define LB_KEY_NOTE0 60u
#define LB_NENC 1u
static const char *const LB_ENC_NAME[LB_NENC] = {"KNOB"};
#define LB_NAN 0u
static const char *const LB_AN_NAME[1] = {""};
#define LB_NPAD 0u
#define LB_NLED 0u
#define LB_LED_NLEVELS 1u

static volatile int32_t lb__enc;
static volatile uint32_t lb__ab;
static void lb_init(void)
{
    pa_input_init();
    (void)pa_board_inputs();                     /* PC0, PC1, PA10, PC10 inputs */
    FM1_PR(FM1_PC, FM1_PU) |= 3u;                /* the encoder's pull-ups */
    lb__ab = FM1_PR(FM1_PC, FM1_IN) & 3u;
}
static void lb_tick(void)                        /* 10 kHz */
{
    static uint32_t k, d;
    static int32_t sub;
    if (++k >= 10u) {                            /* 1 kHz: the encoder, a detent per whole cycle (both high at rest) */
        static const int8_t STEP[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
        uint32_t ab = FM1_PR(FM1_PC, FM1_IN) & 3u;
        k = 0;
        sub += STEP[(lb__ab << 2) | ab];
        lb__ab = ab;
        if (ab == 3u && sub) {
            lb__enc += sub > 0 ? (sub + 2) / 4 : (sub - 2) / 4;
            sub = 0;
        }
    }
    if (++d >= 100u) {                           /* 100 Hz: the buttons */
        d = 0;
        pa_input_poll();
    }
}
static uint32_t lb_buttons(void) { return pa_in.down; }
static uint64_t lb_keys(void) { return 0; }
static uint32_t lb_pads(void) { return 0; }
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
static void lb_analog_poll(void) {}
static int32_t lb_analog(uint32_t i) { (void)i; return -1; }
static void lb_led_level_name(uint32_t lv, char *t) { (void)lv; t[0] = '-'; t[1] = 0; }
static void lb_led(uint32_t i, uint32_t lv) { (void)i; (void)lv; }
static void lb_leds_flush(uint32_t lv) { (void)lv; }

/* the panel: its own init table, display on after the first frame */
#define LB_LCD_SEQ 1
#define LB_LCD_MADCTL 0x60u              /* (in the table) */
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

/* audio: out on channel 0 (the input, channel 1, runs and is not read) */
#define LB_HALF_FRAMES 64u
#define LB_HALF_WORDS (2u * LB_HALF_FRAMES)
static int32_t lb__aout[2u * LB_HALF_WORDS] __attribute__((aligned(4)));
static int32_t lb__ain[2u * LB_HALF_WORDS] __attribute__((aligned(4)));
static void lb_render(int32_t *out, uint32_t frames);
void fm1_alnk0_irq(void)
{
    uint8_t p = pa_audio_pending();
    if (p & PA_AUDIO_IN)
        pa_audio_ack_in();
    if (p & PA_AUDIO_OUT) {
        lb_render(&lb__aout[pa_audio_free_half() * LB_HALF_WORDS], LB_HALF_FRAMES);
        pa_audio_ack_out();
    }
}
extern void isr_alnk0(void);
static int lb_audio_start(void)
{
    pa_audio_init(lb__aout, lb__ain, LB_HALF_WORDS, isr_alnk0, 3);   /* (the buffers are zero: .bss) */
    return 0;
}
