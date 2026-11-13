/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* USB-MIDI event packets (4 bytes, little-endian in a uint32: cable << 4 | CIN, status, data 1, data 2), in and out, and a
 * short text for the screen. Host-tested (tests/midi_test.c). */
#pragma once
#include <stdint.h>

enum { MIDI_NONE, MIDI_NOTE_ON, MIDI_NOTE_OFF, MIDI_CC, MIDI_PB, MIDI_OTHER };

typedef struct {
    uint8_t kind, ch, d1, d2;           /* ch 0..15 */
} midi_msg_t;

/* a channel message out, on cable 0 */
static uint32_t midi_pkt(uint8_t status, uint8_t d1, uint8_t d2)
{
    return (uint32_t)(status >> 4) | (uint32_t)status << 8 | (uint32_t)(d1 & 0x7Fu) << 16 | (uint32_t)(d2 & 0x7Fu) << 24;
}

/* what a packet from usb.c's ring is (channel messages and the clock's real-time bytes only reach it) */
static midi_msg_t midi_parse(uint32_t pkt)
{
    midi_msg_t m;
    uint32_t st = (pkt >> 8) & 0xFFu;
    m.ch = (uint8_t)(st & 15u);
    m.d1 = (uint8_t)((pkt >> 16) & 0x7Fu);
    m.d2 = (uint8_t)((pkt >> 24) & 0x7Fu);
    switch (st & 0xF0u) {
    case 0x90:
        m.kind = m.d2 ? MIDI_NOTE_ON : MIDI_NOTE_OFF;    /* note on at velocity 0 is a note off */
        break;
    case 0x80:
        m.kind = MIDI_NOTE_OFF;
        break;
    case 0xB0:
        m.kind = MIDI_CC;
        break;
    case 0xE0:
        m.kind = MIDI_PB;
        break;
    default:
        m.kind = st >= 0x80u ? MIDI_OTHER : MIDI_NONE;
        break;
    }
    return m;
}

/* a MIDI byte stream (the FM-1's TRS MIDI IN) to packets: channel messages with running status; SysEx and system common
 * end the running status and are dropped, real-time bytes are dropped */
typedef struct {
    uint8_t st, n, d[2];
} midi_rx_t;

static uint32_t midi_rx_byte(midi_rx_t *r, uint8_t b)   /* -> a packet, or 0 */
{
    uint32_t need;
    if (b >= 0xF8u)
        return 0;
    if (b >= 0xF0u) {
        r->st = 0;
        r->n = 0;
        return 0;
    }
    if (b & 0x80u) {
        r->st = b;
        r->n = 0;
        return 0;
    }
    if (!r->st)
        return 0;
    r->d[r->n++] = b;
    need = (r->st & 0xE0u) == 0xC0u ? 1u : 2u;          /* program change, channel pressure: one data byte */
    if (r->n < need)
        return 0;
    r->n = 0;
    return midi_pkt(r->st, r->d[0], need == 2u ? r->d[1] : 0u);
}

/* "90 3C 64" (the status and data bytes as they came: 8 characters and the end) */
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

/* "C4", "F#3" (MIDI 60 = C4) */
static void midi_note_name(uint32_t note, char *t)
{
    static const char N[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t oct = (int32_t)(note / 12u) - 1;
    uint32_t i = 0;
    t[i++] = N[note % 12u][0];
    if (N[note % 12u][1])
        t[i++] = N[note % 12u][1];
    if (oct < 0) {
        t[i++] = '-';
        oct = -oct;
    }
    t[i++] = (char)('0' + oct);
    t[i] = 0;
}
