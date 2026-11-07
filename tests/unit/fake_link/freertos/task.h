// A small copy of the FreeRTOS task API for the link test: the test runs the task function itself (fk_lan_run)
#pragma once
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                                   TaskHandle_t *out, int core);
