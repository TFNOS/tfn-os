#pragma once
// THE WEB PASSWORD: with a password set (SETUP > WEB PASSWORD, or the console "webpass"), each page, API
// and WebSocket of the board web server needs it; with no password the server is open. A browser logs in
// at /login (a session cookie); a script sends "Authorization: Basic <base64 of :password>" (curl -u :password).
// main/web_auth.c gates every handler of the server (the link wraps httpd_register_uri_handler), so a new handler
// is behind the password with no change; web_auth_core.c has the sessions and the limit of the tries.
#include <stdbool.h>

#define WEB_PASS_MIN 4
#define WEB_PASS_MAX 63

void web_auth_init(void);                         // main.c, at the start: the state from NVS
bool web_auth_on(void);                           // a password is set
bool web_auth_check_local(const char *password);  // verify, with throttling; never changes the saved password
// A new password (WEB_PASS_MIN .. WEB_PASS_MAX characters), or "" = no password. The logged-in browsers and the open
// connections end. False and the reason in msg for a bad password.
bool web_auth_set(const char *pw, char *msg, int len);
// wifi.c http_start (server: the httpd_handle_t): the pages /login and /logout (the only ones with no password)
void web_auth_register(void *server);
