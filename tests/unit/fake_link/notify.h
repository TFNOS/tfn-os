// The link test: a small copy of the notices of the OS (main/notify.h)
#pragma once
#include <stdint.h>

#define NT_TITLE 72
#define NT_TEXT  200
uint32_t notify_post(const char *app, const char *title, const char *text, const char *link);
void notify_remove(uint32_t id);
