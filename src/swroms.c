// swroms.c -- see swroms.h.
#include "swroms.h"

#include <string.h>

#include "swzip.h"

bool swroms_size_ok(uint32_t n) {
    for (int i = 0; i < SW_ROM_FILES; i++)
        if (sw_rom_files[i].size == n) return true;
    return n >= 22 && n <= SWROMS_FILE_MAX;
}

static bool take_rom(const uint8_t *data, uint32_t n, uint32_t crc, uint32_t *found) {
    for (int i = 0; i < SW_ROM_FILES; i++)
        if (sw_rom_files[i].size == n && sw_rom_files[i].crc == crc) {
            memcpy(sw_rom_place(i), data, n);
            *found |= 1u << i;
            return true;
        }
    return false;
}

void swroms_take(const uint8_t *data, uint32_t n, uint32_t *found, uint8_t *scratch) {
    for (int i = 0; i < SW_ROM_FILES; i++)
        if (sw_rom_files[i].size == n) {
            if (take_rom(data, n, sw_crc32(0, data, n), found)) return;
            break;
        }
    if (n < 22 || data[0] != 'P' || data[1] != 'K') return;
    swzip_entry_t e;
    for (int i = 0; swzip_entry(data, n, i, &e); i++) {
        if (e.usize > SWROMS_SCRATCH) continue;
        bool known = false;             // by the directory's size and CRC, before unpacking it
        for (int k = 0; k < SW_ROM_FILES; k++)
            if (sw_rom_files[k].size == e.usize && sw_rom_files[k].crc == e.crc && !(*found & 1u << k)) known = true;
        if (!known || !swzip_read(&e, scratch)) continue;
        take_rom(scratch, e.usize, sw_crc32(0, scratch, e.usize), found);
    }
}
