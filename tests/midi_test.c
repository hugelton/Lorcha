/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* src/midi.h on the host: packets out, the text of one. */
#include <stdio.h>
#include <string.h>
#include "midi.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    char t[16];
    CHECK(midi_pkt(0x90, 60, 100) == 0x643C9009u, "note on: %08x", midi_pkt(0x90, 60, 100));
    CHECK(midi_pkt(0xB0, 20, 200) == 0x4814B00Bu, "data masked: %08x", midi_pkt(0xB0, 20, 200));
    CHECK(midi_pkt(0x89, 36, 0) == 0x00248908u, "note off ch 10: %08x", midi_pkt(0x89, 36, 0));
    midi_text(midi_pkt(0x90, 0x3C, 0x64), t);
    CHECK(!strcmp(t, "90 3C 64"), "midi_text: %s", t);
    printf("midi_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
