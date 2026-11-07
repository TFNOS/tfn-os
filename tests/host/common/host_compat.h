// The C library differences of the host builds (tests/host/build.sh puts this file before each source with -include).
// The firmware C library (newlib) and MinGW do not give these faults; glibc (Linux) and the macOS library do:
// - stdlib.h of glibc also declares the POSIX type mode_t, and an app can use the name mode_t for its own enum. The
//   system headers that use mode_t come first here; after them, the name mode_t of the app sources is a new name.
// - Some apps call abs() with no include of stdlib.h (the firmware headers give it).
// - The host branch of some apps calls mkdir(path) with one argument (the MinGW form); POSIX mkdir() has two.
#ifndef HOST_COMPAT_H
#define HOST_COMPAT_H
#include <stdlib.h>
#if !defined(_WIN32) && !defined(__cplusplus)
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#define mode_t host_app_mode_t
static inline int host_mkdir1(const char *path)
{
    return mkdir(path, 0777);
}
#define HOST_MKDIR_PICK(a, b, name, ...) name
#define mkdir(...) HOST_MKDIR_PICK(__VA_ARGS__, mkdir, host_mkdir1)(__VA_ARGS__)
#endif
#endif
