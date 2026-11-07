// The unit test of GET /api/pub (main/tfn_pub.c): the JSON of the built-in keys, the added keys (each with its name and
// its flags) and the key that waits for the tap. The fake web server of tests/unit/fake/ ends an answer at an empty
// chunk, as ESP-IDF does: the answer must end only at its last chunk, with no chunk after the end, and the JSON text
// must be the expected one. Also the prompt of the key that waits (tfn_pub_tick): a consent question, and an answer
// only for the wait that the screen showed (not for a new wait of the same key with other flags). The fakes of the
// board are in tests/unit/fake_pub/ (and this file).
// Build and run: python tests/unit/run.py
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "host_crypto.h"
#include "sysui.h"
#include "tfn_app_priv.h"
#include "web_auth.h"
#include "web_util.h"
#include "wifi.h"

static int s_fails;

static void expect(bool ok, const char *what)
{
    if (!ok) {
        s_fails++;
        printf("FAIL %s\n", what);
    }
}

// ---------------------------------------------------------------- the fakes of the board
void fk_log(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}

static int s_sem;
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)&s_sem;
}

int xSemaphoreTake(SemaphoreHandle_t s, uint32_t wait)
{
    (void)s;
    (void)wait;
    return 1;
}

int xSemaphoreGive(SemaphoreHandle_t s)
{
    (void)s;
    return 1;
}

static int64_t s_now_us = 50LL * 1000 * 1000;
int64_t esp_timer_get_time(void)
{
    return s_now_us;
}

// NVS: one blob (the namespace "tfnpub" is there after the first write)
static uint8_t s_nvs[TFN_PUB_BLOB];
static size_t s_nvs_n;
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    (void)ns;
    *h = 1;
    return mode == NVS_READONLY && !s_nvs_n ? ESP_FAIL : ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    (void)key;
    if (!s_nvs_n || *len < s_nvs_n) {
        return ESP_FAIL;
    }
    memcpy(out, s_nvs, s_nvs_n);
    *len = s_nvs_n;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len)
{
    (void)h;
    (void)key;
    if (len > sizeof s_nvs) {
        return ESP_FAIL;
    }
    memcpy(s_nvs, v, len);
    s_nvs_n = len;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return ESP_OK;
}

void nvs_close(nvs_handle_t h)
{
    (void)h;
}

psa_status_t psa_hash_compute(psa_algorithm_t alg, const uint8_t *in, size_t n, uint8_t *out, size_t out_size,
                              size_t *out_len)
{
    if (alg != PSA_ALG_SHA_256 || out_size < 32) {
        return -1;
    }
    hc_sha256(in, n, out);
    *out_len = 32;
    return PSA_SUCCESS;
}

bool tfa_sig_ok(const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl)
{
    (void)msg;
    (void)n;
    (void)sig;
    (void)sl;
    return false;
}

bool tfa_sig_ok_pk(const uint8_t *pk, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl)
{
    (void)pk;
    return tfa_sig_ok(msg, n, sig, sl);
}

// the built-in store keys: key 1 is set, key 2 is all zeros (no key)
static uint8_t s_builtin[TFN_PUB_BUILTIN][TFN_PUB_PK];
const uint8_t *tfa_builtin_key(int i)
{
    return s_builtin[i];
}

bool tfa_read_all(const char *path, uint8_t **out, size_t *len, size_t max)
{
    (void)path;
    (void)max;
    *out = NULL;
    *len = 0;
    return false;
}

static int s_rescans;
void tfn_rescan(void)
{
    s_rescans++;
}

bool board_sd_mounted(void)
{
    return false;
}

void sysui_message(const char *utf8, int secs)
{
    (void)utf8;
    (void)secs;
}

// the question of the system layer: the prompt of tfn_pub_tick() (its first button, its answer function, and the call
// of sysui_ask_finger() for it)
static int s_asks, s_fingers;
static bool s_ask_on, s_ask_finger;
static char s_ask_yes[16], s_ask_line[64];
static void (*s_ask_done)(int);

void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int))
{
    (void)title;
    (void)no;
    (void)secs;
    snprintf(s_ask_yes, sizeof s_ask_yes, "%s", yes ? yes : "");
    snprintf(s_ask_line, sizeof s_ask_line, "%s", line ? line : "");
    s_ask_done = done;
    s_ask_on = true;
    s_ask_finger = false;
    s_asks++;
}

// the answer of the question (k: 1 = the first button, 0 = the second, -1 = closed)
static void ask_end(int k)
{
    if (!s_ask_on) {
        return;
    }
    s_ask_on = false;
    if (s_ask_done) {
        s_ask_done(k);
    }
}

void sysui_ask_close(void)
{
    ask_end(-1);
}

void sysui_ask_finger(void)
{
    s_fingers++;
    s_ask_finger = s_ask_on;  // (for the question that is on)
}

bool sysui_active(void)
{
    return s_ask_on;
}

bool web_auth_on(void)
{
    return false;
}

void wifi_send_page_head(struct httpd_req *req)
{
    httpd_resp_sendstr_chunk(req, "<head>");
}

// ---------------------------------------------------------------- the fake web server
static httpd_uri_t s_get, s_post;

esp_err_t httpd_register_uri_handler(void *h, const httpd_uri_t *u)
{
    (void)h;
    if (u->method == HTTP_GET) {
        s_get = *u;
    } else {
        s_post = *u;
    }
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    snprintf(r->type, sizeof r->type, "%s", type);
    return ESP_OK;
}

// (as ESP-IDF: an empty chunk is the end of the answer; a chunk after the end is a fault of the code)
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, httpd_len_t len)
{
    if (buf && len < 0) {
        len = (httpd_len_t)strlen(buf);
    }
    if (r->ended) {
        r->after++;
        return ESP_OK;
    }
    if (!buf || len <= 0) {
        r->ended = 1;
        return ESP_OK;
    }
    if (r->n + len >= (httpd_len_t)sizeof r->out) {
        return ESP_FAIL;
    }
    memcpy(r->out + r->n, buf, (size_t)len);
    r->n += (int)len;
    r->out[r->n] = 0;
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *str)
{
    return httpd_resp_send_chunk(r, str, str ? -1 : 0);
}

esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len)
{
    (void)r;
    if (len) {
        buf[0] = 0;
    }
    return ESP_FAIL;
}

esp_err_t httpd_query_key_value(const char *q, const char *key, char *val, size_t len)
{
    (void)q;
    (void)key;
    (void)val;
    (void)len;
    return ESP_FAIL;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t len)
{
    (void)r;
    (void)buf;
    (void)len;
    return 0;
}

// ---------------------------------------------------------------- the steps of the test
static tfn_pub_t key(const char *name, int flags, uint8_t seed)
{
    tfn_pub_t p;
    memset(&p, 0, sizeof p);
    p.pk[0] = 0x04;
    for (int i = 1; i < TFN_PUB_PK; i++) {
        p.pk[i] = (uint8_t)(seed + i);
    }
    snprintf(p.name, sizeof p.name, "%s", name);
    p.flags = (uint8_t)flags;
    return p;
}

static void id_of(const tfn_pub_t *p, char t[20])
{
    uint8_t id[TFN_PUB_ID];
    tfn_pub_key_id_of(p, id);
    tfn_pub_id_text(id, t);
}

// a key goes into the list: the request (as the web or a key file of the card), then "pub allow <key ID>"
static void add_key(const tfn_pub_t *p)
{
    char msg[160];
    uint8_t id[TFN_PUB_ID];
    expect(tfn_pub_request(p, msg, sizeof msg), msg);
    tfn_pub_key_id_of(p, id);
    expect(tfn_pub_allow_id(id, msg, sizeof msg), msg);
}

static void test_page_answer(int keys);

// GET /api/pub, and the check of its answer: the text, one end, no chunk after the end
static void check_get(const char *what, const char *want)
{
    static httpd_req_t req;
    memset(&req, 0, sizeof req);
    expect(s_get.handler && s_get.handler(&req) == ESP_OK, "the GET handler of /api/pub answers");
    if (req.ended != 1 || req.after || strcmp(req.out, want) || strcmp(req.type, "application/json")) {
        s_fails++;
        printf("FAIL %s: ended %d, %d chunks after the end\n  got  %s\n  want %s\n", what, req.ended, req.after,
               req.out, want);
    }
}

// The prompt of the key that waits (tfn_pub_tick): a consent question (sysui_ask_finger), and its answer is only for
// the wait that it showed. The question can stay up to 1 s after its wait (its seconds round up): a new wait that comes
// then does not get the ALLOW of the old question, also a new wait of the SAME key with other flags and another name.
// keys = the count of the added keys now.
static void test_prompt(const tfn_pub_t *waits, int keys)
{
    char msg[160], idt[20];
    tfn_pub_t p;
    int secs = 0;
    const tfn_pub_t four = key("FOUR", TFN_PUB_TEST, 0x40);
    id_of(waits, idt);
    tfn_pub_tick();
    expect(s_asks == 1 && s_ask_on && !strcmp(s_ask_yes, "ALLOW") && strstr(s_ask_line, idt),
           "the prompt shows the key ID and ALLOW");
    expect(s_fingers == 1 && s_ask_finger, "the prompt is a consent question (sysui_ask_finger)");
    s_now_us += (int64_t)TFN_PUB_WAIT_MS * 1000 + 10000;  // (the wait of the key ends; the question still shows)
    tfn_pub_tick();
    expect(tfn_pub_request(&four, msg, sizeof msg), msg);
    ask_end(1);  // (ALLOW of the old question)
    expect(tfn_pub_count() == keys && tfn_pub_waiting(&p, &secs, NULL) && !strcmp(p.name, "FOUR"),
           "ALLOW of the old question does not add the new key: it still waits");
    tfn_pub_tick();
    expect(s_asks == 2 && s_ask_on && s_fingers == 2 && s_ask_finger, "the prompt of the new key: a consent question");
    ask_end(1);
    expect(tfn_pub_count() == keys + 1 && !tfn_pub_waiting(&p, &secs, NULL),
           "ALLOW of the prompt of the new key adds it");
    // the same public key with other flags and another name: the web sends it while the old question still shows
    const tfn_pub_t dev = key("FIVE", TFN_PUB_DEV, 0x50),
                    store_test = key("FRIEND", TFN_PUB_STORE | TFN_PUB_TEST, 0x50);
    expect(tfn_pub_request(&dev, msg, sizeof msg), msg);
    tfn_pub_tick();
    expect(s_asks == 3 && s_ask_on && strstr(s_ask_line, " DEV"), "the prompt of the key FIVE shows the flags DEV");
    s_now_us += (int64_t)TFN_PUB_WAIT_MS * 1000 + 10000;  // (its wait ends; the question still shows)
    tfn_pub_tick();
    expect(tfn_pub_request(&store_test, msg, sizeof msg), msg);
    ask_end(1);  // (ALLOW of the question that showed DEV)
    expect(tfn_pub_count() == keys + 1 && tfn_pub_waiting(&p, &secs, NULL) && !strcmp(p.name, "FRIEND"),
           "ALLOW of the old question of the same key does not add the request with other flags: it still waits");
    tfn_pub_tick();
    expect(s_asks == 4 && s_ask_on && strstr(s_ask_line, "STORE+TEST") && s_ask_finger,
           "the prompt of the new request shows its own flags");
    ask_end(0);
    expect(!tfn_pub_waiting(&p, &secs, NULL) && tfn_pub_count() == keys + 1, "NO ends the wait of the new request");
    test_page_answer(keys + 1);
}

// The answer of the page (tfn_pub_page.c): only for the number of the wait that the row showed. keys = the count of the
// added keys now.
static void test_page_answer(int keys)
{
    char msg[160];
    tfn_pub_t p;
    int secs = 0;
    uint32_t seq = 0, seq2 = 0;
    const tfn_pub_t five = key("FIVE", TFN_PUB_TEST, 0x50);
    expect(tfn_pub_request(&five, msg, sizeof msg), msg);
    expect(tfn_pub_waiting(&p, &secs, &seq) && seq != 0, "a wait has a number");
    expect(!tfn_pub_answer(true, seq - 1, msg, sizeof msg) && tfn_pub_waiting(&p, &secs, NULL),
           "an answer with the number of an old wait: no answer, the key still waits");
    expect(!tfn_pub_answer(false, seq, msg, sizeof msg) && !tfn_pub_waiting(&p, &secs, NULL) && tfn_pub_count() == keys,
           "NO with the number of the wait: the wait ends, no key is added");
    expect(tfn_pub_request(&five, msg, sizeof msg), msg);
    expect(tfn_pub_waiting(&p, &secs, &seq2) && seq2 != seq, "a new wait of the same key: a new number");
    expect(!tfn_pub_answer(true, seq, msg, sizeof msg) && tfn_pub_waiting(&p, &secs, NULL),
           "the number of the old wait of the same key: no answer");
    expect(tfn_pub_answer(true, seq2, msg, sizeof msg) && tfn_pub_count() == keys + 1,
           "ALLOW with the number of the wait adds the key");
}

int main(void)
{
    s_builtin[0][0] = 0x04;
    for (int i = 1; i < TFN_PUB_PK; i++) {
        s_builtin[0][i] = (uint8_t)(0xB0 ^ i);
    }
    tfn_pub_init();
    tfn_pub_web_register(NULL);
    expect(s_get.handler && s_post.handler, "the GET and the POST handlers of /api/pub");
    uint8_t bid[TFN_PUB_ID];
    char b0[20], i1[20], i2[20], i3[20], want[1024], msg[160];
    expect(tfn_pub_builtin_count() == 1 && tfn_pub_builtin_id(0, bid), "one built-in key");
    tfn_pub_id_text(bid, b0);

    snprintf(want, sizeof want, "{\"builtin\":[{\"id\":\"%s\"}],\"keys\":[],\"max\":%d,\"wait\":null}", b0,
             TFN_PUB_MAX);
    check_get("no added key, no key waits", want);

    // two added keys (STORE, DEV+TEST) and a key that waits for the tap (TEST)
    const tfn_pub_t one = key("ONE", TFN_PUB_STORE, 0x10), two = key("TWO", TFN_PUB_DEV | TFN_PUB_TEST, 0x20);
    const tfn_pub_t three = key("THREE", TFN_PUB_TEST, 0x30);
    add_key(&one);
    id_of(&one, i1);
    snprintf(want, sizeof want,
             "{\"builtin\":[{\"id\":\"%s\"}],\"keys\":[{\"id\":\"%s\",\"name\":\"ONE\",\"flags\":\"STORE\"}],"
             "\"max\":%d,\"wait\":null}",
             b0, i1, TFN_PUB_MAX);
    check_get("one added key", want);
    add_key(&two);
    expect(tfn_pub_request(&three, msg, sizeof msg), msg);
    id_of(&two, i2);
    id_of(&three, i3);
    snprintf(want, sizeof want,
             "{\"builtin\":[{\"id\":\"%s\"}],\"keys\":[{\"id\":\"%s\",\"name\":\"ONE\",\"flags\":\"STORE\"},"
             "{\"id\":\"%s\",\"name\":\"TWO\",\"flags\":\"DEV+TEST\"}],\"max\":%d,"
             "\"wait\":{\"id\":\"%s\",\"name\":\"THREE\",\"flags\":\"TEST\",\"secs\":%d}}",
             b0, i1, i2, TFN_PUB_MAX, i3, TFN_PUB_WAIT_MS / 1000);
    check_get("two added keys and a key that waits", want);
    expect(tfn_pub_count() == 2 && s_nvs_n > 0 && s_rescans == 2, "the list: 2 keys, saved, a rescan for each");
    test_prompt(&three, 2);
    printf("PUB WEB TESTS %s\n", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
