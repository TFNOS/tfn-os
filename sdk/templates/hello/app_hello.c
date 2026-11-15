// TFN OS SDK template: HELLO, the smallest complete app. Copy this folder, change the name, build:
//   python sdk/tools/mk_app.py sdk/templates/hello/app_hello.c --sym app_hello --name HELLO --kinds lcd349 --nosign
//          --push <board ip> --open
// (the board must be in the developer mode for a file with no signature: the board page /apps)
// In its own repo, with the SDK package only: sh build.sh <tfn-sdk.zip> (README.md, app.json).
//
// What an app is: one const app_t with the functions that the system calls (main/app.h). The system calls enter()
// when the app opens, then input() for each touch, update() and render() each frame, and leave() when it closes.
// render() draws the WHOLE frame each time (the frame buffer keeps the frame before the last one).
#include <stdio.h>
#include "tfn/app.h"
#include "tfn/gfx.h"
#include "tfn/audio.h"
#include "tfn/mfont.h"

// the two layouts (sdk/docs/visual.md): the 3.49 bar (640 x 172) and the 320 x 240 frame (CoreS3, AMOLED, Watcher)
#if BOARD_BAR_SCREEN
#define TEXT_Y 30
#define BALL_R 22
#else
#define TEXT_Y 34
#define BALL_R 30
#endif

static int64_t s_t0;
static int s_taps;
static int s_x, s_y;

static void enter(int64_t now)
{
    s_t0 = now;
    s_taps = 0;
    s_x = gfx_w / 2;                            // (the frame of this board: gfx_w x gfx_h)
    s_y = gfx_h / 2;
}

static void input(const input_event_t *ev)
{
    if (ev->kind == IN_TAP) {                   // a tap: the logical point of the app frame
        s_taps++;
        s_x = ev->x;
        s_y = ev->y;
        sfx_play(SFX_CLICK);                    // (the sounds obey the quiet setting of the board)
    }
}

static void update(int64_t now)
{
    (void)now;                                  // the game logic goes here (time in ms; no drawing)
}

static void render(int64_t now)
{
    float t = (now - s_t0) / 1000.0f;
    gfx_fill(rgb(18, 14, 40));
    int r = BALL_R + (int)(6 * fsin(t * 3));        // fsin: the fast sine of the graphics (main/gfx.h)
    gfx_fill_circle(s_x, s_y, r, hsv((int)(t * 60) % 360, 200, 255));
    char line[32];
    snprintf(line, sizeof line, "HELLO  TAPS %d", s_taps);
    // the text: an anti-aliased atlas font (MF_*: Bungee, KF_*: Rubik), never the old dot font of gfx_text
    mf_text_c(&MF_N24, gfx_w / 2, TEXT_Y, line, rgb(255, 255, 255), 255, 1);
}

// the menu icon: landscape logical coordinates, a box of s x s from (x, y); the host draws it into icon.bin. Paint
// each pixel of the box: a pixel that is not drawn is a hole in the icon (sdk/docs/visual.md). icon_time() (seconds)
// moves it.
static void icon(int x, int y, int s)
{
    gfx_fill_rect(x, y, s, s, rgb(60, 40, 120));
    gfx_fill_circle(x + s / 2, y + s / 2, s / 4 + (int)(s / 16 * fsin(icon_time() * 3)), rgb(255, 200, 80));
}

const app_t app_hello = {
    .name = "HELLO",
    .landscape = true,                          // 640 x 172 on the 3.49 (false: 172 x 640)
    .enter = enter,
    .input = input,
    .update = update,
    .render = render,
    .draw_icon = icon,
};
