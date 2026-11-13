#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Lorcha's build on Linux x86-64 (tools/build.sh runs it in Docker): per board the app (src/lorcha.c on felucca-bsp), the
# update loader (felucca-bsp's loader/; the Pocket AMP's from pendel-bsp), the package.
#   JIELI_TOOLCHAIN=... AC79_SDK=... sh tools/build_in.sh [--board fm1|smk37|pocketamp|all] [--model pro|elite|both] [--emu]
# --emu: also lorcha-<board>-emu.fwsc (FM-1, SMK-37), for fm1-emu only: it gives PLL0 its stock values when the emulator
# reads them as zero, so the audio starts there (src/lorcha.c LORCHA_EMU_PLL). Never install an -emu package on a unit.
set -eu
TC="${JIELI_TOOLCHAIN:?JIELI_TOOLCHAIN: the JieLi toolchain}"
SDK="${AC79_SDK:?AC79_SDK: the AC79 SDK}"
BOARD=all
MODEL=both
EMU=0
while [ $# -gt 0 ]; do
    case "$1" in
    --board) BOARD="$2"; shift 2 ;;
    --model) MODEL="$2"; shift 2 ;;
    --emu) EMU=1; shift ;;
    *) echo "build_in.sh: $1?" >&2; exit 2 ;;
    esac
done
ROOT="$(pwd)"
B="$ROOT/bsp"
P="$ROOT/pendel-bsp"
CC="$TC/pi32v2/bin/clang -target pi32v2"
LD="$TC/pi32v2/bin/ld"
OBJCOPY="$TC/common/bin/objcopy"
OBJDUMP="$TC/common/bin/objdump"
CFLAGS="-Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function -Werror"
VERSION=$(sed -n 's/^#define LORCHA_VERSION "\(.*\)"/\1/p' src/lorcha.c)

# app OUT NAME DEFINES INCLUDES...: the app image (code in place from XIP, .data and .ram_text appended where they load)
app() {
    ao="$1"; name="$2"; defs="$3"; shift 3
    mkdir -p "$ao/obj"
    $CC -c "$B/chip/crt0.S" -o "$ao/obj/crt0.o"
    $CC -c "$B/chip/fm1_vec.S" -o "$ao/obj/vec.o"
    $CC -c "$B/chip/fm1_isr.S" -o "$ao/obj/isr.o"
    # shellcheck disable=SC2086
    $CC $CFLAGS $defs "$@" -c src/lorcha.c -o "$ao/obj/lorcha.o"
    $LD -T "$B/examples/hwtest/app.ld" -Map "$ao/$name.map" "$ao/obj/crt0.o" "$ao/obj/vec.o" "$ao/obj/isr.o" \
        "$ao/obj/lorcha.o" -o "$ao/$name.elf"
    $OBJCOPY -O binary -j .text "$ao/$name.elf" "$ao/obj/text.bin"
    $OBJCOPY -O binary -j .data "$ao/$name.elf" "$ao/obj/data.bin"
    $OBJCOPY -O binary -j .ram_text "$ao/$name.elf" "$ao/obj/rt.bin"
    $OBJDUMP -t "$ao/$name.elf" > "$ao/obj/syms.txt"
    python3 tools/mkbin.py "$ao/obj" "$ao/$name.bin"
}

# loader OUT LOADER_DIR SRC DEFINES INCLUDES...: the update loader's raw RAM image
loader() {
    lo="$1"; ldir="$2"; src="$3"; defs="$4"; shift 4
    mkdir -p "$lo/obj"
    $CC -c "$ldir/crt0_ldr.S" -o "$lo/obj/crt0_ldr.o"
    # shellcheck disable=SC2086
    $CC -Os -fno-builtin -Wall -Wno-unused-function $defs "$@" -c "$src" -o "$lo/obj/loader.o"
    $LD -T "$ldir/loader.ld" "$lo/obj/crt0_ldr.o" "$lo/obj/loader.o" -o "$lo/loader.elf"
    $OBJCOPY -O binary -j .text "$lo/loader.elf" "$lo/loader.raw"
}

package() {   # OUT NAME BOARD PRODUCT
    python3 tools/lorcha_pkg.py --board "$3" "$1/$2.bin" "$1/loader.raw" "$1/$2.fwsc" --product "$4" --sdk "$SDK"
    grep -q "$4" "$1/$2.bin" || { echo "build_in.sh: the app does not carry the identity $4" >&2; exit 1; }
}

fm1() {
    out=build/fm1; rm -rf "$out"
    app "$out" lorcha-fm1 "-DLORCHA_BOARD_FM1" -Isrc -I"$B/chip" -I"$B/boards/fm1"
    loader "$out" "$B/loader" "$B/loader/loader.c" "" -I"$B/chip" -I"$B/boards/fm1"
    package "$out" lorcha-fm1 fm1 "FM-1_960"
    if [ "$EMU" = 1 ]; then
        app "$out/emu" lorcha-fm1-emu "-DLORCHA_BOARD_FM1 -DLORCHA_EMU_PLL=1" -Isrc -I"$B/chip" -I"$B/boards/fm1"
        cp "$out/loader.raw" "$out/emu/"
        package "$out/emu" lorcha-fm1-emu fm1 "FM-1_960"
    fi
}

smk37() {     # MODEL: pro | elite
    case "$1" in pro) model="SMK-37 Pro" ;; elite) model="SMK-37 Elite" ;; *) echo "model $1?" >&2; exit 2 ;; esac
    out="build/smk37$1"; rm -rf "$out"
    app "$out" "lorcha-smk37$1" "-DLORCHA_BOARD_SMK37" -DSMK_MODEL_NAME="\"$model\"" -include "$B/boards/smk37/smk37_map.h" \
        -Isrc -I"$B/boards/smk37" -I"$B/chip" -I"$B/boards/fm1"
    # the loader: felucca-bsp's, from a copy with patches/felucca-bsp-loader-board-overrides.patch (as it is, loader.c and
    # ldr_core.c replace smk37_map.h's loader values with the FM-1's); -Werror=macro-redefined proves none is replaced
    sb="$out/bsp-smk"; mkdir -p "$sb"; cp -R "$B/chip" "$B/boards" "$B/loader" "$sb/"
    patch -s -d "$sb" -p1 < patches/felucca-bsp-loader-board-overrides.patch
    loader "$out" "$sb/loader" "$sb/loader/loader.c" "-DSMK_LOADER=1 -Werror=macro-redefined" -DSMK_MODEL_NAME="\"$model\"" \
        -include "$sb/boards/smk37/smk37_map.h" -I"$sb/boards/smk37" -I"$sb/chip" -I"$sb/boards/fm1"
    grep -q "ota-$model" "$out/loader.raw" || { echo "build_in.sh: the SMK loader is not ota-$model" >&2; exit 1; }
    rm -rf "$sb"
    package "$out" "lorcha-smk37$1" smk37 "${model}_960"
    if [ "$EMU" = 1 ]; then
        app "$out/emu" "lorcha-smk37$1-emu" "-DLORCHA_BOARD_SMK37 -DLORCHA_EMU_PLL=1" -DSMK_MODEL_NAME="\"$model\"" \
            -include "$B/boards/smk37/smk37_map.h" -Isrc -I"$B/boards/smk37" -I"$B/chip" -I"$B/boards/fm1"
        cp "$out/loader.raw" "$out/emu/"
        package "$out/emu" "lorcha-smk37$1-emu" smk37 "${model}_960"
    fi
}

pocketamp() {
    out=build/pocketamp; rm -rf "$out"
    app "$out" lorcha-pocketamp "-DLORCHA_BOARD_POCKETAMP" -Isrc -I"$B/chip" -I"$B/boards/pocketamp" -I"$B/boards/fm1"
    # the Pocket AMP's loader is pendel-bsp's (boards/pocketamp/pa_loader.c on its own loader/), with felucca-bsp's
    # loader mark (crt0_ldr.S's default), the one Lorcha's chip/ota.c looks for
    loader "$out" "$P/loader" "$P/boards/pocketamp/pa_loader.c" "" -I"$P/chip" -I"$P/boards/pocketamp" -I"$P/boards/fm1"
    package "$out" lorcha-pocketamp pocketamp "Pocket AMP_960"
}

mkdir -p build
case "$BOARD" in
fm1) fm1 ;;
smk37) if [ "$MODEL" = both ]; then smk37 pro; smk37 elite; else smk37 "$MODEL"; fi ;;
pocketamp) pocketamp ;;
all) fm1; smk37 pro; smk37 elite; pocketamp ;;
*) echo "build_in.sh: --board fm1|smk37|pocketamp|all" >&2; exit 2 ;;
esac
echo "Lorcha $VERSION: built ($BOARD)"
