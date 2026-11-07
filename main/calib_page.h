#pragma once

// SETUP > TOUCH TEST and SETUP > TILT CALIBRATE (calib_page.c): the calibrations for a board with a bad touch
// panel or a bad accelerometer.

#include <stdbool.h>
#include <stdint.h>
#include "input.h"

void calib_touch_open(void);
void calib_tilt_open(void);
void calib_update(int64_t now);
// true when the page closes (BACK)
bool calib_input(const input_event_t *ev, int64_t now);
void calib_render(int64_t now);
