# The board guide: how to add a board

This guide tells how to add a new board kind to TFN OS. The example in each step is the M5Stack CoreS3 (kind
`cores3`): its capabilities are in `boards/cores3/board_caps.h` and its driver is `main/board_cores3.c`.

Before you start, open an issue with the form "A board request" (`.github/ISSUE_TEMPLATE/board_request.yml`). A new
kind touches the build, the ABI and the tools, so the maintainer must agree first (`CONTRIBUTING.md`).

## 1. The board kinds now

| Kind | Board | Chip | Frame of the apps | Driver | Defaults file | Partition table |
| --- | --- | --- | --- | --- | --- | --- |
| `lcd349` | Waveshare ESP32-S3-Touch-LCD-3.49 | ESP32-S3 | 172 x 640 (a bar) | `main/board.c` | `sdkconfig.defaults` | `partitions_slim.csv` |
| `cores3` | M5Stack CoreS3 / CoreS3 SE | ESP32-S3 | 240 x 320 | `main/board_cores3.c` | `sdkconfig.defaults.cores3se` | `partitions_slim.csv` |
| `am18` | Waveshare ESP32-S3-Touch-AMOLED-1.8 | ESP32-S3 | 240 x 320, scaled | `main/board_amoled.c` | `sdkconfig.defaults.amoled18` | `partitions_slim.csv` |
| `am175` | Waveshare ESP32-S3-Touch-AMOLED-1.75C | ESP32-S3 | round 466 x 466 | `main/board_am175.c` | `sdkconfig.defaults.amoled175` | `partitions_am175.csv` |
| `watcher` | Seeed SenseCAP Watcher | ESP32-S3 | round 412 x 412 | `main/board_watcher.c` | `sdkconfig.defaults.watcher` | `partitions_slim.csv` |
| `tab5` | M5Stack Tab5 | ESP32-P4 | 240 x 320, scaled 3 times | `main/board_tab5.c` | `sdkconfig.defaults.tab5` | `partitions_tab5.csv` |

Each kind uses `sdkconfig.defaults` first, then its own defaults file. The build of each kind is in
`.github/workflows/firmware.yml` (the list of the defaults files and the chip of each kind).

## 2. The steps

### 2.1 Choose the name of the kind

The kind is a short name in small letters, for example `cores3`. The tools, the app packages (`kind=` in the
manifest), the store and the OTA tag use it. Choose the frame of the apps first: a frame that the apps already know
(240 x 320, round 412 x 412, the 3.49 bar) lets most apps run with no change. A new frame shape needs a layout in each
app.

### 2.2 The Kconfig option

Add the board to the choice "Target board" of `main/Kconfig.projbuild`: one `config TFN_TARGET_<NAME>` with the name of
the board and its panel. The CoreS3 has `CONFIG_TFN_TARGET_CORES3SE`.

### 2.3 boards/<kind>/board_caps.h

Make the folder `boards/<kind>/` and copy `boards/cores3/board_caps.h` into it. Give each name the value of your board.
Each name must be in the header of each board, with its own value (0 = no). The shared code tests these names, never
the name of a board.

`main/tfn/board.h` includes the header of the board of the build: add an `#elif CONFIG_TFN_TARGET_<NAME>` line with
`#include "../../boards/<kind>/board_caps.h"`.

The header has 3 sections. The tables give each name, the value of the CoreS3, and what the name tells.

**The first lines**

| Name | CoreS3 | What it tells |
| --- | --- | --- |
| `BOARD_KIND_NAME` | `"cores3"` | The kind of the build (the app packages, the store, the OTA tag of `main/ota.c`) |

**Section 1: the screen and the UI**

| Name | CoreS3 | What it tells |
| --- | --- | --- |
| `LCD_W`, `LCD_H` | 240, 320 | The frame of the apps in portrait |
| `BOARD_NATIVE_W`, `BOARD_NATIVE_H` | 0, 0 | A big native panel with its own frame (`gfx_native()`); 0 = none |
| `BOARD_ROUND` | 0 | A round panel: only the circle of the native frame shows |
| `BOARD_FIXED_ORIENT` | 0 | No motion sensor for the screen turn: each frame is upright |
| `BOARD_TOUCH_LOGICAL` | 0 | `board_touch_points()` gives logical points of the current frame |
| `BOARD_WHEEL` | 0 | A rotary wheel with a button |
| `BOARD_UI_TABLET` | 0 | The tablet UI: the menu of icons, the home bar, the big layouts |
| `BOARD_HI_FRAME` | 0 | An app can draw at the panel resolution (`gfx_hi_want()`, `gfx_hi()`) |
| `BOARD_ROUND_ZOOM` | 0 | A round panel shows the frame of the apps zoomed to the width of the circle |
| `BOARD_UI_SMOOTH_TEXT` | 0 | The system pages use the anti-aliased font |
| `BOARD_FRAME_MM_PX` | 0.128f | The size of one px of the frame of the apps, in mm |
| `BOARD_NATIVE_MM_PX` | 0.0f | The size of one px of the native frame, in mm (0 = no native frame) |
| `BOARD_ROUND_HI_MM_PX` | 0.0f | The size of one px of the round HI frame, in mm (0 = no such frame) |
| `BOARD_STANDS_LAND` | 1 | The board stands on its long side: the natural frame is landscape |

A round board can also define `BOARD_ROUND_HI` (the AMOLED-1.75C: 466). With no value, `main/tfn/board.h` uses
`BOARD_NATIVE_W`.

**Section 2: the sensors and the input**

| Name | CoreS3 | What it tells |
| --- | --- | --- |
| `BOARD_IMU_QMI8658`, `BOARD_IMU_BMI270`, `BOARD_IMU_WHEEL` | 0, 1, 0 | The motion sensor (one of them is 1; WHEEL = no chip, the wheel is the tilt) |
| `BOARD_IMU_ADDR` | 0x69 | The I2C address of the motion sensor (0 = no chip) |
| `BOARD_IMU_AXIS` | `{{1, 1}, {0, 1}, {2, -1}}` | The axes of a BMI270 in the physical frame of `gfx.h` |
| `BOARD_IMU_TURN_Y` | 0 | A QMI8658 that is turned 180 degrees about the y axis |
| `BOARD_TOUCH_PX_UM` | 128 | The size of one touch px in um (the tap slop of `main/input.c`) |
| `BOARD_KBD_I2C` | 0 | The M5Stack Tab5 Keyboard can connect to the board |
| `BOARD_TOUCH_FRAME_UM` | 128 | The size of one touch px in um in the frame of the apps (`libs/kscroll`) |

**Section 3: the chips, the radio, the sound and the system**

| Name | CoreS3 | What it tells |
| --- | --- | --- |
| `BOARD_RADIO_NATIVE` | 1 | The chip has its own WiFi and BLE radio (0 = the radio is a second chip) |
| `BOARD_EVLOG` | 0 | The event log of the board (`/api/evlog`, the console `evlog`) |
| `BOARD_KIND_CORES3` | 1 | The kind as one name: only the header of the kind defines its own `BOARD_KIND_<KIND>` |
| `BOARD_NAME`, `BOARD_NAME_SHORT` | `"M5Stack CoreS3"` | The full name and the short name of the board |
| `BOARD_PSRAM_MB` | 8 | The size of the PSRAM in MB (the icon budgets of the menu) |
| `BOARD_I2S_MCLK`, `BOARD_I2S_BCLK`, `BOARD_I2S_WS`, `BOARD_I2S_DOUT`, `BOARD_I2S_DIN` | GPIO 0, 34, 33, 13, 14 | The I2S pins of the codecs |
| `BOARD_SPK_AW88298`, `BOARD_SPK_ES8388` | 1, 0 | The speaker codec (both 0 = an ES8311) |
| `BOARD_MIC_SHARED`, `BOARD_MIC_ES7243` | 0, 0 | The microphone (both 0 = an ES7210) |
| `BOARD_MIC_SELECT` | `ES7210_SEL_MIC1 \| ES7210_SEL_MIC2` | The inputs of an ES7210 (0 = no ES7210) |
| `BOARD_USB_PHONES` | 1 | A USB host for USB headphones (`main/usb_phones.c`) |
| `BOARD_CAMERA` | 1 | The camera: 0 = none, 1 = the camera bus of the chip, 2 = an AI chip over SPI |
| `BOARD_BOOT_MENU` | 0 | A boot menu with a second system in `ota_2` (the Tab5) |
| `BOARD_NET_SPEED` | 2 | The speed class in the HELLO of the 2-board games (the faster board is the HOST) |
| `BOARD_FREE_PINS` | `{1, 2, 5, 6, 7, 10, 17, 18}` | The free GPIO pins for the radio modules (`{-1}` = none) |
| `BOARD_RTC_REG` | 0x02 | The time register of the clock chip (0x04 = PCF85063, 0x02 = PCF8563 / BM8563, -1 = none) |
| `BOARD_PMU_LOG` | 0 | The power log of the AXP2101 (the console `pmu`) |
| `BOARD_RFP_WIRING` | `"../../boards/cores3/rfp_wiring.inc"` | The wiring data of the radio and bus apps |

**The wiring file.** `boards/<kind>/rfp_wiring.inc` gives the pads, the sockets and the I2C buses that the radio and
bus apps show (`libs/rfpulse/rfp_board.c` includes it). Copy `boards/cores3/rfp_wiring.inc`, then write the
connectors of your board. Add the connectors to `docs/headers.md` too.

**Check the names.** Each `board_caps.h` must have the same names (except `BOARD_KIND_<KIND>` and the optional
`BOARD_ROUND_HI`). A file that tests a capability must include `board.h` (or a header that includes it) first. Else
the name has no value, and the test is 0 on each board.

### 2.4 The driver: main/board_<kind>.c

Write the driver in `main/board_<kind>.c`. Put all of its code between `#if CONFIG_TFN_TARGET_<NAME>` and `#endif`:
the build compiles all driver files, and each one has code only for its own kind. The name `main/board_<kind>.c` puts
the file into the board layer, the only place where a test of a board name is correct.

Start from the driver of a board that is like yours (`main/board_cores3.c` for an SPI panel with an AXP2101). The
driver gives the functions of `main/board.h` and `main/tfn/board.h`. The CoreS3 driver has these:

| Function | What it does |
| --- | --- |
| `board_init()` | The I2C buses, the power chip, the panel, the touch, the backlight; it starts the `lcd` task |
| `board_lcd_flush()` | Gives one frame buffer to the `lcd` task (it waits until the task is done with the last one) |
| `board_backlight()` | The backlight level, 0 to 255 |
| `board_touch_points()` | The fingers on the panel now |
| `board_pwr_key_down()`, `board_boot_key_down()` | The keys |
| `board_battery()` | The battery level, the voltage, the charge state |
| `board_speaker()`, `board_speaker_hold()` | The speaker amplifier on or off |
| `board_screen_sleep()` | The panel off and on |
| `board_power_off()` | The power off |
| `board_usb_power_out()` | The 5 V output for a USB device (the USB host) |
| `board_sd_card_mount()` | The mount of the SD card; the shared code of `main/board_sd.c` calls it |
| `board_version()`, `board_i2c0()` | The revision of the board and the main I2C bus |

Rules for the driver:
- The `lcd` task runs on core 1 with priority 7. It reads the frame buffer (in PSRAM) and sends it to the panel
  through DMA buffers in internal RAM. Keep the DMA buffers small.
- The parts that a board does not have get the weak defaults of `main/board_common.c` and `main/board_sd.c`. Do not
  write an empty copy.
- Use the shared code: `main/board_sd.c` (the SD card), `main/board_i2c.h` (the chip registers),
  `main/pmic_axp2101.c` (an AXP2101).
- Add the driver to `core.srcs` of `main/apps.json`.

### 2.5 The defaults file: sdkconfig.defaults.<board>

Make `sdkconfig.defaults.<board>` with the values that are not the values of `sdkconfig.defaults`. The CoreS3 file
`sdkconfig.defaults.cores3se` has:

```
CONFIG_TFN_TARGET_CORES3SE=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_TFN_LCD_PCLK_MHZ=80
CONFIG_GC0308_SUPPORT=y
CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX=8192
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_slim.csv"
```

Also give the flash size when it is not 16 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y` for the AMOLED-1.75C) and the
PSRAM mode of the chip.

### 2.6 The partition table

Use `partitions_slim.csv` when the board has 16 MB of flash: two app slots of 3.5 MB (`ota_0`, `ota_1`), `otadata`,
and a FAT partition `storage` (mounted at `/data`) for the app packages. A board with more flash can have its own
table with a bigger `storage` (`partitions_am175.csv`). Keep `nvs` at 0x9000 and `phy_init` at 0xf000, as in all the
tables. A table of your own is "other" for `main/layout.c`: the board keeps its layout, and no update changes it.

### 2.7 The build and the tools

Add the kind to each of these places:

| File | What to add |
| --- | --- |
| `main/CMakeLists.txt` | The kind of the build from the Kconfig option (`set(arcade_kind <kind>)`) |
| `main/apps.json` | The kind in `slim.kinds` (the firmware has only the core apps; the others are packages); the kind in the `only` list of an app that runs only on some kinds |
| `main/tfn_app_priv.h` | `TFN_KIND_<KIND>` (1 when `BOARD_KIND_<KIND>` is defined, else 0), for the conditions of the ABI table |
| `sdk/tools/abi_gen.py` | The kind in `KINDS` and in `KIND_IF` |
| `sdk/tools/mk_app.py` | The kind in `KINDS`, and in `ICON_KIND` when the icon tool of another kind draws its icons |
| `dev.ps1` | The kind in the list of the kinds |
| `.github/workflows/firmware.yml` | The kind in the matrix and its defaults file in the `case` list |

### 2.8 The ABI of the new kind

The ABI lists the firmware names of each kind (`main/tfn_abi.json`, `symbols`). After the first DEV build of the new
kind, run `python sdk/tools/abi_gen.py --kinds <kind>`. It adds the names that the firmware of the kind has, and gives
them the next revision. Then run `python tests/contracts/check_all.py`: the contract of the ABI gets the new kind (a
new kind is an addition, not a loss). `sdk/docs/raw.md` tells about the ABI.

### 2.9 The simulator frame

The simulator (`simulator/`) compiles the firmware for the PC. It has the frames of the 3.49 and of the CoreS3:
`simulator/build.py` (`FRAMES`) and `simulator/idf/sdkconfig.h` (`SIM_FRAME_CORES3` selects the CoreS3 kind). A board
with a frame that the simulator already has needs no change. A new frame shape needs a new entry in both files, then
golden frames for it (`tests/golden/run.py --update`).

## 3. The tests of a new board

1. `python tests/style/check_board_if.py`: no test of a board name out of the board layer.
2. `python tests/style/check_api.py` and `python tests/style/check_format.py <your files>`.
3. `python main/apps_gen.py --check` and `python tests/contracts/check_all.py`.
4. A DEV build of all kinds: the old kinds must give the same image as before (the new kind changes only the board
   layer, its own header and the lists).
5. On the board: the start, the menu, the touch in each corner, SETUP > TOUCH TEST and TILT TEST, the sound, the SD
   card in FILES, a sleep and a wake, the WiFi and the web page, an install of an app package, an OTA update and its
   rollback.
6. The free internal RAM at the menu (the console `mem`): keep about 45 KB or more, else the Bluetooth start fails.

## 4. A new capability

When the shared code needs a fact that no capability gives, add a new name:
1. Add the name to the `board_caps.h` of ALL boards, each with its own value (0 = no), in the correct section.
2. Write a comment above it: what the name tells, and its unit.
3. Use the name in the shared code (`#if BOARD_X` or `if (BOARD_X)`), never a board name.
