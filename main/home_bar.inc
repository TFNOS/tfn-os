// THE HOME BAR OF THE TAB5: the Tab5 has only 1 button, and a short press resets the device, so the bar is the way
// out of an app. Included in main.c only for BOARD_UI_TABLET. The other way out has two steps: a pull down
// from the top middle (the settings panel), then MENU. The bar works as on a phone:
//  - A thin bar shows faintly at the middle of the bottom edge of each app (brighter for 3 s after an app opens).
//  - A swipe UP that starts at the very bottom edge (the last HB_EDGE px, about 4 mm) goes back to the menu. While the
//    finger goes up, the picture of the app becomes a card that follows the finger and gets smaller; the bar lights
//    up (gold) when the finger is far enough (HB_GO px, about 12 mm). Let go there (or flick up): the card shrinks away
//    into the menu. Let go lower: the card comes back and the app goes on.
//  - The app keeps every touch that does not start at that edge, and a touch at the edge that does not go up at once:
//    its events are not held or late (a game that uses the bottom of the screen is not disturbed). Only a touch that
//    starts at the edge and goes up HB_TAKE px within HB_WIN ms (more up than to the side) becomes the bar's: then the
//    app gets an IN_UP at its last point (no tap) and stops under the card, as under the settings panel.
//  - Not in the menu (it has BACK and the tabs), not in the self-test, not while the settings panel, a message or the
//    notification center shows, not in TOUCH LAB (it must see each finger) and not in a TENNIS match of 2 players on one
//    board (the bottom edge is the side of a player).
// main.c: hb_event() gets each touch of the app first; hb_card() = the app stops and the card shows; hb_draw() draws over
// the frame after the app; hb_switched() at each app change.

#include "uikit.h"

#define HB_EDGE  22   // px: the start zone at the bottom edge (3.8 mm on the Tab5)
#define HB_TAKE  24   // px up: the bar takes the finger
#define HB_WIN   320  // ms: ... within this time after the press
#define HB_GO    70   // px up at the lift: the menu (12 mm)
#define HB_FLICK 40   // px up with a fast lift (HB_VFAST px/ms or more): the menu too
#define HB_VFAST 0.5f
#define HB_ANIM  200   // ms: the card goes away or comes back
#define HB_SHOW  3000  // ms: the bar is brighter after an app opens

enum {
    HB_IDLE,
    HB_WATCH,
    HB_OWN,
    HB_GO_ANIM,
    HB_BACK_ANIM
};
static struct {
    int st;
    int x0, y0, x, y;  // the press and the finger now (logical px)
    int64_t t0;
    int64_t pt[8];  // the last points (the speed at the lift)
    int16_t py[8];
    int pn;
    int64_t a_t;         // the start of an animation
    float a_up, a_side;  // the place of the finger at the lift
    bool armed;          // the finger is far enough: the bar is gold
    uint16_t *snap;      // the picture of the app (PSRAM, gfx_w x gfx_h)
    bool snap_need;
    int64_t open_t;  // the time an app opened
} s_hb;

static bool hb_allow(void)
{
#if CONFIG_TFN_SELFTEST
    return false;
#else
    if (!mai_app || mai_app == &app_menu || mai_saver != SAVER_ON) {
        return false;
    }
    if (sysui_panel_on() || sysui_active() || notify_center_on()) {
        return false;
    }
    if (mai_app_is("TOUCH LAB")) {
        return false;
    }
#if ARCADE_APP_ON(TENNIS)
    if (mai_app == &app_tennis && tennis_two_players()) {
        return false;
    }
#endif
    return true;
#endif
}
static void hb_free(void)
{
    free(s_hb.snap);
    s_hb.snap = NULL;
    s_hb.snap_need = false;
}
// the card of the app shows (the app stops under it unless it runs: a radio match)
static bool hb_card(void)
{
    return (s_hb.st == HB_OWN || s_hb.st == HB_BACK_ANIM) && s_hb.snap;
}
static bool hb_busy(void)
{
    return s_hb.st >= HB_OWN;
}
static void hb_point(int y, int64_t t)
{
    s_hb.py[s_hb.pn % 8] = (int16_t)y;
    s_hb.pt[s_hb.pn % 8] = t;
    s_hb.pn++;
}
// the speed up of the finger at the lift (px/ms): the points of the last 100 ms
static float hb_speed(int64_t t)
{
    int n = s_hb.pn < 8 ? s_hb.pn : 8, best = -1;
    for (int k = 0; k < n; k++) {
        int i = (s_hb.pn - 1 - k) % 8;
        if (t - s_hb.pt[i] > 100) {
            break;
        }
        best = i;
    }
    if (best < 0) {
        return 0;
    }
    int last = (s_hb.pn - 1) % 8;
    int64_t dt = s_hb.pt[last] - s_hb.pt[best];
    return dt >= 8 ? (float)(s_hb.py[best] - s_hb.py[last]) / (float)dt : 0;
}
// an app opens: the bar is brighter for a while; a change that is not the home bar's own ends its work
static void hb_switched(const app_t *app, int64_t t)
{
    s_hb.open_t = t;
    if (s_hb.st == HB_GO_ANIM && app == &app_menu) {
        return;
    }
    s_hb.st = HB_IDLE;
    hb_free();
}
// The bar takes the finger: the app gets an IN_UP at its last point (no tap), the rest of the touch is the bar's.
static void hb_take(const input_event_t *ev)
{
    if (s_own == OWN_APP && s_own_down) {
        input_event_t up = {.kind = IN_UP, .x = s_own_x, .y = s_own_y, .t_ms = ev->t_ms - s_frozen_ms};
        mai_app->input(&up);
    }
    s_own = OWN_DROP;
    s_own_down = false;
    s_hb.st = HB_OWN;
    s_hb.armed = false;
    hb_free();
    s_hb.snap = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);
    s_hb.snap_need = s_hb.snap != NULL;
    sfx_ui(SFX_TICK);
}
// each touch of the app first (logical points, the clock of main.c): true = the home bar took the event
static bool hb_event(const input_event_t *ev)
{
    switch (s_hb.st) {
    case HB_IDLE:
        if (ev->kind == IN_DOWN && ev->y >= gfx_h - HB_EDGE && hb_allow()) {
            s_hb.st = HB_WATCH;
            s_hb.x0 = s_hb.x = ev->x;
            s_hb.y0 = s_hb.y = ev->y;
            s_hb.t0 = ev->t_ms;
            s_hb.pn = 0;
            hb_point(ev->y, ev->t_ms);
        }
        return false;
    case HB_WATCH: {
        if (ev->kind == IN_MOVE) {
            int up = s_hb.y0 - ev->y, side = abs(ev->x - s_hb.x0);
            s_hb.x = ev->x;
            s_hb.y = ev->y;
            hb_point(ev->y, ev->t_ms);
            if (up >= HB_TAKE && 2 * up >= 3 * side && ev->t_ms - s_hb.t0 <= HB_WIN && hb_allow()) {
                hb_take(ev);
                return true;
            }
            if (side > HB_TAKE || up < -8 || ev->t_ms - s_hb.t0 > HB_WIN) {
                s_hb.st = HB_IDLE;
            }
        } else if (ev->kind != IN_DOWN) {
            s_hb.st = HB_IDLE;  // (a tap or a hold at the edge: the app's)
        }
        return false;
    }
    case HB_OWN:
        if (ev->kind == IN_MOVE) {
            s_hb.x = ev->x;
            s_hb.y = ev->y;
            hb_point(ev->y, ev->t_ms);
            bool armed = s_hb.y0 - s_hb.y >= HB_GO;
            if (armed && !s_hb.armed) {
                sfx_ui(SFX_TICK);
            }
            s_hb.armed = armed;
            return true;
        }
        if (ev->kind == IN_UP) {
            float up = (float)(s_hb.y0 - ev->y), v = hb_speed(ev->t_ms);
            s_hb.a_up = up < 0 ? 0 : up;
            s_hb.a_side = (float)(ev->x - s_hb.x0);
            s_hb.a_t = tfn_now_ms();
            if (up >= HB_GO || (up >= HB_FLICK && v >= HB_VFAST)) {
                s_hb.st = HB_GO_ANIM;
                sfx_ui(SFX_BACK);
                app_open(&app_menu);
            } else {
                s_hb.st = HB_BACK_ANIM;
            }
            return true;
        }
        return ev->kind != IN_DOWN;  // (the IN_TAP or IN_LONG of the bar's touch)
    default:
        return false;  // (an animation: a new touch is the app's or the menu's)
    }
}

// ---------------------------------------------------------------- drawing
// the part of the pixel (px, py) inside a rounded box 0..w x 0..h of radius r (0..255)
static int hb_cover(int px, int py, int w, int h, int r)
{
    float fx = px + 0.5f, fy = py + 0.5f, cx, cy;
    if (fx < r) {
        cx = (float)r;
    } else if (fx > w - r) {
        cx = (float)(w - r);
    } else {
        return 255;
    }
    if (fy < r) {
        cy = (float)r;
    } else if (fy > h - r) {
        cy = (float)(h - r);
    } else {
        return 255;
    }
    float d = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)), c = r - d + 0.5f;
    return c >= 1 ? 255 : c <= 0 ? 0 : (int)(c * 255);
}
// the room behind the card: a dark blue gradient, lighter at the bottom where the finger is (a dither of the rows: no
// bands of the 16-bit colours)
static void hb_room(void)
{
    for (int y = 0; y < gfx_h; y++) {
        int k = y * 255 / (gfx_h - 1), d = (y & 3) * 2 - 3;
        int r = 10 + k * 20 / 255 + d, g = 12 + k * 22 / 255 + d / 2, b = 30 + k * 46 / 255 + d;
        gfx_fill_rect(0, y, gfx_w, 1,
                      rgb((uint8_t)(r < 0 ? 0 : r), (uint8_t)(g < 0 ? 0 : g), (uint8_t)(b < 0 ? 0 : b)));
    }
}
// the picture of the app at the scale s, its middle at (cx, cy), round corners, alpha a (0..255)
static void hb_card_draw(float s, float cx, float cy, int a)
{
    const int W = gfx_w, H = gfx_h, cw = (int)(W * s), ch = (int)(H * s);
    if (cw < 4 || ch < 4 || !s_hb.snap) {
        return;
    }
    const int x0 = (int)lroundf(cx - cw / 2.0f), y0 = (int)lroundf(cy - ch / 2.0f), r = (int)(26 * s) + 4;
    ui_rrect_a(x0 + 4, y0 + 10, cw, ch, r, rgb(0, 0, 0), a * 110 / 255);  // (its shadow)
    const uint32_t sxs = (uint32_t)(((int64_t)W << 16) / cw), sys = (uint32_t)(((int64_t)H << 16) / ch);
    int i0 = x0 < 0 ? -x0 : 0, i1 = x0 + cw > W ? W - x0 : cw;
    for (int j = 0; j < ch; j++) {
        int y = y0 + j;
        if ((unsigned)y >= (unsigned)H) {
            continue;
        }
        const uint16_t *srow = &s_hb.snap[(int)((j * sys) >> 16) * W];
        uint16_t *drow = &g_fb[y * W + x0];
        bool edge = j < r || j >= ch - r;
        uint32_t sx = (uint32_t)i0 * sxs;
        for (int i = i0; i < i1; i++, sx += sxs) {
            uint16_t c = srow[sx >> 16];
            int cov = edge && (i < r || i >= cw - r) ? hb_cover(i, j, cw, ch, r) : 255;
            cov = cov * a / 255;
            if (cov <= 0) {
                continue;
            }
            drow[i] = cov >= 255 ? c : ui_mix(drow[i], c, cov);
        }
    }
}
// the bar: a pill of width w at the middle x cx, its top y; strength 0..255; gold when armed
static void hb_pill(int cx, int y, int w, int a, bool gold)
{
    ui_rrect_a(cx - w / 2 - 2, y - 2, w + 4, 9, 4, rgb(0, 0, 0), a / 2);
    ui_rrect_a(cx - w / 2, y, w, 5, 3, gold ? rgb(255, 210, 70) : rgb(255, 255, 255), a);
}
// after the app (each frame): the faint bar, or the card while the bar has the finger, or the card that goes away
static void hb_draw(int64_t t, bool live)
{
    const int bw = gfx_w > gfx_h ? 150 : 120;
    if (s_hb.st == HB_IDLE || s_hb.st == HB_WATCH) {
        if (!hb_allow()) {
            return;
        }
        int a = 70, lift = 0;
        if (t - s_hb.open_t < HB_SHOW) {
            a = 70 + (int)(110 * (1.0f - (float)(t - s_hb.open_t) / HB_SHOW));
        }
        if (s_hb.st == HB_WATCH && s_hb.y0 > s_hb.y) {
            lift = (s_hb.y0 - s_hb.y) / 2;
            a = 200;
        }
        hb_pill(gfx_w / 2, gfx_h - 9 - lift, bw, a, false);
        return;
    }
    float up, side, k = 1;
    if (s_hb.st == HB_OWN) {
        up = (float)(s_hb.y0 - s_hb.y);
        side = (float)(s_hb.x - s_hb.x0);
        if (up < 0) {
            up = 0;
        }
    } else {
        k = (float)(t - s_hb.a_t) / HB_ANIM;
        if (k >= 1) {
            s_hb.st = HB_IDLE;
            hb_free();
            return;
        }
        up = s_hb.a_up;
        side = s_hb.a_side;
    }
    if (s_hb.snap && (s_hb.snap_need || live) && s_hb.st != HB_GO_ANIM) {
        memcpy(s_hb.snap, g_fb, (size_t)gfx_w * gfx_h * 2);  // (the picture of the app: once, or each frame if it runs)
        s_hb.snap_need = false;
    }
    // the card: smaller as the finger goes up (half size at half of the height), it follows the finger
    float p = up / (gfx_h * 0.5f);
    if (p > 1) {
        p = 1;
    }
    float e = 1 - (1 - p) * (1 - p), s = 1 - 0.5f * e, cx = gfx_w / 2.0f + side * 0.5f, cy = gfx_h / 2.0f - up * 0.55f;
    int a = 255;
    if (s_hb.st == HB_BACK_ANIM) {  // back to the full frame
        float q = 1 - (1 - k) * (1 - k);
        s += (1 - s) * q;
        cx += (gfx_w / 2.0f - cx) * q;
        cy += (gfx_h / 2.0f - cy) * q;
    } else if (s_hb.st == HB_GO_ANIM) {  // away: smaller, up, it fades over the menu
        float q = k * k;
        s *= 1 - 0.75f * q;
        cy -= gfx_h * 0.12f * q;
        a = (int)(255 * (1 - q));
    }
    if (s_hb.st != HB_GO_ANIM) {
        if (!s_hb.snap) {
            return;  // (no memory for the picture: the app draws under the finger)
        }
        hb_room();
    }
    hb_card_draw(s, cx, cy, a);
    if (s_hb.st == HB_OWN) {
        int by = (int)(cy + gfx_h * s / 2) + 12;
        if (by > gfx_h - 9) {
            by = gfx_h - 9;
        }
        hb_pill((int)cx, by, s_hb.armed ? bw + 30 : bw, 230, s_hb.armed);
    }
}
