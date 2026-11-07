// The simulator: the parts of the operating system of the PC that it uses. sim_os_win.c has them for Windows,
// sim_os_posix.c for Linux and macOS. The other files of simulator/src/ use only these calls (not the API of one
// operating system), so the same simulator builds on each of them.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef _WIN32
// (the same memory layout as SRWLOCK and CONDITION_VARIABLE of windows.h: zero is the start state)
typedef struct { void *p; } sim_mutex_t;
typedef struct { void *p; } sim_cond_t;
#define SIM_MUTEX_INIT {0}
#define SIM_COND_INIT {0}
#else
#include <pthread.h>
typedef pthread_mutex_t sim_mutex_t;
typedef pthread_cond_t sim_cond_t;
#define SIM_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define SIM_COND_INIT PTHREAD_COND_INITIALIZER
#endif

// The start of the simulator: the timers of the operating system
void sim_os_init(void);

// A lock (not recursive) and a condition. sim_cond_wait() waits at most timeout_us (< 0: no limit) with the lock held;
// the result is false at the timeout. A wait can also end with no wake: the caller checks its condition again.
void sim_mutex_init(sim_mutex_t *m);
void sim_mutex_lock(sim_mutex_t *m);
void sim_mutex_unlock(sim_mutex_t *m);
void sim_cond_init(sim_cond_t *c);
bool sim_cond_wait(sim_cond_t *c, sim_mutex_t *m, int64_t timeout_us);
void sim_cond_wake_all(sim_cond_t *c);

// A thread: fn(arg) runs with a stack of stack_bytes. A high priority (the sound) gets a higher priority of the
// operating system where it can.
enum { SIM_PRIO_NORMAL, SIM_PRIO_ABOVE, SIM_PRIO_HIGH };
bool sim_thread_start(void (*fn)(void *), void *arg, size_t stack_bytes, int prio);
// The calling thread ends now
void sim_thread_exit(void) __attribute__((noreturn));
// The calling thread stops for good (it never runs again)
void sim_os_park(void) __attribute__((noreturn));

// The real clock of the PC from the start (monotonic), a real sleep, and a yield of the CPU
int64_t sim_os_now_us(void);
void sim_os_sleep_us(int64_t us);
void sim_os_yield(void);

// The files of the PC: the paths are UTF-8 with '/' separators
FILE *sim_os_fopen(const char *path, const char *mode);
int sim_os_stat(const char *path, struct stat *st);
int sim_os_mkdir(const char *path);
int sim_os_rmdir(const char *path);
int sim_os_unlink(const char *path);
bool sim_os_is_dir(const char *path);
// A rename. replace false: the rename fails when the target exists (as on FAT).
int sim_os_rename(const char *from, const char *to, bool replace);
// The space of the files in a folder (and its sub-folders, at most depth levels), in clusters of 4 KB
uint64_t sim_os_dir_bytes(const char *dir, int depth);

// The memory: sim_os_pages() gives n bytes (read and write, zero) at the start of new pages (NULL: none);
// sim_os_pages_off() makes these pages unusable and keeps their addresses, so each later access is a fault and no new
// block gets these addresses (the guard of the app memory, sim_app_mem.c). sim_os_heap_size(): the size of a block of
// malloc().
void *sim_os_pages(size_t n);
void sim_os_pages_off(void *p, size_t n);
size_t sim_os_heap_size(void *p);
