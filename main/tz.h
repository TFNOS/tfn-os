#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/tz.h.
#include "tfn/tz.h"

#include <stdbool.h>
#include <stdint.h>

// the days from 1970-01-01 to a date, and back
int64_t tz_days(int y, int m, int d);
void tz_date(int64_t days, int *y, int *m, int *d);

// a / b, rounded down (also for a < 0): a time in seconds -> its day
static __attribute__((unused)) int64_t tz_fdiv(int64_t a, int64_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// the day of the week (0 = Sunday) of a day since 1970
static __attribute__((unused)) int tz_wday(int64_t days)
{
    return (int)(((days + 4) % 7 + 7) % 7);
}  // (1970-01-01 was a Thursday)
