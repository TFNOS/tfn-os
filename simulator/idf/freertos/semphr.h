#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial);
void vSemaphoreDelete(SemaphoreHandle_t s);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t wait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s);
UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t s);
#define xSemaphoreCreateBinaryStatic(buf) ((void)(buf), xSemaphoreCreateBinary())
#define xSemaphoreCreateMutexStatic(buf) ((void)(buf), xSemaphoreCreateMutex())
#define xSemaphoreGiveFromISR(s, woken) ((void)(woken), xSemaphoreGive(s))
#define xSemaphoreTakeFromISR(s, woken) ((void)(woken), xSemaphoreTake(s, 0))

#ifdef __cplusplus
}
#endif
