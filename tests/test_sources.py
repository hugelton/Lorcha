#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The sources (MPL-2.0): every string the firmware may draw is in the font (font_test does the check, given the strings), the
licence header on every file, each board header gives the calls lorcha.c makes, no words that must not appear."""
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
BOARDS = ["board_fm1.h", "board_smk37.h", "board_pocketamp.h"]
CALLS = ["LB_NAME", "LB_NBTN", "LB_BTN_NAME", "LB_B_TONE", "LB_B_LED", "LB_B_MIDI", "LB_NKEY", "LB_KEY_NOTE0", "LB_NENC",
         "LB_ENC_NAME", "LB_NAN", "LB_AN_NAME", "LB_NPAD", "LB_NLED", "LB_LED_NLEVELS", "lb_init", "lb_tick", "lb_buttons",
         "lb_keys", "lb_pads", "lb_enc_take", "lb_analog_poll", "lb_analog", "lb_led_level_name", "lb_led", "lb_leds_flush",
         "lb_lcd_hw_init", "lb_lcd_cmd_raw", "lb_lcd_data_raw", "lb_lcd_after_init", "LB_LCD_MADCTL", "lb_audio_start",
         "fm1_alnk0_irq"]


def strings(text):
    return [bytes(s, "utf-8").decode("unicode_escape") for s in re.findall(r'"((?:[^"\\\n]|\\.)*)"', text)]


class Sources(unittest.TestCase):
    def test_drawn_strings_in_font(self):
        """the strings in lorcha.c and the board headers (but the console's, includes and asm), through font_test"""
        found = []
        for f in ["lorcha.c", "midi.h", "tone.h"] + BOARDS:
            for line in (SRC / f).read_text().splitlines():
                if "#include" in line or "__asm__" in line or "con_puts" in line or "section(" in line:
                    continue
                found += [s for s in strings(line) if s and "\r" not in s and "%" != s]
        found.append("SMK-37 Elite")
        exe = ROOT / "build" / "host" / "font_test"
        self.assertTrue(exe.is_file(), "run tests/run.sh (it builds font_test first)")
        r = subprocess.run([str(exe)] + found, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_licence_headers(self):
        files = [p for p in list(SRC.iterdir()) + list((ROOT / "tools").rglob("*")) + list((ROOT / "tests").iterdir())
                 if p.is_file() and p.suffix in (".c", ".h", ".py", ".sh", "")]
        for p in files:
            if p.name in ("Dockerfile",) or "__pycache__" in p.parts:
                continue
            head = p.read_text().splitlines()[:3]
            self.assertTrue(any("SPDX-License-Identifier: MPL-2.0" in h for h in head), p)

    def test_board_headers_complete(self):
        for b in BOARDS:
            text = (SRC / b).read_text()
            for c in CALLS:
                self.assertRegex(text, r"\b" + c + r"\b", f"{b}: {c}")

    def test_no_forbidden_words(self):
        bad = bytes.fromhex("6172676f7379").decode()          # (a name that must not appear, kept out of this file too)
        for p in ROOT.rglob("*"):
            if not p.is_file() or ".git" in p.parts or "build" in p.parts or p.parts[len(ROOT.parts)] in ("bsp", "pendel-bsp"):
                continue
            try:
                text = p.read_text()
            except UnicodeDecodeError:
                continue
            self.assertNotIn(bad, text.lower(), p)


if __name__ == "__main__":
    unittest.main()
