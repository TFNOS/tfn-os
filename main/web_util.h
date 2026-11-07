// Small helpers of the web pages and the /api answers of the board web server.
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

// Sends one HTML page: the shared head of the board pages (wifi_send_page_head), then body, then the end.
esp_err_t web_send_page(httpd_req_t *req, const char *body);

// Sends the text s as one chunk of the answer. An empty text (or NULL) sends nothing: an empty chunk is the end of a
// chunked answer of the ESP-IDF web server, and the client would lose the rest of the answer. Use it for each text
// that can be empty; the end of the answer stays httpd_resp_sendstr_chunk(req, NULL).
esp_err_t web_chunk(httpd_req_t *req, const char *s);

// Sends s as a JSON string with its quotes, in chunks: " and \ get a \ before them, a control character becomes \u00XX.
void web_json_str(httpd_req_t *req, const char *s);

// Decodes a form value in place: '+' becomes a space, %XX becomes the byte XX.
void web_url_decode(char *s);

// True when rel (a path in the board storage, with a '/' at the start) is a file or a folder of the system that the
// web must not read or write: the spill store of the settings, the signature cache, the permission policy, the pool
// record, the account vault and its old key files (the board reads them into a new vault), the folder of the installed
// apps, and the "<name>.part" upload name of each. The FAT file system does not see the case of a name and removes the
// dots and spaces at the end of a name: the check does the same.
// A short FAT name ("TFN_SI~1.BIN") also opens a file, so a first name with a '~' is refused too.
bool web_data_sys_path(const char *rel);

// The old file routes of the main page: GET /f/<path> (a download) and GET /del/<path> (a delete) of a file in the
// board storage. uri = the URI of the request, prefix = "/f" or "/del". Returns the path in the board storage (the
// part of uri after prefix, with a '/' at the start), or NULL when the routes must refuse it: no '/' after prefix,
// 200 characters or more, a "..", a backslash or a control character, or a file or a folder of the system
// (web_data_sys_path). A GET also comes from a page of another site (an <img> tag), so these routes must not reach
// the files of the system.
const char *web_data_route_path(const char *uri, const char *prefix);
