#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/input.h.
#include "tfn/input.h"

#include <stdbool.h>
#include <stdint.h>

// Start the touch task (reads the touch panel at 100 Hz).
void input_init(void);

// Get the next event. Returns false when there are no more events.
bool input_get(input_event_t *ev);

// Put an event in the queue (physical coordinates), for tests and for the keys of the board. Its events are not by a
// finger on the panel (input_injected()).
void input_inject(const input_event_t *ev);

// ---- the rule of a consent: ALLOW of a publisher key or of a pool JOIN, LEAVE of a pool, a new permission of an app,
// the DEVELOPER MODE on (SETUP)
// A consent acts only for a tap of a finger on the panel. input_injected(ev): true = the event ev is not by a finger
// on the panel: input_inject() put it in the queue (the console "tap", a key of the board), or a virtual finger (the
// console "finger" and "finger2", a tap of the manager page: /api/ctl) was in a report of its touch. The touch task
// marks each event at its source, and the mark goes through the queue with the event: the time that an event waits in
// the queue does not change the answer. ev = the event that input_get() gave last, as main.c gives it to sysui_input()
// or to an app (logical points, the app clock). Each other event (a key, an event that a part of the OS makes) is not
// by a finger. The USB console has its own command for each consent ("pub allow", "pool allow", "pool leave",
// "apps get", "apps dev 1"): a person with the cable can write any firmware.
// The mark stays on the full touch: a real finger that comes down while a virtual finger is on (or a short time after
// it goes up) joins that touch, and its tap does not answer a consent. So a web client that sends long presses or
// swipes (/api/ctl) all the time can block each consent at the board. This is safe (no consent acts), and the USB
// console commands above still work.
bool input_injected(const input_event_t *ev);
// A consent also needs a touch that came down after the question showed, and not in its first INPUT_ARM_MS: a finger
// that was on the glass for another thing when the question came does not answer it. The tap must be of the touch
// whose IN_DOWN the question got: one IN_UP between them, no IN_LONG, and less than LONG_PRESS_MS from the IN_DOWN to
// the tap (the touch task gives an IN_TAP only when the last contact of the touch, the time of the IN_TAP, is less
// than LONG_PRESS_MS after its IN_DOWN). So a tap whose IN_DOWN went to another part (the screen saver, the locate
// screen, a full queue) does not answer, also after a long press or a drag on the question. The part that shows a
// question keeps an input_consent_t: input_consent_reset() for a new question, input_consent_shown() in each frame that
// shows it, and input_consent_event() for each event that it gets (before it reads a tap). The times are the clock of
// its events.
// (The STORE question of a new permission is a consent. The install of a signed package from the web, POST
// /api/apps?op=commit, and the token JOIN of a LEGACY build are not under this rule: docs/security.md, section 2.5.)
#define INPUT_ARM_MS 800
enum {
    INPUT_CONSENT_NONE,        // not an IN_TAP
    INPUT_CONSENT_OK,          // a tap by a finger; its touch came down INPUT_ARM_MS or more after the question showed
    INPUT_CONSENT_NOT_FINGER,  // a tap that is not by a finger on the panel (input_injected()): tell it
    // a tap by a finger of a touch that came down before the question showed or too soon, or of a touch whose IN_DOWN
    // the question did not get
    INPUT_CONSENT_EARLY
};
typedef struct {
    int64_t shown_ms;  // the first frame of the question (0 = not shown yet)
    int64_t down_ms;   // the IN_DOWN of the touch now: by a finger, INPUT_ARM_MS or more after shown_ms (0 = none)
    bool up;           // the IN_UP of that touch came (its IN_TAP comes next)
} input_consent_t;
static inline void input_consent_reset(input_consent_t *c)
{
    c->shown_ms = 0;
    c->down_ms = 0;
    c->up = false;
}
static inline void input_consent_shown(input_consent_t *c, int64_t now_ms)
{
    if (!c->shown_ms) {
        c->shown_ms = now_ms;
    }
}
static inline int input_consent_event(input_consent_t *c, const input_event_t *ev)
{
    const bool finger = !input_injected(ev);
    if (ev->kind == IN_DOWN) {
        c->down_ms = finger && c->shown_ms && ev->t_ms >= c->shown_ms + INPUT_ARM_MS ? ev->t_ms : 0;
        c->up = false;
        return INPUT_CONSENT_NONE;
    }
    if (ev->kind == IN_LONG || (ev->kind == IN_UP && c->up)) {
        c->down_ms = 0;  // (a long press gives no tap; a second IN_UP is of another touch)
        return INPUT_CONSENT_NONE;
    }
    if (ev->kind == IN_UP) {
        c->up = true;
        return INPUT_CONSENT_NONE;
    }
    if (ev->kind != IN_TAP) {
        return INPUT_CONSENT_NONE;
    }
    const int64_t dt = ev->t_ms - c->down_ms;
    const bool armed = c->down_ms != 0 && c->up && dt >= 0 && dt < LONG_PRESS_MS;
    c->down_ms = 0;  // (one touch gives one tap)
    c->up = false;
    if (!finger) {
        return INPUT_CONSENT_NOT_FINGER;
    }
    return armed ? INPUT_CONSENT_OK : INPUT_CONSENT_EARLY;
}

// Remove all queued events.
void input_flush(void);
// The keys of a keyboard (the Tab5 keyboard driver, tab5_kbd.c) for main.c: a queue of 128 (INK_* + INM_* << 16)
void input_key_push(int key, int mods);
bool input_key_pop(int *key, int *mods);
// Tests (the console "finger"): a virtual finger at the physical point (px, py) until the time until_ms
// (0 = up). The touch task adds it to the points of the panel, so the events, the end buttons and the
// points of the apps see it as a real finger. Only a consent sees the difference (input_injected()).
void input_virtual_finger(int px, int py, int64_t until_ms);
// The touch calibration (SETUP > TOUCH TEST): an affine map of the panel points in the physical
// coordinates, x' = m[0] x + m[1] y + m[2], y' = m[3] x + m[4] y + m[5]. m = NULL: none. It is saved.
void input_touch_cal_set(const float m[6]);
bool input_touch_cal_get(float m[6]);
// The points of the panel now with no calibration (physical coordinates)
int input_points_raw(int16_t *x, int16_t *y, int max);
// Read the touch panel less often (the screen is off: a touch only wakes it).
void input_slow(bool slow);

// Tests: a second virtual finger (physical coordinates) until the time until_ms (0 = none); see input_virtual_finger.
void input_virtual_finger2(int px, int py, int64_t until_ms);

// ---- the pull zone of the settings panel (main.c sets it, sysui.c reads the pull) ----
// A NEW finger that comes down in the zone (a small box in the middle of the top edge) does not go to the app at once:
// the touch task holds its events. A move of pull_px or more DOWN within PULL_WIN_MS, with the move down at least 1.5
// times the side move, is a pull: the finger then belongs to the panel, and the app never sees it (no events, not in
// input_points(), not on the end buttons). Else (a side move of side_px, a move up, a lift, or HOLD_MS with no move down)
// the held events go to the app with their own times (at most PULL_WIN_MS late), and the finger is a normal finger.
// A finger that comes down out of the zone is never held, also when it moves into the zone later.
#define PULL_HOLD_MS 140  // a still finger in the zone: its events go to the app after this time
#define PULL_WIN_MS  250  // a finger that moves down: the time for the pull
enum {
    ZONE_HOLD,
    ZONE_WATCH,
    ZONE_TAKE
};
typedef struct {
    bool on;       // false: no hold, no pull
    uint8_t mode;  // ZONE_HOLD: the guard above; ZONE_WATCH: no hold, a pull only gives a report (the
                   // finger stays the app's: the first step of the two-step pull); ZONE_TAKE: each finger
                   // that comes down in the zone is the panel's at once (the tab of the two-step pull)
    int16_t x0, y0, x1,
            y1;     // the zone in the touch frame (the points of board_touch_points): x0 <= x < x1, y0 <= y < y1
    int8_t dx, dy;  // "down" of the app frame as a step in the touch frame (one of them 1 or -1, the other 0)
    int16_t pull_px, side_px;  // the move down for a pull; the side or up move that ends the hold (touch px)
} input_zone_t;
void input_pull_zone(const input_zone_t *z);
// The finger of the panel: seq + 1 at each new pull (or a touch in a ZONE_TAKE zone). down = it is on the glass now;
// (x, y) = its point now, (x0, y0) = where it came down, (xp, yp) = its point about 80 ms before (x, y), at tp_ms (the
// speed of a flick); touch frame. watch = a ZONE_WATCH report (the app has the finger). False: no pull since the start.
typedef struct {
    uint32_t seq;
    bool down, watch;
    int16_t x, y, x0, y0, xp, yp;
    int64_t t_ms, t0_ms, tp_ms;
} input_pull_t;
bool input_pull_get(input_pull_t *p);
// While the panel shows: the app sees no finger (input_points() gives none, the end buttons are up).
void input_hide(bool hide);
// The panel comes: each finger that is down now is nobody's until it goes up (no events, no points, no end button), so
// that a thumb that rests on the screen does not keep the events, and a tap on the panel gets them. The touch of the
// event stream ends with an IN_UP and no IN_TAP.
void input_cut(void);
// The app clock (main.c: the time that the app was frozen under the panel): input_ends() gives its times less this.
void input_time_shift(int64_t ms);

void input_wheel_add(int steps);       // (main.c: the arrow keys of a keyboard as wheel steps)
void input_wheel_press_add(void);      // (main.c)
void input_wheel_set_down(bool down);  // (main.c)
