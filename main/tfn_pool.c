// The pool of the board (tfn_pool.h): the board functions of the pool service (tfn_pool_flow.h): the NVS namespace
// "tfnpool", the roster file, the PSA crypto, the two locks, the web routes /api/pool and the console "pool".
// The static state of this file is in PSRAM (main/apps.json "psram"); the two locks are in PSRAM too.
#include "tfn_pool.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arc_mac.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "friends.h"
#include "mbedtls/platform_util.h"
#include "notify.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "sdkconfig.h"
#include "storage.h"
#include "tfn_id.h"
#include "tfn_pool_flow.h"
#include "tfn_util.h"
#include "web_auth.h"
#include "web_util.h"

static const char TAG[] = "pool";
#define NS        "tfnpool"
#define FILE_PATH STORAGE_ROOT "/tfn_pool.bin"
#define FILE_TMP  STORAGE_ROOT "/tfn_pool.tmp"
#define BODY_MAX  (2 * TPF_ROSTER_MAX + 2 * TPF_ENV_LEN + 512)  // the JSON body of op=key
#define HDR_MAX   128                                           // X-TFN-Auth: 16 + 1 + 20 + 1 + 64

static tpf_t s_p;
static SemaphoreHandle_t s_ctl;  // the control lock: the JOIN, the pool changes, the request header
static SemaphoreHandle_t s_hot;  // the lock of the packet path (short)
static bool s_init;

// ---------------------------------------------------------------- the crypto (PSA)

static void sha256(const void *d, size_t n, uint8_t out[32])
{
    size_t ol = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, d, n, out, 32, &ol) != PSA_SUCCESS || ol != 32) {
        memset(out, 0, 32);
    }
}

// HMAC-SHA256 over the parts p[0] || p[1] || ... (a fault gives a random value that matches nothing)
static void hmac(const uint8_t *key, size_t kn, const uint8_t *const *p, const size_t *pn, int np, uint8_t out[32])
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&at, PSA_BYTES_TO_BITS(kn));
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    psa_mac_operation_t op = PSA_MAC_OPERATION_INIT;
    size_t ol = 0;
    bool ok = psa_import_key(&at, key, kn, &k) == PSA_SUCCESS &&
              psa_mac_sign_setup(&op, k, PSA_ALG_HMAC(PSA_ALG_SHA_256)) == PSA_SUCCESS;
    for (int i = 0; ok && i < np; i++) {
        ok = psa_mac_update(&op, p[i], pn[i]) == PSA_SUCCESS;
    }
    ok = ok && psa_mac_sign_finish(&op, out, 32, &ol) == PSA_SUCCESS && ol == 32;
    if (!ok) {
        psa_mac_abort(&op);
        esp_fill_random(out, 32);
    }
    if (k) {
        psa_destroy_key(k);
    }
}

static bool verify(const uint8_t pk[65], const uint8_t *m, size_t n, const uint8_t sig[64])
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    if (psa_import_key(&at, pk, 65, &k) != PSA_SUCCESS) {
        return false;
    }
    const bool ok = psa_verify_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), m, n, sig, 64) == PSA_SUCCESS;
    psa_destroy_key(k);
    return ok;
}

static psa_key_id_t x_import(const uint8_t sk[32])
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&at, 255);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDH);
    psa_key_id_t k = 0;
    return psa_import_key(&at, sk, 32, &k) == PSA_SUCCESS ? k : 0;
}

static bool x25519_pub(const uint8_t sk[32], uint8_t pk[32])
{
    psa_key_id_t k = x_import(sk);
    size_t n = 0;
    const bool ok = k && psa_export_public_key(k, pk, 32, &n) == PSA_SUCCESS && n == 32;
    if (k) {
        psa_destroy_key(k);
    }
    return ok;
}

static bool x25519(const uint8_t sk[32], const uint8_t peer[32], uint8_t out[32])
{
    psa_key_id_t k = x_import(sk);
    size_t n = 0;
    const bool ok = k && psa_raw_key_agreement(PSA_ALG_ECDH, k, peer, 32, out, 32, &n) == PSA_SUCCESS && n == 32;
    if (k) {
        psa_destroy_key(k);
    }
    return ok;
}

#define AEAD_MAX 112  // the longest plain part: the envelope (68), the beacon (43)

static psa_key_id_t aead_key(const uint8_t key[32], bool seal)
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_CHACHA20);
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, seal ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&at, PSA_ALG_CHACHA20_POLY1305);
    psa_key_id_t k = 0;
    return psa_import_key(&at, key, 32, &k) == PSA_SUCCESS ? k : 0;
}

static bool aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *pt,
                      size_t n, uint8_t *ct, uint8_t tag[16])
{
    uint8_t b[AEAD_MAX + 16];
    psa_key_id_t k = n <= AEAD_MAX ? aead_key(key, true) : 0;
    size_t ol = 0;
    const bool ok =
            k &&
            psa_aead_encrypt(k, PSA_ALG_CHACHA20_POLY1305, nonce, 12, aad, an, pt, n, b, n + 16, &ol) == PSA_SUCCESS &&
            ol == n + 16;
    if (k) {
        psa_destroy_key(k);
    }
    if (ok) {
        memcpy(ct, b, n);
        memcpy(tag, b + n, 16);
    }
    mbedtls_platform_zeroize(b, sizeof b);
    return ok;
}

static bool aead_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *ct,
                      size_t n, const uint8_t tag[16], uint8_t *pt)
{
    uint8_t b[AEAD_MAX + 16];
    psa_key_id_t k = n <= AEAD_MAX ? aead_key(key, false) : 0;
    size_t ol = 0;
    if (!k) {
        return false;
    }
    memcpy(b, ct, n);
    memcpy(b + n, tag, 16);
    const bool ok =
            psa_aead_decrypt(k, PSA_ALG_CHACHA20_POLY1305, nonce, 12, aad, an, b, n + 16, pt, n, &ol) == PSA_SUCCESS &&
            ol == n;
    psa_destroy_key(k);
    return ok;
}

static const tfn_pool_ops_t OPS = {
        .sha256 = sha256,
        .hmac = hmac,
        .p256_verify = verify,
        .aead_seal = aead_seal,
        .aead_open = aead_open,
        .x25519 = x25519,
};

// ---------------------------------------------------------------- the NVS, the file, the board

static void rnd(uint8_t *p, size_t n)
{
    esp_fill_random(p, n);
}

static bool nv_get(const char *key, void *p, size_t n)
{
    nvs_handle_t h;
    size_t l = n;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_get_blob(h, key, p, &l) == ESP_OK && l == n;
    nvs_close(h);
    return ok;
}

static bool nv_set(const char *key, const void *p, size_t n)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(h, key, p, n) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) {
        ESP_LOGW(TAG, "the NVS key %s is not saved (a full NVS?)", key);
    }
    return ok;
}

static void nv_erase(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool nv_get_u32(const char *key, uint32_t *v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_get_u32(h, key, v) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool nv_set_u32(const char *key, uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_u32(h, key, v) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static size_t file_read(uint8_t *p, size_t max)
{
    FILE *f = fopen(FILE_PATH, "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(p, 1, max, f);
    const bool more = fgetc(f) != EOF;
    fclose(f);
    return more ? 0 : n;  // (a file over the limit is not a roster)
}

// The new file goes to a second name first, so a power cut never leaves half a roster.
static bool file_write(const uint8_t *p, size_t n)
{
    FILE *f = fopen(FILE_TMP, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(p, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(FILE_PATH);
        ok = rename(FILE_TMP, FILE_PATH) == 0;
    }
    return ok;
}

static void file_erase(void)
{
    remove(FILE_PATH);
    remove(FILE_TMP);
}

// true: the store token (NVS "tfn" "tok") is set and it is tok
static bool token_ok(const char *tok)
{
    char t[72] = "";
    size_t n = sizeof t;
    nvs_handle_t h;
    if (nvs_open("tfn", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    const bool got = nvs_get_str(h, "tok", t, &n) == ESP_OK && t[0];
    nvs_close(h);
    const size_t tl = strlen(t), al = strlen(tok);
    uint8_t d = (uint8_t)(tl != al);
    for (size_t i = 0; i < tl && i < al; i++) {
        d |= (uint8_t)(t[i] ^ tok[i]);
    }
    mbedtls_platform_zeroize(t, sizeof t);
    return got && !d;
}

// A JOIN: the board forgets the manager of before (its addresses and its token).
static void mgr_clear(void)
{
    nvs_handle_t h;
    if (nvs_open("tfn", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "lan");
        nvs_erase_key(h, "vpn");
        nvs_erase_key(h, "tok");
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "the JOIN cleared the manager of before (the addresses and the token)");
}

static void left(const char *pool)
{
    char t[64];
    snprintf(t, sizeof t, "THIS BOARD LEFT THE POOL %s", pool);
    ESP_LOGW(TAG, "%s", t);
    notify_post("SETUP", "POOL", t, NULL);
}

static void hot_lock(void)
{
    xSemaphoreTake(s_hot, portMAX_DELAY);
}

static void hot_unlock(void)
{
    xSemaphoreGive(s_hot);
}

static const tpf_io_t IO = {
        .ops = &OPS,
        .x25519_pub = x25519_pub,
        .random = rnd,
        .now_ms = tfn_now_ms,
        .sign = tfn_id_sign,
        .nvs_get = nv_get,
        .nvs_set = nv_set,
        .nvs_erase = nv_erase,
        .nvs_get_u32 = nv_get_u32,
        .nvs_set_u32 = nv_set_u32,
        .file_read = file_read,
        .file_write = file_write,
        .file_erase = file_erase,
        .token_ok = token_ok,
        .mgr_clear = mgr_clear,
        .left = left,
        .hot_lock = hot_lock,
        .hot_unlock = hot_unlock,
};

// The values of this board now (the device ID and the box key come from the task of tfn_id.c).
static void me_now(tpf_me_t *m)
{
    memset(m, 0, sizeof *m);
    const uint8_t *id = tfn_id_raw(), *pk = tfn_id_pk(), *bx = tfn_id_box(), *bxs = tfn_id_box_sig();
    arc_mac(m->mac);
    snprintf(m->name, sizeof m->name, "%s", friends_my_name());
    if (id && pk) {
        m->ok = true;
        memcpy(m->id, id, 8);
        memcpy(m->pk, pk, 65);
    }
    if (bx && bxs) {
        m->box = true;
        memcpy(m->bx, bx, 32);
        memcpy(m->bxs, bxs, 64);
    }
}

// The control lock, with the values of this board brought up to date.
static bool ctl_take(TickType_t wait)
{
    if (!s_init || xSemaphoreTake(s_ctl, wait) != pdTRUE) {
        return false;
    }
    tpf_me_t m;
    me_now(&m);
    tpf_me(&s_p, &m);
    return true;
}

static void ctl_give(void)
{
    xSemaphoreGive(s_ctl);
}

// ---------------------------------------------------------------- the start and the state

void tfn_pool_init(void)
{
    s_ctl = xSemaphoreCreateMutexWithCaps(MALLOC_CAP_SPIRAM);
    s_hot = xSemaphoreCreateMutexWithCaps(MALLOC_CAP_SPIRAM);
    if (!s_ctl || !s_hot) {
        ESP_LOGW(TAG, "no memory for the locks: no pool at this start");
        return;
    }
    tpf_me_t m;
    me_now(&m);
    tpf_init(&s_p, &IO, &m, tfn_pool_legacy());
    s_init = true;
    static const char *const ST[] = {"none", "join", "ok", "damaged"};
    ESP_LOGI(TAG, "the pool: %s%s%s (strict %d, legacy %d)", ST[s_p.state], s_p.state == TFN_POOL_OK ? " " : "",
             s_p.state == TFN_POOL_OK ? s_p.r.name : "", tfn_pool_strict(), tfn_pool_legacy());
}

bool tfn_pool_on(void)
{
    return s_init && __atomic_load_n(&s_p.state, __ATOMIC_ACQUIRE) == TFN_POOL_OK;
}

bool tfn_pool_strict(void)
{
#if defined(CONFIG_TFN_POOL_STRICT) && CONFIG_TFN_POOL_STRICT
    return true;
#else
    return false;
#endif
}

bool tfn_pool_legacy(void)
{
#if defined(CONFIG_TFN_POOL_LEGACY) && CONFIG_TFN_POOL_LEGACY
    return true;
#else
    return false;
#endif
}

tfn_pool_state_t tfn_pool_state(void)
{
    if (!s_init) {
        return TFN_POOL_NONE;
    }
    const tfn_pool_join_t j = s_p.j.step;
    if (j >= TFN_POOL_JOIN_WAIT && j <= TFN_POOL_JOIN_ALLOWED && !s_p.j.token) {
        return TFN_POOL_JOIN;
    }
    return s_p.state;
}

uint8_t tfn_pool_ptag(void)
{
    return tfn_pool_on() ? s_p.rx.key[0].ptag : 0;
}

const char *tfn_pool_name(void)
{
    return tfn_pool_on() ? s_p.r.name : "";
}

int tfn_pool_my_role(void)
{
    if (!s_init) {
        return -1;
    }
    hot_lock();
    const int r = s_p.state == TFN_POOL_OK && s_p.my_slot >= 0 ? s_p.r.m[s_p.my_slot].role : -1;
    hot_unlock();
    return r;
}

uint32_t tfn_pool_gen(void)
{
    return s_init ? __atomic_load_n(&s_p.gen, __ATOMIC_ACQUIRE) : 0;
}

bool tfn_pool_plain_beacon(void)
{
    if (tfn_pool_on()) {
        return (s_p.r.flags & TFN_POOL_F_LEGACY_BEACON) != 0;
    }
    return tfn_pool_legacy();
}

int tfn_pool_member(const uint8_t mac[6])
{
    if (!tfn_pool_on()) {
        return -1;
    }
    int slot = -1;
    hot_lock();
    for (int i = 0; s_p.state == TFN_POOL_OK && i < s_p.r.nm && slot < 0; i++) {
        if (!memcmp(s_p.r.m[i].mac, mac, 6)) {
            slot = i;
        }
    }
    hot_unlock();
    return slot;
}

int tfn_pool_role(int slot)
{
    tfn_pool_member_t m;
    return tfn_pool_member_get(slot, &m) ? m.role : -1;
}

int tfn_pool_members(void)
{
    return tfn_pool_on() ? s_p.r.nm : 0;
}

bool tfn_pool_member_get(int slot, tfn_pool_member_t *out)
{
    memset(out, 0, sizeof *out);
    if (!tfn_pool_on()) {
        return false;
    }
    hot_lock();
    const bool ok = s_p.state == TFN_POOL_OK && slot >= 0 && slot < s_p.r.nm;
    if (ok) {
        *out = s_p.r.m[slot];
    }
    hot_unlock();
    return ok;
}

// ---------------------------------------------------------------- the packets

int tfn_pool_frame_seal(uint8_t *frame, int len, int cap)
{
    return tfn_pool_on() ? tpf_frame_seal(&s_p, frame, len, cap) : -1;
}

int tfn_pool_frame_open(const uint8_t mac[6], const uint8_t *frame, int len)
{
    return s_init ? tpf_frame_open(&s_p, mac, frame, len) : TFN_POOL_DROP_OTHER;
}

int tfn_pool_udp_seal(uint8_t *pkt, int len, int cap)
{
    return tfn_pool_on() ? tpf_udp_seal(&s_p, pkt, len, cap) : -1;
}

int tfn_pool_udp_open(const uint8_t mac[6], const uint8_t *pkt, int len)
{
    return s_init ? tpf_udp_open(&s_p, mac, pkt, len) : TFN_POOL_DROP_OTHER;
}

void tfn_pool_counts(uint32_t out[TFN_POOL_CNT_N])
{
    memset(out, 0, TFN_POOL_CNT_N * sizeof out[0]);
    if (s_init) {
        hot_lock();
        memcpy(out, s_p.rx.count, sizeof s_p.rx.count);
        hot_unlock();
    }
}

bool tfn_pool_beacon_seal(const uint8_t *plain, int n, uint8_t *out, int *on)
{
    *on = 0;
    return tfn_pool_on() && tpf_beacon_seal(&s_p, plain, n, out, on);
}

bool tfn_pool_beacon_open(const uint8_t *pkt, int n, uint8_t *plain, int *pn)
{
    *pn = 0;
    return tfn_pool_on() && tpf_beacon_open(&s_p, pkt, n, plain, pn);
}

int tfn_pool_join_beacon(uint8_t *out, int cap)
{
    if (!s_init || s_p.j.step != TFN_POOL_JOIN_WAIT || !ctl_take(pdMS_TO_TICKS(10))) {
        return 0;  // (a JOIN step runs now: the next beacon)
    }
    const int n = tpf_join_beacon(&s_p, out, cap);
    ctl_give();
    return n;
}

// ---------------------------------------------------------------- the requests of the pool manager

bool tfn_pool_web_ok(const char *method, const char *uri, const char *hdr)
{
    if (!hdr || !hdr[0] || !ctl_take(portMAX_DELAY)) {
        return false;
    }
    const bool ok = tpf_web_ok(&s_p, method, uri, hdr);
    ctl_give();
    if (!ok) {
        ESP_LOGW(TAG, "a request header X-TFN-Auth that is not right: %s %s", method, uri);
    }
    return ok;
}

static bool hdr_of(httpd_req_t *req, char *hdr, size_t max)
{
    const size_t n = httpd_req_get_hdr_value_len(req, "X-TFN-Auth");
    return n && n < max && httpd_req_get_hdr_value_str(req, "X-TFN-Auth", hdr, max) == ESP_OK;
}

bool tfn_pool_req_mgr(void *r)
{
    httpd_req_t *req = r;
    char hdr[HDR_MAX];
    if (!req || !hdr_of(req, hdr, sizeof hdr) || !ctl_take(portMAX_DELAY)) {
        return false;
    }
    const bool ok = tpf_req_mgr(&s_p, http_method_str(req->method), req->uri, hdr);
    ctl_give();
    return ok;
}

bool tfn_pool_mgr_ok(void *req)
{
    const bool mgr = tfn_pool_req_mgr(req);
    if (!ctl_take(portMAX_DELAY)) {
        return tfn_pool_legacy();
    }
    const char *e = tpf_mgr_ok(&s_p, mgr);
    ctl_give();
    if (e) {
        ESP_LOGW(TAG, "op=mgr refused: not the pool manager");
    }
    return !e;
}

// ---------------------------------------------------------------- the screen

bool tfn_pool_join_start(void)
{
    if (!ctl_take(portMAX_DELAY)) {
        return false;
    }
    const char *e = tpf_join_start(&s_p);
    ctl_give();
    ESP_LOGI(TAG, "JOIN: %s", e ? e : "the board waits for a manager (10 min)");
    return !e;
}

void tfn_pool_join_stop(void)
{
    if (ctl_take(portMAX_DELAY)) {
        tpf_join_stop(&s_p);
        ctl_give();
    }
}

tfn_pool_join_t tfn_pool_join_step(void)
{
    tfn_pool_join_t s = TFN_POOL_JOIN_OFF;
    if (ctl_take(pdMS_TO_TICKS(20))) {
        s = tpf_join_step(&s_p);
        ctl_give();
    } else if (s_init) {
        s = s_p.j.step;  // (a step runs now)
    }
    return s;
}

int tfn_pool_join_left_s(void)
{
    if (!s_init || s_p.j.step < TFN_POOL_JOIN_WAIT || s_p.j.step > TFN_POOL_JOIN_ALLOWED) {
        return 0;
    }
    const int64_t end = s_p.j.step == TFN_POOL_JOIN_WAIT ? s_p.j.until : s_p.j.step_until;
    const int64_t left = end - tfn_now_ms();
    return left > 0 ? (int)((left + 999) / 1000) : 0;
}

const char *tfn_pool_join_why(void)
{
    return s_init && s_p.j.step == TFN_POOL_JOIN_FAIL ? tpf_text(s_p.j.why) : "";
}

bool tfn_pool_join_sas(char digits[TFN_POOL_SAS_LEN], char mgr[17])
{
    digits[0] = 0;
    mgr[0] = 0;
    if (tfn_pool_join_step() != TFN_POOL_JOIN_ASK || !ctl_take(pdMS_TO_TICKS(20))) {
        return false;
    }
    const bool ok = s_p.j.step == TFN_POOL_JOIN_ASK;
    if (ok) {
        memcpy(digits, s_p.j.sas, TFN_POOL_SAS_LEN);
        memcpy(mgr, s_p.j.mname, 17);
    }
    ctl_give();
    return ok;
}

bool tfn_pool_join_allow(const char *digits)
{
    if (!ctl_take(portMAX_DELAY)) {
        return false;
    }
    const char *e = tpf_allow(&s_p, digits);
    ctl_give();
    ESP_LOGI(TAG, "ALLOW (%s): %s", digits ? "with the digits" : "no digits", e ? e : "the board waits for op=set");
    return !e;
}

void tfn_pool_join_no(void)
{
    if (ctl_take(portMAX_DELAY)) {
        tpf_no(&s_p);
        ctl_give();
    }
}

bool tfn_pool_leave(bool by_screen)
{
    if (!ctl_take(portMAX_DELAY)) {
        return false;
    }
    char name[sizeof s_p.r.name];
    snprintf(name, sizeof name, "%s", s_p.r.name);
    const char *e = tpf_leave(&s_p, by_screen);
    ctl_give();
    ESP_LOGI(TAG, "LEAVE %s: %s", name, e ? e : "the pool data is erased");
    return !e;
}

// ---------------------------------------------------------------- the web: GET and POST /api/pool
// The contract with sdk/tools/pool_tool.py: the "notes" of tests/unit/vectors/pool_v1.json ("api", "console").

static void hexs(char *o, const uint8_t *d, int n)
{
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        o[2 * i] = H[d[i] >> 4];
        o[2 * i + 1] = H[d[i] & 15];
    }
    o[2 * n] = 0;
}

static void chunk(httpd_req_t *req, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void chunk(httpd_req_t *req, const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    web_chunk(req, b);  // (a format can give an empty text: it must not end the answer)
}

static const char *const STATE_NAME[] = {"none", "join", "ok", "damaged"};

// The members of the roster as "list" (the control lock is held)
static void json_members(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req, ",\"list\":[");
    for (int i = 0; s_p.state == TFN_POOL_OK && i < s_p.r.nm; i++) {
        const tfn_pool_member_t *m = &s_p.r.m[i];
        char id[17], mac[13];
        hexs(id, m->id, 8);
        hexs(mac, m->mac, 6);
        chunk(req, "%s{\"id\":\"%s\",\"mac\":\"%s\",\"role\":\"%s\",\"flags\":%u,\"me\":%s,\"name\":", i ? "," : "", id,
              mac, tpf_role_name(m->role), m->flags, i == s_p.my_slot ? "true" : "false");
        web_json_str(req, m->name);
        httpd_resp_sendstr_chunk(req, "}");
    }
    httpd_resp_sendstr_chunk(req, "]");
}

// GET /api/pool: only public values (design 2.8.3); no web password (web_auth.c)
static esp_err_t pool_get(httpd_req_t *req)
{
    char pool[33] = "", bx[65] = "", bxs[129] = "", bn[33] = "", mid[17] = "", id[17] = "", mac[13];
    if (!ctl_take(portMAX_DELAY)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no pool service");
    }
    const tpf_t *p = &s_p;
    const bool on = p->state == TFN_POOL_OK;
    hexs(bn, tpf_bn(&s_p), 16);
    hexs(mac, p->me.mac, 6);
    if (p->me.ok) {
        hexs(id, p->me.id, 8);
    }
    if (on) {
        uint8_t k[8];
        hexs(pool, p->r.pool, 16);
        tpf_mid(p, p->r.admin[0], k);
        hexs(mid, k, 8);
    }
    if (p->me.box) {
        hexs(bx, p->me.bx, 32);
        hexs(bxs, p->me.bxs, 64);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    chunk(req, "{\"state\":\"%s\",\"pool\":\"%s\",\"name\":", STATE_NAME[tfn_pool_state()], pool);
    web_json_str(req, on ? p->r.name : "");
    chunk(req,
          ",\"v\":%lu,\"epoch\":%lu,\"role\":\"%s\",\"strict\":%s,\"legacy\":%s,\"members\":%d,\"mgr\":\"%s\","
          "\"mgr_lan\":",
          (unsigned long)(on ? p->r.ver : 0), (unsigned long)(on ? p->rx.key[0].epoch : 0),
          on && p->my_slot >= 0 ? tpf_role_name(p->r.m[p->my_slot].role) : "", tfn_pool_strict() ? "true" : "false",
          tfn_pool_legacy() ? "true" : "false", on ? p->r.nm : 0, mid);
    web_json_str(req, on ? p->r.lan : "");
    httpd_resp_sendstr_chunk(req, ",\"mgr_vpn\":");
    web_json_str(req, on ? p->r.vpn : "");
    json_members(req);
    chunk(req, ",\"id\":\"%s\",\"mac\":\"%s\",\"bx\":\"%s\",\"bxs\":\"%s\",\"bn\":\"%s\"", id, mac, bx, bxs, bn);
    chunk(req, ",\"join\":\"%s\",\"step\":\"%s\",\"why\":\"%s\"", tpf_join_web(p->j.step), tpf_step_name(p->j.step),
          p->j.step == TFN_POOL_JOIN_FAIL && p->j.why ? p->j.why : "");
    ctl_give();
    uint32_t c[TFN_POOL_CNT_N];
    tfn_pool_counts(c);
    chunk(req, ",\"left\":%d,\"v4\":{\"ok\":%lu,\"other\":%lu,\"who\":%lu,\"bad\":%lu,\"old\":%lu}}",
          tfn_pool_join_left_s(), (unsigned long)c[0], (unsigned long)c[1], (unsigned long)c[2], (unsigned long)c[3],
          (unsigned long)c[4]);
    return httpd_resp_sendstr_chunk(req, NULL);
}

// A string value of a flat JSON object (no nesting; the escapes \" and \\ only). false: no such key, or too long.
static bool jstr(const char *b, const char *key, char *out, size_t max)
{
    const size_t kl = strlen(key);
    for (const char *s = strchr(b, '"'); s; s = strchr(s + 1, '"')) {
        if (strncmp(s + 1, key, kl) || s[1 + kl] != '"') {
            continue;
        }
        const char *v = s + 2 + kl;
        while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') {
            v++;
        }
        if (*v++ != ':') {
            continue;
        }
        while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') {
            v++;
        }
        if (*v++ != '"') {
            return false;
        }
        size_t n = 0;
        for (; *v && *v != '"'; v++) {
            if (*v == '\\' && (v[1] == '"' || v[1] == '\\')) {
                v++;
            }
            if (n + 1 >= max) {
                return false;
            }
            out[n++] = *v;
        }
        out[n] = 0;
        return *v == '"';
    }
    return false;
}

// A hex value of the JSON body: its byte count (at most max), -1 = no such key, -2 = not hex or too long.
static int jhex(const char *b, const char *key, uint8_t *out, int max, char *tmp, size_t tn)
{
    if (!jstr(b, key, tmp, tn)) {
        return strstr(b, key) ? -2 : -1;
    }
    const int n = (int)strlen(tmp);
    if (n & 1 || n / 2 > max) {
        return -2;
    }
    for (int i = 0; i < n / 2; i++) {
        unsigned x;
        if (!isxdigit((unsigned char)tmp[2 * i]) || !isxdigit((unsigned char)tmp[2 * i + 1]) ||
            sscanf(tmp + 2 * i, "%2x", &x) != 1) {
            return -2;
        }
        out[i] = (uint8_t)x;
    }
    return n / 2;
}

// An error: the HTTP status of the code and {"error": code, "msg": text}
static esp_err_t fault(httpd_req_t *req, const char *code)
{
    static const char *const ST[] = {"400 Bad Request", "401 Unauthorized", "403 Forbidden", "409 Conflict",
                                     "500 Internal Server Error"};
    const int h = tpf_http(code);
    httpd_resp_set_status(req, ST[h == 400 ? 0 : h == 401 ? 1 : h == 409 ? 3 : h == 500 ? 4 : 2]);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    chunk(req, "{\"error\":\"%s\",\"msg\":", code);
    web_json_str(req, tpf_text(code));
    return httpd_resp_sendstr_chunk(req, "}") == ESP_OK ? httpd_resp_sendstr_chunk(req, NULL) : ESP_FAIL;
}

static esp_err_t good(httpd_req_t *req, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static esp_err_t good(httpd_req_t *req, const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, b);
}

static esp_err_t hello_answer(httpd_req_t *req, const tpf_hello_t *h)
{
    char eb[65], nb[33], pk[131], id[17], bx[65], bxs[129], sig[129];
    hexs(eb, h->eb, 32);
    hexs(nb, h->nb, 16);
    hexs(pk, h->pk, 65);
    hexs(id, h->id, 8);
    hexs(bx, h->bx, 32);
    hexs(bxs, h->bxs, 64);
    hexs(sig, h->sig, 64);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    chunk(req, "{\"ok\":true,\"eb\":\"%s\",\"nb\":\"%s\",\"pk\":\"%s\",\"id\":\"%s\",", eb, nb, pk, id);
    chunk(req, "\"bx\":\"%s\",\"bxs\":\"%s\",\"sig\":\"%s\"}", bx, bxs, sig);
    return httpd_resp_sendstr_chunk(req, NULL);
}

// The work buffers of one POST (PSRAM): the body text, a hex value, the roster bytes
typedef struct {
    char body[BODY_MAX + 1];
    char tmp[2 * TPF_ROSTER_MAX + 2];
    uint8_t roster[TPF_ROSTER_MAX];
} post_mem_t;

// op=hello {"admin", "mid", "c"} (+ "auth" = the store token: the token JOIN; + "name")
static esp_err_t op_hello(httpd_req_t *req, post_mem_t *m)
{
    uint8_t apk[65], c[32];
    char name[48] = "", auth[72];
    if (jhex(m->body, "admin", apk, 65, m->tmp, sizeof m->tmp) != 65 ||
        jhex(m->body, "c", c, 32, m->tmp, sizeof m->tmp) != 32) {
        return fault(req, "form");
    }
    jstr(m->body, "name", name, sizeof name);
    const bool tok = jstr(m->body, "auth", auth, sizeof auth);
    tpf_hello_t h;
    if (!ctl_take(portMAX_DELAY)) {
        return ESP_FAIL;
    }
    const char *e = tpf_hello(&s_p, apk, c, name, tok ? auth : NULL, &h);
    ctl_give();
    mbedtls_platform_zeroize(auth, sizeof auth);
    ESP_LOGI(TAG, "op=hello%s: %s", tok ? " (the token)" : "", e ? e : "answered");
    return e ? fault(req, e) : hello_answer(req, &h);
}

// op=reveal {"em", "nm", "sig"}
static esp_err_t op_reveal(httpd_req_t *req, post_mem_t *m)
{
    uint8_t em[32], nm[16], sig[64];
    if (jhex(m->body, "em", em, 32, m->tmp, sizeof m->tmp) != 32 ||
        jhex(m->body, "nm", nm, 16, m->tmp, sizeof m->tmp) != 16 ||
        jhex(m->body, "sig", sig, 64, m->tmp, sizeof m->tmp) != 64) {
        return fault(req, "form");
    }
    if (!ctl_take(portMAX_DELAY)) {
        return ESP_FAIL;
    }
    const char *e = tpf_reveal(&s_p, em, nm, sig);
    const tfn_pool_join_t s = s_p.j.step;
    if (!e && s == TFN_POOL_JOIN_ASK) {
        // (the USB console shows the 6 digits: "pool allow <digits>" there is the tap of ALLOW)
        printf("POOL MANAGER %s WANTS TO MANAGE THIS BOARD: SAS %.3s %.3s - ALLOW / NO\n", s_p.j.mname, s_p.j.sas,
               s_p.j.sas + 3);
    }
    ctl_give();
    ESP_LOGI(TAG, "op=reveal: %s", e ? e : tpf_step_name(s));
    return e ? fault(req, e) : good(req, "{\"ok\":true,\"join\":\"%s\"}", tpf_join_web(s));
}

// op=set {"roster"} (+ "env" in a JOIN), op=key {"em", "env"} (+ "roster")
static esp_err_t op_change(httpd_req_t *req, post_mem_t *m, bool key)
{
    const bool mgr = tfn_pool_req_mgr(req);
    uint8_t env[TPF_ENV_LEN], em[32];
    const int rl = jhex(m->body, "roster", m->roster, TPF_ROSTER_MAX, m->tmp, sizeof m->tmp);
    const int en = jhex(m->body, "env", env, TPF_ENV_LEN, m->tmp, sizeof m->tmp);
    const int eml = key ? jhex(m->body, "em", em, 32, m->tmp, sizeof m->tmp) : 32;
    if (rl == -2 || en == -2 || eml != 32 || (!key && rl < 0) || (key && en < 0)) {
        return fault(req, "form");
    }
    if (!ctl_take(portMAX_DELAY)) {
        return ESP_FAIL;
    }
    const char *what = "", *e;
    if (key) {
        e = tpf_key(&s_p, em, env, (size_t)en, rl >= 0 ? m->roster : NULL, rl >= 0 ? (size_t)rl : 0, mgr, &what);
    } else {
        e = tpf_set(&s_p, m->roster, (size_t)rl, en >= 0 ? env : NULL, en >= 0 ? (size_t)en : 0, mgr, &what);
    }
    const unsigned long v = s_p.state == TFN_POOL_OK ? (unsigned long)s_p.r.ver : 0;
    const unsigned long ep = s_p.state == TFN_POOL_OK ? (unsigned long)s_p.rx.key[0].epoch : 0;
    ctl_give();
    mbedtls_platform_zeroize(env, sizeof env);
    ESP_LOGI(TAG, "op=%s%s: %s", key ? "key" : "set", mgr ? " (the pool manager)" : "", e ? e : what);
    if (e) {
        return fault(req, e);
    }
    return good(req, "{\"ok\":true,\"state\":\"%s\",\"v\":%lu,\"epoch\":%lu,\"roster\":\"%s\"}",
                STATE_NAME[tfn_pool_state()], v, ep, what);
}

// op=webpass {"pw"}: the web password of the board (only the pool manager)
static esp_err_t op_webpass(httpd_req_t *req, post_mem_t *m)
{
    char pw[WEB_PASS_MAX + 2], msg[64];
    if (!tfn_pool_req_mgr(req)) {
        return fault(req, "auth");
    }
    if (!jstr(m->body, "pw", pw, sizeof pw) || (pw[0] && (strlen(pw) < WEB_PASS_MIN || strlen(pw) > WEB_PASS_MAX))) {
        return fault(req, "form");
    }
    // (the answer first: a new password ends the open connections)
    esp_err_t r = good(req, "{\"ok\":true}");
    const bool ok = web_auth_set(pw, msg, sizeof msg);
    mbedtls_platform_zeroize(pw, sizeof pw);
    ESP_LOGI(TAG, "op=webpass: %s", ok ? "the web password is new" : msg);
    return r;
}

// op=leave: the pool data goes (only the pool manager)
static esp_err_t op_leave(httpd_req_t *req)
{
    if (!tfn_pool_req_mgr(req)) {
        return fault(req, "auth");
    }
    if (!ctl_take(portMAX_DELAY)) {
        return ESP_FAIL;
    }
    const char *e = tpf_leave(&s_p, false);
    ctl_give();
    ESP_LOGI(TAG, "op=leave: %s", e ? e : "the pool data is erased");
    return e ? fault(req, e) : good(req, "{\"ok\":true,\"state\":\"none\"}");
}

static esp_err_t pool_post(httpd_req_t *req)
{
    char q[64] = "", op[12] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "op", op, sizeof op);
    if (req->content_len > BODY_MAX) {
        return fault(req, "form");
    }
    post_mem_t *m = heap_caps_malloc(sizeof *m, MALLOC_CAP_SPIRAM);
    if (!m) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    size_t n = 0;
    while (n < req->content_len) {
        const int r = httpd_req_recv(req, m->body + n, req->content_len - n);
        if (r <= 0) {
            free(m);
            return ESP_FAIL;
        }
        n += (size_t)r;
    }
    m->body[n] = 0;
    esp_err_t e;
    if (!strcmp(op, "hello")) {
        e = op_hello(req, m);
    } else if (!strcmp(op, "reveal")) {
        e = op_reveal(req, m);
    } else if (!strcmp(op, "set") || !strcmp(op, "key")) {
        e = op_change(req, m, op[0] == 'k');
    } else if (!strcmp(op, "webpass")) {
        e = op_webpass(req, m);
    } else if (!strcmp(op, "leave")) {
        e = op_leave(req);
    } else {
        e = fault(req, "form");
    }
    mbedtls_platform_zeroize(m, sizeof *m);
    free(m);
    return e;
}

void tfn_pool_register(void *server)
{
    httpd_uri_t g = {.uri = "/api/pool", .method = HTTP_GET, .handler = pool_get};
    httpd_uri_t p = {.uri = "/api/pool", .method = HTTP_POST, .handler = pool_post};
    httpd_register_uri_handler(server, &g);
    httpd_register_uri_handler(server, &p);
}

// ---------------------------------------------------------------- the console "pool"

static void con_state(void)
{
    tfn_pool_member_t m;
    uint32_t c[TFN_POOL_CNT_N];
    char digits[TFN_POOL_SAS_LEN], mgr[17];
    printf("POOL STATE %s NAME %s ROLE %s MEMBERS %d STRICT %d LEGACY %d\n", STATE_NAME[tfn_pool_state()],
           tfn_pool_on() ? s_p.r.name : "-", tpf_role_name(tfn_pool_my_role()), tfn_pool_members(), tfn_pool_strict(),
           tfn_pool_legacy());
    for (int i = 0; tfn_pool_member_get(i, &m); i++) {
        printf("POOL MEMBER %02X%02X%02X%02X%02X%02X %-7s %s\n", m.mac[0], m.mac[1], m.mac[2], m.mac[3], m.mac[4],
               m.mac[5], tpf_role_name(m.role), m.name);
    }
    tfn_pool_counts(c);
    printf("POOL V4 OK %lu OTHER %lu WHO %lu BAD %lu OLD %lu\n", (unsigned long)c[0], (unsigned long)c[1],
           (unsigned long)c[2], (unsigned long)c[3], (unsigned long)c[4]);
    const tfn_pool_join_t j = tfn_pool_join_step();
    if (tfn_pool_join_sas(digits, mgr)) {
        printf("POOL MANAGER %s WANTS TO MANAGE THIS BOARD: SAS %.3s %.3s - ALLOW / NO\n", mgr, digits, digits + 3);
    } else if (j != TFN_POOL_JOIN_OFF) {
        printf("POOL JOIN %s %d s %s\n", tpf_step_name(j), tfn_pool_join_left_s(), tfn_pool_join_why());
    }
}

// "pool" | "pool join" | "pool stop" | "pool allow <6 digits>" | "pool no" | "pool leave"
void con_pool(char *cmd)
{
    (void)cmd;
    char *a = strtok(NULL, " \t\r\n");
    if (!a) {
        con_state();
    } else if (!strcmp(a, "join")) {
        printf("pool: %s\n", tfn_pool_join_start() ? "JOIN open" : "no JOIN");
    } else if (!strcmp(a, "stop")) {
        tfn_pool_join_stop();
    } else if (!strcmp(a, "allow")) {
        char d[16] = "";
        for (char *b = strtok(NULL, " \t\r\n"); b && strlen(d) + strlen(b) < sizeof d; b = strtok(NULL, " \t\r\n")) {
            strcat(d, b);  // ("482 913" or "482913")
        }
        // (the digits of the line: the JOIN fails for other digits)
        printf("pool: %s\n", tfn_pool_join_allow(d) ? "ALLOW" : "wrong digits or no JOIN");
    } else if (!strcmp(a, "no")) {
        tfn_pool_join_no();
    } else if (!strcmp(a, "leave")) {
        tfn_pool_leave(false);
    } else {
        printf("pool: pool | pool join | pool stop | pool allow <digits> | pool no | pool leave\n");
    }
}
