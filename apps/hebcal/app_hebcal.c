// HEBREW CALENDAR: the Hebrew date, the holidays, the weekly reading and
// the times of Shabbat and of the day, for a city, with no network (the time of day comes from the clock of the board).
//   TODAY     the Hebrew date (big), the date, the events of today or the next holiday, the next candle lighting
//             and havdalah with the reading of the week
//   CALENDAR  the weeks (a drag scrolls, a flick runs on): each day with its Hebrew date and a mark for its events
//   DAY       one day: the Hebrew date, the events, the reading, candle lighting, havdalah, a fast, the day times
//   CITIES    the list of the cities (Israel and the Diaspora)
// The calculations: hc_cal.inc (checked against pyluach and hebcal.com).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tfn/alarm.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/tz.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"

static bool s_he;  // the language: false English, true Hebrew
#include "hebcal/hc_parsha.inc"
#include "hebcal/hc_cal.inc"
#include "hc_text.inc"
#include "hebcal/hc_cities.inc"

#define BAR BOARD_BAR_SCREEN
static int64_t s_now;  // ms
static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

// ================================================================ the settings (saved)
static int s_city = HC_CITY_DEFAULT;
static int s_cmode;   // the candle minutes: 0 the rule of the city, 1 18, 2 20, 3 40
static int s_ilmode;  // the holidays: 0 by the city, 1 Israel, 2 the Diaspora
static bool israel(void)
{
    return s_ilmode ? s_ilmode == 1 : HC_CITIES[s_city].israel;
}
static int cmin(void)
{
    static const int M[4] = {0, 18, 20, 40};
    return s_cmode ? M[s_cmode & 3] : HC_CITIES[s_city].cmin;
}
static uint32_t name_hash(const char *s)  // (the same hash as CLOCK: ck_home)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h ? h : 1;
}
static int city_of_hash(uint32_t h)
{
    for (int i = 0; i < HC_NCITY; i++) {
        if (name_hash(HC_CITIES[i].name[0]) == h) {
            return i;
        }
    }
    return -1;
}

// ================================================================ the time
static tz_rule_t s_tz;
static bool s_time_ok;
static int64_t s_utc;
static int64_t s_today;  // the local day (R.D.)
static int s_min_now;    // the local minute of the day
static void set_city(int c)
{
    s_city = c < 0 || c >= HC_NCITY ? HC_CITY_DEFAULT : c;
    if (!tz_parse(HC_CITIES[s_city].tz, &s_tz)) {
        tz_parse(TZ_IL, &s_tz);
    }
    store_set("hebcal_city", name_hash(HC_CITIES[s_city].name[0]));
}
static int off_min(int64_t f)  // the offset of the zone (minutes) at the local noon of the day f
{
    const int64_t utc = (f - HC_RD1970) * 86400 + 12 * 3600 - s_tz.std_east;
    return tz_offset(&s_tz, utc, NULL) / 60;
}
static void clock_now(void)
{
    s_utc = time(NULL);
    s_time_ok = clock_time_ok();
    if (!s_time_ok) {
        s_today = store_get("hebcal_day", (uint32_t)hc_fixed_greg(2026, 10, 29));
        s_min_now = 12 * 60;
        return;
    }
    const int64_t lt = s_utc + tz_offset(&s_tz, s_utc, NULL);
    s_today = hc_div(lt, 86400) + HC_RD1970;
    s_min_now = (int)((lt - hc_div(lt, 86400) * 86400) / 60);
}

// the day times: a cache of 16 days (each day costs about 400 calls of sinf / cosf)
typedef struct {
    int64_t f;
    int city;
    hc_zman_t z;
} zslot_t;
static zslot_t s_zc[16];
static int s_zk;
static const hc_zman_t *zman(int64_t f)
{
    for (int i = 0; i < 16; i++) {
        if (s_zc[i].f == f && s_zc[i].city == s_city) {
            return &s_zc[i].z;
        }
    }
    s_zk = (s_zk + 1) & 15;
    s_zc[s_zk].f = f;
    s_zc[s_zk].city = s_city;
    const hc_city_t *c = &HC_CITIES[s_city];
    hc_zmanim(f, c->lat, c->lon, off_min(f), &s_zc[s_zk].z);
    return &s_zc[s_zk].z;
}
static void day_times(int64_t f, hc_times_t *t)
{
    hc_day_times(f, israel(), zman(f), cmin(), t);
}

// ================================================================ the texts of a day
// "ח' בחשון תשפ"ז" (always in Hebrew letters)
static void heb_date_he(int64_t f, char *o, int n)
{
    int y, m, d;
    hc_heb(f, &y, &m, &d);
    char a[16], b[16];
    hc_gematria(d, a, sizeof a);
    hc_gematria(y, b, sizeof b);
    const bool he = s_he;
    s_he = true;
    snprintf(o, (size_t)n, "%s ב%s %s", a, hmonth(y, m), b);
    s_he = he;
}
// "8 CHESHVAN 5787"
static void heb_date_en(int64_t f, char *o, int n)
{
    int y, m, d;
    hc_heb(f, &y, &m, &d);
    const bool he = s_he;
    s_he = false;
    snprintf(o, (size_t)n, "%d %s %d", d, hmonth(y, m), y);
    s_he = he;
}
// "THURSDAY 29 OCTOBER 2026" / "יום חמישי, 29 באוקטובר 2026"
static void greg_text(int64_t f, char *o, int n, bool wday)
{
    int y, m, d;
    hc_greg(f, &y, &m, &d);
    const char *w = WDAY[hc_dow(f)][s_he];
    if (s_he) {
        snprintf(o, (size_t)n, "%s%s%d ב%s %d", wday ? w : "", wday ? ", " : "", d, GMONTH[m - 1][1], y);
    } else {
        snprintf(o, (size_t)n, "%s%s%d %s %d", wday ? w : "", wday ? " " : "", d, GMONTH[m - 1][0], y);
    }
}
// the order of the events in a list: the holidays first, the Omer last
static int ev_rank(int id)
{
    const int k = HC_KIND[id];
    return k & (HK_YT | HK_MAJOR) ? 0
           : k & HK_FAST          ? 1
           : k & HK_MODERN        ? 2
           : k & HK_MINOR         ? 3
           : k & HK_SHAB          ? 4
           : k & HK_RC            ? 5
           : k & HK_EREV          ? 6
                                  : 7;
}
static int day_events(int64_t f, hc_ev_t *e, int max)
{
    const int n = hc_events(f, israel(), e, max);
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && ev_rank(e[j].id) < ev_rank(e[j - 1].id); j--) {
            const hc_ev_t t = e[j];
            e[j] = e[j - 1];
            e[j - 1] = t;
        }
    }
    return n;
}

// ================================================================ TODAY: the cache (new at a new day, city or setting)
typedef struct {
    int64_t day;
    int city, il, cm;
    int64_t cf, hf;  // the day of the next candle lighting and of its havdalah (0: none)
    float ct, ht;    // their minutes
    bool cnight;
    char occasion[48];  // "SHABBAT VAYERA", "PESACH"
    int64_t next_f;     // the next holiday (when today has none)
    char next[40];
    bool he;
} today_t;
static today_t s_td;
static void today_calc(void)
{
    const int il = israel(), cm = cmin();
    // the next candle lighting whose havdalah is still to come
    const bool same = s_td.day == s_today && s_td.city == s_city && s_td.il == il && s_td.cm == cm && s_td.he == s_he;
    const int64_t now_abs = s_today * 1440 + s_min_now;
    if (same && (!s_td.hf || s_td.hf * 1440 + (int64_t)s_td.ht > now_abs)) {
        return;
    }
    s_td.day = s_today;
    s_td.city = s_city;
    s_td.il = il;
    s_td.cm = cm;
    s_td.he = s_he;
    // the next candle lighting and the next havdalah; a havdalah before the next candles: the days of rest now (their
    // candles are in the past)
    s_td.cf = s_td.hf = 0;
    int64_t nc = 0, nh = 0;
    float nct = 0, nht = 0;
    bool ncn = false;
    for (int64_t f = s_today; f <= s_today + 8 && (!nc || !nh); f++) {
        hc_times_t t;
        day_times(f, &t);
        if (!nc && t.candles >= 0 && f * 1440 + (int64_t)t.candles > now_abs) {
            nc = f;
            nct = t.candles;
            ncn = t.candles_night;
        }
        if (!nh && t.havdalah >= 0 && f * 1440 + (int64_t)t.havdalah > now_abs) {
            nh = f;
            nht = t.havdalah;
        }
    }
    if (nh && (!nc || nh * 1440 + (int64_t)nht < nc * 1440 + (int64_t)nct)) {
        // the start of these days of rest: the first candle day after the havdalah before them
        for (int64_t f = nh - 1; f >= nh - 4; f--) {
            hc_times_t t;
            day_times(f, &t);
            if (t.havdalah >= 0 && f < nh) {
                break;
            }
            if (t.candles >= 0) {
                s_td.cf = f;
                s_td.ct = t.candles;
                s_td.cnight = t.candles_night;
            }
        }
        if (s_td.cf) {
            s_td.hf = nh;
            s_td.ht = nht;
        }
    } else if (nc) {
        s_td.cf = nc;
        s_td.ct = nct;
        s_td.cnight = ncn;
        for (int64_t g = nc + 1; g <= nc + 4; g++) {
            hc_times_t u;
            day_times(g, &u);
            if (u.havdalah >= 0) {
                s_td.hf = g;
                s_td.ht = u.havdalah;
                break;
            }
        }
    }
    s_td.occasion[0] = 0;
    if (s_td.cf) {
        const int64_t h = s_td.cf + 1;
        hc_ev_t e[12];
        const int n = day_events(h, e, 12);
        int y, m, d;
        hc_heb(h, &y, &m, &d);
        if (n && (HC_KIND[e[0].id] & HK_YT)) {
            ev_text(&e[0], y, s_td.occasion, sizeof s_td.occasion);
        } else {
            char p[40];
            parsha_text(hc_parsha(h, il), p, sizeof p);
            snprintf(s_td.occasion, sizeof s_td.occasion, "%s%s%s", TX(T_SHABBAT), p[0] ? " " : "", p);
        }
    }
    // the next holiday, fast or day of Israel (not chol hamoed, an eve, Rosh Chodesh, the Omer, a special Shabbat)
    s_td.next[0] = 0;
    s_td.next_f = 0;
    for (int64_t f = s_today + 1; f < s_today + 400 && !s_td.next_f; f++) {
        hc_ev_t e[12];
        const int n = day_events(f, e, 12);
        for (int i = 0; i < n; i++) {
            const int id = e[i].id, k = HC_KIND[id];
            if (id == HE_SUKKOT_CHM || id == HE_PESACH_CHM || id == HE_HOSHANA || id == HE_PURIM_KATAN) {
                continue;
            }
            if (!(k & (HK_YT | HK_MAJOR | HK_FAST | HK_MODERN | HK_MINOR))) {
                continue;
            }
            int y, m, d;
            hc_heb(f, &y, &m, &d);
            char t[40];
            hc_ev_t one = e[i];
            if (one.id == HE_CHANUKAH) {
                one.n = 0;
            }
            ev_text(&one, y, t, sizeof t);
            if (one.id == HE_CHANUKAH) {
                snprintf(t, sizeof t, "%s", EV_NAME[HE_CHANUKAH][s_he]);
            }
            const int days = (int)(f - s_today);
            if (days == 1) {
                snprintf(s_td.next, sizeof s_td.next, TX(T_TOMORROW_IS), t);
            } else {
                snprintf(s_td.next, sizeof s_td.next, TX(T_IN_DAYS), t, days);
            }
            s_td.next_f = f;
            break;
        }
    }
}

// ================================================================ the screens and the state of the touches
enum {
    SC_TODAY,
    SC_MONTH,
    SC_DAY,
    SC_CITIES
};
static int s_scr, s_day_from;
static int64_t s_sel;       // DAY: the day
static float s_scroll;      // DAY, CITIES: the scroll of the list (px)
static float s_wk, s_wk_v;  // CALENDAR: the top row (weeks from the week before today), the flick
static bool s_down, s_drag, s_hdrag;
static int s_dx0, s_dy0;
static float s_scroll0, s_wk0;
static int64_t s_mt;  // the time of the last move (ms)
static int s_my;      // the y of the last move

static void open_day(int64_t f, int from)
{
    s_sel = f;
    s_day_from = from;
    s_scroll = 0;
    s_scr = SC_DAY;
}

// ================================================================ the settings panel
enum {
    PI_LANG,
    PI_CITY,
    PI_CMIN,
    PI_IL
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const CMIN_VALS[] = {"BY THE CITY", "לפי העיר",      "18 MIN", "18 דק\xD7\xB3",
                                        "20 MIN",      "20 דק\xD7\xB3", "40 MIN", "40 דק\xD7\xB3"};
static const char *const IL_VALS[] = {"BY THE CITY", "לפי העיר", "ISRAEL", "ישראל", "DIASPORA", "חוץ לארץ"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "hebcal_lang"},
        {PI_ACT, PI_CITY, 0, 0, {"CITY", "עיר"}, NULL, NULL},
        {PI_PICK, PI_CMIN, 4, 0, {"CANDLE LIGHTING", "הדלקת נרות"}, CMIN_VALS, "hebcal_cmin"},
        {PI_PICK, PI_IL, 3, 0, {"HOLIDAYS", "חגים"}, IL_VALS, "hebcal_il"},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))
static bool item_shown(int id)
{
    (void)id;
    return true;
}
static int item_get(int id)
{
    (void)id;
    return 0;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_CMIN:
        s_cmode = v & 3;
        break;
    case PI_IL:
        s_ilmode = v % 3;
        break;
    case PI_CITY:
        s_scroll = 0;
        s_scr = SC_CITIES;
        break;
    }
}
static void panel_opened(void)
{
    s_down = s_drag = s_hdrag = false;
    s_wk_v = 0;
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

#include "hc_draw.inc"

// ================================================================ the touches
static void input(const input_event_t *ev)
{
    const int x = ev->x, y = ev->y;
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = s_hdrag = false;
        s_dx0 = x;
        s_dy0 = s_my = y;
        s_mt = s_now;
        s_scroll0 = s_scroll;
        s_wk0 = s_wk;
        s_wk_v = 0;
        break;
    case IN_MOVE:
        if (!s_down) {
            break;
        }
        if (!s_drag && !s_hdrag) {
            if (abs(y - s_dy0) > 8) {
                s_drag = true;
            } else if (s_scr == SC_DAY && abs(x - s_dx0) > 14) {
                s_hdrag = true;
            }
            if (s_drag || s_hdrag) {
                input_tap_cancel();
            }
        }
        if (s_drag) {
            if (s_scr == SC_MONTH) {
                const float rh = (float)row_h();
                const int64_t dt = s_now - s_mt;
                if (dt > 0) {
                    s_wk_v = s_wk_v * 0.5f + 0.5f * (-(float)(y - s_my) / rh) * 1000.0f / (float)dt;
                }
                s_wk = s_wk0 - (float)(y - s_dy0) / rh;
            } else {
                s_scroll = s_scroll0 - (float)(y - s_dy0);
                if (s_scroll < 0) {
                    s_scroll = 0;
                }
            }
        }
        s_my = y;
        s_mt = s_now;
        break;
    case IN_UP:
        if (s_hdrag && s_scr == SC_DAY && abs(x - s_dx0) > 50) {  // a swipe: the next day toward the reading
            const bool next = (x < s_dx0) != s_he;
            s_sel += next ? 1 : -1;
            s_scroll = 0;
        }
        // no flick after a stop or after a slow drag (under 4 rows a second)
        if (s_scr == SC_MONTH && (!s_drag || s_now - s_mt > 120 || (s_wk_v < 4.0f && s_wk_v > -4.0f))) {
            s_wk_v = 0;
        }
        s_down = s_drag = s_hdrag = false;
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
    s_he = store_get("hebcal_lang", 1) == 1;  // (Hebrew when nothing is saved)
    s_cmode = (int)store_get("hebcal_cmin", 0) & 3;
    s_ilmode = (int)(store_get("hebcal_il", 0) % 3);
    int c = city_of_hash(store_get("hebcal_city", 0));
    if (c < 0) {
        c = city_of_hash(store_get("ck_home", 0));  // (the home city of CLOCK)
    }
    set_city(c);
    clock_sync();  // (the time from the network, once)
    clock_now();
    s_td.day = 0;
    s_scr = SC_TODAY;
    s_wk = 0;
    s_wk_v = 0;
    s_scroll = 0;
    s_down = s_drag = s_hdrag = false;
    sysui_panel_items(&PANEL);
}
static void leave(void)
{
    store_set("hebcal_lang", s_he);
    if (s_time_ok) {
        store_set("hebcal_day", (uint32_t)s_today);
    }
}
static void update(int64_t now)
{
    (void)now;
    const int64_t t = ms_now();
    const float dt = (float)(t - s_now) / 1000.0f;
    s_now = t;
    clock_now();
    if (s_scr == SC_MONTH && !s_down && (s_wk_v > 0.05f || s_wk_v < -0.05f)) {  // the flick runs on and slows
        s_wk += s_wk_v * (dt < 0.1f ? dt : 0.1f);
        s_wk_v *= 1.0f - 4.0f * (dt < 0.1f ? dt : 0.1f);
    }
    if (s_wk < -520) {
        s_wk = -520;
        s_wk_v = 0;
    }
    if (s_wk > 520) {
        s_wk = 520;
        s_wk_v = 0;
    }
}
const app_t app_hebcal = {
        .name = "HEBREW CALENDAR",
        .landscape = true,
        .round = APP_ROUND_HI,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
};
