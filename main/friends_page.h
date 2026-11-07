#pragma once

// SETTINGS > FRIENDS (friends_page.c)

#include <stdbool.h>
#include <stdint.h>
#include "input.h"

void friends_page_open(void);
void friends_page_update(int64_t now);
// true when the page closes (BACK)
bool friends_page_input(const input_event_t *ev, int64_t now);
void friends_page_render(int64_t now);
void friends_page_leave(void);
