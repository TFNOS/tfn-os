// The test of /api/pub: a small copy of the system layer (main/sysui.h): the prompt of a key that waits
#pragma once
#include <stdbool.h>

void sysui_message(const char *utf8, int secs);
void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int));
void sysui_ask_close(void);
void sysui_ask_finger(void);
bool sysui_active(void);
