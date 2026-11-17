// swpad.h -- the controls: a USB pad's analog stick, or the ColecoVision
// controller's joystick, as Star Wars' flight yoke. Portable, header-only.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../sdk/gamepad.h"
#include "sw.h"

// the ColecoVision keypad's codes, as the kernel reports them
enum { SWKEY_0 = 0xA, SWKEY_1 = 0xD, SWKEY_2 = 0x7, SWKEY_3 = 0xC, SWKEY_4 = 0x2, SWKEY_5 = 0x3, SWKEY_6 = 0xE,
       SWKEY_7 = 0x5, SWKEY_8 = 0x1, SWKEY_9 = 0xB, SWKEY_STAR = 0x9, SWKEY_HASH = 0x6, SWKEY_NONE = 0xF };

typedef struct {
    int16_t jx, jy;             // the joystick's yoke, -127..127 (it moves there, and springs back)
    uint16_t frames;            // TV frames since the game started
    bool invert;                // up on the stick aims down (a flight stick's way)
    uint8_t coin_left;          // frames of the coin pulse still to give the game
    uint8_t coin_wait;          // frames the coin key must still be up before a press counts
} swpad_t;

// A coin is a pulse of SWPAD_COIN frames from the press, however long the
// key is held. The game's coin routine takes a coin switch closed for 2 to
// 12 frames (33-200 ms) and ignores a longer one, as a jammed switch: a
// keypad press is often longer than that (test/coin_host.c measures it).
// Another coin needs the key let go for SWPAD_COIN_UP frames first (a key
// that bounces gives one coin).
#define SWPAD_COIN    6
#define SWPAD_COIN_UP 3

// The game finds its yoke's range for itself, slowly: it wants each end held
// for a second or more before its sight is where the yoke points (until
// then a yoke at rest aims low and left; MAME's does the same). Nobody holds
// a stick in a corner that long, so once the game is up (its self-test takes
// 5 s) the yoke is taken to both ends for it, 1.5 s each, while the title
// shows: SWPAD_CAL_FROM to SWPAD_CAL_FROM + 2 * SWPAD_CAL_HOLD frames.
#define SWPAD_CAL_FROM 330
#define SWPAD_CAL_HOLD 90

#define SWPAD_DEAD   120        // a stick this near its middle (of 2047) is at rest
#define SWPAD_PUSH   7          // the joystick held: the yoke moves this much a frame
#define SWPAD_SPRING 10         // let go: back to the middle this fast

static inline int swpad_axis(int v) {       // a stick's -2048..2047 as -127..127
    if (v > -SWPAD_DEAD && v < SWPAD_DEAD) return 0;
    v = v > 0 ? v - SWPAD_DEAD : v + SWPAD_DEAD;
    v = v * 127 / (2047 - SWPAD_DEAD);
    return v > 127 ? 127 : v < -127 ? -127 : v;
}

static inline int16_t swpad_ramp(int16_t at, int dir) {
    if (dir) {
        int v = at + dir * SWPAD_PUSH;
        return (int16_t)(v > 127 ? 127 : v < -127 ? -127 : v);
    }
    if (at > SWPAD_SPRING) return (int16_t)(at - SWPAD_SPRING);
    if (at < -SWPAD_SPRING) return (int16_t)(at + SWPAD_SPRING);
    return 0;
}

// One TV frame's inputs. joy, fire, key: the console's controller as
// cvf_input() gives it (low = pressed; key a SWKEY_ code). pad: a USB pad's
// state, or NULL.
//   the yoke        the pad's left stick (analog), or its d-pad or the
//                   joystick (the yoke moves while held, and springs back);
//                   up aims up, unless s->invert
//   fire            either fire button; a pad's face buttons and triggers
//   coin            keypad 1; a pad's - (select): one coin a press
//   start           keypad 2; a pad's + (the game starts on a fire button)
static inline void swpad_frame(swpad_t *s, uint8_t joy, uint8_t fire, uint8_t key, const gamepad_t *pad, sw_inputs_t *in) {
    int dx = (!(joy & 2) ? 1 : 0) - (!(joy & 8) ? 1 : 0), dy = (!(joy & 1) ? 1 : 0) - (!(joy & 4) ? 1 : 0);    // right, up
    bool f = !(fire & 1) || !(fire & 2), coin = key == SWKEY_1, start = key == SWKEY_2;
    int ax = 0, ay = 0;
    if (pad) {
        uint32_t b = pad->buttons;
        if (b & GP_RIGHT) dx = 1;
        if (b & GP_LEFT) dx = -1;
        if (b & GP_UP) dy = 1;
        if (b & GP_DOWN) dy = -1;
        ax = swpad_axis(pad->lx);
        ay = swpad_axis(pad->ly);
        if (b & (GP_SOUTH | GP_EAST | GP_WEST | GP_NORTH | GP_L | GP_R | GP_ZL | GP_ZR)) f = true;
        if (b & GP_MINUS) coin = true;
        if (b & GP_PLUS) start = true;
    }
    s->jx = swpad_ramp(s->jx, dx);
    s->jy = swpad_ramp(s->jy, dy);
    int x = ax ? ax : s->jx, y = ay ? ay : s->jy;
    if (s->invert) y = -y;
    in->yoke_x = (uint8_t)(128 + x);
    in->yoke_y = (uint8_t)(128 + y);        // up on the stick: the sight goes up
    if (s->frames < SWPAD_CAL_FROM + 2 * SWPAD_CAL_HOLD) {
        if (s->frames >= SWPAD_CAL_FROM) in->yoke_x = in->yoke_y = s->frames < SWPAD_CAL_FROM + SWPAD_CAL_HOLD ? 1 : 255;
        s->frames++;
    }
    in->fire_l = in->fire_r = f;
    in->thumb_l = in->thumb_r = start;
    if (coin && !s->coin_wait) s->coin_left = SWPAD_COIN;
    if (coin) s->coin_wait = SWPAD_COIN_UP;
    else if (s->coin_wait) s->coin_wait--;
    in->coin1 = s->coin_left != 0;
    if (s->coin_left) s->coin_left--;
    in->coin2 = in->service = in->aux_coin = in->self_test = false;
}
