// TFN OS: the trusted publisher list, the parts with no ESP-IDF call (tfn_pub.h): the list, the wait state of a new
// key, the parsers of the files "sigs" and the key file, and the check of the signatures of an app. The hash and the
// ECDSA come from the caller (tfn_pub_ops_t), so the host test (tests/unit/test_pub.c) runs this file as it is.
#include "tfn_pub.h"
#include <stdio.h>
#include <string.h>

static int hexv(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// exactly 2 * nb hex digits at t (no other letters) into out
static bool hex_exact(const char *t, size_t n, uint8_t *out, size_t nb)
{
    if (n != 2 * nb) {
        return false;
    }
    for (size_t i = 0; i < nb; i++) {
        int a = hexv(t[2 * i]), b = hexv(t[2 * i + 1]);
        if (a < 0 || b < 0) {
            return false;
        }
        out[i] = (uint8_t)(a << 4 | b);
    }
    return true;
}

void tfn_pub_key_id(const tfn_pub_ops_t *o, const uint8_t pk[TFN_PUB_PK], uint8_t id[TFN_PUB_ID])
{
    uint8_t h[32];
    o->sha256(pk, TFN_PUB_PK, h);
    memcpy(id, h, TFN_PUB_ID);
}

bool tfn_pub_key_set(const uint8_t pk[TFN_PUB_PK])
{
    return pk && pk[0] == 0x04;
}

void tfn_pub_id_text(const uint8_t id[TFN_PUB_ID], char out[20])
{
    static const char HX[] = "0123456789ABCDEF";
    int k = 0;
    for (int i = 0; i < TFN_PUB_ID; i++) {
        if (i && !(i & 1)) {
            out[k++] = '-';
        }
        out[k++] = HX[id[i] >> 4];
        out[k++] = HX[id[i] & 15];
    }
    out[k] = 0;
}

bool tfn_pub_id_parse(const char *s, uint8_t id[TFN_PUB_ID])
{
    int nd = 0;
    uint8_t v[TFN_PUB_ID] = {0};
    for (; s && *s; s++) {
        int h = hexv(*s);
        if (h >= 0) {
            if (nd >= 2 * TFN_PUB_ID) {
                return false;
            }
            v[nd / 2] = (uint8_t)(v[nd / 2] << 4 | h);
            nd++;
        } else if (*s != '-' && *s != ' ') {
            return false;
        }
    }
    if (nd != 2 * TFN_PUB_ID) {
        return false;
    }
    memcpy(id, v, TFN_PUB_ID);
    return true;
}

int tfn_pub_flags_parse(const char *s)
{
    static const char *const W[3] = {"store", "dev", "test"};
    int f = 0;
    while (s && *s) {
        size_t n = strcspn(s, ",+");
        int hit = -1;
        for (int k = 0; k < 3; k++) {
            size_t wn = strlen(W[k]);
            bool same = n == wn;
            for (size_t i = 0; same && i < n; i++) {
                char c = s[i];
                same = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c) == W[k][i];
            }
            if (same) {
                hit = k;
            }
        }
        if (hit < 0) {
            return -1;
        }
        f |= 1 << hit;
        s += n;
        if (*s) {
            s++;
        }
    }
    return f ? f : -1;
}

void tfn_pub_flags_text(int flags, char *out, int len)
{
    snprintf(out, len, "%s%s%s%s%s", flags & TFN_PUB_STORE ? "STORE" : "",
             (flags & TFN_PUB_STORE) && (flags & (TFN_PUB_DEV | TFN_PUB_TEST)) ? "+" : "",
             flags & TFN_PUB_DEV ? "DEV" : "", (flags & TFN_PUB_DEV) && (flags & TFN_PUB_TEST) ? "+" : "",
             flags & TFN_PUB_TEST ? "TEST" : "");
}

bool tfn_pub_name_set(char out[TFN_PUB_NAME], const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n >= TFN_PUB_NAME) {
        return false;
    }
    char b[TFN_PUB_NAME];
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                  c == '_' || c == '.';
        if (!ok) {
            return false;
        }
        b[i] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    b[n] = 0;
    memcpy(out, b, n + 1);
    return true;
}

int tfn_pub_find(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t id[TFN_PUB_ID])
{
    for (int i = 0; i < l->n; i++) {
        uint8_t k[TFN_PUB_ID];
        tfn_pub_key_id(o, l->e[i].pk, k);
        if (!memcmp(k, id, TFN_PUB_ID)) {
            return i;
        }
    }
    return -1;
}

// the name of an entry is a real text: 1 .. 15 good letters and the end
static bool entry_ok(const tfn_pub_t *p)
{
    if (!tfn_pub_key_set(p->pk) || !(p->flags & TFN_PUB_FLAGS) || (p->flags & ~TFN_PUB_FLAGS)) {
        return false;
    }
    if (!memchr(p->name, 0, TFN_PUB_NAME)) {
        return false;
    }
    char tmp[TFN_PUB_NAME];
    return tfn_pub_name_set(tmp, p->name) && !strcmp(tmp, p->name);
}

bool tfn_pub_add_ok(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const tfn_pub_t *p, char *msg, int len)
{
    uint8_t id[TFN_PUB_ID];
    if (!entry_ok(p)) {
        snprintf(msg, len, "not a good key (a P-256 point 04..., a name of 1-15 letters, flags store/dev/test)");
        return false;
    }
    tfn_pub_key_id(o, p->pk, id);
    for (int i = 0; i < TFN_PUB_BUILTIN; i++) {
        if (o->builtin[i] && !memcmp(o->builtin[i], p->pk, TFN_PUB_PK)) {
            snprintf(msg, len, "this key is a built-in key of the board");
            return false;
        }
    }
    if (tfn_pub_find(o, l, id) >= 0) {
        snprintf(msg, len, "this key is in the list already");
        return false;
    }
    if (l->n >= TFN_PUB_MAX) {
        snprintf(msg, len, "the list is full (%d keys): remove a key first", TFN_PUB_MAX);
        return false;
    }
    return true;
}

bool tfn_pub_add(const tfn_pub_ops_t *o, tfn_pub_list_t *l, const tfn_pub_t *p, char *msg, int len)
{
    if (!tfn_pub_add_ok(o, l, p, msg, len)) {
        return false;
    }
    l->e[l->n++] = *p;
    char t[20];
    uint8_t id[TFN_PUB_ID];
    tfn_pub_key_id(o, p->pk, id);
    tfn_pub_id_text(id, t);
    snprintf(msg, len, "the key %s (%s) is in the list", t, p->name);
    return true;
}

bool tfn_pub_remove(const tfn_pub_ops_t *o, tfn_pub_list_t *l, const uint8_t id[TFN_PUB_ID])
{
    int i = tfn_pub_find(o, l, id);
    if (i < 0) {
        return false;
    }
    memmove(&l->e[i], &l->e[i + 1], (size_t)(l->n - i - 1) * sizeof l->e[0]);
    l->n--;
    memset(&l->e[l->n], 0, sizeof l->e[0]);
    return true;
}

#define ENT (TFN_PUB_PK + TFN_PUB_NAME + 1)

size_t tfn_pub_pack(const tfn_pub_list_t *l, uint8_t *out, size_t max)
{
    size_t n = 5 + (size_t)l->n * ENT;
    if (l->n < 0 || l->n > TFN_PUB_MAX || n > max) {
        return 0;
    }
    memcpy(out, "TPL1", 4);
    out[4] = (uint8_t)l->n;
    for (int i = 0; i < l->n; i++) {
        uint8_t *b = out + 5 + i * ENT;
        memcpy(b, l->e[i].pk, TFN_PUB_PK);
        memcpy(b + TFN_PUB_PK, l->e[i].name, TFN_PUB_NAME);
        b[TFN_PUB_PK + TFN_PUB_NAME] = l->e[i].flags;
    }
    return n;
}

bool tfn_pub_unpack(tfn_pub_list_t *l, const uint8_t *b, size_t n)
{
    memset(l, 0, sizeof *l);
    if (!b || n < 5 || memcmp(b, "TPL1", 4) || b[4] > TFN_PUB_MAX || n != 5 + (size_t)b[4] * ENT) {
        return false;
    }
    tfn_pub_list_t t;
    memset(&t, 0, sizeof t);
    for (int i = 0; i < b[4]; i++) {
        const uint8_t *e = b + 5 + i * ENT;
        tfn_pub_t *p = &t.e[i];
        memcpy(p->pk, e, TFN_PUB_PK);
        memcpy(p->name, e + TFN_PUB_PK, TFN_PUB_NAME);
        p->flags = e[TFN_PUB_PK + TFN_PUB_NAME];
        if (!entry_ok(p)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!memcmp(t.e[j].pk, p->pk, TFN_PUB_PK)) {
                return false;
            }
        }
    }
    t.n = b[4];
    *l = t;
    return true;
}

// the next line of t (n bytes) from *pos: its start and length with no "\r\n"; false at the end
static bool next_line(const char *t, size_t n, size_t *pos, const char **ls, size_t *ln)
{
    if (*pos >= n) {
        return false;
    }
    const char *s = t + *pos;
    const char *e = memchr(s, '\n', n - *pos);
    size_t k = e ? (size_t)(e - s) : n - *pos;
    *pos += k + (e ? 1 : 0);
    while (k && (s[k - 1] == '\r' || s[k - 1] == ' ' || s[k - 1] == '\t')) {
        k--;
    }
    *ls = s;
    *ln = k;
    return true;
}

int tfn_pub_sigs_parse(const char *t, size_t n, tfn_pub_sig_t *out, int max)
{
    size_t pos = 0, ln;
    const char *ls;
    int k = 0;
    if (!t) {
        return 0;
    }
    while (k < max && next_line(t, n, &pos, &ls, &ln)) {
        // "TFNSIG2 " (8) + 16 hex + " " + 128 hex = 153 letters
        if (ln != 153 || memcmp(ls, "TFNSIG2 ", 8) || ls[24] != ' ') {
            continue;
        }
        tfn_pub_sig_t s;
        if (hex_exact(ls + 8, 16, s.id, TFN_PUB_ID) && hex_exact(ls + 25, 128, s.sig, 64)) {
            out[k++] = s;
        }
    }
    return k;
}

// the value of "key=" at the start of a line of t (n bytes), as a start and a length
static bool line_val(const char *t, size_t n, const char *key, const char **v, size_t *vn)
{
    size_t pos = 0, ln, kn = strlen(key);
    const char *ls;
    while (next_line(t, n, &pos, &ls, &ln)) {
        if (ln > kn && !memcmp(ls, key, kn) && ls[kn] == '=') {
            *v = ls + kn + 1;
            *vn = ln - kn - 1;
            return true;
        }
    }
    return false;
}

bool tfn_pub_manifest_pub(const char *m, size_t n, uint8_t id[TFN_PUB_ID])
{
    const char *v;
    size_t vn;
    return m && line_val(m, n, "publisher", &v, &vn) && hex_exact(v, vn, id, TFN_PUB_ID);
}

bool tfn_pub_hex_key(const char *t, size_t n, uint8_t pk[TFN_PUB_PK])
{
    while (n && (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')) {
        t++;
        n--;
    }
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) {
        n--;
    }
    uint8_t k[TFN_PUB_PK];
    if (!hex_exact(t, n, k, TFN_PUB_PK) || !tfn_pub_key_set(k)) {
        return false;
    }
    memcpy(pk, k, TFN_PUB_PK);
    return true;
}

bool tfn_pub_file_parse(const char *t, size_t n, tfn_pub_t *p)
{
    const char *v;
    size_t vn;
    char b[24];
    tfn_pub_t q;
    memset(&q, 0, sizeof q);
    if (!t || n < 8 || memcmp(t, "TFNPUB1", 7) || (t[7] != '\n' && t[7] != '\r')) {
        return false;
    }
    if (!line_val(t, n, "key", &v, &vn) || !tfn_pub_hex_key(v, vn, q.pk)) {
        return false;
    }
    if (!line_val(t, n, "name", &v, &vn) || vn >= sizeof b) {
        return false;
    }
    memcpy(b, v, vn);
    b[vn] = 0;
    if (!tfn_pub_name_set(q.name, b)) {
        return false;
    }
    int f = TFN_PUB_DEV;  // (no flags line: a developer key)
    if (line_val(t, n, "flags", &v, &vn)) {
        if (vn >= sizeof b) {
            return false;
        }
        memcpy(b, v, vn);
        b[vn] = 0;
        f = tfn_pub_flags_parse(b);
    }
    if (f <= 0) {
        return false;
    }
    q.flags = (uint8_t)f;
    *p = q;
    return true;
}

// the end of a wait: its number stays (the next wait gets the next number)
static void wait_end(tfn_pub_wait_t *w)
{
    const uint32_t seq = w->seq;
    memset(w, 0, sizeof *w);
    w->seq = seq;
}

bool tfn_pub_wait_live(tfn_pub_wait_t *w, int64_t now_ms)
{
    if (w->on && now_ms >= w->until_ms) {
        wait_end(w);
    }
    return w->on;
}

bool tfn_pub_wait_start(tfn_pub_wait_t *w, const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const tfn_pub_t *p,
                        int64_t now_ms, char *msg, int len)
{
    if (tfn_pub_wait_live(w, now_ms)) {
        snprintf(msg, len, "a key waits for the board owner already (%s): try again later", w->p.name);
        return false;
    }
    if (!tfn_pub_add_ok(o, l, p, msg, len)) {
        return false;
    }
    w->on = true;
    w->p = *p;
    w->until_ms = now_ms + TFN_PUB_WAIT_MS;
    w->seq++;
    if (!w->seq) {
        w->seq = 1;  // (0 is no wait)
    }
    snprintf(msg, len, "the key waits: tap ALLOW on the screen of the board (%d s)", TFN_PUB_WAIT_MS / 1000);
    return true;
}

bool tfn_pub_wait_answer(tfn_pub_wait_t *w, const tfn_pub_ops_t *o, tfn_pub_list_t *l, bool allow, int64_t now_ms,
                         char *msg, int len)
{
    if (!tfn_pub_wait_live(w, now_ms)) {
        snprintf(msg, len, "no key waits");
        return false;
    }
    tfn_pub_t p = w->p;
    wait_end(w);
    if (!allow) {
        snprintf(msg, len, "the board owner did not allow the key %s", p.name);
        return false;
    }
    return tfn_pub_add(o, l, &p, msg, len);
}

bool tfn_pub_wait_allow_id(tfn_pub_wait_t *w, const tfn_pub_ops_t *o, tfn_pub_list_t *l, const uint8_t id[TFN_PUB_ID],
                           int64_t now_ms, char *msg, int len)
{
    if (!tfn_pub_wait_live(w, now_ms)) {
        snprintf(msg, len, "no key waits");
        return false;
    }
    uint8_t k[TFN_PUB_ID];
    tfn_pub_key_id(o, w->p.pk, k);
    if (memcmp(k, id, TFN_PUB_ID)) {
        char a[20], b[20];
        tfn_pub_id_text(k, a);
        tfn_pub_id_text(id, b);
        // (the key stays in the wait: the tap on the screen can still allow it or not)
        snprintf(msg, len, "the key that waits (%s) has the ID %s, not %s", w->p.name, a, b);
        return false;
    }
    return tfn_pub_wait_answer(w, o, l, true, now_ms, msg, len);
}

// a "sigs" line of the publisher of the manifest that verifies with an added key of these flags: its place, or -1
static int dev_sig(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t *m, size_t ml, const uint8_t pub[8],
                   const tfn_pub_sig_t *s, int ns, int flags)
{
    for (int i = 0; i < ns; i++) {
        if (memcmp(s[i].id, pub, TFN_PUB_ID)) {
            continue;
        }
        int k = tfn_pub_find(o, l, s[i].id);
        if (k >= 0 && (l->e[k].flags & flags) && o->verify(l->e[k].pk, m, ml, s[i].sig)) {
            return k;
        }
    }
    return -1;
}

// the file "sigs" (a function of its own: the lines are on the stack only for a developer package, not on the path
// of the store apps, which also runs in the web task)
__attribute__((noinline)) static int check_sigs(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t *m,
                                                size_t ml, const char *sigs, size_t gl, char who[TFN_PUB_NAME])
{
    uint8_t pub[TFN_PUB_ID];
    tfn_pub_sig_t s[TFN_PUB_SIGS_MAX];
    int ns = gl <= TFN_PUB_SIGS_FILE ? tfn_pub_sigs_parse(sigs, gl, s, TFN_PUB_SIGS_MAX) : 0;
    if (!ns || !tfn_pub_manifest_pub((const char *)m, ml, pub)) {
        return TFN_PUB_NONE;
    }
    int k = dev_sig(o, l, m, ml, pub, s, ns, TFN_PUB_TEST);
    if (k >= 0) {
        snprintf(who, TFN_PUB_NAME, "%s", l->e[k].name);
        return TFN_PUB_OK_TEST;
    }
    k = dev_sig(o, l, m, ml, pub, s, ns, TFN_PUB_DEV);
    if (k >= 0) {
        snprintf(who, TFN_PUB_NAME, "%s", l->e[k].name);
        return TFN_PUB_NOT_REVIEWED;
    }
    return TFN_PUB_NONE;
}

int tfn_pub_check(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t *m, size_t ml, const uint8_t *sig,
                  size_t sl, const char *sigs, size_t gl, char who[TFN_PUB_NAME])
{
    who[0] = 0;
    if (sig && sl == 64) {
        // (the store apps of today: the same check and the same cache as before the list)
        if (o->builtin_ok(m, ml, sig, sl)) {
            return TFN_PUB_OK_BUILTIN;
        }
        for (int i = 0; i < l->n; i++) {
            if ((l->e[i].flags & TFN_PUB_STORE) && o->verify(l->e[i].pk, m, ml, sig)) {
                snprintf(who, TFN_PUB_NAME, "%s", l->e[i].name);
                return TFN_PUB_OK_STORE;
            }
        }
    }
    return sigs && gl ? check_sigs(o, l, m, ml, sigs, gl, who) : TFN_PUB_NONE;
}
