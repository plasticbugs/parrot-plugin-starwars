// swcard.c -- see swcard.h.
#include "swcard.h"

#include <string.h>

#define NV_MAGIC 0x3157534Eu        // "NSW1", then the 256 nibbles, then the settings
#define MAX_FILES 48

static uint8_t sec[512];            // (not on the stack: the plugin's is 2K)

#ifndef SW_ESB
static bool read_file(fs_t *fs, const fs_file_t *f, uint8_t *buf, uint32_t n, void (*tick)(void)) {
    if (fs_open(fs, f) != FS_OK) return false;
    for (uint32_t at = 0; at < n; at += 512) {
        uint32_t lba = fs_lba(fs, at / 512);
        if (!lba || !fs->read(lba, sec)) return false;
        memcpy(buf + at, sec, n - at < 512 ? n - at : 512);
        if (tick) tick();
    }
    return true;
}
#endif

#ifdef SW_ESB
#include "swzip.h"

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

// n bytes of the open file from pos
static bool read_at(fs_t *fs, uint32_t pos, uint8_t *dst, uint32_t n, void (*tick)(void)) {
    while (n) {
        uint32_t lba = fs_lba(fs, pos / 512), o = pos % 512, k = 512 - o < n ? 512 - o : n;
        if (!lba || !fs->read(lba, sec)) return false;
        memcpy(dst, sec + o, k);
        dst += k;
        pos += k;
        n -= k;
        if (tick) tick();
    }
    return true;
}

// which of the set's ROMs still to find has this size and CRC-32 (-1: none)
static int wanted(uint32_t size, uint32_t crc, uint32_t found) {
    for (int i = 0; i < SW_ROM_FILES; i++)
        if (sw_rom_files[i].size == size && sw_rom_files[i].crc == crc && !(found & 1u << i)) return i;
    return -1;
}

// A zip: its end record and directory (the last of the file), then every
// entry the directory says is a ROM still wanted, unpacked to its place and
// taken if its CRC-32 is right.
static void take_zip(fs_t *fs, uint32_t size, uint8_t *buf, uint32_t max, void (*tick)(void), uint32_t *found) {
    uint32_t tail = size < 1024 ? size : 1024, end;
    if (tail < 22 || !read_at(fs, size - tail, buf, tail, tick)) return;
    for (end = tail - 22;; end--) {
        if (le32(buf + end) == 0x06054B50) break;
        if (!end) return;
    }
    uint32_t count = le16(buf + end + 10), cd_size = le32(buf + end + 12), cd_at = le32(buf + end + 16);
    static struct { uint32_t local, csize; uint16_t method; int8_t rom; } want[SW_ROM_FILES];   // (static: the stack is 2K)
    int n = 0;
    if (cd_size > max || cd_at + cd_size > size || !read_at(fs, cd_at, buf, cd_size, tick)) return;
    for (uint32_t at = 0, k = 0; k < count && at + 46 <= cd_size && n < SW_ROM_FILES; k++) {
        const uint8_t *e = buf + at;
        if (le32(e) != 0x02014B50) break;
        int rom = wanted(le32(e + 24), le32(e + 16), *found);
        bool again = false;
        for (int j = 0; j < n; j++) again |= want[j].rom == rom;
        if (rom >= 0 && !again && le32(e + 20) <= max) {
            want[n].local = le32(e + 42);
            want[n].csize = le32(e + 20);
            want[n].method = (uint16_t)le16(e + 10);
            want[n++].rom = (int8_t)rom;
        }
        at += 46 + le16(e + 28) + le16(e + 30) + le16(e + 32);
    }
    for (int j = 0; j < n; j++) {
        static uint8_t head[30];
        uint8_t *place = sw_rom_place(want[j].rom);
        uint32_t usize = sw_rom_files[want[j].rom].size;
        if (want[j].local + 30 > size || !read_at(fs, want[j].local, head, 30, tick) || le32(head) != 0x04034B50) continue;
        uint32_t data = want[j].local + 30 + le16(head + 26) + le16(head + 28);
        if (data + want[j].csize > size || !read_at(fs, data, buf, want[j].csize, tick)) continue;
        bool ok = want[j].method == 8 ? swzip_inflate(buf, want[j].csize, place, usize)
                : want[j].method == 0 && want[j].csize == usize ? (memcpy(place, buf, usize), true) : false;
        if (ok && sw_crc32(0, place, usize) == sw_rom_files[want[j].rom].crc) *found |= 1u << want[j].rom;
    }
}

// a file of the folder: a ROM by itself (read, known by its CRC-32, put in
// its place), or a zip of them
static void take(fs_t *fs, const fs_file_t *f, uint8_t *buf, uint32_t max, void (*tick)(void), uint32_t *found) {
    uint32_t n = (uint32_t)f->size;
    if (f->is_dir || f->size > 0x1000000 || n < 22 || fs_open(fs, f) != FS_OK) return;
    bool rom_size = false;
    for (int i = 0; i < SW_ROM_FILES; i++) rom_size |= sw_rom_files[i].size == n;
    if (rom_size && n <= max) {
        if (!read_at(fs, 0, buf, n, tick)) return;
        int rom = wanted(n, sw_crc32(0, buf, n), *found);
        if (rom >= 0) {
            memcpy(sw_rom_place(rom), buf, n);
            *found |= 1u << rom;
            return;
        }
        if (buf[0] != 'P' || buf[1] != 'K') return;
    } else {
        uint8_t magic[4];
        if (!read_at(fs, 0, magic, 4, tick) || le32(magic) != 0x04034B50) return;
    }
    take_zip(fs, n, buf, max, tick, found);
}
#else
static void take(fs_t *fs, const fs_file_t *f, uint8_t *buf, uint32_t max, void (*tick)(void), uint32_t *found) {
    uint32_t n = (uint32_t)f->size;
    if (f->is_dir || f->size > max || !swroms_size_ok(n)) return;
    if (read_file(fs, f, buf, n, tick)) swroms_take(buf, n, found, sw_loader_scratch());
}

#endif

#ifdef SW_ESB
// (each file as the listing comes to it: a listing keeps its own place
// through a file's reads, fsro.h, and there is no RAM for a list of them)
uint32_t swcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void)) {
    static fs_dir_t dir;                // (too big for the stack)
    uint32_t found = 0;
    fs_file_t e;
    if (fs_dir_open_path(fs, &dir, SWCARD_HOME) == FS_OK)
        while (found != SWROMS_ALL && fs_dir_next(fs, &dir, &e) == 1) take(fs, &e, buf, max, tick, &found);
    if (found != SWROMS_ALL && fs_find(fs, SWCARD_ZIP, &e) == FS_OK) take(fs, &e, buf, max, tick, &found);
    return found;
}
#else
uint32_t swcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void)) {
    static fs_dir_t dir;                // (too big for the stack)
    static fs_file_t files[MAX_FILES];
    uint32_t found = 0;
    int n = 0;
    fs_file_t e;
    if (fs_dir_open_path(fs, &dir, SWCARD_HOME) == FS_OK)       // the folder's listing first: reading a file moves fsro's map
        while (n < MAX_FILES && fs_dir_next(fs, &dir, &e) == 1)
            if (!e.is_dir && e.size <= max && swroms_size_ok((uint32_t)e.size)) files[n++] = e;
    for (int i = 0; i < n && found != SWROMS_ALL; i++) take(fs, &files[i], buf, max, tick, &found);
    if (found != SWROMS_ALL && fs_find(fs, SWCARD_ZIP, &e) == FS_OK) take(fs, &e, buf, max, tick, &found);
    return found;
}
#endif

bool swcard_nv_load(fs_t *fs, uint8_t *opts) {
    fs_file_t f;
    uint32_t magic;
    if (fs_find(fs, SWCARD_HOME "/" SWCARD_NV, &f) != FS_OK || f.is_dir || f.size < 512 || fs_open(fs, &f) != FS_OK) return false;
    uint32_t lba = fs_lba(fs, 0);
    if (!lba || !fs->read(lba, sec)) return false;
    memcpy(&magic, sec, 4);
    if (magic != NV_MAGIC) return false;
    memcpy(sw_nvram(), sec + 4, 256);
    memcpy(opts, sec + 260, SWCARD_OPTS);
    sw_nvram_recall();
    return true;
}

bool swcard_nv_save(fs_t *fs, const uint8_t *opts) {
    static const char nm[] = SWCARD_NV;
    uint16_t name[sizeof nm];
    int n = 0;
    for (; nm[n]; n++) name[n] = (uint16_t)nm[n];
    fs_file_t f;
    bool made;
    uint32_t magic = NV_MAGIC;
    if (fs_create(fs, SWCARD_HOME, name, n, 512, &f, &made) != FS_OK || fs_open(fs, &f) != FS_OK) return false;
    memset(sec, 0, sizeof sec);
    memcpy(sec, &magic, 4);
    memcpy(sec + 4, sw_nvram(), 256);
    memcpy(sec + 260, opts, SWCARD_OPTS);
    return fs_write_sec(fs, 0, sec) == FS_OK;
}
