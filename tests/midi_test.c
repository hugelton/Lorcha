/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* src/midi.h on the host: packets out, packets in, the byte stream parser, the texts. */
#include <stdio.h>
#include <string.h>
#include "midi.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* every packet a byte stream gives */
static int feed(const uint8_t *b, int n, uint32_t *out)
{
    midi_rx_t r;
    int i, k = 0;
    memset(&r, 0, sizeof r);
    for (i = 0; i < n; i++) {
        uint32_t p = midi_rx_byte(&r, b[i]);
        if (p)
            out[k++] = p;
    }
    return k;
}

int main(void)
{
    midi_msg_t m;
    uint32_t p[16];
    char t[16];

    /* out: cable 0, CIN = the status's high nibble, data masked to 7 bits */
    CHECK(midi_pkt(0x90, 60, 100) == 0x643C9009u, "note on: %08x", midi_pkt(0x90, 60, 100));
    CHECK(midi_pkt(0xB0, 20, 200) == 0x4814B00Bu, "CC, data masked: %08x", midi_pkt(0xB0, 20, 200));
    CHECK(midi_pkt(0x89, 36, 0) == 0x00248908u, "note off ch 10: %08x", midi_pkt(0x89, 36, 0));

    /* in */
    m = midi_parse(midi_pkt(0x93, 64, 1));
    CHECK(m.kind == MIDI_NOTE_ON && m.ch == 3 && m.d1 == 64 && m.d2 == 1, "note on");
    m = midi_parse(midi_pkt(0x90, 64, 0));
    CHECK(m.kind == MIDI_NOTE_OFF, "note on at velocity 0 is an off");
    m = midi_parse(midi_pkt(0x80, 64, 64));
    CHECK(m.kind == MIDI_NOTE_OFF, "note off");
    m = midi_parse(midi_pkt(0xBF, 74, 10));
    CHECK(m.kind == MIDI_CC && m.ch == 15 && m.d1 == 74 && m.d2 == 10, "CC");
    m = midi_parse(midi_pkt(0xE0, 0, 64));
    CHECK(m.kind == MIDI_PB, "pitch bend");
    m = midi_parse(0x0F | 0xF8u << 8);
    CHECK(m.kind == MIDI_OTHER, "clock");

    /* the byte stream: running status, real-time inside a message, SysEx ends the running status, one-byte messages */
    {
        static const uint8_t s1[] = {0x90, 60, 100, 62, 90, 0xF8, 64, 0xF8, 80};
        int n = feed(s1, sizeof s1, p);
        CHECK(n == 3 && p[0] == midi_pkt(0x90, 60, 100) && p[1] == midi_pkt(0x90, 62, 90) && p[2] == midi_pkt(0x90, 64, 80),
              "running status, real-time between (%d)", n);
    }
    {
        static const uint8_t s2[] = {0x90, 60, 0xF0, 1, 2, 3, 0xF7, 61, 62, 0xC5, 7, 9, 0x80, 60, 0};
        int n = feed(s2, sizeof s2, p);
        CHECK(n == 3 && p[0] == midi_pkt(0xC5, 7, 0) && p[1] == midi_pkt(0xC5, 9, 0) && p[2] == midi_pkt(0x80, 60, 0),
              "SysEx drops the half message and the running status; program change takes one byte (%d)", n);
    }
    {
        static const uint8_t s3[] = {60, 100, 0x40};
        CHECK(feed(s3, sizeof s3, p) == 0, "data before any status: nothing");
    }

    /* texts */
    midi_text(midi_pkt(0x90, 0x3C, 0x64), t);
    CHECK(!strcmp(t, "90 3C 64"), "midi_text: %s", t);
    midi_note_name(60, t);
    CHECK(!strcmp(t, "C4"), "60: %s", t);
    midi_note_name(66, t);
    CHECK(!strcmp(t, "F#4"), "66: %s", t);
    midi_note_name(0, t);
    CHECK(!strcmp(t, "C-1"), "0: %s", t);
    midi_note_name(127, t);
    CHECK(!strcmp(t, "G9"), "127: %s", t);

    printf("midi_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
