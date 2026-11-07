// The power modes of the board and the power report (power.h).
#include "tfn_util.h"
#include "tfn_extras.h"
#include "power.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/usb_serial_jtag.h"
#include "board.h"
#include "settings.h"
#include <math.h>
#include "driver/temperature_sensor.h"

static const char *TAG = "power";

const uint16_t POWER_OFF_MIN[POWER_OFF_CHOICES] = {0, 15, 30, 60, 120, 240};
const char *const POWER_OFF_NAMES[POWER_OFF_CHOICES] = {"NEVER", "15 MIN", "30 MIN", "1 HOUR", "2 HOURS", "4 HOURS"};
static const char *const MODE_NAMES[PW_MODES] = {"on", "dim", "off"};

#define SAMPLE_US 10000000  // the idle counters: a sample each 10 s and at each change of the mode
#define N_CAUSES  12

static int s_mode = PW_ON;
static bool s_bg;
static int64_t s_mode_at;            // ms: the start of the mode now
static int64_t s_mode_ms[PW_MODES];  // the time in each mode before s_mode_at
static int64_t s_quiet_at;           // ms: the start of STANDBY with no background work (0: not in it)
static struct {
    const char *why;
    uint32_t n;
} s_wakes[N_CAUSES];

#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_full;  // the CPU at full speed while the screen is on
static bool s_full_held, s_ls_now;
#endif
static bool s_usb;  // a PC on the USB port (no light sleep, no power off by the timer)

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static configRUN_TIME_COUNTER_TYPE s_idle_last[2];
static int64_t s_sample_us;
static uint64_t s_idle_us[PW_MODES][2], s_span_us[PW_MODES];
#endif

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
static volatile int64_t s_sleep_us;  // the light sleep time (all of it)
static volatile uint32_t s_sleeps;
static IRAM_ATTR esp_err_t ls_exit(int64_t us, void *arg)
{
    (void)arg;
    s_sleep_us += us;
    s_sleeps++;
    return ESP_OK;
}
#endif

int power_off_choice(void)
{
    uint32_t i = store_get("pw_off", 0);
    return i < POWER_OFF_CHOICES ? (int)i : 0;
}

void power_set_off_choice(int i)
{
    if (i < 0 || i >= POWER_OFF_CHOICES) {
        return;
    }
    store_set("pw_off", (uint32_t)i);
    ESP_LOGI(TAG, "power off after %s in standby", POWER_OFF_NAMES[i]);
}

bool power_light_sleep(void)
{
    return store_get("pw_ls", 1) != 0;
}

static volatile bool s_apply_req;  // a new light sleep setting: the main task applies it (power_tick)
void power_set_light_sleep(bool on)
{
    store_set("pw_ls", on ? 1 : 0);
    s_apply_req = true;
    ESP_LOGI(TAG, "light sleep in standby: %s", on ? "on" : "off");
}

// the CPU speed and the light sleep for the mode now
static void pm_apply(void)
{
#if CONFIG_PM_ENABLE
    if (!s_full) {
        return;
    }
    bool full = s_mode != PW_OFF;
    // a PC on the USB port (its SOF packets): no light sleep (the USB console does not work after a light sleep). The
    // check is here, while the screen is on or just went off (the ticks are normal then): the IDF monitor
    // (CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION) saw a false "disconnected" with the tickless idle, and 3 sleeps stopped the
    // console of the CoreS3. The state stays for the whole STANDBY time.
    if (s_mode != PW_OFF || !s_ls_now) {
        s_usb = usb_serial_jtag_is_connected();
    }
    bool ls = s_mode == PW_OFF && !s_bg && power_light_sleep() && !s_usb;
    if (full && !s_full_held) {
        esp_pm_lock_acquire(s_full);
        s_full_held = true;
    }
    esp_pm_config_t c = {.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, .min_freq_mhz = 80, .light_sleep_enable = ls};
    esp_err_t e = esp_pm_configure(&c);
    if (e != ESP_OK && ls) {
        ESP_LOGW(TAG, "light sleep: %s (the CPU speed only)", esp_err_to_name(e));
        c.light_sleep_enable = false;
        ls = false;
        esp_pm_configure(&c);
    }
    s_ls_now = ls;
    if (!full && s_full_held) {
        esp_pm_lock_release(s_full);
        s_full_held = false;
    }
#endif
}

static void sample(bool force)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    int64_t us = esp_timer_get_time();
    if (!force && us - s_sample_us < SAMPLE_US) {
        return;
    }
    for (int c = 0; c < 2; c++) {
        configRUN_TIME_COUNTER_TYPE v = ulTaskGetIdleRunTimeCounterForCore(c);
        s_idle_us[s_mode][c] += (configRUN_TIME_COUNTER_TYPE)(v - s_idle_last[c]);
        s_idle_last[c] = v;
    }
    s_span_us[s_mode] += (uint64_t)(us - s_sample_us);
    s_sample_us = us;
#else
    (void)force;
#endif
}

void power_init(void)
{
    s_mode_at = tfn_now_ms();
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    for (int c = 0; c < 2; c++) {
        s_idle_last[c] = ulTaskGetIdleRunTimeCounterForCore(c);
    }
    s_sample_us = esp_timer_get_time();
#endif
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    esp_pm_sleep_cbs_register_config_t cb = {.exit_cb = ls_exit};
    esp_pm_light_sleep_register_cbs(&cb);
#endif
#if CONFIG_PM_ENABLE
    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "screen", &s_full) != ESP_OK) {
        s_full = NULL;
    }
    pm_apply();
    ESP_LOGI(TAG, "power management on: %d MHz with the screen on, 80 MHz with it off, light sleep %s",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, power_light_sleep() ? "on" : "off");
#endif
}

void power_mode(int mode, bool background)
{
    if (mode < 0 || mode >= PW_MODES || (mode == s_mode && background == s_bg)) {
        return;
    }
    int64_t t = tfn_now_ms();
    sample(true);
    s_mode_ms[s_mode] += t - s_mode_at;
    s_mode_at = t;
    s_mode = mode;
    s_bg = background;
    bool quiet = mode == PW_OFF && !background;
    if (!quiet) {
        s_quiet_at = 0;
    } else if (!s_quiet_at) {
        s_quiet_at = t;
    }
    pm_apply();
}

void power_wake(const char *why)
{
    int i = 0;
    while (i < N_CAUSES - 1 && s_wakes[i].why && s_wakes[i].why != why) {
        i++;
    }
    if (!s_wakes[i].why) {
        s_wakes[i].why = why;
    } else if (s_wakes[i].why != why) {
        s_wakes[i].why = "other";  // (the last place: all the other causes)
    }
    s_wakes[i].n++;
}

static volatile bool s_off_ask;
void power_off_ask(void)
{
    s_off_ask = true;
}

static volatile bool s_hold;  // app_standby_hold(): the open app waits for a time
void power_hold(bool on)
{
    s_hold = on;
}
static volatile bool s_alarm_hold;  // alarm.c: an armed alarm in the next 24 hours
void power_alarm_hold(bool on)
{
    s_alarm_hold = on;
}
#if CONFIG_TFN_EXTRA_OREF
static volatile bool s_oref_hold;  // oref.c: the watch of HOME FRONT ALERTS is on
void power_oref_hold(bool on)
{
    s_oref_hold = on;
}
#else
#define s_oref_hold false  // (the extra HOME FRONT ALERTS is not in this build)
#endif

bool power_tick(int64_t now)
{
    sample(false);
    if (s_off_ask) {
        ESP_LOGW(TAG, "power off from the manager");
        return true;
    }
    if (s_apply_req) {
        s_apply_req = false;
        pm_apply();
    }
    int min = POWER_OFF_MIN[power_off_choice()];
    if ((s_hold || s_alarm_hold || s_oref_hold) && s_quiet_at) {
        s_quiet_at = now;  // (an app or an alarm waits for a time: the STANDBY
    }
    //  time starts again)
    if (!min || !s_quiet_at || now - s_quiet_at < (int64_t)min * 60000) {
        return false;
    }
    bool chg = false;
    int pct = board_battery(&chg, NULL);
    // on USB power (or no battery): no power off; the time starts again. The 3.49 sees USB power only above a full
    // cell (chg); a full battery on USB reads about 4.15 V, so a PC on the USB port (s_usb) counts too.
    if (pct < 0 || chg || s_usb) {
        s_quiet_at = now;
        return false;
    }
    ESP_LOGW(TAG, "%d min in standby on the battery (%d %%): power off", min, pct);
    return true;
}

static int pct_of(uint64_t part, uint64_t all)
{
    return all ? (int)((part * 100 + all / 2) / all) : -1;
}

int power_json(char *b, int n)
{
    int64_t t = tfn_now_ms();  // (the idle shares: from the last sample of the main task, at most 10 s old)
    int64_t ms[PW_MODES];
    for (int i = 0; i < PW_MODES; i++) {
        ms[i] = s_mode_ms[i] + (i == s_mode ? t - s_mode_at : 0);
    }
    uint32_t hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
    wifi_ps_type_t ps = WIFI_PS_NONE;
    int psi = esp_wifi_get_ps(&ps) == ESP_OK ? (int)ps : -1;
    bool chg = false;
    int mv = 0, pct = board_battery(&chg, &mv);
    bool ls = false, usb = false;
#if CONFIG_PM_ENABLE
    ls = s_ls_now;
    usb = s_usb;
#endif
    int k = snprintf(b, (size_t)n,
                     "{\"mode\":\"%s\",\"bg\":%s,\"mhz\":%u,\"ps\":%d,\"ls_on\":%s,\"ls_set\":%s,\"pwoff\":%d,"
                     "\"usb\":%s,\"bat\":%d,\"mv\":%d,\"chg\":%s,\"up\":%lld,\"t\":{",
                     MODE_NAMES[s_mode], s_bg ? "true" : "false", (unsigned)(hz / 1000000), psi, ls ? "true" : "false",
                     power_light_sleep() ? "true" : "false", POWER_OFF_MIN[power_off_choice()], usb ? "true" : "false",
                     pct, mv, chg ? "true" : "false", (long long)(t / 1000));
    for (int i = 0; i < PW_MODES && k < n; i++) {
        k += snprintf(b + k, (size_t)(n - k), "%s\"%s\":%lld", i ? "," : "", MODE_NAMES[i], (long long)(ms[i] / 1000));
    }
    if (k < n) {
        k += snprintf(b + k, (size_t)(n - k), "},\"idle\":{");
    }
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    for (int i = 0; i < PW_MODES && k < n; i++) {
        k += snprintf(b + k, (size_t)(n - k), "%s\"%s\":[%d,%d]", i ? "," : "", MODE_NAMES[i],
                      pct_of(s_idle_us[i][0], s_span_us[i]), pct_of(s_idle_us[i][1], s_span_us[i]));
    }
#endif
    int64_t sl = 0;
    uint32_t sn = 0;
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    sl = s_sleep_us;
    sn = s_sleeps;
#endif
    if (k < n) {
        k += snprintf(b + k, (size_t)(n - k), "},\"sleep_s\":%lld,\"sleeps\":%lu,\"sleep_pct\":%d,\"wakes\":{",
                      (long long)(sl / 1000000), (unsigned long)sn,
                      pct_of((uint64_t)(sl / 1000), (uint64_t)ms[PW_OFF]));
    }
    for (int i = 0; i < N_CAUSES && s_wakes[i].why && k < n; i++) {
        char w[24];
        int j = 0;
        for (const char *s = s_wakes[i].why; *s && j < (int)sizeof w - 1; s++) {
            w[j++] = *s == '"' ? ' ' : *s;
        }
        w[j] = 0;
        k += snprintf(b + k, (size_t)(n - k), "%s\"%s\":%lu", i ? "," : "", w, (unsigned long)s_wakes[i].n);
    }
    if (k < n) {
        k += snprintf(b + k, (size_t)(n - k), "}}");
    }
    return k < n ? k : n - 1;
}

void power_log(void)
{
    static char b[640];
    power_json(b, sizeof b);
    ESP_LOGI(TAG, "%s", b);
}

// ---------------------------------------------------------------- the temperature of the chip (SETUP)
// All boards (ESP32-S3 and ESP32-P4). The range 20 .. 100 C (the error of the sensor there: 2 C). The sensor stays
// on after the first call.
float board_chip_temp(void)
{
    static temperature_sensor_handle_t s_ts;
    static bool s_ts_bad;
    if (!s_ts && !s_ts_bad) {
        temperature_sensor_config_t c = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
        if (temperature_sensor_install(&c, &s_ts) != ESP_OK) {
            s_ts = NULL;
            s_ts_bad = true;
            ESP_LOGW("power", "the temperature sensor does not start");
        } else if (temperature_sensor_enable(s_ts) != ESP_OK) {
            temperature_sensor_uninstall(s_ts);
            s_ts = NULL;
            s_ts_bad = true;
        }
    }
    float t;
    if (!s_ts || temperature_sensor_get_celsius(s_ts, &t) != ESP_OK) {
        return NAN;
    }
    return t;
}
