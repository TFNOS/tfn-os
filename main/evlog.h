#pragma once
// The event log of the Tab5 (it helps to find a cyan flicker after a wake): a ring of text lines in PSRAM with the wall
// clock time. It keeps each ESP_LOG line, the lines of evlog_add() and one line of display numbers each second
// (board_tab5.c). Find the time of a fault in it; the console "evlog [n]" or the web /api/evlog gives the lines.
// On the other boards all of it is empty (no RAM, no hook).
#include <stdbool.h>
#include <stddef.h>
#include "board.h"

#if BOARD_EVLOG
void evlog_init(void);  // the ring and the log hook (early in app_main)
void evlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Copy lines to out: the lines from line number *pos (0 = the oldest kept); *pos moves on. The result: the bytes.
size_t evlog_read(size_t *pos, char *out, size_t max);
size_t evlog_count(void);          // the line number of the next line (all lines since the start)
void evlog_register(void *httpd);  // GET /api/evlog[?n=lines] (text)
#else
static inline void evlog_init(void)
{
}
static inline void evlog_add(const char *fmt, ...)
{
    (void)fmt;
}
static inline size_t evlog_read(size_t *pos, char *out, size_t max)
{
    (void)pos;
    (void)out;
    (void)max;
    return 0;
}
static inline size_t evlog_count(void)
{
    return 0;
}
static inline void evlog_register(void *httpd)
{
    (void)httpd;
}
#endif
