// A small copy of the ESP-IDF log for the link test (the lines go nowhere)
#pragma once

void fk_esp_log(const char *tag, const char *fmt, ...);
#define ESP_LOGE(tag, ...) fk_esp_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) fk_esp_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) fk_esp_log(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) fk_esp_log(tag, __VA_ARGS__)
