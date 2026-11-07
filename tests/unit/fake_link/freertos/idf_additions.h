// A small copy of the ESP-IDF additions to FreeRTOS for the link test
#pragma once
#include "freertos/queue.h"

QueueHandle_t xQueueCreateWithCaps(int len, size_t item, uint32_t caps);
