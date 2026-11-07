// Settings of the GAMEPAD app (see pad_config.h): 6 profiles, the active profile and
// the Bluetooth name, in NVS, plus the web page that edits them.
//
// NVS: only 32-bit values (store_get / store_set). A profile is 3 words of bit fields
// ("pad_<i>a".."pad_<i>c") and 3 words of name ("pad_<i>d".."pad_<i>f"); the Bluetooth
// name is 8 words ("pad_nm0".."pad_nm7"); "pad_act" is the active profile and
// "pad_ver" is the version of the data.
// Version 1 (before RETRO MOVE) had 5 profiles; version 2 puts RETRO MOVE in place 2 and
// moves the others one place on. The first start after the update moves the saved data.
// Before this change "pad_act" was read only when "pad_ver" was there (only a save on the
// web page wrote it), so without a web save the app opened with profile 1 each time; now
// the first start writes all the data, and the app opens with the last profile.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bt_pad.h"
#include "settings.h"
#include "pad_config.h"

#define DEFAULT_BT_NAME "TFN PAD"
#define OLD_BT_NAME     "ARCADE PAD"  // the name of an older firmware: no own name
#define DATA_VERSION    2
#define OLD_PROFILES    5  // version 1

static pad_profile_t s_prof[PAD_PROFILES];
static int s_active;
static char s_bt_name[PAD_BT_NAME_MAX + 1];
static volatile uint32_t s_version;

static const pad_profile_t DEFAULTS[PAD_PROFILES] = {
        // name          mode              dirs dead range auto fire usey invx invy touch           bl br  gu gd gl gr  gk gs gn  hg click mstyle sens
        {"RETRO", PAD_MODE_RETRO, 8, 6, 25, 0, 1, 1, 0, 0, PAD_TOUCH_HALVES, 1, 2, 0, 0, 0, 0, 0, 0, 0, 20, 1, 0, 5},
        {"RETRO MOVE",
         PAD_MODE_MOVE,
         8,
         6,
         25,
         0,
         1,
         1,
         0,
         0,
         PAD_TOUCH_HALVES,
         1,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         20,
         1,
         0,
         5},
        {"CLASSIC", PAD_MODE_BUTTONS, 8, 6, 25, 0, 1, 1, 0, 0, PAD_TOUCH_HALVES, 1, 2, 0, 0, 0, 0, 0, 0, 0, 20, 1, 0,
         5},
        {"RACING", PAD_MODE_MOTION, 8, 3, 35, 0, 1, 0, 0, 0, PAD_TOUCH_HALVES, 2, 1, 3, 0, 0, 0, 4, 0, 0, 20, 0, 0, 5},
        {"MOTION", PAD_MODE_MOTION, 8, 4, 30, 0, 1, 1, 0, 0, PAD_TOUCH_HALVES, 1, 2, 3, 4, 5, 6, 7, 8, 9, 20, 0, 0, 5},
        {"CUSTOM", PAD_MODE_MOTION, 8, 4, 30, 0, 1, 1, 0, 0, PAD_TOUCH_PAD, 1, 2, 2, 0, 0, 0, 3, 0, 0, 20, 0, 0, 5},
};

// The default names of an older firmware, in the places of DEFAULTS. A board keeps the profile names in NVS: a saved
// name that is the same as the old default of its place is not an own name, so the new default name shows (as
// OLD_BT_NAME). The data of the profile does not change.
static const char *const OLD_NAMES[PAD_PROFILES] = {"ATARI", "ATARI MOVE", NULL, NULL, NULL, NULL};

// version 1 place -> version 2 place (RETRO MOVE is new in place 2)
static int new_place(int old)
{
    return old == 0 ? 0 : old + 1;
}

// The number fields of a profile: the form/JSON key, the place, the range, and the
// bits in NVS (dirs is stored as one bit: 0 = 4, 1 = 8).
typedef struct {
    const char *key;
    uint8_t off, lo, hi, bits;
} field_t;

#define F(k, m, lo, hi, b) {k, (uint8_t)offsetof(pad_profile_t, m), lo, hi, b}
static const field_t FIELDS[] = {
        F("mode", mode, 0, PAD_MODES - 1, 2),
        F("dirs", dirs, 4, 8, 1),
        F("dead", dead, 0, 30, 5),
        F("range", range, 5, 60, 6),
        F("auto", autofire, 0, 20, 5),
        F("fire", fire, 1, 16, 5),
        F("usey", usey, 0, 1, 1),
        F("invx", invx, 0, 1, 1),
        F("invy", invy, 0, 1, 1),
        F("touch", touch, 0, PAD_TOUCHES - 1, 2),
        F("bl", bl, 0, 16, 5),
        F("br", br, 0, 16, 5),
        F("gu", gu, 0, 16, 5),
        F("gd", gd, 0, 16, 5),
        F("gl", gl, 0, 16, 5),
        F("gr", gr, 0, 16, 5),
        F("gk", gk, 0, 16, 5),
        F("gs", gs, 0, 16, 5),
        F("gn", gn, 0, 16, 5),
        F("hg", hg, 13, 60, 6),
        F("click", click, 0, 1, 1),
        // new fields go at the end: the bits of the old data stay in their places
        F("mstyle", mstyle, 0, 1, 1),
        F("msens", msens, 1, 9, 4),
};
#define N_FIELDS ((int)(sizeof(FIELDS) / sizeof(FIELDS[0])))

static uint8_t *field_ptr(pad_profile_t *p, const field_t *f)
{
    return (uint8_t *)p + f->off;
}

static bool field_ok(const field_t *f, int v)
{
    if (v < f->lo || v > f->hi) {
        return false;
    }
    if (!strcmp(f->key, "dirs")) {
        return v == 4 || v == 8;
    }
    return true;
}

// a whole profile is valid (range is more than the dead zone)
static bool profile_ok(const pad_profile_t *p)
{
    for (int i = 0; i < N_FIELDS; i++) {
        if (!field_ok(&FIELDS[i], *field_ptr((pad_profile_t *)p, &FIELDS[i]))) {
            return false;
        }
    }
    return p->range > p->dead + 1 && p->name[0];
}

// copy a text that is already checked; it is cut at the size of dst
static void copy_text(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

// printable ASCII only, no space at the start or the end
static bool clean_text(char *s, int max)
{
    int n = (int)strlen(s);
    if (n > max) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) {
            return false;
        }
    }
    while (n > 0 && s[n - 1] == ' ') {
        s[--n] = 0;
    }
    int a = 0;
    while (s[a] == ' ') {
        a++;
    }
    if (a) {
        memmove(s, s + a, (size_t)(n - a + 1));
    }
    return s[0] != 0;
}

// ---------------------------------------------------------------- NVS

static void put_text(const char *fmt, int idx, const char *s, int words)
{
    char key[16];
    for (int w = 0; w < words; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++) {
            int k = w * 4 + b;
            unsigned char ch = (k < (int)strlen(s)) ? (unsigned char)s[k] : 0;
            v |= (uint32_t)ch << (8 * b);
        }
        snprintf(key, sizeof(key), fmt, idx, w);
        store_set(key, v);
    }
}

static void get_text(const char *fmt, int idx, char *s, int words)
{
    char key[16];
    for (int w = 0; w < words; w++) {
        snprintf(key, sizeof(key), fmt, idx, w);
        uint32_t v = store_get(key, 0);
        for (int b = 0; b < 4; b++) {
            s[w * 4 + b] = (char)((v >> (8 * b)) & 0xFF);
        }
    }
    s[words * 4 - 1] = 0;
}

static void pack(const pad_profile_t *p, uint32_t w[3])
{
    w[0] = w[1] = w[2] = 0;
    int bit = 0;
    for (int i = 0; i < N_FIELDS; i++) {
        const field_t *f = &FIELDS[i];
        uint32_t v = *field_ptr((pad_profile_t *)p, f);
        if (!strcmp(f->key, "dirs")) {
            v = v == 8;
        }
        for (int b = 0; b < f->bits; b++, bit++) {
            if (v & (1u << b)) {
                w[bit / 32] |= 1u << (bit % 32);
            }
        }
    }
}

static void unpack(const uint32_t w[3], pad_profile_t *p, const pad_profile_t *def)
{
    int bit = 0;
    for (int i = 0; i < N_FIELDS; i++) {
        const field_t *f = &FIELDS[i];
        uint32_t v = 0;
        for (int b = 0; b < f->bits; b++, bit++) {
            if (w[bit / 32] & (1u << (bit % 32))) {
                v |= 1u << b;
            }
        }
        if (!strcmp(f->key, "dirs")) {
            v = v ? 8 : 4;
        }
        uint8_t *dst = field_ptr(p, f);
        *dst = field_ok(f, (int)v) ? (uint8_t)v : *field_ptr((pad_profile_t *)def, f);
    }
}

static void save_all(void)
{
    char key[16];
    for (int i = 0; i < PAD_PROFILES; i++) {
        uint32_t w[3];
        pack(&s_prof[i], w);
        for (int k = 0; k < 3; k++) {
            snprintf(key, sizeof(key), "pad_%d%c", i, 'a' + k);
            store_set(key, w[k]);
        }
        // name words: pad_<i>d, pad_<i>e, pad_<i>f
        for (int k = 0; k < 3; k++) {
            uint32_t v = 0;
            for (int b = 0; b < 4; b++) {
                int c = k * 4 + b;
                unsigned char ch = c < (int)strlen(s_prof[i].name) ? (unsigned char)s_prof[i].name[c] : 0;
                v |= (uint32_t)ch << (8 * b);
            }
            snprintf(key, sizeof(key), "pad_%d%c", i, 'd' + k);
            store_set(key, v);
        }
    }
    put_text("pad_nm%d%d", 0, s_bt_name, 8);
    store_set("pad_act", (uint32_t)s_active);
    store_set("pad_ver", DATA_VERSION);
}

// one saved profile (place i in NVS) into p; def gives the values of bad or missing fields
static void load_one(int i, pad_profile_t *p, const pad_profile_t *def)
{
    char key[16];
    uint32_t w[3];
    for (int k = 0; k < 3; k++) {
        snprintf(key, sizeof(key), "pad_%d%c", i, 'a' + k);
        w[k] = store_get(key, 0);
    }
    *p = *def;
    unpack(w, p, def);
    char name[13];
    for (int k = 0; k < 3; k++) {
        snprintf(key, sizeof(key), "pad_%d%c", i, 'd' + k);
        uint32_t v = store_get(key, 0);
        for (int b = 0; b < 4; b++) {
            name[k * 4 + b] = (char)((v >> (8 * b)) & 0xFF);
        }
    }
    name[12] = 0;
    const char *old = OLD_NAMES[def - DEFAULTS];
    if (clean_text(name, PAD_NAME_MAX) && !(old && !strcmp(name, old))) {
        copy_text(p->name, sizeof(p->name), name);
    }
    if (p->range <= p->dead + 1) {
        p->dead = def->dead;
        p->range = def->range;
    }
}

void pad_config_load(void)
{
    memcpy(s_prof, DEFAULTS, sizeof(s_prof));
    copy_text(s_bt_name, sizeof(s_bt_name), DEFAULT_BT_NAME);
    s_active = 0;
    uint32_t ver = store_get("pad_ver", 0);
    uint32_t a = store_get("pad_act", 0);
    if (ver == DATA_VERSION) {
        for (int i = 0; i < PAD_PROFILES; i++) {
            load_one(i, &s_prof[i], &DEFAULTS[i]);
        }
        s_active = a < PAD_PROFILES ? (int)a : 0;
    } else if (ver == 1) {
        // version 1: 5 profiles in the old places
        for (int i = 0; i < OLD_PROFILES; i++) {
            load_one(i, &s_prof[new_place(i)], &DEFAULTS[new_place(i)]);
        }
        s_active = a < OLD_PROFILES ? new_place((int)a) : 0;
    } else {
        // no saved profiles: only the active one was saved (in the old places)
        s_active = a < OLD_PROFILES ? new_place((int)a) : 0;
    }
    if (ver == DATA_VERSION || ver == 1) {
        char bt[33];
        get_text("pad_nm%d%d", 0, bt, 8);
        if (clean_text(bt, PAD_BT_NAME_MAX) && strcmp(bt, OLD_BT_NAME)) {
            copy_text(s_bt_name, sizeof(s_bt_name), bt);
        }
    }
    if (ver != DATA_VERSION) {
        save_all();  // the data of this version, once
    }
    bt_pad_set_name(s_bt_name);
    s_version++;
}

// ---------------------------------------------------------------- for the app

const pad_profile_t *pad_profile(int i)
{
    return &s_prof[(i < 0 || i >= PAD_PROFILES) ? 0 : i];
}

int pad_active(void)
{
    return s_active;
}

void pad_set_active(int i)
{
    if (i < 0 || i >= PAD_PROFILES || i == s_active) {
        return;
    }
    s_active = i;
    store_set("pad_act", (uint32_t)i);
    s_version++;
}

void pad_set_move_style(int style)
{
    if (style < 0 || style > 1 || s_prof[s_active].mstyle == style) {
        return;
    }
    s_prof[s_active].mstyle = (uint8_t)style;
    save_all();
    s_version++;
}

const char *pad_bt_name(void)
{
    return s_bt_name;
}

void pad_config_keep_bt_name(const char *name)
{
    char t[PAD_BT_NAME_MAX + 2];
    if (!name) {
        return;
    }
    copy_text(t, sizeof(t), name);
    if (!clean_text(t, PAD_BT_NAME_MAX) || !strcmp(t, s_bt_name)) {
        return;
    }
    copy_text(s_bt_name, sizeof(s_bt_name), t);
    put_text("pad_nm%d%d", 0, s_bt_name, 8);
    s_version++;
}

uint32_t pad_config_version(void)
{
    return s_version;
}

// ---------------------------------------------------------------- JSON

typedef struct {
    char *buf;
    int len, n;
} out_t;

static void out_s(out_t *o, const char *s)
{
    for (; *s; s++) {
        if (o->n < o->len - 1) {
            o->buf[o->n] = *s;
        }
        o->n++;
    }
}

// a JSON string with escapes; < > & also escaped, so the text is safe in a page
static void out_str(out_t *o, const char *s)
{
    out_s(o, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char e[8];
        if (c == '"' || c == '\\') {
            e[0] = '\\';
            e[1] = (char)c;
            e[2] = 0;
        } else if (c < 0x20 || c == '<' || c == '>' || c == '&' || c > 0x7E) {
            snprintf(e, sizeof(e), "\\u%04x", c);
        } else {
            e[0] = (char)c;
            e[1] = 0;
        }
        out_s(o, e);
    }
    out_s(o, "\"");
}

static void out_int(out_t *o, int v)
{
    char t[12];
    snprintf(t, sizeof(t), "%d", v);
    out_s(o, t);
}

int pad_config_json(char *buf, int len)
{
    if (!buf || len <= 0) {
        return 0;
    }
    out_t o = {buf, len, 0};
    out_s(&o, "{\"name\":");
    out_str(&o, s_bt_name);
    out_s(&o, ",\"active\":");
    out_int(&o, s_active);
    out_s(&o, ",\"profiles\":[");
    for (int i = 0; i < PAD_PROFILES; i++) {
        const pad_profile_t *p = &s_prof[i];
        out_s(&o, i ? ",{\"name\":" : "{\"name\":");
        out_str(&o, p->name);
        for (int k = 0; k < N_FIELDS; k++) {
            out_s(&o, ",\"");
            out_s(&o, FIELDS[k].key);
            out_s(&o, "\":");
            out_int(&o, *field_ptr((pad_profile_t *)p, &FIELDS[k]));
        }
        out_s(&o, "}");
    }
    out_s(&o, "]}");
    int n = o.n < len - 1 ? o.n : len - 1;
    buf[n] = 0;
    return n;
}

// ---------------------------------------------------------------- the form

static int hexval(char c)
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

// URL-decode a..b into out (max - 1 characters). Returns false if it is too long or bad.
static bool url_decode(const char *a, const char *b, char *out, int max)
{
    int n = 0;
    while (a < b) {
        char c = *a++;
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (b - a < 2) {
                return false;
            }
            int h = hexval(a[0]), l = hexval(a[1]);
            if (h < 0 || l < 0) {
                return false;
            }
            c = (char)(h * 16 + l);
            a += 2;
        }
        if (n >= max - 1) {
            return false;
        }
        out[n++] = c;
    }
    out[n] = 0;
    return true;
}

static bool parse_int(const char *s, int *v)
{
    if (!*s) {
        return false;
    }
    char *end;
    long x = strtol(s, &end, 10);
    if (*end || x < -1000 || x > 1000) {
        return false;
    }
    *v = (int)x;
    return true;
}

// one field of the form into the new settings
static bool apply(pad_profile_t *np, char *name, int *act, const char *k, char *v)
{
    if (!strcmp(k, "name")) {
        if (!clean_text(v, PAD_BT_NAME_MAX)) {
            return false;
        }
        copy_text(name, PAD_BT_NAME_MAX + 1, v);
        return true;
    }
    if (!strcmp(k, "active")) {
        int a;
        if (!parse_int(v, &a) || a < 0 || a >= PAD_PROFILES) {
            return false;
        }
        *act = a;
        return true;
    }
    if (k[0] != 'p' || k[1] < '0' || k[1] >= '0' + PAD_PROFILES || k[2] != '_') {
        return true;  // not ours
    }
    pad_profile_t *p = &np[k[1] - '0'];
    const char *f = k + 3;
    if (!strcmp(f, "name")) {
        if (!clean_text(v, PAD_NAME_MAX)) {
            return false;
        }
        copy_text(p->name, sizeof(p->name), v);
        return true;
    }
    for (int i = 0; i < N_FIELDS; i++) {
        if (strcmp(f, FIELDS[i].key)) {
            continue;
        }
        int x;
        if (!parse_int(v, &x) || !field_ok(&FIELDS[i], x)) {
            return false;
        }
        *field_ptr(p, &FIELDS[i]) = (uint8_t)x;
        return true;
    }
    return true;  // an unknown field: no effect
}

bool pad_config_set_form(const char *body, int len)
{
    if (!body || len < 0 || len > 8192) {
        return false;
    }
    pad_profile_t np[PAD_PROFILES];
    memcpy(np, s_prof, sizeof(np));
    char name[PAD_BT_NAME_MAX + 1];
    copy_text(name, sizeof(name), s_bt_name);
    int act = s_active;
    const char *p = body, *end = body + len;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        if (!amp) {
            amp = end;
        }
        const char *eq = memchr(p, '=', (size_t)(amp - p));
        if (eq) {
            char k[24], v[72];
            if (!url_decode(p, eq, k, sizeof(k)) || !url_decode(eq + 1, amp, v, sizeof(v))) {
                return false;
            }
            if (!apply(np, name, &act, k, v)) {
                return false;
            }
        }
        p = amp + 1;
    }
    for (int i = 0; i < PAD_PROFILES; i++) {
        if (!profile_ok(&np[i])) {
            return false;
        }
    }
    memcpy(s_prof, np, sizeof(s_prof));
    s_active = act;
    copy_text(s_bt_name, sizeof(s_bt_name), name);
    save_all();
    bt_pad_set_name(s_bt_name);
    s_version++;
    return true;
}

// ---------------------------------------------------------------- the web page

static const char PAGE[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Gamepad</title><style>"
        "body{font-family:system-ui,sans-serif;margin:0;background:#0d0f1e;color:#e8ecff}"
        "header{background:linear-gradient(90deg,#3a1d6e,#123d6e);padding:1.2em 1.5em}"
        "h1{margin:0;font-size:1.5em}h2{margin:1.2em 1.5em .4em;color:#ffd84a;font-size:1.1em}"
        ".c{margin:.5em 1.5em;padding:.8em 1em;background:#181c34;border-radius:10px}"
        ".s{color:#8a93c0;font-size:.85em}a{color:#7fb0ff}"
        ".t{display:flex;flex-wrap:wrap;gap:.4em;margin:.5em 1.5em}"
        ".t button{background:#232848;color:#e8ecff;border:0;border-radius:8px;padding:.5em "
        ".9em;cursor:pointer;font-size:.95em}"
        ".t button.on{background:#2d6cdf}.t i{color:#6f6;font-style:normal;font-size:.8em}"
        "label{display:flex;justify-content:space-between;align-items:center;gap:1em;padding:.4em 0;border-bottom:1px "
        "solid #232848}"
        "label span{flex:1}"
        "input,select{background:#0d0f1e;color:#e8ecff;border:1px solid #39407a;border-radius:6px;padding:.3em "
        ".5em;font-size:1em}"
        "input[type=range]{width:9em}output{display:inline-block;width:3em;text-align:right;color:#ffd84a}"
        ".b{background:#1e9e5a;color:#fff;border:0;border-radius:6px;padding:.6em 1.4em;font-size:1em;cursor:pointer}"
        "h3{margin:.8em 0 .2em;color:#9fb4ff;font-size:.95em}#msg{margin-left:1em}"
        "</style>"
        "<style>.emb header,.emb "
        "nav{display:none}</style><script>if(window!==top)document.documentElement.classList.add('emb')</script>"
        "</head><body><header><h1>&#127918; Gamepad</h1>"
        "<div class=s>Bluetooth controller &middot; 6 profiles &middot; <a href='/'>files</a></div></header>"
        "<h2>Bluetooth</h2><div class=c>"
        "<label><span>Name in the Bluetooth list</span><input id=name maxlength=29></label>"
        "<label><span>Active profile on the board</span><select id=active></select></label>"
        "<div class=s>A computer or phone that knows the old name can show it until you pair again.</div></div>"
        "<h2>Profiles</h2><div class=t id=tabs></div><div class=c id=ed></div>"
        "<div class=c><button class=b onclick=save()>Save</button><span id=msg class=s></span></div>"
        "<script>"
        "let D,cur=0;"
        "const MODES=['Retro joystick: tilt to move, touch = fire','Buttons: d-pad, A B X Y, select, start',"
        "'Motion: tilt = stick, gestures = buttons','Retro move: move the board (not tilt) to move, touch = fire'];"
        "const MSTYLE=['Hold: pressed while the board moves','Latch: stays on until the next move (lift the board to "
        "let go)'];"
        "const BTN=['none'];for(let i=1;i<=16;i++)BTN.push('Button '+i);"
        "const TOUCH=['Two halves: two buttons','Touchpad: right stick (tap = left button)','Whole screen: one "
        "button'];"
        // key, label, kind, modes (1 retro, 2 buttons, 4 motion, 8 retro move), extra
        "const F=["
        "['name','Profile name','text',15],"
        "['mode','Mode','sel',15,MODES],"
        "['dirs','Directions','dirs',11],"
        "['dead','Dead zone (degrees)','rng',5,[0,30,1]],"
        "['range','Tilt for a full move (degrees)','rng',5,[5,60,1]],"
        "['mstyle','Move style','sel',8,MSTYLE],"
        "['msens','Move sensitivity (1 = big moves only, 9 = small moves)','rng',8,[1,9,1]],"
        "['invx','Invert left / right','chk',13],"
        "['invy','Invert forward / back','chk',13],"
        "['fire','Fire button','btn1',9],"
        "['auto','Auto fire (shots per second, 0 = off)','rng',9,[0,20,1]],"
        "['usey','Forward / back tilt moves the stick too','chk',4],"
        "['touch','Screen','sel',4,TOUCH],"
        "['bl','Left half (or tap / whole screen) button','btn',4],"
        "['br','Right half button','btn',4],"
        "['h','Gestures (Retro move: only the knock, because each move is a direction)','head',15],"
        "['gu','Jerk up','btn',7],['gd','Jerk down','btn',7],['gl','Jerk left','btn',7],['gr','Jerk right','btn',7],"
        "['gk','Knock on the back','btn',15],['gs','Hard shake','btn',7],['gn','Snap (fast move, then stop)','btn',7],"
        "['hg','Jerk strength necessary (g)','rngg',15,[13,60,1]],"
        "['click','Click sound for screen buttons','chk',15]];"
        "function opt(s,list,v,off){list.forEach((t,i)=>{const o=document.createElement('option');"
        "o.value=i+off;o.textContent=t;if(i+off==v)o.selected=true;s.appendChild(o)})}"
        "function tabs(){const t=document.getElementById('tabs');t.innerHTML='';"
        "D.profiles.forEach((p,i)=>{const b=document.createElement('button');b.textContent=(i+1)+'. '+p.name;"
        "if(i==D.active){const m=document.createElement('i');m.textContent=' ACTIVE';b.appendChild(m)}"
        "if(i==cur)b.className='on';b.onclick=()=>{cur=i;tabs();ed()};t.appendChild(b)});"
        "const a=document.getElementById('active');a.innerHTML='';"
        "opt(a,D.profiles.map((p,i)=>(i+1)+'. '+p.name),D.active,0);a.onchange=()=>{D.active=+a.value;tabs()}}"
        "function ed(){const p=D.profiles[cur],e=document.getElementById('ed'),bit=1<<p.mode;e.innerHTML='';"
        "F.forEach(f=>{if(!(f[3]&bit))return;"
        "if(f[2]=='head'){const h=document.createElement('h3');h.textContent=f[1];e.appendChild(h);return}"
        "const l=document.createElement('label'),s=document.createElement('span');s.textContent=f[1];l.appendChild(s);"
        "let c;const k=f[0],v=p[k];"
        "if(f[2]=='text'){c=document.createElement('input');c.maxLength=12;c.value=v;c.oninput=()=>{p.name=c.value;"
        "tabs()}}"
        "else if(f[2]=='sel'){c=document.createElement('select');opt(c,f[4],v,0);"
        "c.onchange=()=>{p[k]=+c.value;if(k=='mode')ed()}}"
        "else if(f[2]=='dirs'){c=document.createElement('select');opt(c,['4 (up, down, left, right)','8 (with "
        "diagonals)'],v==8?1:0,0);"
        "c.onchange=()=>{p.dirs=c.value==1?8:4}}"
        "else if(f[2]=='btn'||f[2]=='btn1'){c=document.createElement('select');"
        "if(f[2]=='btn')opt(c,BTN,v,0);else opt(c,BTN.slice(1),v,1);c.onchange=()=>{p[k]=+c.value}}"
        "else "
        "if(f[2]=='chk'){c=document.createElement('input');c.type='checkbox';c.checked=!!v;c.onchange=()=>{p[k]=c."
        "checked?1:0}}"
        "else{const "
        "w=document.createElement('span'),r=document.createElement('input'),o=document.createElement('output');"
        "r.type='range';r.min=f[4][0];r.max=f[4][1];r.step=f[4][2];r.value=v;"
        "const sh=()=>{o.textContent=f[2]=='rngg'?(r.value/10).toFixed(1):r.value};sh();"
        "r.oninput=()=>{p[k]=+r.value;sh()};w.appendChild(r);w.appendChild(o);c=w}"
        "l.appendChild(c);e.appendChild(l)})}"
        "async function load(){D=await (await fetch('/api/pad')).json();"
        "document.getElementById('name').value=D.name;tabs();ed()}"
        "async function save(){const m=document.getElementById('msg');"
        "const u=new URLSearchParams();u.set('name',document.getElementById('name').value);u.set('active',D.active);"
        "D.profiles.forEach((p,i)=>{for(const k in p)u.set('p'+i+'_'+k,p[k])});"
        "for(const p of D.profiles){if(p.range<=p.dead+1){m.textContent='The full-move tilt must be more than the dead "
        "zone.';return}"
        "if(!p.name.trim()){m.textContent='A profile needs a name.';return}}"
        "m.textContent='Saving...';"
        "try{const r=await fetch('/api/pad',{method:'POST',body:u});"
        "m.textContent=r.ok?'Saved. The board uses it now.':'The board did not accept the values.';if(r.ok)load()}"
        "catch(x){m.textContent='No connection to the board.'}}"
        "load();"
        "</script></body></html>";

const char *pad_config_page(void)
{
    return PAGE;
}
