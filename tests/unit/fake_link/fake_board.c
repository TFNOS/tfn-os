// The fake board of the link test (fake_board.h): the bodies of the ESP-IDF, FreeRTOS, lwIP and OS functions that
// link.c, lan.c, friends.c and walkie_call.c call. One task at a time; the lan task runs in fk_lan_run().
#include "fake_board.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "audio.h"
#include "esp_heap_caps.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "notify.h"
#include "settings.h"
#include "sysui.h"
#include "vpn.h"
#include "wifi.h"

int64_t fk_now_us = 1000000;
uint8_t fk_mac[6];
int fk_wifi;
int fk_channel;
int fk_get_ch_n;
uint32_t fk_hub;
const char *fk_app_name;
fk_out_fn fk_out;
fk_log_fn fk_log;

static void logf_(const char *fmt, ...)
{
    if (!fk_log) {
        return;
    }
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    fk_log(b);
}

// ---------------------------------------------------------------- ESP-IDF
int64_t esp_timer_get_time(void)
{
    return fk_now_us;
}

void fk_esp_log(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}

size_t heap_caps_get_free_size(uint32_t caps)
{
    (void)caps;
    return 100000;
}

size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
    (void)caps;
    return 90000;
}

size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return 50000;
}

const char *esp_err_to_name(esp_err_t e)
{
    return e == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

static esp_now_recv_cb_t s_now_cb;
static esp_now_send_cb_t s_sent_cb;
static bool s_peer;
int fk_now_auto = 1, fk_now_lim = 8, fk_now_stuck, fk_now_inflight, fk_restarts, fk_full_restarts;

void fk_now_done(int n)
{
    for (int i = 0; i < n && fk_now_inflight > 0; i++) {
        fk_now_inflight--;
        if (s_sent_cb) {
            esp_now_send_info_t info = {0};
            s_sent_cb(&info, ESP_NOW_SEND_SUCCESS);
        }
    }
}

esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb)
{
    s_sent_cb = cb;
    return ESP_OK;
}

esp_err_t esp_now_init(void)
{
    return ESP_OK;
}

esp_err_t esp_now_deinit(void)
{
    s_now_cb = NULL;
    s_sent_cb = NULL;
    s_peer = false;
    fk_now_inflight = 0;  // (the frames of the old ESP-NOW get no callback)
    return ESP_OK;
}

esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb)
{
    s_now_cb = cb;
    return ESP_OK;
}

esp_err_t esp_now_unregister_recv_cb(void)
{
    s_now_cb = NULL;
    return ESP_OK;
}

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    (void)peer;
    s_peer = true;
    return ESP_OK;
}

bool esp_now_is_peer_exist(const uint8_t *mac)
{
    (void)mac;
    return s_peer;
}

esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len)
{
    (void)mac;
    if (fk_now_auto) {
        fk_now_done(fk_now_inflight);
    }
    if (fk_now_stuck || fk_now_inflight >= fk_now_lim) {
        return ESP_ERR_ESPNOW_NO_MEM;
    }
    fk_now_inflight++;
    if (fk_out) {
        fk_out('E', 0, data, (int)len);
    }
    return ESP_OK;
}

void fk_espnow_in(const uint8_t mac[6], const uint8_t *p, int n, int rssi)
{
    uint8_t src[6];
    memcpy(src, mac, 6);
    wifi_pkt_rx_ctrl_t rx = {.rssi = rssi};
    esp_now_recv_info_t info = {.src_addr = src, .des_addr = NULL, .rx_ctrl = &rx};
    if (s_now_cb) {
        s_now_cb(&info, p, n);
    }
}

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode)
{
    *mode = WIFI_MODE_STA;
    return ESP_OK;
}

esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps)
{
    *ps = WIFI_PS_MIN_MODEM;
    return ESP_OK;
}

esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps)
{
    (void)ps;
    return ESP_OK;
}

esp_err_t esp_wifi_set_channel(uint8_t ch, wifi_second_chan_t second)
{
    (void)second;
    fk_channel = ch;
    return ESP_OK;
}

esp_err_t esp_wifi_get_channel(uint8_t *ch, wifi_second_chan_t *second)
{
    fk_get_ch_n++;
    *ch = (uint8_t)fk_channel;
    *second = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{
    ap->rssi = -50;
    return ESP_OK;
}

// ---------------------------------------------------------------- FreeRTOS
struct fk_queue {
    int len, head, n;
    size_t item;
    uint8_t *buf;
};

QueueHandle_t xQueueCreateWithCaps(int len, size_t item, uint32_t caps)
{
    (void)caps;
    struct fk_queue *q = calloc(1, sizeof *q);
    q->len = len;
    q->item = item;
    q->buf = calloc((size_t)len, item);
    return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    (void)wait;
    if (q->n == q->len) {
        return pdFALSE;
    }
    memcpy(q->buf + (size_t)((q->head + q->n) % q->len) * q->item, item, q->item);
    q->n++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    (void)wait;
    if (!q->n) {
        return pdFALSE;
    }
    memcpy(item, q->buf + (size_t)q->head * q->item, q->item);
    q->head = (q->head + 1) % q->len;
    q->n--;
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    q->head = q->n = 0;
    return pdTRUE;
}

static void (*s_task)(void *);

BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                                   TaskHandle_t *out, int core)
{
    (void)name;
    (void)stack;
    (void)arg;
    (void)prio;
    (void)out;
    (void)core;
    s_task = fn;
    return pdTRUE;
}

// ---------------------------------------------------------------- lwIP: the lan task reads the pushed datagrams
#define FK_RX 64
static struct {
    uint32_t ip;
    int n;
    uint8_t p[600];
} s_rx[FK_RX];
static int s_rx_head, s_rx_n;
static jmp_buf s_jmp;

void fk_lan_push(uint32_t ip, const uint8_t *p, int n)
{
    if (s_rx_n == FK_RX || n > 600) {
        return;
    }
    int k = (s_rx_head + s_rx_n++) % FK_RX;
    s_rx[k].ip = ip;
    s_rx[k].n = n;
    memcpy(s_rx[k].p, p, (size_t)n);
}

void fk_lan_run(void)
{
    if (s_task && !setjmp(s_jmp)) {
        s_task(NULL);
    }
}

int fk_socket(int domain, int type, int proto)
{
    (void)domain;
    (void)type;
    (void)proto;
    return 3;
}

int fk_setsockopt(int s, int level, int name, const void *v, socklen_t n)
{
    (void)s;
    (void)level;
    (void)name;
    (void)v;
    (void)n;
    return 0;
}

int fk_bind(int s, const struct sockaddr *a, socklen_t n)
{
    (void)s;
    (void)a;
    (void)n;
    return 0;
}

int fk_sendto(int s, const void *p, size_t n, int flags, const struct sockaddr *to, socklen_t tn)
{
    (void)s;
    (void)flags;
    (void)tn;
    const struct sockaddr_in *a = (const struct sockaddr_in *)(const void *)to;
    if (fk_out) {
        fk_out('U', a->sin_addr.s_addr, p, (int)n);
    }
    return (int)n;
}

int fk_recvfrom(int s, void *p, size_t n, int flags, struct sockaddr *from, socklen_t *fn)
{
    (void)s;
    (void)flags;
    (void)fn;
    if (!s_rx_n) {
        longjmp(s_jmp, 1);  // (the end of the list: back to fk_lan_run)
    }
    int k = s_rx_head;
    s_rx_head = (s_rx_head + 1) % FK_RX;
    s_rx_n--;
    struct sockaddr_in *a = (struct sockaddr_in *)(void *)from;
    a->sin_addr.s_addr = s_rx[k].ip;
    int m = s_rx[k].n < (int)n ? s_rx[k].n : (int)n;
    memcpy(p, s_rx[k].p, (size_t)m);
    return m;
}

int fk_close(int s)
{
    (void)s;
    return 0;
}

// ---------------------------------------------------------------- the OS
bool power_deep_now(void)
{
    return false;  // (no DEEP IDLE in the link tests)
}

wifi_state_t wifi_state(void)
{
    return (wifi_state_t)fk_wifi;
}

bool wifi_start(void)
{
    return true;
}

void wifi_radio_restart(int ch, bool full)
{
    logf_("radio restart %d%s", ch, full ? " full" : "");
    fk_restarts++;
    fk_full_restarts += full ? 1 : 0;
    if (fk_now_stuck == 1 || (fk_now_stuck == 2 && full)) {
        fk_now_stuck = 0;
    }
    fk_now_inflight = 0;
}

bool wifi_hotspot_on(void)
{
    return false;
}

bool wifi_scanning(void)
{
    return false;
}

int wifi_hotspot_clients(void)
{
    return 0;
}

void wifi_ps_apply(void)
{
}

uint32_t vpn_hub_ip(void)
{
    return fk_hub;
}

bool vpn_is_tunnel_ip(uint32_t ip)
{
    return (ip & 0xffff) == 10;  // 10.0.x.x (network byte order)
}

static const app_t s_apps[] = {{"PONG"}, {"CHESS"}, {"WALKIE"}, {"TALKIE"}};
const app_t app_walkie = {"WALKIE"}, app_talkie = {"TALKIE"};

const app_t *app_find(const char *name)
{
    for (size_t i = 0; name && i < sizeof s_apps / sizeof s_apps[0]; i++) {
        if (!strcmp(s_apps[i].name, name)) {
            return &s_apps[i];
        }
    }
    return NULL;
}

const app_t *app_current(void)
{
    if (fk_app_name && !strcmp(fk_app_name, "WALKIE")) {
        return &app_walkie;
    }
    if (fk_app_name && !strcmp(fk_app_name, "TALKIE")) {
        return &app_talkie;
    }
    return app_find(fk_app_name);
}

void app_open(const app_t *a)
{
    logf_("open %s", a->name);
}

bool app_in_match(void)
{
    return false;
}

void app_screen_wake(void)
{
}

bool walkie_paired(void)
{
    return false;
}

static uint32_t s_note;

uint32_t notify_post(const char *app, const char *title, const char *text, const char *link)
{
    logf_("notice %s|%s|%s|%s", app, title, text, link);
    return ++s_note;
}

void notify_remove(uint32_t id)
{
    logf_("notice remove %u", (unsigned)id);
}

int settings_lang_all(void)
{
    return 0;
}

uint32_t store_get(const char *key, uint32_t def)
{
    (void)key;
    return def;
}

void store_set(const char *key, uint32_t val)
{
    (void)key;
    (void)val;
}

void sfx_tone(uint16_t f0, uint16_t f1, uint16_t dur_ms, wave_t wave, uint8_t vol)
{
    (void)wave;
    (void)vol;
    logf_("tone %u %u %u", f0, f1, dur_ms);
}

void sysui_message(const char *utf8, int secs)
{
    logf_("message %s %d", utf8, secs);
}

void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int))
{
    (void)done;
    logf_("ask %s|%s|%s|%s|%d", title, line, yes, no, secs);
}

void sysui_ask_close(void)
{
    logf_("ask close");
}

bool sysui_active(void)
{
    return false;
}
