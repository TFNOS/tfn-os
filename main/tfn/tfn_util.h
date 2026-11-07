#pragma once

#include <stdint.h>
#include "esp_timer.h"

// The time since the start of the board, in ms.
static inline int64_t tfn_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// The same time in 32 bits, for the code that keeps it in a uint32_t (it starts again at 0 after about 49 days).
static inline uint32_t tfn_now_ms32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// v limited to lo .. hi.
static inline float tfn_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
