// STANDBY: the data (standby.h). The settings (the NVS keys sb_*), the place of the board, the weather service
// (Open-Meteo, at most each 30 minutes and only while the screen is on and the WiFi is up; the last answer stays in
// /data/standby_wx.bin with its time), the Hebrew date and the Shabbat times (the calculations of HEBREW CALENDAR:
// libs/hebcal), the sun, and the NEXT item.
// The tasks: the main task calls all functions here; the fetch task "sbwx" (core 1, priority 1, its stack in PSRAM)
// does only the network and the JSON, never a file (a task with a PSRAM stack cannot run while the flash writes).
// NOT ON CORE 0 ABOVE THE MAIN TASK (priority 1 on core 0): the TLS of a fetch took the core from the main loop for
// about 2.5 s, and a fetch starts at each wake of the screen (the weather is old then): the touches waited in the
// queue while the screen showed STANDBY, then all of them acted on the menu at once.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "alarm.h"
#include "power.h"
#include "settings.h"
#include "standby_priv.h"
#include "tfnet/tfnet.h"
#include "wifi.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "hebcal/hc_parsha.inc"
#include "hebcal/hc_cal.inc"
#include "hebcal/hc_cities.inc"
#pragma GCC diagnostic pop
#include "standby_roots.inc"

// ================================================================ the settings
sb_cfg_t sb_cfg;
// the default slots: WEATHER, HEBREW DATE, NOTICES, NEXT
#define SLOTS_DEFAULT ((uint32_t)(SL_WEATHER | SL_HEBREW << 8 | SL_NOTICES << 16 | (uint32_t)SL_NEXT << 24))
void sb_cfg_load(void)
{
    sb_cfg.on = store_get("sb_on", 1) != 0;
    sb_cfg.face = (uint8_t)(store_get("sb_face", SB_FACE_SKY) % SB_FACES);
    sb_cfg.autonight = store_get("sb_night", 1) != 0;
    sb_cfg.nfrom = (uint8_t)(store_get("sb_nfrom", 23) % 24);
    sb_cfg.nto = (uint8_t)(store_get("sb_nto", 6) % 24);
    sb_cfg.accent = (uint8_t)(store_get("sb_accent", 0) % SB_ACCENTS);
    const uint32_t s = store_get("sb_slots", SLOTS_DEFAULT);
    for (int i = 0; i < SB_SLOTS; i++) {
        sb_cfg.slot[i] = (uint8_t)((s >> (8 * i)) & 0xFF) % SL_KINDS;
    }
}
void sb_cfg_save(void)
{
    store_set("sb_on", sb_cfg.on);
    store_set("sb_face", sb_cfg.face);
    store_set("sb_night", sb_cfg.autonight);
    store_set("sb_nfrom", sb_cfg.nfrom);
    store_set("sb_nto", sb_cfg.nto);
    store_set("sb_accent", sb_cfg.accent);
    uint32_t s = 0;
    for (int i = 0; i < SB_SLOTS; i++) {
        s |= (uint32_t)sb_cfg.slot[i] << (8 * i);
    }
    store_set("sb_slots", s);
}
uint16_t sb_accent(void)
{
    static const uint8_t A[SB_ACCENTS][3] = {
            {255, 217, 138}, {140, 200, 255}, {140, 240, 190}, {255, 165, 195}, {236, 236, 246}};
    const uint8_t *c = A[sb_cfg.accent % SB_ACCENTS];
    return rgb(c[0], c[1], c[2]);
}
const char *sb_accent_name(int i)
{
    static const char *const N[SB_ACCENTS] = {"AMBER", "SKY", "MINT", "ROSE", "WHITE"};
    return N[(unsigned)i % SB_ACCENTS];
}
const char *sb_slot_name(int kind)
{
    static const char *const N[SL_KINDS] = {"WEATHER", "HEBREW DATE", "NOTICES", "NEXT",
                                            "BATTERY", "STOCKS",      "FRIENDS", "EMPTY"};
    return N[(unsigned)kind % SL_KINDS];
}
const char *sb_face_name(int face)
{
    static const char *const N[SB_FACES] = {"LIVE SKY", "GLANCE", "NIGHT"};
    return N[(unsigned)face % SB_FACES];
}
bool sb_he(void)
{
    return settings_lang_all() == 1;
}

// ================================================================ the weather codes (WMO)
int sb_sky_kind(int code)
{
    if (code <= 1) {
        return SK_CLEAR;
    }
    if (code == 2) {
        return SK_PART;
    }
    if (code == 3) {
        return SK_CLOUD;
    }
    if (code == 45 || code == 48) {
        return SK_FOG;
    }
    if (code >= 51 && code <= 57) {
        return SK_DRIZZLE;
    }
    if (code == 65 || code == 67 || code == 82) {
        return SK_HEAVY;
    }
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
        return SK_RAIN;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return SK_SNOW;
    }
    if (code >= 95) {
        return SK_STORM;
    }
    return SK_PART;
}
const char *sb_cond(int code, bool he)
{
    static const struct {
        uint8_t code;
        const char *en, *he;
    } C[] = {
            {0, "Clear", "בהיר"},
            {1, "Mainly clear", "בהיר ברובו"},
            {2, "Partly cloudy", "מעונן חלקית"},
            {3, "Overcast", "מעונן"},
            {45, "Fog", "ערפל"},
            {48, "Fog", "ערפל"},
            {51, "Drizzle", "טפטוף"},
            {53, "Drizzle", "טפטוף"},
            {55, "Drizzle", "טפטוף"},
            {56, "Freezing drizzle", "טפטוף קופא"},
            {57, "Freezing drizzle", "טפטוף קופא"},
            {61, "Rain", "גשם"},
            {63, "Rain", "גשם"},
            {65, "Heavy rain", "גשם חזק"},
            {66, "Freezing rain", "גשם קופא"},
            {67, "Freezing rain", "גשם קופא"},
            {71, "Snow", "שלג"},
            {73, "Snow", "שלג"},
            {75, "Heavy snow", "שלג כבד"},
            {77, "Snow grains", "גרגרי שלג"},
            {80, "Showers", "ממטרים"},
            {81, "Showers", "ממטרים"},
            {82, "Heavy showers", "ממטרים חזקים"},
            {85, "Snow showers", "ממטרי שלג"},
            {86, "Snow showers", "ממטרי שלג"},
            {95, "Thunderstorm", "סופת רעמים"},
            {96, "Storm, hail", "סופה וברד"},
            {99, "Storm, hail", "סופה וברד"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        if (C[i].code == code) {
            return he ? C[i].he : C[i].en;
        }
    }
    return "";
}

// ================================================================ the place of the board
// In this order: the place that WEATHER shows (its file /data/weather/places.txt and its page), the city of HEBREW
// CALENDAR, the home city of CLOCK, then the place of the IP address (ipwho.is, once a start). None: no weather, and
// the sky follows the clock only.
static sb_place_t s_place;
static int64_t s_place_at = -600000;  // ms: the last look at the files
static sb_place_t s_ip;               // the place of the IP (the fetch task writes it before done)

static uint32_t name_hash(const char *s)  // (the hash of CLOCK and HEBREW CALENDAR: ck_home, hebcal_city)
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
// the Shabbat rules of a place: the nearest city of the list (within about 30 km), else by the place
static void place_rules(sb_place_t *p)
{
    int best = -1;
    float bd = 0.09f;  // (0.3 degrees, squared)
    for (int i = 0; i < HC_NCITY; i++) {
        const float dx = HC_CITIES[i].lat - p->lat, dy = HC_CITIES[i].lon - p->lon;
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    if (best >= 0) {
        p->israel = HC_CITIES[best].israel != 0;
        p->cmin = HC_CITIES[best].cmin;
        return;
    }
    p->israel = p->lat > 29.4f && p->lat < 33.4f && p->lon > 34.2f && p->lon < 35.9f;
    p->cmin = p->israel ? 20 : 18;
}
static bool place_weather(sb_place_t *p)
{
    FILE *f = fopen("/data/weather/places.txt", "r");
    if (!f) {
        return false;
    }
    static char txt[8 * 220 + 1];
    const int len = (int)fread(txt, 1, sizeof txt - 1, f);
    fclose(f);
    txt[len > 0 ? len : 0] = 0;
    const int want = (int)store_get("weather_page", 0);
    int i = 0;
    for (char *line = txt; line && *line; i++) {
        char *next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        // lat|lon|here|name|sub
        char *a = strchr(line, '|');
        char *b = a ? strchr(a + 1, '|') : NULL;
        char *c = b ? strchr(b + 1, '|') : NULL;
        if (c && (i == want || !p->ok)) {
            p->lat = strtof(line, NULL);
            p->lon = strtof(a + 1, NULL);
            char *e = strchr(c + 1, '|');
            const int n = e ? (int)(e - c - 1) : (int)strlen(c + 1);
            snprintf(p->name, sizeof p->name, "%.*s", n < 47 ? n : 47, c + 1);
            p->ok = true;
        }
        line = next;
    }
    return p->ok;
}
static void place_update(int64_t now_ms)
{
    if (now_ms - s_place_at < 600000 && s_place.ok) {
        return;  // (a look each 10 minutes: the user can change the place in WEATHER)
    }
    s_place_at = now_ms;
    sb_place_t p;
    memset(&p, 0, sizeof p);
    if (!place_weather(&p)) {
        int c = city_of_hash(store_get("hebcal_city", 0));
        if (c < 0) {
            c = city_of_hash(store_get("ck_home", 0));
        }
        if (c >= 0) {
            p.ok = true;
            p.lat = HC_CITIES[c].lat;
            p.lon = HC_CITIES[c].lon;
            snprintf(p.name, sizeof p.name, "%s", HC_CITIES[c].name[0]);
            p.israel = HC_CITIES[c].israel != 0;
            p.cmin = HC_CITIES[c].cmin;
            s_place = p;
            return;
        }
        if (s_ip.ok) {
            p = s_ip;
        }
    }
    if (p.ok) {
        place_rules(&p);
        const int hc = city_of_hash(store_get("hebcal_city", 0));
        if (hc >= 0) {
            p.israel = HC_CITIES[hc].israel != 0;  // (the city of HEBREW CALENDAR gives the rules)
            p.cmin = HC_CITIES[hc].cmin;
        }
    }
    s_place = p;
}
const sb_place_t *sb_place(void)
{
    return &s_place;
}

// ================================================================ the weather service
#define WX_FILE  "/data/standby_wx.bin"
#define WX_VER   1
#define WX_EVERY (30 * 60)  // s: at most one fetch in this time
#define WX_RETRY (5 * 60)   // s: after a fault
#define BODY_MAX (8 * 1024)
static sbw_t s_wx;            // the last weather (the main task)
static sbw_t s_got;           // the answer of the task
static volatile int s_state;  // 0 idle, 1 asked, 2 busy, 3 done (s_got), 4 fault
static volatile bool s_need_ip;
static volatile float s_q_lat, s_q_lon;
static int64_t s_try_at;  // unix s: the last try
static bool s_force;

#ifdef ESP_PLATFORM
static const char *TAG = "standby";
static TaskHandle_t s_task;
static volatile bool s_cancel;
static char *s_body;
static bool parse(const char *b, int n, sbw_t *w)
{
    tn_js_t r = tn_js_root(b, n);
    tn_js_t cur = tn_js_key(r, "current"), hr = tn_js_key(r, "hourly"), dy = tn_js_key(r, "daily");
    if (!cur.p || !hr.p) {
        return false;
    }
    w->off = (int32_t)tn_js_num(tn_js_key(r, "utc_offset_seconds"), 0);
    w->t = (float)tn_js_num(tn_js_key(cur, "temperature_2m"), NAN);
    w->cloud = (float)tn_js_num(tn_js_key(cur, "cloud_cover"), NAN);
    w->wind = (float)tn_js_num(tn_js_key(cur, "wind_speed_10m"), NAN);
    w->wdir = (float)tn_js_num(tn_js_key(cur, "wind_direction_10m"), NAN);
    w->code = (int16_t)tn_js_num(tn_js_key(cur, "weather_code"), 0);
    w->day = (uint8_t)tn_js_num(tn_js_key(cur, "is_day"), 1);
    float v[2];
    w->hi = tn_js_nums(tn_js_key(dy, "temperature_2m_max"), v, 1) == 1 ? v[0] : NAN;
    w->lo = tn_js_nums(tn_js_key(dy, "temperature_2m_min"), v, 1) == 1 ? v[0] : NAN;
    int64_t tt[SBW_HOURS];
    float cd[SBW_HOURS];
    const int nh = tn_js_ints(tn_js_key(hr, "time"), tt, SBW_HOURS);
    tn_js_nums(tn_js_key(hr, "temperature_2m"), w->ht, SBW_HOURS);
    const int nc = tn_js_nums(tn_js_key(hr, "weather_code"), cd, SBW_HOURS);
    for (int i = 0; i < nc; i++) {
        w->hcode[i] = (uint8_t)(isnan(cd[i]) ? 0 : cd[i]);
    }
    w->nh = (uint8_t)(nh > 0 ? nh : 0);
    w->h0 = nh > 0 ? tt[0] : 0;
    return !isnan(w->t);
}

static void ask_ip(void)
{
    int st = 0;
    const int n = tn_fetch_ca(SB_ROOTS_IP, "GET", "https://ipwho.is/?fields=success,city,latitude,longitude", NULL,
                              NULL, 0, s_body, BODY_MAX, 10000, &s_cancel, &st);
    if (n <= 0 || st != 200) {
        ESP_LOGW(TAG, "the place of the IP: %s (status %d)", n < 0 ? tn_err_name(n) : "no answer", st);
        return;
    }
    tn_js_t r = tn_js_root(s_body, n);
    sb_place_t p;
    memset(&p, 0, sizeof p);
    p.lat = (float)tn_js_num(tn_js_key(r, "latitude"), NAN);
    p.lon = (float)tn_js_num(tn_js_key(r, "longitude"), NAN);
    tn_js_text(tn_js_key(r, "city"), p.name, sizeof p.name);
    if (!isnan(p.lat) && !isnan(p.lon)) {
        p.ok = true;
        s_ip = p;
        s_q_lat = p.lat;
        s_q_lon = p.lon;
    }
}

static void fetch_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_state != 1) {
            continue;
        }
        s_state = 2;
        // (the TLS of the two fetches: seconds of ECDSA in software on core 1; the task watchdog printed a warning)
        power_idle1_hold(true);
        if (s_need_ip) {
            ask_ip();
            if (!s_ip.ok) {
                power_idle1_hold(false);
                s_state = 4;
                continue;
            }
        }
        char url[512];
        snprintf(url, sizeof url,
                 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,is_day,weather_code,cloud_cover,wind_speed_10m,wind_direction_10m"
                 "&hourly=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
                 "&timezone=auto&timeformat=unixtime&forecast_days=1&forecast_hours=%d",
                 (double)s_q_lat, (double)s_q_lon, SBW_HOURS);
        int st = 0;
        const int n = tn_fetch_ca(SB_ROOTS_METEO, "GET", url, NULL, NULL, 0, s_body, BODY_MAX, 10000, &s_cancel, &st);
        power_idle1_hold(false);
        memset(&s_got, 0, sizeof s_got);
        const bool ok = n > 0 && st == 200 && parse(s_body, n, &s_got);
        ESP_LOGI(TAG, "the weather: %d bytes, status %d, %s", n, st, ok ? "ok" : "FAILED");
        if (ok) {
            s_got.lat = s_q_lat;
            s_got.lon = s_q_lon;
        }
        s_state = ok ? 3 : 4;
    }
}

static bool task_start(void)
{
    if (s_task) {
        return true;
    }
    if (!s_body) {
        s_body = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_body) {
        return false;
    }
    // (the stack in PSRAM: this task never writes the flash; the main task writes the file)
    if (xTaskCreatePinnedToCoreWithCaps(fetch_task, "sbwx", 10240, NULL, 1, &s_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        return false;
    }
    return true;
}
static void task_go(void)
{
    s_state = 1;
    xTaskNotifyGive(s_task);
}
#else
static bool task_start(void)
{
    return false;  // (the PC tests and the simulator: no network)
}
static void task_go(void)
{
}
#endif

void sb_data_init(void)
{
    sb_cfg_load();
    FILE *f = fopen(WX_FILE, "rb");
    if (f) {
        sbw_t w;
        if (fread(&w, sizeof w, 1, f) == 1 && w.ver == WX_VER) {
            s_wx = w;
        }
        fclose(f);
    }
}

void sb_data_fetch(void)
{
    s_force = true;
}

void sb_data_tick(int64_t now_ms, bool screen_on)
{
    place_update(now_ms);
    if (s_state == 3) {
        s_got.ver = WX_VER;
        s_got.at = (int64_t)time(NULL);
        s_wx = s_got;
        s_state = 0;
        FILE *f = fopen(WX_FILE, "wb");
        if (f) {
            fwrite(&s_wx, sizeof s_wx, 1, f);
            fclose(f);
        }
        s_place_at = -600000;  // (a place of the IP: take it now)
    } else if (s_state == 4) {
        s_state = 0;
    }
    if (s_state || !sb_cfg.on || !clock_time_ok()) {
        return;
    }
    const int64_t now = (int64_t)time(NULL);
    const bool moved =
            s_place.ok && s_wx.at && (fabsf(s_wx.lat - s_place.lat) > 0.02f || fabsf(s_wx.lon - s_place.lon) > 0.02f);
    const bool due = !s_wx.at || moved || now - s_wx.at >= WX_EVERY;
    // only while the screen is on (somebody can look at it) and the WiFi is up; not again within WX_RETRY
    if (!s_force && (!due || !screen_on || now - s_try_at < WX_RETRY)) {
        return;
    }
    if (!wifi_ip()[0]) {
        return;
    }
    s_force = false;
    s_try_at = now;
    s_need_ip = !s_place.ok;
    s_q_lat = s_place.lat;
    s_q_lon = s_place.lon;
    if (!task_start()) {
        return;
    }
    task_go();
}

const sbw_t *sb_wx(void)
{
    return &s_wx;
}

// ================================================================ the moment
// The height of the sun over the horizon (degrees), from the NOAA formulas (as WEATHER: an error of some tenths)
static float sun_elev(float lat, float lon, int64_t t)
{
    const float d = (float)((double)(t - 946728000) / 86400.0);
    const float g = 357.529f + 0.98560028f * d, q = 280.459f + 0.98564736f * d;
    const float gr = g * HC_RAD;
    const float L = q + 1.915f * sinf(gr) + 0.020f * sinf(2 * gr);
    const float e = 23.439f - 0.00000036f * d;
    const float Lr = L * HC_RAD, er = e * HC_RAD;
    const float dec = asinf(sinf(er) * sinf(Lr));
    const float ra = atan2f(cosf(er) * sinf(Lr), cosf(Lr)) / HC_RAD;
    const float gmst = fmodf(280.46061837f + 360.98564736629f * d, 360.0f);
    const float ha = (gmst + lon - ra) * HC_RAD;
    const float la = lat * HC_RAD;
    return asinf(sinf(la) * sinf(dec) + cosf(la) * cosf(dec) * cosf(ha)) / HC_RAD;
}
static float moon_age(int64_t t)
{
    const double a = fmod((double)(t - 947182440) / 86400.0 / 29.530588853, 1.0);
    return (float)(a < 0 ? a + 1 : a);
}

// the day parts: once for each local day and place (the Hebrew date, the times of the sun and of Shabbat)
typedef struct {
    int64_t f;  // the R.D. day of the cache (0 = none)
    float plat, plon;
    hc_zman_t z[3];   // today, tomorrow, the day after
    hc_times_t d[3];  // their Shabbat and holiday times
} day_cache_t;
static day_cache_t s_dc;
static tz_rule_t s_tz;
static int64_t s_tz_at = -60000;

static int off_at(int64_t utc)
{
    return tz_offset(&s_tz, utc, NULL);
}
static void day_cache(int64_t f, int64_t utc)
{
    if (s_dc.f == f && s_dc.plat == s_place.lat && s_dc.plon == s_place.lon) {
        return;
    }
    s_dc.f = f;
    s_dc.plat = s_place.lat;
    s_dc.plon = s_place.lon;
    for (int k = 0; k < 3; k++) {
        const int om = off_at(utc + k * 86400) / 60;
        hc_zmanim(f + k, s_place.lat, s_place.lon, om, &s_dc.z[k]);
        hc_day_times(f + k, s_place.israel, &s_dc.z[k], s_place.cmin, &s_dc.d[k]);
    }
}
// the unix time of the minute m after the local 00:00 of the day k (0 = today)
static int64_t at_min(int64_t day0_utc, int k, float m)
{
    return day0_utc + (int64_t)k * 86400 + (int64_t)lroundf(m * 60.0f);
}

// the Shabbat / holiday item: CANDLES from the noon of the day before, HAVDALAH in the holy time
static void next_shabbat(sb_now_t *o, int64_t f, int64_t day0, int mins)
{
    o->shab_kind = NX_NONE;
    // in the holy time (after the candles of today, or a holy day that ends later): the first havdalah from now
    const bool holy_now = (s_dc.d[0].candles >= 0 && mins >= s_dc.d[0].candles) ||
                          (hc_holy(f, s_place.israel) && !(s_dc.d[0].havdalah >= 0 && mins >= s_dc.d[0].havdalah));
    if (holy_now) {
        for (int k = 0; k < 3; k++) {
            const float h = s_dc.d[k].havdalah;
            if (h >= 0 && at_min(day0, k, h) > o->utc) {
                o->shab_kind = NX_HAVDALAH;
                o->shab_at = at_min(day0, k, h);
                return;
            }
        }
    }
    for (int k = 0; k < 3; k++) {
        const float c = s_dc.d[k].candles;
        if (c >= 0 && !s_dc.d[k].candles_night && at_min(day0, k, c) > o->utc) {
            o->shab_kind = NX_CANDLES;
            o->shab_at = at_min(day0, k, c);
            return;
        }
    }
    // the coming Friday (more than 3 days away): its candles
    const int dow = hc_dow(f);
    const int k = (5 - dow + 7) % 7;
    hc_zman_t z;
    hc_zmanim(f + k, s_place.lat, s_place.lon, off_at(o->utc + k * 86400) / 60, &z);
    if (z.t[HZ_SUNSET] >= 0) {
        o->shab_kind = NX_CANDLES;
        o->shab_at = at_min(day0, k, z.t[HZ_SUNSET] - s_place.cmin);
    }
}

// the NEXT item: a running timer > an alarm in the next 12 h > the candles / havdalah (Friday noon to the end of
// Shabbat, or the eve of a holiday) > the next sunrise or sunset (the item is never empty when the place is known)
static void next_item(sb_now_t *o, int64_t f, int64_t day0, int mins)
{
    o->nx_kind = NX_NONE;
    ctimer_t tm[TIMER_MAX];
    const int nt = timer_get(tm, TIMER_MAX);
    const int64_t ms = esp_timer_get_time() / 1000;
    int64_t best = 0;
    for (int i = 0; i < nt; i++) {
        if (tm[i].state == TIMER_RUN && tm[i].end_ms > ms && (!best || tm[i].end_ms < best)) {
            best = tm[i].end_ms;
        }
    }
    if (best) {
        o->nx_kind = NX_TIMER;
        o->nx_at = o->utc + (best - ms + 999) / 1000;
        return;
    }
    const int64_t al = alarm_next(NULL);
    if (al > o->utc && al - o->utc <= 12 * 3600) {
        o->nx_kind = NX_ALARM;
        o->nx_at = al;
        return;
    }
    if (!s_place.ok) {
        return;
    }
    // the Shabbat window: from 12:00 of the day of the candles to the havdalah
    if (o->shab_kind == NX_HAVDALAH || (o->shab_kind == NX_CANDLES && o->shab_at - o->utc < 86400 &&
                                        (o->shab_at - day0) / 86400 == 0 && mins >= 12 * 60)) {
        o->nx_kind = o->shab_kind;
        o->nx_at = o->shab_at;
        return;
    }
    for (int k = 0; k < 2; k++) {
        const float r = s_dc.z[k].t[HZ_SUNRISE], s = s_dc.z[k].t[HZ_SUNSET];
        if (r >= 0 && at_min(day0, k, r) > o->utc) {
            o->nx_kind = NX_SUNRISE;
            o->nx_at = at_min(day0, k, r);
            return;
        }
        if (s >= 0 && at_min(day0, k, s) > o->utc) {
            o->nx_kind = NX_SUNSET;
            o->nx_at = at_min(day0, k, s);
            return;
        }
    }
}

void sb_now(sb_now_t *o)
{
    memset(o, 0, sizeof *o);
    const int64_t ms = esp_timer_get_time() / 1000;
    if (ms - s_tz_at >= 60000) {
        s_tz_at = ms;
        char z[64];
        alarm_tz_get(z, sizeof z);
        if (!tz_parse(z, &s_tz)) {
            memset(&s_tz, 0, sizeof s_tz);
        }
    }
    o->time_ok = clock_time_ok();
    o->utc = (int64_t)time(NULL);
    o->off = off_at(o->utc);
    tz_split(o->utc + o->off, &o->tm);
    o->elev = 30;
    o->moon = moon_age(o->utc);
    const int64_t local = o->utc + o->off;
    const int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    const int64_t f = days + HC_RD1970;
    const int64_t day0 = days * 86400 - o->off;  // (the local 00:00 as unix time)
    const int mins = o->tm.h * 60 + o->tm.mi;
    if (s_place.ok) {
        o->elev = sun_elev(s_place.lat, s_place.lon, o->utc);
        day_cache(f, o->utc);
    } else {
        o->elev = 60.0f * sinf(((float)mins / 1440.0f - 0.25f) * 6.2831853f) - 5.0f;  // (no place: by the clock)
    }
    // the Hebrew day starts at the sunset
    const bool after = s_place.ok && s_dc.z[0].t[HZ_SUNSET] >= 0 && mins >= s_dc.z[0].t[HZ_SUNSET];
    hc_heb(f + (after ? 1 : 0), &o->hy, &o->hm, &o->hd);
    o->hleap = hc_leap(o->hy);
    if (s_place.ok) {
        next_shabbat(o, f, day0, mins);
    }
    next_item(o, f, day0, mins);
}

int sb_data_log(char *b, int n)
{
    const int64_t now = (int64_t)time(NULL);
    return snprintf(b, n, "place %s (%.3f, %.3f, %s, candles %d min); weather %s, %.1f C, code %d, %d s old, state %d",
                    s_place.ok ? s_place.name : "none", (double)s_place.lat, (double)s_place.lon,
                    s_place.israel ? "Israel" : "abroad", s_place.cmin, s_wx.at ? "yes" : "none", (double)s_wx.t,
                    s_wx.code, s_wx.at ? (int)(now - s_wx.at) : -1, s_state);
}

// the Hebrew date as text: "כ״ב בחשוון" (+ the year: "תשפ״ז")
void sb_hdate(const sb_now_t *n, char *o, int max, bool year)
{
    static const char *const M[14] = {"",     "ניסן",  "אייר", "סיוון", "תמוז", "אב",  "אלול",
                                      "תשרי", "חשוון", "כסלו", "טבת",   "שבט",  "אדר", "אדר ב׳"};
    const char *m = n->hm == 12 && n->hleap ? "אדר א׳" : M[n->hm >= 1 && n->hm <= 13 ? n->hm : 0];
    char d[24], y[24];
    hc_gematria(n->hd, d, sizeof d);
    hc_gematria(n->hy % 1000, y, sizeof y);
    if (year) {
        snprintf(o, max, "%s ב%s %s", d, m, y);
    } else {
        snprintf(o, max, "%s ב%s", d, m);
    }
}
