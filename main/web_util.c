// Small helpers of the web pages and the /api answers (web_util.h).
#include "web_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi.h"

#define JSON_CHUNK 160

esp_err_t web_send_page(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "text/html");
    wifi_send_page_head(req);
    web_chunk(req, body);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t web_chunk(httpd_req_t *req, const char *s)
{
    if (!s || !s[0]) {
        return ESP_OK;
    }
    return httpd_resp_sendstr_chunk(req, s);
}

void web_json_str(httpd_req_t *req, const char *s)
{
    char o[JSON_CHUNK];
    int k = 0;
    o[k++] = '"';
    for (; *s; s++) {
        // (room for the longest escape, "\u00XX", and the end quote)
        if (k > (int)sizeof o - 8) {
            httpd_resp_send_chunk(req, o, k);
            k = 0;
        }
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            o[k++] = '\\';
            o[k++] = (char)c;
        } else if (c < ' ') {
            k += snprintf(o + k, 8, "\\u%04x", c);
        } else {
            o[k++] = (char)c;
        }
    }
    o[k++] = '"';
    httpd_resp_send_chunk(req, o, k);
}

void web_url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

// the system names in the board storage (web_data_sys_path), in lower case
static const char *const WEB_SYS_NAMES[] = {"store2.bin",      "tfn_sigok.bin", "tfn_policy.txt", "tfn_policy.sig",
                                            "tfn_pool.bin",    "tfn_pool.tmp",  "apps",           "ai_accounts.enc",
                                            "ai_accounts.tmp", "ai_limits.key", "ai_limits.kew",  NULL};

// true when the name n (len bytes, in lower case) is a system name or the "<name>.part" of one
static bool web_sys_name(const char *n, int len)
{
    for (int i = 0; WEB_SYS_NAMES[i]; i++) {
        const int sl = (int)strlen(WEB_SYS_NAMES[i]);
        if (strncmp(n, WEB_SYS_NAMES[i], (size_t)sl)) {
            continue;
        }
        if (len == sl || (len == sl + 5 && !strncmp(n + sl, ".part", 5))) {
            return true;
        }
    }
    return false;
}

bool web_data_sys_path(const char *rel)
{
    while (*rel == '/' || *rel == ' ') {
        rel++;  // (the spaces at the start too: a refusal of a different name is safe)
    }
    int len = 0;
    while (rel[len] && rel[len] != '/') {
        if (rel[len] == '~') {
            return true;
        }
        len++;
    }
    const int raw = len;
    while (len > 0 && (rel[len - 1] == '.' || rel[len - 1] == ' ')) {
        len--;
    }
    if (raw > 0 && len == 0) {
        return true;  // (a first name of only dots and spaces: FAT can read it as a different name)
    }
    char n[24];
    if (len >= (int)sizeof n) {
        return false;  // (longer than each system name)
    }
    for (int i = 0; i < len; i++) {
        n[i] = (char)tolower((unsigned char)rel[i]);
    }
    n[len] = 0;
    return web_sys_name(n, len);
}

const char *web_data_route_path(const char *uri, const char *prefix)
{
    const size_t pl = strlen(prefix);
    if (strncmp(uri, prefix, pl)) {
        return NULL;
    }
    const char *rel = uri + pl;
    if (rel[0] != '/' || strlen(rel) >= 200 || strstr(rel, "..")) {
        return NULL;
    }
    for (const char *s = rel; *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c < ' ' || c == 127 || c == '\\') {
            return NULL;  // (FAT reads a backslash as a '/': a backslash and then store2.bin opens /store2.bin)
        }
    }
    return web_data_sys_path(rel) ? NULL : rel;
}
