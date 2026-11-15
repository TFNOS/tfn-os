#pragma once

// SETUP > STANDBY (standby_page.c): ON / OFF, the face, AUTO NIGHT and the night hours, the accent colour, the four
// slots of GLANCE (each pill gives the next kind), SHOW IT NOW, the place and the credit of the weather data.

#include <stdbool.h>
#include <stdint.h>
#include "input.h"

void standby_page_open(void);
void standby_page_update(int64_t now);
// true when the page closes (BACK)
bool standby_page_input(const input_event_t *ev, int64_t now);
void standby_page_render(int64_t now);
// the short state for the row of SETUP: "ON, LIVE SKY" or "OFF"
const char *standby_state_text(void);
