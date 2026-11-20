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
// NOMEM=1 of build.sh (HOST_NOMEM): malloc, calloc and realloc of the firmware and of the apps go through a failure
// hook (tests/host/nomem.c: FUZZ_NOMEM_NTH, FUZZ_NOMEM), for the test of the paths of a NULL result. The test frame
// (fuzz.c, stubs.c, nomem.c) uses the real functions: an #undef at its start.
#if defined(HOST_NOMEM) && !defined(__cplusplus)
int host_nomem_fail(size_t n);
static inline void *host_nm_malloc(size_t n)
{
    return host_nomem_fail(n) ? NULL : malloc(n);
}
static inline void *host_nm_calloc(size_t a, size_t b)
{
    return host_nomem_fail(a * b) ? NULL : calloc(a, b);
}
static inline void *host_nm_realloc(void *p, size_t n)
{
    return host_nomem_fail(n) ? NULL : realloc(p, n);
}
#define malloc(n)     host_nm_malloc(n)
#define calloc(a, b)  host_nm_calloc(a, b)
#define realloc(p, n) host_nm_realloc(p, n)
#endif
#endif
