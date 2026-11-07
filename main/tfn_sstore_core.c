// TFN OS: the static store, the parts with no ESP-IDF call (tfn_sstore.h): the URL check, the trusted key of the
// index signature, the parser of index.json and the check of a file. The hash, the ECDSA and the memory come from the
// caller, so the host test (tests/unit/test_sstore.c) runs this file as it is.
//
// The parser reads the form of sdk/tools/store_index.py (STORE_FORMAT 1) with the same checks as its
// check_index_form for the entries of this board kind. It allocates no memory for the JSON: the strings get their
// real bytes in place (an escape is never shorter in the text than in its bytes).
#include "tfn_sstore.h"
#include <stdio.h>
#include <string.h>

#define JS_DEPTH    32   // the deepest JSON value that the parser skips
#define PERMS_TEXT  160  // the permission classes of a package, with commas
#define NUM_DIGITS  15   // the longest number of the index
#define KEY_ID_HEX  16   // the "key_id" of the index: 16 hex digits
#define PUB_HEX     130  // the "pubkey" of the index: 130 hex digits
#define APP_ID_MAX  23   // the longest app id
#define KIND_ID_MAX 15   // the longest board kind

static const char *const FILE_NAMES[TFN_SS_NFILES] = TFN_SS_FILE_NAMES;

const char *tfn_ss_file_name(int i)
{
    return i >= 0 && i < TFN_SS_NFILES ? FILE_NAMES[i] : "";
}

bool tfn_ss_url_ok(const char *u)
{
    size_t n = u ? strlen(u) : 0;
    if (n <= 8 || n >= TFN_SS_URL - 1 || strncmp(u, "https://", 8) || u[8] == '/') {
        return false;
    }
    for (const char *p = u; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= ' ' || c >= 0x7f || strchr("\"\\?#<>", c)) {
            return false;
        }
    }
    return true;
}

bool tfn_ss_join(char *out, size_t max, const char *base, const char *rel)
{
    size_t bl = strlen(base);
    bool slash = bl && base[bl - 1] == '/';
    while (*rel == '/') {
        rel++;
    }
    int k = snprintf(out, max, "%s%s%s", base, slash ? "" : "/", rel);
    return k > 0 && (size_t)k < max;
}

int tfn_ss_index_key(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t *msg, size_t n, const uint8_t *sig,
                     size_t sl, uint8_t pk[TFN_PUB_PK], char who[TFN_PUB_NAME])
{
    who[0] = 0;
    if (!sig || sl != 64) {
        return TFN_PUB_NONE;
    }
    for (int i = 0; i < TFN_PUB_BUILTIN; i++) {
        if (tfn_pub_key_set(o->builtin[i]) && o->verify(o->builtin[i], msg, n, sig)) {
            memcpy(pk, o->builtin[i], TFN_PUB_PK);
            return TFN_PUB_OK_BUILTIN;
        }
    }
    for (int i = 0; l && i < l->n; i++) {
        if ((l->e[i].flags & TFN_PUB_STORE) && o->verify(l->e[i].pk, msg, n, sig)) {
            memcpy(pk, l->e[i].pk, TFN_PUB_PK);
            snprintf(who, TFN_PUB_NAME, "%s", l->e[i].name);
            return TFN_PUB_OK_STORE;
        }
    }
    return TFN_PUB_NONE;
}

bool tfn_ss_file_ok(const tfn_pub_ops_t *o, const tfn_ss_file_t *want, const uint8_t *d, size_t n)
{
    uint8_t h[32];
    if (!want || !want->size || n != want->size || !d) {
        return false;
    }
    o->sha256(d, n, h);
    return !memcmp(h, want->sha, 32);
}

// ---------------------------------------------------------------- the JSON reader
typedef struct {
    char *p, *e;
    int depth;
    bool bad;
} js_t;

static int hexl(char c)  // a lower-case hex digit (the form of the index)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool hex_bytes(const char *t, uint8_t *out, size_t nb)
{
    if (!t || strlen(t) != 2 * nb) {
        return false;
    }
    for (size_t i = 0; i < nb; i++) {
        int a = hexl(t[2 * i]), b = hexl(t[2 * i + 1]);
        if (a < 0 || b < 0) {
            return false;
        }
        out[i] = (uint8_t)(a << 4 | b);
    }
    return true;
}

static bool hex_text(const char *t, size_t nd)
{
    if (!t || strlen(t) != nd) {
        return false;
    }
    for (size_t i = 0; i < nd; i++) {
        if (hexl(t[i]) < 0) {
            return false;
        }
    }
    return true;
}

static void js_ws(js_t *j)
{
    while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        j->p++;
    }
}

static bool js_eat(js_t *j, char c)
{
    js_ws(j);
    if (!j->bad && j->p < j->e && *j->p == c) {
        j->p++;
        return true;
    }
    return false;
}

static bool js_fail(js_t *j)
{
    j->bad = true;
    return false;
}

static bool js_hex4(js_t *j, uint32_t *u)
{
    *u = 0;
    for (int i = 0; i < 4; i++) {
        char c = j->p < j->e ? *j->p : 0;
        int v = hexl(c >= 'A' && c <= 'F' ? (char)(c - 'A' + 'a') : c);
        if (v < 0) {
            return false;
        }
        *u = *u << 4 | (uint32_t)v;
        j->p++;
    }
    return true;
}

// the UTF-8 bytes of the code point u at w
static char *utf8_put(char *w, uint32_t u)
{
    if (u < 0x80) {
        *w++ = (char)u;
    } else if (u < 0x800) {
        *w++ = (char)(0xc0 | u >> 6);
        *w++ = (char)(0x80 | (u & 0x3f));
    } else if (u < 0x10000) {
        *w++ = (char)(0xe0 | u >> 12);
        *w++ = (char)(0x80 | (u >> 6 & 0x3f));
        *w++ = (char)(0x80 | (u & 0x3f));
    } else {
        *w++ = (char)(0xf0 | u >> 18);
        *w++ = (char)(0x80 | (u >> 12 & 0x3f));
        *w++ = (char)(0x80 | (u >> 6 & 0x3f));
        *w++ = (char)(0x80 | (u & 0x3f));
    }
    return w;
}

// the code point of a "\u" escape (after the "u"), with the second half of a surrogate pair; 0 = not good
static uint32_t js_uesc(js_t *j)
{
    uint32_t u, lo;
    if (!js_hex4(j, &u) || !u || (u >= 0xdc00 && u <= 0xdfff)) {
        return 0;
    }
    if (u < 0xd800 || u > 0xdbff) {
        return u;
    }
    if (j->e - j->p < 6 || j->p[0] != '\\' || j->p[1] != 'u') {
        return 0;
    }
    j->p += 2;
    if (!js_hex4(j, &lo) || lo < 0xdc00 || lo > 0xdfff) {
        return 0;
    }
    return 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
}

// a string: its bytes in place, with a 0 at the end; NULL = not a good string
static char *js_str(js_t *j)
{
    static const char ESC_IN[] = "\"\\/bfnrt", ESC_OUT[] = "\"\\/\b\f\n\r\t";
    if (!js_eat(j, '"')) {
        js_fail(j);
        return NULL;
    }
    char *out = j->p, *w = j->p;
    while (j->p < j->e) {
        char c = *j->p++;
        if (c == '"') {
            *w = 0;
            return out;
        }
        if ((unsigned char)c < 0x20 || (c == '\\' && j->p >= j->e)) {
            break;
        }
        if (c != '\\') {
            *w++ = c;
            continue;
        }
        c = *j->p++;
        const char *k = c ? strchr(ESC_IN, c) : NULL;
        if (k) {
            *w++ = ESC_OUT[k - ESC_IN];
            continue;
        }
        uint32_t u = c == 'u' ? js_uesc(j) : 0;
        if (!u) {
            break;
        }
        w = utf8_put(w, u);
    }
    js_fail(j);
    return NULL;
}

// a whole number (no fraction, no exponent)
static bool js_int(js_t *j, long long *v)
{
    js_ws(j);
    bool neg = j->p < j->e && *j->p == '-';
    j->p += neg;
    int nd = 0;
    *v = 0;
    while (j->p < j->e && *j->p >= '0' && *j->p <= '9' && nd < NUM_DIGITS) {
        *v = *v * 10 + (*j->p++ - '0');
        nd++;
    }
    if (!nd || (j->p < j->e && *j->p && strchr("0123456789.eE", *j->p))) {
        return js_fail(j);
    }
    *v = neg ? -*v : *v;
    return true;
}

static void js_skip(js_t *j);

// the rest of an object or an array after its first character (c)
static void js_skip_open(js_t *j, char c)
{
    char close = c == '{' ? '}' : ']';
    if (++j->depth > JS_DEPTH) {
        js_fail(j);
        return;
    }
    if (!js_eat(j, close)) {
        do {
            if (c == '{' && (!js_str(j) || !js_eat(j, ':'))) {
                js_fail(j);
                return;
            }
            js_skip(j);
        } while (!j->bad && js_eat(j, ','));
        if (!js_eat(j, close)) {
            js_fail(j);
        }
    }
    j->depth--;
}

// skip one value of any type
static void js_skip(js_t *j)
{
    static const char *const WORDS[] = {"true", "false", "null"};
    js_ws(j);
    if (j->bad || j->p >= j->e) {
        js_fail(j);
        return;
    }
    char c = *j->p;
    if (c == '"') {
        js_str(j);
        return;
    }
    if (c == '{' || c == '[') {
        j->p++;
        js_skip_open(j, c);
        return;
    }
    for (int i = 0; i < 3; i++) {
        size_t l = strlen(WORDS[i]);
        if ((size_t)(j->e - j->p) >= l && !memcmp(j->p, WORDS[i], l)) {
            j->p += l;
            return;
        }
    }
    const char *s = j->p;
    while (j->p < j->e && *j->p && strchr("-+.eE0123456789", *j->p)) {
        j->p++;
    }
    if (j->p == s) {
        js_fail(j);
    }
}

// the key of the next member of an object (after its '{'), or NULL at its end or at a fault (j->bad)
static char *js_member(js_t *j, bool *first)
{
    if (j->bad || js_eat(j, '}')) {
        return NULL;
    }
    if (!*first && !js_eat(j, ',')) {
        js_fail(j);
        return NULL;
    }
    *first = false;
    char *k = js_str(j);
    if (!k || !js_eat(j, ':')) {
        js_fail(j);
        return NULL;
    }
    return k;
}

// true: the array (after its '[') has one more element
static bool js_elem(js_t *j, bool *first)
{
    if (j->bad || js_eat(j, ']')) {
        return false;
    }
    if (!*first && !js_eat(j, ',')) {
        return js_fail(j);
    }
    *first = false;
    return true;
}

// ---------------------------------------------------------------- the parser of index.json
// the facts of one app of the index (the entry of this board kind only)
typedef struct {
    char *id, *name, *ver, *group, *emoji, *desc, *desc_he, *path;
    long long code, abi, rev, bytes, ram;
    bool has_code, has_kinds, mine;
    char perms[PERMS_TEXT];
    tfn_ss_file_t f[TFN_SS_NFILES];
} app_t;

typedef struct {
    js_t j;
    const tfn_ss_opts_t *op;
    tfn_cat_t *out;
    tfn_ss_pkg_t *pkg;
    int max, n, more;
    char *msg;
    int len;
} ps_t;

static bool ps_fault(ps_t *s, const char *why, const char *what)
{
    if (!s->msg[0]) {
        snprintf(s->msg, s->len, "THE STORE INDEX IS NOT GOOD: %s%s%s", why, what ? " " : "", what ? what : "");
    }
    s->j.bad = true;
    return false;
}

static bool name_ok(const char *t, size_t max, bool under)
{
    size_t n = t ? strlen(t) : 0;
    if (!n || n > max) {
        return false;
    }
    for (; *t; t++) {
        if (!((*t >= 'a' && *t <= 'z') || (*t >= '0' && *t <= '9') || (under && *t == '_'))) {
            return false;
        }
    }
    return true;
}

// "files": {"<name>": {"size": n, "sha256": "<hex>"}, ...}; a name that the board does not know is skipped
static bool ps_files(ps_t *s, app_t *a)
{
    js_t *j = &s->j;
    bool first = true;
    if (!js_eat(j, '{')) {
        return ps_fault(s, "a file list is not an object", a->id);
    }
    for (char *k; (k = js_member(j, &first));) {
        int fi = -1;
        for (int i = 0; i < TFN_SS_NFILES; i++) {
            fi = strcmp(k, FILE_NAMES[i]) ? fi : i;
        }
        if (fi < 0) {
            js_skip(j);
            continue;
        }
        long long size = 0;
        char *sha = NULL;
        bool f1 = true;
        if (!js_eat(j, '{')) {
            return ps_fault(s, "a file is not an object", k);
        }
        for (char *m; (m = js_member(j, &f1));) {
            if (!strcmp(m, "size")) {
                js_int(j, &size);
            } else if (!strcmp(m, "sha256")) {
                sha = js_str(j);
            } else {
                js_skip(j);
            }
        }
        if (j->bad || size <= 0 || size > TFN_SS_FILE_MAX || !hex_bytes(sha, a->f[fi].sha, 32)) {
            return ps_fault(s, "a file has no size or SHA-256", a->id);
        }
        a->f[fi].size = (uint32_t)size;
    }
    return !j->bad;
}

// "perms": ["a", "b"] -> "a,b"
static bool ps_perms(ps_t *s, app_t *a)
{
    js_t *j = &s->j;
    bool first = true;
    size_t k = 0;
    if (!js_eat(j, '[')) {
        return ps_fault(s, "the permissions are not a list", a->id);
    }
    while (js_elem(j, &first)) {
        char *p = js_str(j);
        size_t l = p ? strlen(p) : 0;
        if (!p || k + l + 2 > sizeof a->perms) {
            return ps_fault(s, "the permissions are not good", a->id);
        }
        k += (size_t)snprintf(a->perms + k, sizeof a->perms - k, "%s%s", k ? "," : "", p);
    }
    return !j->bad;
}

// the entry of this board kind: {"path": .., "abi": n, "rev": n, "perms": [..], "bytes": n, "ram": n, "files": {..}}
static bool ps_kind(ps_t *s, app_t *a)
{
    js_t *j = &s->j;
    bool first = true;
    if (!js_eat(j, '{')) {
        return ps_fault(s, "a kind is not an object", a->id);
    }
    a->mine = true;
    for (char *k; (k = js_member(j, &first));) {
        if (!strcmp(k, "path")) {
            a->path = js_str(j);
        } else if (!strcmp(k, "abi")) {
            js_int(j, &a->abi);
        } else if (!strcmp(k, "rev")) {
            js_int(j, &a->rev);
        } else if (!strcmp(k, "bytes")) {
            js_int(j, &a->bytes);
        } else if (!strcmp(k, "ram")) {
            js_int(j, &a->ram);
        } else if (!strcmp(k, "perms")) {
            ps_perms(s, a);
        } else if (!strcmp(k, "files")) {
            ps_files(s, a);
        } else {
            js_skip(j);
        }
    }
    return !j->bad;
}

// "kinds": {"<kind>": {..}, ..}: only the entry of this board kind is read
static bool ps_kinds(ps_t *s, app_t *a)
{
    js_t *j = &s->j;
    bool first = true;
    if (!js_eat(j, '{')) {
        return ps_fault(s, "the kinds are not an object", a->id);
    }
    a->has_kinds = true;
    for (char *k; (k = js_member(j, &first));) {
        if (!name_ok(k, KIND_ID_MAX, false)) {
            return ps_fault(s, "a kind name is not good", k);
        }
        if (!strcmp(k, s->op->kind)) {
            ps_kind(s, a);
        } else {
            js_skip(j);
        }
    }
    return !j->bad;
}

// a text field of an app: a string, else a fault
static bool ps_text(ps_t *s, char **out)
{
    *out = js_str(&s->j);
    return *out ? true : ps_fault(s, "a text field is not a string", NULL);
}

// the catalog entry and the files of one app of this kind (the form is good): false = the app is not for this board
static bool ps_take(ps_t *s, const app_t *a)
{
    char path[64];
    snprintf(path, sizeof path, "apps/%s/%s/", a->id, s->op->kind);
    if (!a->path || strcmp(a->path, path)) {
        return ps_fault(s, "the path is not apps/<id>/<kind>/", a->id);
    }
    if (!a->f[TFN_SS_F_MANIFEST].size || !a->f[TFN_SS_F_ELF].size || !a->f[TFN_SS_F_SIG].size) {
        return ps_fault(s, "a package has no manifest.txt, app.elf or sig", a->id);
    }
    for (int i = 0; i < s->n; i++) {
        if (!strcmp(s->pkg[i].id, a->id)) {
            return ps_fault(s, "an app id is two times in the index", a->id);
        }
    }
    bool unknown = false;
    uint32_t perms = s->op->perms ? s->op->perms(a->perms, &unknown) : 0;
    // (as the manager: a package of another ABI, a newer ABI rev or a class of a newer firmware is not for this board)
    if ((s->op->abi && a->abi && a->abi != s->op->abi) || a->rev > s->op->rev || unknown) {
        return false;
    }
    if (s->n >= s->max) {
        s->more++;
        return false;
    }
    tfn_cat_t *c = &s->out[s->n];
    memset(c, 0, sizeof *c);
    snprintf(c->id, sizeof c->id, "%s", a->id);
    snprintf(c->name, sizeof c->name, "%s", a->name);
    snprintf(c->ver, sizeof c->ver, "%s", a->ver);
    snprintf(c->group, sizeof c->group, "%s", a->group);
    snprintf(c->emoji, sizeof c->emoji, "%s", a->emoji ? a->emoji : "");
    c->code = (uint32_t)a->code;
    c->bytes = (uint32_t)a->bytes;
    c->ram = (uint32_t)a->ram;
    c->perms = perms;
    c->state = TFN_ST_AVAILABLE;
    size_t le = a->desc ? strlen(a->desc) : 0, lh = a->desc_he ? strlen(a->desc_he) : 0;
    if (le + lh && s->op->alloc) {
        c->desc = s->op->alloc(le + lh + 2);
        if (c->desc) {
            memcpy(c->desc, le ? a->desc : "", le + 1);
            memcpy(c->desc + le + 1, lh ? a->desc_he : "", lh + 1);
        }
    }
    memset(&s->pkg[s->n], 0, sizeof s->pkg[s->n]);
    snprintf(s->pkg[s->n].id, sizeof s->pkg[s->n].id, "%s", a->id);
    memcpy(s->pkg[s->n].f, a->f, sizeof a->f);
    s->n++;
    return true;
}

// one member of an app object
static void ps_app_member(ps_t *s, app_t *a, const char *k)
{
    char **text[] = {&a->id, &a->name, &a->ver, &a->group, &a->emoji, &a->desc, &a->desc_he};
    static const char *const TEXT[] = {"id", "name", "ver", "group", "emoji", "desc", "desc_he"};
    for (size_t i = 0; i < sizeof TEXT / sizeof TEXT[0]; i++) {
        if (!strcmp(k, TEXT[i])) {
            ps_text(s, text[i]);
            return;
        }
    }
    if (!strcmp(k, "code")) {
        a->has_code = js_int(&s->j, &a->code);
    } else if (!strcmp(k, "kinds")) {
        ps_kinds(s, a);
    } else {
        js_skip(&s->j);
    }
}

// one app: {"id": .., "name": .., "ver": .., "code": n, "group": .., "emoji": .., "desc": .., "desc_he": ..,
// "kinds": {..}}
static bool ps_app(ps_t *s)
{
    js_t *j = &s->j;
    app_t a;
    bool first = true;
    memset(&a, 0, sizeof a);
    if (!js_eat(j, '{')) {
        return ps_fault(s, "an app is not an object", NULL);
    }
    for (char *k; (k = js_member(j, &first));) {
        ps_app_member(s, &a, k);
    }
    if (j->bad) {
        return ps_fault(s, "the JSON form", a.id);
    }
    if (!name_ok(a.id, APP_ID_MAX, true) || !a.name || !a.ver || !a.group || !a.has_code || !a.has_kinds) {
        return ps_fault(s, "an app has no id, name, ver, code, group or kinds", a.id);
    }
    if (a.mine) {
        ps_take(s, &a);
    }
    return !j->bad;
}

// the members of the root object
static bool ps_root(ps_t *s, const uint8_t pk[TFN_PUB_PK], bool *apps)
{
    js_t *j = &s->j;
    bool first = true, form = false, key = false, kid = false;
    if (!js_eat(j, '{')) {
        return ps_fault(s, "the root is not an object", NULL);
    }
    for (char *k; (k = js_member(j, &first));) {
        long long v = 0;
        uint8_t pt[TFN_PUB_PK];
        if (!strcmp(k, "tfn_store")) {
            form = js_int(j, &v) && v == TFN_SS_FORMAT;
        } else if (!strcmp(k, "pubkey")) {
            char *t = js_str(j);
            key = t && strlen(t) == PUB_HEX && hex_bytes(t, pt, TFN_PUB_PK) && !memcmp(pt, pk, TFN_PUB_PK);
        } else if (!strcmp(k, "key_id")) {
            kid = hex_text(js_str(j), KEY_ID_HEX);
        } else if (!strcmp(k, "apps")) {
            bool f1 = true;
            if (!js_eat(j, '[')) {
                return ps_fault(s, "the apps are not a list", NULL);
            }
            while (js_elem(j, &f1) && ps_app(s)) {
                *apps = true;
            }
        } else {
            js_skip(j);
        }
    }
    if (j->bad) {
        return ps_fault(s, "the JSON form", NULL);
    }
    if (!form) {
        return ps_fault(s, "it is not a store index of form 1", NULL);
    }
    if (!key || !kid) {
        return ps_fault(s, "it names another key than the key of its signature", NULL);
    }
    return true;
}

int tfn_ss_parse(char *j, size_t n, const tfn_ss_opts_t *op, const uint8_t pk[TFN_PUB_PK], tfn_cat_t *out,
                 tfn_ss_pkg_t *pkg, int max, char *msg, int len)
{
    ps_t s = {.j = {.p = j, .e = j + n}, .op = op, .out = out, .pkg = pkg, .max = max, .msg = msg, .len = len};
    bool apps = false;
    msg[0] = 0;
    if (!j || !op || !op->kind || !out || !pkg || max < 0) {
        snprintf(msg, len, "THE STORE INDEX IS NOT GOOD");
        return -1;
    }
    bool ok = ps_root(&s, pk, &apps);
    js_ws(&s.j);
    if (ok && s.j.p != s.j.e) {
        ok = ps_fault(&s, "there is text after the JSON", NULL);
    }
    if (ok && !apps) {
        ok = ps_fault(&s, "it has no apps", NULL);
    }
    if (!ok) {
        tfn_store_cat_free(out, s.n);
        return -1;
    }
    if (s.more) {
        snprintf(msg, len, "the index has %d more apps than the board lists", s.more);
    }
    return s.n;
}
