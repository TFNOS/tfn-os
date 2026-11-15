// A small copy of the ESP-IDF log for the input test: the log lines go nowhere (the arguments are used, so the compiler
// sees no unused variable)
#pragma once
#include <stdio.h>

#define ESP_LOGI(tag, ...) ((void)(tag), (void)snprintf(NULL, 0, __VA_ARGS__))
#define ESP_LOGW(tag, ...) ((void)(tag), (void)snprintf(NULL, 0, __VA_ARGS__))
