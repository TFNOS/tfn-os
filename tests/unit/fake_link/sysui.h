// The link test: a small copy of the system screens of the OS (main/sysui.h)
#pragma once
#include <stdbool.h>

void sysui_message(const char *utf8, int secs);
void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int));
void sysui_ask_close(void);
bool sysui_active(void);
