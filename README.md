# Lorcha

**A template firmware for testing [felucca-bsp](https://github.com/hugelton/felucca-bsp) on three M-VAVE devices: the FM-1,
the SMK-37 Pro / Elite and the Pocket AMP.** One small source, one board header per device, one package per board. It boots,
draws its screen, reads every key and control, drives the LEDs, plays a test tone and a small voice, speaks USB MIDI both
ways, keeps the update entry, and has a HAL test page that shows each input and runs each output. It is a starting point,
not an instrument.

Unofficial. Not affiliated with or endorsed by M-VAVE or JieLi.

| | |
|---|---|
| Status | 0.1.0. Built for all three; run in [fm1-emu](https://github.com/joakimeriksson/fm1-emu) 0.4.0 for the FM-1 and the SMK-37 (screens, inputs, audio). **Never run on a unit.** The Pocket AMP is built only (the emulator has no Pocket AMP) |
| HAL | `bsp/`: felucca-bsp with the SMK-37 board layer ([felucca-bsp-smk37](https://github.com/hugelton/felucca-bsp-smk37) `main`, 2210cce: felucca-bsp `main` d18879b + [#40](https://github.com/hugelton/felucca-bsp/pull/40) + `boards/smk37`, Alpha). `pendel-bsp/`: [pendel-bsp](https://github.com/hugelton/pendel-bsp) 6a85709, for the Pocket AMP's update loader only |
| Licence | MPL-2.0 ([LICENSE](LICENSE)); credits in [NOTICE](NOTICE) and `bsp/NOTICE` |

## What it tests

| Part | How | FM-1 | SMK-37 Pro / Elite | Pocket AMP |
|---|---|---|---|---|
| Boot | the boot guard (two boots in a row that never reach 2 s go to UBOOT), the crash record, the flash part's id | yes | yes | yes |
| Screen | ST7789 240 x 240 through the board's wiring; colour bars and a magenta top-left corner on the TEST page (colour order, orientation) | `boards/fm1/fm1_lcd_hw.h` | `boards/smk37/fm1_lcd_hw.h` (+ its tuning) | `boards/pocketamp/pa_lcd_*.h` (its init table) |
| Buttons | a cell per button lights while held, with its printed name | 14 | 18 | 6 |
| Keys | a strip, a cell per key; each plays the voice and goes out as a USB MIDI note (channel 1) | 27 (F3..G5) | 37 (C3..C6), velocity | none |
| Encoders | a counter each; each sends CC 20 + n; one also sets the filter | 7 (SELECT the filter) | 8 (K1 is the MASTER level and counts nothing; K2 the filter) | the knob (PC0 / PC1, read by Lorcha) |
| Pads | 16 cells; a hit plays the voice and sends a note on channel 10 (36 + pad); the pad's RGB LED shows its velocity | | 16 | |
| Analog | a bar and the value (0..1023) each | MASTER pot, battery | MASTER (K1), 4 faders, pitch, mod, pedal, battery | the audio input's peak |
| LEDs | each button's (and key's) LED follows it; the TONE and PAGE buttons' LEDs show their state; an LED walk on a button | 41, on / off | 19 button LEDs (levels), 16 RGB pads | none known |
| Audio out | a 440 Hz test tone (-12 dBFS) and a mono voice (saw, square, triangle, sine; a low-pass; attack / release); the level from the MASTER pot (or VOL- / VOL+) | ALNK0, port C | the FM-1's | ALNK0, port A (with the input) |
| MIDI in | USB MIDI plays the voice (any channel; CC 74 the filter, CC 120 / 123 all off); the last message on HOME | USB, and the TRS jack | USB | USB |
| MIDI out | keys, pads, encoders, the test note | USB | USB | USB |
| Headphone jack | `HP` on the TEST page's status line | yes | | |
| USB | MIDI and a CDC serial console (`h` help, `p` status, `s` status every second, `1` / `2` pages, `t` tone, `w` LED walk, `n` test note, `c` last crash, `u` UBOOT, `r` reboot); the SysEx soft key (F0 22 24 35 7D F7) for UBOOT | yes | yes | yes |
| Update entry | M-UPGRADE over USB MIDI (`chip/ota.c`): the installer's handshake answers the package's identity, the update stages the package's loader and resets into it; a bar along the bottom shows the steps | staging 0xE0000 | 0xBB000 (smk37_map.h) | 0xB8000 (the top of VM) |

The buttons that run things:

| | FM-1 | SMK-37 | Pocket AMP |
|---|---|---|---|
| HOME / TEST page | HOME | PATCH | HOME |
| test tone on / off | PLAY | SEQ PLAY | knob push |
| LED walk | REC | SEQ REC | |
| MIDI test note (C4, 0.5 s) | SAVE | PARA | RIGHT |
| wave | SEL | SCALE | LEFT |
| octave - / + | OCT- / OCT+ | OCTAVE- / OCTAVE+ | |
| level | MASTER pot | K1 | VOL- / VOL+ |

## Build

Needs the JieLi pi32v2 toolchain (Linux x86-64) and the AC79 SDK (`bsp/tools/get_sdk.sh`), and Docker for a Mac.

    git submodule update --init
    tools/build.sh                        # all boards: build/<board>/lorcha-<board>.fwsc
    tools/build.sh --board fm1            # fm1 | smk37 [--model pro|elite] | pocketamp | all
    tools/build.sh --emu                  # also the emulator-only packages (below)

`JIELI_TOOLCHAIN` (default `~/.jieli/toolchain`) and `AC79_SDK` (default `~/.jieli/ac79-sdk`) are mounted into a
`linux/amd64` Debian container (`tools/docker/Dockerfile`); the sources go in, and `build/` comes back, as a tar stream, so the
checkout may sit outside what the Docker VM mounts. On a Linux x86-64 machine, `tools/build_in.sh` runs the same build directly.

Per board, `build/<board>/` has the app (`.bin`, `.elf`, `.map`), the loader's raw image (`loader.raw`), the wrapped loader
(`.ota.bin`) and the package (`.fwsc`). Identities: `FM-1_960`, `SMK-37 Pro_960`, `SMK-37 Elite_960`, `Pocket AMP_960`.

How a package is made: the app is `src/lorcha.c` with one board header (`-DLORCHA_BOARD_FM1|SMK37|POCKETAMP`), linked with
felucca-bsp's `examples/hwtest/app.ld`. The loader is felucca-bsp's `loader/` (the SMK-37's with the patch below; the
Pocket AMP's is pendel-bsp's `boards/pocketamp/pa_loader.c`), wrapped by `tools/ldrwrap.py` (two heads and 4 KiB LZ4 blocks,
as the SPL loads it). `tools/lorcha_pkg.py` makes the package with felucca-bsp's `tools/fm1pkg.py` (its FM-1 and SMK-37
layouts; the Pocket AMP's is added there). Ship `LICENSES/Apache-2.0.txt` with a package (three SDK files are in it).

## Run in the emulator

    tools/build.sh --emu
    FM1_EMU=/path/to/fm1-emu tools/emu_check.sh        # -> build/emu/<board>/: sheet.png, <board>.wav, wav.txt, run.log

A scripted run per board (FM-1, SMK-37 Pro): boot, the tone, the pages, keys, an encoder, a button, the pot, the LED walk,
MIDI into the FM-1's TRS jack; screen snapshots on a contact sheet, the audio as a WAV with a level and pitch table.

fm1-emu 0.4.0 reads the audio PLL (0x119A0) as zero; felucca-bsp's audio start refuses any clock but the one a bootloader
leaves running, so **the release packages show `AUDIO ERROR -5` in the emulator** (on a unit the PLL holds the stock values).
The `-emu` packages (`--emu`) set the stock values when the PLL reads zero, and only then, so the voice is heard there. They are
for the emulator only: never install an `-emu` package on a unit.

## Tests

    tests/run.sh

The voice (pitch of every note, the note stack, the envelope, the tone, no overflow; under UBSan and ASan), MIDI (packets,
the byte parser), the font (every glyph, every string the firmware draws), the loader's wrapping (round trips, the LZ4 rules,
the reference `lz4`), the packages (identities, the Pocket AMP's layout; with `AC79_SDK`), the SMK-37 loader patch, the built
packages (each app carries its package's identity, each loader is its board's own), the sources (licence headers, the board
headers complete).

## Install (not done yet: nothing here has been on a unit)

`bsp/tools/fm1_install.py build/fm1/lorcha-fm1.fwsc` (FM-1, SMK-37: the installer knows both). Back up the unit first. The
Pocket AMP has no installer here: its flash is not checked against a working package, and `fm1_install.py` does not look for
a Pocket AMP. A firmware that never reaches 2 s twice in a row drops into UBOOT; from UBOOT, `bsp/tools/fm1_rescue.sh` puts the
FM-1's stock firmware back.

## Found while building it

- **The SMK-37's update loader cannot be built from felucca-bsp as it is.** `loader/loader.c` and `loader/ldr_core.c` define
  `FELUCCA_ID`, `FL_RANGE_OK` and `LDR_APP_LO .. LDR_DATA_HI` without `#ifndef`, so what `boards/smk37/smk37_map.h` gives the
  loader (`SMK_LOADER`) is replaced by the FM-1's (the FM-1's app area, records looked for in 0x93000..0xFC000, the identity
  `ota-FM-1_900`); only a warning says so. `patches/felucca-bsp-loader-board-overrides.patch` adds the guards (as pendel-bsp
  has them); the build applies it to a copy for the SMK-37's loader only, with `-Werror=macro-redefined`, and checks the
  loader answers `ota-SMK-37 ..._913`. `tests/test_tools.py` fails once felucca-bsp has the guards, so the patch can go.
- `boards/fm1/fm1_lcd.h` does not build with `boards/smk37/fm1_lcd_hw.h` (its `fm1_lcd_wait` returns nothing and it has no
  `fm1_lcd_spi_off`), so Lorcha draws through its own small panel layer (`src/panel.h`) on all three boards.
- fm1-emu 0.4.0 and the audio PLL: above.

## Layout

| Path | What |
|---|---|
| `src/lorcha.c` | the firmware: boot, the pages, the inputs to actions, MIDI, the console, the update hooks |
| `src/board_fm1.h`, `board_smk37.h`, `board_pocketamp.h` | each board behind the same calls (`lb_*`, `LB_*`): the controls and their names, the LEDs, the panel's wiring, the audio |
| `src/panel.h`, `font.h` | the panel (init, rectangles, text) and a 5 x 7 font |
| `src/synth.h`, `midi.h` | the voice and the tone; MIDI packets and the byte parser (host-tested) |
| `tools/` | `build.sh` (Docker), `build_in.sh`, `mkbin.py`, `ldrwrap.py`, `lorcha_pkg.py`, `emu_check.sh`, `sheet.py`, `wavstat.py` |
| `patches/` | the felucca-bsp change the SMK-37's loader needs |
| `tests/` | `run.sh`: C tests (host cc) and Python tests |

To start a firmware from it: keep `src/board_*.h` and `panel.h`, replace the pages and the voice in `lorcha.c`. A new board is
one more header with the same calls (`tests/test_sources.py` lists them).

## Licence

MPL-2.0, per file. Lorcha's own code is new; the HAL is felucca-bsp's (and pendel-bsp's loader for the Pocket AMP), whose
NOTICE files credit their authors. See [NOTICE](NOTICE).
