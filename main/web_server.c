// The web server of the board: the settings and the list of the modules that add their routes (web_server.h).
#include "web_server.h"
#include "esp_timer.h"
#include <stddef.h>
#include "tfn_extras.h"
#include "app.h"
#if ARCADE_HAS_BOOKS
#include "books_web.h"
#endif
#include "evlog.h"
#include "ota.h"
#include "tfn_id.h"
#include "tfn_pool.h"
#include "tfn_web.h"
#include "web_auth.h"
#include "web_chat.h"
#include "web_files.h"
#if CONFIG_TFN_EXTRA_HA
#include "web_ha.h"
#endif
#if CONFIG_TFN_EXTRA_STOCKS
#include "web_stocks.h"
#endif
#include "web_comp.h"
#include "web_sys.h"
#include "web_walkie.h"
#include "webcam.h"
#include "wifi.h"

static httpd_handle_t s_http;

// Each module adds its routes, in this order. The order counts: for two routes that match the same URI, the first
// one wins (the wildcard routes such as "/f/*" and "/app/*").
// clang-format off
static void (*const WEB_MODULES[])(httpd_handle_t) = {
        wifi_web_register,    // the main page, the files of /data (/f/*, /del/*), the MIDI upload, the gamepad page
#if ARCADE_HAS_BOOKS
        books_web_register,
#endif
        web_walkie_register,
#if CONFIG_TFN_EXTRA_HA
        web_ha_register,
#endif
#if CONFIG_TFN_EXTRA_STOCKS
        web_stocks_register,  // STOCKS: the API key, the stocks
#endif
#if CONFIG_TFN_EXTRA_CHAT
        web_chat_register,    // CHAT: the gateway addresses
#endif
        web_sys_register,     // the message, the locate sound, the sweep, the start picture
        web_comp_register,    // the companions: the list, the backup and the restore (/companions)
        evlog_register,       // the Tab5: GET /api/evlog (the event log); nothing on the other boards
#if CONFIG_TFN_EXTRA_WEBCAM
        webcam_register,
#endif
        ota_register,
        tfn_web_register,     // TFN OS: the installed apps (/api/apps)
        tfn_id_register,      // the device identity: /api/id
        tfn_pool_register,    // the pool: /api/pool
        web_files_register,   // FILES: all the files of the board and of the SD card
        web_auth_register,    // the web password: /login and /logout (each handler above is behind it)
};
// clang-format on

static volatile int64_t s_web_at;  // ms: the last new connection (power.c DEEP IDLE keeps the radio on after it)

static esp_err_t web_open(httpd_handle_t hd, int fd)
{
    (void)hd;
    (void)fd;
    s_web_at = esp_timer_get_time() / 1000;
    return ESP_OK;
}

int64_t web_server_last_ms(void)
{
    return s_web_at;
}

esp_err_t web_server_start(void)
{
    if (s_http) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 8192;      // BOOKS opens EPUB files in the web task
    cfg.max_uri_handlers = 96;  // 64 handlers are not enough (the list is full)
    // When all 7 connections are in use, the oldest one closes and the new one comes in. A page that
    // went away keeps its connections, and the manager then gets no answer.
    cfg.lru_purge_enable = true;
    cfg.open_fn = web_open;
    // Core 0 runs the app. A web task on core 0 (the manager asks each 2 s) gives the games a stop of 50-90 ms
    // each 2 s. On core 1 at priority 2, the audio, LCD, touch and IMU tasks and the render helpers of
    // the apps (priority 3) come first. At priority 3 the stops come back on the
    // CoreS3. While a helper is busy, an answer can come late.
    cfg.core_id = 1;
    cfg.task_priority = 2;
    esp_err_t err = httpd_start(&s_http, &cfg);
    if (err != ESP_OK) {
        s_http = NULL;
        return err;
    }
    for (size_t i = 0; i < sizeof(WEB_MODULES) / sizeof(WEB_MODULES[0]); i++) {
        WEB_MODULES[i](s_http);
    }
    return ESP_OK;
}

httpd_handle_t web_server_handle(void)
{
    return s_http;
}
