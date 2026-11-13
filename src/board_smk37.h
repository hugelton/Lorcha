/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha on the M-VAVE SMK-37 Pro / Elite: felucca-bsp-smk37's boards/smk37 (Alpha), behind the board calls
 * lorcha.c makes (board.h). The build gives boards/smk37/smk37_map.h first (-include) and SMK_MODEL_NAME.
 *   18 buttons, 37 keys with velocity (C3..C6), 8 encoders, 16 pads (velocity, RGB LEDs), 4 faders, the pitch and mod
 *   wheels, the pedal jack and the battery; 19 button LEDs; the ST7789V panel; the FM-1's audio out. */
#pragma once

#define LB_NAME SMK_MODEL_NAME
#undef FELUCCA_ID                         /* (smk37_map.h's is the reference port's range) */
#define FELUCCA_ID SMK_MODEL_NAME "_960"
#define LB_JEDEC_OK(id) FM1_JEDEC_OK(id)  /* smk37_map.h */
/* FL_RANGE_OK, OTA_AREA, OTA_AREA_LEN, OTA_REC_NAME: smk37_map.h (never 0xC0000 or the calibration at 0xC1000) */

#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"

/* ---- the controls: the SMK's own order (SMK_B_*) ---- */
#define LB_NBTN SMK_NBTN
static const char *const LB_BTN_NAME[LB_NBTN] = {"KBANK", "FBANK", "<", ">", "OCT-", "OCT+", "ARP", "NREP", "SCALE",
                                                 "CHORD", "GLOBE", "BT", "PATCH", "PARA", "FX", "SEQ", "PLAY", "REC"};
enum { LB_B_OCT_DN = SMK_B_OCT_DN, LB_B_OCT_UP = SMK_B_OCT_UP, LB_B_PAGE = SMK_B_PATCH, LB_B_NOTE = SMK_B_PARA,
       LB_B_TONE = SMK_B_PLAY, LB_B_WALK = SMK_B_REC, LB_B_VOL_DN = -1, LB_B_VOL_UP = -1, LB_B_WAVE = SMK_B_SCALE };
#define LB_NKEY SMK_NKEY
#define LB_KEY_NOTE0 48u                  /* C3 */
#define LB_NENC SMK_NENC
static const char *const LB_ENC_NAME[LB_NENC] = {"K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8"};
#define LB_ENC_CUTOFF 1u                 /* the encoder that also sets the filter: K2 (K1 is the MASTER level) */
#define LB_NAN 9u
static const char *const LB_AN_NAME[LB_NAN] = {"MASTER", "F1", "F2", "F3", "F4", "PITCH", "MOD", "PEDAL", "BATT"};
#define LB_AN_MASTER 0
#define LB_NPAD SMK_NPAD
#define LB_NLED SMK_NLED                  /* the button LEDs, by button (18 a spare) */
#define LB_HAS_JACK 0

/* encoder i of the eight, through the FM-1's ids (SMK_ENC_AS_FM1); K1 is the MASTER level and gives no steps */
static int32_t lb_enc_take(uint32_t e)
{
    int32_t id = e < SMK_NENC ? SMK_ENC_AS_FM1[e] : -1;
    return id < 0 ? 0 : fm1_enc_take((uint32_t)id);
}
static void lb_init(void)
{
    fm1_input_init();
    fm1_adc_init();
}
static void lb_tick(void) { fm1_input_tick(); }
static uint32_t lb_buttons(void) { return smk_in.btn; }
static uint64_t lb_keys(void) { return smk_in.keys; }
static uint32_t lb_key_vel(uint32_t k) { return k < SMK_NKEY ? smk_in.vel[k] : 0u; }
static uint32_t lb_pads(void) { return smk_in.pads; }
static uint32_t lb_pad_vel(uint32_t p) { return p < SMK_NPAD ? smk_in.pad_vel[p] : 0u; }
static void lb_analog_poll(void) {}
static int32_t lb_analog(uint32_t i)
{
    static const uint8_t FADER[4] = SMK_FADER_ADDR;
    if (!smk_an.sweeps && i)
        return -1;
    switch (i) {
    case 0: return smk_an.master;
    case 1: case 2: case 3: case 4: return smk_an.c[FADER[i - 1u]];
    case 5: return smk_an.c[SMK_C_PITCH];
    case 6: return smk_an.c[SMK_C_MOD];
    case 7: return smk_an.c[SMK_C_PEDAL];
    case 8: return smk_an.c[SMK_C_BATT];
    }
    return -1;
}
static uint32_t lb_jack(void) { return 0; }

/* the button LEDs: button b's own LED is SMK LED b. The FM-1's buttons light through fm1_led_lv (by the FM-1's id), the four
 * the FM-1 has not through smk_led_x; both 0..64 */
static void lb_led(uint32_t i, uint32_t level)
{
    int32_t id;
    if (i >= LB_NLED)
        return;
    id = i < SMK_NBTN ? SMK_BTN_AS_FM1[i] : -1;
    if (id >= 0)
        fm1_led_lv[id] = (uint8_t)level;
    else
        smk_led_x[i] = (uint8_t)level;
}
/* the pads' RGB: one SPI byte per LED bit (0xF0 a 1, 0xC0 a 0), GRB, after 36 zero bytes; pad p is chain slot SLOT[p] */
static const uint8_t LB_PAD_SLOT[SMK_NPAD] = {0, 15, 14, 13, 12, 11, 10, 9, 1, 2, 3, 4, 5, 6, 7, 8};
static uint32_t lb__pad_rgb[SMK_NPAD];
static uint8_t lb__pad_frame[36u + 24u * SMK_NPAD] __attribute__((aligned(4)));
static uint8_t lb__pad_dirty = 1;
static void lb_pad_rgb(uint32_t p, uint32_t rgb)
{
    if (p < SMK_NPAD && lb__pad_rgb[p] != rgb) {
        lb__pad_rgb[p] = rgb;
        lb__pad_dirty = 1;
    }
}
static void lb_leds_flush(void)           /* main loop, between two panel transfers (the panel idle) */
{
    uint32_t p, b;
    if (!fm1_led_levels_on())
        fm1_led_levels_start();
    if (!lb__pad_dirty)
        return;
    lb__pad_dirty = 0;
    for (b = 0; b < 36u; b++)
        lb__pad_frame[b] = 0;
    for (p = 0; p < SMK_NPAD; p++) {
        uint32_t c = lb__pad_rgb[p];
        uint32_t grb = ((c >> 8) & 0xFFu) << 16 | ((c >> 16) & 0xFFu) << 8 | (c & 0xFFu);
        uint8_t *o = &lb__pad_frame[36u + 24u * LB_PAD_SLOT[p]];
        for (b = 0; b < 24u; b++)
            o[b] = (grb >> (23u - b)) & 1u ? 0xF0u : 0xC0u;
    }
    smk_pads_send(lb__pad_frame, sizeof lb__pad_frame);
}

/* ---- the panel ---- */
#define LB_LCD_SETTLE_MS 50u
static void lb_lcd_hw_init(void) { fm1_lcd_hw_init(); }
static void lb_lcd_cmd_raw(uint8_t c) { fm1_lcd_send_cmd(c); fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_data_raw(const void *p, uint32_t n) { fm1_lcd_send_data(p, n); fm1_lcd_wait(); fm1_lcd_deselect(); }
static void lb_lcd_tune(void (*cmd)(uint8_t, const uint8_t *, uint32_t)) { fm1_lcd_tune(cmd); }
#define LB_LCD_MADCTL 0x00u
static void lb_lcd_after_init(void) { fm1_lcd_baud(SMK_LCD_BAUD); }
static uint32_t lb_lcd_timeouts(void) { return fm1_lcd_timeouts; }

/* ---- audio: the FM-1's ---- */
#define LB_AUDIO_HALF_FRAMES 64u
static int32_t lb__abuf[2u * 2u * LB_AUDIO_HALF_FRAMES] __attribute__((aligned(4)));
static void lb_render(int32_t *out, uint32_t frames);
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
static int lb_audio_start(void)
{
    return fm1_audio_init_checked(lb__abuf, sizeof lb__abuf / sizeof lb__abuf[0], 2u * LB_AUDIO_HALF_FRAMES,
                                  FM1_AUDIO_RATE, isr_alnk0, 3);
}
