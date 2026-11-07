#pragma once
// The Tab5 keyboard (tab5_kbd.c): its keys go to the key queue of input.c (input_key_pop). Only the Tab5 has it.
#include <stdbool.h>
#include "board.h"
#if BOARD_KBD_I2C
void tab5_kbd_init(void);
bool tab5_kbd_present(void);
int tab5_kbd_version(void);  // its firmware version (-1: not seen yet)
#else
static inline void tab5_kbd_init(void)
{
}
static inline bool tab5_kbd_present(void)
{
    return false;
}
static inline int tab5_kbd_version(void)
{
    return -1;
}
#endif
