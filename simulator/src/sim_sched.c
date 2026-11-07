// The simulator: the clock, and the lockstep scheduler of the fixed clock (tfnsim_options(TFNSIM_FIXED)).
//
// With the real clock (the window) each task is a thread of the PC that runs when the PC runs it, on the clock of the
// PC. With the fixed clock the result must be the same on each run and on each PC, so the tasks take turns: one task
// runs at a time, until it waits (a sleep, a queue, a semaphore, a notification) or ends. Then the ready task with the
// highest priority runs (of two with the same priority, the one that became ready first). When no task is ready, the
// clock goes to the next time that a task waits for. The clock stops at the limit that the caller of
// sim_sched_run() sets: then all tasks wait, and the caller can read the screen and give the next input.
#include <stdlib.h>
#include "sim.h"
#include "sim_os.h"

#define SCHED_MAX 256

static bool s_fixed;
static sim_mutex_t s_m = SIM_MUTEX_INIT;     // all the state of the scheduler below
static sim_cond_t s_idle_cv = SIM_COND_INIT;
static sim_sched_task_t *s_tasks[SCHED_MAX];
static int s_ntasks;
static sim_sched_task_t *s_cur;              // the task that runs (NULL: none)
static uint64_t s_seq;                       // the order in which the tasks became ready
static volatile int64_t s_now;               // the fixed clock (us)
static int64_t s_limit;                      // the clock stops here
static bool s_idle = true;                   // all tasks wait at the limit

void sim_sched_set_fixed(void)
{
    s_fixed = true;
}

bool sim_sched_fixed(void)
{
    return s_fixed;
}

int64_t sim_now_us(void)
{
    return s_fixed ? s_now : sim_os_now_us();
}

static void make_ready(sim_sched_task_t *t)
{
    t->state = SIM_TASK_READY;
    t->seq = ++s_seq;
}

// the ready task with the highest priority, the first one of that priority; NULL: none
static sim_sched_task_t *best_ready(void)
{
    sim_sched_task_t *best = NULL;
    for (int i = 0; i < s_ntasks; i++) {
        sim_sched_task_t *t = s_tasks[i];
        if (t->state != SIM_TASK_READY) {
            continue;
        }
        if (!best || t->prio > best->prio || (t->prio == best->prio && t->seq < best->seq)) {
            best = t;
        }
    }
    return best;
}

// the earliest time that a waiting task waits for; -1: none
static int64_t next_wake(void)
{
    int64_t next = -1;
    for (int i = 0; i < s_ntasks; i++) {
        sim_sched_task_t *t = s_tasks[i];
        if (t->state == SIM_TASK_WAIT && t->wake_us >= 0 && (next < 0 || t->wake_us < next)) {
            next = t->wake_us;
        }
    }
    return next;
}

// The next task runs (s_m is held). When no task is ready, the clock moves; at the limit all tasks wait (s_idle).
static void dispatch(void)
{
    for (;;) {
        sim_sched_task_t *t = best_ready();
        if (t) {
            s_cur = t;
            t->state = SIM_TASK_RUN;
            sim_cond_wake_all(&t->go);
            return;
        }
        int64_t next = next_wake();
        if (next < 0 || next > s_limit) {
            s_cur = NULL;
            s_now = s_limit;
            s_idle = true;
            sim_cond_wake_all(&s_idle_cv);
            return;
        }
        if (next > s_now) {
            s_now = next;
        }
        for (int i = 0; i < s_ntasks; i++) {
            sim_sched_task_t *w = s_tasks[i];
            if (w->state == SIM_TASK_WAIT && w->wake_us >= 0 && w->wake_us <= s_now) {
                w->timed_out = true;
                make_ready(w);
            }
        }
    }
}

// (s_m is held) the calling task waits for its turn
static void wait_turn(sim_sched_task_t *t)
{
    while (s_cur != t) {
        sim_cond_wait(&t->go, &s_m, -1);
    }
}

void sim_sched_add(sim_sched_task_t *t, int prio)
{
    sim_cond_init(&t->go);
    t->prio = prio;
    t->wake_us = -1;
    sim_mutex_lock(&s_m);
    if (s_ntasks == SCHED_MAX) {
        sim_mutex_unlock(&s_m);
        abort();                            // (the board has far fewer tasks)
    }
    s_tasks[s_ntasks++] = t;
    make_ready(t);
    sim_mutex_unlock(&s_m);
}

void sim_sched_enter(sim_sched_task_t *t)
{
    sim_mutex_lock(&s_m);
    wait_turn(t);
    sim_mutex_unlock(&s_m);
}

bool sim_sched_block(sim_sched_task_t *t, const void *on, int64_t until_us)
{
    sim_mutex_lock(&s_m);
    t->state = SIM_TASK_WAIT;
    t->on = on;
    t->wake_us = until_us;
    t->timed_out = false;
    dispatch();
    wait_turn(t);
    t->on = NULL;
    t->wake_us = -1;
    bool woke = !t->timed_out;
    sim_mutex_unlock(&s_m);
    return woke;
}

void sim_sched_wake(const void *on)
{
    if (!on) {
        return;                             // (NULL: a sleep, only the clock ends it)
    }
    sim_mutex_lock(&s_m);
    for (int i = 0; i < s_ntasks; i++) {
        sim_sched_task_t *t = s_tasks[i];
        if (t->state == SIM_TASK_WAIT && t->on == on) {
            make_ready(t);
        }
    }
    sim_mutex_unlock(&s_m);
}

void sim_sched_yield(sim_sched_task_t *t)
{
    sim_mutex_lock(&s_m);
    make_ready(t);
    dispatch();
    wait_turn(t);
    sim_mutex_unlock(&s_m);
}

void sim_sched_exit(sim_sched_task_t *t)
{
    sim_mutex_lock(&s_m);
    t->state = SIM_TASK_DEAD;
    if (s_cur == t) {
        dispatch();
    }
    sim_mutex_unlock(&s_m);
}

bool sim_sched_run(int64_t us, int64_t real_limit_us)
{
    int64_t give_up = sim_os_now_us() + real_limit_us;
    sim_mutex_lock(&s_m);
    if (s_idle) {
        s_limit = s_now + (us > 0 ? us : 0);
        s_idle = false;
        dispatch();
    }
    while (!s_idle) {
        int64_t left = give_up - sim_os_now_us();
        if (left <= 0) {
            break;
        }
        sim_cond_wait(&s_idle_cv, &s_m, left);
    }
    bool idle = s_idle;
    sim_mutex_unlock(&s_m);
    return idle;
}

const sim_sched_task_t *sim_sched_current(void)
{
    return s_cur;
}
