// THE ALARMS AND THE CLOCK OF THE BOARD (see alarm.h).
// One small task: each 15 s (each 1 s near an alarm, a snooze or a ring) it finds the local time (the home zone of
// CLOCK: stocks_time.c reads the POSIX TZ text) and rings the alarm of that minute. A ring: the screen wakes, the CLOCK
// app opens with the argument "ring" (tfn_find: CLOCK is an app file), and the alarm sound plays each 2.5 s (audio_alert:
// also when the board is muted), louder for 20 s, for 10 minutes at most. With no armed alarm the board can power
// off as before; an armed alarm in the next 24 h keeps the board on (power_alarm_hold).
#include "tfn_util.h"
#include "alarm.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "app.h"
#include "audio.h"
#include "board.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "notify.h"
#include "nvs.h"
#include "power.h"
#include "tz.h"
#include "tfn_app.h"
#include "settings.h"
#include "standby_priv.h"
#include "tfn_speech.h"
#include "driver/i2c_master.h"

static const char *TAG = "alarm";

static SemaphoreHandle_t s_lock;
static alarm_t s_al[ALARM_MAX];
static int s_n;
static char s_tz[64] = "IST-2IDT,M3.4.4/26,M10.5.0";  // (the default zone: Israel)
static tz_rule_t s_z;
static int64_t s_snooze[ALARM_MAX];  // epoch s of a snooze ring (0 = none)
static int32_t s_fired[ALARM_MAX];   // the local minute (since 1970) of the last ring: one ring a minute
static volatile int s_ring = -1;
static int64_t s_ring_t0, s_ring_last;  // ms
static volatile bool s_wake;            // the task must look now (a new list, a stop)
static int s_src;                       // clock_source()
static volatile int s_preview = -1, s_preview_vol;
static ctimer_t s_tm[TIMER_MAX];  // the timers (in RAM only: they count on the clock since the start)

// ================================================================ the sounds (each one about 2 s)
static const tune_note_t SND_CLASSIC[] = {
        {0, 90, 96, 0, W_SQUARE, 70},    {150, 90, 96, 0, W_SQUARE, 70},  {300, 90, 96, 0, W_SQUARE, 70},
        {450, 90, 96, 0, W_SQUARE, 70},  {1000, 90, 96, 0, W_SQUARE, 70}, {1150, 90, 96, 0, W_SQUARE, 70},
        {1300, 90, 96, 0, W_SQUARE, 70}, {1450, 90, 96, 0, W_SQUARE, 70},
};
static const tune_note_t SND_CHIME[] = {
        {0, 600, 84, 0, W_SINE | TUNE_HOLD, 80},   {220, 600, 88, 0, W_SINE | TUNE_HOLD, 75},
        {440, 600, 91, 0, W_SINE | TUNE_HOLD, 70}, {660, 900, 96, 0, W_SINE | TUNE_HOLD, 80},
        {1300, 500, 91, 0, W_TRI | TUNE_HOLD, 50}, {1500, 700, 84, 0, W_TRI | TUNE_HOLD, 50},
};
static const tune_note_t SND_RISING[] = {
        {0, 160, 72, 0, W_TRI, 60},
        {180, 160, 76, 0, W_TRI, 65},
        {360, 160, 79, 0, W_TRI, 70},
        {540, 160, 84, 0, W_TRI, 75},
        {720, 160, 88, 0, W_TRI, 80},
        {900, 160, 91, 0, W_TRI, 85},
        {1080, 400, 96, 0, W_TRI | TUNE_HOLD, 90},
};
static const tune_note_t SND_DIGITAL[] = {
        {0, 120, 93, 0, W_PULSE, 70},    {130, 120, 88, 0, W_PULSE, 70},  {260, 120, 93, 0, W_PULSE, 70},
        {390, 120, 88, 0, W_PULSE, 70},  {800, 120, 93, 0, W_PULSE, 70},  {930, 120, 88, 0, W_PULSE, 70},
        {1060, 120, 93, 0, W_PULSE, 70}, {1190, 120, 88, 0, W_PULSE, 70},
};
static const struct {
    const tune_note_t *t;
    int n;
} SND[ALARM_SOUNDS] = {
        {SND_CLASSIC, sizeof SND_CLASSIC / sizeof SND_CLASSIC[0]},
        {SND_CHIME, sizeof SND_CHIME / sizeof SND_CHIME[0]},
        {SND_RISING, sizeof SND_RISING / sizeof SND_RISING[0]},
        {SND_DIGITAL, sizeof SND_DIGITAL / sizeof SND_DIGITAL[0]},
};

// ================================================================ the clock chip (UTC in the chip)
// PCF85063 (the 3.49, the AMOLED): the time at 0x04; PCF8563 / BM8563 (the Watcher, the CoreS3): at 0x02. Both: the
// seconds (bit 7 = the oscillator stopped: the time is not valid), the minutes, the hours (24 h), the day, the day of
// the week, the month (PCF8563: bit 7 = the century), the year (00 .. 99), all BCD.
#define RTC_REG  BOARD_RTC_REG  // (-1: the RX8130 of the Tab5: no driver yet)
#define RTC_ADDR 0x51

static int bcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 15);
}
static uint8_t tobcd(int v)
{
    return (uint8_t)((v / 10) << 4 | (v % 10));
}

static bool rtc_io(bool wr, uint8_t *b)
{
#if RTC_REG < 0
    (void)wr;
    (void)b;
    return false;
#else
    i2c_master_bus_handle_t bus;
    if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK) {
        return false;
    }
    i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_ADDR, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t d;
    if (i2c_master_bus_add_device(bus, &dc, &d) != ESP_OK) {
        return false;
    }
    uint8_t reg = RTC_REG, w[8];
    esp_err_t e;
    if (wr) {
        w[0] = reg;
        memcpy(w + 1, b, 7);
        e = i2c_master_transmit(d, w, 8, 50);
    } else {
        e = i2c_master_transmit_receive(d, &reg, 1, b, 7, 50);
    }
    i2c_master_bus_rm_device(d);
    return e == ESP_OK;
#endif
}

static int64_t rtc_read(void)
{
    uint8_t b[7];
    if (!rtc_io(false, b) || (b[0] & 0x80)) {
        return 0;  // (the oscillator stopped: no valid time)
    }
    const int y = 2000 + bcd(b[6]), mo = bcd(b[5] & 0x1F), d = bcd(b[3] & 0x3F);
    const int h = bcd(b[2] & 0x3F), mi = bcd(b[1] & 0x7F), s = bcd(b[0] & 0x7F);
    if (y < 2025 || y > 2060 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) {
        return 0;
    }
    return tz_days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

static void rtc_write(int64_t t)
{
    tz_tm_t m;
    tz_split(t, &m);
    uint8_t b[7] = {tobcd(m.s), tobcd(m.mi), tobcd(m.h), tobcd(m.d), (uint8_t)m.wd, tobcd(m.mo), tobcd(m.y % 100)};
    if (rtc_io(true, b)) {
        ESP_LOGI(TAG, "the clock chip has the time of the network");
    }
}

// ================================================================ the time

bool clock_time_ok(void)
{
    return time(NULL) > 1735689600;
}  // (true when the clock is set: its time is after the start of 2025)
int clock_source(void)
{
    return clock_time_ok() ? (s_src ? s_src : 2) : 0;
}

void clock_sync(void)
{
    if (esp_sntp_enabled()) {
        return;  // (the VPN or an earlier call started it)
    }
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&sc) == ESP_OK) {
        ESP_LOGI(TAG, "SNTP started");
    }
}

// ================================================================ the list

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("alarm", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "list", s_al, sizeof(alarm_t) * (size_t)s_n);
    nvs_set_str(h, "tz", s_tz);
    nvs_commit(h);
    nvs_close(h);
}

// The home zone from the key "tz" of an NVS namespace: true = a good zone is there
static bool load_tz(const char *ns)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char tz[64];
    size_t n = sizeof tz;
    bool ok = false;
    if (nvs_get_str(h, "tz", tz, &n) == ESP_OK) {
        tz_rule_t z;
        if (tz_parse(tz, &z)) {
            snprintf(s_tz, sizeof s_tz, "%s", tz);
            ok = true;
        }
    }
    nvs_close(h);
    return ok;
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open("alarm", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof s_al;
        if (nvs_get_blob(h, "list", s_al, &n) == ESP_OK) {
            s_n = (int)(n / sizeof(alarm_t));
        }
        nvs_close(h);
    }
    // The home zone is the one zone of the board (CLOCK, STOCKS, DO NOT DISTURB). An older firmware kept a second
    // zone for STOCKS: a board that has no home zone yet takes that one.
    if (!load_tz("alarm")) {
        load_tz("stocks");
    }
}

int alarm_get(alarm_t *out, int max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int n = s_n < max ? s_n : max;
    memcpy(out, s_al, sizeof(alarm_t) * (size_t)n);
    xSemaphoreGive(s_lock);
    return n;
}

bool alarm_set(const alarm_t *a, int n)
{
    if (n < 0 || n > ALARM_MAX) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_al, a, sizeof(alarm_t) * (size_t)n);
    s_n = n;
    for (int i = 0; i < n; i++) {
        s_al[i].hour %= 24;
        s_al[i].min %= 60;
        s_al[i].days &= 0x7F;
        s_al[i].sound %= ALARM_SOUNDS;
        if (s_al[i].vol < 10 || s_al[i].vol > 100) {
            s_al[i].vol = 70;
        }
        if (s_al[i].snooze < 1 || s_al[i].snooze > 30) {
            s_al[i].snooze = 9;
        }
        s_al[i].label[sizeof s_al[i].label - 1] = 0;
        s_snooze[i] = 0;
    }
    save();
    xSemaphoreGive(s_lock);
    s_wake = true;
    for (int i = 0; i < n; i++) {
        if (a[i].on) {
            clock_sync();
        }
    }
    return true;
}

void alarm_tz_get(char *out, int max)
{
    snprintf(out, (size_t)max, "%s", s_tz);
}

bool alarm_tz_set(const char *p)
{
    tz_rule_t z;
    if (!p || strlen(p) >= sizeof s_tz || !tz_parse(p, &z)) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_tz, sizeof s_tz, "%s", p);
    s_z = z;
    save();
    xSemaphoreGive(s_lock);
    s_wake = true;
    return true;
}

int timer_get(ctimer_t *out, int max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int n = max < TIMER_MAX ? max : TIMER_MAX;
    memcpy(out, s_tm, sizeof(ctimer_t) * (size_t)n);
    xSemaphoreGive(s_lock);
    return n;
}

bool timer_set(const ctimer_t *t, int n)
{
    if (n < 0 || n > TIMER_MAX) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_tm, 0, sizeof s_tm);
    memcpy(s_tm, t, sizeof(ctimer_t) * (size_t)n);
    for (int i = 0; i < TIMER_MAX; i++) {
        ctimer_t *x = &s_tm[i];
        if (x->state > TIMER_DONE) {
            x->state = TIMER_FREE;
        }
        x->sound %= ALARM_SOUNDS;
        if (x->vol < 10 || x->vol > 100) {
            x->vol = 70;
        }
        x->label[sizeof x->label - 1] = 0;
    }
    xSemaphoreGive(s_lock);
    s_wake = true;
    return true;
}

int alarm_ringing(void)
{
    return s_ring;
}

void alarm_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int i = s_ring;
    if (i >= 0 && i < s_n && s_al[i].days == 0 && !s_snooze[i]) {
        s_al[i].on = 0;  // (a once alarm turns off)
        save();
    }
    s_ring = -1;
    xSemaphoreGive(s_lock);
    audio_tune_stop();
    s_wake = true;
    ESP_LOGI(TAG, "stop");
}

void alarm_snooze(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int i = s_ring;
    if (i >= 0 && i < s_n) {
        s_snooze[i] = time(NULL) + (int64_t)s_al[i].snooze * 60;
    }
    if (i >= ALARM_MAX && i < ALARM_MAX + TIMER_MAX) {  // (a timer: one minute more)
        s_tm[i - ALARM_MAX].state = TIMER_RUN;
        s_tm[i - ALARM_MAX].end_ms = esp_timer_get_time() / 1000 + 60000;
    }
    s_ring = -1;
    xSemaphoreGive(s_lock);
    audio_tune_stop();
    s_wake = true;
    ESP_LOGI(TAG, "snooze");
}

void alarm_preview(int sound, int vol)
{
    s_preview_vol = vol < 10 ? 10 : vol > 100 ? 100 : vol;
    s_preview = sound < 0 ? 0 : sound % ALARM_SOUNDS;
    s_wake = true;
}

// the next ring of alarm i after the local time lt (local seconds): its local time, or 0
static int64_t next_local(const alarm_t *a, int64_t lt)
{
    const int64_t day = lt / 86400;
    for (int k = 0; k <= 7; k++) {
        const int64_t t = (day + k) * 86400 + a->hour * 3600 + a->min * 60;
        if (t <= lt) {
            continue;
        }
        const int wd = (int)((day + k + 4) % 7);  // (1970-01-01 was a Thursday)
        if (a->days == 0 || (a->days >> wd) & 1) {
            return t;
        }
    }
    return 0;
}

int64_t alarm_next(int *idx)
{
    if (!clock_time_ok()) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t now = time(NULL), off = tz_offset(&s_z, now, NULL);
    int64_t best = 0;
    int bi = -1;
    for (int i = 0; i < s_n; i++) {
        int64_t t = 0;
        if (s_snooze[i]) {
            t = s_snooze[i];
        } else if (s_al[i].on) {
            const int64_t l = next_local(&s_al[i], now + off);
            if (l) {
                t = l - tz_offset(&s_z, l - off, NULL);  // (back to UTC with the offset of that time)
            }
        }
        if (t && (!best || t < best)) {
            best = t;
            bi = i;
        }
    }
    xSemaphoreGive(s_lock);
    if (idx) {
        *idx = bi;
    }
    return best;
}

// ================================================================ the task

static bool timers_run(void)
{
    for (int i = 0; i < TIMER_MAX; i++) {
        if (s_tm[i].state == TIMER_RUN) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- SPEAK AT AN ALARM
// A board with the voice (tfn_speech.h, the app VOICE): about 100 s before a ring the voice makes its text into a file
// on the card (the voice is slower than real time: about 10 s for a text), for example "Good morning. It is seven
// thirty. Gym. Clear, twenty four degrees."; the ring plays it after its first tone, and again each 30 s.
#define VO_LEAD_S 100
#define VO_N      (ALARM_MAX + TIMER_MAX)
#define VO_TEXT   200
#define VO_REPEAT 30000
static int64_t s_vo_said;  // ms of the last voice of this ring (0 = not yet)

bool alarm_voice_on(void)
{
#if BOARD_TTS
    return store_get("sp_alarm", 0) != 0;
#else
    return false;
#endif
}

void alarm_voice_set(bool on)
{
    store_set("sp_alarm", on ? 1 : 0);
}

#if BOARD_TTS
static char s_vo_text[VO_N][VO_TEXT];
static int64_t s_vo_for[VO_N];  // an alarm: the epoch s of the ring of the made text; a timer: its end (ms)

static void words(int n, char *o, int max)
{
    static const char *const ONES[] = {"zero",     "one",     "two",     "three",     "four",     "five",    "six",
                                       "seven",    "eight",   "nine",    "ten",       "eleven",   "twelve",  "thirteen",
                                       "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char *const TENS[] = {"",      "",      "twenty",  "thirty", "forty",
                                       "fifty", "sixty", "seventy", "eighty", "ninety"};
    const char *neg = n < 0 ? "minus " : "";
    n = n < 0 ? -n : n;
    n = n > 99 ? 99 : n;
    if (n < 20) {
        snprintf(o, (size_t)max, "%s%s", neg, ONES[n]);
    } else {
        snprintf(o, (size_t)max, "%s%s%s%s", neg, TENS[n / 10], n % 10 ? " " : "", n % 10 ? ONES[n % 10] : "");
    }
}

static void vo_path(int k, char *o, int max)
{
    snprintf(o, (size_t)max, "/sdcard/voice/ring%d.tvc", k);
}

// the text of alarm i that rings at the local minute hm (0 .. 1439)
static void vo_alarm_text(int i, int hm, char *o, int max)
{
    const int hour = hm / 60, min = hm % 60;
    const char *g = hour >= 4 && hour < 12    ? "Good morning"
                    : hour >= 12 && hour < 17 ? "Good afternoon"
                    : hour >= 17 && hour < 22 ? "Good evening"
                                              : "Hello";
    char h[24], m[32], d[24];
    words(hour % 12 ? hour % 12 : 12, h, sizeof h);
    if (!min) {
        snprintf(m, sizeof m, "o'clock");
    } else if (min < 10) {
        words(min, d, sizeof d);
        snprintf(m, sizeof m, "oh %s", d);
    } else {
        words(min, m, sizeof m);
    }
    int k = snprintf(o, (size_t)max, "%s. It is %s %s.", g, h, m);
    if (s_al[i].label[0] && k < max) {
        k += snprintf(o + k, (size_t)(max - k), " %s.", s_al[i].label);
    }
    const sbw_t *w = sb_wx();
    if (w && w->at && time(NULL) - w->at < 6 * 3600 && k < max) {
        words((int)(w->t + (w->t < 0 ? -0.5f : 0.5f)), d, sizeof d);
        snprintf(o + k, (size_t)(max - k), " %s, %s degrees.", sb_cond(w->code, false), d);
    }
}

// the text of timer j at its end
static void vo_timer_text(int j, char *o, int max)
{
    const int mins = (int)(s_tm[j].len_s / 60), secs = (int)(s_tm[j].len_s % 60);
    char a[24], b[24], len[96];
    if (mins >= 60) {
        words(mins / 60, a, sizeof a);
        words(mins % 60, b, sizeof b);
        snprintf(len, sizeof len, "%s hour%s%s%s%s", a, mins / 60 > 1 ? "s" : "", mins % 60 ? " and " : "",
                 mins % 60 ? b : "", mins % 60 ? (mins % 60 > 1 ? " minutes" : " minute") : "");
    } else if (mins) {
        words(mins, a, sizeof a);
        snprintf(len, sizeof len, "%s minute%s", a, mins > 1 ? "s" : "");
    } else {
        words(secs, a, sizeof a);
        snprintf(len, sizeof len, "%s seconds", a);
    }
    const int k = snprintf(o, (size_t)max, "Your timer of %s is done.", len);
    if (s_tm[j].label[0] && k < max) {
        snprintf(o + k, (size_t)(max - k), " %s.", s_tm[j].label);
    }
}

// ask the voice for the text of ring slot k (when = its ring); with s_lock
static void vo_make(int k, int64_t when, const char *text)
{
    char path[48];
    vo_path(k, path, sizeof path);
    if (speech_make(text, path)) {
        snprintf(s_vo_text[k], VO_TEXT, "%s", text);
        s_vo_for[k] = when;
        ESP_LOGI(TAG, "the voice of ring %d: %s", k, text);
    }
}

// alarm i rings in d s (at the local time lt + d); with s_lock
static void vo_alarm_ahead(int i, int64_t now, int64_t lt, int64_t d)
{
    if (d <= 0 || d > VO_LEAD_S || s_vo_for[i] == now + d || !alarm_voice_on() || !speech_ready()) {
        return;
    }
    char t[VO_TEXT];
    vo_alarm_text(i, (int)((lt + d) % 86400 / 60), t, sizeof t);
    vo_make(i, now + d, t);
}

// timer j ends in left ms; with s_lock
static void vo_timer_ahead(int j, int64_t left)
{
    const int k = ALARM_MAX + j;
    if (left > VO_LEAD_S * 1000 || left < 12000 || s_tm[j].len_s < 30 || s_vo_for[k] == s_tm[j].end_ms ||
        !alarm_voice_on() || !speech_ready()) {
        return;
    }
    char t[VO_TEXT];
    vo_timer_text(j, t, sizeof t);
    vo_make(k, s_tm[j].end_ms, t);
}

// the ring of slot k: play its voice (after the first tone, then each 30 s); true = it plays now
static bool vo_ring(int k, int64_t t, int64_t el)
{
    if (k < 0 || k >= VO_N || el < 2600 || (s_vo_said && t - s_vo_said < VO_REPEAT) || !s_vo_for[k] ||
        !alarm_voice_on()) {
        return false;
    }
    const int64_t now = time(NULL);
    const bool fresh = k >= ALARM_MAX ? s_vo_for[k] == s_tm[k - ALARM_MAX].end_ms
                                      : now - s_vo_for[k] < 180 && s_vo_for[k] - now < 180;
    char path[48];
    vo_path(k, path, sizeof path);
    if (!fresh || !speech_file_ok(path, s_vo_text[k])) {
        return false;
    }
    const int ms = speech_play_file(path);
    if (ms <= 0) {
        return false;
    }
    s_vo_said = t;
    s_ring_last = t + ms + 500 - 2500;  // (the next tone 0.5 s after the voice)
    ESP_LOGI(TAG, "ring %d: the voice (%d ms)", k, ms);
    return true;
}
#else
static void vo_alarm_ahead(int i, int64_t now, int64_t lt, int64_t d)
{
    (void)i;
    (void)now;
    (void)lt;
    (void)d;
}
static void vo_timer_ahead(int j, int64_t left)
{
    (void)j;
    (void)left;
}
static bool vo_ring(int k, int64_t t, int64_t el)
{
    (void)k;
    (void)t;
    (void)el;
    return false;
}
#endif

static void ring(int i)
{
    s_vo_said = 0;
    s_ring = i;
    s_ring_t0 = tfn_now_ms();
    s_ring_last = 0;
    const bool tm = i >= ALARM_MAX;
    if (tm) {
        ESP_LOGI(TAG, "ring: timer %d (%u s)", i - ALARM_MAX, (unsigned)s_tm[i - ALARM_MAX].len_s);
    } else {
        ESP_LOGI(TAG, "ring %d (%02d:%02d)", i, s_al[i].hour, s_al[i].min);
    }
    app_screen_wake();
    const app_t *clock = tfn_find("CLOCK");
    if (clock) {
        if (app_current() != clock) {
            app_open_arg(clock, "ring");
        }
    } else {
        const char *l = tm ? s_tm[i - ALARM_MAX].label : s_al[i].label;
        notify_post("CLOCK", tm ? "TIMER" : "ALARM", l[0] ? l : tm ? "TIMER" : "ALARM", "");
    }
}

static void task(void *arg)
{
    (void)arg;
    int64_t t_rtc = 0;
    for (;;) {
        int wait = 15000;
        if (s_preview >= 0) {
            const int p = s_preview;
            s_preview = -1;
            audio_alert(SND[p].t, SND[p].n, s_preview_vol);
        }
        // the time of the network into the clock chip: at a sync (the VPN can read the sync state first, so also at
        // the first valid time with SNTP on), then each 6 hours
        const bool synced = esp_sntp_enabled() && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
        if (synced) {
            s_src = 2;
        }
        if (esp_sntp_enabled() && clock_time_ok() && (synced || !t_rtc || tfn_now_ms() - t_rtc > 6 * 3600 * 1000LL)) {
            if (s_src != 1 || synced) {
                s_src = 2;
            }
            rtc_write(time(NULL));
            t_rtc = tfn_now_ms();
        }
        if (clock_time_ok()) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const int64_t now = time(NULL), lt = now + tz_offset(&s_z, now, NULL);
            const int32_t minute = (int32_t)(lt / 60);
            const int wd = (int)((lt / 86400 + 4) % 7), hm = (int)(lt % 86400 / 60);
            bool hold = false;
            if (s_ring < 0) {
                for (int i = 0; i < s_n && s_ring < 0; i++) {
                    const alarm_t *a = &s_al[i];
                    if (s_snooze[i] && now >= s_snooze[i]) {
                        s_snooze[i] = 0;
                        ring(i);
                    } else if (a->on && hm == a->hour * 60 + a->min && s_fired[i] != minute &&
                               (a->days == 0 || (a->days >> wd) & 1)) {
                        s_fired[i] = minute;
                        ring(i);
                    }
                }
            }
            // the next wake: 1 s near an alarm (2 minutes) or a snooze, else 15 s; the power hold for 24 h
            for (int i = 0; i < s_n; i++) {
                const alarm_t *a = &s_al[i];
                int64_t d = -1;
                if (s_snooze[i]) {
                    d = s_snooze[i] - now;
                } else if (a->on) {
                    const int64_t l = next_local(a, lt - 1);
                    if (l) {
                        d = l - lt;
                    }
                }
                if (d >= 0 && d < 24 * 3600) {
                    hold = true;
                }
                if (d >= 0 && d < 120) {
                    wait = 1000;
                }
                vo_alarm_ahead(i, now, lt, d);
            }
            xSemaphoreGive(s_lock);
            power_alarm_hold(hold || timers_run());
        }
        // the timers: the end of each one that runs (also with no time of day)
        {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const int64_t t = tfn_now_ms();
            for (int i = 0; i < TIMER_MAX; i++) {
                ctimer_t *x = &s_tm[i];
                if (x->state != TIMER_RUN) {
                    continue;
                }
                if (t >= x->end_ms && s_ring < 0) {
                    x->state = TIMER_DONE;
                    ring(ALARM_MAX + i);
                } else if (x->end_ms - t < 15000) {
                    wait = 250;  // (near the end: look each 250 ms)
                }
                if (x->state == TIMER_RUN) {
                    vo_timer_ahead(i, x->end_ms - t);
                }
            }
            xSemaphoreGive(s_lock);
            if (!clock_time_ok()) {
                power_alarm_hold(timers_run());
            }
        }
        if (s_ring >= 0) {
            const int64_t t = tfn_now_ms(), el = t - s_ring_t0;
            if (el > 10 * 60 * 1000) {  // (10 minutes: the alarm stops by itself)
                alarm_stop();
            } else if (vo_ring(s_ring, t, el)) {
                app_screen_wake();
            } else if (!s_ring_last || t - s_ring_last >= 2500) {
                const bool tm = s_ring >= ALARM_MAX;
                const int vol = tm ? s_tm[s_ring - ALARM_MAX].vol : s_al[s_ring].vol;
                const int snd = tm ? s_tm[s_ring - ALARM_MAX].sound : s_al[s_ring].sound;
                const int v = vol / 3 + (int)((int64_t)(vol - vol / 3) * (el < 20000 ? el : 20000) / 20000);
                app_screen_wake();
                audio_alert(SND[snd].t, SND[snd].n, v);
                s_ring_last = t;
            }
            wait = 250;
        }
        for (int w = 0; w < wait && !s_wake; w += 250) {
            vTaskDelay(pdMS_TO_TICKS(wait < 250 ? wait : 250));
        }
        s_wake = false;
    }
}

void clock_log(void)
{
    const int64_t c = rtc_read(), t = time(NULL);
    tz_tm_t a, b;
    tz_split(t, &a);
    tz_split(c, &b);
    ESP_LOGI(TAG,
             "clock: source %d (0 none, 1 chip, 2 network), SNTP %s, UTC %04d-%02d-%02d %02d:%02d:%02d, chip %s"
             " %04d-%02d-%02d %02d:%02d:%02d (%+d s), %d alarms, ring %d",
             clock_source(), esp_sntp_enabled() ? "on" : "off", a.y, a.mo, a.d, a.h, a.mi, a.s, c ? "" : "NOT VALID",
             b.y, b.mo, b.d, b.h, b.mi, b.s, c ? (int)(c - t) : 0, s_n, s_ring);
}

void alarm_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    load();
    tz_parse(s_tz, &s_z);
    if (!clock_time_ok()) {  // the clock chip: the time with no network
        const int64_t t = rtc_read();
        if (t) {
            struct timeval tv = {.tv_sec = (time_t)t, .tv_usec = 0};
            settimeofday(&tv, NULL);
            s_src = 1;
            ESP_LOGI(TAG, "the time of the clock chip");
        }
    }
    bool any = false;
    for (int i = 0; i < s_n; i++) {
        any |= s_al[i].on;
    }
    if (any) {
        clock_sync();
    }
    xTaskCreatePinnedToCore(task, "alarm", 3072, NULL, 2, NULL, 1);
}
