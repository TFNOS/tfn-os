#pragma once
// The extras of TFN OS: the optional features of the core (main/Kconfig.projbuild, menu "TFN OS extras";
// main/apps.json core.extras). Include this header (not only sdkconfig.h) before an #if of a CONFIG_TFN_EXTRA_*
// option.
#include "sdkconfig.h"

// A build with no ESP-IDF (the host tests and the simulator: no CONFIG_IDF_TARGET) has all the extras on, as the
// defaults of the firmware.
#ifndef CONFIG_IDF_TARGET
#define CONFIG_TFN_EXTRA_HA        1
#define CONFIG_TFN_EXTRA_WEBCAM    1
#define CONFIG_TFN_EXTRA_RF_NATIVE 1
#define CONFIG_TFN_EXTRA_VPN       1
#endif
