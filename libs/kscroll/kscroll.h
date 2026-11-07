#pragma once
// kscroll: the shared momentum scroll of the lists. It has momentum, like the scroll of a phone: the list does not
// stop when the finger lifts. One header with static functions: each source
// file that includes it has its own copy (no ABI names, no clash between two apps, a store app needs no new firmware).
// The units: px of the logical frame (the axis of the scroll) and ms (the t_ms of the input events, now_ms()).
//
// The feel (the values of the iOS scroll view):
// - after a start distance of 1.8 mm (the tap rule of input.c) the list moves 1:1 with the finger;
// - at the lift, the speed of the last 100 ms goes on and slows down by 0.998 for each ms: a flick of 300 mm/s goes
//   about 150 mm. A finger that stops for 90 ms before the lift gives no glide;
// - a second flick in the same direction during a glide adds to the speed;
// - a touch during a glide stops it, and that touch is not a tap (ks_tap_ok());
// - past an end the list moves with a resistance (the iOS rubber band, c = 0.55 of the view) and springs back;
// - the scroll bar shows during a move and fades 0.4 s after it (ks_bar()).
//
// The use: ks_init() at the enter (and ks_set_max() when the list changes); in input(): ks_down() at IN_DOWN,
// ks_move() at IN_MOVE (true = the drag starts now: call input_tap_cancel()), ks_up() at IN_UP, and at IN_TAP act
// only when ks_tap_ok(); ks_update() in each update() (true = it moves: draw again); ks_pos() is the position to draw.
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "tfn/board.h"  // (BOARD_TOUCH_FRAME_UM)

#ifndef KS_UM_PX  // the size of one logical px in um (as touch_px_um() of input.c)
#define KS_UM_PX BOARD_TOUCH_FRAME_UM
#endif

#define KS_DECEL    0.998f                // the speed factor for each ms of a glide (iOS "normal")
#define KS_SLOP_UM  1800.0f               // the start of a drag (the tap rule)
#define KS_WIN_MS   100                   // the speed comes from the finger of the last 100 ms
#define KS_STILL_MS 90                    // no move for 90 ms before the lift: no glide (the lift debounce is 40 ms)
#define KS_VMIN     (25.0f / KS_UM_PX)    // 25 mm/s in px/ms: a slower lift gives no glide
#define KS_VMAX     (1500.0f / KS_UM_PX)  // 1.5 m/s in px/ms: the fastest glide
#define KS_VSTOP    0.01f                 // px/ms: the glide ends
#define KS_VCATCH   0.05f                 // px/ms: a touch at this speed or more stops a glide (not a tap)
#define KS_RUBBER   0.55f                 // the iOS rubber band
#define KS_SPRING   0.988f                // the spring back to an end, for each ms (95% in about 250 ms)
#define KS_OUTBRAKE 0.96f                 // the speed factor for each ms of a glide past an end
#define KS_NS       8

typedef struct {
    float raw;        // the position without the rubber band (px): 0 .. max inside the list
    float vel;        // px/ms; + = the position grows (the content goes up, or left)
    float max;        // the end of the scroll (px; 0 = the list fits the view: no scroll)
    float view;       // the length of the view (px): the scale of the rubber band
    float to;         // the goal of ks_to()
    float bar;        // 0..1: the scroll bar
    float y0, raw0;   // the drag: the finger and the position at its start
    float fv;         // the speed of the glide at the touch (a second flick adds to it)
    float snap;       // > 0: the list stops on a multiple of snap (a carousel; ks_set_snap())
    float sy[KS_NS];  // the last points of the finger and their times (the speed at the lift)
    int64_t st[KS_NS];
    int64_t prev, t_move;
    uint8_t ns, si;
    bool touch, drag, caught, anim;
} kscroll_t;

static inline float ks__clamp(const kscroll_t *k, float p)
{
    return p < 0 ? 0 : p > k->max ? k->max : p;
}
static inline float ks__rub(float x, float d)
{
    return (1.0f - 1.0f / (x * KS_RUBBER / d + 1.0f)) * d;
}

static inline void ks_init(kscroll_t *k, float view, float max)
{
    memset(k, 0, sizeof *k);
    k->view = view > 1 ? view : 1;
    k->max = max > 0 ? max : 0;
}

static inline void ks_set_view(kscroll_t *k, float view)
{
    k->view = view > 1 ? view : 1;
}

// A carousel: the list stops on a multiple of step (0 = a free list). The glide of a flick gets a speed that ends
// on the nearest step, as the paged lists of iOS do; a slow lift goes to the nearest step.
static inline void ks_set_snap(kscroll_t *k, float step)
{
    k->snap = step > 0 ? step : 0;
}

// A new length of the list. A shorter list puts the position at its new end at once.
static inline void ks_set_max(kscroll_t *k, float max)
{
    max = max > 0 ? max : 0;
    if (max == k->max) {
        return;
    }
    k->max = max;
    if (!k->touch && k->raw > max) {
        k->raw = max;
        k->vel = 0;
    }
    if (k->anim) {
        k->to = ks__clamp(k, k->to);
    }
}

// The position now, with no motion (a new folder, a page that opens).
static inline void ks_jump(kscroll_t *k, float pos)
{
    k->raw = ks__clamp(k, pos);
    k->vel = 0;
    k->anim = false;
}

// A smooth move to a position (a wheel step, a focus that must show).
static inline void ks_to(kscroll_t *k, float pos)
{
    if (k->touch) {
        return;
    }
    k->to = ks__clamp(k, pos);
    k->vel = 0;
    k->anim = true;
}

static inline void ks_nudge(kscroll_t *k, float d)
{
    ks_to(k, (k->anim ? k->to : k->raw) + d);
}

// The position to draw (px), with the rubber band past the ends.
static inline float ks_pos(const kscroll_t *k)
{
    if (k->raw < 0) {
        return -ks__rub(-k->raw, k->view);
    }
    if (k->raw > k->max) {
        return k->max + ks__rub(k->raw - k->max, k->view);
    }
    return k->raw;
}

static inline int ks_ipos(const kscroll_t *k)
{
    return (int)lroundf(ks_pos(k));
}

// The goal position: where a ks_to() or a glide stops (an estimate of the glide: v * k / (1 - k) of KS_DECEL).
static inline float ks_goal(const kscroll_t *k)
{
    if (k->anim) {
        return k->to;
    }
    return ks__clamp(k, k->raw + k->vel * (KS_DECEL / (1.0f - KS_DECEL)));
}

static inline void ks__sample(kscroll_t *k, float y, int64_t t)
{
    k->sy[k->si] = y;
    k->st[k->si] = t;
    k->si = (uint8_t)((k->si + 1) % KS_NS);
    if (k->ns < KS_NS) {
        k->ns++;
    }
}

// IN_DOWN (y = the finger on the axis of the scroll). Returns true when the touch stops a glide.
static inline bool ks_down(kscroll_t *k, float y, int64_t t)
{
    k->caught = k->anim || fabsf(k->vel) >= KS_VCATCH || k->raw < -0.5f || k->raw > k->max + 0.5f;
    k->fv = k->vel;
    k->vel = 0;
    k->anim = false;
    k->touch = true;
    k->drag = false;
    k->y0 = y;
    k->raw0 = k->raw;
    k->ns = k->si = 0;
    ks__sample(k, y, t);
    return k->caught;
}

// IN_MOVE. Returns true at the move that starts the drag (the app then calls input_tap_cancel()).
static inline bool ks_move(kscroll_t *k, float y, int64_t t)
{
    if (!k->touch) {
        return false;
    }
    bool start = false;
    if (!k->drag) {
        if (fabsf(y - k->y0) * (float)KS_UM_PX < KS_SLOP_UM && !k->caught) {
            ks__sample(k, y, t);
            return false;
        }
        k->drag = start = true;
        k->y0 = y;
        k->raw0 = k->raw;
    }
    if (k->max > 0 || k->raw0 != 0) {
        k->raw = k->raw0 - (y - k->y0);
    }
    ks__sample(k, y, t);
    k->bar = 1;
    k->t_move = t;
    return start;
}

// IN_UP. Returns true when the touch was a drag or stopped a glide (then its tap must not act).
static inline bool ks_up(kscroll_t *k, int64_t t)
{
    if (!k->touch) {
        return false;
    }
    k->touch = false;
    if (k->drag && k->max > 0 && k->ns >= 2) {
        int n = (k->si + KS_NS - 1) % KS_NS, o = n;
        float v = 0;
        if (t - k->st[n] <= KS_STILL_MS) {
            for (int i = 1; i < k->ns; i++) {
                int j = (n + KS_NS - i) % KS_NS;
                if (k->st[n] - k->st[j] > KS_WIN_MS) {
                    break;
                }
                o = j;
            }
            int64_t dt = k->st[n] - k->st[o];
            if (dt >= 8) {
                v = -(k->sy[n] - k->sy[o]) / (float)dt;
            }
        }
        if (fabsf(v) < KS_VMIN) {
            v = 0;
        }
        if (v != 0 && k->caught && k->fv * v > 0) {
            v += k->fv;
        }
        if (v > KS_VMAX) {
            v = KS_VMAX;
        }
        if (v < -KS_VMAX) {
            v = -KS_VMAX;
        }
        if (k->snap > 0 && v != 0) {  // the glide ends on the nearest step (past an end: the end and the spring)
            float g = k->raw + v * (KS_DECEL / (1.0f - KS_DECEL));
            if (g >= 0 && g <= k->max) {
                v = (ks__clamp(k, roundf(g / k->snap) * k->snap) - k->raw) * ((1.0f - KS_DECEL) / KS_DECEL);
            }
        }
        k->vel = v;
    }
    if (k->snap > 0 && k->vel == 0 && k->raw >= 0 && k->raw <= k->max) {  // a slow lift: to the nearest step
        k->to = ks__clamp(k, roundf(k->raw / k->snap) * k->snap);
        k->anim = fabsf(k->to - k->raw) > 0.3f;
    }
    bool r = k->drag || k->caught;
    k->drag = false;
    k->t_move = t;
    return r;
}

// At IN_TAP: false when the touch stopped a glide (the list moved under the finger: no tap on a row).
static inline bool ks_tap_ok(const kscroll_t *k)
{
    return !k->caught;
}

// true while the finger moves the list
static inline bool ks_dragging(const kscroll_t *k)
{
    return k->touch && k->drag;
}

static inline bool ks_moving(const kscroll_t *k)
{
    return (k->touch && k->drag) || k->anim || k->vel != 0 || k->raw < 0 || k->raw > k->max;
}

// Each frame. Returns true while the list moves or its bar fades (draw again).
static inline bool ks_update(kscroll_t *k, int64_t now)
{
    float dt = k->prev ? (float)(now - k->prev) : 16.0f;
    k->prev = now;
    if (dt > 50) {
        dt = 50;
    }
    if (dt < 0) {
        dt = 0;
    }
    bool moving = false;
    if (k->touch) {
        moving = k->drag;
    } else if (k->anim) {
        k->raw += (k->to - k->raw) * (1.0f - powf(KS_SPRING, dt));
        if (fabsf(k->to - k->raw) < 0.3f) {
            k->raw = k->to;
            k->anim = false;
        }
        moving = true;
    } else if (k->raw < 0 || k->raw > k->max) {
        float e = k->raw < 0 ? 0 : k->max;
        if (k->raw < 0 ? k->vel < 0 : k->vel > 0) {  // a glide that goes on past the end: a strong brake
            k->raw += k->vel * dt;
            k->vel *= powf(KS_OUTBRAKE, dt);
            if (fabsf(k->vel) < 0.02f) {
                k->vel = 0;
            }
        } else {  // the spring back
            k->vel = 0;
            k->raw += (e - k->raw) * (1.0f - powf(KS_SPRING, dt));
            if (fabsf(e - k->raw) < 0.3f) {
                k->raw = e;
            }
        }
        moving = true;
    } else if (k->vel != 0) {
        k->raw += k->vel * dt;
        k->vel *= powf(KS_DECEL, dt);
        if (fabsf(k->vel) < KS_VSTOP) {
            k->vel = 0;
        }
        moving = true;
    }
    if (!k->touch && !k->anim && k->vel == 0 && k->snap > 0 && k->raw >= 0 && k->raw <= k->max) {
        float t = ks__clamp(k, roundf(k->raw / k->snap) * k->snap);  // the last px to the step
        if (fabsf(t - k->raw) > 0.3f) {
            k->to = t;
            k->anim = moving = true;
        } else {
            k->raw = t;
        }
    }
    if (moving) {
        k->bar = 1;
        k->t_move = now;
    } else if (k->bar > 0 && now - k->t_move > 400) {
        k->bar -= dt / 250.0f;
        if (k->bar < 0) {
            k->bar = 0;
        }
    }
    return moving || k->bar > 0;
}

// The scroll bar in a track of len px: its start and its length (it gets shorter past an end, as on iOS) and its
// alpha 0..256. Returns false when the list fits the view or the bar is not visible.
static inline bool ks_bar(const kscroll_t *k, int len, int *at, int *size, int *alpha)
{
    if (k->max <= 0 || k->bar <= 0 || len <= 0) {
        return false;
    }
    float total = k->view + k->max, s = (float)len * k->view / total, p = ks_pos(k), over = 0;
    if (p < 0) {
        over = -p;
    }
    if (p > k->max) {
        over = p - k->max;
    }
    s -= over * (float)len / k->view;
    float smin = len < 40 ? (float)len * 0.3f : 12.0f;
    if (s < smin) {
        s = smin;
    }
    float f = ks__clamp(k, p) / k->max;
    *size = (int)lroundf(s);
    *at = (int)lroundf(f * ((float)len - s));
    *alpha = (int)lroundf(k->bar * 256.0f);
    return true;
}
