// RFPULSE: the infrared DEMO receiver: each 1.5 to 2.5 s one key press of a made-up remote control (the encoders of
// libs/irdecode), as an IR receiver module gives it (each mark about 60 us longer, a small jitter): a TV (NEC, then two
// repeat frames of a held key), SAMSUNG, SONY (12 bits: 3 frames), RC5 (the toggle bit changes at each press), RC6,
// PANASONIC, LG, JVC, DENON (the frame and the inverted frame), an air conditioner (64 bits: the universal decoder) and a
// remote with no known plan (UNKNOWN). The clock is esp_timer_get_time() (the PC tests move it themselves).
#include "rfpulse.h"

#include <string.h>
#include "esp_timer.h"
#include "irdecode.h"

#define QMAX 1600
#define NDEV 11

static const rfp_caps_t CAPS = {
        .id = "irdemo",
        .name = {"DEMO REMOTES", "שלטים לדוגמה"},
        .mods = RFP_MOD_OOK,
        .flags = RFP_F_DEMO | RFP_F_IR,
        .nfreq = 1,
        .freq_khz = {38},
};

typedef struct {
    bool open;
    int64_t t;  // the time of the stream (us): the end of the last level given
    uint32_t rnd;
    int32_t q[QMAX];
    int qn, qi;
    int64_t next_press;
    int dev;
    uint32_t edges;
    int toggle;
} ctx_t;
static ctx_t s_ctx;

static uint32_t rnd(ctx_t *x)
{
    uint32_t v = x->rnd;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return x->rnd = v;
}

// the level times of a frame as a receiver module gives them
static void push_frame(ctx_t *x, const int32_t *d, int n)
{
    for (int i = 0; i < n && x->qn < QMAX; i++) {
        int32_t v = d[i] < 0 ? -d[i] : d[i];
        v += (int32_t)((int32_t)(rnd(x) % 7) - 3) * v / 100;
        v += d[i] > 0 ? 60 : -60;
        if (v < 30) {
            v = 30;
        }
        x->q[x->qn++] = d[i] > 0 ? v : -v;
    }
}

static void push(ctx_t *x, int32_t v)
{
    if (x->qn < QMAX) {
        x->q[x->qn++] = v;
    }
}

static void press(ctx_t *x, int dev)
{
    static int32_t f[1400];
    ird_code_t c;
    int n = 0;
    push(x, -40000);
    switch (dev) {
    case 0:
        ird_make(IRD_P_NEC, 0x04, 0x08, 0, 0, &c);
        break;
    case 1:
        ird_make(IRD_P_SAMSUNG, 0x07, 0x02, 0, 0, &c);
        break;
    case 2:
        ird_make(IRD_P_SONY, 0x01, 0x15, 0, 12, &c);
        break;
    case 3:
        ird_make(IRD_P_RC5, 0x00, 0x0C, (uint32_t)x->toggle, 0, &c);
        x->toggle ^= 1;
        break;
    case 4:
        ird_make(IRD_P_RC6, 0x00, 0x0C, (uint32_t)x->toggle, 16, &c);
        break;
    case 5:
        ird_make(IRD_P_PANASONIC, 0x100, 0x3D, 0, 0, &c);
        break;
    case 6:
        ird_make(IRD_P_LG, 0x04, 0x0008, 0, 0, &c);
        break;
    case 7:
        ird_make(IRD_P_JVC, 0x03, 0x17, 0, 0, &c);
        break;
    case 8:
        ird_make(IRD_P_DENON, 0x02, 0xE1, 0, 0, &c);
        break;
    case 9: {
        // an air conditioner: a header of 3.1 / 1.6 ms, 64 bits (400 us marks, 1200 / 400 us spaces)
        static const uint8_t B[8] = {0xC4, 0xD3, 0x64, 0x80, 0x00, 0x00, 0x20, 0xE0};
        f[n++] = 3100;
        f[n++] = -1600;
        for (int b = 0; b < 64; b++) {
            f[n++] = 400;
            f[n++] = (B[b >> 3] >> (b & 7)) & 1 ? -1200 : -400;
        }
        f[n++] = 400;
        push_frame(x, f, n);
        push(x, -12000);
        return;
    }
    default: {
        // a remote with no plan of the decoders: three lengths of marks, the spaces with no order
        static const int16_t U[] = {1800, -900, 300,   -900, 300,  -1500, 900,  -300, 300,  -300, 1500, -600, 300,
                                    -900, 900,  -1500, 300,  -300, 300,   -600, 1500, -300, 900,  -900, 300};
        for (unsigned i = 0; i < sizeof(U) / sizeof(U[0]); i++) {
            f[n++] = U[i];
        }
        push_frame(x, f, n);
        push(x, -12000);
        return;
    }
    }
    n = ird_encode_press(&c, f, 1400);
    push_frame(x, f, n);
    if (dev == 0) {  // a held key: two NEC repeat frames
        static const int32_t R[3] = {9000, -2250, 560};
        push(x, -40000);
        push_frame(x, R, 3);
        push(x, -96000);
        push_frame(x, R, 3);
    }
    push(x, -12000);
}

static void refill(ctx_t *x)
{
    x->qn = x->qi = 0;
    if (x->t >= x->next_press) {
        press(x, x->dev);
        x->dev = (x->dev + 1) % NDEV;
        x->next_press = x->t + 1500000 + (int64_t)(rnd(x) % 1000000);
        return;
    }
    push(x, -50000);  // (no light: one long space)
}

static int d_open(void *c)
{
    ctx_t *x = c;
    memset(x, 0, sizeof(*x));
    x->open = true;
    x->t = esp_timer_get_time();
    x->rnd = 0x1D5EED01u;
    x->next_press = x->t + 500000;
    return 0;
}

static int d_read(void *c, int32_t *dur, int max)
{
    ctx_t *x = c;
    if (!x->open) {
        return -1;
    }
    const int64_t now = esp_timer_get_time();
    if (now - x->t > 2000000) {
        x->t = now - 2000000;  // (a reader that was away for long: no flood)
    }
    int n = 0;
    while (n < max && x->t < now) {
        if (x->qi >= x->qn) {
            refill(x);
        }
        int32_t v = x->q[x->qi++];
        dur[n++] = v;
        x->t += v < 0 ? -v : v;
        x->edges++;
    }
    return n;
}

static void d_stats(void *c, rfp_stats_t *s)
{
    ctx_t *x = c;
    memset(s, 0, sizeof(*s));
    s->now_us = esp_timer_get_time();
    s->edges = x->edges;
    s->level = 0;
    s->last_edge_us = x->t;
    s->line = RFP_LINE_ALIVE;
}

static void d_close(void *c)
{
    ((ctx_t *)c)->open = false;
}
static const char *d_error(int e)
{
    (void)e;
    return "the demo failed";
}

static const rfp_ops_t OPS = {d_open, d_read, d_stats, d_close, d_error};

bool rfp_ir_demo(rfp_src_t *out)
{
    out->caps = &CAPS;
    out->ops = &OPS;
    out->ctx = &s_ctx;
    return true;
}
