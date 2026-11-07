#pragma once
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t q);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueSendToFront(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t q);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q);
#define xQueueSendToBack(q, item, wait) xQueueSend(q, item, wait)
#define xQueueSendFromISR(q, item, woken) ((void)(woken), xQueueSend(q, item, 0))
#define xQueueSendToBackFromISR(q, item, woken) ((void)(woken), xQueueSend(q, item, 0))
#define xQueueOverwriteFromISR(q, item, woken) ((void)(woken), xQueueOverwrite(q, item))
#define xQueueReceiveFromISR(q, item, woken) ((void)(woken), xQueueReceive(q, item, 0))
#define uxQueueMessagesWaitingFromISR(q) uxQueueMessagesWaiting(q)

#ifdef __cplusplus
}
#endif
