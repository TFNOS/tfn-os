#pragma once
// SLOWTAP: a slow tap is a tap. The input engine gives IN_LONG to a press of LONG_PRESS_MS (350 ms) or more with no
// move, and no IN_TAP after it. A person who presses slowly (a careful thumb, an older hand) then got nothing from a
// button that acts on IN_TAP (sdk/docs/checklist.md: a slow tap of 350 - 500 ms must do what the person wants).
// st_slow() follows each touch of the app: at the IN_UP of a press with IN_LONG, no move of more than ST_SLOP_PX and
// less than ST_SLOW_MS from its IN_DOWN, it gives an IN_TAP at the point of the IN_DOWN. The app sends that tap to its
// input only on the screens of its buttons (a title, a menu, a card); in the play a hold has its own meaning.
//
// THE USE (one header with static functions: each app has its own copy, no ABI names):
//   static slowtap_t s_slow;
//   static void input(const input_event_t *ev)
//   {
//       input_event_t tap;
//       const bool slow = st_slow(&s_slow, ev, &tap);
//       input_ev(ev);                             // the input function of the app, as before
//       if (slow && <a screen of buttons now>) {
//           input_ev(&tap);
//       }
//   }
#include <stdbool.h>
#include <stdint.h>
#include "tfn/input.h"

#define ST_SLOW_MS 900  // a longer press is a hold, not a tap
#define ST_SLOP_PX 16   // a move of more from the point of the IN_DOWN is a drag

typedef struct {
    int64_t t_down;
    int16_t x, y;
    bool down, longp, moved;
} slowtap_t;

// true: ev is the IN_UP of a slow tap, and *tap is its IN_TAP (at the point of the IN_DOWN, the time of the IN_UP)
static inline bool st_slow(slowtap_t *s, const input_event_t *ev, input_event_t *tap)
{
    switch (ev->kind) {
    case IN_DOWN:
        s->down = true;
        s->longp = false;
        s->moved = false;
        s->t_down = ev->t_ms;
        s->x = ev->x;
        s->y = ev->y;
        break;
    case IN_MOVE: {
        const int dx = ev->x - s->x, dy = ev->y - s->y;
        if (s->down && (dx > ST_SLOP_PX || dx < -ST_SLOP_PX || dy > ST_SLOP_PX || dy < -ST_SLOP_PX)) {
            s->moved = true;
        }
        break;
    }
    case IN_LONG:
        s->longp = true;
        break;
    case IN_UP: {
        const bool slow = s->down && s->longp && !s->moved && ev->t_ms - s->t_down < ST_SLOW_MS;
        s->down = false;
        if (slow) {
            tap->kind = IN_TAP;
            tap->x = s->x;
            tap->y = s->y;
            tap->t_ms = ev->t_ms;
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}
