// The simulator: FreeRTOS on the PC (simulator/src/sim_rtos.c). Each task is a thread of the PC, the queues and the
// semaphores use a lock and a condition variable, and a tick is 1 ms (CONFIG_FREERTOS_HZ=1000, as on the board).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"          // (as in ESP-IDF: main/audio.c gets heap_caps_calloc() from here)

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t StackType_t;

#define configTICK_RATE_HZ      1000
#define configMAX_PRIORITIES    25
#define portTICK_PERIOD_MS      1
#define portTICK_RATE_MS        1
#define portMAX_DELAY           ((TickType_t)0xFFFFFFFFu)
#define pdMS_TO_TICKS(ms)       ((TickType_t)(ms))
#define pdTICKS_TO_MS(t)        ((uint32_t)(t))
#define pdTRUE                  1
#define pdFALSE                 0
#define pdPASS                  1
#define pdFAIL                  0
#define errQUEUE_EMPTY          0
#define errQUEUE_FULL           0
#define tskIDLE_PRIORITY        0
#define tskNO_AFFINITY          0x7FFFFFFF
#define PRO_CPU_NUM             0
#define APP_CPU_NUM             1

// The critical sections: one lock for all (a section never waits on the board, so one lock cannot deadlock)
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
void sim_crit_enter(void);
void sim_crit_exit(void);
#define portMUX_INITIALIZE(m)           ((void)(m))
#define spinlock_initialize(m)          ((void)(m))
#define portENTER_CRITICAL(m)           ((void)(m), sim_crit_enter())
#define portEXIT_CRITICAL(m)            ((void)(m), sim_crit_exit())
#define portENTER_CRITICAL_ISR(m)       portENTER_CRITICAL(m)
#define portEXIT_CRITICAL_ISR(m)        portEXIT_CRITICAL(m)
#define portENTER_CRITICAL_SAFE(m)      portENTER_CRITICAL(m)
#define portEXIT_CRITICAL_SAFE(m)       portEXIT_CRITICAL(m)
#define taskENTER_CRITICAL(m)           portENTER_CRITICAL(m)
#define taskEXIT_CRITICAL(m)            portEXIT_CRITICAL(m)
#define taskENTER_CRITICAL_ISR(m)       portENTER_CRITICAL(m)
#define taskEXIT_CRITICAL_ISR(m)        portEXIT_CRITICAL(m)
void sim_yield(void);
#define portYIELD()                     sim_yield()
#define taskYIELD()                     sim_yield()
#define portYIELD_FROM_ISR(...)         ((void)0)
#define portDISABLE_INTERRUPTS()        ((void)0)
#define portENABLE_INTERRUPTS()         ((void)0)
#define xPortGetCoreID()                1
#define configASSERT(x)                 ((void)0)

typedef void (*TaskFunction_t)(void *);
typedef struct sim_task *TaskHandle_t;
typedef struct sim_queue *QueueHandle_t;
typedef struct sim_queue *SemaphoreHandle_t;

#ifdef __cplusplus
}
#endif
