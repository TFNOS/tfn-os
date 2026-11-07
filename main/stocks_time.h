#pragma once
// STOCKS: the time rules (plain C, no ESP-IDF: the host tests compile it). The US market sessions in New York time
// (the US summer time rules), the holidays of the exchange (from Finnhub) and the next opening. The dates and the
// local time zones are in tz.h.
#include <stdbool.h>
#include <stdint.h>
#include "tz.h"

enum {
    STK_CLOSED,
    STK_PRE,
    STK_OPEN,
    STK_POST
};

// New York: the offset from UTC in seconds (-4 h in the summer time, else -5 h)
int stk_ny_offset(int64_t utc);

// The holidays of the exchange: a closed day (close_min 0), or an early close (close_min = the minute of the close in
// New York, 780 = 13:00; post_end_min = the end of the after-hours trade, 0 = 4 hours after the close).
typedef struct {
    int32_t ymd;
    int16_t close_min, post_end_min;
} stk_hol_t;
#define STK_HOL_MAX 40
void stk_set_holidays(const stk_hol_t *h, int n);
int stk_holidays(stk_hol_t *out, int max);
// the holiday of this New York date (yyyymmdd), or NULL
const stk_hol_t *stk_holiday(int32_t ymd);

// The session at this time (STK_*) and its bounds (unix s; NULL: not needed). STK_CLOSED: from the end of the last
// session to the start of the next pre-market.
int stk_session(int64_t utc, int64_t *start, int64_t *end);
// The next regular opening (09:30 New York) at or after this time, when the regular session is not open now (in a
// pre-market: the opening of today). 0 = none in the next 20 days.
int64_t stk_next_open(int64_t utc);
// The start of the next pre-market (04:00 New York) after this time.
int64_t stk_next_pre(int64_t utc);
// The New York date (yyyymmdd) of a time: the key of a trading day.
int32_t stk_ny_ymd(int64_t utc);
