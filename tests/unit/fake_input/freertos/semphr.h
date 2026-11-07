// A small copy of the FreeRTOS semaphore for the input test (the touch INT line: the test board has none)
#pragma once
#include "freertos/FreeRTOS.h"

typedef struct fk_sem *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
