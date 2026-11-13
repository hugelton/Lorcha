#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The app image from the linker's sections: .text runs in place from XIP (offset 0 = 0x02000120); .ram_text and .data
follow at their load addresses (fm1_cstart copies them to RAM).
  mkbin.py OBJDIR OUT.bin     (OBJDIR: text.bin, data.bin, rt.bin, syms.txt from objdump -t)"""
import re
import sys

XIP = 0x02000120


def sym(syms, name):
    m = re.search(r"^([0-9a-f]+) .*\s" + re.escape(name) + "$", syms, re.M)
    if not m:
        raise SystemExit(f"mkbin: no symbol {name}")
    return int(m.group(1), 16)


def main(objdir, dest):
    syms = open(f"{objdir}/syms.txt").read()
    if sym(syms, "_start") != XIP:
        raise SystemExit(f"mkbin: _start at {sym(syms, '_start'):#x}, not {XIP:#x}")
    img = bytearray(open(f"{objdir}/text.bin", "rb").read())
    parts = [(sym(syms, "_rt_load"), open(f"{objdir}/rt.bin", "rb").read()),
             (sym(syms, "_data_load"), open(f"{objdir}/data.bin", "rb").read())]
    for at, part in sorted(parts):
        if part:
            if at - XIP < len(img):
                raise SystemExit("mkbin: sections overlap")
            img += b"\xff" * (at - XIP - len(img)) + part
    img += b"\xff" * (-len(img) % 4)
    open(dest, "wb").write(img)
    print(f"{dest.rsplit('/', 1)[-1]}: {len(img)} bytes")


if __name__ == "__main__":
    main(*sys.argv[1:])
