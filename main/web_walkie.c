// The WALKIE and FRIENDS part of the board web page, and its audio (see web_walkie.h).
//
// /ws/audio (WebSocket). Text "listen 1" / "listen 0": this board sends its microphone to that
// browser (16-bit mono PCM, 8 kHz, 400 samples = 50 ms in each binary frame; one browser at a
// time). A binary frame from a browser (the same format) plays on the speaker (voice stream of
// audio.c, after a start buffer of 120 ms).
// /api/boards: this board and the boards on the LAN. /api/walkie: the WALKIE state of this
// board; /api/walkie?set=hear_them|they_hear_me&on=0|1 sets a switch (as a tap on the board).

#include <stdio.h>
#include "web_util.h"
#include "tfn_extras.h"
#include "tfn_util.h"
#include "arc_mac.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "settings.h"
#include "input.h"
#include "friends.h"
#include "gfx.h"
#include "lan.h"
#include "link.h"
#include "esp_http_server.h"
#include "web_walkie.h"
#include "webcam.h"
#include "ota.h"
#include "wifi.h"
#include "vpn.h"
#include "esp_wifi.h"
#include "power.h"
#include "tfn_defaults.h"

static const char *TAG = "webwalkie";

#define PKT_N    400  // samples at 8 kHz in a frame (50 ms)
#define RING_N   8192
#define START_MS 120

static httpd_handle_t s_hd;
static volatile int s_listen_fd = -1;
static volatile int64_t s_talk_at;
static int16_t *s_ring;
static volatile uint32_t s_w, s_r;
static int s_acc, s_cnt;
static esp_timer_handle_t s_tmr;

bool web_audio_active(void)
{
    return s_listen_fd >= 0 || (s_talk_at && tfn_now_ms() - s_talk_at < 2000);
}

// ---------------------------------------------------------------- the microphone to a browser

// the microphone task: 24 kHz to 8 kHz (the mean of 3, gain 2, as WALKIE)
static void web_tap(const int16_t *s, int n)
{
    uint32_t w = s_w;
    for (int i = 0; i < n; i++) {
        s_acc += s[i];
        if (++s_cnt < 3) {
            continue;
        }
        int v = s_acc * 2 / 3;
        s_acc = s_cnt = 0;
        if (v > 32767) {
            v = 32767;
        }
        if (v < -32768) {
            v = -32768;
        }
        if (w - s_r < RING_N) {
            s_ring[w++ % RING_N] = (int16_t)v;
        }
    }
    s_w = w;
}

static void listen_stop(void)
{
    if (s_listen_fd < 0) {
        return;
    }
    s_listen_fd = -1;
    audio_mic_web(NULL);
    esp_timer_stop(s_tmr);
    ESP_LOGI(TAG, "listen: off");
}

// the web server task: the whole frames of the ring to the browser
static void send_work(void *arg)
{
    int fd = s_listen_fd;
    if (fd < 0) {
        return;
    }
    if (httpd_ws_get_fd_info(s_hd, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        listen_stop();
        return;
    }
    static int16_t pkt[PKT_N];
    while (s_w - s_r >= PKT_N) {
        for (int i = 0; i < PKT_N; i++) {
            pkt[i] = s_ring[s_r++ % RING_N];
        }
        httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_BINARY, .payload = (uint8_t *)pkt, .len = sizeof pkt};
        if (httpd_ws_send_frame_async(s_hd, fd, &f) != ESP_OK) {
            listen_stop();
            return;
        }
    }
}

static void tmr_cb(void *arg)
{
    if (s_listen_fd >= 0) {
        httpd_queue_work(s_hd, send_work, NULL);
    }
}

// the browser listens or talks: WALKIE opens on the board (from any app; not during a match with another board: that
// would end the match on both boards. The browser then hears the board, but WALKIE does not open)
static void open_walkie(void)
{
    if (app_current() != &app_walkie && !app_in_match()) {
        app_open(&app_walkie);
    }
}

static void listen_start(int fd)
{
    open_walkie();
    s_r = s_w;
    s_acc = s_cnt = 0;
    s_listen_fd = fd;
    audio_mic_web(web_tap);
    esp_timer_stop(s_tmr);
    esp_timer_start_periodic(s_tmr, 50000);
    ESP_LOGI(TAG, "listen: on (a browser hears the microphone of this board)");
}

// ---------------------------------------------------------------- a browser to the speaker

static int16_t s_last;
static void play8(const int16_t *x, int n)
{
    static int16_t out[PKT_N * 3];
    while (n > 0) {
        int m = n > PKT_N ? PKT_N : n, k = 0;
        for (int i = 0; i < m; i++) {
            int a = s_last, b = x[i];
            out[k++] = (int16_t)((2 * a + b) / 3);
            out[k++] = (int16_t)((a + 2 * b) / 3);
            out[k++] = (int16_t)b;
            s_last = x[i];
        }
        audio_stream_write(out, k);
        x += m;
        n -= m;
    }
}

static void talk_pcm(const int16_t *x, int n)
{
    int64_t t = tfn_now_ms();
    if (!s_talk_at || t - s_talk_at > 400) {
        open_walkie();
        // a new talk: the start buffer
        static const int16_t zero[PKT_N] = {0};
        audio_stream_clear();
        audio_stream_gain(512);
        s_last = 0;
        for (int left = 8000 * START_MS / 1000; left > 0; left -= PKT_N) {
            play8(zero, left > PKT_N ? PKT_N : left);
        }
    }
    s_talk_at = t;
    if (audio_stream_fill() > AUDIO_SAMPLE_RATE * 4 / 10) {
        return;  // keep the buffer short
    }
    play8(x, n);
}

// ---------------------------------------------------------------- the handlers

static esp_err_t ws_audio(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        return ESP_OK;  // the handshake
    }
    httpd_ws_frame_t f = {0};
    esp_err_t e = httpd_ws_recv_frame(req, &f, 0);
    if (e != ESP_OK) {
        return e;
    }
    if (f.len > 4096) {
        return ESP_FAIL;
    }
    uint8_t *buf = heap_caps_malloc(f.len + 2, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    f.payload = buf;
    e = f.len ? httpd_ws_recv_frame(req, &f, f.len) : ESP_OK;
    int fd = httpd_req_to_sockfd(req);
    if (e == ESP_OK) {
        if (f.type == HTTPD_WS_TYPE_TEXT) {
            buf[f.len] = 0;
            if (!strcmp((char *)buf, "listen 1")) {
                listen_start(fd);
            } else if (!strcmp((char *)buf, "listen 0") && fd == s_listen_fd) {
                listen_stop();
            }
        } else if (f.type == HTTPD_WS_TYPE_BINARY && f.len >= 2) {
            int16_t tmp[PKT_N * 2];
            int n = (int)(f.len / 2) > PKT_N * 2 ? PKT_N * 2 : (int)(f.len / 2);
            memcpy(tmp, buf, (size_t)n * 2);
            talk_pcm(tmp, n);
        } else if (f.type == HTTPD_WS_TYPE_CLOSE && fd == s_listen_fd) {
            listen_stop();
        }
    }
    free(buf);
    return e;
}

// the kind of board (the build target: the full name in boards/<kind>/board_caps.h)
#define BOARD_KIND BOARD_NAME

#if CONFIG_TFN_EXTRA_VPN
// a key in JSON: base64 has no quote, but a bad value must not break the JSON
static const char *key_or_empty(const char *k)
{
    for (const char *p = k; *p; p++) {
        if (*p == '"' || *p == '\\' || (unsigned char)*p < 32) {
            return "";
        }
    }
    return k;
}
#endif

static void cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
}

static void json_str(char *out, int max, const char *s)
{
    int k = 0;
    for (; *s && k < max - 1; s++) {
        out[k++] = (*s == '"' || *s == '\\' || (unsigned char)*s < 32) ? ' ' : *s;
    }
    out[k] = 0;
}

#define BOARDS_JSON 4608  // (the board and up to LAN_PEERS boards of the LAN and the VPN, and the last OTA fault)
static esp_err_t api_boards(httpd_req_t *req)
{
    char *b = heap_caps_malloc(BOARDS_JSON, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    char name[20], app[16];
    json_str(name, sizeof name, friends_my_name());
    const app_t *a = app_current();
    json_str(app, sizeof app, a ? a->name : "");
    int n = snprintf(b, BOARDS_JSON,
                     "{\"me\":{\"name\":\"%s\",\"ip\":\"%s\",\"app\":\"%s\",\"screen\":\"%s\",\"books\":%s,\"cam\":%s,"
                     "\"board\":\"%s\",",
                     name, wifi_ip(), app, app_screen_state(), ARCADE_HAS_BOOKS ? "true" : "false",
                     webcam_supported() ? "true" : "false", BOARD_KIND);
    n += ota_json(b + n, BOARDS_JSON - n);  // the firmware: build time, slot, OTA ready, board tag
    bool chg = false;
    int mv = 0, pct = board_battery(&chg, &mv);
    n += snprintf(b + n, BOARDS_JSON - n, ",\"bat\":%d,\"chg\":%s,\"mv\":%d", pct, chg ? "true" : "false",
                  mv);  // (-1: no battery)
    char prof[48];
    json_str(prof, sizeof prof, menu_profile());
    n += snprintf(b + n, BOARDS_JSON - n, ",\"prof\":\"%s\",\"profh\":%lu,\"menuv\":3,\"home\":%s", prof,
                  (unsigned long)menu_profile_hash(), menu_home_own() ? "true" : "false");  // (the menu profile)
    n += snprintf(b + n, BOARDS_JSON - n, ",\"pwoff\":%d,\"pwdeep\":%d,\"rr\":%d", power_off_choice(),
                  power_deep_choice(), (int)esp_reset_reason());  // (POWER OFF 0..5; DEEP IDLE 0..3; the last reset)
    // While a game of 2 BOARDS runs, the manager does not read the board and takes no picture (link.h).
    n += snprintf(b + n, BOARDS_JSON - n, ",\"linked\":%s", link_in_use() ? "true" : "false");
    n += snprintf(b + n, BOARDS_JSON - n, "},\"lan\":[");
    static lan_peer_t p[LAN_PEERS];  // (one web server task)
    int k = lan_peers(p, LAN_PEERS);
    for (int i = 0; i < k && n < BOARDS_JSON - 140; i++) {
        uint32_t ip = p[i].ip;
        json_str(name, sizeof name, p[i].name);
        json_str(app, sizeof app, p[i].app);
        n += snprintf(b + n, BOARDS_JSON - n, "%s{\"name\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"app\":\"%s\"}", i ? "," : "",
                      name, (unsigned)(ip & 255), (unsigned)((ip >> 8) & 255), (unsigned)((ip >> 16) & 255),
                      (unsigned)(ip >> 24), app);
    }
    n += snprintf(b + n, BOARDS_JSON - n, "]}");
    cors(req);
    httpd_resp_send(req, b, n);
    free(b);
    return ESP_OK;
}

// GET /api/screen: the next frame of the screen for the manager page. The body: RGB565, high byte first,
// gfx_w x gfx_h (the headers X-W and X-H); ?s=2 or ?s=4: every second (fourth) pixel of each second
// (fourth) row, w/s x h/s and the header X-S: s (the 466 x 466 frame of the 1.75C is 434 KB: 1.2-1.8 s on its WiFi, and this server answers one
// request at a time). No picture: 200 with no body and the header X-Screen: off (the screen is
// off: the main loop draws no frame then) or busy (no frame in 1 s). The answer is not a 503, because the browser
// console of the manager page shows a 503 as an error each 1.2 s.
static esp_err_t screen_state(httpd_req_t *req, const char *st)
{
    httpd_resp_set_hdr(req, "X-Screen", st);
    httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Screen");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
static esp_err_t api_screen(httpd_req_t *req)
{
    extern volatile int g_shot_req, g_shot_ready, g_shot_w, g_shot_h, g_shot_busy;
    extern uint16_t *g_shot_buf;
    cors(req);
    if (!strcmp(app_screen_state(), "off")) {
        return screen_state(req, "off");
    }
    int sc = 1;
    char q[24], v[4];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK &&
        httpd_query_key_value(q, "s", v, sizeof v) == ESP_OK && (v[0] == '2' || v[0] == '4')) {
        sc = v[0] - '0';
    }
    __atomic_add_fetch(&g_shot_busy, 1, __ATOMIC_SEQ_CST);  // (the main loop keeps the buffer while we use it)
    g_shot_ready = 0;
    g_shot_req = 1;
    for (int i = 0; i < 100 && !g_shot_ready; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!g_shot_ready) {
        __atomic_sub_fetch(&g_shot_busy, 1, __ATOMIC_SEQ_CST);
        return screen_state(req, "busy");
    }
    int sw = g_shot_w, sh = g_shot_h;
    if (sc > 1) {
        // (in place: each target pixel is at or before its source; the next request takes a new copy)
        sw /= sc;
        sh /= sc;
        for (int y = 0; y < sh; y++) {
            for (int x = 0; x < sw; x++) {
                g_shot_buf[y * sw + x] = g_shot_buf[sc * y * g_shot_w + sc * x];
            }
        }
    }
#if BOARD_ROUND
    // (a round frame: only the circle shows on the glass, and a menu or an app does not paint the corners
    // in each frame: black there, not old pixels)
    if (gfx_round() && sw == sh) {
        const float r = sw / 2.0f;
        for (int y = 0; y < sh; y++) {
            const float dy = y + 0.5f - r;
            const int hw = dy * dy < r * r ? (int)sqrtf(r * r - dy * dy) + 1 : 0, x0 = (int)r - hw, x1 = (int)r + hw;
            for (int x = 0; x < sw; x++) {
                if (x < x0 || x >= x1) {
                    g_shot_buf[y * sw + x] = 0;
                }
            }
        }
    }
#endif
    char w[12], h[12];
    snprintf(w, sizeof w, "%d", sw);
    snprintf(h, sizeof h, "%d", sh);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "X-W", w);
    httpd_resp_set_hdr(req, "X-H", h);
    httpd_resp_set_hdr(req, "X-S", sc == 4 ? "4" : sc == 2 ? "2" : "1");
    httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-W, X-H, X-S");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
#if BOARD_EVLOG
    extern volatile uint32_t g_ev_screen_bytes;  // (board_tab5.c: the event log line of each second)
    g_ev_screen_bytes += (uint32_t)sw * sh * 2;
#endif
    httpd_resp_send(req, (const char *)g_shot_buf, (ssize_t)sw * sh * 2);
    __atomic_sub_fetch(&g_shot_busy, 1, __ATOMIC_SEQ_CST);
    return ESP_OK;
}

// a finger of the manager page at logical x y (the real touch path: the apps and the end buttons see it)
static void page_finger(int x, int y, int ms)
{
    int px, py;
    gfx_logical_to_touch(x, y, &px, &py);
    input_virtual_finger(px, py, esp_timer_get_time() / 1000 + ms);
}

// The manager controls the board: ?open=<app name> (the app opens and the screen comes on), ?menu=1 (the main
// menu), ?screen=1 / 0 (the screen on / off), and a finger on the screen picture of the page:
// ?tap=x,y, ?long=x,y (0.8 s), ?swipe=x1,y1,x2,y2 (0.3 s), in logical screen points. The answer: the app now, the
// screen, the names of all apps, and "menu": the apps of this board in the menu order with their group.
// GET /api/menu: the menu layout now (text, menu_tree.c); POST /api/menu: a new layout (the body), or RESET
static esp_err_t api_menu_get(httpd_req_t *req)
{
    char *b = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    int n = menu_layout_get(b, 4096);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, b, n);
    free(b);
    return ESP_OK;
}
static esp_err_t api_menu_post(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 4096) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"the size of the layout\"}");
    }
    char *b = heap_caps_malloc((size_t)len + 1, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, b + got, (size_t)(len - got));
        if (r <= 0) {
            free(b);
            return ESP_FAIL;
        }
        got += r;
    }
    char err[64] = "", out[160], q[24];
    // ?force=1: the manager has the last word: the own home of the board goes (HOME 2.0)
    bool force = httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK && strstr(q, "force=1");
    bool ok = menu_layout_set(b, len, err, sizeof err);
    if (ok && force) {
        menu_home_drop();
    }
    free(b);
    char e2[80];
    json_str(e2, sizeof e2, err);
    snprintf(out, sizeof out, "{\"ok\":%s,\"msg\":\"%s\",\"profh\":%lu}", ok ? "true" : "false", e2,
             (unsigned long)menu_profile_hash());
    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, out);
}

static void url_decode(char *s);
static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}
static void poweroff_cb(void *arg)
{
    (void)arg;
    power_off_ask();
}

// bright=<10..255> and vol=<0..AUDIO_VOL_MAX> of /api/ctl: the BRIGHTNESS and the VOLUME from the manager. It does the
// same as the sliders of the system panel (sysui_panel.c): the new value is live at once, a change of the volume ends
// MUTE, and settings_save() keeps the values. A dim or dark screen (the screen saver) stays as it is: the wake gives the
// new brightness. A board in "quiet" (the speaker off) stays silent.
static void ctl_levels(const char *q)
{
    char v[8];
    bool chg = false;
    if (httpd_query_key_value(q, "bright", v, sizeof v) == ESP_OK) {
        int n = atoi(v);
        n = n < 10 ? 10 : n > 255 ? 255 : n;
        g_settings.brightness = (uint8_t)n;
        if (!strcmp(app_screen_state(), "on")) {
            board_backlight((uint8_t)n);
        }
        chg = true;
    }
    if (httpd_query_key_value(q, "vol", v, sizeof v) == ESP_OK) {
        int n = atoi(v);
        n = n < 0 ? 0 : n > AUDIO_VOL_MAX ? AUDIO_VOL_MAX : n;
        g_settings.volume = (uint8_t)n;
        if (g_settings.mute) {
            g_settings.mute = false;
            audio_set_mute(false);
        }
        audio_set_volume(n);
        chg = true;
    }
    if (chg) {
        settings_save();
    }
}

static esp_err_t api_ctl(httpd_req_t *req)
{
    char q[96], v[32];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) {
        int x1, y1, x2, y2;
        if (httpd_query_key_value(q, "tap", v, sizeof v) == ESP_OK && sscanf(v, "%d,%d", &x1, &y1) == 2) {
            page_finger(x1, y1, 90);
        }
        if (httpd_query_key_value(q, "long", v, sizeof v) == ESP_OK && sscanf(v, "%d,%d", &x1, &y1) == 2) {
            page_finger(x1, y1, 800);
        }
        if (httpd_query_key_value(q, "swipe", v, sizeof v) == ESP_OK &&
            sscanf(v, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4) {
            // the finger moves in 15 steps of 20 ms (the web task waits: about 0.3 s)
            for (int i = 0; i <= 15; i++) {
                page_finger(x1 + (x2 - x1) * i / 15, y1 + (y2 - y1) * i / 15, i < 15 ? 200 : 40);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        if (httpd_query_key_value(q, "open", v, sizeof v) == ESP_OK) {
            url_decode(v);  // (LAST%20SHOW: the names with a space)
            const app_t *a = app_find(v);
            if (a) {
                app_open(a);
                app_screen_wake();
            }
        }
        if (httpd_query_key_value(q, "menu", v, sizeof v) == ESP_OK) {
            app_open(&app_menu);
            app_screen_wake();
        }
        // quiet=1: the speaker off (silent tests from the PC), quiet=0: the settings again (as the console "quiet")
        if (httpd_query_key_value(q, "quiet", v, sizeof v) == ESP_OK) {
            bool qq = v[0] == '1';
            board_speaker(!qq);
            if (qq) {
                audio_set_volume(0);
            } else {
                settings_apply();
            }
        }
        ctl_levels(q);
        if (httpd_query_key_value(q, "screen", v, sizeof v) == ESP_OK) {
            if (v[0] == '1') {
                app_screen_wake();
            } else {
                app_saver_now(true);
            }
        }
        if (httpd_query_key_value(q, "reboot", v, sizeof v) == ESP_OK && v[0] == '1') {
            // a restart from the manager: the open app closes first (it saves), then the board restarts
            ESP_LOGW("web", "a restart from the manager");
            app_open(&app_menu);
            static esp_timer_handle_t t;
            const esp_timer_create_args_t ta = {.callback = reboot_cb, .name = "reboot"};
            if (t || esp_timer_create(&ta, &t) == ESP_OK) {
                esp_timer_start_once(t, 1500 * 1000);
            }
        }
        if (httpd_query_key_value(q, "poweroff", v, sizeof v) == ESP_OK && v[0] == '1') {
            // a power off from the manager: the open app closes first (it saves), then the main task powers off
            // (power_tick); a key on the board starts it again
            ESP_LOGW("web", "a power off from the manager");
            app_open(&app_menu);
            static esp_timer_handle_t pt;
            const esp_timer_create_args_t pa = {.callback = poweroff_cb, .name = "poweroff"};
            if (pt || esp_timer_create(&pa, &pt) == ESP_OK) {
                esp_timer_start_once(pt, 1500 * 1000);
            }
        }
    }
    enum {
        CB = 16384,
        NM = 256
    };  // (enough for a board with 90+ apps)
    char *b = heap_caps_malloc(CB, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    char app[16];
    const app_t *a = app_current();
    json_str(app, sizeof app, a ? a->name : "");
    // ("bright", "vol", "mute": the manager shows its BRIGHTNESS and VOLUME controls only when they are here)
    int n = snprintf(b, CB,
                     "{\"app\":\"%s\",\"screen\":\"%s\",\"fps\":%.1f,\"bright\":%d,\"vol\":%d,\"mute\":%s,\"apps\":[",
                     app, app_screen_state(), (double)app_fps(), g_settings.brightness, g_settings.volume,
                     g_settings.mute ? "true" : "false");
    const char **names = heap_caps_malloc(NM * sizeof(char *), MALLOC_CAP_SPIRAM);
    uint8_t *cats = heap_caps_malloc(NM, MALLOC_CAP_SPIRAM);
    if (!names || !cats) {
        free(b);
        free(names);
        free(cats);
        return ESP_FAIL;
    }
    int k = app_names(names, NM);
    for (int i = 0; i < k && n < CB - 100; i++) {
        json_str(app, sizeof app, names[i]);
        n += snprintf(b + n, CB - n, "%s\"%s\"", i ? "," : "", app);
    }
    n += snprintf(b + n, CB - n, "],\"menu\":[");
    k = menu_list(names, cats, NM);
    for (int i = 0; i < k && n < CB - 100; i++) {
        json_str(app, sizeof app, names[i]);
        n += snprintf(b + n, CB - n, "%s[\"%s\",%d]", i ? "," : "", app, cats[i]);
    }
    n += snprintf(b + n, CB - n, "]}");
    cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, b, n);
    free(b);
    free(names);
    free(cats);
    return ESP_OK;
}

// the power report (power.c) and its settings: ?off=<the choice 0..5> (POWER OFF), ?ls=0/1 (the light sleep),
// ?deep=<the choice 0..3> (DEEP IDLE)
static esp_err_t api_power(httpd_req_t *req)
{
    char q[48], v[8];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) {
        if (httpd_query_key_value(q, "off", v, sizeof v) == ESP_OK) {
            power_set_off_choice(atoi(v));
        }
        if (httpd_query_key_value(q, "ls", v, sizeof v) == ESP_OK) {
            power_set_light_sleep(v[0] == '1');
        }
        if (httpd_query_key_value(q, "deep", v, sizeof v) == ESP_OK) {
            power_set_deep_choice(atoi(v));
        }
        if (httpd_query_key_value(q, "x", v, sizeof v) == ESP_OK) {
            char m[8] = "";
            httpd_query_key_value(q, "xm", m, sizeof m);
            power_set_x(atoi(v), atoi(m));
        }
    }
    enum {
        PB = 1024
    };
    char *b = heap_caps_malloc(PB, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    int n = power_json(b, PB);
    cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, b, n);
    free(b);
    return ESP_OK;
}

static esp_err_t api_walkie(httpd_req_t *req)
{
    char q[64], set[16] = "", on[4] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) {
        httpd_query_key_value(q, "set", set, sizeof set);
        httpd_query_key_value(q, "on", on, sizeof on);
        if (!strcmp(set, "hear_them")) {
            walkie_web_set(0, on[0] == '1');
        } else if (!strcmp(set, "they_hear_me")) {
            walkie_web_set(1, on[0] == '1');
        }
    }
    char b[256];
    int n = walkie_web_state(b, sizeof b);
    cors(req);
    httpd_resp_send(req, b, n);
    return ESP_OK;
}

// the WALKIE and FRIENDS part of the board page (after wifi_send_page_head)
static const char BODY[] =
        "<style>.w "
        "b{color:#ffd84a}button.on{background:#1e9e5a}button.t{background:#8a2f2f;touch-action:none;user-select:none;"
        "font-size:1.05em;padding:.7em 1.2em}"
        "button.t.on{background:#d04040}button:disabled{opacity:.4}.lv{height:6px;background:#76ffa0;width:0;border-"
        "radius:3px;margin:.4em 1.5em}"
        ".fr{margin:.3em 1.5em;padding:.6em "
        "1em;background:#181c34;border-radius:10px;display:flex;gap:.6em;align-items:center}"
        ".dot{width:10px;height:10px;border-radius:5px;background:#555}.dot.on{background:#3f3}</style>"
        "<h2>Walkie</h2><div class=f><div class=n id=ws>...</div></div>"
        "<style>.emb #aud,.emb #lv{display:none}</style>"  // in the central manager: its board card has LISTEN and TALK
        "<div class=f id=aud><button id=li onclick='listen()'>LISTEN</button>"
        "<button class=t id=tk onpointerdown='talk(1)' onpointerup='talk(0)' onpointerleave='talk(0)'>HOLD TO "
        "TALK</button>"
        "<span class=s id=nt></span></div><div class=lv id=lv></div>"
        "<h2>Friends</h2><div id=fl></div>"
        "<p class=i>All the boards on the network, and who is a friend with who: the central manager on the PC.</p>"
        "<script>"
        "const S=window.isSecureContext&&navigator.mediaDevices;let w=null,ac=null,mic=null,on=false,tk=false,nt=0;"
        "document.getElementById('nt').textContent=S?'':'TALK: the browser allows the microphone only on HTTPS or "
        "localhost '"
        "+'(Chrome: chrome://flags, Insecure origins treated as secure, add http://'+location.host+'). Or use the "
        "central manager.';"
        "if(!S)document.getElementById('tk').disabled=true;"
        "function sock(){if(w&&w.readyState<2)return w;w=new "
        "WebSocket('ws://'+location.host+'/ws/audio');w.binaryType='arraybuffer';"
        "w.onmessage=e=>play(e.data);w.onclose=()=>{on=false;document.getElementById('li').classList.remove('on')};"
        "return w}"
        "function actx(){if(!ac)ac=new AudioContext();if(ac.state!='running')ac.resume();return ac}"
        "function play(d){const a=actx(),x=new "
        "Int16Array(d),f=a.createBuffer(1,x.length,8000),c=f.getChannelData(0);let m=0;"
        "for(let i=0;i<x.length;i++){c[i]=x[i]/32768;m=Math.max(m,Math.abs(c[i]))}const "
        "s=a.createBufferSource();s.buffer=f;s.connect(a.destination);"
        "const "
        "t=Math.max(a.currentTime+0.15,nt);s.start(t);nt=t+f.duration;document.getElementById('lv').style.width=Math."
        "min(100,m*300)+'%'}"
        "function listen(){actx();const s=sock();on=!on;const go=()=>{s.send(on?'listen 1':'listen 0');nt=0;"
        "document.getElementById('li').classList.toggle('on',on)};s.readyState==1?go():s.addEventListener('open',go,{"
        "once:true})}"
        "async function talk(d){const "
        "b=document.getElementById('tk');if(!d){tk=false;b.classList.remove('on');return}if(!S)return;"
        "const a=actx();if(!mic){const st=await "
        "navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true}});"
        "const src=a.createMediaStreamSource(st),p=a.createScriptProcessor(2048,1,1);let acc=[],ph=0;"
        "p.onaudioprocess=e=>{if(!tk)return;const x=e.inputBuffer.getChannelData(0),r=a.sampleRate/8000;"
        "for(let i=0;i<x.length;i++){ph+=1;if(ph>=r){ph-=r;acc.push(Math.max(-1,Math.min(1,x[i]))*32767)}}"
        "while(acc.length>=400){const f=new Int16Array(acc.splice(0,400));if(w&&w.readyState==1)w.send(f.buffer)}};"
        "src.connect(p);p.connect(a.destination);mic=st}sock();tk=true;b.classList.add('on')}"
        "function sw(k,v){fetch('/api/walkie?set='+k+'&on='+(v?1:0)).then(st)}"
        "function st(){fetch('/api/walkie').then(r=>r.json()).then(j=>{const e=document.getElementById('ws');"
        "if(!j.open){e.textContent='WALKIE is not open on this board.';return}if(!j.paired){e.textContent='WALKIE: "
        "looking for a friend.';return}"
        "e.innerHTML='WALKIE with <b>'+j.peer+'</b>'+(j.talk!='none'?' &mdash; talk: '+j.talk:'')+'<br>'"
        "+'<button class=\"'+(j.hear_them?'on':'')+'\" onclick=\"sw(\\'hear_them\\','+!j.hear_them+')\">I HEAR THEM: "
        "'+(j.hear_them?'ON':'OFF')+'</button> '"
        "+'<button class=\"'+(j.they_hear_me?'on':'')+'\" onclick=\"sw(\\'they_hear_me\\','+!j.they_hear_me+')\">THEY "
        "HEAR ME: '+(j.they_hear_me?'ON':'OFF')+'</button>'}).catch(()=>{})}"
        "function rm(m,n){if(confirm('Remove the friend '+n+'?'))fetch('/api/friends?rm='+m).then(fr)}"
        "function fr(){fetch('/api/friends').then(r=>r.json()).then(j=>{document.getElementById('fl').innerHTML="
        "'<div class=fr><div class=n>This board: <b>'+j.name+'</b> <span class=s>'+j.mac+'</span></div></div>'"
        "+(j.friends.length?j.friends.map(f=>'<div class=fr><span class=\"dot'+(f.near?' on':'')+'\"></span><div "
        "class=n>'+f.name"
        "+' <span class=s>'+f.mac+(f.near?' &middot; near':'')+'</span></div><button "
        "onclick=\"rm(\\''+f.mac+'\\',\\''+f.name+'\\')\">REMOVE</button></div>').join('')"
        ":'<p class=i>No friends yet.</p>')}).catch(()=>{})}"
        "st();fr();setInterval(st,1500);setInterval(fr,4000);"
        "</script></body></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    return web_send_page(req, BODY);
}

static void mac_str(char *out, const uint8_t m[6])
{
    sprintf(out, "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}
static bool mac_parse(const char *s, uint8_t m[6])
{
    for (int i = 0; i < 6; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) {
            return false;
        }
        m[i] = (uint8_t)v;
    }
    return true;
}

// a name from the query (%XX and + decoded), in the letters of the board font: A-Z, 0-9, space, - (the
// manager sets the nickname of a board; at most FRIEND_NAME - 1 letters)
static void query_name(const char *raw, char *out, int max)
{
    int k = 0;
    for (int i = 0; raw[i] && k < max - 1; i++) {
        char c = raw[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && raw[i + 1] && raw[i + 2]) {
            char hx[3] = {raw[i + 1], raw[i + 2], 0};
            c = (char)strtol(hx, NULL, 16);
            i += 2;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || (c == ' ' && k > 0)) {
            out[k++] = c;
        }
    }
    while (k > 0 && out[k - 1] == ' ') {
        k--;
    }
    out[k] = 0;
}

// /api/friends: {"name","mac","max","add","friends":[{"name","mac","near"}]}; ?add=MAC&name=NAME ("add": "ok", or
// "full" when the list has FRIENDS_MAX friends), ?rm=MAC, ?myname=NAME (the name of this board: the lobbies, the beacons
// and the manager show it). An older board sends neither "max" nor "add".
static esp_err_t api_friends(httpd_req_t *req)
{
    char q[160], v[20], nm[20], raw[64];
    uint8_t m[6];
    const char *add = "";  // the result of ?add= (the manager checks it)
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) {
        if (httpd_query_key_value(q, "myname", raw, sizeof raw) == ESP_OK) {
            query_name(raw, nm, FRIEND_NAME);
            if (nm[0]) {
                friends_set_my_name(nm);
            }
        }
        if (httpd_query_key_value(q, "add", v, sizeof v) == ESP_OK && strlen(v) == 12 && mac_parse(v, m)) {
            nm[0] = 0;
            if (httpd_query_key_value(q, "name", raw, sizeof raw) == ESP_OK) {
                query_name(raw, nm, FRIEND_NAME);
            }
            if (!nm[0]) {
                strcpy(nm, "FRIEND");
            }
            add = friends_add(m, nm) ? "ok" : "full";
        }
        if (httpd_query_key_value(q, "rm", v, sizeof v) == ESP_OK && strlen(v) == 12 && mac_parse(v, m)) {
            int i = friends_find(m);
            if (i >= 0) {
                friends_remove(i);
            }
        }
    }
    enum {
        FJ = 2400
    };  // (16 friends)
    char *b = heap_caps_malloc(FJ, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    char name[20], ms[13];
    uint8_t me[6];
    arc_mac(me);
    mac_str(ms, me);
    json_str(name, sizeof name, friends_my_name());
    int n = snprintf(b, FJ, "{\"name\":\"%s\",\"mac\":\"%s\",\"max\":%d,", name, ms, FRIENDS_MAX);
    if (add[0]) {
        n += snprintf(b + n, FJ - n, "\"add\":\"%s\",", add);
    }
    n += snprintf(b + n, FJ - n, "\"friends\":[");
    int near[FRIENDS_MAX], nn = friends_near(near, FRIENDS_MAX);
    for (int i = 0; i < friends_count() && n < FJ - 100; i++) {
        const friend_t *f = friends_get(i);
        bool is_near = false;
        for (int k = 0; k < nn; k++) {
            is_near |= near[k] == i;
        }
        json_str(name, sizeof name, f->name);
        mac_str(ms, f->mac);
        n += snprintf(b + n, FJ - n, "%s{\"name\":\"%s\",\"mac\":\"%s\",\"near\":%s}", i ? "," : "", name, ms,
                      is_near ? "true" : "false");
    }
    n += snprintf(b + n, FJ - n, "]}");
    cors(req);
    httpd_resp_send(req, b, n);
    free(b);
    return ESP_OK;
}

// ---------------------------------------------------------------- the VPN part of the board page

#if CONFIG_TFN_EXTRA_VPN
static const char VPN_BODY[] =
        "<h2>VPN (WireGuard to the hub)</h2><div class=f><div class=n id=st>...</div></div>"
        "<style>.vf label{display:block;margin:.6em 0 .2em;color:#8a93c0;font-size:.85em}.vf "
        "input[type=text]{width:100%;box-sizing:border-box;"
        "background:#0d0f1e;color:#e8ecff;border:1px solid "
        "#2a3058;border-radius:6px;padding:.5em;font:inherit}.vf{display:block}"
        ".k{font-family:monospace;word-break:break-all;color:#ffd84a}</style>"
        "<div class='f vf'><label><input type=checkbox id=on> Use the VPN</label>"
        "<label>The hub: a name or an address</label><input type=text id=host placeholder='vpn.example.com'>"
        "<label>The hub port</label><input type=text id=port placeholder='51820'>"
        "<label>The public key of the hub</label><input type=text id=hub_key>"
        "<label>The tunnel address of this board</label><input type=text id=ip placeholder='" TFN_VPN_BOARD_IP_EXAMPLE
        "'>"
        "<label>The tunnel address of the hub</label><input type=text id=hub_ip placeholder='" TFN_VPN_HUB_IP "'>"
        "<p><button onclick='save()'>SAVE</button> <span class=s id=msg></span></p></div>"
        "<div class=f><div class=n>The public key of this board (put it on the hub; the central manager does it with "
        "one click):"
        "<div class=k id=pub></div></div></div>"
        "<p class=i>Without the VPN, the board finds the other boards on the WiFi network only (and by ESP-NOW).</p>"
        "<script>const F=['host','port','hub_key','ip','hub_ip'];"
        "function "
        "load(){fetch('/api/vpn').then(r=>r.json()).then(j=>{document.getElementById('st').textContent='State: "
        "'+j.state+"
        "(j.state=='connected'?' as '+j.ip:'');document.getElementById('pub').textContent=j.pub;"
        "if(!window.done){window.done=1;document.getElementById('on').checked=j.on;F.forEach(k=>document."
        "getElementById(k).value=j[k]||'')}})}"
        "function save(){const q=new URLSearchParams({set:1,on:document.getElementById('on').checked?1:0});"
        "F.forEach(k=>q.append(k,document.getElementById(k).value.trim()));"
        "fetch('/api/vpn?'+q).then(r=>r.json()).then(j=>{document.getElementById('msg').textContent=j.ok?'Saved.':'Not "
        "saved: '+j.error;load()})}"
        "load();setInterval(load,2000);</script></body></html>";

static esp_err_t vpn_page_get(httpd_req_t *req)
{
    return web_send_page(req, VPN_BODY);
}
#endif

// %xx and '+' of a query value
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        unsigned v;
        if (*s == '%' && s[1] && s[2] && sscanf(s + 1, "%2x", &v) == 1) {
            *o++ = (char)v;
            s += 2;
        } else {
            *o++ = *s == '+' ? ' ' : *s;
        }
    }
    *o = 0;
}

#if CONFIG_TFN_EXTRA_VPN
static bool qval(const char *q, const char *key, char *out, int max)
{
    if (httpd_query_key_value(q, key, out, (size_t)max) != ESP_OK) {
        return false;
    }
    url_decode(out);
    return true;
}

// /api/vpn: the settings, the state and the public key; ?set=1&on=&host=&port=&hub_key=&ip=&hub_ip= saves
static esp_err_t api_vpn(httpd_req_t *req)
{
    char q[400], v[80];
    bool ok = true;
    const char *err = "";
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK && qval(q, "set", v, sizeof v)) {
        vpn_cfg_t c;
        vpn_get(&c);
        if (qval(q, "on", v, sizeof v)) {
            c.on = v[0] == '1';
        }
        if (qval(q, "host", v, sizeof v)) {
            snprintf(c.host, sizeof c.host, "%.63s", v);
        }
        if (qval(q, "port", v, sizeof v)) {
            c.port = (uint16_t)atoi(v);
        }
        if (qval(q, "hub_key", v, sizeof v)) {
            snprintf(c.hub_key, sizeof c.hub_key, "%.47s", v);
        }
        if (qval(q, "ip", v, sizeof v)) {
            snprintf(c.ip, sizeof c.ip, "%.15s", v);
        }
        if (qval(q, "hub_ip", v, sizeof v)) {
            snprintf(c.hub_ip, sizeof c.hub_ip, "%.15s", v);
        }
        ok = vpn_set(&c);
        if (!ok) {
            err = "a value is not right (the hub, the key: 44 characters, the addresses)";
        }
    }
    vpn_cfg_t c;
    vpn_get(&c);
    char host[64], b[600];
    json_str(host, sizeof host, c.host);
    int n = snprintf(b, sizeof b,
                     "{\"ok\":%s,\"error\":\"%s\",\"on\":%s,\"host\":\"%s\",\"port\":%u,\"hub_key\":\"%s\",\"ip\":\"%"
                     "s\",\"hub_ip\":\"%s\","
                     "\"pub\":\"%s\",\"state\":\"%s\",\"time\":%lld}",
                     ok ? "true" : "false", err, c.on ? "true" : "false", host, c.port, key_or_empty(c.hub_key), c.ip,
                     c.hub_ip, vpn_public_key(), vpn_state_name(vpn_state()), (long long)time(NULL));
    cors(req);
    httpd_resp_send(req, b, n);
    return ESP_OK;
}
#endif

void web_walkie_register(void *h)
{
    s_hd = h;
    if (!s_ring) {
        s_ring = heap_caps_malloc(RING_N * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_tmr) {
        const esp_timer_create_args_t a = {.callback = tmr_cb, .name = "webaudio"};
        esp_timer_create(&a, &s_tmr);
    }
    httpd_uri_t pg = {.uri = "/walkie", .method = HTTP_GET, .handler = page_get};
    httpd_uri_t bd = {.uri = "/api/boards", .method = HTTP_GET, .handler = api_boards};
    httpd_uri_t wk = {.uri = "/api/walkie", .method = HTTP_GET, .handler = api_walkie};
    httpd_uri_t ct = {.uri = "/api/ctl", .method = HTTP_GET, .handler = api_ctl};
    httpd_register_uri_handler(h, &ct);
    httpd_uri_t pw = {.uri = "/api/power", .method = HTTP_GET, .handler = api_power};
    httpd_register_uri_handler(h, &pw);
    httpd_uri_t mg = {.uri = "/api/menu", .method = HTTP_GET, .handler = api_menu_get};
    httpd_uri_t mp = {.uri = "/api/menu", .method = HTTP_POST, .handler = api_menu_post};
    httpd_register_uri_handler(h, &mg);
    httpd_register_uri_handler(h, &mp);
    httpd_uri_t sc = {.uri = "/api/screen", .method = HTTP_GET, .handler = api_screen};
    httpd_register_uri_handler(h, &sc);
    httpd_uri_t fr = {.uri = "/api/friends", .method = HTTP_GET, .handler = api_friends};
#if CONFIG_TFN_EXTRA_VPN
    httpd_uri_t vp = {.uri = "/vpn", .method = HTTP_GET, .handler = vpn_page_get};
    httpd_uri_t va = {.uri = "/api/vpn", .method = HTTP_GET, .handler = api_vpn};
    httpd_register_uri_handler(h, &vp);
    httpd_register_uri_handler(h, &va);
#endif
    httpd_register_uri_handler(h, &fr);
    httpd_uri_t au = {.uri = "/ws/audio", .method = HTTP_GET, .handler = ws_audio, .is_websocket = true};
    httpd_register_uri_handler(h, &pg);
    httpd_register_uri_handler(h, &bd);
    httpd_register_uri_handler(h, &wk);
    httpd_register_uri_handler(h, &au);
}

void web_walkie_overlay(void)
{
    bool talks = s_talk_at && tfn_now_ms() - s_talk_at < 500;
    if (s_listen_fd < 0 && !talks) {
        return;
    }
    int u = gfx_native() ? 2 : 1;
    const char *t = talks ? (s_listen_fd >= 0 ? "WEB LISTENS + TALKS" : "WEB TALKS") : "WEB LISTENS";
    int w = gfx_text_width(t, u) + 12 * u, h = 12 * u;
    gfx_fill_rect(gfx_w - w - 4, 2, w, h, rgb(170, 30, 30));
    gfx_fill_circle(gfx_w - w + 2 * u, 2 + h / 2, 2 * u, rgb(255, 255, 255));
    gfx_text(gfx_w - w + 6 * u, 2 + (h - 7 * u) / 2, t, u, rgb(255, 255, 255));
}
