// TFNCOMP, THE MASTER SIDE (tc_master.h): the paired companions, the search, the pairing, the requests and their
// retries, the parts of the long messages, the counter of each companion.
#include "tc_master.h"
#include <string.h>

#define REPLY_MS    300  // a request waits this long for its answer, then it goes again
#define TRIES       8
#define PING_MS     2000  // a ping to an online companion
#define PING_FAST   300   // a ping to a companion that is not online (it searches for this channel)
#define FOUND_MS    10000
#define PAIR_MS     65000
#define PAIR_REQ_MS 150
#define PARTS_MS    400  // a FRAME with a missing part: the NAK with the parts that came
#define SEND_ACK_MS 600  // SEND: the time for the ACK after the last part (the companion sends first)

static const tcm_io_t *s_io;
static uint8_t s_me[6];
static char s_name[17];
static uint16_t s_seq;

static tcm_comp_t s_c[TCM_MAX];
static int s_n;
static tcm_found_t s_found[TCM_FOUND_MAX];

// the pairing
static struct {
    int state;  // tcm_pair_state
    uint8_t mac[6];
    char name[17];
    uint8_t priv[32], pub[32];
    uint32_t until, next;
    int index;
} s_pair;

// the request of each companion
typedef struct {
    int type, state, code;
    uint32_t sent_ms, deadline;
    int tries;
    bool acked;  // CAPTURE: the companion started; SEND: all parts went
    // the fields
    uint8_t pin, a8;
    uint16_t a16;
    tc_wire_t wire[TC_WIRE_MAX];
    int nwire;
    uint8_t cfg_err[TC_WIRE_MAX], cfg_warn[TC_WIRE_MAX];
    int cfg_n;
    bool cfg_ok, cfg_has;
    // SEND
    uint8_t id;
    int nparts;
    uint8_t carrier;
    uint16_t rep_ms, rep_at;
    // OTA: the image, the phase (0 BEGIN, 1 DATA, 2 END), the offset that the companion has (base) and the next one
    // to send
    const uint8_t *img;
    uint32_t size, crc, base, next;
    int phase;
    uint16_t fw;
} req_t;
#define OTA_WINDOW 8    // DATA frames in the air at most
#define OTA_WAIT   400  // no ACK in this time: the DATA from the offset of the companion again
static req_t s_r[TCM_MAX];
static uint8_t s_send_id;
static uint16_t s_send_v[TC_VALS_MAX];
static int s_send_nv;
static int s_send_owner = -1;

// the captured frame
static tc_asm_t s_asm;
static uint32_t s_asm_ms;
static int s_asm_owner = -1;
static int32_t s_frame[TC_VALS_MAX];
static int s_frame_n, s_frame_pin;
static uint8_t s_frame_id;
static bool s_frame_done;  // s_frame_id came whole (its parts that come again only get an ACK)

static uint32_t now(void)
{
    return s_io->now_ms();
}

// a name of up to 16 bytes (an own copy: the app file has no firmware name for it)
static void name_copy(char *dst, const char *src)
{
    int k = 0;
    while (src && src[k] && k < 16) {
        dst[k] = src[k];
        k++;
    }
    while (k <= 16) {
        dst[k++] = 0;
    }
}

// ---------------------------------------------------------------- the frames
static void begin(tc_buf_t *f, int type)
{
    const int ch = s_io->channel ? s_io->channel() : 0;
    tc_begin(f, type, ++s_seq, 0, (uint8_t)ch);
}

static void send_to(int i, tc_buf_t *f)
{
    tcm_comp_t *c = &s_c[i];
    if (!tc_seal(f, c->key, s_me, ++c->tx_ctr)) {
        return;
    }
    s_io->send(c->mac, f->b, f->n);
}

static void simple(int i, int type)
{
    tc_buf_t f;
    begin(&f, type);
    send_to(i, &f);
}

static void send_part(int i, int k)
{
    req_t *r = &s_r[i];
    tc_part_t p;
    memset(&p, 0, sizeof p);
    p.id = r->id;
    p.part = (uint8_t)k;
    p.nparts = (uint8_t)r->nparts;
    p.total = (uint16_t)s_send_nv;
    p.pin = r->pin;
    p.carrier = r->carrier;
    p.rep_ms = r->rep_ms;
    p.rep_at = r->rep_at;
    const int at = k * TC_PART_VALS;
    const int c = s_send_nv - at > TC_PART_VALS ? TC_PART_VALS : s_send_nv - at;
    p.n = (uint8_t)(c > 0 ? c : 0);
    if (c > 0) {
        memcpy(p.v, s_send_v + at, (size_t)c * sizeof p.v[0]);
    }
    tc_buf_t f;
    begin(&f, TC_SEND);
    tc_part_put(&f, &p);
    send_to(i, &f);
}

// (again) the frames of the request now
// the DATA frames of the window
static void ota_fill(int i)
{
    req_t *r = &s_r[i];
    while (r->next < r->size && r->next < r->base + OTA_WINDOW * TC_OTA_CHUNK) {
        const int n = r->size - r->next < TC_OTA_CHUNK ? (int)(r->size - r->next) : TC_OTA_CHUNK;
        tc_buf_t f;
        begin(&f, TC_OTA_DATA);
        tc_put32(&f, r->next);
        tc_put8(&f, (uint32_t)n);
        tc_putn(&f, r->img + r->next, n);
        send_to(i, &f);
        r->next += (uint32_t)n;
    }
}

static void emit(int i)
{
    req_t *r = &s_r[i];
    tc_buf_t f;
    r->sent_ms = now();
    switch (r->type) {
    case TCM_R_INFO:
        simple(i, TC_INFO_REQ);
        return;
    case TCM_R_PING:
        simple(i, TC_PING);
        return;
    case TCM_R_CONFIG:
        begin(&f, TC_CONFIG);
        tc_put8(&f, (uint32_t)r->nwire);
        for (int k = 0; k < r->nwire; k++) {
            tc_putn(&f, &r->wire[k], 4);
        }
        break;
    case TCM_R_WATCH:
        begin(&f, TC_WATCH);
        tc_put8(&f, r->pin);
        tc_put8(&f, r->a8);
        break;
    case TCM_R_PULSE:
        begin(&f, TC_PULSE);
        tc_put8(&f, r->pin);
        tc_put16(&f, r->a16);
        break;
    case TCM_R_OTA:
        if (r->phase == 0) {
            begin(&f, TC_OTA_BEGIN);
            tc_put32(&f, r->size);
            tc_put32(&f, r->crc);
            tc_put16(&f, r->fw);
            break;
        }
        if (r->phase == 2) {
            begin(&f, TC_OTA_END);
            break;
        }
        r->next = r->base;  // (no ACK: from the offset of the companion again)
        ota_fill(i);
        return;
    case TCM_R_SET:
        begin(&f, TC_SET);
        tc_put8(&f, r->pin);
        tc_put16(&f, r->a16);
        tc_put16(&f, r->rep_ms);
        break;
    case TCM_R_CAPTURE:
        begin(&f, TC_CAPTURE);
        tc_put8(&f, r->pin);
        tc_put8(&f, r->a8);
        tc_put8(&f, r->a16);
        break;
    case TCM_R_SEND:
        for (int k = 0; k < r->nparts; k++) {
            send_part(i, k);
        }
        r->acked = true;
        return;
    default:
        return;
    }
    send_to(i, &f);
}

static bool req_new(int i, int type)
{
    if (i < 0 || i >= s_n) {
        return false;
    }
    req_t *r = &s_r[i];
    memset(r, 0, sizeof *r);
    r->type = type;
    r->state = TCM_BUSY;
    r->tries = 1;
    if (s_asm_owner == i) {
        s_asm_owner = -1;
    }
    return true;
}

static void req_end(int i, int state, int code)
{
    s_r[i].state = state;
    s_r[i].code = code;
}

// ---------------------------------------------------------------- the list
void tcm_init(const tcm_io_t *io, const uint8_t my_mac[6], const char *my_name)
{
    s_io = io;
    memcpy(s_me, my_mac, 6);
    name_copy(s_name, my_name);
    s_n = 0;
    memset(s_c, 0, sizeof s_c);
    memset(s_r, 0, sizeof s_r);
    memset(s_found, 0, sizeof s_found);
    memset(&s_pair, 0, sizeof s_pair);
    s_asm_owner = s_send_owner = -1;
    s_frame_n = 0;
    io->random(&s_send_id, 1);
}

int tcm_count(void)
{
    return s_n;
}

tcm_comp_t *tcm_get(int i)
{
    return i >= 0 && i < s_n ? &s_c[i] : NULL;
}

int tcm_find(const uint8_t mac[6])
{
    for (int i = 0; i < s_n; i++) {
        if (!memcmp(s_c[i].mac, mac, 6)) {
            return i;
        }
    }
    return -1;
}

bool tcm_online(int i)
{
    return i >= 0 && i < s_n && s_c[i].seen_ms && now() - s_c[i].seen_ms < TCM_LIVE_MS;
}

void tcm_rename(int i, const char *name)
{
    if (i < 0 || i >= s_n) {
        return;
    }
    name_copy(s_c[i].name, name);
    s_io->changed();
}

static void drop(int i)
{
    for (int k = i; k < s_n - 1; k++) {
        s_c[k] = s_c[k + 1];
        s_r[k] = s_r[k + 1];
    }
    s_n--;
    memset(&s_c[s_n], 0, sizeof s_c[0]);
    memset(&s_r[s_n], 0, sizeof s_r[0]);
    s_asm_owner = s_send_owner = -1;
}

void tcm_forget(int i)
{
    if (i < 0 || i >= s_n) {
        return;
    }
    simple(i, TC_FORGET);
    simple(i, TC_FORGET);
    drop(i);
    s_io->changed();
}

// THE FILE: 'T' 'M' 2 n, then for each companion: the MAC, the name, the key, the chip, the counter (48 bytes), then
// the info: has_info, rev, fw (2), the flags of TC_GPIO_MAX pins, the LED, nwire, TC_WIRE_MAX wires (4 bytes each).
// Version 1 (no info) still loads.
static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int tcm_save(uint8_t *b, int max)
{
    if (max < 4 + s_n * TCM_REC) {
        return 0;
    }
    memset(b, 0, (size_t)(4 + s_n * TCM_REC));
    b[0] = 'T';
    b[1] = 'M';
    b[2] = 2;
    b[3] = (uint8_t)s_n;
    for (int i = 0; i < s_n; i++) {
        uint8_t *r = b + 4 + i * TCM_REC;
        const tcm_comp_t *c = &s_c[i];
        memcpy(r, c->mac, 6);
        memcpy(r + 6, c->name, 17);
        memcpy(r + 23, c->key, 16);
        r[39] = c->chip;
        put32(r + 40, c->tx_ctr);
        uint8_t *q = r + 48;
        q[0] = c->has_info;
        q[1] = c->info.rev;
        q[2] = (uint8_t)c->info.fw;
        q[3] = (uint8_t)(c->info.fw >> 8);
        q[4] = c->info.ngpio;
        memcpy(q + 5, c->info.pins, TC_GPIO_MAX);
        q[5 + TC_GPIO_MAX] = c->info.led;
        q[6 + TC_GPIO_MAX] = c->info.nwire;
        memcpy(q + 7 + TC_GPIO_MAX, c->info.wire, sizeof c->info.wire);
    }
    return 4 + s_n * TCM_REC;
}

bool tcm_load(const uint8_t *b, int n)
{
    if (n < 4 || b[0] != 'T' || b[1] != 'M' || (b[2] != 1 && b[2] != 2) || b[3] > TCM_MAX) {
        return false;
    }
    const int rec = b[2] == 1 ? 48 : TCM_REC;
    if (n < 4 + b[3] * rec) {
        return false;
    }
    s_n = b[3];
    for (int i = 0; i < s_n; i++) {
        const uint8_t *r = b + 4 + i * rec;
        tcm_comp_t *c = &s_c[i];
        memset(c, 0, sizeof *c);
        memcpy(c->mac, r, 6);
        memcpy(c->name, r + 6, 17);
        c->name[16] = 0;
        memcpy(c->key, r + 23, 16);
        c->chip = r[39];
        c->tx_ctr = get32(r + 40);
        c->watch_pin = -1;
        if (b[2] == 2) {
            const uint8_t *q = r + 48;
            c->has_info = q[0] && q[4] <= TC_GPIO_MAX && q[6 + TC_GPIO_MAX] <= TC_WIRE_MAX;
            if (c->has_info) {
                c->info.chip = c->chip;
                c->info.rev = q[1];
                c->info.fw = (uint16_t)(q[2] | q[3] << 8);
                c->info.ngpio = q[4];
                memcpy(c->info.pins, q + 5, TC_GPIO_MAX);
                c->info.led = q[5 + TC_GPIO_MAX];
                c->info.nwire = q[6 + TC_GPIO_MAX];
                memcpy(c->info.wire, q + 7 + TC_GPIO_MAX, sizeof c->info.wire);
            }
        }
    }
    memset(s_r, 0, sizeof s_r);
    return true;
}

// ---------------------------------------------------------------- the search and the pairing
int tcm_found(tcm_found_t *out, int max)
{
    int k = 0;
    for (int i = 0; i < TCM_FOUND_MAX && k < max; i++) {
        if (s_found[i].seen_ms && now() - s_found[i].seen_ms < FOUND_MS && tcm_find(s_found[i].mac) < 0) {
            out[k++] = s_found[i];
        }
    }
    return k;
}

static void on_hello(const uint8_t mac[6], tc_msg_t *m, int rssi)
{
    tcm_found_t h;
    memset(&h, 0, sizeof h);
    memcpy(h.mac, mac, 6);
    h.chip = (uint8_t)tc_get8(m);
    h.rev = (uint8_t)tc_get8(m);
    h.fw = (uint16_t)tc_get16(m);
    h.paired = tc_get8(m);
    if (h.paired) {
        tc_getn(m, h.master, 6);
    }
    h.seen_ms = now() ? now() : 1;
    h.rssi = rssi;
    int slot = -1, old = 0;
    for (int i = 0; i < TCM_FOUND_MAX; i++) {
        if (!memcmp(s_found[i].mac, mac, 6)) {
            slot = i;
            break;
        }
        if (s_found[i].seen_ms < s_found[old].seen_ms) {
            old = i;
        }
    }
    s_found[slot >= 0 ? slot : old] = h;
    if ((s_pair.state == 1 || s_pair.state == 3) && !memcmp(mac, s_pair.mac, 6)) {
        if (m->flags & TC_FL_BOOT) {
            s_pair.state = 3;  // (the companion heard the request: it waits for its BOOT key)
        } else {
            s_pair.next = now();  // (it is on this channel now: the request goes at once)
        }
    }
    // a companion of this board that searches: a ping at once (it stays on this channel)
    const int i = tcm_find(mac);
    if (i >= 0 && h.paired && !memcmp(h.master, s_me, 6)) {
        s_c[i].ping_ms = now();
        simple(i, TC_PING);
    }
}

bool tcm_pair_start(const uint8_t mac[6], const char *name)
{
    memset(&s_pair, 0, sizeof s_pair);
    memcpy(s_pair.mac, mac, 6);
    name_copy(s_pair.name, name);
    s_io->random(s_pair.priv, 32);
    tc_x25519_base(s_pair.pub, s_pair.priv);
    s_pair.state = 1;
    s_pair.until = now() + PAIR_MS;
    s_pair.next = now();
    s_pair.index = -1;
    return true;
}

int tcm_pair_state(void)
{
    return s_pair.state;
}

int tcm_pair_index(void)
{
    return s_pair.index;
}

void tcm_pair_cancel(void)
{
    memset(&s_pair, 0, sizeof s_pair);
}

static void pair_tick(void)
{
    if (s_pair.state != 1 && s_pair.state != 3) {
        return;
    }
    const uint32_t t = now();
    if ((int32_t)(t - s_pair.until) >= 0) {
        s_pair.state = -1;
        return;
    }
    if ((int32_t)(t - s_pair.next) < 0) {
        return;
    }
    s_pair.next = t + PAIR_REQ_MS;
    tc_buf_t f;
    begin(&f, TC_PAIR_REQ);
    tc_putn(&f, s_pair.pub, 32);
    tc_putn(&f, s_name, (int)strlen(s_name));
    s_io->send(s_pair.mac, f.b, f.n);
}

static void on_pair_ok(const uint8_t mac[6], const uint8_t *d, int n, tc_msg_t *m, int rssi)
{
    if ((s_pair.state != 1 && s_pair.state != 3) || memcmp(mac, s_pair.mac, 6)) {
        return;
    }
    uint8_t pub[32], shared[32], key[TC_KEY_LEN];
    if (!tc_getn(m, pub, 32)) {
        return;
    }
    tc_x25519(shared, s_pair.priv, pub);
    tc_pair_key(key, shared, s_pair.pub, pub);
    memset(shared, 0, sizeof shared);
    if (!tc_check(d, n, key, mac)) {
        return;
    }
    int i = tcm_find(mac);
    if (i < 0) {
        if (s_n >= TCM_MAX) {
            s_pair.state = -1;
            return;
        }
        i = s_n++;
    }
    tcm_comp_t *c = &s_c[i];
    memset(c, 0, sizeof *c);
    memcpy(c->mac, mac, 6);
    memcpy(c->key, key, sizeof key);
    name_copy(c->name, s_pair.name);
    for (int k = 0; k < TCM_FOUND_MAX; k++) {
        if (!memcmp(s_found[k].mac, mac, 6)) {
            c->chip = s_found[k].chip;
        }
    }
    c->tx_ctr = 0;
    c->rx_ctr = m->counter;
    c->seen_ms = now() ? now() : 1;
    c->rssi = rssi;
    c->watch_pin = -1;
    memset(&s_r[i], 0, sizeof s_r[i]);
    s_pair.state = 2;
    s_pair.index = i;
    memset(s_pair.priv, 0, sizeof s_pair.priv);
    s_io->changed();
}

// ---------------------------------------------------------------- the requests
bool tcm_info(int i)
{
    if (!req_new(i, TCM_R_INFO)) {
        return false;
    }
    emit(i);
    return true;
}

bool tcm_config(int i, const tc_wire_t *w, int n)
{
    if (n < 0 || n > TC_WIRE_MAX || !req_new(i, TCM_R_CONFIG)) {
        return false;
    }
    memcpy(s_r[i].wire, w, (size_t)n * sizeof *w);
    s_r[i].nwire = n;
    emit(i);
    return true;
}

bool tcm_watch(int i, int pin, bool on)
{
    if (!req_new(i, TCM_R_WATCH)) {
        return false;
    }
    s_r[i].pin = (uint8_t)pin;
    s_r[i].a8 = on;
    s_c[i].watch_pin = on ? pin : -1;
    s_c[i].watch_n = 0;
    s_c[i].watch_edges = 0;
    s_c[i].watch_ms = 0;
    emit(i);
    return true;
}

bool tcm_pulse(int i, int pin, int ms)
{
    if (!req_new(i, TCM_R_PULSE)) {
        return false;
    }
    s_r[i].pin = (uint8_t)pin;
    s_r[i].a16 = (uint16_t)ms;
    emit(i);
    return true;
}

bool tcm_set(int i, int pin, int value, int ms)
{
    if (!req_new(i, TCM_R_SET)) {
        return false;
    }
    s_r[i].pin = (uint8_t)pin;
    s_r[i].a16 = (uint16_t)(value ? 1 : 0);
    s_r[i].rep_ms = (uint16_t)(ms > 0 && ms <= 30000 ? ms : 0);
    // (the new value shows at once; the TC_STATE of the companion tells the real one)
    for (int k = 0; k < s_c[i].nst; k++) {
        if (s_c[i].st[k].pin == pin) {
            s_c[i].st[k].v = s_r[i].a16;
        }
    }
    emit(i);
    return true;
}

bool tcm_ota(int i, const uint8_t *img, int n, int fw)
{
    if (!img || n <= 0 || !req_new(i, TCM_R_OTA)) {
        return false;
    }
    req_t *r = &s_r[i];
    r->img = img;
    r->size = (uint32_t)n;
    r->crc = tc_crc32(0, img, n);
    r->fw = (uint16_t)fw;
    r->phase = 0;
    emit(i);
    return true;
}

int tcm_ota_done(int i)
{
    return i >= 0 && i < s_n && s_r[i].type == TCM_R_OTA ? (int)s_r[i].base : 0;
}

bool tcm_value(int i, int pin, int *v, int *cnt)
{
    if (i < 0 || i >= s_n || !s_c[i].st_ms || !tcm_online(i)) {
        return false;
    }
    for (int k = 0; k < s_c[i].nst; k++) {
        if (s_c[i].st[k].pin == pin) {
            if (v) {
                *v = s_c[i].st[k].v;
            }
            if (cnt) {
                *cnt = s_c[i].st[k].cnt;
            }
            return true;
        }
    }
    return false;
}

bool tcm_capture(int i, int pin, int secs, int gap_ms)
{
    if (!req_new(i, TCM_R_CAPTURE)) {
        return false;
    }
    s_r[i].pin = (uint8_t)pin;
    s_r[i].a8 = (uint8_t)(secs > 0 && secs < 120 ? secs : 10);
    s_r[i].a16 = (uint16_t)(gap_ms > 0 && gap_ms < 200 ? gap_ms : 8);
    s_r[i].deadline = now() + (uint32_t)s_r[i].a8 * 1000 + 2000;
    s_asm.on = false;
    s_asm_owner = i;
    s_frame_n = 0;
    emit(i);
    return true;
}

bool tcm_send(int i, int pin, int carrier_khz, const int32_t *d, int n, int rep_at, int rep_ms)
{
    if (n <= 0 || !req_new(i, TCM_R_SEND)) {
        return false;
    }
    // the values (and the place of the repeat in the values)
    int nv = 0, vat = -1;
    for (int k = 0; k < n; k++) {
        if (k == rep_at) {
            vat = nv;
        }
        nv += tc_vals_from_levels(&d[k], 1, s_send_v + nv, TC_VALS_MAX - nv);
    }
    if (nv <= 0 || nv > TC_VALS_MAX) {
        req_end(i, TCM_FAIL, TC_E_BAD);
        return false;
    }
    req_t *r = &s_r[i];
    s_send_nv = nv;
    s_send_owner = i;
    r->pin = (uint8_t)pin;
    r->id = ++s_send_id;
    r->nparts = (nv + TC_PART_VALS - 1) / TC_PART_VALS;
    r->carrier = (uint8_t)(carrier_khz > 0 && carrier_khz < 256 ? carrier_khz : 0);
    r->rep_ms = (uint16_t)(rep_ms > 0 ? rep_ms : 0);
    r->rep_at = (uint16_t)(vat > 0 ? vat : nv);
    emit(i);
    return true;
}

void tcm_hold(int i)
{
    if (i < 0 || i >= s_n || s_r[i].type != TCM_R_SEND) {
        return;
    }
    tc_buf_t f;
    begin(&f, TC_HOLD);
    tc_put8(&f, s_r[i].id);
    send_to(i, &f);
}

void tcm_stop(int i)
{
    if (i < 0 || i >= s_n) {
        return;
    }
    for (int k = 0; k < 2; k++) {  // (two times: a lost STOP gives up to HOLD_MS of repeats)
        tc_buf_t f;
        begin(&f, TC_STOP);
        tc_put8(&f, s_r[i].id);
        send_to(i, &f);
    }
}

bool tcm_capture_ready(int i)
{
    return i >= 0 && i < s_n && s_r[i].type == TCM_R_CAPTURE && s_r[i].state == TCM_BUSY && s_r[i].acked;
}

int tcm_req(int i)
{
    if (i < 0 || i >= s_n || s_r[i].type == TCM_R_NONE) {
        return TCM_DONE;
    }
    return s_r[i].state;
}

int tcm_req_type(int i)
{
    return i >= 0 && i < s_n ? s_r[i].type : TCM_R_NONE;
}

int tcm_req_code(int i)
{
    return i >= 0 && i < s_n ? s_r[i].code : TC_E_BAD;
}

bool tcm_cfg_result(int i, uint8_t *err, uint8_t *warn, int *n)
{
    if (i < 0 || i >= s_n || s_r[i].type != TCM_R_CONFIG || !s_r[i].cfg_has) {
        return false;
    }
    *n = s_r[i].cfg_n;
    memcpy(err, s_r[i].cfg_err, (size_t)s_r[i].cfg_n);
    memcpy(warn, s_r[i].cfg_warn, (size_t)s_r[i].cfg_n);
    return s_r[i].cfg_ok;
}

int tcm_frame(int32_t *d, int max, int *pin)
{
    const int n = s_frame_n < max ? s_frame_n : max;
    memcpy(d, s_frame, (size_t)n * sizeof *d);
    if (pin) {
        *pin = s_frame_pin;
    }
    return n;
}

// ---------------------------------------------------------------- the answers
static void on_ack(int i, tc_msg_t *m, bool nak)
{
    req_t *r = &s_r[i];
    tc_get16(m);
    const int code = (int)tc_get8(m);
    const uint32_t arg = tc_get32(m);
    if (nak && code == TC_E_COUNTER) {
        // the companion had higher counters of this board (a restart of the app): go on from its value
        if (arg >= s_c[i].tx_ctr) {
            s_c[i].tx_ctr = arg + 1;
            s_io->changed();
        }
        if (r->state == TCM_BUSY) {
            emit(i);
        }
        return;
    }
    if (nak && code == TC_E_PARTS) {
        if (r->type == TCM_R_SEND && r->state == TCM_BUSY && (arg >> 8 & 0xFF) == r->id) {
            for (int k = 0; k < r->nparts; k++) {
                if (!(arg & (1u << k))) {
                    send_part(i, k);
                }
            }
            r->sent_ms = now();
        }
        return;
    }
    if (r->state != TCM_BUSY) {
        return;
    }
    switch (r->type) {
    case TCM_R_WATCH:
    case TCM_R_PULSE:
    case TCM_R_SET:
        req_end(i, nak ? TCM_FAIL : TCM_DONE, code);
        break;
    case TCM_R_OTA:
        if (nak) {
            req_end(i, TCM_FAIL, code);
        } else if (r->phase == 0) {
            r->phase = 1;
            r->base = r->next = 0;
            r->tries = 1;
            emit(i);
        } else if (r->phase == 1 && arg > r->base && arg <= r->size && arg != TC_OTA_DONE) {
            r->base = arg;
            r->tries = 1;
            r->sent_ms = now();
            if (r->base == r->size) {
                r->phase = 2;
                emit(i);
            } else {
                ota_fill(i);
            }
        } else if (r->phase == 2 && arg == TC_OTA_DONE) {
            req_end(i, TCM_DONE, 0);
        }
        break;
    case TCM_R_CAPTURE:
        if (nak) {
            req_end(i, TCM_FAIL, code);
        } else {
            r->acked = true;
        }
        break;
    case TCM_R_SEND:
        if ((arg & 0xFF) == r->id || nak) {
            req_end(i, nak ? TCM_FAIL : TCM_DONE, code);
        }
        break;
    default:
        if (nak) {
            req_end(i, TCM_FAIL, code);
        }
        break;
    }
}

static void on_frame_part(int i, tc_msg_t *m)
{
    tc_part_t p;
    if (!tc_part_get(m, &p)) {
        return;
    }
    if (s_frame_done && p.id == s_frame_id) {
        // (a part of the frame that came: the ACK was lost)
        if (p.part == p.nparts - 1) {
            tc_buf_t f;
            begin(&f, TC_ACK);
            tc_put16(&f, m->seq);
            tc_put8(&f, TC_E_OK);
            tc_put32(&f, p.id);
            send_to(i, &f);
        }
        return;
    }
    if (s_asm_owner != i) {
        return;
    }
    const int r = tc_asm_add(&s_asm, &p);
    s_asm_ms = now();
    if (r != 1) {
        return;
    }
    s_asm.on = false;
    // the ACK: the companion stops the sends again of this frame
    tc_buf_t f;
    begin(&f, TC_ACK);
    tc_put16(&f, m->seq);
    tc_put8(&f, TC_E_OK);
    tc_put32(&f, s_asm.id);
    send_to(i, &f);
    s_frame_id = s_asm.id;
    s_frame_done = true;
    s_frame_n = tc_levels_from_vals(s_asm.v, s_asm.total, s_frame, TC_VALS_MAX);
    s_frame_pin = s_asm.head.pin;
    if (s_r[i].type == TCM_R_CAPTURE && s_r[i].state == TCM_BUSY) {
        req_end(i, TCM_DONE, 0);
    }
}

static void on_edges(int i, tc_msg_t *m)
{
    tcm_comp_t *c = &s_c[i];
    const int pin = (int)tc_get8(m);
    if (pin != c->watch_pin) {
        return;
    }
    c->watch_level = tc_get8(m);
    c->watch_edges = tc_get32(m);
    c->watch_ms = now();
    const int k = (int)tc_get8(m);
    uint16_t v[100];
    for (int j = 0; j < k && j < 100; j++) {
        v[j] = (uint16_t)tc_get16(m);
    }
    int32_t d[100];
    const int nd = tc_levels_from_vals(v, k < 100 ? k : 100, d, 100);
    // the newest level times at the end
    const int cap = (int)(sizeof c->watch_d / sizeof c->watch_d[0]);
    for (int j = 0; j < nd; j++) {
        if (c->watch_n == cap) {
            memmove(c->watch_d, c->watch_d + 1, (size_t)(cap - 1) * sizeof c->watch_d[0]);
            c->watch_n--;
        }
        c->watch_d[c->watch_n++] = d[j];
    }
}

static void on_comp(int i, tc_msg_t *m)
{
    tcm_comp_t *c = &s_c[i];
    req_t *r = &s_r[i];
    switch (m->type) {
    case TC_PONG:
        if (r->type == TCM_R_PING && r->state == TCM_BUSY) {
            req_end(i, TCM_DONE, 0);
        }
        break;
    case TC_INFO:
        if (tc_info_get(m, &c->info)) {
            c->has_info = true;
            c->info_fresh = true;
            c->chip = c->info.chip;
            s_io->changed();
            if (r->type == TCM_R_INFO && r->state == TCM_BUSY) {
                req_end(i, TCM_DONE, 0);
            }
        }
        break;
    case TC_CFG_RES:
        if (r->type == TCM_R_CONFIG && r->state == TCM_BUSY) {
            r->cfg_ok = tc_get8(m);
            r->cfg_n = (int)tc_get8(m);
            if (r->cfg_n > TC_WIRE_MAX) {
                r->cfg_n = 0;
            }
            for (int k = 0; k < r->cfg_n; k++) {
                r->cfg_err[k] = (uint8_t)tc_get8(m);
                r->cfg_warn[k] = (uint8_t)tc_get8(m);
            }
            if (r->cfg_ok) {
                c->info.nwire = (uint8_t)r->nwire;
                memcpy(c->info.wire, r->wire, sizeof r->wire);
                s_io->changed();
            }
            r->cfg_has = true;
            req_end(i, r->cfg_ok ? TCM_DONE : TCM_FAIL, r->cfg_ok ? 0 : TC_E_BAD);
        }
        break;
    case TC_EDGES:
        on_edges(i, m);
        break;
    case TC_STATE: {
        tc_state_t st[TC_WIRE_MAX];
        const int k = tc_states_get(m, st, TC_WIRE_MAX);
        if (!m->bad) {
            memcpy(c->st, st, sizeof st);
            c->nst = k;
            c->st_ms = now() ? now() : 1;
        }
        break;
    }
    case TC_FRAME:
        on_frame_part(i, m);
        break;
    case TC_ACK:
        on_ack(i, m, false);
        break;
    case TC_NAK:
        on_ack(i, m, true);
        break;
    default:
        break;
    }
}

void tcm_rx(const uint8_t mac[6], const uint8_t *d, int n, int rssi)
{
    tc_msg_t m;
    if (!tc_parse(d, n, &m)) {
        return;
    }
    if (m.type == TC_HELLO && !(m.flags & TC_FL_TAG)) {
        on_hello(mac, &m, rssi);
        return;
    }
    if (m.type == TC_PAIR_OK) {
        on_pair_ok(mac, d, n, &m, rssi);
        return;
    }
    const int i = tcm_find(mac);
    if (i < 0 || !(m.flags & TC_FL_TAG) || !tc_check(d, n, s_c[i].key, mac)) {
        return;
    }
    // (the first frame after a start of the app: any counter; then only higher ones)
    if (s_c[i].rx_ctr && m.counter <= s_c[i].rx_ctr) {
        return;
    }
    s_c[i].rx_ctr = m.counter;
    s_c[i].seen_ms = now() ? now() : 1;
    s_c[i].rssi = rssi;
    on_comp(i, &m);
}

// ---------------------------------------------------------------- the clock
static void req_tick(int i)
{
    req_t *r = &s_r[i];
    if (r->type == TCM_R_NONE || r->state != TCM_BUSY) {
        return;
    }
    const uint32_t t = now();
    if (r->type == TCM_R_CAPTURE && r->acked) {
        // a frame with a missing part: ask for the parts again
        if (s_asm.on && s_asm.got && s_asm_owner == i && t - s_asm_ms >= PARTS_MS) {
            tc_buf_t f;
            begin(&f, TC_NAK);
            tc_put16(&f, 0);
            tc_put8(&f, TC_E_PARTS);
            tc_put32(&f, (uint32_t)s_asm.id << 8 | s_asm.got);
            send_to(i, &f);
            s_asm_ms = t;
        }
        if ((int32_t)(t - r->deadline) >= 0) {
            req_end(i, TCM_FAIL, TC_E_TIMEOUT);
        }
        return;
    }
    const uint32_t wait = r->type == TCM_R_SEND                   ? SEND_ACK_MS
                          : r->type == TCM_R_OTA && r->phase == 1 ? OTA_WAIT
                                                                  : REPLY_MS;
    if (t - r->sent_ms < wait) {
        return;
    }
    if (r->tries >= (r->type == TCM_R_OTA ? 3 * TRIES : TRIES)) {
        req_end(i, TCM_FAIL, TC_E_TIMEOUT);
        return;
    }
    r->tries++;
    emit(i);
}

void tcm_tick(void)
{
    const uint32_t t = now();
    pair_tick();
    for (int i = 0; i < s_n; i++) {
        req_tick(i);
        tcm_comp_t *c = &s_c[i];
        const uint32_t every = tcm_online(i) ? PING_MS : PING_FAST;
        const bool busy = s_r[i].type != TCM_R_NONE && s_r[i].state == TCM_BUSY;
        if (!busy && !c->info_fresh && tcm_online(i) && t - c->info_ms >= 1500) {
            // (the wiring may have changed since the file: ask for the info once after each start)
            c->info_ms = t;
            simple(i, TC_INFO_REQ);  // (a plain frame: the request of the app stays)
            continue;
        }
        if (!busy && t - c->ping_ms >= every) {
            c->ping_ms = t;
            simple(i, TC_PING);
        }
        // the live test goes on while the app shows it (the companion stops after 60 s)
        if (c->watch_pin >= 0 && c->watch_ms && t - c->watch_ms > 1500 && !busy) {
            c->watch_ms = t;
            tc_buf_t f;
            begin(&f, TC_WATCH);
            tc_put8(&f, (uint32_t)c->watch_pin);
            tc_put8(&f, 1);
            send_to(i, &f);
        }
    }
}
