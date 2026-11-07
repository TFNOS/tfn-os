// A small copy of FreeRTOS for the link test (one task at a time: no lock)
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define pdTRUE                       1
#define pdFALSE                      0
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m)        ((void)(m))
#define portEXIT_CRITICAL(m)         ((void)(m))
