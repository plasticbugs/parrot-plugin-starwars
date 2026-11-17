// sw.c -- see sw.h.
#include "sw.h"

#include <string.h>

#include "m6809.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SW_HOT __attribute__((section(".time_critical.sw")))
#else
#define SW_HOT
#endif
#define M6809_HOT SW_HOT

#define MASTER_PER_CPU 8            // the 12.096 MHz master clock's ticks a CPU cycle
#define IRQ_CYCLES     6144         // the game's interrupt: 12.096 MHz / 4096 / 12 = 246 Hz
#define TMS_RS_TICKS   20           // READY after /RS: 13 us
#define TMS_WS_TICKS   36           // READY after /WS: 16 of the chip's 672 kHz clocks

#ifdef SW_ESB
const sw_rom_file_t sw_rom_files[SW_ROM_FILES] = {
    {0xef1e3ae5, 0x4000, 0x00000, "136031-101.1f"},
    {0x62ce5c12, 0x4000, 0x04000, "136031-102.1jk"},
    {0x27b0889b, 0x4000, 0x08000, "136031-203.1kl"},
    {0xfd5c725e, 0x4000, 0x0C000, "136031-104.1m"},
    {0xea9e4dce, 0x4000, 0x10000, "136031-105.3u"},
    {0x76d07f59, 0x4000, 0x14000, "136031-106.2u"},
    {0x24ae3815, 0x4000, 0x18000, "136031-113.1jk"},
    {0xca72d341, 0x4000, 0x1C000, "136031-112.1h"},
    {0xb1f9bd12, 0x1000, 0x20000, "136031-111.1l"},
    {0x82fc3eb2, 0x0100, 0x21000, "136021-109.4b"},
    {0xb8d0f69d, 0x0400, 0x21100, "136031-110.7h"},
    {0x6a2a4d98, 0x0400, 0x21500, "136031-109.7j"},
    {0x6a76138f, 0x0400, 0x21900, "136031-108.7k"},
    {0xafbf6e01, 0x0400, 0x21D00, "136031-107.7l"},
};
_Static_assert(sizeof(sw_roms_t) == 0x22100, "the ROM set is 139,520 bytes");
#else
const sw_rom_file_t sw_rom_files[SW_ROM_FILES] = {
    {0x04f1876e, 0x4000, 0x8000,  "136021.214.1f"},
    {0xf725e344, 0x2000, 0x0000,  "136021.102.1hj"},
    {0xf6da0a00, 0x2000, 0x2000,  "136021.203.1jk"},
    {0x7e406703, 0x2000, 0x4000,  "136021.104.1kl"},
    {0xc7e51237, 0x2000, 0x6000,  "136021.206.1m"},
    {0x538e7d2f, 0x1000, 0xC000,  "136021-105.1l"},
    {0xdbf3aea2, 0x2000, 0xD000,  "136021-107.1jk"},
    {0xe38070a8, 0x2000, 0xF000,  "136021-208.1h"},
    {0x82fc3eb2, 0x0100, 0x11000, "136021-109.4b"},
    {0x810e040e, 0x0400, 0x11100, "136021-110.7h"},
    {0xae69881c, 0x0400, 0x11500, "136021-111.7j"},
    {0xecf22628, 0x0400, 0x11900, "136021-112.7k"},
    {0x83febfde, 0x0400, 0x11D00, "136021-113.7l"},
};
_Static_assert(sizeof(sw_roms_t) == 0x12100, "the ROM set is 73,984 bytes");
#endif

uint32_t sw_crc32(uint32_t crc, const uint8_t *d, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

const uint8_t m6809_nz[256] = {
    CC_Z, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
#define N8 CC_N, CC_N, CC_N, CC_N, CC_N, CC_N, CC_N, CC_N
    N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8,
#undef N8
};

// ---- the machine ---------------------------------------------------------------
#ifdef SW_ESB
// The game CPU's memory below $6000 (RAM, the vector ROM); everything above
// is banked ROM, read through the page tables from esb_rom, where the files
// lie as they were read: -101 (the two pages at $6000), -102, -203, -104
// (each a page of $A000, $C000, $E000 and its other), -105, -106 (the
// slapstic's four pages at $8000), then the sound board's -113 ($4000 and
// $C000) and -112 ($6000 and $E000).
static uint8_t mm[0x6000];
static uint8_t esb_rom[SW_ROM_STORE];
#define snd_rom (esb_rom + 0x18000)
static uint8_t snd_ram[0x800];
#else
static uint8_t mm[0x10000];             // the game CPU's memory: RAM, and the ROMs' copies
static uint8_t bankrom[0x4000];
static uint8_t snd_rom[0x4000], snd_ram[0x800];
#endif
#ifdef SW_ESB
static uint8_t avg_prom[0x100];
// (the microcode's four files wait for sw_init at $4000-$4FFF of mm: it
// decodes them into math_code, and then that is the I/O's addresses and RAM)
#define math_prom (mm + 0x4000)
#else
static uint8_t avg_prom[0x100], math_prom[0x1000];
#endif
static uint16_t math_code[1024];        // the microcode: strobes << 8 | address mode << 7 | address
static uintptr_t m_rd[256], m_wr[256], s_rd[256], s_wr[256];    // a page's memory less the page's address; 0: a handler

static m6809_t maincpu, sndcpu;
static sw_inputs_t inputs;
#ifdef SW_ESB
static uint8_t in0, in1, dsw0 = 0xF3, dsw1 = 0x02;      // MAME's defaults for esb
#else
static uint8_t in0, in1, dsw0 = 0x98, dsw1 = 0x02;
#endif
static uint64_t next_irq;
static sw_stats_t stats;

// the latches between the two CPUs
static uint8_t soundlatch, mainlatch;
static bool soundlatch_pending, mainlatch_pending;

// the matrix processor and the divider
static uint16_t MPA, BIC, mp_A, mp_B, mp_C;
static int32_t mp_ACC;
static uint64_t math_until;
static uint16_t divisor, dividend, quotient_shift;
static uint32_t dvd_shift;
static uint32_t prng = 0x9D14ABD7;    // (MAME's seed, so the trace test runs on)

// X2212, the ADC, the output latch
static uint8_t nv_sram[256], nv_e2[256];
static bool nv_dirty, nv_recall_line;
static uint8_t adc_sar, adc_next, outlatch;
static uint64_t adc_at;                 // adc_next is the result from here on (0: none)

// the RIOT
static struct {
    uint8_t ram[128];
    uint8_t pa_out, pa_ddr, pb_out, pb_ddr;
    uint8_t pa7, pa7_dir;
    bool ie_timer, irq_timer, ie_edge, irq_edge, spinning;
    uint8_t shift;
    uint64_t target, timeout;
} riot;

static sw_pokeys_t pokeys;
static tms5220_t tms;
static uint64_t tms_next;               // the next speech sample's time
static int16_t speech[SW_SPEECH_MAX];
static uint32_t speech_w, speech_r;

// the vector generator (its fields are words: vg_run keeps them in registers)
typedef struct {
    uint32_t pc, dvx, dvy, stack[4], timer;
    uint32_t sp, op, state_latch, data, dvy12, int_latch, bin_scale, scale, colour, intensity, halt;
    int32_t xpos, ypos;
    // the picture being drawn
    int32_t last_x, last_y;
    uint32_t have_last, run_points, nvecs;
} vg_t;
static vg_t vg;
static uint64_t vg_halt_at;             // the halt flag is up from here on
static bool vg_running;
static sw_vec_t vecs[SW_VEC_MAX];
static uint32_t pictures;

// the speech chip up to time now (the sound CPU's clock): its READY timer,
// and a sample every SW_TMS_DIV cycles. Called before anything looks at it
// or drives it, so it needn't run in step with the CPU.
static void tms_sync(uint64_t now) {
    for (;;) {
        uint64_t ev = tms.timer_at && tms.timer_at < tms_next ? tms.timer_at : tms_next;
        if (ev > now) break;
        if (ev == tms_next) {
            speech[speech_w++ % SW_SPEECH_MAX] = tms5220_sample(&tms);
            tms_next += SW_TMS_DIV;
        } else {
            tms5220_timer(&tms, ev, TMS_WS_TICKS);
        }
    }
}

// ---- RIOT ----------------------------------------------------------------------
static void riot_irq(void) {
    bool line = (riot.ie_timer && riot.irq_timer) || (riot.ie_edge && riot.irq_edge);
    if (line && !sndcpu.irq) m6809_stop(&sndcpu);   // (the CPU looks at its lines when a run starts)
    sndcpu.irq = line;
}

static uint8_t riot_pa_in(void) {
    // bit 2: the TMS5220's /READY; bit 4: not the sound self test; bit 6: the
    // game hasn't read our last byte; bit 7: a byte from the game is waiting
    return (uint8_t)(0x3B | (tms5220_readyq(&tms) ? 0x04 : 0) | (mainlatch_pending ? 0x40 : 0) | (soundlatch_pending ? 0x80 : 0));
}

static void riot_edge(void) {
    uint8_t state = (uint8_t)((((riot.pa_out & riot.pa_ddr) | (riot_pa_in() & ~riot.pa_ddr)) >> 7) & 1);
    if ((riot.pa7 ^ state) && !(riot.pa7_dir ^ state) && !riot.irq_edge) {
        riot.irq_edge = true;
        riot_irq();
    }
    riot.pa7 = state;
}

static void riot_timer_start(uint8_t data, uint64_t now) {
    riot.spinning = false;
    riot.target = now + 1 + ((uint64_t)data << riot.shift);
}

static void riot_update(uint64_t now) {
    while (now >= riot.target) {
        if (!riot.spinning) {
            riot.timeout = riot.target;
            riot.irq_timer = true;
            riot_irq();
        }
        riot.spinning = true;
        riot.target += 256;
    }
}

static void riot_pa_out(uint64_t now) {
    uint8_t d = (uint8_t)((riot.pa_out & riot.pa_ddr) | (riot.pa_ddr ^ 0xFF));
    tms5220_wsq_w(&tms, d & 1, now, TMS_WS_TICKS);
    tms5220_rsq_w(&tms, d >> 1 & 1, now, TMS_RS_TICKS);
}

static uint8_t riot_r(uint16_t a, uint64_t now) {
    riot_update(now);
    tms_sync(now);
    if (!(a & 0x04)) {
        switch (a & 3) {
        case 0: return (uint8_t)((riot.pa_out & riot.pa_ddr) | (riot_pa_in() & ~riot.pa_ddr));
        case 1: return riot.pa_ddr;
        case 2: return (uint8_t)((riot.pb_out & riot.pb_ddr) | (tms5220_status_r(&tms) & ~riot.pb_ddr));
        default: return riot.pb_ddr;
        }
    }
    if (a & 1) {                        // the interrupt flags
        uint8_t d = (uint8_t)((riot.irq_timer ? 0x80 : 0) | (riot.irq_edge ? 0x40 : 0));
        if (riot.irq_edge) { riot.irq_edge = false; riot_irq(); }
        return d;
    }
    // the timer
    uint8_t shift = riot.spinning ? 0 : riot.shift;
    uint64_t remain = riot.target - now;
    uint8_t val = (uint8_t)(remain >> shift);
    if (!(remain & ((1u << shift) - 1))) val--;
    if (riot.timeout < now) {           // (not while the interrupt is being raised)
        riot.irq_timer = false;
        if (riot.spinning) riot_timer_start(val, now);
    }
    riot.ie_timer = (a & 0x08) != 0;
    riot_irq();
    return val;
}

static void riot_w(uint16_t a, uint8_t v, uint64_t now) {
    static const uint8_t timershift[4] = {0, 3, 6, 10};
    riot_update(now);
    tms_sync(now);
    if (!(a & 0x04)) {
        switch (a & 3) {
        case 0: riot.pa_out = v; riot_pa_out(now); riot_edge(); break;
        case 1: riot.pa_ddr = v; riot_pa_out(now); riot_edge(); break;
        case 2: riot.pb_out = v; tms5220_data_w(&tms, (uint8_t)((riot.pb_out & riot.pb_ddr) | (riot.pb_ddr ^ 0xFF))); break;
        default: riot.pb_ddr = v; tms5220_data_w(&tms, (uint8_t)((riot.pb_out & riot.pb_ddr) | (riot.pb_ddr ^ 0xFF))); break;
        }
    } else if (a & 0x10) {              // the timer
        riot.shift = timershift[a & 3];
        riot_timer_start(v, now);
        riot.irq_timer = false;
        riot.ie_timer = (a & 0x08) != 0;
        riot_irq();
    } else {                            // PA7's edge detector
        riot.pa7_dir = a & 1;
        riot.ie_edge = (a & 2) != 0;
        riot_irq();
    }
}

// ---- the matrix processor (MAME's run_mproc) and the divider ----------------------
static void SW_HOT run_mproc(void) {
    int steps = 0, mptime = 0;
    uint8_t *mathram = mm + 0x5000;
    uint32_t mpa = MPA, bic = BIC, a = mp_A, b = mp_B, acc = (uint32_t)mp_ACC;    // (in registers for the run)
    stats.math_runs++;
    for (; steps < 100000; steps++) {
        mptime += 5;
        unsigned code = math_code[mpa], ip15_8 = code >> 8, ip7 = code & 0x80, ip6_0 = code & 0x7F;
        unsigned ma = ip7 ? ip6_0 : ((ip6_0 & 3) | ((bic & 0x1FF) << 2));
        unsigned mb = ma << 1;
        unsigned ramword = (unsigned)(mathram[mb + 1] | mathram[mb] << 8);
        if (ip15_8 & 0x10) acc = 0;                             // CLEAR_ACC
        if (ip15_8 & 0x01) acc = ramword << 16;                 // LAC
        if (ip15_8 & 0x02) {                                    // READ_ACC
            mathram[mb + 1] = (uint8_t)(acc >> 16);
            mathram[mb] = (uint8_t)(acc >> 24);
        }
        if (ip15_8 & 0x08) bic = (bic + 1) & 0x1FF;             // INC_BIC
        if (ip15_8 & 0x20) {                                    // LDC: ACC += (A - B) * C
            mp_C = (uint16_t)ramword;
            acc += ((uint32_t)((int32_t)((int16_t)a - (int16_t)b) << 1) * (uint32_t)(int32_t)(int16_t)ramword) << 1;
            a = (a & 0x8000) ? 0xFFFF : 0;
            b = (b & 0x8000) ? 0xFFFF : 0;
            mptime += 33;
        }
        if (ip15_8 & 0x40) b = ramword;                         // LDB
        if (ip15_8 & 0x80) a = ramword;                         // LDA
        mpa = (mpa & 0x300) | ((mpa + 1) & 0xFF);
        if (ip15_8 & 0x04) { steps++; break; }                  // M_HALT
    }
    MPA = (uint16_t)mpa;
    BIC = (uint16_t)bic;
    mp_A = (uint16_t)a;
    mp_B = (uint16_t)b;
    mp_ACC = (int32_t)acc;
    stats.math_steps += (unsigned)steps;
    math_until = m6809_now(&maincpu) + (unsigned)(mptime / MASTER_PER_CPU) + 1;
}

static void math_w(unsigned offset, uint8_t data) {
    switch (offset) {
    case 0: MPA = (uint16_t)(data << 2); run_mproc(); break;
    case 1: BIC = (uint16_t)((BIC & 0x00FF) | ((data & 1) << 8)); break;
    case 2: BIC = (uint16_t)((BIC & 0x0100) | data); break;
    case 4:
        divisor = (uint16_t)((divisor & 0x00FF) | (data << 8));
        dvd_shift = dividend;
        quotient_shift = 0;
        break;
    case 5:
        divisor = (uint16_t)((divisor & 0xFF00) | data);
        for (int i = 1; i < 16; i++) {
            quotient_shift = (uint16_t)(quotient_shift << 1);
            if (((int32_t)dvd_shift + (divisor ^ 0xFFFF) + 1) & 0x10000) {
                quotient_shift |= 1;
                dvd_shift = (dvd_shift + (divisor ^ 0xFFFFu) + 1) << 1;
            } else {
                dvd_shift <<= 1;
            }
        }
        break;
    case 6: dividend = (uint16_t)((dividend & 0x00FF) | (data << 8)); break;
    case 7: dividend = (uint16_t)((dividend & 0xFF00) | data); break;
    default: break;
    }
}

// ---- the vector generator (MAME's avg_starwars_device) ---------------------------
#define VG_XCENTER ((SW_SCREEN_W / 2) << 16)
#define VG_YCENTER ((SW_SCREEN_H / 2) << 16)
// (op: the instruction's opcode, g->op, or a constant where it is known)
#define OP0 (op & 1)
#define OP1 (op & 2)
#define OP2 (op & 4)

static inline int16_t vg_coord(int32_t p) {     // 16.16 to sixteenths, kept in an int16_t
    int32_t v = p >> 12;
    if (v != (int16_t)v) v = v < 0 ? -32768 : 32767;
    return (int16_t)v;
}

static inline __attribute__((always_inline)) void vg_point(vg_t *g, int32_t x, int32_t y, uint32_t colour, int intensity) {
    g->run_points++;
    if (g->have_last && intensity > 0 && g->nvecs < SW_VEC_MAX) {
        sw_vec_t *v = &vecs[g->nvecs++];
        v->x0 = vg_coord(g->last_x);
        v->y0 = vg_coord(g->last_y);
        v->x1 = vg_coord(x);
        v->y1 = vg_coord(y);
        v->colour = (uint8_t)colour;
        v->z = (uint8_t)(intensity > 255 ? 255 : intensity);
    }
    g->last_x = x;
    g->last_y = y;
    g->have_last = 1;
}

// A 13-bit delta's doublings before its top two bits differ: MAME's loop
//   while (!((dvy ^ (dvy << 1)) & 0x1000) && !((dvx ^ (dvx << 1)) & 0x1000) && i++ < 16) ...
// stops at the lesser of the two, 16 at most (a delta of 0 never stops it).
static inline int vg_spare(uint32_t v) {
    uint32_t t = (v & 0x1000) ? ~v & 0xFFF : v & 0xFFF;    // the bits that differ from the sign
    if (!t) return (v & 0x1000) ? 12 : 16;
    return __builtin_clz(t) - 20;
}

// the timer after n of MAME's steps  timer >>= 1; timer |= 0x4000 | (OP1 << 7)  (and, for
// a short vector, its low byte alone)
static inline uint32_t vg_timer_shift(uint32_t timer, int n, uint32_t op1) {
    if (n > 15) n = 15;                 // (15 steps fill it; a 16th changes nothing)
    timer = (timer >> n) | (0x7FFF & ~(0x7FFFu >> n));
    if (op1) timer = (timer | (0xFF & ~(0xFFu >> (n > 8 ? 8 : n)))) & 0xFF;
    return timer;
}

static inline __attribute__((always_inline)) int vg_strobe3(vg_t *g, uint32_t op) {
    int cycles = 0;
    g->halt = OP0;
    if (!OP0 && !OP2) {
        cycles = (int)(OP1 ? 0x100 - (g->timer & 0xFF) : 0x8000 - g->timer);
        g->timer = 0;
        // (MAME's int arithmetic, which wraps for a vector far longer than the screen)
        uint32_t k = (uint32_t)cycles * (uint32_t)(g->scale ^ 0xFF);
        g->xpos += (int32_t)((uint32_t)(((g->dvx >> 3) ^ 0x200) - 0x200) * k) >> 4;
        g->ypos -= (int32_t)((uint32_t)(((g->dvy >> 3) ^ 0x200) - 0x200) * k) >> 4;
    }
    if (OP2) {
        cycles = (int)(0x8000 - g->timer);
        g->timer = 0;
        g->xpos = VG_XCENTER;
        g->ypos = VG_YCENTER;
        vg_point(g, g->xpos, g->ypos, 0, 0);
    }
    return cycles;
}

// One state's work: handler h (the state's low three bits; 0-3 latch the
// byte at the generator's PC, 4-7 are the strobes). Returns the ticks a
// vector's drawing takes.
static inline __attribute__((always_inline)) int vg_handler(vg_t *g, unsigned h, uint32_t op) {
    if (h < 4) g->data = mm[g->pc & 0x3FFF];
    switch (h) {
    case 0:
        g->dvy = (g->dvy & 0x1F00) | g->data;
        g->pc = (g->pc + 1) & 0xFFFF;
        return 0;
    case 1:
        g->dvy12 = (g->data >> 4) & 1;
        g->op = g->data >> 5;
        g->int_latch = 0;
        g->dvy = (g->dvy12 << 12) | ((g->data & 0xF) << 8);
        g->dvx = 0;
        g->pc = (g->pc + 1) & 0xFFFF;
        return 0;
    case 2:
        g->dvx = (g->dvx & 0x1F00) | g->data;
        g->pc = (g->pc + 1) & 0xFFFF;
        return 0;
    case 3:
        g->int_latch = g->data >> 4;
        g->dvx = ((g->int_latch & 1) << 12) | ((g->data & 0xF) << 8) | (g->dvx & 0xFF);
        g->pc = (g->pc + 1) & 0xFFFF;
        return 0;
    case 4:
        if (OP0) {
            g->stack[g->sp & 3] = g->pc;
        } else {
            // both deltas doubled (the timer halved, its top filled) while neither's
            // top two bits differ: 17 times at most (MAME's count, for 0 and 0)
            // (as one step: the doublings are the lesser of the two deltas'
            // spare sign bits)
            int n = vg_spare(g->dvy), nx = vg_spare(g->dvx);
            if (nx < n) n = nx;
            g->dvy = (g->dvy & 0x1000) | ((g->dvy << n) & 0x1FFF);
            g->dvx = (g->dvx & 0x1000) | ((g->dvx << n) & 0x1FFF);
            g->timer = vg_timer_shift(g->timer, n, OP1);
        }
        return 0;
    case 5:
        if (!OP2) {
            g->timer = vg_timer_shift(g->timer, (int)g->bin_scale, OP1);
        }
        if (OP2) g->sp = (OP1 ? g->sp - 1 : g->sp + 1) & 0xF;
        return 0;
    case 6:
        if (!OP2 && !g->dvy12) {
            g->intensity = g->dvy & 0xFF;
            g->colour = (g->dvy >> 8) & 0xF;
        }
        if (OP2) {
            if (OP0) g->pc = (g->dvy << 1) & 0xFFFF;
            else g->pc = g->stack[g->sp & 3];
        } else if (g->dvy12) {
            g->scale = g->dvy & 0xFF;
            g->bin_scale = (g->dvy >> 8) & 7;
        }
        return 0;
    default: {
        int c = vg_strobe3(g, op);
        if (!OP0 && !OP2) vg_point(g, g->xpos, g->ypos, g->colour & 7, (int)(((g->int_latch >> 1) * g->intensity) >> 3));
        return c;
    }
    }
}

// The state PROM's path for each instruction, found once (sw_init) by
// walking the PROM: from the state that latches an instruction's first byte
// (handler 1, which sets the opcode) the states that follow depend only on
// the opcode, until the next such state or the halt. So an instruction is
// its handlers in order, with no PROM lookups between; the count of states
// keeps the time the same. (Not found as expected: every state is walked.)
#define VG_SEQ_MAX 24
static struct {
    uint8_t n, states;              // handlers; states from the opcode's latch to the next (or the halt)
    uint8_t h[VG_SEQ_MAX], at[VG_SEQ_MAX];   // each handler, and the states done before it
    uint8_t halts, last;            // it ends in the halt, at this state
} vg_seq[8];
static bool vg_fast;
#define VG_LATCH1 9                 // the state that runs handler 1

// the handlers of each instruction in an Atari AVG, in order (after the
// opcode's own latch): VCTR, HALT, SVEC, STAT/SCAL, CNTR, JSRL, RTSL, JMPL.
// When the PROM's paths are these (vg_derive looks), vg_run has each
// instruction as straight code.
static const uint8_t vg_std[8][7] = {{0, 3, 2, 4, 5, 7, 8}, {0, 7, 8}, {3, 4, 5, 7, 8}, {0, 6, 8}, {0, 4, 7, 8}, {0, 4, 5, 6, 8}, {0, 5, 6, 8}, {0, 6, 8}};
static bool vg_straight;

static void vg_derive(void) {
    vg_fast = vg_straight = false;
#ifndef SW_VG_WALK
    for (unsigned o = 0; o < 8; o++) {
        unsigned st = VG_LATCH1, h = 0, n = 0, states = 0;
        bool done = false;
        vg_seq[o].halts = 0;
        while (!done && states < 64 && n < VG_SEQ_MAX) {
            st = avg_prom[((h ^ 1u) << 7) | (o << 4) | st] & 0xF;
            if (st & 8) {
                if ((st & 7) == 1) break;               // the next instruction's
                vg_seq[o].h[n] = st & 7;
                vg_seq[o].at[n] = (uint8_t)states;
                n++;
                if ((st & 7) == 7 && (o & 1)) {         // strobe 3 with the opcode's low bit: the halt
                    vg_seq[o].halts = 1;
                    vg_seq[o].last = (uint8_t)st;
                    done = true;
                }
            }
            states++;
        }
        if (states >= 64 || n >= VG_SEQ_MAX) return;
        vg_seq[o].n = (uint8_t)n;
        vg_seq[o].states = (uint8_t)states;
    }
    vg_fast = true;
#ifndef SW_VG_BY_SEQ
    vg_straight = vg_seq[1].halts && vg_seq[1].n == 2;
    for (unsigned o = 0; o < 8; o++)
        for (unsigned i = 0; vg_straight; i++) {
            if (vg_std[o][i] == 8) { vg_straight = i == vg_seq[o].n; break; }
            if (i >= vg_seq[o].n || vg_seq[o].h[i] != vg_std[o][i]) vg_straight = false;
        }
#endif
#endif
}

// the list from the top, to its halt: the master clock's ticks it took
static uint32_t SW_HOT vg_run(void) {
    vg_t gg = vg, *g = &gg;             // (a copy the compiler keeps in registers)
    uint32_t cycles = 0;
    g->pc = 0;
    g->sp = 0;
    g->halt = 0;
    g->state_latch &= 0x0F;
    int steps = 0;
    for (; steps < 400000; steps++) {
        unsigned addr = (((g->state_latch >> 4) ^ 1u) << 7) | (unsigned)(g->op << 4) | (g->state_latch & 0xF);
        g->state_latch = (g->state_latch & 0x10) | (avg_prom[addr & 0xFF] & 0xF);
        if (g->state_latch & 8) cycles += (unsigned)vg_handler(g, g->state_latch & 7, g->op);
        if (g->halt && !(g->state_latch & 0x10)) goto halted;
        g->state_latch = (g->halt << 4) | (g->state_latch & 0xF);
        cycles += 8;
        if (vg_fast && g->state_latch == VG_LATCH1) break;     // an instruction's first byte is in: the quick way from here
    }
#define H(h, op) vg_handler(g, h, op)
    if (vg_straight) {
        while (steps < 400000) {
            uint32_t op = g->op;
            switch (op) {
            case 0: H(0, 0); H(3, 0); H(2, 0); H(4, 0); H(5, 0); cycles += (unsigned)H(7, 0); break;
            case 1:
                H(0, 1);
                cycles += 8u * vg_seq[1].at[1] + (unsigned)H(7, 1);
                steps += vg_seq[1].at[1];
                g->state_latch = vg_seq[1].last;
                goto halted;
            case 2: H(3, 2); H(4, 2); H(5, 2); cycles += (unsigned)H(7, 2); break;
            case 3: H(0, 3); H(6, 3); break;
            case 4: H(0, 4); H(4, 4); cycles += (unsigned)H(7, 4); break;
            case 5: H(0, 5); H(4, 5); H(5, 5); H(6, 5); break;
            case 6: H(0, 6); H(5, 6); H(6, 6); break;
            default: H(0, 7); H(6, 7); break;
            }
            cycles += 8u * vg_seq[op].states + 8;
            steps += vg_seq[op].states + 1;
            H(1, 0);                    // the next instruction's opcode
        }
        goto halted;
    }
    while (steps < 400000) {
        const __typeof__(vg_seq[0]) *q = &vg_seq[g->op];
        for (unsigned i = 0; i < q->n; i++) {
            int c = H(q->h[i], g->op);
            if (g->halt) {
                steps += q->at[i];
                g->state_latch = q->last;
                cycles += 8u * q->at[i] + (unsigned)c;
                goto halted;
            }
            cycles += (unsigned)c;
        }
        cycles += 8u * q->states;
        steps += q->states + 1;
        H(1, 0);
        cycles += 8;
    }
#undef H
halted:
    stats.vg_steps += (unsigned)steps;
    vg = gg;
    return cycles;
}

static void vg_go(void) {
    // MAME starts a new picture when the list before had more than 10
    // points; a shorter one (the game sets VGGO for those too) adds to it
    static bool last_big = true;
    if (last_big) vg.nvecs = 0;
    vg.run_points = 0;
    vg.have_last = 0;
    uint32_t c = vg_run();
    last_big = vg.run_points > 10;
    vg_halt_at = m6809_now(&maincpu) + c / MASTER_PER_CPU + 1;
    vg_running = true;
    stats.vg_runs++;
    pictures++;
}

static void vg_reset(void) {
    vg_t *g = &vg;
    (void)g;
    vg.state_latch = 0;
    vg.bin_scale = 0;
    vg.scale = 0;
    vg.colour = 0;
    vg_halt_at = 0;
}

// ---- the game CPU's bus -----------------------------------------------------------
#ifdef SW_ESB
// ---- the 137412-101 slapstic (a port of MAME's slapstic.cpp, Aaron Giles,
// BSD-3-Clause, for this one chip) -------------------------------------------------
// It sits on the address bus and picks one of four 8K pages for
// $8000-$9FFF by what it sees there. Idle, it waits for $8000; then $8080,
// $8090, $80A0 or $80B0 picks a page at once, and two longer sequences
// (MAME's "alternate" and "bitwise") pick one in steps. All but two of its
// states look only at four pages of the range ($80, $95, $9B, $9E), so
// those pages are read through a handler and the rest of the ROM directly;
// in the two states where any access at all moves it on (the alternate
// sequence's middle), every page goes through the handlers until it leaves
// them (slap_any).
enum { SL_IDLE, SL_ACTIVE, SL_ALT_VALID, SL_ALT_SELECT, SL_ALT_COMMIT, SL_BIT_LOAD, SL_BIT_ODD, SL_BIT_EVEN };
static uint8_t slap_state, slap_bank, slap_loaded;
static bool slap_any;
static uintptr_t slap_base;             // the page in, less $8000
// The page tables as they'd be with no slapstic watching: for reads one for
// each setting of the game's other bank switch (r_rd: the one in force), so
// that switch, which the game throws many times a frame, is a copy of the
// ROM's pages from one of two ready tables.
static uintptr_t real_rd[2][256], r_wr[256];
static const uintptr_t *r_rd = real_rd[0];

static inline bool slap_page(unsigned p) { return p == 0x80 || p == 0x95 || p == 0x9B || p == 0x9E; }

static void slap_map(void) {
    for (unsigned p = 0; p < 0x100; p++) {
        m_rd[p] = slap_any || slap_page(p) ? 0 : r_rd[p];
        m_wr[p] = slap_any ? 0 : r_wr[p];
    }
}

static void slap_set_bank(unsigned b) {
    slap_bank = (uint8_t)b;
    slap_base = (uintptr_t)(esb_rom + 0x10000 + b * 0x2000) - 0x8000;
    for (unsigned p = 0x80; p < 0xA0; p++) {
        real_rd[0][p] = real_rd[1][p] = slap_base;
        if (!slap_any && !slap_page(p)) m_rd[p] = slap_base;
    }
    stats.slap_switches++;
}

static void slap_to(unsigned state) {
    bool any = state == SL_ALT_VALID || state == SL_ALT_SELECT;
    slap_state = (uint8_t)state;
    if (any != slap_any) {
        slap_any = any;
        slap_map();
    }
}

// an access to address a, of any kind
static void __attribute__((noinline)) slap_test(uint16_t a) {
    bool in = (a & 0xE000) == 0x8000;
    unsigned o = a & 0x1FFF;
    bool reset = in && o == 0;
    switch (slap_state) {
    case SL_IDLE:
        if (reset) slap_to(SL_ACTIVE);
        break;
    case SL_ACTIVE:
        if (!in) break;
        if (o == 0x0080 || o == 0x0090 || o == 0x00A0 || o == 0x00B0) {
            slap_set_bank((o - 0x80) >> 4);
            slap_to(SL_IDLE);
        } else if ((o & 0x1F00) == 0x1E00) {
            stats.slap_alt++;
            slap_to(SL_ALT_VALID);
        } else if ((o & 0x1FF0) == 0x1540) {
            stats.slap_bit++;
            slap_to(SL_BIT_LOAD);
        }
        break;
    case SL_ALT_VALID:
        if (reset) slap_to(SL_ACTIVE);
        else if (!in && o == 0x1FFF) slap_to(SL_ALT_SELECT);
        else slap_to(SL_ACTIVE);
        break;
    case SL_ALT_SELECT:
        if (reset) slap_to(SL_ACTIVE);
        else if (in && (o & 0x1FFC) == 0x1B5C) {
            slap_loaded = o & 3;
            slap_to(SL_ALT_COMMIT);
        } else slap_to(SL_ACTIVE);
        break;
    case SL_ALT_COMMIT:
        if (reset) slap_to(SL_ACTIVE);
        else if (in && (o & 0x1FCF) == 0x0080) {
            slap_set_bank(slap_loaded);
            slap_to(SL_IDLE);
        }
        break;
    case SL_BIT_LOAD:
        if (reset) slap_to(SL_ACTIVE);
        else if (in && (o & 0x1FCF) == 0x0080) {
            slap_loaded = slap_bank;
            slap_to(SL_BIT_ODD);
        }
        break;
    default: {                          // SL_BIT_ODD, SL_BIT_EVEN: a bit of the page's number at a time
        bool odd = slap_state == SL_BIT_ODD;
        unsigned k = o & 0x1FF3;
        if (reset) slap_to(SL_ACTIVE);
        else if (in && k >= 0x1540 && k <= 0x1543) {
            // odd: $1540 clears bit 0, $1541 sets it, $1542 clears bit 1, $1543 sets it;
            // even: $1540 sets bit 1, $1541 clears it, $1542 sets bit 0, $1543 clears it
            static const uint8_t what[2][4] = {{0x12, 0x02, 0x11, 0x01}, {0x01, 0x11, 0x02, 0x12}};    // 0x1n: set n; 0x0n: clear n
            uint8_t w = what[odd][k - 0x1540];
            slap_loaded = (uint8_t)((w & 0x10) ? (slap_loaded | (w & 3)) : (slap_loaded & ~(w & 3)));
            slap_to(odd ? SL_BIT_EVEN : SL_BIT_ODD);
        } else if (in && (o & 0x1FF8) == 0x1550) {
            slap_set_bank(slap_loaded);
            slap_to(SL_IDLE);
        }
        break;
    }
    }
}

// the two pages at $6000 and the two at $A000-$FFFF, switched together
static void SW_HOT set_bank(int page) {
    r_rd = real_rd[page != 0];
    if (!slap_any) {
        memcpy(m_rd + 0x60, r_rd + 0x60, 0x20 * sizeof m_rd[0]);
        memcpy(m_rd + 0xA0, r_rd + 0xA0, 0x60 * sizeof m_rd[0]);
    }
    stats.bank_switches++;
}

// the two tables (sw_init)
static void esb_tables(void) {
    memset(real_rd, 0, sizeof real_rd);
    memset(r_wr, 0, sizeof r_wr);
    for (unsigned b = 0; b < 2; b++)
        for (unsigned p = 0; p < 0x100; p++) {
            if (p < 0x40 || (p >= 0x48 && p < 0x60)) real_rd[b][p] = (uintptr_t)mm;
            if (p < 0x60 || (p >= 0x80 && p < 0xA0)) continue;
            unsigned file = p < 0x80 ? 0 : 1 + ((p - 0xA0) >> 5);       // -101; -102, -203, -104
            real_rd[b][p] = (uintptr_t)(esb_rom + file * 0x4000 + b * 0x2000) - (p < 0x80 ? 0x6000 : 0xA000 + ((p - 0xA0) >> 5) * 0x2000);
        }
    for (unsigned p = 0; p < 0x100; p++)
        if (p < 0x30 || (p >= 0x48 && p < 0x60)) r_wr[p] = (uintptr_t)mm;
}
#else
static void set_bank(int page) {
    for (unsigned p = 0x60; p < 0x80; p++) m_rd[p] = (uintptr_t)(bankrom + (page ? 0x2000 : 0)) - 0x6000;
    stats.bank_switches++;
}
#endif

static void snd_reset(void);
static void snd_run_to(uint64_t t);

static uint8_t __attribute__((noinline)) main_io_r(uint16_t a) {
#ifdef SW_ESB
    if ((a & 0xE000) == 0x8000) {       // one of the slapstic's pages: the byte, then what it makes of the address
        uint8_t v = *(const uint8_t *)(slap_base + a);
        slap_test(a);
        return v;
    }
    if (slap_any) {
        uintptr_t p = r_rd[a >> 8];
        slap_test(a);
        if (p) return *(const uint8_t *)(p + a);
    }
#endif
    switch (a >> 8) {
    case 0x43:
        switch ((a >> 5) & 7) {
        case 0: return in0;
        case 1: return (uint8_t)(in1 | (m6809_now(&maincpu) >= vg_halt_at ? 0x40 : 0) | (m6809_now(&maincpu) < math_until ? 0x80 : 0));
        case 2: return dsw0;
        case 3: return dsw1;
        case 4:
            if (adc_at && m6809_now(&maincpu) >= adc_at) { adc_sar = adc_next; adc_at = 0; }
            return adc_sar;
        default: return 0;
        }
    case 0x44:
        // (the sound board first, up to this moment: what it has sent and taken)
        if (a == 0x4400) { snd_run_to(m6809_now(&maincpu)); mainlatch_pending = false; return mainlatch; }
        if (a == 0x4401) {
            snd_run_to(m6809_now(&maincpu));
            return (uint8_t)((soundlatch_pending ? 0x80 : 0) | (mainlatch_pending ? 0x40 : 0));
        }
        return 0;
    case 0x45: return nv_sram[a & 0xFF] & 0x0F;
    case 0x47:
        switch (a & 0xFF) {
        case 0: return (uint8_t)(quotient_shift >> 8);
        case 1: return (uint8_t)quotient_shift;
        case 3:
            prng = prng * 1664525u + 1013904223u;
            return (uint8_t)(prng >> 16);
        default: return 0;
        }
    default: return 0;
    }
}

static void __attribute__((noinline)) main_io_w(uint16_t a, uint8_t v) {
#ifdef SW_ESB
    if (slap_any || (a & 0xE000) == 0x8000) {
        uintptr_t p = r_wr[a >> 8];
        slap_test(a);
        if (p) { *(uint8_t *)(p + a) = v; return; }
    }
#endif
    switch (a >> 8) {
    case 0x44:
        if (a == 0x4400) {
            snd_run_to(m6809_now(&maincpu));  // the sound board sees it at this moment, not a slice early
            soundlatch = v;
            soundlatch_pending = true;
            stats.latch_snd++;
            riot_edge();
            m6809_stop(&maincpu);       // the sound CPU's turn
        }
        break;
    case 0x45: nv_sram[a & 0xFF] = v & 0x0F; break;
    case 0x46:
        switch ((a >> 5) & 7) {
        case 0: vg_go(); break;
        case 1: vg_reset(); break;
        case 2: break;                  // the watchdog
        case 3: maincpu.irq = 0; break;
        case 4: {
            uint8_t bit = (uint8_t)(1u << (a & 7)), was = outlatch;
            outlatch = (uint8_t)((v & 0x80) ? (outlatch | bit) : (outlatch & ~bit));
            if ((was ^ outlatch) & 0x10) set_bank(outlatch & 0x10);
            if ((a & 7) == 7) {         // the X2212's array recall, on the line's fall
                bool line = !(outlatch & 0x80);
                if (line && !nv_recall_line) memcpy(nv_sram, nv_e2, sizeof nv_sram);
                nv_recall_line = line;
            }
            break;
        }
        case 5:
            if (memcmp(nv_e2, nv_sram, sizeof nv_e2)) {
                memcpy(nv_e2, nv_sram, sizeof nv_e2);
                nv_dirty = true;
            }
            break;
        case 6:
            if ((a & 0x1F) < 4) {       // the ADC0809's start: the result a few cycles on
                adc_next = (a & 3) == 0 ? inputs.yoke_y : (a & 3) == 1 ? inputs.yoke_x : 0;
                adc_at = m6809_now(&maincpu) + 4;
            }
            break;
        default:
            if (a == 0x46E0) {
                snd_run_to(m6809_now(&maincpu));
                soundlatch_pending = mainlatch_pending = false;
                snd_reset();
            }
            break;
        }
        break;
    case 0x47:
        if ((a & 0xFF) < 8) math_w(a & 7, v);
        break;
    default: break;                     // ROM, and nothing
    }
}

static inline __attribute__((always_inline)) uint8_t main_rd(uint16_t a) {
    uintptr_t p = m_rd[a >> 8];
    return p ? *(const uint8_t *)(p + a) : main_io_r(a);
}
static inline __attribute__((always_inline)) void main_wr(uint16_t a, uint8_t v) {
    uintptr_t p = m_wr[a >> 8];
    if (p) *(uint8_t *)(p + a) = v;
    else main_io_w(a, v);
}

#ifdef SW_ESB
// An instruction's bytes come through its first byte's page (fbase), when
// that page and the one its last byte could be in are the same plain memory.
// Else (the slapstic's watched pages, a ROM's edge, or the slapstic watching
// everything) fbase is 0, and the instruction is run by a second copy of the
// 6809, mainx: small, slow and exact to the bus cycle. It fetches each byte
// by address and makes the cycles in which a 6809 reads for nothing (MAME's
// dummy_read_opcode_arg and dummy_vma, in MAME's places), all told to the
// slapstic, whose alternate sequence is made of just those: the game's
// switch is a  LDA ,X  at $9DFE, whose idle read of $9E00, idle cycle at
// $FFFF and read of the byte at X are the sequence's three steps.
static uint8_t __attribute__((noinline)) main_fetch_slow(uint16_t a) {
    uintptr_t p = r_rd[a >> 8];
    uint8_t v = p ? *(const uint8_t *)(p + a) : 0;
    if (slap_any || (a & 0xE000) == 0x8000) slap_test(a);
    return v;
}
static void __attribute__((noinline)) main_dummy_vma(int n) {
    while (n--) (void)main_fetch_slow(0xFFFF);
}
#undef M6809_HOT
#if defined(__GNUC__) && !defined(__clang__)
#define M6809_HOT __attribute__((optimize("Os")))
#else
#define M6809_HOT
#endif
#define M6809_NAME  mainx
#define M6809_NO_TRACE
#define M6809_FETCH(a) main_fetch_slow(a)
#define M6809_DUMMY(a) (void)main_fetch_slow(a)
#define M6809_DUMMY_VMA(n) main_dummy_vma(n)
#define M6809_RD(a) main_rd(a)
#define M6809_WR(a, v) main_wr(a, v)
#include "m6809_core.h"
#undef M6809_HOT
#define M6809_HOT SW_HOT

// one instruction by mainx, inside a run of the quick copy: its cycles off
// that run's count, or the run ended if a handler asked for that
static void __attribute__((noinline)) main_slow_step(m6809_t *c) {
    uint64_t end = c->end;
    int32_t left = c->icount;
    uint64_t t0 = end - (uint64_t)(int64_t)left;
    mainx_run(c, 1);
    uint64_t t1 = m6809_now(c);
    int32_t cyc = (int32_t)(t1 - t0);
    if (c->icount + cyc == 0) {         // (m6809_stop() in it)
        c->end = t1;
        c->icount = 0;
    } else {
        c->end = end;
        c->icount = left - cyc;
    }
}

// (7 bytes on: the indexed modes ask again from their postbyte, up to two
// bytes in, and must get the same answer)
static inline __attribute__((always_inline)) uintptr_t main_fetch_base(uint16_t pc) {
    uintptr_t p = m_rd[pc >> 8];
    if (__builtin_expect((pc & 0xFF) > 0xF8 && p, 0) && (pc >= 0xFFF0 || m_rd[(pc + 7) >> 8] != p)) return 0;
    return p;
}
#define M6809_NAME  maincpu
#define M6809_FETCH_BASE(pc) main_fetch_base(pc)
#define M6809_SLOW_STEP(c) main_slow_step(c)
#define M6809_FETCH(a) (*(const uint8_t *)(fbase + (uint16_t)(a)))
#define M6809_RD(a) main_rd(a)
#define M6809_WR(a, v) main_wr(a, v)
#include "m6809_core.h"
#else
// An instruction's bytes come through its first byte's page. One that could
// run on into a page of other memory (the banked ROM's edges; never, in this
// game, but it costs a compare) is read a byte at a time into fetch_tmp
// first. (The I/O's addresses hold no program.)
static uint8_t fetch_tmp[6];
static uintptr_t __attribute__((noinline)) main_fetch_edge(uint16_t pc) {
    for (unsigned i = 0; i < sizeof fetch_tmp; i++) {
        uintptr_t p = m_rd[(uint16_t)(pc + i) >> 8];
        fetch_tmp[i] = p ? *(const uint8_t *)(p + (uint16_t)(pc + i)) : 0;
    }
    return (uintptr_t)fetch_tmp - pc;
}
static inline __attribute__((always_inline)) uintptr_t main_fetch_base(uint16_t pc) {
    uintptr_t p = m_rd[pc >> 8];
    if (__builtin_expect((pc & 0xFF) > 0xFA || !p, 0)) {
        if (pc < 0xFFF0 && m_rd[(pc + 5) >> 8] != p) return main_fetch_edge(pc);
        if (!p) return (uintptr_t)mm;
    }
    return p;
}
#define M6809_NAME  maincpu
#define M6809_FETCH_BASE(pc) main_fetch_base(pc)
#define M6809_FETCH(a) (*(const uint8_t *)(fbase + (uint16_t)(a)))
#define M6809_RD(a) main_rd(a)
#define M6809_WR(a, v) main_wr(a, v)
#include "m6809_core.h"
#endif

// ---- the sound CPU's bus ----------------------------------------------------------
static uint8_t __attribute__((noinline)) snd_io_r(uint16_t a) {
    unsigned page = a >> 8;
    if (page >= 0x08 && page < 0x10) {
        soundlatch_pending = false;
        riot_edge();
        return soundlatch;
    }
    if (page == 0x10) {
        if (!(a & 0x80)) return riot.ram[a & 0x7F];
        if ((a & 0xE0) == 0x80) return riot_r(a & 0x1F, m6809_now(&sndcpu));
    }
    return 0;
}

static void __attribute__((noinline)) snd_io_w(uint16_t a, uint8_t v) {
    unsigned page = a >> 8;
    if (page < 0x08) {
        mainlatch = v;
        mainlatch_pending = true;
        stats.latch_main++;
        m6809_stop(&sndcpu);
    } else if (page == 0x10) {
        if (!(a & 0x80)) riot.ram[a & 0x7F] = v;
        else if ((a & 0xE0) == 0x80) riot_w(a & 0x1F, v, m6809_now(&sndcpu) + sndcpu.wofs);
    } else if (page == 0x18 && (a & 0xFF) < 0x40) {
        unsigned o = a & 0x3F;
        pokeys.reg[(o >> 3) & 3][(o & 7) | ((o & 0x20) >> 2)] = v;
    }
}

static inline __attribute__((always_inline)) uint8_t snd_rd(uint16_t a) {
    uintptr_t p = s_rd[a >> 8];
    return p ? *(const uint8_t *)(p + a) : snd_io_r(a);
}
static inline __attribute__((always_inline)) void snd_wr(uint16_t a, uint8_t v) {
    uintptr_t p = s_wr[a >> 8];
    if (p) *(uint8_t *)(p + a) = v;
    else snd_io_w(a, v);
}

#ifdef SW_ESB
// (the sound program's four 8K pieces lie in two files: an instruction that
// could run from one into the next, or isn't in ROM, is read a byte at a
// time into snd_fetch_tmp first)
static uint8_t snd_fetch_tmp[6];
static uintptr_t __attribute__((noinline)) snd_fetch_edge(uint16_t pc) {
    for (unsigned i = 0; i < sizeof snd_fetch_tmp; i++) {
        uintptr_t p = s_rd[(uint16_t)(pc + i) >> 8];
        snd_fetch_tmp[i] = p ? *(const uint8_t *)(p + (uint16_t)(pc + i)) : 0;
    }
    return (uintptr_t)snd_fetch_tmp - pc;
}
static inline __attribute__((always_inline)) uintptr_t snd_fetch_base(uint16_t pc) {
    uintptr_t p = s_rd[pc >> 8];
    if (__builtin_expect((pc & 0xFF) > 0xFA || !p, 0)) {
        if (pc < 0xFFF0 && s_rd[(pc + 5) >> 8] != p) return snd_fetch_edge(pc);
        if (!p) return snd_fetch_edge(pc);
    }
    return p;
}
#define M6809_NAME  sndcpu
#define M6809_FETCH_BASE(pc) snd_fetch_base(pc)
#define M6809_FETCH(a) (*(const uint8_t *)(fbase + (uint16_t)(a)))
#else
// (the sound program runs from its ROM alone, at $4000 and again at $C000)
#define M6809_NAME  sndcpu
#define M6809_FETCH(a) snd_rom[(a) & 0x3FFF]
#endif
#define M6809_RD(a) snd_rd(a)
#define M6809_WR(a, v) snd_wr(a, v)
#include "m6809_core.h"

static void snd_reset(void) {
    sndcpu_reset(&sndcpu);
}

// ---- the programs' waiting loops, not run but counted --------------------------------
// Both programs spend most of their time in a few short loops, waiting: the
// game for its interrupt's tick, for the interrupt to clear a flag, and for
// the matrix processor; the sound program for its timer's interrupt. When a
// CPU is at the top of one and nothing can change what the loop tests before
// the slice ends (no interrupt is due inside a slice: sw_run and snd_run_to
// end one where the next can come), the loop's whole turns in the slice are
// added to the clock and the registers left as a turn leaves them: the same
// as running them, to the cycle (test/idle.sh), at a third of the work.
// The addresses are this ROM set's (sw_init checks the three ROMs' CRC-32s;
// any other set runs every instruction). SW_NO_IDLE_SKIP: never.
static bool idle_ok;

#if defined(SW_ESB) && !defined(SW_NO_IDLE_SKIP)
// The Empire Strikes Back's waiting loops: the same four as Star Wars', in
// other places and with other flag bytes (and the tick's loop masks the
// interrupts for four of its eight instructions; the count is of whole
// turns, so the turn the interrupt comes in is run). These are the esb
// set's addresses, by the three ROMs' CRC-32s.
static bool esb_idle_check(void) {
    return sw_crc32(0, esb_rom, 0x4000) == sw_rom_files[0].crc && sw_crc32(0, esb_rom + 0x4000, 0x4000) == sw_rom_files[1].crc &&
           sw_crc32(0, snd_rom + 0x4000, 0x4000) == sw_rom_files[7].crc;
}

enum { IDLE_TICK = 0x61B6, IDLE_TICK_END = 0x61C6, IDLE_FLAG = 0xA331, IDLE_FLAG_END = 0xA333, IDLE_MATH0 = 0xB33B, IDLE_MATH1 = 0x6287,
       IDLE_SND = 0xFD42, IDLE_SND_END = 0xFD54 };

static void SW_HOT main_slice(int n) {
    if (idle_ok) {
        uint64_t stop = m6809_now(&maincpu) + (unsigned)n;
        for (int step = 0; step < 10; step++) {
            if (maincpu.pending && !(maincpu.irq && (maincpu.cc & CC_I))) break;   // (an interrupt to take)
            uint16_t pc = maincpu.pc;
            uint64_t now = m6809_now(&maincpu);
            if (now >= stop) return;
            uint32_t left = (uint32_t)(stop - now), k;
            bool page1 = (outlatch & 0x10) != 0;
            if (pc >= IDLE_TICK && pc <= IDLE_TICK_END && !page1) {
                // registers set (the interrupts masked the while), then a flag byte in RAM shifted right into the
                // carry: round again while it's clear
                if (pc == IDLE_TICK) {
                    if (mm[0x4844] || (maincpu.cc & (CC_I | CC_F)) || !(k = left / 29)) break;
                    maincpu.x = 0;
                    maincpu.a = 0x48;
                    maincpu.b = 0;
                    maincpu.dp = 0x48;
                    maincpu.cc = (uint8_t)((maincpu.cc & (CC_E | CC_H)) | CC_Z);
                    maincpu.end += 29 * k;
                    break;
                }
            } else if (pc >= IDLE_FLAG && pc <= IDLE_FLAG_END && !page1) {
                // a RAM byte loaded: round again while its top bit is set
                if (pc == IDLE_FLAG) {
                    if (maincpu.dp != 0x48 || !(mm[0x4846] & 0x80) || !(k = left / 7)) break;
                    maincpu.a = mm[0x4846];
                    maincpu.cc = (uint8_t)((maincpu.cc & ~(CC_Z | CC_V)) | CC_N);
                    maincpu.end += 7 * k;
                    break;
                }
            } else if ((pc >= IDLE_MATH0 && pc <= IDLE_MATH0 + 3 && !page1) || (pc >= IDLE_MATH1 && pc <= IDLE_MATH1 + 3 && page1)) {
                // the matrix processor's flag tested at $4320: round again while it runs
                if (pc == IDLE_MATH0 || pc == IDLE_MATH1) {
                    if (now >= math_until) break;
                    k = (uint32_t)((math_until - now + 9) / 10);
                    if (k > left / 10) k = left / 10;
                    if (!k) break;
                    maincpu.cc = (uint8_t)((maincpu.cc & ~(CC_Z | CC_V)) | CC_N);
                    maincpu.end += 10 * k;
                    break;
                }
            } else {
                break;
            }
            maincpu_run(&maincpu, 1);       // inside a loop: an instruction at a time to its top
        }
        uint64_t now = m6809_now(&maincpu);
        if (now >= stop) return;
        n = (int)(stop - now);
    }
    maincpu_run(&maincpu, n);
}

static void SW_HOT snd_slice(int n) {
    if (idle_ok) {
        uint64_t stop = m6809_now(&sndcpu) + (unsigned)n;
        for (int step = 0; step < 12; step++) {
            if (sndcpu.pending && !(sndcpu.irq && (sndcpu.cc & CC_I))) break;
            uint16_t pc = sndcpu.pc;
            uint64_t now = m6809_now(&sndcpu);
            if (now >= stop) return;
            if (pc < IDLE_SND || pc > IDLE_SND_END) break;
            // (as Star Wars' sound program's)
            if (pc == IDLE_SND) {
                uint32_t k = (uint32_t)(stop - now) / 37;
                if (riot.ram[0x00] != 0x3F || riot.ram[0x0B] || sndcpu.dp != 0x10 || sndcpu.x < 2) break;
                if (k > sndcpu.x - 1u) k = sndcpu.x - 1u;
                if (!k) break;
                sndcpu.a = 0x10;
                sndcpu.x = (uint16_t)(sndcpu.x - k);
                sndcpu.cc = (uint8_t)((sndcpu.cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
                sndcpu.end += 37 * k;
                break;
            }
            sndcpu_run(&sndcpu, 1);
        }
        uint64_t now = m6809_now(&sndcpu);
        if (now >= stop) return;
        n = (int)(stop - now);
    }
    sndcpu_run(&sndcpu, n);
}
#elif defined(SW_ESB)
static bool esb_idle_check(void) { return false; }
static void main_slice(int n) { maincpu_run(&maincpu, n); }
static void snd_slice(int n) { sndcpu_run(&sndcpu, n); }
#elif !defined(SW_NO_IDLE_SKIP)
enum { IDLE_TICK = 0x6005, IDLE_TICK_END = 0x6011, IDLE_FLAG = 0x6032, IDLE_FLAG_END = 0x6034, IDLE_MATH = 0xCDBD,
       IDLE_MATH_END = 0xCDC0, IDLE_SND = 0x7D4C, IDLE_SND_END = 0x7D5E };

// the game CPU for n cycles
static void SW_HOT main_slice(int n) {
    if (idle_ok) {
        uint64_t stop = m6809_now(&maincpu) + (unsigned)n;
        for (int step = 0; step < 8; step++) {
            if (maincpu.pending && !(maincpu.irq && (maincpu.cc & CC_I))) break;   // (an interrupt to take)
            uint16_t pc = maincpu.pc;
            uint64_t now = m6809_now(&maincpu);
            if (now >= stop) return;
            uint32_t left = (uint32_t)(stop - now), k;
            if (pc >= IDLE_TICK && pc <= IDLE_TICK_END && !(outlatch & 0x10)) {
                // registers set, then a flag byte in RAM shifted right into the carry: round again while it's clear
                if (pc == IDLE_TICK) {
                    if (mm[0x483D] || !(k = left / 23)) break;
                    maincpu.x = 0;
                    maincpu.a = 0x48;
                    maincpu.b = 0;
                    maincpu.dp = 0x48;
                    maincpu.cc = (uint8_t)((maincpu.cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
                    maincpu.end += 23 * k;
                    break;
                }
            } else if (pc >= IDLE_FLAG && pc <= IDLE_FLAG_END && !(outlatch & 0x10)) {
                // a RAM byte loaded: round again while its top bit is set
                if (pc == IDLE_FLAG) {
                    if (maincpu.dp != 0x48 || !(mm[0x483F] & 0x80) || !(k = left / 7)) break;
                    maincpu.a = mm[0x483F];
                    maincpu.cc = (uint8_t)((maincpu.cc & ~(CC_Z | CC_V)) | CC_N);
                    maincpu.end += 7 * k;
                    break;
                }
            } else if (pc >= IDLE_MATH && pc <= IDLE_MATH_END) {
                // the matrix processor's flag tested at $4320: round again while it runs
                if (pc == IDLE_MATH) {
                    if (now >= math_until) break;
                    k = (uint32_t)((math_until - now + 9) / 10);
                    if (k > left / 10) k = left / 10;
                    if (!k) break;
                    maincpu.cc = (uint8_t)((maincpu.cc & ~(CC_Z | CC_V)) | CC_N);
                    maincpu.end += 10 * k;
                    break;
                }
            } else {
                break;
            }
            maincpu_run(&maincpu, 1);       // inside a loop: an instruction at a time to its top
        }
        uint64_t now = m6809_now(&maincpu);
        if (now >= stop) return;
        n = (int)(stop - now);
    }
    maincpu_run(&maincpu, n);
}

static void SW_HOT snd_slice(int n) {
    if (idle_ok) {
        uint64_t stop = m6809_now(&sndcpu) + (unsigned)n;
        for (int step = 0; step < 12; step++) {
            if (sndcpu.pending && !(sndcpu.irq && (sndcpu.cc & CC_I))) break;
            uint16_t pc = sndcpu.pc;
            uint64_t now = m6809_now(&sndcpu);
            if (now >= stop) return;
            if (pc < IDLE_SND || pc > IDLE_SND_END) break;
            // a guard byte and DP checked, X counted down, a flag byte shifted
            // right into the carry: round again while it's clear
            if (pc == IDLE_SND) {
                uint32_t k = (uint32_t)(stop - now) / 37;
                if (riot.ram[0x00] != 0x3F || riot.ram[0x0B] || sndcpu.dp != 0x10 || sndcpu.x < 2) break;
                if (k > sndcpu.x - 1u) k = sndcpu.x - 1u;
                if (!k) break;
                sndcpu.a = 0x10;
                sndcpu.x = (uint16_t)(sndcpu.x - k);
                sndcpu.cc = (uint8_t)((sndcpu.cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
                sndcpu.end += 37 * k;
                break;
            }
            sndcpu_run(&sndcpu, 1);
        }
        uint64_t now = m6809_now(&sndcpu);
        if (now >= stop) return;
        n = (int)(stop - now);
    }
    sndcpu_run(&sndcpu, n);
}
#else
static void main_slice(int n) { maincpu_run(&maincpu, n); }
static void snd_slice(int n) { sndcpu_run(&sndcpu, n); }
#endif

// the sound board, up to the game CPU's time: the CPU in runs that end where
// its timer's interrupt can come
static void SW_HOT snd_run_to(uint64_t t) {
    for (;;) {
        uint64_t now = m6809_now(&sndcpu);
        if (now >= t) break;
        riot_update(now);
        uint64_t stop = riot.target < t ? riot.target : t;
        snd_slice(stop > now ? (int)(stop - now) : 1);
    }
}

// ---- the outside -------------------------------------------------------------------
void sw_inputs(const sw_inputs_t *in) {
    inputs = *in;
    in0 = (uint8_t)(0xDF & ~((in->coin2 ? 0x01 : 0) | (in->coin1 ? 0x02 : 0) | (in->aux_coin ? 0x04 : 0) | (in->self_test ? 0x10 : 0) |
                             (in->thumb_l ? 0x40 : 0) | (in->fire_r ? 0x80 : 0)));
    in1 = (uint8_t)(0x34 & ~((in->service ? 0x04 : 0) | (in->thumb_r ? 0x10 : 0) | (in->fire_l ? 0x20 : 0)));
}

void sw_reset(void) {
    memset(mm, 0, 0x3000);
    memset(mm + 0x4000, 0, 0x2000);
    memset(snd_ram, 0, sizeof snd_ram);
    memset(&riot, 0, sizeof riot);
    memset(&pokeys, 0, sizeof pokeys);
    memset(&vg, 0, sizeof vg);
    memset(&stats, 0, sizeof stats);
    memset(&maincpu, 0, sizeof maincpu);
    memset(&sndcpu, 0, sizeof sndcpu);
    riot.pa7 = 1;
    riot.shift = 10;
    riot.target = 256u << 10;
    tms5220_reset(&tms);
    tms_next = SW_TMS_DIV;
    speech_w = speech_r = 0;
    MPA = BIC = 0;
    math_until = 0;
    vg_halt_at = 0;
    pictures = 0;
    outlatch = 0;
    adc_sar = 0xFF;
    adc_at = 0;
#ifdef SW_ESB
    slap_any = false;
    slap_state = SL_IDLE;
    slap_set_bank(3);                   // (where MAME's starts)
    set_bank(0);
    slap_map();
#else
    set_bank(0);
#endif
    memcpy(nv_sram, nv_e2, sizeof nv_sram);     // the latch's reset pulls the X2212's recall line
    nv_recall_line = true;
    soundlatch_pending = mainlatch_pending = false;
    next_irq = IRQ_CYCLES;
    sw_inputs_t idle = {.yoke_x = 128, .yoke_y = 128};
    sw_inputs(&idle);
    maincpu_reset(&maincpu);
    sndcpu_reset(&sndcpu);
}

#ifdef SW_ESB
uint8_t *sw_rom_place(int i) {
    uint32_t at = sw_rom_files[i].at;
    if (at < 0x20000) return esb_rom + at;
    if (at < 0x21000) return mm + 0x3000 + (at - 0x20000);
    if (at < 0x21100) return avg_prom + (at - 0x21000);
    return math_prom + (at - 0x21100);
}

uint8_t *sw_loader_scratch(void) { return (uint8_t *)vecs; }
_Static_assert(sizeof vecs >= SW_LOADER_SCRATCH, "the loader's scratch is the vectors' room");
uint8_t *sw_rom_store(void) { return esb_rom; }
#else
uint8_t *sw_rom_place(int i) {
    uint32_t at = sw_rom_files[i].at;
    if (at < 0x8000) return mm + 0x8000 + at;
    if (at < 0xC000) return bankrom + (at - 0x8000);
    if (at < 0xD000) return mm + 0x3000 + (at - 0xC000);
    if (at < 0x11000) return snd_rom + (at - 0xD000);
    if (at < 0x11100) return avg_prom + (at - 0x11000);
    return math_prom + (at - 0x11100);
}

uint8_t *sw_loader_scratch(void) { return mm + 0x4000; }
#endif

void sw_load_set(const sw_roms_t *r) {
    for (int i = 0; i < SW_ROM_FILES; i++) memcpy(sw_rom_place(i), (const uint8_t *)r + sw_rom_files[i].at, sw_rom_files[i].size);
}

void sw_init(void) {
    for (int i = 0; i < 1024; i++) {    // the microcode's four nibbles as a 16-bit word, decoded
        unsigned val = (math_prom[0xC00 + i] & 0xFu) | (math_prom[0x800 + i] << 4 & 0xF0u) | (math_prom[0x400 + i] << 8 & 0xF00u) |
                       (math_prom[i] << 12 & 0xF000u);
        math_code[i] = (uint16_t)val;
    }
    memset(m_rd, 0, sizeof m_rd);
    memset(m_wr, 0, sizeof m_wr);
    memset(s_rd, 0, sizeof s_rd);
    memset(s_wr, 0, sizeof s_wr);
#ifdef SW_ESB
    esb_tables();
    for (unsigned p = 0; p < 0x100; p++) {
        if (p >= 0x20 && p < 0x28) s_rd[p] = s_wr[p] = (uintptr_t)snd_ram - 0x2000;
        // -113's halves at $4000 and $C000, -112's at $6000 and $E000
        if (p >= 0x40 && p < 0x60) s_rd[p] = (uintptr_t)snd_rom - 0x4000;
        if (p >= 0x60 && p < 0x80) s_rd[p] = (uintptr_t)(snd_rom + 0x4000) - 0x6000;
        if (p >= 0xC0 && p < 0xE0) s_rd[p] = (uintptr_t)(snd_rom + 0x2000) - 0xC000;
        if (p >= 0xE0) s_rd[p] = (uintptr_t)(snd_rom + 0x6000) - 0xE000;
    }
    vg_derive();
    idle_ok = esb_idle_check();
#else
    for (unsigned p = 0; p < 0x100; p++) {
        if (p < 0x40 || (p >= 0x48 && p < 0x60) || p >= 0x80) m_rd[p] = (uintptr_t)mm;
        if (p < 0x30 || (p >= 0x48 && p < 0x60)) m_wr[p] = (uintptr_t)mm;
        if (p >= 0x20 && p < 0x28) s_rd[p] = s_wr[p] = (uintptr_t)snd_ram - 0x2000;
        if (p >= 0x40 && p < 0x80) s_rd[p] = (uintptr_t)snd_rom - 0x4000;
        if (p >= 0xC0) s_rd[p] = (uintptr_t)snd_rom - 0xC000;
    }
    vg_derive();
    idle_ok = sw_crc32(0, bankrom, 0x4000) == sw_rom_files[0].crc && sw_crc32(0, mm + 0xC000, 0x2000) == sw_rom_files[3].crc &&
              sw_crc32(0, snd_rom + 0x2000, 0x2000) == sw_rom_files[7].crc;
#endif
    memset(nv_e2, 0xFF, sizeof nv_e2);
    memset(nv_sram, 0, sizeof nv_sram);
    nv_dirty = false;
    sw_reset();
}

void SW_HOT sw_run(uint32_t cycles) {
    uint64_t end = m6809_now(&maincpu) + cycles;
    while (m6809_now(&maincpu) < end) {
        if (m6809_now(&maincpu) >= next_irq) {
            maincpu.irq = 1;
            next_irq += IRQ_CYCLES;
            stats.irqs++;
        }
        // a run to the next interrupt (the sound board catches up at each
        // byte between the two, and here)
        uint64_t stop = end < next_irq ? end : next_irq;
        main_slice((int)(stop - m6809_now(&maincpu)));
        snd_run_to(m6809_now(&maincpu));
    }
    tms_sync(m6809_now(&sndcpu));
}

uint32_t sw_picture(const sw_vec_t **v, int *n) {
    *v = vecs;
    *n = (int)vg.nvecs;
    return pictures;
}

const sw_pokeys_t *sw_pokeys(void) { return &pokeys; }

int sw_speech(int16_t *out, int max, bool *talking) {
    if (speech_w - speech_r > SW_SPEECH_MAX) speech_r = speech_w - SW_SPEECH_MAX;
    int n = 0;
    while (speech_r != speech_w && n < max) out[n++] = speech[speech_r++ % SW_SPEECH_MAX];
    if (talking) *talking = tms5220_talking(&tms);
    return n;
}

uint8_t *sw_nvram(void) { return nv_e2; }
bool sw_nvram_dirty(void) { return nv_dirty; }
void sw_nvram_clean(void) { nv_dirty = false; }
void sw_nvram_recall(void) { memcpy(nv_sram, nv_e2, sizeof nv_sram); }

void sw_stats(sw_stats_t *s) {
    stats.main_cycles = m6809_now(&maincpu);
    stats.snd_cycles = m6809_now(&sndcpu);
    stats.main_pc = maincpu.pc;
    stats.snd_pc = sndcpu.pc;
    *s = stats;
}

#ifdef SW_ESB
uint8_t sw_peek_main(uint16_t a) { return r_rd[a >> 8] ? *(const uint8_t *)(r_rd[a >> 8] + a) : 0; }
#else
uint8_t sw_peek_main(uint16_t a) { return m_rd[a >> 8] ? *(const uint8_t *)(m_rd[a >> 8] + a) : 0; }
#endif
#ifdef SW_TEST_POKE     // (the tests only: a byte of the game's RAM, $4800-$4FFF)
void sw_poke_main(uint16_t a, uint8_t v) { if (a >= 0x4800 && a < 0x5000) mm[a] = v; }
#endif
uint8_t sw_peek_snd(uint16_t a) { return s_rd[a >> 8] ? *(const uint8_t *)(s_rd[a >> 8] + a) : 0; }
