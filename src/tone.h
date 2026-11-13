/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The test tone: a 440 Hz sine (a parabola each half cycle) at -12 dBFS, stereo Q23 at 24 MHz / 544 = 44,117.6 Hz (the FM-1's
 * and the Pocket AMP's I2S rate). Integer only. Host-tested (tests/tone_test.c). */
#pragma once
#include <stdint.h>

#define TONE_INC 42835140u                /* 440 Hz: 440 / 44117.647 x 2^32 */

/* frames of stereo Q23 (out[2i] left, out[2i+1] right) into out; on 0: silence */
static void tone_render(uint32_t *phase, int on, int32_t *out, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t v = 0;
        if (on) {
            int32_t x = (int32_t)(*phase >> 16), a;
            x = x < 32768 ? x : x - 65536;            /* -32768..32767, a half cycle each side */
            a = x < 0 ? -x : x;
            v = (x * (32768 - a)) / 8192;             /* 4 x (1 - |x|): -32768..32767 */
            v = v > 32767 ? 32767 : v;
            *phase += TONE_INC;
        }
        out[2u * i] = out[2u * i + 1u] = v * 64;      /* -12 dBFS of Q23 */
    }
}
