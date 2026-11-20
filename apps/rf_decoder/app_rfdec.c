// RF DECODER: the codes of 315 / 433 MHz remote keys, doorbells and sensors, from a receiver module on the pins.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_rfdec at the end of this file);
// update reads the level times of the receiver (libs/rfpulse) and decodes them.
// The memory: the file statics; the raw ring s_raw, the slicer s_sl and the list s_ent in PSRAM (the first enter
// makes them; they stay while the app file is loaded).
//
// The receiver: an MX-05 or an RXB6 at 3.3 V. In the style of the RCSwitch library: the raw pulses live, and the
// decoded codes (RCSwitch protocols 1..12, or the PWM / PPM / MANCHESTER / NRZ coding that fits best). The user can
// force a decoder and a coding.
//
// THE LAYERS (each one knows only the layer under it):
//   libs/rfpulse   the receivers: the contract (rfpulse.h), the module on the data pin (rfp_gpio.c), the demo, and the
//                  wiring of each board (rfp_board.h: the pads, their roles, the DATA GPIO)
//   libs/rfdecode  the slicer (a stream into packets) and the decoders; pure C
//   this app       the screens: THE WIRING (a sketch of the connector of this board and the module, with the live state
//                  of the pin), LIVE (the raw pulses and the list of the codes), A CODE (its pulses, bits and timing)
// The SDR app (apps/sdr) is the other job: the spectrum. A chip that can do both (a CC1101) can be a source of both.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"
// THE ROUND FRAME (APP_ROUND_HI): LIVE and A CODE have their own layout in the whole circle (layout_round(), the _r
// functions of rfd_draw.inc): the state in the top cap, the strip and the pulses in the width of the chord, the rows
// of the list follow the chord, BACK in the bottom cap. THE WIRING keeps the sketch of the flat frame in the middle
// box (rl_origin.h moves each draw call by (rl_ox, rl_oy), input() takes the origin off each touch point), its title
// in the top cap and its keys in the bottom cap. The Rubik faces are KF_TEXT at the least (rl_big 2).
#include "rndlay/rndlay.h"
#include "rndlay/rl_origin.h"
#include "rfdecode/rfdecode.h"
#include "rfpulse/rfp_board.h"
#include "rfpulse/rfpulse.h"

static bool s_he;  // the language: false English, true Hebrew
#include "rfd_text.inc"
#include "rfd_fonts.inc"

#define BAR BOARD_BAR_SCREEN
static int s_W, s_H;

// ================================================================ the receivers
#define NSRC 4
static rfp_src_t s_src[NSRC];
static int s_nsrc, s_srci = -1;
static const char *s_src_vals[2 * NSRC];
static bool s_open;
static int s_err;

// ================================================================ the state
enum {
    SC_WIRE,
    SC_LIVE,
    SC_INFO
};
static int s_scr;
static int s_mode, s_coding, s_freq, s_filt;  // RFD_MODE_*, RFD_K_* (0 = AUTO), the frequency, the noise filter
static const int FILT_US[3] = {40, 80, 150};
static int64_t s_now;
static rfp_stats_t s_st;
static uint32_t s_rate;  // the edges a second (the line state)
static uint32_t s_rate_edges;
static int64_t s_rate_t;

// the raw pulses: a ring of the last level times (the strip of LIVE)
#define RAWN 8192
static int32_t *s_raw;
static uint32_t s_rawn;    // all level times since the open (the ring index)
static int64_t s_raw_end;  // the time of the end of the newest one

// the slicer and the codes
static rfd_slicer_t *s_sl;
#define ENT_MAX 40
typedef struct {
    rfd_code_t c;
    int64_t t_first, t_last;
    uint16_t reps;
    rfd_packet_t pkt;  // the last packet of the code (the pulses of A CODE)
} ent_t;
static ent_t *s_ent;  // the newest first
static int s_nent;
static int s_sel = -1;  // the code of A CODE
// the generic codes wait for a second equal one (the noise of a receiver can look like one packet of a code)
#define PEND 6
static struct {
    rfd_code_t c;
    int64_t t;
} s_pend[PEND];
static int s_npend;

static float s_scroll;  // the list (px)
static int64_t s_toast_t;
static char s_toast[80];
static void toast(const char *s)
{
    snprintf(s_toast, sizeof(s_toast), "%s", s);
    s_toast_t = s_now;
}

static int64_t now_us(void)
{
    return esp_timer_get_time();
}
static const rfp_src_t *cur(void)
{
    return s_srci >= 0 && s_srci < s_nsrc ? &s_src[s_srci] : NULL;
}

static void src_close(void)
{
    if (s_open && cur()) {
        cur()->ops->close(cur()->ctx);
    }
    s_open = false;
}

static void src_open(int k)
{
    src_close();
    s_srci = k;
    s_err = 0;
    if (!cur()) {
        return;
    }
    int e = cur()->ops->open(cur()->ctx);
    if (e < 0) {
        s_err = e;
        return;
    }
    s_open = true;
    rfd_slicer_init(s_sl, FILT_US[s_filt % 3], 4300);
    s_rawn = 0;
    s_raw_end = now_us();
    s_rate = 0;
    s_rate_edges = 0;
    s_rate_t = now_us();
    store_set("rfd_src", (uint32_t)k);
}

static int src_index(bool demo)
{
    for (int k = 0; k < s_nsrc; k++) {
        if (((s_src[k].caps->flags & RFP_F_DEMO) != 0) == demo) {
            return k;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the codes into the list
static void add_entry(const rfd_code_t *c, const rfd_packet_t *p)
{
    // the same code as the newest entry, within 2 s: one more repeat
    if (s_nent > 0 && rfd_same(&s_ent[0].c, c) && p->t_us - s_ent[0].t_last < 2000000) {
        s_ent[0].reps++;
        s_ent[0].t_last = p->t_us;
        s_ent[0].pkt = *p;
        return;
    }
    if (s_nent == ENT_MAX) {
        s_nent--;
    }
    memmove(&s_ent[1], &s_ent[0], sizeof(ent_t) * (size_t)s_nent);
    s_nent++;
    ent_t *e = &s_ent[0];
    e->c = *c;
    e->t_first = e->t_last = p->t_us;
    e->reps = 1;
    e->pkt = *p;
    if (s_sel >= 0) {
        s_sel++;  // (the code of A CODE moves down one place)
    }
}

// The bits of b are the bits of a, or their head or tail (any kind): the same remote. (A weak signal: a receiver loses
// it at the same place in each repeat, and the tail of a code comes as its own packet: "PWM 12 bits 0xF96" of the
// code 0xC35A0F96, from a real test)
static bool same_bits(const rfd_code_t *a, const rfd_code_t *b)
{
    if (b->nbits > a->nbits || b->nbits < 8) {
        return false;
    }
    bool head = true, tail = true;
    for (int i = 0; i < b->nbits && (head || tail); i++) {
        head = head && rfd_bit(a, i) == rfd_bit(b, i);
        tail = tail && rfd_bit(a, a->nbits - b->nbits + i) == rfd_bit(b, i);
    }
    return head || tail;
}

static void on_packet(const rfd_packet_t *p)
{
    rfd_code_t c;
    if (!rfd_decode(p, s_mode, s_coding, &c)) {
        return;
    }
    // AUTO: the bits of the newest entry (or a part of them) within 2 s, from another decoder or a cut packet (a weak
    // packet of the same remote, for example an RCSwitch code that only the PWM decoder could read): one more repeat of
    // that entry, not a new code
    if (s_mode == RFD_MODE_AUTO && s_nent > 0 && !rfd_same(&s_ent[0].c, &c) && same_bits(&s_ent[0].c, &c) &&
        p->t_us - s_ent[0].t_last < 2000000) {
        s_ent[0].reps++;
        s_ent[0].t_last = p->t_us;
        return;
    }
    if (c.kind == RFD_K_RAW || s_mode == RFD_MODE_GENERIC) {
        if (c.kind != RFD_K_RAW && (c.score < 0.75f || c.nbits < 8)) {
            return;
        }
        add_entry(&c, p);
        return;
    }
    // AUTO and RCSWITCH: only when a second equal packet comes within 1.5 s (a remote sends 3 repeats or more; the
    // noise of a receiver can look like one packet of a code, and the first repeat of an inverted protocol has no
    // sync before it: a wrong bit frame); a generic coding also needs a good fit
    if (c.kind != RFD_K_RCSWITCH && (c.score < 0.9f || c.nbits < 12)) {
        return;
    }
    if (s_nent > 0 && rfd_same(&s_ent[0].c, &c) && p->t_us - s_ent[0].t_last < 2000000) {
        add_entry(&c, p);
        return;
    }
    for (int i = 0; i < s_npend; i++) {
        if (rfd_same(&s_pend[i].c, &c) && p->t_us - s_pend[i].t < 1500000) {
            add_entry(&c, p);
            s_ent[0].reps = 2;
            s_pend[i] = s_pend[--s_npend];
            return;
        }
    }
    if (s_npend == PEND) {
        memmove(&s_pend[0], &s_pend[1], sizeof(s_pend[0]) * (PEND - 1)), s_npend--;
    }
    s_pend[s_npend].c = c;
    s_pend[s_npend].t = p->t_us;
    s_npend++;
}

// The new level times of the receiver: the raw ring, the slicer, the packets
static void pump(void)
{
    if (!s_open || !cur()) {
        return;
    }
    static int32_t buf[1024];
    for (int guard = 0; guard < 16; guard++) {
        int n = cur()->ops->read(cur()->ctx, buf, 1024);
        if (n <= 0) {
            break;
        }
        for (int i = 0; i < n; i++) {
            s_raw[(s_rawn++) & (RAWN - 1)] = buf[i];
        }
        cur()->ops->stats(cur()->ctx, &s_st);
        s_raw_end = s_st.last_edge_us;
        rfd_slicer_feed(s_sl, buf, n, s_raw_end);
        if (n < 1024) {
            break;
        }
    }
    cur()->ops->stats(cur()->ctx, &s_st);
    if (!s_st.level) {
        rfd_slicer_idle(s_sl, (int32_t)(s_st.now_us - s_st.last_edge_us), s_st.now_us);
    }
    rfd_packet_t p;
    while (rfd_slicer_pop(s_sl, &p)) {
        on_packet(&p);
    }
    // the edges a second (each 0.5 s)
    if (s_st.now_us - s_rate_t >= 500000) {
        s_rate = (uint32_t)((uint64_t)(s_st.edges - s_rate_edges) * 1000000u / (uint64_t)(s_st.now_us - s_rate_t));
        s_rate_edges = s_st.edges;
        s_rate_t = s_st.now_us;
    }
}

// ================================================================ the layout and the controls
// the buttons of THE WIRING, the list rows of LIVE, the BACK button of A CODE
static int s_bx[2], s_by[2], s_bw[2], s_bh[2];  // CONNECTED, DEMO
static int s_list_y0, s_row_h, s_strip_y0, s_strip_y1, s_top_h;

// the round frame: the parts of each screen from the size of the circle (466: the cap 88 px, the strip 60 px, rows of
// 51 px). THE WIRING: its two keys in the bottom cap (the points of the box).
static void layout_round(void)
{
    s_top_h = rl_t_top();
    s_strip_y0 = s_top_h + 4;
    s_strip_y1 = s_strip_y0 + gfx_w * 13 / 100;
    s_list_y0 = s_strip_y1 + 6;
    s_row_h = gfx_w * 11 / 100;
    if (s_scr == SC_WIRE) {
        for (int k = 0; k < 2; k++) {
            int x, w;
            rl_t_keyx(k, 2, &x, &w);
            s_bx[k] = x - rl_ox;
            s_bw[k] = w;
            s_by[k] = rl_t_key() - rl_oy;
            s_bh[k] = rl_t_kh();
        }
    } else {
        s_H = gfx_h * 90 / 100;
    }
}

static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    rl_ox = rl_oy = 0;
    rl_big = gfx_round() ? 2 : 0;
    if (gfx_round() && s_scr == SC_WIRE) {  // (THE WIRING: the sketch in the middle box)
        rl_ox = gfx_w * 12 / 100;
        rl_oy = rl_t_top();
        s_W = gfx_w - 2 * rl_ox;
        s_H = rl_t_bot() - rl_oy;
    }
    s_top_h = BAR ? 22 : 26;
    s_strip_y0 = s_top_h + 2;
    s_strip_y1 = s_strip_y0 + (BAR ? 38 : 50);
    s_list_y0 = s_strip_y1 + 4;
    s_row_h = BAR ? 36 : 40;
    // THE WIRING: the two buttons at the right (the 3.49) or at the bottom right (the 320 x 240 frame)
    if (BAR) {
        s_bx[0] = s_bx[1] = s_W - 112;
        s_bw[0] = s_bw[1] = 106;
        s_by[0] = 28;
        s_bh[0] = 48;
        s_by[1] = 82;
        s_bh[1] = 44;
    } else {
        s_bx[0] = s_bx[1] = s_W - 96;
        s_bw[0] = s_bw[1] = 90;
        s_by[0] = s_H - 98;
        s_bh[0] = 46;
        s_by[1] = s_H - 48;
        s_bh[1] = 44;
    }
#if BOARD_UI_TABLET
    // the Tab5 (the frame 640 x 360, px of 0.17 mm): taller parts for the bigger faces of rfd_draw.inc (T5)
    s_top_h = 40;
    s_strip_y0 = s_top_h + 2;
    s_strip_y1 = s_strip_y0 + 84;
    s_list_y0 = s_strip_y1 + 6;
    s_row_h = 58;
    s_bx[0] = s_bx[1] = s_W - 150;
    s_bw[0] = s_bw[1] = 140;
    s_by[0] = s_H - 126;
    s_bh[0] = 56;
    s_by[1] = s_H - 64;
    s_bh[1] = 56;
#endif
    if (gfx_round()) {
        layout_round();
    }
}

static int btn_at(int x, int y)
{
    for (int k = 0; k < 2; k++) {
        if (x >= s_bx[k] - 4 && x < s_bx[k] + s_bw[k] + 4 && y >= s_by[k] - 3 && y < s_by[k] + s_bh[k] + 3) {
            return k;
        }
    }
    return -1;
}

static void go_live(int k)
{
    if (k >= 0 && k != s_srci) {
        src_open(k);
    }
    s_scr = SC_LIVE;
    s_scroll = 0;
}

static bool s_down, s_drag;
static int s_dy0;
static float s_scroll0;

static void input(const input_event_t *ev)
{
    const int x = ev->x - rl_ox, y = ev->y - rl_oy;  // (the round frame: the point in the middle box)
    if (s_scr == SC_WIRE) {
        if (ev->kind != IN_TAP) {
            return;
        }
        int b = btn_at(x, y);
        if (b == 0 && src_index(false) >= 0) {
            store_set("rfd_wired", 1);
            go_live(src_index(false));
        }
        if (b == 1) {
            go_live(src_index(true));
        }
        return;
    }
    if (s_scr == SC_INFO) {
        if (ev->kind == IN_TAP) {
            s_scr = SC_LIVE;  // (a tap anywhere: back; the BACK button shows it)
        }
        return;
    }
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = false;
        s_dy0 = y;
        s_scroll0 = s_scroll;
        break;
    case IN_MOVE:
        if (s_down && y >= s_list_y0 && abs(y - s_dy0) > 8) {
            s_drag = true;
            input_tap_cancel();
        }
        if (s_drag) {
            float mx = (float)(s_nent * s_row_h - (s_H - s_list_y0));
            s_scroll = s_scroll0 - (float)(y - s_dy0);
            if (s_scroll > mx) {
                s_scroll = mx;
            }
            if (s_scroll < 0) {
                s_scroll = 0;
            }
        }
        break;
    case IN_UP:
        s_down = s_drag = false;
        break;
    case IN_TAP:
        if (y >= s_list_y0) {
            int i = (int)((y - s_list_y0 + s_scroll) / s_row_h);
            if (i >= 0 && i < s_nent) {
                s_sel = i;
                s_scr = SC_INFO;
            }
        }
        break;
    default:
        break;
    }
}

// ================================================================ the panel
enum {
    PI_LANG,
    PI_SRC,
    PI_FREQ,
    PI_MODE,
    PI_COD,
    PI_FILT,
    PI_WIRE,
    PI_CLEAR
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const FREQ_VALS[] = {"433.92 MHZ", "433.92 MHZ", "315 MHZ", "315 MHZ"};
static const char *const MODE_VALS[] = {"AUTO", "אוטומטי", "RCSWITCH", "RCSWITCH", "GENERIC", "כללי", "RAW", "גולמי"};
static const char *const COD_VALS[] = {"AUTO", "אוטומטי", "PWM", "PWM", "PPM", "PPM", "MANCHESTER", "MANCHESTER"};
static const char *const FILT_VALS[] = {"40 US", "40 US", "80 US", "80 US", "150 US", "150 US"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "rfd_lang"},
        {PI_PICK, PI_SRC, 2, 0, {"RECEIVER", "מקלט"}, s_src_vals, NULL},
        {PI_PICK, PI_FREQ, 2, 0, {"THE MODULE", "המודול"}, FREQ_VALS, "rfd_freq"},
        {PI_PICK, PI_MODE, 4, 0, {"DECODER", "מפענח"}, MODE_VALS, "rfd_mode"},
        {PI_PICK, PI_COD, 4, 0, {"CODING", "קידוד"}, COD_VALS, "rfd_cod"},
        {PI_PICK, PI_FILT, 3, 1, {"NOISE FILTER", "מסנן רעש"}, FILT_VALS, "rfd_flt"},
        {PI_ACT, PI_WIRE, 0, 0, {"THE WIRING", "החיווט"}, NULL, NULL},
        {PI_ACT2, PI_CLEAR, 0, 0, {"CLEAR THE LIST", "ניקוי הרשימה"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))
static const int CODING_OF[4] = {RFD_K_NONE, RFD_K_PWM, RFD_K_PPM, RFD_K_MANCH};

static bool item_shown(int id)
{
    if (id == PI_SRC) {
        return s_nsrc > 1;
    }
    if (id == PI_COD) {
        return s_mode == RFD_MODE_AUTO || s_mode == RFD_MODE_GENERIC;
    }
    if (id == PI_WIRE) {
        return src_index(false) >= 0;
    }
    return true;
}
static int item_get(int id)
{
    return id == PI_SRC ? (s_srci < 0 ? 0 : s_srci) : 0;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_SRC:
        go_live(v);
        break;
    case PI_FREQ:
        s_freq = v & 1;
        break;
    case PI_MODE:
        s_mode = v & 3;
        s_npend = 0;
        break;
    case PI_COD:
        s_coding = CODING_OF[v & 3];
        s_npend = 0;
        break;
    case PI_FILT:
        s_filt = v % 3;
        if (s_sl) {
            rfd_slicer_init(s_sl, FILT_US[s_filt], 4300);
        }
        break;
    case PI_WIRE:
        if (src_index(false) >= 0) {
            if (s_srci != src_index(false)) {
                src_open(src_index(false));
            }
            s_scr = SC_WIRE;
        }
        break;
    case PI_CLEAR:
        s_nent = 0;
        s_npend = 0;
        s_sel = -1;
        s_scroll = 0;
        if (s_scr == SC_INFO) {
            s_scr = SC_LIVE;
        }
        break;
    }
}
static void panel_opened(void)
{
    s_down = s_drag = false;
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

// ================================================================ enter / leave / update
static void *ps(size_t n)
{
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

static void enter(int64_t now)
{
    s_now = now;
    layout();
    s_he = store_get("rfd_lang", 1) == 1;  // (Hebrew when nothing is saved)
    s_freq = (int)(store_get("rfd_freq", 0) & 1);
    s_mode = (int)(store_get("rfd_mode", 0) & 3);
    s_coding = CODING_OF[store_get("rfd_cod", 0) & 3];
    s_filt = (int)(store_get("rfd_flt", 1) % 3);
    s_nsrc = rfp_enum(s_src, NSRC, false);
    for (int k = 0; k < s_nsrc; k++) {
        s_src_vals[2 * k] = s_src[k].caps->name[0];
        s_src_vals[2 * k + 1] = s_src[k].caps->name[1];
    }
    s_items[1].n = (uint8_t)(s_nsrc < 2 ? 2 : s_nsrc);
    if (!s_raw) {
        s_raw = ps(sizeof(int32_t) * RAWN);
    }
    if (!s_sl) {
        s_sl = ps(sizeof(rfd_slicer_t));
    }
    if (!s_ent) {
        s_ent = ps(sizeof(ent_t) * ENT_MAX);
    }
    s_nent = s_npend = 0;
    s_sel = -1;
    s_scroll = 0;
    s_toast[0] = 0;
    s_down = s_drag = false;
    s_srci = -1;
    s_open = false;
    sysui_panel_items(&PANEL);
    if (!s_raw || !s_sl || !s_ent) {
        s_scr = SC_LIVE;
        toast("NO MEMORY");
        return;
    }
    int g = src_index(false), d = src_index(true);
    int want = (int)store_get("rfd_src", g >= 0 ? (uint32_t)g : 0);
    if (want < 0 || want >= s_nsrc) {
        want = 0;
    }
    if (g >= 0 && store_get("rfd_wired", 0) != 1) {
        src_open(g);  // (THE WIRING shows the live state of the pin)
        s_scr = SC_WIRE;
    } else if (g < 0) {
        go_live(d);
        toast(TX(T_NOPINS));
    } else {
        go_live(want);
    }
}

static void leave(void)
{
    src_close();
    store_set("rfd_lang", s_he);
}

static void update(int64_t now)
{
    s_now = now;
    pump();
}

static bool awake(void)
{
    return true;
}  // (a tool that one watches: the screen stays on)

#include "rfd_draw.inc"

const app_t app_rfdec = {
        .name = "RF DECODER",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round boards: the native round frame, the middle box of layout())
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
