/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* src/font.h on the host: every character has a glyph inside its 5 x 7 cell, lower case maps to upper case, the rest has
 * none; and every character of the strings given on the command line (test_sources.py passes the firmware's) is drawable. */
#include <stdio.h>
#include "font.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    unsigned c, r;
    int i;
    for (c = FONT_FIRST; c <= FONT_LAST; c++) {
        const uint8_t *g = font_glyph((char)c);
        unsigned lit = 0;
        CHECK(g != 0, "'%c' has no glyph", c);
        if (!g)
            continue;
        for (r = 0; r < FONT_H; r++) {
            CHECK(g[r] < 0x20u, "'%c' row %u wider than 5", c, r);
            lit |= g[r];
        }
        CHECK(c == ' ' || lit, "'%c' is blank", c);
    }
    for (c = 'a'; c <= 'z'; c++)
        CHECK(font_glyph((char)c) == font_glyph((char)(c - 32u)), "'%c' as upper case", c);
    CHECK(!font_glyph('{') && !font_glyph('~') && !font_glyph('\n') && !font_glyph((char)0xC3), "no glyph outside the set");
    /* distinct glyphs: no two characters drawn alike (but for the space) */
    for (c = FONT_FIRST + 1u; c <= FONT_LAST; c++) {
        unsigned d;
        for (d = c + 1u; d <= FONT_LAST; d++) {
            const uint8_t *a = font_glyph((char)c), *b = font_glyph((char)d);
            int same = 1;
            for (r = 0; r < FONT_H; r++)
                same &= a[r] == b[r];
            CHECK(!same, "'%c' and '%c' look the same", c, d);
        }
    }
    for (i = 1; i < argc; i++) {
        const char *p;
        for (p = argv[i]; *p; p++)
            CHECK(font_glyph(*p) != 0, "\"%s\": '%c' cannot be drawn", argv[i], *p);
    }
    printf("font_test: %s (%d strings)\n", fails ? "FAILED" : "ok", argc - 1);
    return fails != 0;
}
