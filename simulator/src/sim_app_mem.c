// The simulator: the memory calls of the app files (sim_app_mem.h sends them here). Before a block of an app goes, the
// free checks the sounds that use it (audio_mem_check of main/audio.c) and stops them, as the loader of the board does
// on a firmware with ABI rev 24 or later (main/tfn_app.c). So a sound never reads the freed memory of an app here
// either, also when the audio task is late.
//
// For the checks of simulator/sound_check.py, tfnsim_app_mem() sets:
//   - the free hook: on (the default) = the stop above; off = a firmware before ABI rev 24: the free only counts the
//     sounds that still use the block (each one then reads the freed block);
//   - the guard: each block of at least guard_min bytes gets pages of its own, and its free makes these pages unusable
//     (sim_os_pages_off), so a read after the free is a fault at once (as a page heap). 0 = off. Only for the test of
//     one app: a block that an app gives to a module of the firmware to free (for example tfsfx_load_mem) must not be a
//     guard block, because the module frees it with the free of the C library.
// tfnsim_app_mem_found() counts the frees that found a sound on the block; tfnsim_app_mem_bad() counts the stops that
// did not clear the block (a fault of audio_mem_check).
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "esp_log.h"
#include "sim.h"

static const char *TAG = "simmem";

static volatile bool s_hook = true;
static volatile size_t s_guard_min;
static volatile uint32_t s_found, s_bad;

// ---------------------------------------------------------------- the guard blocks
// their address and size (open addressing; a free leaves the mark GONE, a new block can take its place)
#define GUARD_CAP  4096
#define GUARD_GONE ((void *)1)
static struct {
    void *p;
    size_t n;
} s_guard[GUARD_CAP];
static sim_mutex_t s_guard_mx = SIM_MUTEX_INIT;

static uint32_t guard_hash(const void *p)
{
    return (uint32_t)(((uintptr_t)p >> 12) * 2654435761u) & (GUARD_CAP - 1);
}

static bool guard_add(void *p, size_t n)
{
    bool ok = false;
    sim_mutex_lock(&s_guard_mx);
    uint32_t h = guard_hash(p);
    for (int i = 0; i < GUARD_CAP; i++, h = (h + 1) & (GUARD_CAP - 1)) {
        if (!s_guard[h].p || s_guard[h].p == GUARD_GONE) {
            s_guard[h].p = p;
            s_guard[h].n = n;
            ok = true;
            break;
        }
    }
    sim_mutex_unlock(&s_guard_mx);
    return ok;
}

// true when p is a guard block (its size in *n); take = true: it leaves the list
static bool guard_find(void *p, size_t *n, bool take)
{
    bool found = false;
    sim_mutex_lock(&s_guard_mx);
    uint32_t h = guard_hash(p);
    for (int i = 0; i < GUARD_CAP && s_guard[h].p; i++, h = (h + 1) & (GUARD_CAP - 1)) {
        if (s_guard[h].p == p) {
            *n = s_guard[h].n;
            if (take) {
                s_guard[h].p = GUARD_GONE;
            }
            found = true;
            break;
        }
    }
    sim_mutex_unlock(&s_guard_mx);
    return found;
}

// ---------------------------------------------------------------- the free hook
// the sounds that use the block before it goes: with the hook they stop, without it they only count
static void sound_check(void *p, size_t n)
{
    const bool hook = s_hook;
    const int k = audio_mem_check(p, n, hook);
    if (!k) {
        return;
    }
    __atomic_fetch_add(&s_found, 1, __ATOMIC_SEQ_CST);
    ESP_LOGW(TAG, "an app frees a block of %u bytes that %d sound(s) still use: %s", (unsigned)n, k,
             hook ? "they stop (audio_stop_mem, as the loader of the board)" : "no stop (the free hook is off)");
    if (hook && audio_mem_check(p, n, false)) {
        __atomic_fetch_add(&s_bad, 1, __ATOMIC_SEQ_CST);
        ESP_LOGE(TAG, "audio_mem_check did not stop each sound of the block");
    }
}

// ---------------------------------------------------------------- the calls of the apps
void *sim_app_malloc(size_t n)
{
    const size_t g = s_guard_min;
    if (g && n >= g) {
        void *p = sim_os_pages(n);
        if (p && guard_add(p, n)) {
            return p;
        }
        if (p) {
            sim_os_pages_off(p, n);  // (the list is full: these pages stay unusable, the block comes from the heap)
        }
    }
    return malloc(n);
}

void *sim_app_calloc(size_t a, size_t b)
{
    if (b && a > SIZE_MAX / b) {
        return NULL;
    }
    const size_t g = s_guard_min;
    if (g && a * b >= g) {
        return sim_app_malloc(a * b);  // (new pages are zero)
    }
    return calloc(a, b);
}

void sim_app_free(void *p)
{
    if (!p) {
        return;
    }
    size_t n = 0;
    const bool guard = guard_find(p, &n, true);
    sound_check(p, guard ? n : sim_os_heap_size(p));
    if (guard) {
        sim_os_pages_off(p, n);
    } else {
        free(p);
    }
}

void *sim_app_realloc(void *p, size_t n)
{
    if (!p) {
        return sim_app_malloc(n);
    }
    size_t old = 0;
    if (guard_find(p, &old, false)) {
        if (!n) {
            sim_app_free(p);
            return NULL;
        }
        void *q = sim_app_malloc(n);
        if (!q) {
            return NULL;  // (the old block stays)
        }
        memcpy(q, p, old < n ? old : n);
        sim_app_free(p);
        return q;
    }
    sound_check(p, sim_os_heap_size(p));  // (realloc can move the block or free it)
    return realloc(p, n);
}

// ---------------------------------------------------------------- the interface of the checks (tfnsim.py)
SIM_API void tfnsim_app_mem(int hook, int guard_min)
{
    s_hook = hook != 0;
    s_guard_min = guard_min > 0 ? (size_t)guard_min : 0;
}

SIM_API int tfnsim_app_mem_found(void)
{
    return (int)__atomic_load_n(&s_found, __ATOMIC_SEQ_CST);
}

SIM_API int tfnsim_app_mem_bad(void)
{
    return (int)__atomic_load_n(&s_bad, __ATOMIC_SEQ_CST);
}
