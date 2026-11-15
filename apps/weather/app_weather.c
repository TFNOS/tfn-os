// WEATHER: the weather for the 3.49 (a store app, portrait 172 x 640) and the other frames, from Open-Meteo.
// The task: the app task of TFN OS calls the hooks of app_weather; the fetch task of wx_data.inc gets the data.
// The memory: the file statics; the screen picture (PSRAM, wx_draw.inc) and the answer buffer of the API (PSRAM,
// wx_data.inc) go at leave; the data of the places stays in a cache file.
//
// Open-Meteo (free, no key): the weather now, 48 hours, 10 days, the rain of the next 4 hours in quarters of an hour,
// the air. A live sky at the top (the real height of the sun at the place, the clouds, rain, snow, fog, lightning, the
// stars and the moon in its phase); under it the cards: the rain soon, the next hours, ten days, eight tiles (wind,
// UV, the sun, the moon, humidity, pressure, visibility, the air). MY PLACE comes from the internet address; more
// places from a list or a search. A swipe changes the place, a pull from the top updates. The cache file lets the app
// open at once, also with no WiFi.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfn/wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
static inline void vTaskDelay(int t)
{
    (void)t;
}
#endif
#ifdef _WIN32
#define APP_MKDIR(p) mkdir(p)  // (the PC test on Windows)
#else
#define APP_MKDIR(p) mkdir(p, 0775)
#endif
#include "tfnet/tfnet.h"
#include "tfdraw/tfdraw.h"
#include "kscroll/kscroll.h"

// the page (upright): 172 x 640 on the 3.49, the frame of the other boards (240 x 320); the desk (landscape): 640 x 172,
// or 320 x 240 (its narrow form). frame_sizes() sets them from the frame after each turn.
static int s_W = 172, s_H = 640, s_lw = 640, s_lh = 172, s_pw = 212, s_dx0 = 470;
#define W s_W
#define H s_H
static int s_he;     // the language: 0 English, 1 Hebrew
static bool s_land;  // the board on its side: the desk view (640 x 172, or 320 x 240)
static bool s_rnd;   // the round frame (APP_ROUND_HI): always the page, its parts inside the circle
static void frame_sizes(void)
{
    s_rnd = gfx_round();
    if (s_rnd) {
        s_land = false;
    }
    if (!s_land) {
        s_W = gfx_w;
        s_H = gfx_h;
        return;
    }
    if (gfx_w < 400) {
        s_lw = gfx_w;
        s_lh = gfx_h;
        s_pw = 128;
        s_dx0 = gfx_w;
    }  // (the narrow desk: the days under the chart)
#if BOARD_UI_TABLET
    else if (gfx_h > 200) {
        s_lw = gfx_w;
        s_lh = gfx_h;
        s_pw = 240;
        s_dx0 = gfx_w;
    }  // (the Tab5: the big desk, 640 x 360)
#endif
    else {
        s_lw = 640;
        s_lh = 172;
        s_pw = 212;
        s_dx0 = 470;
    }
}
#include "wx_text.inc"
#include "wx_fonts.inc"
#include "wx_data.inc"
#include "wx_sky.inc"
#include "wx_icons.inc"

enum {
    SC_PLACE,
    SC_PLACES,
    SC_ADD,
    SC_KBD
};
static int s_sc, s_cur;
static int s_unit, s_wunit, s_motion = 1;  // the units (0 C, 1 F), the wind unit, the live sky (1 = on)
// the page of a place
static float s_scroll,
        s_xoff;  // the scroll to draw (ks_pos(&s_pks), set in each update), the slide of a new page from the side
static kscroll_t s_pks;  // the momentum scroll of the page (libs/kscroll)
static bool s_pksd;      // this touch goes to the scroll of the page
static bool s_drag, s_dmoved, s_hdrag;
static float s_drag_x, s_drag_y;
static bool s_pull;  // a pull from the top that will update
// the lists
static float s_lscroll;  // the scroll of the list to draw (ks_pos(&s_lks), set in each update)
static kscroll_t s_lks;  // the momentum scroll of the lists
static bool s_lksd;      // this touch goes to the scroll of the list
static void list_to_top(void)
{
    ks_jump(&s_lks, 0);
    s_lscroll = 0;
}
static int s_lhot = -1, s_add_tab;  // the add screen: 0 Israel, 1 the world, 2 the search results
static int64_t s_hold_t;
static int s_hold_row = -1;
static char s_kbuf[48];
static geo_t s_res[NRES];
static int s_nres;
static bool s_searching;
// the work
static int s_wifi = 1;
static int64_t s_wifi_t, s_last_ms, s_touch_ms, s_toast_t;
static char s_toast[64];
static int s_rq_mine = -1;  // the gen of my last request (-1: none waits)
static int s_rq_what = RQ_NONE;
static float s_rq_lat, s_rq_lon;
static int64_t s_fail_t[NPLACE_MAX];
static bool s_force[NPLACE_MAX];  // (an update now: the panel, a pull from the top)
static int64_t s_ip_tried;
static bool s_ip_need;
static int s_data_ver;
static bool s_eat_tap;
static orient_t s_rot_want;
static int64_t s_rot_t;

static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}
static void toast(const char *t)
{
    snprintf(s_toast, sizeof s_toast, "%s", t);
    s_toast_t = ms_now();
}

// ---------------------------------------------------------------- the units
static int t_disp(float c)
{
    float v = s_unit ? c * 9 / 5 + 32 : c;
    return (int)floorf(v + 0.5f);
}
static void t_str(char *b, int n, float c)
{
    if (isnan(c)) {
        snprintf(b, n, "--");
    } else {
        snprintf(b, n, "%d\xb0", t_disp(c));
    }
}
static void t_str_u(char *b, int n, float c)
{
    if (isnan(c)) {
        snprintf(b, n, "--");
    } else {
        snprintf(b, n, "%d\xc2\xb0", t_disp(c));
    }
}
static float w_disp(float kmh)
{
    static const float K[4] = {1, 1 / 3.6f, 1 / 1.609f, 1 / 1.852f};
    return kmh * K[s_wunit & 3];
}

// ---------------------------------------------------------------- the places
static int place_find(float lat, float lon)
{
    for (int i = 0; i < s_npl; i++) {
        if (fabsf(s_pl[i].lat - lat) < 0.02f && fabsf(s_pl[i].lon - lon) < 0.02f) {
            return i;
        }
    }
    return -1;
}
static int place_add(const char *name, const char *sub, float lat, float lon, bool here)
{
    int i = place_find(lat, lon);
    if (i >= 0) {
        return i;
    }
    if (s_npl >= NPLACE_MAX) {
        toast(TX(T_FULL));
        return -1;
    }
    i = s_npl++;
    if (here) {  // MY PLACE comes first
        memmove(&s_pl[1], &s_pl[0], (size_t)(s_npl - 1) * sizeof s_pl[0]);
        memmove(&s_wx[1], &s_wx[0], (size_t)(s_npl - 1) * sizeof s_wx[0]);
        memmove(&s_fail_t[1], &s_fail_t[0], (size_t)(s_npl - 1) * sizeof s_fail_t[0]);
        memmove(&s_force[1], &s_force[0], (size_t)(s_npl - 1) * sizeof s_force[0]);
        i = 0;
        if (s_cur < s_npl - 1) {
            s_cur++;
        }
    }
    place_t *p = &s_pl[i];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);
    snprintf(p->sub, sizeof p->sub, "%s", sub);
    p->lat = lat;
    p->lon = lon;
    p->here = here;
    memset(&s_wx[i], 0, sizeof s_wx[i]);
    s_fail_t[i] = 0;
    s_force[i] = false;
    places_save();
    s_data_ver++;
    return i;
}
static void place_remove(int i)
{
    if (i < 0 || i >= s_npl) {
        return;
    }
    memmove(&s_pl[i], &s_pl[i + 1], (size_t)(s_npl - i - 1) * sizeof s_pl[0]);
    memmove(&s_wx[i], &s_wx[i + 1], (size_t)(s_npl - i - 1) * sizeof s_wx[0]);
    memmove(&s_fail_t[i], &s_fail_t[i + 1], (size_t)(s_npl - i - 1) * sizeof s_fail_t[0]);
    memmove(&s_force[i], &s_force[i + 1], (size_t)(s_npl - i - 1) * sizeof s_force[0]);
    s_npl--;
    if (s_cur >= s_npl) {
        s_cur = s_npl - 1;
    }
    if (s_cur < 0) {
        s_cur = 0;
    }
    places_save();
    cache_save();
    s_data_ver++;
}
static void go_place(int i, int dir)
{
    if (i < 0 || i >= s_npl) {
        return;
    }
    if (i != s_cur) {
        s_xoff = dir > 0 ? W : dir < 0 ? -W : 0;
        s_scroll = 0;
        ks_jump(&s_pks, 0);
    }
    s_cur = i;
    store_set("weather_page", (uint32_t)i);
    s_data_ver++;
}

// ---------------------------------------------------------------- the rain soon (the quarters of an hour)
// 0 = no rain in the next 4 h; 1 = rain starts in *min; 2 = it rains and stops in *min; 3 = it rains for hours
static int rain_soon(const wx_t *w, int64_t now, int *min)
{
    *min = 0;
    if (!w->fetched || w->nm <= 0) {
        return 0;
    }
    int i0 = 0;
    while (i0 < w->nm - 1 && w->m0 + (int64_t)(i0 + 1) * 900 <= now) {
        i0++;
    }
    bool wet_now = (!isnan(w->mpr[i0]) && w->mpr[i0] >= 0.1f) || w->prec >= 0.1f;
    for (int i = i0; i < w->nm; i++) {
        bool wet = !isnan(w->mpr[i]) && w->mpr[i] >= 0.1f;
        if (wet != wet_now) {
            int m = (int)((w->m0 + (int64_t)i * 900 - now) / 60);
            *min = m < 1 ? 1 : m;
            return wet_now ? 2 : 1;
        }
    }
    return wet_now ? 3 : 0;
}

// ---------------------------------------------------------------- the panel
enum {
    PI_UNIT,
    PI_WIND,
    PI_LANG,
    PI_MOTION,
    PI_UPDATE
};
static const char *const UNIT_NAMES[] = {"CELSIUS", "צלזיוס", "FAHRENHEIT", "פרנהייט"};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_UNIT, 2, 0, {"TEMPERATURE", "טמפרטורה"}, UNIT_NAMES, "weather_unit"},
        {PI_PICK, PI_WIND, 4, 0, {"WIND", "רוח"}, WUNIT_VALS, "weather_wind"},
        {PI_PICK, PI_MOTION, 2, 0, {"LIVE SKY", "שמיים חיים"}, MOTION_VALS, "weather_motion"},
        {PI_PICK, PI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "weather_lang"},
        {PI_ACT, PI_UPDATE, 0, 0, {"UPDATE NOW", "עדכון עכשיו"}, NULL, NULL},
};
static void bands_reset(void);
static void item_set(int id, int v)
{
    if (id == PI_UNIT) {
        s_unit = v;
    }
    if (id == PI_WIND) {
        s_wunit = v;
    }
    if (id == PI_MOTION) {
        s_motion = v == 0;
    }
    if (id == PI_LANG) {
        s_he = v == 1;
    }
    if (id == PI_UPDATE && s_cur < s_npl) {
        s_force[s_cur] = true;
        s_fail_t[s_cur] = 0;
    }
    s_data_ver++;
}
static void item_resume(void)
{
    bands_reset();
}
static const pitems_t PANEL = {ITEMS, 5, NULL, NULL, item_set, NULL, item_resume};

// ---------------------------------------------------------------- the requests (the fetch task)
static void rq_take(void)
{
    if (s_rq_mine < 0 || s_rq.done_gen != s_rq_mine) {
        return;
    }
    int what = s_rq_what;
    s_rq_mine = -1;
    s_rq_what = RQ_NONE;
    bool ok = s_rq.status == RS_OK;
    if (what == RQ_FORECAST) {
        int i = place_find(s_rq_lat, s_rq_lon);
        if (i < 0) {
            return;
        }
        if (!ok) {
            s_fail_t[i] = ms_now();
            s_force[i] = false;
            if (i == s_cur && s_sc == SC_PLACE) {
                toast(TX(T_NOUPD));
            }
            s_data_ver++;
            return;
        }
        s_wx[i] = s_rq.wx;
        s_wx[i].lat = s_pl[i].lat;
        s_wx[i].lon = s_pl[i].lon;
        int64_t now = unix_now();
        if (!now || now < s_rq.wx.now) {  // (no clock from the network yet: the time of the API)
            s_api_unix = s_rq.wx.now;
            s_api_ms = ms_now();
            now = s_rq.wx.now;
        }
        s_wx[i].fetched = now;
        s_fail_t[i] = 0;
        s_force[i] = false;
        cache_save();
        s_data_ver++;
    } else if (what == RQ_IP) {
        s_ip_need = false;
        if (ok) {
            int h = -1;
            for (int i = 0; i < s_npl; i++) {
                if (s_pl[i].here) {
                    h = i;
                }
            }
            if (h >= 0) {
                // MY PLACE moved (more than about 3 km): the new place and its name
                if (fabsf(s_pl[h].lat - s_rq.ip.lat) > 0.03f || fabsf(s_pl[h].lon - s_rq.ip.lon) > 0.03f) {
                    s_pl[h] = s_rq.ip;
                    memset(&s_wx[h], 0, sizeof s_wx[h]);
                    places_save();
                    s_data_ver++;
                }
            } else {
                int i = place_add(s_rq.ip.name, s_rq.ip.sub, s_rq.ip.lat, s_rq.ip.lon, true);
                if (i >= 0) {
                    go_place(i, 0);
                }
            }
        } else if (!s_npl) {
            // no place and no answer: Tel Aviv, until the next try
            int i = place_add(s_he ? PRESETS[0].he : PRESETS[0].en, s_he ? T_ISRAEL[1] : T_ISRAEL[0], PRESETS[0].lat,
                              PRESETS[0].lon, false);
            if (i >= 0) {
                go_place(i, 0);
            }
        }
    } else if (what == RQ_SEARCH) {
        s_searching = false;
        s_nres = ok ? s_rq.ngeo : 0;
        memcpy(s_res, s_rq.geo, sizeof s_res);
        s_add_tab = 2;
        list_to_top();
        if (!ok) {
            toast(TX(T_NOUPD));
        } else if (!s_nres) {
            toast(TX(T_NORES));
        }
        s_data_ver++;
    }
}
static void rq_next(void)
{
    if (s_rq_mine >= 0 || rq_busy() || !s_wifi || !s_task_alive) {
        return;
    }
    int64_t now = unix_now(), ms = ms_now();
    if (s_ip_need && (!s_ip_tried || ms - s_ip_tried > 60000)) {
        s_ip_tried = ms;
        s_rq.he = s_he;
        if (rq_start(RQ_IP)) {
            s_rq_mine = s_rq.gen;
            s_rq_what = RQ_IP;
        }
        return;
    }
    // the place that shows (older than 10 min), then the others (older than 30 min); a failed one waits 60 s
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < s_npl; k++) {
            int i = pass == 0 ? s_cur : k;
            if (pass == 0 && k > 0) {
                break;
            }
            if (i < 0 || i >= s_npl) {
                continue;
            }
            const wx_t *w = &s_wx[i];
            int64_t age = now && w->fetched ? now - w->fetched : 1 << 30;
            if (age < (pass == 0 ? 600 : 1800) && !s_force[i]) {
                continue;
            }
            if (s_fail_t[i] && ms - s_fail_t[i] < 60000) {
                continue;
            }
            s_rq.lat = s_pl[i].lat;
            s_rq.lon = s_pl[i].lon;
            if (rq_start(RQ_FORECAST)) {
                s_rq_mine = s_rq.gen;
                s_rq_what = RQ_FORECAST;
                s_rq_lat = s_pl[i].lat;
                s_rq_lon = s_pl[i].lon;
            }
            return;
        }
    }
}
static bool updating(int i)
{
    return s_rq_mine >= 0 && s_rq_what == RQ_FORECAST && i >= 0 && i < s_npl && place_find(s_rq_lat, s_rq_lon) == i;
}

// ---------------------------------------------------------------- enter / leave
static void enter(int64_t now)
{
    (void)now;
    tfd_screen();
    s_he = store_get("weather_lang", 0) == 1;
    s_unit = (int)store_get("weather_unit", 0) & 1;
    s_wunit = (int)store_get("weather_wind", 0) & 3;
    s_motion = store_get("weather_motion", 0) == 0;
    places_load();
    cache_load();
    s_cur = (int)store_get("weather_page", 0);
    s_land = orient_is_landscape(gfx_orient());
    frame_sizes();
    s_rot_want = gfx_orient();
    if (s_cur >= s_npl) {
        s_cur = 0;
    }
    s_sc = SC_PLACE;
    s_scroll = s_xoff = 0;
    ks_init(&s_pks, 300, 0);  // (the view of the rubber band: a pull of 64 px at the top updates the page)
    ks_init(&s_lks, 520, 0);
    s_lscroll = 0;
    s_pksd = s_lksd = false;
    s_drag = s_pull = false;
    s_rq_mine = -1;
    s_toast[0] = 0;
    memset(s_fail_t, 0, sizeof s_fail_t);
    memset(s_force, 0, sizeof s_force);
    s_ip_tried = 0;
    // MY PLACE: found again each open (the board can travel); with no place at all it is the first thing
    bool here = false;
    for (int i = 0; i < s_npl; i++) {
        if (s_pl[i].here) {
            here = true;
        }
    }
    s_ip_need = here || !s_npl;
    s_touch_ms = s_last_ms = ms_now();
    s_wifi = wifi_state() == WIFI_CONNECTED;
    sky_init();
    if (s_npl) {
        sky_target(&s_wx[s_cur], &s_pl[s_cur], unix_now());
        s_sky = s_sky_to;
    }
    if (!task_start()) {
        toast(TX(T_NOUPD));
    }
    sysui_panel_items(&PANEL);
    s_data_ver++;
}

static void bands_free(void);
static void leave(void)
{
    task_stop();
    bands_free();
    if (gfx_orient() != ORIENT_PORTRAIT && gfx_orient() != ORIENT_PORTRAIT_FLIP) {
        gfx_set_orient(ORIENT_PORTRAIT);
    }
    s_land = false;
    store_set("weather_page", (uint32_t)s_cur);
}

// ---------------------------------------------------------------- the layout of a page (content y; the screen y is
// the content y minus the scroll)
#define HERO_H (s_rnd ? H * 360 / 466 : 300)  // (the round frame: the hero fills the circle)
#define CARD_X (s_rnd ? W * 48 / 466 : 8)
#define CARD_W (W - 2 * CARD_X)
#define HR_H   30                                  // a row of the next hours
#define EV_H   18                                  // a row of a sunrise or a sunset between the hours
#define DY_H   44                                  // a row of a day
#define TILE   (W >= 220 ? (CARD_W - 8) / 2 : 76)  // (a wider page: two wider tiles)
#define HDR_H  (s_rnd ? H * 96 / 466 : 50)         // the small head when the page scrolled down
typedef struct {
    int rain_y, rain_h, hours_y, hours_h, days_y, days_h, tiles_y, tiles_h, foot_y, total;
} lay_t;
static int hours_rows(const wx_t *w, int64_t now, int *ev);
static lay_t page_layout(const wx_t *w, int64_t now)
{
    lay_t L;
    int y = HERO_H;
    int m;
    int rs = rain_soon(w, now, &m);
    L.rain_y = y;
    L.rain_h = rs ? 124 : 0;
    y += L.rain_h + (rs ? 10 : 0);
    int ev = 0;
    int n = hours_rows(w, now, &ev);
    L.hours_y = y;
    L.hours_h = 34 + n * HR_H + ev * EV_H + 6;
    y += L.hours_h + 10;
    L.days_y = y;
    L.days_h = 34 + (w->nd > 0 ? w->nd : 0) * DY_H + 6;
    y += L.days_h + 10;
    L.tiles_y = y;
    L.tiles_h = 4 * (TILE + 8);
    y += L.tiles_h + 4;
    L.foot_y = y;
    L.total = y + 70;
    return L;
}
static float page_max_scroll(void)
{
    if (s_cur >= s_npl || !s_wx[s_cur].fetched) {
        return 0;
    }
    lay_t L = page_layout(&s_wx[s_cur], unix_now());
    float m = (float)(L.total - H);
    return m > 0 ? m : 0;
}

// ---------------------------------------------------------------- the lists: the rows
#define LIST_Y (s_rnd ? H * 104 / 466 : 56)
#define LI     (s_rnd ? W * 50 / 466 : 0)  // the inset of the rows of a list (the round frame: inside the circle)
#define PL_H   72                          // a row of the places
#define AD_H   54                          // a row of the add screen
static int list_rows(void)
{
    if (s_sc == SC_PLACES) {
        return s_npl;
    }
    if (s_add_tab == 2) {
        return s_nres;
    }
    int n = 1;  // (the first row: the search)
    for (int i = 0; i < NPRESET; i++) {
        if (PRESETS[i].il == (s_add_tab == 0)) {
            n++;
        }
    }
    return n;
}
static int list_row_h(void)
{
    return s_sc == SC_PLACES ? PL_H : AD_H;
}
static int list_top(void)
{
    return s_sc == SC_ADD ? LIST_Y + 50 : LIST_Y;
}
static int list_bottom(void)
{
    if (s_rnd) {
        return s_sc == SC_PLACES ? H - H * 112 / 466 : H - H * 40 / 466;
    }
    return s_sc == SC_PLACES ? H - 70 : H;
}
static float list_max(void)
{
    float m = (float)(list_rows() * list_row_h() + 40 - (list_bottom() - list_top()));
    return m > 0 ? m : 0;
}
// the preset of a row of the add screen (row 0 is the search)
static int add_preset(int r)
{
    int n = 1;
    for (int i = 0; i < NPRESET; i++) {
        if (PRESETS[i].il != (s_add_tab == 0)) {
            continue;
        }
        if (n++ == r) {
            return i;
        }
    }
    return -1;
}

// the search: the keyboard of the system (it needs the landscape frame)
static void search_open(void)
{
    s_sc = SC_KBD;
    s_kbuf[0] = 0;
    gfx_set_orient(imu_upright(true, ORIENT_LAND_A, 0.5f));
    kbd_open(TX(T_TYPE), s_kbuf, 40, false);
    kbd_set_ok_question(NULL);
}
static void search_done(bool ok)
{
    gfx_set_orient(imu_upright(false, ORIENT_PORTRAIT, 0.5f));
    tfd_screen();
    s_sc = SC_ADD;
    if (!ok || !s_kbuf[0]) {
        return;
    }
    if (rq_busy() || s_rq_mine >= 0) {
        // (a forecast is in work: the search waits for it)
        s_rq_cancel = true;
    }
    snprintf(s_rq.query, sizeof s_rq.query, "%s", s_kbuf);
    s_rq.he = s_he;
    s_searching = true;
    s_nres = 0;
    s_add_tab = 2;
}

// ---------------------------------------------------------------- input
static bool back_hit(int x, int y)
{
    if (s_rnd) {  // (the back key at the top of the circle)
        return y < LIST_Y - 30 && x > W / 2 - 70 && x < W / 2 + 70;
    }
    return y < LIST_Y - 4 && (s_he ? x > W - 52 : x < 52);
}
static void list_tap(int x, int y)
{
    if (back_hit(x, y)) {
        sfx_play(SFX_BACK);
        if (s_sc == SC_ADD) {
            s_sc = s_npl ? SC_PLACES : SC_PLACE;
        } else {
            s_sc = SC_PLACE;
        }
        list_to_top();
        return;
    }
    const bool add_y = s_rnd ? y >= list_bottom() && y < H - H * 40 / 466 : y >= H - 62 && y < H - 6;
    if (s_sc == SC_PLACES && add_y) {  // + ADD A PLACE
        sfx_play(SFX_CLICK);
        s_sc = SC_ADD;
        s_add_tab = 0;
        list_to_top();
        return;
    }
    if (s_sc == SC_ADD && y >= LIST_Y + 4 && y < LIST_Y + 44) {  // the tabs
        int tab = (x < W / 2) != (s_he != 0) ? 0 : 1;
        if (tab != s_add_tab) {
            s_add_tab = tab;
            list_to_top();
            sfx_play(SFX_CLICK);
        }
        return;
    }
    int top = list_top();
    if (y < top || y >= list_bottom()) {
        return;
    }
    int r = (int)((y - top + s_lscroll) / list_row_h());
    if (r < 0 || r >= list_rows()) {
        return;
    }
    sfx_play(SFX_CLICK);
    if (s_sc == SC_PLACES) {
        go_place(r, 0);
        s_sc = SC_PLACE;
        return;
    }
    if (s_add_tab == 2) {
        int i = place_add(s_res[r].name, s_res[r].sub, s_res[r].lat, s_res[r].lon, false);
        if (i >= 0) {
            go_place(i, 0);
            s_sc = SC_PLACE;
        }
        return;
    }
    if (r == 0) {
        search_open();
        return;
    }
    int p = add_preset(r);
    if (p < 0) {
        return;
    }
    int i = place_add(s_he ? PRESETS[p].he : PRESETS[p].en,
                      s_he ? (PRESETS[p].il ? T_ISRAEL[1] : "") : (PRESETS[p].il ? T_ISRAEL[0] : ""), PRESETS[p].lat,
                      PRESETS[p].lon, false);
    if (i >= 0) {
        go_place(i, 0);
        s_sc = SC_PLACE;
    }
}

static void input_list(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN: {
        s_drag = true;
        s_dmoved = false;
        s_drag_y = (float)ev->y;
        // a touch on the list goes to the scroll (a touch that stops a glide lights no row and gives no tap)
        s_lksd = ev->y >= list_top() && ev->y < list_bottom();
        bool caught = s_lksd && ks_down(&s_lks, (float)ev->y, ev->t_ms);
        s_lhot = s_lksd && !caught ? (int)((ev->y - list_top() + s_lscroll) / list_row_h()) : -1;
        s_hold_row = s_sc == SC_PLACES && s_lhot >= 0 && s_lhot < s_npl ? s_lhot : -1;
        s_hold_t = ev->t_ms;
        break;
    }
    case IN_MOVE:
        if (s_drag) {
            bool start = false;
            if (s_lksd) {
                start = ks_move(&s_lks, (float)ev->y, ev->t_ms);
            } else if (fabsf(s_drag_y - (float)ev->y) > 8) {
                start = true;  // (a touch outside the list: no tap after a move)
            }
            if (start) {
                s_dmoved = true;
                s_lhot = -1;
                s_hold_row = -1;
                input_tap_cancel();
            }
            if (s_dmoved && s_lksd) {
                s_lscroll = ks_pos(&s_lks);
            }
        }
        break;
    case IN_UP:
        if (s_lksd) {
            ks_up(&s_lks, ev->t_ms);
        }
        if (s_drag && !s_dmoved && s_hold_row != -2 && (!s_lksd || ks_tap_ok(&s_lks))) {
            list_tap(ev->x, ev->y);
            s_eat_tap = true;
        }
        s_drag = false;
        s_lhot = -1;
        s_hold_row = -1;
        break;
    default:
        break;
    }
}

static bool list_btn_hit(int x, int y)
{
    if (s_land) {
        return x < 48 && y < 48;
    }
    if (s_rnd) {  // (the key of the places in the top of the circle, at the start side)
        return y < H * 80 / 466 && (s_he ? x > W / 2 && x < W / 2 + 100 : x < W / 2 && x > W / 2 - 100);
    }
    return y < 54 && (s_he ? x > W - 50 : x < 50);
}

static void input_page(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        s_drag = true;
        s_dmoved = false;
        s_hdrag = false;
        s_drag_x = (float)ev->x;
        s_drag_y = (float)ev->y;
        s_pull = false;
        // a touch on the page goes to the scroll (a touch that stops a glide is not a tap)
        s_pksd = !s_land;
        if (s_pksd) {
            ks_down(&s_pks, (float)ev->y, ev->t_ms);
        }
        break;
    case IN_MOVE:
        if (!s_drag) {
            break;
        }
        {
            float dx = (float)ev->x - s_drag_x, dy = s_drag_y - (float)ev->y;
            if (!s_dmoved && (fabsf(dx) > 10 || fabsf(dy) > 10)) {
                s_dmoved = true;
                s_hdrag = fabsf(dx) > fabsf(dy) * 1.3f && s_npl > 1;
                input_tap_cancel();
            }
            if (!s_dmoved) {
                break;
            }
            if (s_hdrag) {
                s_xoff = dx;
                break;
            }
            if (s_land) {
                break;  // (the desk view does not scroll)
            }
            ks_move(&s_pks, (float)ev->y, ev->t_ms);
            float p = ks_pos(&s_pks);  // (a rubber band at the top: the pull to update)
            s_scroll = p < -110 ? -110 : p;
            s_pull = s_scroll < -64;
        }
        break;
    case IN_UP:
        if (s_pksd) {
            ks_up(&s_pks, ev->t_ms);
        }
        if (s_drag && s_hdrag) {
            // a swipe: the next place (to the left) or the one before; in Hebrew the order turns
            int dir = s_xoff < -50 ? 1 : s_xoff > 50 ? -1 : 0;
            if (s_he) {
                dir = -dir;
            }
            int ni = s_cur + dir;
            if (dir && ni >= 0 && ni < s_npl) {
                go_place(ni, s_xoff < 0 ? 1 : -1);
                sfx_tone(700, 900, 30, W_SINE, 20);
            }
            // (else the page glides back to its place in update)
        } else if (s_drag && !s_dmoved) {
            if (list_btn_hit(ev->x, ev->y) && (!s_pksd || ks_tap_ok(&s_pks))) {
                sfx_play(SFX_CLICK);
                s_sc = SC_PLACES;
                list_to_top();
                s_eat_tap = true;
            }
        } else if (s_drag && s_pull) {
            if (s_cur < s_npl) {
                s_force[s_cur] = true;
                s_fail_t[s_cur] = 0;
            }
            sfx_tone(500, 800, 50, W_SINE, 20);
        }
        s_drag = false;
        s_pull = false;
        break;
    default:
        break;
    }
}

static void input(const input_event_t *ev)
{
    s_touch_ms = ms_now();
    if (ev->kind == IN_DOWN) {
        s_eat_tap = false;
    }
    if (ev->kind == IN_TAP && s_eat_tap) {
        s_eat_tap = false;
        return;
    }
    if (s_sc == SC_KBD) {
        int r = kbd_input(ev);
        if (r == KBD_OK) {
            search_done(true);
        } else if (r == KBD_CANCEL) {
            search_done(false);
        }
        return;
    }
    if (s_sc == SC_PLACE) {
        input_page(ev);
    } else {
        input_list(ev);
    }
}

// ---------------------------------------------------------------- the turn of the board
static void bands_reset(void);
static void orient_apply(orient_t o)
{
    if (o == gfx_orient() && orient_is_landscape(o) == s_land) {
        return;
    }
    gfx_set_orient(o);
    tfd_screen();
    s_land = orient_is_landscape(o);
    frame_sizes();
    s_drag = s_hdrag = false;
    s_xoff = 0;
    bands_reset();
}
static void follow_board(int64_t t)
{
    if (s_sc == SC_KBD) {
        return;
    }
    if (s_sc != SC_PLACE) {
        if (orient_is_landscape(gfx_orient())) {
            orient_apply(imu_upright(false, ORIENT_PORTRAIT, 0.3f));
        }
        return;
    }
    if (!g_settings.auto_rotate || !imu_ok()) {
        return;
    }
    orient_t o = imu_upright_any(gfx_orient(), 0.5f);
    if (o == gfx_orient()) {
        s_rot_want = o;
        return;
    }
    if (o != s_rot_want) {
        s_rot_want = o;
        s_rot_t = t;
        return;
    }
    if (t - s_rot_t > 250) {
        orient_apply(o);  // (held for a moment: no turn back and forth)
    }
}

// ---------------------------------------------------------------- update
static void update(int64_t now)
{
    (void)now;
    int64_t t = ms_now();
    float dt = (float)(t - s_last_ms) / 1000.0f;
    s_last_ms = t;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    if (t - s_wifi_t > 1000) {
        s_wifi_t = t;
        s_wifi = wifi_state() == WIFI_CONNECTED;
    }
    follow_board(t);
    // the fetch task: take an answer, start the next request (a search goes first)
    rq_take();
    if (s_searching && s_rq_mine < 0 && !rq_busy() && s_task_alive) {
        if (rq_start(RQ_SEARCH)) {
            s_rq_mine = s_rq.gen;
            s_rq_what = RQ_SEARCH;
        }
    }
    rq_next();
    // the sky of the place that shows
    int64_t un = unix_now();
    if (s_cur < s_npl) {
        sky_target(&s_wx[s_cur], &s_pl[s_cur], un);
    }
    if (s_motion) {
        sky_step(dt);
    } else {
        s_sky = s_sky_to;
        s_flash = 0;
        s_bolt_t = -1;
    }
    // the page: the momentum scroll (the rubber band at the top is the pull to update), the slide of a new page
    if (s_sc == SC_PLACE && !s_land) {
        ks_set_max(&s_pks,
                   fmaxf(page_max_scroll(), 1.0f));  // (1 px at least: a short page can still be pulled to update)
        ks_update(&s_pks, t);
        float p = ks_pos(&s_pks);
        s_scroll = p < -110 ? -110 : p;
    }
    if (s_sc == SC_PLACE && !s_drag && s_xoff != 0) {
        s_xoff += (0 - s_xoff) * (1 - expf(-14 * dt));
        if (fabsf(s_xoff) < 0.5f) {
            s_xoff = 0;
        }
    }
    if (s_sc == SC_PLACES || s_sc == SC_ADD) {
        ks_set_view(&s_lks, (float)(list_bottom() - list_top()));
        ks_set_max(&s_lks, list_max());
        ks_update(&s_lks, t);
        s_lscroll = ks_pos(&s_lks);
        // a hold of a place: it goes (MY PLACE comes back with the next open, or from the add screen)
        if (s_sc == SC_PLACES && s_hold_row >= 0 && !s_dmoved && t - s_hold_t > 1000) {
            place_remove(s_hold_row);
            sfx_tone(600, 300, 90, W_SINE, 26);
            s_hold_row = -2;
        }
    }
    // the battery: the live sky draws at about 25 frames a second; with no motion only a change draws
    bool still = (!s_drag && !ks_moving(&s_pks) && s_xoff == 0) || s_sc != SC_PLACE;
    if (still && t - s_touch_ms > 3000) {
        bool dark = !strcmp(app_screen_state(), "off");
        vTaskDelay(pdMS_TO_TICKS(dark ? 250 : s_motion ? 30 : 120));
    }
}

#include "wx_draw.inc"

static bool awake(void)
{
    return s_sc == SC_KBD || s_searching || (s_rq_mine >= 0 && !s_npl);
}

const app_t app_weather = {
        .name = "WEATHER",
        .landscape = false,
        .round = APP_ROUND_HI,  // (the round boards: the page in the circle, s_rnd)
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
