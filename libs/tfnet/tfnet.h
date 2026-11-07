#pragma once
// tfnet: a small HTTP and HTTPS client for the apps (WEATHER, MQTT and more).
// A module of the firmware: its functions are ABI names (the network functions need the permission "net"). An app in
// the repo includes "../../libs/tfnet/tfnet.h"; an app in its own folder includes "tfnet/tfnet.h" of the SDK package.
// The HTTPS part has its own root certificates (tn_roots.inc: the shared list), or the list that the app gives
// (tn_url_t.roots, tn_fetch_ca). Plain C. Call it only from a task of the app (each call can wait some seconds). Each
// wait stops early when *cancel is true.
// A small JSON reader is at the end (for the answers of the weather and station APIs).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool tls;
    int port;
    char host[96];
    char path[1024];
    // The root certificates of a TLS link: PEM text with a 0 at its end, or NULL = the shared list (14 roots). Each
    // root costs about 2 KB of internal RAM while the link is open: an app with few hosts gives only their roots.
    // tn_url_parse sets NULL.
    const char *roots;
} tn_url_t;
// "http://host[:port]/path" or "https://..."; false = not a URL that tfnet can open
bool tn_url_parse(const char *url, tn_url_t *u);

typedef struct tn_conn tn_conn_t;
// the transport (the socket, the TLS): tfnet.c on the board; a PC test gives its own (TN_FAKE)
void *tn_tr_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err);
int tn_tr_read(void *t, void *b, int n, int timeout_ms);
int tn_tr_write(void *t, const void *b, int n);
void tn_tr_close(void *t);
enum {
    TN_OK = 0,
    TN_EDNS = -1,
    TN_ECONN = -2,
    TN_ETLS = -3,
    TN_ETIME = -4,
    TN_EHTTP = -5,
    TN_ECANCEL = -6,
    TN_EMEM = -7,
    TN_EURL = -8,
    TN_EFULL = -9
};

// A connection to the host of u (TLS with the roots when u->tls). NULL = an error (*err = TN_E*).
tn_conn_t *tn_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err);
int tn_write(tn_conn_t *c, const void *b, int n);
// Up to n bytes: > 0 = the bytes, 0 = the server closed, TN_ETIME = nothing in timeout_ms, < 0 = an error.
int tn_read(tn_conn_t *c, void *b, int n, int timeout_ms);
void tn_close(tn_conn_t *c);

// The head of an HTTP answer
typedef struct {
    int status;      // 200, 302 ... (the line "ICY 200 OK" of an old stream server gives 200)
    int64_t length;  // -1 = not given
    bool chunked;
    int icy_metaint;  // the bytes of audio between two blocks of stream data (0 = none)
    int icy_br;       // the bit rate that the stream gives (kbps, 0 = not given)
    char ctype[48];   // Content-Type, small letters, with no parameters
    char icy_name[64];
    char location[512];  // a redirect
} tn_head_t;

// A GET of url: the request, the head, the redirects (5 at most). extra: more lines of the request ("Icy-MetaData:
// 1\r\n") or NULL. The connection is then at the start of the body (NULL = an error, *err). The last URL (after the
// redirects) goes to final when it is not NULL (512 bytes).
tn_conn_t *tn_request(const char *url, const char *extra, tn_head_t *h, int timeout_ms, volatile bool *cancel, int *err,
                      char *final);
// The whole body of a GET into buf (a 0 at its end; a chunked body too). The length, or TN_E* (TN_EFULL: more than
// max - 1 bytes). *status = the HTTP status (NULL: not needed).
int tn_get(const char *url, char *buf, int max, int timeout_ms, volatile bool *cancel, int *status);
// The same with a method ("POST", "GET" ...), the extra lines of the head ("Authorization: ...\r\n"; a User-Agent line
// there takes the place of the one of tfnet) and a body (NULL: none; with a body: Content-Length)
int tn_fetch(const char *method, const char *url, const char *extra, const void *body, int nbody, char *buf, int max,
             int timeout_ms, volatile bool *cancel, int *status);
// The same as tn_fetch, with the root certificates of the app for the TLS links (roots: as tn_url_t.roots; NULL = the
// shared list). The redirects use the same roots.
int tn_fetch_ca(const char *roots, const char *method, const char *url, const char *extra, const void *body, int nbody,
                char *buf, int max, int timeout_ms, volatile bool *cancel, int *status);
// A short name of an error, for the screen (English)
const char *tn_err_name(int err);

// ---------------------------------------------------------------- the JSON reader
// It reads the text in place (no copy). A "span" is the value of a key: from its first character to the end of the
// value. The keys are searched only in the object that is given (not in the objects inside it).
typedef struct {
    const char *p, *e;
} tn_js_t;
tn_js_t tn_js_root(const char *s, int n);
// The value of key in the object o ({...}); .p = NULL when it is not there
tn_js_t tn_js_key(tn_js_t o, const char *key);
// The element i of an array; .p = NULL when it is not there
tn_js_t tn_js_at(tn_js_t a, int i);
int tn_js_len(tn_js_t a);  // the elements of an array
bool tn_js_ok(tn_js_t v);  // a value that is there and is not null
double tn_js_num(tn_js_t v, double def);
// A string value into out (UTF-8, the escapes \" \\ \/ \n \uXXXX done); false = not a string
bool tn_js_str(tn_js_t v, char *out, int max);
// The same, with the line breaks (\n) kept (a text, not a name)
bool tn_js_text(tn_js_t v, char *out, int max);
// An array of numbers into out (null = NAN); the count
int tn_js_nums(tn_js_t a, float *out, int max);
// The same, for whole numbers (a unix time) (null = 0)
int tn_js_ints(tn_js_t a, int64_t *out, int max);
// The step to the next element of an array or an object: the first value inside v, and the value after it
tn_js_t tn_js_first(tn_js_t v);
tn_js_t tn_js_next(tn_js_t v, tn_js_t cur);
