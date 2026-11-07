// The simulator: included before each C source of an app that a board loads as an app file (simulator/build.py,
// app_file_sources()). The loader of the board sends malloc, calloc, realloc and free of an app file (also
// heap_caps_malloc, heap_caps_calloc and heap_caps_free) to its own functions (main/tfn_app.c), and its free first
// stops each sound that uses the block (audio_stop_mem). These macros send the same names of the app to
// simulator/src/sim_app_mem.c, which does the same. (The heap_caps_* functions of tests/host/common/esp_heap_caps.h call
// malloc, calloc and free, so they come here too.)
//
// The macros are names, not calls: a pointer to free goes to the hook too, as the loader of the board sends each
// reference of an app file to its function, and a member with one of these names (allocator.free of
// third_party/simplewebp) gets the new name in its struct and in each use. The system headers come first (stdlib.h,
// and through it the declarations of malloc.h): their declarations keep the real names.
#pragma once
#include <stdlib.h>

void *sim_app_malloc(size_t n);
void *sim_app_calloc(size_t a, size_t b);
void *sim_app_realloc(void *p, size_t n);
void sim_app_free(void *p);

#define malloc  sim_app_malloc
#define calloc  sim_app_calloc
#define realloc sim_app_realloc
#define free    sim_app_free
