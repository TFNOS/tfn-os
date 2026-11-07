// The parts of the board API (board.h) that are the same on more than one board kind. A board file can have its own
// version of a weak function here: the linker then uses the board version.
#include "board.h"

// The resistance meter of OHMS. Only the CoreS3 has free pins for it (board_cores3.c). The other boards have no
// meter.
__attribute__((weak)) bool board_ohm_start(void)
{
    return false;
}

__attribute__((weak)) void board_ohm_stop(void)
{
}

__attribute__((weak)) bool board_ohm_read(int *mv)
{
    (void)mv;
    return false;
}

// The touch log of TOUCH LAB (tfn/board.h): no reports on a board with no log (board.c of the 3.49 has the real
// ones).
__attribute__((weak)) void board_touch_log(bool on)
{
    (void)on;
}
__attribute__((weak)) int board_touch_reports(touch_rep_t *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}
__attribute__((weak)) int board_touch_read_len_get(void)
{
    return 0;
}
