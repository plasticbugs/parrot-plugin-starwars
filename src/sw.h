// sw.h -- Atari's Star Wars (1983) arcade board: the game's 6809 with the
// matrix processor ("math box"), the divider and the Analog Vector
// Generator; the sound board's 6809 with its RIOT, four POKEYs and the
// TMS5220. Portable C: the plugin and the Mac tests share it.
//
// MAME's driver was the reference throughout (BSD-3-Clause):
// src/mame/atari/starwars.cpp and starwars_m.cpp (Steve Baines, Frank
// Palazzolo), src/devices/video/avgdvg.cpp (Mathis Rosenhauer; thanks to
// Eric Smith, Brad Oliver, Bernd Wiebelt, Aaron Giles, Andrew Caldwell),
// machine/mos6530.cpp, x2212.cpp, adc0808.cpp. The matrix processor and the
// vector generator's state machine are ports of MAME's.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "tms5220.h"

// The ROM set (MAME's "starwars"), 73,984 bytes in 13 files: the user's own,
// read from the card. Each is known by its size and CRC-32, whatever its
// name, and goes straight to its place in the machine (sw_rom_place); the
// tests keep a whole set in a sw_roms_t (sw_load_set).
//
// SW_ESB: the board as The Empire Strikes Back (1985) has it: MAME's "esb"
// set, 139,520 bytes in 14 files. The game's ROM is banked three ways (two
// pages at $6000, two at $A000-$FFFF, and four at $8000 chosen by the
// 137412-101 "slapstic"), and the sound board's ROM is twice the size.
#ifdef SW_ESB
typedef struct {
    uint8_t main[0x18000];      // 136031-101, -102, -203, -104, -105, -106, 16K each
    uint8_t snd[0x8000];        // 136031-113, -112
    uint8_t vec[0x1000];        // 136031-111
    uint8_t avg[0x100];         // 136021-109
    uint8_t math[0x1000];       // 136031-110, -109, -108, -107
} sw_roms_t;
#define SW_ROM_FILES 14
#else
typedef struct {
    uint8_t main[0x8000];       // $8000-$FFFF: 136021.102, .203, .104, .206
    uint8_t bank[0x4000];       // $6000-$7FFF, two pages: 136021.214
    uint8_t vec[0x1000];        // $3000-$3FFF, the vector ROM: 136021-105 (4K)
    uint8_t snd[0x4000];        // the sound CPU's $4000 (and $C000): 136021-107, 136021-208
    uint8_t avg[0x100];         // the vector generator's state PROM: 136021-109
    uint8_t math[0x1000];       // the matrix processor's microcode: 136021-110 to -113
} sw_roms_t;
#define SW_ROM_FILES 13
#endif

typedef struct {
    uint32_t crc;
    uint32_t size;
    uint32_t at;                // its place in sw_roms_t
    const char *name;           // MAME's name for it
} sw_rom_file_t;
extern const sw_rom_file_t sw_rom_files[SW_ROM_FILES];
uint8_t *sw_rom_place(int i);   // where file i's bytes go, before sw_init()
// the game CPU's 64K, free until sw_init() but for the ROMs' places from
// $3000 and $8000 up: $4000-$7FFF is a loader's to use
// (SW_ESB: the picture's vectors' room, free until the game runs; and
// sw_rom_store(), the ROMs' own room, for whatever wants it before they are
// loaded or once they can't be)
uint8_t *sw_loader_scratch(void);
#ifdef SW_ESB
#define SW_LOADER_SCRATCH 20000
#define SW_ROM_STORE 0x20000
uint8_t *sw_rom_store(void);
#else
#define SW_LOADER_SCRATCH 0x4000
#endif
uint32_t sw_crc32(uint32_t crc, const uint8_t *d, uint32_t n);    // start with 0

// buttons (true = pressed) and the yoke (0-255, 128 the middle)
typedef struct {
    bool coin1, coin2, service, fire_l, fire_r, thumb_l, thumb_r, aux_coin, self_test;
    uint8_t yoke_x, yoke_y;     // x: left 0 .. right 255; y: as the pots read (MAME's STICKY)
} sw_inputs_t;

// a vector as the generator drew it: from (x0, y0) to (x1, y1), in sixteenths
// of MAME's screen units (the visible picture is 0-250 across, 0-280 down)
typedef struct {
    int16_t x0, y0, x1, y1;
    uint8_t colour;             // bit 2 red, 1 green, 0 blue
    uint8_t z;                  // brightness, 1-255
} sw_vec_t;
#ifdef SW_ESB
#define SW_VEC_MAX 2000         // (its largest picture seen: 1,206)
#else
#define SW_VEC_MAX 3000
#endif
#define SW_SCREEN_W 250
#define SW_SCREEN_H 280

// the POKEYs' registers as last written: AUDF1 AUDC1 ... AUDF4 AUDC4, AUDCTL
typedef struct {
    uint8_t reg[4][16];
} sw_pokeys_t;

#define SW_CPU_HZ   1512000     // both 6809s: 12.096 MHz / 8
#define SW_TMS_DIV  180         // CPU cycles a speech sample (8400 a second)
// a TV frame (the console's 59.9227 a second) of the board's clock, << 8
#define SW_FRAME_CYCLES_Q8 ((uint32_t)((uint64_t)SW_CPU_HZ * 256 * 10000 / 599227))
#define SW_SPEECH_MAX 512       // speech samples kept between two sw_speech() calls

void sw_load_set(const sw_roms_t *roms);  // a whole set to its places (tests)
void sw_init(void);                       // once the ROMs are in their places; then sw_reset()
void sw_reset(void);
void sw_inputs(const sw_inputs_t *in);
void sw_run(uint32_t cycles);             // both CPUs, cycles of the 1.512 MHz clock

// The picture: a count that goes up when the generator has drawn a new one,
// and its vectors (valid until the next sw_run).
uint32_t sw_picture(const sw_vec_t **v, int *n);
const sw_pokeys_t *sw_pokeys(void);
// speech samples made since the last call (at most SW_SPEECH_MAX, the newest
// kept), -32768..32767; *talking: the chip is speaking now
int sw_speech(int16_t *out, int max, bool *talking);

// the X2212's 256 nibbles (high scores, settings): its EEPROM half, and
// whether the game has stored into it since sw_nvram_clean()
uint8_t *sw_nvram(void);
bool sw_nvram_dirty(void);
void sw_nvram_clean(void);
void sw_nvram_recall(void);               // after loading sw_nvram() from a file

// for tests and the note: where the CPUs are
typedef struct {
    uint64_t main_cycles, snd_cycles;
    uint16_t main_pc, snd_pc;
    uint32_t vg_runs, math_runs, irqs, latch_main, latch_snd, bank_switches;
#ifdef SW_ESB
    uint32_t slap_switches, slap_alt, slap_bit;    // the slapstic's page changes, and its two long ways started
#endif
    uint64_t vg_steps, math_steps;      // the vector generator's states, the matrix processor's instructions
} sw_stats_t;
void sw_stats(sw_stats_t *s);
uint8_t sw_peek_main(uint16_t a);
uint8_t sw_peek_snd(uint16_t a);
