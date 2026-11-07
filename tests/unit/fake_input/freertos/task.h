// A small copy of the FreeRTOS task API for the input test: the test runs the touch task itself, report by report
// (test_input_gate.c: the delay of the task gives the control back to the test).
#pragma once
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                                   TaskHandle_t *out, int core);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *wake, TickType_t ticks);
void vTaskDelay(TickType_t ticks);
