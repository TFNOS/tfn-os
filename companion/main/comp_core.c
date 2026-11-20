// THE COMPANION CORE (comp_core.h): the pairing, the channel search, the frames of the master, the wiring, the live
// test of a pin, the capture of one frame and the send (with the repeat of a held button), the switch and analog
// modules (SET, STATE).
#include "comp_core.h"
#include <stdio.h>
#include <string.h>

#define HOP_MS        250    // the time on each channel of the search
#define SILENT_MS     6500   // no frame of the master for this time (it pings each 2 s): the search starts
#define HELLO_MS      1000   // a HELLO at most in this time on one channel
#define PAIR_MS       60000  // the time for the press of the BOOT key
#define WATCH_MAX_MS  60000
#define WATCH_STEP_MS 200
#define PARTS_WAIT_MS 300  // SEND: a part is missing for this time: a NAK with the parts that came
#define HOLD_MS       300  // the repeat stops after this time with no HOLD
#define CAP_MAX       700
#define NCH           13

static const cc_io_t *s_io;
static cc_saved_t s_sv;
static tc_info_t s_chip;
static uint8_t s_me[6];
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static int s_ch;
static bool s_search;
static uint32_t s_hop_at, s_hello_at;
static int s_hello_n;
static int s_hop_k;
static uint32_t s_master_ms;  // the last good frame of the master (0: none)
static uint32_t s_rx_floor;   // the last counter of the master
static uint32_t s_tx_ctr;
static uint16_t s_seq;
static cc_state_t s_st;

// the pairing that waits for the BOOT key
static struct {
    bool on;
    uint32_t until;
    uint8_t mac[6];
    uint8_t pub[32];
    char name[17];
} s_pair;

// the live test of a pin
static struct {
    int pin;  // -1: off
    uint32_t until, next;
    uint32_t edges;
} s_watch = {-1, 0, 0, 0};

// the capture
static struct {
    int pin;  // -1: off
    uint32_t until;
    uint16_t seq;  // the seq of the CAPTURE (the TIMEOUT NAK)
    int gap_us;
    bool rf;     // a RF receiver: only a clean packet (rf_clean)
    int shorts;  // RF: the levels shorter than RF_SHORT_US
    int n;
    int32_t d[CAP_MAX];
} s_cap = {.pin = -1};

// the last captured frame (a part again for a NAK; all parts again until the ACK of the master)
static struct {
    uint8_t id, pin, gap;
    int n;
    bool wait;  // no ACK yet
    int tries;
    uint32_t next;
    uint16_t v[TC_VALS_MAX];
} s_out;
#define FRAME_AGAIN_MS 400
#define FRAME_TRIES    6

// the send
static tc_asm_t s_asm;
static uint32_t s_asm_t0;
static bool s_done_on;  // the id of the last SEND that went out
static uint8_t s_done_id;
static struct {
    bool on;
    uint8_t id, pin;
    bool inv;
    uint32_t carrier;
    uint16_t rep_ms;
    uint32_t next, hold_until;
    int n;  // the level times of the repeat
    int32_t d[TC_VALS_MAX];
} s_rep;
static int32_t s_tmp[TC_VALS_MAX];

// THE SWITCH AND ANALOG MODULES (the wiring order)
#define DEBOUNCE_TICKS 3    // a switch input: the same new level in 3 reads (cc_tick: 10 ms) = a change
#define ADC_EVERY_MS   100  // an analog input: one read in this time
#define ADC_STEP_MV    40   // a change of this size goes to the master at once
#define STATE_GAP_MS   60   // TC_STATE at most once in this time
static struct {
    uint8_t pin, role, cls;
    bool act;  // the active level (on, pressed, motion, closed)
    uint16_t v, cnt;
    uint8_t same;      // a switch input: the reads with the other level
    uint16_t sent;     // an analog input: the value of the last TC_STATE
    uint32_t back_at;  // a switch output: the end of a "press" (0: none)
    uint16_t back_v;
} s_mod[TC_WIRE_MAX];
static int s_nmod;
static bool s_state_dirty;
// THE UPDATE BY RADIO
#define OTA_IDLE_MS 15000  // no frame of the update in this time: it stops
static struct {
    bool on;
    uint32_t size, crc, next;
    uint32_t last_ms;
    uint32_t restart_at;  // (not 0: the start of the new firmware)
    bool done;            // a good END came (a late frame of the master gets the ACK of the END again)
} s_ota;
static bool s_valid_told;
static uint32_t s_cur_crc;  // the CRC-32 of the bytes that came
static uint32_t s_state_at, s_adc_at;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void logf_(const char *fmt, ...)
{
    if (!s_io->log) {
        return;
    }
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    s_io->log(b);
}

static uint32_t now(void)
{
    return s_io->now_ms();
}

static void save(void)
{
    s_sv.magic = CC_SAVED_MAGIC;
    s_io->save(&s_sv);
}

// ---------------------------------------------------------------- the frames that go out
static void begin(tc_buf_t *f, int type, uint8_t flags)
{
    tc_begin(f, type, ++s_seq, flags, (uint8_t)s_ch);
}

static void send_plain(tc_buf_t *f, const uint8_t *mac)
{
    s_io->send(mac, f->b, f->n);
    s_st.tx_frames++;
}

// a tagged frame to the master
static void send_master(tc_buf_t *f)
{
    if (!s_sv.paired) {
        return;
    }
    s_tx_ctr++;
    if (s_tx_ctr >= s_sv.tx_hi) {
        s_sv.tx_hi = s_tx_ctr + CC_HI_STEP;
        save();
    }
    if (!tc_seal(f, s_sv.key, s_me, s_tx_ctr)) {
        return;
    }
    send_plain(f, s_sv.master);
}

static void ack(int type, uint16_t seq, int code, uint32_t arg)
{
    tc_buf_t f;
    begin(&f, type, 0);
    tc_put16(&f, seq);
    tc_put8(&f, (uint32_t)code);
    tc_put32(&f, arg);
    send_master(&f);
}

static void hello(void)
{
    tc_buf_t f;
    begin(&f, TC_HELLO, (uint8_t)((s_sv.paired ? TC_FL_PAIRED : 0) | (s_pair.on ? TC_FL_BOOT : 0)));
    tc_put8(&f, s_chip.chip);
    tc_put8(&f, s_chip.rev);
    tc_put16(&f, CC_FW);
    tc_put8(&f, s_sv.paired);
    if (s_sv.paired) {
        tc_putn(&f, s_sv.master, 6);
    }
    send_plain(&f, BCAST);
    s_hello_at = now();
}

static void info(void)
{
    tc_buf_t f;
    begin(&f, TC_INFO, 0);
    s_chip.nwire = s_sv.nwire;
    memcpy(s_chip.wire, s_sv.wire, sizeof s_chip.wire);
    tc_info_put(&f, &s_chip);
    send_master(&f);
}

// ---------------------------------------------------------------- the channel
static void set_ch(int ch)
{
    if (ch < 1 || ch > NCH || ch == s_ch) {
        return;
    }
    s_ch = ch;
    s_io->set_channel(ch);
}

// the channel of the master from its frame: the near channels overlap, so a frame can come in on the next channel
static void follow(uint8_t ch)
{
    const int c = ch & 0x7F;
    if (c >= 1 && c <= NCH && c != s_ch) {
        logf_("to channel %d (the channel of the master)", c);
        set_ch(c);
    }
}

// the search: the home channel each second step, the others in turn
static void hop(void)
{
    const uint32_t t = now();
    if (t - s_hop_at < HOP_MS) {
        // a second HELLO on the channel (a lost one costs a whole round of the channels), then one each HELLO_MS
        if (t - s_hello_at >= HELLO_MS || (s_hello_n == 1 && t - s_hello_at >= HOP_MS / 2)) {
            hello();
            s_hello_n++;
        }
        return;
    }
    s_hello_n = 1;
    s_hop_at = t;
    s_hop_k++;
    int ch = s_sv.home_ch ? s_sv.home_ch : 1;
    if (s_hop_k & 1) {
        static int other;
        do {
            other = other % NCH + 1;
        } while (other == ch);
        ch = other;
    }
    set_ch(ch);
    hello();
}

// ---------------------------------------------------------------- the pins
static const tc_wire_t *wired(int pin)
{
    for (int i = 0; i < s_sv.nwire; i++) {
        if (s_sv.wire[i].pin == pin) {
            return &s_sv.wire[i];
        }
    }
    return NULL;
}

static bool rx_inv(const tc_wire_t *w)
{
    const bool ir = w->role == TC_R_IR_RX;  // (an IR receiver: LOW = the carrier)
    return ir != !!(w->flags & TC_WF_INVERT);
}

static bool tx_inv(const tc_wire_t *w)
{
    return !!(w->flags & TC_WF_INVERT);
}

// the active level of a switch module: an output HIGH = on, a pulled-up input LOW, MOTION HIGH; INVERT turns it
static bool mod_act(const tc_wire_t *w)
{
    const bool high = !tc_role_pullup(w->role);
    return high != !!(w->flags & TC_WF_INVERT);
}

static void pins_open(void)
{
    s_nmod = 0;
    for (int i = 0; i < s_sv.nwire; i++) {
        const tc_wire_t *w = &s_sv.wire[i];
        const int cls = tc_role_class(w->role);
        if (cls == TC_C_PULSE_RX) {
            s_io->rx_open(w->pin, rx_inv(w), w->role == TC_R_IR_RX);
        } else if (cls == TC_C_PULSE_TX) {
            s_io->tx_idle(w->pin, tx_inv(w));
        } else if (cls == TC_C_OUT || cls == TC_C_IN || cls == TC_C_ADC) {
            if (!s_io->in_open || !s_io->out_set) {
                continue;
            }
            const int k = s_nmod++;
            memset(&s_mod[k], 0, sizeof s_mod[k]);
            s_mod[k].pin = w->pin;
            s_mod[k].role = w->role;
            s_mod[k].cls = (uint8_t)cls;
            s_mod[k].act = mod_act(w);
            if (cls == TC_C_OUT) {
                s_io->tx_idle(w->pin, !s_mod[k].act);  // (an output at the off level: a relay starts OFF)
            } else if (cls == TC_C_IN) {
                s_io->in_open(w->pin, tc_role_pullup(w->role));
                s_mod[k].v = s_io->in_read(w->pin) == s_mod[k].act;
            } else {
                const int mv = s_io->adc_read ? s_io->adc_read(w->pin) : -1;
                s_mod[k].v = s_mod[k].sent = (uint16_t)(mv < 0 ? 0 : mv);
            }
        }
    }
    s_state_dirty = true;
}

static void pins_close(void)
{
    for (int i = 0; i < s_sv.nwire; i++) {
        const int cls = tc_role_class(s_sv.wire[i].role);
        if (cls == TC_C_PULSE_RX) {
            s_io->rx_close(s_sv.wire[i].pin);
        } else if ((cls == TC_C_OUT || cls == TC_C_IN || cls == TC_C_ADC) && s_io->pin_free) {
            s_io->pin_free(s_sv.wire[i].pin);
        }
    }
    s_nmod = 0;
}

bool cc_wire_set(const tc_wire_t *w, int n, uint8_t *err, uint8_t *warn)
{
    if (n < 0 || n > TC_WIRE_MAX) {
        return false;
    }
    if (!tc_wire_check(s_chip.chip, w, n, err, warn)) {
        return false;
    }
    s_watch.pin = -1;
    s_cap.pin = -1;
    s_rep.on = false;
    pins_close();
    s_sv.nwire = (uint8_t)n;
    memcpy(s_sv.wire, w, (size_t)n * sizeof *w);
    save();
    pins_open();
    logf_("wiring: %d pins", n);
    return true;
}

static void on_config(tc_msg_t *m)
{
    tc_wire_t w[TC_WIRE_MAX];
    const int n = (int)tc_get8(m);
    uint8_t err[TC_WIRE_MAX], warn[TC_WIRE_MAX];
    memset(err, 0, sizeof err);
    memset(warn, 0, sizeof warn);
    bool ok = n <= TC_WIRE_MAX;
    for (int i = 0; ok && i < n; i++) {
        ok = tc_getn(m, &w[i], 4);
    }
    if (ok) {
        ok = cc_wire_set(w, n, err, warn);
    }
    tc_buf_t f;
    begin(&f, TC_CFG_RES, 0);
    tc_put8(&f, ok);
    const int k = n <= TC_WIRE_MAX ? n : 0;
    tc_put8(&f, (uint32_t)k);
    for (int i = 0; i < k; i++) {
        tc_put8(&f, err[i]);
        tc_put8(&f, warn[i]);
    }
    send_master(&f);
}

// ---------------------------------------------------------------- the live test
static void watch_tick(void)
{
    const uint32_t t = now();
    if (s_watch.pin < 0 || (int32_t)(t - s_watch.next) < 0) {
        return;
    }
    if ((int32_t)(t - s_watch.until) >= 0) {
        s_watch.pin = -1;
        return;
    }
    s_watch.next = t + WATCH_STEP_MS;
    int32_t idle = 0;
    bool act = false;
    const int n = s_io->rx_read(s_watch.pin, s_tmp, TC_VALS_MAX, &idle, &act);
    s_watch.edges += (uint32_t)n;
    static uint16_t v[TC_VALS_MAX];
    const int nv = tc_vals_from_levels(s_tmp, n, v, TC_VALS_MAX);
    const int k = nv > 100 ? 100 : nv;
    tc_buf_t f;
    begin(&f, TC_EDGES, 0);
    tc_put8(&f, (uint32_t)s_watch.pin);
    tc_put8(&f, act);
    tc_put32(&f, s_watch.edges);
    tc_put8(&f, (uint32_t)k);
    for (int i = nv - k; i < nv; i++) {
        tc_put16(&f, v[i]);
    }
    send_master(&f);
}

// ---------------------------------------------------------------- the capture
static void frame_part_send(int part)
{
    tc_part_t head;
    memset(&head, 0, sizeof head);
    head.id = s_out.id;
    head.pin = s_out.pin;
    head.carrier = s_out.gap;
    static tc_part_t p[8];
    const int np = tc_parts_make(&head, s_out.v, s_out.n, p, 8);
    for (int k = 0; k < np; k++) {
        if (part >= 0 && k != part) {
            continue;
        }
        tc_buf_t f;
        begin(&f, TC_FRAME, 0);
        tc_part_put(&f, &p[k]);
        send_master(&f);
    }
}

static void cap_done(void)
{
    s_out.id++;
    s_out.pin = (uint8_t)s_cap.pin;
    s_out.gap = (uint8_t)(s_cap.gap_us / 1000);
    s_out.n = tc_vals_from_levels(s_cap.d, s_cap.n, s_out.v, TC_VALS_MAX);
    s_cap.pin = -1;
    s_st.captures++;
    logf_("capture: %d level times", s_cap.n);
    frame_part_send(-1);
    s_out.wait = true;
    s_out.tries = 1;
    s_out.next = now() + FRAME_AGAIN_MS;
}

static void frame_tick(void)
{
    if (!s_out.wait || (int32_t)(now() - s_out.next) < 0) {
        return;
    }
    if (s_out.tries >= FRAME_TRIES) {
        s_out.wait = false;
        return;
    }
    s_out.tries++;
    s_out.next = now() + FRAME_AGAIN_MS;
    frame_part_send(-1);
}

// one level time into the frame: a glitch (shorter than GLITCH_US) and the level after it join the level before it
// (an IR receiver gives no such short level; the slicer of IRremote does the same)
#define GLITCH_US   80
#define RF_SHORT_US 150

// a frame of a RF receiver that is a real packet (not the noise of the receiver)
static bool rf_clean(void)
{
    return s_cap.n >= 16 && s_cap.shorts * 8 < s_cap.n;
}

// the end of a frame (a long low): done, or (a short noise, a noisy RF frame) a new start
static void cap_end(void)
{
    if (s_cap.n >= 4 && (!s_cap.rf || rf_clean())) {
        cap_done();
    } else {
        s_cap.n = 0;
        s_cap.shorts = 0;
    }
}

static void cap_put(int32_t v)
{
    const int32_t a = v < 0 ? -v : v;
    if (s_cap.rf && a < RF_SHORT_US) {
        s_cap.shorts++;
    }
    if (s_cap.rf && s_cap.n >= CAP_MAX) {
        // (a RF frame with no end: noise. Clean: the packet is long; else a new start)
        cap_end();
        if (s_cap.pin < 0) {
            return;
        }
    }
    if (s_cap.n > 0) {
        int32_t *last = &s_cap.d[s_cap.n - 1];
        if (a < GLITCH_US || (v > 0) == (*last > 0)) {
            *last += *last > 0 ? a : -a;
            return;
        }
    }
    if (a >= GLITCH_US && s_cap.n < CAP_MAX) {
        s_cap.d[s_cap.n++] = v;
    }
}

static void cap_tick(void)
{
    if (s_cap.pin < 0) {
        return;
    }
    int32_t idle = 0;
    bool act = false;
    const int n = s_io->rx_read(s_cap.pin, s_tmp, TC_VALS_MAX, &idle, &act);
    for (int i = 0; i < n && s_cap.pin >= 0; i++) {
        const int32_t v = s_tmp[i];
        if (s_cap.n == 0 && v < 0) {
            continue;  // (the quiet before the frame)
        }
        if (v < 0 && -v >= s_cap.gap_us) {
            cap_end();
            continue;
        }
        cap_put(v);
    }
    if (s_cap.pin < 0) {
        return;
    }
    if (s_cap.n > 0 && !act && idle >= s_cap.gap_us) {
        cap_end();
        if (s_cap.pin < 0) {
            return;
        }
    }
    if ((int32_t)(now() - s_cap.until) >= 0) {
        s_cap.pin = -1;
        ack(TC_NAK, s_cap.seq, TC_E_TIMEOUT, 0);
    }
}

static void on_capture(tc_msg_t *m)
{
    const int pin = (int)tc_get8(m);
    const int secs = (int)tc_get8(m);
    const int gap = (int)tc_get8(m);
    const tc_wire_t *w = wired(pin);
    if (!w || !tc_role_rx(w->role)) {
        ack(TC_NAK, m->seq, TC_E_NOT_WIRED, (uint32_t)pin);
        return;
    }
    if (s_cap.pin == pin) {
        ack(TC_ACK, m->seq, TC_E_OK, 0);  // (the master did not get the ACK: the capture goes on)
        return;
    }
    s_out.wait = false;  // (the master wants a new frame: the old one does not go again)
    int32_t idle;
    bool act;
    while (s_io->rx_read(pin, s_tmp, TC_VALS_MAX, &idle, &act) > 0) {
    }
    s_cap.pin = pin;
    s_cap.seq = m->seq;
    s_cap.n = 0;
    s_cap.shorts = 0;
    s_cap.rf = w->role != TC_R_IR_RX;
    s_cap.gap_us = (gap ? gap : 8) * 1000;
    s_cap.until = now() + (uint32_t)(secs ? secs : 10) * 1000;
    ack(TC_ACK, m->seq, TC_E_OK, 0);
}

// ---------------------------------------------------------------- the send
static int do_send(int pin, uint32_t carrier, bool inv, const int32_t *d, int n)
{
    if (s_io->led) {
        s_io->led(false);
    }
    const int r = s_io->tx_send(pin, carrier, inv, d, n);
    s_st.sends++;
    return r;
}

static void on_send_part(tc_msg_t *m)
{
    tc_part_t p;
    if (!tc_part_get(m, &p)) {
        ack(TC_NAK, m->seq, TC_E_BAD, 0);
        return;
    }
    if (s_done_on && p.id == s_done_id) {
        // (the master did not get the ACK and sends the parts again: no second send)
        if (p.part == p.nparts - 1) {
            ack(TC_ACK, m->seq, TC_E_OK, p.id);
        }
        return;
    }
    if (!s_asm.on || s_asm.id != p.id) {
        s_asm_t0 = now();
    }
    const int r = tc_asm_add(&s_asm, &p);
    if (r < 0) {
        ack(TC_NAK, m->seq, TC_E_BAD, 0);
        return;
    }
    if (r == 0) {
        return;
    }
    s_asm.on = false;
    const tc_part_t *h = &s_asm.head;
    const tc_wire_t *w = wired(h->pin);
    if (!w || !tc_role_tx(w->role)) {
        ack(TC_NAK, m->seq, TC_E_NOT_WIRED, h->pin);
        return;
    }
    uint32_t carrier = (uint32_t)h->carrier * 1000;
    if (w->role == TC_R_IR_TX && !carrier) {
        carrier = (uint32_t)(w->carrier_khz ? w->carrier_khz : 38) * 1000;
    }
    if (w->role != TC_R_IR_TX) {
        carrier = 0;
    }
    const int n = tc_levels_from_vals(s_asm.v, s_asm.total, s_tmp, TC_VALS_MAX);
    const int rep_at = h->rep_at < s_asm.total ? h->rep_at : 0;
    int first = n;
    if (rep_at > 0) {
        first = tc_levels_from_vals(s_asm.v, rep_at, s_tmp, TC_VALS_MAX);
    }
    const int e = do_send(h->pin, carrier, tx_inv(w), s_tmp, first);
    s_done_on = true;
    s_done_id = h->id;
    ack(e ? TC_NAK : TC_ACK, m->seq, e ? TC_E_SEND : TC_E_OK, h->id);
    s_rep.on = false;
    if (!e && h->rep_ms) {
        s_rep.on = true;
        s_rep.id = h->id;
        s_rep.pin = h->pin;
        s_rep.inv = tx_inv(w);
        s_rep.carrier = carrier;
        s_rep.rep_ms = h->rep_ms;
        s_rep.next = s_asm_t0 + h->rep_ms;
        if ((int32_t)(now() - s_rep.next) > 0) {
            s_rep.next = now() + 5;
        }
        s_rep.hold_until = now();  // (a repeat only while the HOLD of the master comes: a tap sends one press)
        s_rep.n = tc_levels_from_vals(s_asm.v + rep_at, s_asm.total - rep_at, s_rep.d, TC_VALS_MAX);
    }
}

static void parts_tick(void)
{
    if (s_asm.on && s_asm.got && now() - s_asm_t0 >= PARTS_WAIT_MS) {
        ack(TC_NAK, 0, TC_E_PARTS, (uint32_t)s_asm.id << 8 | s_asm.got);
        s_asm_t0 = now();
    }
}

static void rep_tick(void)
{
    if (!s_rep.on) {
        return;
    }
    const uint32_t t = now();
    if ((int32_t)(t - s_rep.hold_until) >= 0) {
        if (t - s_rep.hold_until > 3000) {
            s_rep.on = false;  // (no HOLD for a long time: the button is free)
        }
        return;
    }
    if ((int32_t)(t - s_rep.next) >= 0) {
        s_rep.next += s_rep.rep_ms;
        if ((int32_t)(t - s_rep.next) > 0) {
            s_rep.next = t + s_rep.rep_ms;
        }
        do_send(s_rep.pin, s_rep.carrier, s_rep.inv, s_rep.d, s_rep.n);
    }
}

static void on_pulse(tc_msg_t *m)
{
    const int pin = (int)tc_get8(m);
    int ms = (int)tc_get16(m);
    const tc_wire_t *w = wired(pin);
    if (!w || !tc_role_tx(w->role)) {
        ack(TC_NAK, m->seq, TC_E_NOT_WIRED, (uint32_t)pin);
        return;
    }
    if (ms <= 0 || ms > 2000) {
        ms = 300;
    }
    // a burst: 10 ms on, 10 ms off (an IR LED with its carrier; a transmitter: the data pin)
    int32_t d[200];
    int n = 0;
    for (int t = 0; t + 20 <= ms && n + 2 <= 200; t += 20) {
        d[n++] = 10000;
        d[n++] = -10000;
    }
    const uint32_t carrier = w->role == TC_R_IR_TX ? (uint32_t)(w->carrier_khz ? w->carrier_khz : 38) * 1000 : 0;
    // (the ACK first: the burst takes longer than the wait of the master, and a second PULSE would come)
    ack(TC_ACK, m->seq, TC_E_OK, 0);
    do_send(pin, carrier, tx_inv(w), d, n);
}

// ---------------------------------------------------------------- the switch and analog modules
int cc_states(tc_state_t *st, int max)
{
    int n = 0;
    for (int k = 0; k < s_nmod && n < max; k++) {
        st[n].pin = s_mod[k].pin;
        st[n].v = s_mod[k].v;
        st[n].cnt = s_mod[k].cnt;
        n++;
    }
    return n;
}

static void state_send(void)
{
    if (!s_nmod) {
        return;
    }
    tc_state_t st[TC_WIRE_MAX];
    const int n = cc_states(st, TC_WIRE_MAX);
    tc_buf_t f;
    begin(&f, TC_STATE, 0);
    tc_states_put(&f, st, n);
    send_master(&f);
    s_state_dirty = false;
    s_state_at = now();
    for (int k = 0; k < s_nmod; k++) {
        s_mod[k].sent = s_mod[k].v;
    }
}

static void out_put(int k, int v)
{
    s_mod[k].v = (uint16_t)(v ? 1 : 0);
    s_io->out_set(s_mod[k].pin, v ? s_mod[k].act : !s_mod[k].act);
    s_state_dirty = true;
}

bool cc_set(int pin, int value, int ms)
{
    for (int k = 0; k < s_nmod; k++) {
        if (s_mod[k].pin == pin && s_mod[k].cls == TC_C_OUT) {
            if (ms > 0) {
                // (a press again while one runs: a retry of the master after a lost ACK, or a second tap. The value
                // to go back to stays the value from before the first press)
                if (!s_mod[k].back_at) {
                    s_mod[k].back_v = s_mod[k].v;
                }
                s_mod[k].back_at = now() + (uint32_t)ms;
                if (!s_mod[k].back_at) {
                    s_mod[k].back_at = 1;
                }
            } else {
                s_mod[k].back_at = 0;
            }
            out_put(k, value);
            logf_("set GPIO %d %s%s", pin, value ? "ON" : "OFF", ms > 0 ? " (a press)" : "");
            return true;
        }
    }
    return false;
}

static void on_set(tc_msg_t *m)
{
    const int pin = (int)tc_get8(m);
    const int v = (int)tc_get16(m);
    int ms = (int)tc_get16(m);
    if (m->bad) {
        ack(TC_NAK, m->seq, TC_E_BAD, 0);
        return;
    }
    if (ms > 30000) {
        ms = 30000;
    }
    if (!cc_set(pin, v, ms)) {
        ack(TC_NAK, m->seq, TC_E_NOT_WIRED, (uint32_t)pin);
        return;
    }
    ack(TC_ACK, m->seq, TC_E_OK, v ? 1u : 0u);
    // (the TC_STATE goes in the next tick: a frame right after the ACK can come first, and then the master does
    // not take the ACK, because its counter is lower)
}

static void mod_tick(void)
{
    const uint32_t t = now();
    const bool adc = (int32_t)(t - s_adc_at) >= ADC_EVERY_MS;
    if (adc) {
        s_adc_at = t;
    }
    for (int k = 0; k < s_nmod; k++) {
        if (s_mod[k].cls == TC_C_OUT) {
            if (s_mod[k].back_at && (int32_t)(t - s_mod[k].back_at) >= 0) {
                s_mod[k].back_at = 0;
                out_put(k, s_mod[k].back_v);
            }
        } else if (s_mod[k].cls == TC_C_IN) {
            const uint16_t lv = s_io->in_read(s_mod[k].pin) == s_mod[k].act;
            if (lv == s_mod[k].v) {
                s_mod[k].same = 0;
            } else if (++s_mod[k].same >= DEBOUNCE_TICKS) {
                s_mod[k].same = 0;
                s_mod[k].v = lv;
                if (lv) {
                    s_mod[k].cnt++;
                }
                s_state_dirty = true;
            }
        } else if (adc && s_io->adc_read) {
            const int mv = s_io->adc_read(s_mod[k].pin);
            if (mv >= 0) {
                s_mod[k].v = (uint16_t)mv;
                const int d = (int)s_mod[k].v - (int)s_mod[k].sent;
                if (d >= ADC_STEP_MV || d <= -ADC_STEP_MV) {
                    s_state_dirty = true;
                }
            }
        }
    }
    // (only to a master that is here: a companion that searches keeps the change for the next PONG)
    if (s_state_dirty && s_sv.paired && !s_search && (int32_t)(t - s_state_at) >= STATE_GAP_MS) {
        state_send();
    }
}

// ---------------------------------------------------------------- the update by radio
static void ota_stop(void)
{
    if (s_ota.on && s_io->ota_abort) {
        s_io->ota_abort();
    }
    s_ota.on = false;
}

static void on_ota(tc_msg_t *m)
{
    if (!s_io->ota_begin) {
        ack(TC_NAK, m->seq, TC_E_OTA, TC_OTA_F_NO);
        return;
    }
    if (m->type == TC_OTA_BEGIN) {
        const uint32_t size = tc_get32(m), crc = tc_get32(m);
        const int fw = (int)tc_get16(m);
        ota_stop();
        s_watch.pin = -1;
        s_cap.pin = -1;
        s_rep.on = false;
        if (m->bad || s_io->ota_begin(size) != 0) {
            ack(TC_NAK, m->seq, TC_E_OTA, TC_OTA_F_BEGIN);
            return;
        }
        s_ota.on = true;
        s_ota.done = false;
        s_ota.size = size;
        s_ota.crc = crc;
        s_ota.next = 0;
        s_ota.last_ms = now();
        s_cur_crc = 0;
        logf_("update: %lu bytes, firmware %d", (unsigned long)size, fw);
        ack(TC_ACK, m->seq, TC_E_OK, 0);
        return;
    }
    if (!s_ota.on) {
        if (s_ota.done) {
            ack(TC_ACK, m->seq, TC_E_OK, TC_OTA_DONE);  // (a late frame after a good END: the update is done)
        } else {
            ack(TC_NAK, m->seq, TC_E_OTA, TC_OTA_F_STATE);
        }
        return;
    }
    s_ota.last_ms = now();
    if (m->type == TC_OTA_DATA) {
        const uint32_t off = tc_get32(m);
        const int n = (int)tc_get8(m);
        uint8_t d[TC_OTA_CHUNK];
        if (!m->bad && n > 0 && n <= TC_OTA_CHUNK && tc_getn(m, d, n) && off == s_ota.next &&
            off + (uint32_t)n <= s_ota.size) {
            if (s_io->ota_write(d, n) != 0) {
                ota_stop();
                ack(TC_NAK, m->seq, TC_E_OTA, TC_OTA_F_WRITE);
                return;
            }
            s_cur_crc = tc_crc32(s_cur_crc, d, n);
            s_ota.next += (uint32_t)n;
        }
        ack(TC_ACK, m->seq, TC_E_OK, s_ota.next);  // (a frame out of order: the offset that the companion wants)
        return;
    }
    // END
    int why = TC_OTA_F_NONE;
    if (s_ota.next != s_ota.size) {
        why = TC_OTA_F_SIZE;
    } else if (s_cur_crc != s_ota.crc) {
        why = TC_OTA_F_CRC;
    } else if (s_io->ota_end() != 0) {
        why = TC_OTA_F_IMAGE;
        s_ota.on = false;  // (ota_end closed the slot)
    }
    if (why) {
        ota_stop();
        logf_("update: failed (%d)", why);
        ack(TC_NAK, m->seq, TC_E_OTA, (uint32_t)why);
        return;
    }
    s_ota.on = false;
    s_ota.done = true;
    logf_("update: the new firmware is ready; a restart");
    ack(TC_ACK, m->seq, TC_E_OK, TC_OTA_DONE);
    s_ota.restart_at = now() + 400;  // (the ACK goes out first)
    if (!s_ota.restart_at) {
        s_ota.restart_at = 1;
    }
}

static void ota_tick(void)
{
    const uint32_t t = now();
    if (s_ota.on && t - s_ota.last_ms > OTA_IDLE_MS) {
        logf_("update: no data for %d s: it stops", OTA_IDLE_MS / 1000);
        ota_stop();
    }
    if (s_ota.restart_at && (int32_t)(t - s_ota.restart_at) >= 0 && s_io->restart) {
        s_ota.restart_at = 0;
        s_io->restart();
    }
}

// ---------------------------------------------------------------- the pairing
static void pair_ok_send(void)
{
    uint8_t pub[32];
    tc_x25519_base(pub, s_sv.my_priv);
    tc_buf_t f;
    begin(&f, TC_PAIR_OK, 0);
    tc_putn(&f, pub, 32);
    send_master(&f);
}

static void on_pair_req(const uint8_t mac[6], tc_msg_t *m)
{
    uint8_t pub[32];
    if (!tc_getn(m, pub, 32)) {
        return;
    }
    if (s_sv.paired && !memcmp(mac, s_sv.master, 6) && !memcmp(pub, s_sv.master_pub, 32)) {
        pair_ok_send();  // (the master did not get the answer: the same keys)
        return;
    }
    if (s_search) {
        s_search = false;  // (stay on this channel for the pairing)
    }
    s_sv.home_ch = (uint8_t)s_ch;
    if (s_pair.on && !memcmp(s_pair.mac, mac, 6)) {
        if (now() - s_hello_at >= 500) {
            hello();  // (the master sees that this companion waits for the BOOT key)
        }
        return;
    }
    memset(&s_pair, 0, sizeof s_pair);
    s_pair.on = true;
    s_pair.until = now() + PAIR_MS;
    memcpy(s_pair.mac, mac, 6);
    memcpy(s_pair.pub, pub, 32);
    int k = tc_left(m);
    if (k > 16) {
        k = 16;
    }
    tc_getn(m, s_pair.name, k);
    logf_("pairing: %02X:%02X:%02X:%02X:%02X:%02X asks; press BOOT", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    hello();
}

void cc_boot_press(void)
{
    if (!s_pair.on) {
        return;
    }
    s_pair.on = false;
    s_io->random(s_sv.my_priv, 32);
    uint8_t pub[32], shared[32];
    tc_x25519_base(pub, s_sv.my_priv);
    tc_x25519(shared, s_sv.my_priv, s_pair.pub);
    tc_pair_key(s_sv.key, shared, s_pair.pub, pub);
    memset(shared, 0, sizeof shared);
    s_sv.paired = 1;
    memcpy(s_sv.master, s_pair.mac, 6);
    memcpy(s_sv.master_pub, s_pair.pub, 32);
    memcpy(s_sv.mname, s_pair.name, sizeof s_sv.mname);
    s_sv.mname[16] = 0;
    s_sv.rx_hi = CC_HI_STEP;
    s_rx_floor = 0;
    s_tx_ctr = s_sv.tx_hi;
    s_sv.tx_hi = s_tx_ctr + CC_HI_STEP;
    s_sv.home_ch = (uint8_t)s_ch;
    save();
    s_master_ms = now();
    s_search = false;
    logf_("paired with %s", s_sv.mname[0] ? s_sv.mname : "the master");
    pair_ok_send();
}

void cc_forget(void)
{
    s_sv.paired = 0;
    memset(s_sv.master, 0, 6);
    memset(s_sv.key, 0, sizeof s_sv.key);
    memset(s_sv.master_pub, 0, 32);
    save();
    s_watch.pin = -1;
    s_cap.pin = -1;
    s_rep.on = false;
    s_search = true;
    logf_("the master is forgotten");
}

void cc_boot_hold(void)
{
    s_pair.on = false;
    cc_forget();
}

// ---------------------------------------------------------------- the frames that come in
static void on_master(tc_msg_t *m)
{
    switch (m->type) {
    case TC_INFO_REQ:
        info();
        break;
    case TC_PING: {
        tc_buf_t f;
        begin(&f, TC_PONG, 0);
        tc_put32(&f, now() / 1000);
        tc_put32(&f, tc_wire_hash(s_sv.wire, s_sv.nwire));
        send_master(&f);
        s_state_dirty = true;  // (all the values after each PONG, in the next tick: a lost TC_STATE costs one ping)
        break;
    }
    case TC_SET:
        on_set(m);
        break;
    case TC_OTA_BEGIN:
    case TC_OTA_DATA:
    case TC_OTA_END:
        on_ota(m);
        break;
    case TC_CONFIG:
        on_config(m);
        break;
    case TC_WATCH: {
        const int pin = (int)tc_get8(m);
        const bool on = tc_get8(m);
        const tc_wire_t *w = wired(pin);
        if (on && (!w || !tc_role_rx(w->role))) {
            ack(TC_NAK, m->seq, TC_E_NOT_WIRED, (uint32_t)pin);
            break;
        }
        s_watch.pin = on ? pin : -1;
        s_watch.until = now() + WATCH_MAX_MS;
        s_watch.next = now();
        s_watch.edges = 0;
        ack(TC_ACK, m->seq, TC_E_OK, 0);
        break;
    }
    case TC_PULSE:
        on_pulse(m);
        break;
    case TC_CAPTURE:
        on_capture(m);
        break;
    case TC_SEND:
        on_send_part(m);
        break;
    case TC_HOLD:
        if (s_rep.on && s_rep.id == (uint8_t)tc_get8(m)) {
            s_rep.hold_until = now() + HOLD_MS;
        }
        break;
    case TC_STOP:
        s_rep.on = false;
        break;
    case TC_ACK: {
        tc_get16(m);
        tc_get8(m);
        if ((tc_get32(m) & 0xFF) == s_out.id) {
            s_out.wait = false;  // (the master has the whole frame)
        }
        break;
    }
    case TC_NAK: {
        const uint32_t seq = tc_get16(m);
        const int code = (int)tc_get8(m);
        const uint32_t arg = tc_get32(m);
        (void)seq;
        if (code == TC_E_PARTS && (arg >> 8 & 0xFF) == s_out.id) {
            s_out.next = now() + FRAME_AGAIN_MS;
            for (int k = 0; k < 8; k++) {
                if (!(arg & (1u << k)) && k * TC_PART_VALS < s_out.n + (k == 0)) {
                    frame_part_send(k);
                }
            }
        }
        break;
    }
    case TC_FORGET:
        ack(TC_ACK, m->seq, TC_E_OK, 0);
        cc_forget();
        break;
    default:
        break;
    }
}

void cc_rx(const uint8_t mac[6], const uint8_t *d, int n)
{
    tc_msg_t m;
    if (!tc_parse(d, n, &m)) {
        return;
    }
    s_st.rx_frames++;
    if (m.type == TC_PAIR_REQ && !(m.flags & TC_FL_TAG)) {
        follow(m.ch);
        on_pair_req(mac, &m);
        return;
    }
    if (!(m.flags & TC_FL_TAG) || !s_sv.paired || memcmp(mac, s_sv.master, 6)) {
        return;
    }
    if (!tc_check(d, n, s_sv.key, mac)) {
        s_st.rx_bad_tag++;
        return;
    }
    s_master_ms = now();
    follow(m.ch);
    if (s_search) {
        s_search = false;
        logf_("the master is on channel %d", s_ch);
    }
    if (s_sv.home_ch != s_ch) {
        s_sv.home_ch = (uint8_t)s_ch;
        save();
    }
    if (m.counter <= s_rx_floor) {
        s_st.rx_old++;
        ack(TC_NAK, m.seq, TC_E_COUNTER, s_rx_floor);
        return;
    }
    s_rx_floor = m.counter;
    if (!s_valid_told && s_io->ota_valid) {
        s_valid_told = true;
        s_io->ota_valid();  // (a good frame of the master: this firmware works; no rollback at the next start)
    }
    if (s_rx_floor >= s_sv.rx_hi) {
        s_sv.rx_hi = s_rx_floor + CC_HI_STEP;
        save();
    }
    on_master(&m);
}

// ---------------------------------------------------------------- the clock
void cc_tick(void)
{
    const uint32_t t = now();
    if (s_pair.on && (int32_t)(t - s_pair.until) >= 0) {
        s_pair.on = false;
        logf_("pairing: no BOOT press in time");
    }
    if (!s_pair.on && !s_search && (!s_sv.paired || (s_master_ms && t - s_master_ms >= SILENT_MS))) {
        s_search = true;
        s_hop_at = t - HOP_MS;
    }
    if (s_search) {
        hop();
    }
    watch_tick();
    cap_tick();
    frame_tick();
    parts_tick();
    rep_tick();
    mod_tick();
    ota_tick();
    if (s_io->led) {
        bool on;
        if (s_pair.on) {
            on = (t / 100) & 1;  // fast: press BOOT
        } else if (!s_sv.paired) {
            on = (t / 700) & 1;  // slow: no master
        } else {
            on = !s_search || ((t / 1500) & 1) == 0;  // on: the master is here
        }
        s_io->led(on && !s_rep.on);
    }
}

void cc_init(const cc_io_t *io, const cc_saved_t *saved, const tc_info_t *chip, const uint8_t my_mac[6])
{
    s_io = io;
    memcpy(s_me, my_mac, 6);
    s_chip = *chip;
    memset(&s_sv, 0, sizeof s_sv);
    if (saved && saved->magic == CC_SAVED_MAGIC) {
        s_sv = *saved;
    }
    if (s_sv.nwire > TC_WIRE_MAX) {
        s_sv.nwire = 0;
    }
    if (!s_sv.home_ch || s_sv.home_ch > NCH) {
        s_sv.home_ch = 1;
    }
    // the counters: all the old ones are below the saved marks
    s_rx_floor = s_sv.rx_hi;
    s_sv.rx_hi = s_rx_floor + CC_HI_STEP;
    s_tx_ctr = s_sv.tx_hi;
    s_sv.tx_hi = s_tx_ctr + CC_HI_STEP;
    save();
    s_ch = 0;
    set_ch(s_sv.home_ch);
    s_search = true;
    s_hop_at = now() - HOP_MS;
    s_master_ms = 0;
    pins_open();
    logf_("companion: %s, %s", tc_chip_name(s_chip.chip), s_sv.paired ? "paired" : "not paired");
}

void cc_state(cc_state_t *s)
{
    s_st.paired = s_sv.paired;
    s_st.searching = s_search;
    s_st.pair_wait = s_pair.on;
    s_st.capturing = s_cap.pin >= 0;
    s_st.repeating = s_rep.on;
    s_st.ch = s_ch;
    s_st.last_master_ms = s_master_ms;
    *s = s_st;
}

const cc_saved_t *cc_saved(void)
{
    return &s_sv;
}
