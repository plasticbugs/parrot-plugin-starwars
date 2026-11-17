// swroms.h -- the ROM set from whatever the card has: loose files or a zip
// (MAME's starwars.zip), each ROM known by its size and CRC-32, never by
// its name (old dumps name them differently). Portable.
#pragma once
#include "sw.h"

// A file's bytes: if it is one of the set's ROMs it goes to its place in the
// machine (sw_rom_place); if it is a zip, every ROM in it does. *found: a
// bit for each of sw_rom_files[] in place so far. scratch: SWROMS_SCRATCH
// bytes (a zip's entry is unpacked there; sw_loader_scratch() will do).
#define SWROMS_SCRATCH 0x4000
#define SWROMS_ALL ((1u << SW_ROM_FILES) - 1)
void swroms_take(const uint8_t *data, uint32_t n, uint32_t *found, uint8_t *scratch);
// could a file of this size be one of the ROMs (or a zip of them)?
bool swroms_size_ok(uint32_t n);
#define SWROMS_FILE_MAX 0x40000     // a zip of the set and its clones' files
