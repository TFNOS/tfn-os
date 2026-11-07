// RFPULSE: the DEMO receiver: the noise of a super-regenerative receiver, and each few seconds the bursts of five made-up
// devices (the encoders of libs/rfdecode): a remote key (RCSwitch protocol 1, 24 bits), a doorbell (protocol 6, the
// inverted levels), a weather sensor (PPM, 36 bits), a light switch (PWM, 32 bits), a car key (Manchester, 48 bits).
// The clock is esp_timer_get_time(): the stream comes in real time (the PC tests move that clock themselves).
#include "rfpulse.h"

#include <string.h>
#include "esp_timer.h"
#include "rfdecode.h"

#define QMAX 2048

static const rfp_caps_t CAPS = {
        .id = "demo",
        .name = {"DEMO SIGNALS", "אותות לדוגמה"},
        .mods = RFP_MOD_OOK | RFP_MOD_ASK | RFP_MOD_FSK,
        .flags = RFP_F_DEMO,
        .nfreq = 2,
        .freq_khz = {433920, 315000},
};

typedef struct {
    bool open;
    int64_t t;  // the time of the stream (us): the end of the last level given
    uint32_t rnd;
    int32_t q[QMAX];
    int qn, qi;
    int64_t next_burst;
    int dev;
    uint32_t edges;
    int last_sign;
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

static void push(ctx_t *x, int32_t v)
{
    if (x->qn < QMAX) {
        x->q[x->qn++] = v;
    }
}

// One burst of a device: a quiet low (a strong signal stops the noise), then the repeats
static void burst(ctx_t *x, int dev)
{
    int32_t one[RFD_MAX_DUR];
    int n = 0, rep = 1;
    uint8_t bits[16];
    switch (dev) {
    case 0:
        n = rfd_enc_rcswitch(1, 0x5D5D51, 24, 340, one, RFD_MAX_DUR);
        rep = 8;
        break;
    case 1:
        n = rfd_enc_rcswitch(6, 0xA1B2C3, 24, 450, one, RFD_MAX_DUR);
        rep = 6;
        break;
    case 2: {
        static const uint8_t B[] = {0x8A, 0x31, 0x5C, 0x02, 0x70};  // 36 bits: an id, 21.5 C, 47 %
        memcpy(bits, B, sizeof(B));
        n = rfd_enc_ppm(bits, 36, 500, 1000, 2000, 9000, one, RFD_MAX_DUR);
        rep = 6;
        break;
    }
    case 3: {
        static const uint8_t B[] = {0xC3, 0x5A, 0x0F, 0x96};
        memcpy(bits, B, sizeof(B));
        n = rfd_enc_pwm(bits, 32, 300, 900, 7000, one, RFD_MAX_DUR);
        rep = 4;
        break;
    }
    default: {
        static const uint8_t B[] = {0xF0, 0x0D, 0x2B, 0xE1, 0x44, 0x7A};
        memcpy(bits, B, sizeof(B));
        n = rfd_enc_manch(bits, 48, 250, 6000, one, RFD_MAX_DUR);
        rep = 3;
        break;
    }
    }
    push(x, -12000);
    // (a protocol with the inverted levels starts with its long low: that low is the sync of the first repeat)
    for (int r = 0; r < rep; r++) {
        for (int i = 0; i < n; i++) {
            int32_t v = one[i];
            // a little jitter of a real transmitter (+-4 %)
            v += (int32_t)((int32_t)(rnd(x) % 9) - 4) * v / 100;
            push(x, v);
        }
    }
    push(x, -8000);
}

static void refill(ctx_t *x)
{
    x->qn = x->qi = 0;
    if (x->t >= x->next_burst) {
        burst(x, x->dev);
        x->dev = (x->dev + 1) % 5;
        x->next_burst = x->t + 1800000 + (int64_t)(rnd(x) % 1500000);
        return;
    }
    // the noise of a super-regenerative receiver with no signal: short random levels
    for (int i = 0; i < 64; i++) {
        int32_t v = 60 + (int32_t)(rnd(x) % 640);
        x->last_sign = -x->last_sign;
        push(x, x->last_sign * v);
    }
}

static int d_open(void *c)
{
    ctx_t *x = c;
    memset(x, 0, sizeof(*x));
    x->open = true;
    x->t = esp_timer_get_time();
    x->rnd = 0xC0FFEE11u;
    x->next_burst = x->t + 600000;
    x->last_sign = 1;
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

bool rfp_demo(rfp_src_t *out)
{
    out->caps = &CAPS;
    out->ops = &OPS;
    out->ctx = &s_ctx;
    return true;
}
