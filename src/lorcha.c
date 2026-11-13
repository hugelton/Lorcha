/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Lorcha: a template firmware that exercises felucca-bsp on the M-VAVE FM-1, SMK-37 Pro / Elite and Pocket AMP.
 * Not an instrument: boot, the screen, the keys and controls, the LEDs, audio out (a test tone and a small voice),
 * USB MIDI in and out, the update entry, and a HAL test page. One source, one board header per device (board_*.h,
 * picked by LORCHA_BOARD_*). One compilation unit, as felucca-bsp's examples.
 *
 * Pages (the board's page button, or '1' / '2' on the serial console):
 *   HOME   the name, the board, the identity, the version; the voice (wave, filter, volume, the note) and USB / MIDI
 *   TEST   every input as it is (buttons, keys, encoders, pads, the analog channels) and each output on a button:
 *          the test tone, an LED walk, a MIDI test note; colour bars along the top (the panel and its colour order)
 * Always: the keys and pads play the voice and go out as USB MIDI notes (channel 1; pads on channel 10); the encoders
 * go out as CC 20.. (the first also sets the filter); MIDI in plays the voice (any channel; CC 74 the filter).
 * USB: MIDI and a CDC serial console (h help). The update entry: M-UPGRADE over USB-MIDI (chip/ota.c), and the SysEx
 * soft key for UBOOT. Two boots in a row that do not reach the main loop's healthy point (2 s) go to UBOOT. */
#include <stdint.h>

#define LORCHA_VERSION "0.1.0"
#ifndef LORCHA_EMU_PLL
#define LORCHA_EMU_PLL 0                    /* 1: the emulator-only build (see app_main) */
#endif
#define FELUCCA_CDC 1                       /* the serial console */
#define FELUCCA_OTA 1                       /* the update entry */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

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

#define FELUCCA_USB_PRODUCT "Lorcha " LB_NAME   /* the USB name (31 characters at most) */
#include "fm1_flash.h"
#include "libc.c"
#include "usb.c"
#include "ota.c"
#include "synth.h"
#include "midi.h"
#include "panel.h"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];

/* the boot guard: NOINIT survives a reset. magic, pending (a boot that has not reached the healthy point), failed (in a row) */
static volatile struct { uint32_t magic, pending, failed; } boot __attribute__((section(".noinit")));
#define BOOT_MAGIC 0x4C4F5243u              /* "LORC" */

static volatile uint32_t ms;
static syn_t syn;
static volatile int32_t gain = 16384;       /* Q15: the master level */
static volatile uint8_t audio_on;
static int audio_err;

/* (irq_save / irq_restore: chip/fm1_flash_access.h) */

/* ---- the interrupts ---- */
void fm1_timer5_irq(void)                   /* 10 kHz: the board's scan, the ms count, USB at 2 kHz */
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

static void lb_render(int32_t *out, uint32_t frames)    /* the audio interrupt */
{
    syn_render(&syn, out, frames, gain);
}

/* ---- the update (chip/ota.c's hooks) ---- */
static uint8_t flash_ok;
static uint32_t flash_id;
static uint32_t ota_now_ms(void) { return ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if ((off & 0xFFFu) || !FL_RANGE_OK(off, 0x1000u))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!FL_RANGE_OK(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read(off, p, n); }
enum { BLACK = 0x0000, WHITE = 0xFFFF, RED = 0xF800, GREEN = 0x07E0, BLUE = 0x001F, YELLOW = 0xFFE0, CYAN = 0x07FF,
       MAGENTA = 0xF81F, DIM = 0x2104, GREY = 0x8410, NAVY = 0x0010, ORANGE = 0xFC00, BROWN = 0x49C5, CREAM = 0xFFBD };
static void ota_show(uint32_t step, int32_t code)   /* the steps as a bar across the bottom; green done, red failed */
{
    pn_fill(0, 226, 240, 14, BLACK);
    if (step < 9u)
        pn_fill(0, 226, (step > 6u ? 6u : step) * 40u, 14, CYAN);
    else
        pn_fill(0, 226, 240, 14, code == 1 ? GREEN : RED);
}
static void ota_commit(const uint8_t *parm)
{
    boot.pending = 0;                       /* a reset on purpose, not a failed boot */
    boot.failed = 0;
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

static void fm1_fault(const fm1_crash_t *c)  /* the guards: a red screen, then a reset; fm1_crash keeps the record */
{
    (void)c;
    pn_fill(0, 0, 240, 240, RED);
    fm1_delay_ms(1500);
    fm1_reboot();
}

/* ---- text helpers ---- */
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
static char *put_i(char *p, int32_t v)
{
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    return put_u(p, (uint32_t)v);
}
static char *put_s(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}
static char *put_hex(char *p, uint32_t v, uint32_t digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[(v >> (4u * digits)) & 15u];
    *p = 0;
    return p;
}

/* ---- the serial console ---- */
static void con_put(char c)
{
    if (!cdc.dtr || co_w - co_r >= CO_N)
        return;
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}
static void con_puts(const char *s) { while (*s) con_put(*s++); }

/* ---- the app's state ---- */
enum { PAGE_HOME, PAGE_TEST, NPAGES };
static uint8_t page, page_drawn = 0xFF;
static int32_t octave;                      /* the keys' octave shift */
static int32_t enc_pos[LB_NENC > 0 ? LB_NENC : 1];
static uint8_t cc_val[LB_NENC > 0 ? LB_NENC : 1];
static uint32_t btn_prev, pad_prev;
static uint64_t key_prev;
static uint8_t key_note[LB_NKEY > 0 ? LB_NKEY : 1];    /* the note each held key sent (the octave may change meanwhile) */
static uint32_t midi_in_n, midi_out_n, midi_drop, midi_last_in;
static uint32_t walk_until, walk_start, note_off_at;
static uint8_t soft_vol = 20;               /* 0..31 (boards without a MASTER pot) */
static uint8_t dirty_home = 1;
static uint8_t console_status;              /* 's': a status line every second */

static void midi_send(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (midi_out_try(midi_pkt(st, d1, d2)))
        midi_out_n++;
    else
        midi_drop++;
}

static void voice_on(uint8_t note, uint8_t vel)
{
    uint32_t k = irq_save();
    syn_note_on(&syn, note, vel);
    irq_restore(k);
    dirty_home = 1;
}
static void voice_off(uint8_t note)
{
    uint32_t k = irq_save();
    syn_note_off(&syn, note);
    irq_restore(k);
    dirty_home = 1;
}
static void voice_all_off(void)
{
    uint32_t k = irq_save();
    syn_all_off(&syn);
    irq_restore(k);
}
static void tone_set(int on)
{
    syn.tone = (uint8_t)(on != 0);
    dirty_home = 1;
}

/* ---- inputs to actions ---- */
static int hit(uint32_t pressed, int b) { return b >= 0 && (pressed >> (uint32_t)b & 1u); }

static void buttons(void)
{
    uint32_t now = lb_buttons(), pressed = now & ~btn_prev;
    btn_prev = now;
#define PRESSED(b) hit(pressed, (b))
    if (PRESSED(LB_B_PAGE))
        page = (uint8_t)((page + 1u) % NPAGES);
    if (PRESSED(LB_B_TONE))
        tone_set(!syn.tone);
    if (PRESSED(LB_B_WALK)) {
        walk_start = ms;
        walk_until = ms + LB_NLED * 80u + 1u;
    }
    if (PRESSED(LB_B_NOTE)) {               /* a MIDI test note: C4, half a second */
        midi_send(0x90, 60, 100);
        voice_on(60, 100);
        note_off_at = ms + 500u;
    }
    if (PRESSED(LB_B_OCT_DN) && octave > -3)
        octave--, dirty_home = 1;
    if (PRESSED(LB_B_OCT_UP) && octave < 3)
        octave++, dirty_home = 1;
    if (PRESSED(LB_B_WAVE))
        syn.wave = (uint8_t)((syn.wave + 1u) % SYN_NWAVE), dirty_home = 1;
    if (PRESSED(LB_B_VOL_DN) && soft_vol)
        soft_vol--, dirty_home = 1;
    if (PRESSED(LB_B_VOL_UP) && soft_vol < 31u)
        soft_vol++, dirty_home = 1;
#undef PRESSED
}

static void keys(void)
{
    uint64_t now = lb_keys(), ch = now ^ key_prev;
    uint32_t k;
    if (!ch)
        return;
    for (k = 0; k < LB_NKEY; k++) {
        if (!(ch >> k & 1u))
            continue;
        if (now >> k & 1u) {
            int32_t n = (int32_t)LB_KEY_NOTE0 + (int32_t)k + 12 * octave;
            uint8_t v = (uint8_t)lb_key_vel(k);
            if (n < 0 || n > 127)
                continue;
            key_note[k] = (uint8_t)n;
            midi_send(0x90, (uint8_t)n, v);
            voice_on((uint8_t)n, v);
        } else {
            midi_send(0x80, key_note[k], 0);
            voice_off(key_note[k]);
        }
    }
    key_prev = now;
}

static void pads(void)
{
    uint32_t now = lb_pads(), ch = now ^ pad_prev, p;
    if (!ch)
        return;
    for (p = 0; p < LB_NPAD; p++) {
        if (!(ch >> p & 1u))
            continue;
        if (now >> p & 1u) {
            uint8_t v = (uint8_t)lb_pad_vel(p);
            midi_send(0x99, (uint8_t)(36u + p), v);
            voice_on((uint8_t)(36u + p), v);
        } else {
            midi_send(0x89, (uint8_t)(36u + p), 0);
            voice_off((uint8_t)(36u + p));
        }
    }
    pad_prev = now;
}

static void encoders(void)
{
    uint32_t e;
    for (e = 0; e < LB_NENC; e++) {
        int32_t s = lb_enc_take(e), v;
        if (!s)
            continue;
        enc_pos[e] += s;
        v = (int32_t)cc_val[e] + s;
        cc_val[e] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        midi_send(0xB0, (uint8_t)(20u + e), cc_val[e]);
        if (e == LB_ENC_CUTOFF) {
            v = (int32_t)syn.cutoff + 2 * s;
            syn.cutoff = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            dirty_home = 1;
        }
    }
}

#ifdef LB_HAS_TRS
static uint32_t midi_trs_n;                 /* messages from the TRS jack */
#endif
static void midi_handle(uint32_t pkt)       /* a message in, from USB or the TRS jack */
{
    midi_msg_t m = midi_parse(pkt);
    midi_in_n++;
    midi_last_in = pkt;
    dirty_home = 1;
    if (m.kind == MIDI_NOTE_ON)
        voice_on(m.d1, m.d2);
    else if (m.kind == MIDI_NOTE_OFF)
        voice_off(m.d1);
    else if (m.kind == MIDI_CC && m.d1 == 74u)
        syn.cutoff = m.d2;
    else if (m.kind == MIDI_CC && (m.d1 == 123u || m.d1 == 120u))
        voice_all_off();
}

static void midi_in(void)
{
    if (midi_in_overflow || midi_usb_lost) {    /* a broken stream or a lost host: start clean */
        mi_r = mi_w;
        midi_in_overflow = 0;
        midi_usb_lost = 0;
        voice_all_off();
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ];
        mi_r++;
        midi_handle(pkt);
    }
#ifdef LB_HAS_TRS
    {
        static midi_rx_t rx;
        uint8_t b[64];
        uint32_t n = lb_trs_take(b, sizeof b), i;
        for (i = 0; i < n; i++) {
            uint32_t pkt = midi_rx_byte(&rx, b[i]);
            if (pkt) {
                midi_trs_n++;
                midi_handle(pkt);
            }
        }
    }
#endif
}

static void level(void)                     /* the master level: the MASTER pot, or the volume buttons */
{
    int32_t g;
#if LB_AN_MASTER >= 0
    int32_t v = lb_analog(LB_AN_MASTER);
    g = v < 0 ? 16384 : v * 32;
#else
    g = (int32_t)soft_vol * 1057;
#endif
    gain = g > 32767 ? 32767 : g;
}

static void leds(void)
{
    uint32_t i, held = lb_buttons();
    uint64_t k = lb_keys();
    if (walk_until && (int32_t)(walk_until - ms) > 0) {     /* one LED at a time, 80 ms each */
        uint32_t at = (ms - walk_start) / 80u;
        for (i = 0; i < LB_NLED; i++)
            lb_led(i, i == at ? 64u : 0u);
    } else {
        walk_until = 0;
        for (i = 0; i < LB_NLED; i++) {
            uint32_t on = 0;
            if (i < LB_NBTN)
                on = held >> i & 1u;
#ifdef LB_LED_KEY0
            if (i >= LB_LED_KEY0 && i - LB_LED_KEY0 < LB_NKEY)
                on = (uint32_t)(k >> (i - LB_LED_KEY0)) & 1u;
#endif
            if ((int32_t)i == LB_B_TONE && syn.tone)
                on = 1;
            if ((int32_t)i == LB_B_PAGE && page == PAGE_TEST)
                on = 1;
            lb_led(i, on ? 64u : 0u);
        }
    }
    (void)k;
    for (i = 0; i < LB_NPAD; i++) {         /* a pad: its velocity as a colour while held, a dim blue at rest */
        uint32_t v = lb_pad_vel(i);
        lb_pad_rgb(i, lb_pads() >> i & 1u ? (v * 2u) << 16 | (127u - v) << 8 : 0x000008u);
    }
    lb_leds_flush();
}

/* ---- the pages ---- */
static void draw_header(const char *title)
{
    pn_fill(0, 0, 240, 18, BROWN);
    pn_text(4, 2, "LORCHA", 2, CREAM, BROWN);
    pn_text(240u - 4u - 6u * 2u * 4u, 2, title, 2, YELLOW, BROWN);
}

static uint32_t usb_shown = ~0u, midi_shown = ~0u, home_note = ~0u;
static void home_draw(int full)
{
    char t[48], *p;
    if (full) {
        pn_fill(0, 0, 240, 240, BLACK);
        draw_header("HOME");
        pn_text(8, 28, LB_NAME, 2, WHITE, BLACK);
        p = put_s(t, "ID ");
        p = put_s(p, FELUCCA_ID);
        pn_text(8, 48, t, 1, GREY, BLACK);
        p = put_s(t, "VERSION ");
        put_s(p, LORCHA_VERSION);
        pn_text(8, 58, t, 1, GREY, BLACK);
        p = put_s(t, "FLASH ");
        p = put_hex(p, flash_id, 6);
        put_s(p, flash_ok ? " OK" : " UNKNOWN: NO UPDATE");
        pn_text(8, 68, t, 1, flash_ok ? GREY : ORANGE, BLACK);
        pn_text(8, 86, "VOICE", 1, CYAN, BLACK);
        pn_text(8, 146, "USB / MIDI", 1, CYAN, BLACK);
        p = put_s(t, LB_BTN_NAME[LB_B_PAGE]);
        put_s(p, ": TEST PAGE");
        pn_text(8, 200, t, 1, GREY, BLACK);
        if (LB_B_TONE >= 0) {
            p = put_s(t, LB_BTN_NAME[LB_B_TONE]);
            put_s(p, ": TEST TONE");
            pn_text(8, 210, t, 1, GREY, BLACK);
        }
        usb_shown = midi_shown = home_note = ~0u;
        dirty_home = 1;
        if (fm1_crash.magic == FM1_CRASH_MAGIC)
            pn_text(150, 210, "LAST RUN CRASHED", 1, YELLOW, BLACK);
    }
    if (dirty_home) {
        dirty_home = 0;
        p = put_s(t, "WAVE ");
        p = put_s(p, SYN_WAVE_NAME[syn.wave]);
        p = put_s(p, "  CUTOFF ");
        p = put_u(p, syn.cutoff);
        pn_field(8, 98, 224, t, 1, WHITE, BLACK);
        p = put_s(t, "LEVEL ");
        p = put_u(p, (uint32_t)gain * 100u / 32767u);
        p = put_s(p, "%  OCTAVE ");
        p = put_i(p, octave);
        pn_field(8, 108, 224, t, 1, WHITE, BLACK);
        p = put_s(t, "TONE ");
        p = put_s(p, syn.tone ? "ON (440 HZ)" : "OFF");
        p = put_s(p, audio_on ? "" : "  AUDIO ERROR ");
        if (!audio_on)
            p = put_i(p, audio_err);
        pn_field(8, 118, 224, t, 1, audio_on ? WHITE : RED, BLACK);
        if (syn.gate) {
            p = put_s(t, "NOTE ");
            midi_note_name(syn.note, p);
        } else {
            put_s(t, "NOTE -");
        }
        pn_field(8, 128, 224, t, 1, WHITE, BLACK);
    }
    if (usb_shown != usb.config) {
        usb_shown = usb.config;
        pn_field(8, 158, 224, usb.config ? "USB CONNECTED" : "USB NOT CONNECTED", 1, usb.config ? GREEN : ORANGE, BLACK);
    }
    if (midi_shown != midi_in_n + midi_out_n + midi_drop) {
        midi_shown = midi_in_n + midi_out_n + midi_drop;
        p = put_s(t, "IN ");
        p = put_u(p, midi_in_n);
#ifdef LB_HAS_TRS
        p = put_s(p, " (TRS ");
        p = put_u(p, midi_trs_n);
        p = put_s(p, ")");
#endif
        p = put_s(p, "  OUT ");
        p = put_u(p, midi_out_n);
        if (midi_drop) {
            p = put_s(p, "  NOT SENT ");
            p = put_u(p, midi_drop);
        }
        pn_field(8, 168, 224, t, 1, WHITE, BLACK);
        p = put_s(t, "LAST IN ");
        if (midi_in_n)
            midi_text(midi_last_in, p);
        else
            put_s(p, "-");
        pn_field(8, 178, 224, t, 1, WHITE, BLACK);
    }
}

/* the TEST page's layout */
#define T_BTN_Y 16u
#define T_BTN_W 39u
#define T_BTN_H 14u
#define T_BTN_ROWS ((LB_NBTN + 5u) / 6u)
#define T_KEY_Y (T_BTN_Y + T_BTN_ROWS * (T_BTN_H + 2u) + 2u)
#define T_KEY_H (LB_NKEY ? 14u : 0u)
#define T_ENC_Y (T_KEY_Y + T_KEY_H + (LB_NKEY ? 4u : 0u))
#define T_ENC_W 29u
#define T_PAD_Y (T_ENC_Y + 22u)
#define T_PAD_H (LB_NPAD ? 22u : 0u)
#define T_AN_Y (T_PAD_Y + T_PAD_H)
#define T_AN_ROW 11u
#define T_STATUS_Y 230u
_Static_assert(T_AN_Y + LB_NAN * T_AN_ROW <= T_STATUS_Y, "the TEST page fits");

static uint32_t t_btn = ~0u, t_pad = ~0u;
static uint64_t t_key = ~0ull;
static int32_t t_enc[LB_NENC > 0 ? LB_NENC : 1], t_an[LB_NAN > 0 ? LB_NAN : 1];
static uint32_t t_status_at;

static void test_btn(uint32_t i, int on)
{
    uint32_t x = (i % 6u) * (T_BTN_W + 1u), y = T_BTN_Y + (i / 6u) * (T_BTN_H + 2u);
    uint16_t bg = on ? WHITE : DIM, fg = on ? BLACK : GREY;
    pn_fill(x, y, T_BTN_W, T_BTN_H, bg);
    pn_text(x + 2u, y + 4u, LB_BTN_NAME[i], 1, fg, bg);
}

static void test_draw(int full)
{
    uint32_t i;
    char t[48], *p;
    if (full) {
        static const uint16_t BARS[6] = {RED, GREEN, BLUE, WHITE, YELLOW, CYAN};
        pn_fill(0, 0, 240, 240, BLACK);
        for (i = 0; i < 6u; i++)
            pn_fill(i * 40u, 0, 40, 12, BARS[i]);
        pn_fill(0, 0, 3, 3, MAGENTA);       /* the top-left corner: the panel's orientation */
        t_btn = ~0u;
        t_pad = ~0u;
        t_key = ~0ull;
        for (i = 0; i < LB_NENC; i++) {
            t_enc[i] = 0x7FFFFFFF;
            pn_text(i * (T_ENC_W + 1u) + 1u, T_ENC_Y, LB_ENC_NAME[i], 1, CYAN, BLACK);
        }
        for (i = 0; i < LB_NAN; i++) {
            t_an[i] = 0x7FFFFFFF;
            pn_text(2, T_AN_Y + i * T_AN_ROW + 1u, LB_AN_NAME[i], 1, CYAN, BLACK);
        }
        if (LB_NKEY == 0u && LB_NPAD == 0u)
            pn_text(2, T_AN_Y + LB_NAN * T_AN_ROW + 6u, "NO KEYS OR PADS ON THIS BOARD", 1, GREY, BLACK);
        t_status_at = 0;
    }
    {
        uint32_t b = lb_buttons();
        if (b != t_btn) {
            for (i = 0; i < LB_NBTN; i++)
                if ((b ^ t_btn) >> i & 1u)
                    test_btn(i, (int)(b >> i & 1u));
            t_btn = b;
        }
    }
    if (LB_NKEY) {
        uint64_t k = lb_keys();
        if (k != t_key) {
            uint32_t w = 240u / (LB_NKEY ? LB_NKEY : 1u);
            for (i = 0; i < LB_NKEY; i++)
                if ((k ^ t_key) >> i & 1u) {
                    static const uint8_t BLACKKEY[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
                    uint32_t on = (uint32_t)(k >> i) & 1u, bk = BLACKKEY[(LB_KEY_NOTE0 + i) % 12u];
                    pn_fill(i * w, T_KEY_Y, w - 1u, T_KEY_H, on ? CYAN : bk ? DIM : GREY);
                }
            t_key = k;
        }
    }
    for (i = 0; i < LB_NENC; i++)
        if (enc_pos[i] != t_enc[i]) {
            t_enc[i] = enc_pos[i];
            put_i(t, enc_pos[i]);
            pn_field(i * (T_ENC_W + 1u) + 1u, T_ENC_Y + 10u, T_ENC_W - 1u, t, 1, WHITE, BLACK);
        }
    if (LB_NPAD) {
        uint32_t pd = lb_pads();
        if (pd != t_pad) {
            for (i = 0; i < LB_NPAD; i++)
                if ((pd ^ t_pad) >> i & 1u) {
                    uint32_t x = (i % 8u) * 30u, y = T_PAD_Y + (i / 8u) * 10u;
                    pn_fill(x, y, 28, 9, pd >> i & 1u ? MAGENTA : DIM);
                }
            t_pad = pd;
        }
    }
    for (i = 0; i < LB_NAN; i++) {
        int32_t v = lb_analog(i);
        if (v != t_an[i]) {
            uint32_t y = T_AN_Y + i * T_AN_ROW;
            t_an[i] = v;
            pn_bar(44, y, 150, 9, v, 1023, GREEN, DIM);
            if (v < 0)
                put_s(t, "--");
            else
                put_u(t, (uint32_t)v);
            pn_field(198, y + 1u, 40, t, 1, WHITE, BLACK);
        }
    }
    if (ms - t_status_at >= 250u) {         /* USB, audio, MIDI, the tone and the jack */
        t_status_at = ms;
        p = put_s(t, usb.config ? "USB:ON " : "USB:-- ");
        p = put_s(p, audio_on ? "AUD:ON " : "AUD:-- ");
        p = put_s(p, "MI ");
        p = put_u(p, midi_in_n);
        p = put_s(p, " MO ");
        p = put_u(p, midi_out_n);
        if (midi_drop) {
            p = put_s(p, "/");
            p = put_u(p, midi_drop);
        }
        p = put_s(p, syn.tone ? " TONE" : "");
#if LB_HAS_JACK
        p = put_s(p, lb_jack() ? " HP" : "");
#endif
        pn_field(0, T_STATUS_Y, 240, t, 1, YELLOW, NAVY);
    }
}

/* ---- the console ---- */
static void status_line(void)
{
    char t[160], *p = t;
    uint32_t i;
    p = put_s(p, "ms=");     p = put_u(p, ms);
    p = put_s(p, " page=");  p = put_u(p, page);
    p = put_s(p, " btn=");   p = put_hex(p, lb_buttons(), 8);
    p = put_s(p, " keys=");  p = put_hex(p, (uint32_t)(lb_keys() >> 32), 2); p = put_hex(p, (uint32_t)lb_keys(), 8);
    p = put_s(p, " enc=");
    for (i = 0; i < LB_NENC; i++) {
        p = put_i(p, enc_pos[i]);
        *p++ = i + 1u < LB_NENC ? ',' : ' ';
    }
    p = put_s(p, "an=");
    for (i = 0; i < LB_NAN; i++) {
        p = put_i(p, lb_analog(i));
        *p++ = i + 1u < LB_NAN ? ',' : ' ';
    }
    p = put_s(p, "usb=");    p = put_u(p, usb.config);
    p = put_s(p, " audio="); p = put_i(p, audio_on ? (int32_t)lb_audio_irqs : audio_err);
    p = put_s(p, " mi=");    p = put_u(p, midi_in_n);
    p = put_s(p, " mo=");    p = put_u(p, midi_out_n);
    p = put_s(p, " lcd_to="); p = put_u(p, lb_lcd_timeouts());
    put_s(p, "\r\n");
    con_puts(t);
}

static void command(char c)
{
    switch (c) {
    case 'h':
        con_puts("Lorcha " LORCHA_VERSION " on " LB_NAME ": h help, p status, s status every second, 1 HOME, 2 TEST, "
                 "t tone, w LED walk, n MIDI test note, c last crash, u UBOOT, r reboot\r\n");
        break;
    case 'p': status_line(); break;
    case 's': console_status ^= 1u; break;
    case '1': page = PAGE_HOME; break;
    case '2': page = PAGE_TEST; break;
    case 't': tone_set(!syn.tone); break;
    case 'w': walk_start = ms; walk_until = ms + LB_NLED * 80u + 1u; break;
    case 'n':
        midi_send(0x90, 60, 100);
        voice_on(60, 100);
        note_off_at = ms + 500u;
        break;
    case 'c': {
        char t[96], *p = t;
        if (fm1_crash.magic != FM1_CRASH_MAGIC) {
            con_puts("no crash on record\r\n");
            break;
        }
        p = put_s(p, "count="); p = put_u(p, fm1_crash.count);
        p = put_s(p, " vec=");  p = put_u(p, fm1_crash.vec);
        p = put_s(p, " pc=");   p = put_hex(p, fm1_crash.pc, 8);
        p = put_s(p, " rets="); p = put_hex(p, fm1_crash.rets, 8);
        put_s(p, "\r\n");
        con_puts(t);
        break;
    }
    case 'u':
        con_puts("UBOOT\r\n");
        fm1_delay_ms(30);
        usb_detach();
        fm1_enter_uboot();
        break;
    case 'r':
        con_puts("reboot\r\n");
        fm1_delay_ms(30);
        usb_detach();
        fm1_reboot();
        break;
    }
}

/* ---- main ---- */
static void app_main(void)
{
    uint32_t last_an = 0, last_led = 0, last_con = 0, healthy = 0;
    syn_init(&syn);
    flash_id = fl_jedec();
    flash_ok = LB_JEDEC_OK(flash_id);
    if (flash_ok)
        ota_boot_cleanup();                 /* what an interrupted update left */
    lb_init();
    pn_init();
    pn_text(60, 104, "LORCHA", 4, CREAM, BLACK);
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
#if LORCHA_EMU_PLL
    /* the emulator-only build (build_in.sh --emu: never a package for a unit): fm1-emu 0.4.0 reads PLL0 as zero, and
     * felucca-bsp's audio start refuses any clock but the one a bootloader leaves running; given those values here, only
     * when it reads zero (on a unit it never does), so the voice can be heard in a WAV */
    if (FM1_AUDIO_PLL_CON0 == 0u && FM1_AUDIO_PLL_CON1 == 0u) {
        FM1_AUDIO_PLL_CON0 = FM1_AUDIO_PLL_STOCK0;
        FM1_AUDIO_PLL_CON1 = FM1_AUDIO_PLL_STOCK1;
    }
#endif
    audio_err = lb_audio_start();
    audio_on = audio_err == 0;
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    for (;;) {
        uint32_t now = ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req)                  /* the SysEx soft key */
            command('u');
        ota_service();                      /* the installer's handshake */
        if (usb.ota_req) {                  /* its update: commits and resets, or comes back */
            usb.ota_req = 0;
            voice_all_off();
            tone_set(0);
            if (flash_ok)
                ota_session();
            page_drawn = 0xFF;
        }
        if (!healthy && now > 2000u) {      /* it runs: the next boot is no loop */
            healthy = 1;
            boot.pending = 0;
            boot.failed = 0;
        }
        buttons();
        keys();
        pads();
        encoders();
        midi_in();
        if (note_off_at && (int32_t)(now - note_off_at) >= 0) {
            note_off_at = 0;
            midi_send(0x80, 60, 0);
            voice_off(60);
        }
        if (now - last_an >= 40u) {
            last_an = now;
            lb_analog_poll();
            level();
        }
        if (now - last_led >= 20u) {
            last_led = now;
            leds();
        }
        if (page != page_drawn) {
            page_drawn = page;
            if (page == PAGE_HOME)
                home_draw(1);
            else
                test_draw(1);
        } else if (page == PAGE_HOME) {
            home_draw(0);
        } else {
            test_draw(0);
        }
        while (ci_r != ci_w) {
            char c = (char)cdc_in[ci_r % CI_N];
            ci_r++;
            command(c);
        }
        if (console_status && now - last_con >= 1000u) {
            last_con = now;
            status_line();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    if (boot.magic != BOOT_MAGIC || boot.pending > 1u || boot.failed > 1u) {
        boot.magic = BOOT_MAGIC;
        boot.pending = 0;
        boot.failed = 0;
    }
    if (boot.pending)
        boot.failed++;
    boot.pending = 1;
    if (boot.failed >= 2u) {                /* two boots in a row went nowhere: UBOOT, so it can be replaced */
        boot.pending = 0;
        boot.failed = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)    /* the flash driver runs from RAM */
        *d = *s;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;)
        ;
}
