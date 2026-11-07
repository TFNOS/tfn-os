#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
typedef int (*crypto_cb)(int, const uint8_t *, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *,
                         size_t, size_t *);
typedef void (*random_cb)(void *, size_t);
static crypto_cb s_crypto;
static random_cb s_random;
static char s_root[2048];
static uint8_t s_key[32], s_imported[32];
static int s_have_key, s_cut = -1, s_bad_close, s_bad_commit, s_hardware;
static bool s_mounted = true;
static void path_map(char *out, const char *in)
{
    snprintf(out, 4096, "%s%s", s_root, in);
}
FILE *test_fopen(const char *p, const char *m)
{
    char q[4096];
    path_map(q, p);
    return fopen(q, m);
}
int test_stat(const char *p, struct stat *s)
{
    char q[4096];
    path_map(q, p);
    return stat(q, s);
}
int test_remove(const char *p)
{
    char q[4096];
    path_map(q, p);
    return remove(q);
}
size_t test_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    size_t bytes = size * n;
    if (s_cut >= 0 && bytes > (size_t)s_cut)
        bytes = (size_t)s_cut;
    return fwrite(p, 1, bytes, f) / size;
}
int test_fclose(FILE *f)
{
    int r = fclose(f);
    return s_bad_close ? EOF : r;
}
#include "../../main/device_vault.c"
void test_config(const char *root, crypto_cb crypto, random_cb random)
{
    snprintf(s_root, sizeof s_root, "%s", root);
    s_crypto = crypto;
    s_random = random;
}
void test_reset(int forget_key)
{
    s_vault_ready = false;
    s_vault_key = 0;
    s_vault_lock = NULL;
    s_cut = -1;
    s_bad_close = 0;
    s_bad_commit = 0;
    if (forget_key == 1)
        s_have_key = 0;
    if (forget_key == 2)
        s_have_key = 1;
}
void test_hardware(int mode)
{
    s_hardware = mode;
}
void test_mount(int mounted)
{
    s_mounted = mounted != 0;
}
bool storage_ok(void)
{
    return s_mounted;
}
int device_guard_vault_hardware_key(uint8_t out[32])
{
    if (!s_hardware)
        return -2;
    if (s_hardware < 0)
        return -1;
    memset(out, 0xc7, 32);
    return 1;
}
bool device_guard_random(void *out, size_t n)
{
    s_random(out, n);
    return true;
}
void test_fault(int cut, int close_error, int commit_error)
{
    s_cut = cut;
    s_bad_close = close_error;
    s_bad_commit = commit_error;
}
void test_key(uint8_t *out)
{
    memcpy(out, s_imported, 32);
}
int nvs_open(const char *ns, int mode, nvs_handle_t *h)
{
    (void)ns;
    (void)mode;
    *h = 1;
    return 0;
}
int nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *n)
{
    (void)h;
    (void)k;
    if (!s_have_key)
        return ESP_ERR_NVS_NOT_FOUND;
    if (*n < 32)
        return -1;
    memcpy(out, s_key, 32);
    *n = 32;
    return 0;
}
int nvs_set_blob(nvs_handle_t h, const char *k, const void *in, size_t n)
{
    (void)h;
    (void)k;
    if (n != 32)
        return -1;
    memcpy(s_key, in, 32);
    s_have_key = 1;
    return 0;
}
int nvs_commit(nvs_handle_t h)
{
    (void)h;
    return s_bad_commit ? -1 : 0;
}
int nvs_erase_key(nvs_handle_t h, const char *k)
{
    (void)h;
    (void)k;
    s_have_key = 0;
    return 0;
}
void nvs_close(nvs_handle_t h)
{
    (void)h;
}
void esp_fill_random(void *out, size_t n)
{
    s_random(out, n);
}
int psa_crypto_init(void)
{
    return 0;
}
int psa_import_key(const psa_key_attributes_t *a, const uint8_t *k, size_t n, psa_key_id_t *id)
{
    (void)a;
    if (n != 32)
        return -1;
    memcpy(s_imported, k, 32);
    *id = 1;
    return 0;
}
int psa_destroy_key(psa_key_id_t id)
{
    (void)id;
    return 0;
}
int psa_aead_encrypt(psa_key_id_t key, int alg, const uint8_t *nonce, size_t nn, const uint8_t *aad,
                     size_t an, const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *used)
{
    (void)key;
    (void)alg;
    if (nn != 12)
        return -1;
    return s_crypto(1, nonce, aad, an, in, n, out, cap, used);
}
int psa_aead_decrypt(psa_key_id_t key, int alg, const uint8_t *nonce, size_t nn, const uint8_t *aad,
                     size_t an, const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *used)
{
    (void)key;
    (void)alg;
    if (nn != 12)
        return -1;
    return s_crypto(0, nonce, aad, an, in, n, out, cap, used);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (void *)1;
}
void xSemaphoreTake(SemaphoreHandle_t h, int timeout)
{
    (void)h;
    (void)timeout;
}
void xSemaphoreGive(SemaphoreHandle_t h)
{
    (void)h;
}
