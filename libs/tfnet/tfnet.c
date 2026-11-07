// tfnet: a small HTTP and HTTPS client and a JSON reader for the apps (see tfnet.h). A module of the firmware: no
// static state (more than one task can make a request at the same time).
// The transport (the socket, the TLS) is in the part under ESP_PLATFORM; a PC test gives its own tn_tr_* functions
// (a fake server), so the HTTP and JSON parts run on the PC too.
#include "tfnet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- the transport

#ifdef ESP_PLATFORM
#include "esp_tls.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
// the shared list of the root certificates (tn_url_t.roots = NULL). Each root costs about 2 KB of internal RAM at
// each TLS link: an app gives only the roots of its hosts in tn_url_t.roots (a root list tool makes the file).
#include "tn_roots.inc"

typedef struct {
    int fd;
    esp_tls_t *tls;
} tr_t;

// a wait for the socket (read or write): 1 = ready, 0 = the time ended, -1 = an error
static int wait_fd(int fd, bool wr, int ms)
{
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    int r = lwip_select(fd + 1, wr ? NULL : &s, wr ? &s : NULL, NULL, &tv);
    return r > 0 ? 1 : r == 0 ? 0 : -1;
}

static int tcp_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port[8];
    snprintf(port, sizeof port, "%d", u->port);
    if (getaddrinfo(u->host, port, &hints, &res) != 0 || !res) {
        *err = TN_EDNS;
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        *err = TN_EMEM;
        return -1;
    }
    // a connect with a time limit (the socket waits without a block, in steps of 200 ms: a cancel stops it)
    lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0) {
        int left = timeout_ms, ok = 0;
        while (left > 0 && !(cancel && *cancel)) {
            int w = wait_fd(fd, true, left < 200 ? left : 200);
            if (w != 0) {
                ok = w;
                break;
            }
            left -= 200;
        }
        int so = 0;
        socklen_t sl = sizeof so;
        if (ok == 1) {
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl);
        }
        if (ok != 1 || so) {
            lwip_close(fd);
            *err = cancel && *cancel ? TN_ECANCEL : ok == 0 ? TN_ETIME : TN_ECONN;
            return -1;
        }
    }
    lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

void *tn_tr_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err)
{
    tr_t *t = calloc(1, sizeof *t);
    if (!t) {
        *err = TN_EMEM;
        return NULL;
    }
    t->fd = -1;
    if (!u->tls) {
        t->fd = tcp_open(u, timeout_ms, cancel, err);
        if (t->fd < 0) {
            free(t);
            return NULL;
        }
        return t;
    }
    t->tls = esp_tls_init();
    if (!t->tls) {
        free(t);
        *err = TN_EMEM;
        return NULL;
    }
    esp_tls_cfg_t cfg = {0};
    // (the bytes with the 0 at the end: the PEM reader needs it)
    if (u->roots) {
        cfg.cacert_buf = (const unsigned char *)u->roots;
        cfg.cacert_bytes = strlen(u->roots) + 1;
    } else {
        cfg.cacert_buf = (const unsigned char *)TN_ROOTS;
        cfg.cacert_bytes = sizeof TN_ROOTS;
    }
    cfg.timeout_ms = timeout_ms;
    if (esp_tls_conn_new_sync(u->host, (int)strlen(u->host), u->port, &cfg, t->tls) != 1) {
        esp_tls_conn_destroy(t->tls);
        free(t);
        *err = TN_ETLS;
        return NULL;
    }
    esp_tls_get_conn_sockfd(t->tls, &t->fd);
    return t;
}

int tn_tr_read(void *p, void *b, int n, int timeout_ms)
{
    tr_t *t = p;
    if (t->tls && esp_tls_get_bytes_avail(t->tls) > 0) {
        int r = (int)esp_tls_conn_read(t->tls, b, n);
        return r >= 0 ? r : TN_ECONN;
    }
    int w = wait_fd(t->fd, false, timeout_ms);
    if (w == 0) {
        return TN_ETIME;
    }
    if (w < 0) {
        return TN_ECONN;
    }
    if (t->tls) {
        int r = (int)esp_tls_conn_read(t->tls, b, n);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            return TN_ETIME;
        }
        return r >= 0 ? r : TN_ECONN;
    }
    int r = recv(t->fd, b, n, 0);
    return r >= 0 ? r : TN_ECONN;
}

int tn_tr_write(void *p, const void *b, int n)
{
    tr_t *t = p;
    int done = 0;
    while (done < n) {
        int r = t->tls ? (int)esp_tls_conn_write(t->tls, (const char *)b + done, n - done)
                       : send(t->fd, (const char *)b + done, n - done, 0);
        if (r <= 0) {
            return TN_ECONN;
        }
        done += r;
    }
    return done;
}

void tn_tr_close(void *p)
{
    tr_t *t = p;
    if (!t) {
        return;
    }
    if (t->tls) {
        esp_tls_conn_destroy(t->tls);  // (it closes the socket too)
    } else if (t->fd >= 0) {
        lwip_close(t->fd);
    }
    free(t);
}
#endif

#if !defined(ESP_PLATFORM) && !defined(TN_FAKE)
// (a build on the PC with no fake server (the icon tool, the host fuzz, the simulator): a transport with no network)
void *tn_tr_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err)
{
    (void)u;
    (void)timeout_ms;
    (void)cancel;
    *err = TN_ECONN;
    return NULL;
}
int tn_tr_read(void *t, void *b, int n, int timeout_ms)
{
    (void)t;
    (void)b;
    (void)n;
    (void)timeout_ms;
    return TN_ECONN;
}
int tn_tr_write(void *t, const void *b, int n)
{
    (void)t;
    (void)b;
    (void)n;
    return TN_ECONN;
}
void tn_tr_close(void *t)
{
    (void)t;
}
#endif

// ---------------------------------------------------------------- the URL
bool tn_url_parse(const char *url, tn_url_t *u)
{
    memset(u, 0, sizeof *u);
    const char *p = url;
    if (!strncmp(p, "http://", 7)) {
        p += 7;
        u->port = 80;
    } else if (!strncmp(p, "https://", 8)) {
        p += 8;
        u->port = 443;
        u->tls = true;
    } else {
        return false;
    }
    int i = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && i < (int)sizeof u->host - 1) {
        u->host[i++] = *p++;
    }
    u->host[i] = 0;
    if (!i) {
        return false;
    }
    if (*p == ':') {
        p++;
        int port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p++ - '0');
        }
        if (port <= 0 || port > 65535) {
            return false;
        }
        u->port = port;
    }
    if (*p == '?') {
        snprintf(u->path, sizeof u->path, "/%s", p);
    } else {
        snprintf(u->path, sizeof u->path, "%s", *p ? p : "/");
    }
    // a space in a path goes out as %20 (some station lists have them)
    for (char *s = u->path; *s; s++) {
        if (*s == ' ' && strlen(u->path) + 2 < sizeof u->path) {
            memmove(s + 3, s + 1, strlen(s + 1) + 1);
            s[0] = '%';
            s[1] = '2';
            s[2] = '0';
        }
    }
    return true;
}

// ---------------------------------------------------------------- the connection
#define HEAD_MAX 3072
struct tn_conn {
    void *tr;
    int pi, pn;  // the bytes of the body that came with the head (a stream server sends audio at once)
    uint8_t pend[HEAD_MAX];
};

tn_conn_t *tn_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err)
{
    tn_conn_t *c = calloc(1, sizeof *c);
    if (!c) {
        *err = TN_EMEM;
        return NULL;
    }
    c->tr = tn_tr_open(u, timeout_ms, cancel, err);
    if (!c->tr) {
        free(c);
        return NULL;
    }
    return c;
}

int tn_write(tn_conn_t *c, const void *b, int n)
{
    return tn_tr_write(c->tr, b, n);
}

int tn_read(tn_conn_t *c, void *b, int n, int timeout_ms)
{
    if (c->pi < c->pn) {
        int k = c->pn - c->pi < n ? c->pn - c->pi : n;
        memcpy(b, c->pend + c->pi, (size_t)k);
        c->pi += k;
        return k;
    }
    return tn_tr_read(c->tr, b, n, timeout_ms);
}

void tn_close(tn_conn_t *c)
{
    if (!c) {
        return;
    }
    tn_tr_close(c->tr);
    free(c);
}

const char *tn_err_name(int err)
{
    switch (err) {
    case TN_EDNS:
        return "NO SUCH HOST";
    case TN_ECONN:
        return "NO CONNECTION";
    case TN_ETLS:
        return "SECURE LINK FAILED";
    case TN_ETIME:
        return "NO ANSWER";
    case TN_EHTTP:
        return "BAD ANSWER";
    case TN_ECANCEL:
        return "STOPPED";
    case TN_EMEM:
        return "NO MEMORY";
    case TN_EURL:
        return "BAD ADDRESS";
    case TN_EFULL:
        return "TOO BIG";
    default:
        return "ERROR";
    }
}

// ---------------------------------------------------------------- HTTP
static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static bool hstart(const char *line, const char *name)  // a header name, any case
{
    while (*name) {
        if (lower(*line++) != *name++) {
            return false;
        }
    }
    return *line == ':';
}
static const char *hval(const char *line)
{
    const char *p = strchr(line, ':');
    if (!p) {
        return "";
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

// the first n bytes of s (less at its 0) at the end of out (*at bytes); the text is cut at max - 1 bytes, as with
// snprintf
static void put(char *out, int max, int *at, const char *s, size_t n)
{
    while (n-- > 0 && *s && *at < max - 1) {
        out[(*at)++] = *s++;
    }
    out[*at] = 0;
}

// a Location: a full URL, or a path on the same server as u (the URL of the request). No static buffer: more than
// one task can make a request at the same time.
static void join_url(const tn_url_t *u, const char *loc, char *out, int max)
{
    if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8)) {
        snprintf(out, max, "%s", loc);
        return;
    }
    bool def = (u->tls && u->port == 443) || (!u->tls && u->port == 80);
    char hp[112];
    if (def) {
        snprintf(hp, sizeof hp, "%s", u->host);
    } else {
        snprintf(hp, sizeof hp, "%s:%d", u->host, u->port);
    }
    int at = 0;
    put(out, max, &at, u->tls ? "https://" : "http://", 8);
    put(out, max, &at, hp, sizeof hp);
    if (loc[0] != '/') {  // (the folder of the path: up to its last '/')
        const char *q = strrchr(u->path, '/');
        put(out, max, &at, u->path, q ? (size_t)(q - u->path) + 1 : sizeof u->path);
    }
    put(out, max, &at, loc, strlen(loc));
}

// (buf: HEAD_MAX bytes of the caller, on the heap: the stacks of the tasks are small)
static int read_head(tn_conn_t *c, tn_head_t *h, int timeout_ms, volatile bool *cancel, char *buf)
{
    int n = 0, end = -1;
    while (end < 0) {
        if (cancel && *cancel) {
            return TN_ECANCEL;
        }
        if (n >= HEAD_MAX - 1) {
            return TN_EHTTP;
        }
        int r = tn_tr_read(c->tr, buf + n, HEAD_MAX - 1 - n, timeout_ms);
        if (r <= 0) {
            return r == 0 ? TN_EHTTP : r;
        }
        int from = n > 3 ? n - 3 : 0;
        n += r;
        buf[n] = 0;
        for (int i = from; i + 3 < n; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                end = i + 4;
                break;
            }
        }
        // (an old server ends the head with two LF only)
        if (end < 0) {
            for (int i = from; i + 1 < n; i++) {
                if (buf[i] == '\n' && buf[i + 1] == '\n') {
                    end = i + 2;
                    break;
                }
            }
        }
    }
    // the body bytes that came with the head
    int extra = n - end;
    if (extra > (int)sizeof c->pend) {
        return TN_EHTTP;
    }
    memcpy(c->pend, buf + end, (size_t)extra);
    c->pi = 0;
    c->pn = extra;
    buf[end] = 0;
    memset(h, 0, sizeof *h);
    h->length = -1;
    // the status line: "HTTP/1.1 200 OK" or "ICY 200 OK"
    const char *sp = strchr(buf, ' ');
    if (!sp || (strncmp(buf, "HTTP/", 5) && strncmp(buf, "ICY", 3))) {
        return TN_EHTTP;
    }
    h->status = atoi(sp + 1);
    char *line = strchr(buf, '\n');
    while (line && *++line) {
        char *e = strchr(line, '\n');
        if (e) {
            *e = 0;
            if (e > line && e[-1] == '\r') {
                e[-1] = 0;
            }
        }
        const char *v = hval(line);
        if (hstart(line, "content-type")) {
            int i = 0;
            while (v[i] && v[i] != ';' && v[i] != ' ' && i < (int)sizeof h->ctype - 1) {
                h->ctype[i] = (char)lower(v[i]);
                i++;
            }
            h->ctype[i] = 0;
        } else if (hstart(line, "content-length")) {
            int64_t L = 0;  // (no atoll in the ABI)
            while (*v >= '0' && *v <= '9') {
                L = L * 10 + (*v++ - '0');
            }
            h->length = L;
        } else if (hstart(line, "transfer-encoding")) {
            h->chunked = strstr(v, "chunked") || strstr(v, "Chunked");
        } else if (hstart(line, "location")) {
            snprintf(h->location, sizeof h->location, "%s", v);
        } else if (hstart(line, "icy-metaint")) {
            h->icy_metaint = atoi(v);
        } else if (hstart(line, "icy-br")) {
            h->icy_br = atoi(v);
        } else if (hstart(line, "icy-name")) {
            snprintf(h->icy_name, sizeof h->icy_name, "%s", v);
        }
        if (!e) {
            break;
        }
        line = e;
    }
    return TN_OK;
}

// the work memory of a request (on the heap: about 5 KB)
typedef struct {
    char cur[1024], next[1024], req[1400], head[HEAD_MAX];
    tn_url_t u;
} req_mem_t;

// a request with a method and a body (a redirect goes on with GET and no body); roots: tn_url_t.roots of each link
static tn_conn_t *request_m(const char *roots, const char *method, const char *url, const char *extra, const void *body,
                            int nbody, tn_head_t *h, int timeout_ms, volatile bool *cancel, int *err, char *final)
{
    req_mem_t *m = malloc(sizeof *m);
    if (!m) {
        *err = TN_EMEM;
        return NULL;
    }
    tn_conn_t *out = NULL;
    *err = TN_EHTTP;
    snprintf(m->cur, sizeof m->cur, "%s", url);
    for (int hop = 0; hop < 6; hop++) {
        if (!tn_url_parse(m->cur, &m->u)) {
            *err = TN_EURL;
            break;
        }
        m->u.roots = roots;
        tn_conn_t *c = tn_open(&m->u, timeout_ms, cancel, err);
        if (!c) {
            break;
        }
        bool def = (m->u.tls && m->u.port == 443) || (!m->u.tls && m->u.port == 80);
        char port[12] = "";
        if (!def) {
            snprintf(port, sizeof port, ":%d", m->u.port);
        }
        const bool ua = extra && strstr(extra, "User-Agent:");  // (the extra lines can give their own)
        char clen[32] = "";
        if (body && hop == 0) {
            snprintf(clen, sizeof clen, "Content-Length: %d\r\n", nbody);
        }
        int n = snprintf(m->req, sizeof m->req,
                         "%s %s HTTP/1.0\r\nHost: %s%s\r\n%sAccept: */*\r\n"
                         "Accept-Encoding: identity\r\nConnection: close\r\n%s%s\r\n",
                         hop ? "GET" : method, m->u.path, m->u.host, port,
                         ua ? "" : "User-Agent: TFN-OS/1.0 (ESP32-S3)\r\n", extra ? extra : "", clen);
        if (n >= (int)sizeof m->req || tn_write(c, m->req, n) != n) {
            tn_close(c);
            *err = TN_ECONN;
            break;
        }
        if (body && hop == 0 && nbody > 0 && tn_write(c, body, nbody) != nbody) {
            tn_close(c);
            *err = TN_ECONN;
            break;
        }
        int r = read_head(c, h, timeout_ms, cancel, m->head);
        if (r != TN_OK) {
            tn_close(c);
            *err = r;
            break;
        }
        if (h->status >= 300 && h->status < 400 && h->location[0]) {
            join_url(&m->u, h->location, m->next, sizeof m->next);
            snprintf(m->cur, sizeof m->cur, "%s", m->next);
            tn_close(c);
            *err = TN_EHTTP;
            continue;
        }
        if (final) {
            snprintf(final, 512, "%.511s", m->cur);  // (512 bytes: a longer URL is cut)
        }
        *err = TN_OK;
        out = c;
        break;
    }
    free(m);
    return out;
}
tn_conn_t *tn_request(const char *url, const char *extra, tn_head_t *h, int timeout_ms, volatile bool *cancel, int *err,
                      char *final)
{
    return request_m(NULL, "GET", url, extra, NULL, 0, h, timeout_ms, cancel, err, final);
}

// a line of a chunked body (the size in hex)
static int read_line(tn_conn_t *c, char *b, int max, int timeout_ms)
{
    int n = 0;
    while (n < max - 1) {
        char ch;
        int r = tn_read(c, &ch, 1, timeout_ms);
        if (r <= 0) {
            return r == 0 ? TN_EHTTP : r;
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            b[n++] = ch;
        }
    }
    b[n] = 0;
    return n;
}

// the body of an answer into buf (a 0 at its end); the connection closes
static int read_body(tn_conn_t *c, tn_head_t h, char *buf, int max, int timeout_ms, volatile bool *cancel)
{
    int n = 0;
    int64_t left = h.chunked ? 0 : h.length;
    for (;;) {
        if (cancel && *cancel) {
            tn_close(c);
            return TN_ECANCEL;
        }
        if (h.chunked && left == 0) {
            char line[24];
            int r = read_line(c, line, sizeof line, timeout_ms);
            if (r < 0) {
                tn_close(c);
                return r;
            }
            if (!line[0]) {
                r = read_line(c, line, sizeof line, timeout_ms);
                if (r < 0) {
                    tn_close(c);
                    return r;
                }
            }
            left = strtol(line, NULL, 16);
            if (left <= 0) {
                break;
            }
        }
        if (!h.chunked && h.length >= 0 && left <= 0) {
            break;
        }
        int want = max - 1 - n;
        if (want <= 0) {
            tn_close(c);
            return TN_EFULL;
        }
        if ((h.chunked || h.length >= 0) && want > left) {
            want = (int)left;
        }
        int r = tn_read(c, buf + n, want, timeout_ms);
        if (r == 0) {
            break;  // the server closed: the end of the body
        }
        if (r < 0) {
            tn_close(c);
            return r;
        }
        n += r;
        if (h.chunked || h.length >= 0) {
            left -= r;
        }
    }
    tn_close(c);
    buf[n] = 0;
    return n;
}
int tn_get(const char *url, char *buf, int max, int timeout_ms, volatile bool *cancel, int *status)
{
    tn_head_t h;
    int err;
    if (status) {
        *status = 0;
    }
    tn_conn_t *c = tn_request(url, NULL, &h, timeout_ms, cancel, &err, NULL);
    if (!c) {
        return err;
    }
    if (status) {
        *status = h.status;
    }
    return read_body(c, h, buf, max, timeout_ms, cancel);
}
int tn_fetch(const char *method, const char *url, const char *extra, const void *body, int nbody, char *buf, int max,
             int timeout_ms, volatile bool *cancel, int *status)
{
    return tn_fetch_ca(NULL, method, url, extra, body, nbody, buf, max, timeout_ms, cancel, status);
}
int tn_fetch_ca(const char *roots, const char *method, const char *url, const char *extra, const void *body, int nbody,
                char *buf, int max, int timeout_ms, volatile bool *cancel, int *status)
{
    tn_head_t h;
    int err;
    if (status) {
        *status = 0;
    }
    tn_conn_t *c = request_m(roots, method, url, extra, body, nbody, &h, timeout_ms, cancel, &err, NULL);
    if (!c) {
        return err;
    }
    if (status) {
        *status = h.status;
    }
    return read_body(c, h, buf, max, timeout_ms, cancel);
}

// ---------------------------------------------------------------- JSON
static const char *ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        p++;
    }
    return p;
}
// the end of the value at p (one character after it)
static const char *vend(const char *p, const char *e)
{
    if (p >= e) {
        return e;
    }
    if (*p == '"') {
        p++;
        while (p < e && *p != '"') {
            p += *p == '\\' ? 2 : 1;
        }
        return p < e ? p + 1 : e;
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < e) {
            char ch = *p;
            if (ch == '"') {
                p = vend(p, e);
                continue;
            }
            if (ch == '{' || ch == '[') {
                depth++;
            } else if (ch == '}' || ch == ']') {
                if (--depth == 0) {
                    return p + 1;
                }
            }
            p++;
        }
        return e;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
        p++;
    }
    return p;
}

tn_js_t tn_js_root(const char *s, int n)
{
    tn_js_t r = {NULL, NULL};
    const char *e = s + n;
    const char *p = ws(s, e);
    if (p < e) {
        r.p = p;
        r.e = vend(p, e);
    }
    return r;
}

tn_js_t tn_js_first(tn_js_t v)
{
    tn_js_t r = {NULL, NULL};
    if (!v.p || (*v.p != '[' && *v.p != '{')) {
        return r;
    }
    const char *p = ws(v.p + 1, v.e);
    if (p >= v.e || *p == ']' || *p == '}') {
        return r;
    }
    if (*v.p == '{') {  // an object: the value of its first key
        p = ws(vend(p, v.e), v.e);
        if (p >= v.e || *p != ':') {
            return r;
        }
        p = ws(p + 1, v.e);
    }
    r.p = p;
    r.e = vend(p, v.e);
    return r;
}

tn_js_t tn_js_next(tn_js_t v, tn_js_t cur)
{
    tn_js_t r = {NULL, NULL};
    if (!cur.p) {
        return r;
    }
    const char *p = ws(cur.e, v.e);
    if (p >= v.e || *p != ',') {
        return r;
    }
    p = ws(p + 1, v.e);
    if (*v.p == '{') {
        p = ws(vend(p, v.e), v.e);
        if (p >= v.e || *p != ':') {
            return r;
        }
        p = ws(p + 1, v.e);
    }
    if (p >= v.e) {
        return r;
    }
    r.p = p;
    r.e = vend(p, v.e);
    return r;
}

tn_js_t tn_js_key(tn_js_t o, const char *key)
{
    tn_js_t r = {NULL, NULL};
    if (!o.p || *o.p != '{') {
        return r;
    }
    size_t kl = strlen(key);
    const char *p = ws(o.p + 1, o.e);
    while (p < o.e && *p == '"') {
        const char *ke = vend(p, o.e);
        bool hit = (size_t)(ke - p) == kl + 2 && !memcmp(p + 1, key, kl);
        p = ws(ke, o.e);
        if (p >= o.e || *p != ':') {
            return r;
        }
        p = ws(p + 1, o.e);
        const char *ve = vend(p, o.e);
        if (hit) {
            r.p = p;
            r.e = ve;
            return r;
        }
        p = ws(ve, o.e);
        if (p < o.e && *p == ',') {
            p = ws(p + 1, o.e);
        } else {
            break;
        }
    }
    return r;
}

tn_js_t tn_js_at(tn_js_t a, int i)
{
    tn_js_t r = {NULL, NULL};
    if (!a.p || *a.p != '[') {
        return r;
    }
    tn_js_t v = tn_js_first(a);
    while (v.p && i-- > 0) {
        v = tn_js_next(a, v);
    }
    return v.p ? v : r;
}

int tn_js_len(tn_js_t a)
{
    int n = 0;
    for (tn_js_t v = tn_js_first(a); v.p; v = tn_js_next(a, v)) {
        n++;
    }
    return n;
}

bool tn_js_ok(tn_js_t v)
{
    return v.p && !(v.e - v.p == 4 && !memcmp(v.p, "null", 4));
}

// a number at p (no strtod in the ABI)
static double num_at(const char *p, const char *e, const char **out)
{
    double sign = 1, v = 0;
    if (p < e && *p == '-') {
        sign = -1;
        p++;
    }
    while (p < e && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
    }
    if (p < e && *p == '.') {
        p++;
        double f = 0.1;
        while (p < e && *p >= '0' && *p <= '9') {
            v += (*p++ - '0') * f;
            f *= 0.1;
        }
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        int es = 1, ex = 0;
        if (p < e && (*p == '+' || *p == '-')) {
            es = *p++ == '-' ? -1 : 1;
        }
        while (p < e && *p >= '0' && *p <= '9') {
            ex = ex * 10 + (*p++ - '0');
        }
        while (ex-- > 0) {
            v = es > 0 ? v * 10 : v / 10;
        }
    }
    if (out) {
        *out = p;
    }
    return sign * v;
}

double tn_js_num(tn_js_t v, double def)
{
    if (!tn_js_ok(v)) {
        return def;
    }
    const char *p = v.p;
    if (*p == '"') {
        p++;  // (a number in quotes)
    }
    if (!((*p >= '0' && *p <= '9') || *p == '-')) {
        return def;
    }
    return num_at(p, v.e, NULL);
}

static int utf8_put(char *o, int max, int n, unsigned cp)
{
    if (cp < 0x80) {
        if (n + 1 < max) {
            o[n++] = (char)cp;
        }
    } else if (cp < 0x800) {
        if (n + 2 < max) {
            o[n++] = (char)(0xC0 | (cp >> 6));
            o[n++] = (char)(0x80 | (cp & 63));
        }
    } else if (n + 3 < max) {
        o[n++] = (char)(0xE0 | (cp >> 12));
        o[n++] = (char)(0x80 | ((cp >> 6) & 63));
        o[n++] = (char)(0x80 | (cp & 63));
    }
    return n;
}

static bool js_str(tn_js_t v, char *out, int max, bool lines)
{
    if (max <= 0) {
        return false;
    }
    out[0] = 0;
    if (!v.p || *v.p != '"') {
        return false;
    }
    const char *p = v.p + 1, *e = v.e - 1;
    int n = 0;
    while (p < e && n < max - 1) {
        char ch = *p++;
        if (ch != '\\') {
            out[n++] = ch;
            continue;
        }
        if (p >= e) {
            break;
        }
        ch = *p++;
        switch (ch) {
        case 'n':
            out[n++] = lines ? '\n' : ' ';
            break;  // (a line break of a name: a space)
        case 't':
            out[n++] = ' ';
            break;
        case 'r':
            break;
        case 'b':
        case 'f':
            break;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4 && p < e; i++, p++) {
                char h = *p;
                cp = cp * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : (lower(h) - 'a' + 10) & 15);
            }
            if (cp >= 0xD800 && cp < 0xE000) {
                cp = '?';  // (a pair of surrogates: an emoji; not in the fonts)
            }
            n = utf8_put(out, max, n, cp);
            break;
        }
        default:
            out[n++] = ch;
        }
    }
    out[n] = 0;
    return true;
}
bool tn_js_str(tn_js_t v, char *out, int max)
{
    return js_str(v, out, max, false);
}
bool tn_js_text(tn_js_t v, char *out, int max)
{
    return js_str(v, out, max, true);
}

int tn_js_nums(tn_js_t a, float *out, int max)
{
    if (!a.p || *a.p != '[') {
        return 0;
    }
    const char *p = a.p + 1, *e = a.e;
    int n = 0;
    while (p < e && n < max) {
        p = ws(p, e);
        if (p >= e || *p == ']') {
            break;
        }
        if (*p == 'n') {
            out[n++] = NAN;
            p += 4;
        } else {
            out[n++] = (float)num_at(p, e, &p);
        }
        p = ws(p, e);
        if (p < e && *p == ',') {
            p++;
        }
    }
    return n;
}

int tn_js_ints(tn_js_t a, int64_t *out, int max)
{
    if (!a.p || *a.p != '[') {
        return 0;
    }
    const char *p = a.p + 1, *e = a.e;
    int n = 0;
    while (p < e && n < max) {
        p = ws(p, e);
        if (p >= e || *p == ']') {
            break;
        }
        if (*p == 'n') {
            out[n++] = 0;
            p += 4;
        } else {
            bool neg = *p == '-';
            if (neg) {
                p++;
            }
            int64_t v = 0;
            while (p < e && *p >= '0' && *p <= '9') {
                v = v * 10 + (*p++ - '0');
            }
            while (p < e && *p != ',' && *p != ']') {
                p++;  // (a fraction: dropped)
            }
            out[n++] = neg ? -v : v;
        }
        p = ws(p, e);
        if (p < e && *p == ',') {
            p++;
        }
    }
    return n;
}
