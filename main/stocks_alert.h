#pragma once
// STOCKS: the price alerts (plain C: the host tests compile it). The user sets a min and a max price for a stock. The
// board shows a notice (with sound, if the user wants it) when the stock crosses a limit. When a stock changes a lot,
// the board offers new limits, with values.
#include <stdbool.h>
#include <stdint.h>

enum {
    STK_AL_NONE,
    STK_AL_ABOVE,
    STK_AL_BELOW
};

// The watch of one stock: the two limits (0 = off) and their state. A limit fires once when the price crosses it from
// the other side; it is ready again when the price is back over the limit by STK_REARM (0.3 %). A state of -1 = not
// known yet (after a start or a change of the limits): the first price sets it and does not fire.
#define STK_REARM 0.003f
typedef struct {
    float lo, hi;
    int8_t lo_arm, hi_arm;
} stk_watch_t;
void stk_watch_reset(stk_watch_t *w);
// the price now: STK_AL_ABOVE / STK_AL_BELOW when it crossed a limit, else STK_AL_NONE
int stk_watch_step(stk_watch_t *w, float p);

// The step of a price (the alert editor, the suggested values): 10 from 1000, 5 from 200, 1 from 50, 0.5 from 10, 0.1
// from 2, else 0.01.
float stk_step(float p);
// v on a step of its price: down (floor) or up (ceil)
float stk_nice(float v, bool up);

// The offer of new limits. A stock with a limit gets an offer when: a limit fired (fired), or the price moved 5 % or more
// from the price at which the limits were set (ref), or today's change is 5 % or more; and not after KEEP
// at a price (quiet) that is within 5 % of the price now. The values: the price -/+ a band of 1.5 x today's range
// (2.5 % .. 12 %; 5 % when the range is not known), on nice steps. Returns true = offer them.
#define STK_MOVE 0.05f
bool stk_offer(float p, float ref, float quiet, float day_hi, float day_lo, float dp, bool has_limit, bool fired,
               float *lo, float *hi);
