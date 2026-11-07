// The link test: a small copy of the settings of the OS (main/settings.h)
#pragma once
#include <stdint.h>

int settings_lang_all(void);
uint32_t store_get(const char *key, uint32_t def);
void store_set(const char *key, uint32_t val);
