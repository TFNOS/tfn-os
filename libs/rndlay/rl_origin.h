#pragma once
// rl_origin: an origin (rl_ox, rl_oy) for the draw calls of one source file of an app that draws with tfdraw, mfont
// and uikit. The round frame of APP_ROUND_HI uses it to draw the layout of a flat frame (one pane) in the middle box of
// the circle: the app sets rl_ox and rl_oy to the corner of the box and its own width and height to the size of the
// box; each point of a draw call moves by (rl_ox, rl_oy). With rl_ox = rl_oy = 0 (the other frames) each call gets the
// same values as before.
//
// The use: include it AFTER "tfdraw/tfdraw.h", "tfn/mfont.h" and "tfn/uikit.h" and BEFORE the draw code. Each name
// below becomes a macro that adds the origin to its points; the code above the include does not change. The touch
// targets of the app must move too (the app knows its own table: add the origin to a target, or take it off a touch
// point). tfd_poly() takes the points of a copy. rl_big = true makes each Rubik face one step bigger (the draw
// calls and ui_text_w(), so that the fits of the app use the bigger face). Draw the parts in the caps of the circle with the origin at 0.
#include "tfdraw/tfdraw.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"

static int rl_ox, rl_oy;  // the corner of the middle box (0, 0: no move)
static int rl_big;        // the round frame: 1 = each Rubik face one step bigger (KF_LABEL -> KF_NAME -> KF_TEXT),
                          // 2 = KF_TEXT at the least (KF_LABEL and KF_NAME -> KF_TEXT)

static inline const kfont_t *rl_f(const kfont_t *f)
{
    if (!rl_big) {
        return f;
    }
    return f == &KF_LABEL ? &KF_NAME : f == &KF_NAME ? &KF_TEXT : f;
}

static inline void rl_poly(const float *p, int n, uint16_t c, int a)
{
    float q[64];
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = p[2 * i] + (float)rl_ox;
        q[2 * i + 1] = p[2 * i + 1] + (float)rl_oy;
    }
    tfd_poly(q, n, c, a);
}

#define tfd_rect(x, y, ...)           (tfd_rect)((x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_rrect(x, y, ...)          (tfd_rrect)((x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_disc(x, y, ...)           (tfd_disc)((x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_ring(x, y, ...)           (tfd_ring)((x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_arc(x, y, ...)            (tfd_arc)((x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_line(x0, y0, x1, y1, ...) (tfd_line)((x0) + rl_ox, (y0) + rl_oy, (x1) + rl_ox, (y1) + rl_oy, __VA_ARGS__)
#define tfd_clip(x0, y0, x1, y1)      (tfd_clip)((x0) + rl_ox, (y0) + rl_oy, (x1) + rl_ox, (y1) + rl_oy)
#define tfd_poly(p, n, c, a)          rl_poly((p), (n), (c), (a))
#define tfd_text_l(f, x, y, ...)      (tfd_text_l)(rl_f(f), (x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_text_r(f, x, y, ...)      (tfd_text_r)(rl_f(f), (x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_text_c(f, x, y, ...)      (tfd_text_c)(rl_f(f), (x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define tfd_text_s(f, x0, x1, y, ...) (tfd_text_s)(rl_f(f), (x0) + rl_ox, (x1) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define mf_text(f, x, y, ...)         (mf_text)((f), (x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define mf_text_c(f, x, y, ...)       (mf_text_c)((f), (x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define ui_text_c(f, x, y, ...)       (ui_text_c)(rl_f(f), (x) + rl_ox, (y) + rl_oy, __VA_ARGS__)
#define ui_text_w(f, s)               (ui_text_w)(rl_f(f), (s))
