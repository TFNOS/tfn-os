#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#define xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, out, core, caps) \
    xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, core)
#define xTaskCreateWithCaps(fn, name, stack, arg, prio, out, caps) xTaskCreate(fn, name, stack, arg, prio, out)
#define vTaskDeleteWithCaps(t) vTaskDelete(t)
#define xQueueCreateWithCaps(len, size, caps) xQueueCreate(len, size)
#define vQueueDeleteWithCaps(q) vQueueDelete(q)
