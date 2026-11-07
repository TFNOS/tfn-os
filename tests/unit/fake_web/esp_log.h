// A fake of the ESP-IDF log for the host test of main/tfn_web.c: the lines go to the test (fake_log in the test).
#pragma once
void fake_log(char level, const char *tag, const char *fmt, ...);
#define ESP_LOGE(tag, ...) fake_log('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) fake_log('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) fake_log('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) fake_log('D', tag, __VA_ARGS__)
