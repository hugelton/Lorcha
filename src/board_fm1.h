/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha on the M-VAVE FM-1 (felucca-bsp boards/fm1): 14 buttons, 27 keys, 7 encoders, the MASTER pot and the battery,
 * 41 LEDs, the ST7789 panel, ALNK0 audio out. */
#pragma once

#define LB_NAME "FM-1"
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"

#define LB_NBTN 14u
static const char *const LB_BTN_NAME[LB_NBTN] = {"OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT",
                                                 "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
enum { LB_B_TONE = 12, LB_B_LED = 13, LB_B_MIDI = 9 };     /* PLAY, REC, SAVE */
#define LB_NKEY 27u
#define LB_KEY_NOTE0 53u                  /* F3 */
#define LB_NENC 7u
static const char *const LB_ENC_NAME[LB_NENC] = {"SEL", "ALG", "K1", "K2", "K3", "K4", "PRE"};
#define LB_NAN 2u
static const char *const LB_AN_NAME[LB_NAN] = {"MASTER", "BATT"};
#define LB_NPAD 0u
#define LB_NLED FM1_NKEY                  /* 0..13 the buttons', 14..40 the keys' */

static int32_t lb__an[LB_NAN];
static void lb_init(void) { fm1_input_init(); fm1_adc_init(); }
static void lb_tick(void) { fm1_input_tick(); }
static uint32_t lb_buttons(void) { return fm1_in.buttons; }
static uint64_t lb_keys(void) { return fm1_in.notes; }
static uint32_t lb_pads(void) { return 0; }
static int32_t lb_enc_take(uint32_t e) { return fm1_enc_take(e); }
static void lb_analog_poll(void)          /* main loop: polled conversions */
{
    lb__an[0] = fm1_adc_read(FM1_ADC_MASTER);
    lb__an[1] = fm1_adc_read(FM1_ADC_BATT);
}
static int32_t lb_analog(uint32_t i) { return i < LB_NAN ? lb__an[i] : -1; }

/* LED levels, as the scan makes them per LED: dark, the glow (~3 %), the mid level (1 frame in 8, + the glow), a breath,
 * lit */
#define LB_LED_NLEVELS 5u
static void lb_led_level_name(uint32_t lv, char *t)
{
    static const char *const N[LB_LED_NLEVELS] = {"OFF", "GLOW 3%", "MID 15%", "BREATH", "FULL"};
    const char *s = N[lv < LB_LED_NLEVELS ? lv : 0];
    while ((*t++ = *s++))
        ;
}
static void lb__bit(uint8_t *a, uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                a[p] = (uint8_t)(on ? a[p] | 1u << r : a[p] & ~(1u << r));
}
static void lb_led(uint32_t id, uint32_t lv)
{
    lb__bit(fm1_led, id, lv == 4u);
    lb__bit(fm1_led_breath, id, lv == 3u);
    lb__bit(fm1_led_mid, id, lv == 2u);
    lb__bit(fm1_led_dim, id, lv == 1u || lv == 2u);
}
static void lb_leds_flush(uint32_t lv) { (void)lv; }

/* the panel */
static void lb_lcd_hw_init(void) { fm1_lcd_hw_init(); }
static void lb_lcd_cmd_raw(uint8_t c) { fm1_lcd_send_cmd(c); (void)fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_data_raw(const void *p, uint32_t n) { fm1_lcd_send_data(p, n); (void)fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_tune(void (*cmd)(uint8_t, const uint8_t *, uint32_t)) { (void)cmd; }
#define LB_LCD_MADCTL 0x00u
static void lb_lcd_after_init(void) { fm1_lcd_baud(4); fm1_lcd_backlight(1); }

/* audio: stereo Q23 at 44,117.6 Hz */
#define LB_HALF_FRAMES 64u
static int32_t lb__abuf[2u * 2u * LB_HALF_FRAMES] __attribute__((aligned(4)));
static void lb_render(int32_t *out, uint32_t frames);
void fm1_alnk0_irq(void)
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        lb_render(&lb__abuf[fm1_audio_free_half() * 2u * LB_HALF_FRAMES], LB_HALF_FRAMES);
        fm1_audio_ack_half();
    }
}
extern void isr_alnk0(void);
static int lb_audio_start(void)
{
    return fm1_audio_init_checked(lb__abuf, sizeof lb__abuf / sizeof lb__abuf[0], 2u * LB_HALF_FRAMES, FM1_AUDIO_RATE,
                                  isr_alnk0, 3);
}
