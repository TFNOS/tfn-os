#pragma once
// Host adapter only. Python supplies real AES-GCM and random bytes; the tests
// interrupt the production vault's file writes at every possible byte.
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
typedef unsigned psa_key_id_t;
typedef int psa_key_attributes_t;
typedef void *SemaphoreHandle_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READWRITE 0
#define PSA_SUCCESS 0
#define PSA_KEY_ATTRIBUTES_INIT 0
#define PSA_KEY_TYPE_AES 0
#define PSA_ALG_GCM 0
#define PSA_KEY_USAGE_ENCRYPT 1
#define PSA_KEY_USAGE_DECRYPT 2
#define portMAX_DELAY 0
int nvs_open(const char *, int, nvs_handle_t *);
int nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
int nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
int nvs_commit(nvs_handle_t);
int nvs_erase_key(nvs_handle_t, const char *);
void nvs_close(nvs_handle_t);
void esp_fill_random(void *, size_t);
bool storage_ok(void);
int psa_crypto_init(void);
int psa_import_key(const psa_key_attributes_t *, const uint8_t *, size_t, psa_key_id_t *);
int psa_destroy_key(psa_key_id_t);
int psa_aead_encrypt(psa_key_id_t, int, const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *,
                     size_t, uint8_t *, size_t, size_t *);
int psa_aead_decrypt(psa_key_id_t, int, const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *,
                     size_t, uint8_t *, size_t, size_t *);
#define psa_set_key_type(a, b) ((void)(a), (void)(b))
#define psa_set_key_bits(a, b) ((void)(a), (void)(b))
#define psa_set_key_algorithm(a, b) ((void)(a), (void)(b))
#define psa_set_key_usage_flags(a, b) ((void)(a), (void)(b))
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void xSemaphoreTake(SemaphoreHandle_t, int);
void xSemaphoreGive(SemaphoreHandle_t);
FILE *test_fopen(const char *, const char *);
int test_stat(const char *, struct stat *);
int test_remove(const char *);
size_t test_fwrite(const void *, size_t, size_t, FILE *);
int test_fclose(FILE *);
#define fopen test_fopen
#define stat(p, s) test_stat(p, s)
#define remove test_remove
#define fwrite test_fwrite
#define fclose test_fclose
