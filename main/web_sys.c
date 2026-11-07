// The board API of the system layer (sysui.h), for the central manager:
//   /api/msg?secs=N&text=...      (or POST /api/msg?secs=N with the text as the body, UTF-8): a message on the screen
//   /api/locate?secs=N[&hz=F]     the locate sound (secs=0: stop); ?save=F keeps F as the locate frequency
//   /api/sweep?f0=&f1=&df=&ms=    the speaker sweep: the microphone level at each frequency, and the best one
//   /api/splash                   GET: the state; ?on=0|1, ?secs=N, ?del=1; POST: the JPEG of the start picture
//   /api/notify?tok=<the manager token> (notify.h): POST a notice, the body (url-encoded UTF-8)
//                                 app=&title=&text=&link=&a1=&a2=&a3=&cb= -> {"ok":true,"id":N} (id 0: that app is off);
//                                 GET [&clear=1]: the notices and the answers of the buttons (clear: the answers go)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "web_util.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "settings.h"
#include "notify.h"
#include "sysui.h"
#include "tfn_store.h"
#include "usb_phones.h"
#include "web_sys.h"

static const char *TAG = "web_sys";

static void reply(httpd_req_t *req, const char *json)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
}
static int qint(const char *q, const char *k, int dflt)
{
    char v[16];
    return httpd_query_key_value(q, k, v, sizeof v) == ESP_OK ? atoi(v) : dflt;
}

static esp_err_t msg_api(httpd_req_t *req)
{
    char q[160] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    int secs = qint(q, "secs", 10);
    char *text = heap_caps_calloc(1, 640, MALLOC_CAP_SPIRAM);
    if (!text) {
        return ESP_FAIL;
    }
    if (req->method == HTTP_POST) {
        int n = req->content_len < 600 ? (int)req->content_len : 600, got = 0;
        while (got < n) {
            int r = httpd_req_recv(req, text + got, n - got);
            if (r <= 0) {
                break;
            }
            got += r;
        }
        text[got] = 0;
    } else {
        char raw[600];
        if (httpd_query_key_value(q, "text", raw, sizeof raw) == ESP_OK) {
            web_url_decode(raw);
            snprintf(text, 600, "%s", raw);
        }
    }
    sysui_message(text, secs);
    heap_caps_free(text);
    reply(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t locate_api(httpd_req_t *req)
{
    char q[96] = "", b[96];
    httpd_req_get_url_query_str(req, q, sizeof q);
    int save = qint(q, "save", 0);
    if (save >= 200 && save <= 8000) {
        store_set("loc_hz", save);
    }
    if (httpd_query_key_value(q, "secs", b, sizeof b) == ESP_OK) {
        sysui_locate(atoi(b), qint(q, "hz", 0));
    }
    snprintf(b, sizeof b, "{\"ok\":true,\"hz\":%d}", sysui_locate_hz());
    reply(req, b);
    return ESP_OK;
}

static esp_err_t sweep_api(httpd_req_t *req)
{
    char q[128] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    int f0 = qint(q, "f0", 300), f1 = qint(q, "f1", 6000), df = qint(q, "df", 100), ms = qint(q, "ms", 120);
    enum {
        NMAX = 200
    };
    float *lv = heap_caps_malloc(NMAX * sizeof(float), MALLOC_CAP_SPIRAM);
    char *b = heap_caps_malloc(NMAX * 10 + 120, MALLOC_CAP_SPIRAM);
    int n = 0;
    if (!lv || !b) {
        heap_caps_free(lv);
        heap_caps_free(b);
        return ESP_FAIL;
    }
    bool ok = sysui_sweep(f0, f1, df, ms, lv, NMAX, &n);
    int best = 0;
    for (int i = 1; i < n; i++) {
        if (lv[i] > lv[best]) {
            best = i;
        }
    }
    int o = snprintf(b, 120, "{\"ok\":%s,\"f0\":%d,\"df\":%d,\"best\":%d,\"lv\":[", ok ? "true" : "false", f0, df,
                     n ? f0 + best * df : 0);
    for (int i = 0; i < n; i++) {
        o += snprintf(b + o, 12, "%s%.0f", i ? "," : "", lv[i]);
    }
    snprintf(b + o, 4, "]}");
    ESP_LOGI(TAG, "sweep %d..%d: best %d Hz", f0, f1, n ? f0 + best * df : 0);
    reply(req, b);
    heap_caps_free(lv);
    heap_caps_free(b);
    return ESP_OK;
}

static esp_err_t splash_api(httpd_req_t *req)
{
    char q[96] = "", b[160];
    httpd_req_get_url_query_str(req, q, sizeof q);
    if (req->method == HTTP_POST) {
        int n = (int)req->content_len;
        if (n <= 0 || n > 400000) {
            reply(req, "{\"ok\":false,\"msg\":\"a JPEG of 400 KB or less\"}");
            return ESP_OK;
        }
        FILE *fp = fopen("/data/splash.tmp", "wb");
        char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        int got = 0;
        if (fp && buf) {
            while (got < n) {
                int r = httpd_req_recv(req, buf, n - got < 4096 ? n - got : 4096);
                if (r <= 0) {
                    break;
                }
                fwrite(buf, 1, (size_t)r, fp);
                got += r;
            }
        }
        if (fp) {
            fclose(fp);
        }
        heap_caps_free(buf);
        if (got != n) {
            remove("/data/splash.tmp");
            reply(req, "{\"ok\":false,\"msg\":\"the upload stopped\"}");
            return ESP_OK;
        }
        remove("/data/splash.jpg");
        rename("/data/splash.tmp", "/data/splash.jpg");
        store_set("splash_on", 1);
        ESP_LOGI(TAG, "splash: %d bytes", n);
    } else {
        int on = qint(q, "on", -1), secs = qint(q, "secs", -1);
        if (on == 0 || on == 1) {
            store_set("splash_on", on);
        }
        if (secs >= 1 && secs <= 15) {
            store_set("splash_s", secs);
        }
        if (qint(q, "del", 0) == 1) {
            remove("/data/splash.jpg");
            store_set("splash_on", 0);
        }
    }
    struct stat st;
    long size = stat("/data/splash.jpg", &st) == 0 ? (long)st.st_size : 0;
    snprintf(b, sizeof b, "{\"ok\":true,\"has\":%s,\"size\":%ld,\"on\":%d,\"secs\":%d}", size ? "true" : "false", size,
             (int)store_get("splash_on", 0), (int)store_get("splash_s", 3));
    reply(req, b);
    return ESP_OK;
}

// one field of the url-encoded body
static void field(const char *body, const char *key, char *out, int n)
{
    out[0] = 0;
    if (httpd_query_key_value(body, key, out, n) == ESP_OK) {
        web_url_decode(out);
    } else {
        out[0] = 0;
    }
}

static esp_err_t notify_api(httpd_req_t *req)
{
    char q[200] = "", tok[80] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "tok", tok, sizeof tok);
    if (!tfn_store_tok_ok(tok)) {
        reply(req, "{\"ok\":false,\"msg\":\"the token of the manager is not right\"}");
        return ESP_OK;
    }
    if (req->method == HTTP_GET) {
        const int max = 16384;
        char *b = heap_caps_malloc(max, MALLOC_CAP_SPIRAM);
        if (!b) {
            return ESP_FAIL;
        }
        int k = notify_json(b, max, qint(q, "clear", 0) != 0);
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, b, k);
        heap_caps_free(b);
        return ESP_OK;
    }
    const int n = req->content_len < 2000 ? (int)req->content_len : 2000;
    char *body = heap_caps_calloc(1, 2001 + 4 * 640, MALLOC_CAP_SPIRAM);
    if (!body) {
        return ESP_FAIL;
    }
    int got = 0;
    while (got < n) {
        int r = httpd_req_recv(req, body + got, n - got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    body[got] = 0;
    char *app = body + 2001, *title = app + 640, *text = title + 640, *link = text + 640;
    char a[3][64], cb[160];
    field(body, "app", app, 160);
    field(body, "title", title, 640);
    field(body, "text", text, 640);
    field(body, "link", link, 320);
    field(body, "a1", a[0], sizeof a[0]);
    field(body, "a2", a[1], sizeof a[1]);
    field(body, "a3", a[2], sizeof a[2]);
    field(body, "cb", cb, sizeof cb);
    const char *act[NT_NACT] = {a[0], a[1], a[2]};
    uint32_t id = notify_post_ex(app[0] ? app : NT_SYSTEM, title, text, link, act, cb);
    heap_caps_free(body);
    char o[64];
    snprintf(o, sizeof o, "{\"ok\":true,\"id\":%lu}", (unsigned long)id);
    reply(req, o);
    ESP_LOGI(TAG, "a notice of the manager: id %lu", (unsigned long)id);
    return ESP_OK;
}

// USB-C HEADPHONES (the CoreS3): the state and the last events (no USB console while the headphones are in)
static esp_err_t phones_api(httpd_req_t *req)
{
    static char b[4096];
    usb_phones_report(b, sizeof b);
    reply(req, b);
    return ESP_OK;
}

void web_sys_register(void *h)
{
    httpd_uri_t u[] = {
            {.uri = "/api/msg", .method = HTTP_GET, .handler = msg_api},
            {.uri = "/api/msg", .method = HTTP_POST, .handler = msg_api},
            {.uri = "/api/locate", .method = HTTP_GET, .handler = locate_api},
            {.uri = "/api/sweep", .method = HTTP_GET, .handler = sweep_api},
            {.uri = "/api/splash", .method = HTTP_GET, .handler = splash_api},
            {.uri = "/api/splash", .method = HTTP_POST, .handler = splash_api},
            {.uri = "/api/notify", .method = HTTP_GET, .handler = notify_api},
            {.uri = "/api/notify", .method = HTTP_POST, .handler = notify_api},
            {.uri = "/api/usbphones", .method = HTTP_GET, .handler = phones_api},
    };
    for (size_t i = 0; i < sizeof u / sizeof u[0]; i++) {
        httpd_register_uri_handler(h, &u[i]);
    }
}
