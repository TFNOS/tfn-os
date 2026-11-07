#pragma once

// NOTIFICATIONS: clickable notices for the user. An app needs a permission to send one, and an option in SETUP
// turns the notices off. The system can also notify the user of a new firmware, or of an update for an app.
//
// A notice comes from the system, from the manager (POST /api/notify, web_sys.c) or from an app (tfn_notify(); a store
// app needs the permission class "notify"). The board keeps the last NT_MAX notices (PSRAM and /data/notify.bin).
// What the user sees (notify.c):
// - THE BANNER: it drops from the top for 4 s; a tap opens the app at the item of the notice (app_open_arg()), a swipe
//   up puts it away (the notice stays in the list). No banner in a game (a setting), in DO NOT DISTURB, or when the
//   app of the notice is open. With the screen off the notice waits (WAKE THE SCREEN is a choice of each app).
// - THE CENTER: all notices on one page (the chip NOTICES of the settings panel opens it): a tap opens, a swipe of a
//   row to the side removes it, CLEAR ALL (tap again), the buttons of a notice (the answer goes to the manager).
// - THE BADGE: the count of the unread notices of an app on its icon in the menu.
// - SETUP > NOTIFICATIONS: ALL ON / OFF, DO NOT DISTURB, IN GAMES, and for each app ON / LIST ONLY / OFF, WAKE, SOUND.
#include <stdbool.h>
#include <stdint.h>
#include "app.h"
#include "input.h"

// A store app (the ABI name, the permission class "notify"): a notice of the app that runs now.
uint32_t tfn_notify(const char *title, const char *text, const char *link);
