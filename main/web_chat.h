#pragma once
// The settings of CHAT (app_chat.c): the address of the assistant gateway (/xiaozhi) and a second address
// for the VPN (the hub of the manager). NVS namespace "chat", keys "url" and "url2". The board page /chat and the API
// /api/chat[?url=..&url2=..] (web_chat.c). The defaults come from main/chat_local.h when it exists (not in git: the
// home address), else empty.
// An extra (CONFIG_TFN_EXTRA_CHAT): when it is off, chat_open_and_talk() is empty.
#include <stddef.h>
#include "tfn_extras.h"

#define WEB_CHAT_URL_MAX 96
// the two addresses now (the saved ones, else the defaults)
void chat_urls(char *url, size_t n1, char *url2, size_t n2);
// web: the page and the API (wifi.c)
void web_chat_register(void *httpd);
// app_chat.c: open CHAT and listen at once (the BOOT key of the 3.49); in CHAT: the same as a tap
#if CONFIG_TFN_EXTRA_CHAT
void chat_open_and_talk(void);
#else
static inline void chat_open_and_talk(void)
{
}
#endif
