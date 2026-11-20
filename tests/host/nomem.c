// The failure hook of NOMEM=1 (tests/host/build.sh, common/host_compat.h): the test of the paths of a NULL result of an
// allocation (on the board a block of PSRAM can be missing after a long time of use). fuzz.c turns it on after the
// init of all apps (host_nomem_on). The environment:
//   FUZZ_NOMEM_NTH=<n>  the n-th allocation of FUZZ_NOMEM_MIN bytes or more fails, no other one;
//   FUZZ_NOMEM=<n>      else each such allocation fails with the chance n/1000 (FUZZ_NOMEM_SEED: the random start);
//   FUZZ_NOMEM_MIN      the smallest size that can fail (default 1024: the big blocks of the apps).
// With FUZZ_ONLY=<name> (one app) a fault of one app does not hide the faults of the next apps.
#undef malloc
#undef calloc
#undef realloc
#include <stdio.h>
#include <stdlib.h>

int host_nomem_on;
static int s_pct = -1;
static long s_nth, s_count;
static unsigned s_x;
static size_t s_min;

int host_nomem_fail(size_t n)
{
    if (s_pct < 0) {
        const char *e = getenv("FUZZ_NOMEM");
        s_pct = e ? atoi(e) : 0;
        e = getenv("FUZZ_NOMEM_SEED");
        s_x = e ? (unsigned)atoi(e) : 12345u;
        e = getenv("FUZZ_NOMEM_MIN");
        s_min = e ? (size_t)atol(e) : 1024;
        e = getenv("FUZZ_NOMEM_NTH");
        s_nth = e ? atol(e) : 0;
    }
    if (!host_nomem_on || n < s_min) {
        return 0;
    }
    if (s_nth > 0) {
        if (++s_count == s_nth) {
            fprintf(stderr, "(nomem: allocation %ld of %u bytes fails)\n", s_count, (unsigned)n);
            return 1;
        }
        return 0;
    }
    if (!s_pct) {
        return 0;
    }
    s_x = s_x * 1103515245u + 12345u;
    if ((int)((s_x >> 16) % 1000) < s_pct) {
        fprintf(stderr, "(nomem: an allocation of %u bytes fails)\n", (unsigned)n);
        return 1;
    }
    return 0;
}
