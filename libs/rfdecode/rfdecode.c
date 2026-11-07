// RFDECODE: the slicer, the decoders and the encoders (rfdecode.h).
// (The work arrays are static, not on the stack: the task of an app has a small stack. One task uses the decoders.)
#include "rfdecode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static void bit_set(rfd_code_t *c, int i, int v)
{
    if (i >= RFD_MAX_BITS) {
        return;
    }
    if (v) {
        c->bits[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    } else {
        c->bits[i >> 3] &= (uint8_t)~(0x80 >> (i & 7));
    }
}

int rfd_bit(const rfd_code_t *c, int i)
{
    return i < RFD_MAX_BITS ? (c->bits[i >> 3] >> (7 - (i & 7))) & 1 : 0;
}

// ================================================================ the slicer

void rfd_slicer_init(rfd_slicer_t *s, int min_pulse_us, int gap_us)
{
    memset(s, 0, sizeof(*s));
    s->min_pulse = min_pulse_us;
    s->gap = gap_us;
}

static void packet_end(rfd_slicer_t *s, int32_t gap_after, int64_t t_us)
{
    rfd_packet_t *p = &s->cur;
    p->gap_after = gap_after;
    p->t_us = t_us;
    // (16 times = 8 bits at least; the noise of a receiver between two long lows is short or has no shape: the
    // decoders find no code in it)
    if (p->n >= 16) {
        if (s->qn - s->qr < RFD_QUEUE) {
            s->q[(s->qn++) % RFD_QUEUE] = *p;
        } else {
            s->dropped++;
        }
    }
    p->n = 0;
    p->gap_before = gap_after;
    s->in = true;  // (the long low starts the next packet)
}

// One level time that is final (it cannot join another one now)
static void level_done(rfd_slicer_t *s, int32_t v, int64_t t_us)
{
    rfd_packet_t *p = &s->cur;
    if (v < 0 && -v >= s->gap) {  // a long low: the end of a packet and the start of the next one
        if (s->in && p->n > 0) {
            packet_end(s, -v, t_us);
        } else {
            p->n = 0;
            p->gap_before = -v;
            s->in = true;
        }
        return;
    }
    if (!s->in) {
        return;  // (the noise before the first long low)
    }
    if (p->n == 0 && v < 0) {
        return;  // (a packet starts with a high)
    }
    if (v > 20 * s->gap || p->n >= RFD_MAX_DUR) {  // a carrier that stays on, or a packet too long: no packet
        if (p->n >= RFD_MAX_DUR) {
            s->dropped++;
        }
        p->n = 0;
        s->in = false;
        return;
    }
    p->d[p->n++] = v;
}

void rfd_slicer_feed(rfd_slicer_t *s, const int32_t *d, int n, int64_t t_us)
{
    for (int i = 0; i < n; i++) {
        int32_t v = d[i];
        if (!v) {
            continue;
        }
        if (s->has_prev && (v > 0) == (s->prev > 0)) {  // the same level again (after a glitch): one level
            s->prev += v;
            continue;
        }
        if (s->has_prev && iabs32(v) < s->min_pulse) {  // a glitch: the level before goes on
            s->prev -= v;  // (v has the other sign: |v| more of the level before, a high or a low; "+= v" would
            continue;      // make the low shorter at a high glitch in a low)
        }
        if (s->has_prev) {
            level_done(s, s->prev, t_us);
        }
        s->prev = v;
        s->has_prev = true;
    }
}

void rfd_slicer_idle(rfd_slicer_t *s, int32_t quiet_us, int64_t t_us)
{
    // the line is low and quiet: the level that waits (the last high) is final, and the packet ends
    if (quiet_us < s->gap) {
        return;
    }
    if (s->has_prev) {
        level_done(s, s->prev, t_us);
        s->has_prev = false;
    }
    if (s->in && s->cur.n > 0) {
        packet_end(s, quiet_us, t_us);
    }
}

bool rfd_slicer_pop(rfd_slicer_t *s, rfd_packet_t *out)
{
    if (s->qr == s->qn) {
        return false;
    }
    *out = s->q[(s->qr++) % RFD_QUEUE];
    return true;
}

// ================================================================ RCSWITCH

// The 12 protocols of the RCSwitch library (the times in pulse lengths: the sync, a zero, a one; inverted: the levels
// are the other way on the air)
typedef struct {
    uint16_t pulse;
    uint8_t sh, sl, zh, zl, oh, ol;
    bool inv;
} rcp_t;
static const rcp_t RCP[12] = {
        {350, 1, 31, 1, 3, 3, 1, false},     // 1: PT2262, EV1527 and most remote keys
        {650, 1, 10, 1, 2, 2, 1, false},     // 2
        {100, 30, 71, 4, 11, 9, 6, false},   // 3
        {380, 1, 6, 1, 3, 3, 1, false},      // 4
        {500, 6, 14, 1, 2, 2, 1, false},     // 5
        {450, 23, 1, 1, 2, 2, 1, true},      // 6: HT6P20B
        {150, 2, 62, 1, 6, 6, 1, false},     // 7: HS2303-PT
        {200, 3, 130, 7, 16, 3, 16, false},  // 8: Conrad RS-200 RX
        {200, 130, 7, 16, 7, 16, 3, true},   // 9: Conrad RS-200 TX
        {365, 18, 1, 3, 1, 1, 3, true},      // 10: 1ByOne doorbell
        {270, 36, 1, 1, 2, 2, 1, true},      // 11: HT12E
        {320, 36, 1, 1, 2, 2, 1, true},      // 12: SM5212
};

static bool near(int32_t v, float want, float tol)
{
    return fabsf((float)v - want) < tol;
}

// One protocol on a packet: the bits and the mean error (in pulse lengths); false when a time does not fit
static bool rcs_try(const rfd_packet_t *p, const rcp_t *r, rfd_code_t *c, float *err)
{
    const int32_t *d = p->d;
    const int n = p->n;
    float delay;
    int first, k;
    if (!r->inv) {
        // the bits as (high, low) pairs, then the high of the sync; the long low of the sync is the gap after it
        if (n < 17 || !(n & 1)) {
            return false;
        }
        delay = (float)p->gap_after / r->sl;
        k = (n - 1) / 2;
        first = 0;
        if (!near(d[n - 1], r->sh * delay, 0.6f * delay)) {
            return false;
        }
    } else {
        // the long low of the sync is the gap BEFORE; the high of the sync starts the packet, then (low, high) pairs
        if (n < 17 || !(n & 1)) {
            return false;
        }
        int32_t g = p->gap_before ? p->gap_before : p->gap_after;
        delay = (float)g / r->sh;
        k = (n - 1) / 2;
        first = 1;
        if (!near(d[0], r->sl * delay, 0.6f * delay)) {
            return false;
        }
    }
    if (k > 64) {
        return false;
    }
    // the pulse unit from the data: each bit is (zh + zl) or (oh + ol) units. The gap must agree with it: a noise spike
    // in the sync low makes a short gap, and with the unit of the gap only, P1 (bits of 4 units) also fits P2 (3 units)
    {
        float sum = 0;
        for (int i = 0; i < k; i++) {
            sum += (float)(iabs32(d[first + 2 * i]) + iabs32(d[first + 2 * i + 1]));
        }
        const float units = 0.5f * (float)(r->zh + r->zl + r->oh + r->ol);
        const float dd = sum / ((float)k * units);
        // (the sync ratio tells the protocol: a PWM remote with a gap of 23 units is not P1 with 31. The bits use the
        // unit of the gap as before: it is exact when the gap is good, and it keeps the order of two equal fits)
        if (delay < 0.8f * dd || delay > 1.25f * dd) {
            return false;
        }
    }
    if (delay < 0.6f * r->pulse || delay > 1.7f * r->pulse) {
        return false;
    }
    // the tolerance: 60 % of the pulse length (as RCSwitch), and at least 20 % of the time (a level of 16 pulses has
    // the jitter of 16 pulses)
#define TOL(m) fmaxf(0.6f * delay, 0.2f * (m) * delay)
    // the error: relative to each time (a protocol with a long pulse does not win only by its pulse unit)
    float e = 0;
    for (int i = 0; i < k; i++) {
        int32_t a = iabs32(d[first + 2 * i]), b = iabs32(d[first + 2 * i + 1]);
        float z = fabsf(a - r->zh * delay) / (r->zh * delay) + fabsf(b - r->zl * delay) / (r->zl * delay);
        float o = fabsf(a - r->oh * delay) / (r->oh * delay) + fabsf(b - r->ol * delay) / (r->ol * delay);
        bool zero = near(a, r->zh * delay, TOL(r->zh)) && near(b, r->zl * delay, TOL(r->zl));
        bool one = near(a, r->oh * delay, TOL(r->oh)) && near(b, r->ol * delay, TOL(r->ol));
        if (zero && (!one || z <= o)) {
            bit_set(c, i, 0);
            e += z;
        } else if (one) {
            bit_set(c, i, 1);
            e += o;
        } else {
            return false;
        }
    }
#undef TOL
    c->nbits = (uint16_t)k;
    c->t_short = (uint16_t)(delay + 0.5f);
    c->t_long = 0;
    c->t_gap = (uint32_t)(r->inv ? (p->gap_before ? p->gap_before : p->gap_after) : p->gap_after);
    // (two protocols of one shape and other pulse lengths, 11 and 12: the nearer pulse length wins)
    *err = e / k + 0.5f * fabsf(delay - r->pulse) / r->pulse;
    return true;
}

bool rfd_rcswitch(const rfd_packet_t *p, rfd_code_t *c)
{
    rfd_code_t best, t;
    float best_e = 1e9f;
    bool ok = false;
    for (int i = 0; i < 12; i++) {
        memset(&t, 0, sizeof(t));
        float e;
        if (!rcs_try(p, &RCP[i], &t, &e) || t.nbits < 8) {
            continue;
        }
        if (e < best_e) {
            best_e = e;
            best = t;
            best.proto = (uint8_t)(i + 1);
            ok = true;
        }
    }
    if (!ok) {
        return false;
    }
    *c = best;
    c->kind = RFD_K_RCSWITCH;
    c->score = best_e < 1 ? 1.0f - best_e * 0.5f : 0.5f;
    return true;
}

// ================================================================ the clusters of the times

static int cmp_i32(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return x < y ? -1 : x > y;
}

// The clusters of a list of times (each one within 35 % of its middle): the middles, smallest first. Returns the count
// (more than max: the times have no shape; the first max middles are in out)
static int clusters(const int32_t *v, int n, uint16_t *out, int max)
{
    if (n <= 0) {
        return 0;
    }
    static int32_t s[RFD_MAX_DUR];
    if (n > RFD_MAX_DUR) {
        n = RFD_MAX_DUR;
    }
    memcpy(s, v, sizeof(int32_t) * (size_t)n);
    qsort(s, (size_t)n, sizeof(int32_t), cmp_i32);
    int cnt = 0;
    double sum = s[0];
    int m = 1;
    for (int i = 1; i <= n; i++) {
        double mid = sum / m;
        if (i == n || s[i] > mid * 1.35 + 40) {
            if (cnt < max) {
                out[cnt] = (uint16_t)(mid > 65535 ? 65535 : mid < 0 ? 0 : mid + 0.5);  // (a bad packet: no wrap)
            }
            cnt++;
            if (i < n) {
                sum = s[i];
                m = 1;
            }
        } else {
            sum += s[i];
            m++;
        }
    }
    return cnt;
}

// the index of the cluster nearest to v (in ratio)
static int nearest(int32_t v, const uint16_t *c, int n, float *fit)
{
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        float d = fabsf((float)v - c[i]) / (float)c[i];
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (fit) {
        *fit = bd;
    }
    return best;
}

void rfd_raw(const rfd_packet_t *p, rfd_code_t *c)
{
    memset(c, 0, sizeof(*c));
    c->kind = RFD_K_RAW;
    static int32_t h[RFD_MAX_DUR / 2 + 1], l[RFD_MAX_DUR / 2 + 1];
    int nh = 0, nl = 0;
    for (int i = 0; i < p->n; i++) {
        if (p->d[i] > 0) {
            h[nh++] = p->d[i];
        } else {
            l[nl++] = -p->d[i];
        }
    }
    int a = clusters(h, nh, c->ch, 4), b = clusters(l, nl, c->cl, 4);
    c->nclust_h = (uint8_t)(a > 4 ? 5 : a);
    c->nclust_l = (uint8_t)(b > 4 ? 5 : b);
    c->t_gap = (uint32_t)p->gap_after;
    c->nbits = (uint16_t)p->n;  // (RAW: the count of the level times)
}

// ================================================================ GENERIC

// The part of the packet after its last inner sync (a low much longer than the others): a preamble or a header
static int data_start(const rfd_packet_t *p)
{
    static int32_t l[RFD_MAX_DUR / 2 + 1];
    int nl = 0;
    for (int i = 1; i < p->n; i += 2) {
        l[nl++] = -p->d[i];
    }
    if (nl < 4) {
        return 0;
    }
    qsort(l, (size_t)nl, sizeof(int32_t), cmp_i32);
    int32_t med = l[nl / 2];
    int start = 0;
    for (int i = 1; i < p->n - 8; i += 2) {
        if (-p->d[i] > 4 * med && -p->d[i] > 1500) {
            start = i + 1;
        }
    }
    return start;
}

static bool try_pwm(const rfd_packet_t *p, int s0, rfd_code_t *c)
{
    static int32_t h[RFD_MAX_DUR / 2 + 1];
    int nh = 0;
    for (int i = s0; i < p->n; i += 2) {
        h[nh++] = p->d[i];
    }
    uint16_t hc[4];
    if (nh < 8 || clusters(h, nh, hc, 4) != 2 || hc[1] < hc[0] * 1.4f) {
        return false;
    }
    // an odd count with a short last high: that high is the sync pulse before the gap (PT2262, EV1527, RCSwitch: 12,
    // 24 or 32 bits and a sync), not a bit
    float f_last;
    if ((nh & 1) && nh >= 9 && nearest(h[nh - 1], hc, 2, &f_last) == 0) {
        nh--;
    }
    float bad = 0;
    for (int i = 0; i < nh; i++) {
        float f;
        bit_set(c, i, nearest(h[i], hc, 2, &f));
        if (f > 0.3f) {
            bad++;
        }
    }
    // a PWM has a fixed period (high + low) or a fixed gap; Manchester (highs of T and 2T too) has neither
    static int32_t per[RFD_MAX_DUR / 2 + 1], lo[RFD_MAX_DUR / 2 + 1];
    int np = 0;
    for (int i = s0; i + 1 < p->n; i += 2) {
        per[np] = p->d[i] - p->d[i + 1];
        lo[np++] = -p->d[i + 1];
    }
    float fixed = 1;
    if (np >= 4) {
        uint16_t pc[4], lcl[4];
        int kp = clusters(per, np, pc, 4), kl = clusters(lo, np, lcl, 4);
        int okp = 0, okl = 0;
        for (int i = 0; i < np; i++) {
            float f;
            nearest(per[i], pc, kp < 4 ? kp : 4, &f);
            if (kp == 1 || f < 0.15f) {
                okp++;
            }
            if (kl == 1) {
                okl++;
            }
        }
        float fp = kp == 1 ? 1.0f : (float)okp / np * 0.5f, fl = kl == 1 ? 1.0f : (float)okl / np;
        fixed = fp > fl ? fp : fl;
    }
    c->nbits = (uint16_t)nh;
    c->t_short = hc[0];
    c->t_long = hc[1];
    c->score = (1.0f - bad / nh) * fixed;
    return true;
}

static bool try_ppm(const rfd_packet_t *p, int s0, rfd_code_t *c)
{
    static int32_t h[RFD_MAX_DUR / 2 + 1], l[RFD_MAX_DUR / 2 + 1];
    int nh = 0, nl = 0;
    for (int i = s0; i < p->n; i++) {
        if (p->d[i] > 0) {
            h[nh++] = p->d[i];
        } else {
            l[nl++] = -p->d[i];
        }
    }
    uint16_t hc[4], lc[4];
    if (nl < 8 || clusters(h, nh, hc, 4) != 1 || clusters(l, nl, lc, 4) != 2 || lc[1] < lc[0] * 1.4f) {
        return false;
    }
    float bad = 0;
    for (int i = 0; i < nl; i++) {
        float f;
        bit_set(c, i, nearest(l[i], lc, 2, &f));
        if (f > 0.3f) {
            bad++;
        }
    }
    c->nbits = (uint16_t)nl;
    c->t_short = lc[0];
    c->t_long = lc[1];
    c->score = 1.0f - bad / nl;
    return true;
}

static bool try_manch(const rfd_packet_t *p, int s0, rfd_code_t *c)
{
    static int32_t all[RFD_MAX_DUR];
    int n = 0;
    for (int i = s0; i < p->n; i++) {
        all[n++] = iabs32(p->d[i]);
    }
    uint16_t cc[4];
    int k = clusters(all, n, cc, 4);
    if (n < 16 || k < 1 || k > 2) {
        return false;
    }
    float t = k == 2 ? (cc[0] + cc[1] / 2.0f) / 2.0f : cc[0];  // the half-bit time
    if (k == 2 && (cc[1] < cc[0] * 1.6f || cc[1] > cc[0] * 2.5f)) {
        return false;
    }
    // the half-bits of the levels
    static uint8_t hb[2 * RFD_MAX_DUR + 2];
    int nhb = 0;
    float bad = 0;
    for (int i = s0; i < p->n; i++) {
        float m = iabs32(p->d[i]) / t;
        int r = m < 1.5f ? 1 : 2;
        if (fabsf(m - r) > 0.35f) {
            bad++;
        }
        for (int j = 0; j < r; j++) {
            hb[nhb++] = p->d[i] > 0;
        }
    }
    hb[nhb++] = 0;  // (the gap after the packet: a low)
    // the pairs: (low, high) = 1, (high, low) = 0 (IEEE 802.3); the phase with the fewest wrong pairs
    int best_ph = 0, best_v = 1 << 30;
    for (int ph = 0; ph < 2; ph++) {
        int v = 0;
        for (int i = ph; i + 1 < nhb; i += 2) {
            if (hb[i] == hb[i + 1]) {
                v++;
            }
        }
        if (v < best_v) {
            best_v = v;
            best_ph = ph;
        }
    }
    int nb = 0;
    for (int i = best_ph; i + 1 < nhb && nb < RFD_MAX_BITS; i += 2) {
        bit_set(c, nb++, hb[i] == 0 && hb[i + 1] == 1);
    }
    if (nb < 8) {
        return false;
    }
    c->nbits = (uint16_t)nb;
    c->t_short = (uint16_t)(t + 0.5f);
    c->t_long = (uint16_t)(2 * t + 0.5f);
    c->score = 1.0f - (bad / n) - (float)best_v / nb;
    if (c->score < 0) {
        c->score = 0;
    }
    return true;
}

static bool try_nrz(const rfd_packet_t *p, int s0, rfd_code_t *c)
{
    static int32_t all[RFD_MAX_DUR];
    int n = 0;
    for (int i = s0; i < p->n; i++) {
        all[n++] = iabs32(p->d[i]);
    }
    uint16_t cc[4];
    int k = clusters(all, n, cc, 4);
    if (n < 8 || k < 1 || k > 4) {
        return false;
    }
    float t = cc[0];
    float bad = 0;
    int nb = 0;
    for (int i = s0; i < p->n && nb < RFD_MAX_BITS; i++) {
        float m = iabs32(p->d[i]) / t;
        int r = (int)(m + 0.5f);
        if (r < 1) {
            r = 1;
        }
        if (r > 8 || fabsf(m - r) > 0.3f) {
            bad++;
        }
        if (r > 8) {
            r = 8;
        }
        for (int j = 0; j < r && nb < RFD_MAX_BITS; j++) {
            bit_set(c, nb++, p->d[i] > 0);
        }
    }
    c->nbits = (uint16_t)nb;
    c->t_short = (uint16_t)(t + 0.5f);
    c->score = (1.0f - bad / n) * 0.9f;  // (NRZ fits almost any clean packet: the others win a tie)
    return true;
}

bool rfd_generic(const rfd_packet_t *p, int force, rfd_code_t *c)
{
    const int s0 = data_start(p);
    rfd_code_t t;
    bool ok = false;
    float best = -1;
    static const int ORDER[4] = {RFD_K_PWM, RFD_K_PPM, RFD_K_MANCH, RFD_K_NRZ};
    for (int i = 0; i < 4; i++) {
        int k = ORDER[i];
        if (force != RFD_K_NONE && force != k) {
            continue;
        }
        memset(&t, 0, sizeof(t));
        bool r = k == RFD_K_PWM     ? try_pwm(p, s0, &t)
                 : k == RFD_K_PPM   ? try_ppm(p, s0, &t)
                 : k == RFD_K_MANCH ? try_manch(p, s0, &t)
                                    : try_nrz(p, s0, &t);
        if (r && t.score > best + 0.02f) {
            best = t.score;
            *c = t;
            c->kind = (uint8_t)k;
            ok = true;
        }
    }
    if (ok) {
        c->t_gap = (uint32_t)p->gap_after;
    }
    return ok;
}

// A copy of the packet with each level shorter than thr joined to the level before it (the same rule as the slicer):
// the noise spikes in a low and the short dropouts in a high of a weak signal
static void deglitch(const rfd_packet_t *p, int32_t thr, rfd_packet_t *q)
{
    q->n = 0;
    q->gap_before = p->gap_before;
    q->gap_after = p->gap_after;
    q->t_us = p->t_us;
    for (int i = 0; i < p->n; i++) {
        int32_t v = p->d[i];
        if (q->n > 0 && ((v > 0) == (q->d[q->n - 1] > 0) || iabs32(v) < thr)) {
            q->d[q->n - 1] -= (v > 0) == (q->d[q->n - 1] > 0) ? -v : v;  // (the level before goes on, |v| more)
            continue;
        }
        q->d[q->n++] = v;
    }
    while (q->n > 0 && q->d[q->n - 1] < 0) {
        q->n--;  // (a packet ends with a high)
    }
}

// AUTO: a PWM code with the times of an RCSwitch protocol (a short and a long high of zh and oh units, a sync gap of sl
// units) is that protocol. Its lows were too bad for rfd_rcswitch (the PWM decoder reads only the highs), but the remote
// is the same: the name RCSWITCH Pn tells the user the protocol to send it again
static void pwm_as_rcswitch(rfd_code_t *c)
{
    for (int i = 0; i < 12; i++) {
        const rcp_t *r = &RCP[i];
        if (r->inv || r->zh >= r->oh || r->ol != r->zh || r->zl != r->oh) {
            continue;  // (the PWM shape: 1 = the long high)
        }
        // (the unit from both highs: a receiver makes the short high shorter, and the long one less so)
        const float t = (float)(c->t_short + c->t_long) / (r->zh + r->oh), ratio = (float)c->t_long / c->t_short;
        if (fabsf(ratio - (float)r->oh / r->zh) > 0.2f * r->oh / r->zh) {
            continue;
        }
        if (t < 0.6f * r->pulse || t > 1.7f * r->pulse) {
            continue;
        }
        if (!c->t_gap || fabsf((float)c->t_gap / t - r->sl) > 0.12f * r->sl) {
            continue;  // (the sync tells the protocol)
        }
        c->kind = RFD_K_RCSWITCH;
        c->proto = (uint8_t)(i + 1);
        c->t_short = (uint16_t)(t + 0.5f);
        c->t_long = 0;
        return;
    }
}

static bool decode_once(const rfd_packet_t *p, int mode, int coding, rfd_code_t *c)
{
    memset(c, 0, sizeof(*c));
    switch (mode) {
    case RFD_MODE_RCSWITCH:
        return rfd_rcswitch(p, c);
    case RFD_MODE_GENERIC:
        return rfd_generic(p, coding, c);
    default:
        if (rfd_rcswitch(p, c)) {
            return true;
        }
        if (!rfd_generic(p, coding, c)) {
            return false;
        }
        if (c->kind == RFD_K_PWM) {
            pwm_as_rcswitch(c);
        }
        return true;
    }
}

bool rfd_decode(const rfd_packet_t *p, int mode, int coding, rfd_code_t *c)
{
    if (mode == RFD_MODE_RAW) {
        memset(c, 0, sizeof(*c));
        rfd_raw(p, c);
        return true;
    }
    if (decode_once(p, mode, coding, c) && (c->kind == RFD_K_RCSWITCH || c->score >= 0.9f)) {
        return true;
    }
    // no code (or a weak fit): again with a larger glitch limit (a receiver at 3.3 V has more glitches)
    static rfd_packet_t q;
    static const int32_t THR[2] = {130, 200};
    rfd_code_t t;
    for (int k = 0; k < 2; k++) {
        deglitch(p, THR[k], &q);
        if (q.n >= 16 && decode_once(&q, mode, coding, &t) && (t.kind == RFD_K_RCSWITCH || t.score >= 0.9f)) {
            *c = t;
            return true;
        }
    }
    return decode_once(p, mode, coding, c);
}

// ================================================================ the text

const char *rfd_kind_name(int kind)
{
    static const char *const N[RFD_K_N] = {"-", "RCSWITCH", "PWM", "PPM", "MANCHESTER", "NRZ", "RAW"};
    return kind >= 0 && kind < RFD_K_N ? N[kind] : "?";
}

void rfd_hex(const rfd_code_t *c, char *out, int max)
{
    static const char H[] = "0123456789ABCDEF";
    int nd = (c->nbits + 3) / 4;
    if (nd + 3 > max) {
        nd = max - 3;
    }
    if (nd <= 0) {
        if (max > 0) {
            out[0] = 0;
        }
        return;
    }
    out[0] = '0';
    out[1] = 'x';
    // the value: the last bit is the bit 0 of the last digit
    for (int j = 0; j < nd; j++) {
        int v = 0;
        for (int b = 0; b < 4; b++) {
            int i = c->nbits - 1 - (4 * (nd - 1 - j) + b);
            if (i >= 0 && rfd_bit(c, i)) {
                v |= 1 << b;
            }
        }
        out[2 + j] = H[v];
    }
    out[2 + nd] = 0;
}

void rfd_dec(const rfd_code_t *c, char *out, int max)
{
    if (c->nbits > 64) {
        if (max > 0) {
            out[0] = 0;
        }
        return;
    }
    unsigned long long v = 0;
    for (int i = 0; i < c->nbits; i++) {
        v = (v << 1) | (unsigned)rfd_bit(c, i);
    }
    snprintf(out, (size_t)max, "%llu", v);
}

void rfd_binary(const rfd_code_t *c, char *out, int max)
{
    int o = 0;
    for (int i = 0; i < c->nbits && o < max - 2; i++) {
        if (i && !(i & 3) && o < max - 2) {
            out[o++] = ' ';
        }
        out[o++] = (char)('0' + rfd_bit(c, i));
    }
    out[o < max ? o : max - 1] = 0;
}

bool rfd_tristate(const rfd_code_t *c, char *out, int max)
{
    if (c->nbits & 1) {
        return false;
    }
    int o = 0;
    for (int i = 0; i + 1 < c->nbits && o < max - 1; i += 2) {
        int a = rfd_bit(c, i), b = rfd_bit(c, i + 1);
        if (a && !b) {
            return false;
        }
        out[o++] = a ? '1' : b ? 'F' : '0';
    }
    out[o] = 0;
    return true;
}

bool rfd_same(const rfd_code_t *a, const rfd_code_t *b)
{
    if (a->kind != b->kind || a->proto != b->proto || a->nbits != b->nbits) {
        return false;
    }
    for (int i = 0; i < a->nbits; i++) {
        if (rfd_bit(a, i) != rfd_bit(b, i)) {
            return false;
        }
    }
    return true;
}

// ================================================================ the encoders

static int put(int32_t *out, int *n, int max, int32_t v)
{
    if (*n >= max) {
        return 0;
    }
    out[(*n)++] = v;
    return 1;
}

int rfd_enc_rcswitch(int proto, uint64_t value, int nbits, int pulse_us, int32_t *out, int max)
{
    if (proto < 1 || proto > 12 || nbits < 1 || nbits > 64) {
        return 0;
    }
    const rcp_t *r = &RCP[proto - 1];
    const int s = r->inv ? -1 : 1, T = pulse_us > 0 ? pulse_us : r->pulse;
    int n = 0;
    for (int i = nbits - 1; i >= 0; i--) {
        bool one = (value >> i) & 1;
        put(out, &n, max, s * T * (one ? r->oh : r->zh));
        put(out, &n, max, -s * T * (one ? r->ol : r->zl));
    }
    put(out, &n, max, s * T * r->sh);
    put(out, &n, max, -s * T * r->sl);
    return n;
}

int rfd_enc_pwm(const uint8_t *bits, int nbits, int t_short, int t_long, int gap_us, int32_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < nbits; i++) {
        bool one = (bits[i >> 3] >> (7 - (i & 7))) & 1;
        put(out, &n, max, one ? t_long : t_short);
        put(out, &n, max, -(i == nbits - 1 ? gap_us : (one ? t_short : t_long)));
    }
    return n;
}

int rfd_enc_ppm(const uint8_t *bits, int nbits, int pulse, int g_short, int g_long, int gap_us, int32_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < nbits; i++) {
        bool one = (bits[i >> 3] >> (7 - (i & 7))) & 1;
        put(out, &n, max, pulse);
        put(out, &n, max, -(one ? g_long : g_short));
    }
    put(out, &n, max, pulse);
    put(out, &n, max, -gap_us);
    return n;
}

int rfd_enc_manch(const uint8_t *bits, int nbits, int t_half, int gap_us, int32_t *out, int max)
{
    // the half-bits: a 1 = (low, high), a 0 = (high, low); then the runs of one level as level times
    static uint8_t hb[2 * RFD_MAX_BITS + 2];
    int nhb = 0;
    hb[nhb++] = 1;  // (a start half-bit: the packet starts with a high)
    for (int i = 0; i < nbits && i < RFD_MAX_BITS; i++) {
        bool one = (bits[i >> 3] >> (7 - (i & 7))) & 1;
        hb[nhb++] = one ? 0 : 1;
        hb[nhb++] = one ? 1 : 0;
    }
    int n = 0;
    for (int i = 0; i < nhb;) {
        int j = i;
        while (j < nhb && hb[j] == hb[i]) {
            j++;
        }
        put(out, &n, max, (hb[i] ? 1 : -1) * t_half * (j - i));
        i = j;
    }
    if (n > 0 && out[n - 1] < 0) {
        out[n - 1] -= gap_us;
    } else {
        put(out, &n, max, -gap_us);
    }
    return n;
}
