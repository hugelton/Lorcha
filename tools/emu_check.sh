#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Lorcha in fm1-emu (FM-1 and SMK-37 targets; the emulator has no Pocket AMP): a scripted run per board, its screen
# snapshots and a contact sheet, the audio as a WAV and a level / pitch table of it, the run's log.
#   FM1_EMU=/path/to/fm1-emu tools/emu_check.sh [fm1|smk37|all]      -> build/emu/<board>/
# Runs the -emu packages (tools/build.sh --emu): fm1-emu 0.4.0 reads the audio PLL as zero, which the release image's
# audio start (rightly) refuses; the release packages are run too, for the screens (their audio shows AUDIO ERROR -5).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${FM1_EMU:-/Volumes/SSD/MacData/ext/fm1-emu/fm1-emu-0.4.0-macos-arm64/fm1-emu}"
WHICH="${1:-all}"
[ -x "$EMU" ] || { echo "emu_check.sh: no fm1-emu at $EMU (FM1_EMU)" >&2; exit 1; }

run() {   # BOARD PACKAGE MS SNAPS KEYS
    d="$ROOT/build/emu/$1"; rm -rf "$d"; mkdir -p "$d/work"
    (cd "$d" && "$EMU" --fwsc "$2" --ms "$3" --wav "$1.wav" --snap "$4" ${5:+--keys "$5"} > run.log 2>&1)
    python3 "$ROOT/tools/sheet.py" "$d/sheet.png" "$d/work"
    python3 "$ROOT/tools/wavstat.py" "$d/$1.wav" > "$d/wav.txt"
    grep -E "^time|lcd frames|unknown MMIO" "$d/run.log" || true
    echo "$1: build/emu/$1/{sheet.png,$1.wav,wav.txt,run.log}"
}

# the FM-1: boot; the tone (PLAY on, off); a key, an encoder, a button, the MASTER pot; the LED levels (REC steps them)
if [ "$WHICH" = fm1 ] || [ "$WHICH" = all ]; then
    run fm1 "$ROOT/build/fm1/emu/lorcha-fm1-emu.fwsc" 5000 1000,1800,2900,3600,4800 \
        '1100:PLAY,2200:PLAY,2400-3200:n7,2500:enc0:+3,2600-3300:SCL,2700:pot:300,3400:REC,3500:REC,4600:REC'
    run fm1-release "$ROOT/build/fm1/lorcha-fm1.fwsc" 1500 1400 ''
fi
# the SMK-37 Pro (built, not the target): boot, the tone (SEQ PLAY), keys, an encoder, a button, the LED levels (SEQ REC)
if [ "$WHICH" = smk37 ] || [ "$WHICH" = all ]; then
    run smk37 "$ROOT/build/smk37pro/emu/lorcha-smk37pro-emu.fwsc" 4000 1800,3500 \
        '1100:SEQPLAY,2200:SEQPLAY,2400-3200:K7,2500:enc1:+3,2600-3300:SCALE,2800:SEQREC'
fi
