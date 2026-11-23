#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "sdkconfig.h"

// THE BOARD OF THE BUILD: its capabilities (boards/<kind>/board_caps.h): the frame of the apps (LCD_W, LCD_H), the
// native panel, round or not, the wheel ... The shared code tests these names, never the name of a board.
#if CONFIG_TFN_TARGET_TAB5
#include "../../boards/tab5/board_caps.h"
#elif CONFIG_TFN_TARGET_CORES3SE
#include "../../boards/cores3/board_caps.h"
#elif CONFIG_TFN_TARGET_AMOLED18
#include "../../boards/am18/board_caps.h"
#elif CONFIG_TFN_TARGET_WATCHER
#include "../../boards/watcher/board_caps.h"
#elif CONFIG_TFN_TARGET_AMOLED175
#include "../../boards/am175/board_caps.h"
#elif CONFIG_TFN_TARGET_CROW7
#include "../../boards/crow7/board_caps.h"
#else
#include "../../boards/lcd349/board_caps.h"
#endif

#ifndef BOARD_ROUND_HI
#define BOARD_ROUND_HI BOARD_NATIVE_W  // (no own size: the round frame of the native size)
#endif

// The 3.49 bar screen (about 1:3.7). Many apps have a layout for that shape only.
#define BOARD_BAR_SCREEN (LCD_H >= 3 * LCD_W)

// Backlight level 0..255 (0 = off).
void board_backlight(uint8_t level);

// The resistance meter of OHMS (the CoreS3 Port B): G9 is a high output that feeds the
// reference resistor, G8 reads the joint (reference from G9 to G8, the part from G8 to GND).
// board_ohm_start() returns false on a board with no free pins (the 3.49).
bool board_ohm_start(void);
void board_ohm_stop(void);
// The joint voltage in mV (the mean of 32 reads). Returns false when there is no reading.
bool board_ohm_read(int *mv);

// The reports of the touch chip for the test app TOUCH LAB (app_touchlab.c): each report as it came (the 3.49 driver;
// a board with no such log gives none: the weak versions of main/board_common.c). count = the count byte of the chip (no touch on the 3.49: 16); n = the points
// in the report; x, y = physical coordinates NOT clamped (a point off the panel shows a broken report); id, ev = the
// id and the event bits of the chip (0 down, 1 up, 2 contact).
typedef struct {
    uint32_t t_ms;
    uint8_t count, n;
    struct {
        int16_t x, y;
        uint8_t id, ev;
    } p[5];
    uint8_t shift;    // the chip dropped 1 or 2 first bytes; the driver put it back
    uint8_t bad;      // still broken (its count byte: not 0..10, not 16)
    uint8_t raw[14];  // the bytes as they came (bytes 0..13), if shift or bad
} touch_rep_t;
void board_touch_log(bool on);                       // start / stop the log (it is off at start)
int board_touch_reports(touch_rep_t *out, int max);  // the reports since the last call (oldest first)
// The length of a touch read (the 3.49: 32 = up to 5 points, 14 = 2 points); 0 = no such setting on this board.
int board_touch_read_len_get(void);

// The microSD card (FAT) at BOARD_SD_ROOT. The 3.49: the TF slot, SDMMC 1-bit (CLK 41, CMD 39,
// D0 40). The CoreS3: the slot on the SPI bus of the LCD (CS 4, MISO 35 = the LCD DC line: a card
// command waits until the LCD is idle and turns GPIO35 to an input for that time).
// board_sd_mount() mounts the card if it is not mounted (no card: false, after about 0.2 s).
// board_sd_check() unmounts a card that does not answer (it was taken out); it returns the state.
// Safe from any task.
#define BOARD_SD_ROOT "/sdcard"
bool board_sd_mount(void);
bool board_sd_check(void);
bool board_sd_mounted(void);
// The size and the free space of the card in KB. False if there is no card.
bool board_sd_info(uint32_t *total_kb, uint32_t *free_kb);
