/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* src/tone.h on the host: 440 Hz, -12 dBFS, left == right, silence when off. */
#include <math.h>
#include <stdio.h>
#include "tone.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    const double rate = 24000000.0 / 544.0;
    static int32_t buf[2 * 64];
    uint32_t ph = 0, n, i, cross = 0;
    int32_t prev = 0, peak = 0;
    CHECK(fabs(TONE_INC * rate / 4294967296.0 - 440.0) < 0.01, "the increment");
    for (n = 0; n < (uint32_t)rate; n += 64) {
        tone_render(&ph, 1, buf, 64);
        for (i = 0; i < 64; i++) {
            int32_t v = buf[2 * i];
            CHECK(v == buf[2 * i + 1], "left == right");
            cross += prev < 0 && v >= 0;
            prev = v;
            peak = v > peak ? v : peak;
        }
    }
    CHECK(fabs(cross * rate / n - 440.0) < 2.0, "pitch %.1f", cross * rate / n);
    CHECK(fabs(peak / 8388608.0 - 0.25) < 0.01, "peak %.3f of full scale (-12 dBFS: 0.25)", peak / 8388608.0);
    tone_render(&ph, 0, buf, 64);
    for (i = 0; i < 128; i++)
        CHECK(buf[i] == 0, "off: silence");
    printf("tone_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
