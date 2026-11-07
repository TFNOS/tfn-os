// A small copy of the ESP-IDF sleep API for the input test
#pragma once
#include "esp_err.h"

esp_err_t esp_sleep_enable_gpio_wakeup(void);
