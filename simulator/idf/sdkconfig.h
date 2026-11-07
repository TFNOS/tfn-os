// The simulator: the Waveshare ESP32-S3-Touch-LCD-3.49 (or another board of build.py --frame: SIM_FRAME_CORES3 = the
// CoreS3 and its 320 x 240 frame, SIM_FRAME_WATCHER = the round SenseCAP Watcher, SIM_FRAME_AM175 = the round
// AMOLED-1.75C, SIM_FRAME_TAB5 = the Tab5 and its tablet UI) with the defaults of main/Kconfig.projbuild and
// sdkconfig.defaults. (It is found
// before tests/host/inc*/sdkconfig.h.) Only the values that the code reads are here; the options of the IDF that select
// hardware code (PM, the console UART) stay off.
#pragma once
#if SIM_FRAME_CORES3
#define CONFIG_TFN_TARGET_CORES3SE 1
#elif SIM_FRAME_WATCHER
#define CONFIG_TFN_TARGET_WATCHER 1
#elif SIM_FRAME_AM175
#define CONFIG_TFN_TARGET_AMOLED175 1
#elif SIM_FRAME_TAB5
#define CONFIG_TFN_TARGET_TAB5 1
#else
#define CONFIG_TFN_TARGET_WS349 1
#endif
#define CONFIG_TFN_BRIGHTNESS 230
#define CONFIG_TFN_VOLUME 85
#define CONFIG_TFN_LCD_PCLK_MHZ 40
#define CONFIG_TFN_IMU_USE_GYRO 1
// (The extras CONFIG_TFN_EXTRA_*: main/tfn_extras.h. A build with no ESP-IDF has each extra of the tree on.)
#define CONFIG_FREERTOS_HZ 1000
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240
