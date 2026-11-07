// The test of the question of the system layer (test_sysui_ask.c): a small copy of main/sysui_priv.h for main/sysui.c.
// It has the headers of the OS that are plain declarations, and the names of the other parts of the OS that sysui.c
// uses (the test file gives each of them).
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn_utf8.h"
#include "sysui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "kfont.h"
#include "mfont.h"
#include "settings.h"
#include "uikit.h"
#include "walkie_call.h"

// (main/app.h)
void app_screen_wake(void);
void menu_redraw_all(void);

// (main/tjpgd_rom.h: the start picture)
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef enum {
    JDR_OK = 0,
    JDR_INP
} JRESULT;
typedef struct {
    uint16_t left, right, top, bottom;
} JRECT;
typedef struct JDEC {
    uint16_t width, height;
    void *device;
} JDEC;
JRESULT jd_prepare(JDEC *jd, UINT (*in)(JDEC *, BYTE *, UINT), void *pool, UINT sz, void *dev);
JRESULT jd_decomp(JDEC *jd, UINT (*out)(JDEC *, void *, JRECT *), BYTE scale);

// (sysui.c, sysui_panel.c) the two bytes of an RGB565 value of the frame change places
static inline uint16_t sw16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
