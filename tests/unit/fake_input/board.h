// The input test: a small copy of the board API of the OS (main/board.h) for main/input.c: the touch panel of the
// test (no INT line) and a wheel that does not turn (input.c compiles all of its parts)
#pragma once
#include <stdbool.h>

#define BOARD_WHEEL 1
int board_wheel_pos(void);
int board_touch_points(int *x, int *y, int max);
int board_touch_int_gpio(void);
void board_touch_int_ack(void);
