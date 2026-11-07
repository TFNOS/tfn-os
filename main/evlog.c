// The event log of the Tab5 (evlog.h). The lines are in a ring of EV_N slots of EV_W bytes in PSRAM (about 1.5 hours
// of the second lines and the log). A line starts with the wall clock time (HH:MM:SS.mmm, the time zone of the board)
// when the clock is set, else with the ms since the start.
#include "sdkconfig.h"
#include "board.h"
#if BOARD_EVLOG
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "evlog.h"

#define EV_N 12000
#define EV_W 112

static char *s_ring;
static volatile size_t s_next;  // the line number of the next line (the slot = s_next % EV_N)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev;

static void put_line(const char *text, size_t n)
{
    if (!s_ring) {
        return;
    }
    char stamp[24];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec > 1700000000) {  // (the clock is set: the wall time)
        struct tm tm;
        time_t t = tv.tv_sec;
        localtime_r(&t, &tm);
        snprintf(stamp, sizeof stamp, "%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec,
                 (int)(tv.tv_usec / 1000));
    } else {
        snprintf(stamp, sizeof stamp, "+%lld ", (long long)(esp_timer_get_time() / 1000));
    }
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        n--;
    }
    if (!n) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    size_t k = s_next++ % EV_N;
    taskEXIT_CRITICAL(&s_mux);
    char *d = s_ring + k * EV_W;
    size_t sl = strlen(stamp);
    memcpy(d, stamp, sl);
    if (n > EV_W - 1 - sl) {
        n = EV_W - 1 - sl;
    }
    memcpy(d + sl, text, n);
    d[sl + n] = 0;
}

// each ESP_LOG line goes to the ring too (then to the console as before)
static int log_hook(const char *fmt, va_list ap)
{
    char b[EV_W];
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(b, sizeof b, fmt, cp);
    va_end(cp);
    // (the colour codes of the console out)
    const char *t = b;
    if (t[0] == '\033') {
        const char *m = strchr(t, 'm');
        if (m) {
            t = m + 1;
        }
    }
    if (n > 0) {
        put_line(t, strnlen(t, sizeof b - (size_t)(t - b)));
    }
    return s_prev ? s_prev(fmt, ap) : vprintf(fmt, ap);
}

void evlog_init(void)
{
    if (s_ring) {
        return;
    }
    s_ring = heap_caps_calloc(EV_N, EV_W, MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        return;
    }
    s_prev = esp_log_set_vprintf(log_hook);
    evlog_add("evlog: start");
}

void evlog_add(const char *fmt, ...)
{
    char b[EV_W];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0) {
        put_line(b, strnlen(b, sizeof b));
    }
}

size_t evlog_count(void)
{
    return s_next;
}

size_t evlog_read(size_t *pos, char *out, size_t max)
{
    if (!s_ring || !max) {
        return 0;
    }
    size_t end = s_next, first = end > EV_N ? end - EV_N : 0, p = *pos < first ? first : *pos, used = 0;
    while (p < end) {
        const char *l = s_ring + (p % EV_N) * EV_W;
        size_t n = strnlen(l, EV_W - 1);
        if (used + n + 1 > max) {
            break;
        }
        memcpy(out + used, l, n);
        used += n;
        out[used++] = '\n';
        p++;
    }
    *pos = p;
    return used;
}

// GET /api/evlog[?n=lines]: the last n lines (all that are kept: no n), as text, in parts
static esp_err_t api_evlog(httpd_req_t *req)
{
    size_t n = EV_N;
    char q[32], v[12];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK &&
        httpd_query_key_value(q, "n", v, sizeof v) == ESP_OK) {
        n = (size_t)strtoul(v, NULL, 10);
    }
    size_t end = s_next, pos = end > n ? end - n : 0;
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    char *b = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!b) {
        return httpd_resp_send_500(req);
    }
    for (;;) {
        size_t k = evlog_read(&pos, b, 8192);
        if (!k) {
            break;
        }
        if (httpd_resp_send_chunk(req, b, (ssize_t)k) != ESP_OK) {
            break;
        }
    }
    free(b);
    return httpd_resp_send_chunk(req, NULL, 0);
}

void evlog_register(void *httpd)
{
    httpd_uri_t u = {.uri = "/api/evlog", .method = HTTP_GET, .handler = api_evlog};
    httpd_register_uri_handler((httpd_handle_t)httpd, &u);
}
#endif
