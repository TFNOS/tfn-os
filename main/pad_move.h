#pragma once

// RETRO MOVE (GAMEPAD): the direction comes from the MOTION of the board, not from its
// angle. The IMU task integrates the linear acceleration to a speed with a leak
// (imu_velocity_phys); the app turns that speed into the user's frame (right, back, up)
// and calls pad_move_step() once for each frame. Pure C with no ESP calls, so the host
// simulation runs the same code.
//
// The rules:
// - a press starts when the speed in the plane of the floor goes above v_on;
// - it ends when the speed falls below PM_OFF_K * v_on, or when the speed turns against
//   the pressed direction (the hand stops);
// - the stop of a move gives a short false speed the other way (the gravity filter of the
//   IMU absorbs a part of each move and gives it back after the move). So for
//   PM_REBOUND_MS after a press, a move against it needs more speed (PM_REBOUND_K of the
//   top speed of that press);
// - a touch on the screen jerks the board: no new press for PM_QUIET_MS after a touch
//   goes down or up (pad_move_quiet);
// - HOLD: the direction is down while the hand moves. LATCH: the direction stays down
//   after the move, until a move in another direction; a lift or a drop of the board
//   (a vertical move) lets it go.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// (the host simulation can set other values with -D)
#ifndef PM_OFF_K
#define PM_OFF_K 0.5f  // the end of a press: this part of v_on
#endif
#ifndef PM_REBOUND_MS
#define PM_REBOUND_MS 500
#endif
#ifndef PM_REBOUND_K
#define PM_REBOUND_K 0.75f  // a move against the last press needs this part of its top speed
#endif
#define PM_QUIET_MS 150
#define PM_LIFT_K   1.6f  // LATCH: a vertical speed this many times the flat speed is a lift
#define PM_HYST_DEG 8.0f

enum {
    PM_HOLD,
    PM_LATCH
};

typedef struct {
    int dir;       // the direction that the hand gives now (hat 0..7), -1 = none
    int out;       // the direction to send (HOLD: = dir; LATCH: the latched one)
    float peak;    // the top speed of the press now
    int last_dir;  // the last press (for the rebound rule)
    float last_peak;
    int64_t last_end;     // ms
    int64_t quiet_until;  // ms
    bool lift;            // LATCH: a lift is in progress (it acts once)
} pad_move_t;

static inline void pad_move_reset(pad_move_t *m)
{
    m->dir = m->out = m->last_dir = -1;
    m->peak = m->last_peak = 0;
    m->last_end = -100000;
    m->quiet_until = 0;
    m->lift = false;
}

// A touch went down or up: no new press for a short time.
static inline void pad_move_quiet(pad_move_t *m, int64_t now_ms)
{
    m->quiet_until = now_ms + PM_QUIET_MS;
}

// The direction (hat value: 0 = up, clockwise to 7 = up-left) of the vector (x right,
// y down), with 4 or 8 directions. Near a border the old direction stays (hyst degrees).
static inline int pm_dir_of(float x, float y, int dirs, int old, float hyst)
{
    float a = atan2f(x, -y) * 57.29578f;  // 0 = up, 90 = right
    if (a < 0) {
        a += 360.0f;
    }
    int n = dirs == 4 ? 4 : 8;
    float step = 360.0f / n;
    int d = (int)floorf((a + step / 2) / step) % n;
    if (old >= 0 && (n == 8 || (old & 1) == 0)) {
        int od = n == 4 ? old / 2 : old;
        float diff = fabsf(fmodf(a - od * step + 540.0f, 360.0f) - 180.0f);
        if (diff < step / 2 + hyst) {
            d = od;
        }
    }
    return n == 4 ? d * 2 : d;
}

// the angle between two hat directions, in steps of 45 degrees (0..4)
static inline int pm_dir_gap(int a, int b)
{
    int d = (a - b + 8) % 8;
    return d > 4 ? 8 - d : d;
}

// One frame. vx = the speed to the right, vy = the speed back (toward the body), vup =
// the speed up, all in m/s in the user's frame. Returns the direction to send (-1 = none).
static inline int pad_move_step(pad_move_t *m, float vx, float vy, float vup, int64_t now_ms, int dirs, int style,
                                float v_on)
{
    float sp = sqrtf(vx * vx + vy * vy);
    if (m->dir >= 0) {
        // the speed along the pressed direction (unit vector of the hat value)
        static const float UX[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
        static const float UY[8] = {-1, -0.7071f, 0, 0.7071f, 1, 0.7071f, 0, -0.7071f};
        float along = vx * UX[m->dir] + vy * UY[m->dir];
        if (sp < PM_OFF_K * v_on || along < PM_OFF_K * v_on * 0.5f) {
            m->last_dir = m->dir;
            m->last_peak = m->peak;
            m->last_end = now_ms;
            m->dir = -1;
        } else {
            m->dir = pm_dir_of(vx, vy, dirs, m->dir, PM_HYST_DEG);
            if (sp > m->peak) {
                m->peak = sp;
            }
        }
    } else if (sp > v_on && now_ms >= m->quiet_until) {
        int d = pm_dir_of(vx, vy, dirs, -1, 0);
        float need = v_on;
        if (m->last_dir >= 0 && now_ms - m->last_end < PM_REBOUND_MS && pm_dir_gap(d, m->last_dir) >= 2) {
            float r = PM_REBOUND_K * m->last_peak;
            if (r > need) {
                need = r;
            }
        }
        if (sp > need) {
            m->dir = d;
            m->peak = sp;
            if (style == PM_LATCH) {
                m->out = d;
            }
        }
    }
    if (style == PM_LATCH) {
        // a lift or a drop of the board lets the latched direction go
        float up = fabsf(vup);
        if (!m->lift && up > v_on && up > PM_LIFT_K * sp) {
            m->lift = true;
            m->out = -1;
            m->dir = -1;
            m->last_end = now_ms;
            m->last_dir = -1;
        } else if (m->lift && up < PM_OFF_K * v_on) {
            m->lift = false;
        }
        if (m->lift) {
            m->dir = -1;
        }
    } else {
        m->out = m->dir;
    }
    return m->out;
}
