// BLE-MIDI 1.0 on the NimBLE stack of the gamepad (see ble_midi.h). The packet code (bm_*)
// is plain C, so the host tests use it too; the GATT and GAP part is for the ESP build.

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "ble_midi.h"

#define Q_MAX      96   // queued messages
#define Q_COALESCE 64   // above this, a new value takes the place of the older one
#define PKT_MAX    244  // the largest packet (an MTU of 247)
#define FLUSH_PKTS 6    // packets in one flush (the stack has a few buffers)

// 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 and 7772E5DB-3868-4112-A1A9-F2669D106BF3, little-endian
const uint8_t BLE_MIDI_SVC_UUID128[16] = {
        0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03,
};
__attribute__((unused)) static const uint8_t CHR_UUID128[16] = {
        0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77,
};

static bm_msg_t s_q[Q_MAX];
static int s_qn;
static uint32_t s_sent, s_drop, s_rx;
static volatile bool s_open;

// the MIDI clock from the other side
static volatile int64_t s_clk_last_us;
static volatile float s_clk_tick_us;  // filtered time between clock ticks (24 a beat)
static volatile uint32_t s_starts;

// ---------------------------------------------------------------- the packet code

int bm_msg_len(uint8_t st)
{
    if (st < 0x80) {
        return 1;
    }
    if (st < 0xC0 || (st >= 0xE0 && st < 0xF0)) {
        return 3;
    }
    if (st < 0xE0) {
        return 2;
    }
    switch (st) {
    case 0xF0:
        return 0;  // SysEx: until 0xF7
    case 0xF1:
    case 0xF3:
        return 2;
    case 0xF2:
        return 3;
    default:
        return 1;
    }
}

int bm_pack(const bm_msg_t *m, int n, uint8_t *out, int cap, int *used)
{
    *used = 0;
    if (n <= 0 || cap < 2 + m[0].len) {
        return 0;
    }
    uint16_t ts = (uint16_t)(m[0].t_ms & 0x1FFF);
    int hi = ts >> 7, low_prev = -1, k = 0;
    out[k++] = (uint8_t)(0x80 | hi);
    for (int i = 0; i < n; i++) {
        uint16_t t = (uint16_t)(m[i].t_ms & 0x1FFF);
        int th = t >> 7, tl = t & 0x7F;
        if (i > 0) {
            // The receiver adds one to the high bits when the low bits go down. So a message
            // goes into this packet only if its time follows from that rule.
            bool same = th == hi && tl >= low_prev;
            bool next = th == ((hi + 1) & 0x3F) && tl < low_prev;
            if (!same && !next) {
                break;
            }
            hi = th;
        }
        if (k + 1 + m[i].len > cap) {
            break;
        }
        out[k++] = (uint8_t)(0x80 | tl);
        for (int j = 0; j < m[i].len; j++) {
            out[k++] = m[i].b[j];
        }
        low_prev = tl;
        (*used)++;
    }
    return *used ? k : 0;
}

int bm_parse(const uint8_t *p, int len, bm_rx_fn fn, void *arg)
{
    if (len < 2 || (p[0] & 0xC0) != 0x80) {
        return -1;
    }
    int hi = p[0] & 0x3F, low_prev = -1, i = 1, n = 0;
    uint8_t running = 0;
    uint16_t ts = 0;
    while (i < len && !(p[i] & 0x80)) {
        i++;  // the end of a SysEx from the last packet
    }
    while (i < len) {
        if (p[i] & 0x80) {
            // a timestamp byte
            int low = p[i] & 0x7F;
            if (low_prev >= 0 && low < low_prev) {
                hi = (hi + 1) & 0x3F;
            }
            low_prev = low;
            ts = (uint16_t)((hi << 7) | low);
            i++;
            if (i >= len) {
                break;
            }
        }
        uint8_t st;
        if (p[i] & 0x80) {
            st = p[i];
            if (st == 0xF0) {
                // SysEx: skip to the timestamp byte before 0xF7 (or the end of the packet)
                int j = i + 1;
                while (j < len && !(p[j] & 0x80)) {
                    j++;
                }
                i = (j + 1 < len && p[j + 1] == 0xF7) ? j + 2 : len;
                continue;
            }
            if (st >= 0xF8) {  // realtime: one byte, the running status stays
                if (fn) {
                    fn(&st, 1, ts, arg);
                }
                n++;
                i++;
                continue;
            }
            i++;
            running = st < 0xF0 ? st : 0;
        } else {
            if (!running) {  // a data byte with no status: skip it
                i++;
                continue;
            }
            st = running;
        }
        int need = bm_msg_len(st) - 1;
        if (need < 0 || i + need > len) {
            break;
        }
        uint8_t msg[3] = {st, 0, 0};
        for (int j = 0; j < need; j++) {
            if (p[i + j] & 0x80) {
                return n;  // a bad message
            }
            msg[1 + j] = p[i + j];
        }
        if (fn) {
            fn(msg, need + 1, ts, arg);
        }
        n++;
        i += need;
    }
    return n;
}

// ---------------------------------------------------------------- the queue

static uint32_t key_of(const bm_msg_t *m)
{
    uint8_t st = m->b[0];
    if ((st & 0xF0) == 0xB0) {
        return (uint32_t)st << 8 | m->b[1];  // a controller: by number
    }
    return (uint32_t)st << 8;
}

bool ble_midi_send(const uint8_t *msg, int len, uint32_t t_ms)
{
    if (len < 1 || len > 3) {
        return false;
    }
    bm_msg_t m = {.t_ms = t_ms, .len = (uint8_t)len};
    memcpy(m.b, msg, len);
    if (s_qn >= Q_COALESCE) {
        uint32_t k = key_of(&m);
        for (int i = s_qn - 1; i >= 0; i--) {
            if (key_of(&s_q[i]) == k) {
                // the newest value wins; the time stays in order
                uint32_t t = s_q[i].t_ms;
                s_q[i] = m;
                s_q[i].t_ms = t;
                return true;
            }
        }
    }
    if (s_qn >= Q_MAX) {
        s_drop++;
        return false;
    }
    s_q[s_qn++] = m;
    return true;
}

uint32_t ble_midi_sent(void)
{
    return s_sent;
}

uint32_t ble_midi_dropped(void)
{
    return s_drop;
}

uint32_t ble_midi_received(void)
{
    return s_rx;
}

float ble_midi_clock_bpm(void)
{
    int64_t now = esp_timer_get_time();
    if (!s_clk_last_us || now - s_clk_last_us > 1000000 || s_clk_tick_us <= 0) {
        return 0;
    }
    return 60e6f / (s_clk_tick_us * 24.0f);
}

uint32_t ble_midi_starts(void)
{
    return s_starts;
}

static void on_rx(const uint8_t *msg, int len, uint16_t ts, void *arg)
{
    (void)len;
    (void)ts;
    (void)arg;
    s_rx++;
    if (msg[0] == 0xF8) {
        int64_t now = esp_timer_get_time();
        int64_t d = now - s_clk_last_us;
        if (s_clk_last_us && d > 2000 && d < 200000) {
            s_clk_tick_us = s_clk_tick_us > 0 ? s_clk_tick_us + 0.1f * ((float)d - s_clk_tick_us) : (float)d;
        }
        s_clk_last_us = now;
    } else if (msg[0] == 0xFA || msg[0] == 0xFB) {
        s_starts++;
    }
}

// the messages in time order (lanes add them in turn; the packets need the time order)
static void sort_queue(void)
{
    for (int i = 1; i < s_qn; i++) {
        bm_msg_t m = s_q[i];
        int j = i - 1;
        while (j >= 0 && (int32_t)(s_q[j].t_ms - m.t_ms) > 0) {
            s_q[j + 1] = s_q[j];
            j--;
        }
        s_q[j + 1] = m;
    }
}

static void q_remove(int n)
{
    memmove(s_q, s_q + n, (size_t)(s_qn - n) * sizeof(bm_msg_t));
    s_qn -= n;
}

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(ESP_PLATFORM) && CONFIG_BT_ENABLED
// ================================================================ the ESP part

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "esp_log.h"
#include "bt_pad.h"

static const char *TAG = "blemidi";

static const ble_uuid128_t SVC_UUID = {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03}};
static ble_uuid128_t s_chr_uuid = {.u = {.type = BLE_UUID_TYPE_128}};

static uint16_t s_val_handle;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_notify;
static volatile int s_mtu = 23;
static uint8_t s_peer[6];

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;  // the spec: a read gives no data
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[PKT_MAX + 8];
        uint16_t n = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &n) == 0) {
            bm_parse(buf, n, on_rx, NULL);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static struct ble_gatt_chr_def s_chrs[2];
static struct ble_gatt_svc_def s_svcs[2];

int ble_midi_register(void)
{
    memcpy(s_chr_uuid.value, CHR_UUID128, 16);
    memset(s_chrs, 0, sizeof(s_chrs));
    memset(s_svcs, 0, sizeof(s_svcs));
    s_chrs[0].uuid = &s_chr_uuid.u;
    s_chrs[0].access_cb = chr_access;
    s_chrs[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY;
    s_chrs[0].val_handle = &s_val_handle;
    s_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s_svcs[0].uuid = &SVC_UUID.u;
    s_svcs[0].characteristics = s_chrs;
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_svcs);
    }
    if (rc) {
        ESP_LOGE(TAG, "the MIDI service was not added: %d", rc);
    }
    return rc;
}

void ble_midi_gap_event(struct ble_gap_event *ev)
{
    struct ble_gap_conn_desc desc;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            break;
        }
        s_conn = ev->connect.conn_handle;
        s_notify = false;
        s_mtu = 23;
        if (ble_gap_conn_find(s_conn, &desc) == 0) {
            memcpy(s_peer, desc.peer_id_addr.val, 6);
        }
        if (s_open) {
            // a short connection interval: 7.5 .. 15 ms (a fast controller)
            struct ble_gap_upd_params p = {
                    .itvl_min = 6,
                    .itvl_max = 12,
                    .latency = 0,
                    .supervision_timeout = 300,
            };
            ble_gap_update_params(s_conn, &p);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_notify = false;
        s_mtu = 23;
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_val_handle) {
            s_notify = ev->subscribe.cur_notify;
            ESP_LOGI(TAG, "notify %s", s_notify ? "on" : "off");
        }
        break;
    case BLE_GAP_EVENT_MTU:
        s_mtu = ev->mtu.value;
        break;
    default:
        break;
    }
}

void ble_midi_open(void)
{
    s_open = true;
    bt_pad_start();
    bt_pad_adv_midi(true);
}

void ble_midi_close(void)
{
    s_open = false;
    s_qn = 0;
    bt_pad_adv_midi(false);
}

bm_state_t ble_midi_state(void)
{
    if (!s_open) {
        return BM_OFF;
    }
    pad_state_t ps = bt_pad_state();
    if (ps == PAD_FAILED) {
        return BM_FAILED;
    }
    if (ps == PAD_OFF || ps == PAD_STARTING) {
        return BM_STARTING;
    }
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        return BM_ADVERTISING;
    }
    return s_notify ? BM_LINKED : BM_CONNECTED;
}

const char *ble_midi_name(void)
{
    return bt_pad_midi_name();
}

void ble_midi_peer(char *buf, int len)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        if (len > 0) {
            buf[0] = 0;
        }
        return;
    }
    snprintf(buf, len, "%02X:%02X:%02X", s_peer[2], s_peer[1], s_peer[0]);
}

int ble_midi_mtu(void)
{
    return s_mtu;
}

void ble_midi_disconnect(void)
{
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void ble_midi_flush(void)
{
    if (!s_qn) {
        return;
    }
    if (ble_midi_state() != BM_LINKED) {
        s_drop += s_qn;
        s_qn = 0;
        return;
    }
    sort_queue();
    int cap = s_mtu - 3;
    if (cap > PKT_MAX) {
        cap = PKT_MAX;
    }
    if (cap < 20) {
        cap = 20;
    }
    for (int pk = 0; pk < FLUSH_PKTS && s_qn > 0; pk++) {
        uint8_t buf[PKT_MAX];
        int used = 0;
        int n = bm_pack(s_q, s_qn, buf, cap, &used);
        if (!n) {
            q_remove(1);  // cannot happen: a message always fits
            s_drop++;
            continue;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, (uint16_t)n);
        if (!om) {
            break;  // no buffer now: the rest waits
        }
        if (ble_gatts_notify_custom(s_conn, s_val_handle, om) != 0) {
            break;  // om is used up
        }
        s_sent += used;
        q_remove(used);
    }
}

#else
// ================================================================ the host part (tests), and a board with no Bluetooth

void (*bm_host_sink)(const uint8_t *pkt, int len);
bool bm_host_linked;
int bm_host_mtu = 23;

void bm_host_receive(const uint8_t *pkt, int len)
{
    bm_parse(pkt, len, on_rx, NULL);
}

int ble_midi_register(void)
{
    return 0;
}

void ble_midi_gap_event(struct ble_gap_event *ev)
{
    (void)ev;
}

void ble_midi_open(void)
{
    s_open = true;
}

void ble_midi_close(void)
{
    s_open = false;
    s_qn = 0;
}

bm_state_t ble_midi_state(void)
{
    if (!s_open) {
        return BM_OFF;
    }
    return bm_host_linked ? BM_LINKED : BM_ADVERTISING;
}

const char *ble_midi_name(void)
{
    return "TFN MIDI";
}

void ble_midi_peer(char *buf, int len)
{
    snprintf(buf, len, "%s", bm_host_linked ? "12:34:56" : "");
}

int ble_midi_mtu(void)
{
    return bm_host_linked ? bm_host_mtu : 23;
}

void ble_midi_disconnect(void)
{
    bm_host_linked = false;
}

void ble_midi_flush(void)
{
    if (!s_qn) {
        return;
    }
    if (ble_midi_state() != BM_LINKED) {
        s_drop += s_qn;
        s_qn = 0;
        return;
    }
    sort_queue();
    int cap = bm_host_mtu - 3;
    if (cap > PKT_MAX) {
        cap = PKT_MAX;
    }
    if (cap < 20) {
        cap = 20;
    }
    for (int pk = 0; pk < FLUSH_PKTS && s_qn > 0; pk++) {
        uint8_t buf[PKT_MAX];
        int used = 0;
        int n = bm_pack(s_q, s_qn, buf, cap, &used);
        if (!n) {
            q_remove(1);
            s_drop++;
            continue;
        }
        if (bm_host_sink) {
            bm_host_sink(buf, n);
        }
        s_sent += used;
        q_remove(used);
    }
}
#endif
