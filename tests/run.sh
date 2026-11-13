#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Lorcha's host tests: the tone, MIDI and the font in C (host cc), the tools and the sources in Python.
#   tests/run.sh          (CC to pick the compiler; AC79_SDK for the package tests)
set -eu
cd "$(dirname "$0")/.."
O=build/host
mkdir -p "$O"
CC="${CC:-cc}"
FLAGS="-std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=undefined,address -fno-sanitize-recover=all -Isrc"
for t in tone midi font; do
    # shellcheck disable=SC2086
    $CC $FLAGS "tests/${t}_test.c" -o "$O/${t}_test" -lm
done
"$O/tone_test"
"$O/midi_test"
"$O/font_test"
python3 -m unittest discover -s tests -p "test_*.py"
echo "tests: ok"
