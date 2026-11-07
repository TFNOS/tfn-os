#pragma once
// The private header of the system layer (sysui.h): sysui.c (the message, the locate sound, the question, the
// start picture, the speaker sweep), sysui_layout.c (the layouts of the settings panel) and sysui_panel.c (the
// settings panel). Only these files include it.
#include "tfn_utf8.h"
#include "sysui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "imu.h"
#include "kfont.h"
#include "mfont.h"
#include "notify.h"
#include "settings.h"
#include "uikit.h"
#include "walkie_call.h"
#include "tjpgd_rom.h"

// (sysui.c, sysui_panel.c) the two bytes of an RGB565 value of the frame change places
static inline uint16_t sw16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

// the settings panel: its layouts (sysui_layout.c) and its state (sysui_panel.c)
enum {
    PL_BAR_L,
    PL_BAR_P,
    PL_BOX_L,
    PL_BOX_P,
    PL_NAT_L,
    PL_NAT_P,
    PL_ROUND,
    PL_ZOOM_L,
    PL_ZOOM_P,
    PL_TAB_L,
    PL_TAB_P,
    PL_N
};
enum {
    PC_VOL,
    PC_BRI,
    PC_RESUME,
    PC_MENU,
    PC_N
};
typedef struct {
    int16_t x, y, w, h;
} pbox_t;
typedef struct {
    int16_t H;                   // the height of the open panel (its top is the top of the frame)
    int16_t tx0, tx1, ty0, ty1;  // the band of the app name
    pbox_t c[PC_N];              // VOLUME, BRIGHTNESS, RESUME, MENU (the panel open)
    int16_t zx0, zx1, zy1;       // the pull zone: zx0 <= x < zx1, y < zy1
    int16_t grip_y;              // the top of the grip over the app
    uint8_t ft, fl, fb;          // the fonts (KFS[]): the app name, the slider labels, the buttons (the largest)
} pdef_t;
#define SLOT_MAX 6
typedef struct {
    int16_t H, tx0, tx1, ty0, ty1;
    pbox_t c[PC_N];
    uint8_t ft, fl, fb, nslot;
    pbox_t slot[SLOT_MAX];
} pdefi_t;

// the tables and the functions of the layouts (sysui_layout.c)
extern const pdef_t SUI_PDEF[PL_N];
extern const pdefi_t SUI_PDEFI[PL_N];
float sui_mm_px(void);
int sui_pl_kind(void);
int sui_rs(int v);
void sui_rs_box(pbox_t *b);
void sui_rs_def(pdef_t *d);
const pdef_t *sui_pdef_now(void);
