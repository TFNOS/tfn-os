// The change of the flash layout over the air: a board with the old layout (partitions.csv: two 6 MB app
// slots, /data 3.8 MB) that runs a firmware built for the slim layout (partitions_slim.csv: two 3.5 MB slots, /data
// 8.9 MB) changes its partition table by itself, and keeps all the files of /data and the NVS. MANUAL.md "The change of
// the layout over the air".
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    LAYOUT_OTHER,
    LAYOUT_OLD,
    LAYOUT_SLIM
};

// app_main, after board_init() and before storage_init(): the steps of the change (they can take 1-2 minutes, with a
// progress screen; a step restarts the board). Afterwards /data can be mounted already (storage_ok()).
void layout_boot(void);
// each frame: a board that must change waits until its firmware is marked good and the menu is open, then restarts
void layout_tick(int64_t now);
int layout_now(void);  // LAYOUT_OLD / LAYOUT_SLIM / LAYOUT_OTHER (the table that the board runs now)
// the state for the manager: "" (nothing to do), "wait" (it changes at a restart soon), "noroom" (the files of /data do
// not fit in the archive), "battery" (it waits for more battery), "done" (it changed in this start), "copyfail" (step 1
// cannot give a copy in ota_0 that starts: the board stays on the old table until a next update)
const char *layout_state(void);
void layout_console(
        const char *args);  // the console "lay": the state; "lay test <digits>" = test stops (the last digit first)
// the app files that a change left out (the store has them): /data/apps_dropped.txt; an install takes its id off
void layout_drop_done(const char *id);
int layout_drop_json(char *o, int max);  // ["id", ...] (the JSON of /api/apps "dropped")
