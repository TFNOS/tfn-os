// The web server of the board (inside the OS): the start and the list of the modules that add their routes.
// wifi.c starts it when the board is on a WiFi network or the hotspot is on.
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

// The start of the server and the routes of all the modules, in the order of WEB_MODULES (web_server.c). A second
// call does nothing. An error: the server did not start (the caller tries again later).
esp_err_t web_server_start(void);

// The server, or NULL before the start.
httpd_handle_t web_server_handle(void);
