#pragma once
// HOME FRONT ALERTS: the alerts of Pikud HaOref for the chosen places, in the background (as the alarms
// of CLOCK): a red screen (the app HOME FRONT opens) and a siren, also in another app or with the screen off.
// OFF by default: the app HOME FRONT turns it on (its settings file and its store keys: libs/oref/oref_core.inc).
// An extra (CONFIG_TFN_EXTRA_OREF): when it is off, these functions are empty.
#include <stdint.h>
#include "tfn_extras.h"

#if CONFIG_TFN_EXTRA_OREF
void oref_init(void);         // main.c: read the settings (the task starts only when the watch is on)
void oref_tick(int64_t now);  // main.c, each pass of the main loop: the settings, the alert, the siren (the flash
                              // work of the service: the poll task has its stack in the PSRAM)
void oref_log(void);          // the console "oref": the state of the watch
#else
static inline void oref_init(void)
{
}
static inline void oref_tick(int64_t now)
{
    (void)now;
}
static inline void oref_log(void)
{
}
#endif
