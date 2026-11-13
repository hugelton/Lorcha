/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* src/synth.h on the host: pitch, the note stack, the envelope, the tone, no overflow. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "synth.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const double RATE = 24000000.0 / 544.0;

static double freq_of(uint32_t inc) { return inc * RATE / 4294967296.0; }

/* the pitch of what syn_render makes, by rising zero crossings over n frames */
static double measure(syn_t *s, uint32_t n)
{
    static int32_t buf[2 * 64];
    uint32_t done = 0, cross = 0, i;
    int32_t prev = 0;
    while (done < n) {
        syn_render(s, buf, 64, 32767);
        for (i = 0; i < 64; i++) {
            int32_t v = buf[2 * i];
            if (prev < 0 && v >= 0)
                cross++;
            prev = v;
        }
        done += 64;
    }
    return cross * RATE / done;
}

int main(void)
{
    syn_t s;
    uint32_t n, i;
    int32_t buf[2 * 64];

    /* the pitch table: A4 is 440 Hz, each note a semitone, every MIDI note within 0.1 % */
    for (n = 0; n < 128; n++) {
        double want = 440.0 * pow(2.0, (n - 69.0) / 12.0), got = freq_of(syn_inc(n));
        CHECK(fabs(got / want - 1.0) < (n < 12 ? 0.01 : 0.001), "note %u: %.3f Hz, want %.3f", n, got, want);
    }
    CHECK(fabs(freq_of(SYN_TONE_INC) - 440.0) < 0.01, "the tone's increment: %.3f Hz", freq_of(SYN_TONE_INC));

    /* the note stack: the last held sounds; let go, the one before comes back; all off */
    syn_init(&s);
    syn_note_on(&s, 60, 100);
    syn_note_on(&s, 64, 90);
    CHECK(s.note == 64 && s.gate, "last note priority");
    syn_note_off(&s, 64);
    CHECK(s.note == 60 && s.gate && s.level == 100, "back to the note held before");
    syn_note_off(&s, 60);
    CHECK(!s.gate && s.n == 0, "all let go");
    for (n = 0; n < 40; n++)
        syn_note_on(&s, (uint8_t)(30 + n), 64);
    CHECK(s.n == SYN_STACK && s.note == 69, "a full stack drops the oldest (n %u, note %u)", s.n, s.note);
    syn_note_on(&s, 60, 0);
    CHECK(s.level == 1, "velocity 0 on: taken as 1");
    syn_all_off(&s);
    CHECK(!s.gate && !s.n, "all off");

    /* the envelope: full within ~6 ms, silent within ~220 ms of the release */
    syn_init(&s);
    syn_note_on(&s, 69, 127);
    for (n = 0; n < 300; n += 64)
        syn_render(&s, buf, 64, 32767);
    CHECK(s.env == 1 << 24, "attack done after ~6.5 ms: %d", (int)s.env);
    syn_note_off(&s, 69);
    for (n = 0; n < (uint32_t)(0.22 * RATE); n += 64)
        syn_render(&s, buf, 64, 32767);
    CHECK(s.env == 0, "release done after 220 ms: %d", (int)s.env);
    syn_render(&s, buf, 64, 32767);
    for (i = 0; i < 128; i++)
        CHECK(buf[i] == 0, "silence after the release");

    /* the voice's pitch through the filter, every wave; no sample past Q23 at full gain */
    for (n = 0; n < SYN_NWAVE; n++) {
        double f;
        syn_init(&s);
        s.wave = (uint8_t)n;
        s.cutoff = 127;
        syn_note_on(&s, 69, 127);
        f = measure(&s, (uint32_t)RATE);
        CHECK(fabs(f - 440.0) < 3.0, "%s: %.1f Hz, want 440", SYN_WAVE_NAME[n], f);
    }
    for (n = 0; n < SYN_NWAVE; n++) {
        int32_t peak = 0;
        syn_init(&s);
        s.wave = (uint8_t)n;
        s.cutoff = 127;
        s.tone = 1;
        syn_note_on(&s, 100, 127);
        for (i = 0; i < 2000; i++) {
            uint32_t k;
            syn_render(&s, buf, 64, 32767);
            for (k = 0; k < 128; k++) {
                int32_t a = buf[k] < 0 ? -buf[k] : buf[k];
                if (a > peak)
                    peak = a;
                CHECK(buf[2 * (k / 2)] == buf[2 * (k / 2) + 1], "left == right");
            }
        }
        CHECK(peak <= 8388607 / 2 + 128, "%s and the tone: peak %d over -6 dBFS", SYN_WAVE_NAME[n], (int)peak);
        CHECK(peak > 8388607 / 8, "%s and the tone: peak %d (too quiet)", SYN_WAVE_NAME[n], (int)peak);
    }

    /* the tone alone: 440 Hz at -12 dBFS of the voice's range (Q15 >> 2), then -6 dBFS of Q23 */
    syn_init(&s);
    s.tone = 1;
    {
        double f = measure(&s, (uint32_t)RATE);
        int32_t peak = 0;
        CHECK(fabs(f - 440.0) < 2.0, "the tone: %.1f Hz", f);
        for (i = 0; i < 100; i++) {
            uint32_t k;
            syn_render(&s, buf, 64, 32767);
            for (k = 0; k < 128; k++)
                peak = buf[k] > peak ? buf[k] : peak;
        }
        CHECK(abs(peak - (8191 << 7)) < (64 << 7), "the tone's peak: %d", (int)peak);
    }
    /* gain 0: silence */
    syn_init(&s);
    s.tone = 1;
    syn_note_on(&s, 60, 127);
    syn_render(&s, buf, 64, 0);
    for (i = 0; i < 128; i++)
        CHECK(buf[i] == 0, "gain 0");

    printf("synth_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
