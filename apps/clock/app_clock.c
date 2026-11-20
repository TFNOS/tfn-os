// CLOCK: a clock face with the date, the alarms, the timers, the stopwatch and a world clock.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_clock at the end of this file).
// The memory: the file statics of this file and of its .inc parts. On a round panel, ck_round.inc makes two frames
// in PSRAM at the enter and frees them at the leave.
//
//   CLOCK    the time of the home city (big), the seconds, the date, the next alarm, two cities of the world clock
//   ALARMS   up to 8 alarms: the time, the days (once, every day, the days of the week), the sound, the volume, the
//            snooze, a name; each one on or off
//   WORLD    the cities (up to 8): their time, their day (today, tomorrow, yesterday), the hours from home, day or night
//   RING     the alarm rings: SNOOZE, or a hold of 1 s to STOP
//   TIMER    up to 4 count-downs: START / PAUSE, RESET, DELETE; NEW TIMER: 1 3 5 10 15 minutes or an own
//            time, a name. They ring in the system as the alarms (timer_set of main/alarm.h; ABI rev 18).
//   STOPWATCH  START / STOP, LAP, RESET; it keeps its start, so it runs on when CLOCK closes (not over a restart)
// THE LAYERS: the alarms ring in the system (main/alarm.c): also when another app is open or the screen is off, the
// service opens this app with the argument "ring". This app only edits the list (alarm_set) and the home zone
// (alarm_tz_set). The zones of the cities: POSIX TZ texts (ck_cities.inc), read by tz.c (tz_parse and tz_offset of
// tfn/tz.h).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tfn/alarm.h"
#include "tfn/app.h"
#ifndef APP_ROUND_HI
#define APP_ROUND_HI 2  // (a tfn/app.h with app_t.round as a bool has no APP_ROUND_HI)
#endif
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/tz.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"
#if BOARD_ROUND
#include "esp_heap_caps.h"
#endif

static bool s_he;  // the language: false English, true Hebrew
#include "ck_text.inc"
#include "ck_cities.inc"
#include "interfont/ck_fonts.inc"

#define BAR BOARD_BAR_SCREEN
static int64_t s_now;  // ms
static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

// ================================================================ the settings (saved)
static bool s_h24 = true, s_secs = true, s_night, s_keep = true;
static int s_home = CITY_HOME_DEFAULT;
#define WORLD_MAX 8
static int s_world[WORLD_MAX], s_nworld;

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h ? h : 1;
}
static int city_of_hash(uint32_t h)
{
    for (int i = 0; i < NCITY; i++) {
        if (name_hash(CITIES[i].name[0]) == h) {
            return i;
        }
    }
    return -1;
}

static void save(void)
{
    store_set("ck_opts", (uint32_t)(s_h24 | s_secs << 1 | s_night << 2 | s_keep << 3 | 1 << 7));
    store_set("ck_home", name_hash(CITIES[s_home].name[0]));
    char k[8];
    for (int i = 0; i < WORLD_MAX; i++) {
        snprintf(k, sizeof k, "ck_w%d", i);
        store_set(k, i < s_nworld ? name_hash(CITIES[s_world[i]].name[0]) : 0);
    }
}

static void load(void)
{
    const uint32_t o = store_get("ck_opts", 0);
    if (o & 0x80) {
        s_h24 = o & 1;
        s_secs = o & 2;
        s_night = o & 4;
        s_keep = o & 8;
    }
    const int h = city_of_hash(store_get("ck_home", 0));
    s_home = h >= 0 ? h : CITY_HOME_DEFAULT;
    s_nworld = 0;
    char k[8];
    for (int i = 0; i < WORLD_MAX; i++) {
        snprintf(k, sizeof k, "ck_w%d", i);
        const int c = city_of_hash(store_get(k, 0));
        if (c >= 0) {
            s_world[s_nworld++] = c;
        }
    }
    if (!(o & 0x80) && !s_nworld) {  // (the first open: three cities)
        static const char *const DEF[] = {"LONDON", "NEW YORK", "TOKYO"};
        for (int i = 0; i < 3; i++) {
            const int c = city_of_hash(name_hash(DEF[i]));
            if (c >= 0) {
                s_world[s_nworld++] = c;
            }
        }
    }
}

// ================================================================ the time
static int64_t s_utc;  // the time now (s)
static bool s_time_ok;

// the local time of a city (s), its offset (s), the name of its zone now
static int64_t city_local(int c, int *off, const char **zn)
{
    tz_rule_t z;
    if (!tz_parse(CITIES[c].tz, &z)) {
        memset(&z, 0, sizeof z);
    }
    const char *n = NULL;
    const int o = tz_offset(&z, s_utc, &n);
    if (off) {
        *off = o;
    }
    if (zn) {  // (a copy: n points into z, a variable of this call)
        static char name[8];
        snprintf(name, sizeof name, "%s", n ? n : "");
        *zn = name;
    }
    return s_utc + o;
}

// "07:42" or "7:42" (12 h: *ampm = "AM" / "PM")
static void hm_text(char *o, int n, int h, int m, const char **ampm)
{
    if (s_h24) {
        snprintf(o, (size_t)n, "%02d:%02d", h, m);
        if (ampm) {
            *ampm = "";
        }
    } else {
        snprintf(o, (size_t)n, "%d:%02d", h % 12 ? h % 12 : 12, m);
        if (ampm) {
            *ampm = h < 12 ? "AM" : "PM";
        }
    }
}

// ================================================================ the alarms (a copy of the list of the system)
static alarm_t s_al[ALARM_MAX];
static int s_nal;

static void al_load(void)
{
    s_nal = alarm_get(s_al, ALARM_MAX);
}
static void al_save(void)
{
    alarm_set(s_al, s_nal);
}

static void days_text(char *o, int n, uint8_t d)
{
    if (d == 0) {
        snprintf(o, (size_t)n, "%s", TX(T_ONCE));
        return;
    }
    if (d == 0x7F) {
        snprintf(o, (size_t)n, "%s", TX(T_EVERYDAY));
        return;
    }
    if (d == 0x1F) {
        snprintf(o, (size_t)n, "%s", TX(T_SUNTHU));
        return;
    }
    if (d == 0x3E) {
        snprintf(o, (size_t)n, "%s", TX(T_MONFRI));
        return;
    }
    if (d == 0x41) {
        snprintf(o, (size_t)n, "%s", TX(T_WEEKEND));
        return;
    }
    int k = 0;
    o[0] = 0;
    for (int i = 0; i < 7 && k < n - 8; i++) {
        if ((d >> i) & 1) {
            k += snprintf(o + k, (size_t)(n - k), "%s%s", k ? " " : "", DAY_SHORT[i][s_he ? 1 : 0]);
        }
    }
}

// ================================================================ the screens
enum {
    SC_CLOCK,
    SC_ALARMS,
    SC_EDIT,
    SC_WORLD,
    SC_CITIES,
    SC_RING,
    SC_KBD,
    SC_TIMER,
    SC_TNEW,
    SC_SW
};
static int s_scr;
static int s_edit;  // the alarm of EDIT (-1 = a new one)
static alarm_t s_ed;
static int s_pick_home;  // CITIES: 1 = the home city, 0 = a city for WORLD
static float s_scroll;
static int64_t s_toast_t, s_del_t;
static char s_toast[96];
static char s_kbuf[32];
static int64_t s_hold_t;       // RING: the hold of STOP started (0 = no finger)
static int s_drag_digit = -1;  // EDIT: a drag on the hour (0) or the minute (1)
static void toast(const char *s)
{
    snprintf(s_toast, sizeof s_toast, "%s", s);
    s_toast_t = s_now;
}

static void edit_open(int i)
{
    s_edit = i;
    if (i >= 0) {
        s_ed = s_al[i];
    } else {
        memset(&s_ed, 0, sizeof s_ed);
        s_ed.on = 1;
        s_ed.hour = 7;
        s_ed.min = 0;
        s_ed.days = 0x1F;  // (SUN-THU: the work week of the home city)
        s_ed.vol = 70;
        s_ed.snooze = 9;
    }
    s_scroll = 0;
    s_del_t = 0;
    s_scr = SC_EDIT;
}

static void edit_save(void)
{
    if (s_edit >= s_nal) {
        s_edit = -1;  // (the list changed under the EDIT page: the alarm goes in as a new one)
    }
    if (s_edit < 0) {
        if (s_nal >= ALARM_MAX) {
            toast(TX(T_FULL));
            return;
        }
        s_al[s_nal++] = s_ed;
    } else {
        s_al[s_edit] = s_ed;
    }
    al_save();
    s_scr = SC_ALARMS;
    s_scroll = 0;
}

static void edit_delete(void)
{
    // (a new alarm, or the list changed under the EDIT page and the alarm is not in it now: nothing to delete;
    // a delete past the end must do nothing)
    if (s_edit < 0 || s_edit >= s_nal) {
        s_scr = SC_ALARMS;
        return;
    }
    s_toast[0] = 0;
    memmove(&s_al[s_edit], &s_al[s_edit + 1], sizeof(alarm_t) * (size_t)(s_nal - s_edit - 1));
    s_nal--;
    al_save();
    s_scr = SC_ALARMS;
    s_scroll = 0;
}

// ================================================================ the timers (a copy of the list of the system)
static ctimer_t s_tm[TIMER_MAX];
static int s_tn_min = 5, s_tn_sec;  // NEW TIMER: the time
static char s_tn_label[24];
static int s_kbd_for;  // the keyboard: 0 = the name of an alarm, 1 = of a new timer
static int64_t s_tdel_t;
static int s_tdel_i = -1;
static void tm_load(void)
{
    timer_get(s_tm, TIMER_MAX);
}
static void tm_save(void)
{
    timer_set(s_tm, TIMER_MAX);
}
// the time that is left (ms); -1 = a free place
static int64_t tm_left(int i)
{
    const ctimer_t *t = &s_tm[i];
    if (t->state == TIMER_RUN) {
        return t->end_ms > s_now ? t->end_ms - s_now : 0;
    }
    if (t->state == TIMER_PAUSE) {
        return t->left_ms;
    }
    if (t->state == TIMER_DONE) {
        return 0;
    }
    return -1;
}
static void tm_go(int i)  // START / PAUSE (a done one starts again with its length)
{
    ctimer_t *t = &s_tm[i];
    if (t->state == TIMER_RUN) {
        t->left_ms = tm_left(i);
        t->state = TIMER_PAUSE;
    } else if (t->state == TIMER_PAUSE) {
        t->end_ms = s_now + t->left_ms;
        t->state = TIMER_RUN;
    } else if (t->state == TIMER_DONE) {
        t->end_ms = s_now + (int64_t)t->len_s * 1000;
        t->state = TIMER_RUN;
    }
    tm_save();
}
static void tm_reset(int i)
{
    s_tm[i].state = TIMER_PAUSE;
    s_tm[i].left_ms = (int64_t)s_tm[i].len_s * 1000;
    tm_save();
}
static void tm_new(void)
{
    const int len = s_tn_min * 60 + s_tn_sec;
    if (len <= 0) {
        return;
    }
    for (int i = 0; i < TIMER_MAX; i++) {
        if (s_tm[i].state != TIMER_FREE) {
            continue;
        }
        ctimer_t *t = &s_tm[i];
        memset(t, 0, sizeof *t);
        t->state = TIMER_RUN;
        t->len_s = (uint32_t)len;
        t->end_ms = s_now + (int64_t)len * 1000;
        t->sound = 1;  // (CHIME)
        t->vol = 70;
        snprintf(t->label, sizeof t->label, "%s", s_tn_label);
        tm_save();
        s_scr = SC_TIMER;
        return;
    }
    toast(TX(T_FULL));
}
// "4:59" or "1:02:03" (a time that is left, up to the next second)
static void left_text(char *o, int n, int64_t ms)
{
    const int64_t s = (ms + 999) / 1000;
    if (s >= 3600) {
        snprintf(o, (size_t)n, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    } else {
        snprintf(o, (size_t)n, "%d:%02d", (int)(s / 60), (int)(s % 60));
    }
}

// ================================================================ the stopwatch (saved: it runs on when CLOCK closes)
#define LAPS 20
static bool s_sw_run;
static uint32_t s_sw0, s_sw_acc;  // the start (the clock of the board, ms) of this run; the time before
static uint32_t s_lap[LAPS];      // the time at each lap (ms since the start)
static int s_nlap;
static uint32_t sw_ms(void)
{
    return s_sw_acc + (s_sw_run ? (uint32_t)s_now - s_sw0 : 0);
}
static void sw_save(void)
{
    store_set("ck_swr", s_sw_run);
    store_set("ck_sw0", s_sw0);
    store_set("ck_swa", s_sw_acc);
    store_set("ck_swn", (uint32_t)s_nlap);
    char k[8];
    for (int i = 0; i < s_nlap; i++) {
        snprintf(k, sizeof k, "ck_l%d", i);
        store_set(k, s_lap[i]);
    }
}
static void sw_load(void)
{
    s_sw_run = store_get("ck_swr", 0) != 0;
    s_sw0 = store_get("ck_sw0", 0);
    s_sw_acc = store_get("ck_swa", 0);
    s_nlap = (int)store_get("ck_swn", 0);
    if (s_nlap < 0 || s_nlap > LAPS) {
        s_nlap = LAPS;
    }
    char k[8];
    for (int i = 0; i < s_nlap; i++) {
        snprintf(k, sizeof k, "ck_l%d", i);
        s_lap[i] = store_get(k, 0);
    }
    if (s_sw_run && s_sw0 > (uint32_t)s_now) {  // (the board started again: the run is lost; the time stops)
        s_sw_run = false;
        sw_save();
    }
}
static void sw_go(void)
{
    if (s_sw_run) {
        s_sw_acc = sw_ms();
        s_sw_run = false;
    } else {
        s_sw0 = (uint32_t)s_now;
        s_sw_run = true;
    }
    sw_save();
}
static void sw_lap(void)  // (running: a lap; stopped: RESET)
{
    if (s_sw_run) {
        if (s_nlap == LAPS) {
            memmove(s_lap, s_lap + 1, sizeof(s_lap[0]) * (LAPS - 1));
            s_nlap--;
        }
        s_lap[s_nlap++] = sw_ms();
    } else {
        s_sw_acc = 0;
        s_nlap = 0;
    }
    sw_save();
}

static void kbd_done(bool ok)
{
    s_scr = s_kbd_for ? SC_TNEW : SC_EDIT;
    tfd_screen();
    if (ok && s_kbd_for) {
        strlcpy(s_tn_label, s_kbuf, sizeof s_tn_label);
    } else if (ok) {
        strlcpy(s_ed.label, s_kbuf, sizeof s_ed.label);
    }
}

static void set_home(int c)
{
    s_home = c;
    alarm_tz_set(CITIES[c].tz);
    save();
}

#if BOARD_ROUND
static int clampi_ck(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
#define PI_CK  3.14159265f
#define TAU_CK 6.2831853f
enum {
    FACE_CLASSIC,
    FACE_POCKET,
    FACE_CHRONO,
    FACE_N
};
static int s_face = FACE_POCKET;  // (saved: clock_face; the panel writes it)
static int s_dial_key = -1;       // the face, the night mode, the language, the day of the cache
static int64_t s_peek_t;          // a tap on a face: the CLOCK page shows until this time (ms)
#endif
// ================================================================ the settings panel
enum {
    PI_LANG,
    PI_H24,
    PI_SECS,
    PI_NIGHT,
    PI_KEEP,
    PI_HOME,
    PI_FACE
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const H24_VALS[] = {"12 H", "12 שעות", "24 H", "24 שעות"};
static const char *const ONOFF[] = {"OFF", "כבוי", "ON", "פועל"};
static const char *const FACE_VALS[] = {"CLASSIC", "קלאסי", "POCKET WATCH", "שעון כיס", "CHRONO", "כרונוגרף"};
static pitem_t s_items[] = {
        {PI_PICK, PI_FACE, 3, 1, {"FACE", "פני השעון"}, FACE_VALS, "clock_face"},  // (a round panel only; first)
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "clock_lang"},
        {PI_PICK, PI_H24, 2, 0, {"THE HOURS", "שעות"}, H24_VALS, NULL},
        {PI_PICK, PI_SECS, 2, 0, {"SECONDS", "שניות"}, ONOFF, NULL},
        {PI_PICK, PI_NIGHT, 2, 0, {"NIGHT MODE", "מצב לילה"}, ONOFF, NULL},
        {PI_PICK, PI_KEEP, 2, 0, {"SCREEN ALWAYS ON", "מסך תמיד דולק"}, ONOFF, NULL},
        {PI_ACT, PI_HOME, 0, 0, {"HOME CITY", "עיר הבית"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))
static bool item_shown(int id)
{
    return id != PI_FACE || BOARD_ROUND;
}
static int item_get(int id)
{
    switch (id) {
    case PI_H24:
        return s_h24;
    case PI_SECS:
        return s_secs;
    case PI_NIGHT:
        return s_night;
    case PI_KEEP:
        return s_keep;
    default:
        return 0;
    }
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_H24:
        s_h24 = v;
        break;
    case PI_SECS:
        s_secs = v;
        break;
    case PI_NIGHT:
        s_night = v;
        break;
    case PI_KEEP:
        s_keep = v;
        break;
#if BOARD_ROUND
    case PI_FACE:
        s_face = v;
        s_dial_key = -1;
        s_peek_t = 0;
        break;
#endif
    case PI_HOME:
        s_pick_home = 1;
        s_scroll = 0;
        s_scr = SC_CITIES;
        break;
    }
    save();
}
static bool s_down, s_drag;
static void panel_opened(void)
{
    s_down = s_drag = false;
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

#include "ck_draw.inc"
#include "ck_round.inc"

// ================================================================ the touches
static int s_dy0, s_dx0;
static float s_scroll0;
static int s_drag_base;

#if BOARD_ROUND
static void input_page(const input_event_t *ev)
#else
static void input(const input_event_t *ev)
#endif
{
    if (s_scr == SC_KBD) {
        const int r = kbd_input(ev);
        if (r == KBD_OK) {
            kbd_done(true);
        } else if (r == KBD_CANCEL) {
            kbd_done(false);
        }
        return;
    }
    const int x = ev->x, y = ev->y;
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = false;
        s_dx0 = x;
        s_dy0 = y;
        s_scroll0 = s_scroll;
        s_drag_digit = s_scr == SC_EDIT ? digit_at(x, y) : -1;
        s_drag_base = s_drag_digit == 0 ? s_ed.hour : s_ed.min;
        if (s_scr == SC_RING && hold_at(x, y)) {
            s_hold_t = s_now;
        }
        break;
    case IN_MOVE:
        if (s_down && abs(y - s_dy0) > 8 && (s_scr != SC_RING)) {
            s_drag = true;
            input_tap_cancel();
        }
        if (s_drag && s_drag_digit >= 0) {  // a drag on a digit: up = more (a step each 14 px)
            const int steps = (s_dy0 - y) / 14;
            if (s_drag_digit == 0) {
                s_ed.hour = (uint8_t)(((s_drag_base + steps) % 24 + 24) % 24);
            } else {
                s_ed.min = (uint8_t)(((s_drag_base + steps) % 60 + 60) % 60);
            }
        } else if (s_drag) {
            s_scroll = s_scroll0 - (float)(y - s_dy0);
            if (s_scroll < 0) {
                s_scroll = 0;
            }
        }
        if (s_scr == SC_RING && s_hold_t && !hold_at(x, y)) {
            s_hold_t = 0;
        }
        break;
    case IN_UP:
        s_down = s_drag = false;
        s_drag_digit = -1;
        s_hold_t = 0;
        break;
    case IN_TAP: {
        const int h = hit_at(x, y);
        if (h >= 0) {
            hit_do(h);
        }
        break;
    }
    default:
        break;
    }
}

// ================================================================ enter / leave / update

static void enter(int64_t now)
{
    (void)now;
    s_now = ms_now();
    s_he = store_get("clock_lang", 1) == 1;  // (Hebrew when nothing is saved)
    load();
    al_load();
    tm_load();
    sw_load();
    char tz[64];
    alarm_tz_get(tz, sizeof tz);
    if (strcmp(tz, CITIES[s_home].tz)) {
        alarm_tz_set(CITIES[s_home].tz);
    }
    clock_sync();  // (the time from the network, once)
    s_scroll = 0;
    s_toast[0] = 0;
    s_down = s_drag = false;
    s_hold_t = 0;
    s_utc = time(NULL);
    s_time_ok = clock_time_ok();
    sysui_panel_items(&PANEL);
#if BOARD_ROUND
    round_enter();
#endif
    s_scr = !strcmp(app_launch_arg(), "ring") || alarm_ringing() >= 0 ? SC_RING : SC_CLOCK;
}

static void leave(void)
{
    store_set("clock_lang", s_he);
    save();
#if BOARD_ROUND
    round_leave();
#endif
}

static void update(int64_t now)
{
    (void)now;
    s_now = ms_now();
    s_utc = time(NULL);
    s_time_ok = clock_time_ok();
    tm_load();
    const int r = alarm_ringing();
    if (r >= 0 && s_scr != SC_RING && s_scr != SC_KBD) {
        s_scr = SC_RING;  // (a ring while the app is open)
    }
    if (r < 0 && s_scr == SC_RING) {
        s_scr = SC_CLOCK;  // (the ring stopped: by itself too)
    }
    if (s_scr == SC_RING && s_hold_t && s_now - s_hold_t >= 1000) {  // STOP: a hold of 1 s
        s_hold_t = 0;
        const bool was_timer = alarm_ringing() >= ALARM_MAX;
        alarm_stop();
        al_load();
        s_scr = was_timer ? SC_TIMER : SC_CLOCK;
        sfx_ui(SFX_CLICK);
    }
}

static bool awake(void)
{
    return true;
}
static bool keep_on(void)
{
    return s_keep || s_scr == SC_RING;
}  // (a clock on the table: the screen stays on)
static bool background(void)
{
    return true;
}  // (the ring comes from the system; the face stays current)

const app_t app_clock = {
        .name = "CLOCK",
        .landscape = true,
        .round_fit = true,
        .round = APP_ROUND_HI,  // (a round panel: the watch faces and the pages in the circle, ck_round.inc)
        .run = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .keep_on = keep_on,
        .background = background,
};
