// IR DECODER: the codes of infrared remote controls, from an IR receiver module on the pins of the board.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_irdec at the end of this file);
// update reads the level times of the receiver (libs/rfpulse) and decodes them.
// The memory: the file statics; the raw ring s_raw, the SEND frames s_loop, the slicer s_sl and the list s_ent in
// PSRAM (the first enter makes them; they stay while the app file is loaded).
//
// The receiver: a TSOP38238 or a VS1838B (a KY-022 board). In the style of the Arduino IRremote library: the protocol
// by itself (NEC, SAMSUNG, SONY, RC5, RC6, PANASONIC, LG, JVC, DENON and more), the address and the command, the raw data; a
// frame of no known protocol: the universal decoder (PULSE DISTANCE, PULSE WIDTH), else UNKNOWN with its hash and the
// raw times. SEND: an IR LED module on the TX pin sends the code again (the RMT makes the carrier).
//
// THE LAYERS (each one knows only the layer under it):
//   libs/rfpulse   the receivers (rfpulse.h: rfp_enum_ir, the IR receiver on the IR pin, the IR demo), the IR LED
//                  (rfp_ir_send) and the wiring of each board (rfp_board_ir(): the pads, their roles, the GPIOs)
//   libs/irdecode  the slicer (a stream into frames), the decoders and the encoders; pure C
//   this app       the screens: THE WIRING (a sketch of the connector of this board and the two modules, with the live
//                  state of the pin), LIVE (the raw marks and the list of the codes), A CODE (its fields, bits, timing,
//                  the raw times, SEND)
// The RF DECODER app (apps/rf_decoder) is the same plan for 315 / 433 MHz remotes; both receivers can stay connected.
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
// functions of ird_draw.inc): the state in the top cap, the strip and the pulses in the width of the chord, the rows
// of the list follow the chord, SEND in the bottom cap. THE WIRING keeps the sketch of the flat frame in the middle
// box (rl_origin.h moves each draw call by (rl_ox, rl_oy), input() takes the origin off each touch point), its title
// in the top cap and its keys in the bottom cap. The Rubik faces are KF_TEXT at the least (rl_big 2).
#include "rndlay/rndlay.h"
#include "rndlay/rl_origin.h"
#include "irdecode/irdecode.h"
#include "rfpulse/rfp_board.h"
#include "rfpulse/rfpulse.h"

static bool s_he;  // the language: false English, true Hebrew
#include "ird_text.inc"
#include "ird_fonts.inc"

#define BAR BOARD_BAR_SCREEN
static int s_W, s_H;

// ================================================================ the receivers
#define NSRC 3
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
static int s_mode;  // 0 AUTO, 1 RAW ONLY
static int64_t s_now;
static rfp_stats_t s_st;
static uint32_t s_rate;  // the edges a second (the line state)
static uint32_t s_rate_edges;
static int64_t s_rate_t;
static bool s_seen;  // the IR pin had a signal since the open (THE WIRING: CONNECTED)

// the raw levels: a ring of the last level times (the strip of LIVE)
#define RAWN 4096
static int32_t *s_raw;
static uint32_t s_rawn;    // all level times since the open (the ring index)
static int64_t s_raw_end;  // the time of the end of the newest one

// the slicer and the codes
static ird_slicer_t *s_sl;
#define ENT_MAX 40
typedef struct {
    ird_code_t c;
    int64_t t_first, t_last;
    uint16_t reps;
    int16_t n;  // the level times of its last frame (A CODE, and SEND of an UNKNOWN code)
    int32_t d[IRD_MAX_DUR];
} ent_t;
static ent_t *s_ent;  // the newest first
static int s_nent;
static int s_sel = -1;  // the code of A CODE

// the frames that SEND gives to the demo receiver (the demo has no LED: the app receives them itself)
#define LOOPN 2400
static int32_t *s_loop;
static int s_nloop;

static float s_scroll;   // the list (px)
static float s_iscroll;  // the text of A CODE (px)
static int s_itext_h;    // the height of that text (the render sets it)
static int64_t s_toast_t;
static char s_toast[96];
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
static bool cur_demo(void)
{
    return cur() && (cur()->caps->flags & RFP_F_DEMO);
}
// SEND is possible: an IR LED pin (the receiver on the pins), or the demo (it receives the code)
static bool can_send(void)
{
    return cur_demo() || (cur() && rfp_board_ir()->tx_gpio >= 0);
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
    s_seen = false;
    if (!cur()) {
        return;
    }
    int e = cur()->ops->open(cur()->ctx);
    if (e < 0) {
        s_err = e;
        return;
    }
    s_open = true;
    ird_slicer_init(s_sl, 60);
    s_rawn = 0;
    s_raw_end = now_us();
    s_rate = 0;
    s_rate_edges = 0;
    s_rate_t = now_us();
    s_nloop = 0;
    store_set("ird_src", (uint32_t)k);
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
static void add_entry(const ird_code_t *c, const ird_frame_t *f)
{
    if (s_nent == ENT_MAX) {
        s_nent--;
    }
    memmove(&s_ent[1], &s_ent[0], sizeof(ent_t) * (size_t)s_nent);
    s_nent++;
    ent_t *e = &s_ent[0];
    e->c = *c;
    e->t_first = e->t_last = f->t_us;
    e->reps = 1;
    e->n = (int16_t)f->n;
    memcpy(e->d, f->d, sizeof(int32_t) * (size_t)f->n);
    if (s_sel >= 0) {
        s_sel++;  // (the code of A CODE moves down one place)
    }
}

// A repeat frame belongs to the code of the newest entry: the NEC repeat to NEC, ONKYO, APPLE and LG; the SAMSUNG
// repeat to SAMSUNG; a JVC frame with no header and the second DENON / SHARP frame to the same command
static bool repeat_of(const ird_code_t *e, const ird_code_t *r)
{
    switch (r->proto) {
    case IRD_P_NEC:
        return e->proto == IRD_P_NEC || e->proto == IRD_P_ONKYO || e->proto == IRD_P_APPLE || e->proto == IRD_P_LG;
    case IRD_P_SAMSUNG:
        return e->proto == IRD_P_SAMSUNG || e->proto == IRD_P_SAMSUNG48;
    default:
        return e->proto == r->proto && e->address == r->address && e->command == r->command;
    }
}

static void on_frame(const ird_frame_t *f)
{
    ird_code_t c;
    if (!ird_decode(f->d, f->n, s_mode == 1, &c)) {
        return;
    }
    if (c.proto == IRD_P_UNKNOWN && s_mode == 0 && f->n < 8) {
        return;  // (a few spikes of a lamp: no code)
    }
    ent_t *e = s_nent > 0 ? &s_ent[0] : NULL;
    // a held key: its repeat frames (or the same frame again) within 0.3 s of the last one: one more repeat
    if (e && (c.flags & IRD_F_REPEAT) && repeat_of(&e->c, &c) && f->t_us - e->t_last < 300000) {
        e->reps++;
        e->t_last = f->t_us;
        return;
    }
    if (e && ird_same(&e->c, &c) && f->t_us - e->t_last < 1000000) {
        e->reps++;
        e->t_last = f->t_us;
        e->n = (int16_t)f->n;
        memcpy(e->d, f->d, sizeof(int32_t) * (size_t)f->n);
        return;
    }
    add_entry(&c, f);
}

// The new level times of the receiver: the raw ring, the slicer, the frames
static void pump(void)
{
    if (!s_open || !cur()) {
        return;
    }
    static int32_t buf[1024];
    if (s_nloop > 0) {  // (SEND to the demo: the frames of the code, then a gap)
        ird_slicer_feed(s_sl, s_loop, s_nloop, now_us());
        s_nloop = 0;
    }
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
        ird_slicer_feed(s_sl, buf, n, s_raw_end);
        if (n < 1024) {
            break;
        }
    }
    cur()->ops->stats(cur()->ctx, &s_st);
    if (s_st.line == RFP_LINE_ALIVE) {
        s_seen = true;
    }
    if (!s_st.level) {
        ird_slicer_idle(s_sl, (int32_t)(s_st.now_us - s_st.last_edge_us), s_st.now_us);
    }
    static ird_frame_t s_f;  // (no frame on the stack: 2.4 KB)
    while (ird_slicer_pop(s_sl, &s_f)) {
        on_frame(&s_f);
    }
    // the edges a second (each 0.5 s)
    if (s_st.now_us - s_rate_t >= 500000) {
        s_rate = (uint32_t)((uint64_t)(s_st.edges - s_rate_edges) * 1000000u / (uint64_t)(s_st.now_us - s_rate_t));
        s_rate_edges = s_st.edges;
        s_rate_t = s_st.now_us;
    }
}

// SEND: one key press of the code (the frames of its protocol), or the raw times of an UNKNOWN code
static void send(const ent_t *e)
{
    static int32_t d[LOOPN];
    int n = 0;
    uint32_t hz = e->c.carrier_hz ? e->c.carrier_hz : 38000;
    if (e->c.proto != IRD_P_UNKNOWN && !(e->c.flags & IRD_F_REPEAT && e->c.proto != IRD_P_JVC)) {
        n = ird_encode_press(&e->c, d, LOOPN);
    }
    if (n <= 0) {
        // the raw times: each mark less the excess of the receiver (about 50 us), each space more
        for (int i = 0; i < e->n && i < LOOPN; i++) {
            d[n++] = e->d[i] > 0 ? (e->d[i] > 100 ? e->d[i] - 50 : e->d[i]) : e->d[i] - 50;
        }
    }
    if (n <= 0) {
        return;
    }
    if (cur_demo()) {
        if (s_nloop + n + 2 > LOOPN) {
            return;
        }
        s_loop[s_nloop++] = -(IRD_GAP_US + 12000);
        memcpy(&s_loop[s_nloop], d, sizeof(int32_t) * (size_t)n);
        s_nloop += n;
        s_loop[s_nloop++] = -(IRD_GAP_US + 12000);
        toast(TX(T_SENT_DEMO));
        return;
    }
    int r = rfp_ir_send(d, n, hz);
    if (r == 0) {
        toast(TX(T_SENT));
    } else {
        toast(r == -2 ? TX(T_NOLED) : rfp_ir_send_error(r));
    }
}

// ================================================================ the layout and the controls
// the buttons of THE WIRING, the list rows of LIVE, the BACK and SEND buttons of A CODE
static int s_bx[2], s_by[2], s_bw[2], s_bh[2];  // CONNECTED, DEMO
static int s_list_y0, s_row_h, s_strip_y0, s_strip_y1, s_top_h;
static int s_send_x, s_send_y, s_send_w, s_send_h;
static int s_itext_y0;                    // the top of the text of A CODE
static int s_back_w = 84, s_back_h = 36;  // BACK of A CODE

// the round frame: the parts of each screen from the size of the circle (466: the cap 88 px, the strip 60 px, rows of
// 51 px; A CODE: its text from y 186 to the keys). THE WIRING: its two keys in the bottom cap (the points of the box).
static void layout_round(void)
{
    s_top_h = rl_t_top();
    s_strip_y0 = s_top_h + 4;
    s_strip_y1 = s_strip_y0 + gfx_w * 13 / 100;
    s_list_y0 = s_strip_y1 + 6;
    s_row_h = gfx_w * 11 / 100;
    s_itext_y0 = s_top_h + gfx_w * 21 / 100;
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
        s_H = s_scr == SC_INFO ? rl_t_bot() : gfx_h * 90 / 100;
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
        s_by[0] = s_H - 90;
        s_bh[0] = 42;
        s_by[1] = s_H - 44;
        s_bh[1] = 40;
    }
    s_send_w = BAR ? 110 : 90;
    s_send_h = 36;
    s_send_x = s_W - s_send_w - 4;
    s_send_y = 4;
    s_itext_y0 = (BAR ? 82 : 96) + 2;
#if BOARD_UI_TABLET
    // the Tab5 (the frame 640 x 360, px of 0.17 mm): taller parts for the bigger faces of ird_draw.inc (T5)
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
    s_send_w = 130;
    s_send_h = 50;
    s_send_x = s_W - s_send_w - 4;
    s_itext_y0 = 150;
    s_back_w = 108;
    s_back_h = 50;
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
    const int x = ev->x - rl_ox, y = ev->y - rl_oy;               // (the round frame: the point in the middle box)
    if (gfx_round() && s_scr == SC_INFO && ev->kind == IN_TAP) {  // (the round frame: BACK in the top cap, SEND below)
        if (y < s_top_h) {
            s_scr = SC_LIVE;
        } else if (y >= rl_t_key() - 4 && can_send() && s_sel >= 0 && s_sel < s_nent) {
            send(&s_ent[s_sel]);
        }
        return;
    }
    if (s_scr == SC_WIRE) {
        if (ev->kind != IN_TAP) {
            return;
        }
        int b = btn_at(x, y);
        if (b == 0 && src_index(false) >= 0) {
            store_set("ird_wired", 1);
            go_live(src_index(false));
        }
        if (b == 1) {
            go_live(src_index(true));
        }
        return;
    }
    float *scr = s_scr == SC_INFO ? &s_iscroll : &s_scroll;
    const int area_y0 = s_scr == SC_INFO ? s_itext_y0 : s_list_y0;
    const float content = s_scr == SC_INFO ? (float)s_itext_h : (float)(s_nent * s_row_h);
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = false;
        s_dy0 = y;
        s_scroll0 = *scr;
        break;
    case IN_MOVE:
        if (s_down && y >= area_y0 && abs(y - s_dy0) > 8) {
            s_drag = true;
            input_tap_cancel();
        }
        if (s_drag) {
            float mx = content - (float)(s_H - area_y0);
            *scr = s_scroll0 - (float)(y - s_dy0);
            if (*scr > mx) {
                *scr = mx;
            }
            if (*scr < 0) {
                *scr = 0;
            }
        }
        break;
    case IN_UP:
        s_down = s_drag = false;
        break;
    case IN_TAP:
        if (s_scr == SC_INFO) {
            if (x < s_back_w + 12 && y < s_back_h + 10) {
                s_scr = SC_LIVE;
            } else if (can_send() && s_sel >= 0 && s_sel < s_nent && x >= s_send_x - 6 && y < s_send_y + s_send_h + 6) {
                send(&s_ent[s_sel]);
            }
            break;
        }
        if (y >= s_list_y0) {
            int i = (int)((y - s_list_y0 + s_scroll) / s_row_h);
            if (i >= 0 && i < s_nent) {
                s_sel = i;
                s_iscroll = 0;
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
    PI_MODE,
    PI_WIRE,
    PI_CLEAR
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const MODE_VALS[] = {"AUTO", "אוטומטי", "RAW ONLY", "גולמי בלבד"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "ird_lang"},
        {PI_PICK, PI_SRC, 2, 0, {"RECEIVER", "מקלט"}, s_src_vals, NULL},
        {PI_PICK, PI_MODE, 2, 0, {"DECODER", "מפענח"}, MODE_VALS, "ird_mode"},
        {PI_ACT, PI_WIRE, 0, 0, {"THE WIRING", "החיווט"}, NULL, NULL},
        {PI_ACT2, PI_CLEAR, 0, 0, {"CLEAR THE LIST", "ניקוי הרשימה"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))

static bool item_shown(int id)
{
    if (id == PI_SRC) {
        return s_nsrc > 1;
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
    case PI_MODE:
        s_mode = v & 1;
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
    s_he = store_get("ird_lang", 1) == 1;  // (Hebrew when nothing is saved)
    s_mode = (int)(store_get("ird_mode", 0) & 1);
    s_nsrc = rfp_enum_ir(s_src, NSRC);
    for (int k = 0; k < s_nsrc; k++) {
        s_src_vals[2 * k] = s_src[k].caps->name[0];
        s_src_vals[2 * k + 1] = s_src[k].caps->name[1];
    }
    s_items[1].n = (uint8_t)(s_nsrc < 2 ? 2 : s_nsrc);
    if (!s_raw) {
        s_raw = ps(sizeof(int32_t) * RAWN);
    }
    if (!s_sl) {
        s_sl = ps(sizeof(ird_slicer_t));
    }
    if (!s_ent) {
        s_ent = ps(sizeof(ent_t) * ENT_MAX);
    }
    if (!s_loop) {
        s_loop = ps(sizeof(int32_t) * LOOPN);
    }
    s_nent = 0;
    s_nloop = 0;
    s_sel = -1;
    s_scroll = s_iscroll = 0;
    s_toast[0] = 0;
    s_down = s_drag = false;
    s_srci = -1;
    s_open = false;
    sysui_panel_items(&PANEL);
    if (!s_raw || !s_sl || !s_ent || !s_loop) {
        s_scr = SC_LIVE;
        toast("NO MEMORY");
        return;
    }
    int g = src_index(false), d = src_index(true);
    int want = (int)store_get("ird_src", g >= 0 ? (uint32_t)g : 0);
    if (want < 0 || want >= s_nsrc) {
        want = 0;
    }
    if (g >= 0 && store_get("ird_wired", 0) != 1) {
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
    store_set("ird_lang", s_he);
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

#include "ird_draw.inc"

const app_t app_irdec = {
        .name = "IR DECODER",
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
