# What in this repository came from elsewhere

The plugin's own code, in `src/`, is under the BSD 3-Clause license
(`LICENSE`). The copy of the cart's SDK in `sdk/` is under the MIT license
(`sdk/LICENSE`). These parts came from elsewhere and keep their own terms.

| Where | What | License |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |

The built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and TinyUSB (MIT).

## MAME

The plugin's own code in `src/` was written for it. Parts of it are ports
of MAME's code, or were written with MAME's code as the reference. That
code is under the BSD 3-Clause license (`LICENSE`). It comes from MAME
(<https://github.com/mamedev/mame>, tag `mame0288`), and its copyright
holders are these.

| Here | From MAME | Copyright holders |
|---|---|---|
| `src/sw.c`: the matrix processor, the divider, the memory maps, the inputs | `src/mame/atari/starwars.cpp`, `starwars_m.cpp` | Steve Baines, Frank Palazzolo |
| `src/sw.c`: the vector generator | `src/devices/video/avgdvg.cpp` | Mathis Rosenhauer (with thanks to Eric Smith, Brad Oliver, Bernd Wiebelt, Aaron Giles, Andrew Caldwell) |
| `src/tms5220.c` | `src/devices/sound/tms5220.cpp`, `tms5110r.hxx` (the chip's tables) | Frank Palazzolo, Aaron Giles, Jonathan Gevaryahu, Raphael Nabet, Couriersud, Michael Zapf |
| `src/sw.c`: the RIOT, X2212 and ADC0809 (written with MAME's as the reference) | `src/devices/machine/mos6530.cpp`, `x2212.cpp`, `adc0808.cpp` | Curt Coder; smf, Barry Rodewald; Dirk Best |
| `src/m6809_core.h` (its flags, cycle counts and interrupt entry checked against MAME's) | `src/devices/cpu/m6809` | Nathan Woods |
| `src/swcv.c`: the POKEY's clocks and polynomial counters | `src/devices/sound/pokey.cpp` | Brad Oliver, Eric Smith, Juergen Buchmueller |

The zip reader in `src/swzip.c` follows RFC 1951 and the method of
zlib's `contrib/puff` by Mark Adler (zlib license). It was written afresh.

Star Wars is a trademark of Lucasfilm Ltd. The game is Atari's, 1983. This plugin contains none of the game.

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
