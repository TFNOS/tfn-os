#pragma once
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { eNoAction, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                                   TaskHandle_t *out, BaseType_t core);
#define xTaskCreate(fn, name, stack, arg, prio, out) xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, tskNO_AFFINITY)
// (a static task: the simulator does not use the given stack and TCB)
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                           UBaseType_t prio, StackType_t *stk, void *tcb, BaseType_t core);
#define xTaskCreateStatic(fn, name, stack, arg, prio, stk, tcb) \
    xTaskCreateStaticPinnedToCore(fn, name, stack, arg, prio, stk, tcb, tskNO_AFFINITY)
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *prev, TickType_t inc);
BaseType_t xTaskDelayUntil(TickType_t *prev, TickType_t inc);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t t);
#define pcTaskGetTaskName pcTaskGetName
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t);
UBaseType_t uxTaskPriorityGet(TaskHandle_t t);
void vTaskPrioritySet(TaskHandle_t t, UBaseType_t prio);
void vTaskSuspend(TaskHandle_t t);
void vTaskResume(TaskHandle_t t);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
BaseType_t xTaskNotifyGive(TaskHandle_t t);
void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *woken);
BaseType_t xTaskNotify(TaskHandle_t t, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t value, eNotifyAction action, BaseType_t *woken);
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit, uint32_t *value, TickType_t wait);

#ifdef __cplusplus
}
#endif
