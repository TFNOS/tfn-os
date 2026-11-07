// The "Home Assistant" part of the board web page (/ha) and its API (see ha.h).
//   GET  /api/ha                 {"url","token_set","items":[{"id","name"}]}  (never the token)
//   POST /api/ha/config          body: the address on line 1, the token on line 2 ("" keeps it)
//   GET  /api/ha/test            {"ok","status","message"}
//   GET  /api/ha/search?q=&d=    [{"id","name","state"}]  (HA does the search: a template)
//   POST /api/ha/items           body: "entity_id|name" lines, in the order of the tiles

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_util.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "ha.h"
#include "web_ha.h"
#include "wifi.h"

// the kinds of entities that the HOME app can show or control
#define DOMAINS                                                                                                        \
    "'light','switch','fan','cover','scene','script','climate','lock','media_player','input_boolean',"                 \
    "'button','input_button','sensor','binary_sensor','automation','siren','vacuum'"

static void jstr(char *out, int max, const char *s)
{
    int k = 0;
    for (; *s && k < max - 1; s++) {
        out[k++] = (*s == '"' || *s == '\\' || (unsigned char)*s < 32) ? ' ' : *s;
    }
    out[k] = 0;
}

static void json_head(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
}

static char *read_body(httpd_req_t *req, int max)
{
    if (req->content_len <= 0 || (int)req->content_len > max) {
        return NULL;
    }
    char *b = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM);
    if (!b) {
        return NULL;
    }
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, b + got, req->content_len - got);
        if (r <= 0) {
            free(b);
            return NULL;
        }
        got += r;
    }
    b[got] = 0;
    return b;
}

static esp_err_t api_get(httpd_req_t *req)
{
    ha_item_t it[HA_MAX];
    int n = ha_items(it, HA_MAX);
    char *b = heap_caps_malloc(6000, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    char u[100], id[HA_ID_LEN], nm[HA_NAME_LEN];
    jstr(u, sizeof u, ha_url());
    int k = snprintf(b, 6000, "{\"url\":\"%s\",\"token_set\":%s,\"items\":[", u, ha_token_set() ? "true" : "false");
    for (int i = 0; i < n; i++) {
        jstr(id, sizeof id, it[i].id);
        jstr(nm, sizeof nm, it[i].name);
        k += snprintf(b + k, 6000 - k, "%s{\"id\":\"%s\",\"name\":\"%s\"}", i ? "," : "", id, nm);
    }
    k += snprintf(b + k, 6000 - k, "]}");
    json_head(req);
    httpd_resp_send(req, b, k);
    free(b);
    return ESP_OK;
}

static esp_err_t api_config(httpd_req_t *req)
{
    char *b = read_body(req, 600);
    bool ok = false;
    if (b) {
        char *url = b, *tok = strchr(b, '\n');
        if (tok) {
            *tok++ = 0;
        } else {
            tok = "";
        }
        char *e = strchr(tok, '\n');
        if (e) {
            *e = 0;
        }
        for (char *p = url + strlen(url); p > url && (p[-1] == '\r' || p[-1] == ' ');) {
            *--p = 0;
        }
        for (char *p = tok + strlen(tok); p > tok && (p[-1] == '\r' || p[-1] == ' ');) {
            *--p = 0;
        }
        ok = ha_set_config(url, tok);
        memset(b, 0, strlen(b));
        free(b);
    }
    json_head(req);
    httpd_resp_sendstr(
            req,
            ok ? "{\"ok\":true}"
               : "{\"ok\":false,\"error\":\"the address is not right (an example: http://homeassistant.local:8123)\"}");
    return ESP_OK;
}

static esp_err_t api_test(httpd_req_t *req)
{
    char out[160], msg[160], b[260];
    int st = ha_get_api(out, sizeof out);
    jstr(msg, sizeof msg, st < 0 ? "no connection to the address" : out);
    snprintf(b, sizeof b, "{\"ok\":%s,\"status\":%d,\"message\":\"%s\"}", st == 200 ? "true" : "false", st, msg);
    json_head(req);
    httpd_resp_sendstr(req, b);
    return ESP_OK;
}

static esp_err_t api_search(httpd_req_t *req)
{
    char qs[200], q[64] = "", d[32] = "";
    if (httpd_req_get_url_query_str(req, qs, sizeof qs) == ESP_OK) {
        httpd_query_key_value(qs, "q", q, sizeof q);
        httpd_query_key_value(qs, "d", d, sizeof d);
    }
    for (char *p = q; *p; p++) {
        if (*p == '+') {
            *p = ' ';
        } else if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    ha_clean(q);
    ha_clean(d);
    char tpl[900];
    snprintf(tpl, sizeof tpl,
             "{%% set q = '%s' %%}{%% set n = namespace(c=0) %%}"
             "{%% for s in states | sort(attribute='entity_id') %%}"
             "{%% if s.domain in [%s] and ('%s' == '' or s.domain == '%s') and (q in s.entity_id or q in (s.name | "
             "lower)) and n.c < 80 %%}"
             "{{ s.entity_id }}|{{ s.name | replace('|', '/') | replace('\\n', ' ') }}|{{ s.state }}\n"
             "{%% set n.c = n.c + 1 %%}{%% endif %%}{%% endfor %%}",
             q, DOMAINS, d, d);
    char *res = heap_caps_malloc(12000, MALLOC_CAP_SPIRAM), *b = heap_caps_malloc(16000, MALLOC_CAP_SPIRAM);
    if (!res || !b) {
        free(res);
        free(b);
        return ESP_FAIL;
    }
    int st = ha_template(tpl, res, 12000);
    int k = 0;
    if (st != 200) {
        char m[120];
        jstr(m, sizeof m, st < 0 ? "no connection to Home Assistant" : res);
        k = snprintf(b, 16000, "{\"error\":\"%d: %s\"}", st, m);
    } else {
        k = snprintf(b, 16000, "[");
        int cnt = 0;
        for (char *line = strtok(res, "\n"); line && k < 15500; line = strtok(NULL, "\n")) {
            char *n = strchr(line, '|');
            if (!n) {
                continue;
            }
            *n++ = 0;
            char *s = strchr(n, '|');
            if (!s) {
                continue;
            }
            *s++ = 0;
            char id[HA_ID_LEN], nm[64], sv[40];
            jstr(id, sizeof id, line);
            jstr(nm, sizeof nm, n);
            jstr(sv, sizeof sv, s);
            k += snprintf(b + k, 16000 - k, "%s{\"id\":\"%s\",\"name\":\"%s\",\"state\":\"%s\"}", cnt++ ? "," : "", id,
                          nm, sv);
        }
        k += snprintf(b + k, 16000 - k, "]");
    }
    json_head(req);
    httpd_resp_send(req, b, k);
    free(res);
    free(b);
    return ESP_OK;
}

static esp_err_t api_items(httpd_req_t *req)
{
    char *b = read_body(req, 4000);
    if (!b) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_OK;
    }
    static ha_item_t it[HA_MAX];
    int n = 0;
    for (char *line = strtok(b, "\n"); line && n < HA_MAX; line = strtok(NULL, "\n")) {
        char *nm = strchr(line, '|');
        if (nm) {
            *nm++ = 0;
        }
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) {
            line[--l] = 0;
        }
        if (!l || !strchr(line, '.')) {
            continue;
        }
        snprintf(it[n].id, HA_ID_LEN, "%.63s", line);
        snprintf(it[n].name, HA_NAME_LEN, "%.31s", nm && nm[0] ? nm : line);
        char *r = strchr(it[n].name, '\r');
        if (r) {
            *r = 0;
        }
        n++;
    }
    free(b);
    ha_set_items(it, n);
    json_head(req);
    char o[40];
    snprintf(o, sizeof o, "{\"ok\":true,\"count\":%d}", n);
    httpd_resp_sendstr(req, o);
    return ESP_OK;
}

// the page body: main/web/ha.html, in the firmware (CMake EMBED_TXTFILES: a 0 at the end)
extern const char ha_html_start[] asm("_binary_ha_html_start");

static esp_err_t page_get(httpd_req_t *req)
{
    return web_send_page(req, ha_html_start);
}

void web_ha_register(void *h)
{
    httpd_uri_t u[] = {
            {.uri = "/ha", .method = HTTP_GET, .handler = page_get},
            {.uri = "/api/ha", .method = HTTP_GET, .handler = api_get},
            {.uri = "/api/ha/config", .method = HTTP_POST, .handler = api_config},
            {.uri = "/api/ha/test", .method = HTTP_GET, .handler = api_test},
            {.uri = "/api/ha/search", .method = HTTP_GET, .handler = api_search},
            {.uri = "/api/ha/items", .method = HTTP_POST, .handler = api_items},
    };
    for (size_t i = 0; i < sizeof u / sizeof u[0]; i++) {
        httpd_register_uri_handler(h, &u[i]);
    }
}
