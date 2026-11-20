// TOUCH LAB: a test app that shows what the touch panel detects, for the games with two thumbs. It shows the raw data
// of the panel, so that a fault of the input (as a jump that does not come each time in WRONG ROOM) is measured, not
// assumed.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_touchlab at the end of this file).
// The memory: the file statics; s_raw and s_ev in PSRAM (lab_enter makes them, lab_leave frees them).
//
// - THE PANEL: each report of the touch chip (the 3.49 driver keeps them: board_touch_reports, 100 a second), drawn as
//   small dots with the id and the event bits of the chip, and a TIMELINE of the last 3 s, one column for each
//   report: green at the top = a point in the left half, orange at the bottom = a point in the right half, a grey dot
//   = a report with no point, red = a BROKEN report (a point off the panel).
// - THE GAME VIEW: the points that a game gets (input_points: after the finger tracker), with the rule of WRONG ROOM:
//   the left thumb = the point most to the left in the left half (a stick from where it came down), the right thumb =
//   the point most to the right in the right half. Big rings: green = the left thumb, orange = the right thumb.
// - THE COUNTS: RIGHT = the right thumb came down (a press: a jump in WRONG ROOM), TAP = it went up within 0.35 s, LONG
//   = later, LEFT LOST = the left thumb went away while the right thumb was down, BROKEN = broken reports. While both
//   thumbs are down: how many reports had both points, one point, or none. The last right touches: their time and the
//   reports during each one. The same lines go to the console ("lab: ...").
// - RESET (the box at the top right) sets the counts to 0. The test: hold the left thumb, tap with the right thumb N
//   times, compare RIGHT with N.
// - FREEZE (the box left of RESET): the strip and the counts stop, so the last 3 s can be read at any speed; the raw
//   reports of those 3 s and the last game events go to /data/touchlab/f<k>.txt (http://<board>/f/touchlab/f<k>.txt).
//   A second tap goes on.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "tfn/app.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/uikit.h"
#include "aatext/aatext.h"

// THE TEXT: the Bungee faces of the firmware in place of the 5 x 7 dot font (libs/aatext)
#define gfx_text           aat_text
#define gfx_text_width     aat_w
#define gfx_text_center    aat_text_center
#define gfx_text_center_in aat_text_center_in

static const char *TAG = "lab";

#define TL_N   300    // the timeline: the last 300 reports (3 s at 100 a second)
#define TAP_S  0.35f  // a tap of WRONG ROOM: up within this time
#define HIST_N 5
enum {
    K_NONE,
    K_L,
    K_R,
    K_B,
    K_BROKEN
};

static uint8_t s_tl[TL_N];
static int s_tl_w, s_tl_n;
static touch_rep_t s_last;  // the last report with points (its dots)
static int64_t s_last_t;
static int s_rep_n, s_rep_data, s_broken, s_fixed;  // (s_fixed: a broken report that a read again made good)
static int s_press, s_taps, s_long, s_left_lost;
static int s_two_b, s_two_one, s_two_e;   // the reports while the game sees both thumbs
static int s_d_b, s_d_one, s_d_e, s_d_x;  // the reports during this right touch
typedef struct {
    int ms, b, one, e, x;
} touch_t;
static touch_t s_hist[HIST_N];
static int s_hist_n;
static bool s_lon, s_ron;
static int s_lx, s_ly, s_rx, s_ry, s_cx, s_cy;  // the thumbs and the centre of the stick (logical)
static int64_t s_now, s_rt0, s_rate_t;
static int s_rate, s_rate_n;

// FREEZE: the raw reports of the timeline (in its order) and the last game events, in PSRAM while the app is open
#define EV_N 60
typedef struct {
    uint32_t t;
    char s[60];
} lev_t;
static touch_rep_t *s_raw;
static lev_t *s_ev;
static int s_ev_w, s_ev_n;
static bool s_frozen;
static int s_freeze_k, s_saved_k;

// (no vsnprintf, fprintf or fputc: an app file of TFN OS has only snprintf and fwrite of these names)
static lev_t *s_ev_cur;
static lev_t *ev_slot(void)
{
    if (!s_ev) {
        return NULL;
    }
    lev_t *e = &s_ev[s_ev_w];
    e->t = (uint32_t)s_now;
    s_ev_w = (s_ev_w + 1) % EV_N;
    if (s_ev_n < EV_N) {
        s_ev_n++;
    }
    return e;
}
#define ev_add(...) ((void)((s_ev_cur = ev_slot()) && snprintf(s_ev_cur->s, sizeof s_ev_cur->s, __VA_ARGS__)))
// one formatted line part into a file
#define FPF(f, ...)                                                                                                    \
    do {                                                                                                               \
        char b_[320];                                                                                                  \
        int n_ = snprintf(b_, sizeof b_, __VA_ARGS__);                                                                 \
        if (n_ > 0)                                                                                                    \
            fwrite(b_, 1, n_ < (int)sizeof b_ ? (size_t)n_ : sizeof b_ - 1, (f));                                      \
    } while (0)

static void lab_reset(void)
{
    memset(s_tl, 0, sizeof s_tl);
    s_tl_w = s_tl_n = 0;
    s_rep_n = s_rep_data = s_broken = s_fixed = 0;
    s_press = s_taps = s_long = s_left_lost = 0;
    s_two_b = s_two_one = s_two_e = 0;
    s_hist_n = 0;
    s_last_t = 0;
    s_ev_w = s_ev_n = 0;
}

// the Tab5 (640 x 360, 0.17 mm a pixel): bigger words and boxes (TL_TAB5)
#if BOARD_UI_TABLET
#define TL_TAB5 1
#else
#define TL_TAB5 0
#endif
static void reset_box(int *x, int *y, int *w, int *h)
{
    if (gfx_round()) {  // (the round frame: RESET at the right of the bottom cap, FREEZE at its left)
        *w = gfx_w * 104 / 466;
        *h = 48;
        *x = gfx_w / 2 + 6;
        *y = gfx_h * 384 / 466;
        return;
    }
    *w = BOARD_BAR_SCREEN ? 76 : TL_TAB5 ? 92 : 46;
    *h = BOARD_BAR_SCREEN ? 22 : TL_TAB5 ? 36 : 20;
    *x = gfx_w - *w;
    *y = 0;
}

// FREEZE: left of RESET, the same size
static void freeze_box(int *x, int *y, int *w, int *h)
{
    reset_box(x, y, w, h);
    *x -= *w + 6;
}

static bool in_box4(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx - 4 && x < bx + bw + 4 && y >= by - 4 && y < by + bh + 4;
}
static bool in_reset(int x, int y)
{
    int bx, by, bw, bh;
    reset_box(&bx, &by, &bw, &bh);
    if (gfx_round()) {
        return in_box4(x, y, bx, by, bw, bh);
    }
    return x >= bx - 4 && y < by + bh + 4;
}

// RESET or FREEZE (the game view does not count a thumb there)
static bool in_boxes(int x, int y)
{
    int bx, by, bw, bh;
    freeze_box(&bx, &by, &bw, &bh);
    if (gfx_round()) {
        return in_box4(x, y, bx, by, bw, bh) || in_reset(x, y);
    }
    return x >= bx - 4 && y < by + bh + 4;
}

// the frozen 3 s to /data/touchlab/f<k>.txt: the raw reports (oldest first), then the game events
static void freeze_save(void)
{
    s_saved_k = -1;
#ifdef ESP_PLATFORM
    if (!s_raw) {
        return;
    }
    mkdir("/data/touchlab", 0777);
    char path[48];
    snprintf(path, sizeof path, "/data/touchlab/f%d.txt", ++s_freeze_k);
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "freeze: cannot write %s", path);
        return;
    }
    FPF(f,
        "TOUCH LAB freeze %d at %lu ms: %d reports (r <ms> <count byte> <points> x,y,id,event ...; x,y = board pixels"
        " as the chip sent them; then L/R/B/-/X = left, right, both, none, broken), then the game events (e <ms> "
        "...)\n",
        s_freeze_k, (unsigned long)s_now, s_tl_n);
    static const char KC[] = {'-', 'L', 'R', 'B', 'X'};
    for (int i = 0; i < s_tl_n; i++) {
        int k = (s_tl_w - s_tl_n + i + TL_N) % TL_N;
        const touch_rep_t *r = &s_raw[k];
        FPF(f, "r %lu %u %u", (unsigned long)r->t_ms, r->count, r->n);
        for (int j = 0; j < r->n; j++) {
            FPF(f, " %d,%d,%u,%u", r->p[j].x, r->p[j].y, r->p[j].id, r->p[j].ev);
        }
        FPF(f, " %c", KC[s_tl[k] < 5 ? s_tl[k] : 0]);
        if (r->shift || r->bad) {
            FPF(f, " %s, the read:", r->bad ? "BROKEN" : r->shift == 1 ? "shift 1 (put back)" : "shift 2 (put back)");
            for (int j = 0; j < 14; j++) {
                FPF(f, " %02x", r->raw[j]);
            }
        }
        FPF(f, "\n");
    }
    for (int i = 0; i < s_ev_n; i++) {
        const lev_t *e = &s_ev[(s_ev_w - s_ev_n + i + EV_N) % EV_N];
        FPF(f, "e %lu %s\n", (unsigned long)e->t, e->s);
    }
    fclose(f);
    s_saved_k = s_freeze_k;
    ESP_LOGI(TAG, "frozen: the last 3 s are in %s", path);
#endif
}

static void lab_enter(int64_t now)
{
    s_now = s_rate_t = now;  // (the time of an app is in ms)
    s_rate = s_rate_n = 0;
    s_lon = s_ron = false;
    s_frozen = false;
    s_saved_k = 0;
    if (!s_raw) {
        s_raw = heap_caps_calloc(TL_N, sizeof(touch_rep_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_ev) {
        s_ev = heap_caps_calloc(EV_N, sizeof(lev_t), MALLOC_CAP_SPIRAM);
    }
    lab_reset();
    board_touch_log(true);
    ESP_LOGI(TAG, "TOUCH LAB: hold the left thumb, tap with the right thumb, compare RIGHT with your taps");
}

static void lab_leave(void)
{
    board_touch_log(false);
    heap_caps_free(s_raw);
    heap_caps_free(s_ev);
    s_raw = NULL;
    s_ev = NULL;
}

static void lab_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    int fx, fy, fw, fh;
    freeze_box(&fx, &fy, &fw, &fh);
    const bool on_f = gfx_round() ? in_box4(ev->x, ev->y, fx, fy, fw, fh)
                                  : ev->x >= fx - 4 && ev->x < fx + fw + 3 && ev->y < fy + fh + 4;
    if (on_f) {
        s_frozen = !s_frozen;
        if (s_frozen) {
            freeze_save();
        } else {
            ESP_LOGI(TAG, "go on");
        }
    } else if (in_reset(ev->x, ev->y) && !s_frozen) {
        lab_reset();
        ESP_LOGI(TAG, "reset");
    }
}

// the kind of one report (the halves by the logical x)
static int rep_kind(const touch_rep_t *r)
{
    if (r->bad) {
        return K_BROKEN;  // (still broken after the reads again)
    }
    if (r->n == 0) {
        return K_NONE;
    }
    bool l = false, rr = false;
    for (int i = 0; i < r->n; i++) {
        int x = r->p[i].x, y = r->p[i].y;
        if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) {
            return K_BROKEN;
        }
        int lx, ly;
        gfx_touch_to_logical(x, y, &lx, &ly);
        if (lx < gfx_w / 2) {
            l = true;
        } else {
            rr = true;
        }
    }
    return l && rr ? K_B : l ? K_L : K_R;
}

static void lab_update(int64_t now)
{
    s_now = now;
    // the panel: each report since the last frame
    touch_rep_t rep[48];
    int nr = board_touch_reports(rep, 48);
    if (s_frozen) {
        return;  // (the ring of the driver is read, so it does not fill; all stays)
    }
    for (int i = 0; i < nr; i++) {
        int k = rep_kind(&rep[i]);
        if (s_raw) {
            s_raw[s_tl_w] = rep[i];
        }
        s_tl[s_tl_w] = (uint8_t)k;
        s_tl_w = (s_tl_w + 1) % TL_N;
        if (s_tl_n < TL_N) {
            s_tl_n++;
        }
        s_rep_n++;
        s_rate_n++;
        if (rep[i].n) {
            s_rep_data++;
            s_last = rep[i];
            s_last_t = s_now;
        }
        if (rep[i].shift && !rep[i].bad) {
            s_fixed++;
            if (s_fixed <= 20) {
                ESP_LOGI(TAG,
                         "shifted by %d byte(s), put back (%d): %02x %02x %02x %02x %02x %02x %02x %02x, %d points",
                         rep[i].shift, s_fixed, rep[i].raw[0], rep[i].raw[1], rep[i].raw[2], rep[i].raw[3],
                         rep[i].raw[4], rep[i].raw[5], rep[i].raw[6], rep[i].raw[7], rep[i].n);
            }
        }
        if (k == K_BROKEN) {
            s_broken++;
            const uint8_t *w = rep[i].raw;  // (the 14 bytes as they came from the chip)
            ESP_LOGI(TAG,
                     "broken report %d (count %d, %d points): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                     "%02x %02x %02x",
                     s_broken, rep[i].count, rep[i].n, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9],
                     w[10], w[11], w[12], w[13]);
        }
        if (s_ron) {
            if (k == K_B) {
                s_d_b++;
            } else if (k == K_L || k == K_R) {
                s_d_one++;
            } else if (k == K_NONE) {
                s_d_e++;
            } else {
                s_d_x++;
            }
        }
        if (s_lon && s_ron) {
            if (k == K_B) {
                s_two_b++;
            } else if (k == K_NONE) {
                s_two_e++;
            } else {
                s_two_one++;
            }
        }
    }
    if (s_now - s_rate_t >= 1000) {
        s_rate = s_rate_n;
        s_rate_n = 0;
        s_rate_t = s_now;
    }
    // the game view: the points after the finger tracker, with the rule of WRONG ROOM
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    bool left = false, right = false;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    for (int i = 0; i < n; i++) {
        int x, y;
        gfx_touch_to_logical(px[i], py[i], &x, &y);
        if (in_boxes(x, y)) {
            continue;
        }
        if (x < gfx_w / 2) {
            if (!left || x < lx) {
                left = true, lx = x, ly = y;
            }
        } else if (!right || x > rx) {
            right = true, rx = x, ry = y;
        }
    }
    if (left && !s_lon) {
        s_cx = lx, s_cy = ly, ev_add("LEFT down at %d,%d", lx, ly);  // the stick starts there
    }
    if (!left && s_lon) {
        ev_add("LEFT up (last at %d,%d)%s", s_lx, s_ly, right || s_ron ? " - LOST while the right is down" : "");
    }
    if (!left && s_lon && (right || s_ron)) {
        s_left_lost++;
        ESP_LOGI(TAG, "LEFT LOST while the right thumb is down (%d)", s_left_lost);
    }
    if (right && !s_ron) {
        s_press++;
        s_rt0 = s_now;
        s_d_b = s_d_one = s_d_e = s_d_x = 0;
        ESP_LOGI(TAG, "RIGHT %d down at %d,%d (the left thumb %s)", s_press, rx, ry, left ? "down" : "up");
        ev_add("RIGHT %d down at %d,%d (left %s at %d,%d)", s_press, rx, ry, left ? "down" : "up", lx, ly);
    }
    if (!right && s_ron) {
        int ms = (int)(s_now - s_rt0);
        if (ms < (int)(TAP_S * 1000)) {
            s_taps++;
        } else {
            s_long++;
        }
        memmove(&s_hist[1], &s_hist[0], sizeof(touch_t) * (HIST_N - 1));
        s_hist[0] = (touch_t){ms, s_d_b, s_d_one, s_d_e, s_d_x};
        if (s_hist_n < HIST_N) {
            s_hist_n++;
        }
        ESP_LOGI(TAG, "RIGHT %d up after %d ms (%s); the reports in it: both %d, one %d, none %d, broken %d", s_press,
                 ms, ms < (int)(TAP_S * 1000) ? "TAP" : "LONG", s_d_b, s_d_one, s_d_e, s_d_x);
        ev_add("RIGHT %d up after %d ms", s_press, ms);
    }
    s_lon = left;
    s_ron = right;
    if (left) {
        s_lx = lx, s_ly = ly;
    }
    if (right) {
        s_rx = rx, s_ry = ry;
    }
}

// the round frame (APP_ROUND_HI): the counts in the top cap, the thumbs in the whole wide middle, the last touches and
// the timeline under them (as wide as the circle there), FREEZE and RESET in the bottom cap; the smooth fonts
static void lab_render_round(void)
{
    const uint16_t BG = rgb(10, 14, 30), DIM = rgb(110, 120, 150), INK = rgb(235, 240, 255), GRN = rgb(80, 230, 120),
                   ORA = rgb(255, 160, 60), RED = rgb(255, 60, 60), GRY = rgb(110, 110, 120);
    const int W = gfx_w, H = gfx_h, cx = W / 2;
    gfx_fill(BG);
    char b[120];
    snprintf(b, sizeof b, "RIGHT %d  TAP %d  LONG %d", s_press, s_taps, s_long);
    ui_text_c(&KF_NAME, cx, H * 62 / 466, b, INK);
    snprintf(b, sizeof b, "LEFT LOST %d  BROKEN %d", s_left_lost, s_broken);
    ui_text_c(&KF_NAME, cx, H * 86 / 466, b, s_left_lost || s_broken ? ORA : INK);
    int two = s_two_b + s_two_one + s_two_e;
    if (s_frozen) {
        snprintf(b, sizeof b, s_saved_k > 0 ? "FROZEN: /data/touchlab/f%d.txt" : "FROZEN (NOT SAVED)", s_saved_k);
    } else if (two) {
        snprintf(b, sizeof b, "2 THUMBS: BOTH %d%%  ONE %d%%  NONE %d%%", s_two_b * 100 / two, s_two_one * 100 / two,
                 s_two_e * 100 / two);
    } else {
        snprintf(b, sizeof b, "HOLD LEFT, TAP RIGHT  -  %d/S", s_rate);
    }
    ui_text_c(&KF_TEXT, cx, H * 112 / 466, b, s_frozen ? rgb(120, 200, 255) : DIM);
    // the area of the thumbs: the line between the halves, the names of the halves
    const int ya = H * 124 / 466, yb = H * 300 / 466;
    for (int y = ya; y < yb; y += 6) {
        gfx_fill_rect(cx, y, 1, 3, DIM);
    }
    ui_text_c(&KF_TEXT, cx - W * 120 / 466, H * 150 / 466, "LEFT THUMB", DIM);
    ui_text_c(&KF_TEXT, cx + W * 120 / 466, H * 150 / 466, "RIGHT THUMB", DIM);
    if (s_lon) {
        int dx = s_lx - s_cx;
        gfx_fill_rect(s_cx - 3, s_cy, 7, 1, DIM);
        gfx_fill_rect(s_cx, s_cy - 3, 1, 7, DIM);
        gfx_ring(s_lx, s_ly, 17, 13, GRN);
        const char *st = dx > 22 ? "RUN >" : dx > 8 ? "STEP >" : dx < -22 ? "< RUN" : dx < -8 ? "< STEP" : "STAND";
        ui_text_c(&KF_TEXT, s_lx, s_ly + 36, st, GRN);
    }
    if (s_ron) {
        gfx_ring(s_rx, s_ry, 17, 13, ORA);
        snprintf(b, sizeof b, "%d MS", (int)(s_now - s_rt0));
        ui_text_c(&KF_TEXT, s_rx, s_ry + 36, b, ORA);
    }
    if (s_last_t && s_now - s_last_t < 150) {
        for (int i = 0; i < s_last.n; i++) {
            int x = s_last.p[i].x, y = s_last.p[i].y, lx, ly;
            bool broken = x < 0 || x >= LCD_W || y < 0 || y >= LCD_H;
            gfx_touch_to_logical(x < 0        ? 0
                                 : x >= LCD_W ? LCD_W - 1
                                              : x,
                                 y < 0        ? 0
                                 : y >= LCD_H ? LCD_H - 1
                                              : y,
                                 &lx, &ly);
            gfx_fill_circle(lx, ly, 4, broken ? RED : INK);
            snprintf(b, sizeof b, "ID%d E%d", s_last.p[i].id, s_last.p[i].ev);
            ui_text_c(&KF_LABEL, lx + 34, ly + 4, b, broken ? RED : INK);
        }
    }
    // the last right touch (the newest) over the timeline
    if (s_hist_n) {
        const touch_t *t = &s_hist[s_hist_n - 1];
        snprintf(b, sizeof b, "LAST %d MS  B%d ONE%d NO%d BAD%d", t->ms, t->b, t->one, t->e, t->x);
        ui_text_c(&KF_TEXT, cx, H * 318 / 466, b, t->ms < (int)(TAP_S * 1000) ? INK : ORA);
    } else {
        ui_text_c(&KF_TEXT, cx, H * 318 / 466, "GREEN L  ORANGE R  GREY NONE  RED BAD", DIM);
    }
    // the timeline: as wide as the circle at its bottom row
    const int ty = H * 328 / 466, th = H * 44 / 466, half = th / 2, dyb = ty + th - H / 2, R = W / 2 - 10;
    int hw = 0;
    while ((hw + 1) * (hw + 1) + dyb * dyb <= R * R) {
        hw++;
    }
    int cw = 2 * hw / TL_N;
    if (cw < 1) {
        cw = 1;
    }
    const int tw = cw * TL_N, x0 = cx - tw / 2;
    gfx_fill_rect(x0 - 1, ty - 1, tw + 2, th + 2, rgb(24, 28, 50));
    for (int i = 0; i < s_tl_n; i++) {
        int k = s_tl[(s_tl_w - s_tl_n + i + TL_N) % TL_N];
        int x = x0 + (TL_N - s_tl_n + i) * cw;
        if (k == K_BROKEN) {
            gfx_fill_rect(x, ty, cw, th, RED);
        } else if (k == K_NONE) {
            gfx_fill_rect(x, ty + half - 1, cw, 2, GRY);
        } else {
            if (k == K_L || k == K_B) {
                gfx_fill_rect(x, ty, cw, half - 1, GRN);
            }
            if (k == K_R || k == K_B) {
                gfx_fill_rect(x, ty + half + 1, cw, th - half - 1, ORA);
            }
        }
    }
    // FREEZE and RESET
    int bx, by, bw, bh;
    reset_box(&bx, &by, &bw, &bh);
    ui_rrect(bx, by, bw, bh, 12, s_frozen ? rgb(30, 34, 54) : rgb(46, 52, 84));
    ui_text_c(&KF_NAME, bx + bw / 2, by + bh / 2 + 7, "RESET", s_frozen ? DIM : INK);
    freeze_box(&bx, &by, &bw, &bh);
    ui_rrect(bx, by, bw, bh, 12, s_frozen ? rgb(40, 90, 150) : rgb(46, 52, 84));
    ui_text_c(&KF_NAME, bx + bw / 2, by + bh / 2 + 7, s_frozen ? "FROZEN" : "FREEZE", INK);
}

static void lab_render(int64_t now)
{
    (void)now;
    if (gfx_round()) {
        lab_render_round();
        return;
    }
    const uint16_t BG = rgb(10, 14, 30), DIM = rgb(70, 80, 110), INK = rgb(235, 240, 255), GRN = rgb(80, 230, 120),
                   ORA = rgb(255, 160, 60), RED = rgb(255, 60, 60), GRY = rgb(110, 110, 120);
    bool bar = BOARD_BAR_SCREEN;
    int W = gfx_w, H = gfx_h, s1 = bar || TL_TAB5 ? 2 : 1;
    const int ts = TL_TAB5 ? 2 : 1;  // the scale of the small words
    gfx_fill(BG);
    char b[120];
    int bx, by, bw, bh;
    reset_box(&bx, &by, &bw, &bh);
    // the counts (the long words if they fit before RESET, else smaller, else short words)
    snprintf(b, sizeof b, "RIGHT %d  TAP %d  LONG %d  LEFT LOST %d  BROKEN %d", s_press, s_taps, s_long, s_left_lost,
             s_broken);
    int sc = s1, lim = bx - bw - 14;  // (the counts end before FREEZE: the long words, else the short ones)
    if (gfx_text_width(b, sc) > lim) {
        snprintf(b, sizeof b, "RIGHT %d TAP %d LONG %d LOST %d BAD %d", s_press, s_taps, s_long, s_left_lost, s_broken);
    }
    if (gfx_text_width(b, sc) > lim) {
        sc = 1;
    }
    gfx_text(4, 3, b, sc, INK);
    int two = s_two_b + s_two_one + s_two_e;
    if (two && bar) {
        snprintf(b, sizeof b,
                 "BOTH THUMBS DOWN - THE PANEL SAW: BOTH %d%%  ONE %d%%  NONE %d%%  %d/S  SHIFTED %d  READ %d",
                 s_two_b * 100 / two, s_two_one * 100 / two, s_two_e * 100 / two, s_rate, s_fixed,
                 board_touch_read_len_get());
    } else if (two) {
        snprintf(b, sizeof b, "2 THUMBS: BOTH %d%% ONE %d%% NONE %d%% %d/S", s_two_b * 100 / two, s_two_one * 100 / two,
                 s_two_e * 100 / two, s_rate);
    } else if (bar) {
        snprintf(b, sizeof b,
                 "HOLD THE LEFT THUMB, TAP WITH THE RIGHT ONE, COMPARE RIGHT WITH YOUR TAPS. %d/S  READ %d", s_rate,
                 board_touch_read_len_get());
    } else {
        snprintf(b, sizeof b, "HOLD LEFT, TAP RIGHT. REPORTS %d/S", s_rate);
    }
    if (s_frozen && s_saved_k > 0) {
        snprintf(b, sizeof b, "FROZEN: THE LAST 3 S ARE IN /data/touchlab/f%d.txt - TAP FROZEN TO GO ON", s_saved_k);
    } else if (s_frozen) {
        snprintf(b, sizeof b, "FROZEN (NOT SAVED) - TAP FROZEN TO GO ON");
    }
    int y2 = bar ? 24 : TL_TAB5 ? 42 : 22;  // (the 3.49: under FREEZE and RESET, not on their edge)
    int s2 = ts > 1 && gfx_text_width(b, ts) <= W - 8 ? ts : 1;
    gfx_text(4, y2, b, s2, s_frozen ? rgb(120, 200, 255) : DIM);
    // RESET
    int rs = bar || TL_TAB5 ? 2 : 1;
    gfx_frame_rect(bx, by, bw, bh, 1, s_frozen ? DIM : INK);
    gfx_text_center_in(bx, bw, by + (bh - 7 * rs) / 2, "RESET", rs, s_frozen ? DIM : INK);
    int fx, fy, fw, fh;
    freeze_box(&fx, &fy, &fw, &fh);
    if (s_frozen) {
        gfx_fill_rect(fx, fy, fw, fh, rgb(40, 90, 150));
    }
    gfx_frame_rect(fx, fy, fw, fh, 1, INK);
    gfx_text_center_in(fx, fw, fy + (fh - 7 * rs) / 2, s_frozen ? "FROZEN" : "FREEZE", rs, INK);
    // the area of the thumbs, the line between the halves
    int ya = y2 + 10 * ts, yb = H - (bar ? 44 : 70);
    for (int y = ya; y < yb; y += 6) {
        gfx_fill_rect(W / 2, y, 1, 3, DIM);
    }
    gfx_text(6, ya, "LEFT THUMB", ts, DIM);
    gfx_text(W - 6 - gfx_text_width("RIGHT THUMB", ts), ya, "RIGHT THUMB", ts, DIM);
    // the colours of the timeline (the 3.49: between the labels; a square screen: over the LAST line)
    const char *legend = "TIMELINE: GREEN = LEFT, ORANGE = RIGHT, GREY = NONE, RED = BROKEN";
    if (bar) {
        gfx_text_center(ya, legend, 1, DIM);
    } else {
        gfx_text(6, yb - 9 * ts, "TIMELINE: GREEN L, ORANGE R, GREY NONE, RED BAD", ts, DIM);
    }
    // the game view: the stick and the thumbs
    if (s_lon) {
        int dx = s_lx - s_cx;
        gfx_fill_rect(s_cx - 3, s_cy, 7, 1, DIM);
        gfx_fill_rect(s_cx, s_cy - 3, 1, 7, DIM);
        gfx_ring(s_lx, s_ly, 17, 13, GRN);
        const char *st = dx > 22 ? "RUN >" : dx > 8 ? "STEP >" : dx < -22 ? "< RUN" : dx < -8 ? "< STEP" : "STAND";
        gfx_text(s_lx - gfx_text_width(st, ts) / 2, s_ly + 20, st, ts, GRN);
    }
    if (s_ron) {
        gfx_ring(s_rx, s_ry, 17, 13, ORA);
        snprintf(b, sizeof b, "%d MS", (int)(s_now - s_rt0));
        gfx_text(s_rx - gfx_text_width(b, ts) / 2, s_ry + 20, b, ts, ORA);
    }
    // the panel: the points of the last report with points (for 150 ms), with their id and event
    if (s_last_t && s_now - s_last_t < 150) {
        for (int i = 0; i < s_last.n; i++) {
            int x = s_last.p[i].x, y = s_last.p[i].y, lx, ly;
            bool broken = x < 0 || x >= LCD_W || y < 0 || y >= LCD_H;
            gfx_touch_to_logical(x < 0        ? 0
                                 : x >= LCD_W ? LCD_W - 1
                                              : x,
                                 y < 0        ? 0
                                 : y >= LCD_H ? LCD_H - 1
                                              : y,
                                 &lx, &ly);
            gfx_fill_circle(lx, ly, 4, broken ? RED : INK);
            snprintf(b, sizeof b, "ID%d E%d", s_last.p[i].id, s_last.p[i].ev);
            gfx_text(lx + 8, ly - 4, b, 1, broken ? RED : INK);
        }
    }
    // the last right touches: the time and the reports in each (B both, 1 one point, - none, X broken)
    int yh = bar ? yb - 9 : yb - 20 * ts;
    int x = 6;
    gfx_text(x, yh, "LAST:", ts, DIM);
    x += gfx_text_width("LAST: ", ts);
    for (int i = 0; i < s_hist_n; i++) {
        const touch_t *t = &s_hist[i];
        snprintf(b, sizeof b, "%d MS (B%d ONE%d NO%d BAD%d)", t->ms, t->b, t->one, t->e, t->x);
        uint16_t c = t->ms < (int)(TAP_S * 1000) ? INK : ORA;
        if (x + gfx_text_width(b, ts) > W - 4) {
            break;
        }
        gfx_text(x, yh, b, ts, c);
        x += gfx_text_width(b, ts) + 12;
    }
    // the timeline of the reports: the newest at the right
    int cw = (W - 12) / TL_N;
    if (cw < 1) {
        cw = 1;
    }
    int tw = cw * TL_N, x0 = (W - tw) / 2, ty = yb + 2, th = H - ty - 2, half = th / 2;
    gfx_fill_rect(x0 - 1, ty - 1, tw + 2, th + 2, rgb(24, 28, 50));
    for (int i = 0; i < s_tl_n; i++) {
        int k = s_tl[(s_tl_w - s_tl_n + i + TL_N) % TL_N];
        int cx = x0 + (TL_N - s_tl_n + i) * cw;
        if (k == K_BROKEN) {
            gfx_fill_rect(cx, ty, cw, th, RED);
        } else if (k == K_NONE) {
            gfx_fill_rect(cx, ty + half - 1, cw, 2, GRY);
        } else {
            if (k == K_L || k == K_B) {
                gfx_fill_rect(cx, ty, cw, half - 1, GRN);
            }
            if (k == K_R || k == K_B) {
                gfx_fill_rect(cx, ty + half + 1, cw, th - half - 1, ORA);
            }
        }
    }
}

static bool lab_awake(void)
{
    return true;
}

#include "tficon/tficon.h"
// the menu icon: a touch panel with its grid: two fingers touch it one after the other; rings and cross hairs show
// each point
static void lab_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x4be3d5), tfi_hex(0x00796b));
    float u = s / 100.0f, ph = tfi_ph(2);
    int gx = x + (int)(14 * u), gy = y + (int)(18 * u), gw = (int)(72 * u), gh = (int)(64 * u);
    tfi_drop(gx, gy, gw, gh, (int)(7 * u), (int)(3 * u), 90);
    tfi_rbox(gx, gy, gw, gh, (int)(7 * u), tfi_hex(0x123040), tfi_hex(0x081820), 256);
    for (int k = 1; k < 6; k++) {
        tfd_rect(gx + k * gw / 6, gy + (int)(3 * u), 1, gh - (int)(6 * u), tfi_hex(0x2a6070), 256);
    }
    for (int k = 1; k < 5; k++) {
        tfd_rect(gx + (int)(3 * u), gy + k * gh / 5, gw - (int)(6 * u), 1, tfi_hex(0x2a6070), 256);
    }
    static const float TP[2][2] = {{36, 40}, {64, 62}};
    static const uint32_t TC[2] = {0x6dff8a, 0xffa53d};
    for (int k = 0; k < 2; k++) {
        float q = ph * 2 - k * 0.5f;
        q -= (float)(int)(q + 1) - 1;
        q -= (float)(int)q;
        float on = q < 0.6f ? 1 - tfi_step(0.45f, 0.6f, q) : 0;
        float tx = x + TP[k][0] * u, ty = y + TP[k][1] * u;
        uint16_t c = tfi_hex(TC[k]);
        if (on <= 0) {
            continue;
        }
        tfi_ring(tx, ty, (6 + q * 20) * u, 2.4f * u, c, tfi_qa((int)(256 * (1 - q / 0.6f)), 3));
        int qa = tfi_qa((int)(256 * on), 3);
        tfd_rect((int)(tx - 12 * u), (int)(ty - 0.8f * u), (int)(24 * u), (int)(1.6f * u) > 1 ? (int)(1.6f * u) : 1, c,
                 qa);
        tfd_rect((int)(tx - 0.8f * u), (int)(ty - 12 * u), (int)(1.6f * u) > 1 ? (int)(1.6f * u) : 1, (int)(24 * u), c,
                 qa);
        tfi_disc(tx, ty, 7 * u, c, c, qa / 2);
        tfi_disc(tx, ty, 4 * u, rgb(255, 255, 255), rgb(255, 255, 255), qa);
    }
    tfi_end();
}

const app_t app_touchlab = {
        .name = "TOUCH LAB",
        .landscape = true,
        .enter = lab_enter,
        .leave = lab_leave,
        .input = lab_input,
        .update = lab_update,
        .render = lab_render,
        .draw_icon = lab_icon,
        .awake = lab_awake,
        .round = APP_ROUND_HI,  // (the round boards: lab_render_round)
};
