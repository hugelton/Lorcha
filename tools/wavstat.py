#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Level and pitch of a WAV, every 100 ms: wavstat.py IN.wav. The pitch by rising zero crossings (a rough check: a saw or a
sine reads near its fundamental). Standard library only."""
import math
import struct
import sys
import wave


def main(path):
    w = wave.open(path)
    n, ch, fr = w.getnframes(), w.getnchannels(), w.getframerate()
    s = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    left = s[0::ch]
    print(f"{path}: {fr} Hz, {ch} ch, {n / fr:.2f} s")
    step = fr // 10
    for i in range(0, n - step + 1, step):
        seg = left[i:i + step]
        rms = math.sqrt(sum(x * x for x in seg) / len(seg))
        zc = sum(1 for a, b in zip(seg, seg[1:]) if a < 0 <= b)
        db = 20 * math.log10(rms / 32768) if rms else -999
        print(f"{i / fr:5.1f} s  rms {rms:7.0f} ({db:6.1f} dBFS)  ~{zc * 10:5d} Hz")


if __name__ == "__main__":
    main(sys.argv[1])
