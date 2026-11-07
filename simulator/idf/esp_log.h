// The simulator: the log of the board goes to the console of the PC ("I (1234) main: open MENU"), as the serial log.
// (It is found before tests/host/common/esp_log.h, which drops the log.) TFN_SIM_LOG=E, W, I or D sets the level.
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
void sim_log(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGE(tag, ...) sim_log(ESP_LOG_ERROR, tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) sim_log(ESP_LOG_WARN, tag, __VA_ARGS__)
#if SIM_LOG_INFO_OFF
// (build.py LOG_INFO_OFF: a file whose info lines name a variable that only the board build has; the host fuzz also
// drops these lines)
#define ESP_LOGI(tag, ...) ((void)0)
#else
#define ESP_LOGI(tag, ...) sim_log(ESP_LOG_INFO, tag, __VA_ARGS__)
#endif
#define ESP_LOGD(tag, ...) sim_log(ESP_LOG_DEBUG, tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) sim_log(ESP_LOG_VERBOSE, tag, __VA_ARGS__)
#define ESP_EARLY_LOGE ESP_LOGE
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGI ESP_LOGI
#define ESP_DRAM_LOGE ESP_LOGE
#define ESP_DRAM_LOGW ESP_LOGW
#define ESP_DRAM_LOGI ESP_LOGI
#define esp_log_level_set(tag, level) ((void)(tag), (void)(level))
#define esp_log_level_get(tag) ((void)(tag), ESP_LOG_INFO)
#define ESP_LOG_BUFFER_HEX(tag, buf, len) ((void)(tag), (void)(buf), (void)(len))
#define ESP_LOG_BUFFER_HEXDUMP(tag, buf, len, level) ((void)(tag), (void)(buf), (void)(len), (void)(level))

#ifdef __cplusplus
}
#endif
