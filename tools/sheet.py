#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""A contact sheet of fm1-emu's screen snapshots: sheet.py OUT.png WORKDIR (its lcd_<ms>.bmp, in time order). Needs Pillow."""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def main(out, work):
    shots = sorted(((int(m.group(1)), p) for p in Path(work).glob("lcd_*.bmp")
                    if (m := re.match(r"lcd_(\d+)\.bmp$", p.name))), key=lambda t: t[0])
    if not shots:
        raise SystemExit("sheet.py: no lcd_<ms>.bmp")
    cols = min(3, len(shots))
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 250, rows * 264), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (t, p) in enumerate(shots):
        x, y = (i % cols) * 250 + 5, (i // cols) * 264 + 5
        sheet.paste(Image.open(p).convert("RGB").resize((240, 240)), (x, y))
        d.text((x, y + 243), f"{t} ms", fill=(255, 255, 255))
    sheet.save(out)
    print(f"{out}: {len(shots)} snapshots")


if __name__ == "__main__":
    main(*sys.argv[1:])
