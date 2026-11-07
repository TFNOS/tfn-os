// MQTT: THE SCREENS (included by app_mqtt.c; they read its state).
// The task: the render and input hooks of the app task.
// The memory: the file statics of app_mqtt.c; the touch targets of each frame go into s_hit[], and the next tap uses
// them: the drawing and the touches have one layout.
//
// The screens:
//   START    the two modes (CLIENT, BROKER)
//   SERVERS  the brokers: 3 public test brokers, the address of the board page, TYPE AN ADDRESS
//   MAIN     CLIENT: the state line, the topics (or the stream), VIEW / SUBSCRIBE / PUBLISH
//            BROKER: its address (a big card while no client is there), the clients, the traffic, PUBLISH
//   TOPIC    one topic: its whole last message (text, JSON with indents, or hex), a chart of its number values
//   PUB      the topic, the message, QoS, RETAIN, SEND
//   SUBS     the subscriptions (X removes one), ADD (the keyboard), the common filters

#define C_BG    rgb(7, 10, 16)
#define C_PANEL rgb(15, 21, 32)
#define C_ROW   rgb(12, 17, 26)
#define C_LINE  rgb(30, 40, 56)
#define C_TEXT  rgb(228, 234, 242)
#define C_DIM   rgb(120, 134, 154)
#define C_FAINT rgb(70, 82, 100)
#define C_CLI   rgb(64, 214, 196)  // the colour of the client
#define C_BRK   rgb(255, 178, 64)  // the colour of the broker
#define C_OK    rgb(70, 220, 120)
#define C_WARN  rgb(255, 160, 60)
#define C_BAD   rgb(255, 86, 80)
#define C_BTN   rgb(26, 36, 52)
#define C_BTN_E rgb(44, 58, 80)
#define C_NUM   rgb(140, 200, 255)
#define C_KEY   rgb(150, 210, 255)  // the keys of a JSON text
#define C_STR   rgb(200, 230, 160)

static uint16_t accent(void)
{
    return s_mode == 1 ? C_BRK : C_CLI;
}

// ---------------------------------------------------------------- the layout
static int s_W, s_H, s_top_h, s_pane_x;
static int s_list_y0, s_list_y1;  // the list area (y)
static int s_list_x1;             // the right end of the list area
static int s_row_h;
static bool s_narrow;  // the 320 x 240 frame: one pane, a bar of buttons at the bottom

static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    s_narrow = s_W < 480;
    s_top_h = BAR ? 26 : 30;
    s_list_y0 = s_top_h + 2;
    s_list_y1 = s_narrow ? s_H - 46 : s_H;
    s_list_x1 = s_narrow ? s_W : s_W - 104;
    s_row_h = s_narrow ? 40 : 29;
    s_pane_x = s_W / 2;
}

// ---------------------------------------------------------------- the touch targets
enum {
    H_START,
    H_SRV,
    H_VIEW,
    H_SUBS,
    H_PUB,
    H_ROW,
    H_SROW,
    H_BACK,
    H_TO_TOPIC,
    H_PUB_TOPIC,
    H_PUB_MSG,
    H_PUB_QOS,
    H_PUB_RET,
    H_PUB_SEND,
    H_SUB_ADD,
    H_SUB_X,
    H_SUB_COMMON,
    H_RETRY,
    H_BVIEW,
    H_SRV_LIST
};
typedef struct {
    int16_t x, y, w, h, id, arg;
} hit_t;
#define HITN 64
static hit_t s_hit[HITN];
static int s_nhit;
static void hit(int x, int y, int w, int h, int id, int arg)
{
    if (s_nhit < HITN) {
        s_hit[s_nhit++] = (hit_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)id, (int16_t)arg};
    }
}
// a target cut to the band y0..y1 (a row that the list scrolled half out)
static void hit_in(int x, int y, int w, int h, int id, int arg, int y0, int y1)
{
    const int a = y < y0 ? y0 : y, b = y + h > y1 ? y1 : y + h;
    if (b - a >= 26) {
        hit(x, a, w, b - a, id, arg);  // (a thin slice is no target)
    }
}
// the last target that has the point (a slop of 4 px round each)
static int hit_at(int x, int y)
{
    for (int i = s_nhit - 1; i >= 0; i--) {
        const hit_t *h = &s_hit[i];
        if (x >= h->x - 4 && x < h->x + h->w + 4 && y >= h->y - 4 && y < h->y + h->h + 4) {
            return i;
        }
    }
    return -1;
}

static const char *const COMMON[] = {"#",         "$SYS/#",    "home/#", "tfn/#", "zigbee2mqtt/#", "homeassistant/#",
                                     "tasmota/#", "shellies/#"};
#define NCOMMON ((int)(sizeof COMMON / sizeof COMMON[0]))

static void open_topic(int slot)
{
    if (slot < 0 || slot >= TOPN || !s_top[slot].used) {
        return;
    }
    s_sel = slot;
    s_dscroll = 0;
    s_scr = SC_TOPIC;
}

static void hit_do(int i)
{
    const hit_t *h = &s_hit[i];
    switch (h->id) {
    case H_START:
        set_mode(h->arg);
        break;
    case H_SRV:  // (SRV_PAGE + 1 = TYPE AN ADDRESS)
        if (h->arg > SRV_PAGE || (h->arg == SRV_PAGE && !s_acct.host[0])) {
            kbd_go(KB_HOST, SC_SERVERS);
        } else {
            if (s_mode != 0) {
                s_mode = 0, store_set("mqtt_mode", 0);
            }
            set_server(h->arg);
        }
        break;
    case H_SRV_LIST:
        s_scr = SC_SERVERS;
        break;
    case H_VIEW:
        s_view ^= 1;
        s_scroll = 0;
        break;
    case H_BVIEW:
        s_view ^= 1;
        s_scroll = 0;
        break;
    case H_SUBS:
        s_scr = SC_SUBS;
        s_scroll = 0;
        break;
    case H_PUB:
        s_scr = SC_PUB;
        break;
    case H_ROW:
        open_topic(h->arg);
        break;
    case H_SROW: {
        const sent_t *e = &s_str[(uint32_t)h->arg % STRN];
        if (e->kind == SK_MSG) {
            open_topic(top_find(e->topic));
        }
        break;
    }
    case H_BACK:
        if (s_scr == SC_SERVERS && s_mode == 0 && !server_ok()) {
            s_scr = SC_START;
        } else {
            s_scr = SC_MAIN;
        }
        s_scroll = 0;
        break;
    case H_TO_TOPIC:
        if (s_sel >= 0) {
            snprintf(s_pub_topic, sizeof s_pub_topic, "%s", s_top[s_sel].topic);
        }
        s_scr = SC_PUB;
        break;
    case H_PUB_TOPIC:
        kbd_go(KB_TOPIC, SC_PUB);
        break;
    case H_PUB_MSG:
        kbd_go(KB_MSG, SC_PUB);
        break;
    case H_PUB_QOS:
        s_pub_qos = h->arg;
        break;
    case H_PUB_RET:
        s_pub_ret = !s_pub_ret;
        break;
    case H_PUB_SEND:
        publish_now();
        break;
    case H_SUB_ADD:
        kbd_go(KB_FILTER, SC_SUBS);
        break;
    case H_SUB_X:
        sub_remove(h->arg);
        break;
    case H_SUB_COMMON:
        sub_add(COMMON[h->arg]);
        break;
    case H_RETRY:
        apply();
        break;
    }
}

// ---------------------------------------------------------------- small parts

static void txt_l(const kfont_t *f, int x, int base, const char *s, uint16_t c, int w)
{
    tfd_text_l(f, x, base, s, c, w);
}
// a text at the start side (the right end in Hebrew) of the box x0..x1
static void txt_s(const kfont_t *f, int x0, int x1, int base, const char *s, uint16_t c)
{
    tfd_text_s(f, x0, x1, base, s, c, s_he);
}

static void button(int x, int y, int w, int h, const char *label, bool hot, int id, int arg)
{
    tfd_rrect(x, y, w, h, 8, hot ? accent() : C_BTN, 256);
    if (!hot) {
        tfd_rrect(x + 1, y + 1, w - 2, h - 2, 7, C_BTN, 256);
    }
    tfd_rrect(x, y, w, 1, 0, hot ? accent() : C_BTN_E, 256);
    const kfont_t *f = ui_text_w(&KF_NAME, label) <= w - 10 ? &KF_NAME : &KF_LABEL;
    tfd_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, label, hot ? C_BG : C_TEXT, w - 8);
    hit(x, y, w, h, id, arg);
}

static void age(char *o, int n, int64_t t)
{
    const int64_t s = (s_now - t) / 1000;
    if (s < 60) {
        snprintf(o, (size_t)n, "%ds", (int)s);
    } else if (s < 3600) {
        snprintf(o, (size_t)n, "%dm", (int)(s / 60));
    } else {
        snprintf(o, (size_t)n, "%dh", (int)(s / 3600));
    }
}

static void dot(int cx, int cy, uint16_t c, bool pulse)
{
    if (pulse) {
        const float ph = (float)(s_now % 1400) / 1400.0f;
        tfd_disc((float)cx, (float)cy, 4.0f + 6.0f * ph, c, (int)(120 * (1 - ph)));
    }
    tfd_disc((float)cx, (float)cy, 4.0f, c, 256);
}

// the line of the state (CLIENT): the dot, the broker, the state; the counters at the right end
static void top_bar(const char *back_title)
{
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    tfd_rect(0, s_top_h, s_W, 1, C_LINE, 256);
    const int base = s_top_h / 2 + 6;
    if (back_title) {
        // BACK at the left end (the pull zone of the panel is in the middle of the top edge)
        tfd_rrect(4, 3, 64, s_top_h - 6, 6, C_BTN, 256);
        tfd_text_c(&KF_LABEL, 36, base - 1, TX(T_BACK), C_TEXT, 60);
        hit(0, 0, 80, s_top_h + 4, H_BACK, 0);
        txt_l(&KF_NAME, 78, base, back_title, C_TEXT, s_W - 90);
        return;
    }
    char b[160];
    uint16_t c = C_DIM;
    bool pulse = false;
    const char *st = TX(T_OFF);
    if (!s_wifi) {
        st = TX(T_NOWIFI);
        c = C_BAD;
    } else {
        switch (s_st.state) {
        case MQN_DIAL:
        case MQN_WAIT:
            st = TX(T_DIAL);
            c = C_WARN;
            pulse = true;
            break;
        case MQN_UP:
            st = TX(T_UP);
            c = C_OK;
            break;
        case MQN_LISTEN:
            st = TX(T_LISTEN);
            c = C_OK;
            pulse = true;
            break;
        case MQN_RETRY:
            snprintf(b, sizeof b, TX(T_RETRY), s_st.retry_s);
            st = b;
            c = C_WARN;
            break;
        case MQN_FAIL:
            st = TX(T_FAIL);
            c = C_BAD;
            break;
        }
    }
    dot(12, s_top_h / 2, c, pulse);
    char name[120];
    if (s_mode == 1) {
        snprintf(name, sizeof name, "%s:%d", wifi_ip()[0] ? wifi_ip() : "-", s_st.port ? s_st.port : 1883);
    } else {
        snprintf(name, sizeof name, "%s", server_name());
    }
    char r[48];
    if (s_mode == 1) {
        snprintf(r, sizeof r, "%d · %.1f/s", s_st.clients, (double)s_rate);
    } else if (s_st.state == MQN_UP && s_st.ping_ms >= 0 && !s_narrow) {
        snprintf(r, sizeof r, "%u · %.1f/s · %dms", (unsigned)s_nmsg, (double)s_rate, s_st.ping_ms);
    } else {
        snprintf(r, sizeof r, "%u · %.1f/s", (unsigned)s_nmsg, (double)s_rate);
    }
    const int rw = mf_width(&MQ_SM, r, 0);
    int nw = ui_text_w(&KF_NAME, name);
    const int nmax = s_W - 24 - 8 - (s_narrow ? 0 : rw + 16);
    if (nw > nmax) {
        nw = nmax;
    }
    txt_l(&KF_NAME, 24, base, name, C_TEXT, nmax);
    const int sx = 24 + nw + 10, sw = ui_text_w(&KF_LABEL, st);
    const bool st_fits = sx + sw <= s_W - 8;
    const bool r_fits = (st_fits ? sx + sw + 12 : sx) + rw <= s_W - 8;
    if (st_fits) {
        txt_l(&KF_LABEL, sx, base - 1, st, c, sw + 2);
    }
    if (r_fits) {
        mf_text(&MQ_SM, s_W - 8 - rw, base - 1, r, C_DIM, 255, 0);
    }
}

static void toast_draw(void)
{
    if (!s_toast[0] || s_now - s_toast_t > 2600) {
        return;
    }
    const int w = ui_text_w(&KF_NAME, s_toast) + 28;
    const int ww = w > s_W - 20 ? s_W - 20 : w;
    const int y = s_narrow ? s_H - 86 : s_top_h + 8;
    const int a = s_now - s_toast_t > 2200 ? (int)(256 * (2600 - (s_now - s_toast_t)) / 400) : 256;
    const int cx = s_narrow ? s_W / 2 : (s_W - 104) / 2;
    tfd_rrect(cx - ww / 2, y, ww, 34, 10, rgb(36, 54, 78), a);
    tfd_text_c(&KF_NAME, cx, y + 23, s_toast, C_TEXT, ww - 16);
}

static float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}

// ---------------------------------------------------------------- START and SERVERS

static void card(int x, int y, int w, int h, const char *t1, const char *t2, uint16_t c, int id, int arg, bool big)
{
    tfd_rrect(x, y, w, h, 12, C_PANEL, 256);
    tfd_rrect(x, y, 4, h, 2, c, 256);
    const kfont_t *f1 = big ? &KF_BIG : &KF_NAME;
    if (ui_text_w(f1, t1) > w - 24) {
        f1 = &KF_NAME;
    }
    const int mid = y + h / 2;
    if (t2 && t2[0] && ui_text_w(&KF_LABEL, t2) > w - 16 && h >= 80) {
        // (two lines: the words that fit, then the rest)
        char l1[120];
        const char *rest = "";
        int sp = -1;
        for (int k = 0; t2[k] && k < (int)sizeof l1 - 1; k++) {
            l1[k] = t2[k];
            l1[k + 1] = 0;
            if (ui_text_w(&KF_LABEL, l1) > w - 16) {
                break;
            }
            if (t2[k] == ' ') {
                sp = k;
            }
        }
        if (sp > 0) {
            l1[sp] = 0;
            rest = t2 + sp + 1;
        }
        tfd_text_c(f1, x + w / 2, mid - 8, t1, c, w - 20);
        tfd_text_c(&KF_LABEL, x + w / 2, mid + 14, l1, C_DIM, w - 16);
        tfd_text_c(&KF_LABEL, x + w / 2, mid + 31, rest, C_DIM, w - 16);
    } else if (t2 && t2[0]) {
        tfd_text_c(f1, x + w / 2, mid - 2, t1, c, w - 20);
        tfd_text_c(&KF_LABEL, x + w / 2, mid + 18, t2, C_DIM, w - 16);
    } else {
        tfd_text_c(f1, x + w / 2, mid + f1->asc / 2, t1, c, w - 20);
    }
    hit(x, y, w, h, id, arg);
}

static void draw_start(void)
{
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    tfd_text_c(&KF_NAME, s_W / 2, s_top_h / 2 + 6, "MQTT", C_TEXT, s_W);
    const int g = 10, y = s_top_h + g;
    const int h = s_H - y - g;
    if (s_narrow) {
        const int hh = (h - g) / 2;
        card(g, y, s_W - 2 * g, hh, TX(T_CLIENT), TX(T_CLIENT_SUB), C_CLI, H_START, 0, true);
        card(g, y + hh + g, s_W - 2 * g, hh, TX(T_BROKER), TX(T_BROKER_SUB), C_BRK, H_START, 1, true);
    } else {
        const int w = (s_W - 3 * g) / 2;
        card(g, y, w, h, TX(T_CLIENT), TX(T_CLIENT_SUB), C_CLI, H_START, 0, true);
        card(2 * g + w, y, w, h, TX(T_BROKER), TX(T_BROKER_SUB), C_BRK, H_START, 1, true);
    }
}

static void draw_servers(void)
{
    top_bar(TX(T_CHOOSE));
    const int g = 8;
    char sub[120];
    snprintf(sub, sizeof sub, "%s", s_acct.host[0] ? s_acct.host : TX(T_PAGE_NONE));
    if (s_narrow) {
        const int y0 = s_top_h + 6, h = (s_H - y0 - 4) / 5 - 4;
        for (int i = 0; i < NPRE; i++) {
            card(g, y0 + i * (h + 4), s_W - 2 * g, h, PRE[i].host, NULL, C_CLI, H_SRV, i, false);
        }
        card(g, y0 + 3 * (h + 4), s_W - 2 * g, h, TX(T_PAGE), sub, C_CLI, H_SRV, SRV_PAGE, false);
        card(g, y0 + 4 * (h + 4), s_W - 2 * g, h, TX(T_TYPE_ADDR), NULL, C_DIM, H_SRV, SRV_PAGE + 1, false);
    } else {
        const int y0 = s_top_h + 6, h = (s_H - y0 - 6 - g) / 2;
        const int w3 = (s_W - 4 * g) / 3;
        for (int i = 0; i < NPRE; i++) {
            card(g + i * (w3 + g), y0, w3, h, PRE[i].host, TX(T_PUBLIC), C_CLI, H_SRV, i, false);
        }
        const int w2 = (s_W - 3 * g) / 2;
        card(g, y0 + h + g, w2, h, TX(T_PAGE), sub, C_CLI, H_SRV, SRV_PAGE, false);
        card(2 * g + w2, y0 + h + g, w2, h, TX(T_TYPE_ADDR), TX(T_HINT_PAGE), C_DIM, H_SRV, SRV_PAGE + 1, false);
    }
}

// ---------------------------------------------------------------- MAIN: the lists

static bool is_binary(const uint8_t *m, int n);

// the value of a message for one row: a number in the number face, a text in the Rubik face, the size of binary data
static void row_value(const uint8_t *m, int n, int full, int x1, int base, int w, uint16_t c)
{
    char b[96];
    float v;
    if (n == 0) {
        tfd_text_r(&KF_LABEL, x1, base, TX(T_EMPTY), C_FAINT, w);
        return;
    }
    if (as_number(m, n, &v)) {
        preview(b, sizeof b, m, n);
        char *p = b;
        while (*p == ' ') {
            p++;
        }
        const mfont_t *f = s_narrow ? &MQ_SM : &MQ_MD;
        mf_text(f, x1 - mf_width(f, p, 0), base, p, C_NUM, 255, 0);
        return;
    }
    if (is_binary(m, n)) {
        snprintf(b, sizeof b, "[ %d B ]", full);
        mf_text(&MQ_SM, x1 - mf_width(&MQ_SM, b, 0), base, b, C_DIM, 255, 0);
        return;
    }
    preview(b, sizeof b, m, n);
    tfd_text_r(&KF_LABEL, x1, base, b, c, w);
}

static void draw_topics(void)
{
    const int y0 = s_list_y0, y1 = s_list_y1, x1 = s_list_x1;
    const float mx = (float)(s_ntop * s_row_h - (y1 - y0));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    tfd_clip(0, y0, x1, y1);
    char a[16];
    for (int i = (int)(s_scroll / s_row_h); i < s_ntop; i++) {
        const int y = y0 + i * s_row_h - (int)s_scroll;
        if (y >= y1) {
            break;
        }
        const top_t *z = &s_top[s_order[i]];
        const bool fresh = s_now - z->t < 600;
        tfd_rect(0, y, x1, s_row_h - 1, fresh ? rgb(18, 30, 40) : (i & 1 ? C_ROW : C_BG), 256);
        if (fresh) {
            tfd_rect(0, y, 3, s_row_h - 1, accent(), (int)(256 * (600 - (s_now - z->t)) / 600));
        }
        age(a, sizeof a, z->t);
        if (s_narrow) {
            txt_l(&KF_NAME, 8, y + 17, z->topic, C_TEXT, x1 - 60);
            mf_text(&MQ_SM, x1 - 8 - mf_width(&MQ_SM, a, 0), y + 15, a, C_FAINT, 255, 0);
            char cnt[16];
            snprintf(cnt, sizeof cnt, "x%u", (unsigned)z->count);
            mf_text(&MQ_SM, 8, y + 34, cnt, C_FAINT, 255, 0);
            row_value(z->msg, z->len, z->full, x1 - 8, y + 35, x1 - 60, C_STR);
        } else {
            const int tw = (x1 - 16) * 52 / 100;
            txt_l(&KF_NAME, 8, y + 20, z->topic, C_TEXT, tw);
            mf_text(&MQ_SM, x1 - 6 - mf_width(&MQ_SM, a, 0), y + 19, a, C_FAINT, 255, 0);
            row_value(z->msg, z->len, z->full, x1 - 44, y + 20, x1 - tw - 64, C_STR);
        }
        hit_in(0, y, x1, s_row_h - 1, H_ROW, s_order[i], y0, y1);
    }
    tfd_noclip();
    if (!s_ntop) {
        const int cy = (y0 + y1) / 2;
        const bool up = s_st.state == MQN_UP;
        tfd_text_c(&KF_NAME, x1 / 2, cy - 4, up ? (s_nsub ? TX(T_WAITING) : TX(T_NOSUBS)) : TX(T_NOMSG), C_DIM,
                   x1 - 20);
        char b[200] = "";
        int k = 0;
        for (int i = 0; i < s_nsub && k < 180; i++) {
            k += snprintf(b + k, sizeof b - (size_t)k, "%s%s", i ? "   " : "", s_subs[i]);
        }
        if (s_nsub) {
            tfd_text_c(&KF_LABEL, x1 / 2, cy + 18, b, C_FAINT, x1 - 20);
        }
        if (s_st.state == MQN_RETRY || s_st.state == MQN_FAIL) {
            tfd_text_c(&KF_LABEL, x1 / 2, cy + 38, s_st.why, C_WARN, x1 - 20);
        }
    }
}

static const char *client_name(int slot)
{
    if (slot < 0) {
        return TX(T_APP);
    }
    if (slot < MQ_BRK_CLIENTS && s_cl[slot].id[0]) {
        return s_cl[slot].id;
    }
    return "?";
}

// the stream (CLIENT) or the traffic (BROKER): the newest first
static void draw_stream(int x0, int x1)
{
    const int y0 = s_list_y0, y1 = s_list_y1;
    const int n = (int)(s_nstr < STRN ? s_nstr : STRN);
    const bool two = s_narrow || x1 - x0 < 400;
    const int rh = two ? 40 : s_row_h;
    const float mx = (float)(n * rh - (y1 - y0));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    tfd_clip(x0, y0, x1, y1);
    char a[16], b[160];
    for (int i = (int)(s_scroll / rh); i < n; i++) {
        const int y = y0 + i * rh - (int)s_scroll;
        if (y >= y1) {
            break;
        }
        const uint32_t idx = s_nstr - 1 - (uint32_t)i;
        const sent_t *e = &s_str[idx % STRN];
        tfd_rect(x0, y, x1 - x0, rh - 1, i & 1 ? C_ROW : C_BG, 256);
        age(a, sizeof a, e->t);
        const int aw = mf_width(&MQ_SM, a, 0);
        mf_text(&MQ_SM, x1 - 6 - aw, y + (s_narrow ? 15 : 19), a, C_FAINT, 255, 0);
        const int w = x1 - x0;
        if (e->kind == SK_MSG) {
            uint16_t qc = e->qos ? C_NUM : C_FAINT;
            char q[24];
            snprintf(q, sizeof q, "Q%d%s", e->qos, e->retain ? " R" : "");
            if (two) {
                txt_l(&KF_NAME, x0 + 8, y + 17, e->topic, C_TEXT, w - aw - 60);
                mf_text(&MQ_SM, x0 + 8, y + 34, q, qc, 255, 0);
                if (s_mode == 1) {
                    snprintf(b, sizeof b, "%s  >  %s", client_name(e->slot), e->text);
                    txt_l(&KF_LABEL, x0 + 44, y + 35, b, C_STR, w - 52);
                } else {
                    txt_l(&KF_LABEL, x0 + 44, y + 35, e->text[0] ? e->text : TX(T_EMPTY), C_STR, w - 52);
                }
            } else {
                const int tw = w * 42 / 100;
                txt_l(&KF_NAME, x0 + 8, y + 20, e->topic, C_TEXT, tw);
                mf_text(&MQ_SM, x0 + 14 + tw, y + 19, q, qc, 255, 0);
                txt_l(&KF_LABEL, x0 + 52 + tw, y + 19, e->text[0] ? e->text : TX(T_EMPTY), C_STR, w - tw - aw - 70);
            }
            hit_in(x0, y, w, rh - 1, H_SROW, (int)(idx % STRN), y0, y1);
        } else {
            uint16_t c = e->kind == SK_JOIN ? C_OK : e->kind == SK_LEAVE ? C_WARN : e->kind == SK_SUB ? C_NUM : C_DIM;
            // (a long line is cut)
            if (e->kind == SK_JOIN) {
                snprintf(b, sizeof b, "%.60s  %s  %.60s", e->topic, TX(T_JOIN), e->text);
            } else if (e->kind == SK_LEAVE) {
                snprintf(b, sizeof b, "%.60s  %s  (%.60s)", e->topic, TX(T_LEAVE), e->text);
            } else if (e->kind == SK_SUB) {
                snprintf(b, sizeof b, "%s  %s  %s", client_name(e->slot), TX(T_SUBD), e->topic);
            } else {
                snprintf(b, sizeof b, "%s", e->text);
            }
            tfd_disc((float)x0 + 12, (float)(y + rh / 2), 3.5f, c, 256);
            txt_l(&KF_LABEL, x0 + 22, y + rh / 2 + 5, b, c, w - aw - 34);
        }
    }
    tfd_noclip();
    if (!n) {
        tfd_text_c(&KF_NAME, (x0 + x1) / 2, (y0 + y1) / 2 + 6, TX(T_NOMSG), C_DIM, x1 - x0 - 20);
    }
}

// the address card of the broker (while no client is there)
static void draw_addr_card(int x0, int y0, int x1, int y1)
{
    const int cx = (x0 + x1) / 2;
    char b[96];
    const char *ip = wifi_ip();
    snprintf(b, sizeof b, "%s : %d", ip && ip[0] ? ip : "-", s_st.port ? s_st.port : 1883);
    const int h = y1 - y0;
    tfd_rrect(x0 + 6, y0 + 6, x1 - x0 - 12, h - 12, 14, C_PANEL, 256);
    tfd_text_c(&KF_LABEL, cx, y0 + 6 + h / 5, TX(T_CONNECT_TO), C_DIM, x1 - x0 - 30);
    mf_text_c(&MQ_BIG, cx, y0 + h / 2 + 8, b, C_BRK, 255, 0);
    char l[120];
    if (s_blogin && s_acct.user[0]) {
        snprintf(l, sizeof l, TX(T_LOGIN), s_acct.user);
    } else {
        snprintf(l, sizeof l, "%s", TX(T_OPEN));
    }
    tfd_text_c(&KF_LABEL, cx, y0 + h / 2 + 32, l, C_TEXT, x1 - x0 - 30);
    tfd_text_c(&KF_LABEL, cx, y1 - 16, s_st.state == MQN_FAIL ? s_st.why : TX(T_STOPS),
               s_st.state == MQN_FAIL ? C_BAD : C_FAINT, x1 - x0 - 30);
}

static void draw_clients(int x0, int x1)
{
    const int y0 = s_list_y0, y1 = s_list_y1;
    const int rh = s_narrow ? 40 : 36;
    int row = 0;
    char b[96], a[16];
    tfd_clip(x0, y0, x1, y1);
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        const mqn_client_t *c = &s_cl[s];
        if (!c->up) {
            continue;
        }
        const int y = y0 + row * rh;
        row++;
        tfd_rect(x0, y, x1 - x0, rh - 1, row & 1 ? C_ROW : C_BG, 256);
        tfd_disc((float)x0 + 12, (float)y + 14, 4, C_OK, 256);
        txt_l(&KF_NAME, x0 + 22, y + 18, c->id, C_TEXT, x1 - x0 - 90);
        age(a, sizeof a, c->t_conn);
        mf_text(&MQ_SM, x1 - 6 - mf_width(&MQ_SM, a, 0), y + 17, a, C_FAINT, 255, 0);
        snprintf(b, sizeof b, "%s   SUB %d   IN %u   OUT %u", c->addr, c->nsub, (unsigned)c->n_in, (unsigned)c->n_out);
        mf_text(&MQ_SM, x0 + 22, y + rh - 6, b, C_DIM, 255, 0);
    }
    tfd_noclip();
}

static void side_buttons(void)
{
    // the column at the right end (the 3.49), or the bar at the bottom (the 320 x 240 frame)
    const char *l[3];
    int id[3], n = 0;
    if (s_mode == 0) {
        l[n] = s_view ? TX(T_TOPICS) : TX(T_STREAM);
        id[n++] = H_VIEW;
        l[n] = TX(T_SUB);
        id[n++] = H_SUBS;
        l[n] = TX(T_PUB);
        id[n++] = H_PUB;
    } else {
        if (s_narrow && s_st.clients) {
            l[n] = s_view ? TX(T_TRAFFIC) : TX(T_CLIENTS);
            id[n++] = H_BVIEW;
        }  // (the view it goes to)
        l[n] = TX(T_PUB);
        id[n++] = H_PUB;
    }
    if (s_narrow) {
        const int g = 6, w = (s_W - (n + 1) * g) / n, y = s_H - 42;
        tfd_rect(0, y - 4, s_W, 46, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            button(g + i * (w + g), y, w, 38, l[i], i == n - 1, id[i], 0);
        }
    } else {
        const int x = s_W - 100, y0 = s_top_h + 4, g = 4;
        const int h = (s_H - y0 - 2 - (3 - 1) * g) / 3;
        tfd_rect(s_W - 104, s_top_h + 1, 104, s_H - s_top_h - 1, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            button(x, y0 + (3 - n + i) * (h + g), 96, h, l[i], i == n - 1, id[i], 0);
        }
    }
}

static void draw_main(void)
{
    if (s_mode == 0) {
        if (s_view == 0) {
            draw_topics();
        } else {
            draw_stream(0, s_list_x1);
        }
        if (s_topfull && s_view == 0 && s_ntop >= TOPN) {
            tfd_text_c(&KF_LABEL, s_list_x1 / 2, s_list_y1 - 6, TX(T_NONE_KEPT), C_FAINT, s_list_x1);
        }
    } else {
        if (!s_st.clients) {
            draw_addr_card(0, s_list_y0, s_list_x1, s_list_y1);
        } else if (!s_narrow) {
            draw_clients(0, s_pane_x - 52);
            tfd_rect(s_pane_x - 52, s_list_y0, 1, s_list_y1 - s_list_y0, C_LINE, 256);
            draw_stream(s_pane_x - 51, s_list_x1);
        } else if (s_view == 0) {
            draw_stream(0, s_W);
        } else {
            draw_clients(0, s_W);
        }
    }
    // (the bars after the lists: a row that the list scrolled half out goes under them)
    top_bar(NULL);
    if (s_mode == 0) {
        hit(20, 0, s_narrow ? 150 : 260, s_top_h, H_SRV_LIST, 0);  // (a tap on the broker: the list)
    }
    side_buttons();
}

// ---------------------------------------------------------------- A TOPIC

// the lines of a text that fit w px (the words go on the next line; a long word is cut); JSON gets indents.
// Calls line(i, text, colour) for each line; returns the count.
typedef void (*line_fn)(int i, const char *s, uint16_t c, void *u);

// JSON with indents (2 spaces a level), into o (n bytes): false if the text is not JSON
static bool json_pretty(const uint8_t *m, int len, char *o, int n)
{
    int i = 0;
    while (i < len && (m[i] == ' ' || m[i] == '\n' || m[i] == '\r' || m[i] == '\t')) {
        i++;
    }
    if (i >= len || (m[i] != '{' && m[i] != '[')) {
        return false;
    }
    int k = 0, depth = 0;
    bool str = false, esc = false;
#define PUT(ch)                                                                                                        \
    do {                                                                                                               \
        if (k < n - 1)                                                                                                 \
            o[k++] = (char)(ch);                                                                                       \
    } while (0)
    for (; i < len; i++) {
        const uint8_t c = m[i];
        if (str) {
            PUT(c);
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                str = false;
            }
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        if (c == '"') {
            str = true;
            PUT(c);
            continue;
        }
        if (c == '{' || c == '[') {
            PUT(c);
            depth++;
            // (an empty {} or [] stays on its line)
            int j = i + 1;
            while (j < len && (m[j] == ' ' || m[j] == '\n' || m[j] == '\r' || m[j] == '\t')) {
                j++;
            }
            if (j < len && (m[j] == '}' || m[j] == ']')) {
                continue;
            }
            PUT('\n');
            for (int d = 0; d < depth; d++) {
                PUT(' ');
                PUT(' ');
            }
            continue;
        }
        if (c == '}' || c == ']') {
            depth = depth > 0 ? depth - 1 : 0;
            if (k > 0 && o[k - 1] != '{' && o[k - 1] != '[') {
                PUT('\n');
                for (int d = 0; d < depth; d++) {
                    PUT(' ');
                    PUT(' ');
                }
            }
            PUT(c);
            continue;
        }
        if (c == ',') {
            PUT(c);
            PUT('\n');
            for (int d = 0; d < depth; d++) {
                PUT(' ');
                PUT(' ');
            }
            continue;
        }
        if (c == ':') {
            PUT(':');
            PUT(' ');
            continue;
        }
        PUT(c);
    }
#undef PUT
    o[k] = 0;
    return depth == 0 && !str;
}

static bool is_binary(const uint8_t *m, int n)
{
    int bad = 0;
    for (int i = 0; i < n; i++) {
        if (m[i] < 32 && m[i] != '\n' && m[i] != '\r' && m[i] != '\t') {
            bad++;
        }
    }
    return bad > 0 && bad * 10 > n / 10;  // (more than 1 % control bytes)
}

// the text of a message as lines of at most w px
static int wrap(const kfont_t *f, const char *s, int w, line_fn fn, void *u, uint16_t c)
{
    int n = 0;
    char line[200];
    while (*s) {
        // one source line
        const char *e = strchr(s, '\n');
        const int len = e ? (int)(e - s) : (int)strlen(s);
        int at = 0;
        if (len == 0) {
            fn(n++, "", c, u);
        }
        while (at < len) {
            // the longest start that fits (at a space if there is one)
            int k = 0, last_sp = -1;
            while (at + k < len && k < (int)sizeof line - 5) {
                int l = 1;
                const uint8_t ch = (uint8_t)s[at + k];
                if (ch >= 0xC0) {
                    l = ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : 2;
                }
                memcpy(line, s + at, (size_t)(k + l));
                line[k + l] = 0;
                if (ui_text_w(f, line) > w) {
                    break;
                }
                if (ch == ' ') {
                    last_sp = k;
                }
                k += l;
            }
            if (k == 0) {
                k = 1;
            }
            if (at + k < len && last_sp > 0) {
                k = last_sp + 1;
            }
            memcpy(line, s + at, (size_t)k);
            line[k] = 0;
            fn(n++, line, c, u);
            at += k;
        }
        if (!e) {
            break;
        }
        s = e + 1;
    }
    return n;
}

typedef struct {
    int x, y0, lh, ys, ye;
} wr_t;
static void line_none(int i, const char *s, uint16_t c, void *u)
{
    (void)i;
    (void)s;
    (void)c;
    (void)u;
}
static void line_draw(int i, const char *s, uint16_t c, void *u)
{
    wr_t *w = u;
    const int y = w->y0 + i * w->lh;  // (the base line)
    if (y - KF_LABEL.asc < w->ys || y + 4 > w->ye) {
        return;  // (whole lines only: ui_text has no clip)
    }
    // a JSON key ("key": ...) in its own colour
    const char *q = s;
    while (*q == ' ') {
        q++;
    }
    if (*q == '"') {
        const char *z = strstr(q, "\": ");
        if (z) {
            char k[160];
            const int kl = (int)(z - s) + 2;
            if (kl < (int)sizeof k) {
                memcpy(k, s, (size_t)kl);
                k[kl] = 0;
                ui_text_c(&KF_LABEL, w->x + ui_text_w(&KF_LABEL, k) / 2, y, k, C_KEY);
                const char *v = s + kl;
                const int vx = w->x + ui_text_w(&KF_LABEL, k);
                ui_text_c(&KF_LABEL, vx + ui_text_w(&KF_LABEL, v) / 2, y, v, *v == '"' || v[1] == '"' ? C_STR : C_NUM);
                return;
            }
        }
    }
    ui_text_c(&KF_LABEL, w->x + ui_text_w(&KF_LABEL, s) / 2, y, s, c);
}

static void draw_chart(const top_t *z, int x, int y, int w, int h)
{
    tfd_rrect(x, y, w, h, 10, C_PANEL, 256);
    float mn = 1e30f, mxv = -1e30f;
    for (int i = 0; i < z->nh; i++) {
        const float v = z->hist[(z->hi - z->nh + i + HISTN) % HISTN];
        if (v < mn) {
            mn = v;
        }
        if (v > mxv) {
            mxv = v;
        }
    }
    const float last = z->hist[(z->hi - 1 + HISTN) % HISTN];
    char b[32];
    snprintf(b, sizeof b, "%.6g", (double)last);
    mf_text(&MQ_BIG, x + 10, y + 28, b, C_NUM, 255, 0);
    char r[64];
    snprintf(r, sizeof r, "%s %.5g   %s %.5g", TX(T_MIN), (double)mn, TX(T_MAX), (double)mxv);
    txt_l(&KF_LABEL, x + 10, y + h - 8, r, C_DIM, w - 20);
    // the line of the values
    const int gx = x + 10, gy = y + 36, gw = w - 20, gh = h - 36 - 22;
    if (z->nh < 2 || gh < 10) {
        return;
    }
    const float span = mxv - mn > 1e-9f ? mxv - mn : 1.0f;
    float px = 0, py = 0;
    for (int i = 0; i < z->nh; i++) {
        const float v = z->hist[(z->hi - z->nh + i + HISTN) % HISTN];
        const float xx = gx + (float)gw * (float)i / (float)(z->nh - 1);
        const float yy = gy + gh - (float)gh * (v - mn) / span;
        if (i) {
            tfd_line(px, py, xx, yy, 2.0f, C_NUM, 230);
        }
        px = xx;
        py = yy;
    }
    tfd_disc(px, py, 3.2f, C_NUM, 256);
}

static char s_pretty[2600];

static void draw_topic(void)
{
    if (s_sel < 0 || !s_top[s_sel].used) {
        s_scr = SC_MAIN;
        return;
    }
    const top_t *z = &s_top[s_sel];
    const int x1 = s_narrow ? s_W : s_W - 104;
    int ty0 = s_top_h + 26;
    int tx1 = x1;
    float dummy;
    const bool chart = z->nh >= 1 && as_number(z->msg, z->len, &dummy);
    if (chart) {
        if (s_narrow) {
            ty0 += 98;
        } else {
            tx1 = x1 - 244;
        }
    }
    // the text: JSON with indents, or the text, or hex
    const int y1 = s_narrow ? s_H - 46 : s_H;
    const char *show;
    if (z->len == 0) {
        show = TX(T_EMPTY);
    } else if (is_binary(z->msg, z->len)) {
        int k = snprintf(s_pretty, sizeof s_pretty, TX(T_BINARY), z->full);
        k += snprintf(s_pretty + k, sizeof s_pretty - (size_t)k, "\n");
        for (int i = 0; i < z->len && k < (int)sizeof s_pretty - 60; i += 8) {
            k += snprintf(s_pretty + k, sizeof s_pretty - (size_t)k, "%04x ", i);
            for (int j = 0; j < 8 && i + j < z->len; j++) {
                k += snprintf(s_pretty + k, sizeof s_pretty - (size_t)k, " %02x", z->msg[i + j]);
            }
            k += snprintf(s_pretty + k, sizeof s_pretty - (size_t)k, "\n");
        }
        show = s_pretty;
    } else if (json_pretty(z->msg, z->len, s_pretty, sizeof s_pretty)) {
        show = s_pretty;
    } else {
        const int n = z->len <= 0 ? 0 : z->len < (int)sizeof s_pretty - 1 ? z->len : (int)sizeof s_pretty - 1;
        memcpy(s_pretty, z->msg, (size_t)n);
        s_pretty[n] = 0;
        for (int i = 0; i < n; i++) {
            if ((uint8_t)s_pretty[i] < 32 && s_pretty[i] != '\n') {
                s_pretty[i] = ' ';
            }
        }
        show = s_pretty;
    }
    wr_t w = {10, 0, KF_LABEL.line, ty0, y1};
    const int lines = wrap(&KF_LABEL, show, tx1 - 22, line_none, NULL, C_TEXT);
    s_dtext_h = lines * KF_LABEL.line + 10;
    const float mx = (float)(s_dtext_h - (y1 - ty0));
    s_dscroll = clampf(s_dscroll, 0, mx > 0 ? mx : 0);
    w.y0 = ty0 + KF_LABEL.asc + 4 - (int)s_dscroll;
    tfd_clip(0, ty0, tx1, y1);
    wrap(&KF_LABEL, show, tx1 - 22, line_draw, &w, C_TEXT);
    tfd_noclip();
    // the head over the text: the facts of the last message (Latin and numbers only: one order in both languages)
    tfd_rect(0, 0, x1, ty0, C_BG, 256);
    top_bar(z->topic);
    char a[16], m[200];
    age(a, sizeof a, z->t);
    snprintf(m, sizeof m, "QoS %d%s   %d B   %s   x%u", z->qos, z->retain ? "   RETAINED" : "", z->full, a,
             (unsigned)z->count);
    mf_text(&MQ_SM, 10, s_top_h + 17, m, C_DIM, 255, 0);
    if (chart) {
        if (s_narrow) {
            draw_chart(z, 8, s_top_h + 26, s_W - 16, 92);
        } else {
            draw_chart(z, x1 - 236, s_top_h + 26, 228, s_H - s_top_h - 32);
        }
    }
    if (z->full > z->len) {
        char t[64];
        snprintf(t, sizeof t, "... %d / %d B", z->len, z->full);
        mf_text(&MQ_SM, tx1 - 8 - mf_width(&MQ_SM, t, 0), y1 - 6, t, C_FAINT, 255, 0);
    }
    // PUBLISH TO THIS TOPIC
    if (s_narrow) {
        tfd_rect(0, s_H - 46, s_W, 46, C_PANEL, 256);
        button(6, s_H - 42, s_W - 12, 38, TX(T_TO_TOPIC), true, H_TO_TOPIC, 0);
    } else {
        tfd_rect(s_W - 104, s_top_h + 1, 104, s_H - s_top_h - 1, C_PANEL, 256);
        button(s_W - 100, s_H - 52, 96, 48, TX(T_PUB), true, H_TO_TOPIC, 0);
    }
}

// ---------------------------------------------------------------- PUBLISH

static void field(int x, int y, int w, int h, const char *label, const char *val, int id)
{
    tfd_rrect(x, y, w, h, 8, C_PANEL, 256);
    tfd_rrect(x, y + h - 2, w, 2, 1, C_LINE, 256);
    txt_s(&KF_LABEL, x + 10, x + w - 10, y + 15, label, C_DIM);
    txt_l(&KF_NAME, x + 10, y + h - 8, val[0] ? val : "...", val[0] ? C_TEXT : C_FAINT, w - 20);
    hit(x, y, w, h, id, 0);
}

static void draw_pub(void)
{
    top_bar(TX(T_PUB));
    const int g = 6;
    if (s_narrow) {
        int y = s_top_h + 6;
        field(g, y, s_W - 2 * g, 46, TX(T_TOPIC), s_pub_topic, H_PUB_TOPIC);
        y += 52;
        field(g, y, s_W - 2 * g, 46, TX(T_MESSAGE), s_pub_msg, H_PUB_MSG);
        y += 54;
        const int bw = (s_W - 5 * g) / 4;
        button(g, y, bw, 36, "QoS 0", s_pub_qos == 0, H_PUB_QOS, 0);
        button(2 * g + bw, y, bw, 36, "QoS 1", s_pub_qos == 1, H_PUB_QOS, 1);
        char r[48];
        snprintf(r, sizeof r, "%s", TX(T_RETAIN));
        button(3 * g + 2 * bw, y, 2 * bw + g, 36, r, s_pub_ret, H_PUB_RET, 0);
        tfd_rect(0, s_H - 46, s_W, 46, C_PANEL, 256);
        button(g, s_H - 42, s_W - 2 * g, 38, TX(T_SEND), true, H_PUB_SEND, 0);
    } else {
        const int fw = s_W - 104 - 2 * g;
        field(g, s_top_h + 5, fw, 44, TX(T_TOPIC), s_pub_topic, H_PUB_TOPIC);
        field(g, s_top_h + 54, fw, 44, TX(T_MESSAGE), s_pub_msg, H_PUB_MSG);
        const int y = s_top_h + 104, bw = 90;
        button(g, y, bw, 36, "QoS 0", s_pub_qos == 0, H_PUB_QOS, 0);
        button(2 * g + bw, y, bw, 36, "QoS 1", s_pub_qos == 1, H_PUB_QOS, 1);
        button(3 * g + 2 * bw, y, 130, 36, TX(T_RETAIN), s_pub_ret, H_PUB_RET, 0);
        tfd_rect(s_W - 104, s_top_h + 1, 104, s_H - s_top_h - 1, C_PANEL, 256);
        button(s_W - 100, s_top_h + 5, 96, s_H - s_top_h - 9, TX(T_SEND), true, H_PUB_SEND, 0);
    }
}

// ---------------------------------------------------------------- SUBSCRIPTIONS

static void draw_subs(void)
{
    top_bar(TX(T_SUBS_OF));
    const int g = 6;
    const int x1 = s_narrow ? s_W : s_W - 104;
    // the list (an X at the right end of each)
    const int rh = 34;
    int y = s_top_h + 4;
    for (int i = 0; i < s_nsub; i++) {
        tfd_rrect(g, y, x1 - 2 * g, rh - 4, 8, C_PANEL, 256);
        txt_l(&KF_NAME, g + 10, y + 21, s_subs[i], C_TEXT, x1 - 2 * g - 60);
        tfd_rrect(x1 - g - 44, y + 2, 40, rh - 8, 7, C_BTN, 256);
        tfd_text_c(&KF_NAME, x1 - g - 24, y + 21, "X", C_BAD, 30);
        hit(x1 - g - 50, y, 50, rh - 4, H_SUB_X, i);
        y += rh;
        if (y > s_H - (s_narrow ? 120 : 70)) {
            break;
        }
    }
    if (!s_nsub) {
        tfd_text_c(&KF_NAME, x1 / 2, y + 20, TX(T_NOSUBS), C_DIM, x1 - 20);
        y += 34;
    }
    // the common filters (the ones not in the list)
    y += 4;
    txt_s(&KF_LABEL, g + 2, x1 - g, y + 12, TX(T_COMMON), C_FAINT);
    y += 18;
    int x = g;
    for (int i = 0; i < NCOMMON; i++) {
        bool have = false;
        for (int j = 0; j < s_nsub; j++) {
            have |= !strcmp(s_subs[j], COMMON[i]);
        }
        if (have) {
            continue;
        }
        const int w = ui_text_w(&KF_LABEL, COMMON[i]) + 22;
        if (x + w > x1 - g) {
            x = g;
            y += 32;
        }
        if (y + 28 > (s_narrow ? s_H - 46 : s_H)) {
            break;
        }
        tfd_rrect(x, y, w, 28, 14, C_BTN, 256);
        tfd_text_c(&KF_LABEL, x + w / 2, y + 19, COMMON[i], C_CLI, w);
        hit(x, y, w, 28, H_SUB_COMMON, i);
        x += w + 6;
    }
    if (s_narrow) {
        tfd_rect(0, s_H - 46, s_W, 46, C_PANEL, 256);
        button(g, s_H - 42, s_W - 2 * g, 38, TX(T_ADD), true, H_SUB_ADD, 0);
    } else {
        tfd_rect(s_W - 104, s_top_h + 1, 104, s_H - s_top_h - 1, C_PANEL, 256);
        button(s_W - 100, s_top_h + 5, 96, s_H - s_top_h - 9, TX(T_ADD), true, H_SUB_ADD, 0);
    }
}

// ---------------------------------------------------------------- render

static void render(int64_t now)
{
    (void)now;
    if (s_scr == SC_KBD) {
        kbd_render(now);
        return;
    }
    layout();
    tfd_screen();
    tfd_noclip();
    s_nhit = 0;
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    switch (s_scr) {
    case SC_START:
        draw_start();
        break;
    case SC_SERVERS:
        draw_servers();
        break;
    case SC_TOPIC:
        draw_topic();
        break;
    case SC_PUB:
        draw_pub();
        break;
    case SC_SUBS:
        draw_subs();
        break;
    default:
        draw_main();
        break;
    }
    toast_draw();
}

// ---------------------------------------------------------------- the menu icon
#include "tficon/tficon.h"
// the menu icon: a broker in the middle and three clients: a message goes from one client to the broker, then to the
// two others (two times in 2 s)
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0xc18cff), tfi_hex(0x5e35b1));
    float u = s / 100.0f, ph = tfi_ph(1);
    float bx = x + 50 * u, by = y + 54 * u;
    static const float CP[3][2] = {{22, 28}, {80, 34}, {66, 82}};
    static const uint32_t CC[3] = {0xffd84a, 0x5dffb0, 0xff7aa8};
    for (int k = 0; k < 3; k++) {
        tfi_line(bx, by, x + CP[k][0] * u, y + CP[k][1] * u, 2.4f * u, false, tfi_hex(0xe8dcff), 256);
    }
    for (int k = 0; k < 3; k++) {
        float cx = x + CP[k][0] * u, cy = y + CP[k][1] * u;
        tfi_drop_disc(cx, cy, 9 * u, 2.5f * u, 80);
        tfi_disc(cx, cy, 9 * u, tfi_hex(CC[k]), tfi_hex(CC[k]), 256);
        tfi_spark(cx - 3 * u, cy - 3 * u, 2.6f * u, 256);
    }
    tfi_drop_disc(bx, by, 14 * u, 3 * u, 90);
    tfi_disc(bx, by, 14 * u, rgb(255, 255, 255), rgb(255, 255, 255), 256);
    tfi_ring(bx, by, 8 * u, 3 * u, tfi_hex(0x7e57c2), 256);
    // the message
    float mx, my;
    if (ph < 0.45f) {
        float q = tfi_step(0, 0.45f, ph);
        mx = x + CP[0][0] * u + (bx - x - CP[0][0] * u) * q;
        my = y + CP[0][1] * u + (by - y - CP[0][1] * u) * q;
        tfi_disc(mx, my, 6.5f * u, tfi_hex(0xfff3a0), tfi_hex(0xfff3a0), 256);
        tfi_disc(mx, my, 3.5f * u, rgb(255, 255, 255), rgb(255, 255, 255), 256);
    } else {
        float q = tfi_step(0.5f, 0.95f, ph);
        for (int k = 1; k < 3; k++) {
            mx = bx + (x + CP[k][0] * u - bx) * q;
            my = by + (y + CP[k][1] * u - by) * q;
            if (ph > 0.93f) {
                continue;
            }
            tfi_disc(mx, my, 6.5f * u, tfi_hex(0xfff3a0), tfi_hex(0xfff3a0), 256);
            tfi_disc(mx, my, 3.5f * u, rgb(255, 255, 255), rgb(255, 255, 255), 256);
        }
    }
    tfi_end();
}
