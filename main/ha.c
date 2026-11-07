// Home Assistant client (see ha.h).

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "nvs.h"
#include "ha.h"

static const char *TAG = "ha";

static char s_url[96];
static char s_token[300];
static ha_item_t s_items[HA_MAX];
static int s_n;
static SemaphoreHandle_t s_mx;

static void lock(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
}
static void unlock(void)
{
    xSemaphoreGive(s_mx);
}

void ha_init(void)
{
    s_mx = xSemaphoreCreateMutex();
    nvs_handle_t h;
    if (nvs_open("ha", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = sizeof s_url;
    if (nvs_get_str(h, "url", s_url, &n) != ESP_OK) {
        s_url[0] = 0;
    }
    n = sizeof s_token;
    if (nvs_get_str(h, "token", s_token, &n) != ESP_OK) {
        s_token[0] = 0;
    }
    n = sizeof s_items;
    if (nvs_get_blob(h, "items", s_items, &n) == ESP_OK && n % sizeof(ha_item_t) == 0) {
        s_n = (int)(n / sizeof(ha_item_t));
    }
    nvs_close(h);
    for (int i = 0; i < s_n; i++) {
        s_items[i].id[HA_ID_LEN - 1] = 0;
        s_items[i].name[HA_NAME_LEN - 1] = 0;
    }
}

const char *ha_url(void)
{
    return s_url;
}
bool ha_token_set(void)
{
    return s_token[0] != 0;
}
bool ha_ready(void)
{
    return s_url[0] && s_token[0];
}

bool ha_set_config(const char *url, const char *token)
{
    // an address with no scheme gets http://, and one with no port gets the port of Home Assistant (8123): an address
    // with no port would use port 80, and HOME would not connect (port 80 does not answer)
    char full[sizeof s_url];
    if (url && url[0]) {
        bool sch = !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
        const char *host = sch ? strstr(url, "//") + 2 : url;
        const char *end = host + strcspn(host, "/");
        bool port = memchr(host, ':', (size_t)(end - host)) != NULL;
        int n = snprintf(full, sizeof full, "%s%.*s%s%s", sch ? "" : "http://", (int)(sch ? end - url : end - host),
                         sch ? url : host, port || !strncmp(url, "https://", 8) ? "" : ":8123", end);
        if (n <= 0 || n >= (int)sizeof full || end == host) {
            return false;
        }
        url = full;
    }
    if (token && strlen(token) >= sizeof s_token) {
        return false;
    }
    lock();
    if (url && url[0]) {
        snprintf(s_url, sizeof s_url, "%s", url);
        size_t l = strlen(s_url);
        while (l && s_url[l - 1] == '/') {
            s_url[--l] = 0;  // no slash at the end
        }
    }
    if (token && token[0]) {
        snprintf(s_token, sizeof s_token, "%s", token);
    }
    nvs_handle_t h;
    if (nvs_open("ha", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "url", s_url);
        nvs_set_str(h, "token", s_token);
        nvs_commit(h);
        nvs_close(h);
    }
    unlock();
    ESP_LOGI(TAG, "settings: %s, token %s", s_url, s_token[0] ? "set" : "not set");
    return true;
}

int ha_items(ha_item_t *out, int max)
{
    lock();
    int n = s_n < max ? s_n : max;
    memcpy(out, s_items, sizeof(ha_item_t) * (size_t)n);
    unlock();
    return n;
}

void ha_set_items(const ha_item_t *items, int n)
{
    if (n > HA_MAX) {
        n = HA_MAX;
    }
    lock();
    memcpy(s_items, items, sizeof(ha_item_t) * (size_t)n);
    s_n = n;
    nvs_handle_t h;
    if (nvs_open("ha", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "items", s_items, sizeof(ha_item_t) * (size_t)s_n);
        nvs_commit(h);
        nvs_close(h);
    }
    unlock();
}

void ha_clean(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (isalnum((unsigned char)*s) || *s == ' ' || *s == '.' || *s == '_' || *s == '-') {
            *o++ = *s;
        }
    }
    *o = 0;
}

// ---------------------------------------------------------------- HTTP

typedef struct {
    char *out;
    int max, n;
} body_t;

static esp_err_t on_event(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b && b->out) {
        int room = b->max - 1 - b->n;
        int k = e->data_len < room ? e->data_len : room;
        if (k > 0) {
            memcpy(b->out + b->n, e->data, (size_t)k);
            b->n += k;
            b->out[b->n] = 0;
        }
    }
    return ESP_OK;
}

static int request(const char *method_path, bool post, const char *body, char *out, int max)
{
    if (!ha_ready()) {
        return -1;
    }
    char url[200], auth[320];
    lock();
    snprintf(url, sizeof url, "%s%s", s_url, method_path);
    snprintf(auth, sizeof auth, "Bearer %s", s_token);
    unlock();
    body_t b = {out, max, 0};
    if (out && max) {
        out[0] = 0;
    }
    esp_http_client_config_t c = {
            .url = url,
            .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
            .timeout_ms = 6000,
            .event_handler = on_event,
            .user_data = &b,
            .crt_bundle_attach = esp_crt_bundle_attach,  // (an https:// address)
            .buffer_size = 1024,
            .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) {
        return -1;
    }
    esp_http_client_set_header(h, "Authorization", auth);
    memset(auth, 0, sizeof auth);
    if (post) {
        esp_http_client_set_header(h, "Content-Type", "application/json");
        esp_http_client_set_post_field(h, body, (int)strlen(body));
    }
    esp_err_t err = esp_http_client_perform(h);
    int status = err == ESP_OK ? esp_http_client_get_status_code(h) : -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: %s", method_path, esp_err_to_name(err));
    }
    esp_http_client_cleanup(h);
    return status;
}

int ha_get_api(char *out, int max)
{
    return request("/api/", false, NULL, out, max);
}

// a JSON string (the quotes, \ and the control characters escaped)
static int json_escape(const char *in, char *out, int max)
{
    int k = 0;
    for (; *in && k < max - 7; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            out[k++] = '\\';
            out[k++] = (char)c;
        } else if (c == '\n') {
            out[k++] = '\\';
            out[k++] = 'n';
        } else if (c < 32) {
            k += snprintf(out + k, (size_t)(max - k), "\\u%04x", c);
        } else {
            out[k++] = (char)c;
        }
    }
    out[k] = 0;
    return k;
}

int ha_template(const char *tpl, char *out, int max)
{
    int n = (int)strlen(tpl) * 2 + 32;
    char *body = heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM);
    if (!body) {
        return -1;
    }
    int k = snprintf(body, (size_t)n, "{\"template\":\"");
    k += json_escape(tpl, body + k, n - k - 3);
    snprintf(body + k, (size_t)(n - k), "\"}");
    int st = request("/api/template", true, body, out, max);
    free(body);
    return st;
}

int ha_call(const char *domain, const char *service, const char *entity, const char *extra_json)
{
    char path[96], body[256];
    snprintf(path, sizeof path, "/api/services/%s/%s", domain, service);
    snprintf(body, sizeof body, "{\"entity_id\":\"%s\"%s%s}", entity, extra_json && extra_json[0] ? "," : "",
             extra_json ? extra_json : "");
    char out[64];
    int st = request(path, true, body, out, sizeof out);
    ESP_LOGI(TAG, "%s.%s %s: %d", domain, service, entity, st);
    return st;
}
