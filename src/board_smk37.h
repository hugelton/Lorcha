/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha on the M-VAVE SMK-37 Pro / Elite (felucca-bsp-smk37 boards/smk37, Alpha; the build gives smk37_map.h first): 18
 * buttons, 37 keys, 8 encoders, 16 pads with RGB LEDs, 4 faders, the wheels, the pedal, the battery, 19 button LEDs, the
 * ST7789V panel, the FM-1's audio out. */
#pragma once

#define LB_NAME SMK_MODEL_NAME
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"

#define LB_NBTN SMK_NBTN                  /* in the SMK's order (SMK_B_*) */
static const char *const LB_BTN_NAME[LB_NBTN] = {"KBANK", "FBANK", "<", ">", "OCT-", "OCT+", "ARP", "NREP", "SCALE",
                                                 "CHORD", "GLOBE", "BT", "PATCH", "PARA", "FX", "SEQ", "PLAY", "REC"};
enum { LB_B_TONE = SMK_B_PLAY, LB_B_LED = SMK_B_REC, LB_B_MIDI = SMK_B_PARA };
#define LB_NKEY SMK_NKEY
#define LB_KEY_NOTE0 48u                  /* C3 */
#define LB_NENC SMK_NENC
static const char *const LB_ENC_NAME[LB_NENC] = {"K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8"};
#define LB_NAN 9u
static const char *const LB_AN_NAME[LB_NAN] = {"K1 LVL", "F1", "F2", "F3", "F4", "PITCH", "MOD", "PEDAL", "BATT"};
#define LB_NPAD SMK_NPAD
#define LB_NLED SMK_NLED                  /* LED b is button b's (18 a spare) */

static void lb_init(void) { fm1_input_init(); fm1_adc_init(); }
static void lb_tick(void) { fm1_input_tick(); }
static uint32_t lb_buttons(void) { return smk_in.btn; }
static uint64_t lb_keys(void) { return smk_in.keys; }
static uint32_t lb_pads(void) { return smk_in.pads; }
/* K1 is the board layer's MASTER level (smk_an.master, an analog channel here), the others through the FM-1's ids */
static int32_t lb_enc_take(uint32_t e)
{
    int32_t id = e < SMK_NENC ? SMK_ENC_AS_FM1[e] : -1;
    return id < 0 ? 0 : fm1_enc_take((uint32_t)id);
}
static void lb_analog_poll(void) {}
static int32_t lb_analog(uint32_t i)
{
    static const uint8_t FADER[4] = SMK_FADER_ADDR;
    if (i == 0)
        return smk_an.master;
    if (!smk_an.sweeps)
        return -1;
    if (i <= 4u)
        return smk_an.c[FADER[i - 1u]];
    return i == 5u ? smk_an.c[SMK_C_PITCH] : i == 6u ? smk_an.c[SMK_C_MOD] : i == 7u ? smk_an.c[SMK_C_PEDAL]
                                                                                     : smk_an.c[SMK_C_BATT];
}

/* LED levels: the button LEDs take 0..64 (the board layer's levels mode); the pads' RGB the same share of 255, white */
#define LB_LED_NLEVELS 7u
static const uint8_t LB_LV[LB_LED_NLEVELS] = {0, 2, 4, 8, 16, 32, 64};
static void lb_led_level_name(uint32_t lv, char *t)
{
    uint32_t v = LB_LV[lv < LB_LED_NLEVELS ? lv : 0];
    if (v >= 10u)
        *t++ = (char)('0' + v / 10u);
    *t++ = (char)('0' + v % 10u);
    *t++ = '/'; *t++ = '6'; *t++ = '4'; *t = 0;
}
static void lb_led(uint32_t i, uint32_t lv)
{
    int32_t id = i < SMK_NBTN ? SMK_BTN_AS_FM1[i] : -1;
    uint8_t v = LB_LV[lv < LB_LED_NLEVELS ? lv : 0];
    if (i >= SMK_NLED)
        return;
    if (id >= 0)
        fm1_led_lv[id] = v;
    else
        smk_led_x[i] = v;
}
/* the pads: one SPI byte per LED bit (0xF0 a 1, 0xC0 a 0), GRB, after 36 zero bytes; pad p is chain slot SLOT[p] */
static const uint8_t LB_PAD_SLOT[SMK_NPAD] = {0, 15, 14, 13, 12, 11, 10, 9, 1, 2, 3, 4, 5, 6, 7, 8};
static uint8_t lb__pad_frame[36u + 24u * SMK_NPAD] __attribute__((aligned(4)));
static uint32_t lb__pad_shown = ~0u;
static void lb_leds_flush(uint32_t lv)    /* main loop, the panel idle */
{
    uint32_t p, b, key = lv << 16 | smk_in.pads;
    if (!fm1_led_levels_on())
        fm1_led_levels_start();
    if (key == lb__pad_shown)
        return;
    lb__pad_shown = key;
    for (b = 0; b < 36u; b++)
        lb__pad_frame[b] = 0;
    for (p = 0; p < SMK_NPAD; p++) {
        uint32_t c = smk_in.pads >> p & 1u ? 255u : LB_LV[lv < LB_LED_NLEVELS ? lv : 0] * 255u / 64u;
        uint32_t grb = c << 16 | c << 8 | c;
        uint8_t *o = &lb__pad_frame[36u + 24u * LB_PAD_SLOT[p]];
        for (b = 0; b < 24u; b++)
            o[b] = (grb >> (23u - b)) & 1u ? 0xF0u : 0xC0u;
    }
    smk_pads_send(lb__pad_frame, sizeof lb__pad_frame);
}

/* the panel */
static void lb_lcd_hw_init(void) { fm1_lcd_hw_init(); }
static void lb_lcd_cmd_raw(uint8_t c) { fm1_lcd_send_cmd(c); fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_data_raw(const void *p, uint32_t n) { fm1_lcd_send_data(p, n); fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_tune(void (*cmd)(uint8_t, const uint8_t *, uint32_t)) { fm1_lcd_tune(cmd); }
#define LB_LCD_MADCTL 0x00u
static void lb_lcd_after_init(void) { fm1_lcd_baud(SMK_LCD_BAUD); }

/* audio: the FM-1's */
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
