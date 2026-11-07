// A small copy of the ESP-IDF timer for the link test: the test sets the time (fk_now_us)
#pragma once
#include <stdint.h>

int64_t esp_timer_get_time(void);
