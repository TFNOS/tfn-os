// The simulator: the calls of sim_os.h on Windows (Win32 and the UCRT). The paths are UTF-8; the file calls of Windows
// get them as UTF-16.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <process.h>
#include <direct.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "sim_os.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

_Static_assert(sizeof(sim_mutex_t) == sizeof(SRWLOCK), "sim_mutex_t is an SRWLOCK");
_Static_assert(sizeof(sim_cond_t) == sizeof(CONDITION_VARIABLE), "sim_cond_t is a CONDITION_VARIABLE");

static LARGE_INTEGER s_qpf, s_qpc0;

void sim_os_init(void)
{
    QueryPerformanceFrequency(&s_qpf);
    QueryPerformanceCounter(&s_qpc0);
    timeBeginPeriod(1);
}

// ---------------------------------------------------------------- the locks
void sim_mutex_init(sim_mutex_t *m)
{
    InitializeSRWLock((PSRWLOCK)m);
}

void sim_mutex_lock(sim_mutex_t *m)
{
    AcquireSRWLockExclusive((PSRWLOCK)m);
}

void sim_mutex_unlock(sim_mutex_t *m)
{
    ReleaseSRWLockExclusive((PSRWLOCK)m);
}

void sim_cond_init(sim_cond_t *c)
{
    InitializeConditionVariable((PCONDITION_VARIABLE)c);
}

bool sim_cond_wait(sim_cond_t *c, sim_mutex_t *m, int64_t timeout_us)
{
    DWORD ms = INFINITE;
    if (timeout_us >= 0) {
        ms = (DWORD)((timeout_us + 999) / 1000);
    }
    if (SleepConditionVariableSRW((PCONDITION_VARIABLE)c, (PSRWLOCK)m, ms, 0)) {
        return true;
    }
    return GetLastError() != ERROR_TIMEOUT;
}

void sim_cond_wake_all(sim_cond_t *c)
{
    WakeAllConditionVariable((PCONDITION_VARIABLE)c);
}

// ---------------------------------------------------------------- the threads
typedef struct {
    void (*fn)(void *);
    void *arg;
} thread_start_t;

static unsigned __stdcall thread_thunk(void *p)
{
    thread_start_t s = *(thread_start_t *)p;
    free(p);
    s.fn(s.arg);
    return 0;
}

bool sim_thread_start(void (*fn)(void *), void *arg, size_t stack_bytes, int prio)
{
    thread_start_t *s = malloc(sizeof *s);
    if (!s) {
        return false;
    }
    s->fn = fn;
    s->arg = arg;
    uintptr_t h = _beginthreadex(NULL, (unsigned)stack_bytes, thread_thunk, s, CREATE_SUSPENDED, NULL);
    if (!h) {
        free(s);
        return false;
    }
    if (prio == SIM_PRIO_HIGH) {
        SetThreadPriority((HANDLE)h, THREAD_PRIORITY_HIGHEST);
    } else if (prio == SIM_PRIO_ABOVE) {
        SetThreadPriority((HANDLE)h, THREAD_PRIORITY_ABOVE_NORMAL);
    }
    ResumeThread((HANDLE)h);
    CloseHandle((HANDLE)h);
    return true;
}

void sim_thread_exit(void)
{
    _endthreadex(0);
    for (;;) {
        Sleep(INFINITE);
    }
}

void sim_os_park(void)
{
    for (;;) {
        Sleep(INFINITE);
    }
}

// ---------------------------------------------------------------- the clock
int64_t sim_os_now_us(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    int64_t d = c.QuadPart - s_qpc0.QuadPart;
    int64_t f = s_qpf.QuadPart;
    return d / f * 1000000 + d % f * 1000000 / f;
}

// A sleep with a high resolution timer of each thread (Windows 10 1803 and later), else Sleep() with 1 ms periods
static _Thread_local HANDLE t_timer;
static _Thread_local bool t_timer_tried;

void sim_os_sleep_us(int64_t us)
{
    if (us <= 0) {
        SwitchToThread();
        return;
    }
    if (!t_timer_tried) {
        t_timer_tried = true;
        t_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (t_timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(us * 10);          // (100 ns units, relative)
        if (SetWaitableTimer(t_timer, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(t_timer, INFINITE);
            return;
        }
    }
    Sleep((DWORD)((us + 999) / 1000));
}

void sim_os_yield(void)
{
    SwitchToThread();
}

// ---------------------------------------------------------------- the files
static bool wide(const char *path, wchar_t *w, int n)
{
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n) > 0;
}

FILE *sim_os_fopen(const char *path, const char *mode)
{
    wchar_t w[MAX_PATH];
    wchar_t wm[16];
    if (!wide(path, w, MAX_PATH) || !wide(mode, wm, 16)) {
        return NULL;
    }
    return _wfopen(w, wm);
}

// (the struct stat of the UCRT on x86-64 is struct _stat64i32)
_Static_assert(sizeof(struct stat) == sizeof(struct _stat64i32), "struct stat of the UCRT");

int sim_os_stat(const char *path, struct stat *st)
{
    char m[MAX_PATH * 2];
    wchar_t w[MAX_PATH];
    snprintf(m, sizeof m, "%s", path);
    size_t n = strlen(m);
    while (n > 3 && m[n - 1] == '/') {
        m[--n] = 0;                         // (Windows: "dir/" fails)
    }
    if (!wide(m, w, MAX_PATH)) {
        return -1;
    }
    return _wstat64i32(w, (struct _stat64i32 *)st);
}

int sim_os_mkdir(const char *path)
{
    wchar_t w[MAX_PATH];
    return wide(path, w, MAX_PATH) ? _wmkdir(w) : -1;
}

int sim_os_rmdir(const char *path)
{
    wchar_t w[MAX_PATH];
    return wide(path, w, MAX_PATH) ? _wrmdir(w) : -1;
}

int sim_os_unlink(const char *path)
{
    wchar_t w[MAX_PATH];
    return wide(path, w, MAX_PATH) ? _wunlink(w) : -1;
}

bool sim_os_is_dir(const char *path)
{
    wchar_t w[MAX_PATH];
    if (!wide(path, w, MAX_PATH)) {
        return false;
    }
    DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int sim_os_rename(const char *from, const char *to, bool replace)
{
    wchar_t a[MAX_PATH];
    wchar_t b[MAX_PATH];
    if (!wide(from, a, MAX_PATH) || !wide(to, b, MAX_PATH)) {
        return -1;
    }
    return MoveFileExW(a, b, replace ? MOVEFILE_REPLACE_EXISTING : 0) ? 0 : -1;
}

static uint64_t dir_bytes_w(const wchar_t *dir, int depth)
{
    wchar_t pat[MAX_PATH];
    _snwprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    uint64_t sum = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
            continue;
        }
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            uint64_t n = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            sum += (n + 4095) / 4096 * 4096;
        } else if (depth > 0) {
            wchar_t sub[MAX_PATH];
            _snwprintf(sub, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            sum += 4096 + dir_bytes_w(sub, depth - 1);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return sum;
}

uint64_t sim_os_dir_bytes(const char *dir, int depth)
{
    wchar_t w[MAX_PATH];
    return wide(dir, w, MAX_PATH) ? dir_bytes_w(w, depth) : 0;
}

// ---------------------------------------------------------------- the memory
void *sim_os_pages(size_t n)
{
    return VirtualAlloc(NULL, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void sim_os_pages_off(void *p, size_t n)
{
    VirtualFree(p, n, MEM_DECOMMIT);  // (the addresses stay reserved: an access is an access violation)
}

size_t sim_os_heap_size(void *p)
{
    return _msize(p);
}
#endif
