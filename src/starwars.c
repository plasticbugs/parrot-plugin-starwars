// starwars -- Atari's Star Wars (1983) on the ColecoVision: the cart runs
// the arcade board (../sw.c) from the user's own ROMs on the card and sends
// the picture and the sound to the console as the Game Boy kernel's frame
// lists (../swcv.c). RESET goes back to the menu.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c)
//   core 0  this loop: for each TV frame the console starts, a frame's worth
//           of both 6809s, the vector generator's newest picture drawn and
//           what changed of it, the sound, as the next list
//
// The ROMs (MAME's "starwars" set) are read from the card's /STARWARS
// folder, loose or zipped, or from /STARWARS.ZIP, each known by its size
// and CRC-32. The game's high scores and settings (its X2212) are kept in
// /STARWARS/STARWARS.NV, with the yoke's way up as it was left.
//
// The picture is in colour, and up on the stick aims down (a flight
// stick's way), unless the option button turns that round.
//
// Controls (../swpad.h): the yoke is a USB pad's left stick, or the
// controller's joystick; fire is any face button or trigger, or either fire
// button; coin is - or keypad 1, start + or keypad 2. The option button,
// keypad 0 or the right stick's click (or capture): up and down the other
// way round.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "cvdiag.h"
#include "cvframes.h"
#include "cvpad.h"
#include "cvsd.h"
#include "vdp.h"
#include "gbkernel.h"
#include "handoff.h"
#include "swcv.h"
#include "swpad.h"
#include "swcard.h"

#define OPEN_US     20000           // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS 4000            // a hang reboots into the menu
#define NOTE_US     1000000         // the menu's note is brought up to date this often
#define DRAW_BY_US  10500           // a new picture is drawn only if the board's frame was done by now
#define FILE_MAX    0xE000          // the largest file looked at (a zip of the set is 46K)
// SW_ESB: the same plugin as The Empire Strikes Back (../esb/CMakeLists.txt),
// from MAME's "esb" set in /ESB
#ifdef SW_ESB
#define GAME        "Empire"        // in the menu's notes
#define TITLE       "THE EMPIRE"
#define TITLE2      "STRIKES BACK"
#define TAG         "ESB"
#define ROMS_HINT   "PUT MAME'S ESB ROMS, OR", "ESB.ZIP, IN " SWCARD_HOME
#else
#define GAME        "Star Wars"
#define TITLE       "STAR WARS"
#define TAG         "SW"
#define ROMS_HINT   "PUT MAME'S STARWARS ROMS, OR", "STARWARS.ZIP, IN " SWCARD_HOME
#endif

static swcv_t enc;
static uint8_t list[SWCV_LIST_MAX];
static size_t list_len;
static bool list_pending;                   // built, not published yet
static uint32_t seen_frames;
static fs_t fs;
static bool card_ok;
#ifdef SW_ESB
// A message's canvas is the ROMs' own room (the messages come before they
// are loaded, or when they can't be), and a file is read into the vectors'
// (sw.h): this game's ROMs leave no RAM for either.
#define CANVAS      (*(canvas_t *)(void *)sw_rom_store())
_Static_assert(sizeof(canvas_t) <= SW_ROM_STORE, "the canvas fits the ROMs' room");
#define FILE_BUF    sw_loader_scratch()
#undef FILE_MAX
#define FILE_MAX    SW_LOADER_SCRATCH
#else
// a message's canvas; while the ROMs load, the file being looked at
static union {
    canvas_t canvas;
    uint8_t file[FILE_MAX];
} big __attribute__((aligned(4)));
#define CANVAS      big.canvas
#define FILE_BUF    big.file
#endif

// ---- the console, a list a TV frame ------------------------------------------------
static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

// TV frames the console has started since the last call (0: none yet, or
// the last list isn't out)
static uint32_t new_frames(void) {
    cvpad_task();
    cvf_task(time_us_32());
    watchdog_update();
    publish();
    if (list_pending) return 0;
    uint32_t f = cvf_frames(), n = f - seen_frames;
    seen_frames = f;
    return n;
}

static void send(void) {
    list_len = swcv_tv_frame(&enc, list);
    list_pending = true;
    publish();
}

// ---- messages ---------------------------------------------------------------------
static void centre(int y, const char *s, uint8_t colour, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    canvas_text(&CANVAS, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, colour, scale);
}

static void message(const char *head, const char *l1, const char *l2, const char *l3) {
    canvas_init(&CANVAS);
#ifdef TITLE2
    centre(16, TITLE, C_LYELLOW, 2);
    centre(40, TITLE2, C_LYELLOW, 2);
#else
    centre(32, TITLE, C_LYELLOW, 3);
#endif
    if (head) centre(80, head, C_WHITE, 1);
    if (l1) centre(100, l1, C_GRAY, 1);
    if (l2) centre(112, l2, C_GRAY, 1);
    if (l3) centre(124, l3, C_GRAY, 1);
    centre(176, "RESET: BACK TO THE MENU", C_LBLUE, 1);
    swcv_canvas(&enc, &CANVAS.px[0][0]);
}

// serve the console, a list a frame, for us microseconds (forever: 0)
static void serve_for(uint32_t us) {
    static const sw_pokeys_t quiet;
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frames()) continue;
        swcv_sound(&enc, &quiet, NULL, 0, false);
        send();
    }
}

static void __attribute__((noreturn)) fail(const char *why, const char *l2, const char *l3, const char *menu_note) {
    message("IT CAN'T START", why, l2, l3);
    cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

// ---- the card: the ROMs, and the X2212's file -----------------------------------------
// between a file's sectors: the console keeps its picture
static void card_tick(void) {
    static const sw_pokeys_t quiet;
    if (!new_frames()) return;
    swcv_sound(&enc, &quiet, NULL, 0, false);
    send();
}

// on the way back to the menu: the high scores and settings, if the game
// stored any since they were read
static swpad_t pad_state;
static bool opts_changed;
// opts[2] in STARWARS.NV: 2 from 0.1.1 on, where colour is always on and
// invert starts on; a file from 0.1.0 keeps its scores but not its choices
#define OPTS_FORMAT 2

static void going_back(void) {
    if (!card_ok || (!sw_nvram_dirty() && !opts_changed)) return;
    const uint8_t opts[SWCARD_OPTS] = {enc.colour, pad_state.invert, OPTS_FORMAT, 0};
    watchdog_update();
    fs_set_writer(&fs, cvsd_write);
    if (swcard_nv_save(&fs, opts) && sw_nvram_dirty()) cvf_note(GAME ": scores saved");
    watchdog_update();
}

// ---- the game -----------------------------------------------------------------------
static uint8_t last_key = SWKEY_NONE;
static uint32_t last_usb, seen_pic, cyc_frac;
static uint32_t late, us_max, pics, pics_at_note;
static int16_t speech[SW_SPEECH_MAX];

static void game_frame(uint32_t elapsed) {
    uint32_t t0 = time_us_32();
    uint8_t joy, fire, key;
    cvf_input(&joy, &fire, &key);
    gamepad_t g;
    bool have = cvpad_get(0, &g);
    bool flip = key == SWKEY_0 && last_key != SWKEY_0;     // the option button: up and down
    last_key = key;
    if (have) {
        if (g.buttons & ~last_usb & (GP_RSTICK | GP_CAPTURE)) flip = true;
        last_usb = g.buttons;
    } else last_usb = 0;
    if (flip) {
        pad_state.invert = !pad_state.invert;
        opts_changed = true;
    }
    sw_inputs_t in;
    swpad_frame(&pad_state, joy, fire, key, have ? &g : NULL, &in);
    sw_inputs(&in);

    // the board, for the frames since the last list (two at most: a slow
    // stretch slows the game rather than piling up)
    if (elapsed > 2) elapsed = 2;
    cyc_frac += SW_FRAME_CYCLES_Q8 * elapsed;
    sw_run(cyc_frac >> 8);
    cyc_frac &= 255;

    const sw_vec_t *v;
    int nv;
    uint32_t pic = sw_picture(&v, &nv);
    static uint8_t put_off;
    if (pic != seen_pic) {
        // a frame whose board took most of its time already (two vector
        // lists in it, say) leaves the drawing to the next, twice at most:
        // the list is out in time, and the console keeps the picture it has
        if (time_us_32() - t0 > DRAW_BY_US && put_off < 2) {
            put_off++;
        } else {
            put_off = 0;
            pics += pic - seen_pic;
            seen_pic = pic;
            swcv_vectors(&enc, v, nv);
        }
    }
    bool talking;
    int ns = sw_speech(speech, SW_SPEECH_MAX, &talking);
    swcv_sound(&enc, sw_pokeys(), speech, ns, talking);
    send();
    uint32_t us = time_us_32() - t0;
    if (us > us_max) us_max = us;
}

// the menu's bottom line, should RESET come: the game's pictures a second,
// TV frames the cart was late for, its slowest frame
static void update_note(void) {
    static uint32_t t_last, late_last;
    uint32_t t = time_us_32();
    if (t - t_last < NOTE_US) return;
    unsigned per_s = MIN(pics - pics_at_note, 99u), dl = MIN(late - late_last, 99u), ms = MIN(us_max / 1000, 99u),
             tenths = us_max / 100 % 10;
    char n[48];
    snprintf(n, sizeof n, TAG " %s %u/s %uL %u.%ums", enc.colour ? "col" : "bw", per_s, dl, ms, tenths);
    cvf_note(n);
    t_last = t;
    late_last = late;
    pics_at_note = pics;
    us_max = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    cvd_stage(1);
    cvpad_init();
    swcv_init(&enc);
    swcv_set_colour(&enc, true);            // always in colour
    pad_state.invert = true;                // up aims down, until the option button
    message("LOOKING FOR THE ROMS", "IN " SWCARD_HOME " ON THE CARD", NULL, NULL);
    serve_for(OPEN_US);

    if (!cvsd_card_in() || !cvsd_init()) fail(cvsd_card_in() ? "THE CARD DIDN'T ANSWER" : "NO CARD IN THE SLOT", NULL, NULL, GAME ": no card");
    if (fs_mount(&fs, cvsd_read) != FS_OK) fail("NO FAT OR EXFAT ON THE CARD", NULL, NULL, GAME ": the card didn't mount");
    card_ok = true;
    cvd_stage(2);
    watchdog_enable(WATCHDOG_MS, true);
    uint32_t found = swcard_load_roms(&fs, FILE_BUF, FILE_MAX, card_tick);
    cvd_stage(3);
    if (found != SWROMS_ALL) {
        int missing = 0;
        const char *first = "";
        for (int i = 0; i < SW_ROM_FILES; i++)
            if (!(found & 1u << i)) { if (!missing++) first = sw_rom_files[i].name; }
        char l2[40], note[33];
#ifdef SW_ESB
        if (!found) snprintf(l2, sizeof l2, "NONE OF ITS 14 FILES IS THERE");
        else snprintf(l2, sizeof l2, "%d OF 14 MISSING: %.14s", missing, first);
#else
        if (!found) snprintf(l2, sizeof l2, "NONE OF ITS 13 FILES IS THERE");
        else snprintf(l2, sizeof l2, "%d OF 13 MISSING: %.14s", missing, first);
#endif
        for (char *c = l2; *c; c++)
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
#ifdef SW_ESB
        snprintf(note, sizeof note, GAME ": %d of 14 ROMs missing", missing);
#else
        snprintf(note, sizeof note, "Star Wars: %d of 13 ROMs missing", missing);
#endif
        fail(ROMS_HINT, l2, note);
    }
    sw_init();
    uint8_t opts[SWCARD_OPTS] = {0};
    if (swcard_nv_load(&fs, opts)) {        // the scores, and how it was last played
        if (opts[2] == OPTS_FORMAT) pad_state.invert = opts[1] != 0;
    }
    cvf_on_exit(going_back);
    cvd_stage(4);
    cvf_note(GAME);

    for (;;) {
        uint32_t n = new_frames();
        if (!n) continue;
        if (n > 1) late += n - 1;
        game_frame(n);
        update_note();
    }
}
