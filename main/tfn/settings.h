#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t volume;      // 0..100
    uint8_t brightness;  // 10..255
    bool mute;
    bool auto_rotate;  // pick the upright side with the IMU
    uint8_t sleep;     // screensaver: index into SLEEP_SECONDS (0 = never)
} settings_t;

extern settings_t g_settings;

// Small values in NVS (best scores and so on)
uint32_t store_get(const char *key, uint32_t def);
void store_set(const char *key, uint32_t val);
