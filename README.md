# Lorcha

**A small on-screen checker for [felucca-bsp](https://github.com/hugelton/felucca-bsp) on the M-VAVE FM-1.** It shows the keys,
buttons and knobs as you press and turn them, steps the LEDs through their brightness levels, plays a test tone, and shows
whether USB enumerated and what MIDI goes in and out. It is not an instrument and has no update entry.

The same source also builds for the SMK-37 Pro / Elite and the Pocket AMP. The FM-1 is the target; the other two are built
but not the target.

Unofficial. Not affiliated with or endorsed by M-VAVE or JieLi.

## The screen

| Part | Shown | FM-1 control |
|---|---|---|
| Buttons | a cell per button, white while held | all 14 |
| Keys | a strip, cyan while held; each sends a USB MIDI note (channel 1) | 27 keys F3..G5 |
| Knobs | a count per encoder; the analog inputs as bars and values (0..1023) | 7 encoders, MASTER pot, battery |
| LED PWM | every LED at one level; a held button's or key's own LED full | REC steps OFF, GLOW 3%, MID 15%, BREATH, FULL |
| Audio | a 440 Hz sine at -12 dBFS | PLAY on / off |
| USB | `USB ENUMERATED` or not; MIDI in count and last message; MIDI out count (`/n`: not sent) | SAVE sends C4 |
| Panel | colour bars (R G B W Y C) top right, a magenta corner pixel for orientation | |

SMK-37: SEQ PLAY tone, SEQ REC LED level (button LEDs 0..64/64 and the pads in white), PARA C4; pads send notes on channel 10.
Pocket AMP: knob push tone, RIGHT C4; no LEDs known to felucca-bsp.

Back to another firmware: UBOOT (SysEx soft key `F0 22 24 35 7D F7`, or two boots in a row that never reach 2 s), then
the stock firmware with `bsp/tools/fm1_rescue.sh`, then any package. Lorcha cannot be replaced over M-UPGRADE directly.

## Build

Needs the JieLi pi32v2 toolchain (Linux x86-64), the AC79 SDK (`bsp/tools/get_sdk.sh`) and Docker.

    git submodule update --init
    tools/build.sh --board fm1        # -> build/fm1/lorcha-fm1.fwsc  (fm1 | smk37 | pocketamp | all; --emu)

The sources go into a `linux/amd64` container as a tar stream and `build/` comes back the same way;
`JIELI_TOOLCHAIN` (default `~/.jieli/toolchain`) and `AC79_SDK` (default `~/.jieli/ac79-sdk`) are mounted. The package holds
the app and felucca-bsp's update loader (wrapped by `tools/ldrwrap.py`), packed by felucca-bsp's `tools/fm1pkg.py`
through `tools/lorcha_pkg.py`. Ship `LICENSES/Apache-2.0.txt` with a package (three SDK files are in it).

## Emulator and tests

    tools/build.sh --emu && tools/emu_check.sh      # fm1-emu: build/emu/<board>/sheet.png, the WAV and a pitch table
    tests/run.sh                                    # the tone, MIDI, the font, the packaging tools

fm1-emu 0.4.0 reads the audio PLL as zero, so felucca-bsp's audio start refuses it and the release package shows
`AUDIO ERROR -5` there. The `-emu` packages set the stock PLL values only when it reads zero: emulator only, never on a unit.

## Notes on felucca-bsp

- The SMK-37's update loader cannot be built from felucca-bsp as it is: `loader/loader.c` and `ldr_core.c` replace
  `smk37_map.h`'s loader values with the FM-1's (no `#ifndef`). `patches/felucca-bsp-loader-board-overrides.patch` adds the
  guards for that build only.
- `boards/fm1/fm1_lcd.h` does not build with `boards/smk37/fm1_lcd_hw.h`; Lorcha draws with its own `src/panel.h`.

## Licence

MPL-2.0 ([LICENSE](LICENSE)). felucca-bsp (`bsp/`) and pendel-bsp (`pendel-bsp/`, the Pocket AMP's loader) keep their own
MPL-2.0 files and NOTICEs; see [NOTICE](NOTICE).
