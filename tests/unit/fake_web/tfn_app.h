// The app header for the host test of main/tfn_web.c (test_tfn_web.c): the real header of main/, with the two app
// folders in the work folder of the test. The paths stay short: the board code keeps them in buffers of 48 to 72
// bytes.
#pragma once
#include_next "tfn_app.h"
#undef TFN_DATA_DIR
#undef TFN_CARD_DIR
#define TFN_DATA_DIR "d/apps"
#define TFN_CARD_DIR "c/apps"
