// TFNCOMP: the frames, the parts, the pins and the tags (tfncomp.h)
#include "tfncomp.h"
#include <string.h>

// ---------------------------------------------------------------- the names
int tc_role_class(int role)
{
    switch (role) {
    case TC_R_IR_RX:
    case TC_R_RF315_RX:
    case TC_R_RF433_RX:
        return TC_C_PULSE_RX;
    case TC_R_IR_TX:
    case TC_R_RF315_TX:
    case TC_R_RF433_TX:
        return TC_C_PULSE_TX;
    case TC_R_RELAY:
    case TC_R_OUTPUT:
        return TC_C_OUT;
    case TC_R_BUTTON:
    case TC_R_MOTION:
    case TC_R_CONTACT:
        return TC_C_IN;
    case TC_R_ANALOG:
        return TC_C_ADC;
    default:
        return TC_C_NONE;
    }
}

bool tc_role_rx(int role)
{
    return tc_role_class(role) == TC_C_PULSE_RX;
}

bool tc_role_tx(int role)
{
    return tc_role_class(role) == TC_C_PULSE_TX;
}

bool tc_role_pullup(int role)
{
    return role == TC_R_BUTTON || role == TC_R_CONTACT;
}

const char *tc_role_name(int role)
{
    static const char *const N[TC_R_N] = {
            "NONE",  "IR RECEIVER", "IR LED", "315 RECEIVER", "315 TRANSMITTER", "433 RECEIVER", "433 TRANSMITTER",
            "RELAY", "OUTPUT",      "BUTTON", "MOTION",       "CONTACT",         "ANALOG"};
    return role >= 0 && role < TC_R_N ? N[role] : "?";
}

const char *tc_err_name(int e)
{
    static const char *const N[TC_E_N] = {"OK",       "NO SUCH GPIO", "FLASH PIN", "INPUT ONLY",    "TWO ROLES",
                                          "BAD ROLE", "BUSY",         "USB PIN",   "CONSOLE PIN",   "NOT WIRED",
                                          "TIMEOUT",  "OLD COUNTER",  "BAD DATA",  "PARTS MISSING", "SEND FAILED",
                                          "NO ADC",   "UPDATE FAILED"};
    return e >= 0 && e < TC_E_N ? N[e] : "?";
}

const char *tc_chip_name(int chip)
{
    switch (chip) {
    case TC_CHIP_ESP32:
        return "ESP32";
    case TC_CHIP_S3:
        return "ESP32-S3";
    case TC_CHIP_C3:
        return "ESP32-C3";
    case TC_CHIP_C6:
        return "ESP32-C6";
    case TC_CHIP_S2:
        return "ESP32-S2";
    default:
        return "?";
    }
}

// ---------------------------------------------------------------- the pins of each chip
int tc_chip_gpios(int chip)
{
    switch (chip) {
    case TC_CHIP_ESP32:
        return 40;
    case TC_CHIP_S3:
        return 49;
    case TC_CHIP_C3:
        return 22;
    case TC_CHIP_C6:
        return 31;
    case TC_CHIP_S2:
        return 47;
    default:
        return 0;
    }
}

static bool in(int g, const int8_t *list)
{
    for (; *list >= 0; list++) {
        if (*list == g) {
            return true;
        }
    }
    return false;
}

uint8_t tc_pin_flags(int chip, int g)
{
    if (g < 0 || g >= tc_chip_gpios(chip)) {
        return 0;
    }
    uint8_t f = TC_P_OK;
    switch (chip) {
    case TC_CHIP_ESP32: {
        static const int8_t NONE[] = {20, 24, 28, 29, 30, 31, -1};
        static const int8_t STRAP[] = {0, 2, 5, 12, 15, -1};
        if (in(g, NONE)) {
            return 0;
        }
        if (g >= 6 && g <= 11) {
            return TC_P_FLASH;
        }
        if (g == 1 || g == 3) {
            return TC_P_CONSOLE;
        }
        if (g >= 34) {
            f |= TC_P_IN_ONLY;
        }
        if (in(g, STRAP)) {
            f |= TC_P_STRAP;
        }
        if (g == 16 || g == 17) {
            f |= TC_P_WARN;  // (the PSRAM of a WROVER module)
        }
        if (g == 2) {
            f |= TC_P_LED;  // (the blue LED of the DevKit boards)
        }
        return f;
    }
    case TC_CHIP_S3: {
        static const int8_t STRAP[] = {0, 3, 45, 46, -1};
        if (g >= 22 && g <= 25) {
            return 0;
        }
        if (g >= 26 && g <= 32) {
            return TC_P_FLASH;
        }
        if (g == 19 || g == 20) {
            return TC_P_USB;
        }
        if (g == 43 || g == 44) {
            return TC_P_CONSOLE;
        }
        if (g >= 33 && g <= 37) {
            f |= TC_P_WARN;  // (the octal PSRAM of an R8 module)
        }
        if (in(g, STRAP)) {
            f |= TC_P_STRAP;
        }
        if (g == 48) {
            f |= TC_P_LED;  // (the RGB LED of the DevKitC)
        }
        return f;
    }
    case TC_CHIP_C3: {
        static const int8_t STRAP[] = {2, 8, 9, -1};
        if (g >= 12 && g <= 17) {
            return TC_P_FLASH;
        }
        if (g == 18 || g == 19) {
            return TC_P_USB;
        }
        if (g == 20 || g == 21) {
            return TC_P_CONSOLE;
        }
        if (in(g, STRAP)) {
            f |= TC_P_STRAP;
        }
        if (g == 8) {
            f |= TC_P_LED;
        }
        return f;
    }
    case TC_CHIP_C6: {
        static const int8_t STRAP[] = {4, 5, 8, 9, 15, -1};
        if (g >= 24) {
            return TC_P_FLASH;
        }
        if (g == 12 || g == 13) {
            return TC_P_USB;
        }
        if (g == 16 || g == 17) {
            return TC_P_CONSOLE;
        }
        if (in(g, STRAP)) {
            f |= TC_P_STRAP;
        }
        if (g == 8) {
            f |= TC_P_LED;
        }
        return f;
    }
    case TC_CHIP_S2: {
        static const int8_t STRAP[] = {0, 45, 46, -1};
        if (g >= 22 && g <= 25) {
            return 0;
        }
        if (g >= 26 && g <= 32) {
            return TC_P_FLASH;
        }
        if (g == 19 || g == 20) {
            return TC_P_USB;
        }
        if (g == 43 || g == 44) {
            return TC_P_CONSOLE;
        }
        if (g == 46) {
            f |= TC_P_IN_ONLY;
        }
        if (in(g, STRAP)) {
            f |= TC_P_STRAP;
        }
        return f;
    }
    default:
        return 0;
    }
}

bool tc_wire_check(int chip, const tc_wire_t *w, int n, uint8_t *err, uint8_t *warn)
{
    bool ok = true;
    for (int i = 0; i < n; i++) {
        const uint8_t f = tc_pin_flags(chip, w[i].pin);
        uint8_t e = TC_E_OK;
        uint8_t wn = 0;
        if (w[i].role == TC_R_NONE || w[i].role >= TC_R_N) {
            e = TC_E_ROLE;
        } else if (f & TC_P_FLASH) {
            e = TC_E_FLASH;
        } else if (f & TC_P_USB) {
            e = TC_E_USB;
        } else if (f & TC_P_CONSOLE) {
            e = TC_E_CONSOLE;
        } else if (!(f & TC_P_OK)) {
            e = TC_E_NO_PIN;
        } else if ((f & TC_P_IN_ONLY) && (tc_role_tx(w[i].role) || tc_role_class(w[i].role) == TC_C_OUT)) {
            e = TC_E_IN_ONLY;
        } else if (w[i].role == TC_R_ANALOG && !tc_pin_adc(chip, w[i].pin)) {
            e = TC_E_NO_ADC;
        }
        for (int j = 0; j < i && e == TC_E_OK; j++) {
            if (w[j].pin == w[i].pin) {
                e = TC_E_DUP;
            }
        }
        if (f & TC_P_STRAP) {
            wn |= TC_W_STRAP;
        }
        if ((f & TC_P_LED) && tc_role_class(w[i].role) != TC_C_OUT) {
            wn |= TC_W_LED;  // (an output on the LED pin: the LED is the module)
        }
        if ((f & TC_P_IN_ONLY) && chip == TC_CHIP_ESP32 && tc_role_pullup(w[i].role)) {
            wn |= TC_W_PULL;  // (GPIO 34-39 of the ESP32 have no pull resistors)
        }
        if (err) {
            err[i] = e;
        }
        if (warn) {
            warn[i] = wn;
        }
        ok = ok && e == TC_E_OK;
    }
    return ok;
}

uint32_t tc_crc32(uint32_t crc, const uint8_t *p, int n)
{
    crc = ~crc;
    for (int i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

bool tc_img_info(const uint8_t *img, int n, int *chip, int *fw)
{
    // the image header (24 bytes): magic 0xE9 at 0, the chip id at 12; the first segment header (8 bytes); the app
    // description: magic 0xABCD5432 at 32, the version string at 48
    if (!img || n < 96 || img[0] != 0xE9 || img[32] != 0x32 || img[33] != 0x54 || img[34] != 0xCD || img[35] != 0xAB) {
        return false;
    }
    const int id = img[12] | img[13] << 8;
    const int c = id == 0    ? TC_CHIP_ESP32
                  : id == 9  ? TC_CHIP_S3
                  : id == 5  ? TC_CHIP_C3
                  : id == 13 ? TC_CHIP_C6
                  : id == 2  ? TC_CHIP_S2
                             : 0;
    int v = 0;
    for (int i = 48; i < 80 && img[i] >= '0' && img[i] <= '9'; i++) {
        v = v * 10 + (img[i] - '0');
    }
    if (chip) {
        *chip = c;
    }
    if (fw) {
        *fw = v;
    }
    return c != 0;
}

bool tc_pin_adc(int chip, int g)
{
    switch (chip) {
    case TC_CHIP_ESP32:
        return g >= 32 && g <= 39;  // ADC1 (ADC2 does not work with the radio on)
    case TC_CHIP_S3:
    case TC_CHIP_S2:
        return g >= 1 && g <= 10;
    case TC_CHIP_C3:
        return g >= 0 && g <= 4;
    case TC_CHIP_C6:
        return g >= 0 && g <= 6;
    default:
        return false;
    }
}

void tc_states_put(tc_buf_t *f, const tc_state_t *st, int n)
{
    tc_put8(f, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        tc_put8(f, st[i].pin);
        tc_put16(f, st[i].v);
        tc_put16(f, st[i].cnt);
    }
}

int tc_states_get(tc_msg_t *m, tc_state_t *st, int max)
{
    const int n = (int)tc_get8(m);
    int k = 0;
    for (int i = 0; i < n && !m->bad; i++) {
        tc_state_t s;
        s.pin = (uint8_t)tc_get8(m);
        s.v = (uint16_t)tc_get16(m);
        s.cnt = (uint16_t)tc_get16(m);
        if (!m->bad && k < max) {
            st[k++] = s;
        }
    }
    return m->bad ? 0 : k;
}

uint32_t tc_wire_hash(const tc_wire_t *w, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        const uint8_t b[4] = {w[i].pin, w[i].role, w[i].flags, w[i].carrier_khz};
        for (int k = 0; k < 4; k++) {
            h = (h ^ b[k]) * 16777619u;
        }
    }
    return h;
}

// ---------------------------------------------------------------- a frame to send
void tc_begin(tc_buf_t *f, int type, uint16_t seq, uint8_t flags, uint8_t ch)
{
    f->b[0] = TC_MAGIC0;
    f->b[1] = TC_MAGIC1;
    f->b[2] = TC_VERSION;
    f->b[3] = (uint8_t)type;
    f->b[4] = (uint8_t)seq;
    f->b[5] = (uint8_t)(seq >> 8);
    f->b[6] = (uint8_t)(flags & ~TC_FL_TAG);
    f->b[7] = ch;
    f->n = TC_HDR;
    f->bad = false;
}

int tc_room(const tc_buf_t *f)
{
    return TC_FRAME_MAX - TC_TAIL - f->n;
}

void tc_putn(tc_buf_t *f, const void *p, int n)
{
    if (n < 0 || tc_room(f) < n) {
        f->bad = true;
        return;
    }
    if (n > 0) {
        memcpy(f->b + f->n, p, (size_t)n);
    }
    f->n += n;
}

void tc_put8(tc_buf_t *f, uint32_t v)
{
    const uint8_t b = (uint8_t)v;
    tc_putn(f, &b, 1);
}

void tc_put16(tc_buf_t *f, uint32_t v)
{
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    tc_putn(f, b, 2);
}

void tc_put32(tc_buf_t *f, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    tc_putn(f, b, 4);
}

static void put64le(uint8_t *o, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        o[i] = (uint8_t)(v >> (8 * i));
    }
}

bool tc_seal(tc_buf_t *f, const uint8_t key[TC_KEY_LEN], const uint8_t src[6], uint32_t counter)
{
    if (f->bad || f->n + TC_TAIL > TC_FRAME_MAX) {
        return false;
    }
    f->b[6] |= TC_FL_TAG;
    uint8_t *c = f->b + f->n;
    c[0] = (uint8_t)counter;
    c[1] = (uint8_t)(counter >> 8);
    c[2] = (uint8_t)(counter >> 16);
    c[3] = (uint8_t)(counter >> 24);
    f->n += 4;
    put64le(f->b + f->n, tc_siphash(key, src, 6, f->b, (size_t)f->n));
    f->n += 8;
    return true;
}

// ---------------------------------------------------------------- a received frame
bool tc_parse(const uint8_t *d, int n, tc_msg_t *m)
{
    memset(m, 0, sizeof *m);
    if (!d || n < TC_HDR || d[0] != TC_MAGIC0 || d[1] != TC_MAGIC1 || d[2] != TC_VERSION) {
        return false;
    }
    m->type = d[3];
    m->seq = (uint16_t)(d[4] | d[5] << 8);
    m->flags = d[6];
    m->ch = d[7];
    m->body = d + TC_HDR;
    m->blen = n - TC_HDR;
    if (m->flags & TC_FL_TAG) {
        if (m->blen < TC_TAIL) {
            return false;
        }
        m->blen -= TC_TAIL;
        const uint8_t *c = m->body + m->blen;
        m->counter = (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
    }
    return true;
}

bool tc_check(const uint8_t *d, int n, const uint8_t key[TC_KEY_LEN], const uint8_t src[6])
{
    if (n < TC_HDR + TC_TAIL || !(d[6] & TC_FL_TAG)) {
        return false;
    }
    uint8_t t[8];
    put64le(t, tc_siphash(key, src, 6, d, (size_t)(n - 8)));
    uint8_t diff = 0;
    for (int i = 0; i < 8; i++) {
        diff |= (uint8_t)(t[i] ^ d[n - 8 + i]);
    }
    return diff == 0;
}

int tc_left(const tc_msg_t *m)
{
    return m->blen - m->at;
}

bool tc_getn(tc_msg_t *m, void *p, int n)
{
    if (n < 0 || tc_left(m) < n) {
        m->bad = true;
        if (p && n > 0) {
            memset(p, 0, (size_t)n);
        }
        return false;
    }
    if (p && n > 0) {
        memcpy(p, m->body + m->at, (size_t)n);
    }
    m->at += n;
    return true;
}

uint32_t tc_get8(tc_msg_t *m)
{
    uint8_t b = 0;
    tc_getn(m, &b, 1);
    return b;
}

uint32_t tc_get16(tc_msg_t *m)
{
    uint8_t b[2];
    tc_getn(m, b, 2);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8;
}

uint32_t tc_get32(tc_msg_t *m)
{
    uint8_t b[4];
    tc_getn(m, b, 4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

// ---------------------------------------------------------------- the bodies
void tc_info_put(tc_buf_t *f, const tc_info_t *in)
{
    tc_put8(f, in->chip);
    tc_put8(f, in->rev);
    tc_put16(f, in->fw);
    const int ng = in->ngpio > TC_GPIO_MAX ? TC_GPIO_MAX : in->ngpio;
    tc_put8(f, (uint32_t)ng);
    tc_putn(f, in->pins, ng);
    tc_put8(f, in->led);
    const int nw = in->nwire > TC_WIRE_MAX ? TC_WIRE_MAX : in->nwire;
    tc_put8(f, (uint32_t)nw);
    for (int i = 0; i < nw; i++) {
        tc_putn(f, &in->wire[i], 4);
    }
}

bool tc_info_get(tc_msg_t *m, tc_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->chip = (uint8_t)tc_get8(m);
    out->rev = (uint8_t)tc_get8(m);
    out->fw = (uint16_t)tc_get16(m);
    out->ngpio = (uint8_t)tc_get8(m);
    if (out->ngpio > TC_GPIO_MAX) {
        return false;
    }
    tc_getn(m, out->pins, out->ngpio);
    out->led = (uint8_t)tc_get8(m);
    out->nwire = (uint8_t)tc_get8(m);
    if (out->nwire > TC_WIRE_MAX) {
        return false;
    }
    for (int i = 0; i < out->nwire; i++) {
        tc_getn(m, &out->wire[i], 4);
    }
    return !m->bad;
}

void tc_part_put(tc_buf_t *f, const tc_part_t *p)
{
    tc_put8(f, p->id);
    tc_put8(f, p->part);
    tc_put8(f, p->nparts);
    tc_put16(f, p->total);
    tc_put8(f, p->pin);
    tc_put8(f, p->carrier);
    tc_put16(f, p->rep_ms);
    tc_put16(f, p->rep_at);
    const int n = p->n > TC_PART_VALS ? TC_PART_VALS : p->n;
    tc_put8(f, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        tc_put16(f, p->v[i]);
    }
}

bool tc_part_get(tc_msg_t *m, tc_part_t *p)
{
    memset(p, 0, sizeof *p);
    p->id = (uint8_t)tc_get8(m);
    p->part = (uint8_t)tc_get8(m);
    p->nparts = (uint8_t)tc_get8(m);
    p->total = (uint16_t)tc_get16(m);
    p->pin = (uint8_t)tc_get8(m);
    p->carrier = (uint8_t)tc_get8(m);
    p->rep_ms = (uint16_t)tc_get16(m);
    p->rep_at = (uint16_t)tc_get16(m);
    p->n = (uint8_t)tc_get8(m);
    if (p->n > TC_PART_VALS) {
        return false;
    }
    for (int i = 0; i < p->n; i++) {
        p->v[i] = (uint16_t)tc_get16(m);
    }
    return !m->bad && p->nparts >= 1 && p->nparts <= 8 && p->part < p->nparts && p->total <= TC_VALS_MAX;
}

int tc_parts_make(const tc_part_t *head, const uint16_t *v, int n, tc_part_t *p, int max_parts)
{
    if (n < 0 || n > TC_VALS_MAX) {
        return 0;
    }
    int np = (n + TC_PART_VALS - 1) / TC_PART_VALS;
    if (np < 1) {
        np = 1;
    }
    if (np > max_parts) {
        return 0;
    }
    for (int k = 0; k < np; k++) {
        p[k] = *head;
        p[k].part = (uint8_t)k;
        p[k].nparts = (uint8_t)np;
        p[k].total = (uint16_t)n;
        const int at = k * TC_PART_VALS;
        const int c = n - at > TC_PART_VALS ? TC_PART_VALS : n - at;
        p[k].n = (uint8_t)c;
        if (c > 0) {
            memcpy(p[k].v, v + at, (size_t)c * sizeof *v);
        }
    }
    return np;
}

int tc_asm_add(tc_asm_t *a, const tc_part_t *p)
{
    if (!a->on || a->id != p->id || a->nparts != p->nparts || a->total != p->total) {
        a->on = true;
        a->id = p->id;
        a->nparts = p->nparts;
        a->total = p->total;
        a->got = 0;
        a->head = *p;
    }
    const int at = p->part * TC_PART_VALS;
    const int want = a->total - at > TC_PART_VALS ? TC_PART_VALS : a->total - at;
    if (want < 0 || p->n != want) {
        return -1;
    }
    if (p->n > 0) {
        memcpy(a->v + at, p->v, (size_t)p->n * sizeof p->v[0]);
    }
    a->got |= (uint8_t)(1u << p->part);
    return a->got == (uint8_t)((1u << a->nparts) - 1) ? 1 : 0;
}

// ---------------------------------------------------------------- the level times
int tc_vals_from_levels(const int32_t *d, int n, uint16_t *v, int max)
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        const uint16_t lvl = d[i] > 0 ? 0x8000 : 0;
        int32_t t = d[i] > 0 ? d[i] : -d[i];
        while (t > 0 && k < max) {
            const int32_t part = t > 32767 ? 32767 : t;
            v[k++] = (uint16_t)(lvl | (uint16_t)part);
            t -= part;
        }
    }
    return k;
}

int tc_levels_from_vals(const uint16_t *v, int n, int32_t *d, int max)
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        const bool mark = v[i] & 0x8000;
        const int32_t t = v[i] & 0x7FFF;
        if (!t) {
            continue;
        }
        if (k > 0 && (d[k - 1] > 0) == mark) {
            d[k - 1] += mark ? t : -t;
        } else if (k < max) {
            d[k++] = mark ? t : -t;
        }
    }
    return k;
}

// ---------------------------------------------------------------- SipHash-2-4
static uint64_t rotl(uint64_t x, int b)
{
    return (x << b) | (x >> (64 - b));
}

#define SIPROUND                                                                                                       \
    do {                                                                                                               \
        v0 += v1;                                                                                                      \
        v1 = rotl(v1, 13);                                                                                             \
        v1 ^= v0;                                                                                                      \
        v0 = rotl(v0, 32);                                                                                             \
        v2 += v3;                                                                                                      \
        v3 = rotl(v3, 16);                                                                                             \
        v3 ^= v2;                                                                                                      \
        v0 += v3;                                                                                                      \
        v3 = rotl(v3, 21);                                                                                             \
        v3 ^= v0;                                                                                                      \
        v2 += v1;                                                                                                      \
        v1 = rotl(v1, 17);                                                                                             \
        v1 ^= v2;                                                                                                      \
        v2 = rotl(v2, 32);                                                                                             \
    } while (0)

static uint64_t get64le(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = v << 8 | p[i];
    }
    return v;
}

uint64_t tc_siphash(const uint8_t key[16], const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    const uint64_t k0 = get64le(key), k1 = get64le(key + 8);
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    const size_t len = an + bn;
    uint8_t blk[8];
    int bi = 0;
    for (size_t i = 0; i < len; i++) {
        blk[bi++] = i < an ? a[i] : b[i - an];
        if (bi == 8) {
            const uint64_t m = get64le(blk);
            v3 ^= m;
            SIPROUND;
            SIPROUND;
            v0 ^= m;
            bi = 0;
        }
    }
    uint64_t last = (uint64_t)(len & 0xFF) << 56;
    for (int i = 0; i < bi; i++) {
        last |= (uint64_t)blk[i] << (8 * i);
    }
    v3 ^= last;
    SIPROUND;
    SIPROUND;
    v0 ^= last;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

void tc_pair_key(uint8_t key[TC_KEY_LEN], const uint8_t shared[32], const uint8_t pub_master[32],
                 const uint8_t pub_comp[32])
{
    uint8_t msg[7 + 16 + 64];
    memcpy(msg + 7, shared + 16, 16);
    memcpy(msg + 23, pub_master, 32);
    memcpy(msg + 55, pub_comp, 32);
    memcpy(msg, "TFNCPK1", 7);
    put64le(key, tc_siphash(shared, msg, sizeof msg, NULL, 0));
    msg[6] = '2';
    put64le(key + 8, tc_siphash(shared, msg, sizeof msg, NULL, 0));
}
