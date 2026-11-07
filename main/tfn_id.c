// The device identity of the board (tfn_id.h): the P-256 key pair in the NVS namespace "tfnid", the ID, the X25519
// statement, the box key of the pool (NVS "tfnpool"), the web route GET /api/id. The static state of this file is in PSRAM (main/apps.json "psram"): it adds
// no internal RAM. The short task of the start has its stack in internal RAM (the NVS writes), and it ends.
#include "tfn_id.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "tfn_extras.h"
#include "wifi.h"
#if CONFIG_TFN_EXTRA_VPN
#include "vpn.h"
#define HAS_VPN 1
#else
#define HAS_VPN 0
#endif

static const char TAG[] = "tfnid";
#define ID_NS         "tfnid"    // the NVS namespace (not "arcade": store_make_room() never moves these keys)
#define POOL_NS       "tfnpool"  // the namespace of the pool (tfn_pool.c): the box key "bx" and "bxs"
#define WAIT_RADIO_MS 120000     // the first start: the task waits this long for the WiFi start, then it stops
#define WAIT_STEP_MS  500
#define TASK_STACK    6144          // (the P-256 key generation and the signature of mbedTLS)
#define X_MSG_LEN     (5 + 8 + 32)  // "TFNX1", the 8 ID bytes, the 32 bytes of the VPN public key

static uint8_t s_pk[TFN_ID_PK_LEN];
static uint8_t s_idb[8];
static char s_id[TFN_ID_LEN];
static uint8_t s_xs[TFN_ID_XS_LEN];
static char s_x25519[48];
static uint8_t s_bx[32], s_bxs[TFN_ID_XS_LEN];
static bool s_ready;     // s_pk, s_idb and s_id hold the values (written once, then only read)
static bool s_xs_ready;  // s_xs and s_x25519 hold the values
static bool s_bx_ready;  // s_bx and s_bxs hold the values

static bool ready(void)
{
    return __atomic_load_n(&s_ready, __ATOMIC_ACQUIRE);
}

static void sha256_psa(const uint8_t *data, size_t n, uint8_t out[32])
{
    size_t ol = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, data, n, out, 32, &ol) != PSA_SUCCESS || ol != 32) {
        memset(out, 0, 32);
    }
}

static void set_pk(const uint8_t pk[TFN_ID_PK_LEN])
{
    memcpy(s_pk, pk, TFN_ID_PK_LEN);
    tfn_id_bytes(s_pk, sha256_psa, s_idb);
    tfn_id_text(s_idb, s_id);
    __atomic_store_n(&s_ready, true, __ATOMIC_RELEASE);
}

// The attributes of the key pair: ECDSA P-256 with SHA-256.
static void pair_attr(psa_key_attributes_t *at, bool export_it)
{
    psa_set_key_type(at, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(at, 256);
    psa_set_key_usage_flags(at, PSA_KEY_USAGE_SIGN_MESSAGE | (export_it ? PSA_KEY_USAGE_EXPORT : 0));
    psa_set_key_algorithm(at, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
}

// The key pair: from sk (old = true), or a new one (its sk goes into sk). Returns the PSA key, 0 = a fault.
static psa_key_id_t pair_get(uint8_t sk[32], bool old)
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    pair_attr(&at, !old);
    psa_key_id_t k = 0;
    if (old) {
        return psa_import_key(&at, sk, 32, &k) == PSA_SUCCESS ? k : 0;
    }
    size_t n = 0;
    if (psa_generate_key(&at, &k) != PSA_SUCCESS) {
        return 0;
    }
    if (psa_export_key(k, sk, 32, &n) != PSA_SUCCESS || n != 32) {
        psa_destroy_key(k);
        return 0;
    }
    return k;
}

// The key of the board: sk and pk into the NVS (a new key only when sk is not there; with sk and no pk, pk again
// from sk). A fault (for example a full NVS): no ID at this start, a new try at the next start.
static bool key_make(void)
{
    uint8_t sk[32], pk[TFN_ID_PK_LEN];
    size_t n = sizeof sk;
    nvs_handle_t h;
    if (nvs_open(ID_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "the NVS does not open: no ID at this start");
        return false;
    }
    const bool old = nvs_get_blob(h, "sk", sk, &n) == ESP_OK && n == sizeof sk;
    psa_key_id_t k = pair_get(sk, old);
    bool ok = k != 0;
    if (ok) {
        ok = psa_export_public_key(k, pk, sizeof pk, &n) == PSA_SUCCESS && n == sizeof pk;
        psa_destroy_key(k);
    }
    if (ok && !old) {
        ok = nvs_set_blob(h, "sk", sk, sizeof sk) == ESP_OK;
    }
    ok = ok && nvs_set_blob(h, "pk", pk, sizeof pk) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    mbedtls_platform_zeroize(sk, sizeof sk);
    if (!ok) {
        ESP_LOGW(TAG, "the key is not made or not saved (a full NVS?): no ID at this start");
        return false;
    }
    set_pk(pk);
    ESP_LOGI(TAG, "%s, ID %s", old ? "the public key again from the private key" : "a new key pair", s_id);
    return true;
}

// The signature (r || s) of msg (n bytes) by sk of the NVS namespace "tfnid".
static bool dev_sign(const uint8_t *msg, size_t n, uint8_t sig[TFN_ID_XS_LEN])
{
    uint8_t sk[32];
    size_t l = sizeof sk;
    nvs_handle_t h;
    bool ok = nvs_open(ID_NS, NVS_READONLY, &h) == ESP_OK;
    if (ok) {
        ok = nvs_get_blob(h, "sk", sk, &l) == ESP_OK && l == sizeof sk;
        nvs_close(h);
    }
    psa_key_id_t k = ok ? pair_get(sk, true) : 0;
    mbedtls_platform_zeroize(sk, sizeof sk);
    if (!k) {
        return false;
    }
    ok = psa_sign_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, n, sig, TFN_ID_XS_LEN, &l) == PSA_SUCCESS &&
         l == TFN_ID_XS_LEN;
    psa_destroy_key(k);
    return ok;
}

bool tfn_id_sign(const uint8_t *msg, size_t n, uint8_t sig[TFN_ID_XS_LEN])
{
    const bool box = n >= 6 && !memcmp(msg, "TFNBX1", 6), join = n >= 5 && !memcmp(msg, "TFNJ1", 5);
    return ready() && (box || join) && dev_sign(msg, n, sig);
}

// ---------------------------------------------------------------- the box key of the pool

// The attributes of an X25519 key (export = the private part can go out: only at the first make).
static void box_attr(psa_key_attributes_t *at, bool export_it)
{
    psa_set_key_type(at, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(at, 255);
    psa_set_key_usage_flags(at, PSA_KEY_USAGE_DERIVE | (export_it ? PSA_KEY_USAGE_EXPORT : 0));
    psa_set_key_algorithm(at, PSA_ALG_ECDH);
}

// The public part of the private box key bx (old = true), or a new key pair (its private part into bx).
static bool box_pub(uint8_t bx[32], uint8_t pub[32], bool old)
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    box_attr(&at, !old);
    psa_key_id_t k = 0;
    size_t n = 0;
    if ((old ? psa_import_key(&at, bx, 32, &k) : psa_generate_key(&at, &k)) != PSA_SUCCESS) {
        return false;
    }
    bool ok = old || (psa_export_key(k, bx, 32, &n) == PSA_SUCCESS && n == 32);
    ok = ok && psa_export_public_key(k, pub, 32, &n) == PSA_SUCCESS && n == 32;
    psa_destroy_key(k);
    return ok;
}

// The box key: the copy of the NVS, else a new one with its signature. A fault: no box key at this start.
static void box_make(void)
{
    uint8_t bx[32], pub[32], sig[TFN_ID_XS_LEN], msg[6 + 8 + 32];
    size_t n = sizeof bx, ns = sizeof sig;
    nvs_handle_t h;
    if (nvs_open(POOL_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "the NVS does not open: no box key at this start");
        return;
    }
    const bool old = nvs_get_blob(h, "bx", bx, &n) == ESP_OK && n == sizeof bx &&
                     nvs_get_blob(h, "bxs", sig, &ns) == ESP_OK && ns == sizeof sig;
    bool ok = box_pub(bx, pub, old);
    if (ok && !old) {
        memcpy(msg, "TFNBX1", 6);
        memcpy(msg + 6, s_idb, 8);
        memcpy(msg + 14, pub, 32);
        ok = dev_sign(msg, sizeof msg, sig) && nvs_set_blob(h, "bx", bx, sizeof bx) == ESP_OK &&
             nvs_set_blob(h, "bxs", sig, sizeof sig) == ESP_OK && nvs_commit(h) == ESP_OK;
        ESP_LOGI(TAG, "the box key of the pool: %s", ok ? "a new key pair" : "a fault (a full NVS?)");
    }
    nvs_close(h);
    mbedtls_platform_zeroize(bx, sizeof bx);
    if (ok) {
        memcpy(s_bx, pub, sizeof pub);
        memcpy(s_bxs, sig, sizeof sig);
        __atomic_store_n(&s_bx_ready, true, __ATOMIC_RELEASE);
    }
}

#if CONFIG_TFN_EXTRA_VPN
// A WireGuard key (44 characters of base64) as 32 bytes. Returns false for a bad text.
static bool b64_32(const char *s, uint8_t out[32])
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!s || strlen(s) != 44 || s[43] != '=') {
        return false;
    }
    uint32_t acc = 0;
    int bits = 0, k = 0;
    for (int i = 0; i < 43 && k < 32; i++) {
        const char *p = strchr(B64, s[i]);
        if (!p || !*p) {
            return false;
        }
        acc = acc << 6 | (uint32_t)(p - B64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[k++] = (uint8_t)(acc >> bits);
        }
    }
    return k == 32;
}

// true when xs is a good signature of msg by the key of the board
static bool x_verify(const uint8_t *msg, const uint8_t xs[TFN_ID_XS_LEN])
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    if (psa_import_key(&at, s_pk, TFN_ID_PK_LEN, &k) != PSA_SUCCESS) {
        return false;
    }
    const bool ok =
            psa_verify_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, X_MSG_LEN, xs, TFN_ID_XS_LEN) == PSA_SUCCESS;
    psa_destroy_key(k);
    return ok;
}

#endif

// The X25519 statement: the copy of the NVS when it binds the VPN key of now, else a new signature.
static void statement(void)
{
#if CONFIG_TFN_EXTRA_VPN
    const char *x = vpn_public_key();
    uint8_t msg[X_MSG_LEN], xs[TFN_ID_XS_LEN];
    if (!b64_32(x, msg + 5 + 8)) {
        return;
    }
    memcpy(msg, "TFNX1", 5);
    memcpy(msg + 5, s_idb, 8);
    nvs_handle_t h;
    if (nvs_open(ID_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    size_t n = sizeof xs;
    bool ok = nvs_get_blob(h, "xs", xs, &n) == ESP_OK && n == sizeof xs && x_verify(msg, xs);
    if (!ok) {
        ok = dev_sign(msg, X_MSG_LEN, xs);
        if (ok && (nvs_set_blob(h, "xs", xs, sizeof xs) != ESP_OK || nvs_commit(h) != ESP_OK)) {
            ESP_LOGW(TAG, "the X25519 statement is not saved (a full NVS?)");
        }
        ESP_LOGI(TAG, "the X25519 statement: %s", ok ? "a new signature" : "a fault");
    }
    nvs_close(h);
    if (ok) {
        memcpy(s_xs, xs, sizeof xs);
        snprintf(s_x25519, sizeof s_x25519, "%s", x);
        __atomic_store_n(&s_xs_ready, true, __ATOMIC_RELEASE);
    }
#endif
}

static void id_task(void *arg)
{
    (void)arg;
    // a new key (the device key, the box key, the VPN key) needs true random numbers: the radio on (the WiFi started)
    int waited = 0;
    while (wifi_state() == WIFI_OFF && waited < WAIT_RADIO_MS) {
        vTaskDelay(pdMS_TO_TICKS(WAIT_STEP_MS));
        waited += WAIT_STEP_MS;
    }
    if (wifi_state() == WIFI_OFF) {
        ESP_LOGW(TAG, "the WiFi did not start: no new key at this start");
    } else if (!ready()) {
        key_make();
    }
    if (ready()) {
        box_make();
        statement();
    }
    vTaskDelete(NULL);
}

void tfn_id_init(void)
{
    psa_crypto_init();
    nvs_handle_t h;
    uint8_t pk[TFN_ID_PK_LEN];
    size_t n = sizeof pk, ns = 0;
    if (nvs_open(ID_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "sk", NULL, &ns) == ESP_OK && ns == 32 && nvs_get_blob(h, "pk", pk, &n) == ESP_OK &&
            n == sizeof pk && pk[0] == 4) {
            set_pk(pk);
        }
        nvs_close(h);
    }
    // (the task also loads the box key: its public part needs an X25519 step)
    if (xTaskCreatePinnedToCore(id_task, "tfnid", TASK_STACK, NULL, 1, NULL, 1) != pdPASS) {
        ESP_LOGW(TAG, "no memory for the task: no new key or statement at this start");
    }
}

const char *tfn_id_str(void)
{
    return ready() ? s_id : "";
}

const uint8_t *tfn_id_pk(void)
{
    return ready() ? s_pk : NULL;
}

const uint8_t *tfn_id_raw(void)
{
    return ready() ? s_idb : NULL;
}

const uint8_t *tfn_id_box(void)
{
    return __atomic_load_n(&s_bx_ready, __ATOMIC_ACQUIRE) ? s_bx : NULL;
}

const uint8_t *tfn_id_box_sig(void)
{
    return __atomic_load_n(&s_bx_ready, __ATOMIC_ACQUIRE) ? s_bxs : NULL;
}

const uint8_t *tfn_id_xs(void)
{
    return __atomic_load_n(&s_xs_ready, __ATOMIC_ACQUIRE) ? s_xs : NULL;
}

const char *tfn_id_x25519(void)
{
    return __atomic_load_n(&s_xs_ready, __ATOMIC_ACQUIRE) ? s_x25519 : "";
}

static void hex(char *out, const uint8_t *d, int n)
{
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        out[2 * i] = H[d[i] >> 4];
        out[2 * i + 1] = H[d[i] & 15];
    }
    out[2 * n] = 0;
}

// GET /api/id: only public values (never sk)
static esp_err_t id_api(httpd_req_t *req)
{
    char pk[2 * TFN_ID_PK_LEN + 1] = "", xs[2 * TFN_ID_XS_LEN + 1] = "", out[400];
    const uint8_t *p = tfn_id_pk(), *s = tfn_id_xs();
    if (p) {
        hex(pk, p, TFN_ID_PK_LEN);
    }
    if (s) {
        hex(xs, s, TFN_ID_XS_LEN);
    }
    snprintf(out, sizeof out, "{\"id\":\"%s\",\"pk\":\"%s\",\"x25519\":\"%s\",\"xs\":\"%s\",\"alg\":\"p256\"}",
             tfn_id_str(), pk, tfn_id_x25519(), xs);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, out);
}

void tfn_id_register(void *server)
{
    httpd_uri_t u = {.uri = "/api/id", .method = HTTP_GET, .handler = id_api};
    httpd_register_uri_handler(server, &u);
}
