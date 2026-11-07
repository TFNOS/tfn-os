// The test of /api/pub (test_pub_web.c): a small copy of the private header of the installed apps
// (main/tfn_app_priv.h) with the parts that main/tfn_pub.c uses. The test gives the functions.
#pragma once
#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "tfn_pub.h"

// the log: the test gives fk_log
void fk_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGE(tag, ...) fk_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) fk_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) fk_log(tag, __VA_ARGS__)

// FreeRTOS: one task, so a lock is a counter
typedef struct fk_sem *SemaphoreHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t s, uint32_t wait);
int xSemaphoreGive(SemaphoreHandle_t s);

int64_t esp_timer_get_time(void);

// NVS: one blob in memory
typedef uint32_t nvs_handle_t;
typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);

// PSA: the SHA-256 of tests/unit/host_crypto.c
typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
#define PSA_SUCCESS     0
#define PSA_ALG_SHA_256 0x02000009u
psa_status_t psa_hash_compute(psa_algorithm_t alg, const uint8_t *in, size_t n, uint8_t *out, size_t out_size,
                              size_t *out_len);

// the installed apps and the board
bool tfa_sig_ok(const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl);
bool tfa_sig_ok_pk(const uint8_t *pk, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl);
const uint8_t *tfa_builtin_key(int i);
bool tfa_read_all(const char *path, uint8_t **out, size_t *len, size_t max);
void tfn_rescan(void);
bool board_sd_mounted(void);
