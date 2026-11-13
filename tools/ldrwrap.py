#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Wrap an update loader's raw RAM image as the package's ota.bin, the way the SPL loads it.

  ota.bin  0x00  head A (32 B): CRC16 of its own 30 bytes, CRC16 of everything from 0x20, offset 0x20,
                 length from 0x20, flags 0x41, name "usb_hid_ota.bin"
           0x20  head B (32 B): CRC16 of its own 30 bytes, CRC16 of the raw image, the raw length, the load
                 address (0x01C0A800), flags 0, the same name
           0x40  blocks: u32 compressed length, u32 raw length (at most 4 KiB), an LZ4 block; one after another

The layout was read off a working package's ota.bin (tests/test_tools.py checks a round trip on the image this
build makes). Each block is compressed on its own (no dictionary across blocks). Standard library only.

  ldrwrap.py wrap LOADER.raw OTA.bin       ldrwrap.py unwrap OTA.bin LOADER.raw
"""
import struct
import sys

NAME = b"usb_hid_ota.bin"
LOAD = 0x01C0A800
BLOCK = 0x1000
MAX_IMAGE = 0x14000          # loader/loader.ld: the loader's RAM
MARK = b"FELUCCA-LOADER-1"   # loader/crt0_ldr.S: the app's chip/ota.c looks for it in the first literals


def crc16(data, crc=0):
    """CRC16-CCITT, poly 0x1021, init 0 (JieLi's file and head checksums)"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def _len_bytes(n):
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return out


def lz4_block(src):
    """one LZ4 block (greedy, a 4-byte hash); the format's end rules kept: the last match ends at least 5 bytes
    before the end, and none starts in the last 12"""
    n = len(src)
    out = bytearray()
    table = {}
    anchor = i = 0
    limit = n - 12
    while i < limit:
        key = src[i:i + 4]
        cand = table.get(key)
        table[key] = i
        if cand is None or i - cand > 0xFFFF:
            i += 1
            continue
        m = 4
        while i + m < n - 5 and src[cand + m] == src[i + m]:
            m += 1
        lit = i - anchor
        token = (min(lit, 15) << 4) | min(m - 4, 15)
        out.append(token)
        if lit >= 15:
            out += _len_bytes(lit - 15)
        out += src[anchor:i]
        out += struct.pack("<H", i - cand)
        if m - 4 >= 15:
            out += _len_bytes(m - 4 - 15)
        i += m
        anchor = i
    lit = n - anchor
    out.append(min(lit, 15) << 4)
    if lit >= 15:
        out += _len_bytes(lit - 15)
    out += src[anchor:]
    return bytes(out)


def lz4_unblock(blk, raw_len):
    out = bytearray()
    i = 0
    while i < len(blk):
        t = blk[i]
        i += 1
        lit = t >> 4
        if lit == 15:
            while True:
                x = blk[i]
                i += 1
                lit += x
                if x != 255:
                    break
        out += blk[i:i + lit]
        i += lit
        if i >= len(blk):
            break
        off = blk[i] | blk[i + 1] << 8
        i += 2
        if off == 0 or off > len(out):
            raise ValueError("LZ4: bad offset")
        m = t & 15
        if m == 15:
            while True:
                x = blk[i]
                i += 1
                m += x
                if x != 255:
                    break
        for _ in range(m + 4):
            out.append(out[-off])
    if len(out) != raw_len:
        raise ValueError(f"LZ4: {len(out)} bytes, the head says {raw_len}")
    return bytes(out)


def head(dcrc, a, b, flags):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, flags, 0, 0, NAME + b"\0")
    return struct.pack("<H", crc16(body)) + body


def wrap(image):
    if not image or len(image) > MAX_IMAGE:
        raise SystemExit(f"ldrwrap: the loader is {len(image)} B (1 .. {MAX_IMAGE})")
    blocks = bytearray()
    for o in range(0, len(image), BLOCK):
        raw = image[o:o + BLOCK]
        c = lz4_block(raw)
        blocks += struct.pack("<II", len(c), len(raw)) + c
    rest = head(crc16(image), len(image), LOAD, 0x00) + blocks
    return head(crc16(rest), 0x20, len(rest), 0x41) + rest


def unwrap(pkg):
    """-> the raw image; checks both heads, every block"""
    def rd(at):
        hc, dc, a, b, fl = struct.unpack_from("<HHIIB", pkg, at)
        if crc16(pkg[at + 2:at + 32]) != hc or pkg[at + 16:at + 32].rstrip(b"\0") != NAME:
            raise ValueError(f"head at {at:#x}: bad")
        return dc, a, b, fl
    dca, off, ln, fla = rd(0)
    dcb, raw_len, load, _ = rd(0x20)
    if off != 0x20 or ln != len(pkg) - 0x20 or crc16(pkg[0x20:]) != dca or fla != 0x41 or load != LOAD:
        raise ValueError("head A / B: inconsistent")
    out, p = bytearray(), 0x40
    while p < len(pkg):
        cl, rl = struct.unpack_from("<II", pkg, p)
        p += 8
        if rl > BLOCK:
            raise ValueError("block over 4 KiB")
        out += lz4_unblock(pkg[p:p + cl], rl)
        p += cl
    if len(out) != raw_len or crc16(out) != dcb:
        raise ValueError("raw image: length or CRC")
    return bytes(out)


def has_mark(pkg):
    """as chip/ota.c recognises its own loader: the mark among the first block's first literals"""
    return MARK in pkg[0x48:0x48 + 64 + 16]


def main(argv):
    if len(argv) != 4 or argv[1] not in ("wrap", "unwrap"):
        raise SystemExit(__doc__)
    data = open(argv[2], "rb").read()
    out = wrap(data) if argv[1] == "wrap" else unwrap(data)
    open(argv[3], "wb").write(out)
    if argv[1] == "wrap":
        assert unwrap(out) == data
        print(f"{argv[3]}: {len(out)} B (loader {len(data)} B){'' if has_mark(out) else ', NO loader mark'}")


if __name__ == "__main__":
    main(sys.argv)
