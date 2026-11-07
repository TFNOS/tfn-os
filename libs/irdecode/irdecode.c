// IRDECODE: the slicer, the decoders and the encoders of infrared remote controls (irdecode.h). The timing of each
// protocol comes from the Arduino IRremote library (version 4: the *.hpp files of each protocol).
#include "irdecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A receiver module makes each mark longer and each space shorter (the MARK_EXCESS_MICROS of IRremote: 20 to 100 us).
// The decoders take this time off each mark and add it to each space.
#define EXCESS 50

static int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

// ================================================================ the slicer

void ird_slicer_init(ird_slicer_t *s, int min_pulse_us)
{
    memset(s, 0, sizeof(*s));
    s->min_pulse = min_pulse_us;
}

static void frame_end(ird_slicer_t *s, int64_t t_us)
{
    ird_frame_t *f = &s->cur;
    f->t_us = t_us;
    if (f->n >= 3) {  // (a spike alone is not a frame: an NEC repeat has 3 levels)
        if (f->n > 0 && f->d[f->n - 1] < 0) {
            f->n--;  // (the frame ends with a mark)
        }
        if (s->qn - s->qr < IRD_QUEUE) {
            s->q[(s->qn++) % IRD_QUEUE] = *f;
        } else {
            s->dropped++;
        }
    }
    f->n = 0;
}

// One level time that is final (it cannot join another one now)
static void level_done(ird_slicer_t *s, int32_t v, int64_t t_us)
{
    ird_frame_t *f = &s->cur;
    if (v < 0 && -v >= IRD_GAP_US) {  // a long space: the end of a frame, and the next mark starts one
        if (s->in && f->n > 0) {
            frame_end(s, t_us);
        }
        f->n = 0;
        f->gap_before = -v;
        s->in = true;
        return;
    }
    if (!s->in) {
        return;  // (the levels before the first long space)
    }
    if (f->n == 0 && v < 0) {
        return;  // (a frame starts with a mark)
    }
    if (v > 200000 || f->n >= IRD_MAX_DUR) {  // a mark that stays on, or a frame too long: no frame
        if (f->n >= IRD_MAX_DUR) {
            s->dropped++;
        }
        f->n = 0;
        s->in = false;
        return;
    }
    f->d[f->n++] = v;
}

void ird_slicer_feed(ird_slicer_t *s, const int32_t *d, int n, int64_t t_us)
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
        if (s->has_prev && iabs32(v) < s->min_pulse) {  // a glitch: the level before goes on (|v| more of it)
            s->prev -= v;
            continue;
        }
        if (s->has_prev) {
            level_done(s, s->prev, t_us);
        }
        s->prev = v;
        s->has_prev = true;
    }
    // a space that is already a gap ends the frame now (a source that gives a long space as more space values, for
    // example the demo, must not keep the frame until the next mark)
    if (s->has_prev && s->prev < 0 && -s->prev >= IRD_GAP_US) {
        level_done(s, s->prev, t_us);
        s->has_prev = false;
    }
}

void ird_slicer_idle(ird_slicer_t *s, int32_t quiet_us, int64_t t_us)
{
    // the line is a space with no edge: the level that waits (the last mark) is final, and the frame ends
    if (quiet_us < IRD_GAP_US) {
        return;
    }
    if (s->has_prev) {
        level_done(s, s->prev, t_us);
        s->has_prev = false;
    }
    if (s->in && s->cur.n > 0) {
        frame_end(s, t_us);
    }
}

bool ird_slicer_pop(ird_slicer_t *s, ird_frame_t *out)
{
    if (s->qr == s->qn) {
        return false;
    }
    *out = s->q[(s->qr++) % IRD_QUEUE];
    return true;
}

// ================================================================ the bits

int ird_bit(const ird_code_t *c, int i)
{
    if (i < 0 || i >= c->nbits) {
        return 0;
    }
    return (c->bits[i >> 3] >> (7 - (i & 7))) & 1;
}

static void set_bit(ird_code_t *c, int i, int v)
{
    if (i < 0 || i >= IRD_MAX_BITS) {
        return;
    }
    if (v) {
        c->bits[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    } else {
        c->bits[i >> 3] &= (uint8_t)~(0x80 >> (i & 7));
    }
}

// n bits from the bit "from": the first one on the air is the LSB (msb = false) or the MSB
static uint32_t field(const ird_code_t *c, int from, int n, bool msb)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        uint32_t b = (uint32_t)ird_bit(c, from + i);
        if (msb) {
            v = (v << 1) | b;
        } else {
            v |= b << i;
        }
    }
    return v;
}

static void put(ird_code_t *c, int from, int n, uint32_t v, bool msb)
{
    for (int i = 0; i < n; i++) {
        set_bit(c, from + i, msb ? (int)((v >> (n - 1 - i)) & 1) : (int)((v >> i) & 1));
    }
    if (from + n > c->nbits) {
        c->nbits = (uint16_t)(from + n);
    }
}

// ================================================================ the timing of each protocol
enum {
    CD_PD,
    CD_RC5,
    CD_RC6
};  // the coding: marks and spaces of two lengths, or a Manchester code
typedef struct {
    uint8_t coding;
    bool msb;
    bool stop;                // a stop mark after the last bit
    uint16_t hm, hs;          // the header (0: none)
    uint16_t om, os, zm, zs;  // a one and a zero: the mark and the space
    uint32_t carrier;
    uint32_t period;  // a key press with more frames: from the start of a frame to the start of the next one
} pt_t;
static const pt_t PT[IRD_P_N] = {
        [IRD_P_NEC] = {CD_PD, false, true, 9000, 4500, 560, 1690, 560, 560, 38000, 110000},
        [IRD_P_ONKYO] = {CD_PD, false, true, 9000, 4500, 560, 1690, 560, 560, 38000, 110000},
        [IRD_P_APPLE] = {CD_PD, false, true, 9000, 4500, 560, 1690, 560, 560, 38000, 110000},
        [IRD_P_SAMSUNG] = {CD_PD, false, true, 4480, 4480, 560, 1680, 560, 560, 38000, 110000},
        [IRD_P_SAMSUNG48] = {CD_PD, false, true, 4480, 4480, 560, 1680, 560, 560, 38000, 110000},
        [IRD_P_LG] = {CD_PD, true, true, 9000, 4200, 500, 1500, 500, 550, 38000, 110000},
        [IRD_P_SONY] = {CD_PD, false, false, 2400, 600, 1200, 600, 600, 600, 40000, 45000},
        [IRD_P_RC5] = {CD_RC5, true, false, 0, 0, 889, 889, 889, 889, 36000, 114000},
        [IRD_P_RC6] = {CD_RC6, true, false, 2666, 889, 444, 444, 444, 444, 36000, 107000},
        [IRD_P_PANASONIC] = {CD_PD, false, true, 3456, 1728, 432, 1296, 432, 432, 37000, 130000},
        [IRD_P_KASEIKYO] = {CD_PD, false, true, 3456, 1728, 432, 1296, 432, 432, 37000, 130000},
        [IRD_P_DENON_K] = {CD_PD, false, true, 3456, 1728, 432, 1296, 432, 432, 37000, 130000},
        [IRD_P_SHARP_K] = {CD_PD, false, true, 3456, 1728, 432, 1296, 432, 432, 37000, 130000},
        [IRD_P_JVC_K] = {CD_PD, false, true, 3456, 1728, 432, 1296, 432, 432, 37000, 130000},
        [IRD_P_MITSUBISHI_K] = {CD_PD, false, true, 3456, 1728, 432, 1296, 432, 432, 37000, 130000},
        [IRD_P_JVC] = {CD_PD, false, true, 8400, 4200, 525, 1575, 525, 525, 38000, 60000},
        [IRD_P_DENON] = {CD_PD, true, true, 0, 0, 260, 1820, 260, 780, 38000, 65000},
        [IRD_P_SHARP] = {CD_PD, true, true, 0, 0, 260, 1820, 260, 780, 38000, 65000},
        [IRD_P_BOSEWAVE] = {CD_PD, false, true, 1060, 1425, 534, 1468, 534, 468, 38000, 60000},
        [IRD_P_WHYNTER] = {CD_PD, true, true, 2850, 2850, 750, 2150, 750, 750, 38000, 110000},
};

static const char *const NAMES[IRD_P_N] = {
        "UNKNOWN",
        "NEC",
        "ONKYO",
        "APPLE",
        "SAMSUNG",
        "SAMSUNG48",
        "LG",
        "SONY",
        "RC5",
        "RC6",
        "PANASONIC",
        "KASEIKYO",
        "DENON KASEIKYO",
        "SHARP KASEIKYO",
        "JVC KASEIKYO",
        "MITSUBISHI KASEIKYO",
        "JVC",
        "DENON",
        "SHARP",
        "BOSEWAVE",
        "WHYNTER",
        "PULSE DISTANCE",
        "PULSE WIDTH",
};

const char *ird_name(int proto)
{
    return proto >= 0 && proto < IRD_P_N ? NAMES[proto] : "?";
}

bool ird_has_fields(int proto)
{
    return proto > IRD_P_UNKNOWN && proto < IRD_P_PULSE_DISTANCE;
}

static void set_timing(ird_code_t *c)
{
    const pt_t *t = &PT[c->proto];
    c->hdr_mark = t->hm;
    c->hdr_space = t->hs;
    c->one_mark = t->om;
    c->one_space = t->os;
    c->zero_mark = t->zm;
    c->zero_space = t->zs;
    c->carrier_hz = t->carrier;
    if (t->msb) {
        c->flags |= IRD_F_MSB;
    }
}

// ================================================================ the decoders

typedef struct {
    const int32_t *d;
    int n;
} fr_t;

// the mark at i (the receiver excess off), or -1; the space at i (the excess on), the gap after the frame, or -1
static int32_t M(const fr_t *f, int i)
{
    return i >= 0 && i < f->n && f->d[i] > 0 ? f->d[i] - EXCESS : -1;
}
static int32_t S(const fr_t *f, int i)
{
    if (i >= f->n) {
        return 1000000;
    }
    return i >= 0 && f->d[i] < 0 ? -f->d[i] + EXCESS : -1;
}
static bool near(int32_t v, int nom)
{
    const int tol = nom / 4 + 60;  // (IRremote: 25 %; and 60 us for the short times of a weak receiver)
    return v >= nom - tol && v <= nom + tol;
}

// The bits from the level i, as the timing of t (CD_PD): the mark and the space of each bit; the last bit with no
// space when there is no stop mark (SONY: the gap after it)
static bool pd_bits(const fr_t *f, int i, int nbits, const pt_t *t, ird_code_t *c)
{
    for (int b = 0; b < nbits; b++, i += 2) {
        int32_t m = M(f, i), s = S(f, i + 1);
        bool last = b + 1 == nbits && !t->stop;
        int v;
        if (t->om != t->zm) {  // the marks tell the bit (pulse width)
            if (near(m, t->om) && !near(m, t->zm)) {
                v = 1;
            } else if (near(m, t->zm)) {
                v = 0;
            } else {
                return false;
            }
            if (!last && !near(s, v ? t->os : t->zs)) {
                return false;
            }
        } else {  // the spaces tell the bit (pulse distance)
            if (!near(m, t->om)) {
                return false;
            }
            if (near(s, t->os) && !near(s, t->zs)) {
                v = 1;
            } else if (near(s, t->zs)) {
                v = 0;
            } else {
                return false;
            }
        }
        set_bit(c, b, v);
    }
    if (t->stop && !near(M(f, i), t->om)) {
        return false;
    }
    c->nbits = (uint16_t)nbits;
    return true;
}

static bool header(const fr_t *f, const pt_t *t)
{
    return near(M(f, 0), t->hm) && near(S(f, 1), t->hs);
}

// a frame of a CD_PD protocol with a header: the header, nbits bits (n = the count of the level times)
static bool pd_frame(const fr_t *f, int proto, int nbits, ird_code_t *c)
{
    const pt_t *t = &PT[proto];
    const int n = (t->hm ? 2 : 0) + 2 * nbits + (t->stop ? 1 : -1);
    if (f->n != n || (t->hm && !header(f, t))) {
        return false;
    }
    if (!pd_bits(f, t->hm ? 2 : 0, nbits, t, c)) {
        return false;
    }
    c->proto = (uint8_t)proto;
    set_timing(c);
    return true;
}

static bool dec_nec(const fr_t *f, ird_code_t *c)
{
    // the repeat frame of a held key: 9 ms, 2.25 ms, a mark (LG sends the same)
    if (f->n == 3 && near(M(f, 0), 9000) && near(S(f, 1), 2250) && near(M(f, 2), 560)) {
        c->proto = IRD_P_NEC;
        c->flags |= IRD_F_REPEAT;
        set_timing(c);
        c->hdr_space = 2250;
        return true;
    }
    if (!pd_frame(f, IRD_P_NEC, 32, c)) {
        return false;
    }
    const uint32_t a0 = field(c, 0, 8, false), a1 = field(c, 8, 8, false);
    const uint32_t c0 = field(c, 16, 8, false), c1 = field(c, 24, 8, false), a16 = field(c, 0, 16, false);
    if (a16 == 0x87EE) {  // APPLE: the Apple id in the address, then the command and the device id
        c->proto = IRD_P_APPLE;
        c->command = c0;
        c->address = c1;
    } else if ((c0 ^ c1) != 0xFF) {  // ONKYO: a 16-bit command with no inverted copy
        c->proto = IRD_P_ONKYO;
        c->address = a16;
        c->command = field(c, 16, 16, false);
    } else {
        c->address = (a0 ^ a1) == 0xFF ? a0 : a16;  // (the extended NEC: a 16-bit address)
        c->command = c0;
    }
    return true;
}

static bool dec_samsung(const fr_t *f, ird_code_t *c)
{
    const pt_t *t = &PT[IRD_P_SAMSUNG];
    if (f->n == 5 && header(f, t) && near(M(f, 2), 560) && near(S(f, 3), 1680) && near(M(f, 4), 560)) {
        c->proto = IRD_P_SAMSUNG;  // the repeat frame: the header, one 1 bit, the stop mark
        c->flags |= IRD_F_REPEAT;
        set_timing(c);
        return true;
    }
    if (pd_frame(f, IRD_P_SAMSUNG48, 48, c)) {
        c->address = field(c, 0, 16, false);
        c->command = field(c, 16, 32, false);
        return true;
    }
    if (!pd_frame(f, IRD_P_SAMSUNG, 32, c)) {
        return false;
    }
    c->address = field(c, 0, 16, false);
    const uint32_t c0 = field(c, 16, 8, false), c1 = field(c, 24, 8, false);
    c->command = (c0 ^ c1) == 0xFF ? c0 : field(c, 16, 16, false);
    return true;
}

static bool dec_lg(const fr_t *f, ird_code_t *c)
{
    if (!pd_frame(f, IRD_P_LG, 28, c)) {
        return false;
    }
    c->address = field(c, 0, 8, true);
    c->command = field(c, 8, 16, true);
    uint32_t sum = 0;
    for (int k = 0; k < 4; k++) {
        sum += (c->command >> (4 * k)) & 0xF;
    }
    if ((sum & 0xF) != field(c, 24, 4, true)) {
        c->flags |= IRD_F_PARITY;
    }
    return true;
}

static bool dec_sony(const fr_t *f, ird_code_t *c)
{
    static const int NB[3] = {12, 15, 20};
    for (int k = 0; k < 3; k++) {
        if (pd_frame(f, IRD_P_SONY, NB[k], c)) {
            c->command = field(c, 0, 7, false);
            c->address = field(c, 7, NB[k] - 7, false);
            return true;
        }
    }
    return false;
}

static bool dec_jvc(const fr_t *f, ird_code_t *c)
{
    bool rep = false;
    if (!pd_frame(f, IRD_P_JVC, 16, c)) {
        // a repeat: the same frame with no header
        const pt_t *t = &PT[IRD_P_JVC];
        if (f->n != 33 || !pd_bits(f, 0, 16, t, c)) {
            return false;
        }
        c->proto = IRD_P_JVC;
        set_timing(c);
        rep = true;
    }
    c->address = field(c, 0, 8, false);
    c->command = field(c, 8, 8, false);
    if (rep) {
        c->flags |= IRD_F_REPEAT;
    }
    return true;
}

static bool dec_denon(const fr_t *f, ird_code_t *c)
{
    if (!pd_frame(f, IRD_P_DENON, 15, c)) {
        return false;
    }
    c->address = field(c, 0, 5, true);
    c->command = field(c, 5, 8, true);
    c->extra = field(c, 13, 2, true);
    // the frame bits: DENON 00 (the second frame: 11, the inverted command), SHARP 10 (01)
    if (c->extra == 2 || c->extra == 1) {
        c->proto = IRD_P_SHARP;
    }
    if (c->extra == 3 || c->extra == 1) {
        c->flags |= IRD_F_REPEAT;
        c->command = ~c->command & 0xFF;
    }
    return true;
}

static bool dec_bose(const fr_t *f, ird_code_t *c)
{
    if (!pd_frame(f, IRD_P_BOSEWAVE, 16, c)) {
        return false;
    }
    c->command = field(c, 0, 8, false);
    if ((c->command ^ field(c, 8, 8, false)) != 0xFF) {
        c->flags |= IRD_F_PARITY;
    }
    return true;
}

static bool dec_whynter(const fr_t *f, ird_code_t *c)
{
    // a mark and a space of 750 us, then the header and 32 bits
    if (f->n != 69 || !near(M(f, 0), 750) || !near(S(f, 1), 750)) {
        return false;
    }
    fr_t g = {f->d + 2, f->n - 2};
    if (!pd_frame(&g, IRD_P_WHYNTER, 32, c)) {
        return false;
    }
    c->command = field(c, 0, 32, true);
    return true;
}

static int kaseikyo_proto(uint32_t vendor)
{
    switch (vendor) {
    case 0x2002:
        return IRD_P_PANASONIC;
    case 0x3254:
        return IRD_P_DENON_K;
    case 0x5AAA:
        return IRD_P_SHARP_K;
    case 0x0103:
        return IRD_P_JVC_K;
    case 0xCB23:
        return IRD_P_MITSUBISHI_K;
    default:
        return IRD_P_KASEIKYO;
    }
}

static uint32_t vendor_parity(uint32_t v)
{
    v ^= v >> 8;
    return (v ^ (v >> 4)) & 0xF;
}

static bool dec_kaseikyo(const fr_t *f, ird_code_t *c)
{
    if (!pd_frame(f, IRD_P_KASEIKYO, 48, c)) {
        return false;
    }
    const uint32_t vendor = field(c, 0, 16, false);
    c->proto = (uint8_t)kaseikyo_proto(vendor);
    c->extra = vendor;
    c->address = field(c, 20, 12, false);
    c->command = field(c, 32, 8, false);
    const uint32_t par = field(c, 16, 8, false) ^ field(c, 24, 8, false) ^ field(c, 32, 8, false);
    if (field(c, 16, 4, false) != vendor_parity(vendor) || par != field(c, 40, 8, false)) {
        c->flags |= IRD_F_PARITY;
    }
    return true;
}

// The levels of a Manchester frame in half-bit units (1 = a mark): each level time is k units (1..kmax)
static int units(const fr_t *f, int i0, int t, int kmax, uint8_t *u, int max)
{
    int nu = 0;
    for (int i = i0; i < f->n; i++) {
        const int32_t v = f->d[i] > 0 ? f->d[i] - EXCESS : -f->d[i] + EXCESS;
        const int k = (int)((v + t / 2) / t);
        if (k < 1 || k > kmax || iabs32(v - k * t) > t * 2 / 5) {
            return -1;
        }
        for (int j = 0; j < k; j++) {
            if (nu >= max) {
                return -1;
            }
            u[nu++] = f->d[i] > 0;
        }
    }
    return nu;
}

static bool dec_rc5(const fr_t *f, ird_code_t *c)
{
    // 14 bits of 2 x 889 us: a 1 = a space, then a mark; the first half of the first bit is the space before the frame
    uint8_t u[64];
    u[0] = 0;
    int nu = units(f, 0, 889, 2, u + 1, 60);
    if (nu < 0) {
        return false;
    }
    nu++;
    if (nu & 1) {
        u[nu++] = 0;  // (a last 0 bit: its space goes into the gap)
    }
    if (nu != 28) {
        return false;
    }
    for (int b = 0; b < 14; b++) {
        if (u[2 * b] == u[2 * b + 1]) {
            return false;
        }
        set_bit(c, b, u[2 * b + 1]);
    }
    c->nbits = 14;
    if (!ird_bit(c, 0)) {
        return false;
    }
    c->proto = IRD_P_RC5;
    set_timing(c);
    c->address = field(c, 3, 5, true);
    c->command = field(c, 8, 6, true) | (ird_bit(c, 1) ? 0 : 0x40);  // (RC5X: the second bit is the inverted 7th)
    if (ird_bit(c, 2)) {
        c->flags |= IRD_F_TOGGLE;
    }
    return true;
}

static bool dec_rc6(const fr_t *f, ird_code_t *c)
{
    // the header 2666 / 889; then a 1 = a mark, then a space, of 444 us each; the 5th bit (the toggle) is 2 x 889 us
    if (!near(M(f, 0), 2666) || !near(S(f, 1), 889)) {
        return false;
    }
    uint8_t u[IRD_MAX_BITS * 2 + 8];
    int nu = units(f, 2, 444, 6, u, IRD_MAX_BITS * 2);
    if (nu < 0) {
        return false;
    }
    const int real = nu;
    for (int k = 0; k < 4; k++) {
        u[nu++] = 0;  // (the last half of a 1 bit: in the gap)
    }
    int pos = 0, b = 0;
    while (pos < real && b < IRD_MAX_BITS) {
        const int sz = b == 4 ? 2 : 1;
        for (int j = 1; j < sz; j++) {
            if (u[pos + j] != u[pos] || u[pos + sz + j] != u[pos + sz]) {
                return false;
            }
        }
        if (u[pos] == u[pos + sz]) {
            return false;
        }
        set_bit(c, b++, u[pos]);
        pos += 2 * sz;
    }
    c->nbits = (uint16_t)b;
    if (b < 5 + 8 || !ird_bit(c, 0)) {
        return false;
    }
    c->proto = IRD_P_RC6;
    set_timing(c);
    const int nd = b - 5;
    if (ird_bit(c, 4)) {
        c->flags |= IRD_F_TOGGLE;
    }
    c->extra = field(c, 1, 3, true);  // the mode
    c->command = field(c, 5 + nd - 8, 8, true);
    c->address = field(c, 5, nd - 8 > 24 ? 24 : nd - 8, true);
    return true;
}

// ---------------------------------------------------------------- the universal decoder
// The lengths of a set of times: one cluster, or two (short and long, with a clear gap between them)
typedef struct {
    int n;           // 1 or 2 (0: no fit)
    int32_t thr;     // two: the border
    int32_t lo, hi;  // the mean of each cluster
} cl_t;

static cl_t cluster(const int32_t *v, int n)
{
    cl_t r = {0, 0, 0, 0};
    if (n <= 0) {
        return r;
    }
    int32_t mn = v[0], mx = v[0];
    for (int i = 1; i < n; i++) {
        if (v[i] < mn) {
            mn = v[i];
        }
        if (v[i] > mx) {
            mx = v[i];
        }
    }
    if (mx <= mn + mn / 2 + 80) {  // one length
        int64_t s = 0;
        for (int i = 0; i < n; i++) {
            s += v[i];
        }
        r.n = 1;
        r.lo = r.hi = (int32_t)(s / n);
        return r;
    }
    const int32_t thr = (mn + mx) / 2;
    int64_t s0 = 0, s1 = 0;
    int n0 = 0, n1 = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] < thr) {
            if (v[i] > mn + mn / 2 + 80) {
                return r;  // (a third length: no fit)
            }
            s0 += v[i];
            n0++;
        } else {
            if (v[i] < mx - mx / 3 - 80) {
                return r;
            }
            s1 += v[i];
            n1++;
        }
    }
    r.n = 2;
    r.thr = thr;
    r.lo = (int32_t)(s0 / n0);
    r.hi = (int32_t)(s1 / n1);
    return r;
}

static bool dec_universal(const fr_t *f, ird_code_t *c)
{
    if (f->n < 15) {
        return false;  // (8 bits at least)
    }
    static int32_t mk[IRD_MAX_DUR / 2 + 1], sp[IRD_MAX_DUR / 2 + 1];
    // the header: a first mark (or space) much longer than all the data marks (or spaces)
    int32_t mmax = 0, smax = 0;
    for (int i = 2; i < f->n; i++) {
        int32_t v = iabs32(f->d[i]);
        if (f->d[i] > 0 && v > mmax) {
            mmax = v;
        }
        if (f->d[i] < 0 && v > smax) {
            smax = v;
        }
    }
    const bool hdr = f->d[0] > mmax * 3 / 2 || -f->d[1] > smax * 3 / 2;
    const int i0 = hdr ? 2 : 0;
    int nm = 0, ns = 0;
    for (int i = i0; i < f->n; i++) {
        if (f->d[i] > 0) {
            mk[nm++] = f->d[i] - EXCESS;
        } else {
            sp[ns++] = -f->d[i] + EXCESS;
        }
    }
    const cl_t cm = cluster(mk, nm), cs = cluster(sp, ns);
    if (!cm.n || !cs.n || (cm.n == 1 && cs.n == 1)) {
        return false;
    }
    memset(c->bits, 0, sizeof(c->bits));
    int nb;
    if (cm.n == 1) {  // PULSE DISTANCE: each mark and the space after it; the last mark = the stop
        nb = ns;
        if (nb > IRD_MAX_BITS) {
            return false;
        }
        for (int b = 0; b < nb; b++) {
            set_bit(c, b, sp[b] >= cs.thr);
        }
        c->proto = IRD_P_PULSE_DISTANCE;
        c->flags |= IRD_F_STOP;
        c->one_mark = c->zero_mark = (uint16_t)cm.lo;
        c->one_space = (uint16_t)cs.hi;
        c->zero_space = (uint16_t)cs.lo;
    } else {  // PULSE WIDTH: each mark (a long mark = 1); the spaces: one or two lengths
        nb = nm;
        if (nb > IRD_MAX_BITS) {
            return false;
        }
        int64_t s1 = 0, s0 = 0;
        int n1 = 0, n0 = 0;
        for (int b = 0; b < nb; b++) {
            const int v = mk[b] >= cm.thr;
            set_bit(c, b, v);
            if (b < ns) {
                if (cs.n == 2 && (sp[b] >= cs.thr) == v) {
                    return false;  // (two space lengths: a long mark, a short space)
                }
                if (v) {
                    s1 += sp[b], n1++;
                } else {
                    s0 += sp[b], n0++;
                }
            }
        }
        c->proto = IRD_P_PULSE_WIDTH;
        c->one_mark = (uint16_t)cm.hi;
        c->zero_mark = (uint16_t)cm.lo;
        c->one_space = (uint16_t)(n1 ? s1 / n1 : cs.lo);
        c->zero_space = (uint16_t)(n0 ? s0 / n0 : cs.lo);
    }
    if (nb < 8) {
        return false;
    }
    c->nbits = (uint16_t)nb;
    if (hdr) {
        c->flags |= IRD_F_HEADER;
        c->hdr_mark = (uint16_t)(f->d[0] - EXCESS > 65535 ? 65535 : f->d[0] - EXCESS);
        c->hdr_space = (uint16_t)(-f->d[1] + EXCESS > 65535 ? 65535 : -f->d[1] + EXCESS);
    }
    c->carrier_hz = 38000;
    return true;
}

uint32_t ird_hash(const int32_t *d, int n)
{
    uint32_t h = 2166136261u;  // (IRremote decodeHash: FNV-1, 32 bits)
    for (int i = 0; i + 2 < n; i++) {
        const int64_t a = iabs32(d[i]), b = iabs32(d[i + 2]);
        const uint32_t v = b * 10 < a * 8 ? 0 : a * 10 < b * 8 ? 2 : 1;
        h = (h * 16777619u) ^ v;
    }
    return h;
}

bool ird_decode(const int32_t *d, int n, bool raw_only, ird_code_t *c)
{
    memset(c, 0, sizeof(*c));
    if (n < 1) {
        return false;
    }
    c->nraw = (uint16_t)n;
    c->hash = ird_hash(d, n);
    if (n < 3) {
        return false;
    }
    if (!raw_only) {
        const fr_t f = {d, n};
        typedef bool (*dec_fn)(const fr_t *, ird_code_t *);
        static const dec_fn DEC[] = {dec_nec,      dec_samsung, dec_lg,    dec_sony, dec_rc5,     dec_rc6,
                                     dec_kaseikyo, dec_jvc,     dec_denon, dec_bose, dec_whynter, dec_universal};
        for (unsigned k = 0; k < sizeof(DEC) / sizeof(DEC[0]); k++) {
            const uint32_t h = c->hash;
            memset(c, 0, sizeof(*c));
            c->nraw = (uint16_t)n;
            c->hash = h;
            if (DEC[k](&f, c)) {
                return true;
            }
        }
        const uint32_t h = c->hash;
        memset(c, 0, sizeof(*c));
        c->nraw = (uint16_t)n;
        c->hash = h;
    }
    c->proto = IRD_P_UNKNOWN;
    c->carrier_hz = 38000;
    return true;
}

// ================================================================ the text

void ird_raw_hex(const ird_code_t *c, char *out, int max)
{
    if (max <= 0) {
        return;
    }
    out[0] = 0;
    if (!c->nbits) {
        return;
    }
    const bool msb = c->flags & IRD_F_MSB;
    int o = 0;
    for (int w = 0; w * 64 < c->nbits && o < max - 1; w++) {
        const int nb = c->nbits - w * 64 > 64 ? 64 : c->nbits - w * 64;
        uint64_t v = 0;
        for (int i = 0; i < nb; i++) {
            uint64_t b = (uint64_t)ird_bit(c, w * 64 + i);
            if (msb) {
                v = (v << 1) | b;
            } else {
                v |= b << i;
            }
        }
        const int nd = (nb + 3) / 4;
        o += snprintf(out + o, (size_t)(max - o), "%s0x%0*llX", w ? " " : "", nd, (unsigned long long)v);
    }
}

void ird_binary(const ird_code_t *c, char *out, int max)
{
    int o = 0;
    for (int i = 0; i < c->nbits && o < max - 2; i++) {
        if (i && !(i & 7)) {
            out[o++] = ' ';
        }
        out[o++] = (char)('0' + ird_bit(c, i));
    }
    if (max > 0) {
        out[o < max ? o : max - 1] = 0;
    }
}

bool ird_same(const ird_code_t *a, const ird_code_t *b)
{
    if (a->proto != b->proto) {
        return false;
    }
    if ((a->flags & IRD_F_REPEAT) || (b->flags & IRD_F_REPEAT)) {
        return false;
    }
    if (a->proto == IRD_P_UNKNOWN) {
        return a->hash == b->hash && a->nraw == b->nraw;
    }
    if (ird_has_fields(a->proto)) {
        if (a->address != b->address || a->command != b->command) {
            return false;
        }
        if (a->proto != IRD_P_RC5 && a->proto != IRD_P_RC6) {
            return a->nbits == b->nbits;
        }
        return a->nbits == b->nbits;  // (RC5 / RC6: the toggle bit does not count)
    }
    if (a->nbits != b->nbits) {
        return false;
    }
    for (int i = 0; i < a->nbits; i++) {
        if (ird_bit(a, i) != ird_bit(b, i)) {
            return false;
        }
    }
    return true;
}

// ================================================================ the encoders

typedef struct {
    int32_t *out;
    int n, max;
    bool full;
} em_t;

// one more level time: the same level as the last one joins it
static void emit(em_t *e, int32_t v)
{
    if (!v) {
        return;
    }
    if (e->n > 0 && (e->out[e->n - 1] > 0) == (v > 0)) {
        e->out[e->n - 1] += v;
        return;
    }
    if (e->n >= e->max) {
        e->full = true;
        return;
    }
    e->out[e->n++] = v;
}

static void enc_pd(const ird_code_t *c, const pt_t *t, em_t *e)
{
    if (c->proto == IRD_P_WHYNTER) {
        emit(e, 750);
        emit(e, -750);
    }
    if (t->hm) {
        emit(e, t->hm);
        emit(e, -(int32_t)t->hs);
    }
    for (int b = 0; b < c->nbits; b++) {
        const int v = ird_bit(c, b);
        emit(e, v ? t->om : t->zm);
        if (b + 1 < c->nbits || t->stop) {
            emit(e, -(int32_t)(v ? t->os : t->zs));
        }
    }
    if (t->stop) {
        emit(e, t->om);
    }
}

static void enc_manch(const ird_code_t *c, bool rc6, em_t *e)
{
    const int32_t T = rc6 ? 444 : 889;
    if (rc6) {
        emit(e, 2666);
        emit(e, -889);
    }
    for (int b = 0; b < c->nbits; b++) {
        const int v = ird_bit(c, b);
        const int32_t h = rc6 && b == 4 ? 2 * T : T;
        // RC5: a 1 = a space, then a mark; RC6: a 1 = a mark, then a space
        const bool mark_first = rc6 ? v : !v;
        if (b == 0 && !rc6) {
            emit(e, T);  // (RC5: the space before the first mark is the gap before the frame)
            continue;
        }
        emit(e, mark_first ? h : -h);
        emit(e, mark_first ? -h : h);
    }
    if (e->n > 0 && e->out[e->n - 1] < 0) {
        e->n--;  // (the frame ends with a mark)
    }
}

static void enc_universal(const ird_code_t *c, em_t *e)
{
    if (c->flags & IRD_F_HEADER) {
        emit(e, c->hdr_mark);
        emit(e, -(int32_t)c->hdr_space);
    }
    const bool stop = c->proto == IRD_P_PULSE_DISTANCE;
    for (int b = 0; b < c->nbits; b++) {
        const int v = ird_bit(c, b);
        emit(e, v ? c->one_mark : c->zero_mark);
        if (b + 1 < c->nbits || stop) {
            emit(e, -(int32_t)(v ? c->one_space : c->zero_space));
        }
    }
    if (stop) {
        emit(e, c->one_mark);
    }
}

static void enc_frame(const ird_code_t *c, em_t *e)
{
    if (c->proto == IRD_P_UNKNOWN || c->proto >= IRD_P_N) {
        return;
    }
    if (c->proto >= IRD_P_PULSE_DISTANCE) {
        enc_universal(c, e);
        return;
    }
    const pt_t *t = &PT[c->proto];
    if (c->flags & IRD_F_REPEAT) {  // the repeat frames (a held key)
        if (c->proto == IRD_P_NEC || c->proto == IRD_P_ONKYO || c->proto == IRD_P_APPLE || c->proto == IRD_P_LG) {
            emit(e, 9000);
            emit(e, -2250);
            emit(e, t->om);
            return;
        }
        if (c->proto == IRD_P_SAMSUNG || c->proto == IRD_P_SAMSUNG48) {
            emit(e, t->hm);
            emit(e, -(int32_t)t->hs);
            emit(e, t->om);
            emit(e, -(int32_t)t->os);
            emit(e, t->om);
            return;
        }
        if (c->proto == IRD_P_JVC) {
            ird_code_t k = *c;
            k.flags &= (uint8_t)~IRD_F_REPEAT;
            pt_t nh = *t;
            nh.hm = nh.hs = 0;
            enc_pd(&k, &nh, e);
            return;
        }
    }
    if (t->coding == CD_RC5) {
        enc_manch(c, false, e);
    } else if (t->coding == CD_RC6) {
        enc_manch(c, true, e);
    } else {
        enc_pd(c, t, e);
    }
}

int ird_encode(const ird_code_t *c, int32_t *out, int max)
{
    em_t e = {out, 0, max, false};
    enc_frame(c, &e);
    return e.full ? 0 : e.n;
}

int ird_encode_press(const ird_code_t *c, int32_t *out, int max)
{
    em_t e = {out, 0, max, false};
    if (c->proto == IRD_P_UNKNOWN || c->proto >= IRD_P_N) {
        return 0;
    }
    ird_code_t k = *c;
    if (k.proto != IRD_P_JVC) {
        k.flags &= (uint8_t)~IRD_F_REPEAT;
    }
    const pt_t *t = &PT[k.proto < IRD_P_PULSE_DISTANCE ? k.proto : IRD_P_NEC];
    int frames = 1;
    if (k.proto == IRD_P_SONY) {
        frames = 3;
    }
    if (k.proto == IRD_P_DENON || k.proto == IRD_P_SHARP) {
        frames = 2;
    }
    for (int f = 0; f < frames; f++) {
        const int start = e.n;
        int64_t len = 0;
        if (f == 1 && (k.proto == IRD_P_DENON || k.proto == IRD_P_SHARP)) {
            // the second frame: the inverted command and the inverted frame bits
            for (int b = 5; b < 15; b++) {
                set_bit(&k, b, !ird_bit(&k, b));
            }
        }
        enc_frame(&k, &e);
        for (int i = start; i < e.n; i++) {
            len += iabs32(e.out[i]);
        }
        if (f + 1 < frames) {
            int64_t gap = (int64_t)t->period - len;
            if (gap < IRD_GAP_US + 2000) {
                gap = IRD_GAP_US + 2000;
            }
            emit(&e, -(int32_t)gap);
        }
    }
    return e.full ? 0 : e.n;
}

void ird_make(int proto, uint32_t address, uint32_t command, uint32_t extra, int nbits, ird_code_t *c)
{
    memset(c, 0, sizeof(*c));
    if (proto <= IRD_P_UNKNOWN || proto >= IRD_P_PULSE_DISTANCE) {
        return;
    }
    c->proto = (uint8_t)proto;
    c->address = address;
    c->command = command;
    switch (proto) {
    case IRD_P_NEC:
        if (address > 0xFF) {
            put(c, 0, 16, address, false);
        } else {
            put(c, 0, 8, address, false), put(c, 8, 8, ~address & 0xFF, false);
        }
        put(c, 16, 8, command & 0xFF, false);
        put(c, 24, 8, ~command & 0xFF, false);
        break;
    case IRD_P_ONKYO:
        put(c, 0, 16, address, false);
        put(c, 16, 16, command, false);
        break;
    case IRD_P_APPLE:
        put(c, 0, 16, 0x87EE, false);
        put(c, 16, 8, command, false);
        put(c, 24, 8, address, false);
        break;
    case IRD_P_SAMSUNG:
        put(c, 0, 16, address > 0xFF ? address : address | (address << 8), false);
        if (command > 0xFF) {
            put(c, 16, 16, command, false);
        } else {
            put(c, 16, 8, command, false), put(c, 24, 8, ~command & 0xFF, false);
        }
        if (address <= 0xFF) {
            c->address = address | (address << 8);
        }
        break;
    case IRD_P_SAMSUNG48:
        put(c, 0, 16, address, false);
        put(c, 16, 32, command, false);
        break;
    case IRD_P_LG: {
        uint32_t sum = 0;
        for (int k = 0; k < 4; k++) {
            sum += (command >> (4 * k)) & 0xF;
        }
        put(c, 0, 8, address, true);
        put(c, 8, 16, command, true);
        put(c, 24, 4, sum & 0xF, true);
        break;
    }
    case IRD_P_SONY:
        if (nbits != 15 && nbits != 20) {
            nbits = 12;
        }
        put(c, 0, 7, command, false);
        put(c, 7, nbits - 7, address, false);
        break;
    case IRD_P_RC5:
        put(c, 0, 1, 1, true);
        put(c, 1, 1, command & 0x40 ? 0 : 1, true);
        put(c, 2, 1, extra & 1, true);
        put(c, 3, 5, address, true);
        put(c, 8, 6, command & 0x3F, true);
        if (extra & 1) {
            c->flags |= IRD_F_TOGGLE;
        }
        break;
    case IRD_P_RC6: {
        if (nbits < 16) {
            nbits = 16;
        }
        const int nd = nbits;
        put(c, 0, 1, 1, true);
        put(c, 1, 3, 0, true);  // mode 0
        put(c, 4, 1, extra & 1, true);
        put(c, 5, nd - 8, address, true);
        put(c, 5 + nd - 8, 8, command, true);
        if (extra & 1) {
            c->flags |= IRD_F_TOGGLE;
        }
        c->extra = 0;
        break;
    }
    case IRD_P_PANASONIC:
    case IRD_P_KASEIKYO:
    case IRD_P_DENON_K:
    case IRD_P_SHARP_K:
    case IRD_P_JVC_K:
    case IRD_P_MITSUBISHI_K: {
        static const uint16_t VID[] = {0x2002, 0, 0x3254, 0x5AAA, 0x0103, 0xCB23};
        const uint32_t v = proto == IRD_P_KASEIKYO ? extra : VID[proto - IRD_P_PANASONIC];
        put(c, 0, 16, v, false);
        put(c, 16, 4, vendor_parity(v), false);
        put(c, 20, 12, address, false);
        put(c, 32, 8, command, false);
        put(c, 40, 8, field(c, 16, 8, false) ^ field(c, 24, 8, false) ^ (command & 0xFF), false);
        c->extra = v;
        break;
    }
    case IRD_P_JVC:
        put(c, 0, 8, address, false);
        put(c, 8, 8, command, false);
        break;
    case IRD_P_DENON:
    case IRD_P_SHARP:
        put(c, 0, 5, address, true);
        put(c, 5, 8, command, true);
        put(c, 13, 2, proto == IRD_P_SHARP ? 2 : 0, true);
        c->extra = proto == IRD_P_SHARP ? 2 : 0;
        break;
    case IRD_P_BOSEWAVE:
        put(c, 0, 8, command, false);
        put(c, 8, 8, ~command & 0xFF, false);
        c->address = 0;
        break;
    case IRD_P_WHYNTER:
        put(c, 0, 32, command, true);
        c->address = 0;
        break;
    }
    set_timing(c);
}
