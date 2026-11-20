// STANDBY: the state, the touches, the frame rate and the console (standby.h).
// When it shows: at each wake of the screen and after SB_IDLE_MS with no touch or key in the MENU (never over an app,
// the settings panel, the notices or a message). It goes: the end of each touch (a tap, a long touch, a swipe), a key,
// the PWR key, the panel, an app (a touch on a slot of GLANCE opens the app of the slot). On a dim screen the touch
// that wakes the screen also acts (main.c frame_saver); on a dark screen it only wakes it.
// The frame rate: STANDBY_FAST_MS while the sky moves (clouds, rain, snow) or a timer counts, else STANDBY_SLOW_MS;
// between two frames main.c draws nothing and sends nothing to the LCD (standby_skip).
#include "standby.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "audio.h"
#include "board.h"
#include "esp_log.h"
#include "notify.h"
#include "settings.h"
#include "standby_priv.h"
#include "sysui.h"
#include "tfn_util.h"

static const char *TAG = "standby";

#define SB_IDLE_MS      60000  // the menu with no touch this long: STANDBY
#define STANDBY_FAST_MS 100    // 10 frames a second: the sky drifts
#define STANDBY_SLOW_MS 1000   // 1 frame a second: NIGHT, a still sky
#define SWIPE_PX        40     // a swipe up this long opens the menu

static bool s_on, s_bl_low, s_preview;
static bool s_screen = true;  // the screen is on (not dim, not off): standby_tick
static int64_t s_next_at;     // ms: the next frame
static int64_t s_last_at;     // ms: the last frame (the step of the sky)
static int16_t s_x0, s_y0;
static int s_press = -1;         // the slot under the finger (-1 = none)
static bool s_down, s_had_down;  // (s_had_down: a finger came down after the show)
static bool s_eat;               // the IN_TAP / IN_LONG of the touch that closed STANDBY is not the menu's

// the slots of the frame (standby_draw.c writes them at each draw)
int sb_hit_n;
sb_hit_t sb_hit[SB_SLOTS];
int sb_period_ms = STANDBY_FAST_MS;

void standby_init(void)
{
    sb_data_init();
}

bool standby_on(void)
{
    return s_on && app_current() == &app_menu;
}

void standby_show(int64_t now)
{
    if (!sb_cfg.on || app_current() != &app_menu) {
        return;
    }
    if (!s_on) {
        ESP_LOGI(TAG, "shows");
    }
    s_on = true;
    s_next_at = 0;
    s_last_at = now;
    s_press = -1;
    s_down = s_had_down = false;  // (the rest of the touch that woke the screen does nothing)
    sb_hit_n = 0;
    menu_redraw_all();  // (the menu draws all of it again after STANDBY)
}

static void backlight_back(void)
{
    if (s_bl_low) {
        s_bl_low = false;
        if (s_screen) {
            board_backlight(g_settings.brightness);
        }
    }
}

void standby_hide(void)
{
    if (s_on) {
        ESP_LOGI(TAG, "ends");
    }
    s_on = false;
    s_press = -1;
    backlight_back();
    menu_redraw_all();
}

void standby_wake(int64_t now)
{
    if (app_current() == &app_menu) {
        standby_show(now);
    }
}

// NIGHT: a low backlight (the LCD boards: the black is not off); the screen saver dims it more
void sb_night_backlight(bool night)
{
    if (night && !s_bl_low && s_screen) {
        s_bl_low = true;
        const int b = g_settings.brightness / 3;
        board_backlight((uint8_t)(b < 10 ? 10 : b));
    } else if (!night) {
        backlight_back();
    }
}

// SETUP > STANDBY > SHOW IT NOW: STANDBY shows when the menu is the app (the next frames)
void sb_preview(void)
{
    s_preview = true;
}

void standby_tick(int64_t now, int screen, int64_t hand_at)
{
    s_screen = screen == 2;
    sb_data_tick(now, screen > 0);
    const bool menu = app_current() == &app_menu;
    if (!menu || !sb_cfg.on) {
        if (s_on) {
            standby_hide();
        }
        if (!menu) {
            sb_sky_free();  // (an app: the PSRAM of the sky goes back)
        }
        return;
    }
    if (s_preview) {
        s_preview = false;
        standby_show(now);
    }
    // (also when the screen dims at the same minute: SLEEP 1 MIN; then STANDBY shows dim)
    if (!s_on && screen > 0 && now - hand_at >= SB_IDLE_MS && !sysui_panel_on() && !notify_center_on() &&
        !sysui_active()) {
        standby_show(now);
    }
    if (s_on && sysui_panel_on()) {
        standby_hide();  // (the settings panel came down: the menu is under it)
    }
}

bool standby_skip(int64_t now)
{
    if (!standby_on() || sysui_panel_on() || notify_center_on() || notify_banner_on() || sysui_active()) {
        return false;
    }
    return now < s_next_at;
}

static int slot_at(int x, int y)
{
    for (int i = 0; i < sb_hit_n; i++) {
        const sb_hit_t *h = &sb_hit[i];
        if (x >= h->x - 4 && x < h->x + h->w + 4 && y >= h->y - 4 && y < h->y + h->h + 4) {
            return i;
        }
    }
    return -1;
}

// a tap on a slot: its app (or the notices), else the menu
static void open_slot(int i)
{
    const char *name = NULL;
    sb_now_t n;
    switch (sb_hit[i].kind) {
    case SL_WEATHER:
        name = "WEATHER";
        break;
    case SL_HEBREW:
        name = "HEBREW CALENDAR";
        break;
    case SL_NOTICES:
        standby_hide();
        notify_center_open();
        return;
    case SL_NEXT:
        sb_now(&n);
        name = n.nx_kind == NX_TIMER || n.nx_kind == NX_ALARM        ? "CLOCK"
               : n.nx_kind == NX_CANDLES || n.nx_kind == NX_HAVDALAH ? "HEBREW CALENDAR"
                                                                     : "WEATHER";
        break;
    case SL_BATTERY:
        name = "SETUP";
        break;
    case SL_STOCKS:
        name = "STOCKS";
        break;
    default:
        break;
    }
    standby_hide();
    const app_t *a = name ? app_find(name) : NULL;
    if (a) {
        sfx_ui(SFX_CLICK);
        app_open(a);
    }
}

// THE END OF A TOUCH CLOSES STANDBY (each lift of a finger that came down on it), not only an IN_TAP: a tap must be
// shorter than LONG_PRESS_MS and move less than 1.8 mm, so a slow or a sliding touch did nothing, and the user had to
// touch many times. The IN_TAP or the IN_LONG of the same touch comes after its IN_UP: STANDBY eats it, so the menu
// under it does not get a tap with no IN_DOWN.
bool standby_input(const input_event_t *ev)
{
    if (!standby_on()) {
        if (s_eat && (ev->kind == IN_TAP || ev->kind == IN_LONG)) {
            s_eat = false;
            return true;  // (the rest of the touch that closed STANDBY)
        }
        if (ev->kind == IN_DOWN) {
            s_eat = false;
        }
        return false;
    }
    switch (ev->kind) {
    case IN_DOWN:
        s_down = s_had_down = true;
        s_x0 = ev->x;
        s_y0 = ev->y;
        s_press = slot_at(ev->x, ev->y);
        s_next_at = 0;
        break;
    case IN_MOVE:
        if (abs(ev->x - s_x0) > 14 || abs(ev->y - s_y0) > 14) {
            s_press = -1;
        }
        break;
    case IN_UP:
        s_down = false;
        s_next_at = 0;
        if (!s_had_down) {
            break;  // (the touch that woke the screen)
        }
        s_had_down = false;
        s_eat = true;
        if (s_press >= 0 && s_y0 - ev->y < SWIPE_PX && slot_at(ev->x, ev->y) == s_press) {
            open_slot(s_press);  // a touch on a slot: its app
        } else {
            sfx_ui(SFX_CLICK);
            standby_hide();  // a tap, a long touch or a swipe: the menu where it was
        }
        break;
    default:
        break;  // (IN_TAP, IN_LONG: the IN_UP acts)
    }
    return true;
}

int sb_pressed(void)
{
    return s_down ? s_press : -1;
}

void standby_draw(int64_t now)
{
    float dt = (float)(now - s_last_at) / 1000.0f;
    s_last_at = now;
    sb_draw_frame(now, dt);
    s_next_at = now + sb_period_ms;
}

void standby_console(char *args)
{
    char *a = strtok(args, " \t\r\n"), *v = strtok(NULL, " \t\r\n");
    const int64_t now = tfn_now_ms();
    if (a && !strcmp(a, "show")) {
        if (app_current() != &app_menu) {
            app_open(&app_menu);
        }
        app_screen_wake();  // (the screen on)
        sb_preview();       // (STANDBY shows when the menu is the app)
    } else if (a && !strcmp(a, "hide")) {
        standby_hide();
    } else if (a && !strcmp(a, "fetch")) {
        sb_data_fetch();
    } else if (a && v && !strcmp(a, "face")) {
        sb_cfg.face = (uint8_t)(atoi(v) % SB_FACES);
        sb_cfg_save();
    } else if (a && v && !strcmp(a, "on")) {
        sb_cfg.on = atoi(v) != 0;
        sb_cfg_save();
    } else if (a && v && !strcmp(a, "night")) {
        sb_cfg.autonight = atoi(v) != 0;
        sb_cfg_save();
    }
    char b[256];
    sb_data_log(b, sizeof b);
    printf("STANDBY %s (%s), face %s, auto night %d (%02d-%02d), slots %d %d %d %d; %s\n",
           standby_on() ? "SHOWS" : "OFF", sb_cfg.on ? "on" : "turned off", sb_face_name(sb_cfg.face), sb_cfg.autonight,
           sb_cfg.nfrom, sb_cfg.nto, sb_cfg.slot[0], sb_cfg.slot[1], sb_cfg.slot[2], sb_cfg.slot[3], b);
}
