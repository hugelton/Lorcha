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
    (cd "$d" && "$EMU" --fwsc "$2" --ms "$3" --wav "$1.wav" --snap "$4" --keys "$5" > run.log 2>&1)
    python3 "$ROOT/tools/sheet.py" "$d/sheet.png" "$d/work"
    python3 "$ROOT/tools/wavstat.py" "$d/$1.wav" > "$d/wav.txt"
    grep -E "^time|lcd frames|unknown MMIO" "$d/run.log" || true
    echo "$1: build/emu/$1/{sheet.png,$1.wav,wav.txt,run.log}"
}

# the FM-1: boot (HOME), MIDI into the TRS jack (C4 on, off), the test tone (PLAY on, off), the TEST page (HOME), a key, an
# encoder, a button, the MASTER pot, the LED walk (REC), back to HOME
if [ "$WHICH" = fm1 ] || [ "$WHICH" = all ]; then
    run fm1 "$ROOT/build/fm1/emu/lorcha-fm1-emu.fwsc" 7000 1500,2200,3100,3900,4500,5300,6500 \
        '1600:midi:903c64,2400:midi:803c00,2600:PLAY,3400:PLAY,3600:HOME,4000-4700:n7,4100:enc0:+3,4200-4800:SCL,4300:pot:300,5000:REC,6000:HOME'
    run fm1-release "$ROOT/build/fm1/lorcha-fm1.fwsc" 2600 2000,2500 '2100:HOME'
fi
# the SMK-37 Pro: boot, the tone (SEQ PLAY), the TEST page (PATCH), keys (velocity), encoders, the octave, the wave (SCALE),
# the walk (SEQ REC), back to HOME
if [ "$WHICH" = smk37 ] || [ "$WHICH" = all ]; then
    run smk37 "$ROOT/build/smk37pro/emu/lorcha-smk37pro-emu.fwsc" 7000 1500,3100,3900,4500,5300,6500 \
        '1600:SEQPLAY,2400:SEQPLAY,2600-3000:K12,3600:PATCH,4000-4700:K7,4100:enc1:+3,4200-4800:SCALE,5000:SEQREC,5800-6200:K20,6300:PATCH'
    run smk37-release "$ROOT/build/smk37pro/lorcha-smk37pro.fwsc" 2600 2000,2500 '2100:PATCH'
fi
