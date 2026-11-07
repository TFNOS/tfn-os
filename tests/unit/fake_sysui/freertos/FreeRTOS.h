// The test of the question of the system layer: a small copy of FreeRTOS (one task, no lock; one tick is one ms)
#pragma once
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define pdMS_TO_TICKS(ms)            ((TickType_t)(ms))
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m)        ((void)(m))
#define portEXIT_CRITICAL(m)         ((void)(m))
