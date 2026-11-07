// The fingers for input_points() (input.c includes this; a host test runs it on the PC).
//
// The panel can lose ONE of two fingers for a poll or two. If the points then went from 2 to 1 at once, a game would
// see a thumb go up and come down again: a floating stick would start again at the thumb (the hero stops), and the
// right thumb would give a tap (a jump) or break a hold. So each point of a report goes to the nearest finger of the
// last report; a finger that the panel loses keeps its last place for FINGER_HOLD_MS, also while another finger stays
// down; and the fingers keep their order (the first finger down is first), also when the panel changes the order of
// its points.
//
// The panel can also drop a RESTING thumb for the whole tap of the other thumb (262 ms: one
// point in each report). A finger that was down FINGER_REST_MS or more, and is lost while another finger is down, stays
// up to FINGER_HOLD_MULTI_MS (a real lift of it while the other finger is down shows that much later).
//
// The settings panel: input.c must know which finger owns each point of a report (the pull from
// the top hides one finger from the app). fingers_track() also gives the slot of each finger (oslot) and the slot of each
// point of the report (own); a slot keeps its finger until the finger goes. zone and zx, zy belong to input.c (the pull
// guard); a new finger starts with zone 0.
//
// Needs before the include: INPUT_MAX_POINTS, FINGER_HOLD_MS, and the types int16_t, int64_t, bool.

#define FINGER_JUMP_PX 160  // the farthest a finger moves from one report to the next (more: a new finger)
#ifndef FINGER_REST_MS
#define FINGER_REST_MS 150  // a finger down this long is a resting thumb
#endif
#ifndef FINGER_HOLD_MULTI_MS
#define FINGER_HOLD_MULTI_MS                                                                                           \
    150     // how long a lost resting thumb stays while another finger is down (600 until the
            // 14-byte read of board.c; with it the panel keeps both thumbs: a short cover)
#endif

typedef struct {
    bool on;
    int16_t x, y;
    int64_t seen;    // the time of the last report that had this finger (ms)
    int64_t t0;      // the time it came down
    uint32_t seq;    // the order: when the finger came down
    bool multi;      // it was lost while another finger was down (the long hold until it comes back)
    uint8_t zone;    // (input.c) the pull guard of this finger: 0 = none
    int16_t zx, zy;  // (input.c) where it came down
} finger_t;

static finger_t s_fing[INPUT_MAX_POINTS];
static uint32_t s_fing_seq;

// One report of the panel (n points, physical coordinates, after the calibration) at the time t (ms). Writes the
// fingers in their order into ox / oy and returns their count. oslot (or NULL) gets the slot of each finger in that
// order; own (or NULL) gets the slot of each point of the report (-1: a point that got no slot, more than 5 fingers).
static int fingers_track(const int *px, const int *py, int n, int64_t t, int16_t *ox, int16_t *oy, int8_t *oslot,
                         int8_t *own)
{
    bool used[INPUT_MAX_POINTS] = {false}, got[INPUT_MAX_POINTS] = {false};
    if (n > INPUT_MAX_POINTS) {
        n = INPUT_MAX_POINTS;
    }
    if (own) {
        for (int j = 0; j < n; j++) {
            own[j] = -1;
        }
    }
    // the nearest pair first, then the next nearest ...
    for (;;) {
        int bi = -1, bj = -1;
        long bd = (long)FINGER_JUMP_PX * FINGER_JUMP_PX + 1;
        for (int i = 0; i < INPUT_MAX_POINTS; i++) {
            if (!s_fing[i].on || got[i]) {
                continue;
            }
            for (int j = 0; j < n; j++) {
                if (used[j]) {
                    continue;
                }
                long dx = px[j] - s_fing[i].x, dy = py[j] - s_fing[i].y, d = dx * dx + dy * dy;
                if (d < bd) {
                    bd = d, bi = i, bj = j;
                }
            }
        }
        if (bi < 0) {
            break;
        }
        got[bi] = used[bj] = true;
        if (own) {
            own[bj] = (int8_t)bi;
        }
        s_fing[bi].x = (int16_t)px[bj];
        s_fing[bi].y = (int16_t)py[bj];
        s_fing[bi].seen = t;
        s_fing[bi].multi = false;
    }
    // a finger that the panel lost: it stays at its last place for FINGER_HOLD_MS; a resting one that was lost while a
    // NEWER finger was down stays longer (also after that finger is up: the panel can show it again late). (A lost
    // newest finger, a real lift of the tapping thumb, gets the short time: else quick taps would join.)
    bool fresh = false;  // a point that is a new finger (newer than all)
    for (int j = 0; j < n; j++) {
        fresh = fresh || !used[j];
    }
    for (int i = 0; i < INPUT_MAX_POINTS; i++) {
        if (!s_fing[i].on || got[i]) {
            continue;
        }
        bool newer = fresh;
        for (int j = 0; j < INPUT_MAX_POINTS; j++) {
            newer = newer || (s_fing[j].on && got[j] && s_fing[j].seq > s_fing[i].seq);
        }
        if (newer) {
            s_fing[i].multi = true;
        }
        bool rest = s_fing[i].multi && s_fing[i].seen - s_fing[i].t0 >= FINGER_REST_MS;
        if (t - s_fing[i].seen >= (rest ? FINGER_HOLD_MULTI_MS : FINGER_HOLD_MS)) {
            s_fing[i].on = false;
        }
    }
    // a new finger: a free place, the last in the order
    for (int j = 0; j < n; j++) {
        if (used[j]) {
            continue;
        }
        for (int i = 0; i < INPUT_MAX_POINTS; i++) {
            if (s_fing[i].on) {
                continue;
            }
            s_fing[i] = (finger_t){true, (int16_t)px[j], (int16_t)py[j], t, t, ++s_fing_seq, false,
                                   0,    (int16_t)px[j], (int16_t)py[j]};
            if (own) {
                own[j] = (int8_t)i;
            }
            break;
        }
    }
    // the fingers in their order
    int k = 0, idx[INPUT_MAX_POINTS];
    for (int i = 0; i < INPUT_MAX_POINTS; i++) {
        if (!s_fing[i].on) {
            continue;
        }
        int p = k++;
        while (p > 0 && s_fing[idx[p - 1]].seq > s_fing[i].seq) {
            idx[p] = idx[p - 1];
            p--;
        }
        idx[p] = i;
    }
    for (int i = 0; i < k; i++) {
        ox[i] = s_fing[idx[i]].x;
        oy[i] = s_fing[idx[i]].y;
        if (oslot) {
            oslot[i] = (int8_t)idx[i];
        }
    }
    return k;
}

// (the short call: the fingers in their order only)
static inline int fingers_update(const int *px, const int *py, int n, int64_t t, int16_t *ox, int16_t *oy)
{
    return fingers_track(px, py, n, t, ox, oy, NULL, NULL);
}
