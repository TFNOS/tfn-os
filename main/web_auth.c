// THE WEB PASSWORD: the gate of the board web server (see web_auth.h).
// - The password is never kept: NVS (namespace "webauth") has a random salt and PBKDF2-HMAC-SHA256 of it (2000
//   rounds, about 0.25 s on the 3.49).
// - The gate: the link wraps httpd_register_uri_handler (main/CMakeLists.txt), so each handler that any file (or an
//   installed app) registers gets this gate in front of it. A request passes with the session cookie of /login, or
//   with an HTTP Basic header (a script). Else: a page goes to /login, an API call gets 401 JSON. The server never
//   sends "WWW-Authenticate: Basic": a browser that kept a Basic password would send it for each other site's request
//   too (a forged request to the board).
// - A WebSocket (/ws/audio) is checked before its handshake (CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT). A firmware
//   without that option refuses all WebSocket frames while a password is set (closed, not open).
// - Wrong passwords: WA_FREE_TRIES from one address, then a wait that doubles (web_auth_core.c).
// - The pool manager: a request with a good header X-TFN-Auth (an HMAC with the request key of this board) passes,
//   also with a password (tfn_pool.h). A wrong header is not a wrong password: the request goes on to the other ways.
//   GET /api/pool (public values only) needs no password.
#include "tfn_util.h"
#include "web_auth.h"
#include "web_auth_core.h"
#include "device_guard.h"
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "friends.h"
#include "tfn_pool.h"

static const char *TAG = "web";

#define NS     "webauth"
#define ROUNDS 2000
#define COOKIE "tfn_s"
#define BC_N   4                 // the Basic headers that passed (their SHA-256), so a script does not wait 0.1 s
#define BC_MS  (10 * 60 * 1000)  // each time

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_on;
static uint8_t s_salt[16], s_hash[32];
static uint32_t s_rounds = ROUNDS;
static httpd_handle_t s_srv;
static struct {
    uint8_t d[32];
    int64_t at;
} s_bc[BC_N];

void web_auth_init(void)
{
    psa_crypto_init();  // (the hash of a Basic header comes before any PBKDF2)
    nvs_handle_t h;
    bool on = false;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t ls = sizeof s_salt, lh = sizeof s_hash;
        uint32_t r = 0;
        on = nvs_get_blob(h, "salt", s_salt, &ls) == ESP_OK && ls == sizeof s_salt &&
             nvs_get_blob(h, "hash", s_hash, &lh) == ESP_OK && lh == sizeof s_hash &&
             nvs_get_u32(h, "rounds", &r) == ESP_OK && r > 0;
        if (on) {
            s_rounds = r;
        }
        nvs_close(h);
    }
    s_on = on;
    ESP_LOGI(TAG, "the web password is %s", on ? "on" : "off");
}

bool web_auth_on(void)
{
    return s_on;
}

// PBKDF2-HMAC-SHA256 (RFC 8018), one block of 32 bytes
static bool pbkdf2(const char *pw, const uint8_t salt[16], uint32_t rounds, uint8_t out[32])
{
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    size_t pl = strlen(pw);
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&at, PSA_BYTES_TO_BITS(pl));
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    if (psa_import_key(&at, (const uint8_t *)pw, pl, &k) != PSA_SUCCESS) {
        return false;
    }
    uint8_t u[32], in[20];
    memcpy(in, salt, 16);
    in[16] = 0;
    in[17] = 0;
    in[18] = 0;
    in[19] = 1;  // INT(1): the first block
    size_t ol = 0;
    bool ok = psa_mac_compute(k, PSA_ALG_HMAC(PSA_ALG_SHA_256), in, sizeof in, u, 32, &ol) == PSA_SUCCESS && ol == 32;
    memcpy(out, u, 32);
    uint8_t v[32];
    for (uint32_t i = 1; ok && i < rounds; i++) {
        ok = psa_mac_compute(k, PSA_ALG_HMAC(PSA_ALG_SHA_256), u, 32, v, 32, &ol) == PSA_SUCCESS && ol == 32;
        memcpy(u, v, 32);  // (the next round reads this one: no shared input and output buffer)
        for (int j = 0; j < 32; j++) {
            out[j] ^= u[j];
        }
    }
    psa_destroy_key(k);
    memset(u, 0, sizeof u);
    memset(v, 0, sizeof v);
    return ok;
}

static bool pw_right(const char *pw)
{
    uint8_t salt[16], want[32], got[32];
    uint32_t rounds;
    taskENTER_CRITICAL(&s_mux);
    memcpy(salt, s_salt, 16);
    memcpy(want, s_hash, 32);
    rounds = s_rounds;
    taskEXIT_CRITICAL(&s_mux);
    bool ok = pw[0] && pbkdf2(pw, salt, rounds, got) && wa_eq(got, want, 32);
    memset(got, 0, sizeof got);
    return ok;
}

#if CONFIG_TFN_DEVICE_PROTECTION
bool web_auth_check_local(const char *pw)
{
    if (!s_on || !pw) {
        return false;
    }
    const uint32_t local_id = 0xFFFFFFFDu;
    int64_t now = tfn_now_ms(), wait;
    taskENTER_CRITICAL(&s_mux);
    wait = wa_wait_ms(local_id, now);
    taskEXIT_CRITICAL(&s_mux);
    if (wait) {
        return false;
    }
    bool ok = pw_right(pw);
    taskENTER_CRITICAL(&s_mux);
    if (ok) {
        wa_pass(local_id);
    } else {
        wa_fail(local_id, now);
    }
    taskEXIT_CRITICAL(&s_mux);
    return ok;
}
#endif

// each connection ends (a new password: the browsers log in again, a WebSocket of LISTEN ends)
static void close_all(void)
{
    if (!s_srv) {
        return;
    }
    size_t n = 16;
    int fds[16];
    if (httpd_get_client_list(s_srv, &n, fds) != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        httpd_sess_trigger_close(s_srv, fds[i]);
    }
}

bool web_auth_set(const char *pw, char *msg, int len)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (device_guard_enabled() && !device_guard_local_admin() && !device_guard_usb_admin()) {
        snprintf(msg, (size_t)len, "ADMIN ACCESS REQUIRED");
        return false;
    }
#endif
    size_t n = pw ? strlen(pw) : 0;
#if CONFIG_TFN_DEVICE_PROTECTION
    if (device_guard_enabled() && !n) {
        snprintf(msg, (size_t)len, "PROTECTED MODE NEEDS A WEB PASSWORD");
        return false;
    }
#endif
    if (n && (n < WEB_PASS_MIN || n > WEB_PASS_MAX)) {
        snprintf(msg, (size_t)len, "THE PASSWORD NEEDS %d TO %d CHARACTERS", WEB_PASS_MIN, WEB_PASS_MAX);
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)pw[i] < 32 || pw[i] == 127) {
            snprintf(msg, (size_t)len, "THE PASSWORD HAS A CONTROL CHARACTER");
            return false;
        }
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        snprintf(msg, (size_t)len, "NO SETTINGS STORE");
        return false;
    }
    bool ok = true;
    uint8_t salt[16], hash[32];
    if (n) {
        esp_fill_random(salt, sizeof salt);
        ok = pbkdf2(pw, salt, ROUNDS, hash) && nvs_set_blob(h, "salt", salt, sizeof salt) == ESP_OK &&
             nvs_set_blob(h, "hash", hash, sizeof hash) == ESP_OK && nvs_set_u32(h, "rounds", ROUNDS) == ESP_OK;
    } else {
        nvs_erase_key(h, "hash");
        nvs_erase_key(h, "salt");
        nvs_erase_key(h, "rounds");
    }
    ok = ok && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) {
        snprintf(msg, (size_t)len, "COULD NOT SAVE THE PASSWORD");
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    if (n) {
        memcpy(s_salt, salt, 16);
        memcpy(s_hash, hash, 32);
        s_rounds = ROUNDS;
    }
    s_on = n > 0;
    wa_sess_clear();
    wa_tries_clear();
    memset(s_bc, 0, sizeof s_bc);
    taskEXIT_CRITICAL(&s_mux);
    memset(hash, 0, sizeof hash);
    close_all();
    ESP_LOGI(TAG, "the web password is %s", n ? "on (a new one)" : "off");
    snprintf(msg, (size_t)len, n ? "THE WEB PASSWORD IS ON" : "THE WEB PASSWORD IS OFF");
    return true;
}

// ---------------------------------------------------------------- the gate

// the client address: IPv4 as it is, an IPv6 address as a hash (an IPv4 in IPv6: its 4 bytes)
static uint32_t client_ip(httpd_req_t *req)
{
    struct sockaddr_storage sa;
    socklen_t sl = sizeof sa;
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&sa, &sl) != 0) {
        return 0;
    }
    if (sa.ss_family == AF_INET) {
        return ((struct sockaddr_in *)&sa)->sin_addr.s_addr;
    }
    const uint8_t *b = (const uint8_t *)&((struct sockaddr_in6 *)&sa)->sin6_addr;
    uint32_t v4;
    static const uint8_t MAPPED[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    if (!memcmp(b, MAPPED, 12)) {
        memcpy(&v4, b + 12, 4);
        return v4;
    }
    uint32_t hv = 2166136261u;
    for (int i = 0; i < 16; i++) {
        hv = (hv ^ b[i]) * 16777619u;
    }
    return hv;
}

enum {
    AUTH_OK,
    AUTH_NO,
    AUTH_WAIT
};

static int check(httpd_req_t *req, int64_t *wait)
{
    *wait = 0;
    // the pool manager: a good header X-TFN-Auth passes, also with a password (tfn_pool.h)
    char ph[128];
    const size_t pl = httpd_req_get_hdr_value_len(req, "X-TFN-Auth");
    if (pl && pl < sizeof ph && httpd_req_get_hdr_value_str(req, "X-TFN-Auth", ph, sizeof ph) == ESP_OK &&
        tfn_pool_web_ok(http_method_str(req->method), req->uri, ph)) {
        return AUTH_OK;
    }
    // GET /api/pool has only public values (the board nonce of the request header, the box key): no password
    if (req->method == HTTP_GET && !strncmp(req->uri, "/api/pool", 9) && (!req->uri[9] || req->uri[9] == '?')) {
        return AUTH_OK;
    }
#if CONFIG_TFN_DEVICE_PROTECTION
    if (!s_on) {
        return AUTH_NO;
    }
#else
    if (!s_on) {
        return AUTH_OK;
    }
#endif
    const int64_t now = tfn_now_ms();
    char tok[WA_TOKEN_LEN + 2];
    size_t tl = sizeof tok;
    if (httpd_req_get_cookie_val(req, COOKIE, tok, &tl) == ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        bool ok = wa_sess_ok(tok, now);
        taskEXIT_CRITICAL(&s_mux);
        if (ok) {
            return AUTH_OK;
        }
    }
    size_t hl = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hl == 0 || hl > 200) {
        return AUTH_NO;
    }
    char hv[204];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hv, sizeof hv) != ESP_OK) {
        return AUTH_NO;
    }
    uint8_t d[32];
    size_t dl = 0;
    bool known = false;
    if (psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)hv, strlen(hv), d, sizeof d, &dl) == PSA_SUCCESS) {
        taskENTER_CRITICAL(&s_mux);
        for (int i = 0; i < BC_N; i++) {
            if (s_bc[i].at && now - s_bc[i].at < BC_MS && wa_eq(s_bc[i].d, d, 32)) {
                known = true;
            }
        }
        taskEXIT_CRITICAL(&s_mux);
    }
    if (known) {
        return AUTH_OK;
    }
    const uint32_t ip = client_ip(req);
    taskENTER_CRITICAL(&s_mux);
    *wait = wa_wait_ms(ip, now);
    taskEXIT_CRITICAL(&s_mux);
    if (*wait) {
        return AUTH_WAIT;
    }
    char pw[WEB_PASS_MAX + 2];
    bool ok = wa_basic_password(hv, pw, sizeof pw) && pw_right(pw);
    memset(pw, 0, sizeof pw);
    memset(hv, 0, sizeof hv);
    taskENTER_CRITICAL(&s_mux);
    if (ok) {
        wa_pass(ip);
        int k = 0;
        for (int i = 1; i < BC_N; i++) {
            if (s_bc[i].at < s_bc[k].at) {
                k = i;
            }
        }
        memcpy(s_bc[k].d, d, 32);
        s_bc[k].at = now;
    } else {
        wa_fail(ip, now);
    }
    taskEXIT_CRITICAL(&s_mux);
    if (!ok) {
        ESP_LOGW(TAG, "a wrong password (Basic) for %s", req->uri);
    }
    return ok ? AUTH_OK : AUTH_NO;
}

// a page (a browser asks for HTML) goes to the login; an API call gets 401 (or 429 in the wait) as JSON
static esp_err_t deny(httpd_req_t *req, int r, int64_t wait)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (r == AUTH_WAIT) {
        char s[16], j[64];
        snprintf(s, sizeof s, "%d", (int)((wait + 999) / 1000));
        snprintf(j, sizeof j, "{\"error\":\"wait\",\"secs\":%s}", s);
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_hdr(req, "Retry-After", s);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, j);
    }
    char acc[64] = "";
    httpd_req_get_hdr_value_str(req, "Accept", acc, sizeof acc);
    bool page = req->method == HTTP_GET && strncmp(req->uri, "/api/", 5) && strncmp(req->uri, "/ws/", 4) &&
                strstr(acc, "text/html");
    if (page) {
        static char loc[300];  // (the server task only)
        char *o = loc + snprintf(loc, sizeof loc, "/login?next=");
        for (const char *p = req->uri; *p && o < loc + sizeof loc - 4; p++) {
            unsigned char c = (unsigned char)*p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/-_.~", c)) {
                *o++ = (char)c;
            } else {
                o += sprintf(o, "%%%02X", c);
            }
        }
        *o = 0;
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", loc);
        return httpd_resp_send(req, NULL, 0);
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"error\":\"login\",\"login\":\"/login\"}");
}

typedef struct {
    esp_err_t (*handler)(httpd_req_t *r);
    void *ctx;
    bool ws;
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
    esp_err_t (*pre)(httpd_req_t *r);
#endif
} gate_t;

static esp_err_t gate(httpd_req_t *req)
{
    gate_t *g = req->user_ctx;
    if (g->ws) {
        // a frame of a WebSocket: its handshake passed the check (ws_pre), and a new password closed the old ones
#if !CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
        if (s_on) {
            return ESP_FAIL;  // (no check of the handshake in this firmware: no WebSocket with a password)
        }
#endif
    } else {
        int64_t wait;
        int r = check(req, &wait);
        if (r != AUTH_OK) {
            return deny(req, r, wait);
        }
    }
    req->user_ctx = g->ctx;
    return g->handler(req);
}

#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
static esp_err_t ws_pre(httpd_req_t *req)
{
    gate_t *g = req->user_ctx;
    int64_t wait;
    int r = check(req, &wait);
    if (r != AUTH_OK) {
        deny(req, r, wait);
        return ESP_FAIL;  // (no handshake: the connection closes)
    }
    if (!g->pre) {
        return ESP_OK;
    }
    req->user_ctx = g->ctx;
    esp_err_t e = g->pre(req);
    req->user_ctx = g;
    return e;
}
#endif

esp_err_t __real_httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);

esp_err_t __wrap_httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *u)
{
    if (!u || !u->handler) {
        return __real_httpd_register_uri_handler(handle, u);
    }
    s_srv = handle;
    gate_t *g = heap_caps_malloc(sizeof *g, MALLOC_CAP_SPIRAM);
    if (!g) {
        g = malloc(sizeof *g);
    }
    if (!g) {
        return ESP_ERR_NO_MEM;  // (never an open handler)
    }
    *g = (gate_t){.handler = u->handler, .ctx = u->user_ctx, .ws = u->is_websocket};
    httpd_uri_t c = *u;
    c.handler = gate;
    c.user_ctx = g;
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
    if (c.is_websocket) {
        g->pre = u->ws_pre_handshake_cb;
        c.ws_pre_handshake_cb = ws_pre;
    }
#endif
    esp_err_t e = __real_httpd_register_uri_handler(handle, &c);
    if (e != ESP_OK) {
        free(g);
    }
    return e;
}

// ---------------------------------------------------------------- the pages /login and /logout

static void html_text(char *o, size_t max, const char *s)
{
    size_t n = 0;
    for (; *s && n + 7 < max; s++) {
        const char *e = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" : NULL;
        if (e) {
            size_t l = strlen(e);
            memcpy(o + n, e, l);
            n += l;
        } else {
            o[n++] = *s;
        }
    }
    o[n] = 0;
}

static esp_err_t login_page(httpd_req_t *req, const char *next, const char *note, const char *status)
{
    char nx[300], nm[64];
    html_text(nx, sizeof nx, next);
    html_text(nm, sizeof nm, friends_my_name());
    static char page[1800];  // (the server task only)
    snprintf(page, sizeof page,
             "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
             "content='width=device-width,initial-scale=1'>"
             "<title>%s: log in</title><style>body{background:#12142a;color:#e8e8f0;font:17px system-ui,sans-serif;"
             "display:flex;justify-content:center;align-items:center;min-height:90vh;margin:0 16px}"
             "form{background:#1d2140;padding:28px;border-radius:14px;width:100%%;max-width:340px}"
             "h1{font-size:22px;margin:0 0 4px}p{color:#9aa0c0;margin:0 0 18px;font-size:14px}"
             "input{box-sizing:border-box;width:100%%;font-size:18px;padding:12px;border-radius:8px;border:1px solid "
             "#46508a;"
             "background:#0e1024;color:#fff;margin-bottom:14px}button{width:100%%;font-size:18px;padding:12px;border:0;"
             "border-radius:8px;background:#f5d21e;color:#000;font-weight:600}.n{color:#ff8080;margin:0 0 12px}</style>"
             "</head><body><form method=post action=/login><h1>%s</h1><p>This board needs its web password "
             "(SETUP &gt; WEB PASSWORD on the board).</p>%s%s%s"
             "<input type=password name=p autofocus autocomplete=current-password placeholder=Password>"
             "<input type=hidden name=next value=\"%s\"><button>Log in</button></form></body></html>",
             nm, nm, note[0] ? "<div class=n>" : "", note, note[0] ? "</div>" : "", nx);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, page);
}

static esp_err_t go(httpd_req_t *req, const char *where)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", where);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t login_get(httpd_req_t *req)
{
    char q[300] = "", nx0[260] = "", nx[260];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) {
        wa_form_field(q, "next", nx0, sizeof nx0);
    }
    wa_next_path(nx0, nx, sizeof nx);
    if (!s_on) {
        return go(req, nx);
    }
    return login_page(req, nx, "", "200 OK");
}

static esp_err_t login_post(httpd_req_t *req)
{
    char body[400];
    int n = 0;
    while (n < (int)sizeof body - 1 && n < (int)req->content_len) {
        int r = httpd_req_recv(req, body + n, sizeof body - 1 - (size_t)n);
        if (r <= 0) {
            break;
        }
        n += r;
    }
    body[n] = 0;
    char pw[WEB_PASS_MAX + 2] = "", nx0[260] = "", nx[260];
    wa_form_field(body, "next", nx0, sizeof nx0);
    wa_next_path(nx0, nx, sizeof nx);
    if (!s_on) {
        return go(req, nx);
    }
    const uint32_t ip = client_ip(req);
    const int64_t now = tfn_now_ms();
    taskENTER_CRITICAL(&s_mux);
    int64_t wait = wa_wait_ms(ip, now);
    taskEXIT_CRITICAL(&s_mux);
    if (wait) {
        char note[64];
        snprintf(note, sizeof note, "Too many wrong passwords: wait %d s.", (int)((wait + 999) / 1000));
        memset(body, 0, sizeof body);
        return login_page(req, nx, note, "429 Too Many Requests");
    }
    bool ok = wa_form_field(body, "p", pw, sizeof pw) && strlen(pw) <= WEB_PASS_MAX && pw_right(pw);
    memset(body, 0, sizeof body);
    memset(pw, 0, sizeof pw);
    if (!ok) {
        taskENTER_CRITICAL(&s_mux);
        wa_fail(ip, now);
        taskEXIT_CRITICAL(&s_mux);
        ESP_LOGW(TAG, "a wrong password at /login");
        return login_page(req, nx, "Wrong password.", "401 Unauthorized");
    }
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof rnd);
    char tok[WA_TOKEN_LEN + 1];
    taskENTER_CRITICAL(&s_mux);
    wa_pass(ip);
    wa_sess_new(rnd, now, tok);
    taskEXIT_CRITICAL(&s_mux);
    char ck[WA_TOKEN_LEN + 96];
    snprintf(ck, sizeof ck, COOKIE "=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d", tok, (int)(WA_LIFE_MS / 1000));
    httpd_resp_set_hdr(req, "Set-Cookie", ck);
    ESP_LOGI(TAG, "a browser logged in");
    return go(req, nx);
}

static esp_err_t logout_get(httpd_req_t *req)
{
    char tok[WA_TOKEN_LEN + 2];
    size_t tl = sizeof tok;
    if (httpd_req_get_cookie_val(req, COOKIE, tok, &tl) == ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        wa_sess_drop(tok);
        taskEXIT_CRITICAL(&s_mux);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", COOKIE "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return go(req, s_on ? "/login" : "/");
}

void web_auth_register(void *server)
{
    httpd_handle_t s = server;
    s_srv = s;
    // (the real registration: these pages need no password)
    httpd_uri_t lg = {.uri = "/login", .method = HTTP_GET, .handler = login_get};
    httpd_uri_t lp = {.uri = "/login", .method = HTTP_POST, .handler = login_post};
    httpd_uri_t lo = {.uri = "/logout", .method = HTTP_GET, .handler = logout_get};
    __real_httpd_register_uri_handler(s, &lg);
    __real_httpd_register_uri_handler(s, &lp);
    __real_httpd_register_uri_handler(s, &lo);
}
