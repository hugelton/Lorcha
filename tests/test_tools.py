#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The host tools: the loader's wrapping (ldrwrap.py), the packages (lorcha_pkg.py), the SMK-37 loader patch, the built
packages when build/ has them. Standard library only; the package tests need AC79_SDK (else skipped), the reference LZ4
check the lz4 command (else skipped), the patch checks patch(1)."""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ldrwrap  # noqa: E402
import lorcha_pkg  # noqa: E402

SDK = os.environ.get("AC79_SDK", str(Path.home() / ".jieli" / "ac79-sdk"))
HAVE_SDK = (Path(SDK) / "cpu" / "wl82" / "tools" / "uboot.boot").is_file()
PRODUCTS = {"fm1": "FM-1_960", "smk37pro": "SMK-37 Pro_960", "smk37elite": "SMK-37 Elite_960",
            "pocketamp": "Pocket AMP_960"}


def fake_loader(n=10000, seed=1):
    """a loader-like image: the entry's jump, the mark, then code-like bytes with repeats"""
    r = random.Random(seed)
    body = bytearray(b"\x00\x20" + ldrwrap.MARK)
    words = [r.getrandbits(32).to_bytes(4, "little") for _ in range(64)]
    while len(body) < n:
        body += r.choice(words) if r.random() < 0.7 else bytes([r.getrandbits(8)])
    return bytes(body[:n])


def identity_of(pkg):
    """the .fwsc's identity, as an installer or fm1-emu reads it: one marker byte after each 0x2F-byte block"""
    out = []
    for i in range(36):
        m = pkg[i * 0x30 + 0x2F]
        if m == 0x7D:
            break
        out.append(chr((m - i - 1) & 0xFF))
    return "".join(out)


class LoaderWrap(unittest.TestCase):
    def test_round_trip(self):
        for n in (1, 15, 16, 4095, 4096, 4097, 12000, ldrwrap.MAX_IMAGE):
            img = fake_loader(n, n)
            pkg = ldrwrap.wrap(img)
            self.assertEqual(ldrwrap.unwrap(pkg), img, n)

    def test_heads(self):
        img = fake_loader(9000)
        pkg = ldrwrap.wrap(img)
        _, dc, off, ln, fl = struct.unpack_from("<HHIIB", pkg, 0)
        self.assertEqual((off, ln, fl), (0x20, len(pkg) - 0x20, 0x41))
        self.assertEqual(dc, ldrwrap.crc16(pkg[0x20:]))
        _, dc, raw_len, load = struct.unpack_from("<HHII", pkg, 0x20)
        self.assertEqual((raw_len, load, dc), (len(img), 0x01C0A800, ldrwrap.crc16(img)))
        self.assertTrue(ldrwrap.has_mark(pkg))

    def test_lz4_rules(self):
        """each block: the last 5 bytes are literals, no match starts in the last 12, offsets within the block"""
        for seed in range(20):
            raw = fake_loader(4096, seed)
            blk = ldrwrap.lz4_block(raw)
            self.assertEqual(ldrwrap.lz4_unblock(blk, len(raw)), raw)
            i, out = 0, 0
            while i < len(blk):
                t = blk[i]
                i += 1
                lit = t >> 4
                if lit == 15:
                    while blk[i] == 255:
                        lit += 255
                        i += 1
                    lit += blk[i]
                    i += 1
                i += lit
                out += lit
                if i >= len(blk):
                    self.assertGreaterEqual(lit, 5)
                    break
                self.assertLess(out, len(raw) - 12)
                m = t & 15
                i += 2
                if m == 15:
                    while blk[i] == 255:
                        m += 255
                        i += 1
                    m += blk[i]
                    i += 1
                out += m + 4
                self.assertLessEqual(out, len(raw) - 5)

    def test_bad_input(self):
        pkg = bytearray(ldrwrap.wrap(fake_loader(5000)))
        pkg[100] ^= 1
        with self.assertRaises(ValueError):
            ldrwrap.unwrap(bytes(pkg))
        with self.assertRaises(SystemExit):
            ldrwrap.wrap(b"\0" * (ldrwrap.MAX_IMAGE + 1))

    @unittest.skipUnless(shutil.which("lz4"), "no lz4 command")
    def test_reference_lz4(self):
        """the blocks, as an LZ4 legacy frame, decode with the reference lz4"""
        img = fake_loader(12000, 7)
        frame = b"\x02\x21\x4c\x18"
        for o in range(0, len(img), 4096):
            c = ldrwrap.lz4_block(img[o:o + 4096])
            frame += struct.pack("<I", len(c)) + c
        with tempfile.TemporaryDirectory() as d:
            Path(d, "a.lz4").write_bytes(frame)
            subprocess.run(["lz4", "-d", "-f", "-q", f"{d}/a.lz4", f"{d}/a.raw"], check=True)
            self.assertEqual(Path(d, "a.raw").read_bytes(), img)


@unittest.skipUnless(HAVE_SDK, "no AC79 SDK (AC79_SDK)")
class Packages(unittest.TestCase):
    def build(self, board, product, app_len=20000):
        app = bytes(random.Random(app_len).getrandbits(8) for _ in range(app_len))
        pkg, ota = lorcha_pkg.build(board, app, fake_loader(), product, SDK)
        return pkg, ota

    def test_identity_and_blocks(self):
        for board, product, blocks in (("fm1", "FM-1_960", 20), ("smk37", "SMK-37 Pro_960", 36),
                                       ("pocketamp", "Pocket AMP_960", 20)):
            pkg, _ = self.build(board, product)
            self.assertEqual(identity_of(pkg), product, board)
            # 36-block packages: the markers after the identity are all unused (the SMK-37's form)
            if blocks == 36:
                self.assertTrue(all(pkg[i * 0x30 + 0x2F] == 0x7D for i in range(len(product), 36)))

    def test_pocketamp_layout(self):
        pk = lorcha_pkg.load_fm1pkg(ROOT / "bsp")
        pk.SDK = SDK
        pk.BOARDS["pocketamp"] = lorcha_pkg.POCKETAMP
        pk.APP_SLOT = lorcha_pkg.POCKETAMP_SLOT
        flash = pk.flash_image(b"\x55" * 1000, pk.KEY, "pocketamp")
        self.assertEqual(len(flash), 0xA5000)              # the app area ends where VM starts
        regions = lorcha_pkg.POCKETAMP[1]
        self.assertEqual([r[0] for r in regions], [0xA5000, 0, 0xBD000, 0xBE000, 0xBF000])
        self.assertTrue(all(r[0] + r[1] <= 0x100000 for r in regions))

    def test_too_big(self):
        with self.assertRaises(SystemExit):
            lorcha_pkg.build("fm1", b"\0" * (lorcha_pkg.SLOTS["fm1"] + 4), fake_loader(), "FM-1_960", SDK)

    def test_needs_the_mark(self):
        with self.assertRaises(SystemExit):
            lorcha_pkg.build("fm1", b"\0" * 100, b"\x01" * 5000, "FM-1_960", SDK)


class SmkLoaderPatch(unittest.TestCase):
    PATCH = ROOT / "patches" / "felucca-bsp-loader-board-overrides.patch"

    @unittest.skipUnless(shutil.which("patch"), "no patch command")
    def test_applies_to_bsp(self):
        with tempfile.TemporaryDirectory() as d:
            shutil.copytree(ROOT / "bsp" / "loader", Path(d) / "loader")
            r = subprocess.run(["patch", "-s", "-d", d, "-p1", "-i", str(self.PATCH)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            src = (Path(d) / "loader" / "ldr_core.c").read_text()
            self.assertIn("#ifndef LDR_APP_HI", src)

    def test_still_needed(self):
        """bsp/ as pinned replaces a board's loader values: when this fails, the patch went upstream; drop it"""
        core = (ROOT / "bsp" / "loader" / "ldr_core.c").read_text()
        self.assertNotIn("#ifndef LDR_APP_HI", core)


@unittest.skipUnless((ROOT / "build" / "fm1" / "lorcha-fm1.fwsc").is_file(), "not built (tools/build.sh)")
class Built(unittest.TestCase):
    def test_packages(self):
        for d, product in PRODUCTS.items():
            pkg = (ROOT / "build" / d / f"lorcha-{d}.fwsc")
            if not pkg.is_file():
                continue
            raw = pkg.read_bytes()
            self.assertEqual(identity_of(raw), product, d)
            app = (ROOT / "build" / d / f"lorcha-{d}.bin").read_bytes()
            self.assertIn(product.encode(), app, d)        # the app answers the package's identity
            self.assertLessEqual(len(app), lorcha_pkg.SLOTS["smk37" if d.startswith("smk37") else d])
            ota = (ROOT / "build" / d / f"lorcha-{d}.ota.bin").read_bytes()
            img = ldrwrap.unwrap(ota)
            self.assertTrue(ldrwrap.has_mark(ota), d)
            want = {"fm1": b"ota-FM-1_900", "smk37pro": b"ota-SMK-37 Pro_913", "smk37elite": b"ota-SMK-37 Elite_913",
                    "pocketamp": b"ota-Pocket AMP_900"}[d]
            self.assertIn(want, img, d)                    # each board's own loader
            if d.startswith("smk37"):
                self.assertNotIn(b"ota-FM-1_900", img)
            self.assertNotIn(b"PENDEL-LOADER", img)


if __name__ == "__main__":
    unittest.main()
