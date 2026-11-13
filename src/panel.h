/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The 240 x 240 RGB565 panel of all three boards, on the board's wiring calls (lb_lcd_hw_init, lb_lcd_cmd_raw,
 * lb_lcd_data_raw: one command byte, one DMA from RAM, each waited for and deselected). The init is the ST7789's
 * (SWRESET, SLPOUT, COLMOD 565, MADCTL, the board's tuning, INVON, NORON) or the board's own sequence (LB_LCD_SEQ: the
 * Pocket AMP's table); the display goes on after the first frame. Blocking, main loop only.
 * (felucca-bsp's boards/fm1/fm1_lcd.h does this for the FM-1; the SMK-37's wiring header waits without a result, so
 * Lorcha has this one for all three.) */
#pragma once
#include <stdint.h>
#include "font.h"

#define PN_W 240u
#define PN_H 240u
#define PN_STRIP_ROWS 16u
static uint8_t pn_args[16];
static uint8_t pn_strip[PN_W * 2u * PN_STRIP_ROWS] __attribute__((aligned(4)));   /* pixels, MSB first */
static uint32_t pn_bad;                 /* drawing calls out of the screen (refused) */

static void pn_cmd(uint8_t c, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    lb_lcd_cmd_raw(c);
    if (!n)
        return;
    if (n > sizeof pn_args)
        n = sizeof pn_args;
    for (i = 0; i < n; i++)
        pn_args[i] = d[i];              /* the DMA reads RAM only */
    lb_lcd_data_raw(pn_args, n);
}

static int pn_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t a[4];
    if (!w || !h || x >= PN_W || y >= PN_H || w > PN_W - x || h > PN_H - y) {
        if (w && h)
            pn_bad++;
        return -1;
    }
    a[0] = 0; a[1] = (uint8_t)x; a[2] = 0; a[3] = (uint8_t)(x + w - 1u);
    pn_cmd(0x2A, a, 4);
    a[1] = (uint8_t)y; a[3] = (uint8_t)(y + h - 1u);
    pn_cmd(0x2B, a, 4);
    pn_cmd(0x2C, 0, 0);
    return 0;
}

static void pn_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, rows, n;
    if (pn_window(x, y, w, h))
        return;
    rows = h < PN_STRIP_ROWS ? h : PN_STRIP_ROWS;
    for (i = 0; i < w * rows; i++) {
        pn_strip[2u * i] = (uint8_t)(c >> 8);
        pn_strip[2u * i + 1u] = (uint8_t)c;
    }
    for (; h; h -= n) {
        n = h < rows ? h : rows;
        lb_lcd_data_raw(pn_strip, w * n * 2u);
    }
}

static void pn_init(void)
{
    static const uint8_t colmod = 0x55, madctl = LB_LCD_MADCTL;
    lb_lcd_hw_init();
    fm1_delay_ms(50);
    pn_cmd(0x01, 0, 0);                 /* SWRESET */
    fm1_delay_ms(150);
#ifdef LB_LCD_SEQ
    lb_lcd_seq(pn_cmd);
#else
    pn_cmd(0x11, 0, 0);                 /* SLPOUT */
    fm1_delay_ms(120);
    pn_cmd(0x3A, &colmod, 1);           /* RGB565 */
    pn_cmd(0x36, &madctl, 1);
    lb_lcd_tune(pn_cmd);
    pn_cmd(0x21, 0, 0);                 /* INVON: IPS */
    pn_cmd(0x13, 0, 0);                 /* NORON */
#endif
    (void)colmod;
    (void)madctl;
    lb_lcd_after_init();
    pn_fill(0, 0, PN_W, PN_H, 0);
    pn_cmd(0x29, 0, 0);                 /* DISPON, after the first frame */
}

/* text at (x, y), each font pixel s x s, one column of gap; drawn in strips of at most PN_STRIP_ROWS rows;
 * -> its width. Clipped to the screen's right edge. */
static uint32_t pn_text(uint32_t x, uint32_t y, const char *t, uint32_t s, uint16_t fg, uint16_t bg)
{
    uint32_t n = 0, w, h = FONT_H * s, row, col, i;
    const char *p;
    for (p = t; *p; p++)
        n++;
    if (!n || x >= PN_W)
        return 0;
    w = n * (FONT_W + 1u) * s;
    if (w > PN_W - x)
        w = (PN_W - x) / ((FONT_W + 1u) * s) * ((FONT_W + 1u) * s);
    if (!w || h > PN_STRIP_ROWS || pn_window(x, y, w, h))
        return 0;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++) {
            uint32_t ch = col / ((FONT_W + 1u) * s), gx = col % ((FONT_W + 1u) * s) / s, gy = row / s;
            const uint8_t *g = font_glyph(t[ch]);
            uint16_t c;
            if (!g)
                g = font_glyph('?');
            c = gx < FONT_W && (g[gy] >> (FONT_W - 1u - gx)) & 1u ? fg : bg;
            i = 2u * (row * w + col);
            pn_strip[i] = (uint8_t)(c >> 8);
            pn_strip[i + 1u] = (uint8_t)c;
        }
    lb_lcd_data_raw(pn_strip, w * h * 2u);
    return w;
}

/* text padded with the background to width w (a field that is redrawn) */
static void pn_field(uint32_t x, uint32_t y, uint32_t w, const char *t, uint32_t s, uint16_t fg, uint16_t bg)
{
    uint32_t tw = pn_text(x, y, t, s, fg, bg);
    if (tw < w)
        pn_fill(x + tw, y, w - tw, FONT_H * s, bg);
}

/* a bar of v / max over w pixels, lit then dark */
static void pn_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int32_t v, int32_t max, uint16_t on, uint16_t off)
{
    uint32_t lit = v <= 0 ? 0u : v >= max ? w : (uint32_t)v * w / (uint32_t)max;
    if (lit)
        pn_fill(x, y, lit, h, on);
    if (lit < w)
        pn_fill(x + lit, y, w - lit, h, off);
}
