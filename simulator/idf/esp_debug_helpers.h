// The simulator: no backtrace of the chip (the print of main.c after a failed request does nothing).
#pragma once
#include "esp_err.h"
static inline esp_err_t esp_backtrace_print(int depth)
{
    (void)depth;
    return ESP_OK;
}
