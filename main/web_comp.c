// THE COMPANIONS on the board page (/companions, main/web/comp.html): the paired companions (/data/companions.bin) and
// the remotes of CONTROL (/data/remote.bin) in a readable form, a backup (one JSON file, made by the page) and a
// restore. The page reads and writes the two files with /api/comp?f=companions|remotes (GET: the bytes; POST: new
// bytes). A restore waits while an app uses the companions (the app saves its own copy at its close).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "storage.h"
#include "web_comp.h"
#include "web_util.h"

static const char *TAG = "web_comp";
#define COMP_FILE_MAX (200 * 1024)

// main/companion.c (not in the ABI)
bool comp_in_use(void);
void comp_files_changed(void);

extern const char comp_html_start[] asm("_binary_comp_html_start");

static esp_err_t page_get(httpd_req_t *req)
{
    return web_send_page(req, comp_html_start);
}

static const char *file_of(httpd_req_t *req)
{
    char q[48] = "", f[16] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "f", f, sizeof f);
    return !strcmp(f, "remotes") ? "/data/remote.bin" : !strcmp(f, "companions") ? "/data/companions.bin" : NULL;
}

static esp_err_t answer(httpd_req_t *req, bool ok, const char *msg)
{
    char o[160];
    snprintf(o, sizeof o, "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", msg);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, o);
    return ESP_OK;
}

static esp_err_t api_get(httpd_req_t *req)
{
    const char *path = file_of(req);
    if (!path) {
        return answer(req, false, "f = companions or remotes");
    }
    uint8_t *b = heap_caps_malloc(COMP_FILE_MAX, MALLOC_CAP_SPIRAM);
    size_t n = 0;
    if (!b || !storage_read(path, b, COMP_FILE_MAX, &n) || !n) {
        heap_caps_free(b);
        httpd_resp_set_status(req, "404 Not Found");
        return answer(req, false, "no file");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send(req, (const char *)b, (ssize_t)n);
    heap_caps_free(b);
    return ESP_OK;
}

static esp_err_t api_post(httpd_req_t *req)
{
    const char *path = file_of(req);
    if (!path) {
        return answer(req, false, "f = companions or remotes");
    }
    if (comp_in_use()) {
        return answer(req, false, "close COMPANIONS and CONTROL on the board first");
    }
    const int len = req->content_len;
    if (len < 4 || len > COMP_FILE_MAX) {
        return answer(req, false, "a bad size");
    }
    uint8_t *b = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
    if (!b) {
        return answer(req, false, "no memory");
    }
    int got = 0;
    while (got < len) {
        const int r = httpd_req_recv(req, (char *)b + got, (size_t)(len - got));
        if (r <= 0) {
            break;
        }
        got += r;
    }
    // (only a file of its kind: the companions 'T' 'M', the remotes "RM" version 1 or 2)
    const bool comps = !strcmp(path, "/data/companions.bin");
    const bool kind_ok = comps ? b[0] == 'T' && b[1] == 'M'
                               : b[3] == 0x52 && b[2] == 0x4D && b[1] == 0x31 && (b[0] == 1 || b[0] == 2);
    const bool ok = got == len && kind_ok && storage_write(path, b, (size_t)len);
    heap_caps_free(b);
    if (ok) {
        comp_files_changed();
    }
    ESP_LOGI(TAG, "restore %s: %d bytes, %s", path, len, ok ? "OK" : "refused");
    return answer(req, ok, ok ? "restored" : kind_ok ? "the write failed" : "not a file of this kind");
}

void web_comp_register(void *h)
{
    httpd_uri_t u[] = {
            {.uri = "/companions", .method = HTTP_GET, .handler = page_get},
            {.uri = "/api/comp", .method = HTTP_GET, .handler = api_get},
            {.uri = "/api/comp", .method = HTTP_POST, .handler = api_post},
    };
    for (size_t i = 0; i < sizeof u / sizeof u[0]; i++) {
        httpd_register_uri_handler(h, &u[i]);
    }
}
