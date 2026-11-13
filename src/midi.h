/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* USB-MIDI event packets (cable << 4 | CIN, status, data 1, data 2; little-endian in a uint32) out, and a short text of one
 * for the screen. Host-tested (tests/midi_test.c). */
#pragma once
#include <stdint.h>

/* a channel message on cable 0 */
static uint32_t midi_pkt(uint8_t status, uint8_t d1, uint8_t d2)
{
    return (uint32_t)(status >> 4) | (uint32_t)status << 8 | (uint32_t)(d1 & 0x7Fu) << 16 | (uint32_t)(d2 & 0x7Fu) << 24;
}

/* "90 3C 64": the packet's three bytes */
static void midi_text(uint32_t pkt, char *t)
{
    static const char H[] = "0123456789ABCDEF";
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        uint32_t b = (pkt >> (8u * (i + 1u))) & 0xFFu;
        t[3u * i] = H[b >> 4];
        t[3u * i + 1u] = H[b & 15u];
        t[3u * i + 2u] = i < 2u ? ' ' : 0;
    }
}
