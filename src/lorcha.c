/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha: a small checker for felucca-bsp on the M-VAVE FM-1 (and, built but not the target, the SMK-37 and Pocket AMP).
 * One screen shows:
 *   the buttons, keys and pads as pressed; the encoders as turned (a count each); the pots and other analog inputs
 *   the LEDs' brightness levels: all at one level, the LED button steps through the levels the board can make (a held
 *     button's or key's own LED full)
 *   audio: a 440 Hz test tone on the TONE button
 *   USB: whether the host enumerated it; MIDI in (a count, the last message); MIDI out (the keys and pads as notes, the
 *     MIDI button a C4)
 * No update entry: a unit goes back through UBOOT (the SysEx soft key F0 22 24 35 7D F7, or two boots in a row that never
 * reach 2 s). */
#include <stdint.h>

#define FELUCCA_CDC 0
#define FELUCCA_OTA 0
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#ifndef LORCHA_EMU_PLL
#define LORCHA_EMU_PLL 0                    /* 1: the emulator-only build (see app_main) */
#endif

#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_timer.h"
#if defined(LORCHA_BOARD_FM1)
#include "board_fm1.h"
#elif defined(LORCHA_BOARD_SMK37)
#include "board_smk37.h"
#elif defined(LORCHA_BOARD_POCKETAMP)
#include "board_pocketamp.h"
#else
#error "LORCHA_BOARD_FM1, LORCHA_BOARD_SMK37 or LORCHA_BOARD_POCKETAMP"
#endif
#undef FELUCCA_ID
#define FELUCCA_ID LB_NAME "_960"           /* the identity a host asks for; == the package's */
#define FELUCCA_USB_PRODUCT "Lorcha " LB_NAME
#include "libc.c"
#include "usb.c"
#include "midi.h"
#include "tone.h"
#include "panel.h"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];

static volatile struct { uint32_t magic, pending, failed; } boot __attribute__((section(".noinit")));
static volatile uint32_t ms;
static volatile uint8_t tone_on;
static uint32_t tone_phase;

void fm1_timer5_irq(void)                   /* 10 kHz: the board's scan, ms, USB at 2 kHz */
{
    static uint32_t sub, last, acc;
    uint32_t t = fm1_ticks();
    fm1_timer5_ack();
    lb_tick();
    acc += t - last;
    last = t;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        ms++;
    }
    fm1_ms = ms;
    if (++sub == 5u) {
        sub = 0;
        usb_poll();
    }
}
extern void isr_timer5(void);

static void lb_render(int32_t *out, uint32_t frames) { tone_render(&tone_phase, tone_on, out, frames); }

static void fm1_fault(const fm1_crash_t *c)  /* the guards: a red screen, then a reset */
{
    (void)c;
    pn_fill(0, 0, 240, 240, 0xF800);
    fm1_delay_ms(1500);
    fm1_reboot();
}

static char *put_u(char *p, uint32_t v)
{
    char b[10];
    uint32_t n = 0;
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n)
        *p++ = b[--n];
    *p = 0;
    return p;
}
static char *put_s(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

enum { BLACK = 0x0000, WHITE = 0xFFFF, RED = 0xF800, GREEN = 0x07E0, BLUE = 0x001F, YELLOW = 0xFFE0, CYAN = 0x07FF,
       MAGENTA = 0xF81F, DIM = 0x2104, GREY = 0x8410, ORANGE = 0xFC00, BROWN = 0x49C5, CREAM = 0xFFBD };

/* the layout */
#define Y_BTN 18u
#define BTN_W 39u
#define BTN_H 13u
#define Y_KEY (Y_BTN + (LB_NBTN + 5u) / 6u * (BTN_H + 2u) + 2u)
#define Y_PAD (Y_KEY + (LB_NKEY ? 14u : 0u))
#define Y_ENC (Y_PAD + (LB_NPAD ? 18u : 0u))
#define ENC_W 30u
#define Y_AN (Y_ENC + 19u)
#define AN_ROW 9u
#define Y_STATUS 202u
_Static_assert(Y_AN + LB_NAN * AN_ROW <= Y_STATUS - 4u, "the screen fits");

static int32_t enc_pos[LB_NENC], enc_shown[LB_NENC], an_shown[LB_NAN > 0 ? LB_NAN : 1];
static uint32_t btn_shown = ~0u, pad_shown = ~0u, btn_prev, pad_prev, first = 1;
static uint64_t key_shown = ~0ull, key_prev;
static uint32_t led_level = LB_LED_NLEVELS - 1u, led_shown = ~0u;
static uint32_t mi_n, mo_n, mo_lost, mi_last, status_shown = ~0u;
static int audio_err;

static void midi_send(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (midi_out_try(midi_pkt(st, d1, d2)))
        mo_n++;
    else
        mo_lost++;                          /* no host, or the ring full */
}

static void draw_static(void)
{
    uint32_t i;
    static const uint16_t BARS[6] = {RED, GREEN, BLUE, WHITE, YELLOW, CYAN};
    pn_fill(0, 0, 240, 240, BLACK);
    pn_fill(0, 0, 240, 16, BROWN);
    pn_text(4, 1, "LORCHA", 2, CREAM, BROWN);
    pn_text(84, 5, LB_NAME, 1, YELLOW, BROWN);
    for (i = 0; i < 6u; i++)                /* the colour order */
        pn_fill(178u + i * 10u, 3, 10, 10, BARS[i]);
    pn_fill(237, 0, 3, 3, MAGENTA);         /* the top-right corner: the orientation */
    for (i = 0; i < LB_NENC; i++)
        pn_text(i * ENC_W + 1u, Y_ENC, LB_ENC_NAME[i], 1, CYAN, BLACK);
    for (i = 0; i < LB_NAN; i++) {
        an_shown[i] = 0x7FFFFFFF;
        pn_text(2, Y_AN + i * AN_ROW + 1u, LB_AN_NAME[i], 1, CYAN, BLACK);
    }
}

static void draw_inputs(void)
{
    uint32_t i, b = lb_buttons(), pd = lb_pads();
    uint64_t k = lb_keys();
    char t[16];
    for (i = 0; i < LB_NBTN; i++)
        if ((b ^ btn_shown) >> i & 1u) {
            uint32_t x = i % 6u * (BTN_W + 1u), y = Y_BTN + i / 6u * (BTN_H + 2u), on = b >> i & 1u;
            pn_fill(x, y, BTN_W, BTN_H, on ? WHITE : DIM);
            pn_text(x + 2u, y + 3u, LB_BTN_NAME[i], 1, on ? BLACK : GREY, on ? WHITE : DIM);
        }
    btn_shown = b;
    for (i = 0; i < LB_NKEY; i++)
        if ((k ^ key_shown) >> i & 1u) {
            static const uint8_t SHARP[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
            uint32_t w = 240u / (LB_NKEY ? LB_NKEY : 1u);
            pn_fill(i * w, Y_KEY, w - 1u, 11, k >> i & 1u ? CYAN : SHARP[(LB_KEY_NOTE0 + i) % 12u] ? DIM : GREY);
        }
    key_shown = k;
    for (i = 0; i < LB_NPAD; i++)
        if ((pd ^ pad_shown) >> i & 1u)
            pn_fill(i % 8u * 30u, Y_PAD + i / 8u * 8u, 28, 7, pd >> i & 1u ? MAGENTA : DIM);
    pad_shown = pd;
    for (i = 0; i < LB_NENC; i++)
        if (enc_pos[i] != enc_shown[i] || first) {
            int32_t v = enc_pos[i];
            char *p = t;
            enc_shown[i] = v;
            if (v < 0) {
                *p++ = '-';
                v = -v;
            }
            put_u(p, (uint32_t)v);
            pn_field(i * ENC_W + 1u, Y_ENC + 9u, ENC_W - 2u, t, 1, WHITE, BLACK);
        }
    first = 0;
    for (i = 0; i < LB_NAN; i++) {
        int32_t v = lb_analog(i);
        if (v != an_shown[i]) {
            an_shown[i] = v;
            pn_bar(44, Y_AN + i * AN_ROW, 150, 8, v, 1023, GREEN, DIM);
            if (v < 0)
                put_s(t, "--");
            else
                put_u(t, (uint32_t)v);
            pn_field(198, Y_AN + i * AN_ROW + 1u, 40, t, 1, WHITE, BLACK);
        }
    }
}

static void draw_status(void)
{
    char t[48], *p;
    uint32_t key = (uint32_t)tone_on | usb.config << 1 | led_level << 2 | (mi_n + mo_n + mo_lost) << 8;
    if (key == status_shown)
        return;
    status_shown = key;
    p = put_s(t, "LED ");
    if (LB_NLED)
        lb_led_level_name(led_level, p);
    else
        put_s(p, "NONE KNOWN ON THIS BOARD");
    pn_field(4, Y_STATUS, 236, t, 1, WHITE, BLACK);
    if (audio_err) {
        p = put_s(t, "AUDIO ERROR -");
        put_u(p, (uint32_t)-audio_err);
    } else {
        put_s(t, tone_on ? "TONE 440 HZ ON" : "TONE OFF");
    }
    pn_field(4, Y_STATUS + 10u, 236, t, 1, audio_err ? RED : tone_on ? YELLOW : WHITE, BLACK);
    p = put_s(t, usb.config ? "USB ENUMERATED" : "USB NOT ENUMERATED");
    p = put_s(p, "  IN ");
    p = put_u(p, mi_n);
    p = put_s(p, " OUT ");
    p = put_u(p, mo_n);
    if (mo_lost) {
        p = put_s(p, "/");
        put_u(p, mo_lost);
    }
    pn_field(4, Y_STATUS + 20u, 236, t, 1, usb.config ? GREEN : ORANGE, BLACK);
    p = put_s(t, "LAST IN ");
    if (mi_n)
        midi_text(mi_last, p);
    else
        put_s(p, "-");
    pn_field(4, Y_STATUS + 30u, 236, t, 1, WHITE, BLACK);
}

static int hit(uint32_t pressed, int b) { return b >= 0 && (pressed >> (uint32_t)b & 1u); }

static void inputs(void)
{
    uint32_t b = lb_buttons(), pressed = b & ~btn_prev, released = ~b & btn_prev, pd = lb_pads(), i;
    uint64_t k = lb_keys(), kc = k ^ key_prev;
    btn_prev = b;
    if (hit(pressed, LB_B_TONE))
        tone_on = !tone_on;
    if (hit(pressed, LB_B_LED))
        led_level = (led_level + 1u) % LB_LED_NLEVELS;
    if (hit(pressed, LB_B_MIDI))
        midi_send(0x90, 60, 100);
    if (hit(released, LB_B_MIDI))
        midi_send(0x80, 60, 0);
    for (i = 0; i < LB_NKEY; i++)
        if (kc >> i & 1u)
            midi_send(k >> i & 1u ? 0x90 : 0x80, (uint8_t)(LB_KEY_NOTE0 + i), k >> i & 1u ? 100 : 0);
    key_prev = k;
    for (i = 0; i < LB_NPAD; i++)
        if ((pd ^ pad_prev) >> i & 1u)
            midi_send(pd >> i & 1u ? 0x99 : 0x89, (uint8_t)(36u + i), pd >> i & 1u ? 100 : 0);
    pad_prev = pd;
    for (i = 0; i < LB_NENC; i++)
        enc_pos[i] += lb_enc_take(i);
    if (midi_in_overflow || midi_usb_lost) {    /* a broken stream or a lost host: start clean */
        mi_r = mi_w;
        midi_in_overflow = 0;
        midi_usb_lost = 0;
    }
    while (mi_r != mi_w) {
        mi_last = midi_in_q[mi_r % MQ];
        mi_r++;
        mi_n++;
    }
}

static void leds(void)                      /* every LED at the level; a held button's or key's own LED full */
{
    uint32_t i, b = lb_buttons();
    uint64_t k = lb_keys();
    uint32_t sig = led_level ^ b * 31u ^ (uint32_t)k * 7u ^ (uint32_t)(k >> 32) * 13u;
    if (sig != led_shown) {
        led_shown = sig;
        for (i = 0; i < LB_NLED; i++) {
            uint32_t held = i < LB_NBTN ? b >> i & 1u : 0u;
#ifdef LORCHA_BOARD_FM1
            if (i >= 14u)
                held = (uint32_t)(k >> (i - 14u)) & 1u;
#endif
            lb_led(i, held ? LB_LED_NLEVELS - 1u : led_level);
        }
    }
    lb_leds_flush(led_level);
}

static void app_main(void)
{
    uint32_t last_an = 0, last_led = 0, healthy = 0;
    lb_init();
    pn_init();
    draw_static();
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
#if LORCHA_EMU_PLL
    /* the emulator-only build (build_in.sh --emu, never for a unit): fm1-emu 0.4.0 reads the audio PLL as zero, and
     * felucca-bsp's audio start takes only the clock a bootloader leaves running; given here only when it reads zero */
    if (FM1_AUDIO_PLL_CON0 == 0u && FM1_AUDIO_PLL_CON1 == 0u) {
        FM1_AUDIO_PLL_CON0 = FM1_AUDIO_PLL_STOCK0;
        FM1_AUDIO_PLL_CON1 = FM1_AUDIO_PLL_STOCK1;
    }
#endif
    audio_err = lb_audio_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    for (;;) {
        uint32_t now = ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req) {                /* the SysEx soft key */
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        if (!healthy && now > 2000u) {      /* it runs: the next boot is no loop */
            healthy = 1;
            boot.pending = 0;
            boot.failed = 0;
        }
        inputs();
        if (now - last_an >= 40u) {
            last_an = now;
            lb_analog_poll();
        }
        if (now - last_led >= 20u) {
            last_led = now;
            leds();
        }
        draw_inputs();
        draw_status();
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    if (boot.magic != 0x4C4F5243u || boot.pending > 1u || boot.failed > 1u) {
        boot.magic = 0x4C4F5243u;           /* "LORC" */
        boot.pending = 0;
        boot.failed = 0;
    }
    if (boot.pending)
        boot.failed++;
    boot.pending = 1;
    if (boot.failed >= 2u) {                /* two boots in a row went nowhere: UBOOT */
        boot.pending = 0;
        boot.failed = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;)
        ;
}
