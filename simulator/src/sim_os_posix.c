// The simulator: the calls of sim_os.h on Linux and macOS (POSIX threads and files).
#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include "sim_os.h"

static int64_t s_t0;

static int64_t mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void sim_os_init(void)
{
    s_t0 = mono_us();
}

// ---------------------------------------------------------------- the locks
void sim_mutex_init(sim_mutex_t *m)
{
    pthread_mutex_init(m, NULL);
}

void sim_mutex_lock(sim_mutex_t *m)
{
    pthread_mutex_lock(m);
}

void sim_mutex_unlock(sim_mutex_t *m)
{
    pthread_mutex_unlock(m);
}

void sim_cond_init(sim_cond_t *c)
{
    pthread_cond_init(c, NULL);
}

bool sim_cond_wait(sim_cond_t *c, sim_mutex_t *m, int64_t timeout_us)
{
    if (timeout_us < 0) {
        pthread_cond_wait(c, m);
        return true;
    }
    // (pthread_cond_timedwait takes a time of CLOCK_REALTIME)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t ns = (int64_t)ts.tv_nsec + timeout_us % 1000000 * 1000;
    ts.tv_sec += (time_t)(timeout_us / 1000000 + ns / 1000000000);
    ts.tv_nsec = (long)(ns % 1000000000);
    return pthread_cond_timedwait(c, m, &ts) != ETIMEDOUT;
}

void sim_cond_wake_all(sim_cond_t *c)
{
    pthread_cond_broadcast(c);
}

// ---------------------------------------------------------------- the threads
typedef struct {
    void (*fn)(void *);
    void *arg;
} thread_start_t;

static void *thread_thunk(void *p)
{
    thread_start_t s = *(thread_start_t *)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

bool sim_thread_start(void (*fn)(void *), void *arg, size_t stack_bytes, int prio)
{
    (void)prio;                             // (a higher priority needs rights that a user does not have)
    thread_start_t *s = malloc(sizeof *s);
    if (!s) {
        return false;
    }
    s->fn = fn;
    s->arg = arg;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, stack_bytes);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    int rc = pthread_create(&t, &at, thread_thunk, s);
    pthread_attr_destroy(&at);
    if (rc) {
        free(s);
        return false;
    }
    return true;
}

void sim_thread_exit(void)
{
    pthread_exit(NULL);
}

void sim_os_park(void)
{
    for (;;) {
        pause();
    }
}

// ---------------------------------------------------------------- the clock
int64_t sim_os_now_us(void)
{
    return mono_us() - s_t0;
}

void sim_os_sleep_us(int64_t us)
{
    if (us <= 0) {
        sched_yield();
        return;
    }
    struct timespec ts = {(time_t)(us / 1000000), (long)(us % 1000000 * 1000)};
    while (nanosleep(&ts, &ts) && errno == EINTR) {
        continue;
    }
}

void sim_os_yield(void)
{
    sched_yield();
}

// ---------------------------------------------------------------- the files
FILE *sim_os_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int sim_os_stat(const char *path, struct stat *st)
{
    return stat(path, st);
}

int sim_os_mkdir(const char *path)
{
    return mkdir(path, 0777);
}

int sim_os_rmdir(const char *path)
{
    return rmdir(path);
}

int sim_os_unlink(const char *path)
{
    return unlink(path);
}

bool sim_os_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int sim_os_rename(const char *from, const char *to, bool replace)
{
    struct stat st;
    if (!replace && stat(to, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    return rename(from, to);
}

uint64_t sim_os_dir_bytes(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    uint64_t sum = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        char sub[4096];
        snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(sub, &st) != 0) {
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            sum += ((uint64_t)st.st_size + 4095) / 4096 * 4096;
        } else if (depth > 0) {
            sum += 4096 + sim_os_dir_bytes(sub, depth - 1);
        }
    }
    closedir(d);
    return sum;
}

// ---------------------------------------------------------------- the memory
void *sim_os_pages(size_t n)
{
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

void sim_os_pages_off(void *p, size_t n)
{
    mprotect(p, n, PROT_NONE);     // (the addresses stay mapped: an access is a fault)
    madvise(p, n, MADV_DONTNEED);  // (the memory goes back to the system)
}

size_t sim_os_heap_size(void *p)
{
#ifdef __APPLE__
    return malloc_size(p);
#else
    return malloc_usable_size(p);
#endif
}
#endif
