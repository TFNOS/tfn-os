#pragma once
// The private header of SETUP: main/app_settings.c (the list, the app) and its parts app_settings_wifi.c (the
// WiFi and QR pages) and app_settings_draw.c (the drawing of the list). Only these files include it.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn_util.h"
#include "uikit.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "bt_pad.h"
#include "gfx.h"
#include "imu.h"
#include "kbd.h"
#include "friends.h"
#include "friends_page.h"
#include "calib_page.h"
#include "ota.h"
#include "qr.h"
#include "settings.h"
#include "power.h"
#include "storage.h"
#include "wifi.h"
#include "tfn_app.h"
#include "tfn_pub.h"
#include "tfn_sstore.h"
#include "tfn_defaults.h"
#include "pool_page.h"
#include "usb_audio.h"
#include "usb_phones.h"
#include "notify.h"
#include "notify_page.h"
#include "standby_page.h"
#include "sysui.h"
#include "web_auth.h"
#include "device_guard.h"
#if CONFIG_TFN_DEVICE_PROTECTION
#define GUARD_BUILD 1
#else
#define GUARD_BUILD 0
#endif
#include "../libs/kscroll/kscroll.h"
#if BOARD_UI_SMOOTH_TEXT || BOARD_ROUND
// THE TAB5 TEXT (also the AMOLED-1.75C): the dot font of the 3.49 rows is coarse at 1280 x 720 and on the scaled
// frame of a fine AMOLED. Each
// gfx_text* call of this page uses an anti-aliased font of about the same size. scale 1 = KF_LABEL, 2 = KF_TEXT,
// 3 = KF_BIG, 4 and more = KF_TITLE. y stays the top of the dot letters (7 x scale rows); the letters of the font sit in
// the middle of that band.
#define SCR_W_T5 640
int set_t5_text_width(const char *s, int sc);
void set_t5_text(int x, int y, const char *s, int sc, uint16_t c);
void set_t5_text_center(int y, const char *s, int sc, uint16_t c);
void set_t5_text_center_in(int x, int w, int y, const char *s, int sc, uint16_t c);
#define gfx_text           set_t5_text
#define gfx_text_width     set_t5_text_width
#define gfx_text_center    set_t5_text_center
#define gfx_text_center_in set_t5_text_center_in
#endif

#define DRAG_PX 10    // finger travel that starts a scroll (same as the tap limit)
#if BOARD_ROUND_ZOOM  // (the 1.75C shows the 320 x 240 frame scaled: its rows)
#define BTN_W 56      // the Watcher (SETUP at 1:1, 0.089 mm/px): the zones of the controls are 60 px (5.3 mm)
#define CTL_H 52
#else
#define BTN_W 44
#define CTL_H 40
#endif

// SET_WIDE: the layout of one line in each row, 640 px wide: the 3.49 bar screen (640 x 172) and the Tab5 (640 x 360:
// the same rows, more of them on the screen)
#define SET_WIDE (BOARD_BAR_SCREEN || BOARD_UI_TABLET)
#if SET_WIDE
// the 640 x 172 bar screen: the name and the controls on one line
#define SCR_W 640
#if BOARD_UI_TABLET
#define SCR_H 360
#else
#define SCR_H 172
#endif
#define BACK_W 64  // the BACK strip at the left (all the height)
#define LIST_X 72  // the rows: x 72 .. 622
#define LIST_W 550
#define SBAR_X 628  // the scroll bar
#define ROW_H  56
#define TEXT_X (LIST_X + 12)
// controls at the right of a row end at x 556: x 560..622 has no control, so a tap
// there does not change a setting (the on-board self-test taps at 566, 70)
#define MINUS_X    300
#define BAR_X      352
#define BAR_W      152
#define PLUS_X     512
#define BAR_DY     9    // the volume and brightness bars: y + 9 .. y + 31 in a row
#define CTL_Y      8    // controls: y + 8 .. y + 48 in a row
#define RIGHT_X    436  // a wide button at the right: 436 .. 556
#define RIGHT_W    120
#define SLP_PREV_X (RIGHT_X - 40)  // SCREEN SLEEP: < and the value box
#define SLP_NEXT_X (RIGHT_X + 10)
#define SLP_NEXT_W (RIGHT_W - 10)
#define HS_PREV_X  280  // HOTSPOT: < mode >
#define HS_NEXT_X  380
#define HSQR_X     (TEXT_X - 8)  // HOTSPOT: a tap on the text opens the QR page
#define HSQR_Y     0
#define HSQR_W     190
#define HSQR_H     (ROW_H - 4)
#define TILT_CX    520
#define TILT_R     22
#else
// the 320 x 240 screen: the name at the top of a row, the controls below it
#define SCR_W 320
#define SCR_H 240
#if BOARD_ROUND_ZOOM
// the Watcher: a BACK strip of 60 px (5.3 mm) and rows of 86 px with controls of 52 px (zones of 60 px)
#define BACK_W     56
#define LIST_X     62  // the rows: x 62 .. 308
#define LIST_W     246
#define ROW_H      86
#define MINUS_X    72
#define BAR_X      136
#define BAR_W      104
#define PLUS_X     248
#define SLP_NEXT_X 136
#define SLP_NEXT_W 164
#define HS_PREV_X  72
#define HS_NEXT_X  136  // (the mode name is on the name line)
#define NAME_N     16   // the letters of a network name beside a button
#else
#define BACK_W     40  // the BACK strip at the left (all the height)
#define LIST_X     46  // the rows: x 46 .. 308
#define LIST_W     262
#define ROW_H      76  // three rows on the screen
#define MINUS_X    56
#define BAR_X      108
#define BAR_W      140
#define PLUS_X     256
#define SLP_NEXT_X 104
#define SLP_NEXT_W 196
#define HS_PREV_X  56
#define HS_NEXT_X  152
#define NAME_N     20
#endif
#define SBAR_X     312  // the scroll bar
#define TEXT_X     (LIST_X + 10)
#define NOTE_R     (LIST_X + LIST_W - 8)  // the right end of a short note on the name line
#define CTL_Y      26                     // controls: y + 26 .. y + 26 + CTL_H in a row
#define BAR_DY     (CTL_Y + (CTL_H - 22) / 2)
#define RIGHT_X    200  // a wide button at the right: 200 .. 300
#define RIGHT_W    100
#define SLP_PREV_X MINUS_X
#define TILT_CX    276
#define TILT_R     20
#if BOARD_ROUND
// THE ROUND LIST: in the native round frame (APP_ROUND_HI: 466 x 466, 412 on the Watcher; SR() scales the values of
// 466) the title is on the top cap, the rows are in a middle box (y SET_LIST_Y .. SET_LIST_B), BACK is on the bottom
// cap. The other pages of SETUP show the 320 x 240 frame in the circle: each value below chooses at run time (the
// values of the 320 x 240 frame after ':').
#define SET_RND gfx_round()
#define SR(v)   ((v) * gfx_w / 466)
enum {
    SF_BACK_W = BACK_W,
    SF_LIST_X = LIST_X,
    SF_LIST_W = LIST_W,
    SF_ROW_H = ROW_H,
    SF_MINUS_X = MINUS_X,
    SF_BAR_X = BAR_X,
    SF_BAR_W = BAR_W,
    SF_PLUS_X = PLUS_X,
    SF_SLP_NEXT_X = SLP_NEXT_X,
    SF_SLP_NEXT_W = SLP_NEXT_W,
    SF_HS_PREV_X = HS_PREV_X,
    SF_HS_NEXT_X = HS_NEXT_X,
    SF_BTN_W = BTN_W,
    SF_CTL_H = CTL_H
};
#undef SCR_W
#undef SCR_H
#undef BACK_W
#undef LIST_X
#undef LIST_W
#undef ROW_H
#undef MINUS_X
#undef BAR_X
#undef BAR_W
#undef PLUS_X
#undef SLP_NEXT_X
#undef SLP_NEXT_W
#undef HS_PREV_X
#undef HS_NEXT_X
#undef BTN_W
#undef CTL_H
#undef SBAR_X
#undef CTL_Y
#undef RIGHT_X
#undef RIGHT_W
#undef TILT_CX
#define SCR_W      (SET_RND ? gfx_w : 320)
#define SCR_H      (SET_RND ? gfx_h : 240)
#define BACK_W     (SET_RND ? 0 : SF_BACK_W)
#define LIST_X     (SET_RND ? SR(64) : SF_LIST_X)
#define LIST_W     (SET_RND ? SR(338) : SF_LIST_W)
#define ROW_H      (SET_RND ? SR(92) : SF_ROW_H)
#define BTN_W      (SET_RND ? SR(56) : SF_BTN_W)
#define CTL_H      (SET_RND ? SR(46) : SF_CTL_H)
#define MINUS_X    (SET_RND ? LIST_X + SR(12) : SF_MINUS_X)
#define BAR_X      (SET_RND ? MINUS_X + BTN_W + SR(10) : SF_BAR_X)
#define BAR_W      (SET_RND ? LIST_W - 2 * BTN_W - SR(44) : SF_BAR_W)
#define PLUS_X     (SET_RND ? BAR_X + BAR_W + SR(10) : SF_PLUS_X)
#define SLP_NEXT_X (SET_RND ? MINUS_X + BTN_W + SR(10) : SF_SLP_NEXT_X)
#define SLP_NEXT_W (SET_RND ? LIST_X + LIST_W - SR(12) - SLP_NEXT_X : SF_SLP_NEXT_W)
#define HS_PREV_X  (SET_RND ? MINUS_X : SF_HS_PREV_X)
#define HS_NEXT_X  (SET_RND ? HS_PREV_X + BTN_W + SR(10) : SF_HS_NEXT_X)
#define SBAR_X     (SET_RND ? LIST_X + LIST_W + SR(8) : 312)
#define CTL_Y      (SET_RND ? SR(38) : 26)
#define RIGHT_X    (SET_RND ? LIST_X + LIST_W - SR(12) - RIGHT_W : 200)
#define RIGHT_W    (SET_RND ? SR(130) : 100)
#define TILT_CX    (SET_RND ? LIST_X + LIST_W - SR(40) : 276)
#define SET_LIST_Y (SET_RND ? SR(96) : 0)  // the top of the rows
#define SET_LIST_B (SET_RND ? SR(380) : SCR_H)
#define SET_BACK_Y SR(388)  // BACK: the bottom cap
#define SET_BACK_H SR(50)
#endif
#endif
#ifndef SET_LIST_Y
#define SET_LIST_Y 0  // the rows: from the top of the screen to its bottom
#define SET_LIST_B SCR_H
#endif

enum {
    R_VOLUME,
    R_BRIGHT,
    R_SOUND,
    R_UISND,
    R_USBSND,
    R_ROTATE,
    R_LANG,
    R_SLEEP,
    R_POWER,
    R_DEEP,  // DEEP IDLE: OFF / 10 S / 30 S / 60 S (power.h)
    R_MOVE,
    R_SKEY,
    R_WIFI,
    R_HOTSPOT,
    R_WEB,
    R_WEBPASS,
    R_FRIENDS,
    R_NOTIFY,
    R_STANDBY,  // STANDBY: the screen at a wake and in the idle menu (standby_page.h)
    R_TILT,
    R_ASCALE,
    R_TCAL,
    R_TOUCH,
    R_DEV,
    R_STORE,  // STORE SOURCE: AUTO, MANAGER or STATIC (tfn_sstore.h)
    R_PUB,    // TRUSTED PUBLISHERS: the keys that can sign an app (tfn_pub.h)
    R_POOL,   // POOL: the pool of this board, its members, LEAVE, JOIN (pool_page.h)
    R_TEMP,   // the temperature of the chip, an info line before ABOUT
    R_MEM,    // the free memory: internal RAM and PSRAM
    R_ABOUT,
    N_ROWS
};
#define MAX_SCROLL (set_nrows * ROW_H - (SET_LIST_B - SET_LIST_Y) + 4)

enum {
    CTRL_NONE,
    CTRL_BACK,
    CTRL_VOL_MINUS,
    CTRL_VOL_BAR,
    CTRL_VOL_PLUS,
    CTRL_BRI_MINUS,
    CTRL_BRI_BAR,
    CTRL_BRI_PLUS,
    CTRL_SOUND,
    CTRL_ROTATE,
    CTRL_SLEEP_PREV,
    CTRL_SLEEP_NEXT,
    CTRL_PWOFF_PREV,
    CTRL_PWOFF_NEXT,
    CTRL_DEEP_PREV,
    CTRL_DEEP_NEXT,
    CTRL_WIFI,
    CTRL_HS_PREV,
    CTRL_HS_NEXT,
    CTRL_HS_START,
    CTRL_HS_QR,
    CTRL_WEB,
    CTRL_TILT,
    CTRL_QR_START,
    CTRL_FRIENDS,
    CTRL_NOTIFY,
    CTRL_AS_MINUS,
    CTRL_AS_MODE,
    CTRL_AS_PLUS,
    CTRL_TCAL,
    CTRL_TOUCH,
    CTRL_DEV,
    CTRL_USBSND,
    CTRL_LANG,
    CTRL_MOVE,
    CTRL_WEBPASS,
    CTRL_SKEY,
    CTRL_UISND,
    CTRL_PUB,
    CTRL_POOL,
    CTRL_STORE,
    CTRL_STANDBY
};

// pages
enum {
    PAGE_MAIN,
    PAGE_WIFI,
    PAGE_KBD,
    PAGE_QR,
    PAGE_FRIENDS,
    PAGE_CALIB,
    PAGE_ASK,
    PAGE_NOTIFY,
    PAGE_SAVED,
    PAGE_PUB,
    PAGE_POOL,
    PAGE_STANDBY
};

// the WiFi page and its keyboard. THE THUMB: the rows are 43 px (3.49, 5.7 mm) and 58 px (the 320 x 240
// frame: CoreS3 7.4 mm, AMOLED 6.4 mm, the Watcher at 1:1 5.2 mm); BACK, SCAN and the arrows 40-48 px; FORGET is 40 px
// away from SCAN. An OPEN network and FORGET ask first (PAGE_ASK: NO at the left, YES at the right): a tap on an open
// network would join it at once, and wifi_connect() would save it over the home WiFi (a WiFi-only board then falls off).
#if SET_WIDE
#if BOARD_UI_TABLET
#define N_LIST 4  // the Tab5: 4 network rows of 60 px below a top line of 52 px
#define WTOP_H 52
#else
#define N_LIST 3
#define WTOP_H 40  // the top line: < BACK, the state, SCAN, FORGET
#endif
#define WBACK_W   80
#define WSCAN_X   420  // SCAN 420 .. 516, FORGET 556 .. 640
#define WSCAN_W   96
#define WFORGET_X 556
#if BOARD_UI_TABLET
#define LIST_Y 56
#define LIST_H 60
#else
#define LIST_Y 41
#define LIST_H 43
#endif
#define WLIST_W 592  // the network rows: x 0 .. 592, the arrows at the right
#define ARROW_X 596
#define ARROW_W 44
#define WCONN_X 490  // DISCONNECT / CONNECT (a saved network, no scan list): the bottom right
#if BOARD_UI_TABLET
#define WCONN_Y 300
#define WCONN_H 52
#else
#define WCONN_Y 124
#define WCONN_H 44
#endif
#define WCONN_W     146
#define WAUTO_X     4  // AUTO: ON / OFF of the saved network (with DISCONNECT / CONNECT): the bottom left
#define WAUTO_W     144
#define SV_NAME_W   370  // SAVED: the name 10 .. 380, AUTO: ON / OFF, FORGET (text size 2)
#define SV_AUTO_X   384
#define SV_AUTO_W   112
#define SV_FORGET_X 502
#define SV_FORGET_W 86
#define SV_TS       2
#else
#define N_LIST      3
#define WTOP_H      48  // (the Watcher at 1:1: 4.3 mm)
#define WBACK_W     76
#define WSCAN_X     84  // SCAN 84 .. 180, FORGET 220 .. 320
#define WSCAN_W     96
#define WFORGET_X   220
#define LIST_Y      62
#define LIST_H      58
#define WLIST_W     268
#define ARROW_X     272
#define ARROW_W     48
#define WCONN_X     164  // DISCONNECT / CONNECT (a saved network, no scan list): the bottom right
#define WCONN_Y     194
#define WCONN_W     152
#define WCONN_H     44
#define WAUTO_X     4  // AUTO: ON / OFF of the saved network: the bottom left
#define WAUTO_W     152
#define SV_NAME_W   134  // SAVED: the name 8 .. 142, AUTO: ON / OFF, FORGET (text size 1)
#define SV_AUTO_X   146
#define SV_AUTO_W   64
#define SV_FORGET_X 214
#define SV_FORGET_W 52
#define SV_TS       1
#endif
#define ARROW_H (N_LIST * LIST_H / 2)  // the up and the down arrow: each one half of the list

// the shared functions and variables of the parts
// app_settings.c
extern uint16_t set_c_text, set_c_dim, set_c_box, set_c_box_on, set_c_accent, set_c_card, set_c_card2, set_c_line;
int set_row_y(int i);
extern int set_active;
extern bool set_kbd_web;
extern int set_nrows;
extern int set_page;
extern uint8_t set_rows[N_ROWS];
extern float set_scroll;
// app_settings_wifi.c
void set_ask_answer(int a);
void set_ask_input(const input_event_t *ev);
void set_ask_render(void);
void set_back_strip(bool hot);
void set_qr_input(const input_event_t *ev);
void set_qr_render(int64_t now);
extern int set_ask_kind, set_ask_hot, set_ask_focus;
extern wifi_ap_t set_list[16];
extern bool set_list_on;
extern int set_nlist, set_list_top;
extern char set_pass[65];
extern char set_pick[33];
extern int set_qr_press;
extern int set_sv_top, set_sv_press;
extern int set_wfocus;
void set_saved_input(const input_event_t *ev);
void set_saved_render(void);
void set_wifi_input(const input_event_t *ev);
uint16_t set_wifi_line(char *buf, int len);
void set_wifi_pick(int i);
void set_wifi_render(int64_t now);
// app_settings_draw.c
void set_list_render(int64_t now);
extern float set_chip_c;
extern uint32_t set_free_int, set_free_ps, set_free_fs;
