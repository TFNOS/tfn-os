// TFN COMPANION: the real companion core (companion/main/comp_core.c) and the real master side
// (libs/tfncomp/tc_master.c) on a fake radio with channels, overlapped near channels and lost frames, a fake IR
// receiver and a fake IR LED: the search, the pairing, the wiring, the live test, 8 IR protocols learned and sent
// byte-equal, a long frame in parts, hold and release, restarts, a bad tag, a replay, a channel move.
//   test_companion [loss %] [seed] -> "COMPANION OK ..." (DUO_V=1: the log of the companion)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comp_core.h"
#include "irdecode/irdecode.h"
#include "tfncomp/tc_ir.h"
#include "tfncomp/tc_master.h"

static int s_bad;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            s_bad++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static uint32_t g_ms = 1000;
static uint32_t g_rng = 12345;
static int g_loss;  // percent of the frames that the air loses
static int g_verbose;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return g_rng >> 8;
}

static const uint8_t M[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t C[6] = {0xA0, 0xA3, 0xB3, 0xFE, 0xB4, 0xB8};
static const uint8_t X[6] = {0x99, 0x22, 0x33, 0x44, 0x55, 0x77};  // a stranger
static int g_mch = 11;                                             // the channel of the master (its WiFi)
static int g_cch = 1;                                              // the channel of the companion

// ---------------------------------------------------------------- the air
typedef struct {
    uint8_t from[6], to[6];
    bool bcast;
    int ch;
    int n;
    uint8_t d[250];
    uint32_t at;
} pkt_t;
#define QN 512
static pkt_t g_q[QN];
static int g_qn;
static uint32_t g_sent, g_lost;

static void air(const uint8_t *from, const uint8_t *to, int ch, const uint8_t *d, int n)
{
    g_sent++;
    if ((int)(rnd() % 100) < g_loss) {
        g_lost++;
        return;
    }
    if (g_qn >= QN) {
        return;
    }
    pkt_t *p = &g_q[g_qn++];
    memcpy(p->from, from, 6);
    static const uint8_t FF[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    p->bcast = !to || !memcmp(to, FF, 6);
    if (to) {
        memcpy(p->to, to, 6);
    }
    p->ch = ch;
    p->n = n;
    memcpy(p->d, d, (size_t)n);
    p->at = g_ms + 1 + rnd() % 3;
}

static void deliver(void)
{
    int k = 0;
    for (int i = 0; i < g_qn; i++) {
        pkt_t *p = &g_q[i];
        if ((int32_t)(g_ms - p->at) < 0) {
            g_q[k++] = *p;
            continue;
        }
        const bool to_c = p->bcast || !memcmp(p->to, C, 6);
        const bool to_m = p->bcast || !memcmp(p->to, M, 6);
        // (the near channels overlap: a frame can come in on the channel next to it)
        const bool near_c = abs(p->ch - g_cch) <= 2 && (p->ch == g_cch || rnd() % 3 == 0);
        const bool near_m = abs(p->ch - g_mch) <= 2 && (p->ch == g_mch || rnd() % 3 == 0);
        if (memcmp(p->from, C, 6) && to_c && near_c) {
            cc_rx(p->from, p->d, p->n);
        }
        if (memcmp(p->from, M, 6) && to_m && near_m) {
            tcm_rx(p->from, p->d, p->n, -50);
        }
    }
    g_qn = k;
}

// ---------------------------------------------------------------- the companion: fake pins
typedef struct {
    int32_t v[2000];
    uint32_t end[2000];  // the end of each level (us)
    int n, rd;
    uint32_t last_end;
    bool open, inv;
} fpin_t;
static fpin_t g_pin[40];
typedef struct {
    uint32_t t;
    int pin;
    uint32_t carrier;
    int n;
    int32_t d[800];
} txrec_t;
static txrec_t g_tx[64];
static int g_ntx;
static cc_saved_t g_saved;
static bool g_has_saved;

static uint32_t now_us(void)
{
    return g_ms * 1000u;
}

// a frame on the air for the IR receiver of a pin, from now
static void inject(int pin, const int32_t *d, int n)
{
    fpin_t *p = &g_pin[pin];
    uint32_t t = now_us();
    if (p->n < 2000) {
        p->v[p->n] = -(int32_t)(t - p->last_end > 0x3FFFFFFF ? 0x3FFFFFFF : t - p->last_end);  // the quiet before
        p->end[p->n++] = t;
    }
    for (int i = 0; i < n && p->n < 2000; i++) {
        t += (uint32_t)(d[i] < 0 ? -d[i] : d[i]);
        p->v[p->n] = d[i];
        p->end[p->n++] = t;
    }
    p->last_end = t;
}

static void c_send(const uint8_t *mac, const uint8_t *d, int n)
{
    air(C, mac, g_cch, d, n);
}
static void c_set_channel(int ch)
{
    g_cch = ch;
}
static uint32_t f_now(void)
{
    return g_ms;
}
static void f_random(uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}
static void c_save(const cc_saved_t *s)
{
    g_saved = *s;
    g_has_saved = true;
}
static bool c_rx_open(int pin, bool inv, bool ir)
{
    (void)ir;
    g_pin[pin].open = true;
    g_pin[pin].inv = inv;
    return true;
}
static void c_rx_close(int pin)
{
    g_pin[pin].open = false;
}
static int c_rx_read(int pin, int32_t *d, int max, int32_t *idle_us, bool *active)
{
    fpin_t *p = &g_pin[pin];
    const uint32_t t = now_us();
    int k = 0;
    while (k < max && p->rd < p->n && p->end[p->rd] <= t) {
        d[k++] = p->v[p->rd++];
    }
    const uint32_t last = p->rd ? p->end[p->rd - 1] : 0;
    *idle_us = (int32_t)(t - last);
    *active = p->rd < p->n && p->v[p->rd] > 0;  // (the level that runs now)
    return k;
}
static int c_tx_send(int pin, uint32_t carrier, bool inv, const int32_t *d, int n)
{
    (void)inv;
    if (g_ntx < 64) {
        txrec_t *r = &g_tx[g_ntx++];
        r->t = g_ms;
        r->pin = pin;
        r->carrier = carrier;
        r->n = n < 800 ? n : 800;
        memcpy(r->d, d, (size_t)r->n * sizeof *d);
    }
    return 0;
}
static void c_tx_idle(int pin, bool inv)
{
    (void)pin;
    (void)inv;
}
static void c_log(const char *s)
{
    if (g_verbose) {
        printf("  [%6u comp] %s\n", g_ms, s);
    }
}
static const cc_io_t CIO = {.send = c_send,
                            .set_channel = c_set_channel,
                            .now_ms = f_now,
                            .random = f_random,
                            .save = c_save,
                            .rx_open = c_rx_open,
                            .rx_close = c_rx_close,
                            .rx_read = c_rx_read,
                            .tx_send = c_tx_send,
                            .tx_idle = c_tx_idle,
                            .log = c_log};

static tc_info_t chip_esp32(void)
{
    tc_info_t in;
    memset(&in, 0, sizeof in);
    in.chip = TC_CHIP_ESP32;
    in.rev = 3;
    in.fw = CC_FW;
    in.ngpio = (uint8_t)tc_chip_gpios(TC_CHIP_ESP32);
    for (int g = 0; g < in.ngpio; g++) {
        in.pins[g] = tc_pin_flags(TC_CHIP_ESP32, g);
    }
    in.led = 2;
    return in;
}

static void comp_boot(void)
{
    const tc_info_t in = chip_esp32();
    cc_init(&CIO, g_has_saved ? &g_saved : NULL, &in, C);
}

// ---------------------------------------------------------------- the master
static uint8_t g_msave[TCM_SAVE_MAX];
static int g_msave_n;
static bool m_send(const uint8_t *mac, const uint8_t *d, int n)
{
    air(M, mac, g_mch, d, n);
    return true;
}
static int m_channel(void)
{
    return g_mch | 0x80;
}
static void m_changed(void)
{
    g_msave_n = tcm_save(g_msave, sizeof g_msave);
}
static const tcm_io_t MIO = {m_send, f_now, f_random, m_channel, m_changed};

static void master_boot(void)
{
    tcm_init(&MIO, M, "TEST MASTER");
    if (g_msave_n) {
        tcm_load(g_msave, g_msave_n);
    }
}

// ---------------------------------------------------------------- the clock
static void step(int ms)
{
    for (int i = 0; i < ms; i++) {
        g_ms++;
        deliver();
        if (g_ms % 10 == 0) {
            cc_tick();
            tcm_tick();
        }
    }
}

// run until the request of companion 0 ends (or max ms); returns its state
static int wait_req(int max)
{
    for (int t = 0; t < max; t += 10) {
        if (tcm_req(0) != TCM_BUSY) {
            return tcm_req(0);
        }
        step(10);
    }
    return tcm_req(0);
}

// ---------------------------------------------------------------- the tests
static void t_search_pair(void)
{
    // the companion searches (it starts on channel 1); the master is on 11
    tcm_found_t f[8];
    int n = 0;
    for (int t = 0; t < 30000 && !n; t += 10) {
        step(10);
        n = tcm_found(f, 8);
    }
    CHECK(n == 1 && !memcmp(f[0].mac, C, 6) && f[0].chip == TC_CHIP_ESP32 && !f[0].paired, "found (%d)", n);
    if (g_verbose) {
        printf("  found after %u ms\n", g_ms);
    }
    tcm_pair_start(C, "LIVING ROOM");
    for (int t = 0; t < 20000 && tcm_pair_state() != 3; t += 10) {
        step(10);
    }
    cc_state_t st;
    cc_state(&st);
    CHECK(st.pair_wait && tcm_pair_state() == 3, "waits for the BOOT key (%d %d)", st.pair_wait, tcm_pair_state());
    cc_boot_press();
    for (int t = 0; t < 5000 && tcm_pair_state() == 3; t += 10) {
        step(10);
    }
    CHECK(tcm_pair_state() == 2 && tcm_count() == 1, "paired (%d)", tcm_pair_state());
    CHECK(g_saved.paired && !memcmp(g_saved.master, M, 6), "the companion saved its master");
    if (!tcm_count()) {
        printf("COMPANION FAIL: no pairing\n");
        exit(1);
    }
    CHECK(!strcmp(tcm_get(0)->name, "LIVING ROOM"), "name");
    step(3000);
    CHECK(tcm_online(0), "online");
}

static void t_info_config(void)
{
    CHECK(tcm_info(0) && wait_req(5000) == TCM_DONE, "info");
    CHECK(tcm_get(0)->has_info && tcm_get(0)->info.chip == TC_CHIP_ESP32 && tcm_get(0)->info.ngpio == 40, "info data");
    const tc_wire_t bad[2] = {{7, TC_R_IR_RX, 0, 0}, {4, TC_R_IR_TX, 0, 38}};
    CHECK(tcm_config(0, bad, 2) && wait_req(5000) == TCM_FAIL, "a bad wiring fails");
    uint8_t e[8], w[8];
    int n = 0;
    CHECK(!tcm_cfg_result(0, e, w, &n) && n == 2 && e[0] == TC_E_FLASH && e[1] == 0, "a flash pin (%d %d)", n, e[0]);
    const tc_wire_t good[2] = {{18, TC_R_IR_RX, 0, 0}, {4, TC_R_IR_TX, 0, 38}};
    CHECK(tcm_config(0, good, 2) && wait_req(5000) == TCM_DONE, "wiring");
    CHECK(tcm_cfg_result(0, e, w, &n) && n == 2 && e[0] == 0 && e[1] == 0, "wiring result");
    CHECK(g_saved.nwire == 2 && g_pin[18].open && g_pin[18].inv, "the receiver pin is open");
}

static void t_watch(void)
{
    CHECK(tcm_watch(0, 18, true) && wait_req(3000) == TCM_DONE, "watch");
    int32_t d[8] = {600, -600, 600, -600, 600, -600, 600, -1200};
    inject(18, d, 8);
    step(1000);
    CHECK(tcm_get(0)->watch_edges >= 8 && (g_loss || tcm_get(0)->watch_n >= 8), "edges %u", tcm_get(0)->watch_edges);
    CHECK(tcm_watch(0, 18, false) && wait_req(3000) == TCM_DONE, "watch off");
    step(500);
}

// a remote sends a code of a protocol: capture, decode, plan, send, compare
static void learn_and_press(int proto, uint32_t a, uint32_t c, int nbits)
{
    ird_code_t code, got;
    ird_make(proto, a, c, 0, nbits, &code);
    static int32_t press[1400], frame[TC_VALS_MAX];
    const int np = ird_encode_press(&code, press, 1400);
    CHECK(tcm_capture(0, 18, 5, 8), "capture %s", ird_name(proto));
    for (int t = 0; t < 3000 && !tcm_capture_ready(0); t += 10) {
        step(10);
    }
    CHECK(tcm_capture_ready(0), "%s: the companion listens", ird_name(proto));
    step(200);
    inject(18, press, np);
    int st = wait_req(8000);
    CHECK(st == TCM_DONE, "%s: the frame came (%d code %d)", ird_name(proto), st, tcm_req_code(0));
    int pin = -1;
    const int nf = tcm_frame(frame, TC_VALS_MAX, &pin);
    CHECK(pin == 18 && nf > 4, "%s: frame %d", ird_name(proto), nf);
    CHECK(ird_decode(frame, nf, false, &got) && got.proto == proto && got.address == code.address &&
                  got.command == code.command,
          "%s: decoded as %s 0x%X 0x%X", ird_name(proto), ird_name(got.proto), got.address, got.command);
    // the press
    static int32_t plan_d[1400];
    tc_ir_plan_t pl;
    CHECK(tc_ir_plan(&got, plan_d, 1400, &pl), "%s: plan", ird_name(proto));
    const int before = g_ntx;
    CHECK(tcm_send(0, 4, pl.carrier_khz, plan_d, pl.n, pl.rep_at, pl.rep_ms), "send");
    st = wait_req(5000);
    CHECK(st == TCM_DONE, "%s: sent (%d code %d)", ird_name(proto), st, tcm_req_code(0));
    CHECK(g_ntx == before + 1, "%s: one send (%d)", ird_name(proto), g_ntx - before);
    if (g_ntx == before + 1) {
        const txrec_t *r = &g_tx[before];
        const int first = pl.rep_at ? pl.rep_at : pl.n;
        CHECK(r->pin == 4 && r->carrier == (uint32_t)pl.carrier_khz * 1000 && r->n == first &&
                      !memcmp(r->d, plan_d, (size_t)first * 4),
              "%s: the LED got the press (n %d / %d, carrier %u)", ird_name(proto), r->n, first, r->carrier);
        ird_code_t back;
        const int one = proto == IRD_P_SONY || proto == IRD_P_DENON || proto == IRD_P_SHARP ? 0 : r->n;
        if (one) {
            CHECK(ird_decode(r->d, one, false, &back) && ird_same(&back, &code), "%s: the LED frame decodes",
                  ird_name(proto));
        }
    }
    step(300);
}

static void t_hold(void)
{
    ird_code_t code;
    ird_make(IRD_P_NEC, 0x04, 0x08, 0, 0, &code);
    static int32_t d[1400];
    tc_ir_plan_t pl;
    tc_ir_plan(&code, d, 1400, &pl);
    int before = g_ntx;
    tcm_send(0, 4, pl.carrier_khz, d, pl.n, pl.rep_at, pl.rep_ms);
    // (the finger stays: the first press can come late when frames are lost)
    for (int t = 0; t < 5000 && g_ntx == before; t += 100) {
        step(100);
        tcm_hold(0);
    }
    before = g_ntx - 1;
    for (int t = 0; t < 1000; t += 100) {
        step(100);
        tcm_hold(0);
    }
    const int during = g_ntx - before;
    tcm_stop(0);
    step(1000);
    const int after = g_ntx - before;
    if (g_verbose) {
        printf("  hold 1 s: %d sends (then %d)\n", during, after);
    }
    CHECK(during <= 12 && during >= (g_loss ? 2 : 7), "NEC hold: about 9 repeats (%d)", during);
    CHECK(after - during <= (g_loss ? 3 : 1), "it stops at the release (%d more)", after - during);
    if (during >= 2) {
        const txrec_t *r = &g_tx[before + 1];
        CHECK(r->n == 3 && r->d[0] == 9000 && r->d[1] == -2250, "the NEC repeat frame");
    }
}

static void t_long_frame(void)
{
    // an air conditioner: 600 level times in 7 parts
    static int32_t d[599], frame[TC_VALS_MAX];
    d[0] = 3500;
    d[1] = -1750;
    for (int i = 2; i < 599; i++) {
        d[i] = (i & 1) ? -((i * 7) % 3 ? 400 : 1250) : 450;
    }
    CHECK(tcm_capture(0, 18, 5, 8), "capture AC");
    for (int t = 0; t < 3000 && !tcm_capture_ready(0); t += 10) {
        step(10);
    }
    step(100);
    inject(18, d, 599);
    CHECK(wait_req(9000) == TCM_DONE, "AC frame came (code %d)", tcm_req_code(0));
    const int n = tcm_frame(frame, TC_VALS_MAX, NULL);
    CHECK(n == 599 && !memcmp(frame, d, sizeof d), "AC frame the same (%d)", n);
    static int32_t pd[800];
    tc_ir_plan_t pl;
    tc_ir_plan_raw(frame, n, 38, pd, 800, &pl);
    const int before = g_ntx;
    CHECK(tcm_send(0, 4, 38, pd, pl.n, 0, 0) && wait_req(6000) == TCM_DONE, "AC send");
    CHECK(g_ntx == before + 1 && g_tx[before].n == 599 && !memcmp(g_tx[before].d, d, sizeof d), "AC on the LED");
    step(300);
}

static void t_restarts(void)
{
    // the app restarts with an old counter in its file: the companion asks for a higher one
    const int keep = g_msave_n;
    g_msave_n = keep;
    tcm_get(0)->tx_ctr = 3;
    m_changed();
    master_boot();
    CHECK(tcm_info(0) && wait_req(5000) == TCM_DONE, "after a restart of the app (counter %u)", tcm_get(0)->tx_ctr);
    // the companion restarts: its counter floor jumps
    comp_boot();
    CHECK(tcm_info(0) && wait_req(8000) == TCM_DONE, "after a restart of the companion (counter %u)",
          tcm_get(0)->tx_ctr);
}

static void t_attacks(void)
{
    const int loss = g_loss;
    g_loss = 0;
    cc_state_t a, b;
    cc_state(&a);
    // a stranger with the right MAC but no key
    tc_buf_t f;
    tc_begin(&f, TC_FORGET, 1, 0, (uint8_t)g_mch);
    uint8_t key[16] = {1, 2, 3};
    tc_seal(&f, key, M, 999999);
    air(M, C, g_cch, f.b, f.n);
    // a stranger asks for a pairing: no change without the BOOT key
    tc_begin(&f, TC_PAIR_REQ, 1, 0, (uint8_t)g_cch);
    uint8_t pub[32] = {9};
    tc_putn(&f, pub, 32);
    air(X, C, g_cch, f.b, f.n);
    step(500);
    cc_state(&b);
    CHECK(b.rx_bad_tag > a.rx_bad_tag && g_saved.paired && !memcmp(g_saved.master, M, 6), "a bad tag does nothing");
    // a replay of an old frame of the master: the counter stops it
    const uint32_t old = b.rx_old;
    static uint8_t keep[250];
    int keep_n = 0;
    tc_begin(&f, TC_PING, 5, 0, (uint8_t)g_mch);
    tc_seal(&f, tcm_get(0)->key, M, 1);  // (an old counter)
    memcpy(keep, f.b, (size_t)f.n);
    keep_n = f.n;
    air(M, C, g_cch, keep, keep_n);
    step(300);
    cc_state(&b);
    CHECK(b.rx_old > old, "a replay is old");
    step(60000);  // (the pairing of the stranger ends)
    g_loss = loss;
}

static void t_channel_move(void)
{
    // the master moves to another WiFi channel: the companion finds it again
    g_mch = 6;
    uint32_t t0 = g_ms;
    for (int t = 0; t < 60000 && (tcm_online(0) || g_ms - t0 < 7000); t += 10) {
        step(10);
    }
    CHECK(!tcm_online(0), "offline after the move");
    t0 = g_ms;
    for (int t = 0; t < 60000 && !tcm_online(0); t += 10) {
        step(10);
    }
    if (g_verbose) {
        printf("  online again %u ms after the offline\n", g_ms - t0);
    }
    CHECK(tcm_online(0) && g_cch == 6, "online again on channel 6");
}

static void t_forget(void)
{
    tcm_forget(0);
    step(500);
    CHECK(tcm_count() == 0, "the master forgot it");
    CHECK(!g_saved.paired || g_loss, "the companion forgot its master");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_loss = argc > 1 ? atoi(argv[1]) : 0;
    g_rng = argc > 2 ? (uint32_t)atoi(argv[2]) : 12345;
    g_verbose = getenv("DUO_V") != NULL;
    comp_boot();
    master_boot();
    t_search_pair();
    t_info_config();
    t_watch();
    learn_and_press(IRD_P_NEC, 0x04, 0x08, 0);
    learn_and_press(IRD_P_SAMSUNG, 0x07, 0x02, 0);
    learn_and_press(IRD_P_SONY, 0x01, 0x15, 12);
    learn_and_press(IRD_P_RC5, 0x00, 0x0C, 0);
    learn_and_press(IRD_P_RC6, 0x00, 0x0C, 16);
    learn_and_press(IRD_P_PANASONIC, 0x100, 0x3D, 0);
    learn_and_press(IRD_P_LG, 0x04, 0x08, 0);
    learn_and_press(IRD_P_JVC, 0x03, 0x17, 0);
    t_hold();
    t_long_frame();
    t_restarts();
    t_attacks();
    t_channel_move();
    t_forget();
    printf("COMPANION %s: loss %d %%, %u frames, %u lost, %d faults\n", s_bad ? "FAIL" : "OK", g_loss, g_sent, g_lost,
           s_bad);
    return s_bad ? 1 : 0;
}
