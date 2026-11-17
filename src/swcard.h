// swcard.h -- the plugin's files on the card, through fsro: the ROM set
// (loose files or a zip in /STARWARS, or /STARWARS.ZIP) and the game's
// X2212 (high scores, settings) in /STARWARS/STARWARS.NV. Portable: the
// plugin and the Mac test (test/card.sh, on disk images) share it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../sdplayer/fsro.h"
#include "swroms.h"

#ifdef SW_ESB                           // The Empire Strikes Back: /ESB, /ESB.ZIP, /ESB/ESB.NV
#define SWCARD_HOME "/ESB"
#define SWCARD_ZIP  "/ESB.ZIP"
#define SWCARD_NV   "ESB.NV"
#else
#define SWCARD_HOME "/STARWARS"
#define SWCARD_ZIP  "/STARWARS.ZIP"
#define SWCARD_NV   "STARWARS.NV"
#endif

// Every file in SWCARD_HOME that could be a ROM or a zip of them, then
// SWCARD_ZIP, until the set is whole: each ROM to its place (sw_rom_place).
// buf: max bytes to read a file into (a file larger is passed over); tick:
// called between sectors (the plugin serves the console), or NULL. Returns
// swroms' found bits (SWROMS_ALL: the whole set).
// (SW_ESB: a zip is never read whole, as its 78K has nowhere to go: its
// directory, then each ROM's packed bytes into buf and unpacked straight to
// its place. buf must take a 16K ROM, or the most a zip packs one into.)
uint32_t swcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void));

// The X2212 from its file into the machine (after sw_init), with the
// plugin's own settings (SWCARD_OPTS bytes: the picture's colour, the yoke's
// way up): false, and opts untouched, if there is no file or it isn't one.
#define SWCARD_OPTS 4
bool swcard_nv_load(fs_t *fs, uint8_t *opts);
// both to the file, made if it isn't there (fs needs its writer)
bool swcard_nv_save(fs_t *fs, const uint8_t *opts);
