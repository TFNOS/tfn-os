// SDRDSP: the waterfall ring and the persistence map (sdr_wf.h).
#include "sdr_wf.h"

#include <math.h>
#include <string.h>
#include "sdr_fft.h"
#include "sdr_trace.h"

bool sdr_wf_init(sdr_wf_t *h, int w, int rows)
{
    memset(h, 0, sizeof(*h));
    h->q = sdr_alloc((size_t)w * rows);
    h->hdr = sdr_alloc(sizeof(sdr_wf_row_t) * (size_t)rows);
    if (!h->q || !h->hdr) {
        sdr_wf_free(h);
        return false;
    }
    h->w = w;
    h->rows = rows;
    return true;
}

void sdr_wf_free(sdr_wf_t *h)
{
    sdr_free(h->q);
    sdr_free(h->hdr);
    memset(h, 0, sizeof(*h));
}

void sdr_wf_clear(sdr_wf_t *h)
{
    h->head = h->count = 0;
}

void sdr_wf_push(sdr_wf_t *h, const float *db, int n, int64_t f0_hz, double df_hz, uint32_t t_ms)
{
    if (!h->q) {
        return;
    }
    if (n > h->w) {
        n = h->w;
    }
    uint8_t *r = h->q + (size_t)h->head * h->w;
    for (int i = 0; i < n; i++) {
        r[i] = db[i] <= SDR_NODATA ? 0 : sdr_wf_q(db[i]);
    }
    for (int i = n; i < h->w; i++) {
        r[i] = 0;
    }
    sdr_wf_row_t *d = &h->hdr[h->head];
    d->f0_hz = f0_hz;
    d->df_hz = (float)df_hz;
    d->t_ms = t_ms;
    h->head = (h->head + 1) % h->rows;
    if (h->count < h->rows) {
        h->count++;
    }
}

const uint8_t *sdr_wf_row(const sdr_wf_t *h, int age, const sdr_wf_row_t **hdr)
{
    if (age < 0 || age >= h->count) {
        return NULL;
    }
    int k = (h->head - 1 - age + 2 * h->rows) % h->rows;
    if (hdr) {
        *hdr = &h->hdr[k];
    }
    return h->q + (size_t)k * h->w;
}

bool sdr_persist_init(sdr_persist_t *p, int w, int levels)
{
    memset(p, 0, sizeof(*p));
    p->d = sdr_alloc(sizeof(uint16_t) * (size_t)w * levels);
    if (!p->d) {
        return false;
    }
    p->w = w;
    p->levels = levels;
    p->range_db = 1;
    return true;
}

void sdr_persist_free(sdr_persist_t *p)
{
    sdr_free(p->d);
    memset(p, 0, sizeof(*p));
}

void sdr_persist_scale(sdr_persist_t *p, float top_db, float range_db, int64_t f0_hz, double df_hz)
{
    if (!p->d) {
        return;
    }
    if (p->top_db == top_db && p->range_db == range_db && p->f0_hz == f0_hz && fabs(p->df_hz - df_hz) < 1e-6) {
        return;
    }
    memset(p->d, 0, sizeof(uint16_t) * (size_t)p->w * p->levels);
    p->top_db = top_db;
    p->range_db = range_db > 1 ? range_db : 1;
    p->f0_hz = f0_hz;
    p->df_hz = df_hz;
}

void sdr_persist_push(sdr_persist_t *p, const float *db, int n, int shift)
{
    if (!p->d) {
        return;
    }
    if (n > p->w) {
        n = p->w;
    }
    const size_t cells = (size_t)p->w * p->levels;
    for (size_t k = 0; k < cells; k++) {
        p->d[k] -= p->d[k] >> shift;
    }
    const float k = (float)p->levels / p->range_db;
    for (int i = 0; i < n; i++) {
        if (db[i] <= SDR_NODATA) {
            continue;
        }
        int l = (int)((p->top_db - db[i]) * k);
        if (l < 0) {
            l = 0;
        }
        if (l >= p->levels) {
            l = p->levels - 1;
        }
        uint16_t *c = &p->d[(size_t)l * p->w + i];
        *c = *c > 65535 - 2048 ? 65535 : (uint16_t)(*c + 2048);
    }
}
