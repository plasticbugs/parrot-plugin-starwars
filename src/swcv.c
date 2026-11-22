// swcv.c -- see swcv.h.
#include "swcv.h"

#include <string.h>

// ---- lists (nescv.c's, from gbkernel.asm's costs) ---------------------------------
#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    480
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

// A frame of speech: the console goes from one list straight into the next
// (it finds the vertical blank already past), so a list's length is the
// frame's. C_FRAME: the kernel from a list's end mark to the next list's
// first command, not waiting (gbkernel.asm's frame: 45 instructions; MAME's
// console agrees, test/console.sh). PCM_L: the whole of it, a little over a
// TV frame, so the console is always a little late and never waits.
#define C_FRAME    357
#define PCM_L      (FRAME_T + 300)
#define PCM_FIRST  (C_FRAME + C_END_VB)
// a sample's two bytes; then from one sample to the next, to the 1/256 T
#define C_SAMPLE   (C_PSG + 2 * C_PSG_BYTE)
#define PCM_STEP   (((PCM_L - PCM_FIRST - C_SAMPLE - C_END) << 8) / (SWCV_PCM_N - 1))

#define PATTERNS 0x0000
#define COLOURS  0x2000
#define NAMES    0x1800
#define WHITE    0xF1               // white on black

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static void l_run_head(list_t *l, uint16_t addr, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    l->t += C_PSG + C_PSG_BYTE * n;
    l->b[l->n++] = 2;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

// bytes a run may have and still leave the list inside its part of the frame
static int room(const list_t *l) {
    int t = l->vblank ? ((int)VB_LIMIT - (int)l->t - C_END_VB - C_RUN_VB) / C_BYTE_VB
                      : ((int)LIST_LIMIT - (int)l->t - C_END - C_RUN_ACT) / C_BYTE_ACT;
    return t > 255 ? 255 : t;
}

// ---- the picture ----------------------------------------------------------------
// MAME's screen units (0-250 across, 0-280 down), in sixteenths, to pixels
#define PX(x) (((x) * 4178 + 32768) >> 16)
#define PY(y) (((y) * 2799 + 32768) >> 16)

static const uint8_t vec_colour[8] = {0xF1, 0x51, 0x31, 0x71, 0x91, 0xD1, 0xB1, 0xF1};   // by red, green, blue

static int outcode(int x, int y) {
    return (x < 0 ? 1 : x >= SWCV_W ? 2 : 0) | (y < 0 ? 4 : y >= SWCV_H ? 8 : 0);
}

// A line, clipped to the screen (Cohen-Sutherland), then Bresenham's with the
// pixel's place in the bitmap kept as a byte address and a bit: a step across
// moves the bit, a step down the address (by 1 inside a cell, by 249 to the
// cell below). col 0: black and white, where the colour plane isn't looked at.
static inline __attribute__((always_inline)) void line(swcv_t *e, int x0, int y0, int x1, int y1, uint8_t col) {
    int c0 = outcode(x0, y0), c1 = outcode(x1, y1);
    for (int i = 0; i < 8 && (c0 | c1); i++) {
        if (c0 & c1) return;
        int c = c0 ? c0 : c1, x, y;
        if (c & 8) { x = x0 + (x1 - x0) * (SWCV_H - 1 - y0) / (y1 - y0); y = SWCV_H - 1; }
        else if (c & 4) { x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0); y = 0; }
        else if (c & 2) { y = y0 + (y1 - y0) * (SWCV_W - 1 - x0) / (x1 - x0); x = SWCV_W - 1; }
        else { y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0); x = 0; }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode(x0, y0); }
        else { x1 = x; y1 = y; c1 = outcode(x1, y1); }
    }
    if (c0 | c1) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    uint8_t *pat = e->want_pat + ((y0 >> 3) * 32 + (x0 >> 3)) * 8 + (y0 & 7);
    unsigned bit = 0x80u >> (x0 & 7);
    int n = dx > dy ? dx : dy, yin = y0 & 7;
    for (;; n--) {
        *pat |= (uint8_t)bit;
        if (col) pat[e->want_col - e->want_pat] = col;
        if (!n) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            if (sx > 0) { bit >>= 1; if (!bit) { bit = 0x80; pat += 8; } }
            else { bit <<= 1; if (bit == 0x100) { bit = 1; pat -= 8; } }
        }
        if (e2 < dx) {
            err += dx;
            if (sy > 0) { if (++yin == 8) { yin = 0; pat += 249; } else pat++; }
            else { if (--yin < 0) { yin = 7; pat -= 249; } else pat--; }
        }
    }
}

SWCV_HOT void swcv_vectors(swcv_t *e, const sw_vec_t *v, int n) {
    memset(e->want_pat, 0, sizeof e->want_pat);
    if (e->colour) {
        for (int i = 0; i < n; i++, v++)
            if (v->z >= e->threshold) line(e, PX(v->x0), PY(v->y0), PX(v->x1), PY(v->y1), vec_colour[v->colour & 7]);
    } else {
        for (int i = 0; i < n; i++, v++)
            if (v->z >= e->threshold) line(e, PX(v->x0), PY(v->y0), PX(v->x1), PY(v->y1), 0);
    }
    e->stats.pictures++;
    e->dirty = true;
}

void swcv_canvas(swcv_t *e, const uint8_t *px) {
    for (int y = 0; y < SWCV_H; y++)
        for (int c = 0; c < 32; c++, px += 8) {
            unsigned a = (unsigned)(((y >> 3) * 32 + c) * 8 + (y & 7));
            uint8_t pat = 0, col = 15;
            for (int x = 0; x < 8; x++)
                if (px[x] > 1) { pat |= (uint8_t)(0x80 >> x); col = px[x]; }
            e->want_pat[a] = pat;
            e->want_col[a] = (uint8_t)(e->colour ? col << 4 | 1 : WHITE);
        }
    e->stats.pictures++;
    e->dirty = true;
}

void swcv_screen(const swcv_t *e, uint8_t *px) {
    for (int y = 0; y < SWCV_H; y++)
        for (int x = 0; x < SWCV_W; x++) {
            unsigned a = (unsigned)(((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7));
            uint8_t c = e->vram_col[a];
            c = (e->vram_pat[a] & (0x80 >> (x & 7))) ? c >> 4 : c & 15;
            px[y * SWCV_W + x] = c ? c : 1;
        }
}

// ---- what the console needs -------------------------------------------------------
// the colour byte place j should hold: in colour, its vector's where there
// are pixels (where there are none, whatever is there will do)
static inline uint8_t col_want(const swcv_t *e, unsigned j) {
    if (!e->colour) return WHITE;
    return e->want_pat[j] ? e->want_col[j] : e->vram_col[j];
}
static inline bool need_pat(const swcv_t *e, unsigned j) { return e->want_pat[j] != e->vram_pat[j]; }
static inline bool need_col(const swcv_t *e, unsigned j) { return col_want(e, j) != e->vram_col[j]; }

// The next bytes to send, from the cursor on (once round): a place's colour
// bytes before its pattern bytes. *m: the run's natural length, with gaps of
// up to 3 bytes that are right already taken in (a new run costs more).
// Most of the screen is right already, and is passed over a word at a time:
// four pattern bytes the same as the console's need nothing, unless colour
// bytes are still to go (col_todo: in black and white only until the colour
// table's one fill is in; in colour, wherever there are pixels).
SWCV_HOT static bool next_run(swcv_t *e, bool *colour, unsigned *s, int *m) {
    if (!e->dirty) return false;
    unsigned i = e->cursor;
    for (unsigned k = 0; k < SWCV_BYTES;) {
        if (!(i & 3) && k + 4 <= SWCV_BYTES) {
            uint32_t w, v;
            memcpy(&w, e->want_pat + i, 4);
            memcpy(&v, e->vram_pat + i, 4);
            if (w == v && (e->colour ? !w : !e->col_todo)) {
                k += 4;
                i = i + 4 == SWCV_BYTES ? 0 : i + 4;
                continue;
            }
        }
        bool nc = (e->colour || e->col_todo) && need_col(e, i);
        if (!nc && !need_pat(e, i)) {
            k++;
            i = i + 1 == SWCV_BYTES ? 0 : i + 1;
            continue;
        }
        e->cursor = (uint16_t)i;
        *colour = nc;
        *s = i;
        unsigned end = i + 1, gap = 0;
        for (unsigned j = i + 1; j < SWCV_BYTES && j - i < 255; j++) {
            if (nc ? need_col(e, j) : need_pat(e, j)) { end = j + 1; gap = 0; }
            else if (++gap > 3) break;
        }
        *m = (int)(end - i);
        return true;
    }
    e->dirty = false;                   // a whole round with nothing to send
    e->col_todo = false;
    return false;
}

static void put_run(swcv_t *e, list_t *l, bool colour, unsigned s, int n) {
    l_run_head(l, (uint16_t)((colour ? COLOURS : PATTERNS) + s), n);
    uint8_t *d = l->b + l->n;
    if (colour) {
        for (int k = 0; k < n; k++) d[k] = e->vram_col[s + k] = col_want(e, s + k);
        e->stats.col_bytes += (unsigned)n;
    } else {
        memcpy(d, e->want_pat + s, n);
        memcpy(e->vram_pat + s, d, n);
        e->stats.pat_bytes += (unsigned)n;
    }
    l->n += n;
    e->stats.runs++;
}

// what's on screen written again as it is: a write the console lost or took
// wrong doesn't stay
static void refresh_run(swcv_t *e, list_t *l, int n) {
    unsigned at = e->refresh_at;
    bool colour = at >= SWCV_BYTES;
    unsigned s = colour ? at - SWCV_BYTES : at;
    if (s + n > SWCV_BYTES) n = (int)(SWCV_BYTES - s);
    l_run_head(l, (uint16_t)((colour ? COLOURS : PATTERNS) + s), n);
    memcpy(l->b + l->n, (colour ? e->vram_col : e->vram_pat) + s, n);
    l->n += n;
    e->refresh_at = (uint16_t)((at + n) % (2 * SWCV_BYTES));
    e->stats.refreshed += (unsigned)n;
}

uint32_t swcv_backlog(const swcv_t *e) {
    uint32_t n = 0;
    if (!e->dirty) return 0;
    for (unsigned j = 0; j < SWCV_BYTES; j++) n += need_pat(e, j) + need_col(e, j);
    return n;
}

// ---- a frame's list, the console waiting for the vertical blank --------------------
static void names(swcv_t *e, list_t *l) {
    while (e->names_at < 768) {
        int n = room(l), left = 768 - e->names_at;
        if (n < 16 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 1) return;
        if (n > left) n = left;
        l_run_head(l, (uint16_t)(NAMES + e->names_at), n);
        for (int k = 0; k < n; k++) l->b[l->n++] = (uint8_t)(e->names_at + k);
        e->names_at = (uint16_t)(e->names_at + n);
    }
    e->names_done = true;
}

SWCV_HOT static void plain_list(swcv_t *e, list_t *l) {
    // After speech the console is out of step: its lists ran back to back,
    // so this one starts anywhere in the frame, and the vertical blank part's
    // quick writes (23 T apart) could fall in the active display, where the
    // TMS9918 takes one only every 28. So first a list with the sound alone:
    // it ends before the next vertical blank, or just past it, and the
    // console waits again; then one written at the display's pace
    // throughout; then as usual.
    if (e->resync) {
        l_end_vblank(l);
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
        if (e->resync-- == 2) return;
    } else if (e->tone_n) {
        l_psg(l, e->tone_bytes, e->tone_n);
    }
    if (!e->names_done) names(e, l);
    bool colour;
    unsigned s;
    int m;
    while (e->names_done && next_run(e, &colour, &s, &m)) {
        int n = room(l);
        if (n < m && l->vblank) {           // the rest of the vertical blank is too short for it
            if (n >= 8) put_run(e, l, colour, s, n);
            l_end_vblank(l);
            continue;
        }
        if (n < 1) break;
        put_run(e, l, colour, s, n < m ? n : m);
    }
    for (int k = 0; k < 2 && e->names_done; k++) {      // with the time left
        int n = room(l);
        if (n < 32) break;
        refresh_run(e, l, n);
    }
    l_end_vblank(l);
}

// ---- a frame's list with speech in it ----------------------------------------------
// One VRAM run that takes all the time to `until` (less than a byte's worth
// left over): from the next byte the console needs, carrying on over bytes
// that are right already; with nothing needed, what's there written again.
SWCV_HOT static void fill(swcv_t *e, list_t *l, uint32_t until) {
    int avail = (int)until - (int)l->t;
    if (avail < C_RUN_ACT + C_BYTE_ACT) return;
    int n = (avail - C_RUN_ACT) / C_BYTE_ACT;
    if (n > 255) n = 255;
    bool colour;
    unsigned s;
    int m;
    if (!e->names_done || !next_run(e, &colour, &s, &m)) {
        if (e->refresh_at % SWCV_BYTES + (unsigned)n > SWCV_BYTES) e->refresh_at = (uint16_t)(e->refresh_at / SWCV_BYTES * SWCV_BYTES);
        refresh_run(e, l, n);
        return;
    }
    if (s + (unsigned)n > SWCV_BYTES) s = SWCV_BYTES - (unsigned)n;
    put_run(e, l, colour, s, n);
}

// A sample's two bytes, then VRAM to the next sample's time. The run's
// bytes are 31 T each; a sample byte sent again is 52 T (the level is the
// same), and none, one or two of those bring the next sample within 10 T of
// its time. A short run (a lone byte or two: a flat line's) goes in as one
// of two runs when there is room.
SWCV_HOT static void pcm_list(swcv_t *e, list_t *l) {
    l->t = C_FRAME;
    l_end_vblank(l);
    unsigned tone_at = 0;
    for (int k = 0; k < SWCV_PCM_N; k++) {
        uint8_t b[10];
        int n = 0;
        b[n++] = e->pcm_bytes[k][0];
        b[n++] = e->pcm_bytes[k][1];
        // the tones' changes, a register or two with each sample (a register's
        // second byte must follow its first)
        while (tone_at < e->tone_n && n < 5) {
            unsigned g = 1;
            while (tone_at + g < e->tone_n && !(e->tone_bytes[tone_at + g] & 0x80)) g++;
            if (n + (int)g > 6) break;
            memcpy(b + n, e->tone_bytes + tone_at, g);
            n += (int)g;
            tone_at += g;
        }
        if (k + 1 == SWCV_PCM_N) {
            l_psg(l, b, n);
            break;
        }
        uint32_t until = PCM_FIRST + (((uint32_t)(k + 1) * PCM_STEP) >> 8);
        int avail = (int)until - (int)l->t - (C_PSG + C_PSG_BYTE * n);
        bool colour;
        unsigned s;
        int m = 0;
        bool two = e->names_done && next_run(e, &colour, &s, &m) && m <= 2 &&
                   avail >= 2 * C_RUN_ACT + C_BYTE_ACT * (m + 2);
        int base = avail - (two ? 2 : 1) * C_RUN_ACT, best = 0, best_left = 99;
        for (int r = 0; r < 3 && base - C_PSG_BYTE * r >= C_BYTE_ACT * (two ? m + 1 : 1); r++) {
            int left = (base - C_PSG_BYTE * r) % C_BYTE_ACT;
            if (left < best_left) { best_left = left; best = r; }
        }
        while (best--) b[n++] = e->pcm_bytes[k][1];
        l_psg(l, b, n);
        if (two) put_run(e, l, colour, s, m);
        fill(e, l, until);
    }
    e->stats.pcm_frames++;
    e->resync = 2;
}

size_t swcv_tv_frame(swcv_t *e, uint8_t *out) {
    list_t l = {out, 0, C_START, true};
    if (e->pcm_frame) pcm_list(e, &l);
    else plain_list(e, &l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->tone_n = 0;
    e->stats.tv_frames++;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    // (nothing left: the scan went all the way round and found the console up to date)
    if (!e->dirty) e->stats.settled++;
    return l.n;
}

// ---- the sound ---------------------------------------------------------------------
// The DAC: tones 1 and 2 at a period of 1, their attenuators as measured on
// the user's console (coleco/ladder.json, channels 1 and 2), summed. Level i
// of DAC_LEVELS is i / (DAC_LEVELS - 1) of DAC_TOP.
#define DAC_LEVELS 128
#define DAC_TOP    1.40f
#define DAC_MID    64               // silence while it speaks
// The chip's speech, times two: it sits near a sixteenth of full scale (a
// median of about 2,000) and passes a half only in its loudest 1%, so the
// voice comes up 6 dB and only those peaks are clipped (the user found it
// quiet under the music).
#define SPEECH_SCALE 120
// the tone and noise under the speech, this many 2 dB steps quieter
#define SPEECH_DUCK  2
// The Empire Strikes Back 0.1.2: its dialogue was still low under its music
// on the console. Only the values of PSG bytes the lists already had change
// (never whether or when a byte is sent: a list is 0.1.0's, byte for byte,
// but for these values):
//   - the voice goes through a soft limit instead of a straight gain: a
//     sample s of the chip's 32,768 becomes 63 levels x s (1 + K) / (s + K),
//     K = SPEECH_KNEE / 32768. Its middle (a sample of 1,550) comes up 12 dB
//     on 0.1.0, its RMS 3, and its loudest reach the top without being cut off
//   - the tone and the noise left under it go down SPEECH_DUCK_MORE steps
//     more (10 dB in all), as the same attenuation bytes
// (ATT_SENT: the attenuation in the byte, for the one remembered and
// compared, so the bytes sent are the same in number)
#ifdef SW_ESB
#define SPEECH_KNEE      3000
#define SPEECH_DUCK_MORE 3
#define ATT_SENT(e, att) ((e)->pcm && (att) + SPEECH_DUCK_MORE < 15 ? (att) + SPEECH_DUCK_MORE : (e)->pcm ? 15 : (att))
#else
#define ATT_SENT(e, att) (att)
#endif
static const float ladder[2][16] = {
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
static uint8_t dac_pair[DAC_LEVELS];        // attenuations: channel 1's << 4 | channel 2's
static bool dac_ready;

float swcv_dac_level(int a1, int a2) { return ladder[0][a1 & 15] + ladder[1][a2 & 15]; }

static void dac_init(void) {
    for (int i = 0; i < DAC_LEVELS; i++) {
        float want = DAC_TOP * (float)i / (DAC_LEVELS - 1), best = 1e9f;
        for (int a = 0; a < 16; a++)
            for (int b = 0; b < 16; b++) {
                float d = ladder[0][a] + ladder[1][b] - want;
                if (d < 0) d = -d;
                if (d < best) { best = d; dac_pair[i] = (uint8_t)(a << 4 | b); }
            }
    }
    dac_ready = true;
}

// a POKEY volume (0-15, linear) as an SN76489 attenuation (2 dB steps)
static const uint8_t vol_att[16] = {15, 12, 9, 7, 6, 5, 4, 3, 3, 2, 2, 1, 1, 1, 0, 0};

typedef struct {
    uint16_t period;                // the SN76489's, 1-1023
    uint8_t vol, key;               // key: 1 + 4 * POKEY + channel
} voice_t;

// The POKEYs' sixteen channels as tones and one noise. Star Wars clocks them
// at 1.512 MHz; a period n at the PSG's 3.579545 MHz / 32 is the same pitch.
static int pokey_voices(const sw_pokeys_t *p, voice_t *v, int *noise_vol, int *noise_rate) {
    int n = 0, nv = 0, nr = 2;
    for (int k = 0; k < 4; k++) {
        const uint8_t *r = p->reg[k];
        uint8_t ctl = r[8];
        for (int c = 0; c < 4; c++) {
            uint8_t audc = r[2 * c + 1];
            int vol = audc & 15;
            if (!vol || (audc & 0x10)) continue;        // silent, or a level only
            // the divider: its count, and the clock it counts
            uint32_t ticks, per;                        // per: PSG periods a tick, << 16
            bool joined = (c == 1 && (ctl & 0x10)) || (c == 3 && (ctl & 0x08));
            bool fast = joined ? (ctl & (c == 1 ? 0x40 : 0x20)) : (c == 0 && (ctl & 0x40)) || (c == 2 && (ctl & 0x20));
            if ((c == 0 && (ctl & 0x10)) || (c == 2 && (ctl & 0x08))) continue;    // the low half of a 16-bit pair
            if (joined) ticks = (uint32_t)(r[2 * c] << 8 | r[2 * c - 2]) + (fast ? 7 : 1);
            else ticks = (uint32_t)r[2 * c] + (fast ? 4 : 1);
            per = fast ? 9697 : (ctl & 1) ? 1105455 : 271515;
            int dist = audc >> 5;
            if (dist == 0 || dist == 2 || dist == 4) {  // the 17-bit (or 9-bit) noise
                if (vol > nv) {
                    nv = vol;
                    uint32_t q = (uint32_t)(((uint64_t)ticks * per) >> 16);     // the shift clock, as a PSG period
                    nr = q < 45 ? 0 : q < 90 ? 1 : 2;      // nearest of its 6,991, 3,496 and 1,748 Hz
                }
                continue;
            }
            uint64_t q = (uint64_t)ticks * per;
            if (dist == 6) q = q * 15 / 2;              // the 4-bit counter's pattern: 15 steps
            else if (dist == 1 || dist == 3) q = q * 31 / 2;    // the 5-bit one's: 31
            q >>= 16;
            if (q < 5) continue;                        // above hearing
            while (q > 1023) q >>= 1;                   // below the PSG's lowest: up an octave
            int same = -1;
            for (int i = 0; i < n; i++)
                if (v[i].period == q) same = i;
            if (same >= 0) {                            // in unison: one voice, a little louder
                if (vol > v[same].vol) v[same].vol = (uint8_t)vol;
                if (v[same].vol < 15) v[same].vol++;
                continue;
            }
            v[n].period = (uint16_t)q;
            v[n].vol = (uint8_t)vol;
            v[n].key = (uint8_t)(1 + 4 * k + c);
            n++;
        }
    }
    *noise_vol = nv;
    *noise_rate = nr;
    return n;
}

// Every list writes all of the chip, changed or not: each tone's period
// and attenuator, the noise's attenuator, and the noise's control while the
// noise is silent. Up to Star Wars 0.1.7 and Empire 0.1.6 only the changes
// went out, so one byte the console misread (a bus hiccup) could leave a
// wrong tone or volume until the game changed it; now it lasts a list at
// most. The noise's control is the exception while the noise sounds: a
// write restarts its shift register, and once a list that would repeat
// the noise 60 times a second (a buzz), so then it goes out on a change
// only. A silent channel keeps its last period.
static void tone_set(swcv_t *e, int c, int period, int att) {
    uint8_t *b = e->tone_bytes;
    if (period > 0) e->psg_period[c] = (int16_t)period;
    int p = e->psg_period[c] > 0 ? e->psg_period[c] : 1;
    b[e->tone_n++] = (uint8_t)(0x80 | c << 5 | (p & 15));
    b[e->tone_n++] = (uint8_t)(p >> 4 & 0x3F);
    b[e->tone_n++] = (uint8_t)(0x90 | c << 5 | ATT_SENT(e, att));
    e->psg_att[c] = (int8_t)att;
}

void swcv_sound(swcv_t *e, const sw_pokeys_t *p, const int16_t *speech, int nspeech, bool talking) {
    if (!dac_ready) dac_init();
    e->tone_n = 0;

    // speech: the DAC comes up when the chip starts talking and goes a few
    // frames after its last sample
    bool heard = talking;
    for (int i = 0; i < nspeech && !heard; i++) heard = speech[i] > 256 || speech[i] < -256;
    if (heard) e->pcm_hold = 6;
    else if (e->pcm_hold) e->pcm_hold--;
    bool was = e->pcm;
    e->pcm_frame = e->pcm_hold > 0 || e->pcm_gain > 0;
    if (e->pcm_frame) {                     // tones 1 and 2 are the DAC (parked every list)
        static const uint8_t park[] = {0xA1, 0x00, 0xC1, 0x00};
        memcpy(e->tone_bytes, park, 4);
        e->tone_n = 4;
        e->psg_period[1] = e->psg_period[2] = 1;
        if (!was) e->voice_key[1] = e->voice_key[2] = 0;
    }
    e->pcm = e->pcm_frame;
    if (e->pcm_frame) {
        for (int k = 0; k < SWCV_PCM_N; k++) {
            // this frame's samples, 8,400 a second in, SWCV_PCM_N a frame out
            int32_t s = e->speech_last;
            if (nspeech > 0) {
                uint32_t pos = (uint32_t)k * (uint32_t)nspeech * 256 / SWCV_PCM_N;
                unsigned i = pos >> 8, f = pos & 255;
                int32_t a = speech[i], b = speech[i + 1 < (unsigned)nspeech ? i + 1 : i];
                s = a + (((b - a) * (int32_t)f) >> 8);
            }
            if (e->pcm_hold > 2) { if (e->pcm_gain < 32 && !(k & 3)) e->pcm_gain++; }
            else if (e->pcm_gain && !(k & 3)) e->pcm_gain--;
            // the level: silence at DAC_MID, half the chip's full scale 60 levels either side, by the gain
#ifdef SPEECH_KNEE
            int32_t a = (s < 0 ? -s : s) >> 3;
            if (a > 4096) a = 4096;
            a = ((63 * a * (4096 + SPEECH_KNEE / 8)) >> 12) / (a + SPEECH_KNEE / 8);
            int32_t lvl = DAC_MID + (s < 0 ? -a : a);
#else
            int32_t lvl = DAC_MID + ((s * SPEECH_SCALE) >> 15);
#endif
            if (lvl < 1) lvl = 1;
            if (lvl > DAC_LEVELS - 1) lvl = DAC_LEVELS - 1;
            lvl = lvl * e->pcm_gain / 32;
            uint8_t pair = dac_pair[lvl];
            e->pcm_bytes[k][0] = (uint8_t)(0xB0 | pair >> 4);
            e->pcm_bytes[k][1] = (uint8_t)(0xD0 | (pair & 15));
        }
        e->psg_att[1] = e->psg_att[2] = -1;
        if (nspeech > 0) e->speech_last = speech[nspeech - 1];
    } else {
        e->speech_last = 0;
    }

    // the tones: the loudest voices, each staying on the channel it has
    voice_t v[16];
    int noise_vol, noise_rate;
    int n = pokey_voices(p, v, &noise_vol, &noise_rate), chans = e->pcm ? 1 : 3;
    int pick[3] = {-1, -1, -1};
    bool taken[16] = {false};
    for (int c = 0; c < chans; c++) {       // the loudest `chans` of them
        int best = -1, best_score = -1;
        for (int i = 0; i < n; i++) {
            if (taken[i]) continue;
            int score = v[i].vol * 2;
            for (int k = 0; k < chans; k++)
                if (e->voice_key[k] == v[i].key) score++;   // one playing already wins a tie
            if (score > best_score) { best_score = score; best = i; }
        }
        if (best < 0) break;
        taken[best] = true;
        pick[c] = best;
    }
    int chan_of[3] = {-1, -1, -1};          // channel -> voice
    for (int c = 0; c < chans; c++)         // those playing keep their channel
        for (int k = 0; k < chans; k++)
            if (pick[k] >= 0 && e->voice_key[c] == v[pick[k]].key && chan_of[c] < 0) { chan_of[c] = pick[k]; pick[k] = -1; }
    for (int k = 0; k < chans; k++) {
        if (pick[k] < 0) continue;
        for (int c = 0; c < chans; c++)
            if (chan_of[c] < 0) { chan_of[c] = pick[k]; break; }
    }
    for (int c = 0; c < chans; c++) {
        if (chan_of[c] >= 0) {
            int att = vol_att[v[chan_of[c]].vol] + (e->pcm ? SPEECH_DUCK : 0);
            tone_set(e, c, v[chan_of[c]].period, att > 15 ? 15 : att);
            e->voice_key[c] = v[chan_of[c]].key;
        } else {
            tone_set(e, c, 0, 15);
            e->voice_key[c] = 0;
        }
    }
    int natt = vol_att[noise_vol] + (e->pcm ? SPEECH_DUCK : 0);
    if (natt > 15) natt = 15;
    int nc = noise_vol ? 4 | noise_rate : e->psg_noise >= 0 ? e->psg_noise : 4;
    if (nc != e->psg_noise || natt == 15) {     // (a write restarts the shift register: while it sounds, only on a change)
        e->tone_bytes[e->tone_n++] = (uint8_t)(0xE0 | nc);
        e->psg_noise = (int16_t)nc;
    }
    e->tone_bytes[e->tone_n++] = (uint8_t)(0xF0 | ATT_SENT(e, natt));
    e->psg_att[3] = (int8_t)natt;
}

void swcv_set_colour(swcv_t *e, bool colour) {
    e->colour = colour;
    e->dirty = true;
    e->col_todo = true;
}

void swcv_init(swcv_t *e) {
    memset(e, 0, sizeof *e);
    e->threshold = 1;
    for (int c = 0; c < 3; c++) e->psg_period[c] = -1;
    for (int c = 0; c < 4; c++) e->psg_att[c] = 15;     // the kernel's start: all off
    e->psg_noise = -1;
    e->dirty = true;
    e->col_todo = true;
}
