// Protected builds authenticate administration without a startup PIN.
#include "device_guard.h"
#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include "esp_log.h"
#include "device_guard_core.h"
#include "web_auth.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_secure_boot.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "bootloader_random.h"
#include "mbedtls/private/ctr_drbg.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"
#ifndef DEVICE_GUARD_PUBLIC_HEADER
#error "A protected build needs its owner's public keys"
#endif
#include DEVICE_GUARD_PUBLIC_HEADER
_Static_assert(sizeof DG_ADMIN_PUBKEY == 65 && sizeof DG_APP_PUBKEY == 65, "P-256 public keys required");
static dg_state_t s_guard;
static portMUX_TYPE s_guard_mux = portMUX_INITIALIZER_UNLOCKED;
static uint64_t dg_now(void)
{
    return (uint64_t)esp_timer_get_time() / 1000u;
}
static mbedtls_ctr_drbg_context s_rng;
static SemaphoreHandle_t s_rng_mutex;
static bool s_seed_entropy;
static int dg_entropy(void *unused, unsigned char *out, size_t len)
{
    (void)unused;
    if (!s_seed_entropy) {
        return -1;
    }
    esp_fill_random(out, len);
    return 0;
}
bool device_guard_random_init(void)
{
    // Before ADC, Wi-Fi or Bluetooth starts: seed a standard AES-256 DRBG with
    // live SAR-ADC entropy. Later security operations also work with RF off.
    bootloader_random_enable();
    s_seed_entropy = true;
    mbedtls_ctr_drbg_init(&s_rng);
    static const unsigned char context[] = "TFN device protection v1";
    int result = mbedtls_ctr_drbg_seed(&s_rng, dg_entropy, NULL, context, sizeof context - 1);
    s_seed_entropy = false;
    bootloader_random_disable();
    if (result) {
        return false;
    }
    mbedtls_ctr_drbg_set_reseed_interval(&s_rng, INT_MAX);
    s_rng_mutex = xSemaphoreCreateMutex();
    return s_rng_mutex != NULL;
}
bool device_guard_random(void *out, size_t len)
{
    if (!s_rng_mutex || !out) {
        return false;
    }
    xSemaphoreTake(s_rng_mutex, portMAX_DELAY);
    int result = mbedtls_ctr_drbg_random(&s_rng, out, len);
    xSemaphoreGive(s_rng_mutex);
    return result == 0;
}
int device_guard_vault_hardware_key(uint8_t out[32])
{
#if CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC
    // Domain separation from the two native NVS XTS-key derivation inputs.
    static const uint8_t context[32] = "TFN AI account vault key v1";
    return esp_hmac_calculate((hmac_key_id_t)CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID, context, sizeof context, out) == ESP_OK
                   ? 1
                   : -1;
#else
    (void)out;
    return -2;
#endif
}
static vprintf_like_t s_log_previous;
static int dg_log(const char *format, va_list args)
{
    // Boot and background logs can contain saved SSIDs or device names. Reading
    // logs must not extend the administration session's idle deadline.
    bool connected = usb_serial_jtag_is_connected();
    uint64_t now = dg_now();
    taskENTER_CRITICAL(&s_guard_mux);
    dg_state_t copy = s_guard;
    bool allowed = dg_usb(&copy, connected, now);
    if (!allowed) {
        s_guard.usb = false;
    }
    taskEXIT_CRITICAL(&s_guard_mux);
    return allowed && s_log_previous ? s_log_previous(format, args) : 0;
}
void device_guard_log_init(void)
{
    if (!s_log_previous) {
        s_log_previous = esp_log_set_vprintf(dg_log);
    }
}
static bool dg_verify(const uint8_t pub[65], const uint8_t *data, size_t len, const uint8_t *sig, size_t siglen)
{
    if (siglen != 64 || psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key = 0;
    if (psa_import_key(&at, pub, 65, &key) != PSA_SUCCESS) {
        return false;
    }
    bool ok = psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), data, len, sig, siglen) == PSA_SUCCESS;
    psa_destroy_key(key);
    return ok;
}
bool device_guard_enabled(void)
{
    return true;
}
bool device_guard_setup_allowed(void)
{
    return device_guard_local_admin();
}
void device_guard_tick(void)
{
    if (!usb_serial_jtag_is_connected()) {
        taskENTER_CRITICAL(&s_guard_mux);
        s_guard.usb = false;
        s_guard.pending = false;
        taskEXIT_CRITICAL(&s_guard_mux);
    }
}
const uint8_t *device_guard_app_key(void)
{
    return DG_APP_PUBKEY;
}
bool device_guard_verify_app(const uint8_t *data, size_t len, const uint8_t *sig, size_t siglen)
{
    return dg_verify(DG_APP_PUBKEY, data, len, sig, siglen);
}
bool device_guard_usb_admin(void)
{
    bool connected = usb_serial_jtag_is_connected();
    uint64_t now = dg_now();
    taskENTER_CRITICAL(&s_guard_mux);
    bool ok = dg_usb(&s_guard, connected, now);
    taskEXIT_CRITICAL(&s_guard_mux);
    return ok;
}
bool device_guard_local_admin(void)
{
    uint64_t now = dg_now();
    taskENTER_CRITICAL(&s_guard_mux);
    bool ok = dg_local(&s_guard, now);
    taskEXIT_CRITICAL(&s_guard_mux);
    return ok;
}
void device_guard_local_lock(void)
{
    taskENTER_CRITICAL(&s_guard_mux);
    s_guard.local_until = 0;
    taskEXIT_CRITICAL(&s_guard_mux);
}
bool device_guard_local_unlock(const char *password)
{
    bool ok = web_auth_check_local(password);
    taskENTER_CRITICAL(&s_guard_mux);
    s_guard.local_until = ok ? dg_now() + DG_LOCAL_MS : 0;
    taskEXIT_CRITICAL(&s_guard_mux);
    return ok;
}
bool device_guard_local_path(const char *path)
{
    return !dg_private_path(path) || device_guard_local_admin();
}
static void dg_hex(char *out, const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = hex[data[i] >> 4];
        out[2 * i + 1] = hex[data[i] & 15];
    }
    out[2 * len] = 0;
}
static bool dg_unhex(const char *in, uint8_t *out, size_t len)
{
    if (!in || strlen(in) != len * 2) {
        return false;
    }
    for (size_t i = 0; i < len * 2; i++) {
        unsigned c = (uint8_t)in[i], v = c >= '0' && c <= '9'   ? c - '0'
                                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                : 16;
        if (v == 16) {
            return false;
        }
        if (!(i & 1)) {
            out[i / 2] = (uint8_t)(v << 4);
        } else {
            out[i / 2] |= (uint8_t)v;
        }
    }
    return true;
}
static void dg_admin(const char *args)
{
    uint64_t now = dg_now();
    if (!strcmp(args, "challenge")) {
        uint8_t nonce[32], mac[6];
        char nh[65], mh[13];
        if (!device_guard_random(nonce, sizeof nonce)) {
            puts("ADMIN WAIT");
            return;
        }
        taskENTER_CRITICAL(&s_guard_mux);
        bool ok = dg_challenge(&s_guard, nonce, now);
        taskEXIT_CRITICAL(&s_guard_mux);
        if (!ok || esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
            puts("ADMIN WAIT");
            return;
        }
        dg_hex(nh, nonce, sizeof nonce);
        dg_hex(mh, mac, sizeof mac);
        printf("ADMIN CHALLENGE %s %s\n", mh, nh);
        return;
    }
    if (!strncmp(args, "auth ", 5)) {
        uint8_t nonce[32], sig[64], mac[6];
        char nh[65], mh[13], message[112];
        taskENTER_CRITICAL(&s_guard_mux);
        bool pending = dg_take(&s_guard, now, nonce);
        taskEXIT_CRITICAL(&s_guard_mux);
        bool ok = false;
        if (pending && dg_unhex(args + 5, sig, sizeof sig) && esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            dg_hex(nh, nonce, sizeof nonce);
            dg_hex(mh, mac, sizeof mac);
            int n = snprintf(message, sizeof message, "TFNADMIN1:%s:%s", mh, nh);
            ok = n > 0 && (size_t)n < sizeof message &&
                 dg_verify(DG_ADMIN_PUBKEY, (const uint8_t *)message, (size_t)n, sig, sizeof sig);
        }
        memset(nonce, 0, sizeof nonce);
        memset(sig, 0, sizeof sig);
        taskENTER_CRITICAL(&s_guard_mux);
        dg_result(&s_guard, ok, now);
        taskEXIT_CRITICAL(&s_guard_mux);
        puts(ok ? "ADMIN OK" : "ADMIN DENIED");
        return;
    }
    if (!strcmp(args, "lock")) {
        taskENTER_CRITICAL(&s_guard_mux);
        dg_lock(&s_guard);
        taskEXIT_CRITICAL(&s_guard_mux);
        puts("ADMIN LOCKED");
        return;
    }
    if (!strcmp(args, "local")) {
        if (!device_guard_usb_admin()) {
            puts("ADMIN REQUIRED");
            return;
        }
        taskENTER_CRITICAL(&s_guard_mux);
        s_guard.local_until = now + DG_LOCAL_MS;
        taskEXIT_CRITICAL(&s_guard_mux);
        puts("ADMIN LOCAL 120 SECONDS");
        return;
    }
    printf("ADMIN %s PROTECTED secure_boot=%d flash_encryption=%d nvs_encryption=%d\n",
           device_guard_usb_admin() ? "AUTHORIZED" : "LOCKED", esp_secure_boot_enabled(),
           esp_efuse_is_flash_encryption_enabled(),
#if CONFIG_NVS_ENCRYPTION
           1
#else
           0
#endif
    );
}
bool device_guard_console(const char *command, const char *args)
{
    if (!strcmp(command, "admin")) {
        dg_admin(args ? args : "");
        return false;
    }
    if (dg_public_command(command, args ? args : "")) {
        return true;
    }
    if (device_guard_usb_admin()) {
        return true;
    }
    puts("ADMIN REQUIRED");
    return false;
}
#endif
