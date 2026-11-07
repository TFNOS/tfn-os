#pragma once

// SETTINGS > NOTIFICATIONS (notify_page.c): ALL ON / OFF, DO NOT DISTURB, IN GAMES, and for each app that sends notices
// ON / LIST ONLY / OFF, WAKE THE SCREEN and SOUND (notify.h keeps the values).

#include <stdbool.h>
#include <stdint.h>
#include "input.h"

void notify_page_open(void);
void notify_page_update(int64_t now);
// true when the page closes (BACK)
bool notify_page_input(const input_event_t *ev, int64_t now);
void notify_page_render(int64_t now);
