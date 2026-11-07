// The test of the question of the system layer: a small copy of the ESP-IDF heap (the PSRAM is the normal heap)
#pragma once
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM         (1 << 10)
#define MALLOC_CAP_INTERNAL       (1 << 11)
#define heap_caps_malloc(n, caps) ((void)(caps), malloc(n))
#define heap_caps_free(p)         free(p)
// (SETUP shows the free memory)
static inline size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return 100000;
}
