// TFN OS SDK example 4: WIFI SCAN, a WiFi scanner (a utility that uses RAW ESP-IDF functions).
//
// NETWORKS: each network that the board hears, with its signal in dBm, its channel and its security, the strongest
// first. CHANNELS: the networks as curves over the 13 channels of 2.4 GHz (a full channel shows where not to put your
// router). A tap on a network opens FOLLOW: the live signal of that one network, a graph of the last minute and a
// tick sound (the settings panel), to find the best place for a board or a router.
//
// What this example shows:
//   - RAW ESP-IDF functions in an app file (sdk/docs/raw.md): esp_wifi_scan_start() and the other functions of the
//     permission class "wifi.scan" (main/tfn_abi_extra.json). mk_app.py writes "perms=wifi.scan" into the manifest,
//     the STORE asks the user before the install, and the board links these names only for an app with the class
//   - a task of the app (behave.md section 10): the blocking scan runs in its own task, and the task ends in leave()
//   - a hand-over of data from the task to the app task with a flag (no lock in render())
//   - a firmware function next to the raw ones: wifi_state() (the scan waits while the board connects)
//   - the layouts of the 3.49 bar and of the 320 x 240 frame, a list that scrolls, a graph, panel items, the saves
//
// Build (from tfn-os/; the board needs a firmware with ABI rev 1 or newer):
//   python sdk/tools/mk_app.py sdk/examples/wifiscan/app_wifiscan.c --sym app_wifiscan --name "WIFI SCAN" --group UTIL --kinds lcd349,cores3 --nosign
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfn/wifi.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"                       // the RAW ESP-IDF header: the real types and functions
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// ---------------------------------------------------------------- the layout of each board kind
typedef struct { int x, y, w, h; } rect_t;

#if BOARD_BAR_SCREEN
static const rect_t R_TAB_NET  = {6, 4, 172, 36};
static const rect_t R_TAB_CH   = {184, 4, 150, 36};
static const rect_t R_BACK     = {6, 4, 96, 36};
#define STATUS_X    634                     // the right end of the status line (top right)
#define STATUS_Y    28
#define LIST_Y      44                      // the list: 2 columns of cells, 3 rows on the screen
#define LIST_HT      128
#define CELL_COLS   2
#define CELL_W      311
#define CELL_H      40
#define CELL_GAP    4
#define PLOT_X0     34                      // CHANNELS: the plot box
#define PLOT_X1     632
#define PLOT_Y0     48
#define PLOT_Y1     148
#define CHAN_Y      166                     // the baseline of the channel numbers
#define TITLE_X     114                     // FOLLOW: the name of the network
#define TITLE_Y     30
#define TITLE_W     330
#define BIG_CX      118                     // the big number
#define BIG_Y       98
#define QUAL_Y      124
#define METER_Y     136
#define METER_W     200
#define BEST_Y      166
#define GRAPH_X0    238
#define GRAPH_X1    634
#define GRAPH_Y0    46
#define GRAPH_Y1    168
#else
static const rect_t R_TAB_NET  = {4, 4, 152, 36};
static const rect_t R_TAB_CH   = {162, 4, 154, 36};
static const rect_t R_BACK     = {4, 4, 92, 36};
#define STATUS_X    316                     // the status line is at the bottom on this frame
#define STATUS_Y    236
#define LIST_Y      44
#define LIST_HT      178
#define CELL_COLS   1
#define CELL_W      312
#define CELL_H      44
#define CELL_GAP    2
#define PLOT_X0     30
#define PLOT_X1     314
#define PLOT_Y0     50
#define PLOT_Y1     196
#define CHAN_Y      214
#define TITLE_X     104
#define TITLE_Y     30
#define TITLE_W     212
#define BIG_CX      84
#define BIG_Y       96
#define QUAL_Y      122
#define METER_Y     132
#define METER_W     150
#define BEST_Y      0                       // (the BEST line is in the right column on this frame)
#define GRAPH_X0    8
#define GRAPH_X1    312
#define GRAPH_Y0    156
#define GRAPH_Y1    232
#endif

#define CELL_X0     ((gfx_w - CELL_COLS * CELL_W - (CELL_COLS - 1) * 6) / 2)
#define ROW_STEP    (CELL_H + CELL_GAP)

#define MAX_REC     48                      // the records of one scan
#define MAX_NET     40                      // the networks in the list (the same name on more access points is one)
#define HIST_N      200                     // FOLLOW: the samples of the graph (about one each 0.3 s: a minute)
#define MISS_MAX    4                       // a network that 4 full scans do not hear leaves the list
#define FULL_PAUSE  1500                    // ms between two full scans
#define CHAN_PAUSE  120                     // ms between two scans of one channel (FOLLOW)
#define MINE_PAUSE  250                     // ms between two reads of the network in use (FOLLOW, no scan)
#define DB_LO       (-95)                   // the range of the graphs
#define DB_HI       (-25)

#define KEY_SORT    "wifiscan_sort"         // the save keys start with the prefix of the app (<id>_)
#define KEY_SOUND   "wifiscan_snd"

enum { V_NET, V_CHAN, V_FOLLOW };
enum { M_FULL, M_CHAN, M_MINE };            // what the scan task does
enum { SORT_SIGNAL, SORT_NAME, SORT_CHANNEL };

// the settings panel
enum { HI_SORT, HI_SOUND };
static const char *const SORT_VALS[] = {"SIGNAL", "SIGNAL", "NAME", "NAME", "CHANNEL", "CHANNEL"};
static const char *const SOUND_VALS[] = {"OFF", "OFF", "ON", "ON"};
static const pitem_t ITEMS[] = {
    {PI_PICK, HI_SORT, 3, 0, {"SORT", "SORT"}, SORT_VALS, KEY_SORT},
    {PI_PICK, HI_SOUND, 2, 1, {"FOLLOW SOUND", "FOLLOW SOUND"}, SOUND_VALS, KEY_SOUND},
};

// ---------------------------------------------------------------- the state
typedef struct {
    char ssid[33];                          // "" = a hidden network (its key is then the BSSID)
    uint8_t bssid[6];                       // of the strongest access point
    uint8_t ch, auth, n_ap, miss;
    int8_t rssi, best;
    bool mine;                              // the board uses this network now
    uint32_t seen;                          // the number of the last scan that heard it
} net_t;

// what the scan task gives to the app task: one buffer and a flag (the task writes only when the flag is 0)
static wifi_ap_record_t *s_back;            // MAX_REC records (PSRAM)
static volatile int s_nback;
static volatile int s_back_mode;            // M_* of the result
static volatile bool s_ready;               // a result waits for the app task
static volatile int s_err;                  // the last error of esp_wifi_scan_start (0 = none)
static volatile int s_want_mode, s_want_ch; // what the app task wants now
static volatile bool s_quit, s_in_scan;
#ifdef ESP_PLATFORM
static TaskHandle_t s_task;
static volatile bool s_task_done;
#else
static int64_t s_next_host;                 // (the PC test: one step of the task in update())
#endif

static net_t *s_net;                        // MAX_NET (PSRAM)
static int s_nnet;
static uint32_t s_scan_no;
static int64_t s_last_full_ms;
static int s_view, s_sort;
static bool s_sound;
static int s_scroll, s_scroll0;             // the list: the scroll in px
static bool s_down, s_dragging;
static int s_y0, s_x0;
static int64_t s_now;
static uint8_t s_my_bssid[6];
static bool s_have_mine;
// FOLLOW
static char s_f_ssid[33];
static uint8_t s_f_bssid[6];
static bool s_f_hidden, s_f_mine;
static int s_f_ch;
static int8_t s_hist[HIST_N];               // 0 = not heard in that sample
static int s_hist_n;
static int8_t s_f_rssi, s_f_best;
static int s_f_miss;                        // the scans of its channel in a row that did not hear it
static int64_t s_f_seen_ms, s_tick_ms;

// ---------------------------------------------------------------- the scan task (raw ESP-IDF)
// One step: a full scan, a scan of one channel, or a read of the network in use. A blocking esp_wifi_scan_start()
// returns when the scan ends; the result stays in the driver until esp_wifi_scan_get_ap_records() takes it (the
// firmware does not read a scan that it did not start: main/wifi.c).
static void scan_step(void)
{
    int mode = s_want_mode;
    wifi_state_t ws = wifi_state();
    if (ws == WIFI_OFF) { s_err = ESP_ERR_WIFI_NOT_STARTED; return; }
    if (ws == WIFI_CONNECTING) { s_err = 1; return; }      // (a scan now would stop the connection)
    if (mode == M_MINE) {
        wifi_ap_record_t r;
        if (esp_wifi_sta_get_ap_info(&r) == ESP_OK) {
            s_back[0] = r;
            s_nback = 1;
        } else {
            s_nback = 0;
        }
        s_back_mode = M_MINE;
        s_err = 0;
        s_ready = true;
        return;
    }
    wifi_scan_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.show_hidden = true;
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    if (mode == M_CHAN) {
        cfg.channel = (uint8_t)s_want_ch;
        cfg.scan_time.active.min = 60;
        cfg.scan_time.active.max = 110;
    } else {
        cfg.scan_time.active.min = 60;
        cfg.scan_time.active.max = 120;
    }
    s_in_scan = true;
    esp_err_t r = esp_wifi_scan_start(&cfg, true);
    s_in_scan = false;
    if (r != ESP_OK) { s_err = (int)r; return; }
    if (s_quit) { esp_wifi_clear_ap_list(); return; }       // (leave() stopped the scan: free its list)
    uint16_t n = MAX_REC;
    if (esp_wifi_scan_get_ap_records(&n, s_back) != ESP_OK) n = 0;
    // the network in use: its BSSID marks it in the list
    wifi_ap_record_t me;
    s_have_mine = esp_wifi_sta_get_ap_info(&me) == ESP_OK;
    if (s_have_mine) memcpy(s_my_bssid, me.bssid, 6);
    s_nback = n;
    s_back_mode = mode;
    s_err = 0;
    s_ready = true;
}

static int step_pause(int mode) { return mode == M_FULL ? FULL_PAUSE : mode == M_CHAN ? CHAN_PAUSE : MINE_PAUSE; }

#ifdef ESP_PLATFORM
static void scan_task(void *arg)
{
    (void)arg;
    while (!s_quit) {
        if (s_ready) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }   // (the app task did not take the last one yet)
        int mode = s_want_mode;
        scan_step();
        // the pause; a change of the mode (a tap on a network) ends it at once
        int wait = s_err ? 400 : step_pause(mode);
        for (int t = 0; t < wait && !s_quit && s_want_mode == mode; t += 20) vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_task_done = true;
    vTaskDelete(NULL);                      // a task of the app must end (behave.md section 10)
}
#endif

static void want(int mode, int ch)
{
    s_want_ch = ch;
    s_want_mode = mode;
}

// ---------------------------------------------------------------- the list of networks
static bool key_eq(const net_t *n, const wifi_ap_record_t *r)
{
    if (!r->ssid[0] || !n->ssid[0]) return !r->ssid[0] && !n->ssid[0] && !memcmp(n->bssid, r->bssid, 6);
    return !strcmp(n->ssid, (const char *)r->ssid);
}

static int cmp_net(const void *a, const void *b)
{
    const net_t *x = a, *y = b;
    if (s_sort == SORT_NAME) {
        if (!x->ssid[0] != !y->ssid[0]) return !x->ssid[0] ? 1 : -1;        // (the hidden ones at the end)
        int c = strcmp(x->ssid, y->ssid);
        if (c) return c;
    } else if (s_sort == SORT_CHANNEL && x->ch != y->ch) {
        return x->ch - y->ch;
    }
    if (x->miss != y->miss) return x->miss - y->miss;       // (the networks of the last scan first)
    return y->rssi - x->rssi;
}

static void follow_sample(int rssi)
{
    if (s_hist_n == HIST_N) {
        memmove(s_hist, s_hist + 1, HIST_N - 1);
        s_hist_n--;
    }
    s_hist[s_hist_n++] = (int8_t)(rssi ? rssi : 0);
    if (rssi) {
        s_f_rssi = (int8_t)rssi;
        if (!s_f_best || rssi > s_f_best) s_f_best = (int8_t)rssi;
        s_f_seen_ms = s_now;
    }
}

// a miss that keeps the last value in the graph (the time of "heard" does not change)
static void follow_sample_hold(void)
{
    int64_t seen = s_f_seen_ms;
    follow_sample(s_f_rssi);
    s_f_seen_ms = seen;
}

// a result of the scan task: into the list (a full scan), or into the graph of FOLLOW
static void take_result(void)
{
    int n = s_nback, mode = s_back_mode;
    if (mode == M_MINE) {
        follow_sample(n ? s_back[0].rssi : 0);
        return;
    }
    s_scan_no++;
    for (int i = 0; i < n; i++) {
        const wifi_ap_record_t *r = &s_back[i];
        int k = 0;
        while (k < s_nnet && !key_eq(&s_net[k], r)) k++;
        if (k == s_nnet) {
            if (s_nnet == MAX_NET) continue;
            memset(&s_net[k], 0, sizeof s_net[k]);
            snprintf(s_net[k].ssid, sizeof s_net[k].ssid, "%s", (const char *)r->ssid);
            s_nnet++;
        }
        net_t *t = &s_net[k];
        if (t->seen != s_scan_no) {         // the first access point of this network in this scan
            t->seen = s_scan_no;
            t->n_ap = 0;
            t->rssi = -127;
            t->mine = false;
        }
        t->n_ap++;
        if (s_have_mine && !memcmp(r->bssid, s_my_bssid, 6)) t->mine = true;
        if (r->rssi > t->rssi) {            // the strongest access point gives the values
            t->rssi = r->rssi;
            t->ch = r->primary;
            t->auth = (uint8_t)r->authmode;
            memcpy(t->bssid, r->bssid, 6);
        }
        if (!t->best || t->rssi > t->best) t->best = t->rssi;
        t->miss = 0;
    }
    // FOLLOW: the sample of its network (a scan of its channel)
    if (s_view == V_FOLLOW && mode == M_CHAN) {
        int best = 0;
        for (int i = 0; i < n; i++) {
            const wifi_ap_record_t *r = &s_back[i];
            bool same = s_f_hidden ? !memcmp(r->bssid, s_f_bssid, 6) : !strcmp((const char *)r->ssid, s_f_ssid);
            if (same && (!best || r->rssi > best)) best = r->rssi;
        }
        // one short scan can miss a beacon: the last value stays for 3 misses (no gap in the line)
        if (best) {
            s_f_miss = 0;
            follow_sample(best);
        } else if (++s_f_miss <= 3 && s_f_rssi) {
            follow_sample_hold();
        } else {
            follow_sample(0);
        }
    }
    if (mode == M_FULL) {
        s_last_full_ms = s_now;
        // the networks that this scan did not hear: one more miss; MISS_MAX misses: out of the list
        int w = 0;
        for (int k = 0; k < s_nnet; k++) {
            if (s_net[k].seen != s_scan_no && ++s_net[k].miss >= MISS_MAX) continue;
            s_net[w++] = s_net[k];
        }
        s_nnet = w;
    }
    qsort(s_net, (size_t)s_nnet, sizeof s_net[0], cmp_net);
}

// ---------------------------------------------------------------- small helpers
static bool in_rect(const rect_t *r, int x, int y, int m)
{
    return x >= r->x - m && x < r->x + r->w + m && y >= r->y - m && y < r->y + r->h + m;
}

static uint16_t qual_col(int rssi)
{
    if (rssi >= -55) return rgb(70, 220, 120);
    if (rssi >= -67) return rgb(170, 220, 70);
    if (rssi >= -75) return rgb(245, 200, 60);
    if (rssi >= -85) return rgb(245, 140, 60);
    return rgb(235, 80, 80);
}

static const char *qual_word(int rssi)
{
    if (rssi >= -55) return "EXCELLENT";
    if (rssi >= -67) return "GOOD";
    if (rssi >= -75) return "FAIR";
    if (rssi >= -85) return "WEAK";
    return "VERY WEAK";
}

static const char *auth_name(int a)
{
    switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "ENTERPRISE";
    default: return "SECURE";
    }
}

// the colour of a network in CHANNELS (from its name: the same network keeps its colour)
static uint16_t net_col(const net_t *n)
{
    uint32_t h = 2166136261u;
    for (const char *p = n->ssid[0] ? n->ssid : (const char *)n->bssid; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return hsv((int)(h % 360), 170, 255);
}

// text from the left end x (ui_text_c draws from the centre); cut with ".." when it is wider than w
static void text_l(const kfont_t *f, int x, int base, const char *s, int w, uint16_t col)
{
    char b[40];
    snprintf(b, sizeof b, "%s", s);
    int tw = ui_text_w(f, b);
    for (int l = (int)strlen(b); tw > w && l > 2; ) {
        while (l > 1 && ((uint8_t)b[l - 1] & 0xC0) == 0x80) l--;   // (not in the middle of a UTF-8 letter)
        l--;
        b[l] = 0;
        char c[44];
        snprintf(c, sizeof c, "%s..", b);
        tw = ui_text_w(f, c);
        if (tw <= w) { ui_text_c(f, x + tw / 2, base, c, col); return; }
    }
    ui_text_c(f, x + tw / 2, base, b, col);
}

static void text_r(const kfont_t *f, int xr, int base, const char *s, uint16_t col)
{
    int tw = ui_text_w(f, s);
    ui_text_c(f, xr - tw / 2, base, s, col);
}

static const char *net_name(const net_t *n) { return n->ssid[0] ? n->ssid : "(HIDDEN)"; }

// 4 signal bars, w x h, bottom left at (x, y)
static void draw_bars(int x, int y, int w, int h, int rssi, bool dim)
{
    int lit = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
    uint16_t on = dim ? rgb(90, 96, 110) : qual_col(rssi), off = rgb(44, 50, 64);
    int bw = (w - 6) / 4;
    for (int i = 0; i < 4; i++) {
        int bh = h * (i + 1) / 4;
        gfx_fill_rect(x + i * (bw + 2), y - bh, bw, bh, i < lit ? on : off);
    }
}

// ---------------------------------------------------------------- FOLLOW
static void follow_open(const net_t *n)
{
    snprintf(s_f_ssid, sizeof s_f_ssid, "%s", n->ssid);
    memcpy(s_f_bssid, n->bssid, 6);
    s_f_hidden = !n->ssid[0];
    s_f_mine = n->mine;
    s_f_ch = n->ch;
    s_f_rssi = n->rssi;
    s_f_best = n->rssi;
    s_f_seen_ms = s_now;
    s_hist_n = 0;
    s_f_miss = 0;
    follow_sample(n->rssi);
    s_view = V_FOLLOW;
    // the network in use: a read of its signal, no scan (faster, and the connection stays undisturbed)
    want(n->mine ? M_MINE : M_CHAN, n->ch);
    sfx_play(SFX_CLICK);
}

static void follow_close(void)
{
    s_view = V_NET;
    want(M_FULL, 0);
    sfx_play(SFX_BACK);
}

// ---------------------------------------------------------------- the app functions
static void item_set(int id, int v)
{
    if (id == HI_SORT) {
        s_sort = v;
        qsort(s_net, (size_t)s_nnet, sizeof s_net[0], cmp_net);
    }
    if (id == HI_SOUND) s_sound = v == 1;
}
static const pitems_t PANEL = {ITEMS, 2, NULL, NULL, item_set, NULL, NULL};

static void enter(int64_t now)
{
    s_now = now;
    s_sort = (int)store_get(KEY_SORT, 0);
    if (s_sort < 0 || s_sort > 2) s_sort = 0;
    s_sound = store_get(KEY_SOUND, 1) == 1;
    s_view = V_NET;
    s_scroll = 0;
    s_down = s_dragging = false;
    s_nnet = 0;
    s_scan_no = 0;
    s_last_full_ms = 0;
    s_have_mine = false;
    s_ready = false;
    s_err = 0;
    s_quit = false;
    s_in_scan = false;
    want(M_FULL, 0);
    // the memory: freed in leave() (and by the system when the app closes, if a leave() did not come)
    if (!s_back) s_back = heap_caps_calloc(MAX_REC, sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    if (!s_net) s_net = heap_caps_calloc(MAX_NET, sizeof(net_t), MALLOC_CAP_SPIRAM);
    sysui_panel_items(&PANEL);
    if (!s_back || !s_net) return;
#ifdef ESP_PLATFORM
    s_task_done = false;
    // an internal stack: the WiFi driver code runs in this task
    if (xTaskCreatePinnedToCore(scan_task, "wifiscan", 4096, NULL, 3, &s_task, 0) != pdPASS) s_task = NULL;
#else
    s_next_host = now;
#endif
}

static void leave(void)
{
    s_quit = true;
#ifdef ESP_PLATFORM
    if (s_task) {
        if (s_in_scan) esp_wifi_scan_stop();            // (a blocking scan returns at once)
        for (int i = 0; i < 200 && !s_task_done; i++) vTaskDelay(pdMS_TO_TICKS(10));
        s_task = NULL;
    }
    if (!s_task_done) return;               // (the task still runs: its buffers stay; the system frees them later)
#endif
    free(s_back);
    free(s_net);
    s_back = NULL;
    s_net = NULL;
}

static bool awake(void) { return s_view == V_FOLLOW; }     // FOLLOW: you walk with the board and look at it

static int list_rows(void) { return (s_nnet + CELL_COLS - 1) / CELL_COLS; }
static int scroll_max(void)
{
    int m = list_rows() * ROW_STEP - CELL_GAP - LIST_HT;
    return m > 0 ? m : 0;
}

// the network under a point of the list (-1: none)
static int cell_at(int x, int y)
{
    if (y < LIST_Y || y >= LIST_Y + LIST_HT) return -1;
    int r = (y - LIST_Y + s_scroll) / ROW_STEP, c = (x - CELL_X0) / (CELL_W + 6);
    if (c < 0 || c >= CELL_COLS || x < CELL_X0) return -1;
    int i = r * CELL_COLS + c;
    return i < s_nnet ? i : -1;
}

static int plot_x(float ch) { return PLOT_X0 + (int)((ch + 1.0f) * (PLOT_X1 - PLOT_X0) / 16.0f); }
static int plot_y(int rssi, int y0, int y1)
{
    if (rssi < DB_LO) rssi = DB_LO;
    if (rssi > DB_HI) rssi = DB_HI;
    return y1 - (rssi - DB_LO) * (y1 - y0) / (DB_HI - DB_LO);
}

// CHANNELS: the network whose peak is nearest to a tap (-1: none within 40 px)
static int peak_at(int x, int y)
{
    int best = -1, bd = 40 * 40;
    for (int i = 0; i < s_nnet; i++) {
        int dx = plot_x(s_net[i].ch) - x, dy = plot_y(s_net[i].rssi, PLOT_Y0, PLOT_Y1) - y;
        if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = i; }
    }
    return best;
}

static void input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_dragging = false;
        s_x0 = ev->x;
        s_y0 = ev->y;
        s_scroll0 = s_scroll;
        break;
    case IN_MOVE:
        if (s_down && s_view == V_NET && !s_dragging && abs(ev->y - s_y0) > 10 && s_y0 >= LIST_Y) {
            s_dragging = true;
            input_tap_cancel();             // the scroll wins: this touch gives no tap
        }
        if (s_dragging) {
            s_scroll = s_scroll0 - (ev->y - s_y0);
            if (s_scroll < 0) s_scroll = 0;
            if (s_scroll > scroll_max()) s_scroll = scroll_max();
        }
        break;
    case IN_UP:
        s_down = false;
        break;
    case IN_TAP:
        // a precise target acts when the finger comes off the glass (IN_TAP)
        if (s_view == V_FOLLOW) {
            if (in_rect(&R_BACK, ev->x, ev->y, 6)) follow_close();
            break;
        }
        if (in_rect(&R_TAB_NET, ev->x, ev->y, 4)) { if (s_view != V_NET) sfx_play(SFX_TICK); s_view = V_NET; break; }
        if (in_rect(&R_TAB_CH, ev->x, ev->y, 4)) { if (s_view != V_CHAN) sfx_play(SFX_TICK); s_view = V_CHAN; break; }
        {
            int i = s_view == V_NET ? cell_at(ev->x, ev->y) : peak_at(ev->x, ev->y);
            if (i >= 0) follow_open(&s_net[i]);
        }
        break;
    default:
        break;
    }
}

static void update(int64_t now)
{
    s_now = now;
#ifndef ESP_PLATFORM
    if (!s_ready && now >= s_next_host && s_back) {
        int mode = s_want_mode;
        scan_step();
        s_next_host = now + (s_err ? 400 : step_pause(mode));
    }
#endif
    if (s_ready) {
        __sync_synchronize();               // (the writes of the task come before the flag)
        if (s_net) take_result();
        s_ready = false;
    }
    if (s_scroll > scroll_max()) s_scroll = scroll_max();
    // FOLLOW: a tick, faster and higher with a stronger signal (a sound to walk with)
    if (s_view == V_FOLLOW && s_sound && now - s_f_seen_ms < 2500) {
        int r = s_f_rssi < -95 ? -95 : s_f_rssi > -35 ? -35 : s_f_rssi;
        int gap = 1100 - (r + 95) * 16;     // -95 dBm: 1.1 s, -35 dBm: 0.14 s
        if (now - s_tick_ms >= gap) {
            s_tick_ms = now;
            uint16_t f = (uint16_t)(500 + (r + 95) * 18);
            sfx_tone(f, f, 22, W_SINE, 40);
        }
    }
}

// ---------------------------------------------------------------- drawing
#define BG rgb(16, 20, 26)                  // (rgb(): the byte order of the panel; never a raw 16-bit number)

static void tab(const rect_t *r, const char *t, bool cur)
{
    ui_rbox(r->x, r->y, r->w, r->h, 12, cur ? rgb(40, 96, 170) : rgb(26, 32, 44), cur ? rgb(110, 170, 255) : rgb(52, 62, 82), 2);
    ui_text_c(&KF_NAME, r->x + r->w / 2, r->y + r->h / 2 + 6, t, cur ? rgb(255, 255, 255) : rgb(170, 180, 200));
}

static void draw_status(void)
{
    char b[48];
    uint16_t col = rgb(130, 140, 160);
    if (s_err == ESP_ERR_WIFI_NOT_STARTED) { snprintf(b, sizeof b, "WIFI IS OFF (SETUP > WIFI)"); col = rgb(245, 170, 70); }
    else if (s_err == 1) snprintf(b, sizeof b, "THE BOARD CONNECTS: WAIT");
    else if (s_err) { snprintf(b, sizeof b, "NO SCAN NOW (0x%x)", s_err); col = rgb(245, 170, 70); }
    else if (!s_scan_no) snprintf(b, sizeof b, "SCANNING...");
    else {
        int ago = (int)((s_now - s_last_full_ms) / 1000);
        snprintf(b, sizeof b, ago < 2 ? "LIVE" : "%d S AGO", ago);
        if (s_in_scan) snprintf(b, sizeof b, "SCANNING");
    }
    text_r(&KF_LABEL, STATUS_X, STATUS_Y, b, col);
    if (s_in_scan) {                        // a dot that moves while the radio scans
        int w = ui_text_w(&KF_LABEL, b), p = (int)((s_now / 120) % 3);
        for (int i = 0; i < 3; i++) gfx_fill_circle(STATUS_X - w - 22 + i * 7, STATUS_Y - 4, 2, i == p ? rgb(110, 170, 255) : rgb(60, 70, 90));
    }
}

static void draw_list(void)
{
    for (int i = 0; i < s_nnet; i++) {
        int r = i / CELL_COLS, c = i % CELL_COLS;
        int x = CELL_X0 + c * (CELL_W + 6), y = LIST_Y + r * ROW_STEP - s_scroll;
        if (y + CELL_H < LIST_Y || y >= LIST_Y + LIST_HT) continue;
        const net_t *n = &s_net[i];
        bool old = n->miss > 0;             // the last scan did not hear it
        ui_rbox(x, y, CELL_W, CELL_H, 10, rgb(26, 32, 44), n->mine ? rgb(70, 200, 120) : rgb(40, 48, 64), n->mine ? 2 : 1);
        draw_bars(x + 10, y + CELL_H - 9, 30, 24, n->rssi, old);
        text_l(&KF_NAME, x + 48, y + 19, net_name(n), CELL_W - 48 - 64, old ? rgb(120, 126, 140) : n->ssid[0] ? rgb(240, 244, 250) : rgb(150, 160, 180));
        char b[48];
        snprintf(b, sizeof b, "CH %d  %s", n->ch, auth_name(n->auth));
        if (n->n_ap > 1) snprintf(b + strlen(b), sizeof b - strlen(b), "  x%d", n->n_ap);   // (more access points)
        if (n->mine) snprintf(b + strlen(b), sizeof b - strlen(b), "  IN USE");
        text_l(&KF_LABEL, x + 48, y + CELL_H - 5, b, CELL_W - 48 - 64, n->mine ? rgb(110, 220, 150) : rgb(130, 140, 160));
        snprintf(b, sizeof b, "%d", n->rssi);
        int w = mf_width(&MF_N20, b, 1);
        mf_text(&MF_N20, x + CELL_W - 12 - w, y + 27, b, old ? rgb(110, 116, 130) : qual_col(n->rssi), 255, 1);
        mf_text_c(&MF_TINY, x + CELL_W - 12 - w / 2, y + CELL_H - 2, "DBM", rgb(110, 120, 140), 255, 0);
    }
    // the bars above and under the list cover the cells that scroll out (render() draws the tabs after the list)
    gfx_fill_rect(0, 0, gfx_w, LIST_Y - 2, BG);
    if (LIST_Y + LIST_HT < gfx_h) gfx_fill_rect(0, LIST_Y + LIST_HT, gfx_w, gfx_h - LIST_Y - LIST_HT, BG);
    if (!s_nnet && s_scan_no) ui_text_c(&KF_TEXT, gfx_w / 2, LIST_Y + LIST_HT / 2, "NO NETWORK IN RANGE", rgb(150, 160, 180));
    // the scroll bar
    int sm = scroll_max();
    if (sm > 0) {
        int th = LIST_HT * LIST_HT / (LIST_HT + sm), ty = LIST_Y + (LIST_HT - th) * s_scroll / sm;
        ui_rrect(gfx_w - 4, ty, 3, th, 1, rgb(90, 100, 130));
    }
}

static void draw_channels(void)
{
    // the grid: dBm lines and the 13 channels
    for (int d = -90; d <= -30; d += 20) {
        int y = plot_y(d, PLOT_Y0, PLOT_Y1);
        gfx_fill_rect(PLOT_X0, y, PLOT_X1 - PLOT_X0, 1, rgb(34, 40, 54));
        char b[8];
        snprintf(b, sizeof b, "%d", d);
        mf_text(&MF_TINY, 2, y + 4, b, rgb(100, 110, 130), 255, 0);
    }
    gfx_fill_rect(PLOT_X0, PLOT_Y1, PLOT_X1 - PLOT_X0, 1, rgb(70, 80, 100));
    int busy[15] = {0};
    for (int i = 0; i < s_nnet; i++) if (s_net[i].ch >= 1 && s_net[i].ch <= 14) busy[s_net[i].ch]++;
    int my_ch = 0;
    for (int i = 0; i < s_nnet; i++) if (s_net[i].mine) my_ch = s_net[i].ch;
    for (int c = 1; c <= 13; c++) {
        char b[4];
        snprintf(b, sizeof b, "%d", c);
        uint16_t col = c == my_ch ? rgb(110, 220, 150) : busy[c] ? rgb(200, 206, 220) : rgb(90, 100, 120);
        mf_text_c(&MF_LABEL, plot_x((float)c), CHAN_Y, b, col, 255, 0);
    }
    // each network: a curve over its 20 MHz (its channel +- 2), the weak ones first (the strong ones on top)
    for (int i = s_nnet - 1; i >= 0; i--) {
        const net_t *n = &s_net[i];
        if (n->ch < 1 || n->ch > 14) continue;
        uint16_t col = net_col(n), dark = ui_mix(col, BG, 60);
        int xc = plot_x(n->ch), half = plot_x(n->ch + 2.0f) - xc, top = plot_y(n->rssi, PLOT_Y0, PLOT_Y1);
        int h = PLOT_Y1 - top;
        for (int dx = -half; dx <= half; dx++) {
            float u = (float)dx / (float)half;
            int y = PLOT_Y1 - (int)(h * (1.0f - u * u));
            if ((dx & 3) == 0) gfx_fill_rect(xc + dx, y + 2, 1, PLOT_Y1 - y - 2, dark);   // a light fill: thin lines
            gfx_fill_rect(xc + dx, y, 1, 2, col);
        }
    }
    // the names at the peaks: the strongest first, no name over another one
    rect_t used[12];
    int nu = 0;
    int order[MAX_NET], no = 0;
    for (int i = 0; i < s_nnet; i++) order[no++] = i;
    for (int a = 1; a < no; a++)
        for (int b = a; b > 0 && s_net[order[b]].rssi > s_net[order[b - 1]].rssi; b--) { int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t; }
    for (int k = 0; k < no && nu < 12; k++) {
        const net_t *n = &s_net[order[k]];
        if (n->ch < 1 || n->ch > 14) continue;
        char nm[16];
        snprintf(nm, sizeof nm, "%s", net_name(n));
        int w = ui_text_w(&KF_LABEL, nm), xc = plot_x(n->ch), y = plot_y(n->rssi, PLOT_Y0, PLOT_Y1) - 5;
        rect_t r = {xc - w / 2 - 2, y - 13, w + 4, 16};
        if (r.x < PLOT_X0) r.x = PLOT_X0;
        if (r.x + r.w > PLOT_X1) r.x = PLOT_X1 - r.w;
        if (r.y < PLOT_Y0 - 6) continue;
        bool hit = false;
        for (int j = 0; j < nu; j++)
            hit |= r.x < used[j].x + used[j].w && used[j].x < r.x + r.w && r.y < used[j].y + used[j].h && used[j].y < r.y + r.h;
        if (hit) continue;
        used[nu++] = r;
        ui_text_c(&KF_LABEL, r.x + r.w / 2, r.y + 13, nm, net_col(n));
    }
    if (!s_nnet && s_scan_no) ui_text_c(&KF_TEXT, gfx_w / 2, (PLOT_Y0 + PLOT_Y1) / 2, "NO NETWORK IN RANGE", rgb(150, 160, 180));
}

static void draw_follow(void)
{
    // the back key and the name
    ui_rbox(R_BACK.x, R_BACK.y, R_BACK.w, R_BACK.h, 12, rgb(26, 32, 44), rgb(70, 84, 110), 2);
    ui_text_c(&KF_NAME, R_BACK.x + R_BACK.w / 2, R_BACK.y + R_BACK.h / 2 + 6, "< BACK", rgb(220, 226, 240));
    text_l(&KF_BIG, TITLE_X, TITLE_Y, s_f_hidden ? "(HIDDEN)" : s_f_ssid, TITLE_W, rgb(250, 250, 255));
    bool lost = s_now - s_f_seen_ms > 4000;  // not heard for 4 s
    int r = s_f_rssi;
    char b[48];
    // the big number
    if (lost) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", r);
    uint16_t qc = lost ? rgb(120, 126, 140) : qual_col(r);
    mf_text_oc(&MF_N34, BIG_CX - 14, BIG_Y, b, qc, rgb(8, 10, 14), 2, 2);
    int bw = mf_width(&MF_N34, b, 2);
    mf_text(&MF_LABEL, BIG_CX - 14 + bw / 2 + 6, BIG_Y, "DBM", rgb(140, 150, 170), 255, 0);
    ui_text_c(&KF_TEXT, BIG_CX, QUAL_Y, lost ? "NOT HEARD" : qual_word(r), qc);
    // the meter
    int mx = BIG_CX - METER_W / 2, mw = METER_W;
    ui_rrect(mx, METER_Y, mw, 12, 6, rgb(34, 40, 54));
    if (!lost) {
        int f = (r - DB_LO) * mw / (DB_HI - DB_LO);
        if (f > mw) f = mw;
        if (f >= 12) ui_rrect(mx, METER_Y, f, 12, 6, qc);
    }
    // the facts
    const net_t *n = NULL;
    for (int i = 0; i < s_nnet; i++)
        if (s_f_hidden ? !memcmp(s_net[i].bssid, s_f_bssid, 6) : !strcmp(s_net[i].ssid, s_f_ssid)) n = &s_net[i];
    snprintf(b, sizeof b, "CH %d  %s%s", s_f_ch, n ? auth_name(n->auth) : "", s_f_mine ? "  IN USE" : "");
#if BOARD_BAR_SCREEN
    text_r(&KF_LABEL, 634, TITLE_Y - 2, b, s_f_mine ? rgb(110, 220, 150) : rgb(150, 160, 180));
    snprintf(b, sizeof b, "BEST %d DBM", s_f_best);
    ui_text_c(&KF_LABEL, BIG_CX, BEST_Y, b, rgb(150, 160, 180));
#else
    text_r(&KF_LABEL, 312, 62, b, s_f_mine ? rgb(110, 220, 150) : rgb(150, 160, 180));
    snprintf(b, sizeof b, "BEST %d", s_f_best);
    text_r(&KF_LABEL, 312, 84, b, rgb(150, 160, 180));
    (void)BEST_Y;
#endif
    // the graph: the bands of the quality, then the line of the samples (the newest at the right)
    ui_rbox(GRAPH_X0, GRAPH_Y0, GRAPH_X1 - GRAPH_X0, GRAPH_Y1 - GRAPH_Y0, 8, rgb(20, 24, 32), rgb(44, 52, 70), 1);
    static const int LINES[] = {-55, -67, -75, -85};
    for (int i = 0; i < 4; i++) {
        int y = plot_y(LINES[i], GRAPH_Y0 + 4, GRAPH_Y1 - 4);
        for (int x = GRAPH_X0 + 4; x < GRAPH_X1 - 4; x += 6) gfx_fill_rect(x, y, 3, 1, ui_mix(qual_col(LINES[i]), BG, 70));
    }
    int gw = GRAPH_X1 - GRAPH_X0 - 8;
    int px = -1, py = 0;
    for (int i = 0; i < s_hist_n; i++) {
        int x = GRAPH_X1 - 4 - (s_hist_n - 1 - i) * gw / (HIST_N - 1);
        if (!s_hist[i]) { px = -1; continue; }
        int y = plot_y(s_hist[i], GRAPH_Y0 + 4, GRAPH_Y1 - 4);
        uint16_t c = qual_col(s_hist[i]);
        if (px >= 0) {                      // a line of 2 px from the last point
            int x0 = px, y0 = py, steps = abs(x - x0) > abs(y - y0) ? abs(x - x0) : abs(y - y0);
            for (int k = 0; k <= steps; k++)
                gfx_fill_rect(x0 + (x - x0) * k / (steps ? steps : 1), y0 + (y - y0) * k / (steps ? steps : 1), 2, 2, c);
        } else {
            gfx_fill_rect(x, y, 2, 2, c);
        }
        px = x;
        py = y;
    }
    if (px >= 0 && !lost) gfx_fill_circle(px, py, 4, qc);
}

static void render(int64_t now)
{
    s_now = now;
    gfx_fill(BG);                           // the whole frame, first
    if (s_view == V_FOLLOW) {
        draw_follow();
        return;
    }
    if (s_view == V_NET) draw_list();       // (first: the cells that scroll out go under the tabs)
    else draw_channels();
    char t[24];
    snprintf(t, sizeof t, s_nnet ? "NETWORKS %d" : "NETWORKS", s_nnet);
    tab(&R_TAB_NET, t, s_view == V_NET);
    tab(&R_TAB_CH, "CHANNELS", s_view == V_CHAN);
    draw_status();
}

// The menu icon: the WiFi fan; its 3 arcs light up one by one from the dot (a loop of 2 s). Only gfx_* / ui_* shapes;
// the whole box.
static void icon(int x, int y, int s)
{
    float u = fmodf(icon_time(), 2.0f);
    gfx_fill_rect(x, y, s, s, rgb(18, 34, 60));
    int cx = x + s / 2, cy = y + s * 78 / 100;
    int lit = (int)(u / 0.4f);              // 0..4: the dot, then the arcs; at 4-5 all of them
    uint16_t on = rgb(120, 220, 255), off = rgb(44, 70, 104);
    gfx_fill_circle(cx, cy, s / 14 > 2 ? s / 14 : 2, rgb(255, 255, 255));
    for (int a = 1; a <= 3; a++) {
        float r = s * (0.18f + 0.15f * a);
        uint16_t c = lit >= a ? on : off;
        int dots = 10 + a * 6, rad = s / 26 > 1 ? s / 26 : 1;
        for (int k = 0; k <= dots; k++) {
            float ang = -2.36f + 1.57f * k / dots;          // -135 to -45 degrees: the upper quarter
            gfx_fill_circle(cx + (int)(cosf(ang) * r), cy + (int)(sinf(ang) * r), rad, c);
        }
    }
}

const app_t app_wifiscan = {
    .name = "WIFI SCAN",
    .landscape = true,                      // 640 x 172 on the 3.49, 320 x 240 on the other boards
    .enter = enter,
    .leave = leave,
    .input = input,
    .update = update,
    .render = render,
    .draw_icon = icon,
    .awake = awake,
    .round_fit = true,
};
