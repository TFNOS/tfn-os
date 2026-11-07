// The simulator: a fault of the firmware (an access violation, a division by zero, abort(), a failed assert()) stops
// the simulated board, not the window. The log gets the fault, the app that was open and the stack with the names of
// the functions and the lines (DbgHelp reads tfnsim.pdb); the thread of the fault stops for good, the state becomes
// SIM_HALT, and the window shows the reason. (A fault in the thread of the window itself still ends the program.)
// This file is for Windows; sim_crash_posix.c is the same for Linux and macOS.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim.h"

static const char *TAG = "fault";
static DWORD s_ui_thread;               // the thread of the window (tfnsim_start)
static volatile LONG s_in_fault;

static const char *code_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "an access violation (a bad pointer)";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "an array bound";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "an integer division by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "an illegal instruction (a trap)";
    case EXCEPTION_STACK_OVERFLOW: return "a stack overflow";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "a misaligned access";
    case EXCEPTION_PRIV_INSTRUCTION: return "a privileged instruction";
    default: return "an exception";
    }
}

static void sym_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    // the search path: the folder of this DLL (tfnsim.pdb is next to it)
    char dir[MAX_PATH] = "";
    HMODULE me = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)&sym_init, &me) && GetModuleFileNameA(me, dir, sizeof dir)) {
        char *slash = strrchr(dir, '\\');
        if (slash) *slash = 0;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), dir[0] ? dir : NULL, TRUE);
}

// one line of the stack: "  app_chess.c:812 chess_update (tfnsim.dll+0x1a2b3)"
static void frame_line(DWORD64 pc, char *out, int len)
{
    HANDLE p = GetCurrentProcess();
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *s = (SYMBOL_INFO *)buf;
    memset(buf, 0, sizeof buf);
    s->SizeOfStruct = sizeof(SYMBOL_INFO);
    s->MaxNameLen = 255;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line = {.SizeOfStruct = sizeof(IMAGEHLP_LINE64)};
    DWORD ldisp = 0;
    HMODULE mod = NULL;
    char mname[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)pc, &mod)) {
        GetModuleFileNameA(mod, mname, sizeof mname);
    }
    const char *base = strrchr(mname, '\\');
    base = base ? base + 1 : mname;
    bool have_sym = SymFromAddr(p, pc, &disp, s);
    bool have_line = SymGetLineFromAddr64(p, pc, &ldisp, &line);
    const char *file = have_line ? line.FileName : NULL;
    if (file) {
        const char *f = strrchr(file, '\\');
        const char *g = strrchr(file, '/');
        file = f > g ? f + 1 : g ? g + 1 : file;
    }
    snprintf(out, (size_t)len, "  %s%s%.0u %s (%s+0x%llx)", file ? file : "", file ? ":" : "",
             file ? (unsigned)line.LineNumber : 0u, have_sym ? s->Name : "?", base,
             (unsigned long long)(pc - (DWORD64)(uintptr_t)mod));
}

static void log_stack(CONTEXT *ctx)
{
    sym_init();
    CONTEXT c = *ctx;
    STACKFRAME64 f = {0};
    f.AddrPC.Offset = c.Rip;
    f.AddrPC.Mode = AddrModeFlat;
    f.AddrFrame.Offset = c.Rbp;
    f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = c.Rsp;
    f.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 16; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &f, &c, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL) || !f.AddrPC.Offset) break;
        char line[512];
        frame_line(f.AddrPC.Offset, line, sizeof line);
        sim_log(ESP_LOG_ERROR, TAG, "%s", line);
    }
}

static LONG WINAPI on_fault(EXCEPTION_POINTERS *e)
{
    if (GetCurrentThreadId() == s_ui_thread || InterlockedExchange(&s_in_fault, 1)) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    char what[120];
    snprintf(what, sizeof what, "%s (0x%08lx)", code_name(code), (unsigned long)code);
    sim_log(ESP_LOG_ERROR, TAG, "THE FIRMWARE STOPPED: %s, task %s", what, pcTaskGetName(NULL));
    log_stack(e->ContextRecord);
    sim_crash_stop_board(what);
    s_in_fault = 0;
    sim_park();
}

static void on_abort(int sig)
{
    (void)sig;
    if (GetCurrentThreadId() == s_ui_thread) return;
    sim_log(ESP_LOG_ERROR, TAG, "THE FIRMWARE STOPPED: abort() (a failed assert() or a check of the code)");
    CONTEXT c;
    RtlCaptureContext(&c);
    log_stack(&c);
    sim_crash_stop_board("abort()");
    sim_park();
}

void sim_crash_init(void)
{
    s_ui_thread = GetCurrentThreadId();
    SetUnhandledExceptionFilter(on_fault);
    signal(SIGABRT, on_abort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}
#endif
