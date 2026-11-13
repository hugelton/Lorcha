#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Lorcha's .fwsc for one board: the app image and the update loader's raw image in, the package out.

  lorcha_pkg.py --board fm1|smk37|pocketamp APP.bin LOADER.raw OUT.fwsc --product "FM-1_960" [--sdk DIR]

The package itself is felucca-bsp's tools/fm1pkg.py (the FM-1 and the SMK-37 layouts are its own). The Pocket AMP's
layout is added here, as facts of that unit's flash (the app area [0x4000, 0xA5000), VM [0xA5000, 0xBD000), BTIF
0xBD000, OTP 0xBE000, USR [0xBF000, 0xFF000)); felucca-bsp has no Pocket AMP package yet. The loader is wrapped by
ldrwrap.py. The three JieLi SDK files fm1pkg.py puts in are Apache-2.0: ship LICENSES/Apache-2.0.txt with a package.
"""
import argparse
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ldrwrap  # noqa: E402


def load_fm1pkg(bsp):
    spec = importlib.util.spec_from_file_location("fm1pkg", bsp / "tools" / "fm1pkg.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# The Pocket AMP: (the app area's size, the region descriptors, the interleave blocks), as fm1pkg.BOARDS has them,
# and its app slot (the app.bin entry; cfg_tool.bin and the cfg directory follow it inside the app area)
POCKETAMP = (0xA5000, [
    (0xA5000, 0x18000, 0x12, 0x81, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),     # VM
    (0, 0xA5000, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),         # PRCT
    (0xBD000, 0x1000, 0x92, 0x81, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),    # BTIF
    (0xBE000, 0x1000, 0x92, 0x81, 0, bytes.fromhex("4f545000ffffffffffffffff4155544f")),    # OTP
    (0xBF000, 0x40000, 0x92, 0x81, 1, "USR")], 20)
POCKETAMP_SLOT = 0xA0024

# what the app may be at most, per board (the app.bin slot)
SLOTS = {"fm1": 0x8DFBC, "smk37": 0x8DFBC, "pocketamp": POCKETAMP_SLOT}


def build(board, app, loader_raw, product, sdk=None, bsp=HERE.parent / "bsp"):
    """-> (the package, the wrapped ota.bin)"""
    pk = load_fm1pkg(bsp)
    pk.SDK = sdk
    if board == "pocketamp":
        pk.BOARDS["pocketamp"] = POCKETAMP
        pk.APP_SLOT = POCKETAMP_SLOT
    if board not in pk.BOARDS:
        raise SystemExit(f"lorcha_pkg: no layout for {board}")
    if len(app) > SLOTS[board]:
        raise SystemExit(f"lorcha_pkg: the app is {len(app)} B, the {board} slot {SLOTS[board]} B")
    ota = ldrwrap.wrap(loader_raw)
    if not ldrwrap.has_mark(ota):
        raise SystemExit("lorcha_pkg: the loader's mark is not where the app's ota.c looks")
    flash = pk.flash_image(app, pk.KEY, board)
    if board == "pocketamp":
        # the app area must end before VM: fm1pkg sizes flash.bin by the board, the region by the slot
        assert len(flash) == POCKETAMP[0]
    return pk.ufw(flash, ota, product, pk.BOARDS[board][2]), ota


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True, choices=sorted(SLOTS))
    ap.add_argument("app", type=Path)
    ap.add_argument("loader", type=Path, help="the loader's raw RAM image")
    ap.add_argument("out", type=Path)
    ap.add_argument("--product", required=True)
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    pkg, ota = build(a.board, a.app.read_bytes(), a.loader.read_bytes(), a.product, a.sdk)
    a.out.write_bytes(pkg)
    a.out.with_suffix(".ota.bin").write_bytes(ota)
    print(f"{a.out.name}: {len(pkg)} B, identity {a.product!r}, loader {len(ota)} B")


if __name__ == "__main__":
    main()
