#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/alarm.h.
#include "tfn/alarm.h"

#include <stdbool.h>
#include <stdint.h>

#define ALARM_SOUNDS 4

void alarm_init(void);  // main.c: load, read the clock chip, start the task
void clock_log(void);   // the console "clock": the source, the time, the time of the chip
