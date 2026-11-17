// tms5220.c -- see tms5220.h. MAME's tms5220.cpp, for the one chip and the
// one way Star Wars drives it.
#include "tms5220.h"

#include <string.h>

#define NOCOMMAND 0xFF

static const uint8_t kbits[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
static const uint8_t energytable[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};
static const uint8_t pitchtable[64] = {
    0,   15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  44,  46,  48,
    50,  52,  53,  56,  58,  60,  62,  65,  68,  70,  72,  76,  78,  80,  84,  86,
    91,  94,  98,  101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159};
static const int16_t ktable[10][32] = {
    {-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
     -412, -380, -339, -288, -227, -158, -81,  -1,   80,   157,  226,  287,  337,  379,  411,  436},
    {-328, -303, -274, -244, -211, -175, -138, -99,  -59,  -18,  24,   64,   105,  143,  180,  215,
     248,  278,  306,  331,  354,  374,  392,  408,  422,  435,  445,  455,  463,  470,  476,  506},
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409},
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307}};
static const int8_t chirptable[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b, 0x13,
    0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};
static const uint8_t interp_coeff[8] = {0, 3, 3, 3, 2, 2, 1, 1};

static void process_command(tms5220_t *t, uint8_t cmd);
static uint8_t status_read(tms5220_t *t, bool clear_int);

static void zero_new_frame(tms5220_t *t) {
    t->zpar = t->uv_zpar = true;
    t->OLDE = t->OLDP = true;
    t->new_energy_idx = 0;
    t->new_pitch_idx = 0;
    for (int i = 0; i < 4; i++) t->new_k_idx[i] = 0;
    for (int i = 4; i < 7; i++) t->new_k_idx[i] = 0xF;
    for (int i = 7; i < 10; i++) t->new_k_idx[i] = 0x7;
}

static void update_fifo_status_and_ints(tms5220_t *t) {
    t->ready_pin = t->io_ready;
    if (t->fifo_count <= 8) {
        if (!t->buffer_low) { t->buffer_low = true; t->irq_pin = true; }
    } else {
        t->buffer_low = false;
    }
    if (t->fifo_count == 0) {
        if (!t->buffer_empty) { t->buffer_empty = true; t->irq_pin = true; }
        if (t->DDIS) t->TALK = t->SPEN = false;
    } else {
        t->buffer_empty = false;
    }
    if (t->prev_talk && !tms5220_talking(t)) {
        t->irq_pin = true;
        t->DDIS = false;
        t->prev_talk = false;
        if (t->command != NOCOMMAND) {
            process_command(t, t->command);
            if (t->data_latched) t->timer_at = 1;       // (MAME: 16 clocks on; here, the next tms5220_timer())
            else t->io_ready = true;
            t->ready_pin = t->io_ready;
        }
    }
    t->prev_talk = tms5220_talking(t);
    if (t->rs_ws == 0x01 && !t->RDB_flag) t->read_latch = status_read(t, false);
}

static void data_write(tms5220_t *t, uint8_t data) {
    bool old_low = t->buffer_low;
    if (t->DDIS) {                          // Speak External: into the FIFO
        if (t->fifo_count < TMS_FIFO) {
            t->fifo[t->fifo_tail] = data;
            t->fifo_tail = (uint8_t)((t->fifo_tail + 1) % TMS_FIFO);
            t->fifo_count++;
            update_fifo_status_and_ints(t);
            if (!t->SPEN && old_low && !t->buffer_low) {
                zero_new_frame(t);
                t->SPEN = true;
                t->TALK = true;             // (MAME's FAST_START_HACK)
            }
        }
        t->data_latched = false;
    } else {
        process_command(t, data);
    }
    if (!t->data_latched) t->io_ready = true;
}

static int read_bits(tms5220_t *t, int count) {
    int val = 0;
    if (!t->DDIS) return (1 << count) - 1;  // (no speech ROM)
    while (count--) {
        val = (val << 1) | ((t->fifo[t->fifo_head] >> t->fifo_bits_taken) & 1);
        if (++t->fifo_bits_taken >= 8) {
            t->fifo_count--;
            t->fifo[t->fifo_head] = 0;
            t->fifo_head = (uint8_t)((t->fifo_head + 1) % TMS_FIFO);
            t->fifo_bits_taken = 0;
            update_fifo_status_and_ints(t);
        }
    }
    return val;
}

static uint8_t status_read(tms5220_t *t, bool clear_int) {
    if (t->RDB_flag) {
        t->RDB_flag = false;
        return t->read_byte;
    }
    if (clear_int) t->irq_pin = false;
    return (uint8_t)(tms5220_talking(t) << 7 | t->buffer_low << 6 | t->buffer_empty << 5);
}

static void process_command(tms5220_t *t, uint8_t cmd) {
    t->command = cmd;
    switch (cmd & 0x70) {
    case 0x10:                              // read byte
        if (!tms5220_talking(t)) {
            t->read_byte = (uint8_t)read_bits(t, 8);
            t->RDB_flag = true;
            t->command = NOCOMMAND;
        }
        break;
    case 0x00: case 0x20:                   // (set rate: the 5220C's)
        t->command = NOCOMMAND;
        break;
    case 0x30:                              // read and branch
        if (!tms5220_talking(t)) {
            t->RDB_flag = false;
            t->command = NOCOMMAND;
        }
        break;
    case 0x40:                              // load address
        if (!tms5220_talking(t)) t->command = NOCOMMAND;
        break;
    case 0x50:                              // speak (from the speech ROM there isn't)
        t->SPEN = true;
        t->TALK = true;
        t->DDIS = false;
        zero_new_frame(t);
        t->command = NOCOMMAND;
        break;
    case 0x60:                              // speak external
        memset(t->fifo, 0, sizeof t->fifo);
        t->fifo_head = t->fifo_tail = t->fifo_count = t->fifo_bits_taken = 0;
        t->DDIS = true;
        zero_new_frame(t);
        t->RDB_flag = false;
        t->command = NOCOMMAND;
        break;
    case 0x70: {                            // reset
        uint8_t rs_ws = t->rs_ws;
        uint64_t at = t->timer_at;
        tms5220_reset(t);
        t->rs_ws = rs_ws;
        t->timer_at = at;
        t->command = NOCOMMAND;
        break;
    }
    }
    update_fifo_status_and_ints(t);
}

static void parse_frame(tms5220_t *t) {
    t->uv_zpar = t->zpar = false;
    t->IP = 0;
    update_fifo_status_and_ints(t);
    if (t->DDIS && t->buffer_empty) return;
    t->new_energy_idx = (uint8_t)read_bits(t, 4);
    update_fifo_status_and_ints(t);
    if (t->DDIS && t->buffer_empty) return;
    if (t->new_energy_idx == 0 || t->new_energy_idx == 15) return;
    int rep = read_bits(t, 1);
    t->new_pitch_idx = (uint8_t)read_bits(t, 6);
    t->uv_zpar = t->new_pitch_idx == 0;
    update_fifo_status_and_ints(t);
    if (t->DDIS && t->buffer_empty) return;
    if (rep) return;
    for (int i = 0; i < 4; i++) {
        t->new_k_idx[i] = (uint8_t)read_bits(t, kbits[i]);
        update_fifo_status_and_ints(t);
        if (t->DDIS && t->buffer_empty) return;
    }
    if (t->new_pitch_idx == 0) return;
    for (int i = 4; i < 10; i++) {
        t->new_k_idx[i] = (uint8_t)read_bits(t, kbits[i]);
        update_fifo_status_and_ints(t);
        if (t->DDIS && t->buffer_empty) return;
    }
}

// (MAME wraps a to 10 bits and b to 15 with loops; the same, in one step each)
static inline int32_t matrix_multiply(int32_t a, int32_t b) {
    a = (int32_t)(((uint32_t)a + 512) & 1023) - 512;
    b = (int32_t)(((uint32_t)b + 16384) & 32767) - 16384;
    return (a * b) >> 9;
}

static int32_t lattice_filter(tms5220_t *t) {
    int32_t *u = t->u, *x = t->x;
    const int16_t *k = t->current_k;
    u[10] = matrix_multiply(t->previous_energy, t->excitation << 6);
    for (int i = 9; i >= 0; i--) u[i] = u[i + 1] - matrix_multiply(k[i], x[i]);
    for (int i = 9; i >= 1; i--) x[i] = x[i - 1] + matrix_multiply(k[i - 1], u[i - 1]);
    x[0] = u[0];
    t->previous_energy = t->current_energy;
    return u[0];
}

static int16_t clip_analog(int32_t c) {
    if (c > 2047) c = 2047;
    else if (c < -2048) c = -2048;
    c &= ~0xF;
    return (int16_t)((c << 4) | ((c & 0x7F0) >> 3) | ((c & 0x400) >> 10));
}

int16_t tms5220_sample(tms5220_t *t) {
    int16_t out;
    if (t->TALKD) {
        if (t->IP == 0 && t->PC == 12 && t->subcycle == 1) {
            parse_frame(t);                 // (sets IP = 0: this chip's reload)
            if (t->new_energy_idx == 0x0F) {
                t->TALK = t->SPEN = false;
                update_fifo_status_and_ints(t);
            }
            bool new_unv = t->new_pitch_idx == 0, new_sil = t->new_energy_idx == 0;
            t->inhibit = (!t->OLDP && new_unv) || (t->OLDP && !new_unv) || (t->OLDE && !new_sil) || (t->OLDP && new_sil);
        } else {
            bool inh = t->inhibit && t->IP != 0;
            int sh = interp_coeff[t->IP];
            if (t->subcycle == 2) {
                switch (t->PC) {
                case 0:
                    if (t->IP == 0) t->pitch_zero = false;
                    t->current_energy = (int16_t)((t->current_energy + (((energytable[t->new_energy_idx] - t->current_energy) * (1 - inh)) >> sh)) * (1 - t->zpar));
                    break;
                case 1:
                    t->current_pitch = (int16_t)((t->current_pitch + (((pitchtable[t->new_pitch_idx] - t->current_pitch) * (1 - inh)) >> sh)) * (1 - t->zpar));
                    break;
                case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: {
                    int i = t->PC - 2;
                    t->current_k[i] = (int16_t)((t->current_k[i] + (((ktable[i][t->new_k_idx[i]] - t->current_k[i]) * (1 - inh)) >> sh)) * (1 - (i < 4 ? t->zpar : t->uv_zpar)));
                    break;
                }
                default: break;
                }
            }
        }
        if (t->OLDP) t->excitation = (t->RNG & 1) ? ~0x3F : 0x40;
        else t->excitation = chirptable[t->pitch_count >= 51 ? 51 : t->pitch_count];
        for (int i = 0; i < 20; i++) {
            int bitout = ((t->RNG >> 12) ^ (t->RNG >> 3) ^ (t->RNG >> 2) ^ t->RNG) & 1;
            t->RNG = (uint16_t)(t->RNG << 1 | bitout);
        }
        int32_t s = lattice_filter(t);
        s = (int32_t)(((uint32_t)s + 16384) & 32767) - 16384;
        out = clip_analog(s);
        t->subcycle++;
        if (t->subcycle == 2 && t->PC == 12) {
            if (t->IP == 7 && t->inhibit) t->pitch_zero = true;
            if (t->IP == 7) {
                t->OLDE = t->new_energy_idx == 0;
                t->OLDP = t->new_pitch_idx == 0;
                if (!t->TALK && !t->SPEN) t->TALKD = t->TALK;
                update_fifo_status_and_ints(t);
                if (!t->TALK && t->SPEN) t->TALK = true;
            }
            t->subcycle = t->subc_reload;
            t->PC = 0;
            t->IP = (uint8_t)((t->IP + 1) & 7);
        } else if (t->subcycle == 3) {
            t->subcycle = t->subc_reload;
            t->PC++;
        }
        t->pitch_count++;
        if (t->pitch_count >= t->current_pitch || t->pitch_zero) t->pitch_count = 0;
        t->pitch_count &= 0x1FF;
    } else {
        t->subcycle++;
        if (t->subcycle == 2 && t->PC == 12) {
            if (t->IP == 7) {
                t->TALKD = t->TALK;
                update_fifo_status_and_ints(t);
                if (!t->TALK && t->SPEN) t->TALK = true;
            }
            t->subcycle = t->subc_reload;
            t->PC = 0;
            t->IP = (uint8_t)((t->IP + 1) & 7);
        } else if (t->subcycle == 3) {
            t->subcycle = t->subc_reload;
            t->PC++;
        }
        out = -1;
    }
    return out;
}

void tms5220_reset(tms5220_t *t) {
    memset(t, 0, sizeof *t);
    t->buffer_empty = t->buffer_low = true;
    t->command = NOCOMMAND;
    t->inhibit = true;
    t->subc_reload = 1;
    t->OLDE = t->OLDP = true;
    t->RNG = 0x1FFF;
    t->rs_ws = 3;
    t->io_ready = true;                     // (as MAME's device_start leaves it)
}

void tms5220_timer(tms5220_t *t, uint64_t now, uint32_t ws_ticks) {
    if (!t->timer_at || now < t->timer_at) return;
    t->timer_at = 0;
    switch (t->rs_ws) {
    case 0x02:                              // a write
        if (t->fifo_count >= TMS_FIFO && t->DDIS) {     // the FIFO is full: try again
            t->timer_at = now + ws_ticks;
            break;
        }
        if (t->command == NOCOMMAND) {
            t->data_latched = false;
            data_write(t, t->write_latch);
        }
        break;
    case 0x01:                              // a read
        t->read_latch = status_read(t, true);
        t->io_ready = true;
        break;
    default:
        t->io_ready = true;
        break;
    }
    t->ready_pin = t->io_ready;
}

static void line(tms5220_t *t, uint8_t new_val, int state, uint64_t now, uint32_t ticks) {
    if (new_val == t->rs_ws) return;
    t->rs_ws = new_val;
    if (new_val == 0) return;
    if (new_val == 3) { t->read_latch = 0xFF; return; }
    if (!state) {
        t->io_ready = false;
        t->ready_pin = false;
        t->timer_at = now + ticks;
    }
}

void tms5220_rsq_w(tms5220_t *t, int state, uint64_t now, uint32_t rs_ticks) {
    state &= 1;
    line(t, (uint8_t)((t->rs_ws & 1) | state << 1), state, now, rs_ticks);
}

void tms5220_wsq_w(tms5220_t *t, int state, uint64_t now, uint32_t ws_ticks) {
    state &= 1;
    line(t, (uint8_t)((t->rs_ws & 2) | state), state, now, ws_ticks);
}

void tms5220_data_w(tms5220_t *t, uint8_t data) {
    t->write_latch = data;
    t->data_latched = true;
}

uint8_t tms5220_status_r(tms5220_t *t) {
    return t->rs_ws == 0x01 ? t->read_latch : 0xFF;
}

bool tms5220_readyq(tms5220_t *t) {
    return !t->io_ready;
}
