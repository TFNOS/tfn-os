// SDRDSP: the traces, the floor and the peaks (sdr_trace.h).
#include "sdr_trace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sdr_fft.h"

bool sdr_traces_init(sdr_traces_t *t, int nmax)
{
    memset(t, 0, sizeof(*t));
    for (int k = 0; k < SDR_TR_N; k++) {
        t->v[k] = sdr_alloc(sizeof(float) * (size_t)nmax);
        if (!t->v[k]) {
            return false;
        }
    }
    t->avg_p = sdr_alloc(sizeof(float) * (size_t)nmax);
    t->cnt = sdr_alloc(sizeof(uint16_t) * (size_t)nmax);
    if (!t->avg_p || !t->cnt) {
        return false;
    }
    t->nmax = nmax;
    t->avg_n = 16;
    sdr_traces_reset(t);
    return true;
}

void sdr_traces_free(sdr_traces_t *t)
{
    for (int k = 0; k < SDR_TR_N; k++) {
        sdr_free(t->v[k]);
    }
    sdr_free(t->avg_p);
    sdr_free(t->cnt);
    memset(t, 0, sizeof(*t));
}

void sdr_traces_reset(sdr_traces_t *t)
{
    for (int i = 0; i < t->nmax; i++) {
        for (int k = 0; k < SDR_TR_N; k++) {
            t->v[k][i] = SDR_NODATA;
        }
        t->avg_p[i] = 0;
        t->cnt[i] = 0;
    }
    t->frames = 0;
}

static float db2p(float db)
{
    return powf(10.0f, db * 0.1f);
}
static float p2db(float p)
{
    return 10.0f * log10f(p + 1e-20f);
}

void sdr_traces_axis(sdr_traces_t *t, int n, int64_t f0_hz, double df_hz)
{
    if (n > t->nmax) {
        n = t->nmax;
    }
    if (t->n == n && t->f0_hz == f0_hz && fabs(t->df_hz - df_hz) < 1e-6) {
        return;
    }
    if (t->n <= 0 || t->df_hz <= 0) {
        t->n = n;
        t->f0_hz = f0_hz;
        t->df_hz = df_hz;
        sdr_traces_reset(t);
        return;
    }
    // the old values: a copy (the traces are small: 1024 points at most)
    const int on = t->n;
    const double of0 = (double)t->f0_hz, odf = t->df_hz;
    float *old[SDR_TR_N + 1];
    uint16_t *ocnt = malloc(sizeof(uint16_t) * (size_t)on);
    bool ok = ocnt != NULL;
    for (int k = 0; k <= SDR_TR_N; k++) {
        old[k] = malloc(sizeof(float) * (size_t)on);
        if (!old[k]) {
            ok = false;
        }
    }
    if (ok) {
        for (int k = 0; k < SDR_TR_N; k++) {
            memcpy(old[k], t->v[k], sizeof(float) * (size_t)on);
        }
        memcpy(old[SDR_TR_N], t->avg_p, sizeof(float) * (size_t)on);
        memcpy(ocnt, t->cnt, sizeof(uint16_t) * (size_t)on);
        for (int i = 0; i < n; i++) {
            // the old points under the band of the new point i
            double a = ((double)f0_hz + i * df_hz - of0) / odf, b = ((double)f0_hz + (i + 1) * df_hz - of0) / odf;
            int j0 = (int)floor(a), j1 = (int)ceil(b) - 1;
            if (j1 < j0) {
                j1 = j0;
            }
            if (j0 < 0) {
                j0 = 0;
            }
            if (j1 > on - 1) {
                j1 = on - 1;
            }
            float mx = SDR_NODATA, mn = SDR_NODATA, lv = SDR_NODATA;
            double ap = 0;
            int ac = 0, cmin = 65535;
            if (j0 <= j1 && b > 0 && a < on) {
                for (int j = j0; j <= j1; j++) {
                    if (old[SDR_TR_MAX][j] > SDR_NODATA && old[SDR_TR_MAX][j] > mx) {
                        mx = old[SDR_TR_MAX][j];
                    }
                    if (old[SDR_TR_MIN][j] > SDR_NODATA && (mn == SDR_NODATA || old[SDR_TR_MIN][j] < mn)) {
                        mn = old[SDR_TR_MIN][j];
                    }
                    if (old[SDR_TR_LIVE][j] > lv) {
                        lv = old[SDR_TR_LIVE][j];
                    }
                    if (ocnt[j]) {
                        ap += old[SDR_TR_N][j];
                        ac++;
                        if (ocnt[j] < cmin) {
                            cmin = ocnt[j];
                        }
                    }
                }
            }
            t->v[SDR_TR_MAX][i] = mx;
            t->v[SDR_TR_MIN][i] = mn;
            t->v[SDR_TR_LIVE][i] = lv;
            t->avg_p[i] = ac ? (float)(ap / ac) : 0;
            t->cnt[i] = ac ? (uint16_t)cmin : 0;
            t->v[SDR_TR_AVG][i] = ac ? p2db(t->avg_p[i]) : SDR_NODATA;
        }
    } else {
        sdr_traces_reset(t);
    }
    for (int k = 0; k <= SDR_TR_N; k++) {
        free(old[k]);
    }
    free(ocnt);
    for (int i = n; i < t->nmax; i++) {
        for (int k = 0; k < SDR_TR_N; k++) {
            t->v[k][i] = SDR_NODATA;
        }
        t->avg_p[i] = 0;
        t->cnt[i] = 0;
    }
    t->n = n;
    t->f0_hz = f0_hz;
    t->df_hz = df_hz;
}

void sdr_traces_push(sdr_traces_t *t, const float *db, float dt_s)
{
    const float fall = t->decay_db * dt_s;
    const int an = t->avg_n < 1 ? 1 : t->avg_n > 60000 ? 60000 : t->avg_n;
    for (int i = 0; i < t->n; i++) {
        float d = db[i];
        t->v[SDR_TR_LIVE][i] = d;
        if (d <= SDR_NODATA) {
            continue;  // (a segment that did not tune: the holds and the mean stay)
        }
        float *mx = &t->v[SDR_TR_MAX][i];
        if (*mx > SDR_NODATA && fall > 0) {
            *mx -= fall;
        }
        if (d > *mx) {
            *mx = d;
        }
        float *mn = &t->v[SDR_TR_MIN][i];
        if (*mn == SDR_NODATA || d < *mn) {
            *mn = d;
        }
        float p = db2p(d);
        if (t->cnt[i] < an) {
            t->cnt[i]++;
        }
        t->avg_p[i] += (p - t->avg_p[i]) / t->cnt[i];
        t->v[SDR_TR_AVG][i] = p2db(t->avg_p[i]);
    }
    t->frames++;
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

float sdr_floor(const float *db, int n)
{
    static float tmp[1024];  // (not on the stack: the task of an engine has a small stack)
    int m = 0;
    for (int i = 0; i < n && m < 1024; i++) {
        if (db[i] > SDR_NODATA) {
            tmp[m++] = db[i];
        }
    }
    if (!m) {
        return SDR_NODATA;
    }
    qsort(tmp, (size_t)m, sizeof(float), cmpf);
    return tmp[m / 5];
}

int sdr_peaks(const float *db, int n, float floor_db, float rise_db, int sep, sdr_peak_t *out, int max)
{
    if (sep < 1) {
        sep = 1;
    }
    int cnt = 0;
    for (int i = 1; i < n - 1 && n >= 3; i++) {
        float v = db[i];
        if (v <= SDR_NODATA || v < floor_db + rise_db) {
            continue;
        }
        if (!(v > db[i - 1] && v >= db[i + 1])) {
            continue;
        }
        // the prominence: walk to each side until a higher point; the higher of the two lowest points between
        float lo_l = v, lo_r = v;
        int j = i - 1;
        while (j >= 0 && db[j] <= v) {
            if (db[j] > SDR_NODATA && db[j] < lo_l) {
                lo_l = db[j];
            }
            j--;
        }
        bool hl = j >= 0;
        j = i + 1;
        while (j < n && db[j] <= v) {
            if (db[j] > SDR_NODATA && db[j] < lo_r) {
                lo_r = db[j];
            }
            j++;
        }
        bool hr = j < n;
        float base = hl && hr ? (lo_l > lo_r ? lo_l : lo_r) : hl ? lo_l : hr ? lo_r : (lo_l < lo_r ? lo_l : lo_r);
        if (v - base < rise_db) {
            continue;
        }
        // the top with a fraction: a parabola through three points
        float a = db[i - 1], c = db[i + 1], den = a - 2 * v + c;
        float x = (float)i + (den < 0 ? 0.5f * (a - c) / den : 0.0f);
        sdr_peak_t pk = {i, v, x};
        // keep the highest `max`, at least sep apart
        bool near_higher = false;
        for (int k = 0; k < cnt; k++) {
            if (abs(out[k].i - i) < sep) {
                if (out[k].db >= v) {
                    near_higher = true;
                } else {
                    out[k] = out[--cnt];
                    k--;
                }
            }
        }
        if (near_higher) {
            continue;
        }
        if (cnt < max) {
            out[cnt++] = pk;
        } else {
            int lo = 0;
            for (int k = 1; k < cnt; k++) {
                if (out[k].db < out[lo].db) {
                    lo = k;
                }
            }
            if (out[lo].db < v) {
                out[lo] = pk;
            }
        }
    }
    // the highest first
    for (int a = 0; a < cnt; a++) {
        for (int b = a + 1; b < cnt; b++) {
            if (out[b].db > out[a].db) {
                sdr_peak_t t = out[a];
                out[a] = out[b];
                out[b] = t;
            }
        }
    }
    return cnt;
}
