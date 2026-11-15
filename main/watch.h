#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/watch.h.
#include "tfn/watch.h"

#include <stdbool.h>
#include <stdint.h>

// (main.c) the watch task, after tfn_init()
void watch_init(void);
// the rounds (SETUP > NOTIFICATIONS): 0 = OFF, else the minutes between two rounds (5, 10, 15, 30)
int watch_every(void);
void watch_set_every(int min);
// (notify.c) the name of the app whose watcher runs on this task now (tfn_notify of a watcher), NULL = none
const char *tfn_watch_app_now(void);
// the watchers (the log, the manager): JSON of the loaded watchers, their last check and their faults
int tfn_watch_json(char *out, int max);
// (tests: the console "watch now") a round now with all the watchers
void tfn_watch_now(void);
// (power.c DEEP IDLE) a round is due or runs now: the radio stays on for it
bool watch_busy(void);
