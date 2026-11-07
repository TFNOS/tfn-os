// The link test: a small copy of the app API of the OS (main/app.h)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_timer.h"

typedef struct {
    const char *name;
} app_t;
extern const app_t app_walkie, app_talkie;
const app_t *app_current(void);
const app_t *app_find(const char *name);
void app_open(const app_t *a);
bool app_in_match(void);
void app_screen_wake(void);
