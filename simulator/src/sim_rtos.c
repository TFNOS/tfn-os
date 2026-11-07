// The simulator: FreeRTOS, the clock, the random values and the log of the board. Each task is a thread of the PC.
// The queues, the semaphores and the task notifications use a lock and a condition (a "wait object", wobj_t). A tick
// is 1 ms, as on the board. esp_timer_get_time() counts from the start of the simulator, as the board counts from its
// boot.
//
// Two clocks: the real clock of the PC (the window), or the fixed clock (tfnsim_options, TFNSIM_FIXED: the tests).
// With the fixed clock the tasks take turns (sim_sched.c), a wait goes to the scheduler, and esp_random(), rand() and
// time() start from fixed values: the same input gives the same frames on each run and on each PC.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sim.h"
// (not sim_det.h: its time() and rand() macros are for the firmware; this file has their functions)

// ---------------------------------------------------------------- the wait objects
// A lock and a condition. A wait releases the lock while it waits. With the fixed clock the scheduler does the wait,
// and a wake makes the waiting tasks ready.
typedef struct {
    sim_mutex_t m;
    sim_cond_t cv;
} wobj_t;

struct sim_task {
    sim_sched_task_t sched;         // (first: sim_rtos_running() gets the task from it)
    TaskFunction_t fn;
    void *arg;
    char name[24];
    UBaseType_t prio;
    wobj_t w;                       // the notifications
    uint32_t nvalue;                // the notification value
    bool npending;                  // a notification waits (xTaskNotifyWait)
};
static _Thread_local struct sim_task *t_self;

static void wobj_init(wobj_t *w)
{
    sim_mutex_init(&w->m);
    sim_cond_init(&w->cv);
}

// The lock of w is held. Wait for wobj_wake(w) or the time until_us (-1: no time). The result is false at the time.
// A wait can also end with no wake: the caller checks its condition again.
static bool wobj_wait(wobj_t *w, int64_t until_us)
{
    if (sim_sched_fixed()) {
        if (!t_self) {
            return false;           // (a thread of the window is not a task: it cannot wait)
        }
        sim_mutex_unlock(&w->m);
        bool woke = sim_sched_block(&t_self->sched, w, until_us);
        sim_mutex_lock(&w->m);
        return woke;
    }
    int64_t left = -1;
    if (until_us >= 0) {
        left = until_us - sim_now_us();
        if (left <= 0) {
            return false;
        }
    }
    return sim_cond_wait(&w->cv, &w->m, left);
}

static void wobj_wake(wobj_t *w)
{
    if (sim_sched_fixed()) {
        sim_sched_wake(w);
    } else {
        sim_cond_wake_all(&w->cv);
    }
}

// the end of a wait of n ticks (-1 for portMAX_DELAY)
static int64_t wait_until(TickType_t wait)
{
    return wait == portMAX_DELAY ? -1 : sim_now_us() + (int64_t)wait * 1000;
}

// ---------------------------------------------------------------- the start, the clock, the critical sections
static sim_mutex_t s_crit = SIM_MUTEX_INIT;     // portENTER_CRITICAL (one lock for all; it nests)
static _Thread_local char t_id;                 // (its address tells the threads apart)
static const void *volatile s_crit_owner;
static int s_crit_depth;
static bool s_ready;

void sim_rtos_init(void)
{
    if (s_ready) {
        return;
    }
    s_ready = true;
    sim_os_init();
}

int64_t esp_timer_get_time(void)
{
    return sim_now_us();
}

void sim_sleep_us(int64_t us)
{
    if (!sim_sched_fixed()) {
        sim_os_sleep_us(us);
    } else if (!t_self) {
        return;                     // (a thread of the window)
    } else if (us <= 0) {
        sim_sched_yield(&t_self->sched);
    } else {
        sim_sched_block(&t_self->sched, NULL, sim_now_us() + us);
    }
}

// (with the fixed clock one task runs at a time: the section needs no lock)
void sim_crit_enter(void)
{
    if (sim_sched_fixed()) {
        return;
    }
    if (s_crit_owner == &t_id) {
        s_crit_depth++;
        return;
    }
    sim_mutex_lock(&s_crit);
    s_crit_owner = &t_id;
    s_crit_depth = 1;
}

void sim_crit_exit(void)
{
    if (sim_sched_fixed() || s_crit_owner != &t_id) {
        return;
    }
    if (--s_crit_depth == 0) {
        s_crit_owner = NULL;
        sim_mutex_unlock(&s_crit);
    }
}

void sim_yield(void)
{
    sim_sleep_us(0);
}

// The calling thread stops for good (a power off, a restart, a fault): with the fixed clock the next task runs
void sim_park(void)
{
    if (sim_sched_fixed() && t_self) {
        sim_sched_exit(&t_self->sched);
    }
    sim_os_park();
}

const char *sim_rtos_running(void)
{
    const sim_sched_task_t *t = sim_sched_current();
    return t ? ((const struct sim_task *)t)->name : "";
}

// ---------------------------------------------------------------- the random values and the time of day
static uint64_t s_esp_seed;                     // esp_random(): xorshift64*, shared by all tasks
static uint32_t s_rand_seed = 1;                // rand() and srand(): the same on each PC (sim_det.h)
static int64_t s_epoch_s = -1;                  // the fixed time of day at the start (-1: the clock of the PC)

void sim_rtos_fixed(uint32_t seed, int64_t epoch_s)
{
    sim_sched_set_fixed();
    s_esp_seed = ((uint64_t)seed * 6364136223846793005ULL + 1442695040888963407ULL) | 1;
    s_rand_seed = seed;
    s_epoch_s = epoch_s;
}

uint32_t esp_random(void)
{
    uint64_t old = __atomic_load_n(&s_esp_seed, __ATOMIC_RELAXED);
    if (!old) {
        uint64_t start = ((uint64_t)sim_os_now_us() * 6364136223846793005ULL + 1442695040888963407ULL) | 1;
        __atomic_compare_exchange_n(&s_esp_seed, &old, start, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        old = __atomic_load_n(&s_esp_seed, __ATOMIC_RELAXED);
    }
    uint64_t x;
    do {
        x = old;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
    } while (!__atomic_compare_exchange_n(&s_esp_seed, &old, x, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return (uint32_t)((x * 2685821657736338717ULL) >> 32);
}

// the linear congruential generator of the C standard (its example): 0..32767 on each PC
int sim_rand(void)
{
    s_rand_seed = s_rand_seed * 1103515245u + 12345u;
    return (int)((s_rand_seed / 65536u) % 32768u);
}

void sim_srand(unsigned seed)
{
    s_rand_seed = seed;
}

time_t sim_time(time_t *out)
{
    time_t t = s_epoch_s < 0 ? time(NULL) : (time_t)(s_epoch_s + sim_now_us() / 1000000);
    if (out) {
        *out = t;
    }
    return t;
}

// ---------------------------------------------------------------- the log
#define LOG_RING 32768
static char s_log[LOG_RING];
static uint32_t s_log_w, s_log_r;
static sim_mutex_t s_log_lock = SIM_MUTEX_INIT;
static int s_log_level = -1;

static int log_level_of_env(void)
{
    const char *e = getenv("TFN_SIM_LOG");
    static const char NAMES[] = "NEWIDV";       // (the order of esp_log_level_t)
    const char *p = e && *e ? strchr(NAMES, *e) : NULL;
    return p ? (int)(p - NAMES) : ESP_LOG_INFO;
}

void sim_log(int level, const char *tag, const char *fmt, ...)
{
    if (s_log_level < 0) {
        s_log_level = log_level_of_env();
    }
    if (level > s_log_level) {
        return;
    }
    char line[600];
    static const char L[] = "?EWIDV";
    int n = snprintf(line, sizeof line, "%c (%lld) %s: ", L[level < 6 ? level : 0], (long long)(sim_now_us() / 1000),
                     tag ? tag : "?");
    va_list ap;
    va_start(ap, fmt);
    if (n > 0 && n < (int)sizeof line) {
        vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    }
    va_end(ap);
    line[sizeof line - 2] = 0;
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = 0;
    }
    line[len++] = '\n';
    line[len] = 0;
    sim_mutex_lock(&s_log_lock);
    fputs(line, stdout);
    fflush(stdout);
    for (size_t i = 0; i < len; i++) {
        s_log[s_log_w++ % LOG_RING] = line[i];
    }
    if (s_log_w - s_log_r > LOG_RING) {
        s_log_r = s_log_w - LOG_RING;
    }
    sim_mutex_unlock(&s_log_lock);
}

int sim_log_take(char *out, int cap)
{
    sim_mutex_lock(&s_log_lock);
    int n = 0;
    while (s_log_r != s_log_w && n < cap - 1) {
        out[n++] = s_log[s_log_r++ % LOG_RING];
    }
    out[n] = 0;
    sim_mutex_unlock(&s_log_lock);
    return n;
}

// ---------------------------------------------------------------- the errors and the system
const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NVS_NOT_FOUND:
        return "ESP_ERR_NVS_NOT_FOUND";
    default:
        return "ESP_ERR";
    }
}

void sim_error_check_failed(esp_err_t rc, const char *file, int line, const char *expr)
{
    sim_log(ESP_LOG_ERROR, "sim", "ESP_ERROR_CHECK failed: %s (0x%x) at %s:%d: %s", esp_err_to_name(rc), rc, file, line,
            expr);
    sim_set_state(SIM_HALT, "ESP_ERROR_CHECK failed");
    sim_park();
}

void esp_restart(void)
{
    sim_log(ESP_LOG_INFO, "sim", "esp_restart(): the simulator starts again");
    sim_set_state(SIM_RESTART, "esp_restart()");
    sim_park();
}

esp_reset_reason_t esp_reset_reason(void)
{
    return ESP_RST_POWERON;
}

uint32_t esp_get_free_heap_size(void)
{
    return 180000;
}

uint32_t esp_get_minimum_free_heap_size(void)
{
    return 150000;
}

// ---------------------------------------------------------------- the tasks
static struct sim_task *task_new(const char *name, UBaseType_t prio)
{
    struct sim_task *t = calloc(1, sizeof *t);
    if (!t) {
        abort();
    }
    snprintf(t->name, sizeof t->name, "%s", name ? name : "task");
    t->prio = prio;
    wobj_init(&t->w);
    return t;
}

// (the real clock only: with the fixed clock the scheduler knows only the tasks of xTaskCreate)
void sim_rtos_adopt(const char *name)
{
    if (!t_self) {
        t_self = task_new(name, 1);
    }
}

static void task_thunk(void *p)
{
    struct sim_task *t = p;
    t_self = t;
    if (sim_sched_fixed()) {
        sim_sched_enter(&t->sched);
    }
    t->fn(t->arg);
    vTaskDelete(NULL);              // (a FreeRTOS task never returns; this one ends as vTaskDelete(NULL) ends it)
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                                   TaskHandle_t *out, BaseType_t core)
{
    (void)core;
    struct sim_task *t = task_new(name, prio);
    t->fn = fn;
    t->arg = arg;
    // the stack of the PC: the x86-64 code needs more than the board (4x, and at least 1 MB)
    size_t size = (size_t)stack * 4 < (1u << 20) ? (1u << 20) : (size_t)stack * 4;
    // the sound must never wait (as on the board: the audio task has the highest priority)
    int os_prio = prio >= 8 ? SIM_PRIO_HIGH : prio >= 5 ? SIM_PRIO_ABOVE : SIM_PRIO_NORMAL;
    if (sim_sched_fixed()) {
        sim_sched_add(&t->sched, (int)prio);
    }
    if (!sim_thread_start(task_thunk, t, size, os_prio)) {
        if (sim_sched_fixed()) {
            sim_sched_exit(&t->sched);
        }
        return pdFAIL;
    }
    if (out) {
        *out = t;
    }
    return pdPASS;
}

TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                           UBaseType_t prio, StackType_t *stk, void *tcb, BaseType_t core)
{
    (void)stk;
    (void)tcb;
    TaskHandle_t t = NULL;
    xTaskCreatePinnedToCore(fn, name, stack, arg, prio, &t, core);
    return t;
}

void vTaskDelete(TaskHandle_t t)
{
    if (t == NULL || t == t_self) {
        if (sim_sched_fixed() && t_self) {
            sim_sched_exit(&t_self->sched);
        }
        sim_thread_exit();
    }
    // (another task: the simulator cannot stop a thread safely; no code of the firmware does it on this board)
    sim_log(ESP_LOG_WARN, "sim", "vTaskDelete of another task (%s): not done", t->name);
}

void vTaskDelay(TickType_t ticks)
{
    if (ticks == portMAX_DELAY) {
        sim_park();
    }
    sim_sleep_us((int64_t)ticks * 1000);
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(sim_now_us() / 1000);
}

TickType_t xTaskGetTickCountFromISR(void)
{
    return xTaskGetTickCount();
}

BaseType_t xTaskDelayUntil(TickType_t *prev, TickType_t inc)
{
    TickType_t target = *prev + inc;
    int32_t wait = (int32_t)(target - xTaskGetTickCount());
    *prev = target;
    if (wait > 0) {
        sim_sleep_us((int64_t)wait * 1000);
        return pdTRUE;
    }
    return pdFALSE;
}

void vTaskDelayUntil(TickType_t *prev, TickType_t inc)
{
    xTaskDelayUntil(prev, inc);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    if (!t_self && !sim_sched_fixed()) {
        sim_rtos_adopt("thread");   // (with the fixed clock only the tasks of the scheduler are tasks)
    }
    return t_self;
}

const char *pcTaskGetName(TaskHandle_t t)
{
    if (!t) {
        t = xTaskGetCurrentTaskHandle();
    }
    return t ? t->name : "window";
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t)
{
    (void)t;
    return 2048;
}

UBaseType_t uxTaskPriorityGet(TaskHandle_t t)
{
    if (!t) {
        t = t_self;
    }
    return t ? t->prio : 1;
}

void vTaskPrioritySet(TaskHandle_t t, UBaseType_t prio)
{
    if (t) {
        t->prio = prio;             // (the scheduler keeps the priority of the start)
    }
}

void vTaskSuspend(TaskHandle_t t)
{
    if (t == NULL || t == t_self) {
        sim_park();
    }
}

void vTaskResume(TaskHandle_t t)
{
    (void)t;
}

void vTaskSuspendAll(void)
{
    sim_crit_enter();
}

BaseType_t xTaskResumeAll(void)
{
    sim_crit_exit();
    return pdFALSE;
}

// ---------------------------------------------------------------- the task notifications
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{
    struct sim_task *t = xTaskGetCurrentTaskHandle();
    if (!t) {
        return 0;
    }
    int64_t until = wait_until(wait);
    sim_mutex_lock(&t->w.m);
    while (t->nvalue == 0 && wobj_wait(&t->w, until)) {
        continue;
    }
    uint32_t v = t->nvalue;
    if (v) {
        t->nvalue = clear ? 0 : v - 1;
    }
    t->npending = false;
    sim_mutex_unlock(&t->w.m);
    return v;
}

static bool notify_apply(struct sim_task *t, uint32_t value, eNotifyAction action)
{
    switch (action) {
    case eSetBits:
        t->nvalue |= value;
        return true;
    case eIncrement:
        t->nvalue++;
        return true;
    case eSetValueWithOverwrite:
        t->nvalue = value;
        return true;
    case eSetValueWithoutOverwrite:
        if (t->npending) {
            return false;
        }
        t->nvalue = value;
        return true;
    default:
        return true;
    }
}

BaseType_t xTaskNotify(TaskHandle_t t, uint32_t value, eNotifyAction action)
{
    if (!t) {
        return pdFAIL;
    }
    sim_mutex_lock(&t->w.m);
    bool ok = notify_apply(t, value, action);
    if (ok) {
        t->npending = true;
    }
    wobj_wake(&t->w);
    sim_mutex_unlock(&t->w.m);
    return ok ? pdPASS : pdFAIL;
}

BaseType_t xTaskNotifyGive(TaskHandle_t t)
{
    return xTaskNotify(t, 0, eIncrement);
}

void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    xTaskNotifyGive(t);
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t value, eNotifyAction action, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return xTaskNotify(t, value, action);
}

BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit, uint32_t *value, TickType_t wait)
{
    struct sim_task *t = xTaskGetCurrentTaskHandle();
    if (!t) {
        return pdFALSE;
    }
    int64_t until = wait_until(wait);
    sim_mutex_lock(&t->w.m);
    if (!t->npending) {
        t->nvalue &= ~clear_entry;
    }
    while (!t->npending && wobj_wait(&t->w, until)) {
        continue;
    }
    BaseType_t got = t->npending ? pdTRUE : pdFALSE;
    if (value) {
        *value = t->nvalue;
    }
    if (got) {
        t->nvalue &= ~clear_exit;
    }
    t->npending = false;
    sim_mutex_unlock(&t->w.m);
    return got;
}

// ---------------------------------------------------------------- the queues and the semaphores
enum { Q_QUEUE, Q_BINARY, Q_MUTEX, Q_RECURSIVE, Q_COUNTING };
struct sim_queue {
    wobj_t w;                               // a put and a get wake the same waits
    uint8_t *buf;
    UBaseType_t len, size, head, count;     // a semaphore: count = the free tokens, len = the most tokens
    int kind;
    struct sim_task *owner;                 // a recursive mutex: the holder and its depth
    int depth;
};

static struct sim_queue *q_new(int kind, UBaseType_t len, UBaseType_t size)
{
    struct sim_queue *q = calloc(1, sizeof *q);
    if (!q) {
        abort();
    }
    wobj_init(&q->w);
    q->kind = kind;
    q->len = len ? len : 1;
    q->size = size;
    if (size) {
        q->buf = calloc(q->len, size);
    }
    return q;
}

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size)
{
    return q_new(Q_QUEUE, len, item_size);
}

void vQueueDelete(QueueHandle_t q)
{
    if (!q) {
        return;
    }
    free(q->buf);
    free(q);
}

// (the lock of q is held) the slot of a new item; it counts the item
static UBaseType_t q_slot(QueueHandle_t q, bool front, bool overwrite)
{
    if (overwrite && q->count >= q->len) {
        return (q->head + q->count - 1) % q->len;
    }
    q->count++;
    if (front) {
        q->head = (q->head + q->len - 1) % q->len;
        return q->head;
    }
    return (q->head + q->count - 1) % q->len;
}

static BaseType_t q_put(QueueHandle_t q, const void *item, TickType_t wait, bool front, bool overwrite)
{
    if (!q) {
        return pdFAIL;
    }
    int64_t until = wait_until(wait);
    sim_mutex_lock(&q->w.m);
    while (q->count >= q->len && !overwrite) {
        if (!wobj_wait(&q->w, until) && q->count >= q->len) {
            sim_mutex_unlock(&q->w.m);
            return errQUEUE_FULL;
        }
    }
    if (q->size) {
        UBaseType_t slot = q_slot(q, front, overwrite);
        memcpy(q->buf + (size_t)slot * q->size, item, q->size);
    } else if (q->count < q->len) {
        q->count++;
    }
    wobj_wake(&q->w);
    sim_mutex_unlock(&q->w.m);
    return pdPASS;
}

static BaseType_t q_get(QueueHandle_t q, void *item, TickType_t wait, bool peek)
{
    if (!q) {
        return pdFAIL;
    }
    int64_t until = wait_until(wait);
    sim_mutex_lock(&q->w.m);
    while (q->count == 0) {
        if (!wobj_wait(&q->w, until) && q->count == 0) {
            sim_mutex_unlock(&q->w.m);
            return pdFALSE;
        }
    }
    if (q->size && item) {
        memcpy(item, q->buf + (size_t)q->head * q->size, q->size);
    }
    if (!peek) {
        if (q->size) {
            q->head = (q->head + 1) % q->len;
        }
        q->count--;
        wobj_wake(&q->w);
    }
    sim_mutex_unlock(&q->w.m);
    return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    return q_put(q, item, wait, false, false);
}

BaseType_t xQueueSendToFront(QueueHandle_t q, const void *item, TickType_t wait)
{
    return q_put(q, item, wait, true, false);
}

BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item)
{
    return q_put(q, item, 0, false, true);
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    return q_get(q, item, wait, false);
}

BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait)
{
    return q_get(q, item, wait, true);
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    if (!q) {
        return pdFAIL;
    }
    sim_mutex_lock(&q->w.m);
    q->head = 0;
    q->count = 0;
    wobj_wake(&q->w);
    sim_mutex_unlock(&q->w.m);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    if (!q) {
        return 0;
    }
    sim_mutex_lock(&q->w.m);
    UBaseType_t n = q->count;
    sim_mutex_unlock(&q->w.m);
    return n;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q)
{
    return q ? q->len - uxQueueMessagesWaiting(q) : 0;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return q_new(Q_BINARY, 1, 0);
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial)
{
    struct sim_queue *q = q_new(Q_COUNTING, max, 0);
    q->count = initial;
    return q;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    struct sim_queue *q = q_new(Q_MUTEX, 1, 0);
    q->count = 1;
    return q;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    struct sim_queue *q = q_new(Q_RECURSIVE, 1, 0);
    q->count = 1;
    return q;
}

void vSemaphoreDelete(SemaphoreHandle_t s)
{
    vQueueDelete(s);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    return q_get(s, NULL, wait, false);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    return q_put(s, NULL, 0, false, false);
}

UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t s)
{
    return uxQueueMessagesWaiting(s);
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t wait)
{
    struct sim_task *me = xTaskGetCurrentTaskHandle();
    sim_mutex_lock(&s->w.m);
    if (me && s->owner == me) {
        s->depth++;
        sim_mutex_unlock(&s->w.m);
        return pdTRUE;
    }
    sim_mutex_unlock(&s->w.m);
    if (!q_get(s, NULL, wait, false)) {
        return pdFALSE;
    }
    sim_mutex_lock(&s->w.m);
    s->owner = me;
    s->depth = 1;
    sim_mutex_unlock(&s->w.m);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s)
{
    sim_mutex_lock(&s->w.m);
    if (s->owner != t_self) {
        sim_mutex_unlock(&s->w.m);
        return pdFALSE;
    }
    if (--s->depth > 0) {
        sim_mutex_unlock(&s->w.m);
        return pdTRUE;
    }
    s->owner = NULL;
    sim_mutex_unlock(&s->w.m);
    return xSemaphoreGive(s);
}
