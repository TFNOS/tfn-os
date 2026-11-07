#pragma once
// THE CAPABILITIES OF THE BOARD M5Stack CoreS3-SE (kind "cores3").
// main/board.h includes the file of the board of the build. The shared code (main/, libs/, apps/) tests these
// names, never the name of a board: a new board is a new folder boards/<kind>/ with this file and its driver.
// Each name has a value on each board (0 = no).

#define BOARD_KIND_NAME "cores3"

// ---------------------------------------------------------------- the screen and the UI
// the frame of the apps in portrait (ORIENT_PORTRAIT of gfx.h): the short side is horizontal
#define LCD_W          240
#define LCD_H          320
#define BOARD_NATIVE_W 0
#define BOARD_NATIVE_H 0
// BOARD_ROUND: a round panel: the native frame is the square around the circle, and only the circle shows. An app
// with a round layout (app_t.round) gets the native frame (gfx_round()); the other apps draw the CoreS3 frame, and the
// board zooms it to fill the width of the circle.
// BOARD_FIXED_ORIENT: no motion sensor for the screen turn (each frame is upright).
// BOARD_TOUCH_LOGICAL: board_touch_points() gives logical points of the current frame (no turn, no scale in
// gfx_touch_to_logical()).
// BOARD_WHEEL: a rotary wheel with a button (board_wheel_pos(), board_pwr_key_down()).
#define BOARD_ROUND         0
#define BOARD_FIXED_ORIENT  0
#define BOARD_TOUCH_LOGICAL 0
#define BOARD_WHEEL         0
// the tablet UI: the menu of icons, the home bar at the bottom edge, the big layouts of the system apps and of
// the apps
#define BOARD_UI_TABLET 0
// the HI frame (gfx_hi_want/gfx_hi): an app can draw at the panel resolution
#define BOARD_HI_FRAME 0
// BOARD_ROUND_ZOOM: the round panel shows the frame of the apps zoomed to the width of the circle (its corners
// do not show), or at 1:1 for an app with app_t.round_fit (board_round_fit()). 0 = no zoom: the board shows
// all of the frame in the circle (or the panel is not round)
#define BOARD_ROUND_ZOOM 0
// the system pages (SETUP) draw their text in the anti-aliased font, not in the dot font (the dot font is
// coarse on a fine panel)
#define BOARD_UI_SMOOTH_TEXT 0
// the size of one logical px in mm (the thresholds of the pull and the flick of the settings panel): in the
// frame of the apps, in the native frame and in the round HI frame (0 = the board has no such frame)
#define BOARD_FRAME_MM_PX    0.128f
#define BOARD_NATIVE_MM_PX   0.0f
#define BOARD_ROUND_HI_MM_PX 0.0f

// the board stands on its long side: the natural frame of the apps is 320 x 240 (landscape). The bar screen
// of the 3.49 has its own name (BOARD_BAR_SCREEN of board.h)
#define BOARD_STANDS_LAND 1

// ---------------------------------------------------------------- the sensors and the input
// the kind of the motion sensor (one of them is 1): BOARD_IMU_QMI8658 = the QMI8658 chip, BOARD_IMU_BMI270 = the
// BMI270 chip (bmi270_config.c), BOARD_IMU_WHEEL = no chip: the wheel is a left/right tilt (imu.c)
#define BOARD_IMU_QMI8658 0
#define BOARD_IMU_BMI270  1
#define BOARD_IMU_WHEEL   0
// the I2C address of the motion sensor (0 = no chip)
#define BOARD_IMU_ADDR 0x69
// the BMI270 axes in the physical frame of gfx.h (x right, y down in portrait, z into the screen): {sensor axis 0..2,
// sign} for physical x, y and z (imu.c, BMI_AXIS)
#define BOARD_IMU_AXIS {{1, 1}, {0, 1}, {2, -1}}
// the QMI8658 is turned 180 degrees about the physical y axis, compared with the 3.49 (imu.c: the axes 1 and 2 change
// sign)
#define BOARD_IMU_TURN_Y 0
// the size of one px of board_touch_points() in um (input.c: the tap slop in mm)
#define BOARD_TOUCH_PX_UM 128
// the M5Stack Tab5 Keyboard (an STM32 on I2C, tab5_kbd.c) can connect to the board
#define BOARD_KBD_I2C 0

// the size of one px of the touch points in um in the frame of the apps (not the native frame): libs/kscroll
#define BOARD_TOUCH_FRAME_UM 128

// ---------------------------------------------------------------- the chips, the radio, the sound, the system
// the chip has its own WiFi and BLE radio (ESP-NOW, BLE, the native radio); 0 = the radio is a second chip
#define BOARD_RADIO_NATIVE 1
// the event log of the board (evlog.h: GET /api/evlog, the console "evlog")
#define BOARD_EVLOG 0
// the kind of the build as one name: only this file defines it (tfn_app_priv.h sets TFN_KIND_<kind> from it for
// the conditions of the ABI table tfn_abi.inc)
#define BOARD_KIND_CORES3 1
// the name of the board: the full name (the walkie page, /api/walkie/info) and the short name (the header of the board
// web page)
#define BOARD_NAME       "M5Stack CoreS3"
#define BOARD_NAME_SHORT "M5Stack CoreS3"
// the size of the PSRAM (MB): the icon budgets of the menu (tfn_app_priv.h); 32 or more = the icons stay at an app
// load, and a task reads all icons at the start
#define BOARD_PSRAM_MB 8
// the I2S pins of the codecs (the speaker and the microphones)
#define BOARD_I2S_MCLK GPIO_NUM_0
#define BOARD_I2S_BCLK GPIO_NUM_34
#define BOARD_I2S_WS   GPIO_NUM_33
#define BOARD_I2S_DOUT GPIO_NUM_13
#define BOARD_I2S_DIN  GPIO_NUM_14
// the speaker codec: BOARD_SPK_AW88298 = an AW88298, BOARD_SPK_ES8388 = an ES8388; both 0 = an ES8311
#define BOARD_SPK_AW88298 1
#define BOARD_SPK_ES8388  0
// the microphone: BOARD_MIC_SHARED = the ES8311 of the speaker (one device for both), BOARD_MIC_ES7243 = an ES7243
// (or ES7243E); both 0 = an ES7210 with the inputs BOARD_MIC_SELECT (0 = no ES7210)
#define BOARD_MIC_SHARED 0
#define BOARD_MIC_ES7243 0
#define BOARD_MIC_SELECT (ES7210_SEL_MIC1 | ES7210_SEL_MIC2)
// a USB host for USB headphones and USB sound cards (usb_phones.c)
#define BOARD_USB_PHONES 1
// the camera (webcam.c): 0 = none, 1 = on the camera bus of the chip (esp32-camera), 2 = on the Himax AI chip (SSCMA
// over SPI)
#define BOARD_CAMERA 1
// the boot menu of Tab5Boot: TFN OS in ota_0 / ota_1, a different system in ota_2 (an update writes the TFN slot that
// does not run; the console "bootslot")
#define BOARD_BOOT_MENU 0
// the speed class of the board in the HELLO of net2 (the faster board is the HOST; 0 = not known)
#define BOARD_NET_SPEED 2
// the free GPIO pins for the 315 / 433 MHz modules (the console "rf pin|tx|probe", docs/headers.md; {-1} = none)
#define BOARD_FREE_PINS {1, 2, 5, 6, 7, 10, 17, 18}
// the time register of the clock chip at 0x51 (alarm.c): 0x04 = a PCF85063, 0x02 = a PCF8563 / BM8563, -1 = no driver
#define BOARD_RTC_REG 0x02
// the power log of the AXP2101 in the board driver (board_pmu_log(), board_pmu_a3(): the console "pmu")
#define BOARD_PMU_LOG 0
// the wiring data of the radio and bus apps (libs/rfpulse/rfp_board.c: the pads, the sockets, the I2C buses);
// the path from libs/rfpulse/
#define BOARD_RFP_WIRING "../../boards/cores3/rfp_wiring.inc"
