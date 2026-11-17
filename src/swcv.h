// swcv: the cart's side of Star Wars on the ColecoVision. Portable C (no
// pico-sdk), used by the plugin and by the Mac tests.
//
// The console runs gbkernel (coleco/gbcv/kernel/gbkernel.asm): each TV frame
// it takes a command list from the cart and runs it (format and costs in its
// header). This makes those lists:
//
//   once:                 swcv_init(e)
//   each new picture:     swcv_vectors(e, v, n)     (or swcv_canvas for a message)
//   each TV frame:        swcv_sound(e, pokeys, speech, nspeech, talking)
//                         len = swcv_tv_frame(e, list)
//
// The picture. The vector generator's lines are drawn into a 256 x 192
// bitmap, and the TMS9918 shows it in Graphics II with name i = tile i: one
// pattern byte for every 8 x 1 pixels, 6144 of them. A list carries the bytes
// that differ from what the console holds, as many as the frame has time
// for (about 1,700 in a run, 215 runs of five); a picture that needs more
// takes two or three frames, and the newest picture is always the one going
// in. Black and white is the start: one colour table fill, then pattern
// bytes only. In colour each 8 x 1 takes its last vector's colour, at the
// cost of a colour byte for each new one.
//
// The sound. The four POKEYs' loudest voices play on the SN76489's three
// tones and its noise, set once a frame. While the TMS5220 speaks, tones 1
// and 2 are parked at a period of 1 and their two attenuators make a DAC
// (the movie player's way, with two channels of its three): the speech goes
// out as SWCV_PCM_N samples a frame, spread evenly through the list between
// the VRAM runs, and the list is cut to end exactly one sample period before
// the next begins: the console then runs list after list without waiting
// for the vertical blank, and the samples keep their spacing across frames.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sw.h"

#define SWCV_W      256
#define SWCV_H      192
#define SWCV_BYTES  6144            // pattern bytes, and as many colour bytes
#define SWCV_LIST_MAX 3000          // (every command costs 23 T or more a byte, in a frame of 59,736)
#define SWCV_PCM_N  100             // speech samples a TV frame while the TMS5220 talks: 5,990 a second

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SWCV_HOT __attribute__((section(".time_critical.swcv")))
#else
#define SWCV_HOT
#endif

typedef struct {
    uint32_t tv_frames, pictures, pat_bytes, col_bytes, runs, refreshed, pcm_frames;
    uint32_t settled;               // frames that ended with nothing left to send
    uint32_t t_max;                 // the longest list, in T
} swcv_stats_t;

typedef struct {
    // the console's VRAM as the lists so far leave it, and the newest picture
    uint8_t vram_pat[SWCV_BYTES], vram_col[SWCV_BYTES];
    uint8_t want_pat[SWCV_BYTES], want_col[SWCV_BYTES];
    bool colour;                    // each vector in its colour (else white)
    uint8_t threshold;              // vectors dimmer than this aren't drawn
    bool names_done;
    uint16_t names_at;
    uint16_t cursor;                // where the next list's scan starts
    uint16_t refresh_at;
    bool dirty;                     // want differs from vram somewhere (or may)
    bool col_todo;                  // black and white: the colour table isn't all white yet

    // the PSG as the lists so far leave it
    int16_t psg_period[3];          // -1: not known
    int8_t psg_att[4];
    int16_t psg_noise;
    uint8_t voice_key[3];           // which POKEY voice each tone plays (0: none)
    // this frame's sound
    uint8_t tone_bytes[24];
    uint8_t tone_n;
    bool pcm;                       // tones 1 and 2 are the DAC
    uint8_t pcm_hold;               // frames the DAC stays after the last word
    uint8_t pcm_gain;               // the DAC's level, 0-32 (it comes and goes over a few frames)
    uint8_t pcm_bytes[SWCV_PCM_N][2];
    int16_t speech_last;
    bool pcm_frame;                 // this frame's list carries samples
    uint8_t resync;                 // lists still to go before the console is back in step with the vertical blank
    swcv_stats_t stats;
} swcv_t;

void swcv_init(swcv_t *e);
void swcv_set_colour(swcv_t *e, bool colour);

// a new picture: the vector generator's lines (sw_picture())
void swcv_vectors(swcv_t *e, const sw_vec_t *v, int n);
// a new picture: 256 x 192 pixels, a TMS9918 colour each (0 or 1: black); for messages
void swcv_canvas(swcv_t *e, const uint8_t *px);

// this frame's sound: the POKEYs' registers, and the speech samples made
// since the last call (8,400 a second: about 140 a frame)
void swcv_sound(swcv_t *e, const sw_pokeys_t *p, const int16_t *speech, int nspeech, bool talking);

// this TV frame's list into out (SWCV_LIST_MAX bytes); returns its length
size_t swcv_tv_frame(swcv_t *e, uint8_t *out);

// the picture the console shows once the lists so far have run, a TMS9918
// colour a pixel (tests, previews)
void swcv_screen(const swcv_t *e, uint8_t *px);
// bytes the console still needs (tests; a whole scan)
uint32_t swcv_backlog(const swcv_t *e);
// the DAC's level for an attenuator pair (tests: the lists' sound as a WAV)
float swcv_dac_level(int att1, int att2);
