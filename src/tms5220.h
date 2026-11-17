// tms5220.h -- the TMS5220 speech synthesizer, as Star Wars uses it (Speak
// External: the sound CPU feeds LPC frames through the 16-byte FIFO).
//
// A C port of MAME's tms5220.cpp (BSD-3-Clause; Frank Palazzolo, Aaron
// Giles, Jonathan Gevaryahu, Raphael Nabet, Couriersud, Michael Zapf) for
// the TMS5220 variant with its /RS and /WS timing; the coefficient tables
// are MAME's tms5110r.hxx (the chip's ROM). No speech ROM (VSM) interface.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TMS_FIFO 16

typedef struct {
    uint8_t fifo[TMS_FIFO];
    uint8_t fifo_head, fifo_tail, fifo_count, fifo_bits_taken;
    bool prev_talk, SPEN, DDIS, TALK, TALKD, buffer_low, buffer_empty, irq_pin, ready_pin;
    bool OLDE, OLDP, zpar, uv_zpar, inhibit, pitch_zero, RDB_flag, io_ready, data_latched;
    uint8_t new_energy_idx, new_pitch_idx, new_k_idx[10];
    int16_t current_energy, current_pitch, current_k[10], previous_energy;
    uint8_t subcycle, subc_reload, PC, IP;
    uint16_t RNG;
    int16_t excitation, pitch_count;
    int32_t u[11], x[10];
    uint8_t rs_ws, write_latch, read_latch, command, read_byte;
    uint64_t timer_at;              // the READY timer: when it fires (0: not running)
} tms5220_t;

void tms5220_reset(tms5220_t *t);
// the /RS and /WS lines (0 = active), at time now (in the caller's clock);
// rs_ticks and ws_ticks: how long READY stays away after each (13 us, and 16
// of the chip's clocks)
void tms5220_rsq_w(tms5220_t *t, int state, uint64_t now, uint32_t rs_ticks);
void tms5220_wsq_w(tms5220_t *t, int state, uint64_t now, uint32_t ws_ticks);
void tms5220_data_w(tms5220_t *t, uint8_t data);
uint8_t tms5220_status_r(tms5220_t *t);
bool tms5220_readyq(tms5220_t *t);          // the /READY pin (true: not ready)
// the READY timer, when now has reached it
void tms5220_timer(tms5220_t *t, uint64_t now, uint32_t ws_ticks);
// one sample (the chip makes 8000 a second at 640 kHz; Star Wars clocks it
// at 672 kHz: 8400), -32768..32767
int16_t tms5220_sample(tms5220_t *t);
static inline bool tms5220_talking(const tms5220_t *t) { return t->SPEN || t->TALKD; }
