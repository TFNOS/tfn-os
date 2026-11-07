#pragma once

// THE BACKGROUND CHECKS OF THE APP FILES (the board groups the network requests of
// several apps into one batch to save power). An app file can have a second, small part: watch.elf (sdk/tools/mk_app.py builds it
// from the "watch" sources of the app; the permission class "background", CHECKS THE NETWORK IN THE BACKGROUND). The
// board keeps it loaded in PSRAM, also while other apps run, and calls its check() in the background rounds: one wake
// of the radio and the CPU for all the watchers that are due (SETUP > NOTIFICATIONS: BACKGROUND CHECKS every 5, 10, 15
// or 30 minutes, or OFF). A check looks at the network (libs/tfnet) and sends a notice for each new item (tfn_notify,
// notify.h). The rules of a check:
// - it cannot draw, play a sound or read the touch: the loader links no such name into watch.elf (the app does that);
// - about 8 s at most (20 s: a fault; three faults in a row turn the watcher off), no task of its own;
// - no round while a 2-board link runs, or while a game shows (up to 15 minutes later);
// - a check that stops the board (a crash) is off from the next start (a notice says so), until a new version of the
//   app comes.
// The app and its watcher share their saved values (store_get / store_set with the saves prefix of the app), their
// files and the secret store below.
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t every_min;   // the time between two checks (minutes): the board runs it in the next round after it
    void (*init)(void);   // once, after the load (NULL = nothing)
    void (*check)(void);  // one check (the watch task: a low priority, core 0, a stack of 8 KB)
} watch_t;

// THE SECRET STORE (the permission class "secret", KEEPS A SECRET KEY ON THE BOARD): a key, a token or a password of
// the app, for the app and for its watcher. Each app reads only its own values (the NVS namespace "tfnsec": the console
// storeall does not show it). Up to 400 bytes a value. A full flash read by USB can still get it (no encryption).
int tfn_secret_get(const char *key, char *out, int max);  // the length of the value, -1 = no value
bool tfn_secret_set(const char *key, const char *value);  // NULL or "" = the value goes
