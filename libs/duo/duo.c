// Two end buttons for the two-player games (see duo.h).

#include "tfn/duo.h"
#include "tfn/gfx.h"
#include "tfn/input.h"

static int s_local = -1;  // 2 BOARDS: the player of this board (both ends)
void duo_set_local(int p)
{
    s_local = p;
}

#if BOARD_ROUND
#include <math.h>
#include "tfn/uikit.h"
// THE ROUND LAYOUT (ECHO in the round frame of the panel): the ends of the touch task are zones of the
// physical frame, and they do not fit the circle. Here each button is a side part of the circle: a finger at x < 0.36
// of the width (logical) presses the left button, a finger at x > 0.64 the right one. The times are the times of the
// frames (duo_poll() once in each frame).
#define RND_PAD_W (gfx_w * 17 / 100)  // the drawn part of each side (the touch zone is wider)
static input_end_t s_rend[2];
static void round_ends(input_end_t e[2], int64_t now)
{
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    for (int k = 0; k < 2; k++) {
        bool contact = false;
        for (int i = 0; i < n; i++) {
            int lx, ly;
            gfx_touch_to_logical(px[i], py[i], &lx, &ly);
            if (k == 0 ? lx < gfx_w * 36 / 100 : lx > gfx_w * 64 / 100) {
                contact = true;
            }
        }
        input_end_t *b = &s_rend[k];
        if (contact && !b->down) {
            b->down = true;
            b->down_ms = now;
            b->presses++;
        } else if (!contact && b->down) {
            b->down = false;
            b->up_ms = now;
        }
        e[k] = *b;
    }
}
#endif
// the ends of the touch task (the round frame: the side parts of the circle)
static void get_ends(input_end_t e[2], int64_t now)
{
#if BOARD_ROUND
    if (gfx_round()) {
        round_ends(e, now);
        return;
    }
#endif
    (void)now;
    input_ends(e);
}

// 2 BOARDS: both ends as one button
static void merge_ends(input_end_t e[2])
{
    input_end_t m = e[0];
    m.down = e[0].down || e[1].down;
    m.presses = e[0].presses + e[1].presses;
    if (e[1].down_ms > m.down_ms) {
        m.down_ms = e[1].down_ms;
    }
    if (e[1].up_ms > m.up_ms) {
        m.up_ms = e[1].up_ms;
    }
    e[0] = e[1] = m;
}

void duo_reset(duo_btn_t b[2], int64_t now)
{
    input_end_t e[2];
    get_ends(e, now);
    if (s_local >= 0) {
        merge_ends(e);
    }
    bool swap = gfx_orient() == ORIENT_LAND_B && !gfx_round();
    for (int p = 0; p < 2; p++) {
        bool cpu = b[p].cpu;
        b[p] = (duo_btn_t){.cpu = cpu, .down_ms = now, .up_ms = now};
        // a finger that is down now does not count as a new press
        b[p].seen = e[swap ? 1 - p : p].presses;
    }
}

void duo_poll(duo_btn_t b[2], int64_t now)
{
    input_end_t e[2];
    get_ends(e, now);
    if (s_local >= 0) {
        merge_ends(e);
    }
    // In ORIENT_LAND_A the physical top end is at the logical left; in LAND_B at the right (the round frame: no turn).
    bool swap = gfx_orient() == ORIENT_LAND_B && !gfx_round();
    for (int p = 0; p < 2; p++) {
        duo_btn_t *bt = &b[p];
        if (bt->cpu) {
            bt->pressed = false;
            bt->released = false;
        } else {
            const input_end_t *s = &e[swap ? 1 - p : p];
            bool was = bt->down;
            bt->pressed = s->presses != bt->seen;
            bt->seen = s->presses;
            bt->down = s->down;
            bt->released = (was || bt->pressed) && !s->down;
            bt->down_ms = s->down_ms;
            bt->up_ms = s->up_ms;
        }
        int64_t h = bt->down ? now - bt->down_ms : bt->up_ms - bt->down_ms;
        bt->held_ms = (int32_t)(h < 0 ? 0 : h > 600000 ? 600000 : h);
    }
}

void duo_cpu_press(duo_btn_t *b, int64_t now)
{
    if (b->down) {
        return;
    }
    b->down = true;
    b->pressed = true;
    b->down_ms = now;
    b->held_ms = 0;
}

void duo_cpu_release(duo_btn_t *b, int64_t now)
{
    if (!b->down) {
        return;
    }
    b->down = false;
    b->released = true;
    b->up_ms = now;
    b->held_ms = (int32_t)(now - b->down_ms);
}

uint16_t duo_color(int player, int v)
{
    return hsv(player ? 330 : 190, 200, v < 0 ? 0 : v > 255 ? 255 : v);
}

bool duo_multitouch(void)
{
    return input_multitouch();
}

static int s_side = -1;  // duo_draw_net: the end to draw (-1: the end of the player)

void duo_draw_net(int local, const duo_btn_t *b, const char *label, float glow)
{
    for (int side = 0; side < 2; side++) {
        s_side = side;
        duo_draw_button(local, b, label, glow);
    }
    s_side = -1;
}

#if BOARD_ROUND
// the round frame: the pad is the side part of the circle (from its edge to RND_PAD_W), a thumb spot in its middle,
// the label under the spot
static void duo_draw_round(int side, int player, const duo_btn_t *b, const char *label, float glow)
{
    int D = gfx_w, r = D / 2, pw = RND_PAD_W;
    bool on = b && b->down;
    if (glow > 1) {
        glow = 1;
    }
    int gy = D - (int)(glow * D);  // (the charge fills the pad from the bottom)
    for (int y = 0; y < D; y++) {
        int dy = y - r, q = r * r - dy * dy;
        if (q <= 0) {
            continue;
        }
        int h = (int)sqrtf((float)q);
        int a = side ? D - pw : r - h, e = side ? r + h : pw;
        if (e <= a) {
            continue;
        }
        int v = 40 + (D - y) / 10 + (on ? 40 : 0);
        uint16_t c = glow > 0 && y >= gy ? duo_color(player, 150 + (int)(glow * 100)) : duo_color(player, v);
        gfx_fill_rect(a, y, e - a, 1, c);
        gfx_fill_rect(side ? a : e - (on ? 4 : 2), y, on ? 4 : 2, 1,
                      duo_color(player, on ? 255 : 150));  // (the inner edge)
    }
    int cx = side ? D - pw / 2 - D / 60 : pw / 2 + D / 60, cy = r;
    ui_ring(cx, cy, on ? 22 : 20, on ? 6 : 4, duo_color(player, on ? 255 : 190), 255);
    if (on) {
        ui_circle(cx, cy, 13, rgb(255, 255, 255), 255);
    }
    if (label && label[0]) {
        ui_text_c(&KF_LABEL, cx, cy + 46, label, on ? rgb(255, 255, 255) : duo_color(player, 230));
    }
}
#endif

#if BOARD_BAR_SCREEN
void duo_draw_button(int player, const duo_btn_t *b, const char *label, float glow)
{
    int x = (s_side >= 0 ? s_side : player) ? 640 - DUO_BTN_W : 0;
    int w = DUO_BTN_W;
    bool on = b && b->down;
    // body: a dark pad with a soft top light
    for (int y = 0; y < 172; y += 4) {
        int v = 40 + (172 - y) / 8 + (on ? 40 : 0);
        gfx_fill_rect(x, y, w, 4, duo_color(player, v));
    }
    // the charge from the bottom
    if (glow > 0) {
        if (glow > 1) {
            glow = 1;
        }
        int h = (int)(glow * 164);
        gfx_fill_rect(x + 4, 168 - h, w - 8, h, duo_color(player, 150 + (int)(glow * 100)));
    }
    // edge and a thumb spot
    gfx_frame_rect(x, 0, w, 172, on ? 4 : 2, duo_color(player, on ? 255 : 150));
    int cx = x + w / 2;
    gfx_ring(cx, 70, on ? 24 : 22, on ? 18 : 19, duo_color(player, on ? 255 : 190));
    if (on) {
        gfx_fill_circle(cx, 70, 14, rgb(255, 255, 255));
    }
    if (label && label[0]) {
        // size 2 when the label fits the pad; size 1 is too small to read
        const int sc = gfx_text_width(label, 2) <= w - 8 ? 2 : 1;
        gfx_text_center_in(x, w, sc == 2 ? 146 : 150, label, sc, on ? rgb(255, 255, 255) : duo_color(player, 230));
    }
}
#else
// CoreS3: a pad in the lower corner of the player, with a thumb spot and the label
void duo_draw_button(int player, const duo_btn_t *b, const char *label, float glow)
{
#if BOARD_ROUND
    if (gfx_round()) {
        duo_draw_round(s_side >= 0 ? s_side : player, player, b, label, glow);
        return;
    }
#endif
    int w = DUO_BTN_W, h = DUO_BTN_H;
    int x = (s_side >= 0 ? s_side : player) ? gfx_w - w : 0, y0 = gfx_h - h;
    bool on = b && b->down;
    for (int y = 0; y < h; y += 4) {
        int v = 40 + (h - y) / 3 + (on ? 40 : 0);
        gfx_fill_rect(x, y0 + y, w, 4, duo_color(player, v));
    }
    // the charge from the bottom
    if (glow > 0) {
        if (glow > 1) {
            glow = 1;
        }
        int gh = (int)(glow * (h - 8));
        gfx_fill_rect(x + 4, y0 + h - 4 - gh, w - 8, gh, duo_color(player, 150 + (int)(glow * 100)));
    }
    gfx_frame_rect(x, y0, w, h, on ? 3 : 2, duo_color(player, on ? 255 : 150));
    int cx = x + w / 2, cy = y0 + 22;
    gfx_ring(cx, cy, on ? 16 : 15, on ? 11 : 12, duo_color(player, on ? 255 : 190));
    if (on) {
        gfx_fill_circle(cx, cy, 9, rgb(255, 255, 255));
    }
    if (label && label[0]) {
        const int sc = gfx_text_width(label, 2) <= w - 8 && h >= 50 ? 2 : 1;  // (size 2 when it fits)
        gfx_text_center_in(x, w, y0 + h - (sc == 2 ? 18 : 12), label, sc,
                           on ? rgb(255, 255, 255) : duo_color(player, 230));
    }
}
#endif
