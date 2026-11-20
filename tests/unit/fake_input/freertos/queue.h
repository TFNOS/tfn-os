// A small copy of the FreeRTOS queue for the input test (the events of the touch task)
#pragma once
#include "freertos/FreeRTOS.h"

typedef struct fk_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(int len, size_t item);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t q);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
