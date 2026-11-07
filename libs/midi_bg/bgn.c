// BGN: the compact note form of MIDI_BG (see bgn.h).
#include <stdlib.h>
#include <string.h>
#include "bgn.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *ps_alloc(size_t n)
{
    return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM);
}
#else
static void *ps_alloc(size_t n)
{
    return malloc(n ? n : 1);
}
#endif

#define F_PROG    0x01
#define F_VEL     0x02
#define F_SAME    0x04
#define N_STREAMS 5

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static float rdf(const uint8_t *p)
{
    uint32_t v = rd32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void wrf(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wr32(p, v);
}

// a stream: the bytes from p to end
typedef struct {
    const uint8_t *p, *end;
} rd_t;

static bool rd_byte(rd_t *r, uint8_t *v)
{
    if (r->p >= r->end) {
        return false;
    }
    *v = *r->p++;
    return true;
}

// a varint (7 bits in each byte, the high bit = more)
static bool rd_var(rd_t *r, uint32_t *v)
{
    uint32_t x = 0;
    for (int s = 0; s < 35; s += 7) {
        uint8_t b;
        if (!rd_byte(r, &b)) {
            return false;
        }
        x |= (uint32_t)(b & 0x7F) << s;
        if (!(b & 0x80)) {
            *v = x;
            return true;
        }
    }
    return false;
}

static size_t wr_var(uint8_t *p, uint32_t v)
{
    size_t n = 0;
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        p[n++] = b | (v ? 0x80 : 0);
    } while (v);
    return n;
}

static int cmp_note(const void *pa, const void *pb)
{
    const midi_note_t *a = pa, *b = pb;
    if (a->on != b->on) {
        return a->on < b->on ? -1 : 1;
    }
    if (a->pitch != b->pitch) {
        return a->pitch > b->pitch ? -1 : 1;  // the top note first
    }
    if (a->ch != b->ch) {
        return a->ch < b->ch ? -1 : 1;
    }
    if (a->off != b->off) {
        return a->off < b->off ? -1 : 1;
    }
    if (a->vel != b->vel) {
        return a->vel < b->vel ? -1 : 1;
    }
    if (a->prog != b->prog) {
        return a->prog < b->prog ? -1 : 1;
    }
    return 0;
}

int bgn_normalize(midi_notes_t *m)
{
    if (m->n > 1) {
        qsort(m->notes, (size_t)m->n, sizeof(midi_note_t), cmp_note);
    }
    int w = 0;
    for (int i = 0; i < m->n; i++) {
        const midi_note_t *a = &m->notes[i];
        if (w > 0) {
            const midi_note_t *b = &m->notes[w - 1];
            if (b->on == a->on && b->ch == a->ch && b->pitch == a->pitch) {
                continue;
            }
        }
        m->notes[w++] = *a;
    }
    int removed = m->n - w;
    m->n = w;
    return removed;
}

bool bgn_decode(const uint8_t *data, size_t len, midi_notes_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len < BGN_HEADER || memcmp(data, BGN_MAGIC, 4)) {
        return false;
    }
    uint32_t n = rd32(data + 4);
    // each note needs 3 bytes or more (time, flags, pitch)
    if (n == 0 || n > (len - BGN_HEADER) / 3) {
        return false;
    }
    rd_t s[N_STREAMS];
    size_t at = BGN_HEADER;
    for (int k = 0; k < N_STREAMS; k++) {
        uint32_t sl = rd32(data + 24 + 4 * k);
        if (sl > len - at) {
            return false;
        }
        s[k].p = data + at;
        s[k].end = data + at + sl;
        at += sl;
    }
    if (at != len) {
        return false;
    }
    midi_note_t *notes = ps_alloc((size_t)n * sizeof(midi_note_t));
    if (!notes) {
        return false;
    }
    out->tpb = rdf(data + 8);
    out->bpm = rdf(data + 12);
    out->seconds = rdf(data + 16);
    out->ts_num = data[20];
    out->ts_den = data[21];
    out->channels = (uint16_t)(data[22] | data[23] << 8);
    uint32_t i = 0;
    for (int ch = 0; ch < 16; ch++) {
        if (!(out->channels >> ch & 1)) {
            continue;
        }
        uint32_t count, on = 0, dt, dur = 0;
        uint8_t prog = 0, vel = 0;
        if (!rd_var(&s[0], &count) || count > n - i) {
            goto bad;
        }
        for (uint32_t j = 0; j < count; j++, i++) {
            uint8_t fl, pitch;
            if (!rd_var(&s[0], &dt) || !rd_byte(&s[1], &fl) || !rd_byte(&s[2], &pitch)) {
                goto bad;
            }
            if ((fl & F_PROG) && !rd_byte(&s[3], &prog)) {
                goto bad;
            }
            if ((fl & F_VEL) && !rd_byte(&s[3], &vel)) {
                goto bad;
            }
            if (!(fl & F_SAME) && !rd_var(&s[4], &dur)) {
                goto bad;
            }
            on += dt;
            notes[i] = (midi_note_t){
                    .on = on, .off = on + dur, .ch = (uint8_t)ch, .pitch = pitch, .vel = vel, .prog = prog};
        }
    }
    // all the data must be used
    if (i != n) {
        goto bad;
    }
    for (int k = 0; k < N_STREAMS; k++) {
        if (s[k].p != s[k].end) {
            goto bad;
        }
    }
    out->notes = notes;
    out->n = (int)n;
    qsort(out->notes, n, sizeof(midi_note_t), cmp_note);
    return true;
bad:
    free(notes);
    memset(out, 0, sizeof(*out));
    return false;
}

size_t bgn_encode(const midi_notes_t *m, uint8_t *buf, size_t cap)
{
    if (cap < BGN_MAX_SIZE(m->n)) {
        return 0;
    }
    // the stream sizes are not known yet: make each stream in its own part of a work buffer
    size_t n = (size_t)m->n;
    uint8_t *st[N_STREAMS];
    size_t sz[N_STREAMS] = {0};
    size_t room[N_STREAMS] = {16 * 5 + n * 5, n, n, 2 * n, n * 5};
    for (int k = 0; k < N_STREAMS; k++) {
        st[k] = malloc(room[k]);
    }
    // the notes of each channel in time order (an index list, so the input stays as it is)
    int *idx = malloc((n ? n : 1) * sizeof(int));
    uint16_t channels = 0;
    for (int ch = 0; ch < 16; ch++) {
        size_t c = 0;
        for (size_t i = 0; i < n; i++) {
            if (m->notes[i].ch == ch) {
                idx[c++] = (int)i;
            }
        }
        if (!c) {
            continue;
        }
        channels |= (uint16_t)(1u << ch);
        // a stable sort by the start (insertion sort: the list is almost sorted)
        for (size_t a = 1; a < c; a++) {
            int v = idx[a];
            size_t b = a;
            while (b > 0 && m->notes[idx[b - 1]].on > m->notes[v].on) {
                idx[b] = idx[b - 1], b--;
            }
            idx[b] = v;
        }
        sz[0] += wr_var(st[0] + sz[0], (uint32_t)c);
        uint32_t on = 0, last_dur = 0;
        int prog = -1, vel = -1;
        bool have_dur = false;
        for (size_t a = 0; a < c; a++) {
            const midi_note_t *x = &m->notes[idx[a]];
            uint32_t dur = x->off - x->on;
            uint8_t fl = 0;
            if (prog != x->prog) {
                fl |= F_PROG;
            }
            if (vel != x->vel) {
                fl |= F_VEL;
            }
            if (have_dur && dur == last_dur) {
                fl |= F_SAME;
            }
            sz[0] += wr_var(st[0] + sz[0], x->on - on);
            on = x->on;
            st[1][sz[1]++] = fl;
            st[2][sz[2]++] = x->pitch;
            if (fl & F_PROG) {
                st[3][sz[3]++] = x->prog, prog = x->prog;
            }
            if (fl & F_VEL) {
                st[3][sz[3]++] = x->vel, vel = x->vel;
            }
            if (!(fl & F_SAME)) {
                sz[4] += wr_var(st[4] + sz[4], dur), last_dur = dur, have_dur = true;
            }
        }
    }
    free(idx);
    memcpy(buf, BGN_MAGIC, 4);
    wr32(buf + 4, (uint32_t)m->n);
    wrf(buf + 8, m->tpb);
    wrf(buf + 12, m->bpm);
    wrf(buf + 16, m->seconds);
    buf[20] = m->ts_num;
    buf[21] = m->ts_den;
    buf[22] = (uint8_t)channels;
    buf[23] = (uint8_t)(channels >> 8);
    size_t k = BGN_HEADER;
    for (int j = 0; j < N_STREAMS; j++) {
        wr32(buf + 24 + 4 * j, (uint32_t)sz[j]);
        memcpy(buf + k, st[j], sz[j]);
        k += sz[j];
        free(st[j]);
    }
    return k;
}
