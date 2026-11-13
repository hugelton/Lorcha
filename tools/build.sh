#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Lorcha for one board or all: build/<board>/lorcha-<board>.fwsc (and the app's .bin, the loader, the map).
#   tools/build.sh [--board fm1|smk37|pocketamp|all] [--model pro|elite|both]   (the SMK-37's model; default both)
# The JieLi toolchain is Linux x86-64: this runs tools/build_in.sh in Docker (linux/amd64), with
#   JIELI_TOOLCHAIN (default ~/.jieli/toolchain) and AC79_SDK (default ~/.jieli/ac79-sdk) mounted read-only.
# The sources go into the container as a tar stream and build/ comes back the same way, so the repository may sit outside
# what the Docker VM mounts (colima mounts only $HOME by default); the toolchain and the SDK are mounted.
# Inside a Linux x86-64 shell with the toolchain, run tools/build_in.sh directly (same arguments, the two variables set).
set -eu
set -o pipefail 2>/dev/null || true      # (a failed build must fail the pipe below)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
SDK="${AC79_SDK:-$HOME/.jieli/ac79-sdk}"
IMAGE="${LORCHA_IMAGE:-lorcha-jl:2}"
[ -x "$TC/pi32v2/bin/clang" ] || { echo "build.sh: no JieLi toolchain at $TC (JIELI_TOOLCHAIN)" >&2; exit 1; }
[ -f "$SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no AC79 SDK at $SDK (AC79_SDK; bsp/tools/get_sdk.sh)" >&2; exit 1; }
[ -f "$ROOT/bsp/chip/ota.c" ] || { echo "build.sh: bsp/ is empty: git submodule update --init" >&2; exit 1; }
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$IMAGE" "$ROOT/tools/docker"
fi
mkdir -p "$ROOT/build"
tar -C "$ROOT" -cf - --exclude .git --exclude build --exclude __pycache__ src tools patches bsp pendel-bsp |
    docker run --rm -i --platform linux/amd64 -v "$TC:/tc:ro" -v "$SDK:/sdk:ro" \
        -e JIELI_TOOLCHAIN=/tc -e AC79_SDK=/sdk "$IMAGE" \
        sh -c 'mkdir /w && cd /w && tar -xf - && sh tools/build_in.sh "$@" >&2 && tar -cf - build' sh "$@" |
    tar -C "$ROOT" -xf -
