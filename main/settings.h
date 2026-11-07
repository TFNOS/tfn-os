#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/settings.h.
#include "tfn/settings.h"

#include <stdbool.h>
#include <stdint.h>

#define SLEEP_CHOICES 5
extern const uint16_t SLEEP_SECONDS[SLEEP_CHOICES];  // 0, 30, 60, 120, 300
extern const char *const SLEEP_NAMES[SLEEP_CHOICES];

void settings_load(void);
void settings_save(void);
// Send the volume, mute and brightness to the hardware.
void settings_apply(void);

// the spill store: the keys that the full NVS had no room for (a file in /data; the console storeall, storedel, nvs)
int store_spill_count(void);
// After /data mounts: when the NVS has too little room for the keys of the system (the Bluetooth pairings, the WiFi),
// keys of the apps go to the spill store. Returns the number of keys that went.
int store_make_room(void);
bool store_spill_get(int i, char key[16], uint32_t *val);
bool store_spill_del(const char *key);
// SETUP > APP LANGUAGE: the language of all apps (-1 = not set, 0 English, 1 Hebrew). A missing "*_lang" key of an app
// gives it; settings_lang_all_set() also clears the "*_lang" keys of the apps (each app then follows it).
int settings_lang_all(void);
void settings_lang_all_set(int lang);
