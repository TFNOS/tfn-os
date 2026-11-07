// TFN OS: the trusted publisher list on the board (tfn_pub.h): the list in NVS, the PSA hash and ECDSA for the core
// (tfn_pub_core.c), the prompt on the screen, GET / POST /api/pub and the console "pub".
//
// A key gets into the list ONLY with a tap of a finger on the screen (the rule of a consent, input.h: a tap of the
// console or of the web on ALLOW gives no answer) for the wait that the screen showed (the number of the wait), or with
// "pub allow <key ID>" on the USB console (the cable counts as the tap: a person with the cable can write any
// firmware): a web request or a key file of the card only starts the wait (tfn_pub_wait_t) and the prompt. The web has
// no way to the ALLOW. A remove makes the board safer: the screen, the console (USB), or the web with the web password.
#include "tfn_app_priv.h"
#include "esp_http_server.h"
#include "sysui.h"
#include "web_auth.h"
#include "web_util.h"

static const char *TAG = "tfnpub";

#define PUB_NS  "tfnpub"
#define PUB_KEY "list"
#define PUB_DIR "/sdcard/pub"  // the key files of the card (SETUP > TRUSTED PUBLISHERS > ADD)

// (all in PSRAM: the "psram" list of main/apps.json)
static tfn_pub_list_t s_list;
static tfn_pub_wait_t s_wait;
static uint8_t s_blob[TFN_PUB_BLOB];
static char s_body[TFN_PUB_SIGS_FILE + 1];
static SemaphoreHandle_t s_lock;
static tfn_pub_ops_t s_ops;
static bool s_asking;       // the prompt is on the screen (the app task)
static uint32_t s_ask_seq;  // the number of the wait of the prompt (its answer is only for this wait)

static void lk(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}
static void ul(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}
static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void ops_sha256(const void *d, size_t n, uint8_t out[32])
{
    size_t ol = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, d, n, out, 32, &ol) != PSA_SUCCESS) {
        memset(out, 0, 32);
    }
}
static bool ops_verify(const uint8_t pk[TFN_PUB_PK], const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    return tfa_sig_ok_pk(pk, msg, n, sig, 64);
}

// the list after a change: into NVS (s_lock held)
static bool save_locked(void)
{
    size_t n = tfn_pub_pack(&s_list, s_blob, sizeof s_blob);
    nvs_handle_t h;
    if (!n || nvs_open(PUB_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_blob(h, PUB_KEY, s_blob, n) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void tfn_pub_init(void)
{
    if (s_lock) {
        return;
    }
    s_ops.sha256 = ops_sha256;
    s_ops.verify = ops_verify;
    s_ops.builtin_ok = tfa_sig_ok;
    for (int i = 0; i < TFN_PUB_BUILTIN; i++) {
        s_ops.builtin[i] = tfa_builtin_key(i);
    }
    s_lock = xSemaphoreCreateMutex();
    // (a board with no added key has no namespace: the read-only open fails, and nothing is written)
    nvs_handle_t h;
    if (nvs_open(PUB_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof s_blob;
        if (nvs_get_blob(h, PUB_KEY, s_blob, &n) == ESP_OK && !tfn_pub_unpack(&s_list, s_blob, n)) {
            ESP_LOGE(TAG, "the publisher list in NVS is not right: the board uses the built-in keys only");
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "%d built-in store keys, %d added keys", tfn_pub_builtin_count(), s_list.n);
}

int tfn_pub_app_check(const uint8_t *m, size_t ml, const uint8_t *sig, size_t sl, const char *sigs, size_t gl,
                      char who[TFN_PUB_NAME])
{
    if (!s_lock) {  // (no list yet: the built-in keys only)
        who[0] = 0;
        return sig && tfa_sig_ok(m, ml, sig, sl) ? TFN_PUB_OK_BUILTIN : TFN_PUB_NONE;
    }
    lk();
    int v = tfn_pub_check(&s_ops, &s_list, m, ml, sig, sl, sigs, gl, who);
    ul();
    return v;
}

int tfn_pub_count(void)
{
    return s_list.n;
}

bool tfn_pub_get(int i, tfn_pub_t *out)
{
    lk();
    bool ok = i >= 0 && i < s_list.n;
    if (ok) {
        *out = s_list.e[i];
    }
    ul();
    return ok;
}

int tfn_pub_builtin_count(void)
{
    int n = 0;
    for (int i = 0; i < TFN_PUB_BUILTIN; i++) {
        n += tfn_pub_key_set(tfa_builtin_key(i));
    }
    return n;
}

// the built-in key i of the ones that are set (0 .. tfn_pub_builtin_count() - 1)
bool tfn_pub_builtin_id(int i, uint8_t id[TFN_PUB_ID])
{
    for (int k = 0; k < TFN_PUB_BUILTIN; k++) {
        const uint8_t *pk = tfa_builtin_key(k);
        if (tfn_pub_key_set(pk) && i-- == 0) {
            uint8_t h[32];
            ops_sha256(pk, TFN_PUB_PK, h);
            memcpy(id, h, TFN_PUB_ID);
            return true;
        }
    }
    return false;
}

bool tfn_pub_waiting(tfn_pub_t *out, int *secs, uint32_t *seq)
{
    int64_t now = now_ms();
    lk();
    bool on = tfn_pub_wait_live(&s_wait, now);
    if (on) {
        *out = s_wait.p;
        *secs = (int)((s_wait.until_ms - now + 999) / 1000);
        if (seq) {
            *seq = s_wait.seq;
        }
    }
    ul();
    return on;
}

bool tfn_pub_request(const tfn_pub_t *p, char *msg, int len)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (device_guard_enabled()) {
        snprintf(msg, len, "a protected board takes no added publisher key");
        return false;
    }
#endif
    if (!s_lock) {
        snprintf(msg, len, "the publisher list is not ready");
        return false;
    }
    lk();
    bool ok = tfn_pub_wait_start(&s_wait, &s_ops, &s_list, p, now_ms(), msg, len);
    ul();
    ESP_LOGI(TAG, "a request for the key %s: %s", p->name, msg);
    return ok;
}

bool tfn_pub_request_card(char *msg, int len)
{
    if (!board_sd_mounted()) {
        snprintf(msg, len, "no SD card");
        return false;
    }
    DIR *d = opendir(PUB_DIR);
    if (!d) {
        snprintf(msg, len, "no folder %s on the card", PUB_DIR);
        return false;
    }
    struct dirent *de;
    bool found = false, ok = false;
    snprintf(msg, len, "no key file (TFNPUB1) in %s", PUB_DIR);
    while (!ok && (de = readdir(d))) {
        char path[300];
        uint8_t *t = NULL;
        size_t n = 0;
        tfn_pub_t p;
        snprintf(path, sizeof path, "%s/%s", PUB_DIR, de->d_name);
        if (de->d_name[0] == '.' || !tfa_read_all(path, &t, &n, TFN_PUB_SIGS_FILE)) {
            continue;
        }
        bool good = tfn_pub_file_parse((const char *)t, n, &p);
        free(t);
        if (!good) {
            continue;
        }
        char why[96];
        lk();
        bool can = tfn_pub_add_ok(&s_ops, &s_list, &p, why, sizeof why);
        ul();
        if (can) {
            ok = tfn_pub_request(&p, msg, len);
        } else if (!found) {
            snprintf(msg, len, "%s: %s", de->d_name, why);
        }
        found = true;
    }
    closedir(d);
    return ok;
}

bool tfn_pub_remove_id(const uint8_t id[TFN_PUB_ID], char *msg, int len)
{
    char t[20];
    tfn_pub_id_text(id, t);
    lk();
    bool ok = tfn_pub_remove(&s_ops, &s_list, id);
    bool saved = ok && save_locked();
    ul();
    if (!ok) {
        snprintf(msg, len, "the key %s is not in the list (a built-in key cannot go)", t);
        return false;
    }
    snprintf(msg, len, "the key %s is out of the list%s", t, saved ? "" : " (NOT SAVED: the NVS is full)");
    ESP_LOGI(TAG, "%s", msg);
    tfn_rescan();  // (the apps of this key: their state again)
    return true;
}

void tfn_pub_key_id_of(const tfn_pub_t *p, uint8_t id[TFN_PUB_ID])
{
    tfn_pub_key_id(&s_ops, p->pk, id);
}

// ---------------------------------------------------------------- the prompt (the app task)
static volatile bool s_con_done;  // the USB console allowed the key: the prompt on the screen must close

// after an answer (s_lock not held): the message on the screen and the apps of the key
static void answered(bool ok, bool saved)
{
    if (ok) {
        sysui_message(saved ? "THE KEY IS TRUSTED NOW" : "THE KEY IS TRUSTED UNTIL THE NEXT START (THE NVS IS FULL)",
                      3);
        tfn_rescan();
    }
}

// (s_lock held) the wait now has the number seq (the same request: the same key, name and flags)
static bool wait_is_locked(uint32_t seq)
{
    return tfn_pub_wait_live(&s_wait, now_ms()) && s_wait.seq == seq;
}

bool tfn_pub_answer(bool allow, uint32_t seq, char *msg, int len)
{
    lk();
    bool ok = false;
    if (wait_is_locked(seq)) {
        ok = tfn_pub_wait_answer(&s_wait, &s_ops, &s_list, allow, now_ms(), msg, len);
    } else {
        snprintf(msg, len, "the wait of the question ended: no answer");
    }
    bool saved = ok && save_locked();
    ul();
    ESP_LOGI(TAG, "the answer of the board owner: %s%s", msg, ok && !saved ? " (NOT SAVED: the NVS is full)" : "");
    answered(ok, saved);
    return ok;
}

bool tfn_pub_allow_id(const uint8_t id[TFN_PUB_ID], char *msg, int len)
{
    if (!s_lock) {
        snprintf(msg, len, "the publisher list is not ready");
        return false;
    }
    lk();
    bool ok = tfn_pub_wait_allow_id(&s_wait, &s_ops, &s_list, id, now_ms(), msg, len);
    bool saved = ok && save_locked();
    ul();
    ESP_LOGI(TAG, "ALLOW of the USB console: %s%s", msg, ok && !saved ? " (NOT SAVED: the NVS is full)" : "");
    if (ok) {
        s_con_done = true;
    }
    answered(ok, saved);
    return ok;
}

static void ask_done(int k)
{
    char msg[96];
    tfn_pub_t p;
    int secs = 0;
    s_asking = false;
    // (no key waits: the console answered, or the time ended. The answer is only for the wait of the prompt: the prompt
    // can stay up to 1 s after its wait, and a new wait can start then, also for the same key with other flags.)
    if (tfn_pub_waiting(&p, &secs, NULL)) {
        tfn_pub_answer(k == 1, s_ask_seq, msg, sizeof msg);
    }
}

void tfn_pub_tick(void)
{
    tfn_pub_t p;
    int secs = 0;
    uint32_t seq = 0;
    if (!s_lock || (!s_wait.on && !s_asking)) {
        s_con_done = false;
        return;  // (nothing waits: no lock in each frame)
    }
    if (!tfn_pub_waiting(&p, &secs, &seq)) {
        if (s_asking && s_con_done) {
            sysui_ask_close();  // (the console allowed the key: the prompt goes away at once; ask_done runs)
        }
        s_con_done = false;
        s_asking = false;  // (the time ended: sysui closes its question by its own time)
        return;
    }
    if (s_asking || sysui_active()) {
        return;  // (another question or a message is on the screen: the prompt comes after it)
    }
    char t[48], l[48], id[20], fl[20];
    uint8_t k[TFN_PUB_ID];
    tfn_pub_key_id(&s_ops, p.pk, k);
    tfn_pub_id_text(k, id);
    tfn_pub_flags_text(p.flags, fl, sizeof fl);
    snprintf(t, sizeof t, "TRUST THE KEY %s?", p.name);
    snprintf(l, sizeof l, "%s %s", id, fl);
    s_ask_seq = seq;  // (the wait of the data that the prompt shows: the same read)
    s_asking = true;
    sysui_ask(t, l, "ALLOW", "NO", secs > 0 ? secs : 1, ask_done);
    sysui_ask_finger();  // (a tap of the console or of the web on ALLOW: the key keeps its wait)
}

// ---------------------------------------------------------------- /api/pub
// GET: {"builtin":[{"id"}],"keys":[{"id","name","flags"}],"max","wait":{"id","name","flags","secs"}|null}
// POST ?op=add&name=<name>&flags=store|dev|test, the body = the key (130 hex digits) or a key file (TFNPUB1): the key
//      waits for the tap of the board owner on the screen (TFN_PUB_WAIT_MS)
// POST ?op=rm&id=<key ID>: only with a web password set (the gate of web_auth.c checks it)
// (web_chunk: a text that can be empty, as the tail of a key of the list, must not end the answer)
static void send_id(httpd_req_t *req, const uint8_t *pk)
{
    uint8_t k[TFN_PUB_ID];
    char t[64];
    tfn_pub_key_id(&s_ops, pk, k);
    tfn_pub_id_text(k, t);
    httpd_resp_sendstr_chunk(req, "\"id\":\"");
    web_chunk(req, t);
    httpd_resp_sendstr_chunk(req, "\"");
}
static void send_key(httpd_req_t *req, const tfn_pub_t *p, const char *tail)
{
    char fl[20];
    tfn_pub_flags_text(p->flags, fl, sizeof fl);
    httpd_resp_sendstr_chunk(req, "{");
    send_id(req, p->pk);
    httpd_resp_sendstr_chunk(req, ",\"name\":");
    web_json_str(req, p->name);
    httpd_resp_sendstr_chunk(req, ",\"flags\":\"");
    web_chunk(req, fl);
    httpd_resp_sendstr_chunk(req, "\"");
    web_chunk(req, tail);
    httpd_resp_sendstr_chunk(req, "}");
}

static esp_err_t pub_get(httpd_req_t *req)
{
    char b[48];
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"builtin\":[");
    for (int k = 0, c = 0; k < TFN_PUB_BUILTIN; k++) {
        if (tfn_pub_key_set(tfa_builtin_key(k))) {
            httpd_resp_sendstr_chunk(req, c++ ? ",{" : "{");
            send_id(req, tfa_builtin_key(k));
            httpd_resp_sendstr_chunk(req, "}");
        }
    }
    httpd_resp_sendstr_chunk(req, "],\"keys\":[");
    tfn_pub_t p;
    for (int i = 0; tfn_pub_get(i, &p); i++) {
        if (i) {
            httpd_resp_sendstr_chunk(req, ",");
        }
        send_key(req, &p, "");
    }
    int secs = 0;
    snprintf(b, sizeof b, "],\"max\":%d,\"wait\":", TFN_PUB_MAX);
    httpd_resp_sendstr_chunk(req, b);
    if (tfn_pub_waiting(&p, &secs, NULL)) {
        snprintf(b, sizeof b, ",\"secs\":%d", secs);
        send_key(req, &p, b);
        httpd_resp_sendstr_chunk(req, "}");
    } else {
        httpd_resp_sendstr_chunk(req, "null}");
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

// the key of op=add: a key file in the body, or the hex key in the body with name= and flags= in the query
static bool add_key(httpd_req_t *req, const char *q, tfn_pub_t *p, char *msg, int len)
{
    int n = 0;
    if (req->content_len > TFN_PUB_SIGS_FILE) {
        snprintf(msg, len, "the body is %u bytes: the key is 130 hex digits", (unsigned)req->content_len);
        return false;
    }
    while (n < (int)req->content_len) {
        int r = httpd_req_recv(req, s_body + n, req->content_len - n);
        if (r <= 0) {
            snprintf(msg, len, "the body did not come");
            return false;
        }
        n += r;
    }
    s_body[n] = 0;
    if (tfn_pub_file_parse(s_body, (size_t)n, p)) {
        return true;
    }
    char name[48] = "", fl[32] = "dev";
    httpd_query_key_value(q, "name", name, sizeof name);
    httpd_query_key_value(q, "flags", fl, sizeof fl);
    web_url_decode(name);
    web_url_decode(fl);
    int f = tfn_pub_flags_parse(fl);
    memset(p, 0, sizeof *p);
    if (!tfn_pub_hex_key(s_body, (size_t)n, p->pk) || !tfn_pub_name_set(p->name, name) || f <= 0) {
        snprintf(msg, len,
                 "op=add: the body = the key (130 hex digits, 04...), name= (1-15 letters), "
                 "flags= store, dev, test");
        return false;
    }
    p->flags = (uint8_t)f;
    return true;
}

static esp_err_t pub_post(httpd_req_t *req)
{
    char q[160] = "", op[8] = "", id[32] = "", msg[160] = "";
    bool ok = false;
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "op", op, sizeof op);
    httpd_query_key_value(q, "id", id, sizeof id);
    if (!strcmp(op, "add")) {
        tfn_pub_t p;
        ok = add_key(req, q, &p, msg, sizeof msg) && tfn_pub_request(&p, msg, sizeof msg);
    } else if (!strcmp(op, "rm")) {
        uint8_t k[TFN_PUB_ID];
        web_url_decode(id);
        if (!web_auth_on()) {
            snprintf(msg, sizeof msg,
                     "a remove by the web needs a web password (SETUP > WEB PASSWORD); or remove "
                     "the key on the screen");
        } else if (!tfn_pub_id_parse(id, k)) {
            snprintf(msg, sizeof msg, "op=rm needs id= (the key ID, 16 hex digits)");
        } else {
            ok = tfn_pub_remove_id(k, msg, sizeof msg);
        }
    } else {
        snprintf(msg, sizeof msg, "op = add or rm");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, ok ? "{\"ok\":true,\"msg\":" : "{\"ok\":false,\"msg\":");
    web_json_str(req, msg);
    httpd_resp_sendstr_chunk(req, "}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

void tfn_pub_web_register(void *h)
{
    httpd_uri_t g = {.uri = "/api/pub", .method = HTTP_GET, .handler = pub_get};
    httpd_uri_t p = {.uri = "/api/pub", .method = HTTP_POST, .handler = pub_post};
    httpd_register_uri_handler(h, &g);
    httpd_register_uri_handler(h, &p);
}

// ---------------------------------------------------------------- the console
// "pub": the list | "pub rm <key ID>" | "pub card" (the prompt for a key file of the card) | "pub allow <key ID>" (the
// ALLOW of the key that waits; one line of the result: "PUB ALLOW OK <key ID> <name>" or "PUB ALLOW FAIL <why>")
static void con_allow(const char *first)
{
    char d[40] = "", msg[160], t[20];
    uint8_t k[TFN_PUB_ID];
    tfn_pub_t p;
    int secs = 0;
    // ("3F2A-91C0-5B7E-0D44", "3f2a91c05b7e0d44" or "3F2A 91C0 5B7E 0D44")
    for (const char *b = first; b && strlen(d) + strlen(b) + 1 < sizeof d; b = strtok(NULL, " \t\r\n")) {
        strcat(d, b);
    }
    if (!tfn_pub_id_parse(d, k)) {
        printf("PUB ALLOW FAIL pub allow needs the key ID (16 hex digits)\n");
        return;
    }
    bool had = tfn_pub_waiting(&p, &secs, NULL);
    if (!tfn_pub_allow_id(k, msg, sizeof msg)) {
        printf("PUB ALLOW FAIL %s\n", msg);
        return;
    }
    tfn_pub_id_text(k, t);
    printf("PUB ALLOW OK %s %s\n", t, had ? p.name : "");
}

void con_pub(char *cmd)
{
    (void)cmd;
    char *a = strtok(NULL, " \t\r\n");
    char *b = strtok(NULL, " \t\r\n");
    char msg[160], t[20];
    uint8_t k[TFN_PUB_ID];
    if (a && !strcmp(a, "allow")) {
        con_allow(b);
        return;
    }
    if (a && !strcmp(a, "rm") && b && tfn_pub_id_parse(b, k)) {
        tfn_pub_remove_id(k, msg, sizeof msg);
        ESP_LOGI(TAG, "pub: %s", msg);
        return;
    }
    if (a && !strcmp(a, "card")) {
        tfn_pub_request_card(msg, sizeof msg);
        ESP_LOGI(TAG, "pub: %s", msg);
        return;
    }
    for (int i = 0; tfn_pub_builtin_id(i, k); i++) {
        tfn_pub_id_text(k, t);
        ESP_LOGI(TAG, "pub: built-in %s", t);
    }
    tfn_pub_t p;
    for (int i = 0; tfn_pub_get(i, &p); i++) {
        char fl[20];
        tfn_pub_key_id(&s_ops, p.pk, k);
        tfn_pub_id_text(k, t);
        tfn_pub_flags_text(p.flags, fl, sizeof fl);
        ESP_LOGI(TAG, "pub: %s %-15s %s", t, p.name, fl);
    }
    int secs = 0;
    if (tfn_pub_waiting(&p, &secs, NULL)) {
        ESP_LOGI(TAG, "pub: the key %s waits for the tap of the board owner (%d s)", p.name, secs);
    }
    ESP_LOGI(TAG, "pub: %d built-in, %d of %d added", tfn_pub_builtin_count(), tfn_pub_count(), TFN_PUB_MAX);
}
