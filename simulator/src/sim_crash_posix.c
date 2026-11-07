// The simulator: a fault of the firmware on Linux and macOS (a bad pointer, a division by zero, an illegal
// instruction, abort() or a failed assert()) stops the simulated board, not the program. The log gets the fault, the
// task and the stack (backtrace(): the names of the exported functions, and the offsets in libtfnsim for addr2line);
// the thread of the fault stops for good and the state becomes SIM_HALT. A fault in the thread of the window (the
// thread of tfnsim_start) still ends the program.
#ifndef _WIN32
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim.h"

static const char *TAG = "fault";
static pthread_t s_ui_thread;

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "an access violation (a bad pointer)";
    case SIGBUS:
        return "a bus error (a bad or misaligned pointer)";
    case SIGFPE:
        return "an arithmetic fault (an integer division by zero)";
    case SIGILL:
        return "an illegal instruction (a trap)";
    case SIGABRT:
        return "abort() (a failed assert() or a check of the code)";
    default:
        return "a signal";
    }
}

static void log_stack(void)
{
    void *pcs[24];
    int n = backtrace(pcs, 24);
    char **names = backtrace_symbols(pcs, n);
    for (int i = 2; i < n; i++) {               // (not the handler and the frame of the signal)
        sim_log(ESP_LOG_ERROR, TAG, "  %s", names ? names[i] : "?");
    }
    free(names);
}

static void on_signal(int sig)
{
    if (pthread_equal(pthread_self(), s_ui_thread)) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    char what[120];
    snprintf(what, sizeof what, "%s (signal %d)", signal_name(sig), sig);
    sim_log(ESP_LOG_ERROR, TAG, "THE FIRMWARE STOPPED: %s, task %s", what, pcTaskGetName(NULL));
    log_stack();
    sim_crash_stop_board(what);
    sim_park();
}

void sim_crash_init(void)
{
    s_ui_thread = pthread_self();
    static const int SIGS[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;                   // (a parked thread keeps the signal blocked otherwise)
    for (size_t i = 0; i < sizeof SIGS / sizeof SIGS[0]; i++) {
        sigaction(SIGS[i], &sa, NULL);
    }
}
#endif
