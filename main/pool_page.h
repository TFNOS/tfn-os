#pragma once
// SETUP > POOL (pool_page.c): the pool of this board (the name, the role, the members, the link counts), LEAVE, and
// the JOIN screen with the 6 digits of the pairing (ALLOW / NO). Only the API of tfn_pool.h.
#include <stdbool.h>
#include <stdint.h>
#include "input.h"

void pool_page_open(void);
// SETUP closes, or the page closes: a JOIN that did not end stops (the board stops 'A' 'J').
void pool_page_leave(void);
void pool_page_update(int64_t now);
bool pool_page_input(const input_event_t *ev, int64_t now);  // true when the page closes (BACK)
void pool_page_render(int64_t now);
// The text of the POOL row of the SETUP list: a short state ("NO POOL", "HOME: 5 MEMBERS", "POOL DAMAGED").
void pool_page_row_text(char *buf, int n);
