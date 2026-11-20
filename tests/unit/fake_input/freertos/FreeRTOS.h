// A small copy of FreeRTOS for the input test (test_input_gate.c): one task, no lock; one tick is one ms.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define pdTRUE                       1
#define pdFALSE                      0
#define pdMS_TO_TICKS(ms)            ((TickType_t)(ms))
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m)        ((void)(m))
#define portEXIT_CRITICAL(m)         ((void)(m))
#define portYIELD_FROM_ISR()         ((void)0)
