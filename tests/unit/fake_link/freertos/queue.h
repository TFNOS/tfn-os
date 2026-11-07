// A small copy of the FreeRTOS queue for the link test
#pragma once
#include "freertos/FreeRTOS.h"

typedef struct fk_queue *QueueHandle_t;
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t q);
