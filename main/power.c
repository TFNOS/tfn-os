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
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/usb_serial_jtag.h"
#include "board.h"
#include "settings.h"
#include "wifi.h"
#include "app.h"
#include "link.h"
#include "walkie_call.h"
#include "watch.h"
#include "web_server.h"
#include <math.h>
#include "driver/temperature_sensor.h"

static const char *TAG = "power";

const uint16_t POWER_OFF_MIN[POWER_OFF_CHOICES] = {0, 15, 30, 60, 120, 240};
const char *const POWER_OFF_NAMES[POWER_OFF_CHOICES] = {"NEVER", "15 MIN", "30 MIN", "1 HOUR", "2 HOURS", "4 HOURS"};
static const char *const MODE_NAMES[PW_MODES] = {"on", "dim", "off"};
const uint16_t POWER_DEEP_S[POWER_DEEP_CHOICES] = {0, 10, 30, 60};
const char *const POWER_DEEP_NAMES[POWER_DEEP_CHOICES] = {"OFF", "10 S", "30 S", "60 S"};

#define SAMPLE_US 10000000  // the idle counters: a sample each 10 s and at each change of the mode
#define N_CAUSES  12

static int s_mode = PW_ON;
static bool s_bg;
static int64_t s_mode_at;            // ms: the start of the mode now
static int64_t s_mode_ms[PW_MODES];  // the time in each mode before s_mode_at
static int64_t s_quiet_at;           // ms: the start of STANDBY with no background work (0: not in it)
static int64_t s_off_at;             // ms: the start of the POWER OFF time (a hold starts it again)
static struct {
    const char *why;
    uint32_t n;
} s_wakes[N_CAUSES];

#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_full;  // the CPU at full speed while the screen is on
static bool s_full_held, s_ls_now;
#endif
static bool s_usb;                   // a PC on the USB port (no light sleep, no power off by the timer)
static volatile int s_x, s_xm = 60;  // the test switches (power_set_x)
static int64_t s_radio_off_at;       // ms: the radio stopped by PWX_RADIO (0: on)

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

int power_deep_choice(void)
{
    uint32_t i = store_get("pw_deep", 3);
    return i < POWER_DEEP_CHOICES ? (int)i : 3;
}

void power_set_deep_choice(int i)
{
    if (i < 0 || i >= POWER_DEEP_CHOICES) {
        return;
    }
    store_set("pw_deep", (uint32_t)i);
    ESP_LOGI(TAG, "deep idle on the battery: %s", POWER_DEEP_NAMES[i]);
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

static bool s_deep;          // DEEP IDLE acts now (deep_tick)
static bool s_deep_off;      // the radio sleeps now (DEEP IDLE)
static int64_t s_deep_t;     // ms: the radio went to sleep, or the check started
static int64_t s_deep_from;  // ms: the start of DEEP IDLE (the power source waits SRC_SETTLE_MS after it)

int power_x(void)
{
    if (s_mode != PW_OFF || s_bg) {
        return 0;
    }
    return s_x | (s_deep ? PWX_TOUCH | PWX_IMU | PWX_LOOP : 0);
}

bool power_deep_now(void)
{
    return s_deep;
}

void power_set_x(int bits, int minutes)
{
    s_x = bits & 15;
    if (minutes > 0) {
        s_xm = minutes;
    }
    ESP_LOGW(TAG, "test switches %d, radio off %d min", s_x, s_xm);
}

// PWX_RADIO: the radio off (from the main task, in power_tick)
static void radio_test(int64_t now)
{
    bool want = (power_x() & PWX_RADIO) != 0;
    if (want && !s_radio_off_at) {
        s_radio_off_at = now;
        wifi_radio_sleep(true);
    } else if (s_radio_off_at && (!want || now - s_radio_off_at >= (int64_t)s_xm * 60000)) {
        s_radio_off_at = 0;
        s_x &= ~PWX_RADIO;
        wifi_radio_sleep(false);
    }
}

// ---------------------------------------------------------------- the power source (a charger or the battery)
// A board on a charger keeps its behaviour. On the battery the DEEP IDLE can act.
// 1. A board that can tell (board_ext_power(): the VBUS bit of the AXP2101) gives the answer.
// 2. The 3.49 cannot tell: a PC (s_usb) or a voltage above a full cell (chg) = external power; else the trend of the
//    voltage in STANDBY. The logs: on a charger the voltage stays within +-4 mV for hours; on the battery it falls
//    15..80 mV an hour. So a fall of SRC_FALL_MV from the highest value = the battery, a rise of SRC_RISE_MV from the
//    lowest = a charger. Between them the state stays (a DEEP IDLE board falls slowly but stays on the battery).
//    The first SRC_SETTLE_MS of each STANDBY are not used (the voltage comes back up after the screen goes off).
// At the start the source is not known: the board acts as on a charger (the old behaviour) until it knows.
#define SRC_EVERY_MS  60000
#define SRC_SETTLE_MS 180000
#define SRC_FALL_MV   12
#define SRC_RISE_MV   15

static int8_t s_src = -1;       // 1 = the battery, 0 = external power, -1 = not known yet
static int8_t s_src_hw = -1;    // the last board_ext_power()
static int64_t s_src_at;        // ms: the last sample
static int s_src_hi, s_src_lo;  // mV: the range of this STANDBY (0: no sample yet)
static int s_src_pct = -1;      // the battery percent of the last sample

// THE POWER SAVE RULE: the DEEP IDLE (and in it the 3.49 DISPLAY OFF: board_display_off, then a key wakes the
// screen, not a touch) acts only when BOTH are true: the board is on the battery (the voltage falls, or no VBUS) AND
// the battery is under SAVE_PCT. Else the board keeps the normal STANDBY (it stays on the network).
#define SAVE_PCT 80
static bool save_ok(void)
{
    return s_src == 1 && !s_usb && s_src_hw != 1 && s_src_pct >= 0 && s_src_pct < SAVE_PCT;
}

bool power_on_battery(void)
{
    return s_src == 1;
}

// the source over a software restart (an OTA, a reboot from the manager): the board does not wait again for the
// trend of the voltage. A power cut or a reset by a key clears it.
#define SRC_KEEP_MAGIC 0x5352434bu
static RTC_NOINIT_ATTR uint32_t s_src_keep;  // SRC_KEEP_MAGIC + the source in the low byte

static void src_set(int8_t v, const char *why, int mv)
{
    if (v != s_src) {
        ESP_LOGI(TAG, "power source: %s (%s, %d mV)", v ? "the battery" : "external", why, mv);
        s_src = v;
    }
    s_src_keep = (SRC_KEEP_MAGIC & 0xffffff00u) | (uint8_t)v;
}

static void src_restore(void)
{
    const esp_reset_reason_t r = esp_reset_reason();
    if ((s_src_keep & 0xffffff00u) == (SRC_KEEP_MAGIC & 0xffffff00u) && (r == ESP_RST_SW || r == ESP_RST_PANIC)) {
        s_src = (int8_t)(s_src_keep & 0xff) == 1 ? 1 : (s_src_keep & 0xff) == 0 ? 0 : -1;
        ESP_LOGI(TAG, "power source after a restart: %d (kept)", s_src);
    }
    s_src_keep = 0;
}

static void src_tick(int64_t now)
{
    if (s_src_at && now - s_src_at < SRC_EVERY_MS) {
        return;
    }
    s_src_at = now;
    bool chg = false;
    int mv = 0;
    int pct = board_battery(&chg, &mv);
    s_src_pct = pct;
    s_src_hw = (int8_t)board_ext_power();
    if (pct < 0 || s_usb || chg || s_src_hw == 1) {
        s_src_hi = s_src_lo = 0;
        src_set(0, pct < 0 ? "no battery" : s_usb ? "a PC" : chg ? "the voltage" : "VBUS", mv);
        return;
    }
    if (s_src_hw == 0) {
        src_set(1, "no VBUS", mv);
        return;
    }
    if (s_mode != PW_OFF || s_bg || !s_quiet_at || now - s_quiet_at < SRC_SETTLE_MS ||
        (s_deep && now - s_deep_from < SRC_SETTLE_MS)) {
        s_src_hi = s_src_lo = 0;  // (only STANDBY with no background work: a steady load)
        return;
    }
    if (s_deep && (!s_deep_off || now - s_deep_t < 5000)) {
        return;  // (DEEP IDLE: only samples while the radio sleeps, the same load each time)
    }
    if (!s_src_hi) {
        s_src_hi = s_src_lo = mv;
        return;
    }
    s_src_hi = mv > s_src_hi ? mv : s_src_hi;
    s_src_lo = mv < s_src_lo ? mv : s_src_lo;
    if (s_src_hi - mv >= SRC_FALL_MV) {
        src_set(1, "the voltage falls", mv);
        s_src_hi = mv;  // (a new start for the next change)
        s_src_lo = mv;
    } else if (mv - s_src_lo >= SRC_RISE_MV) {
        src_set(0, "the voltage rises", mv);
        s_src_hi = mv;
        s_src_lo = mv;
    }
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

// ---------------------------------------------------------------- DEEP IDLE (power.h)
// The radio stops in the untouched MENU on the battery. A check: the radio starts and connects; then it stays on for
// DEEP_KEEP_MS (the beacons with the DEEP flag: the manager sees the board and reads it at once), and while the web
// server gets new connections (the manager reads the board, starts an OTA or an install: those set the background
// state, s_bg, which ends DEEP IDLE until they end), while a watcher round is due or runs, and while a call rings.
#define DEEP_AFTER_MS 60000  // STANDBY this long first (a short pause of the user keeps the old way)
#define DEEP_KEEP_MS  3000   // a check: the radio stays on this long after the connect (2 beacons)
#define DEEP_WEB_MS   2000   // ... and this long after the last new web connection
#define DEEP_JOIN_MS  15000  // a check with no connect in this time ends (the network is not there)
#define DEEP_MAX_MS   60000  // a check ends after this time (only the background state holds the radio longer)
#define DEEP_WAKE_MS  500    // a key wake: the radio starts this long after the screen

static int64_t s_deep_conn;                 // ms: the connect of this check (0: not yet)
static uint32_t s_deep_checks;              // the checks (the report)
static int64_t s_deep_sleep_ms;             // the radio sleep time (the report)
static int s_deep_last_ms, s_deep_join_ms;  // the last check: its length and its connect time (-1: none)

static bool deep_ok(int64_t now)
{
    const app_t *a = app_current();
    return power_deep_choice() > 0 && save_ok() && s_mode == PW_OFF && !s_bg && !s_x && s_quiet_at &&
           now - s_quiet_at >= DEEP_AFTER_MS && !s_hold && a == &app_menu && !link_in_use() && !walkie_paired() &&
           walkie_call_state(NULL, NULL) != WKC_CALLING && (s_deep_off || wifi_can_sleep() || s_deep);
}

static void deep_radio(bool off, int64_t now)
{
    if (off == s_deep_off) {
        return;
    }
    if (!off) {
        s_deep_sleep_ms += now - s_deep_t;
        s_deep_checks++;
    }
    s_deep_off = off;
    s_deep_t = now;
    s_deep_conn = 0;
    wifi_radio_sleep(off);
}

static void deep_tick(int64_t now)
{
    const bool ok = deep_ok(now);
    if (ok != s_deep) {
        s_deep = ok;
        s_deep_from = now;
        ESP_LOGI(TAG, "deep idle %s", ok ? "starts" : "ends");
        if (ok) {
            s_deep_t = now;  // (the radio is on: a check that ends at once)
            s_deep_conn = now;
        }
    }
    if (!s_deep) {
        // (a key woke the screen: the radio starts DEEP_WAKE_MS later; the backlight, the full CPU speed and the
        // start of the radio at the same time can pull the cell voltage down too far)
        if (!s_deep_off || s_mode == PW_OFF || now - s_mode_at >= DEEP_WAKE_MS) {
            deep_radio(false, now);
        }
        return;
    }
    if (save_ok()) {
        board_display_off();  // (once: the board keeps it until the wake)
    }
    if (s_deep_off) {
        if (now - s_deep_t >= (int64_t)POWER_DEEP_S[power_deep_choice()] * 1000) {
            deep_radio(false, now);  // a check
        }
        return;
    }
    if (!s_deep_conn && wifi_ip()[0]) {
        s_deep_conn = now;
    }
    const int64_t web = web_server_last_ms();
    bool done;
    if (!s_deep_conn) {
        done = now - s_deep_t >= DEEP_JOIN_MS;
    } else {
        done = now - s_deep_conn >= DEEP_KEEP_MS && now - web >= DEEP_WEB_MS && !watch_busy();
    }
    if (done || now - s_deep_t >= DEEP_MAX_MS) {
        if (s_deep_t != s_deep_conn) {
            s_deep_last_ms = (int)(now - s_deep_t);
            s_deep_join_ms = s_deep_conn ? (int)(s_deep_conn - s_deep_t) : -1;
            ESP_LOGI(TAG, "deep idle: a check of %d ms (connect %d ms)", s_deep_last_ms, s_deep_join_ms);
        }
        deep_radio(true, now);
    }
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
    src_restore();
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
        s_off_at = 0;
    } else if (!s_quiet_at) {
        s_quiet_at = t;
        s_off_at = t;
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
    radio_test(now);
    src_tick(now);
    deep_tick(now);
    int min = POWER_OFF_MIN[power_off_choice()];
    if ((s_hold || s_alarm_hold || s_oref_hold) && s_off_at) {
        s_off_at = now;  // (an app or an alarm waits for a time: the POWER OFF time starts again; not s_quiet_at, which
                         // the power source and DEEP IDLE use)
    }
    if (!min || !s_off_at || now - s_off_at < (int64_t)min * 60000) {
        return false;
    }
    bool chg = false;
    int pct = board_battery(&chg, NULL);
    // on USB power (or no battery): no power off; the time starts again. The 3.49 sees USB power only above a full
    // cell (chg); a full battery on USB reads about 4.15 V, so a PC on the USB port (s_usb) counts too.
    if (pct < 0 || chg || s_usb) {
        s_off_at = now;
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
                     "\"usb\":%s,\"bat\":%d,\"mv\":%d,\"chg\":%s,\"up\":%lld,\"x\":%d,\"xm\":%d,\"src\":%d,\"src_hw\":%"
                     "d,\"deep\":%d,\"deep_on\":%s,\"radio_off\":%s,\"checks\":%lu,\"radio_off_s\":%lld,\"check_ms\":%"
                     "d,\"join_ms\":%d,\"hold\":\"%s%"
                     "s%s\",\"t\":{",
                     MODE_NAMES[s_mode], s_bg ? "true" : "false", (unsigned)(hz / 1000000), psi, ls ? "true" : "false",
                     power_light_sleep() ? "true" : "false", POWER_OFF_MIN[power_off_choice()], usb ? "true" : "false",
                     pct, mv, chg ? "true" : "false", (long long)(t / 1000), s_x, s_xm, s_src, s_src_hw,
                     power_deep_choice(), s_deep ? "true" : "false", s_deep_off ? "true" : "false",
                     (unsigned long)s_deep_checks,
                     (long long)((s_deep_sleep_ms + (s_deep_off ? t - s_deep_t : 0)) / 1000), s_deep_last_ms,
                     s_deep_join_ms, s_hold ? "app " : "", s_alarm_hold ? "alarm " : "", s_oref_hold ? "oref" : "");
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
        k += snprintf(b + k, (size_t)(n - k), "}");
    }
    const int shunt = board_bat_shunt_uv();  // (the Tab5: the battery current monitor; for the source check)
    if (shunt != INT32_MIN && k < n) {
        k += snprintf(b + k, (size_t)(n - k), ",\"shunt_uv\":%d", shunt);
    }
    if (k < n) {
        k += snprintf(b + k, (size_t)(n - k), "}");
    }
    return k < n ? k : n - 1;
}

void power_log(void)
{
    static char b[1024];
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
