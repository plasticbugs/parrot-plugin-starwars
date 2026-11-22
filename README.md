# Star Wars

This plugin plays Atari's 1983 Star Wars arcade game on the ColecoVision. It needs your own copy of the game's ROMs.

## Setup

1. Copy `STARWARS.CVP` into the `/PLUGINS` folder on the card.
2. Put the ROMs from MAME's `starwars` set in a folder named `/STARWARS`. You can copy the 13 files loose, or copy `starwars.zip` as it is. You can also put the zip in the card's top folder as `/STARWARS.ZIP`.
3. Start Star Wars from the Plugins page in the menu.

The files can have any names, because the plugin knows each ROM by its size and contents. If a ROM is missing, the screen names it. Only MAME's `starwars` set works, not the older `starwars1` or `starwarso` sets.

The game runs its own self-test first, which takes about five seconds.

## Controls

On the ColecoVision controller:

- The joystick moves the yoke. The yoke springs back when you let go.
- Either fire button fires.
- Keypad * or # adds a coin.
- Keypad 1 starts the game. Fire starts it too.
- Keypad 0 swaps up and down.
- Press RESET on the console to go back to the menu.

On a USB pad, through a powered hub:

- The left stick moves the yoke. The D-pad works too.
- Any face button, L, R, ZL or ZR fires.
- Minus adds a coin, and plus starts the game.
- Click the right stick, or press the capture button, to swap up and down.

At first, up on the stick aims down, the way a flight stick works. The plugin remembers which way you left it.

About five seconds after the game starts, the stick does nothing for three seconds. The plugin is moving the yoke to both ends so the game can learn its range.

## Saves

High scores and the game's settings are saved in `/STARWARS/STARWARS.NV` when you press RESET. Press RESET before you turn the console off, or new high scores are lost.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Steve Baines and Frank Palazzolo, for the Star Wars driver and the matrix processor.
- Mathis Rosenhauer, for the vector generator, with thanks to Eric Smith, Brad Oliver, Bernd Wiebelt, Aaron Giles and Andrew Caldwell.
- Frank Palazzolo, Aaron Giles, Jonathan Gevaryahu, Raphael Nabet, Couriersud and Michael Zapf, for the TMS5220 speech chip.
- Curt Coder, smf, Barry Rodewald and Dirk Best, for the RIOT, X2212 and ADC0809 chips.
- Nathan Woods, for the 6809 CPU.
- Brad Oliver, Eric Smith and Juergen Buchmueller, for the POKEY sound chip.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license.

The plugin itself is by Parrot Cart, and it's free. Its source is in this repository, https://github.com/plasticbugs/parrot-plugin-starwars.

Star Wars is a trademark of Lucasfilm Ltd. Parrot Cart isn't connected with them. This plugin doesn't include any game ROMs.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file, byte for byte.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/STARWARS.CVP`. Copy it into the `/PLUGINS` folder on the card.

On Ubuntu, `tools/ci/setup.sh` installs the same tools that GitHub uses to
build each release.

`CART-README.md` is the text the cart shows on the plugin's page. This
README starts with the same text.

## License

The plugin is under the BSD 3-Clause license (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
