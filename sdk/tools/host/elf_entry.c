// TFN OS SDK: the entry of an app file (sdk/tools/mk_app.py adds it to the sources of the app). The loader
// (main/tfn_app.c) calls app_main(argc, argv) after it links the file: argv[0] points to the const app_t * that the
// board gets, argv[1] is the ABI version of the firmware. APP_SYM is the app_t of the app (-DAPP_SYM=app_dive).
//
// The constructors (C++ static objects, __attribute__((constructor))): the loader places only .text, .rodata, .data,
// .data.rel.ro and .bss, so the link script of mk_app.py (sdk/tools/host/app.ld) puts .ctors and .init_array inside
// .data.rel.ro, between these names; the entry runs them before it gives the app.
#include <stdint.h>
#include "tfn/app.h"

extern const app_t APP_SYM;
extern const uintptr_t __tfn_ctors_start[] __attribute__((weak, visibility("hidden")));
extern const uintptr_t __tfn_ctors_end[] __attribute__((weak, visibility("hidden")));
extern const uintptr_t __tfn_init_start[] __attribute__((weak, visibility("hidden")));
extern const uintptr_t __tfn_init_end[] __attribute__((weak, visibility("hidden")));

__attribute__((visibility("default"))) int app_main(int argc, char *argv[])
{
    // .ctors from the end (the GCC order), .init_array from the start
    for (const uintptr_t *p = __tfn_ctors_end; p && p > __tfn_ctors_start;) {
        uintptr_t f = *--p;
        if (f && f != (uintptr_t)-1) ((void (*)(void))f)();
    }
    for (const uintptr_t *p = __tfn_init_start; p && p < __tfn_init_end; p++)
        if (*p && *p != (uintptr_t)-1) ((void (*)(void))*p)();
    if (argc > 0 && argv[0]) *(const app_t **)argv[0] = &APP_SYM;
    return 0;
}
