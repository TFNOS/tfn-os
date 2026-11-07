// TFN OS SDK: the entry of the watcher of an app (watch.elf; main/watch.h). sdk/tools/mk_app.py adds it to the watch
// sources of the app. The loader (main/tfn_app.c) calls watch_main(argc, argv) on the watch task after it links the
// file: argv[0] points to the const watch_t * that the board gets, argv[1] is the ABI version of the firmware. WATCH_SYM
// is the watch_t of the app (-DWATCH_SYM=watch_mail). The constructors run first, as in elf_entry.c.
#include <stdint.h>
#include "tfn/watch.h"

extern const watch_t WATCH_SYM;
extern const uintptr_t __tfn_ctors_start[] __attribute__((weak, visibility("hidden")));
extern const uintptr_t __tfn_ctors_end[] __attribute__((weak, visibility("hidden")));
extern const uintptr_t __tfn_init_start[] __attribute__((weak, visibility("hidden")));
extern const uintptr_t __tfn_init_end[] __attribute__((weak, visibility("hidden")));

__attribute__((visibility("default"))) int watch_main(int argc, char *argv[])
{
    for (const uintptr_t *p = __tfn_ctors_end; p && p > __tfn_ctors_start;) {
        uintptr_t f = *--p;
        if (f && f != (uintptr_t)-1) ((void (*)(void))f)();
    }
    for (const uintptr_t *p = __tfn_init_start; p && p < __tfn_init_end; p++)
        if (*p && *p != (uintptr_t)-1) ((void (*)(void))*p)();
    if (argc > 0 && argv[0]) *(const watch_t **)argv[0] = &WATCH_SYM;
    return 0;
}
