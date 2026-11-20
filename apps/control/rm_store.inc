// CONTROL: the remotes and their keys, and their file /data/remote.bin (the paired companions: the service of TFN OS,
// tfn/companion.h). An IR key keeps the decoded code (the encoder makes the frame again) and, for a code with no
// encoder (UNKNOWN), the raw level times as the 16-bit values of tfncomp. A SWITCH key and a SENSOR tile keep the
// companion (MAC) and the pin of their module. The file: version 1 (only IR keys) loads too; a save writes version 2.
#define RM_MAX      8
#define RB_MAX      30
#define RAW_MAX     TC_VALS_MAX
#define REM_FILE    "/data/remote.bin"
#define FILE_MAGIC  0x524D3101u  // "RM1" and the version
#define FILE_MAGIC2 0x524D3102u  // version 2: each key has its kind

// the kinds of a key
enum {
    K_IR,      // a learned IR code: through the IR LED of the remote
    K_SWITCH,  // a switch output of a companion (a relay, a light)
    K_SENSOR,  // a switch input or an analog input of a companion: a tile with its live value
    K_RF       // a learned 433 / 315 MHz code: through the transmitter of a companion (mac, pin)
};
// a learned RF code (the fields of rfd_code_t that the encoders need; the raw packet is in raw too)
typedef struct {
    uint8_t kind, proto;  // RFD_K_*, the RCSWITCH protocol
    uint16_t nbits;
    uint8_t bits[16];
    uint16_t t_short, t_long;
    uint32_t t_gap;
} rfk_t;
// the action of a SWITCH key
enum {
    A_TOGGLE,
    A_ON,
    A_OFF,
    A_PRESS  // on for PRESS_OUT_MS, then off
};
#define PRESS_OUT_MS 500

typedef struct {
    char name[20];
    uint8_t kind;    // K_*
    uint8_t mac[6];  // SWITCH, SENSOR: the companion
    uint8_t pin;     // SWITCH, SENSOR: the pin of the module
    uint8_t act;     // SWITCH: A_*; RF: the band (0: 433, 1: 315)
    rfk_t rf;        // RF: the code
    ird_code_t code;
    uint8_t carrier_khz;
    uint8_t toggle;  // RC5 / RC6: the toggle bit of the next press (not saved)
    uint16_t nraw;   // the raw values (UNKNOWN codes)
    uint16_t *raw;   // PSRAM
} btn_t;

typedef struct {
    char name[24];
    uint8_t mac[6];  // the companion of the IR keys
    uint8_t pin;     // its IR LED (0xFF: none yet)
    uint8_t nb;
    btn_t b[RB_MAX];
} rem_t;

static rem_t *s_rem;  // RM_MAX (PSRAM)
static int s_nrem;
static bool s_dirty;
static int64_t s_dirty_t;

static bool raw_only(const ird_code_t *c)
{
    return c->proto == IRD_P_UNKNOWN || c->proto >= IRD_P_N;
}

static void btn_free(btn_t *b)
{
    if (b->raw) {
        heap_caps_free(b->raw);
    }
    memset(b, 0, sizeof *b);
}

static void rem_free_all(void)
{
    for (int r = 0; r < s_nrem; r++) {
        for (int k = 0; k < s_rem[r].nb; k++) {
            btn_free(&s_rem[r].b[k]);
        }
    }
    s_nrem = 0;
}

static void mark_dirty(void)
{
    s_dirty = true;
    s_dirty_t = esp_timer_get_time() / 1000;
}

// ---------------------------------------------------------------- the file of the remotes
typedef struct {
    uint8_t *p;
    int n, max;
    bool bad;
} wbuf_t;

static void wb(wbuf_t *w, const void *d, int n)
{
    if (w->n + n > w->max) {
        w->bad = true;
        return;
    }
    memcpy(w->p + w->n, d, (size_t)n);
    w->n += n;
}

static bool rb(const uint8_t *p, int len, int *at, void *d, int n)
{
    if (*at + n > len) {
        return false;
    }
    memcpy(d, p + *at, (size_t)n);
    *at += n;
    return true;
}

// the fields of a code (not the struct: a new firmware can change its layout)
static void put_code(wbuf_t *w, const ird_code_t *c)
{
    const uint8_t h[4] = {c->proto, c->flags, (uint8_t)c->nbits, (uint8_t)(c->nbits >> 8)};
    wb(w, h, 4);
    wb(w, &c->address, 4);
    wb(w, &c->command, 4);
    wb(w, &c->extra, 4);
    wb(w, c->bits, IRD_MAX_BITS / 8);
    const uint16_t t[6] = {c->hdr_mark, c->hdr_space, c->one_mark, c->one_space, c->zero_mark, c->zero_space};
    wb(w, t, sizeof t);
    wb(w, &c->carrier_hz, 4);
}

static bool get_code(const uint8_t *p, int len, int *at, ird_code_t *c)
{
    memset(c, 0, sizeof *c);
    uint8_t h[4];
    uint16_t t[6];
    if (!rb(p, len, at, h, 4) || !rb(p, len, at, &c->address, 4) || !rb(p, len, at, &c->command, 4) ||
        !rb(p, len, at, &c->extra, 4) || !rb(p, len, at, c->bits, IRD_MAX_BITS / 8) || !rb(p, len, at, t, sizeof t) ||
        !rb(p, len, at, &c->carrier_hz, 4)) {
        return false;
    }
    c->proto = h[0];
    c->flags = h[1];
    c->nbits = (uint16_t)(h[2] | h[3] << 8);
    c->hdr_mark = t[0];
    c->hdr_space = t[1];
    c->one_mark = t[2];
    c->one_space = t[3];
    c->zero_mark = t[4];
    c->zero_space = t[5];
    return c->proto < IRD_P_N && c->nbits <= IRD_MAX_BITS;
}

static void rem_save(void)
{
    s_dirty = false;
    // the size: the fixed part of each button and its raw values
    int size = 8;
    for (int r = 0; r < s_nrem; r++) {
        size += 32 + s_rem[r].nb * (20 + 9 + 28 + 80 + 3 + 2);
        for (int k = 0; k < s_rem[r].nb; k++) {
            size += s_rem[r].b[k].nraw * 2;
        }
    }
    uint8_t *buf = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return;
    }
    wbuf_t w = {buf, 0, size, false};
    const uint32_t magic = FILE_MAGIC2;
    wb(&w, &magic, 4);
    const uint8_t n = (uint8_t)s_nrem;
    wb(&w, &n, 1);
    for (int r = 0; r < s_nrem; r++) {
        const rem_t *m = &s_rem[r];
        wb(&w, m->name, 24);
        wb(&w, m->mac, 6);
        wb(&w, &m->pin, 1);
        wb(&w, &m->nb, 1);
        for (int k = 0; k < m->nb; k++) {
            const btn_t *b = &m->b[k];
            wb(&w, b->name, 20);
            wb(&w, &b->kind, 1);
            if (b->kind != K_IR) {
                wb(&w, b->mac, 6);
                wb(&w, &b->pin, 1);
                wb(&w, &b->act, 1);
                if (b->kind == K_RF) {
                    const rfk_t *k = &b->rf;
                    const uint8_t h[4] = {k->kind, k->proto, (uint8_t)k->nbits, (uint8_t)(k->nbits >> 8)};
                    wb(&w, h, 4);
                    wb(&w, k->bits, 16);
                    wb(&w, &k->t_short, 2);
                    wb(&w, &k->t_long, 2);
                    wb(&w, &k->t_gap, 4);
                    wb(&w, &b->nraw, 2);
                    if (b->nraw) {
                        wb(&w, b->raw, b->nraw * 2);
                    }
                }
                continue;
            }
            put_code(&w, &b->code);
            wb(&w, &b->carrier_khz, 1);
            wb(&w, &b->nraw, 2);
            if (b->nraw) {
                wb(&w, b->raw, b->nraw * 2);
            }
        }
    }
    if (!w.bad) {
        storage_write(REM_FILE, buf, (size_t)w.n);
    }
    heap_caps_free(buf);
}

static void rem_load(void)
{
    rem_free_all();
    const int max = 160 * 1024;
    uint8_t *p = heap_caps_malloc((size_t)max, MALLOC_CAP_SPIRAM);
    size_t len = 0;
    if (!p) {
        return;
    }
    if (!storage_read(REM_FILE, p, (size_t)max, &len) || len < 5) {
        heap_caps_free(p);
        return;
    }
    int at = 0;
    uint32_t magic = 0;
    uint8_t n = 0;
    rb(p, (int)len, &at, &magic, 4);
    rb(p, (int)len, &at, &n, 1);
    const bool v2 = magic == FILE_MAGIC2;
    if ((magic != FILE_MAGIC && !v2) || n > RM_MAX) {
        heap_caps_free(p);
        return;
    }
    for (int r = 0; r < n; r++) {
        rem_t *m = &s_rem[r];
        memset(m, 0, sizeof *m);
        if (!rb(p, (int)len, &at, m->name, 24) || !rb(p, (int)len, &at, m->mac, 6) ||
            !rb(p, (int)len, &at, &m->pin, 1) || !rb(p, (int)len, &at, &m->nb, 1) || m->nb > RB_MAX) {
            m->nb = 0;
            break;
        }
        m->name[23] = 0;
        s_nrem = r + 1;
        for (int k = 0; k < m->nb; k++) {
            btn_t *b = &m->b[k];
            if (!rb(p, (int)len, &at, b->name, 20) || (v2 && !rb(p, (int)len, &at, &b->kind, 1))) {
                m->nb = (uint8_t)k;
                memset(b, 0, sizeof *b);
                break;
            }
            if (b->kind != K_IR) {
                b->name[19] = 0;
                if (b->kind > K_RF || !rb(p, (int)len, &at, b->mac, 6) || !rb(p, (int)len, &at, &b->pin, 1) ||
                    !rb(p, (int)len, &at, &b->act, 1)) {
                    m->nb = (uint8_t)k;
                    memset(b, 0, sizeof *b);
                    break;
                }
                if (b->kind == K_RF) {
                    rfk_t *f = &b->rf;
                    uint8_t h[4];
                    bool ok = rb(p, (int)len, &at, h, 4) && rb(p, (int)len, &at, f->bits, 16) &&
                              rb(p, (int)len, &at, &f->t_short, 2) && rb(p, (int)len, &at, &f->t_long, 2) &&
                              rb(p, (int)len, &at, &f->t_gap, 4) && rb(p, (int)len, &at, &b->nraw, 2) &&
                              b->nraw <= RAW_MAX;
                    f->kind = h[0];
                    f->proto = h[1];
                    f->nbits = (uint16_t)(h[2] | h[3] << 8);
                    if (ok && b->nraw) {
                        b->raw = heap_caps_malloc((size_t)b->nraw * 2, MALLOC_CAP_SPIRAM);
                        ok = b->raw && rb(p, (int)len, &at, b->raw, b->nraw * 2);
                    }
                    if (!ok) {
                        btn_free(b);
                        m->nb = (uint8_t)k;
                        break;
                    }
                }
                continue;
            }
            if (!get_code(p, (int)len, &at, &b->code) || !rb(p, (int)len, &at, &b->carrier_khz, 1) ||
                !rb(p, (int)len, &at, &b->nraw, 2) || b->nraw > RAW_MAX) {
                m->nb = (uint8_t)k;
                memset(b, 0, sizeof *b);
                break;
            }
            b->name[19] = 0;
            if (b->nraw) {
                b->raw = heap_caps_malloc((size_t)b->nraw * 2, MALLOC_CAP_SPIRAM);
                if (!b->raw || !rb(p, (int)len, &at, b->raw, b->nraw * 2)) {
                    if (b->raw) {
                        heap_caps_free(b->raw);
                    }
                    memset(b, 0, sizeof *b);
                    m->nb = (uint8_t)k;
                    break;
                }
            }
        }
    }
    heap_caps_free(p);
}

// ---------------------------------------------------------------- the changes
static int rem_new(const char *name, const uint8_t mac[6], int pin)
{
    if (s_nrem >= RM_MAX) {
        return -1;
    }
    rem_t *m = &s_rem[s_nrem];
    memset(m, 0, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", name);
    memcpy(m->mac, mac, 6);
    m->pin = (uint8_t)pin;
    mark_dirty();
    return s_nrem++;
}

static void rem_delete(int r)
{
    if (r < 0 || r >= s_nrem) {
        return;
    }
    for (int k = 0; k < s_rem[r].nb; k++) {
        btn_free(&s_rem[r].b[k]);
    }
    for (int i = r; i < s_nrem - 1; i++) {
        s_rem[i] = s_rem[i + 1];
    }
    s_nrem--;
    memset(&s_rem[s_nrem], 0, sizeof s_rem[0]);
    mark_dirty();
}

// a new button from a learned frame: the code, and the raw values when the code has no encoder
static int btn_add(int r, const char *name, const ird_code_t *c, const int32_t *frame, int nf)
{
    if (r < 0 || r >= s_nrem || s_rem[r].nb >= RB_MAX) {
        return -1;
    }
    btn_t *b = &s_rem[r].b[s_rem[r].nb];
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name);
    b->code = *c;
    b->carrier_khz = (uint8_t)((c->carrier_hz ? c->carrier_hz : 38000) / 1000);
    if (raw_only(c)) {
        static uint16_t v[RAW_MAX];
        const int n = tc_vals_from_levels(frame, nf, v, RAW_MAX);
        b->raw = heap_caps_malloc((size_t)n * 2, MALLOC_CAP_SPIRAM);
        if (!b->raw) {
            return -1;
        }
        memcpy(b->raw, v, (size_t)n * 2);
        b->nraw = (uint16_t)n;
    }
    mark_dirty();
    return s_rem[r].nb++;
}

// the RF code and the raw packet of a key (a new one or LEARN AGAIN)
static bool rf_fill(btn_t *b, const rfd_code_t *c, const int32_t *frame, int nf)
{
    b->rf.kind = c->kind;
    b->rf.proto = c->proto;
    b->rf.nbits = c->nbits;
    memcpy(b->rf.bits, c->bits, sizeof b->rf.bits);
    b->rf.t_short = c->t_short;
    b->rf.t_long = c->t_long;
    b->rf.t_gap = c->t_gap;
    static uint16_t v[RAW_MAX];
    const int n = tc_vals_from_levels(frame, nf, v, RAW_MAX);
    b->raw = n ? heap_caps_malloc((size_t)n * 2, MALLOC_CAP_SPIRAM) : NULL;
    if (!b->raw) {
        return false;
    }
    memcpy(b->raw, v, (size_t)n * 2);
    b->nraw = (uint16_t)n;
    return true;
}

// a new RF key: the transmitter (companion ci, pin; ci < 0: none yet) and the band
static int btn_add_rf(int r, const char *name, int ci, int pin, int band, const rfd_code_t *c, const int32_t *frame,
                      int nf)
{
    if (r < 0 || r >= s_nrem || s_rem[r].nb >= RB_MAX) {
        return -1;
    }
    btn_t *b = &s_rem[r].b[s_rem[r].nb];
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name);
    b->kind = K_RF;
    const tcm_comp_t *t = tcm_get(ci);
    if (t) {
        memcpy(b->mac, t->mac, 6);
    }
    b->pin = (uint8_t)(t ? pin : 0xFF);
    b->act = (uint8_t)band;
    if (!rf_fill(b, c, frame, nf)) {
        return -1;
    }
    mark_dirty();
    return s_rem[r].nb++;
}

// a new SWITCH key or SENSOR tile: the module (companion ci, pin) and the action
static int btn_add_mod(int r, const char *name, int kind, int ci, int pin, int act)
{
    const tcm_comp_t *c = tcm_get(ci);
    if (r < 0 || r >= s_nrem || s_rem[r].nb >= RB_MAX || !c) {
        return -1;
    }
    btn_t *b = &s_rem[r].b[s_rem[r].nb];
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name);
    b->kind = (uint8_t)kind;
    memcpy(b->mac, c->mac, 6);
    b->pin = (uint8_t)pin;
    b->act = (uint8_t)act;
    mark_dirty();
    return s_rem[r].nb++;
}

static void btn_delete(int r, int k)
{
    rem_t *m = &s_rem[r];
    if (k < 0 || k >= m->nb) {
        return;
    }
    btn_free(&m->b[k]);
    for (int i = k; i < m->nb - 1; i++) {
        m->b[i] = m->b[i + 1];
    }
    m->nb--;
    memset(&m->b[m->nb], 0, sizeof m->b[0]);
    mark_dirty();
}

// a button one place back (dir -1) or on (dir +1)
static void btn_move(int r, int k, int dir)
{
    rem_t *m = &s_rem[r];
    const int j = k + dir;
    if (k < 0 || k >= m->nb || j < 0 || j >= m->nb) {
        return;
    }
    const btn_t t = m->b[k];
    m->b[k] = m->b[j];
    m->b[j] = t;
    mark_dirty();
}

// a new code for a button (LEARN AGAIN): its name and its place stay
static bool btn_relearn(int r, int k, const ird_code_t *c, const int32_t *frame, int nf)
{
    rem_t *m = &s_rem[r];
    if (k < 0 || k >= m->nb) {
        return false;
    }
    char name[20];
    memcpy(name, m->b[k].name, sizeof name);
    btn_free(&m->b[k]);
    btn_t *b = &m->b[k];
    snprintf(b->name, sizeof b->name, "%s", name);
    b->code = *c;
    b->carrier_khz = (uint8_t)((c->carrier_hz ? c->carrier_hz : 38000) / 1000);
    if (raw_only(c)) {
        static uint16_t v[RAW_MAX];
        const int n = tc_vals_from_levels(frame, nf, v, RAW_MAX);
        b->raw = heap_caps_malloc((size_t)n * 2, MALLOC_CAP_SPIRAM);
        if (b->raw) {
            memcpy(b->raw, v, (size_t)n * 2);
            b->nraw = (uint16_t)n;
        }
    }
    mark_dirty();
    return true;
}
