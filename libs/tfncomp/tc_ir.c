// TFNCOMP, THE IR PLAN OF A BUTTON (tc_ir.h)
#include "tc_ir.h"
#include <string.h>

static int64_t len_of(const int32_t *d, int n)
{
    int64_t t = 0;
    for (int i = 0; i < n; i++) {
        t += d[i] < 0 ? -(int64_t)d[i] : d[i];
    }
    return t;
}

// the time from the start of one frame to the next while a key is held (the remotes of each protocol)
static int period_ms(int proto, int64_t press_us)
{
    int p;
    switch (proto) {
    case IRD_P_SONY:
        p = 45;
        break;
    case IRD_P_RC5:
        p = 114;
        break;
    case IRD_P_RC6:
        p = 107;
        break;
    case IRD_P_JVC:
        p = 55;
        break;
    case IRD_P_PANASONIC:
    case IRD_P_KASEIKYO:
    case IRD_P_DENON_K:
    case IRD_P_SHARP_K:
    case IRD_P_JVC_K:
    case IRD_P_MITSUBISHI_K:
        p = 130;
        break;
    default:
        p = 108;
        break;
    }
    const int min = (int)(press_us / 1000) + IRD_GAP_US / 1000 + 2;
    return p > min ? p : min;
}

bool tc_ir_plan(const ird_code_t *c, int32_t *d, int max, tc_ir_plan_t *p)
{
    memset(p, 0, sizeof *p);
    if (c->proto == IRD_P_UNKNOWN || c->proto >= IRD_P_N) {
        return false;
    }
    ird_code_t k = *c;
    k.flags &= (uint8_t)~IRD_F_REPEAT;
    const int n = ird_encode_press(&k, d, max);
    if (n <= 0) {
        return false;
    }
    p->n = n;
    p->carrier_khz = (int)((c->carrier_hz ? c->carrier_hz : 38000) / 1000);
    p->rep_ms = period_ms(c->proto, len_of(d, n));
    if (c->proto == IRD_P_NEC || c->proto == IRD_P_ONKYO || c->proto == IRD_P_APPLE) {
        // the NEC repeat frame: 9 ms mark, 2.25 ms space, the stop mark
        if (n + 3 > max) {
            return false;
        }
        p->rep_at = n;
        d[p->n++] = 9000;
        d[p->n++] = -2250;
        d[p->n++] = 560;
        p->rep_ms = 108;
    } else if (c->proto == IRD_P_SONY || c->proto == IRD_P_JVC) {
        // SONY: one frame again (the press has 3); JVC: the frame with no header
        ird_code_t r = k;
        if (c->proto == IRD_P_JVC) {
            r.flags |= IRD_F_REPEAT;
        }
        const int m = ird_encode(&r, d + n, max - n);
        if (m <= 0) {
            return false;
        }
        p->rep_at = n;
        p->n = n + m;
        p->rep_ms = period_ms(c->proto, len_of(d + n, m));
    }
    return true;
}

bool tc_ir_plan_raw(const int32_t *raw, int nraw, int carrier_khz, int32_t *d, int max, tc_ir_plan_t *p)
{
    memset(p, 0, sizeof *p);
    if (nraw <= 0 || nraw > max) {
        return false;
    }
    memcpy(d, raw, (size_t)nraw * sizeof *d);
    p->n = nraw;
    p->carrier_khz = carrier_khz > 0 ? carrier_khz : 38;
    p->rep_ms = period_ms(IRD_P_UNKNOWN, len_of(raw, nraw));
    return true;
}
