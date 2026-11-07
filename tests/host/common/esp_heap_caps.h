#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
static inline void *heap_caps_aligned_alloc(size_t a, size_t n, unsigned c) { (void)a; (void)c; return malloc(n); }
static inline void *heap_caps_malloc(size_t n, unsigned c) { (void)c; return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t s, unsigned c) { (void)c; return calloc(n, s); }
static inline size_t heap_caps_get_free_size(unsigned c) { (void)c; return 100000; }
static inline size_t heap_caps_get_largest_free_block(unsigned c) { (void)c; return 100000; }
static inline void heap_caps_free(void *p) { free(p); }
typedef void (*esp_alloc_failed_hook_t)(size_t size, unsigned caps, const char *fn);
static inline int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t f) { (void)f; return 0; }
