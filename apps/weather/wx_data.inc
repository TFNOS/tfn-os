// WEATHER: the data (included by app_weather.c): the places, the forecast of each place (Open-Meteo: no key, one
// request of about 6 KB), the air quality, the place of the IP (ipwho.is), the search of a city (the geocoding API).
// The task: the fetch task "wxnet" (core 0) does the network and the JSON (never a file); the app task reads and
// writes the cache file.
// The memory: the file statics; the answer buffer (PSRAM) comes with the fetch task and goes at task_stop.
#include "wx_roots.inc"  // (the roots of ipwho.is: GTS Root R1/R4, ISRG Root X1/X2)
#define NPLACE_MAX 8
#define NH         48  // the hours from the start of this hour
#define ND         10  // the days
#define NM         16  // the quarters of an hour (4 h of rain)
#define WX_FILE_V  3
#ifdef ESP_PLATFORM
#define WX_DIR "/data/weather"
#else
#define WX_DIR "sandbox/weather"  // (the PC test)
#endif

typedef struct {
    float lat, lon;
    uint8_t here;   // 1 = MY PLACE (from the IP address)
    char name[48];  // in the language of the search (UTF-8)
    char sub[48];   // the region and the country
} place_t;

typedef struct {
    uint32_t ver;
    float lat, lon;   // the place of this data (the key in the cache)
    int64_t fetched;  // unix s of the fetch (0 = no data)
    int64_t now;      // current.time of the API
    int32_t off;      // utc_offset_seconds of the place
    float t, feels, hum, dew, wind, wdir, gust, press, cloud, prec, vis, uv;
    int16_t code;
    uint8_t day;
    int64_t h0;
    int16_t nh;
    float ht[NH], hf[NH], hpop[NH], hpr[NH], hw[NH], hwd[NH], hcl[NH], huv[NH], hhum[NH];
    uint8_t hcode[NH], hday[NH];
    int64_t m0;
    int16_t nm;
    float mpr[NM];
    int16_t nd;
    int64_t dt[ND], drise[ND], dset[ND];
    float dmax[ND], dmin[ND], dpop[ND], dpr[ND], duv[ND], dwind[ND];
    uint8_t dcode[ND];
    int16_t aqi;  // US AQI (-1 = none)
    float pm25, pm10;
} wx_t;

static place_t s_pl[NPLACE_MAX];
static int s_npl;
static wx_t s_wx[NPLACE_MAX];  // (in the order of s_pl)

// ---------------------------------------------------------------- the time
// The board clock comes from the network (SNTP). Before it has the time, the time of the last answer of the API
// and the time since it give the time.
static int64_t s_api_unix, s_api_ms;
static int64_t unix_now(void)
{
    int64_t t = (int64_t)time(NULL);
    if (t > 1700000000) {
        return t;
    }
    if (s_api_unix) {
        return s_api_unix + (esp_timer_get_time() / 1000 - s_api_ms) / 1000;
    }
    return 0;
}
// a unix time as the hour and the minute of a place (off = its offset from UTC in seconds)
static void hm_of(int64_t t, int off, int *h, int *m)
{
    int64_t s = t + off;
    int64_t d = s % 86400;
    if (d < 0) {
        d += 86400;
    }
    *h = (int)(d / 3600);
    *m = (int)(d % 3600 / 60);
}
static int wday_of(int64_t t, int off)  // 0 = Sunday
{
    int64_t days = (t + off) / 86400;
    if ((t + off) < 0 && (t + off) % 86400) {
        days--;
    }
    int w = (int)((days + 4) % 7);  // (1970-01-01 was a Thursday)
    return w < 0 ? w + 7 : w;
}

// ---------------------------------------------------------------- the sun and the moon
// The height of the sun over the horizon (degrees), from the NOAA formulas (an error of some tenths of a degree)
static float sun_elev(float lat, float lon, int64_t t)
{
    float d = (float)((double)(t - 946728000) / 86400.0);  // the days from 2000-01-01 12:00 UTC
    float g = 357.529f + 0.98560028f * d, q = 280.459f + 0.98564736f * d;
    float gr = g * 0.0174533f;
    float L = q + 1.915f * sinf(gr) + 0.020f * sinf(2 * gr);
    float e = 23.439f - 0.00000036f * d;
    float Lr = L * 0.0174533f, er = e * 0.0174533f;
    float dec = asinf(sinf(er) * sinf(Lr));
    float ra = atan2f(cosf(er) * sinf(Lr), cosf(Lr)) / 0.0174533f;
    float gmst = fmodf(280.46061837f + 360.98564736629f * d, 360.0f);
    float ha = (gmst + lon - ra) * 0.0174533f;
    float la = lat * 0.0174533f;
    return asinf(sinf(la) * sinf(dec) + cosf(la) * cosf(dec) * cosf(ha)) / 0.0174533f;
}
// the age of the moon: 0..1 (0 = new, 0.5 = full)
static float moon_age(int64_t t)
{
    double days = (double)(t - 947182440) / 86400.0;  // a new moon: 2000-01-06 18:14 UTC
    double a = fmod(days / 29.530588853, 1.0);
    return (float)(a < 0 ? a + 1 : a);
}
static int moon_phase8(float age)
{
    return (int)(age * 8 + 0.5f) & 7;
}

// ---------------------------------------------------------------- the files of the places and the cache
static bool places_load(void)
{
    s_npl = 0;
    FILE *f = fopen(WX_DIR "/places.txt", "r");
    if (!f) {
        return false;
    }
    static char txt[NPLACE_MAX * 220 + 1];  // (no fgetc in the ABI: the whole file, then its lines)
    int len = (int)fread(txt, 1, sizeof txt - 1, f);
    txt[len > 0 ? len : 0] = 0;
    char *next = txt;
    while (next && *next) {
        char *line = next;
        next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        if (s_npl < NPLACE_MAX && line[0]) {
            // lat|lon|here|name|sub
            place_t *p = &s_pl[s_npl];
            memset(p, 0, sizeof *p);
            char *a = line, *b;
            int k = 0;
            while (k < 5 && a) {
                b = strchr(a, '|');
                if (b) {
                    *b = 0;
                }
                if (k == 0) {
                    p->lat = strtof(a, NULL);
                } else if (k == 1) {
                    p->lon = strtof(a, NULL);
                } else if (k == 2) {
                    p->here = (uint8_t)atoi(a);
                } else if (k == 3) {
                    snprintf(p->name, sizeof p->name, "%s", a);
                } else {
                    snprintf(p->sub, sizeof p->sub, "%s", a);
                }
                a = b ? b + 1 : NULL;
                k++;
            }
            if (k >= 4 && p->name[0]) {
                s_npl++;
            }
        }
    }
    fclose(f);
    return s_npl > 0;
}

static void places_save(void)
{
    APP_MKDIR(WX_DIR);
    FILE *f = fopen(WX_DIR "/places.txt", "w");
    if (!f) {
        return;
    }
    for (int i = 0; i < s_npl; i++) {
        char line[200];
        int n = snprintf(line, sizeof line, "%.4f|%.4f|%d|%s|%s\n", (double)s_pl[i].lat, (double)s_pl[i].lon,
                         s_pl[i].here, s_pl[i].name, s_pl[i].sub);
        fwrite(line, 1, (size_t)n, f);
    }
    fclose(f);
}

// the cache: the data of each place (the key: its lat and lon)
static void cache_save(void)
{
    APP_MKDIR(WX_DIR);
    FILE *f = fopen(WX_DIR "/cache.bin", "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < s_npl; i++) {
        if (s_wx[i].fetched) {
            fwrite(&s_wx[i], sizeof s_wx[i], 1, f);
        }
    }
    fclose(f);
}

static void cache_load(void)
{
    memset(s_wx, 0, sizeof s_wx);
    FILE *f = fopen(WX_DIR "/cache.bin", "rb");
    if (!f) {
        return;
    }
    static wx_t w;
    while (fread(&w, sizeof w, 1, f) == 1) {
        if (w.ver != WX_FILE_V) {
            break;
        }
        for (int i = 0; i < s_npl; i++) {
            if (fabsf(w.lat - s_pl[i].lat) < 0.01f && fabsf(w.lon - s_pl[i].lon) < 0.01f) {
                s_wx[i] = w;
            }
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------- the fetch task
enum {
    RQ_NONE,
    RQ_FORECAST,
    RQ_IP,
    RQ_SEARCH
};
enum {
    RS_IDLE,
    RS_BUSY,
    RS_OK,
    RS_FAIL
};
#define NRES 10
typedef struct {
    char name[48], sub[48];
    float lat, lon;
} geo_t;
static struct {
    volatile int kind, gen, done_gen, status;
    int place;  // RQ_FORECAST: the index of the place
    float lat, lon;
    char query[64];
    bool he;
    // the results (the task writes them; the app reads them after done_gen == gen)
    wx_t wx;
    geo_t geo[NRES];
    int ngeo;
    place_t ip;
    int err;
} s_rq;
static volatile bool s_task_run, s_task_alive, s_rq_cancel;
static TaskHandle_t s_task;
static char *s_body;  // the answer of the API (PSRAM)
#define BODY_MAX (48 * 1024)

static void url_enc(const char *in, char *out, int max)
{
    static const char HEX[] = "0123456789ABCDEF";
    int n = 0;
    for (const uint8_t *p = (const uint8_t *)in; *p && n < max - 4; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
            *p == '.' || *p == '_') {
            out[n++] = (char)*p;
        } else if (*p == ' ') {
            out[n++] = '%';
            out[n++] = '2';
            out[n++] = '0';
        } else {
            out[n++] = '%';
            out[n++] = HEX[*p >> 4];
            out[n++] = HEX[*p & 15];
        }
    }
    out[n] = 0;
}

static int fnums(tn_js_t o, const char *k, float *out, int max)
{
    return tn_js_nums(tn_js_key(o, k), out, max);
}

static bool parse_forecast(const char *b, int n, wx_t *w)
{
    tn_js_t r = tn_js_root(b, n);
    if (!r.p || *r.p != '{') {
        return false;
    }
    tn_js_t cur = tn_js_key(r, "current"), hr = tn_js_key(r, "hourly"), dy = tn_js_key(r, "daily");
    tn_js_t mq = tn_js_key(r, "minutely_15");
    if (!cur.p || !hr.p || !dy.p) {
        return false;
    }
    w->off = (int32_t)tn_js_num(tn_js_key(r, "utc_offset_seconds"), 0);
    w->now = (int64_t)tn_js_num(tn_js_key(cur, "time"), 0);
    w->t = (float)tn_js_num(tn_js_key(cur, "temperature_2m"), NAN);
    w->feels = (float)tn_js_num(tn_js_key(cur, "apparent_temperature"), NAN);
    w->hum = (float)tn_js_num(tn_js_key(cur, "relative_humidity_2m"), NAN);
    w->dew = (float)tn_js_num(tn_js_key(cur, "dew_point_2m"), NAN);
    w->wind = (float)tn_js_num(tn_js_key(cur, "wind_speed_10m"), NAN);
    w->wdir = (float)tn_js_num(tn_js_key(cur, "wind_direction_10m"), NAN);
    w->gust = (float)tn_js_num(tn_js_key(cur, "wind_gusts_10m"), NAN);
    w->press = (float)tn_js_num(tn_js_key(cur, "pressure_msl"), NAN);
    w->cloud = (float)tn_js_num(tn_js_key(cur, "cloud_cover"), NAN);
    w->prec = (float)tn_js_num(tn_js_key(cur, "precipitation"), 0);
    w->vis = (float)tn_js_num(tn_js_key(cur, "visibility"), NAN);
    w->uv = (float)tn_js_num(tn_js_key(cur, "uv_index"), NAN);
    w->code = (int16_t)tn_js_num(tn_js_key(cur, "weather_code"), 0);
    w->day = (uint8_t)tn_js_num(tn_js_key(cur, "is_day"), 1);
    static int64_t tt[NH > ND ? NH : ND];
    static float tmp[NH];
    w->nh = (int16_t)tn_js_ints(tn_js_key(hr, "time"), tt, NH);
    w->h0 = w->nh ? tt[0] : 0;
    fnums(hr, "temperature_2m", w->ht, NH);
    fnums(hr, "apparent_temperature", w->hf, NH);
    fnums(hr, "precipitation_probability", w->hpop, NH);
    fnums(hr, "precipitation", w->hpr, NH);
    fnums(hr, "wind_speed_10m", w->hw, NH);
    fnums(hr, "wind_direction_10m", w->hwd, NH);
    fnums(hr, "cloud_cover", w->hcl, NH);
    fnums(hr, "uv_index", w->huv, NH);
    fnums(hr, "relative_humidity_2m", w->hhum, NH);
    int k = fnums(hr, "weather_code", tmp, NH);
    for (int i = 0; i < k; i++) {
        w->hcode[i] = (uint8_t)(isnan(tmp[i]) ? 0 : tmp[i]);
    }
    k = fnums(hr, "is_day", tmp, NH);
    for (int i = 0; i < k; i++) {
        w->hday[i] = (uint8_t)(isnan(tmp[i]) ? 1 : tmp[i]);
    }
    w->nm = 0;
    if (mq.p) {
        w->nm = (int16_t)tn_js_ints(tn_js_key(mq, "time"), tt, NM);
        w->m0 = w->nm ? tt[0] : 0;
        fnums(mq, "precipitation", w->mpr, NM);
    }
    w->nd = (int16_t)tn_js_ints(tn_js_key(dy, "time"), w->dt, ND);
    tn_js_ints(tn_js_key(dy, "sunrise"), w->drise, ND);
    tn_js_ints(tn_js_key(dy, "sunset"), w->dset, ND);
    fnums(dy, "temperature_2m_max", w->dmax, ND);
    fnums(dy, "temperature_2m_min", w->dmin, ND);
    fnums(dy, "precipitation_probability_max", w->dpop, ND);
    fnums(dy, "precipitation_sum", w->dpr, ND);
    fnums(dy, "uv_index_max", w->duv, ND);
    fnums(dy, "wind_speed_10m_max", w->dwind, ND);
    k = fnums(dy, "weather_code", tmp, ND);
    for (int i = 0; i < k; i++) {
        w->dcode[i] = (uint8_t)(isnan(tmp[i]) ? 0 : tmp[i]);
    }
    return w->nh > 0 && w->nd > 0 && !isnan(w->t);
}

static void do_forecast(void)
{
    char url[900];
    snprintf(
            url, sizeof url,
            "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
            "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,"
            "cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,visibility,uv_index,dew_point_2m"
            "&hourly=temperature_2m,apparent_temperature,precipitation_probability,precipitation,weather_code,"
            "cloud_cover,wind_speed_10m,wind_direction_10m,uv_index,is_day,relative_humidity_2m"
            "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,uv_index_max,precipitation_sum,"
            "precipitation_probability_max,wind_speed_10m_max&minutely_15=precipitation&timezone=auto"
            "&timeformat=unixtime&forecast_days=10&forecast_hours=48&forecast_minutely_15=16",
            (double)s_rq.lat, (double)s_rq.lon);
    int st = 0;
    int n = tn_get(url, s_body, BODY_MAX, 10000, &s_rq_cancel, &st);
    memset(&s_rq.wx, 0, sizeof s_rq.wx);
    bool ok = n > 0 && st == 200 && parse_forecast(s_body, n, &s_rq.wx);
#ifdef ESP_PLATFORM
    ESP_LOGI("weather", "forecast %.2f %.2f: %d bytes, status %d, %s", (double)s_rq.lat, (double)s_rq.lon, n, st,
             ok ? "ok" : "FAILED");
#endif
    if (!ok) {
        s_rq.err = n < 0 ? n : TN_EHTTP;
        s_rq.status = RS_FAIL;
        return;
    }
    s_rq.wx.ver = WX_FILE_V;
    s_rq.wx.lat = s_rq.lat;
    s_rq.wx.lon = s_rq.lon;
    s_rq.wx.aqi = -1;
    // the air (a second, small request; no air data is no error)
    snprintf(url, sizeof url,
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&current=us_aqi,pm2_5,pm10&timeformat=unixtime",
             (double)s_rq.lat, (double)s_rq.lon);
    n = tn_get(url, s_body, BODY_MAX, 8000, &s_rq_cancel, &st);
    if (n > 0 && st == 200) {
        tn_js_t c = tn_js_key(tn_js_root(s_body, n), "current");
        tn_js_t a = tn_js_key(c, "us_aqi");
        if (tn_js_ok(a)) {
            s_rq.wx.aqi = (int16_t)tn_js_num(a, -1);
        }
        s_rq.wx.pm25 = (float)tn_js_num(tn_js_key(c, "pm2_5"), NAN);
        s_rq.wx.pm10 = (float)tn_js_num(tn_js_key(c, "pm10"), NAN);
    }
    s_rq.status = RS_OK;
}

// geocoding: the places with this name (in the language of the app)
static int geocode(const char *q, bool he, geo_t *out, int max)
{
    char enc[200], url[400];
    url_enc(q, enc, sizeof enc);
    snprintf(url, sizeof url, "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=%s&format=json",
             enc, max, he ? "he" : "en");
    int st = 0;
    int n = tn_get(url, s_body, BODY_MAX, 10000, &s_rq_cancel, &st);
    if (n <= 0 || st != 200) {
        return n < 0 ? n : TN_EHTTP;
    }
    tn_js_t res = tn_js_key(tn_js_root(s_body, n), "results");
    int k = 0;
    for (tn_js_t v = tn_js_first(res); v.p && k < max; v = tn_js_next(res, v)) {
        geo_t *g = &out[k];
        memset(g, 0, sizeof *g);
        tn_js_str(tn_js_key(v, "name"), g->name, sizeof g->name);
        char a1[48] = "", cc[48] = "";
        tn_js_str(tn_js_key(v, "admin1"), a1, sizeof a1);
        tn_js_str(tn_js_key(v, "country"), cc, sizeof cc);
        if (a1[0] && cc[0] && strcmp(a1, g->name)) {
            snprintf(g->sub, sizeof g->sub, "%.22s, %.22s", a1, cc);
        } else {
            snprintf(g->sub, sizeof g->sub, "%s", cc);
        }
        g->lat = (float)tn_js_num(tn_js_key(v, "latitude"), NAN);
        g->lon = (float)tn_js_num(tn_js_key(v, "longitude"), NAN);
        if (g->name[0] && !isnan(g->lat) && !isnan(g->lon)) {
            k++;
        }
    }
    return k;
}

// The place of the IP address: the free endpoint of ipwhois.io (https://ipwho.is). It needs no key, it uses HTTPS,
// and its free plan allows commercial use (https://ipwhois.io/documentation: 1000 requests a day for each client
// IP address). The app sends one request at each open. The answer: "success" (true or false), the city, the region,
// the country, "latitude", "longitude". The TLS link uses the roots of wx_roots.inc (the host is not in the shared
// list of tfnet).
static void do_ip(void)
{
    int st = 0;
    int n = tn_fetch_ca(WX_ROOTS, "GET", "https://ipwho.is/?fields=success,city,region,country,latitude,longitude",
                        NULL, NULL, 0, s_body, BODY_MAX, 8000, &s_rq_cancel, &st);
    place_t *p = &s_rq.ip;
    memset(p, 0, sizeof *p);
    if (n > 0 && st == 200) {
        tn_js_t r = tn_js_root(s_body, n);
        tn_js_t ok = tn_js_key(r, "success");
        if (ok.p && ok.e - ok.p >= 4 && !strncmp(ok.p, "true", 4)) {
            tn_js_str(tn_js_key(r, "city"), p->name, sizeof p->name);
            char c[48] = "";
            tn_js_str(tn_js_key(r, "country"), c, sizeof c);
            snprintf(p->sub, sizeof p->sub, "%s", c);
            p->lat = (float)tn_js_num(tn_js_key(r, "latitude"), NAN);
            p->lon = (float)tn_js_num(tn_js_key(r, "longitude"), NAN);
            p->here = 1;
        }
    }
#ifdef ESP_PLATFORM
    ESP_LOGI("weather", "the place of the IP: %d bytes, status %d, %s", n, st, p->name[0] ? "ok" : "FAILED");
#endif
    if (!p->name[0] || isnan(p->lat)) {
        s_rq.status = RS_FAIL;
        s_rq.err = n < 0 ? n : TN_EHTTP;
        return;
    }
    // the name in Hebrew: the first place of this name in the same country (the geocoding API)
    if (s_rq.he) {
        static geo_t g[3];
        int k = geocode(p->name, true, g, 3);
        for (int i = 0; i < k; i++) {
            if (fabsf(g[i].lat - p->lat) < 0.3f && fabsf(g[i].lon - p->lon) < 0.3f) {
                snprintf(p->name, sizeof p->name, "%s", g[i].name);
                snprintf(p->sub, sizeof p->sub, "%s", g[i].sub);
                break;
            }
        }
    }
    s_rq.status = RS_OK;
}

static void fetch_task(void *arg)
{
    (void)arg;
    int seen = 0;
    while (s_task_run) {
        if (s_rq.gen == seen) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        seen = s_rq.gen;
        s_rq_cancel = false;
        s_rq.status = RS_BUSY;
        s_rq.err = 0;
        if (s_rq.kind == RQ_FORECAST) {
            do_forecast();
        } else if (s_rq.kind == RQ_IP) {
            do_ip();
        } else if (s_rq.kind == RQ_SEARCH) {
            int k = geocode(s_rq.query, s_rq.he, s_rq.geo, NRES);
            s_rq.ngeo = k > 0 ? k : 0;
            s_rq.err = k < 0 ? k : 0;
            s_rq.status = k >= 0 ? RS_OK : RS_FAIL;
        }
        s_rq.done_gen = seen;
    }
    s_task_alive = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // (leave() deletes the task: vTaskDeleteWithCaps)
    }
}

// a request to the task (the app task); false = the task is busy with another request
static bool rq_busy(void)
{
    return s_rq.done_gen != s_rq.gen;
}
static bool rq_start(int kind)
{
    if (rq_busy() || !s_task_alive) {
        return false;
    }
    s_rq.kind = kind;
    s_rq.gen++;
    return true;
}

static bool task_start(void)
{
    if (!s_body) {
        s_body = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_body) {
        return false;
    }
    s_rq.gen = s_rq.done_gen = 0;
    s_task_run = true;
    s_task_alive = true;
#ifdef ESP_PLATFORM
    // the stack in PSRAM (this task never writes the flash: the app task writes the files)
    if (xTaskCreatePinnedToCoreWithCaps(fetch_task, "wxnet", 12288, NULL, 3, &s_task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task_alive = false;
        return false;
    }
#endif
    return true;
}

static void task_stop(void)
{
    s_rq_cancel = true;
    s_task_run = false;
#ifdef ESP_PLATFORM
    for (int i = 0; i < 300 && s_task_alive; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_task && !s_task_alive) {
        vTaskDeleteWithCaps(s_task);
    }
#endif
    s_task = NULL;
    s_task_alive = false;
    free(s_body);
    s_body = NULL;
}
