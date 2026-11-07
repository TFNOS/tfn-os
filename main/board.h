#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/board.h.
#include "tfn/board.h"

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "sdkconfig.h"

typedef enum {
    BOARD_V1 = 1,
    BOARD_V2 = 2,
} board_ver_t;

// Start the I2C buses, IO expander, backlight, LCD and touch.
void board_init(void);

board_ver_t board_version(void);

// I2C0 (SDA 47, SCL 48): TCA9554, ES8311 codec, RTC, IMU
i2c_master_bus_handle_t board_i2c0(void);

// Send a full 172x640 frame (RGB565, byte-swapped) to the LCD.
// Send a frame. fb has the logical layout of orientation `orient` (orient_t of gfx.h);
// the LCD task turns it to the physical layout.
void board_lcd_flush(const uint16_t *fb, int orient);

// Speaker amplifier on or off (the self-test runs with it off).
void board_speaker(bool on);
// The screen is off (the screensaver): the amplifier is off; on the CoreS3 the LCD also sleeps
// (the 3.49 LCD must stay awake: its chip also reads the touch panel). false: both back as
// before (the amplifier state of board_speaker()). The backlight is separate (board_backlight()).
void board_screen_sleep(bool sleep);
// While the screen sleeps, keep the amplifier on (an app plays sound in the background).
void board_speaker_hold(bool hold);

// Read one touch point in physical screen coordinates. Returns true when a finger is on the panel.
// Touch points (physical coordinates), up to max. Returns the count (0 = no touch).
int board_touch_points(int *x, int *y, int max);

// The length of a touch read (the 3.49: 32 = up to 5 points, 14 = 2 points; the console "touchread"). 0 = no such
// setting on this board.
void board_touch_read_len(int n);
// The AMOLED touch chip (the console "tpdiag", "tpreg", "tpset"): a count of the reads each second for N s; one
// register read (val < 0) or write. The other boards: nothing (-1).
void board_tp_diag(int secs);
int board_tp_reg(int reg, int val);

// The touch interrupt (input.c, with the screen off): the GPIO that goes low when the touch panel has a touch
// (directly or through an IO expander), or -1 (the 3.49: no such line; then the touch is read each 50 ms).
// board_touch_int_ack() releases a latched expander line (the CoreS3 AW9523B); empty on the other boards.
int board_touch_int_gpio(void);
void board_touch_int_ack(void);

// True while the PWR key is down (the Watcher: the wheel button).
bool board_pwr_key_down(void);
// The second key of the 3.49 (BOOT, GPIO0): down now. The other boards: false (a long press opens CHAT).
bool board_boot_key_down(void);

// BOARD_WHEEL: the position of the wheel in steps (clicks) from the start, + = clockwise.
int board_wheel_pos(void);
// BOARD_ROUND: on = the CoreS3 frame at 1:1 in the middle of the circle (all of it shows); off = the zoom.
void board_round_fit(bool on);
// The raw count of the wheel encoder (for the console check of the steps).
int board_wheel_raw(void);

// Cut the battery power latch. On USB power, go to deep sleep until the PWR key is pushed.
void board_power_off(void);
#if CONFIG_TFN_TARGET_AMOLED18 || CONFIG_TFN_TARGET_AMOLED175
void board_pmu_log(int secs);  // the power log of the AXP2101 (the console "pmu [N]")
void board_pmu_a3(bool on);    // A3V3 (sound, microphone) on/off; on also clears the off of the heat guard
#endif
// 5 V out on the USB-C for a USB device (only the CoreS3; the other boards return false: their USB-C only takes power)
bool board_usb_power_out(bool on);
// The temperature of the chip (the internal sensor of the ESP32-P4 or ESP32-S3) in degrees C; NAN when it cannot read
// it. The first call starts the sensor (SETUP reads it once a second). In power.c, for all boards.
float board_chip_temp(void);

// The battery: the charge in percent (0..100), or -1 when the board cannot tell (no battery).
// *charging (can be NULL) = true while USB power charges the battery (the 3.49 cannot see
// the charger: there it is true when the voltage shows USB power). The board reads the
// battery at most every 2 s, so a call in each frame is cheap. *mv (can be NULL) = the
// battery voltage in mV.
int board_battery(bool *charging, int *mv);
