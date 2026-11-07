// A fake of the ESP-IDF web server for the host unit tests: an answer goes into the buffer of the request. A test
// defines the functions that its code calls. As in ESP-IDF, an empty chunk (NULL or an empty text) is the end of
// a chunked answer: the fake functions of a test keep the end (ended) and count each chunk that comes after it
// (after: a client reads it as garbage, or loses it).
#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef struct httpd_req {
    char out[1 << 16];
    int n;
    char type[64];
    int ended;  // the end of the answer came (an empty chunk)
    int after;  // the chunks after the end
    // (the request: the fields of the real API that the code reads, and the body that httpd_req_recv gives)
    int method;
    const char *uri;
    size_t content_len;
    const char *query;  // the query of the URI, for httpd_req_get_url_query_str
    const char *body;
    size_t body_pos;
    size_t body_cut;  // httpd_req_recv fails after this many bytes of the body (a link that breaks); 0 = never
} httpd_req_t;

// (ssize_t of the real API: Windows has no such type)
typedef ptrdiff_t httpd_len_t;

typedef void *httpd_handle_t;
typedef int httpd_method_t;
#define HTTP_GET               1  // (the values of the real API)
#define HTTP_POST              3
#define HTTPD_SOCK_ERR_TIMEOUT (-3)
#define HTTPD_RESP_USE_STRLEN  (-1)
typedef enum {
    HTTPD_404_NOT_FOUND = 404
} httpd_err_code_t;

typedef struct httpd_uri {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
} httpd_uri_t;

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, httpd_len_t len);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *str);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, httpd_len_t len);
esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t error, const char *msg);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len);
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t val_size);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
