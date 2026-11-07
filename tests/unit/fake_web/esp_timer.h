// A fake of the ESP-IDF timer for the host test of main/tfn_web.c.
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
