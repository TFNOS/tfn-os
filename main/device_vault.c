// The account vault protects tokens from other file viewers. Production keys
// derive from the read-protected HMAC eFuse. Software-only staging uses NVS.
#include "device_guard.h"
#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef DEVICE_VAULT_HOST
#include "vault_platform.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_random.h"
#include "psa/crypto.h"
#include "storage.h"
#endif
#define DV_MAX  12288u
#define DV_HEAD 32u
#define DV_OVER (DV_HEAD + 16u)
#define DV_FILE "/data/ai_accounts.enc"
#define DV_NEW  "/data/ai_accounts.tmp"
static psa_key_id_t s_vault_key;
static SemaphoreHandle_t s_vault_lock;
static bool s_vault_ready;
static void dv_clear(void *p, size_t n)
{
    volatile unsigned char *b = p;
    while (n--) {
        *b++ = 0;
    }
}
static int dv_exists(const char *p)
{
    struct stat st;
    if (!stat(p, &st)) {
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}
static int dv_read_file(const char *path, char *out, size_t cap, uint64_t *generation)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    uint8_t *b = malloc(DV_MAX + DV_OVER + 1);
    if (!b) {
        fclose(f);
        return -1;
    }
    size_t n = fread(b, 1, DV_MAX + DV_OVER + 1, f);
    bool ok = !ferror(f);
    fclose(f);
    size_t text = 0, written = 0;
    uint64_t gen = 0;
    if (ok && n >= DV_OVER && n <= DV_MAX + DV_OVER && !memcmp(b, "TFNACCT1", 8)) {
        for (unsigned i = 0; i < 4; i++) {
            text |= (size_t)b[20 + i] << (8 * i);
        }
        for (unsigned i = 0; i < 8; i++) {
            gen |= (uint64_t)b[24 + i] << (8 * i);
        }
        ok = gen && text && text <= DV_MAX && text <= cap && n == text + DV_OVER;
        if (ok) {
            ok = psa_aead_decrypt(s_vault_key, PSA_ALG_GCM, b + 8, 12, b, DV_HEAD, b + DV_HEAD, n - DV_HEAD,
                                  (uint8_t *)out, cap, &written) == PSA_SUCCESS &&
                 written == text;
        }
    } else {
        ok = false;
    }
    dv_clear(b, n <= DV_MAX + DV_OVER + 1 ? n : DV_MAX + DV_OVER + 1);
    free(b);
    if (!ok) {
        dv_clear(out, cap);
        return -1;
    }
    *generation = gen;
    return (int)text;
}
static int dv_scan(char *out, size_t cap, uint64_t *generation, int *slot)
{
    // Two authenticated generations: a write changes only the older slot. No
    // rename or deletion can remove the last complete copy after a power cut.
    const char *paths[] = {DV_FILE, DV_NEW};
    bool any = false;
    int best = 0;
    uint64_t gen = 0;
    char *candidate = malloc(DV_MAX);
    if (!candidate) {
        return -1;
    }
    *slot = -1;
    *generation = 0;
    for (int i = 0; i < 2; i++) {
        any |= dv_exists(paths[i]);
        int n = dv_read_file(paths[i], candidate, DV_MAX, &gen);
        if (n > 0 && gen > *generation) {
            if ((size_t)n > cap) {
                best = -1;
                *generation = gen;
                *slot = i;
            } else {
                memcpy(out, candidate, (size_t)n);
                best = n;
                *generation = gen;
                *slot = i;
            }
        }
    }
    dv_clear(candidate, DV_MAX);
    free(candidate);
    return best ? best : any ? -1 : 0;
}
static bool dv_write_to(const char *text, size_t len, const char *target, uint64_t gen)
{
    if (!len || len > DV_MAX || !gen) {
        return false;
    }
    uint8_t *b = malloc(len + DV_OVER);
    char *check = malloc(DV_MAX);
    bool ok = false;
    if (!b || !check) {
        goto done;
    }
    memcpy(b, "TFNACCT1", 8);
    if (!device_guard_random(b + 8, 12)) {
        goto done;
    }
    for (unsigned i = 0; i < 4; i++) {
        b[20 + i] = (uint8_t)(len >> (8 * i));
    }
    for (unsigned i = 0; i < 8; i++) {
        b[24 + i] = (uint8_t)(gen >> (8 * i));
    }
    size_t used = 0;
    if (psa_aead_encrypt(s_vault_key, PSA_ALG_GCM, b + 8, 12, b, DV_HEAD, (const uint8_t *)text, len, b + DV_HEAD,
                         len + 16, &used) != PSA_SUCCESS ||
        used != len + 16) {
        goto done;
    }
    FILE *f = fopen(target, "wb");
    if (!f) {
        goto done;
    }
    ok = fwrite(b, 1, len + DV_OVER, f) == len + DV_OVER;
    if (fclose(f)) {
        ok = false;
    }
    uint64_t verified = 0;
    ok = ok && dv_read_file(target, check, DV_MAX, &verified) == (int)len && verified == gen &&
         !memcmp(check, text, len);
done:
    if (b) {
        dv_clear(b, len + DV_OVER);
        free(b);
    }
    if (check) {
        dv_clear(check, DV_MAX);
        free(check);
    }
    return ok;
}
static bool dv_write(const char *text, size_t len)
{
    char *check = malloc(DV_MAX);
    if (!check) {
        return false;
    }
    uint64_t gen = 0;
    int slot = -1;
    int n = dv_scan(check, DV_MAX, &gen, &slot);
    dv_clear(check, DV_MAX);
    free(check);
    return n >= 0 && gen != UINT64_MAX && dv_write_to(text, len, slot == 0 ? DV_NEW : DV_FILE, gen + 1);
}
static bool dv_import(const uint8_t key[32])
{
    if (s_vault_key) {
        psa_destroy_key(s_vault_key);
        s_vault_key = 0;
    }
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&at, 256);
    psa_set_key_algorithm(&at, PSA_ALG_GCM);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    return psa_import_key(&at, key, 32, &s_vault_key) == PSA_SUCCESS;
}
static bool dv_staging_key(uint8_t key[32], bool create)
{
    nvs_handle_t h;
    if (nvs_open("dgvault", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    size_t len = 32;
    esp_err_t err = nvs_get_blob(h, "key", key, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND && create && !dv_exists(DV_FILE) && !dv_exists(DV_NEW)) {
        if (!device_guard_random(key, 32)) {
            nvs_close(h);
            return false;
        }
        err = nvs_set_blob(h, "key", key, 32);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        if (err == ESP_OK) {
            len = 32;
            err = nvs_get_blob(h, "key", key, &len);
        }
    }
    nvs_close(h);
    return err == ESP_OK && len == 32;
}
bool device_guard_vault_init(void)
{
    if (s_vault_ready) {
        return true;
    }
    if (!storage_ok()) {
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    uint8_t key[32], legacy_key[32];
    int hardware = device_guard_vault_hardware_key(key);
    if (hardware == -1) {
        return false;
    }
    if (hardware == -2 && !dv_staging_key(key, true)) {
        dv_clear(key, sizeof key);
        return false;
    }
    bool imported = dv_import(key);
    if (!imported) {
        dv_clear(key, sizeof key);
        return false;
    }
    s_vault_lock = xSemaphoreCreateMutex();
    if (!s_vault_lock) {
        return false;
    }
    char *text = malloc(DV_MAX + 1);
    if (!text) {
        return false;
    }
    uint64_t generation = 0;
    int slot = -1;
    int got = dv_scan(text, DV_MAX, &generation, &slot);
    bool ok = got >= 0;
    if (got < 0 && hardware == 1 && dv_staging_key(legacy_key, false) && dv_import(legacy_key)) {
        // Preserve the newest old-key slot until a new hardware-key copy has
        // passed readback. A reset at any step can resume with one valid key.
        got = dv_scan(text, DV_MAX, &generation, &slot);
        ok = got > 0 && generation != UINT64_MAX && dv_import(key) &&
             dv_write_to(text, (size_t)got, slot == 0 ? DV_NEW : DV_FILE, generation + 1);
    }
    dv_clear(key, sizeof key);
    dv_clear(legacy_key, sizeof legacy_key);
    if (ok && !got) {
        const char *legacy[] = {"/data/ai_limits.key", "/data/ai_limits.kew"};
        for (unsigned i = 0; i < 2; i++) {
            FILE *f = fopen(legacy[i], "rb");
            if (!f) {
                continue;
            }
            size_t n = fread(text, 1, DV_MAX + 1, f);
            ok = !ferror(f) && n >= 13 && n <= DV_MAX && !memcmp(text, "AILIM-ACCT 1\n", 13);
            fclose(f);
            if (ok) {
                ok = dv_write(text, n);
            }
            got = ok ? (int)n : -1;
            break;
        }
    }
    if (ok && got > 0) {
        char *check = malloc(DV_MAX);
        if (!check) {
            ok = false;
        } else {
            const char *paths[] = {DV_FILE, DV_NEW};
            for (unsigned i = 0; i < 2 && ok; i++) {
                uint64_t gen = 0;
                if (dv_read_file(paths[i], check, DV_MAX, &gen) < 0) {
                    ok = dv_write(text, (size_t)got);
                }
            }
            dv_clear(check, DV_MAX);
            free(check);
        }
    }
    // Delete only after verified encrypted storage exists. Flash encryption
    // also protects the old FAT clusters once hardware provisioning completes.
    if (ok && got > 0) {
        remove("/data/ai_limits.key");
        remove("/data/ai_limits.kew");
    }
    if (ok && hardware == 1) {
        nvs_handle_t h;
        if (nvs_open("dgvault", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_key(h, "key");
            nvs_commit(h);
            nvs_close(h);
        }
    }
    dv_clear(text, DV_MAX + 1);
    free(text);
    s_vault_ready = ok;
    return ok;
}
int device_guard_accounts_read(char *out, size_t cap)
{
    if (!out || !cap || !s_vault_ready) {
        return -1;
    }
    uint64_t generation;
    int slot;
    xSemaphoreTake(s_vault_lock, portMAX_DELAY);
    int n = dv_scan(out, cap, &generation, &slot);
    xSemaphoreGive(s_vault_lock);
    if (n < 0) {
        dv_clear(out, cap);
    }
    return n;
}
int device_guard_accounts_write(const char *text, size_t len)
{
    if (!text || !s_vault_ready) {
        return -1;
    }
    xSemaphoreTake(s_vault_lock, portMAX_DELAY);
    bool ok = dv_write(text, len);
    xSemaphoreGive(s_vault_lock);
    return ok ? 1 : -1;
}
#endif
