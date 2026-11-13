/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A small test voice: one oscillator (saw, square, triangle, sine), a one-pole low-pass, an attack / release envelope,
 * mono with last-note priority; and a separate 440 Hz test tone at -12 dBFS. Integer only, 32-bit math only (no 64-bit
 * multiplies in the audio interrupt: felucca-bsp docs/CPU.md). Host-tested (tests/synth_test.c).
 *
 *   syn_note_on(&s, note, vel), syn_note_off(&s, note), syn_all_off(&s)
 *   syn_render(&s, out, frames, gain)   stereo Q23 (out[2i] = L, out[2i+1] = R), gain Q15
 * The rate is the FM-1's I2S clock, 24 MHz / 544 = 44,117.6 Hz; the Pocket AMP's is the same. */
#pragma once
#include <stdint.h>

#define SYN_RATE_NUM 24000000u
#define SYN_RATE_DEN 544u
#define SYN_STACK 16u
#define SYN_ATTACK_STEP 76260u        /* Q24 full scale over ~5 ms */
#define SYN_RELEASE_STEP 1901u        /* ~200 ms */
#define SYN_TONE_INC 42835140u        /* 440 Hz */
enum { SYN_SAW, SYN_SQUARE, SYN_TRI, SYN_SINE, SYN_NWAVE };
static const char *const SYN_WAVE_NAME[SYN_NWAVE] = {"SAW", "SQUARE", "TRI", "SINE"};

/* phase increments of notes 120..131 (C9..B9); a lower octave halves them */
static const uint32_t SYN_INC_TOP[12] = {815037662u, 863502323u, 914848843u, 969248586u, 1026883106u, 1087944753u,
                                         1152637315u, 1221176696u, 1293791642u, 1370724496u, 1452232016u, 1538586225u};

typedef struct {
    uint32_t phase, inc, tone_phase;
    int32_t env;                      /* Q24, 0 .. 1 << 24 */
    int32_t lp;                       /* the filter's state, Q15 */
    uint8_t stack[SYN_STACK], vel[SYN_STACK], n;   /* held notes, the last on top */
    uint8_t wave, cutoff;             /* cutoff 0..127 */
    uint8_t tone;                     /* the 440 Hz test tone on */
    uint8_t note, gate, level;        /* the sounding note, whether it is held, its velocity */
} syn_t;

static uint32_t syn_inc(uint32_t note)
{
    uint32_t oct;
    if (note > 131u)
        note = 131u;
    oct = 10u - note / 12u;           /* octaves below the top one (notes 120..131) */
    return SYN_INC_TOP[note % 12u] >> oct;
}

static void syn_init(syn_t *s)
{
    uint32_t i;
    uint8_t *p = (uint8_t *)s;
    for (i = 0; i < sizeof *s; i++)
        p[i] = 0;
    s->cutoff = 96;
}

static void syn__sound(syn_t *s, uint8_t note, uint8_t vel)
{
    s->note = note;
    s->level = vel;
    s->inc = syn_inc(note);
    s->gate = 1;
}

static void syn_note_on(syn_t *s, uint8_t note, uint8_t vel)
{
    uint32_t i, j;
    if (!vel)
        vel = 1;
    for (i = 0; i < s->n; i++)        /* again: to the top */
        if (s->stack[i] == note) {
            for (j = i; j + 1u < s->n; j++) {
                s->stack[j] = s->stack[j + 1u];
                s->vel[j] = s->vel[j + 1u];
            }
            s->n--;
            break;
        }
    if (s->n == SYN_STACK) {          /* full: the oldest goes */
        for (j = 0; j + 1u < s->n; j++) {
            s->stack[j] = s->stack[j + 1u];
            s->vel[j] = s->vel[j + 1u];
        }
        s->n--;
    }
    s->stack[s->n] = note;
    s->vel[s->n] = vel;
    s->n++;
    syn__sound(s, note, vel);
}

static void syn_note_off(syn_t *s, uint8_t note)
{
    uint32_t i, j;
    for (i = 0; i < s->n; i++)
        if (s->stack[i] == note) {
            for (j = i; j + 1u < s->n; j++) {
                s->stack[j] = s->stack[j + 1u];
                s->vel[j] = s->vel[j + 1u];
            }
            s->n--;
            break;
        }
    if (s->n)
        syn__sound(s, s->stack[s->n - 1u], s->vel[s->n - 1u]);   /* back to the one held before */
    else
        s->gate = 0;
}

static void syn_all_off(syn_t *s)
{
    s->n = 0;
    s->gate = 0;
}

/* -32768..32767 from the phase */
static int32_t syn_osc(uint32_t ph, uint32_t wave)
{
    int32_t x = (int32_t)(ph >> 16) - 32768;          /* the saw, -32768..32767 */
    switch (wave) {
    case SYN_SQUARE:
        return ph < 0x80000000u ? 24000 : -24000;
    case SYN_TRI:
        x = x < 0 ? -x : x;                           /* 0..32768 */
        return 2 * x - 32768 > 32767 ? 32767 : 2 * x - 32768;
    case SYN_SINE: {
        int32_t a, y;
        x = (int32_t)(ph >> 16);                      /* 0..65535: one cycle */
        x = x < 32768 ? x : x - 65536;                /* -32768..32767: a half cycle each side */
        a = x < 0 ? -x : x;
        y = (x * (32768 - a)) >> 13;                  /* 4 x (1 - |x|): a parabola each half */
        return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
    }
    default:
        return x;
    }
}

/* the test tone: a sine (as syn_osc), -12 dBFS */
static int32_t syn_tone_sample(syn_t *s)
{
    int32_t v = syn_osc(s->tone_phase, SYN_SINE) >> 2;
    s->tone_phase += SYN_TONE_INC;
    return v;
}

/* frames of stereo Q23 into out; gain Q15 (0..32767) */
static void syn_render(syn_t *s, int32_t *out, uint32_t frames, int32_t gain)
{
    uint32_t i;
    int32_t k = 64 + (int32_t)s->cutoff * 31;         /* the filter's coefficient, Q12: ~1.5 % .. ~98 % */
    int32_t vel = 128 + s->level;                     /* 128..255 of 256: velocity counts half */
    for (i = 0; i < frames; i++) {
        int32_t v, e;
        if (s->gate) {
            s->env += SYN_ATTACK_STEP;
            if (s->env > (1 << 24))
                s->env = 1 << 24;
        } else if (s->env > 0) {
            s->env -= SYN_RELEASE_STEP;
            if (s->env < 0)
                s->env = 0;
        }
        v = 0;
        if (s->env > 0) {
            v = syn_osc(s->phase, s->wave);
            s->phase += s->inc;
            s->lp += ((v - s->lp) * k) >> 12;
            e = s->env >> 9;                          /* Q15 */
            v = (s->lp * e) >> 15;
            v = (v * vel) >> 8;
        } else {
            s->lp = 0;
        }
        if (s->tone)
            v += syn_tone_sample(s);
        v = (v * gain) >> 15;
        if (v > 32767)
            v = 32767;
        if (v < -32768)
            v = -32768;
        out[2u * i] = out[2u * i + 1u] = v * 128;     /* Q15 -> Q23 at -6 dBFS (a multiply: v may be negative) */
    }
}
