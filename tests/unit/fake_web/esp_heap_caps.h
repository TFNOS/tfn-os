// A fake of the ESP-IDF heap for the host test of main/tfn_web.c: the C library heap.
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM             1
#define MALLOC_CAP_INTERNAL           2
#define heap_caps_malloc(n, caps)     ((void)(caps), malloc(n))
#define heap_caps_calloc(k, n, caps)  ((void)(caps), calloc((k), (n)))
#define heap_caps_realloc(p, n, caps) ((void)(caps), realloc((p), (n)))
#define heap_caps_free(p)             free(p)
