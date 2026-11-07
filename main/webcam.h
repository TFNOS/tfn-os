// The web camera (the CoreS3 GC0308): the page /cam and the picture /api/cam.jpg.
// The camera starts at the first picture request and stops 15 s after the last one.
// The other boards have no camera: /api/cam.jpg answers 404 and the page says so.
// An extra (CONFIG_TFN_EXTRA_WEBCAM): when it is off, the board has no camera and these functions are empty.
#pragma once
#include <stdbool.h>
#include "tfn_extras.h"

#if CONFIG_TFN_EXTRA_WEBCAM
bool webcam_supported(void);  // this board can have a camera (the CoreS3 target)
bool webcam_active(void);     // a browser got a picture in the last 15 s
void webcam_register(void *httpd);
void webcam_overlay(void);  // "CAMERA ON" at the top of the screen while it is on
// The console "wcam ...": the Watcher sends one AT command to its AI chip and prints the replies for 3 s ("wcam
// AT+ID?"); "wcam reset" resets the chip. Other boards: a line that says that there is no such chip.
void webcam_console(const char *line);
#else
static inline bool webcam_supported(void)
{
    return false;
}
static inline bool webcam_active(void)
{
    return false;
}
static inline void webcam_overlay(void)
{
}
#endif
